// Persistencia de conexoes no formato do DBeaver (ADR 0012).
//
// A leitura contra o arquivo REAL do DBeaver e' feita pelo
// spikes/dbeaver_import. Aqui ficam os casos que precisam ser reproduziveis
// em qualquer maquina: o formato aceito, o que acontece com campo ausente, e
// o ciclo gravar-ler.
#include "test_main.hpp"

#include "db/connection_store.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace otter::db;

namespace {

namespace fs = std::filesystem;

// Diretorio temporario proprio por teste: rodar a suite duas vezes em
// paralelo nao pode fazer um teste ler o arquivo do outro.
class TempDir {
public:
    explicit TempDir(std::string_view name)
        : path_(fs::temp_directory_path() / ("otter-test-" + std::string(name))) {
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    TempDir(const TempDir&)            = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] StoreLocation location() const {
        StoreLocation loc;
        loc.directory    = path_.string();
        loc.data_sources = (path_ / "data-sources.json").string();
        loc.credentials  = (path_ / "credentials-config.json").string();
        return loc;
    }

    void write(std::string_view file, std::string_view content) const {
        std::ofstream out(path_ / file, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

private:
    fs::path path_;
};

} // namespace

OTTER_TEST(store_reads_the_dbeaver_format) {
    const TempDir dir("read-dbeaver");

    // Recorte fiel de um data-sources.json real: porta como string, acento
    // escapado, propriedades de driver.
    dir.write("data-sources.json", R"({
  "folders": {},
  "connections": {
    "postgres-jdbc-19f22-abc": {
      "provider": "postgresql",
      "driver": "postgres-jdbc",
      "name": "Produção ERP",
      "save-password": true,
      "configuration": {
        "host": "db.example.com",
        "port": "5433",
        "database": "ERP_TID",
        "user": "relatorio",
        "type": "prod",
        "properties": {"ApplicationName": "C-Otter"}
      }
    }
  }
})");

    auto profiles = load_profiles(dir.location());
    OTTER_CHECK(profiles.has_value());
    OTTER_CHECK_EQ(profiles->size(), std::size_t{1});

    const StoredProfile& stored = profiles->front();
    OTTER_CHECK(stored.supported);
    OTTER_CHECK_EQ(stored.provider, std::string{"postgresql"});

    const ConnectionProfile& p = stored.profile;
    OTTER_CHECK_EQ(p.name, std::string{"Produção ERP"});
    OTTER_CHECK_EQ(p.host, std::string{"db.example.com"});
    OTTER_CHECK_EQ(p.port, std::uint16_t{5433});
    OTTER_CHECK_EQ(p.database, std::string{"ERP_TID"});
    OTTER_CHECK_EQ(p.user, std::string{"relatorio"});
    OTTER_CHECK(p.type == ConnectionType::production);
    OTTER_CHECK(p.save_password);
    OTTER_CHECK_EQ(p.driver_properties.at("ApplicationName"),
                   std::string{"C-Otter"});
}

OTTER_TEST(store_reports_why_a_connection_is_unusable) {
    const TempDir dir("unsupported");

    dir.write("data-sources.json", R"({
  "connections": {
    "mysql8-123": {
      "provider": "mysql", "driver": "mysql8", "name": "legado",
      "configuration": {"host": "10.0.0.1", "port": "3306"}
    }
  }
})");

    auto profiles = load_profiles(dir.location());
    OTTER_CHECK(profiles.has_value());
    OTTER_CHECK_EQ(profiles->size(), std::size_t{1});

    // A conexao aparece, esmaecida e COM O MOTIVO -- esconder faria o usuario
    // achar que o C-Otter nao leu o arquivo dele (diretiva 6).
    const StoredProfile& stored = profiles->front();
    OTTER_CHECK(!stored.supported);
    OTTER_CHECK(!stored.unsupported_reason.empty());
    OTTER_CHECK_EQ(stored.profile.host, std::string{"10.0.0.1"});

    // O motivo e' texto fixo em ingles -- chave de traducao, resolvida por
    // TR() na hora de desenhar. Um texto montado com o nome do provider
    // nunca casaria com o catalogo.
    OTTER_CHECK(stored.unsupported_reason.find("MySQL") != std::string::npos);
}

OTTER_TEST(store_survives_fields_it_does_not_know) {
    const TempDir dir("unknown-fields");

    dir.write("data-sources.json", R"({
  "connections": {
    "postgres-jdbc-1": {
      "provider": "postgresql", "driver": "postgres-jdbc", "name": "x",
      "campo-de-versao-futura": {"algo": [1, 2, 3]},
      "configuration": {
        "host": "h", "port": "5432",
        "outra-coisa-desconhecida": true
      }
    }
  }
})");

    // Campo desconhecido nao pode derrubar a leitura: o arquivo pode vir de
    // uma versao do DBeaver mais nova que a nossa.
    auto profiles = load_profiles(dir.location());
    OTTER_CHECK(profiles.has_value());
    OTTER_CHECK_EQ(profiles->size(), std::size_t{1});
    OTTER_CHECK_EQ(profiles->front().profile.host, std::string{"h"});

    // E precisa ficar guardado para regravacao.
    OTTER_CHECK(profiles->front().raw_json.find("campo-de-versao-futura") !=
                std::string::npos);
}

OTTER_TEST(store_round_trips_a_profile_with_password) {
    const TempDir dir("round-trip");

    StoredProfile stored;
    stored.id       = "postgres-jdbc-teste";
    stored.provider = "postgresql";
    stored.driver   = "postgres-jdbc";
    stored.supported = true;

    stored.profile.name     = "Conexão de teste";
    stored.profile.host     = "localhost";
    stored.profile.port     = 5432;
    stored.profile.database = "ERP_TID";
    stored.profile.user     = "postgres";
    stored.profile.password = "senha-com-acento-ção";
    stored.profile.save_password = true;
    stored.profile.type     = ConnectionType::test;
    stored.profile.driver_properties["sslmode"] = "prefer";

    OTTER_CHECK(save_profiles(dir.location(), {stored}).has_value());

    auto back = load_profiles(dir.location());
    OTTER_CHECK(back.has_value());
    OTTER_CHECK_EQ(back->size(), std::size_t{1});

    const ConnectionProfile& p = back->front().profile;
    OTTER_CHECK_EQ(p.name, std::string{"Conexão de teste"});
    OTTER_CHECK_EQ(p.host, std::string{"localhost"});
    OTTER_CHECK_EQ(p.port, std::uint16_t{5432});
    OTTER_CHECK_EQ(p.user, std::string{"postgres"});
    OTTER_CHECK_EQ(p.password, std::string{"senha-com-acento-ção"});
    OTTER_CHECK(p.type == ConnectionType::test);
    OTTER_CHECK_EQ(p.driver_properties.at("sslmode"), std::string{"prefer"});
}

OTTER_TEST(store_does_not_write_a_password_when_told_not_to) {
    const TempDir dir("no-password");

    StoredProfile stored;
    stored.id = "postgres-jdbc-sem-senha";
    stored.provider = "postgresql";
    stored.profile.name     = "producao";
    stored.profile.password = "nao-deve-ir-para-o-disco";
    stored.profile.save_password = false;

    OTTER_CHECK(save_profiles(dir.location(), {stored}).has_value());

    // Sem senha a guardar, o arquivo de credenciais nao deve existir: um
    // arquivo cifrado vazio so' geraria duvida sobre o que ha' dentro.
    OTTER_CHECK(!fs::exists(dir.location().credentials));

    auto back = load_profiles(dir.location());
    OTTER_CHECK(back.has_value());
    OTTER_CHECK(back->front().profile.password.empty());
}

OTTER_TEST(store_reads_a_missing_file_as_empty_not_as_error) {
    const TempDir dir("missing");

    // Primeira execucao do programa: nao ha' arquivo, e isso e' normal.
    auto profiles = load_profiles(dir.location());
    OTTER_CHECK(profiles.has_value());
    OTTER_CHECK(profiles->empty());
}

OTTER_TEST(store_keeps_connections_when_credentials_cannot_be_read) {
    const TempDir dir("bad-credentials");

    dir.write("data-sources.json", R"({
  "connections": {
    "postgres-jdbc-1": {
      "provider": "postgresql", "name": "x",
      "configuration": {"host": "h", "port": "5432"}
    }
  }
})");
    // Lixo no lugar do arquivo cifrado -- chave trocada, arquivo corrompido.
    dir.write("credentials-config.json", "isto nao decifra com a chave certa");

    // Perder a lista inteira de conexoes porque a senha nao abriu seria pior
    // que pedir a senha: o usuario ficaria sem saber que elas existem.
    auto profiles = load_profiles(dir.location());
    OTTER_CHECK(profiles.has_value());
    OTTER_CHECK_EQ(profiles->size(), std::size_t{1});
    OTTER_CHECK(profiles->front().profile.password.empty());
}

OTTER_TEST(store_reports_a_malformed_data_sources_file) {
    const TempDir dir("malformed");
    dir.write("data-sources.json", R"({"connections": {)");

    // Aqui, ao contrario das credenciais, falhar e' certo: ler pela metade
    // faria a gravacao seguinte apagar as conexoes que nao foram lidas.
    auto profiles = load_profiles(dir.location());
    OTTER_CHECK(!profiles.has_value());
    OTTER_CHECK(profiles.error().message().find("data-sources") !=
                std::string::npos);
}
