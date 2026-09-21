// Prova que o modo SSL decide certo o que exigir e o que verificar.
//
// Por que existe: o aperto de mao TLS em si nao e' testavel em unidade -- ele
// depende de um servidor. Mas a DECISAO que o precede e', e errar nela e' o
// tipo de defeito que nao aparece em teste nenhum: uma conexao que o usuario
// acredita cifrada e nao esta'.
//
// A verificacao contra servidor de verdade esta' em spikes/dbeaver_import/
// tls_live.cpp, que confirma que `require` negocia e `verify-full` recusa um
// certificado autoassinado.
#include "test_main.hpp"

#include "db/connection_config.hpp"
#include "db/holt.hpp"

using namespace otter;

OTTER_TEST(ssl_mode_disable_does_not_enable_tls) {
    db::ConnConfig config;
    config.ssl_mode = db::SslMode::disable;

    OTTER_CHECK(!config.ssl_enabled());
    OTTER_CHECK(!config.ssl_verifies_certificate());
}

OTTER_TEST(ssl_mode_allow_and_prefer_do_not_require_tls) {
    // No libpq, `allow` e `prefer` significam "tenta cifrar, aceita em claro".
    // Trata-los como exigencia faria o C-Otter recusar conexoes que o DBeaver
    // aceita; trata-los como garantia seria mentir para o usuario. Nenhum dos
    // dois exige -- e a UI avisa.
    for (const db::SslMode mode : {db::SslMode::allow, db::SslMode::prefer}) {
        db::ConnConfig config;
        config.ssl_mode = mode;

        OTTER_CHECK(!config.ssl_enabled());
        OTTER_CHECK(!config.ssl_verifies_certificate());
    }
}

OTTER_TEST(ssl_mode_require_encrypts_without_verifying) {
    db::ConnConfig config;
    config.ssl_mode = db::SslMode::require;

    OTTER_CHECK(config.ssl_enabled());
    // Verificar aqui quebraria todo servidor de desenvolvimento com
    // certificado autoassinado -- que e' onde `require` mais aparece.
    OTTER_CHECK(!config.ssl_verifies_certificate());
}

OTTER_TEST(ssl_mode_verify_modes_encrypt_and_verify) {
    for (const db::SslMode mode :
         {db::SslMode::verify_ca, db::SslMode::verify_full}) {
        db::ConnConfig config;
        config.ssl_mode = mode;

        OTTER_CHECK(config.ssl_enabled());
        OTTER_CHECK(config.ssl_verifies_certificate());
    }
}

OTTER_TEST(ssl_mode_round_trips_through_text) {
    // O perfil e' gravado em JSON com o nome do modo. Um round-trip que perde
    // `verify-full` e volta como `disable` DESLIGARIA o TLS de uma conexao
    // configurada para exigi-lo, em silencio.
    for (const db::SslMode mode :
         {db::SslMode::disable, db::SslMode::allow, db::SslMode::prefer,
          db::SslMode::require, db::SslMode::verify_ca,
          db::SslMode::verify_full}) {
        OTTER_CHECK(db::ssl_mode_from_string(db::to_string(mode)) == mode);
    }
}

OTTER_TEST(ssl_mode_unknown_text_falls_back_to_disable) {
    // Um modo gravado por uma versao futura nao deve ser promovido a
    // exigencia: a conexao falharia sem que nada na tela explicasse por que.
    OTTER_CHECK(db::ssl_mode_from_string("verify-quantum") ==
                db::SslMode::disable);
    OTTER_CHECK(db::ssl_mode_from_string("") == db::SslMode::disable);
}

OTTER_TEST(profile_ssl_disabled_checkbox_wins_over_saved_mode) {
    // A caixa "usar SSL" desmarcada com `verify-full` gravado embaixo e' o
    // estado normal de quem experimentou e desligou. Se o modo vencesse, a
    // conexao exigiria TLS com a caixa desmarcada na tela.
    db::ConnectionProfile profile;
    profile.ssl.enabled = false;
    profile.ssl.mode    = db::SslMode::verify_full;

    OTTER_CHECK(!profile.to_conn_config().ssl_enabled());

    profile.ssl.enabled = true;
    OTTER_CHECK(profile.to_conn_config().ssl_enabled());
    OTTER_CHECK(profile.to_conn_config().ssl_verifies_certificate());
}
