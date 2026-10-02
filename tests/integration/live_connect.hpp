// Conexao e contagem das suites que falam com um PostgreSQL de verdade.
//
// A senha vem do perfil salvo no C-Otter quando PGPASSWORD nao esta' no
// ambiente -- ver o comentario em test_catalog_live.cpp: sem isso a suite so'
// rodava com a senha a mao, e ficava dias sem rodar.
#pragma once

#include "db/connection_store.hpp"
#include "db/drivers/postgres.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace live {

inline int failures = 0;
inline int checks   = 0;

inline void check(bool condition, const std::string& what) {
    ++checks;
    if (condition) {
        std::printf("  [ OK ] %s\n", what.c_str());
    } else {
        std::printf("  [FAIL] %s\n", what.c_str());
        ++failures;
    }
}

inline std::string env_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string(fallback);
}

// As suites ao vivo so' falam com um servidor DESTA maquina.
//
// Em 2026-10-01 a suite de catalogo conectou num servidor de PRODUCAO: ela
// pegava "o primeiro perfil PostgreSQL com senha salva", e depois que as
// senhas do pgAdmin foram importadas esse primeiro perfil era o de producao.
// Nada foi alterado la' (so' leituras de catalogo e um DROP TABLE IF EXISTS de
// uma tabela que nao existe), mas a suite nao tinha por que estar la'.
//
// A trava e' no HOST FINAL, depois de argumentos, ambiente e perfil: nao
// depende de como a configuracao foi montada.
inline bool is_local_host(const std::string& host) {
    return host == "localhost" || host == "127.0.0.1" || host == "::1";
}

inline void require_local(const otter::db::ConnConfig& config) {
    if (is_local_host(config.host)) return;
    std::fprintf(stderr,
                 "recusado: as suites ao vivo so' rodam contra localhost (host = %s)\n",
                 config.host.c_str());
    std::exit(2);
}

inline otter::db::ConnConfig config_from(int argc, char** argv) {
    otter::db::ConnConfig config;
    if (argc >= 6) {
        config.host     = argv[1];
        config.port     = static_cast<std::uint16_t>(std::atoi(argv[2]));
        config.database = argv[3];
        config.user     = argv[4];
        config.password = argv[5];
        require_local(config);
        return config;
    }

    config.host     = env_or("PGHOST", "localhost");
    config.port     = static_cast<std::uint16_t>(
                          std::atoi(env_or("PGPORT", "5432").c_str()));
    config.database = env_or("PGDATABASE", "ERP_TID");
    config.user     = env_or("PGUSER", "postgres");
    config.password = env_or("PGPASSWORD", "");

    if (config.password.empty()) {
        auto profiles = otter::db::load_profiles(otter::db::otter_store_location());
        if (profiles) {
            for (const auto& stored : *profiles) {
                if (stored.profile.driver_id != "postgresql") continue;
                if (stored.profile.password.empty()) continue;
                // So' perfil LOCAL: a suite cria e apaga objetos, e nao pode
                // cair num servidor de producao que esteja salvo ao lado.
                if (!is_local_host(stored.profile.host)) continue;
                const std::string database = config.database;
                config          = stored.profile.to_conn_config();
                config.database = database;
                std::printf("senha do perfil salvo \"%s\"\n",
                            stored.profile.effective_name().c_str());
                break;
            }
        }
    }
    require_local(config);
    return config;
}

} // namespace live
