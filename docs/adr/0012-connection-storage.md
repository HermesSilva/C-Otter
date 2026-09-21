# ADR 0012 — Persistência de conexões e senhas

**Data:** 2026-09-21
**Status:** Aceito

## Contexto

As conexões não sobrevivem ao fechamento do programa: host, porta, banco e usuário são
redigitados a cada execução. É o defeito mais incômodo do uso diário.

Pedido do usuário: **mesmo formato do DBeaver, e importar as conexões já existentes**.
A máquina de desenvolvimento tem um workspace real em
`%APPDATA%\DBeaverData\workspace6\General\.dbeaver\` com dezenas de conexões MySQL e
PostgreSQL cadastradas.

## Como o DBeaver guarda

Dois arquivos, separando o que é público do que é secreto:

| Arquivo | Conteúdo | Proteção |
|---------|----------|----------|
| `data-sources.json` | Host, porta, banco, usuário, driver, propriedades | Texto claro |
| `credentials-config.json` | Senhas | AES-128-CBC |

O formato de `data-sources.json` é um objeto `connections`, com uma chave por conexão:

```json
{
  "folders": {},
  "connections": {
    "postgres-jdbc-19f2...": {
      "provider": "postgresql",
      "driver": "postgres-jdbc",
      "name": "ERP_TID",
      "save-password": true,
      "configuration": {
        "host": "localhost", "port": "5432", "database": "ERP_TID",
        "url": "jdbc:postgresql://localhost:5432/ERP_TID",
        "type": "dev", "auth-model": "native",
        "properties": { ... }, "provider-properties": { ... }
      }
    }
  }
}
```

### Configuração de rede: `handlers`

O SSL e o túnel SSH **não ficam em `properties`**. São *network handlers*, cada
um com id próprio e um mapa de propriedades dentro de `configuration.handlers`.
Extraído de `DataSourceSerializerModern.java:727` (leitura) e `:1203` (escrita).

O id e as chaves mudam por driver — não há um formato comum:

| Driver | Id do handler | Chaves | Fonte |
|--------|---------------|--------|-------|
| PostgreSQL | `postgre_ssl` | `sslMode`: `disable`/`require`/`verify-ca`/`verify-full` | `PostgreConstants.java:129` e `:68` |
| MySQL | `mysql_ssl` | `ssl.require` e `ssl.verify.server`, dois booleanos | `MySQLConstants.java:38` e `:43-44` |

Certificados, comuns aos dois: `ssl.ca.cert`, `ssl.client.cert`,
`ssl.client.key`.

```json
"handlers": {
  "mysql_ssl": {
    "type": "CONFIG",
    "enabled": true,
    "properties": { "ssl.require": true, "ssl.verify.server": false }
  }
}
```

O `enabled` manda: o DBeaver **omite** handler desabilitado ao gravar, e um
handler presente com `enabled: false` é resto de configuração desligada. O
C-Otter segue as duas regras — sem isso, um perfil com a caixa desmarcada na
tela exigiria TLS.

A criptografia das credenciais, extraída de `DefaultValueEncryptor.java` e
`BaseProjectImpl.java`:

- **AES/CBC/PKCS5Padding**, chave de 128 bits
- IV de 16 bytes **no início do arquivo**, seguido do texto cifrado
- Chave **fixa e pública no código-fonte** do DBeaver:
  `ba bb 4a 9f 77 4a b8 53 c9 6c 2d 65 3d fe 54 4a`

## Decisão

**Ler e escrever o formato do DBeaver, incluindo a criptografia.**

### O que isso significa sobre segurança — dito sem rodeios

A chave é uma constante num projeto de código aberto. Qualquer pessoa com acesso ao arquivo
decifra as senhas em segundos. **É ofuscação, não proteção.** Vale exatamente contra olhar
casual — alguém abrindo o arquivo no editor — e contra nada além disso.

Aceitamos porque o requisito é interoperar com o DBeaver, e interoperar significa falar o
mesmo formato, defeitos inclusive. Mas o programa **diz isso ao usuário** na tela, ao lado
de "salvar senha", em vez de deixá-lo supor que há um cofre por trás.

Um modo mais seguro — DPAPI no Windows, que amarra o segredo à conta do usuário do sistema —
fica registrado como trabalho futuro. Ele **quebra a compatibilidade** com o DBeaver por
definição: um arquivo que só a sua conta decifra não pode ser lido por outro programa.

### Onde o C-Otter guarda o que é seu

```
%APPDATA%\C-Otter\
    data-sources.json        mesmo formato do DBeaver
    credentials-config.json  mesmo formato, mesma chave
```

Arquivo separado do workspace do DBeaver **de propósito**. Escrever no diretório dele
arriscaria corromper a configuração de uma ferramenta que o usuário usa para trabalhar, e um
formato que ainda não implementamos por inteiro (pastas, credenciais de SSH, perfis de rede)
perderia campos na primeira gravação.

### Importação

Menu **Arquivo → Importar do DBeaver**. Lê o workspace real, lista o que encontrou, e o
usuário escolhe o que trazer. Conexões de SGBDs ainda não suportados aparecem **com o
motivo**, não escondidas — o usuário precisa saber que o C-Otter as viu e por que não pode
usá-las ainda.

A importação **nunca escreve** no diretório do DBeaver. Só lê.

## Consequências

- As conexões sobrevivem ao fechamento, que era o objetivo
- Quem já usa DBeaver não redigita nada
- A senha em disco tem a mesma proteção fraca do DBeaver, **declarada na tela**
- Campos do DBeaver que ainda não entendemos são **preservados** ao regravar um perfil
  importado, em vez de descartados

## Alternativas rejeitadas

**Formato próprio.** Mais limpo, e impediria a importação, que era o pedido.

**DPAPI agora.** Mais seguro e incompatível. Fica como opção futura por conexão: quem não
precisa de interoperabilidade escolhe o cofre do sistema.

**Não salvar senha.** É o padrão para conexões marcadas como *Produção* — ver a aba Geral do
assistente. Para desenvolvimento local, exigir a senha a cada início seria atrito sem ganho.
