// C-Otter -- db/connection_import.hpp
//
// Conexoes salvas por OUTRAS ferramentas, para a primeira execucao.
//
// Pedido do usuario (2026-10-01): "Ao abrir e a pasta nao existir ou estiver
// vazia deve copiar as conexoes salvas, se existirem, do DBeaver, do pgAdmin
// e do MS SQL Server Management Studio."
//
//   DBeaver   data-sources.json + credentials-config.json de cada workspace
//             (db/connection_store.hpp ja' os le; as senhas vem junto)
//   pgAdmin   a tabela `server` de pgadmin4.db, um arquivo SQLite. As senhas
//             estao la', cifradas (AES-CFB8) com uma chave que o pgAdmin
//             guarda no cofre do sistema: no Windows ela e' lida do
//             Gerenciador de Credenciais e as senhas VEM. Quem usa a
//             senha-mestra DIGITADA nao tem a chave no cofre: sem senha.
//   SSMS      UserSettings.xml, a lista de servidores recentes. O SSMS 20+
//             guarda a senha de cada servidor no Gerenciador de Credenciais
//             do Windows (o XML fica com `<Password />` vazio): ela e' lida
//             de la' e VEM. As do SSMS 18/19, cifradas por DPAPI dentro do
//             XML, nao.
//
// So' LEITURA: nada aqui escreve na pasta de outra ferramenta.
#pragma once

#include "base/error.hpp"
#include "db/connection_store.hpp"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace otter::db {

// --- SQLite, o minimo para ler uma tabela ---------------------------------------
//
// O pgAdmin guarda os servidores num banco SQLite. O projeto nao embute a
// biblioteca do SQLite para ler UMA tabela: o formato do arquivo e' publico
// e estavel (https://www.sqlite.org/fileformat2.html), e percorrer a arvore
// B de uma tabela sao duzentas linhas.
//
// Cada linha vira nome-da-coluna -> texto. NULL e BLOB ficam de fora do mapa.
// Limites: so' UTF-8, e nao le' o que ainda esta' no arquivo -wal.
using SqliteRow = std::map<std::string, std::string>;

[[nodiscard]] Result<std::vector<SqliteRow>> read_sqlite_table(
    std::string_view file_bytes, std::string_view table);

// --- pgAdmin 4 -------------------------------------------------------------------

// `master_key` = a chave do pgAdmin (pgadmin_master_key). Vazia, as conexoes
// vem sem senha.
[[nodiscard]] std::vector<StoredProfile> pgadmin_profiles(std::string_view database_bytes,
                                                         std::string_view master_key = {});

// Uma senha da coluna `server.password`: hex(base64(IV || AES-CFB8(senha))).
// A chave e' ajustada como o pgAdmin faz (`pad` de pgadmin/utils/crypto.py):
// 16, 24 ou 32 bytes vao como estao; outro tamanho e' completado com '}'
// ate' 32.
//
// CFB8 nao acusa chave errada -- devolve lixo. Por isso o resultado so' e'
// aceito se for texto UTF-8 sem caractere de controle; senao, erro.
[[nodiscard]] Result<std::string> pgadmin_decrypt(std::string_view stored,
                                                  std::string_view master_key);

// A chave com que o pgAdmin desta maquina cifra as senhas: a entrada
// `pgAdmin4` / `pgadmin4-master-password` do cofre do sistema. Vazio quando
// nao ha' (pgAdmin com senha-mestra digitada, ou fora do Windows).
[[nodiscard]] std::string pgadmin_master_key();

// --- SQL Server Management Studio ---------------------------------------------------

// A senha salva de um servidor do SSMS: (instancia como esta' no XML, usuario,
// metodo de autenticacao) -> senha, ou vazio. Quem a implementa e'
// external_profiles (o cofre do Windows); nos testes, uma tabela.
using SsmsSecretLookup = std::function<std::string(
    std::string_view instance, std::string_view user, std::string_view method)>;

[[nodiscard]] std::vector<StoredProfile> ssms_profiles(std::string_view user_settings_xml,
                                                      const SsmsSecretLookup& secret = {});

// O alvo da credencial no cofre, como o SSMS o escreve:
// "Microsoft:SSMS:<versao>:<instancia>:<usuario>:<tipo de servidor>:<metodo>".
[[nodiscard]] std::string ssms_credential_target(std::string_view major_version,
                                                 std::string_view instance,
                                                 std::string_view user,
                                                 std::string_view method);

// --- Primeira execucao ----------------------------------------------------------------

// A pasta de dados ainda nao tem nada do programa: nao existe, ou nao tem
// nem conexoes nem preferencias.
[[nodiscard]] bool store_is_fresh(const StoreLocation& location);

// O grupo da arvore em que uma conexao importada entra: o nome da ferramenta
// e, quando a conexao ja' tinha pasta la', "<ferramenta>/<pasta>". Pedido do
// usuario (2026-10-01): "Ao importar as conexoes, mesmo as do DBeaver, crie o
// grupo como das outras ferramentas."
[[nodiscard]] std::string tool_folder(std::string_view tool, std::string_view original);

// De onde cada conexao veio, para o relato na tela.
struct ExternalProfiles {
    std::vector<StoredProfile> profiles;
    std::size_t from_dbeaver = 0;
    std::size_t from_pgadmin = 0;
    std::size_t from_ssms    = 0;
};

// Tudo o que as tres ferramentas tem salvo nesta maquina, sem repetir o mesmo
// alvo (driver + host + porta + banco + usuario) e com nomes unicos.
[[nodiscard]] ExternalProfiles external_profiles();

} // namespace otter::db
