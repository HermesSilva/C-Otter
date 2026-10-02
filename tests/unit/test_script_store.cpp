// C-Otter -- testes de ui/script_store: os scripts em `.script` e o indice.
//
// O que se protege: o usuario pediu que os scripts sejam gravados sozinhos e
// que o programa nao pergunte ao sair. Sem a pergunta, um campo do indice que
// e' gravado e nao e' lido -- ou um nome de arquivo que se repete -- perde
// trabalho em silencio.
#include "test_main.hpp"

#include "ui/script_store.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace otter;
using namespace otter::ui;

namespace {

namespace fs = std::filesystem;

// Na pasta temporaria, e apagada no fim (diretiva 14).
struct TempDir {
    fs::path path = fs::temp_directory_path() / "otter-test-scripts";
    TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    [[nodiscard]] std::string str() const { return path.string(); }
};

} // namespace

OTTER_TEST(script_session_round_trips_every_field) {
    const TempDir dir;

    ScriptSession session;
    session.active = "Script-1.sql";
    session.scripts.push_back({"Script.sql", "postgres-jdbc-1", "PostgreSQL 18", "",
                               "", false, true});
    session.scripts.push_back({"Script-1.sql", "postgres-jdbc-1", "PostgreSQL 18",
                               "ERP_TID", "Relatório de vendas", true, true});
    session.scripts.push_back({"C:\\fora\\consulta.sql", "", "localhost_1", "", "",
                               false, false});

    OTTER_CHECK(save_script_session(dir.str(), session));
    OTTER_CHECK(load_script_session(dir.str()) == session);
}

OTTER_TEST(script_session_missing_or_broken_is_empty) {
    const TempDir dir;
    OTTER_CHECK(load_script_session(dir.str()).scripts.empty());

    // Indice corrompido nao impede o programa de abrir.
    fs::create_directories(dir.path);
    std::ofstream(script_session_path(dir.str())) << "{ isto nao e' json";
    OTTER_CHECK(load_script_session(dir.str()).scripts.empty());
}

OTTER_TEST(script_names_follow_the_dbeaver_series) {
    const TempDir dir;
    OTTER_CHECK_EQ(unique_script_name(dir.str(), {}), std::string{"Script"});

    OTTER_CHECK(write_script(script_path(dir.str(), "Script.sql"), "select 1"));
    OTTER_CHECK_EQ(unique_script_name(dir.str(), {}), std::string{"Script-1"});

    // Uma aba aberta e ainda nao gravada tambem ocupa o nome: sem isto duas
    // abas novas se chamariam "Script-1" e a segunda gravaria por cima.
    OTTER_CHECK_EQ(unique_script_name(dir.str(), {"Script-1"}), std::string{"Script-2"});
}

OTTER_TEST(script_write_replaces_and_reads_back) {
    const TempDir dir;
    const std::string path = script_path(dir.str(), "Script.sql");

    OTTER_CHECK(write_script(path, "select 1"));
    OTTER_CHECK(write_script(path, "select 'ção';\n"));
    OTTER_CHECK_EQ(read_script(path).value_or(""), std::string{"select 'ção';\n"});

    // O temporario nao fica para tras, e a lista so' tem o script.
    const std::vector<std::string> names = list_scripts(dir.str());
    OTTER_CHECK_EQ(names.size(), std::size_t{1});
    OTTER_CHECK_EQ(names.front(), std::string{"Script.sql"});
    OTTER_CHECK(!fs::exists(path + ".tmp"));

    OTTER_CHECK(!read_script(script_path(dir.str(), "nao-existe.sql")).has_value());
}

OTTER_TEST(script_file_field_is_relative_only_inside_the_folder) {
    const TempDir dir;
    fs::create_directories(dir.path / "sub");

    const std::string inside = script_path(dir.str(), "Script-3.sql");
    OTTER_CHECK(is_stored_script(dir.str(), inside));
    OTTER_CHECK_EQ(script_file_field(dir.str(), inside), std::string{"Script-3.sql"});

    // Fora da pasta (ou numa subpasta) o caminho inteiro e' guardado.
    const std::string outside = (dir.path / "sub" / "a.sql").string();
    OTTER_CHECK(!is_stored_script(dir.str(), outside));
    OTTER_CHECK_EQ(script_file_field(dir.str(), outside), outside);
    OTTER_CHECK_EQ(script_path(dir.str(), outside), outside);
}

OTTER_TEST(script_file_name_refuses_what_windows_refuses) {
    OTTER_CHECK_EQ(script_file_name("Script-1"), std::string{"Script-1.sql"});
    OTTER_CHECK_EQ(script_file_name("vendas.SQL"), std::string{"vendas.SQL"});
    OTTER_CHECK_EQ(script_file_name("a/b:c*?"), std::string{"a_b_c__.sql"});
    OTTER_CHECK_EQ(script_file_name("  fim. "), std::string{"fim.sql"});
    OTTER_CHECK_EQ(script_file_name(""), std::string{"Script.sql"});
}
