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
    "sqlite-123": {
      "provider": "sqlite", "driver": "sqlite_jdbc", "name": "legado",
      "configuration": {"host": "10.0.0.1", "port": "0"}
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
    OTTER_CHECK(stored.unsupported_reason.find("SQLite") != std::string::npos);
}

OTTER_TEST(store_maps_mysql_and_mariadb_to_the_mysql_driver) {
    // MariaDB fala o mesmo protocolo e e' servido pelo MESMO driver, mas o
    // DBeaver guarda "mariadb" como provider. Sem este mapeamento, metade das
    // conexoes importadas continuaria marcada como indisponivel.
    const TempDir dir("mysql-family");

    dir.write("data-sources.json", R"({
  "connections": {
    "mysql8-1": {
      "provider": "mysql", "driver": "mysql8", "name": "producao",
      "configuration": {"host": "10.0.0.1", "port": "3306"}
    },
    "mariadb-2": {
      "provider": "mariadb", "driver": "mariaDB", "name": "homologacao",
      "configuration": {"host": "10.0.0.2", "port": "3307"}
    }
  }
})");

    auto profiles = load_profiles(dir.location());
    OTTER_CHECK(profiles.has_value());
    OTTER_CHECK_EQ(profiles->size(), std::size_t{2});

    for (const StoredProfile& stored : *profiles) {
        OTTER_CHECK(stored.supported);
        OTTER_CHECK(stored.unsupported_reason.empty());
        OTTER_CHECK_EQ(stored.profile.driver_id, std::string{"mysql"});
    }
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

OTTER_TEST(store_keeps_the_password_of_a_profile_saved_without_an_id) {
    // O caminho da IMPORTACAO: o perfil vem sem id, porque o do DBeaver
    // pertence ao arquivo dele e nao e' reaproveitado. O id e' gerado na
    // gravacao, e precisa ser o MESMO nos dois arquivos -- a conexao vai para
    // data-sources.json sob ele, e a senha para credentials-config.json sob a
    // mesma chave.
    //
    // O teste acima nao pegava isto porque fixava o id a mao. Com o id
    // gerado numa variavel local e descartado, a senha ia para a chave "" e
    // nenhum perfil a encontrava de volta -- o sintoma na tela era
    // "Access denied (using password: NO)" logo apos importar.
    const TempDir dir("generated-id");

    StoredProfile stored;
    stored.id.clear();                    // como a importacao deixa
    stored.provider  = "mysql";
    stored.driver    = "mysql8";
    stored.supported = true;

    stored.profile.driver_id     = "mysql";
    stored.profile.name          = "importada";
    stored.profile.host          = "localhost";
    stored.profile.port          = 3306;
    stored.profile.user          = "root";
    stored.profile.password      = "senha-secreta";
    stored.profile.save_password = true;

    OTTER_CHECK(save_profiles(dir.location(), {stored}).has_value());

    auto back = load_profiles(dir.location());
    OTTER_CHECK(back.has_value());
    OTTER_CHECK_EQ(back->size(), std::size_t{1});

    const StoredProfile& read = back->front();
    OTTER_CHECK(!read.id.empty());        // ganhou um id
    OTTER_CHECK_EQ(read.profile.password, std::string{"senha-secreta"});
    OTTER_CHECK_EQ(read.profile.user, std::string{"root"});
    OTTER_CHECK_EQ(read.profile.driver_id, std::string{"mysql"});

    // E o id sobrevive a uma segunda gravacao, em vez de mudar a cada
    // salvamento -- se mudasse, a senha se perderia na volta seguinte.
    const std::string first_id = read.id;
    OTTER_CHECK(save_profiles(dir.location(), *back).has_value());

    auto again = load_profiles(dir.location());
    OTTER_CHECK(again.has_value() && again->size() == 1);
    OTTER_CHECK_EQ(again->front().id, first_id);
    OTTER_CHECK_EQ(again->front().profile.password, std::string{"senha-secreta"});
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

// --- O provider precisa acompanhar o driver --------------------------------

OTTER_TEST(store_maps_the_mysql_driver_back_to_the_mysql_provider) {
    // A conversao inversa de apply_driver. Sem ela, `remember_profile`
    // cravava "postgresql" em todo perfil novo -- e um perfil MySQL criado na
    // tela voltava da releitura como PostgreSQL.
    OTTER_CHECK_EQ(std::string(provider_for_driver("mysql").provider),
                   std::string{"mysql"});
    OTTER_CHECK_EQ(std::string(provider_for_driver("postgresql").provider),
                   std::string{"postgresql"});

    // Os nomes de driver sao os do DBeaver, para que um perfil criado aqui
    // abra la'.
    OTTER_CHECK_EQ(std::string(provider_for_driver("mysql").driver),
                   std::string{"mysql8"});
}

OTTER_TEST(store_falls_back_to_postgresql_for_an_unknown_driver) {
    // Provider vazio nao casaria com nada em apply_driver, e o perfil
    // voltaria marcado como "nao suportado" -- pior que um padrao errado.
    OTTER_CHECK_EQ(std::string(provider_for_driver("").provider),
                   std::string{"postgresql"});
    OTTER_CHECK_EQ(std::string(provider_for_driver("cassandra").provider),
                   std::string{"postgresql"});
}

OTTER_TEST(store_round_trips_a_mysql_profile_as_mysql) {
    // O defeito completo, de ponta a ponta: gravar um perfil MySQL e ler de
    // volta tem de devolver driver_id "mysql". Com o provider cravado em
    // "postgresql", a releitura devolvia "postgresql" -- e a conexao falava o
    // protocolo errado, terminando em "reading packet header: tempo esgotado".
    const TempDir dir("mysql-round-trip");

    const ProviderNames names = provider_for_driver("mysql");

    StoredProfile stored;
    stored.provider  = std::string(names.provider);
    stored.driver    = std::string(names.driver);
    stored.supported = true;

    stored.profile.driver_id = "mysql";
    stored.profile.host      = "localhost";
    stored.profile.port      = 3306;
    stored.profile.user      = "root";

    OTTER_CHECK(save_profiles(dir.location(), {stored}).has_value());

    auto back = load_profiles(dir.location());
    OTTER_CHECK(back.has_value());
    OTTER_CHECK_EQ(back->size(), std::size_t{1});
    OTTER_CHECK_EQ(back->front().profile.driver_id, std::string{"mysql"});
    OTTER_CHECK_EQ(back->front().profile.port, std::uint16_t{3306});
}

OTTER_TEST(store_keeps_two_profiles_that_differ_only_by_driver) {
    // Um PostgreSQL e um MySQL no mesmo host podem ter porta, banco e usuario
    // iguais. Casar perfis sem olhar o DRIVER fazia um sobrescrever o outro --
    // e o sobrevivente ficava com provider e driver do protocolo errado.
    const TempDir dir("two-drivers");

    StoredProfile pg;
    pg.provider  = "postgresql";
    pg.driver    = "postgres-jdbc";
    pg.supported = true;
    pg.profile.driver_id = "postgresql";
    pg.profile.host      = "localhost";
    pg.profile.port      = 5432;
    pg.profile.user      = "root";

    StoredProfile my;
    my.provider  = "mysql";
    my.driver    = "mysql8";
    my.supported = true;
    my.profile.driver_id = "mysql";
    my.profile.host      = "localhost";
    my.profile.port      = 5432;   // de proposito: so' o driver os distingue
    my.profile.user      = "root";

    OTTER_CHECK(save_profiles(dir.location(), {pg, my}).has_value());

    auto back = load_profiles(dir.location());
    OTTER_CHECK(back.has_value());
    OTTER_CHECK_EQ(back->size(), std::size_t{2});

    bool found_pg = false;
    bool found_my = false;
    for (const StoredProfile& s : *back) {
        if (s.profile.driver_id == "postgresql") found_pg = true;
        if (s.profile.driver_id == "mysql")      found_my = true;
    }
    OTTER_CHECK(found_pg);
    OTTER_CHECK(found_my);
}

// --- Preferencias do editor, por conexao ------------------------------------
//
// Por que existe: elas so' valem se sobreviverem ao reinicio. Um campo que o
// usuario ajusta e que volta ao padrao na proxima sessao e' o tipo de coisa
// que a diretriz 6 chama de campo que finge funcionar.

OTTER_TEST(store_round_trips_the_editor_options) {
    const TempDir dir("editor-options");

    StoredProfile stored;
    stored.id       = "postgres-editor";
    stored.provider = "postgresql";
    stored.driver   = "postgres-jdbc";
    stored.supported = true;
    stored.profile.host = "localhost";

    // Todos DIFERENTES do padrao: gravar so' o que difere e' a regra, e um
    // teste com valores padrao nao provaria nada.
    stored.profile.editor.keyword_case         = 2;     // minusculas
    stored.profile.editor.indent_width         = 2;
    stored.profile.editor.river_style          = false;
    stored.profile.editor.wrap_select_after    = 7;
    stored.profile.editor.complete_on_typing   = false;
    stored.profile.editor.complete_in_comments = true;
    stored.profile.editor.complete_in_strings  = true;
    stored.profile.editor.auto_insert_single   = true;
    stored.profile.editor.complete_delay_ms    = 500;
    stored.profile.editor.tab_size             = 8;
    stored.profile.editor.show_line_numbers    = false;
    stored.profile.editor.auto_indent          = false;
    stored.profile.editor.show_matching_brackets = false;
    stored.profile.editor.show_whitespace      = true;
    stored.profile.editor.page_size            = 1000;
    stored.profile.editor.stop_script_on_error = false;
    stored.profile.editor.null_text            = "NULO";
    stored.profile.editor.align_numbers_right  = false;
    stored.profile.editor.word_wrap            = true;
    stored.profile.editor.code_folding         = false;
    stored.profile.editor.export_format        = 3;
    stored.profile.editor.export_write_header  = false;
    stored.profile.editor.export_null_text     = "NULL";

    OTTER_CHECK(save_profiles(dir.location(), {stored}).has_value());

    auto back = load_profiles(dir.location());
    OTTER_CHECK(back.has_value());
    OTTER_CHECK_EQ(back->size(), std::size_t{1});

    const EditorOptions& e = back->front().profile.editor;
    OTTER_CHECK_EQ(e.keyword_case, 2);
    OTTER_CHECK_EQ(e.indent_width, 2);
    OTTER_CHECK(!e.river_style);
    OTTER_CHECK_EQ(e.wrap_select_after, std::size_t{7});
    OTTER_CHECK(!e.complete_on_typing);
    OTTER_CHECK(e.complete_in_comments);
    OTTER_CHECK(e.complete_in_strings);
    OTTER_CHECK(e.auto_insert_single);
    OTTER_CHECK_EQ(e.complete_delay_ms, 500);
    OTTER_CHECK_EQ(e.tab_size, 8);
    OTTER_CHECK(!e.show_line_numbers);
    OTTER_CHECK(!e.auto_indent);
    OTTER_CHECK(!e.show_matching_brackets);
    OTTER_CHECK(e.show_whitespace);
    OTTER_CHECK_EQ(e.page_size, 1000);
    OTTER_CHECK(!e.stop_script_on_error);
    OTTER_CHECK_EQ(e.null_text, std::string{"NULO"});
    OTTER_CHECK(!e.align_numbers_right);
    OTTER_CHECK(e.word_wrap);
    OTTER_CHECK(!e.code_folding);
    OTTER_CHECK_EQ(e.export_format, 3);
    OTTER_CHECK(!e.export_write_header);
    OTTER_CHECK_EQ(e.export_null_text, std::string{"NULL"});
}

OTTER_TEST(store_uses_editor_defaults_when_the_key_is_absent) {
    // Um perfil importado do DBeaver nao tem "otter-editor". Ele precisa
    // carregar com os padroes, nao com zeros -- indent_width zero produziria
    // SQL formatado sem indentacao nenhuma.
    const TempDir dir("editor-absent");

    StoredProfile stored;
    stored.id       = "postgres-plain";
    stored.provider = "postgresql";
    stored.driver   = "postgres-jdbc";
    stored.supported = true;
    stored.profile.host = "localhost";

    OTTER_CHECK(save_profiles(dir.location(), {stored}).has_value());

    auto back = load_profiles(dir.location());
    OTTER_CHECK(back.has_value());

    const EditorOptions defaults;
    const EditorOptions& e = back->front().profile.editor;
    OTTER_CHECK_EQ(e.keyword_case, defaults.keyword_case);
    OTTER_CHECK_EQ(e.indent_width, defaults.indent_width);
    OTTER_CHECK_EQ(e.river_style, defaults.river_style);
    OTTER_CHECK_EQ(e.complete_delay_ms, defaults.complete_delay_ms);
    OTTER_CHECK_EQ(e.tab_size, defaults.tab_size);
    OTTER_CHECK_EQ(e.show_line_numbers, defaults.show_line_numbers);
    OTTER_CHECK_EQ(e.auto_indent, defaults.auto_indent);
    OTTER_CHECK_EQ(e.page_size, defaults.page_size);
    OTTER_CHECK_EQ(e.stop_script_on_error, defaults.stop_script_on_error);
    OTTER_CHECK_EQ(e.null_text, defaults.null_text);
    OTTER_CHECK_EQ(e.align_numbers_right, defaults.align_numbers_right);
    OTTER_CHECK_EQ(e.export_format, defaults.export_format);
}

// --- Propriedades do driver chegam ao ConnConfig ----------------------------
//
// Por que existe: elas iam para disco e paravam ali. O ConnConfig tinha um
// campo `options` que NINGUEM preenchia e NINGUEM lia -- o usuario digitava
// "search_path=vendas", conectava, e a sessao abria com o search_path
// padrao. O sintoma nao aparece em lugar nenhum: a conexao funciona, so' que
// sem o que foi pedido.

OTTER_TEST(profile_carries_driver_properties_to_the_connection) {
    ConnectionProfile profile;
    profile.driver_id = "postgresql";
    profile.host      = "localhost";
    profile.driver_properties["search_path"]       = "vendas,public";
    profile.driver_properties["statement_timeout"] = "5000";

    const ConnConfig config = profile.to_conn_config();

    OTTER_CHECK_EQ(config.driver_properties.size(), std::size_t{2});
    OTTER_CHECK_EQ(config.driver_properties.at("search_path"),
                   std::string{"vendas,public"});
    OTTER_CHECK_EQ(config.driver_properties.at("statement_timeout"),
                   std::string{"5000"});
}

OTTER_TEST(store_round_trips_driver_properties) {
    // Ja' eram gravadas; o teste fixa isso agora que elas TEM efeito --
    // perde-las no disco passou a ser um defeito visivel.
    const TempDir dir("driver-props");

    StoredProfile stored;
    stored.id        = "postgres-props";
    stored.provider  = "postgresql";
    stored.driver    = "postgres-jdbc";
    stored.supported = true;
    stored.profile.host = "localhost";
    stored.profile.driver_properties["search_path"] = "vendas";
    stored.profile.driver_properties["TimeZone"]    = "America/Sao_Paulo";

    OTTER_CHECK(save_profiles(dir.location(), {stored}).has_value());

    auto back = load_profiles(dir.location());
    OTTER_CHECK(back.has_value());

    const auto& props = back->front().profile.driver_properties;
    OTTER_CHECK_EQ(props.size(), std::size_t{2});
    OTTER_CHECK_EQ(props.at("search_path"), std::string{"vendas"});
    OTTER_CHECK_EQ(props.at("TimeZone"), std::string{"America/Sao_Paulo"});
}

// --- Nivel de isolamento ----------------------------------------------------
//
// Por que existe: -1 significa "nao mexer", e confundi-lo com 0
// (read_uncommitted) trocaria o isolamento de TODA conexao que nunca
// escolheu um -- silenciosamente, e para o nivel mais frouxo que existe.

OTTER_TEST(profile_isolation_default_does_not_touch_the_server) {
    ConnectionProfile profile;
    profile.host = "localhost";
    // -1 e' o padrao; nada foi escolhido.

    OTTER_CHECK(!profile.to_conn_config().isolation_level.has_value());
}

OTTER_TEST(profile_carries_the_chosen_isolation_level) {
    ConnectionProfile profile;
    profile.host = "localhost";
    profile.isolation_level = 3;   // serializable

    const auto level = profile.to_conn_config().isolation_level;
    OTTER_CHECK(level.has_value());
    OTTER_CHECK(*level == IsolationLevel::serializable);
}

OTTER_TEST(profile_ignores_an_out_of_range_isolation_level) {
    // Perfil corrompido ou de uma versao futura: cai no padrao do servidor,
    // em vez de converter para um nivel arbitrario.
    ConnectionProfile profile;
    profile.host = "localhost";

    profile.isolation_level = 99;
    OTTER_CHECK(!profile.to_conn_config().isolation_level.has_value());

    profile.isolation_level = -7;
    OTTER_CHECK(!profile.to_conn_config().isolation_level.has_value());
}

OTTER_TEST(store_round_trips_the_isolation_level) {
    const TempDir dir("isolation");

    StoredProfile stored;
    stored.id        = "postgres-iso";
    stored.provider  = "postgresql";
    stored.driver    = "postgres-jdbc";
    stored.supported = true;
    stored.profile.host = "localhost";
    stored.profile.isolation_level = 2;   // repeatable read

    OTTER_CHECK(save_profiles(dir.location(), {stored}).has_value());

    auto back = load_profiles(dir.location());
    OTTER_CHECK(back.has_value());
    OTTER_CHECK_EQ(back->front().profile.isolation_level, 2);
}

OTTER_TEST(store_reads_no_isolation_as_server_default) {
    // Perfil importado do DBeaver nao tem "otter-isolation". Precisa
    // carregar como -1, nao como 0 -- que seria read_uncommitted.
    const TempDir dir("isolation-absent");

    StoredProfile stored;
    stored.id        = "postgres-plain-iso";
    stored.provider  = "postgresql";
    stored.driver    = "postgres-jdbc";
    stored.supported = true;
    stored.profile.host = "localhost";

    OTTER_CHECK(save_profiles(dir.location(), {stored}).has_value());

    auto back = load_profiles(dir.location());
    OTTER_CHECK(back.has_value());
    OTTER_CHECK_EQ(back->front().profile.isolation_level, -1);
}
