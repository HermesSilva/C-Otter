"""Regenera os documentos gerados: DBEAVER-MAP.md e EDITOR-COMMANDS.md.

    python tools/regen_docs.py

`map_dbeaver.py` escreve em stdout; redirecionar no PowerShell grava UTF-16 e
quebra os acentos, por isso a gravacao e' feita aqui, em bytes.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
env = dict(os.environ, PYTHONIOENCODING='utf-8')

out = subprocess.run([sys.executable, os.path.join(HERE, 'map_dbeaver.py')], env=env,
                     capture_output=True, cwd=ROOT)
if out.returncode != 0:
    sys.stderr.buffer.write(out.stderr)
    sys.exit(out.returncode)
open(os.path.join(ROOT, 'docs', 'DBEAVER-MAP.md'), 'wb').write(out.stdout)

text = out.stdout.decode('utf-8')
summary = text.split('## Cobertura do C-Otter')[1].split('## Comandos que faltam')[0]
sys.stdout.buffer.write(summary.strip().encode('utf-8') + b'\n')

doc = subprocess.run([sys.executable, os.path.join(HERE, 'commands_doc.py')], env=env,
                     capture_output=True, cwd=ROOT)
sys.stdout.buffer.write(doc.stdout)
sys.stderr.buffer.write(doc.stderr)
sys.exit(doc.returncode)
