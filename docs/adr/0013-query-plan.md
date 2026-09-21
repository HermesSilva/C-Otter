# ADR 0013 — Plano de execução

**Data:** 2026-09-21
**Status:** Aceito

## Contexto

`Ctrl+Shift+E` estava anunciado no menu e não fazia nada. Ver o plano é o que separa
"a consulta está lenta" de "a consulta está lenta **porque** faz sequential scan em 2
milhões de linhas".

O PostgreSQL oferece dois modos, e a diferença entre eles é perigosa:

| Comando | O que faz | Custo |
|---|---|---|
| `EXPLAIN` | Estima. **Não executa** a consulta | Milissegundos |
| `EXPLAIN ANALYZE` | **EXECUTA** e mede o tempo real | O da consulta inteira |

## O problema do ANALYZE

`EXPLAIN ANALYZE UPDATE cliente SET credito = 0` **altera as linhas**. O mesmo vale para
`INSERT`, `DELETE` e qualquer função com efeito colateral. Um usuário que clica em
"explicar" esperando uma estimativa e recebe uma tabela alterada tem todo o direito de
considerar isso um defeito grave.

Como o DBeaver trata (`PostgreExecutionPlan.java`): envolve em transação e faz **rollback
sempre**, com o comentário

> `Rollback changes because EXPLAIN actually executes query and it could be INSERT/UPDATE`

## Decisão

**`EXPLAIN` simples é o padrão.** `ANALYZE` exige uma caixa marcada de propósito, com o
aviso do que ele faz ao lado — não num tooltip escondido.

**Quando `ANALYZE` é usado, a execução é envolvida em transação com rollback garantido**,
mesmo em auto-commit:

```sql
BEGIN;
EXPLAIN (ANALYZE, BUFFERS, FORMAT JSON) <consulta>;
ROLLBACK;
```

O rollback acontece **mesmo se o `EXPLAIN` falhar** — é o único jeito de garantir que a
análise não deixa rastro. Se o rollback em si falhar, isso vai para a barra de status
como erro, nunca em silêncio.

### `FORMAT JSON`, não texto

O texto do `EXPLAIN` é feito para leitura humana e mudou de forma entre versões do
PostgreSQL. O JSON é estável desde a 9.0 e já vem em árvore, com `Plan` e `Plans`
aninhados — exatamente a estrutura que a interface precisa desenhar. Parsear o texto
seria reconstruir o que o servidor já entrega pronto.

O parser de JSON necessário já existe (`base/json.cpp`, escrito para o ADR 0012).

### O que a árvore destaca

Um plano de 40 nós não ajuda se tudo parece igual. Três marcações, por ordem de utilidade:

| Marca | Quando | Por quê |
|---|---|---|
| **Custo relativo** em barra | Sempre | O nó mais caro é o que vale otimizar |
| `Seq Scan` em vermelho | Sempre | Varredura completa é a causa mais comum de lentidão |
| **Estimativa errada** | Só com `ANALYZE` | `rows=1` estimado contra `rows=50000` real explica planos ruins |

## Consequências

- Ver o plano vira operação segura por padrão
- `ANALYZE` continua disponível, com o custo declarado na tela
- Um `EXPLAIN ANALYZE` numa consulta lenta **demora o tempo da consulta** — isso é
  inerente e está dito na interface
- O plano não é paginado nem exportado; é uma árvore, não uma tabela

## Alternativas rejeitadas

**`EXPLAIN ANALYZE` como padrão.** Dá números reais, que são mais úteis. Mas transformar
um comando de diagnóstico em execução silenciosa é o tipo de surpresa que destrói
confiança na ferramenta.

**Bloquear `ANALYZE` em `INSERT`/`UPDATE`/`DELETE`.** Tentador, mas o rollback já resolve,
e há casos legítimos — medir uma carga antes de rodá-la de verdade.

**Texto cru do `EXPLAIN` numa aba.** Trivial de implementar e é o que o `psql` faz. Fica
como fallback quando o JSON não puder ser lido: melhor mostrar texto que nada.
