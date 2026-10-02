# C-Otter

> **Why "C-Otter"?** Say it out loud: *sea otter*. It's a playful nod to DBeaver, from a
> lighter, faster cousin that shares the same river, and it's written in C. Sea otters hold
> hands while they sleep so they never drift apart, keep a favorite rock in a pocket, and
> float together in a raft. Basically, they were born for databases.
> Here, every JOIN is an **OTTER JOIN**.

A universal database manager written in C++23, inspired by the architecture of
[DBeaver](https://github.com/dbeaver/dbeaver) — same river, faster current.

## What it is

C-Otter is a cross-platform database client: it connects, browses metadata, edits data in
a grid, runs SQL and moves data between DBMSs. The difference from its ancestor is the
runtime cost — no JVM, no OSGi, no JDBC, with native drivers and an immediate-mode UI.

## v1 targets

| Axis | Decision |
|------|----------|
| Language | C++23, zero dependencies in the core |
| UI | Dear ImGui + GLFW/OpenGL 3.3, in-house virtualized grid, vector icons |
| Drivers | In-house wire protocols, no client library: PostgreSQL, MySQL/MariaDB, SQL Server and SQL Anywhere done; SQLite and Oracle to do |
| Platforms | Windows and Linux — CMake + MSVC/Clang |
| Build | CMake 4.x + Ninja, fully static link (`/MT`); packages for both systems built by GitHub Actions |

## Otter glossary

Project terms that show up in the code and the docs:

| Term | Meaning |
|------|---------|
| **Raft** | The workspace: the set of connections that float together |
| **Holt** | A single connection/datasource (the otter's den) |
| **Pocket Rock** | Local metadata cache — the favorite rock the otter keeps in its pocket |
| **Otter Join** | A JOIN. Always. |
| **Float** | An execution session on top of a Holt |

## Status

**In development — the application runs and connects to real databases.**

Four DBMSs, all through protocols written in this project (`lib/`), without libpq,
libmysqlclient, FreeTDS or ODBC. Several simultaneous connections, SQL editor with
highlighting and autocomplete over real metadata, **editable** virtualized grid with
paging, transactions, object editor, export and import, three themes and i18n
(EN + pt-BR). Connections are stored in DBeaver's format; on first run the ones from
DBeaver, pgAdmin and SSMS are copied over.

**Coverage against DBeaver: 68.0% of its 281 commands and 78.9% of its 147 shortcuts**,
measured by `tools/regen_docs.py` against its `plugin.xml` files — see
[`docs/DBEAVER-MAP.md`](docs/DBEAVER-MAP.md) and [`docs/PARITY.md`](docs/PARITY.md).

### Per DBMS

Legend: ✅ done · 🟡 works, with the stated difference · ⬜ missing. "Verified" means run
against the server, not just compiled.

| | PostgreSQL | MySQL / MariaDB | SQL Server | SQL Anywhere |
|---|---|---|---|---|
| Protocol | wire v3 (`lib/pgwire`) | `lib/mywire` | TDS 7.4 (`lib/tdswire`) | TDS 5.0 (`lib/tdswire`) |
| Authentication | SCRAM-SHA-256, MD5 | `mysql_native_password`, `caching_sha2_password` | SQL Server and Windows (SSPI) | database native |
| TLS (Windows) | 🟡 implemented, handshake not verified | ✅ | ✅ | ⬜ no encryption |
| Object tree, against DBeaver's | ✅ 59 of 59 folders | 🟡 24 of 27 nodes | 🟡 21 of 30 folders | 🟡 14 of 17 folders |
| Query, paging, editable grid | ✅ | ✅ | ✅ | ✅ |
| Object editor (create, alter, drop) | ✅ 26 of 27 types | ✅ | 🟡 10 of 16 types | ✅ |
| Permissions (`GRANT` / `REVOKE`) | ✅ | 🟡 per object | ✅ | ✅ |
| Maintenance tools | ✅ | ✅ | ✅ | ✅ |
| Backup and restore | ✅ `pg_dump` / `pg_restore` | ✅ `mysqldump` / `mysql` | ✅ `BACKUP` / `RESTORE DATABASE` | 🟡 SQL generated, not executed |
| Sessions and locks | ✅ | ✅ sessions | ✅ | ✅ |
| Execution plan (*Explain*) | ✅ | ⬜ not verified | ⬜ | ⬜ |
| Multiple result sets in a batch | — | — | 🟡 first one only | 🟡 first one only |
| Verified against | PostgreSQL 18 | MySQL 8.0.46 | SQL Server 2022 | SQL Anywhere 16 |
| Live suite | `otter_tests_live` | 108 + 111 checks | 187 checks | 337 checks |
| Full map | [`NAVIGATOR-TREE`](docs/NAVIGATOR-TREE.md), [`OBJECT-EDITOR`](docs/OBJECT-EDITOR.md) | [`MYSQL-MAP`](docs/MYSQL-MAP.md) | [`MSSQL-MAP`](docs/MSSQL-MAP.md) | [`SQLANYWHERE-MAP`](docs/SQLANYWHERE-MAP.md) |

#### PostgreSQL — what is missing

- **Verified TLS**: `SSLRequest` is implemented, but the test server runs with `ssl = off`
  and the handshake has never been exercised.
- Global Backup (`pg_dumpall`) and the foreign data wrappers wizard.
- Virtual keys, and `With Hierarchy` on the permissions tab.
- The source of materialized views, triggers and rules is read-only; foreign keys have no
  `DEFERRABLE` in the form.
- 7 of the 62 tree lists: the columns of constraints, FKs, indexes and references show up
  in the detail, not as child nodes.
- *Navigator view* Simple / Custom: the tree is always the Advanced one.

#### MySQL / MariaDB — what is missing

- **MariaDB has not been verified** (sequences, packages): the test machine only has MySQL.
- In the tree: database-level indexes and triggers (the virtual folders) and packages.
- TLS client certificate (stored and read, not used) and the prepared protocol
  (`COM_STMT_*`).
- Privileges: the global and per-schema matrix (today it is per object) and the list of
  routine privileges.
- Account limits and the table's engine, charset and auto-increment: shown, changed only
  through SQL.
- *Explain*: the plan reader understands PostgreSQL's format; on MySQL it has not been
  verified.
- Open defect: an empty password fails with a cryptography message instead of saying the
  password is empty.

#### SQL Server — what is missing

- **Authentication**: 2 of DBeaver's 8 models. Missing are NTLM with typed credentials and
  the four Entra ID (Active Directory) ones.
- TDS 8.0 (`Encrypt=strict`).
- Multiple result sets: only the first one reaches the grid.
- Parameters go in the command text (there is no RPC) and there is no bulk load (`BULK`).
- *Explain* (`SHOWPLAN_XML`) and *Execute SQL script natively* (`sqlcmd`) are disabled,
  with the reason shown.
- In the tree: external tables, partitions, schema-level triggers and extended properties
  other than `MS_Description`.
- In the editor: data type, table type and external table; indexes without included
  columns or columnstore; synonyms have no form.
- `geometry`, `geography` and `hierarchyid` are shown as binary; `bit` as `1`/`0`.
- Named instances verified only against a test SQL Server Browser.

#### SQL Anywhere — what is missing

- **No encryption**: password and data travel in clear text. The dialog says so and refuses
  "Use SSL"; the way out is the SSH tunnel.
- SAP ASE is not supported (same protocol, different catalog): the connection fails saying
  so.
- Multiple result sets: only the first one reaches the grid.
- *Explain* and *Execute SQL script natively* (`dbisql`) are disabled, with the reason
  shown.
- Renaming routines, triggers, events, sequences and domains; creating a materialized view
  through the form.
- Folders that only list, with no editor: login policies, dbspaces, remote servers, web
  services, publications. Left out of the tree: SQL Remote / MobiLink, LDAP, certificates,
  maintenance plans and mirroring.
- `BACKUP DATABASE` and `VALIDATE DATABASE`: SQL checked, not executed.

#### Common to all four

- **Linux**: it builds, passes the unit tests and opens, but with **no TLS** and no
  integrated authentication; the live suites have never run there
  ([ADR 0006](docs/adr/0006-rendering-backend.md)).
- SSH tunnel through the system's `ssh` client: key or agent, no typed password; not
  verified against a real SSH server.
- Direct transfer between databases, XLSX, and creating the target table on import.
- Other DBMSs in the driver catalog: SQLite and Oracle are planned; the rest are out of
  scope for v1, and the screen says which is which.

### Running and releasing

```powershell
build test                      # Windows: builds win-release and runs the tests
build\win-release\bin\c-otter.exe
```

```bash
cmake --preset linux-release && cmake --build build/linux-release    # Linux, Clang 19+
build/linux-release/bin/c-otter
```

Each system's package comes out of `tools\package.ps1` and `tools/package.sh`. Releasing
is pushing a tag: `git tag v0.1.0 && git push origin v0.1.0` triggers
[`.github/workflows/release.yml`](.github/workflows/release.yml), which builds, tests,
packages both and creates the Release. The tag must match the `VERSION` in
`CMakeLists.txt`.

### Documentation

The documents below are written in Portuguese.

- [`CLAUDE.md`](CLAUDE.md) — **working directives; read first**
- [`docs/DBEAVER-MAP.md`](docs/DBEAVER-MAP.md) — exhaustive map of DBeaver, generated by `tools/map_dbeaver.py`
- [`docs/NAVIGATOR-TREE.md`](docs/NAVIGATOR-TREE.md) — object tree: ~70 nodes and the state of each
- [`docs/ELEMENTS.md`](docs/ELEMENTS.md) — operational guide: every button, menu and shortcut
- [`docs/PARITY.md`](docs/PARITY.md) — features against DBeaver
- [`docs/UI-SCOPE.md`](docs/UI-SCOPE.md) — structural gaps and implementation order
- [`docs/ANALYSIS.md`](docs/ANALYSIS.md) — analysis of DBeaver as an architectural reference
- [`docs/PLAN.md`](docs/PLAN.md) — phased roadmap · [`docs/EFFORT.md`](docs/EFFORT.md) — effort in person-hours
- [`docs/adr/`](docs/adr/) — 26 architectural decisions, with the revoked ones marked
- [`lang/README.md`](lang/README.md) — how to add a language

## License

To be defined. DBeaver is **Apache 2.0** (confirmed in its `LICENSE.md`), which allows
derivative work as long as the copyright notice, the license text and a `NOTICE` stating
the modifications are preserved. Since the plan includes porting metadata queries from the
`ext.*` plugins (see [`docs/ANALYSIS.md`](docs/ANALYSIS.md) §4), attribution is
**mandatory** even though C-Otter is a rewrite — to be decided before Phase 1.
