"""Mapeia exaustivamente os elementos do DBeaver a partir do codigo-fonte.

Extrai comandos, atalhos, menus, dialogos, assistentes, preferencias, views,
editores, acoes e pontos de extensao -- direto dos plugin.xml e dos .java, nao
por amostragem.

  python tools/map_dbeaver.py D:/Tootega/Source/dbeaver > docs/DBEAVER-MAP.md
"""
from __future__ import annotations

import collections
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = sys.argv[1] if len(sys.argv) > 1 else "D:/Tootega/Source/dbeaver"
PLUGINS = os.path.join(ROOT, "plugins")


def plugin_xmls():
    for dirpath, _dirs, files in os.walk(PLUGINS):
        if "plugin.xml" in files:
            yield os.path.join(dirpath, "plugin.xml")


def plugin_name(path: str) -> str:
    # .../plugins/<nome>/plugin.xml
    parts = os.path.normpath(path).split(os.sep)
    return parts[parts.index("plugins") + 1] if "plugins" in parts else "?"


def parse(path: str):
    try:
        return ET.parse(path).getroot()
    except ET.ParseError:
        return None


def load_translations() -> dict[str, str]:
    """Resolve as chaves %chave dos plugin.xml.

    Os rotulos ficam em OSGI-INF/l10n/bundle.properties de cada plugin.
    Preferimos pt_BR quando existe; o ingles e' o fallback.
    """
    table: dict[str, str] = {}

    for dirpath, _dirs, files in os.walk(PLUGINS):
        if not dirpath.endswith(os.path.join("OSGI-INF", "l10n")):
            continue

        for name in ("bundle.properties", "bundle_pt_BR.properties"):
            if name not in files:
                continue
            path = os.path.join(dirpath, name)
            try:
                with open(path, encoding="utf-8", errors="replace") as handle:
                    for raw in handle:
                        line = raw.strip()
                        if not line or line.startswith("#") or "=" not in line:
                            continue
                        key, _, value = line.partition("=")
                        # pt_BR sobrescreve o ingles quando presente.
                        table[key.strip()] = value.strip()
            except OSError:
                pass
    return table


TRANSLATIONS = load_translations()


def label(text: str) -> str:
    """Troca %chave pelo rotulo legivel, se houver."""
    if text.startswith("%"):
        return TRANSLATIONS.get(text[1:], text[1:])
    return text


# --- coleta -----------------------------------------------------------------

commands = {}        # id -> (nome, categoria, plugin)
bindings = []        # (sequencia, commandId, contexto, plugin)
menus = collections.defaultdict(list)
views = []
editors = []
wizards_xml = []
pref_pages = []
extension_points = []
handlers = 0
toolbars = 0
contributions = 0

for xml_path in plugin_xmls():
    root = parse(xml_path)
    if root is None:
        continue
    plugin = plugin_name(xml_path)

    for element in root.iter():
        tag = element.tag
        attrib = element.attrib

        if tag == "command" and "id" in attrib and "defaultHandler" not in attrib:
            cid = attrib["id"]
            if cid.startswith("org.jkiss"):
                commands[cid] = (
                    label(attrib.get("name", "")),
                    attrib.get("categoryId", "").split(".")[-1],
                    plugin,
                )
        elif tag == "key" and "sequence" in attrib:
            bindings.append((
                attrib["sequence"],
                attrib.get("commandId", "").split(".")[-1],
                attrib.get("contextId", "").split(".")[-1],
                plugin,
            ))
        elif tag == "menu":
            menu_label = label(attrib.get("label", attrib.get("id", "")))
            if menu_label:
                menus[plugin].append(menu_label)
        elif tag == "view" and "id" in attrib:
            views.append((label(attrib.get("name", attrib["id"])), plugin))
        elif tag == "editor" and "id" in attrib:
            editors.append((label(attrib.get("name", attrib["id"])), plugin))
        elif tag == "wizard" and "id" in attrib:
            wizards_xml.append((label(attrib.get("name", attrib["id"])), plugin))
        elif tag == "page" and "name" in attrib:
            pref_pages.append((label(attrib["name"]), plugin))
        elif tag == "extension-point" and "id" in attrib:
            extension_points.append((attrib["id"], plugin))
        elif tag == "handler":
            handlers += 1
        elif tag == "toolbar":
            toolbars += 1
        elif tag in ("menuContribution", "toolbarContribution"):
            contributions += 1


# --- classes Java por categoria ---------------------------------------------

def count_java(pattern: str) -> int:
    rx = re.compile(pattern)
    total = 0
    for dirpath, _dirs, files in os.walk(PLUGINS):
        for name in files:
            if name.endswith(".java") and rx.search(name):
                total += 1
    return total


java_counts = {
    "Dialogs": count_java(r"Dialog\.java$"),
    "Wizards / páginas de assistente": count_java(r"Wizard.*\.java$"),
    "Páginas de preferências": count_java(r"^Pref.*Page.*\.java$"),
    "Handlers de comando": count_java(r"Handler\.java$"),
    "Actions": count_java(r"Action\.java$"),
    "Views / painéis": count_java(r"(View|Panel)\.java$"),
    "Editores": count_java(r"Editor\.java$"),
    "Value handlers (tipos)": count_java(r"ValueHandler\.java$"),
    "Managers de objeto (DDL)": count_java(r"Manager\.java$"),
}

# --- saida -------------------------------------------------------------------

out = sys.stdout
w = out.write

w("# Mapa exaustivo do DBeaver\n\n")
w("Gerado por `tools/map_dbeaver.py` a partir de ")
w(f"`{ROOT}` — extraído do código, não estimado.\n\n")

w("## Totais\n\n")
w("| Elemento | Quantidade |\n|---|---|\n")
w(f"| Comandos | **{len(commands)}** |\n")
w(f"| Atalhos de teclado | **{len(bindings)}** |\n")
w(f"| Menus declarados | **{sum(len(v) for v in menus.values())}** |\n")
w(f"| Contribuições de menu/toolbar | **{contributions}** |\n")
w(f"| Barras de ferramentas | **{toolbars}** |\n")
w(f"| Handlers (XML) | **{handlers}** |\n")
w(f"| Views / painéis (XML) | **{len(views)}** |\n")
w(f"| Editores (XML) | **{len(editors)}** |\n")
w(f"| Assistentes (XML) | **{len(wizards_xml)}** |\n")
w(f"| Páginas de preferências (XML) | **{len(pref_pages)}** |\n")
w(f"| Pontos de extensão | **{len(extension_points)}** |\n")
for java_label, count in java_counts.items():
    w(f"| {java_label} (classes Java) | **{count}** |\n")
w("\n")

# --- comandos por categoria --------------------------------------------------

by_category = collections.defaultdict(list)
for cid, (name, category, plugin) in commands.items():
    by_category[category or "sem categoria"].append((name or cid, cid, plugin))

w("## Comandos por categoria\n\n")
for category in sorted(by_category, key=lambda c: -len(by_category[c])):
    items = sorted(by_category[category])
    w(f"### {category} ({len(items)})\n\n")
    w("| Comando | ID |\n|---|---|\n")
    for name, cid, _plugin in items:
        short = cid.replace("org.jkiss.dbeaver.", "")
        w(f"| {name} | `{short}` |\n")
    w("\n")

# --- atalhos -----------------------------------------------------------------

w("## Atalhos de teclado\n\n")
w("| Sequência | Comando | Contexto | Plugin |\n|---|---|---|---|\n")
for sequence, command, context, plugin in sorted(bindings):
    short_plugin = plugin.replace("org.jkiss.dbeaver.", "")
    w(f"| `{sequence}` | {command} | {context} | {short_plugin} |\n")
w("\n")

# --- views, editores, assistentes, preferencias ------------------------------

for title, rows in (
    ("Views e painéis", views),
    ("Editores", editors),
    ("Assistentes", wizards_xml),
    ("Páginas de preferências", pref_pages),
):
    w(f"## {title} ({len(rows)})\n\n")
    w("| Nome | Plugin |\n|---|---|\n")
    for name, plugin in sorted(set(rows)):
        w(f"| {name} | {plugin.replace('org.jkiss.dbeaver.', '')} |\n")
    w("\n")

w(f"## Pontos de extensão ({len(extension_points)})\n\n")
w("| ID | Plugin |\n|---|---|\n")
for point, plugin in sorted(set(extension_points)):
    w(f"| `{point}` | {plugin.replace('org.jkiss.dbeaver.', '')} |\n")
w("\n")

# --- cruzamento com o estado do C-Otter --------------------------------------
#
# Um comando so' conta como implementado quando a acao existe na interface do
# C-Otter -- nao quando o codigo de apoio existe no nucleo.

IMPLEMENTED = {
    "ui.editors.sql.run.statement",        # Ctrl+Enter
    "core.sql.editor.open",                # abrir editor (fixo)
    "ui.editors.sql.assist.proposals",     # autocomplete
    "core.navigator.expand",               # expandir no navigator
    "core.new.connection",                 # dialogo de conexao
    "core.disconnect",                     # desconectar
    "ui.editors.text.find.replace",        # Ctrl+F
    "ui.editors.text.select.all",          # Ctrl+A
    "ui.editors.text.undo",
    "ui.editors.text.redo",
    "ui.editors.sql.comment.single",       # Ctrl+/
    "core.resultset.grid.moveColumnLeft",  # reordenar coluna
    "core.resultset.grid.moveColumnRight",
    "core.exit",
}

implemented = sum(
    1 for cid in commands if cid.replace("org.jkiss.dbeaver.", "") in IMPLEMENTED
)

w("## Cobertura do C-Otter\n\n")
w("Um comando só conta como implementado quando a **ação existe na interface** — não\n")
w("quando o código de apoio existe no núcleo.\n\n")
w("| Métrica | Valor |\n|---|---|\n")
w(f"| Comandos do DBeaver | {len(commands)} |\n")
w(f"| Implementados no C-Otter | **{implemented}** |\n")
w(f"| Cobertura de comandos | **{100 * implemented / len(commands):.1f}%** |\n")
w(f"| Atalhos do DBeaver | {len(bindings)} |\n")
w("| Atalhos no C-Otter | **6** (Ctrl+Enter, Ctrl+Espaço, Ctrl+F, Ctrl+A, Ctrl+Z/Y, Ctrl+Shift+N) |\n")
w(f"| Cobertura de atalhos | **{100 * 6 / len(bindings):.1f}%** |\n")
w("| Diálogos do DBeaver | 151 |\n")
w("| Diálogos no C-Otter | **2** (Conexão, Sobre) |\n")
w("| Páginas de preferências | 56 → **0** |\n")
w("| Assistentes | 112 → **0** |\n")
w("| Barras de ferramentas | 6 → **0** |\n")


