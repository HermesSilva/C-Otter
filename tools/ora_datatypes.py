"""Gera lib/orawire/data_types.inc a partir do python-oracledb.

    python tools/ora_datatypes.py <pasta do python-oracledb>

Na negociacao (mensagem DATA_TYPES) o cliente Oracle declara ~320 trincas
(tipo, tipo de conversao, representacao). Nao ha' especificacao publica do
TNS/TTC (ADR 0027): a tabela vem do driver "thin" da propria Oracle, que e'
codigo aberto (UPL 1.0 / Apache 2.0). Copiar 320 linhas 'a mao seria a fonte
de erro; este script le a tabela e as constantes de la' e escreve so' os
numeros.
"""
import re
import sys
from pathlib import Path

root = Path(sys.argv[1]) / "src" / "oracledb"
table_file = root / "impl" / "thin" / "messages" / "data_types.pyx"
sources = [table_file, root / "base_impl.pxd"]

constants: dict[str, int] = {}
assign = re.compile(r"^\s*((?:TNS|ORA)_[A-Z0-9_]+)\s*=\s*(\d+)(?:\s*\+\s*(\d+))?\s*$")
for source in sources:
    for line in source.read_text(encoding="utf-8").splitlines():
        m = assign.match(line)
        if m:
            constants[m.group(1)] = int(m.group(2)) + int(m.group(3) or 0)

# Sobre o TEXTO da tabela, e nao linha a linha: as trincas de nome comprido
# quebram em duas linhas. A primeira versao lia por linha e perdia justamente
# essas -- TIMESTAMP WITH TIME ZONE, INTERVAL, BINARY_FLOAT/DOUBLE --, e o
# servidor respondia ORA-03115 a qualquer consulta com esses tipos.
text = table_file.read_text(encoding="utf-8")
table = text[text.index("DATA_TYPES = ["):]
table = table[:table.index("\n]\n")]

rows = []
for m in re.finditer(r"\[\s*([A-Z0-9_]+),\s*([A-Z0-9_]+),\s*([A-Z0-9_]+)\s*\]", table):
    values = [int(v) if v.isdigit() else constants[v] for v in m.groups()]
    if values[0] == 0:
        break
    rows.append((values, m.group(1)))

# A tabela da referencia tem uma trinca por "[" que abre: se a conta nao
# fechar, alguma escapou do padrao acima.
expected = table.count("[") - 2      # o "[" da lista e o terminador [0, 0, 0]
if len(rows) != expected:
    sys.exit(f"esperava {expected} trincas, li {len(rows)}")

out = Path(__file__).resolve().parent.parent / "lib" / "orawire" / "data_types.inc"
with out.open("w", encoding="utf-8", newline="\n") as f:
    f.write("// GERADO por tools/ora_datatypes.py -- nao editar 'a mao.\n")
    f.write("// Fonte: python-oracledb (Oracle, UPL 1.0 / Apache 2.0),\n")
    f.write("// src/oracledb/impl/thin/messages/data_types.pyx.\n")
    f.write("// {tipo, tipo de conversao, representacao}\n")
    for values, name in rows:
        f.write("{%d, %d, %d},  // %s\n" % (*values, name))

print(f"{len(rows)} tipos -> {out}")
