// C-Otter -- db/connection_store.hpp
//
// Le' e grava conexoes no formato do DBeaver (ADR 0012).
//
// Dois arquivos, como o DBeaver: `data-sources.json` com o que e' publico e
// `credentials-config.json` com as senhas cifradas em AES-128-CBC.
#pragma once

#include "base/error.hpp"
#include "db/connection_config.hpp"

#include <string>
#include <vector>

namespace otter::db {

// Onde os arquivos ficam.
struct StoreLocation {
    std::string directory;         // pasta que contem os dois arquivos
    std::string data_sources;      // caminho completo de data-sources.json
    std::string credentials;       // caminho completo de credentials-config.json

    [[nodiscard]] bool exists() const;
};

// Pasta propria do C-Otter: %APPDATA%\C-Otter (ou ~/.config/c-otter).
//
// Separada do workspace do DBeaver de proposito: escrever la' arriscaria
// corromper a configuracao de uma ferramenta em uso, e campos que ainda nao
// implementamos se perderiam na primeira gravacao (ADR 0012).
[[nodiscard]] StoreLocation otter_store_location();

// Workspaces do DBeaver encontrados na maquina, para importar.
//
// Pode haver mais de um: o DBeaver permite varios projetos, cada um com seu
// `.dbeaver/`. Devolve vazio quando o DBeaver nao esta' instalado.
[[nodiscard]] std::vector<StoreLocation> dbeaver_store_locations();

// Perfil lido do disco, com o que nao entendemos preservado.
struct StoredProfile {
    ConnectionProfile profile;

    // Chave da conexao no JSON ("postgres-jdbc-19f2..."). Mantida para que
    // regravar um perfil importado atualize a entrada certa.
    std::string id;

    // `provider` e `driver` do DBeaver, por exemplo "postgresql" e
    // "postgres-jdbc". Guardados mesmo quando nao suportamos o SGBD: e' o que
    // permite dizer ao usuario POR QUE uma conexao nao pode ser usada.
    std::string provider;
    std::string driver;

    // Verdadeiro quando o C-Otter sabe falar com este SGBD. Quando falso,
    // `unsupported_reason` diz o motivo -- a conexao aparece na lista de
    // importacao, esmaecida, nunca escondida.
    bool        supported = false;
    std::string unsupported_reason;

    // Campos do JSON de origem que nao mapeamos. Regravados como vieram, para
    // nao destruir configuracao de uma versao mais nova do DBeaver.
    std::string raw_json;
};

// Le' os dois arquivos de um local.
//
// Um `credentials-config.json` ausente ou ilegivel NAO e' erro: as conexoes
// vem sem senha e o usuario a digita. Perder a lista inteira porque a chave
// mudou seria pior.
[[nodiscard]] Result<std::vector<StoredProfile>> load_profiles(
    const StoreLocation& location);

// Grava. Cria o diretorio se preciso.
//
// `save_passwords` falso omite o arquivo de credenciais por completo -- e' o
// que acontece com conexoes marcadas como Producao.
// O par (provider, driver) do formato do DBeaver para um driver nosso.
//
// A conversao inversa de `apply_driver`, e ela precisa existir: gravar um
// perfil novo exige escolher o provider a partir do `driver_id`, e cravar
// "postgresql" ali fazia todo perfil MySQL criado na tela ser gravado como
// PostgreSQL -- que so' aparecia na RELEITURA, conectando com o protocolo
// errado e terminando em timeout.
struct ProviderNames {
    std::string_view provider;
    std::string_view driver;
};

[[nodiscard]] ProviderNames provider_for_driver(std::string_view driver_id) noexcept;

[[nodiscard]] Status save_profiles(const StoreLocation& location,
                                   const std::vector<StoredProfile>& profiles);

} // namespace otter::db
