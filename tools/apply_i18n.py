"""Troca literais em portugues por TR("texto em ingles") nos fontes da UI.

Uso pontual: a i18n foi introduzida depois da UI, entao este script faz a
conversao em massa. Codigo novo ja' nasce com TR().

  python tools/apply_i18n.py
"""
import io
import re
import sys

# original em portugues -> texto em ingles (que vira a chave de traducao)
MAP = {
    # Dialogo de conexao
    "Navegador": "Navigator settings",
    "Mostrar todos os bancos": "Show all databases",
    "Mostrar bancos template": "Show template databases",
    "Mostrar bancos sem acesso": "Show inaccessible databases",
    "Desempenho": "Performance",
    "Ler estatísticas de tamanho": "Read size statistics",
    "Ler todos os tipos de dado": "Read all data types",
    "Ler colunas das chaves": "Read key columns",
    "Usar prepared statements": "Use prepared statements",
    "Role da sessão": "Session role",
    "Substituir fuso horário legado": "Replace legacy timezone",
    "Propriedades do driver": "Driver properties",
    "Parâmetros passados diretamente ao driver na conexão.":
        "Parameters passed directly to the driver on connect.",
    "Propriedade": "Property",
    "Valor": "Value",
    "propriedade": "property",
    "valor": "value",
    "Adicionar": "Add",
    "Usar túnel SSH": "Use SSH tunnel",
    "Usar SSL": "Use SSL",
    "Usar proxy SOCKS": "Use SOCKS proxy",
    "Não implementado.": "Not implemented.",
    "Host SSH": "SSH host",
    "Chave privada": "Private key",
    "Chave pública": "Public key",
    "Agente SSH": "SSH agent",
    "Modo": "Mode",
    "Certificado da CA": "CA certificate",
    "Certificado do cliente": "Client certificate",
    "Chave do cliente": "Client key",
    "Transações": "Transactions",
    "Conexão somente leitura": "Read-only connection",
    "Sessão": "Session",
    "Schema padrão": "Default schema",
    "Consultas de inicialização": "Initialization queries",
    "Conexão": "Connection",
    "Fechar conexões ociosas": "Close idle connections",
    "Identificação": "Identification",
    "Nome da conexão": "Connection name",
    "Descrição": "Description",
    "Pasta": "Folder",
    "Tipo de conexão": "Connection type",
    "Desenvolvimento": "Development",
    "Teste": "Test",
    "Produção": "Production",
    "Senha##ssh": "Password##ssh",
    "Usuário##ssh": "User##ssh",
    "Autenticação##ssh": "Authentication##ssh",
    "Porta##ssh": "Port##ssh",
    "Host##proxy": "Host##proxy",
    "Porta##proxy": "Port##proxy",
    "Usuário##proxy": "User##proxy",
    "Senha##proxy": "Password##proxy",
    "Timeout (s)##ssh": "Timeout (s)##ssh",
    "Keep-alive (s)##ssh": "Keep-alive (s)##ssh",
    "Timeout (s)": "Timeout (s)",
    "Intervalo (s)": "Interval (s)",
    "Keep-alive": "Keep-alive",
    "Auto-commit": "Auto-commit",

    # main_shell -- menus
    "Arquivo": "File",
    "Editar": "Edit",
    "Ajuda": "Help",
    "Nova conexão...": "New connection...",
    "Editar conexão...": "Edit connection...",
    "Desconectar": "Disconnect",
    "Sair": "Exit",
    "Desfazer": "Undo",
    "Refazer": "Redo",
    "Selecionar tudo": "Select all",
    "Localizar": "Find",
    "Executar": "Execute",
    "Demo do ImGui": "ImGui demo",
    "Sobre o C-Otter": "About C-Otter",

    # main_shell -- paineis
    "Nova conexão": "New connection",
    "nenhuma conexão": "no connection",
    "conecte-se para navegar o schema": "connect to browse the schema",
    "  carregando...": "  loading...",
    "Copiar nome": "Copy name",
    "auto-commit": "auto-commit",
    "transação manual": "manual transaction",
    "somente leitura": "read only",
    "Executar  (Ctrl+Enter)": "Execute  (Ctrl+Enter)",
    "sem sugestões": "no suggestions",
    "execute uma query para ver o resultado": "run a query to see the result",
    "comando executado": "command executed",
    "nenhuma query ainda": "no queries yet",
    "tempo": "time",
    "linhas": "rows",
    "estado": "state",
    "erro": "error",
    "Resultado": "Result",
    "Editar conexão": "Edit connection",
}


def convert(path: str) -> int:
    with io.open(path, encoding="utf-8") as handle:
        source = handle.read()

    changes = 0
    for portuguese, english in MAP.items():
        # Só literais completos entre aspas, para não pegar substrings.
        needle = '"' + portuguese + '"'
        if needle in source:
            source = source.replace(needle, 'TR("' + english + '")')
            changes += 1

    # TR(TR(x)) pode surgir se um literal já estava convertido.
    source = re.sub(r'TR\(TR\((".*?")\)\)', r'TR(\1)', source)

    with io.open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source)
    return changes


if __name__ == "__main__":
    targets = sys.argv[1:] or [
        "src/ui/connection_dialog.cpp",
        "src/ui/main_shell.cpp",
    ]
    for target in targets:
        print(f"{target}: {convert(target)} substituições")
