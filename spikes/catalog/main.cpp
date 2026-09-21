// Spike: valida as consultas de catalogo contra um banco real.
//
// Mais barato que verificar pela UI: se uma query esta' errada, o erro aparece
// aqui com a mensagem do servidor.
//
//   spike_catalog <host> <port> <database> <user> <password> [tabela] [schema]
#include "db/catalog.hpp"
#include "db/drivers/postgres.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

void section(const char* title) {
    std::printf("\n=== %s ===\n", title);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
                     "uso: spike_catalog <host> <port> <db> <user> <pass> [tabela]\n");
        return 2;
    }

    otter::db::ConnConfig config;
    config.host     = argv[1];
    config.port     = static_cast<std::uint16_t>(std::atoi(argv[2]));
    config.database = argv[3];
    config.user     = argv[4];
    config.password = argv[5];

    const std::string table  = argc > 6 ? argv[6] : "";
    const std::string schema = argc > 7 ? argv[7] : "public";

    auto holt = otter::db::postgres_driver().connect(config);
    if (!holt) {
        std::fprintf(stderr, "conexão falhou: %s\n",
                     holt.error().to_string().c_str());
        return 1;
    }

    otter::db::PostgresCatalog catalog(**holt);
    std::printf("PostgreSQL %d.%d\n", catalog.version().major,
                catalog.version().minor);

    // --- Sequences -----------------------------------------------------------
    section("Sequences");
    if (auto sequences = catalog.load_sequences(schema)) {
        std::printf("%zu encontrada(s)\n", sequences->size());
        for (std::size_t i = 0; i < std::min<std::size_t>(sequences->size(), 5); ++i) {
            const auto& s = (*sequences)[i];
            std::printf("  %-36s last=%lld inc=%lld  %s\n", s.name.c_str(),
                        static_cast<long long>(s.last_value),
                        static_cast<long long>(s.increment),
                        s.owned_by.c_str());
        }
    } else {
        std::printf("FALHOU: %s\n", sequences.error().to_string().c_str());
    }

    // --- Tabelas, views e materialized views ---------------------------------
    //
    // A arvore separa as tres em pastas distintas, com filhos diferentes: a
    // view nao tem constraints, a materialized view nao tem triggers. Conferir
    // a classificacao aqui e' mais barato que descobrir na UI.
    section("Relacoes por tipo");
    if (auto relations = catalog.load_tables(schema)) {
        std::size_t tables_n = 0, views_n = 0, mviews_n = 0, other_n = 0;
        for (const auto& t : *relations) {
            switch (t.kind) {
                case otter::db::ObjKind::view:              ++views_n;  break;
                case otter::db::ObjKind::materialized_view: ++mviews_n; break;
                case otter::db::ObjKind::table:             ++tables_n; break;
                default:                                    ++other_n;  break;
            }
        }
        std::printf("%zu tabela(s), %zu view(s), %zu materialized view(s), "
                    "%zu outra(s)\n", tables_n, views_n, mviews_n, other_n);

        for (const auto& t : *relations) {
            if (!t.is_view()) continue;

            std::printf("  [%-17s] %-28s constraints=%s indices=%s triggers=%s\n",
                        std::string(otter::db::to_string(t.kind)).c_str(),
                        t.name.c_str(),
                        t.has_constraints() ? "sim" : "nao",
                        t.has_indexes()     ? "sim" : "nao",
                        t.has_triggers()    ? "sim" : "nao");

            if (auto def = catalog.load_view_definition(schema, t.name)) {
                // So' a primeira linha: o corpo inteiro polui a saida.
                const std::string& body = *def;
                const std::size_t eol = body.find('\n');
                std::printf("      def: %s%s\n",
                            body.substr(0, std::min(eol, std::size_t{68})).c_str(),
                            body.size() > 68 ? " ..." : "");
            } else {
                std::printf("      def FALHOU: %s\n",
                            def.error().to_string().c_str());
            }
        }
    } else {
        std::printf("FALHOU: %s\n", relations.error().to_string().c_str());
    }

    // --- Rotinas -------------------------------------------------------------
    section("Functions / Procedures");
    if (auto routines = catalog.load_routines(schema)) {
        std::printf("%zu encontrada(s)\n", routines->size());
        for (std::size_t i = 0; i < std::min<std::size_t>(routines->size(), 5); ++i) {
            const auto& r = (*routines)[i];
            std::printf("  [%s] %s(%s) -> %s  [%s]\n",
                        std::string(otter::db::to_string(r.kind)).c_str(),
                        r.name.c_str(), r.arguments.c_str(),
                        r.return_type.c_str(), r.language.c_str());
        }
    } else {
        std::printf("FALHOU: %s\n", routines.error().to_string().c_str());
    }

    // --- Por tabela ----------------------------------------------------------
    std::string target = table;
    if (target.empty()) {
        auto tables = catalog.load_tables(schema);
        if (tables && !tables->empty()) target = tables->front().name;
    }
    if (target.empty()) {
        std::printf("\nnenhuma tabela para inspecionar\n");
        return 0;
    }

    std::printf("\n--- tabela: %s ---\n", target.c_str());

    section("Constraints");
    if (auto constraints = catalog.load_constraints(schema, target)) {
        std::printf("%zu encontrada(s)\n", constraints->size());
        for (const auto& c : *constraints) {
            std::printf("  [%-11s] %-30s %s\n",
                        std::string(otter::db::to_string(c.kind)).c_str(),
                        c.name.c_str(), c.definition.c_str());
        }
    } else {
        std::printf("FALHOU: %s\n", constraints.error().to_string().c_str());
    }

    section("Indexes");
    if (auto indexes = catalog.load_indexes(schema, target)) {
        std::printf("%zu encontrado(s)\n", indexes->size());
        for (const auto& i : *indexes) {
            std::printf("  %-34s %-6s %-8s %s%s\n", i.name.c_str(),
                        i.method.c_str(), i.size_pretty.c_str(),
                        i.unique ? "UNIQUE " : "",
                        i.primary ? "PRIMARY" : "");
        }
    } else {
        std::printf("FALHOU: %s\n", indexes.error().to_string().c_str());
    }

    section("Foreign keys desta tabela");
    if (auto keys = catalog.load_table_foreign_keys(schema, target)) {
        std::printf("%zu encontrada(s)\n", keys->size());
        for (const auto& k : *keys) {
            std::printf("  %s.%s -> %s.%s  ON DELETE %s\n",
                        k.source_table.c_str(), k.source_column.c_str(),
                        k.target_table.c_str(), k.target_column.c_str(),
                        k.on_delete.c_str());
        }
    } else {
        std::printf("FALHOU: %s\n", keys.error().to_string().c_str());
    }

    section("References (quem aponta para esta tabela)");
    if (auto refs = catalog.load_references(schema, target)) {
        std::printf("%zu encontrada(s)\n", refs->size());
        for (const auto& r : *refs) {
            std::printf("  %s.%s -> %s.%s\n",
                        r.source_table.c_str(), r.source_column.c_str(),
                        r.target_table.c_str(), r.target_column.c_str());
        }
    } else {
        std::printf("FALHOU: %s\n", refs.error().to_string().c_str());
    }

    section("Triggers");
    if (auto triggers = catalog.load_triggers(schema, target)) {
        std::printf("%zu encontrado(s)\n", triggers->size());
        for (const auto& t : *triggers) {
            std::printf("  %-30s %s %s%s\n", t.name.c_str(),
                        t.timing.c_str(), t.events.c_str(),
                        t.enabled ? "" : "  [desabilitado]");
        }
    } else {
        std::printf("FALHOU: %s\n", triggers.error().to_string().c_str());
    }

    std::printf("\nOK\n");
    return 0;
}
