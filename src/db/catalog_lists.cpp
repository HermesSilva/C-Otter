// C-Otter -- db/catalog_lists.cpp
//
// As pastas do PostgreSQL que so' LISTAM: bancos, roles, extensoes,
// tablespaces, encodings, politicas, dependencias... (ADR 0018).
//
// As consultas seguem as do DBeaver (`PostgreDatabase.java`,
// `PostgreSchema.java`, `PostgreDependency.java`), reduzidas ao que a arvore
// mostra: nome, detalhe e descricao. Cada uma e' exercida contra um servidor
// de verdade em tests/integration/test_catalog_live.cpp -- uma consulta de
// catalogo que compila nao e' uma consulta que o servidor aceita.
#include "db/catalog.hpp"

#include <cstdio>
#include <iterator>
#include <string>

namespace otter::db {
namespace {

std::string lit(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('\'');
    for (char c : text) {
        if (c == '\'') out.push_back('\'');
        out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

std::string ident(std::string_view name) {
    std::string out;
    out.reserve(name.size() + 2);
    out.push_back('"');
    for (char c : name) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

// 'schema.relacao' como literal pronto para ::regclass.
std::string regclass(std::string_view schema, std::string_view relation) {
    return lit(ident(schema) + "." + ident(relation)) + "::regclass";
}

// So' digitos: os ids de job vem do proprio catalogo, mas entram no SQL sem
// aspas -- e nada que nao seja numero pode passar.
bool all_digits(std::string_view text) {
    if (text.empty()) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// O que depende de `target` (um oid). E' a consulta de
// PostgreDependency.readDependencies(dependents = true) do DBeaver, com o
// codigo de uma letra do tipo trocado pelo nome por extenso.
//
// `policies`: junta pg_policy (9.5+). Sem ela, a politica de RLS de uma tabela
// aparecia como uma linha SEM NOME -- o pg_depend aponta para um catalogo que
// a consulta original nao conhece. Para os demais catalogos nao previstos, o
// nome do catalogo (`classid`) entra no lugar: "pg_publication_rel" diz mais
// que uma linha em branco.
std::string dependents_sql(const std::string& target_oid, bool policies) {
    const std::string policy_name = policies ? "pol.polname, " : "";
    const std::string policy_kind =
        policies ? "    WHEN pol.oid IS NOT NULL THEN 'policy'" : "";
    const std::string policy_join =
        policies ? "  LEFT JOIN pg_policy pol ON dep.objid = pol.oid"
                   "   AND dep.classid = 'pg_policy'::regclass"
                 : "";

    return
        "SELECT DISTINCT"
        "  COALESCE("
        "    CASE WHEN cl.relname IS NOT NULL AND att.attname IS NOT NULL"
        "         THEN cl.relname || '.' || att.attname END,"
        "    cl.relname, co.conname, pr.proname, tg.tgname, ty.typname,"
        "    la.lanname, rw.rulename, ns.nspname, attr.attname, " + policy_name +
        "    dep.classid::regclass::text) AS refname,"
        "  CASE"
        "    WHEN cl.relkind = 'r' THEN 'table'"
        "    WHEN cl.relkind = 'v' THEN 'view'"
        "    WHEN cl.relkind = 'm' THEN 'materialized view'"
        "    WHEN cl.relkind = 'i' THEN 'index'"
        "    WHEN cl.relkind = 'I' THEN 'partitioned index'"
        "    WHEN cl.relkind = 'S' THEN 'sequence'"
        "    WHEN cl.relkind = 'f' THEN 'foreign table'"
        "    WHEN cl.relkind = 'p' THEN 'partitioned table'"
        "    WHEN cl.relkind = 'c' THEN 'composite type'"
        "    WHEN cl.relkind IS NOT NULL THEN 'relation'"
        "    WHEN tg.oid IS NOT NULL THEN 'trigger'"
        "    WHEN ty.oid IS NOT NULL THEN 'type'"
        "    WHEN ns.oid IS NOT NULL THEN 'schema'"
        "    WHEN pr.oid IS NOT NULL THEN 'function'"
        "    WHEN la.oid IS NOT NULL THEN 'language'"
        "    WHEN rw.oid IS NOT NULL THEN 'rule'"
        "    WHEN co.oid IS NOT NULL THEN 'constraint'"
        "    WHEN ad.oid IS NOT NULL THEN 'default'" + policy_kind +
        "    ELSE 'object' END AS kind,"
        "  COALESCE(nsc.nspname, nso.nspname, nsp.nspname, nst.nspname,"
        "           nsrw.nspname, tgrn.nspname, '') AS nspname,"
        "  COALESCE(coc.relname, clrw.relname, tgr.relname, '') AS ownertable,"
        "  COALESCE(pg_get_expr(ad.adbin, ad.adrelid), '') AS adefval,"
        "  dep.deptype::text"
        "  FROM pg_depend dep"
        "  LEFT JOIN pg_class cl ON dep.objid = cl.oid"
        "  LEFT JOIN pg_attribute att"
        "         ON dep.objid = att.attrelid AND dep.objsubid = att.attnum"
        "  LEFT JOIN pg_namespace nsc ON cl.relnamespace = nsc.oid"
        "  LEFT JOIN pg_proc pr ON dep.objid = pr.oid"
        "  LEFT JOIN pg_namespace nsp ON pr.pronamespace = nsp.oid"
        "  LEFT JOIN pg_trigger tg ON dep.objid = tg.oid"
        "  LEFT JOIN pg_class tgr ON tg.tgrelid = tgr.oid"
        "  LEFT JOIN pg_namespace tgrn ON tgr.relnamespace = tgrn.oid"
        "  LEFT JOIN pg_type ty ON dep.objid = ty.oid"
        "  LEFT JOIN pg_namespace nst ON ty.typnamespace = nst.oid"
        "  LEFT JOIN pg_constraint co ON dep.objid = co.oid"
        "  LEFT JOIN pg_class coc ON co.conrelid = coc.oid"
        "  LEFT JOIN pg_namespace nso ON co.connamespace = nso.oid"
        "  LEFT JOIN pg_rewrite rw ON dep.objid = rw.oid"
        "  LEFT JOIN pg_class clrw ON clrw.oid = rw.ev_class"
        "  LEFT JOIN pg_namespace nsrw ON clrw.relnamespace = nsrw.oid"
        "  LEFT JOIN pg_language la ON dep.objid = la.oid"
        "  LEFT JOIN pg_namespace ns ON dep.objid = ns.oid"
        "  LEFT JOIN pg_attrdef ad ON ad.oid = dep.objid" + policy_join +
        "  LEFT JOIN pg_attribute attr"
        "         ON attr.attrelid = ad.adrelid AND attr.attnum = ad.adnum"
        " WHERE dep.refobjid = " + target_oid +
        " ORDER BY kind, refname";
}

// Subconsulta que acha o oid de uma rotina pela assinatura JA' formatada pelo
// servidor -- o mesmo criterio de load_routine_definition, e pelo mesmo
// motivo: remontar 'schema.nome(args)'::regprocedure falha com os nomes dos
// parametros no meio.
std::string routine_oid(std::string_view schema, std::string_view name,
                        std::string_view arguments) {
    return
        "(SELECT p.oid FROM pg_proc p"
        "   JOIN pg_namespace n ON n.oid = p.pronamespace"
        "  WHERE n.nspname = " + lit(schema) +
        "    AND p.proname = " + lit(name) +
        "    AND pg_get_function_arguments(p.oid) = " + lit(arguments) +
        "  LIMIT 1)";
}

} // namespace

Result<std::vector<DatabaseMeta>> PostgresCatalog::load_databases(
    bool templates, bool unavailable) {
    // O tamanho so' e' pedido onde ha' CONNECT: pg_database_size de um banco
    // sem esse privilegio da' ERRO, e um erro numa linha derruba a lista
    // inteira. O CASE avalia a funcao so' quando a condicao passa.
    std::string sql =
        "SELECT db.datname,"
        "       pg_get_userbyid(db.datdba),"
        "       pg_encoding_to_char(db.encoding),"
        "       COALESCE(shobj_description(db.oid, 'pg_database'), ''),"
        "       CASE WHEN has_database_privilege(db.oid, 'CONNECT')"
        "            THEN pg_database_size(db.oid) ELSE -1 END,"
        "       db.datistemplate,"
        "       db.datallowconn"
        "  FROM pg_database db"
        " WHERE true";
    if (!unavailable) sql += " AND db.datallowconn";
    if (!templates)   sql += " AND NOT db.datistemplate";
    sql += " ORDER BY db.datname";

    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(sql));

    std::vector<DatabaseMeta> databases;
    databases.reserve(rs.row_count());

    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        DatabaseMeta db;
        db.name     = std::string(rs.text(r, 0));
        db.owner    = std::string(rs.text(r, 1));
        db.encoding = std::string(rs.text(r, 2));
        db.comment  = std::string(rs.text(r, 3));

        std::int64_t size = -1;
        const std::string_view text = rs.text(r, 4);
        if (!text.empty() && text[0] != '-') {
            size = 0;
            for (char c : text) {
                if (c < '0' || c > '9') break;
                size = size * 10 + (c - '0');
            }
        }
        db.size_bytes    = size;
        db.size_pretty   = format_size(size);
        db.is_template   = rs.text(r, 5) == "t";
        db.allow_connect = rs.text(r, 6) == "t";
        databases.push_back(std::move(db));
    }
    return databases;
}

std::string format_size(std::int64_t bytes) {
    if (bytes < 0) return {};

    // Uma casa decimal abaixo de 10, nenhuma acima -- "7.7M", "285M". E' o
    // formato da coluna de tamanho do DBeaver, que quem vem de la' ja' le'.
    static constexpr const char* kUnits[] = {"", "K", "M", "G", "T"};
    double      value = static_cast<double>(bytes);
    std::size_t unit  = 0;
    while (value >= 1024.0 && unit + 1 < std::size(kUnits)) {
        value /= 1024.0;
        ++unit;
    }

    char buffer[32];
    if (unit == 0) {
        std::snprintf(buffer, sizeof buffer, "%lld", static_cast<long long>(bytes));
    } else if (value < 10.0) {
        std::snprintf(buffer, sizeof buffer, "%.1f%s", value, kUnits[unit]);
    } else {
        std::snprintf(buffer, sizeof buffer, "%.0f%s", value, kUnits[unit]);
    }
    return buffer;
}

Result<std::vector<CatalogItem>> PostgresCatalog::load_list(
    CatalogList list, std::string_view a, std::string_view b,
    std::string_view c) {
    // Toda consulta devolve as mesmas quatro colunas -- nome, detalhe,
    // descricao e um booleano --, para que a leitura abaixo seja uma so'.
    std::string sql;

    switch (list) {
        case CatalogList::schema_indexes:
            sql =
                "SELECT i.relname,"
                "       t.relname || '  ' || am.amname,"
                "       pg_get_indexdef(i.oid),"
                "       ix.indisunique"
                "  FROM pg_index ix"
                "  JOIN pg_class i ON i.oid = ix.indexrelid"
                "  JOIN pg_class t ON t.oid = ix.indrelid"
                "  JOIN pg_namespace n ON n.oid = i.relnamespace"
                "  JOIN pg_am am ON am.oid = i.relam"
                " WHERE n.nspname = " + lit(a) +
                " ORDER BY t.relname, i.relname";
            break;

        case CatalogList::aggregates:
            sql =
                "SELECT p.proname || '(' ||"
                "         pg_get_function_identity_arguments(p.oid) || ')',"
                "       format_type(p.prorettype, NULL),"
                "       COALESCE(obj_description(p.oid, 'pg_proc'), ''),"
                "       false"
                "  FROM pg_aggregate ag"
                "  JOIN pg_proc p ON p.oid = ag.aggfnoid"
                "  JOIN pg_namespace n ON n.oid = p.pronamespace"
                " WHERE n.nspname = " + lit(a) +
                " ORDER BY 1";
            break;

        case CatalogList::dependencies:
        case CatalogList::routine_dependencies: {
            const std::string target =
                list == CatalogList::dependencies
                    ? regclass(a, b) + "::oid"
                    : routine_oid(a, b, c);

            OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(dependents_sql(target, version_.at_least(9, 5))));

            std::vector<CatalogItem> items;
            items.reserve(rs.row_count());
            for (std::size_t r = 0; r < rs.row_count(); ++r) {
                CatalogItem item;
                item.name   = std::string(rs.text(r, 0));
                item.detail = std::string(rs.text(r, 1));

                const std::string_view schema = rs.text(r, 2);
                const std::string_view owner  = rs.text(r, 3);
                const std::string_view expr   = rs.text(r, 4);

                // A dependencia interna e' a que o servidor cria sozinho (o
                // tipo-linha da tabela, o indice da PK). Marcada porque nao
                // impede um DROP -- cai junto.
                item.flag = rs.text(r, 5) == "i" || rs.text(r, 5) == "a";

                if (!schema.empty()) item.tooltip += "schema: " + std::string(schema);
                if (!owner.empty()) {
                    if (!item.tooltip.empty()) item.tooltip += "\n";
                    item.tooltip += "on: " + std::string(owner);
                }
                if (!expr.empty()) {
                    if (!item.tooltip.empty()) item.tooltip += "\n";
                    item.tooltip += std::string(expr);
                }
                items.push_back(std::move(item));
            }
            return items;
        }

        case CatalogList::rules:
            sql =
                "SELECT r.rulename,"
                "       CASE r.ev_type WHEN '1' THEN 'SELECT' WHEN '2' THEN 'UPDATE'"
                "                      WHEN '3' THEN 'INSERT' WHEN '4' THEN 'DELETE'"
                "                      ELSE '' END ||"
                "       CASE WHEN r.is_instead THEN '  INSTEAD' ELSE '' END,"
                "       pg_get_ruledef(r.oid, true),"
                "       r.ev_enabled <> 'D'"
                "  FROM pg_rewrite r"
                " WHERE r.ev_class = " + regclass(a, b) +
                "   AND r.rulename <> '_RETURN'"
                " ORDER BY r.rulename";
            break;

        case CatalogList::policies:
            // Row Level Security existe desde o 9.5 (ADR 0010).
            if (!version_.at_least(9, 5)) return std::vector<CatalogItem>{};
            sql =
                "SELECT p.polname,"
                "       CASE p.polcmd WHEN 'r' THEN 'SELECT' WHEN 'a' THEN 'INSERT'"
                "                     WHEN 'w' THEN 'UPDATE' WHEN 'd' THEN 'DELETE'"
                "                     ELSE 'ALL' END,"
                "       'TO ' || COALESCE((SELECT string_agg(r.rolname, ', ')"
                "                            FROM pg_roles r"
                "                           WHERE r.oid = ANY (p.polroles)), 'PUBLIC') ||"
                "       COALESCE(E'\\nUSING (' ||"
                "                pg_get_expr(p.polqual, p.polrelid) || ')', '') ||"
                "       COALESCE(E'\\nWITH CHECK (' ||"
                "                pg_get_expr(p.polwithcheck, p.polrelid) || ')', ''),"
                "       true"
                "  FROM pg_policy p"
                " WHERE p.polrelid = " + regclass(a, b) +
                " ORDER BY p.polname";
            break;

        case CatalogList::child_tables:
            // Heranca (INHERITS), nao particao: a particao tem pasta propria
            // e misturar as duas mostraria a mesma tabela em dois lugares.
            sql =
                "SELECT c.relname,"
                "       n.nspname,"
                "       COALESCE(obj_description(c.oid, 'pg_class'), ''),"
                "       false"
                "  FROM pg_inherits i"
                "  JOIN pg_class c ON c.oid = i.inhrelid"
                "  JOIN pg_namespace n ON n.oid = c.relnamespace"
                "  JOIN pg_class parent ON parent.oid = i.inhparent"
                " WHERE i.inhparent = " + regclass(a, b) +
                "   AND parent.relkind <> 'p'"
                " ORDER BY c.relname";
            break;

        case CatalogList::routine_parameters:
            // proallargtypes so' e' preenchido quando ha' parametro de saida;
            // sem ele, a lista e' proargtypes e todos sao de entrada.
            sql =
                "SELECT COALESCE(NULLIF(p.proargnames[s.i], ''), '$' || s.i),"
                "       format_type(COALESCE(p.proallargtypes[s.i],"
                "                            p.proargtypes[s.i - 1]), NULL),"
                "       CASE COALESCE(p.proargmodes[s.i], 'i')"
                "            WHEN 'i' THEN 'IN' WHEN 'o' THEN 'OUT'"
                "            WHEN 'b' THEN 'INOUT' WHEN 'v' THEN 'VARIADIC'"
                "            WHEN 't' THEN 'TABLE' ELSE '' END,"
                "       COALESCE(p.proargmodes[s.i], 'i') IN ('o', 't')"
                "  FROM pg_proc p,"
                "       generate_series(1, COALESCE("
                "           array_length(p.proallargtypes, 1), p.pronargs)) AS s(i)"
                " WHERE p.oid = " + routine_oid(a, b, c) +
                " ORDER BY s.i";
            break;

        case CatalogList::event_triggers:
            // Event triggers existem desde o 9.3.
            if (!version_.at_least(9, 3)) return std::vector<CatalogItem>{};
            sql =
                "SELECT e.evtname,"
                "       e.evtevent,"
                "       'EXECUTE FUNCTION ' || e.evtfoid::regproc::text ||"
                "       COALESCE(E'\\n' || d.description, ''),"
                "       e.evtenabled <> 'D'"
                "  FROM pg_event_trigger e"
                "  LEFT JOIN pg_description d"
                "         ON d.objoid = e.oid"
                "        AND d.classoid = 'pg_event_trigger'::regclass"
                " ORDER BY e.evtname";
            break;

        case CatalogList::extensions:
            sql =
                "SELECT e.extname,"
                "       e.extversion || '  ' || n.nspname,"
                "       COALESCE(d.description, ''),"
                "       true"
                "  FROM pg_extension e"
                "  JOIN pg_namespace n ON n.oid = e.extnamespace"
                "  LEFT JOIN pg_description d"
                "         ON d.objoid = e.oid"
                "        AND d.classoid = 'pg_extension'::regclass"
                " ORDER BY e.extname";
            break;

        case CatalogList::tablespaces:
            // pg_tablespace_location devolve vazio para os dois embutidos
            // (pg_default, pg_global), que moram no diretorio de dados.
            sql =
                "SELECT t.spcname,"
                "       pg_get_userbyid(t.spcowner),"
                "       COALESCE(NULLIF(pg_tablespace_location(t.oid), ''),"
                "                '(data directory)'),"
                "       false"
                "  FROM pg_tablespace t"
                " ORDER BY t.spcname";
            break;

        case CatalogList::foreign_data_wrappers:
            sql =
                "SELECT w.fdwname,"
                "       COALESCE(NULLIF(w.fdwhandler::regproc::text, '-'), ''),"
                "       COALESCE(array_to_string(w.fdwoptions, ', '), ''),"
                "       false"
                "  FROM pg_foreign_data_wrapper w"
                " ORDER BY w.fdwname";
            break;

        case CatalogList::foreign_servers:
            sql =
                "SELECT s.srvname,"
                "       w.fdwname,"
                "       COALESCE(array_to_string(s.srvoptions, ', '), ''),"
                "       false"
                "  FROM pg_foreign_server s"
                "  JOIN pg_foreign_data_wrapper w ON w.oid = s.srvfdw"
                " ORDER BY s.srvname";
            break;

        case CatalogList::user_mappings:
            // A view pg_user_mappings, e nao a tabela pg_user_mapping: a
            // tabela exige superusuario; a view esconde as opcoes (que
            // costumam trazer SENHA) de quem nao pode ve-las.
            sql =
                "SELECT m.usename,"
                "       '',"
                "       '',"
                "       false"
                "  FROM pg_user_mappings m"
                " WHERE m.srvname = " + lit(a) +
                " ORDER BY 1";
            break;

        case CatalogList::settings:
            sql =
                "SELECT s.name,"
                "       s.setting || COALESCE(' ' || s.unit, ''),"
                "       s.short_desc || E'\\n' || s.category ||"
                "       E'\\nsource: ' || s.source,"
                "       s.source NOT IN ('default', 'override')"
                "  FROM pg_settings s"
                " ORDER BY s.name";
            break;

        case CatalogList::roles:
            sql =
                "SELECT r.rolname,"
                "       concat_ws(', ',"
                "         CASE WHEN r.rolsuper THEN 'superuser' END,"
                "         CASE WHEN r.rolcreatedb THEN 'create db' END,"
                "         CASE WHEN r.rolcreaterole THEN 'create role' END,"
                "         CASE WHEN r.rolreplication THEN 'replication' END),"
                "       COALESCE(shobj_description(r.oid, 'pg_authid'), ''),"
                "       r.rolcanlogin"
                "  FROM pg_roles r"
                " ORDER BY r.rolname";
            break;

        case CatalogList::role_members:
        case CatalogList::role_belongs: {
            const bool members = list == CatalogList::role_members;
            sql =
                std::string("SELECT other.rolname,") +
                "       CASE WHEN m.admin_option THEN 'admin' ELSE '' END,"
                "       '',"
                "       other.rolcanlogin"
                "  FROM pg_auth_members m"
                "  JOIN pg_roles self ON self.oid = " +
                (members ? "m.roleid" : "m.member") +
                "  JOIN pg_roles other ON other.oid = " +
                (members ? "m.member" : "m.roleid") +
                " WHERE self.rolname = " + lit(a) +
                " ORDER BY other.rolname";
            break;
        }

        case CatalogList::access_methods:
            // amtype (indice ou tabela) existe desde o 9.6.
            sql = std::string("SELECT am.amname,") +
                  (version_.at_least(9, 6)
                       ? "       CASE am.amtype WHEN 'i' THEN 'index'"
                         "                      WHEN 't' THEN 'table' ELSE '' END,"
                       : "       'index',") +
                  "       COALESCE(obj_description(am.oid, 'pg_am'), ''),"
                  "       false"
                  "  FROM pg_am am"
                  " ORDER BY am.amname";
            break;

        case CatalogList::operator_classes:
            sql =
                "SELECT oc.opcname,"
                "       format_type(oc.opcintype, NULL),"
                "       n.nspname,"
                "       oc.opcdefault"
                "  FROM pg_opclass oc"
                "  JOIN pg_am am ON am.oid = oc.opcmethod"
                "  JOIN pg_namespace n ON n.oid = oc.opcnamespace"
                " WHERE am.amname = " + lit(a) +
                " ORDER BY oc.opcname, 2";
            break;

        case CatalogList::operator_families:
            sql =
                "SELECT f.opfname,"
                "       n.nspname,"
                "       '',"
                "       false"
                "  FROM pg_opfamily f"
                "  JOIN pg_am am ON am.oid = f.opfmethod"
                "  JOIN pg_namespace n ON n.oid = f.opfnamespace"
                " WHERE am.amname = " + lit(a) +
                " ORDER BY f.opfname";
            break;

        case CatalogList::encodings:
            // Nao ha' catalogo de encodings. O DBeaver os deduz das
            // conversoes (pg_conversion); aqui entram os dois lados, senao
            // um encoding que so' aparece como ORIGEM ficaria de fora.
            sql =
                "SELECT e.name, '', '',"
                "       e.name = pg_encoding_to_char("
                "           (SELECT encoding FROM pg_database"
                "             WHERE datname = current_database()))"
                "  FROM (SELECT pg_encoding_to_char(conforencoding) AS name"
                "          FROM pg_conversion"
                "         UNION"
                "        SELECT pg_encoding_to_char(contoencoding)"
                "          FROM pg_conversion) e"
                " WHERE e.name <> ''"
                " ORDER BY e.name";
            break;

        case CatalogList::collations:
            sql =
                "SELECT c.collname,"
                "       n.nspname,"
                "       COALESCE(c.collcollate, '') ||"
                "       CASE WHEN c.collencoding >= 0"
                "            THEN E'\\n' || pg_encoding_to_char(c.collencoding)"
                "            ELSE '' END,"
                "       false"
                "  FROM pg_collation c"
                "  JOIN pg_namespace n ON n.oid = c.collnamespace"
                " ORDER BY c.collname, n.nspname";
            break;

        case CatalogList::languages:
            sql =
                "SELECT l.lanname,"
                "       CASE WHEN l.lanpltrusted THEN 'trusted' ELSE '' END,"
                "       COALESCE(obj_description(l.oid, 'pg_language'), ''),"
                "       l.lanispl"
                "  FROM pg_language l"
                " ORDER BY l.lanname";
            break;

        case CatalogList::available_extensions:
            sql =
                "SELECT x.name,"
                "       x.default_version ||"
                "       COALESCE('  installed ' || x.installed_version, ''),"
                "       COALESCE(x.comment, ''),"
                "       x.installed_version IS NOT NULL"
                "  FROM pg_available_extensions x"
                " ORDER BY x.name";
            break;

        case CatalogList::jobs:
        case CatalogList::job_steps:
        case CatalogList::job_schedules: {
            // pgAgent e' uma extensao, com schema proprio. Sem ele, consultar
            // pgagent.pga_job e' erro -- e "relation does not exist" numa
            // pasta da arvore pareceria defeito, nao ausencia.
            OTTER_ASSIGN_OR_RETURN(
                auto probe,
                holt_.query("SELECT to_regclass('pgagent.pga_job') IS NOT NULL"));
            if (probe.row_count() == 0 || probe.text(0, 0) != "t") {
                return std::vector<CatalogItem>{};
            }

            if (list == CatalogList::jobs) {
                // O nome leva o id: os passos e agendas sao pedidos por ele.
                sql =
                    "SELECT j.jobname,"
                    "       j.jobid::text,"
                    "       COALESCE(j.jobdesc, ''),"
                    "       j.jobenabled"
                    "  FROM pgagent.pga_job j"
                    " ORDER BY j.jobname";
            } else {
                if (!all_digits(a)) return std::vector<CatalogItem>{};
                sql = list == CatalogList::job_steps
                    ? "SELECT s.jstname,"
                      "       CASE s.jstkind WHEN 's' THEN 'SQL' ELSE 'batch' END,"
                      "       s.jstcode,"
                      "       s.jstenabled"
                      "  FROM pgagent.pga_jobstep s"
                      " WHERE s.jstjobid = " + std::string(a) +
                      " ORDER BY s.jstname"
                    : "SELECT s.jscname,"
                      "       s.jscstart::text,"
                      "       COALESCE(s.jscdesc, ''),"
                      "       s.jscenabled"
                      "  FROM pgagent.pga_schedule s"
                      " WHERE s.jscjobid = " + std::string(a) +
                      " ORDER BY s.jscname";
            }
            break;
        }

        // As demais listas sao de outros SGBDs (SQL Server, MySQL, SQL
        // Anywhere), lidas pelos catalogos deles; aqui nao ha' consulta.
        default:
            break;
    }

    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(sql));

    std::vector<CatalogItem> items;
    items.reserve(rs.row_count());
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        CatalogItem item;
        item.name    = std::string(rs.text(r, 0));
        item.detail  = std::string(rs.text(r, 1));
        item.tooltip = std::string(rs.text(r, 2));
        item.flag    = rs.text(r, 3) == "t";
        items.push_back(std::move(item));
    }
    return items;
}

} // namespace otter::db
