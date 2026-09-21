# ADR 0011 — Paginação de resultados

**Data:** 2026-09-21
**Status:** Aceito

## Contexto

`SELECT * FROM evento_auditoria` numa tabela de milhões de linhas travava a interface até o
servidor terminar de enviar tudo. O `ResultSet` colunar aguenta o volume em memória, mas o
caminho até lá é bloqueante: o worker lê todas as `DataRow` antes de devolver o controle.

Pior que a espera é a falta de sinal — a janela fica parada, sem barra de progresso e sem
como cancelar. Um cliente de banco que congela ao executar a consulta mais comum do mundo
não é utilizável.

Como o DBeaver resolve (`ModelPreferences.RESULT_SET_MAX_ROWS`): busca **200 linhas** por
vez e relê ao rolar. Tem duas estratégias, escolhidas por preferência:

- `setMaxRows` no statement JDBC — o servidor ainda calcula tudo, o driver corta
- `RESULT_SET_MAX_ROWS_USE_SQL` — reescreve a consulta com `LIMIT`/`OFFSET`

## Decisão

**Reescrever a consulta com `LIMIT` e `OFFSET`**, 200 linhas por página.

A alternativa correta seria o protocolo estendido: `Bind` num portal e `Execute` com
`max_rows`, buscando o resto com `Execute` sucessivos. É como o `libpq` implementa cursores
e **não desperdiça trabalho do servidor** — ele para de produzir linhas quando o portal
enche.

Ficou para depois porque exige `Parse`/`Bind`/`Describe`/`Execute`/`Sync` e gestão de
portal com estado entre chamadas, sobre uma implementação de protocolo que hoje só faz
query simples. É trabalho que vale a pena, mas não para destravar a UI agora.

### O que a reescrita custa

**O servidor calcula o resultado inteiro a cada página.** Um `OFFSET 100000` faz o
PostgreSQL produzir e descartar 100 mil linhas. Para `ORDER BY` sem índice, cada página
reordena tudo.

Isso é aceitável para navegar as primeiras páginas — que é o uso real — e ruim para varrer
uma tabela inteira. Quem precisa varrer deve exportar, não rolar a grade.

### Quando NÃO reescrever

A reescrita só se aplica quando é seguramente correta:

| Caso | Ação | Por quê |
|------|------|---------|
| `SELECT` simples | Reescreve | |
| Já tem `LIMIT` | **Não reescreve** | O usuário pediu um limite; sobrepor seria mentir sobre o que foi executado |
| `INSERT`/`UPDATE`/`DELETE` | Não reescreve | Não produz linhas para paginar |
| `WITH ... SELECT` | Reescreve | `LIMIT` no fim vale para a consulta externa |
| Múltiplos comandos | Não reescreve | Sem separador de script ligado ainda |
| `EXPLAIN`, `SHOW`, `VALUES` | Não reescreve | Resultado pequeno por natureza |

Na dúvida, **não reescreve**: executar algo diferente do que o usuário escreveu é pior que
uma espera.

### O que aparece na tela

A grade mostra a página e diz que é uma página: `linhas 1–200`, com botões de avançar e
voltar. Um resultado truncado sem aviso seria o defeito descrito na diretiva 6 — um campo
que finge funcionar.

Quando a consulta foi reescrita, o inspetor de queries mostra o SQL **efetivamente
executado**, com o `LIMIT` acrescentado. O usuário precisa poder auditar o que rodou.

## Ordenação (acrescentado em 2026-09-21)

Clicar no cabeçalho da grade reescreve a consulta com `ORDER BY`, no mesmo
mecanismo. **A ordenação tem que ir para o servidor**: com paginação, ordenar as
200 linhas visíveis produziria as 200 primeiras linhas na ordem do banco,
reordenadas entre si — que não são as 200 menores do resultado.

O `ORDER BY` vai **antes** do `LIMIT`, como exige a gramática. Uma consulta que já
tem `ORDER BY` de nível externo não é reordenada: dois `ORDER BY` são erro de
sintaxe, e sobrepor o do usuário executaria algo diferente do que está na tela.

O custo aparece no inspetor: contra 2 milhões de linhas sem índice, ordenar levou
76 ms contra 0,88 ms sem ordenação.

## Filtro por coluna (acrescentado em 2026-09-21)

Mesmo mecanismo, com uma diferença: o filtro **envolve** a consulta numa subconsulta
em vez de anexar `WHERE`. Anexar seria errado de três jeitos:

- A consulta pode já ter `WHERE` — dois na mesma não compilam
- Pode terminar em `GROUP BY` ou `HAVING`, e `WHERE` depois deles é sintaxe inválida
- Mesmo quando compilasse, filtraria **antes** da agregação, produzindo um resultado
  diferente do que a grade mostra

```sql
SELECT * FROM (
  <consulta do usuário>
) AS otter_filter
 WHERE "coluna" > 100
 ORDER BY "outra" DESC
 LIMIT 201
```

O envolvimento tem um efeito colateral útil: o `ORDER BY` do usuário passa para dentro
da subconsulta, e a ordenação da grade deixa de conflitar com ele.

A expressão é escrita pelo usuário e vai para o SQL **como digitada** — é uma cláusula
`WHERE`, não um valor. Quem digita `1=1 OR TRUE` está consultando o próprio banco com as
próprias credenciais; não há elevação de privilégio a impedir. A interface chama o campo
de *expressão* justamente para dizer isso.

## Consequências

- A UI não trava mais com `SELECT` sem `LIMIT`
- `COUNT(*)` do total não é feito: custaria uma segunda varredura. A grade diz
  `200+` enquanto houver próxima página, não um total falso
- Paginar com `OFFSET` alto é lento, e isso é inerente à decisão
- O protocolo estendido continua desejável; esta decisão não o impede

## Alternativas rejeitadas

**Buscar tudo em background com carregamento incremental.** Resolveria o travamento sem
mudar o SQL, mas mantém o servidor produzindo milhões de linhas que ninguém vai ver, e o
consumo de memória continua proporcional à tabela.

**Cursor `DECLARE`/`FETCH`.** Funciona e é eficiente, mas exige transação aberta: um cursor
sem `WITH HOLD` morre no `COMMIT`. Amarrar a navegação da grade ao estado transacional
criaria uma interação difícil de explicar — rolar a grade falharia depois de um commit.
