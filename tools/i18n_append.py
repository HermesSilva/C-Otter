"""Acrescenta pares [ingles, traducao] ao catalogo pt-BR embutido.

Uso, de um script da fase:

    from i18n_append import append
    append('Grade de resultados', [("Add row", "Adicionar linha"), ...])

Chave que ja' existe e' pulada (com aviso): o teste do catalogo reprova
chave repetida. `tools/missing_i18n.py` lista o que ainda falta.
"""
import io

CATALOG = 'src/base/i18n_pt_br.cpp'


def _unescape(s):
    return bytes(s, 'utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8')


def _keys(text):
    body = text[text.index('kPtBr[] = {'):text.index('void register_pt_br')]
    body = body[body.index('{') + 1:]
    out, cur, i, n = [], None, 0, len(body)
    while i < n:
        c = body[i]
        if body.startswith('//', i):
            j = body.find('\n', i)
            i = n if j < 0 else j
        elif c == '"':
            j = i + 1
            while body[j] != '"':
                j += 2 if body[j] == '\\' else 1
            cur = (cur or '') + _unescape(body[i + 1:j])
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
    return set(out[0::2])


def _c(s):
    return s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')


def append(section, pairs):
    text = io.open(CATALOG, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in text else '\n'
    known = _keys(text)

    lines = [nl + '    // --- %s ---' % section]
    seen = set()
    added = 0
    for key, value in pairs:
        if key in seen:
            raise SystemExit('repetida na lista: ' + key)
        seen.add(key)
        if key in known:
            print('ja existe:', key)
            continue
        lines.append('    "%s",%s        "%s",' % (_c(key), nl, _c(value)))
        added += 1

    end = text.index('};', text.index('kPtBr[] = {'))
    text = text[:end].rstrip() + nl + nl.join(lines) + nl + text[end:]
    io.open(CATALOG, 'w', encoding='utf-8', newline='').write(text)
    print('acrescentadas', added)
