// C-Otter -- ui/data_transfer.cpp
//
// "Import Data": carregar um CSV numa tabela. A regra -- ler o arquivo, casar
// as colunas, gerar os INSERTs em lote -- esta' em db/import.cpp, com teste;
// aqui, a janela.
//
// O assistente do DBeaver tem cinco paginas (origem, mapeamento de tabelas,
// mapeamento de colunas, opcoes, confirmacao). Para UM arquivo e UMA tabela
// elas cabem numa janela so': arquivo e formato em cima, a previa com o
// destino de cada coluna no meio, as opcoes de carga embaixo.
#include "ui/main_shell.hpp"

#include "db/mysql_object.hpp"

#include "base/i18n.hpp"
#include "db/ddl.hpp"
#include "db/import.hpp"
#include "ui/file_dialog.hpp"
#include "ui/hint.hpp"
#include "ui/theme.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace otter::ui {
namespace {

ImU32 col(std::uint32_t c) { return static_cast<ImU32>(c); }
ImVec4 col4(std::uint32_t c) { return ImGui::ColorConvertU32ToFloat4(col(c)); }

constexpr std::size_t kPreviewRows = 30;

} // namespace

void MainShell::open_import(const std::string& schema, const std::string& table) {
    if (active_connection_ >= connections_.size()) return;

    import_form_               = {};
    import_form_.open          = true;
    import_form_.connection_id = connections_[active_connection_].id;
    import_form_.schema        = schema;
    import_form_.table         = table;
}

// Le' o arquivo e refaz a previa. Chamado quando o caminho ou o formato muda.
void MainShell::reload_import_file(bool detect) {
    ImportForm& form = import_form_;
    form.status.clear();
    form.failed = false;
    form.text.clear();
    form.preview = {};
    form.mapping.clear();

    // O caminho vem do ImGui e do dialogo nativo em UTF-8; como `char*` o
    // Windows o leria na pagina de codigo local, e "relatório.csv" nao abriria.
    const std::filesystem::path file(
        std::u8string_view(reinterpret_cast<const char8_t*>(form.path)));
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (form.path[0] != '\0') {
            form.status = TR("cannot read the file");
            form.failed = true;
        }
        return;
    }
    form.text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());

    // O delimitador e' adivinhado ao ESCOLHER o arquivo; depois disso quem
    // manda e' o usuario -- readivinhar a cada mudanca desfaria a escolha dele.
    if (detect) form.options.delimiter = db::detect_delimiter(form.text);

    form.preview = db::parse_csv(form.text, form.options, kPreviewRows);
    form.mapping = db::match_columns(form.preview.header, form.table_columns);
}

void MainShell::draw_import_data_window() {
    ImportForm& form = import_form_;
    if (!form.open) return;

    const Palette& p = colors();
    Connection* owner = connection_by_id(form.connection_id);
    if (owner == nullptr) {
        form.open = false;
        return;
    }
    Session& target = *owner->session;

    // As colunas da tabela de destino, do modelo da arvore.
    if (form.table_columns.empty()) {
        if (const std::optional<db::TableMeta> table = target.table(form.schema, form.table)) {
            if (table->columns_loaded) {
                for (const db::ColumnMeta& column : table->columns) {
                    form.table_columns.push_back(column.name);
                }
                form.mapping = db::match_columns(form.preview.header, form.table_columns);
            } else if (!target.busy()) {
                target.load_columns_async(form.schema, form.table);
            }
        }
    }

    // A carga em curso terminou?
    if (form.running && !target.busy()) {
        form.running = false;
        form.failed  = target.last_script_failed();
        if (form.failed) {
            // O lote que falhou deixou a transacao aberta e abortada: desfaz,
            // para a tabela ficar como estava -- nada de carga pela metade.
            form.status = std::string(TR("nothing was imported: the load was rolled back")) +
                          "\n" + target.status_message();
            target.rollback_async();
        } else {
            form.status = TRF("%zu row(s) imported into %s", form.rows,
                              db::qualified_name(form.schema, form.table).c_str());
            target.invalidate_table(form.schema, form.table);
        }
    }

    ImGui::SetNextWindowSize(ImVec2(860, 620), ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(with_alpha(p.bg_darkest, 1.0f)));
    const bool visible = ImGui::Begin(TRW("Import Data", "###ImportData"), &form.open,
                                      ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor();
    if (!visible) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(col4(p.text_dim), "%s", TR("Target table"));
    ImGui::SameLine();
    ImGui::TextColored(col4(p.text_bright), "%s",
                       db::qualified_name(form.schema, form.table).c_str());
    ImGui::Separator();

    // --- Arquivo e formato --------------------------------------------------------
    ImGui::SetNextItemWidth(-190.0f);
    if (ImGui::InputText("##path", form.path, sizeof form.path,
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        reload_import_file(/*detect=*/true);
    }
    const bool path_edited = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (ImGui::Button(TR("Browse..."), ImVec2(100, 0))) {
        if (auto chosen = open_file_dialog(
                TR("Import Data"), {{"CSV", "*.csv;*.txt;*.tsv"}, {"*", "*.*"}})) {
            std::snprintf(form.path, sizeof form.path, "%s", chosen->c_str());
            reload_import_file(/*detect=*/true);
        }
    } else if (path_edited) {
        reload_import_file(/*detect=*/true);
    }
    ImGui::SameLine();
    ImGui::TextColored(col4(p.text_dim), "%s", TR("File"));

    bool reparse = false;
    static constexpr struct { char value; const char* label; } kDelimiters[] = {
        {',', ","}, {';', ";"}, {'\t', "Tab"}, {'|', "|"},
    };
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Delimiter"));
    for (const auto& [value, label] : kDelimiters) {
        ImGui::SameLine();
        if (ImGui::RadioButton(label, form.options.delimiter == value)) {
            form.options.delimiter = value;
            reparse = true;
        }
    }
    ImGui::SameLine(0.0f, 24.0f);
    reparse |= ImGui::Checkbox(TR("Header row"), &form.options.header);

    ImGui::SameLine(0.0f, 24.0f);
    {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%s", form.options.null_text.c_str());
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::InputText(TR("NULL text"), buffer, sizeof buffer)) {
            form.options.null_text = buffer;
            reparse = true;
        }
        if (ImGui::IsItemHovered()) {
            hint_fmt("%s", TR("The text that means NULL, in a field without quotes.\n"
                              "Empty: an empty unquoted field is NULL and \"\" is an "
                              "empty string."));
        }
    }
    if (reparse && !form.text.empty()) {
        // O mapeamento escolhido sobrevive quando as colunas continuam as
        // mesmas (mudou so' o texto de NULL, por exemplo).
        const std::vector<std::string> header = form.preview.header;
        const std::vector<std::string> kept   = form.mapping;
        form.preview = db::parse_csv(form.text, form.options, kPreviewRows);
        form.mapping = form.preview.header == header
                           ? kept
                           : db::match_columns(form.preview.header, form.table_columns);
    }

    ImGui::TextColored(col4(p.text_dim), "%s",
                       TR("The file is read as UTF-8."));

    ImGui::Separator();

    // --- Previa e mapeamento --------------------------------------------------------
    const float footer = ImGui::GetFrameHeightWithSpacing() * 3.6f;

    if (form.text.empty()) {
        ImGui::BeginChild("##importempty", ImVec2(0.0f, -footer));
        ImGui::TextColored(col4(p.text_dim), "%s", TR("choose a CSV file to import"));
        ImGui::EndChild();
    } else {
        if (!form.preview.error.empty()) {
            ImGui::TextColored(col4(p.error), "%s", form.preview.error.c_str());
        }

        // O ImGui nao desenha mais de 64 colunas numa tabela (ver a grade).
        const std::size_t shown = std::min<std::size_t>(form.preview.header.size(), 64);
        if (form.mapping.size() < form.preview.header.size()) {
            form.mapping.resize(form.preview.header.size());
        }

        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_NoSavedSettings;

        if (shown > 0 &&
            ImGui::BeginTable("##importpreview", static_cast<int>(shown), flags,
                              ImVec2(0.0f, -footer))) {
            ImGui::TableSetupScrollFreeze(0, 2);
            for (std::size_t c = 0; c < shown; ++c) {
                ImGui::TableSetupColumn(form.preview.header[c].c_str(),
                                        ImGuiTableColumnFlags_WidthFixed, 150.0f);
            }
            ImGui::TableHeadersRow();

            // A segunda linha fixa: para onde cada coluna do arquivo vai.
            ImGui::TableNextRow();
            for (std::size_t c = 0; c < shown; ++c) {
                ImGui::TableSetColumnIndex(static_cast<int>(c));
                ImGui::PushID(static_cast<int>(c));
                ImGui::SetNextItemWidth(-FLT_MIN);

                std::string& mapped = form.mapping[c];
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      col(mapped.empty() ? p.text_dim : p.accent_light));
                if (ImGui::BeginCombo("##target",
                                      mapped.empty() ? TR("(skip)") : mapped.c_str())) {
                    ImGui::PopStyleColor();
                    if (ImGui::Selectable(TR("(skip)"), mapped.empty())) mapped.clear();
                    for (const std::string& column : form.table_columns) {
                        if (ImGui::Selectable(column.c_str(), column == mapped)) {
                            mapped = column;
                        }
                    }
                    ImGui::EndCombo();
                } else {
                    ImGui::PopStyleColor();
                }
                ImGui::PopID();
            }

            for (const auto& row : form.preview.rows) {
                ImGui::TableNextRow();
                for (std::size_t c = 0; c < shown && c < row.size(); ++c) {
                    ImGui::TableSetColumnIndex(static_cast<int>(c));
                    if (row[c].has_value()) {
                        ImGui::TextUnformatted(row[c]->c_str());
                    } else {
                        ImGui::TextColored(col4(p.text_dim), "[NULL]");
                    }
                }
            }
            ImGui::EndTable();
        }
    }

    // --- Opcoes de carga e botoes ---------------------------------------------------
    ImGui::Checkbox(TR("Truncate target table before load"), &form.truncate);
    if (form.truncate) {
        ImGui::SameLine();
        ImGui::TextColored(col4(p.error), "%s", TR("removes EVERY row first"));
    }
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt(TR("Rows per INSERT"), &form.batch_rows, 100, 1000);
    form.batch_rows = std::clamp(form.batch_rows, 1, 10000);

    const bool mapped_any =
        std::any_of(form.mapping.begin(), form.mapping.end(),
                    [](const std::string& column) { return !column.empty(); });
    const bool can_import =
        !form.running && !form.text.empty() && form.preview.error.empty() &&
        mapped_any && target.state() == SessionState::connected && !target.busy();

    ImGui::BeginDisabled(!can_import && !(import_submit_ && can_import));
    const bool clicked = ImGui::Button(TR("Import"), ImVec2(140, 0));
    ImGui::EndDisabled();
    const bool submit = (clicked || import_submit_) && can_import;
    import_submit_ = false;

    if (submit) {
        // Agora o arquivo INTEIRO -- a previa parou em algumas dezenas de
        // linhas.
        const db::CsvTable data = db::parse_csv(form.text, form.options);

        db::ImportPlan plan;
        plan.schema         = form.schema;
        plan.table          = form.table;
        plan.columns        = form.mapping;
        plan.truncate_first = form.truncate;
        plan.batch_rows     = static_cast<std::size_t>(form.batch_rows);

        // O dialeto de citacao e' o da conexao de destino.
        const db::QuoteStyle previous = db::sql_dialect();
        db::set_sql_dialect_for(owner->profile.driver_id);
        db::ImportScript script = db::generate_import(data, plan);
        const std::string begin(db::transaction_begin_sql());
        const std::string commit(db::transaction_commit_sql());
        db::set_sql_dialect(previous);

        if (!script.error.empty()) {
            form.status = TR(script.error.c_str());
            form.failed = true;
        } else if (script.rows == 0) {
            form.status = TR("the file has no data rows");
            form.failed = true;
        } else {
            // Uma transacao para a carga toda: falhar na linha 90.000 nao
            // pode deixar 89.500 gravadas. (No MySQL o TRUNCATE confirma
            // sozinho -- e' do servidor.)
            const bool own_transaction = target.auto_commit();
            if (own_transaction) {
                script.statements.insert(script.statements.begin(), begin);
                script.statements.emplace_back(commit);
            }
            form.rows    = script.rows;
            form.running = true;
            form.failed  = false;
            form.status.clear();
            target.execute_script_async(std::move(script.statements));
        }
    }

    ImGui::SameLine();
    if (ImGui::Button(TR("Close"), ImVec2(120, 0))) form.open = false;

    ImGui::SameLine();
    if (form.running) {
        ImGui::TextColored(col4(p.text), TR("importing: batch %zu of %zu"),
                           target.script_progress(), target.script_total());
    } else if (!form.status.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(col4(form.failed ? p.error : p.ok), "%s", form.status.c_str());
        ImGui::PopTextWrapPos();
    } else if (!form.text.empty() && form.preview.error.empty()) {
        ImGui::TextColored(col4(p.text_dim),
                           form.preview.truncated ? TR("preview: first %zu rows")
                                                  : TR("%zu row(s) in the file"),
                           form.preview.rows.size());
    }

    ImGui::End();
}

// --- Backup e Restore --------------------------------------------------------------
//
// Pelo cliente nativo do PostgreSQL, como no DBeaver. O comando e' montado em
// db/native_tools.cpp (com teste: a senha nunca vai na linha de comando); aqui
// ficam o formulario e a janela com a saida do programa.

void MainShell::open_backup(const db::ObjectRef& ref) {
    if (active_connection_ >= connections_.size()) return;

    tool_form_               = {};
    tool_form_.kind          = ToolForm::Kind::backup;
    tool_form_.connection_id = connections_[active_connection_].id;
    tool_form_.database      = ref.type == db::ObjectType::database
                                   ? ref.name
                                   : session().database_name();
    const bool mysql = connections_[active_connection_].profile.driver_id == "mysql";
    if (mysql) {
        // No MySQL nao ha' schema: o banco E' o `schema` do objeto, e a tabela
        // vai para o mysqldump pelo nome simples.
        if (ref.type == db::ObjectType::table) {
            tool_form_.database = ref.schema;
            tool_form_.table    = ref.name;
        } else {
            tool_form_.database = ref.name;
        }
    } else if (ref.type == db::ObjectType::schema) {
        tool_form_.schema = ref.name;
    } else if (ref.type == db::ObjectType::table) {
        tool_form_.table = db::qualified_name(ref.schema, ref.name);
    }

    // Um nome de arquivo sugerido, na pasta do usuario.
    const char* home = std::getenv("USERPROFILE");
    if (home == nullptr) home = std::getenv("HOME");
    const std::string what = !tool_form_.table.empty()    ? ref.name
                             : !tool_form_.schema.empty() ? tool_form_.schema
                                                          : tool_form_.database;
    std::snprintf(tool_form_.file, sizeof tool_form_.file, "%s/%s%s",
                  home != nullptr ? home : ".", what.c_str(),
                  mysql ? ".sql"
                        : std::string(db::dump_file_extension(db::DumpFormat::custom)).c_str());
}

void MainShell::open_restore(const std::string& database) {
    if (active_connection_ >= connections_.size()) return;

    tool_form_               = {};
    tool_form_.kind          = ToolForm::Kind::restore;
    tool_form_.connection_id = connections_[active_connection_].id;
    tool_form_.database      = database;
}

void MainShell::start_tool(std::string title, Result<ProcessOptions> command,
                           bool reload_after) {
    if (!command) {
        tool_form_.error = TR(command.error().message().c_str());
        return;
    }

    auto process = Process::start(*command);
    if (!process) {
        tool_form_.error = process.error().to_string();
        return;
    }

    tool_run_               = ToolRun{};
    tool_run_.open          = true;
    tool_run_.title         = std::move(title);
    tool_run_.command_line  = display_command(*command);
    tool_run_.process       = std::move(*process);
    tool_run_.running       = true;
    tool_run_.reload_after  = reload_after;
    tool_run_.connection_id = tool_form_.connection_id;

    tool_form_.kind = ToolForm::Kind::none;
}

void MainShell::draw_tool_windows() {
    const Palette& p = colors();

    // --- O formulario ------------------------------------------------------------
    if (tool_form_.kind != ToolForm::Kind::none) {
        ToolForm& form = tool_form_;
        Connection* owner = connection_by_id(form.connection_id);
        if (owner == nullptr) {
            form.kind = ToolForm::Kind::none;
        } else if (owner->profile.driver_id == "mysql") {
            draw_mysql_tool_form();
        } else {
            const bool backup = form.kind == ToolForm::Kind::backup;

            bool open = true;
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                    ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, col(with_alpha(p.bg_darkest, 1.0f)));
            const bool visible = ImGui::Begin(
                backup ? TRW("Backup", "###ToolForm") : TRW("Restore", "###ToolForm"),
                &open,
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoCollapse);
            ImGui::PopStyleColor();

            if (visible) {
                // O que entra no backup (ou para onde vai o restore).
                std::string scope = form.database;
                if (!form.table.empty())       scope += "  /  " + form.table;
                else if (!form.schema.empty()) scope += "  /  " + form.schema;
                ImGui::TextColored(col4(p.text_dim), "%s", scope.c_str());

                // O programa que vai rodar, ou a falta dele -- antes de o
                // usuario preencher o formulario inteiro.
                const db::DumpFormat format = static_cast<db::DumpFormat>(form.format);
                const std::string tool_name =
                    backup ? std::string("pg_dump")
                           : std::string(db::restore_program(format));
                const std::string program = db::find_postgres_tool(tool_name);
                if (program.empty()) {
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
                    ImGui::TextColored(col4(p.error),
                                       TR("%s was not found. Install the PostgreSQL "
                                          "client tools; C-Otter looks in the PATH and "
                                          "in the PostgreSQL installation folders."),
                                       tool_name.c_str());
                    ImGui::PopTextWrapPos();
                } else {
                    ImGui::TextColored(col4(p.text_dim), "%s", program.c_str());
                }
                ImGui::Separator();

                ImGui::SeparatorText(TR("Settings"));
                static constexpr db::DumpFormat kFormats[] = {
                    db::DumpFormat::custom, db::DumpFormat::directory,
                    db::DumpFormat::tar, db::DumpFormat::plain};
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::BeginCombo(TR("Format"),
                                      std::string(db::to_string(format)).c_str())) {
                    for (const db::DumpFormat item : kFormats) {
                        if (ImGui::Selectable(std::string(db::to_string(item)).c_str(),
                                              item == format)) {
                            // A extensao acompanha o formato, como na
                            // exportacao de resultado.
                            const std::string old_ext(db::dump_file_extension(format));
                            std::string file = form.file;
                            if (backup && !old_ext.empty() && file.ends_with(old_ext)) {
                                file.resize(file.size() - old_ext.size());
                                file += std::string(db::dump_file_extension(item));
                                std::snprintf(form.file, sizeof form.file, "%s",
                                              file.c_str());
                            }
                            form.format = static_cast<int>(item);
                        }
                    }
                    ImGui::EndCombo();
                }

                if (backup) {
                    static constexpr const char* kLevels[] = {
                        "", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
                    int level = form.compression + 1;
                    ImGui::SetNextItemWidth(220.0f);
                    if (ImGui::Combo(TR("Compression"), &level, kLevels,
                                     IM_ARRAYSIZE(kLevels))) {
                        form.compression = level - 1;
                    }
                    ImGui::SetNextItemWidth(220.0f);
                    ImGui::InputText(TR("Encoding"), form.encoding, sizeof form.encoding);

                    ImGui::Checkbox(TR("Use SQL INSERT instead of COPY for rows"),
                                    &form.use_inserts);
                    ImGui::Checkbox(TR("Do not backup privileges (GRANT/REVOKE)"),
                                    &form.no_privileges);
                    ImGui::Checkbox(TR("Discard objects owner"), &form.no_owner);
                    ImGui::Checkbox(TR("Add drop database statement"), &form.clean);
                    ImGui::Checkbox(TR("Add create database statement"), &form.create);
                } else {
                    ImGui::Checkbox(
                        TR("Clean (drop) database objects before recreating them"),
                        &form.clean);
                    if (form.clean) {
                        ImGui::TextColored(col4(p.error), "%s",
                                           TR("drops the objects of the backup that "
                                              "exist in the database"));
                    }
                    ImGui::Checkbox(TR("Discard objects owner"), &form.no_owner);
                    ImGui::Checkbox(TR("Create database"), &form.create);
                }

                ImGui::SeparatorText(backup ? TR("Output") : TR("Input"));
                ImGui::SetNextItemWidth(420.0f);
                ImGui::InputText("##toolfile", form.file, sizeof form.file);
                ImGui::SameLine();
                if (ImGui::Button(TR("Browse..."))) {
                    if (backup) {
                        if (auto chosen = save_file_dialog(TR("Backup"), {{"*", "*.*"}},
                                                           form.file)) {
                            std::snprintf(form.file, sizeof form.file, "%s",
                                          chosen->c_str());
                        }
                    } else if (auto chosen = open_file_dialog(
                                   TR("Choose backup file"),
                                   {{"Backup", "*.backup;*.dump;*.tar;*.sql"},
                                    {"*", "*.*"}})) {
                        std::snprintf(form.file, sizeof form.file, "%s", chosen->c_str());
                    }
                }
                ImGui::SameLine();
                ImGui::TextColored(col4(p.text_dim), "%s",
                                   backup ? TR("File") : TR("Backup file"));

                ImGui::Separator();
                if (!form.error.empty()) {
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
                    ImGui::TextColored(col4(p.error), "%s", form.error.c_str());
                    ImGui::PopTextWrapPos();
                }

                const bool can_start = !program.empty() && form.file[0] != '\0' &&
                                       !tool_run_.running;
                ImGui::BeginDisabled(!can_start);
                const bool clicked = ImGui::Button(TR("Start"), ImVec2(140, 0));
                ImGui::EndDisabled();
                const bool submit = (clicked || tool_submit_) && can_start;
                tool_submit_ = false;

                if (submit) {
                    // A conexao do perfil, com o banco escolhido -- e pela
                    // ponta local do tunel SSH, quando ha' um.
                    db::ConnConfig connection = owner->profile.to_conn_config();
                    connection.database = form.database;
                    if (const std::uint16_t port = owner->session->tunnel_port()) {
                        connection.host = "127.0.0.1";
                        connection.port = port;
                    }

                    form.error.clear();
                    if (backup) {
                        db::BackupOptions options;
                        options.format        = format;
                        options.compression   = form.compression;
                        options.encoding      = form.encoding;
                        options.use_inserts   = form.use_inserts;
                        options.no_privileges = form.no_privileges;
                        options.no_owner      = form.no_owner;
                        options.clean         = form.clean;
                        options.create        = form.create;
                        options.file          = form.file;
                        if (!form.table.empty())       options.tables.push_back(form.table);
                        else if (!form.schema.empty()) options.schemas.push_back(form.schema);

                        start_tool(TRF("Backup of %s", form.database.c_str()),
                                   db::backup_command(connection, options, program),
                                   /*reload_after=*/false);
                    } else {
                        db::RestoreOptions options;
                        options.format   = format;
                        options.clean    = form.clean;
                        options.no_owner = form.no_owner;
                        options.create   = form.create;
                        options.file     = form.file;

                        start_tool(TRF("Restore into %s", form.database.c_str()),
                                   db::restore_command(connection, options, program),
                                   /*reload_after=*/true);
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button(TR("Cancel"), ImVec2(120, 0))) {
                    form.kind = ToolForm::Kind::none;
                }
            }
            ImGui::End();
            if (!open) form.kind = ToolForm::Kind::none;
        }
    } else {
        tool_submit_ = false;
    }

    // --- A execucao --------------------------------------------------------------
    ToolRun& run = tool_run_;

    // Colhido a cada quadro mesmo com a janela fechada: um pipe cheio pararia
    // o pg_dump no meio do backup.
    if (run.running) {
        run.output += run.process.read_output();
        if (!run.process.running()) {
            run.output   += run.process.read_output();
            run.running   = false;
            run.exit_code = run.process.exit_code().value_or(-1);

            if (run.exit_code == 0 && run.reload_after) {
                if (Connection* owner = connection_by_id(run.connection_id)) {
                    owner->session->reload_catalog_async();
                }
            }
            show_toast(run.exit_code == 0
                           ? std::string(TRF("%s: finished", run.title.c_str()))
                           : std::string(TRF("%s: failed (exit code %d)",
                                             run.title.c_str(), run.exit_code)));
        }
        // O log de um backup grande passa de megabytes: guarda o fim.
        constexpr std::size_t kKeep = 256 * 1024;
        if (run.output.size() > kKeep * 2) {
            run.output.erase(0, run.output.size() - kKeep);
        }
    }

    if (!run.open) return;

    ImGui::SetNextWindowSize(ImVec2(760, 460), ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(with_alpha(p.bg_darkest, 1.0f)));
    const bool visible =
        ImGui::Begin((run.title + "###ToolRun").c_str(), &run.open,
                     ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor();

    if (visible) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(col4(p.text_dim), "%s", run.command_line.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();

        const float footer = ImGui::GetFrameHeightWithSpacing() + 4.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, col(p.bg_darkest));
        if (ImGui::BeginChild("##tooloutput", ImVec2(0.0f, -footer),
                              ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            ImGui::TextUnformatted(run.output.c_str(),
                                   run.output.c_str() + run.output.size());
            // Acompanha o fim enquanto roda.
            if (run.running) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        if (run.running) {
            if (ImGui::Button(TR("Cancel"), ImVec2(120, 0))) {
                run.process.kill();
            }
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text), "%s", TR("running..."));
        } else {
            if (ImGui::Button(TR("Close"), ImVec2(120, 0))) run.open = false;
            ImGui::SameLine();
            if (ImGui::Button(TR("Copy"), ImVec2(120, 0))) {
                ImGui::SetClipboardText(run.output.c_str());
            }
            ImGui::SameLine();
            if (run.exit_code == 0) {
                ImGui::TextColored(col4(p.ok), "%s", TR("finished"));
            } else {
                ImGui::TextColored(col4(p.error), TR("failed (exit code %d)"),
                                   run.exit_code);
            }
        }
    }
    ImGui::End();
}

// --- MySQL: Dump database / Restore / Execute script --------------------------------
//
// O assistente "Dump database" do DBeaver (MySQLExportSettings) e o "Execute
// script" (MySQLScriptExecuteSettings), pelo mysqldump e pelo mysql do
// sistema -- como no PostgreSQL, o formato do dump e' do proprio SGBD (ADR 0021).
void MainShell::draw_mysql_tool_form() {
    const Palette& p = colors();
    ToolForm& form = tool_form_;
    Connection* found = connection_by_id(form.connection_id);
    if (found == nullptr) {
        form.kind = ToolForm::Kind::none;
        return;
    }
    Connection& owner = *found;
    const bool backup = form.kind == ToolForm::Kind::backup;

    bool open = true;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(with_alpha(p.bg_darkest, 1.0f)));
    const bool visible = ImGui::Begin(
        backup ? TRW("Dump database", "###ToolForm") : TRW("Execute script", "###ToolForm"),
        &open,
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleColor();

    if (visible) {
        std::string scope = form.database;
        if (!form.table.empty()) scope += "  /  " + form.table;
        ImGui::TextColored(col4(p.text_dim), "%s", scope.c_str());

        const char* tool_name = backup ? "mysqldump" : "mysql";
        const std::string program = db::find_mysql_tool(tool_name);
        if (program.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
            ImGui::TextColored(col4(p.error),
                               TR("%s was not found. Install the MySQL client tools; "
                                  "C-Otter looks in the PATH and in the MySQL "
                                  "installation folders."),
                               tool_name);
            ImGui::PopTextWrapPos();
        } else {
            ImGui::TextColored(col4(p.text_dim), "%s", program.c_str());
        }
        ImGui::Separator();

        if (backup) {
            ImGui::SeparatorText(TR("Settings"));
            const char* methods[] = {TR("Online backup in single transaction"),
                                     TR("Lock all tables"), TR("Normal (no locks)")};
            ImGui::SetNextItemWidth(320.0f);
            ImGui::Combo(TR("Execution method"), &form.my_method, methods,
                         IM_ARRAYSIZE(methods));

            ImGui::Checkbox(TR("No CREATE statements"), &form.my_no_create);
            ImGui::Checkbox(TR("Add DROP statements"), &form.my_add_drop);
            ImGui::Checkbox(TR("Disable keys"), &form.my_disable_keys);
            ImGui::Checkbox(TR("Extended inserts"), &form.my_extended);
            ImGui::Checkbox(TR("Dump events"), &form.my_events);
            ImGui::Checkbox(TR("Dump routines"), &form.my_routines);
            ImGui::Checkbox(TR("Additional comments"), &form.my_comments);
            ImGui::Checkbox(TR("Dump binary data in hex"), &form.my_hex_blob);
            ImGui::Checkbox(TR("No data (structure only)"), &form.my_no_data);
        } else {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
            ImGui::TextColored(col4(p.text_dim), "%s",
                               TR("Runs the SQL file through the mysql client: a dump "
                                  "made by mysqldump, or any script."));
            ImGui::PopTextWrapPos();
        }

        ImGui::SeparatorText(backup ? TR("Output") : TR("Input"));
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("##toolfile", form.file, sizeof form.file);
        ImGui::SameLine();
        if (ImGui::Button(TR("Browse..."))) {
            if (backup) {
                if (auto chosen = save_file_dialog(TR("Dump database"),
                                                   {{"SQL", "*.sql"}, {"*", "*.*"}},
                                                   form.file)) {
                    std::snprintf(form.file, sizeof form.file, "%s", chosen->c_str());
                }
            } else if (auto chosen = open_file_dialog(TR("Choose backup file"),
                                                      {{"SQL", "*.sql"}, {"*", "*.*"}})) {
                std::snprintf(form.file, sizeof form.file, "%s", chosen->c_str());
            }
        }

        ImGui::Separator();
        if (!form.error.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
            ImGui::TextColored(col4(p.error), "%s", form.error.c_str());
            ImGui::PopTextWrapPos();
        }

        const bool can_start = !program.empty() && form.file[0] != '\0' && !tool_run_.running;
        ImGui::BeginDisabled(!can_start);
        const bool clicked = ImGui::Button(TR("Start"), ImVec2(140, 0));
        ImGui::EndDisabled();
        const bool submit = (clicked || tool_submit_) && can_start;
        tool_submit_ = false;

        if (submit) {
            db::ConnConfig connection = owner.profile.to_conn_config();
            connection.database = form.database;
            if (const std::uint16_t port = owner.session->tunnel_port()) {
                connection.host = "127.0.0.1";
                connection.port = port;
            }

            form.error.clear();
            if (backup) {
                db::MysqlDumpOptions options;
                options.method = static_cast<db::MysqlDumpOptions::Method>(
                    std::clamp(form.my_method, 0, 2));
                options.no_create       = form.my_no_create;
                options.add_drop        = form.my_add_drop;
                options.disable_keys    = form.my_disable_keys;
                options.extended_insert = form.my_extended;
                options.events          = form.my_events;
                options.routines        = form.my_routines;
                options.comments        = form.my_comments;
                options.hex_blob        = form.my_hex_blob;
                options.no_data         = form.my_no_data;
                options.file            = form.file;
                if (!form.table.empty()) options.tables.push_back(form.table);

                start_tool(TRF("Backup of %s", form.database.c_str()),
                           db::mysql_dump_command(connection, options, program),
                           /*reload_after=*/false);
            } else {
                start_tool(TRF("Restore into %s", form.database.c_str()),
                           db::mysql_script_command(connection, form.file, program),
                           /*reload_after=*/true);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"), ImVec2(120, 0))) form.kind = ToolForm::Kind::none;
    }
    ImGui::End();
    if (!open) form.kind = ToolForm::Kind::none;
}

} // namespace otter::ui
