// C-Otter -- ui/script_store.hpp
//
// Os scripts SQL em disco: a pasta `.script`, ao lado do executavel, e o
// indice que diz quais estavam abertos e a que conexao cada um pertence.
//
// Pedido do usuario (2026-10-01): "Todos script, deve ser salvo, na pasta
// '.script', referente ao exe, ao sair do app nao deve perguntar para salvar,
// porque o salvamento deve ser automatico, alguns ms, apos parar a digitacao."
//
// No DBeaver os scripts moram em `<workspace>/<projeto>/Scripts`, com os nomes
// `Script.sql`, `Script-1.sql`, ... (SQLEditorUtils.createNewScript), a conexao
// de cada um fica nos metadados do projeto, e os editores abertos voltam ao
// reabrir o programa. Aqui o indice (`session.json`) faz os dois papeis.
//
// Sem ImGui e sem Session: e' o que da' para testar (tests/unit).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace otter::ui {

// Um script conhecido: aberto numa aba, ou fechado e ainda em disco.
struct ScriptEntry {
    // Nome do arquivo dentro da pasta de scripts ("Script-1.sql"), ou o
    // caminho inteiro quando o arquivo e' de fora dela (aberto por
    // "Open SQL script").
    std::string file;

    // A conexao: a chave do perfil salvo e, de reserva, o nome -- a chave
    // muda se o perfil for apagado e recriado; o nome, se for renomeado.
    std::string connection_id;
    std::string connection;
    // Banco, quando o script e' da sessao de OUTRO banco do servidor
    // (ADR 0018). Vazio = o banco do proprio perfil.
    std::string database;

    // Nome dado pelo usuario em "Rename tab". Vazio = o nome do arquivo.
    std::string title;

    bool pinned = false;
    bool open   = true;

    bool operator==(const ScriptEntry&) const = default;
};

struct ScriptSession {
    std::vector<ScriptEntry> scripts;   // na ordem das abas
    std::string              active;    // `file` do script que estava na frente

    bool operator==(const ScriptSession&) const = default;
};

// `<pasta do executavel>/.script`. Nao cria a pasta.
//
// OTTER_SCRIPT_DIR troca a pasta: e' o que os roteiros de conferencia usam
// para nao abrir -- nem regravar -- os scripts de quem esta' usando a maquina.
[[nodiscard]] std::string scripts_directory();

// O indice: `<pasta>/session.json`.
[[nodiscard]] std::string script_session_path(const std::string& directory);

// Indice ausente ou ilegivel devolve a sessao vazia: o programa abre sem aba,
// e os arquivos continuam na pasta.
[[nodiscard]] ScriptSession load_script_session(const std::string& directory);

// Cria a pasta se preciso. Falso se nao conseguiu gravar.
bool save_script_session(const std::string& directory, const ScriptSession& session);

// O texto do indice, sem gravar: quem grava compara com o ultimo para nao
// reescrever o arquivo a cada meio segundo.
[[nodiscard]] std::string serialize_script_session(const ScriptSession& session);

// O caminho de `file`: relativo e' dentro da pasta; absoluto fica como esta'.
[[nodiscard]] std::string script_path(const std::string& directory,
                                      const std::string& file);

// O inverso: so' o nome quando o arquivo esta' NA pasta de scripts.
[[nodiscard]] std::string script_file_field(const std::string& directory,
                                            const std::string& path);

// Verdadeiro se `path` e' um arquivo diretamente dentro da pasta de scripts.
[[nodiscard]] bool is_stored_script(const std::string& directory,
                                    const std::string& path);

// O nome de uma aba nova: "Script", "Script-1", "Script-2", ... -- o primeiro
// que nao e' de um arquivo da pasta nem esta' em `taken` (as abas abertas que
// ainda nao foram gravadas). Sem a extensao.
[[nodiscard]] std::string unique_script_name(const std::string& directory,
                                             const std::vector<std::string>& taken);

// Um nome de arquivo a partir de um titulo: sem separador de pasta nem os
// caracteres que o Windows recusa. Vazio vira "Script".
[[nodiscard]] std::string script_file_name(std::string_view title);

// Grava o texto inteiro. Por arquivo temporario + rename: o programa pode ser
// encerrado a qualquer instante, e um script pela metade seria perder o que
// ja' estava salvo.
bool write_script(const std::string& path, std::string_view text);

[[nodiscard]] std::optional<std::string> read_script(const std::string& path);

// Os arquivos `.sql` da pasta (so' o nome), em ordem alfabetica.
[[nodiscard]] std::vector<std::string> list_scripts(const std::string& directory);

} // namespace otter::ui
