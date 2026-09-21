"""Remove entradas repetidas do catalogo pt-BR.

Repeticao com a MESMA traducao e' lixo e sai em silencio. Repeticao com
traducoes DIFERENTES e' defeito -- qual vence depende da ordem de insercao no
mapa -- e e' reportada para decisao humana, nunca resolvida por conta propria.

    python tools/dedupe_catalog.py [--apply]
"""

import io
import re
import sys

PATH = "src/base/i18n_pt_br.cpp"

# Uma entrada e' um par de literais numa linha: "chave", "traducao",
ENTRY = re.compile(r'^(\s*)("(?:[^"\\]|\\.)*"),\s*("(?:[^"\\]|\\.)*"),\s*$')


def main() -> int:
    apply = "--apply" in sys.argv

    lines = io.open(PATH, encoding="utf-8").read().split("\n")
    seen = {}
    out = []
    removed = 0
    conflicts = []

    for line in lines:
        match = ENTRY.match(line)
        if not match:
            out.append(line)
            continue

        key, value = match.group(2), match.group(3)
        if key in seen:
            if seen[key] == value:
                removed += 1
                continue
            conflicts.append((key, seen[key], value))
            out.append(line)
            continue

        seen[key] = value
        out.append(line)

    for key, first, second in conflicts:
        print(f"CONFLITO {key}: {first} vs {second}")

    print(f"{removed} repeticao(oes) identica(s), {len(conflicts)} conflito(s)")

    if apply and removed:
        io.open(PATH, "w", encoding="utf-8").write("\n".join(out))
        print("aplicado")

    return 1 if conflicts else 0


if __name__ == "__main__":
    raise SystemExit(main())
