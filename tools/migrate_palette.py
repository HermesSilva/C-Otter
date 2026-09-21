"""Migra `palette::x` (constantes globais) para `colors().x` (tema ativo).

Uso pontual: o sistema de temas substituiu as constantes por campos do tema
ativo, para que a troca de tema funcione em tempo real.

  python tools/migrate_palette.py
"""
import io
import re
import sys

# nome antigo -> novo campo em Palette
RENAMES = {
    "fur": "accent",
    "fur_light": "accent_light",
    "fur_dark": "accent_dark",
    "cream": "text_bright",
    "water": "accent_dark",
}


def convert(path: str) -> int:
    with io.open(path, encoding="utf-8") as handle:
        source = handle.read()

    def replace(match: re.Match) -> str:
        name = match.group(1)
        return "colors()." + RENAMES.get(name, name)

    source, count = re.subn(r"palette::(\w+)", replace, source)

    with io.open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source)
    return count


if __name__ == "__main__":
    targets = sys.argv[1:] or [
        "src/ui/main_shell.cpp",
        "src/ui/connection_dialog.cpp",
        "src/ui/app_window_glfw.cpp",
    ]
    for target in targets:
        print(f"{target}: {convert(target)} substituições")
