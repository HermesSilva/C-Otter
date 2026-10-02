"""Lista as chaves de TR()/TRW() dos fontes que nao estao no catalogo pt-BR."""
import glob, io, re, sys, json

LIT = r'"(?:[^"\\]|\\.)*"'

def unescape(s):
    return bytes(s, 'utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8')

def literal(group):
    # literais adjacentes sao concatenados pelo compilador
    return ''.join(unescape(m[1:-1]) for m in re.findall(LIT, group))

catalog = io.open('src/base/i18n_pt_br.cpp', encoding='utf-8').read()
body = catalog[catalog.index('kPtBr[] = {'):catalog.index('void register_pt_br')]
def tokens(text):
    """Literais (com adjacentes unidos) separados por virgula; pula comentarios."""
    out, cur, i, n = [], None, 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            i = text.index('\n', i) if '\n' in text[i:] else n
        elif c == '"':
            j = i + 1
            while text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            cur = (cur or '') + unescape(text[i + 1:j])
            i = j + 1
        elif c == ',':
            if cur is not None:
                out.append(cur)
            cur = None
            i += 1
        else:
            i += 1
    if cur is not None:
        out.append(cur)
    return out

lits = tokens(body[body.index('{') + 1:])
assert len(lits) % 2 == 0, len(lits)
known = set(lits[0::2])

keys = {}
for path in glob.glob('src/**/*.cpp', recursive=True) + glob.glob('src/**/*.hpp', recursive=True):
    text = io.open(path, encoding='utf-8').read()
    text_nc = re.sub(r'//[^\n]*', '', text)
    for m in re.finditer(r'\bTRW?\(\s*((?:' + LIT + r'\s*)+)', text_nc):
        keys.setdefault(literal(m.group(1)), path)

# A tabela de comandos: rotulo (3o literal... ) e nota passam por TR(info.label/note).
cmd = io.open('src/ui/commands.cpp', encoding='utf-8').read()
cmd = re.sub(r'//[^\n]*', '', cmd)
for m in re.finditer(r'\{Command::\w+,\s*' + LIT + r',\s*((?:' + LIT + r'\s*)+),(.*?)\},\s*(?=\{Command::|\};)', cmd, re.S):
    keys.setdefault(literal(m.group(1)), 'commands.cpp:label')
    rest = m.group(2)
    tail = re.search(r'((?:' + LIT + r'\s*)+)$', rest.strip())
    if tail:
        note = literal(tail.group(1))
        if note:
            keys.setdefault(note, 'commands.cpp:note')

missing = sorted(k for k in keys if k and k not in known)
out = io.open('build/missing_i18n.txt', 'w', encoding='utf-8')
for k in missing:
    out.write(json.dumps(k, ensure_ascii=False) + '   # ' + keys[k].replace('\\', '/') + '\n')
print(len(keys), 'chaves;', len(missing), 'sem traducao')
