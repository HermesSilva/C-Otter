"""Recorta e amplia um pedaco de uma captura de tela.

Sobreposicao de dois pixels, atalho colado no rotulo, icone em cima da seta:
nada disso aparece numa captura inteira reduzida. Recortar e ampliar antes de
dar um efeito visual por conferido (diretivas 2 e 13 do CLAUDE.md).

    python tools/crop.py captura.png recorte.png X Y LARGURA ALTURA FATOR
"""
import sys

from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
x, y, w, h, k = map(int, sys.argv[3:8])
Image.open(src).crop((x, y, x + w, y + h)).resize((w * k, h * k), Image.LANCZOS).save(dst)
