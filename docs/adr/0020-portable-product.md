# ADR 0020 — Produto portátil: tudo numa pasta `.C-Otter` ao lado do executável

**Data:** 2026-09-30
**Status:** Aceito — altera o **local** definido no ADR 0012 (o formato dos arquivos não muda)

## Contexto

Os dados do programa estavam em `%APPDATA%\C-Otter` (`~/.config/C-Otter` fora do Windows):
conexões, senhas, disposição das janelas e, desde o ADR 0019, as preferências.

Pedidos do usuário, em sequência:

> "O config deve ficar numa pasta `.C-Otter`, na mesma pasta do executável; se não existir,
> deve criar e salvar o config com as opções default; o tema default é âmbar."

> "Este arquivo `imgui.ini` deve ficar também na pasta citada, e deve ter o nome
> `layout.ini`."

> "Este produto será sempre um modelo portátil, pode anotar em diretivas."

## Decisão

**O C-Otter não escreve nada fora da pasta do executável.** Tudo o que ele grava fica em
`<pasta do executável>/.C-Otter/`:

| Arquivo | Conteúdo |
|---|---|
| `settings.json` | Perfil de atalhos, conjunto de ícones, tema, idioma, disposição do editor |
| `layout.ini` | Posição e ancoragem das janelas (o `imgui.ini` do ImGui, com outro nome) |
| `data-sources.json` | Conexões, no formato do DBeaver (ADR 0012) |
| `credentials-config.json` | Senhas, cifradas como no DBeaver (ADR 0012) |

- A regra mora num lugar só: `otter::data_directory()` em `src/base/paths.hpp`. Quem
  precisa de um caminho de dados pede ali — nada de `getenv("APPDATA")` espalhado.
- **Primeira execução:** a pasta é criada e o `settings.json` é gravado com **todas** as
  opções no padrão (tema **âmbar**, atalhos e ícones do DBeaver, idioma do ambiente). O
  arquivo serve também de lista do que pode ser configurado.
- **Quem já usava:** se a pasta nova não tem conexões e a antiga tem, as conexões e as
  senhas são **copiadas** uma vez (`import_legacy_store`). O antigo fica intacto.
  **Revogado em 2026-10-01:** a cópia automática foi removida a pedido do usuário —
  trazia de volta, a cada pasta nova, conexões de teste ("MySQL de teste", "_1", "_2").
  A função continua no código, sem ser chamada ao abrir; ver ADR 0023.
- **Diálogo de conexão no início:** só abre sozinho quando não há conexão salva. Com
  conexões na árvore, o programa abre direto no navegador (pedido do mesmo dia).

## Alternativas rejeitadas

| Alternativa | Por que não |
|---|---|
| Manter em `%APPDATA%` | É o lugar convencional de um programa *instalado*; o usuário definiu que este é portátil. |
| Portátil só quando existir um arquivo marcador (como VS Code e outros fazem) | Dois modos, dois caminhos de código, e a pergunta "onde estão minhas conexões?" passa a depender de um arquivo que ninguém lembra. O pedido foi "sempre". |
| Só as preferências na pasta do executável, conexões em `%APPDATA%` | Foi a primeira leitura do pedido; deixaria o produto metade portátil — copiar a pasta levaria o tema e deixaria as conexões para trás. |
| Mover (em vez de copiar) os dados antigos | Irreversível, e quebraria uma versão anterior ainda em uso na mesma máquina. |
| Migrar também `settings.json` e `layout.ini` antigos | O pedido foi explícito: sem config, criar com os padrões. A disposição antiga ainda guardava ids de janelas que já não existem. |

## O que mais passou a morar em `settings.json` (2026-10-01)

Dois pedidos do usuário, os dois estado de tela que o ImGui não guarda entre execuções:

- **`closed-folders`** — "Faça com que as pastas da árvore seja salvo o último estado
  aberto/fechado." Os grupos de conexão que ficaram **fechados**. Guardam-se os fechados,
  não os abertos, para um grupo novo nascer aberto. Só os grupos: as pastas de dentro de
  uma conexão (Tables, Views…) dependem de conectar, e reabri-las sozinhas dispararia
  leituras no servidor a cada início.
- **`window`** — "O tamanho e posição da janela principal deve ser gravado, e
  restaurando." O retângulo da janela **normal** (o de antes de maximizar) e se estava
  maximizada. É gravado quando a janela para de mudar, não só ao sair — encerrar pelo
  gerenciador de tarefas não perde a posição. Ao abrir, `fit_window_placement`
  (`ui/window_placement.cpp`, com teste) confere o gravado contra os monitores que
  existem agora: janela num monitor desligado volta centrada no principal, e tamanho
  maior que a tela é cortado.

## Consequências

- **Copiar a pasta do programa leva tudo junto — inclusive as senhas.** A cifra de
  `credentials-config.json` usa a chave fixa do DBeaver (ADR 0012): protege de um olhar
  casual, e de mais nada. Um pendrive com o C-Otter é um pendrive com as senhas. O aviso
  "weak encryption" do diálogo de conexão continua valendo, agora com mais peso.
- **A pasta do executável precisa permitir escrita.** Em `C:\Program Files` não permite;
  o programa abre com os padrões e avisa na tela ao tentar gravar. Um instalador, se um dia
  existir, tem de instalar numa pasta do usuário.
- **Dois C-Otter em pastas diferentes têm conexões diferentes.** É o que "portátil"
  significa, mas surpreende quem espera uma configuração por usuário.
- **No desenvolvimento**, a pasta é `build\win-release\bin\.C-Otter` — dentro de `build/`,
  que um *clean* apaga. A cópia em `%APPDATA%\C-Otter` é o que resta das conexões de teste;
  não apagá-la. Todos os executáveis de teste e os *spikes* ficam no mesmo `bin` e leem a
  mesma pasta.
- **Os testes não escrevem mais no diretório de trabalho:** o contexto ImGui dos testes de
  ícone deixava um `imgui.ini` na raiz do repositório.
