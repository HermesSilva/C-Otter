# ADR 0010 — Compatibilidade com versões antigas de SGBD

**Data:** 2026-09-21
**Status:** Aceito

## Contexto

Requisito: **boa compatibilidade retroativa com todas as versões dos bancos**.

É um requisito de mercado real. Servidores de produção raramente estão na última versão:
PostgreSQL 9.6 e MySQL 5.7 ainda rodam em sistemas críticos, e um cliente que só fala com a
versão mais nova é inútil justamente onde mais se precisa dele.

O protocolo próprio (ADR 0009) **ajuda** aqui: com `libpq`, a compatibilidade depende da
versão da biblioteca instalada. Falando o protocolo diretamente, nós controlamos o que
negociar.

## Decisão 1 — Alvos de compatibilidade

| SGBD | Mínimo suportado | Lançamento | Por quê |
|------|-----------------|------------|---------|
| **PostgreSQL** | **8.4** | 2009 | Protocolo v3 estável desde 7.4 (2003) |
| **MySQL** | **5.5** | 2010 | Protocolo 4.1 estável; 5.5 ainda é comum em legado |
| **MariaDB** | **5.5** | 2012 | Fork do MySQL 5.5 |
| **SQL Server** | **2008** | 2008 | TDS 7.3; 2005 exigiria TDS 7.2 |
| **SQLite** | **3.x** | 2004 | Formato de arquivo estável desde 2004 |
| **Oracle** | **11g** | 2007 | Limite prático do cliente |

## Decisão 2 — Degradação explícita, nunca silenciosa

O cliente **detecta** a versão no handshake e ajusta o comportamento. Três regras:

1. **Nunca falhar por falta de recurso opcional.** Se `pg_stat_statements` não existe, o
   painel de estatísticas some — a conexão não cai.
2. **Nunca fingir suporte.** Se o servidor não faz algo, a UI mostra isso, não simula.
3. **Consultas de catálogo por faixa de versão**, não uma única query "moderna" que quebra
   em servidor antigo.

### Autenticação — já implementado

O `lib/pgwire` suporta os três métodos, em ordem histórica:

| Método | Desde | Estado |
|--------|-------|--------|
| `cleartext` | sempre | Implementado (só com TLS, em produção) |
| `md5` | 7.2 (2002) | Implementado — legado, fraco, mas ainda em uso |
| `SCRAM-SHA-256` | 10 (2017) | Implementado, com verificação mútua |

Um servidor 9.6 pede `md5`; um 18 pede `SCRAM`. O cliente responde ao que vier, sem
configuração do usuário.

### Consultas de catálogo por versão

Exemplo concreto — coluna renomeada no PostgreSQL 10:

```sql
-- PostgreSQL >= 10
SELECT relname, relispartition FROM pg_class ...

-- PostgreSQL < 10: relispartition nao existe
SELECT relname, false AS relispartition FROM pg_class ...
```

Isso se repete em dezenas de pontos: `pg_stat_activity.pid` era `procpid` antes do 9.2;
`pg_roles.rolbypassrls` só existe desde 9.5. **Essas variações são exatamente o ativo que o
DBeaver acumulou em `ext.postgresql`** (41.619 linhas) — e a razão pela qual portar aquelas
consultas vale tanto (`docs/ANALYSIS.md` §4).

Implementação: cada consulta declara a faixa de versão em que vale.

```cpp
struct CatalogQuery {
    Version min_version;   // inclusive
    Version max_version;   // exclusive; {} = sem limite
    std::string_view sql;
};
```

### Capacidades detectadas, não presumidas

`Capabilities` (já em `db/holt.hpp`) passa a ser preenchida a partir da versão real:

| Capacidade | PostgreSQL |
|------------|-----------|
| `ddl_in_transaction` | sempre |
| Partições declarativas | ≥ 10 |
| `GENERATED ... AS IDENTITY` | ≥ 10 |
| `MERGE` | ≥ 15 |
| Colunas geradas | ≥ 12 |
| `pg_stat_statements` | se a extensão existir |

## Decisão 3 — Teste em matriz, não só na última versão

Compatibilidade sem teste automatizado é promessa vazia. A CI roda a suíte contra:

| SGBD | Versões testadas |
|------|-----------------|
| PostgreSQL | 9.6, 12, 15, 18 |
| MySQL | 5.7, 8.0, 8.4 |
| MariaDB | 10.6, 11 |
| SQL Server | 2017, 2019, 2022 |

Via contêiner, na Fase 3. **Uma versão que não está na matriz não é suportada** — é uma
esperança.

## Custo

| Item | h/h |
|------|-----|
| Infraestrutura de versionamento de consultas | 80 |
| Consultas de catálogo por faixa (PostgreSQL) | 120 |
| Idem para os demais SGBDs | 240 |
| Matriz de teste em contêiner na CI | 160 |
| **Total** | **600 h/h** |
| **Total do projeto** | ~16.200 → **≈ 16.800 h/h** |

## Consequências

- Nenhuma consulta de catálogo pode ser escrita sem declarar a faixa de versão em que vale
- A UI precisa de um caminho consistente para "recurso indisponível nesta versão"
- Versões muito antigas (PostgreSQL < 9) receberão suporte básico: conectar, navegar,
  executar — sem recursos modernos

## Referências

- ADR 0009 — protocolo nativo, que torna esta política possível
- `docs/ANALYSIS.md` §4 — as consultas de catálogo do DBeaver como ativo portável
