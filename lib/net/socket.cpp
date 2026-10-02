#include "net/socket.hpp"

#include <cstring>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t  = SOCKET;
using socklen_t = int;
// Tamanho em send/recv: int no Winsock, size_t no POSIX.
using io_size_t = int;
#define OTTER_INVALID_SOCKET INVALID_SOCKET
#define OTTER_CLOSE_SOCKET   ::closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using socket_t = int;
using io_size_t = std::size_t;
#define OTTER_INVALID_SOCKET (-1)
#define OTTER_CLOSE_SOCKET   ::close
#endif

namespace otter::net {
namespace {

socket_t to_native(std::uintptr_t handle) { return static_cast<socket_t>(handle); }

std::string last_error_message() {
#ifdef _WIN32
    const int code = WSAGetLastError();
    char* buffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&buffer), 0, nullptr);

    std::string message = length > 0 && buffer != nullptr
                              ? std::string(buffer, length)
                              : ("erro " + std::to_string(code));
    if (buffer != nullptr) LocalFree(buffer);

    while (!message.empty() &&
           (message.back() == '\n' || message.back() == '\r' || message.back() == ' ')) {
        message.pop_back();
    }
    return message;
#else
    return std::string(std::strerror(errno));
#endif
}

} // namespace

Result<std::vector<std::byte>> udp_exchange(std::string_view host, std::uint16_t port,
                                            std::span<const std::byte> request,
                                            std::chrono::milliseconds timeout) {
    OTTER_RETURN_IF_ERROR(initialize_network());

    addrinfo hints = {};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    const std::string host_text(host);
    const std::string port_text = std::to_string(port);

    addrinfo* candidates = nullptr;
    if (::getaddrinfo(host_text.c_str(), port_text.c_str(), &hints, &candidates) != 0 ||
        candidates == nullptr) {
        return fail(Errc::connection_failed,
                    "não foi possível resolver '" + host_text + "'");
    }

    std::string last_error = "nenhum endereço utilizável";
    Result<std::vector<std::byte>> result =
        fail(Errc::connection_failed, host_text + ":" + port_text + " — " + last_error);

    for (addrinfo* it = candidates; it != nullptr; it = it->ai_next) {
        const socket_t s = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (s == OTTER_INVALID_SOCKET) continue;

        // connect() num socket UDP so' fixa o destino: as respostas de outro
        // endereco sao descartadas pelo sistema, e um ICMP "porta fechada"
        // vira erro na leitura em vez de esperar o tempo todo.
        if (::connect(s, it->ai_addr, static_cast<socklen_t>(it->ai_addrlen)) != 0 ||
            ::send(s, reinterpret_cast<const char*>(request.data()),
                   static_cast<io_size_t>(request.size()), 0) < 0) {
            last_error = last_error_message();
            OTTER_CLOSE_SOCKET(s);
            continue;
        }

        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(s, &readable);
        timeval tv;
        tv.tv_sec  = static_cast<long>(timeout.count() / 1000);
        tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

        const int ready = ::select(static_cast<int>(s) + 1, &readable, nullptr, nullptr, &tv);
        if (ready > 0) {
            std::vector<std::byte> buffer(65536);
            const auto received = ::recv(s, reinterpret_cast<char*>(buffer.data()),
                                         static_cast<io_size_t>(buffer.size()), 0);
            if (received >= 0) {
                buffer.resize(static_cast<std::size_t>(received));
                OTTER_CLOSE_SOCKET(s);
                ::freeaddrinfo(candidates);
                return buffer;
            }
            last_error = last_error_message();
            result = fail(Errc::connection_failed,
                          host_text + ":" + port_text + " (UDP) — " + last_error);
        } else if (ready == 0) {
            result = fail(Errc::timed_out,
                          host_text + ":" + port_text + " (UDP) — sem resposta");
        } else {
            last_error = last_error_message();
            result = fail(Errc::connection_failed,
                          host_text + ":" + port_text + " (UDP) — " + last_error);
        }
        OTTER_CLOSE_SOCKET(s);
    }

    ::freeaddrinfo(candidates);
    return result;
}

Status initialize_network() {
#ifdef _WIN32
    static Status result = [] () -> Status {
        WSADATA data;
        const int code = WSAStartup(MAKEWORD(2, 2), &data);
        if (code != 0) {
            return fail(Errc::internal, "WSAStartup falhou: " + std::to_string(code));
        }
        return Status{};
    }();
    return result;
#else
    return {};
#endif
}

Socket::~Socket() { close(); }

Socket::Socket(Socket&& other) noexcept : handle_(other.handle_) {
    other.handle_ = kInvalid;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        handle_       = other.handle_;
        other.handle_ = kInvalid;
    }
    return *this;
}

bool Socket::is_open() const noexcept { return handle_ != kInvalid; }

void Socket::close() noexcept {
    if (handle_ != kInvalid) {
        OTTER_CLOSE_SOCKET(to_native(handle_));
        handle_ = kInvalid;
    }
}

Result<Socket> Socket::connect(std::string_view host, std::uint16_t port,
                               std::chrono::milliseconds timeout) {
    OTTER_RETURN_IF_ERROR(initialize_network());

    addrinfo hints = {};
    hints.ai_family   = AF_UNSPEC;      // IPv4 ou IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    const std::string host_text(host);
    const std::string port_text = std::to_string(port);

    addrinfo* candidates = nullptr;
    const int code = ::getaddrinfo(host_text.c_str(), port_text.c_str(),
                                   &hints, &candidates);
    if (code != 0 || candidates == nullptr) {
        return fail(Errc::connection_failed,
                    "não foi possível resolver '" + host_text + "'");
    }

    std::string last_error = "nenhum endereço utilizável";
    socket_t connected = OTTER_INVALID_SOCKET;

    for (addrinfo* it = candidates; it != nullptr; it = it->ai_next) {
        socket_t s = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (s == OTTER_INVALID_SOCKET) continue;

        // Conecta em modo nao bloqueante para poder aplicar o timeout; um
        // connect bloqueante ignora SO_RCVTIMEO e pode travar por minutos.
#ifdef _WIN32
        u_long non_blocking = 1;
        ::ioctlsocket(s, FIONBIO, &non_blocking);
#else
        const int flags = ::fcntl(s, F_GETFL, 0);
        ::fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif

        int status = ::connect(s, it->ai_addr, static_cast<socklen_t>(it->ai_addrlen));
        bool in_progress = false;

#ifdef _WIN32
        in_progress = (status != 0) && (WSAGetLastError() == WSAEWOULDBLOCK);
#else
        in_progress = (status != 0) && (errno == EINPROGRESS);
#endif

        if (status != 0 && in_progress) {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(s, &writable);

            timeval tv;
            tv.tv_sec  = static_cast<long>(timeout.count() / 1000);
            tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

            const int ready = ::select(static_cast<int>(s) + 1, nullptr, &writable,
                                       nullptr, &tv);
            if (ready > 0) {
                // select() acorda tambem em falha: confirmar via SO_ERROR.
                int so_error = 0;
                socklen_t length = sizeof(so_error);
                ::getsockopt(s, SOL_SOCKET, SO_ERROR,
                             reinterpret_cast<char*>(&so_error), &length);
                status = (so_error == 0) ? 0 : -1;
            } else {
                status = -1;
                last_error = ready == 0 ? "tempo esgotado ao conectar"
                                        : last_error_message();
            }
        } else if (status != 0) {
            last_error = last_error_message();
        }

        if (status == 0) {
            // Volta ao modo bloqueante: as leituras usam SO_RCVTIMEO.
#ifdef _WIN32
            u_long blocking = 0;
            ::ioctlsocket(s, FIONBIO, &blocking);
#else
            const int f = ::fcntl(s, F_GETFL, 0);
            ::fcntl(s, F_SETFL, f & ~O_NONBLOCK);
#endif
            connected = s;
            break;
        }

        OTTER_CLOSE_SOCKET(s);
    }

    ::freeaddrinfo(candidates);

    if (connected == OTTER_INVALID_SOCKET) {
        return fail(Errc::connection_failed,
                    host_text + ":" + port_text + " — " + last_error);
    }

    Socket socket(static_cast<std::uintptr_t>(connected));
    socket.set_no_delay(true);
    socket.set_read_timeout(timeout);
    socket.set_write_timeout(timeout);
    return socket;
}

Result<std::uint16_t> free_local_port() {
    OTTER_RETURN_IF_ERROR(initialize_network());

    const socket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == OTTER_INVALID_SOCKET) {
        return fail(Errc::io_error, "cannot create a socket: " + last_error_message());
    }

    sockaddr_in address = {};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port        = 0;   // o sistema escolhe

    socklen_t length = sizeof address;
    if (::bind(s, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 ||
        ::getsockname(s, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        const std::string message = last_error_message();
        OTTER_CLOSE_SOCKET(s);
        return fail(Errc::io_error, "cannot reserve a local port: " + message);
    }

    const std::uint16_t port = ntohs(address.sin_port);
    OTTER_CLOSE_SOCKET(s);
    return port;
}

void Socket::set_no_delay(bool enabled) {
    if (!is_open()) return;
    const int value = enabled ? 1 : 0;
    ::setsockopt(to_native(handle_), IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char*>(&value), sizeof(value));
}

void Socket::set_read_timeout(std::chrono::milliseconds timeout) {
    if (!is_open()) return;
#ifdef _WIN32
    const DWORD value = static_cast<DWORD>(timeout.count());
    ::setsockopt(to_native(handle_), SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&value), sizeof(value));
#else
    timeval tv;
    tv.tv_sec  = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    ::setsockopt(to_native(handle_), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

void Socket::set_write_timeout(std::chrono::milliseconds timeout) {
    if (!is_open()) return;
#ifdef _WIN32
    const DWORD value = static_cast<DWORD>(timeout.count());
    ::setsockopt(to_native(handle_), SOL_SOCKET, SO_SNDTIMEO,
                 reinterpret_cast<const char*>(&value), sizeof(value));
#else
    timeval tv;
    tv.tv_sec  = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    ::setsockopt(to_native(handle_), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

Status Socket::write_all(std::span<const std::byte> data) {
    if (!is_open()) return fail(Errc::closed, "socket fechado");

    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto chunk = static_cast<io_size_t>(
            std::min<std::size_t>(data.size() - sent, 1 << 20));

        const auto written = ::send(
            to_native(handle_),
            reinterpret_cast<const char*>(data.data() + sent),
            chunk, 0);

        if (written <= 0) {
            return fail(Errc::io_error, "falha ao enviar: " + last_error_message());
        }
        sent += static_cast<std::size_t>(written);
    }
    return {};
}

Result<std::size_t> Socket::read_some(std::span<std::byte> buffer) {
    if (!is_open()) return fail(Errc::closed, "socket fechado");
    if (buffer.empty()) return std::size_t{0};

    const auto got = ::recv(to_native(handle_),
                            reinterpret_cast<char*>(buffer.data()),
                            static_cast<io_size_t>(buffer.size()), 0);

    if (got < 0) {
#ifdef _WIN32
        if (WSAGetLastError() == WSAETIMEDOUT)
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK)
#endif
        {
            return fail(Errc::timed_out, "tempo esgotado na leitura");
        }
        return fail(Errc::io_error, "falha ao receber: " + last_error_message());
    }
    return static_cast<std::size_t>(got);
}

Status Socket::read_exact(std::span<std::byte> buffer) {
    std::size_t total = 0;
    while (total < buffer.size()) {
        OTTER_ASSIGN_OR_RETURN(
            const std::size_t got,
            read_some(buffer.subspan(total)));

        if (got == 0) {
            // Servidor fechou no meio de uma mensagem: nao ha' interpretacao
            // valida para dados truncados de protocolo.
            return fail(Errc::protocol_error,
                        "conexão encerrada pelo servidor durante a leitura");
        }
        total += got;
    }
    return {};
}

} // namespace otter::net
