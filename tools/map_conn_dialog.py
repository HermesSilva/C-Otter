"""Extrai a arvore de paginas do dialogo de conexao do DBeaver.

O dialogo "Connection <nome> configuration" nao e' uma lista fixa: ele e'
montado a partir das <page> declaradas nos plugin.xml, ligadas por
`category`. Este script reconstroi essa hierarquia e resolve cada rotulo
pelo bundle.properties do plugin que declarou a pagina -- os rotulos vao
para a UI, entao precisam ser os oficiais, nao traduzidos de memoria
(diretriz 1).

  python tools/map_conn_dialog.py > docs/CONN-DIALOG-TREE.md
"""
import os
import re
import sys
import xml.etree.ElementTree as ET

DBEAVER = r"D:\Tootega\Source\dbeaver"

# Raizes que o dialogo de conexao mostra. O dialogo global de preferencias
# tem mais; estas sao as alcancaveis a partir de uma conexao.
ROOTS = [
    "org.jkiss.dbeaver.preferences.main.connections",
    "org.jkiss.dbeaver.preferences.main.resultset",
    "org.jkiss.dbeaver.preferences.main.sqleditor",
    "org.jkiss.dbeaver.preferences.main.dataformat",
]


def load_bundle(plugin_dir):
    """Rotulos do plugin. Chave sem o '%' inicial."""
    path = os.path.join(plugin_dir, "OSGI-INF", "l10n", "bundle.properties")
    labels = {}
    if not os.path.isfile(path):
        return labels
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            labels[key.strip()] = value.strip()
    return labels


def collect():
    pages = {}   # id -> {name, category, plugin}
    for root, dirs, files in os.walk(os.path.join(DBEAVER, "plugins")):
        if "plugin.xml" not in files:
            continue
        labels = load_bundle(root)
        plugin = os.path.basename(root)
        try:
            tree = ET.parse(os.path.join(root, "plugin.xml"))
        except ET.ParseError:
            continue
        for page in tree.iter("page"):
            pid = page.get("id")
            if not pid or not pid.startswith("org.jkiss.dbeaver.preferences"):
                continue
            name = page.get("name", "")
            if name.startswith("%"):
                name = labels.get(name[1:], name)
            # A mesma pagina e' declarada mais de uma vez (dialogo global e
            # dialogo de conexao). Fica a primeira que trouxe rotulo legivel.
            if pid in pages and not pages[pid]["name"].startswith("%"):
                continue
            pages[pid] = {
                "name": name,
                "category": page.get("category", ""),
                "plugin": plugin,
            }
    return pages


def main():
    pages = collect()
    children = {}
    for pid, info in pages.items():
        children.setdefault(info["category"], []).append(pid)

    out = sys.stdout.write
    out("# Arvore de paginas do dialogo de conexao (DBeaver)\n\n")
    out("Gerado por `tools/map_conn_dialog.py`. Rotulos vindos do\n")
    out("`bundle.properties` de cada plugin, nao traduzidos de memoria.\n\n")

    total = 0

    def walk(pid, depth):
        nonlocal total
        info = pages.get(pid)
        if not info:
            return
        total += 1
        out("%s- **%s** `%s`\n" % ("  " * depth, info["name"],
                                   pid.replace("org.jkiss.dbeaver.preferences.", "")))
        for child in sorted(children.get(pid, []),
                            key=lambda c: pages[c]["name"]):
            walk(child, depth + 1)

    for root in ROOTS:
        walk(root, 0)

    out("\nTotal de paginas alcancaveis: **%d**\n" % total)


if __name__ == "__main__":
    main()
