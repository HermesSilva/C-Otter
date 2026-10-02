# ADR 0019 — Perfis de atalho, conjuntos de ícones e o cartão de dica

**Data:** 2026-09-30
**Status:** Aceito

## Contexto

Três pedidos do usuário, no mesmo dia, sobre a mesma tensão — *ser igual ao DBeaver* e
*ser melhor que ele*:

> "Para teclas de atalhos, crie perfis, DBeaver e C-Otter, desta forma o usuário pode
> escolher qual perfil vai usar. Este projeto pode e deve evoluir o DBeaver, não regredir."

> "Não é possível copiar e usar os ícones originais do DBeaver?"

> "Altere os Hints para um modelo como este ou superior (...) estamos numa era moderna que
> tudo deve ser elegante, suave, simples e funcional."

A diretiva 12 pede paridade: quem vem do DBeaver precisa achar o que procura onde está
acostumado. Mas paridade cega também herda o que o DBeaver herdou do Eclipse sem querer —
`Ctrl+Shift+X` para maiúsculas, `Ctrl+D` para apagar a linha, `Ctrl+]` para script novo.

## Decisão

### 1. Uma tabela de comandos, dois perfis de atalho

`src/ui/commands.cpp` tem **uma linha por comando**: o id do DBeaver, o rótulo, o contexto,
as teclas do perfil **DBeaver**, as teclas do perfil **C-Otter**, o ícone, o estado e a nota
do que difere. Menu, barra lateral, menu de contexto, atalhos e a janela *Show shortcuts*
leem essa mesma tabela; `docs/EDITOR-COMMANDS.md` é gerado dela.

- **DBeaver** é o padrão. O público vem de lá, e a mão já sabe aquelas teclas.
- **C-Otter** só difere onde há razão: a tecla que os editores atuais usam (`Ctrl+T` script
  novo, `Ctrl+Shift+K` apagar linha, `F12` ir para a declaração), ou uma tecla do DBeaver
  que colide com uma do editor de texto.
- A escolha fica em `settings.json` (`%APPDATA%\C-Otter`), junto de tema, idioma e
  conjunto de ícones.

Um teste impede dois comandos com a mesma tecla no mesmo perfil e contexto, e exige que
toda tecla que o `plugin.xml` do DBeaver define esteja no perfil DBeaver.

### 2. Dois conjuntos de ícones

- **DBeaver** (padrão): os SVG originais, copiados por `tools/embed_icons.py` para
  `assets/icons/dbeaver/` e embutidos no executável. São rasterizados em tempo de execução
  pelo **nanosvg** no tamanho exato em que aparecem — nítidos em qualquer DPI, sem atlas.
- **C-Otter**: os vetoriais de `src/ui/icons.cpp`, na cor do tema. Continuam sendo o que
  aparece para um ícone sem equivalente no DBeaver e quando não há textura (testes).

A pasta de cada tipo segue o `icon="#..."` do `<tree>` do DBeaver (`folder_icon()`); no
conjunto C-Otter a pasta leva o ícone do conteúdo, como sempre levou.

### 3. O cartão de dica

Toda dica passa por `src/ui/hint.{hpp,cpp}`: cartão de cantos arredondados, borda na cor
de destaque, faixa de título com o atalho desenhado como tecla, linhas "rótulo … valor",
texto corrido e trecho de SQL em fonte de código. Aparece com atraso curto e esmaecer;
depois da primeira, as seguintes vêm sem atraso. `ImGui::SetTooltip` não é mais usado na
interface.

## Alternativas rejeitadas

| Alternativa | Por que não |
|---|---|
| Só as teclas do DBeaver | O usuário pediu explicitamente os dois perfis; e fixaria no produto as heranças do Eclipse. |
| Só teclas próprias | Quebra a diretiva 12: quem vem do DBeaver perderia `Alt+X`, `Ctrl+\`, `F4`. |
| Atalhos editáveis um a um (como as *Keys* do Eclipse) | É o passo seguinte natural, não o primeiro: sem perfis prontos, todo usuário começaria configurando. A tabela já comporta um terceiro perfil. |
| Redesenhar todos os ícones no estilo do C-Otter | Era a diretiva 5 original. O usuário perguntou duas vezes pelos originais: reconhecer o ícone de relance vale mais que um desenho próprio. Os vetoriais ficaram como segundo conjunto, não foram apagados. |
| Converter os SVG em PNG em tempo de build | Um PNG por tamanho por DPI, ou reamostragem borrada. O nanosvg rasteriza no tamanho pedido. |
| Fonte de ícones | Uma cor só; os ícones do DBeaver são coloridos. |
| librsvg / resvg para ler SVG | LGPL e Rust, respectivamente; o ADR 0002 exige link estático sem LGPL e a base é C++. O nanosvg é zlib, dois cabeçalhos, e lê o subconjunto de SVG que estes ícones usam. |
| Estilizar o tooltip do ImGui só com cores | Continua sendo uma caixa de texto corrido: sem título, sem colunas, sem tecla destacada. |

## Consequências

- **Dependência nova:** nanosvg (zlib), fixada no commit
  `239e102ec2c691f2902e20ace2ed36ee4a35cfe6`, em `third_party/nanosvg/`.
- **Atribuição:** os ícones são do DBeaver, Apache 2.0; `assets/icons/dbeaver/NOTICE` lista
  cada arquivo e a origem. Os logotipos de PostgreSQL e MySQL são marcas dos seus donos e
  servem só para identificar o SGBD da conexão — ver `docs/LICENSES.md`.
- **O que o nanosvg não lê** (filtros, texto, `<use>`): um ícone assim sairia vazio, em
  silêncio. O teste `icons_dbeaver_originals_all_rasterize` exige pixels visíveis de cada
  original, em quatro tamanhos.
- **O ícone do DBeaver tem cor própria** e não aceita a do tema nem a do estado. O estado da
  conexão passou a ser um ponto no canto do ícone, como o selo que o DBeaver põe.
- **A diretiva 5 do `CLAUDE.md` foi reescrita**: de "desenhar um ícone próprio por tipo"
  para "usar o original do DBeaver; desenhar só o que ele não tem".
- **A dica tem uma convenção de texto**: `"Rótulo (Ctrl+X)"` na primeira linha vira título
  e tecla. Um parêntese que não é atalho ("3 row(s)") fica no texto — há teste para isso.
