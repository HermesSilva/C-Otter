"""Copia os icones ORIGINAIS do DBeaver para o repositorio e gera o codigo que
os embute no executavel.

    python tools/embed_icons.py [caminho-do-repositorio-do-dbeaver]

Por que existe: o usuario pediu os icones do DBeaver em vez dos desenhados
("Nao e' possivel copiar e usar os icones originais do DBeaver?"). O DBeaver e'
Apache 2.0, que permite a copia com atribuicao -- ver assets/icons/dbeaver/NOTICE
e docs/LICENSES.md.

O que faz:
  1. copia cada arquivo do mapa abaixo para assets/icons/dbeaver/, com o nome
     do enumerador do C-Otter (o original fica registrado no NOTICE);
  2. gera src/ui/icon_assets.cpp: o texto de cada SVG e, para os PNG dos
     drivers, os pixels RGBA ja' decodificados (o C-Otter nao tem decodificador
     de PNG, e nao precisa de um so' para tres icones).

O SVG e' rasterizado EM EXECUCAO pela nanosvg, no tamanho exato do DPI. Nada de
atlas pre-renderizado: um icone de 16 px esticado para 24 borra.

O mapa: `<enumerador de ui/icons.hpp>: <caminho dentro de dbeaver/plugins>`.
Os ids vem do `<tree>` do plugin.xml do PostgreSQL (atributo icon="#id",
resolvido por DBIcon.java) e dos `commandImages` dos plugin.xml de UI.
Onde o DBeaver nao declara icone (ou declara um id que DBIcon nao conhece),
ele mostra a pasta ou a pagina generica -- e e' isso que o mapa repete.
"""
import io
import os
import shutil
import sys

from PIL import Image

MODEL = 'org.jkiss.dbeaver.model/icons/'
UI = 'org.jkiss.dbeaver.ui/icons/'
DATA = 'org.jkiss.dbeaver.ui.editors.data/icons/'

MAP = {
    # --- Acoes ---------------------------------------------------------------
    'connect':       UI + 'sql/connect.svg',
    'disconnect':    UI + 'sql/disconnect.svg',
    'play':          UI + 'sql/sql_exec.svg',
    'stop':          UI + 'sql/sql_cancel.svg',
    'commit':        UI + 'sql/commit.svg',
    'rollback':      UI + 'sql/rollback.svg',
    'refresh':       UI + 'refresh.svg',
    'search':        UI + 'misc/search.svg',
    'settings':      UI + 'configuration.svg',
    'plus':          UI + 'create.svg',
    'pin':           'org.jkiss.dbeaver.ui.editors.data/icons/pin.svg',
    'save':          UI + 'file/save.svg',
    'first_page':    UI + 'sql/resultset_first.svg',
    'chevron_left':  UI + 'sql/resultset_previous.svg',
    'chevron_right': UI + 'sql/resultset_next.svg',
    'last_page':     UI + 'sql/resultset_last.svg',
    'warning':       MODEL + 'status/warning.svg',
    'error':         MODEL + 'status/error.svg',
    'info':          MODEL + 'status/info.svg',
    'lock':          MODEL + 'tree/locked.svg',
    # Visao de registro: o DBeaver usa `details` (RS_DETAILS); `grid` e' o
    # modo tabela.
    'record':        UI + 'sql/details.svg',
    'grid_mode':     UI + 'sql/grid.svg',

    # A grade de resultado.
    'accept':            UI + 'sql/accept.svg',
    'reject':            UI + 'misc/cancel.svg',
    'row_add':           UI + 'sql/row_add.svg',
    'row_copy':          UI + 'sql/row_copy.svg',
    'row_edit':          UI + 'sql/row_edit.svg',
    'row_delete':        UI + 'sql/row_delete.svg',
    'panels':            UI + 'sql/panel_customize.svg',
    'panel_calc':        DATA + 'panel_aggregate.svg',
    'panel_grouping':    DATA + 'panel_grouping.svg',
    'panel_metadata':    DATA + 'panel_metadata.svg',
    'panel_references':  DATA + 'panel_references.svg',
    'filter_apply':      UI + 'misc/filter_apply.svg',
    'filter_reset':      UI + 'misc/filter_reset.svg',
    'filter_config':     UI + 'misc/filter_config.svg',
    'filter_value':      UI + 'misc/filter_value.svg',
    'filter':        UI + 'misc/filter.svg',

    # --- Arvore: objetos ----------------------------------------------------------
    'database':          MODEL + 'tree/database.svg',
    'schema':            MODEL + 'tree/schema.svg',
    'table':             MODEL + 'tree/table.svg',
    'foreign_table':     MODEL + 'tree/table.svg',
    'view':              MODEL + 'tree/view.svg',
    'materialized_view': MODEL + 'tree/view.svg',
    'column':            MODEL + 'tree/column.svg',
    'key':               MODEL + 'tree/key.svg',
    'constraint':        MODEL + 'tree/unique_constraint.svg',
    'foreign_key':       MODEL + 'tree/foreign_key.svg',
    'index':             MODEL + 'tree/index.svg',
    'references':        MODEL + 'tree/reference.svg',
    'dependency':        MODEL + 'tree/reference.svg',
    'partition':         MODEL + 'tree/partition.svg',
    'trigger':           MODEL + 'tree/trigger.svg',
    'event_trigger':     MODEL + 'tree/trigger.svg',
    'policy':            MODEL + 'tree/locked.svg',
    'sequence':          MODEL + 'tree/sequence.svg',
    'function':          MODEL + 'tree/function.svg',
    'aggregate':         MODEL + 'tree/function.svg',
    'procedure':         MODEL + 'tree/procedure.svg',
    'parameter':         MODEL + 'tree/argument.svg',
    'data_type':         MODEL + 'tree/data_type.svg',
    'extension':         MODEL + 'tree/package.svg',
    'extension_available': MODEL + 'tree/package.svg',
    'tablespace':        MODEL + 'tree/tablespace.svg',
    'role':              MODEL + 'tree/user.svg',
    'user':              MODEL + 'tree/user.svg',
    'role_group':        MODEL + 'tree/group.svg',
    'grant':             MODEL + 'tree/permissions.svg',
    'event':             MODEL + 'tree/event.svg',
    'sessions':          MODEL + 'tree/sessions.svg',
    'locks':             MODEL + 'tree/locked.svg',
    'synonym':           MODEL + 'tree/synonym.svg',
    'job':               MODEL + 'tree/job.svg',
    'job_step':          MODEL + 'tree/job.svg',
    'job_schedule':      MODEL + 'tree/job_schedule.svg',
    'foreign_server':    MODEL + 'tree/server.svg',
    'generic_server':    MODEL + 'tree/server.svg',

    # Sem icone declarado no DBeaver (ou com um id que DBIcon nao conhece):
    # la' aparecem com a pagina generica.
    'object_page':       MODEL + 'tree/page.svg',
    'rule':              MODEL + 'tree/page.svg',
    'foreign_wrapper':   MODEL + 'tree/page.svg',
    'user_mapping':      MODEL + 'tree/page.svg',
    'setting':           MODEL + 'tree/page.svg',
    'access_method':     MODEL + 'tree/page.svg',
    'operator_class':    MODEL + 'tree/page.svg',
    'operator_family':   MODEL + 'tree/page.svg',
    'encoding':          MODEL + 'tree/page.svg',
    'collation':         MODEL + 'tree/page.svg',
    'language':          MODEL + 'tree/page.svg',

    # --- Arvore: pastas -------------------------------------------------------------
    'folder':            MODEL + 'tree/folder.svg',
    'folder_database':   MODEL + 'tree/folder_database.svg',
    'folder_schema':     MODEL + 'tree/folder_schema.svg',
    'folder_table':      MODEL + 'tree/folder_table.svg',
    'folder_view':       MODEL + 'tree/folder_view.svg',
    'folder_link':       MODEL + 'tree/folder_link.svg',
    'folder_user':       MODEL + 'tree/folder_user.svg',
    'folder_constraint': MODEL + 'tree/folder_constraint.svg',
    'folder_columns':    MODEL + 'tree/columns.svg',
    'folder_admin':      MODEL + 'tree/folder_admin.svg',
    'folder_info':       MODEL + 'tree/folder_info.svg',
    'administer':        MODEL + 'tree/folder_admin.svg',
    'system_info':       MODEL + 'tree/folder_info.svg',
    'storage':           MODEL + 'tree/folder_info.svg',

    # --- Barra lateral do editor SQL ------------------------------------------------
    'play_new':      UI + 'sql/sql_exec_new.svg',
    'play_script':   UI + 'sql/sql_script_exec.svg',
    'plan':          UI + 'sql/sql_plan.svg',
    'ai':            'org.jkiss.dbeaver.model.ai/icons/ai.svg',
    'terminal':      UI + 'sql/sql_console.svg',
    'server_output': UI + 'sql/page_output.svg',
    'exec_log':      UI + 'sql/page_error.svg',
    'variables':     UI + 'sql/variable.svg',
    'outline':       UI + 'sql/toggle_outline.svg',

    # --- Drivers (PNG; o @2x vai junto) ---------------------------------------------
    'pg_server': 'org.jkiss.dbeaver.ext.postgresql/icons/postgresql_icon.png',
    'my_server': 'org.jkiss.dbeaver.ext.mysql/icons/mysql_icon.png',
    'ms_server': 'org.jkiss.dbeaver.ext.mssql/icons/mssql_icon.png',
    'sa_server': 'org.jkiss.dbeaver.ext.mssql/icons/sybase_icon.png',
}

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ASSETS = os.path.join(ROOT, 'assets', 'icons', 'dbeaver')
OUTPUT = os.path.join(ROOT, 'src', 'ui', 'icon_assets.cpp')


def rgba_array(path):
    """Os pixels RGBA do PNG, ja' decodificados, e o lado da imagem."""
    image = Image.open(path).convert('RGBA')
    assert image.width == image.height, path
    return image.width, image.tobytes()


def c_bytes(data):
    lines = []
    for i in range(0, len(data), 20):
        lines.append('    ' + ','.join(str(b) for b in data[i:i + 20]) + ',')
    return '\n'.join(lines)


def c_raw_string(text):
    # O MSVC limita cada literal a ~16 KB; literais adjacentes sao concatenados.
    pieces = []
    for i in range(0, len(text), 12000):
        piece = text[i:i + 12000]
        assert ')otter"' not in piece
        pieces.append('R"otter(' + piece + ')otter"')
    return '\n    '.join(pieces)


def main():
    plugins = os.path.join(sys.argv[1] if len(sys.argv) > 1
                           else r'D:\Tootega\Source\dbeaver', 'plugins')
    os.makedirs(ASSETS, exist_ok=True)

    notice = [
        'Icons copied from DBeaver (https://github.com/dbeaver/dbeaver),',
        'Copyright (C) 2010-2025 DBeaver Corp and others.',
        'Licensed under the Apache License, Version 2.0',
        '(http://www.apache.org/licenses/LICENSE-2.0).',
        '',
        'The files are unmodified; only the names changed, to the name of the',
        'C-Otter icon each one is used for. Regenerate with tools/embed_icons.py.',
        '',
        'The PostgreSQL and MySQL icons are trademarks of their owners and are',
        'used only to identify the database a connection talks to.',
        '',
        'file <- original (under dbeaver/plugins)',
        '',
    ]

    arrays = []
    rows = []

    for name in sorted(MAP):
        source = os.path.join(plugins, MAP[name].replace('/', os.sep))
        assert os.path.exists(source), source
        extension = os.path.splitext(source)[1]

        target = os.path.join(ASSETS, name + extension)
        shutil.copyfile(source, target)
        notice.append('%s%s <- %s' % (name, extension, MAP[name]))

        if extension == '.svg':
            text = io.open(source, encoding='utf-8').read()
            arrays.append('const char kSvg_%s[] =\n    %s;\n'
                          % (name, c_raw_string(text)))
            rows.append('    {Icon::%s, kSvg_%s, nullptr, 0, nullptr, 0},'
                        % (name, name))
        else:
            size1, data1 = rgba_array(source)
            arrays.append('const unsigned char kPng_%s[] = {\n%s\n};\n'
                          % (name, c_bytes(data1)))

            # A versao @2x, para telas de alta densidade.
            double = source.replace(extension, '@2x' + extension)
            size2, second = 0, 'nullptr'
            if os.path.exists(double):
                shutil.copyfile(double, os.path.join(ASSETS, name + '@2x' + extension))
                size2, data2 = rgba_array(double)
                arrays.append('const unsigned char kPng_%s_2x[] = {\n%s\n};\n'
                              % (name, c_bytes(data2)))
                second = 'kPng_%s_2x' % name
            rows.append('    {Icon::%s, nullptr, kPng_%s, %d, %s, %d},'
                        % (name, name, size1, second, size2))

    io.open(os.path.join(ASSETS, 'NOTICE'), 'w', encoding='utf-8',
            newline='\n').write('\n'.join(notice) + '\n')

    out = [
        '// GERADO por tools/embed_icons.py -- nao editar a mao.',
        '//',
        '// Os icones originais do DBeaver (Apache 2.0), embutidos no executavel:',
        '// o texto de cada SVG e os pixels dos PNG de driver. Os arquivos estao em',
        '// assets/icons/dbeaver/, com a atribuicao no NOTICE de la.',
        '#include "ui/icon_assets.hpp"',
        '',
        'namespace otter::ui {',
        'namespace {',
        '',
    ]
    out.extend(arrays)
    out.append('const IconAsset kAssets[] = {')
    out.extend(rows)
    out.append('};')
    out.append('')
    out.append('} // namespace')
    out.append('')
    out.append('std::span<const IconAsset> icon_assets() { return kAssets; }')
    out.append('')
    out.append('} // namespace otter::ui')

    io.open(OUTPUT, 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
    print('%d icones em %s' % (len(MAP), OUTPUT))


if __name__ == '__main__':
    main()
