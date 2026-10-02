# ADR 0021 — Túnel SSH e backup/restore por programas do sistema

**Data:** 2026-10-01
**Status:** Aceito

## Contexto

Duas funcionalidades do DBeaver que o C-Otter não tinha, pedidas no mesmo passo
("elevar tudo para próximo de 100%"):

- **Túnel SSH** — a aba SSH do diálogo de conexão gravava os campos e dizia na tela
  "não implementado".
- **Backup e Restore** do PostgreSQL — o menu Tools do DBeaver.

As duas têm a mesma pergunta por baixo: implementar o protocolo/formato aqui, ou
delegar a um programa que já existe na máquina?

## Decisão

**Delegar, nos dois casos, a um processo filho** (`src/base/process.cpp`), sem
shell no meio — programa e argumentos vão separados.

| Funcionalidade | Programa | Como |
|---|---|---|
| Túnel SSH | `ssh` (OpenSSH) | `ssh -N -L 127.0.0.1:<porta local>:<host>:<porta> ...`; a conexão do banco vai para a ponta local |
| Backup | `pg_dump` | os argumentos do assistente do DBeaver |
| Restore | `pg_restore`; `psql` para dump em texto | idem |

### Túnel SSH

- `BatchMode=yes`: o `ssh` nunca pergunta nada. Não há terminal; uma pergunta
  deixaria o processo parado até o tempo esgotar.
- `ExitOnForwardFailure=yes`: porta local ocupada derruba o `ssh`, em vez de
  deixá-lo vivo sem túnel.
- `StrictHostKeyChecking=accept-new`: aceita servidor **novo**, recusa o que
  **mudou** de chave. `no` aceitaria quem estivesse no meio do caminho.
- `-L 127.0.0.1:...` explícito: sem o endereço, o banco ficaria exposto na rede
  local por uma porta sem senha.
- `--` antes do destino: um host começando por `-` não vira opção
  (`-oProxyCommand=...` executaria um programa).
- A ponta local é uma porta livre escolhida pelo sistema; a conexão espera ela
  atender, e se o `ssh` morrer antes, o erro traz o que ele disse.

### Backup e Restore

- A senha vai em `PGPASSWORD`, no **ambiente** do processo. Na linha de comando
  ela apareceria na lista de processos de qualquer usuário da máquina. Há teste
  que varre os argumentos atrás dela.
- `--no-password`: sem terminal, perguntar a senha seria travar.
- O programa é procurado nas instalações do PostgreSQL (a mais nova primeiro) e
  depois no `PATH` — o instalador do Windows não põe o `bin` no `PATH`, e um
  `pg_dump` mais velho que o servidor recusa o backup.

## O que isso custa

| Custo | Como aparece |
|---|---|
| **Sem senha SSH digitada.** O `ssh` só lê senha de um terminal | A aba SSH diz isso assim que "Password" é escolhido; a conexão recusa antes de iniciar o processo, com a razão. Chave ou agente funcionam |
| Chave com frase secreta só pelo agente (`ssh-add`) | Idem |
| Depende de o `ssh` / `pg_dump` existirem na máquina | A falta é dita na tela, com o nome do programa. O OpenSSH vem com Windows 10+, macOS e Linux; `pg_dump` exige o cliente do PostgreSQL — como no DBeaver |
| O `ssh` grava em `~/.ssh/known_hosts` | Fora da pasta `.C-Otter` (ADR 0020). É o arquivo do **usuário**, o mesmo que o `ssh` dele já usa — o C-Otter não escreve lá, o `ssh` escreve |
| `pg_dump` não passa por proxy SOCKS | Recusado com a razão, em vez de tentar por fora do proxy |

## Alternativas rejeitadas

| Alternativa | Por que não |
|---|---|
| **libssh2 / libssh** | libssh é LGPL — o ADR 0002 (link estático) a exclui. libssh2 é BSD, mas traz OpenSSL (ou outro backend de criptografia) para o link, e o projeto usa só a criptografia do sistema (ADR 0009, 0017) |
| **Implementar SSH2 aqui** | Troca de chaves, cifras, MAC, canais, agente, formatos de chave: um protocolo de criptografia inteiro para manter. Erro ali é falha de segurança, não defeito de tela |
| **Reimplementar o formato do `pg_dump`** | O formato `custom` é do PostgreSQL e muda entre versões. O DBeaver também delega |
| **Backup "lógico" próprio** (gerar o SQL de cada objeto) | O DDL do editor de objeto recria um objeto; um banco inteiro, na ordem de dependência, com dados, é o que o `pg_dump` faz há 25 anos |
| **`sshpass` / `SSH_ASKPASS` para a senha** | `sshpass` não existe no Windows; `SSH_ASKPASS` exigiria um segundo executável só para devolver a senha, e a deixaria num canal que outro processo do usuário lê |

## Verificação

| O que | Como | Resultado |
|---|---|---|
| Argumentos do `ssh`, recusas | `tests/unit/test_process.cpp` | ✅ |
| Processo: saída, código, ambiente, kill | idem | ✅ |
| Túnel: caminho de falha (nada escuta na porta) | `spike_ssh_live` | ✅ recusa em ~2 s, com a mensagem do `ssh` |
| **Túnel de verdade** | `spike_ssh_live <perfil> <host> <porta> <usuário>` | ⬜ **não conferido**: a máquina de teste não tem servidor SSH |
| Comandos do backup/restore | `tests/unit/test_native_tools.cpp` | ✅ |
| Backup Custom e Plain, restore por `pg_restore` e `psql` | na aplicação, contra o PostgreSQL 18 local, num banco de rascunho | ✅ |

## Adendo (2026-10-01): MySQL

A mesma decisão vale para o MySQL: *Dump database* roda o `mysqldump` e *Restore* /
*Execute script* rodam o `mysql` do sistema (`db/mysql_object.cpp`). O programa é
procurado no PATH e em `Program Files\MySQL\*\bin`; faltando, a tela diz qual.

- A senha vai em `MYSQL_PWD`, no ambiente do processo — nunca na linha de comando.
  A variável é marcada como obsoleta pela Oracle, mas é a única forma sem arquivo:
  a alternativa (`--defaults-extra-file`) gravaria a senha em disco.
- O script é executado por `--execute="source <arquivo>"`, sem redirecionar a
  entrada padrão.
- Pelo proxy SOCKS os clientes não passam: recusado com o motivo, como no pg_dump.
