# ADR 0022 — O editor de objeto é um documento, e o nó central do dock é o do editor

**Data:** 2026-10-01
**Status:** Aceito

## Contexto

O DBeaver abre um **editor de objeto** ao dar duplo clique (ou `F4`) num nó da
árvore: uma aba da área de edição com Properties, as seções (Columns,
Constraints, ..., DDL) e Data. O C-Otter mostrava o que havia sobre o objeto
dentro da própria árvore, e o duplo clique numa tabela abria um script com
`SELECT *`.

Mapa completo em `docs/OBJECT-EDITOR.md`.

## Decisão

### 1. O editor de objeto é um `SqlDocument` com um `ObjectView`

A aba, o resultado, a grade, a paginação e a edição de células já existem no
documento de script. O editor de objeto os reaproveita:

- o **editor de texto** do documento guarda o DDL (ou o fonte da função);
- o **resultado** do documento guarda os dados da tabela, e a aba Data desenha a
  mesma grade (`draw_grid_view`), com filtro, edição e exportação;
- o que é só do objeto — qual é, a seção aberta, as propriedades alteradas —
  fica em `ui/object_view.hpp`.

Para os comandos do editor SQL (executar, formatar, comentar) uma aba de objeto
conta como "sem documento": o texto dela é o DDL lido do servidor.

### 2. Tudo o que altera passa pela janela "Review SQL"

Renomear, comentar, trocar dono, GRANT, DROP, VACUUM, criar objeto: a regra gera
um `AlterScript` (`db/object_info.cpp`, `db/object_ddl.cpp`) e a tela o mostra
antes de executar — o mesmo ponto único do ADR 0016. Marcar uma caixa de
privilégio **não** muda o estado na tela: quem muda é o comando, depois de
confirmado, e a releitura redesenha.

### 3. A regra não conhece a tela, e é conferida no servidor

As consultas ao catálogo e os geradores são funções puras. O teste unitário
confere o texto (citação de nomes, a senha fora do DDL de role); o teste ao vivo
(`otter_tests_object_live`) confere que o servidor aceita cada consulta e cada
comando, e que **o DDL mostrado recria o objeto** — apaga o original e roda o
DDL, tudo em `BEGIN ... ROLLBACK`.

Foi esse teste que achou o DDL de tabela saindo com `DEFAULT nextval(...)` numa
coluna `serial`: sintaticamente válido, e incapaz de recriar a tabela.

### 4. O nó central do dockspace passa a ser o do editor

Com um editor de objeto na frente, o painel de resultado não tem o que mostrar
(os dados estão na aba Data), e a metade de baixo da janela é devolvida ao
editor.

Isso exigiu inverter o corte do layout: o nó **novo** do `DockBuilderSplitNode`
é o do resultado, e o que sobra — que herda o papel de nó central — é o do
editor. Era o inverso, e o nó central do ImGui nunca se recolhe: com o resultado
escondido, a metade de baixo ficava vazia. O mesmo defeito afetava "Toggle
results panel".

## Divergências do DBeaver, com a razão

| Divergência | Razão |
|---|---|
| Statistics, Permissions e DDL são seções da lista, como no DBeaver; `ER Diagram` não existe | Diagramas ER estão fora do escopo pedido |
| Materialized view, trigger e regra têm o fonte somente leitura | Não há `CREATE OR REPLACE` para eles; "gravar" seria apagar e recriar, perdendo dados e dependentes sem avisar |
| O esqueleto de função em `sql` tem corpo `SELECT`, não `BEGIN END` | O do DBeaver só compila em plpgsql |
| Session Manager e Lock Manager são consultas numa aba, com os botões acima da grade | ADR 0018: reaproveitam a grade em vez de um editor próprio |
| A carga de CSV é sempre uma transação | Falhar na linha 90.000 não pode deixar 89.500 gravadas |

## Alternativas rejeitadas

| Alternativa | Por que não |
|---|---|
| Janela própria por objeto (não um documento) | Duplicaria a grade, as abas e a paginação; e o DBeaver o põe na área de edição |
| Editar propriedades direto na árvore | É onde estavam, somente leitura; quem vem do DBeaver procura o editor |
| Gerar o ALTER e executar sem revisão | DDL não tem desfazer (ADR 0016) |
| Buscar o DDL de tabela de uma função do servidor | O PostgreSQL não tem `pg_get_tabledef` |
