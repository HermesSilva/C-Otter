// Rotulos de estado de transacao e de nivel de isolamento.
//
// Por que existe: `to_string(IsolationLevel)` nao e' texto de tela -- ele vai
// DENTRO de `SET TRANSACTION ISOLATION LEVEL <x>`. Trocar "READ COMMITTED"
// por "read committed" ou por "READ-COMMITTED" (a grafia do MySQL em
// @@transaction_isolation) produz um comando que o servidor recusa, e o erro
// so' aparece em tempo de execucao, na conexao do usuario.
//
// `to_string(TxnState)` alimenta a barra de status, que passou a mostrar o
// estado da transacao. Os dois tem `default` implicito devolvendo um valor
// seguro; sem teste, um enumerador novo passaria despercebido por ele.
#include "test_main.hpp"

#include "db/holt.hpp"

#include <string>

using namespace otter::db;

OTTER_TEST(txn_state_labels_cover_every_enumerator) {
    OTTER_CHECK_EQ(std::string(to_string(TxnState::idle)),   std::string{"idle"});
    OTTER_CHECK_EQ(std::string(to_string(TxnState::active)), std::string{"active"});
    OTTER_CHECK_EQ(std::string(to_string(TxnState::failed)), std::string{"failed"});
}

OTTER_TEST(isolation_labels_are_the_sql_spelling) {
    // Com espaco e em maiusculas: e' o que vai depois de
    // SET TRANSACTION ISOLATION LEVEL. Hifen aqui quebra o comando.
    OTTER_CHECK_EQ(std::string(to_string(IsolationLevel::read_uncommitted)),
                   std::string{"READ UNCOMMITTED"});
    OTTER_CHECK_EQ(std::string(to_string(IsolationLevel::read_committed)),
                   std::string{"READ COMMITTED"});
    OTTER_CHECK_EQ(std::string(to_string(IsolationLevel::repeatable_read)),
                   std::string{"REPEATABLE READ"});
    OTTER_CHECK_EQ(std::string(to_string(IsolationLevel::serializable)),
                   std::string{"SERIALIZABLE"});
}

OTTER_TEST(isolation_labels_have_no_hyphen_or_underscore) {
    // O nome do enumerador usa underscore e o MySQL reporta com hifen; nem um
    // nem outro pode vazar para o SQL gerado.
    constexpr IsolationLevel kAll[] = {
        IsolationLevel::read_uncommitted, IsolationLevel::read_committed,
        IsolationLevel::repeatable_read,  IsolationLevel::serializable,
    };

    for (const IsolationLevel level : kAll) {
        const std::string label(to_string(level));
        OTTER_CHECK(label.find('_') == std::string::npos);
        OTTER_CHECK(label.find('-') == std::string::npos);
        OTTER_CHECK(!label.empty());
    }
}
