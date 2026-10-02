# ADR 0023 — Importar conexões de outras ferramentas na primeira execução

**Estado:** aceito (2026-10-01)

## Contexto

Pedido do usuário:

> "Todas as configurações e as conexões salvas devem ficar na pasta `.C-Otter`.
> Ao abrir e a pasta não existir ou estiver vazia deve copiar as conexões salvas,
> se existirem, do DBeaver, do pgAdmin e do MS SQL Server Management Studio."

A primeira parte já era o ADR 0020 (produto portátil). A segunda é nova: até aqui só
havia *File → Import from DBeaver*, manual.

## Decisão

Na **primeira execução** — a pasta `.C-Otter` não existe, ou não tem nem
`data-sources.json` nem `settings.json` — o programa lê as conexões salvas de três
ferramentas e as grava na sua própria pasta. Nunca escreve na pasta de nenhuma delas.

| Origem | Arquivo | O que vem | O que **não** vem |
|---|---|---|---|
| DBeaver | `DBeaverData/workspace*/<projeto>/.dbeaver/data-sources.json` + `credentials-config.json` | tudo o que `load_profiles` já lia, senhas inclusive | — |
| pgAdmin 4 | `%APPDATA%\pgAdmin\pgadmin4.db`, tabela `server` | nome, host, porta, banco, usuário, comentário, role, `sslmode`, timeout, túnel SSH e **a senha** (desde 2026-10-01, ver abaixo) | a senha de quem usa a senha-mestra *digitada* do pgAdmin; a senha do túnel SSH |
| SSMS 18–21 | `%APPDATA%\Microsoft\SQL Server Management Studio\<versão>\UserSettings.xml` | servidor (host, porta, instância), usuário, banco | a senha: é cifrada pelo Windows (DPAPI) para o SSMS |

Regras:

- **Só na primeira execução.** Quem apagou as conexões de propósito não as quer de
  volta a cada abertura; depois, a importação é pelo menu.
- A pasta antiga do próprio C-Otter (`%APPDATA%\C-Otter`, ADR 0020) **não** é mais
  copiada: ela ressuscitava conexões de teste a cada pasta nova, e o usuário pediu a
  remoção (2026-10-01). Só as conexões das outras ferramentas entram sozinhas.
- O mesmo alvo (driver + host + porta + banco + usuário) entra uma vez só; nomes
  repetidos ganham sufixo `_N`, como em qualquer gravação.
- As do pgAdmin vão para a pasta **pgAdmin** da árvore; as do SSMS, para
  **SQL Server Management Studio**.
- **Atualização (2026-10-01): as senhas do pgAdmin vêm.** O usuário perguntou: "as
  conexões migradas do pgAdmin não estão conectando, não é possível migrar as senhas?".
  É. A coluna `server.password` guarda `hex(base64(IV ‖ AES-CFB8(senha)))`
  (`pgadmin/utils/crypto.py` e `PgAdminDbBinaryString`, conferidos no pgAdmin 9.18
  instalado e no fonte em `D:\Tootega\Source\pgAdminEx`), e a chave é a que o pgAdmin
  guarda no cofre do sistema pelo `keyring` — no Windows, a entrada `pgAdmin4` /
  `pgadmin4-master-password` do Gerenciador de Credenciais, que o sistema entrega a
  qualquer programa da conta do usuário. `base/os_secret.cpp` a lê (só leitura),
  `base/aes.cpp` ganhou o AES-CFB8 de 128/192/256 bits, e
  `db::pgadmin_decrypt` faz o resto. Como o CFB8 não acusa chave errada, só é aceito o
  resultado que for texto UTF-8 sem caractere de controle — senão a conexão entra sem
  senha. Fora: quem configurou a senha-mestra **digitada** (a chave não está no cofre) e
  Linux/macOS (Secret Service e Keychain não são lidos).
- **A janela *File → Import connections*** (era "Import from DBeaver") lista as três
  ferramentas e nunca duplica: uma conexão que já está aqui **sem senha** recebe só a
  senha, e uma que está na **raiz** vai para o grupo da ferramenta. É o caminho para
  quem importou antes destas mudanças.
- **Atualização (2026-10-01): as do DBeaver também têm grupo.** Pedido do usuário: "Ao
  importar as conexões, mesmo as do DBeaver, crie o grupo como das outras ferramentas."
  Entram em **DBeaver**; as que já tinham pasta lá, em `DBeaver/<pasta>`
  (`db::tool_folder`). Na raiz elas se misturavam com as criadas no próprio C-Otter.
- **Atualização (2026-10-01): as senhas do SSMS 20+ vêm.** A alternativa rejeitada
  abaixo ("decifrar as senhas do SSMS") tinha dois motivos; o primeiro — não haver driver
  — caiu com o ADR 0024, e o segundo vale igual para as do DBeaver e do pgAdmin, que o
  usuário pediu. O SSMS 20 e 21 nem usam mais DPAPI no XML: deixam `<Password />` vazio e
  guardam a senha no Gerenciador de Credenciais, sob
  `Microsoft:SSMS:<versão>:<instância>:<usuário>:<tipo>:<método>`
  (`db::ssms_credential_target`), lida por `read_generic_credential`. As do SSMS 18/19
  (blob DPAPI dentro do XML) continuam de fora.
  **No arquivo de conexões que já existia, estas senhas NÃO foram aplicadas
  automaticamente** — são contas `sa` de servidores remotos, e copiá-las do cofre do
  Windows para um arquivo de cifra fraca é decisão de quem usa: *File → Import
  connections* as traz marcadas, a um clique.
- **Atualização (ADR 0024, 2026-10-01):** o C-Otter passou a falar com o SQL Server, e
  as conexões do SSMS entram como perfis do driver `sqlserver` — com *Windows
  Authentication* quando era o caso no SSMS. O item abaixo fica como registro.
- As do SSMS entram marcadas como **não suportadas**: o C-Otter ainda não fala com o
  SQL Server. Aparecem desabilitadas, com o motivo na dica — melhor que sumirem, e
  ficam prontas para quando o driver existir.

Código: `src/db/connection_import.{hpp,cpp}`; o gancho é
`MainShell::import_external_on_first_run`. Para ver o que seria importado numa
máquina, sem gravar nada: `spike_external_import.exe` (não imprime senhas).

## Alternativas rejeitadas

- **Embutir a biblioteca do SQLite** para ler o `pgadmin4.db`. É domínio público e
  caberia na licença, mas são 250 mil linhas para ler uma tabela. O formato do
  arquivo é público e estável; o leitor escrito à mão (`read_sqlite_table`, ~200
  linhas) percorre a árvore B da tabela, com páginas de transbordo, e é testado
  contra um banco de verdade gerado pelo SQLite (`tools/gen_sqlite_fixture.py`).
  Limites assumidos: só UTF-8, e não lê o que ainda está no arquivo `-wal`.
- **Decifrar as senhas do SSMS com `CryptUnprotectData`.** Funcionaria para o usuário
  logado, mas não há driver para usá-las, e uma senha copiada para um arquivo que
  viaja com a pasta (ADR 0020) deixa de estar protegida pela conta do Windows.
- **Importar a cada abertura** (ou sempre que não houver conexão). Desfaria a
  exclusão feita pelo usuário.
- **Perguntar antes de importar.** O pedido foi copiar; a pergunta seria um diálogo
  a mais na primeira tela de quem acabou de abrir o programa. O aviso passageiro diz
  quantas vieram, e de onde.

## Consequências

- Quem abre o C-Otter pela primeira vez já vê as conexões que usa no dia a dia.
- As senhas do DBeaver passam a existir também em `.C-Otter/credentials-config.json`
  — o custo já registrado no ADR 0020.
- Verificado numa cópia do executável em pasta temporária (o produto é portátil, e a
  pasta de dados nasce ao lado do executável): 30 conexões gravadas nesta máquina,
  nenhuma senha em texto claro, e a segunda abertura não reimporta.
