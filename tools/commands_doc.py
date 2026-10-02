"""Gera docs/EDITOR-COMMANDS.md a partir da tabela de src/ui/commands.cpp.

O documento e' o mapa dos comandos do editor SQL do DBeaver com o estado de
cada um no C-Otter (diretivas 1 e 3 do CLAUDE.md). Sai da MESMA tabela que o
programa usa para o menu, a barra e os atalhos: nao ha' como o documento dizer
uma tecla e o programa responder a outra.

    python tools/commands_doc.py
"""
import io
import re

SOURCE = 'src/ui/commands.cpp'
TARGET = 'docs/EDITOR-COMMANDS.md'

MODS = {'C': 'Ctrl', 'S': 'Shift', 'A': 'Alt', 'CS': 'Ctrl+Shift',
        'CA': 'Ctrl+Alt', 'CAS': 'Ctrl+Alt+Shift'}
KEYS = {'Backslash': '\\', 'LeftBracket': '[', 'RightBracket': ']', 'Slash': '/',
        'Apostrophe': "'", 'UpArrow': 'Up', 'DownArrow': 'Down',
        'LeftArrow': 'Left', 'RightArrow': 'Right', 'Equal': '=', 'Minus': '-',
        'Comma': ',', 'Period': '.', 'Semicolon': ';', 'GraveAccent': '`'}
STATE = {'ok': '✅', 'part': '🟡', 'todo': '⬜', 'out': '➖'}
CONTEXT = {'ed': 'editor', 'nav': 'navegador', 'gl': 'global', 'gr': 'grade'}
MODS['AS'] = 'Alt+Shift'
KEYS.update({'Escape': 'Esc', 'Backspace': 'Backspace', 'Insert': 'Insert',
             'Delete': 'Delete', 'GraveAccent': '`'})


def split_top(text):
    """Parte nas virgulas de nivel zero, respeitando strings e parenteses."""
    parts, depth, cur, i = [], 0, '', 0
    while i < len(text):
        c = text[i]
        if c == '"':
            j = i + 1
            while text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            cur += text[i:j + 1]
            i = j + 1
            continue
        if c in '({':
            depth += 1
        elif c in ')}':
            depth -= 1
        if c == ',' and depth == 0:
            parts.append(cur.strip())
            cur = ''
        else:
            cur += c
        i += 1
    if cur.strip():
        parts.append(cur.strip())
    return parts


def literal(text):
    return ''.join(m[1:-1].replace('\\"', '"').replace('\\\\', '\\')
                   for m in re.findall(r'"(?:[^"\\]|\\.)*"', text))


def chord(expr):
    expr = expr.strip()
    mods, key = '', expr
    if '|' in expr:
        mods, key = [p.strip() for p in expr.split('|')]
    key = key.replace('ImGuiKey_', '')
    key = KEYS.get(key, key)
    return (MODS[mods] + '+' if mods else '') + key


def keys(expr):
    expr = expr.strip()
    if expr == 'none':
        return ''
    inner = expr[expr.index('(') + 1:expr.rindex(')')]
    return ', '.join('`' + chord(c) + '`' for c in split_top(inner))


def main():
    text = io.open(SOURCE, encoding='utf-8').read()
    table = text[text.index('constexpr CommandInfo kCommands[] = {'):]
    table = table[table.index('{') + 1:]
    table = re.sub(r'//[^\n]*', '', table)

    rows, pos = [], 0
    while True:
        start = table.find('{Command::', pos)
        if start < 0:
            break
        depth, i = 0, start
        while True:
            c = table[i]
            if c == '"':
                i += 1
                while table[i] != '"':
                    i += 2 if table[i] == '\\' else 1
            elif c == '{':
                depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        fields = split_top(table[start + 1:i])
        pos = i + 1
        assert len(fields) == 9, fields
        rows.append({
            'name': fields[0].replace('Command::', ''),
            'id': literal(fields[1]),
            'label': literal(fields[2]),
            'context': CONTEXT[fields[3]],
            'dbeaver': keys(fields[4]),
            'otter': keys(fields[5]),
            'state': fields[7],
            'note': literal(fields[8]),
        })

    def is_grid(r):
        return r['id'].startswith('core.resultset.') or r['name'].startswith('grid_')

    areas = [
        ('Editor SQL', [r for r in rows if not is_grid(r)],
         '`plugins/org.jkiss.dbeaver.ui.editors.sql/plugin.xml`, mais os do editor de texto '
         'do Eclipse que o editor SQL herda (ir para a linha, maiúsculas/minúsculas, excluir linha)'),
        ('Grade de resultados', [r for r in rows if is_grid(r)],
         '`plugins/org.jkiss.dbeaver.ui.editors.data/plugin.xml` (`core.resultset.*`, 63 '
         'comandos), mais copiar, colar e selecionar tudo, que no DBeaver são do Eclipse'),
    ]

    def tally(group):
        return {k: sum(1 for r in group if r['state'] == k) for k in STATE}

    out = []
    w = out.append
    w('# Comandos do editor SQL e da grade de resultados')
    w('')
    w('> **Gerado** por `tools/commands_doc.py` a partir de `src/ui/commands.cpp`. Não editar à')
    w('> mão: alterar a tabela e gerar de novo.')
    w('')
    w('O denominador é o DBeaver (diretiva 4). Cada linha é um comando dele, com a tecla nos')
    w('dois perfis de atalho e o que difere quando difere.')
    w('')
    w('| Área | ✅ igual | 🟡 com diferença | ⬜ falta | ➖ fora | Total |')
    w('|---|---|---|---|---|---|')
    for title, group, _ in areas:
        t = tally(group)
        w('| %s | %d | %d | %d | %d | %d |' % (title, t['ok'], t['part'], t['todo'],
                                             t['out'], len(group)))
    t = tally(rows)
    w('| **Total** | **%d** | **%d** | **%d** | **%d** | **%d** |'
      % (t['ok'], t['part'], t['todo'], t['out'], len(rows)))
    w('')
    w('✅ = feito e com o mesmo efeito; 🟡 = funciona, com a diferença dita na nota;')
    w('⬜ = não implementado; ➖ = decisão registrada de não fazer, com a razão na nota.')
    w('')
    w('## Perfis de atalho')
    w('')
    w('Dois perfis, escolhidos em *Help → Keymap* e gravados em `settings.json`:')
    w('')
    w('- **DBeaver** (padrão): as teclas do DBeaver, para quem vem de lá.')
    w('- **C-Otter**: onde o DBeaver herda uma tecla estranha do Eclipse, a tecla que os editores')
    w('  atuais usam. As diferenças são as linhas em que as duas colunas não coincidem.')
    w('')
    w('A janela *Help → Keymap → Show shortcuts...* mostra esta mesma tabela dentro do programa.')
    w('')
    w('As teclas da **grade** só valem com a grade em foco — o contexto `resultset.focused` do')
    w('DBeaver. É por isso que `Ctrl+S` grava as alterações da grade ali e o script no editor,')
    w('e que `Ctrl+D` copia da linha de cima num e apaga a linha no outro.')
    w('')

    for title, group, source in areas:
        t = tally(group)
        bound = [r for r in group if r['dbeaver']]
        bound_done = [r for r in bound if r['state'] in ('ok', 'part')]
        w('## ' + title)
        w('')
        w('Fonte: ' + source + '.')
        w('')
        w('Dos %d comandos, %d respondem (✅ + 🟡). Dos %d que têm tecla no DBeaver, %d respondem'
          % (len(group), t['ok'] + t['part'], len(bound), len(bound_done)))
        w('à mesma tecla no perfil **DBeaver**.')
        w('')
        w('| | Comando | Contexto | DBeaver | C-Otter | Nota |')
        w('|---|---|---|---|---|---|')
        for r in group:
            w('| %s | %s | %s | %s | %s | %s |' % (
                STATE[r['state']], r['label'], r['context'], r['dbeaver'], r['otter'],
                r['note'].replace('|', '\\|')))
        w('')

    count = tally(rows)

    io.open(TARGET, 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
    print('%s: %d comandos (%d ok, %d parcial, %d faltando, %d fora)' % (
        TARGET, len(rows), count['ok'], count['part'], count['todo'], count['out']))


main()
