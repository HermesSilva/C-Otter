"""Monta varias capturas (ou recortes delas) numa folha so'.

Conferir dez estados de tela um a um custa dez leituras; lado a lado, uma.

    python tools/sheet.py saida.png X Y LARGURA ALTURA COLUNAS a.png b.png ...

Cada imagem e' recortada no mesmo retangulo e numerada no canto.
"""
import sys

from PIL import Image, ImageDraw

out = sys.argv[1]
x, y, w, h, columns = map(int, sys.argv[2:7])
paths = sys.argv[7:]

rows = (len(paths) + columns - 1) // columns
sheet = Image.new("RGB", (w * columns, h * rows), (0, 0, 0))
draw = ImageDraw.Draw(sheet)

for i, path in enumerate(paths):
    tile = Image.open(path).convert("RGB").crop((x, y, x + w, y + h))
    ox, oy = (i % columns) * w, (i // columns) * h
    sheet.paste(tile, (ox, oy))
    draw.rectangle((ox, oy, ox + 22, oy + 16), fill=(200, 60, 0))
    draw.text((ox + 6, oy + 2), str(i + 1), fill=(255, 255, 255))

sheet.save(out)
