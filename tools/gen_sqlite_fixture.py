"""Gera o teste de db/connection_import com um banco SQLite de verdade embutido.

O banco imita o pgadmin4.db: tabela `server` com as colunas que o importador
le', pagina de 512 bytes e linhas o bastante para a arvore B ter pagina
interior e um registro com pagina de transbordo.
"""
import base64
import os
import sqlite3
import tempfile

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

# --- As senhas, cifradas como o pgAdmin as grava ---------------------------------
#
# pgadmin/utils/crypto.py: base64(IV || AES-CFB8(senha)), e a coluna do modelo
# (PgAdminDbBinaryString) guarda o HEX desse texto. O IV e' fixo aqui para o
# arquivo gerado ser sempre o mesmo.
MASTER_KEY = '0123456789abcdef'          # 16 caracteres: o token_urlsafe(12) do pgAdmin
PASSWORD = "s3nh@ d'água"
IV = bytes(range(0x10, 0x20))


def pad(key):
    key = key.encode()[:32]
    return key if len(key) in (16, 24, 32) else key.ljust(32, b'}')


def pgadmin_encrypt(plaintext, key, hexed=True):
    encryptor = Cipher(algorithms.AES(pad(key)), modes.CFB8(IV)).encryptor()
    text = base64.b64encode(IV + encryptor.update(plaintext.encode()) + encryptor.finalize())
    return text.hex() if hexed else text.decode()


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
path = os.path.join(tempfile.gettempdir(), 'otter-fixture.db')
if os.path.exists(path):
    os.remove(path)

con = sqlite3.connect(path)
con.execute('pragma page_size = 512')
con.execute('''CREATE TABLE "server" (
    id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
    name VARCHAR(128) NOT NULL,
    host VARCHAR(128),
    port INTEGER,
    maintenance_db VARCHAR(64),
    username VARCHAR(64),
    comment VARCHAR(1024),
    password VARCHAR,
    role VARCHAR(64),
    use_ssh_tunnel INTEGER DEFAULT 0,
    tunnel_host VARCHAR(128),
    tunnel_port INTEGER,
    tunnel_username VARCHAR(64),
    tunnel_authentication INTEGER DEFAULT 0,
    tunnel_identity_file VARCHAR(64),
    connection_params JSON,
    CHECK (port >= 1)
)''')
rows = [
    ('Local', 'localhost', 5432, 'postgres', 'postgres', None,
     pgadmin_encrypt(PASSWORD, MASTER_KEY), None, 0,
     None, None, None, 0, None, '{"sslmode": "prefer", "connect_timeout": 10}'),
    ('Produção', 'db.example.com', 54045, 'erp', 'ana', 'x' * 900, None, 'relatorio', 1,
     'bastion.example', 2222, 'deploy', 1, 'C:/keys/id', '{"sslmode": "verify-full"}'),
]
for i in range(40):
    rows.append(('srv%02d' % i, 'h%02d.example' % i, 6000 + i, 'db', 'u', None, None, None, 0,
                 None, None, None, 0, None, None))
con.executemany('INSERT INTO server (name, host, port, maintenance_db, username, comment, '
                'password, role, use_ssh_tunnel, tunnel_host, tunnel_port, tunnel_username, '
                'tunnel_authentication, tunnel_identity_file, connection_params) '
                'VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)', rows)
con.commit()
con.close()

data = open(path, 'rb').read()
os.remove(path)

lines = []
for i in range(0, len(data), 20):
    lines.append('    ' + ','.join('0x%02x' % b for b in data[i:i + 20]) + ',')

test = r'''// C-Otter -- testes de db/connection_import: as conexoes do pgAdmin e do SSMS.
//
// GERADO por tools/gen_sqlite_fixture.py -- o vetor abaixo e' um banco SQLite
// de verdade (pagina de 512 bytes, 42 linhas: arvore com pagina interior e um
// registro com transbordo), criado pela biblioteca do SQLite. O leitor daqui
// e' escrito a' mao, e so' um arquivo real prova que ele le' o formato.
#include "test_main.hpp"

#include "db/connection_import.hpp"

#include <string>
#include <string_view>

using namespace otter;
using namespace otter::db;

namespace {

constexpr unsigned char kPgAdminDb[] = {
@@BYTES@@
};

std::string_view fixture() {
    return {reinterpret_cast<const char*>(kPgAdminDb), sizeof kPgAdminDb};
}

} // namespace

OTTER_TEST(sqlite_reads_every_row_across_interior_pages) {
    const auto rows = read_sqlite_table(fixture(), "server");
    OTTER_CHECK(rows.has_value());
    OTTER_CHECK_EQ(rows->size(), std::size_t{42});

    // INTEGER PRIMARY KEY nao e' gravado no registro: vem do rowid.
    OTTER_CHECK_EQ(rows->front().at("id"), std::string{"1"});
    OTTER_CHECK_EQ(rows->front().at("name"), std::string{"Local"});
    OTTER_CHECK_EQ(rows->front().at("port"), std::string{"5432"});
    // NULL fica de fora do mapa.
    OTTER_CHECK(rows->front().find("comment") == rows->front().end());

    // O comentario de 900 bytes nao cabe na pagina: veio das de transbordo.
    OTTER_CHECK_EQ((*rows)[1].at("comment"), std::string(900, 'x'));
    OTTER_CHECK_EQ((*rows)[1].at("name"), std::string{"Produção"});
    OTTER_CHECK_EQ(rows->back().at("host"), std::string{"h39.example"});
}

OTTER_TEST(sqlite_refuses_what_is_not_a_database) {
    OTTER_CHECK(!read_sqlite_table("isto nao e' um banco", "server").has_value());
    OTTER_CHECK(!read_sqlite_table(fixture(), "tabela_ausente").has_value());
    // Arquivo cortado no meio: erro ou menos linhas, nunca leitura fora do buffer.
    const auto cut = read_sqlite_table(fixture().substr(0, 2000), "server");
    OTTER_CHECK(!cut.has_value() || cut->size() < 42);
}

OTTER_TEST(pgadmin_password_is_hex_of_base64_of_aes_cfb8) {
    // Gerados pelo `cryptography` do Python, a mesma biblioteca do pgAdmin.
    const std::string_view stored = "@@ENC_HEX@@";
    const auto password = pgadmin_decrypt(stored, "@@MASTER_KEY@@");
    OTTER_CHECK(password.has_value());
    OTTER_CHECK_EQ(*password, std::string{"@@PASSWORD@@"});

    // Banco antigo: o base64 direto, sem a camada de hex.
    const auto old_format = pgadmin_decrypt("@@ENC_B64@@", "@@MASTER_KEY@@");
    OTTER_CHECK(old_format.has_value());
    OTTER_CHECK_EQ(*old_format, std::string{"@@PASSWORD@@"});

    // Chave que nao tem 16, 24 ou 32 bytes e' completada com '}' ate' 32 --
    // o `pad` do pgAdmin. E' o caso da senha-mestra digitada.
    const auto short_key = pgadmin_decrypt("@@ENC_SHORT@@", "curta-1234");
    OTTER_CHECK(short_key.has_value());
    OTTER_CHECK_EQ(*short_key, std::string{"@@PASSWORD@@"});
}

OTTER_TEST(pgadmin_wrong_key_gives_no_password_instead_of_garbage) {
    // CFB8 nao tem como acusar chave errada: devolve bytes aleatorios. Uma
    // "senha" dessas gravada no perfil seria pior que senha nenhuma.
    const std::string_view stored = "@@ENC_HEX@@";
    OTTER_CHECK(!pgadmin_decrypt(stored, "fedcba9876543210").has_value());
    OTTER_CHECK(!pgadmin_decrypt(stored, "").has_value());
    OTTER_CHECK(!pgadmin_decrypt("", "@@MASTER_KEY@@").has_value());
    OTTER_CHECK(!pgadmin_decrypt("isto nao e' base64 !!", "@@MASTER_KEY@@").has_value());
    // Curto demais para ter o IV.
    OTTER_CHECK(!pgadmin_decrypt("QUJD", "@@MASTER_KEY@@").has_value());
}

OTTER_TEST(imported_connections_go_under_the_group_of_their_tool) {
    // Sem pasta na ferramenta de origem: o grupo e' o nome dela.
    OTTER_CHECK_EQ(tool_folder("DBeaver", ""), std::string{"DBeaver"});
    // Com pasta la': fica debaixo do grupo.
    OTTER_CHECK_EQ(tool_folder("DBeaver", "Produção"), std::string{"DBeaver/Produção"});
    // Importar de novo nao empilha "DBeaver/DBeaver/...".
    OTTER_CHECK_EQ(tool_folder("DBeaver", "DBeaver"), std::string{"DBeaver"});
    OTTER_CHECK_EQ(tool_folder("DBeaver", "DBeaver/Produção"), std::string{"DBeaver/Produção"});
    // Uma pasta que so' COMECA igual nao e' o grupo.
    OTTER_CHECK_EQ(tool_folder("DBeaver", "DBeaverOld"), std::string{"DBeaver/DBeaverOld"});
}

OTTER_TEST(pgadmin_servers_bring_the_password_when_the_key_is_known) {
    const std::vector<StoredProfile> with_key = pgadmin_profiles(fixture(), "@@MASTER_KEY@@");
    OTTER_CHECK_EQ(with_key.size(), std::size_t{42});
    OTTER_CHECK_EQ(with_key[0].profile.password, std::string{"@@PASSWORD@@"});
    OTTER_CHECK(with_key[0].profile.save_password);
    // Servidor sem senha salva no pgAdmin continua sem.
    OTTER_CHECK(with_key[1].profile.password.empty());
    OTTER_CHECK(!with_key[1].profile.save_password);

    // Chave errada: a conexao vem, a senha nao.
    const std::vector<StoredProfile> wrong = pgadmin_profiles(fixture(), "fedcba9876543210");
    OTTER_CHECK_EQ(wrong.size(), std::size_t{42});
    OTTER_CHECK(wrong[0].profile.password.empty());
}

OTTER_TEST(pgadmin_servers_become_profiles_without_the_password) {
    const std::vector<StoredProfile> profiles = pgadmin_profiles(fixture());
    OTTER_CHECK_EQ(profiles.size(), std::size_t{42});

    const ConnectionProfile& local = profiles[0].profile;
    OTTER_CHECK(profiles[0].supported);
    OTTER_CHECK_EQ(profiles[0].provider, std::string{"postgresql"});
    OTTER_CHECK_EQ(local.name, std::string{"Local"});
    OTTER_CHECK_EQ(local.host, std::string{"localhost"});
    OTTER_CHECK_EQ(local.port, std::uint16_t{5432});
    OTTER_CHECK_EQ(local.database, std::string{"postgres"});
    OTTER_CHECK_EQ(local.user, std::string{"postgres"});
    OTTER_CHECK_EQ(local.folder, std::string{"pgAdmin"});
    // Sem a chave do pgAdmin a senha nao vem -- e o texto cifrado nunca e'
    // importado como se fosse a senha.
    OTTER_CHECK(local.password.empty());
    OTTER_CHECK(!local.save_password);
    // `prefer` nao exige TLS.
    OTTER_CHECK(!local.ssl.enabled);
    OTTER_CHECK(local.connect_timeout == std::chrono::seconds(10));

    const ConnectionProfile& prod = profiles[1].profile;
    OTTER_CHECK_EQ(prod.port, std::uint16_t{54045});
    OTTER_CHECK_EQ(prod.postgres.session_role, std::string{"relatorio"});
    OTTER_CHECK(prod.ssl.enabled);
    OTTER_CHECK(prod.ssl.mode == SslMode::verify_full);
    OTTER_CHECK(prod.ssh.enabled);
    OTTER_CHECK_EQ(prod.ssh.host, std::string{"bastion.example"});
    OTTER_CHECK_EQ(prod.ssh.port, std::uint16_t{2222});
    OTTER_CHECK_EQ(prod.ssh.user, std::string{"deploy"});
    OTTER_CHECK(prod.ssh.auth == SshAuthType::public_key);
    OTTER_CHECK_EQ(prod.ssh.private_key_path, std::string{"C:/keys/id"});
}

OTTER_TEST(ssms_recent_servers_become_sql_server_profiles) {
    // Recorte fiel de um UserSettings.xml do SSMS 20.
    const char* xml = R"(<SqlStudio><SSMS><ConnectionOptions><ServerTypes><Element>
<Value><ServerTypeItem><Servers>
<Element><Item><ServerConnectionItem>
  <Instance>10.0.0.76,55033</Instance>
  <AuthenticationMethod>1</AuthenticationMethod>
  <Connections><Element><Item><ServerConnectionSettings>
    <Password>AQAAANCMnd8BFdERjHoAwE</Password>
    <Instance>10.0.0.76,55033</Instance>
    <UserName>usr_app</UserName>
    <ServerType>8c91a03d-f9b4-46c0-a305-b5dcc79ff907</ServerType>
    <AuthenticationMethod>1</AuthenticationMethod>
    <Database />
  </ServerConnectionSettings></Item></Element></Connections>
  <Databases><Element><Item><string>master</string></Item></Element></Databases>
</ServerConnectionItem></Item></Element>
<Element><Item><ServerConnectionItem>
  <Instance>HERMESWS\SITTAX</Instance>
  <AuthenticationMethod>0</AuthenticationMethod>
  <Connections><Element><Item><ServerConnectionSettings>
    <Password />
    <Instance>HERMESWS\SITTAX</Instance>
    <UserName>DOMINIO\hermes</UserName>
    <ServerType>8c91a03d-f9b4-46c0-a305-b5dcc79ff907</ServerType>
    <AuthenticationMethod>0</AuthenticationMethod>
    <Database>vendas</Database>
  </ServerConnectionSettings></Item></Element></Connections>
</ServerConnectionItem></Item></Element>
<Element><Item><ServerConnectionItem>
  <Instance>olap01</Instance>
  <Connections><Element><Item><ServerConnectionSettings>
    <Instance>olap01</Instance>
    <ServerType>3f9d5c2e-0000-0000-0000-000000000000</ServerType>
  </ServerConnectionSettings></Item></Element></Connections>
</ServerConnectionItem></Item></Element>
</Servers></ServerTypeItem></Value></Element></ServerTypes></ConnectionOptions></SSMS></SqlStudio>)";

    // O cofre do Windows, aqui uma tabela: quem foi consultado, e com o que.
    std::vector<std::string> asked;
    const SsmsSecretLookup vault = [&asked](std::string_view instance, std::string_view user,
                                            std::string_view method) {
        asked.push_back(ssms_credential_target("20", instance, user, method));
        return user == "usr_app" ? std::string("s3nh@ do cofre") : std::string{};
    };

    const std::vector<StoredProfile> with_vault = ssms_profiles(xml, vault);
    OTTER_CHECK_EQ(with_vault.size(), std::size_t{2});
    // O SSMS 20+ deixa `<Password />` vazio no XML e guarda a senha no
    // Gerenciador de Credenciais, sob este alvo -- com a instancia COMO ESTA'
    // no XML ("host,porta"), nao como o perfil a guarda.
    OTTER_CHECK_EQ(asked.size(), std::size_t{1});
    OTTER_CHECK_EQ(asked[0],
                   std::string{"Microsoft:SSMS:20:10.0.0.76,55033:usr_app:"
                               "8c91a03d-f9b4-46c0-a305-b5dcc79ff907:1"});
    OTTER_CHECK_EQ(with_vault[0].profile.password, std::string{"s3nh@ do cofre"});
    OTTER_CHECK(with_vault[0].profile.save_password);
    // Autenticacao do Windows nao tem senha a procurar.
    OTTER_CHECK(with_vault[1].profile.password.empty());

    const std::vector<StoredProfile> profiles = ssms_profiles(xml);
    // O terceiro nao e' Database Engine: fica de fora.
    OTTER_CHECK_EQ(profiles.size(), std::size_t{2});

    // Entram como conexoes do driver do SQL Server (ADR 0024).
    OTTER_CHECK(profiles[0].supported);
    OTTER_CHECK_EQ(profiles[0].profile.driver_id, std::string{"sqlserver"});
    OTTER_CHECK(profiles[0].profile.auth_model == AuthModel::database_native);
    OTTER_CHECK_EQ(profiles[0].provider, std::string{"sqlserver"});
    OTTER_CHECK_EQ(profiles[0].profile.host, std::string{"10.0.0.76"});
    OTTER_CHECK_EQ(profiles[0].profile.port, std::uint16_t{55033});
    OTTER_CHECK_EQ(profiles[0].profile.user, std::string{"usr_app"});
    OTTER_CHECK(profiles[0].profile.database.empty());
    // Sem acesso ao cofre a senha nao vem -- e o texto do XML (o blob DPAPI
    // do SSMS 18/19) nunca e' importado como se fosse a senha.
    OTTER_CHECK(profiles[0].profile.password.empty());

    // Instancia nomeada: fica inteira no host, na porta padrao; autenticacao
    // do Windows nao tem usuario a guardar.
    OTTER_CHECK_EQ(profiles[1].profile.host, std::string{"HERMESWS\\SITTAX"});
    OTTER_CHECK_EQ(profiles[1].profile.port, std::uint16_t{1433});
    OTTER_CHECK(profiles[1].profile.user.empty());
    OTTER_CHECK(profiles[1].profile.auth_model == AuthModel::windows);
    OTTER_CHECK_EQ(profiles[1].profile.database, std::string{"vendas"});
}
'''
out = (test.replace('@@BYTES@@', '\n'.join(lines))
           .replace('@@ENC_HEX@@', pgadmin_encrypt(PASSWORD, MASTER_KEY))
           .replace('@@ENC_B64@@', pgadmin_encrypt(PASSWORD, MASTER_KEY, hexed=False))
           .replace('@@ENC_SHORT@@', pgadmin_encrypt(PASSWORD, 'curta-1234'))
           .replace('@@MASTER_KEY@@', MASTER_KEY)
           .replace('@@PASSWORD@@', PASSWORD)
           .replace('\n', '\r\n'))
open(os.path.join(ROOT, 'tests', 'unit', 'test_connection_import.cpp'), 'wb').write(out.encode('utf-8'))
print('fixture bytes', len(data))
