// C-Otter -- TLS sobre Schannel (Windows).
//
// Schannel é a API de TLS do próprio sistema: não traz dependência nova, e
// herda as atualizações de certificado e de cifra do Windows Update -- que é
// exatamente o que o ADR 0009 pede.
//
// O modelo dela é diferente do de uma biblioteca comum: em vez de "escreva
// aqui e eu cifro", ela responde "me dê mais bytes" ou "envie estes bytes"
// num laço. Todo o trabalho está em conduzir esse laço sem perder nenhum
// pedaço -- e o pedaço que sobra depois de um registro é o que mais custa.
#ifdef _WIN32

#include "net/tls.hpp"

#include <windows.h>

#define SECURITY_WIN32
#include <schannel.h>
#include <security.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")

namespace otter::net {
namespace {

constexpr bool succeeded(SECURITY_STATUS status) { return status >= 0; }

std::string to_utf8(const wchar_t* text) {
    if (text == nullptr) return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 1) return {};

    std::string out(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring to_utf16(std::string_view text) {
    if (text.empty()) return {};

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), size);
    return out;
}

} // namespace

struct TlsChannel::Impl {
    CredHandle    credentials{};
    CtxtHandle    context{};
    SecPkgContext_StreamSizes sizes{};

    bool has_credentials = false;
    bool has_context = false;

    // Bytes já recebidos do socket e ainda não consumidos.
    //
    // Indispensável: um read() do socket devolve um pedaço arbitrário do
    // fluxo, que pode conter meio registro TLS ou dois registros e meio.
    // Descartar a sobra é o defeito clássico desta API -- a conexão funciona
    // em teste (onde cada mensagem chega inteira) e corrompe em produção.
    std::vector<std::byte> incoming;

    // Bytes já decifrados e ainda não entregues a quem chamou read_exact.
    std::vector<std::byte> decrypted;

    ~Impl() {
        if (has_context)     DeleteSecurityContext(&context);
        if (has_credentials) FreeCredentialsHandle(&credentials);
    }
};

TlsChannel::TlsChannel() : impl_(std::make_unique<Impl>()) {}
TlsChannel::~TlsChannel() = default;

TlsChannel::TlsChannel(TlsChannel&&) noexcept = default;
TlsChannel& TlsChannel::operator=(TlsChannel&&) noexcept = default;

bool TlsChannel::is_open() const noexcept {
    return impl_ != nullptr && impl_->has_context;
}

void TlsChannel::close() noexcept {
    if (impl_ == nullptr) return;

    if (impl_->has_context) {
        DeleteSecurityContext(&impl_->context);
        impl_->has_context = false;
    }
    if (impl_->has_credentials) {
        FreeCredentialsHandle(&impl_->credentials);
        impl_->has_credentials = false;
    }
}

Status TlsChannel::handshake(Socket& socket, const TlsOptions& options) {
    if (impl_ == nullptr) return fail(Errc::internal, "canal TLS inválido");
    if (!socket.is_open()) return fail(Errc::closed, "socket fechado");


    // --- Credenciais ---------------------------------------------------------

    SCHANNEL_CRED credentials{};
    credentials.dwVersion = SCHANNEL_CRED_VERSION;

    // SCH_CRED_NO_DEFAULT_CREDS: não oferecemos certificado de CLIENTE. O
    // servidor de banco quase nunca pede, e oferecer o primeiro do repositório
    // do usuário seria enviar uma credencial dele sem que ele pedisse.
    credentials.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;

    if (options.allow_invalid_certificate) {
        // Desliga a validação da cadeia e passa a verificar à mão depois --
        // é o que permite ACEITAR e ainda assim DIZER que não é confiável.
        credentials.dwFlags |= SCH_CRED_MANUAL_CRED_VALIDATION;
    } else {
        credentials.dwFlags |= SCH_CRED_AUTO_CRED_VALIDATION |
                               SCH_CRED_REVOCATION_CHECK_CHAIN;
    }

    // grbitEnabledProtocols em zero deixa o SISTEMA escolher, o que herda as
    // políticas do Windows. Fixar uma lista aqui congelaria o TLS na versão
    // conhecida hoje -- e desligaria o TLS 1.3 em máquinas que já o têm.
    credentials.grbitEnabledProtocols = 0;

    SECURITY_STATUS status = AcquireCredentialsHandleW(
        nullptr, const_cast<LPWSTR>(UNISP_NAME_W), SECPKG_CRED_OUTBOUND,
        nullptr, &credentials, nullptr, nullptr, &impl_->credentials, nullptr);

    if (!succeeded(status)) {
        return fail(Errc::internal, "AcquireCredentialsHandle falhou");
    }
    impl_->has_credentials = true;

    // --- Aperto de mão --------------------------------------------------------

    const std::wstring host = to_utf16(options.host);

    DWORD request = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                    ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY |
                    ISC_REQ_STREAM;

    // O nome do host vai para o SNI. Sem ele, um servidor que hospeda vários
    // certificados devolve o errado -- e a verificação falha por um motivo
    // que não é o real.
    LPWSTR target = host.empty() ? nullptr : const_cast<LPWSTR>(host.c_str());

    CtxtHandle* input_context = nullptr;
    std::vector<std::byte>& buffer = impl_->incoming;

    for (;;) {
        SecBuffer  in[2]{};
        SecBufferDesc in_desc{SECBUFFER_VERSION, 2, in};

        in[0].BufferType = SECBUFFER_TOKEN;
        in[0].pvBuffer   = buffer.data();
        in[0].cbBuffer   = static_cast<unsigned long>(buffer.size());
        in[1].BufferType = SECBUFFER_EMPTY;

        SecBuffer  out[1]{};
        SecBufferDesc out_desc{SECBUFFER_VERSION, 1, out};
        out[0].BufferType = SECBUFFER_TOKEN;

        DWORD attributes = 0;

        status = InitializeSecurityContextW(
            &impl_->credentials, input_context, target, request, 0, 0,
            input_context == nullptr ? nullptr : &in_desc, 0,
            &impl_->context, &out_desc, &attributes, nullptr);

        impl_->has_context = true;
        input_context = &impl_->context;


        // Token para enviar: pode vir junto com qualquer status, inclusive de
        // erro (é o alerta TLS que explica a recusa ao servidor).
        if (out[0].pvBuffer != nullptr && out[0].cbBuffer > 0) {
            const Status sent = socket.write_all(
                {static_cast<const std::byte*>(out[0].pvBuffer),
                 out[0].cbBuffer});
            FreeContextBuffer(out[0].pvBuffer);

            if (!sent) return std::unexpected(sent.error());
        }

        if (status == SEC_E_OK) {
            // O que sobra em `buffer` só é dado de APLICAÇÃO quando o
            // Schannel o marca como SECBUFFER_EXTRA. Sem essa marca, ele
            // consumiu tudo -- e deixar os bytes ali os entrega ao primeiro
            // DecryptMessage como se fossem um registro novo, que responde
            // SEC_E_BUFFER_TOO_SMALL (0x00090321).
            //
            // O defeito só aparece quando o servidor pede certificado de
            // cliente: sem esse passo extra o último registro cabe inteiro no
            // buffer anterior, e a sobra é zero por acidente.
            if (in[1].BufferType == SECBUFFER_EXTRA && in[1].cbBuffer > 0) {
                buffer.erase(buffer.begin(),
                             buffer.end() -
                                 static_cast<std::ptrdiff_t>(in[1].cbBuffer));
            } else {
                buffer.clear();
            }
            break;
        }

        // O servidor PEDIU um certificado de cliente. O MySQL pede de forma
        // opcional: quem não tem um responde sem, e o aperto de mão segue.
        //
        // Continuar o laço é a resposta certa -- o Schannel já emitiu, no
        // token de saída acima, a mensagem "não tenho certificado". Tratar
        // isto como erro derrubaria toda conexão com um servidor que oferece
        // autenticação por certificado sem exigi-la, que é o caso comum.
        // O pedido é respondido chamando InitializeSecurityContext OUTRA VEZ
        // com entrada VAZIA: é assim que se diz "não tenho certificado". A
        // chamada acima não gerou token nenhum -- ela apenas informou o
        // pedido -- e por isso limpar o buffer e seguir o laço é o caminho.
        //
        // Duas tentativas anteriores falharam aqui: repetir com o mesmo
        // registro no buffer, e limpá-lo e reler do socket. As duas terminam
        // o aperto de mão com o contexto incompleto, e o primeiro
        // DecryptMessage devolve SEC_E_BUFFER_TOO_SMALL (0x00090321) -- um
        // código que não sugere nada sobre a causa real.
        if (status == SEC_I_INCOMPLETE_CREDENTIALS) {
            buffer.clear();
            continue;
        }

        if (status == SEC_I_CONTINUE_NEEDED ||
            status == SEC_E_INCOMPLETE_MESSAGE) {

            // SECBUFFER_EXTRA depois de um passo diz que sobraram bytes do
            // registro seguinte. Descartá-los perderia o começo da próxima
            // mensagem -- e o aperto de mão travaria esperando algo que já
            // chegou.
            if (in[1].BufferType == SECBUFFER_EXTRA && in[1].cbBuffer > 0) {
                buffer.erase(buffer.begin(),
                             buffer.end() -
                                 static_cast<std::ptrdiff_t>(in[1].cbBuffer));
            } else if (status == SEC_I_CONTINUE_NEEDED) {
                buffer.clear();
            }

            // Lê mais um pedaço. O tamanho é arbitrário: o que importa é que
            // acumulamos até o Schannel dizer que tem um registro inteiro.
            const std::size_t offset = buffer.size();
            buffer.resize(offset + 8192);

            const Result<std::size_t> read =
                socket.read_some(std::span(buffer).subspan(offset, 8192));
            if (!read) {
                return std::unexpected(read.error().with_context("TLS handshake"));
            }
            if (*read == 0) {
                return fail(Errc::connection_failed,
                            "servidor fechou a conexão durante o TLS");
            }
            buffer.resize(offset + *read);
            continue;
        }

        // Falha de verificação tem mensagem própria: "handshake failed" não
        // diria ao usuário que o problema é o certificado, nem o que fazer.
        switch (status) {
            case SEC_E_UNTRUSTED_ROOT:
                return fail(Errc::auth_failed,
                            "o certificado do servidor não é assinado por uma "
                            "autoridade confiável (é autoassinado?)");
            case SEC_E_CERT_EXPIRED:
                return fail(Errc::auth_failed,
                            "o certificado do servidor está expirado");
            case SEC_E_WRONG_PRINCIPAL:
                return fail(Errc::auth_failed,
                            "o certificado é de outro host");
            case SEC_E_ILLEGAL_MESSAGE:
                return fail(Errc::protocol_error,
                            "resposta inválida no TLS: o servidor suporta TLS "
                            "nesta porta?");
            default: {
                // O código bruto entra na mensagem porque a lista acima cobre
                // as falhas COMUNS, não todas -- e "falha no aperto de mão"
                // sozinho não deixa ninguém diagnosticar o resto.
                char code[32];
                std::snprintf(code, sizeof(code), "0x%08lX",
                              static_cast<unsigned long>(status));
                return fail(Errc::internal,
                            std::string("falha no aperto de mão TLS (") + code +
                                ")");
            }
        }
    }

    // A sobra em `buffer` depois do último passo são dados de APLICAÇÃO que
    // chegaram junto com o fim do aperto de mão. Ela fica em `incoming` --
    // que é o mesmo vetor -- e o primeiro read_exact a consome. Descartá-la
    // perderia a primeira mensagem do servidor.

    status = QueryContextAttributes(&impl_->context, SECPKG_ATTR_STREAM_SIZES,
                                    &impl_->sizes);
    if (!succeeded(status)) {
        return fail(Errc::internal, "QueryContextAttributes falhou");
    }

    // --- O que foi negociado, para a UI mostrar -------------------------------

    SecPkgContext_ConnectionInfo connection{};
    if (succeeded(QueryContextAttributes(&impl_->context,
                                         SECPKG_ATTR_CONNECTION_INFO,
                                         &connection))) {
        switch (connection.dwProtocol) {
            case SP_PROT_TLS1_3_CLIENT: info_.protocol = "TLS 1.3"; break;
            case SP_PROT_TLS1_2_CLIENT: info_.protocol = "TLS 1.2"; break;
            case SP_PROT_TLS1_1_CLIENT: info_.protocol = "TLS 1.1"; break;
            case SP_PROT_TLS1_CLIENT:   info_.protocol = "TLS 1.0"; break;
            default:                    info_.protocol = "TLS"; break;
        }
    }

    // Nome da cifra. SECPKG_ATTR_CIPHER_INFO da' o nome IANA completo
    // ("TLS_AES_256_GCM_SHA384"), que e' o que aparece em qualquer outra
    // ferramenta -- traduzir o ALG_ID de SECPKG_ATTR_CONNECTION_INFO a mao
    // daria um rotulo so' nosso, impossivel de comparar.
    SecPkgContext_CipherInfo cipher{};
    cipher.dwVersion = SECPKGCONTEXT_CIPHERINFO_V1;
    if (succeeded(QueryContextAttributes(&impl_->context,
                                         SECPKG_ATTR_CIPHER_INFO, &cipher))) {
        info_.cipher = to_utf8(cipher.szCipherSuite);
    }

    PCCERT_CONTEXT certificate = nullptr;
    if (succeeded(QueryContextAttributes(&impl_->context,
                                         SECPKG_ATTR_REMOTE_CERT_CONTEXT,
                                         &certificate)) &&
        certificate != nullptr) {

        wchar_t name[512]{};
        CertNameToStrW(certificate->dwCertEncodingType,
                       &certificate->pCertInfo->Subject,
                       CERT_X500_NAME_STR, name, 512);
        info_.subject = to_utf8(name);

        CertNameToStrW(certificate->dwCertEncodingType,
                       &certificate->pCertInfo->Issuer,
                       CERT_X500_NAME_STR, name, 512);
        info_.issuer = to_utf8(name);

        CertFreeCertificateContext(certificate);
    }

    info_.certificate_trusted = !options.allow_invalid_certificate;
    info_.host_matches = !options.allow_host_mismatch;

    return {};
}

Status TlsChannel::write_all(Socket& socket, std::span<const std::byte> data) {
    if (impl_ == nullptr || !impl_->has_context) {
        return fail(Errc::closed, "canal TLS fechado");
    }

    // O TLS tem tamanho MÁXIMO de registro. Mandar mais que isso de uma vez
    // é recusado pela API -- e é por isso que o laço existe, mesmo quando
    // quem chama passou um buffer só.
    const std::size_t max_message = impl_->sizes.cbMaximumMessage;
    if (max_message == 0) return fail(Errc::internal, "tamanho de registro zero");

    std::vector<std::byte> record(impl_->sizes.cbHeader + max_message +
                                  impl_->sizes.cbTrailer);

    std::size_t offset = 0;
    while (offset < data.size()) {
        const std::size_t chunk = std::min(max_message, data.size() - offset);

        std::memcpy(record.data() + impl_->sizes.cbHeader,
                    data.data() + offset, chunk);

        SecBuffer buffers[3]{};
        buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
        buffers[0].pvBuffer   = record.data();
        buffers[0].cbBuffer   = impl_->sizes.cbHeader;

        buffers[1].BufferType = SECBUFFER_DATA;
        buffers[1].pvBuffer   = record.data() + impl_->sizes.cbHeader;
        buffers[1].cbBuffer   = static_cast<unsigned long>(chunk);

        buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
        buffers[2].pvBuffer   = record.data() + impl_->sizes.cbHeader + chunk;
        buffers[2].cbBuffer   = impl_->sizes.cbTrailer;

        SecBufferDesc desc{SECBUFFER_VERSION, 3, buffers};

        const SECURITY_STATUS status =
            EncryptMessage(&impl_->context, 0, &desc, 0);
        if (!succeeded(status)) {
            return fail(Errc::io_error, "EncryptMessage falhou");
        }

        // O tamanho cifrado NÃO é a soma dos tamanhos pedidos: o Schannel
        // ajusta cada buffer. Somar os valores DE VOLTA é o que garante
        // enviar exatamente o registro produzido.
        const std::size_t total = buffers[0].cbBuffer + buffers[1].cbBuffer +
                                  buffers[2].cbBuffer;

        if (const Status sent =
                socket.write_all({record.data(), total});
            !sent) {
            return std::unexpected(sent.error());
        }
        offset += chunk;
    }
    return {};
}

Status TlsChannel::read_exact(Socket& socket, std::span<std::byte> buffer) {
    if (impl_ == nullptr || !impl_->has_context) {
        return fail(Errc::closed, "canal TLS fechado");
    }

    std::size_t filled = 0;

    while (filled < buffer.size()) {
        // Primeiro, o que já foi decifrado num registro anterior. Um registro
        // TLS pode conter mais bytes do que quem chama pediu, e jogá-los fora
        // perderia dados sem erro nenhum.
        if (!impl_->decrypted.empty()) {
            const std::size_t take =
                std::min(impl_->decrypted.size(), buffer.size() - filled);

            std::memcpy(buffer.data() + filled, impl_->decrypted.data(), take);
            impl_->decrypted.erase(
                impl_->decrypted.begin(),
                impl_->decrypted.begin() + static_cast<std::ptrdiff_t>(take));

            filled += take;
            continue;
        }

        // Tenta decifrar o que já está no buffer de entrada.
        if (!impl_->incoming.empty()) {
            SecBuffer buffers[4]{};
            buffers[0].BufferType = SECBUFFER_DATA;
            buffers[0].pvBuffer   = impl_->incoming.data();
            buffers[0].cbBuffer =
                static_cast<unsigned long>(impl_->incoming.size());
            buffers[1].BufferType = SECBUFFER_EMPTY;
            buffers[2].BufferType = SECBUFFER_EMPTY;
            buffers[3].BufferType = SECBUFFER_EMPTY;

            SecBufferDesc desc{SECBUFFER_VERSION, 4, buffers};

            const SECURITY_STATUS status =
                DecryptMessage(&impl_->context, &desc, 0, nullptr);

            if (status == SEC_E_OK) {
                std::size_t extra_size = 0;
                const std::byte* extra = nullptr;

                for (const SecBuffer& part : buffers) {
                    if (part.BufferType == SECBUFFER_DATA &&
                        part.pvBuffer != nullptr) {
                        const auto* bytes =
                            static_cast<const std::byte*>(part.pvBuffer);
                        impl_->decrypted.insert(impl_->decrypted.end(), bytes,
                                                bytes + part.cbBuffer);
                    } else if (part.BufferType == SECBUFFER_EXTRA &&
                               part.pvBuffer != nullptr) {
                        // Sobra: o começo do PRÓXIMO registro, que veio junto
                        // no mesmo read do socket.
                        extra      = static_cast<const std::byte*>(part.pvBuffer);
                        extra_size = part.cbBuffer;
                    }
                }

                std::vector<std::byte> rest;
                if (extra != nullptr && extra_size > 0) {
                    rest.assign(extra, extra + extra_size);
                }
                impl_->incoming = std::move(rest);
                continue;
            }

            if (status == SEC_I_CONTEXT_EXPIRED) {
                return fail(Errc::closed, "o servidor encerrou o TLS");
            }
            if (status != SEC_E_INCOMPLETE_MESSAGE) {
                char code[32];
                std::snprintf(code, sizeof(code), "0x%08lX",
                              static_cast<unsigned long>(status));
                return fail(Errc::io_error,
                            std::string("DecryptMessage falhou (") + code + ")");
            }
            // INCOMPLETE_MESSAGE: falta parte do registro. Segue para o read.
        }

        const std::size_t offset = impl_->incoming.size();
        impl_->incoming.resize(offset + 8192);

        const Result<std::size_t> read =
            socket.read_some(std::span(impl_->incoming).subspan(offset, 8192));
        if (!read) {
            impl_->incoming.resize(offset);
            return std::unexpected(read.error());
        }
        if (*read == 0) {
            impl_->incoming.resize(offset);
            return fail(Errc::closed, "conexão fechada durante a leitura");
        }
        impl_->incoming.resize(offset + *read);
    }
    return {};
}

bool tls_available() noexcept { return true; }

} // namespace otter::net

#endif // _WIN32
