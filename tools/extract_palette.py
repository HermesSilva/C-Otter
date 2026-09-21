"""Extrai a paleta dominante do logo para gerar o tema da UI."""
from PIL import Image
from collections import Counter
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "Midia/Logo.png"
img = Image.open(path).convert("RGB")
img.thumbnail((400, 400))

# Quantiza levemente para agrupar tons quase iguais do anti-aliasing.
def q(c):
    return tuple((v // 12) * 12 for v in c)

pixels = img.get_flattened_data() if hasattr(img, "get_flattened_data") else img.getdata()
counts = Counter(q(p) for p in pixels)
total = sum(counts.values())

print(f"{'HEX':<10} {'RGB':<18} {'%':>6}")
print("-" * 36)
for color, n in counts.most_common(12):
    pct = 100.0 * n / total
    if pct < 0.4:
        continue
    r, g, b = color
    print(f"#{r:02X}{g:02X}{b:02X}   ({r:3d},{g:3d},{b:3d})   {pct:5.1f}%")
