# ADR 0014 — Grade editável

**Data:** 2026-09-21
**Status:** Aceito

## Contexto

A grade é somente leitura. Editar dados é o que separa um visualizador de um cliente de
banco, e é também a operação mais perigosa que a ferramenta vai oferecer: um `UPDATE` com
`WHERE` errado altera linhas que ninguém pediu para alterar.

## A pergunta que define tudo: como identificar a linha?

Para gerar `UPDATE cliente SET nome = 'x' WHERE ???`, é preciso saber o que vai no `WHERE`.
As opções, e por que só uma funciona:

| Candidato | Problema |
|---|---|
| Número da linha na grade | Não existe no banco. A ordem é do resultado, não da tabela |
| Todas as colunas no `WHERE` | Falha com `NULL` (`= NULL` nunca casa) e atualiza duplicatas juntas |
| `ctid` do PostgreSQL | Muda a cada `UPDATE` e some no `VACUUM FULL`. Específico do PostgreSQL |
| **Chave primária ou única** | **Funciona.** É o que o DBeaver usa (`DBDRowIdentifier`) |

**Decisão: sem PK ou constraint única conhecida, a grade não edita.** Ela diz isso na barra,
com o motivo — não oferece um campo que falha na hora de salvar.

## Quando a edição é possível

Três condições, todas necessárias:

1. O resultado vem de **uma única tabela** — um `JOIN` não tem para onde escrever
2. Essa tabela tem **PK ou constraint única** carregada no catálogo
3. Todas as colunas da chave estão **no resultado** — `SELECT nome FROM cliente` não dá
   para atualizar, porque falta `cliente_id`

Falhando qualquer uma, a grade continua legível e diz por quê.

### Como saber de que tabela vem

O `RowDescription` do protocolo v3 traz, por coluna, o **OID da tabela** e o **número do
atributo**. É informação que o servidor já manda e que o C-Otter hoje descarta. Ela responde
as três condições sem adivinhação — muito melhor que analisar o `FROM` da consulta, que
quebra com alias, subconsulta e CTE.

## Fluxo de edição

**Edição em buffer, gravação explícita.** Cada célula alterada fica marcada como suja e
**não vai para o banco** até o usuário confirmar. O alternativo — gravar a cada tecla — faz
um `UPDATE` por caractere e torna impossível desistir.

```
1. duplo clique na célula      → editor embutido
2. Enter/Tab                    → valor no buffer, célula marcada
3. Ctrl+S ou botão "Salvar"     → gera os UPDATE e executa
4. Esc ou "Descartar"           → buffer limpo, grade volta ao original
```

**Um `UPDATE` por linha alterada**, com todas as colunas sujas daquela linha:

```sql
UPDATE otter_test.cliente
   SET nome = 'novo', credito = 500
 WHERE cliente_id = 7;
```

### Em transação, sempre

As alterações vão dentro de `BEGIN`/`COMMIT`, mesmo em auto-commit. Salvar cinco linhas e
falhar na terceira deixaria duas gravadas e três não — estado que o usuário não pediu e não
consegue reproduzir. Ou tudo, ou nada.

### O que a interface precisa mostrar

| Sinal | Por quê |
|---|---|
| Célula suja com fundo distinto | Saber o que mudou antes de gravar |
| Valor original no tooltip | Poder comparar sem desfazer |
| Contagem de alterações pendentes | `3 alterações` na barra da grade |
| Aviso ao trocar de aba com pendências | Perder edição em silêncio é inaceitável |

## Consequências

- Editar exige PK; consultas com `JOIN` continuam somente leitura
- O `RowDescription` passa a ser guardado (`table_oid`, `column_attnum`)
- `NULL` precisa ser distinguível de string vazia na edição — um botão "definir NULL",
  já que digitar nada significa string vazia
- Inserir e excluir também funcionam (acrescentado no mesmo dia). Os comandos saem na
  ordem `INSERT` → `UPDATE` → `DELETE`: uma linha nova pode referenciar algo que a
  exclusão removeria, e a ordem inversa violaria a chave estrangeira
- Coluna em branco numa linha nova fica **fora** do `INSERT`, para a tabela aplicar seu
  `DEFAULT` — é o que se espera ao não preencher um `serial`

## Alternativas rejeitadas

**Gravar a cada célula.** Simples e instantâneo. Faz um `UPDATE` por tecla e impede desistir.

**Editar por `ctid`.** Funciona no PostgreSQL e permite editar tabela sem PK. Rejeitado
porque `ctid` muda a cada `UPDATE`: editar duas células da mesma linha usaria um `ctid` que
a primeira gravação já invalidou.

**Gerar o `UPDATE` e só mostrar ao usuário.** Seguro e inútil — quem queria editar na grade
não quer copiar SQL para outra aba.
