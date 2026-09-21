# ADR 0009 — Protocolo wire nativo, sem bibliotecas de cliente

**Data:** 2026-09-21
**Status:** Aceito

## Contexto

A integração com `libpq` falhou por um conflito que não se resolve com flags:

```
libpq.a  ->  /DEFAULTLIB:MSVCRT     (runtime DINAMICA)
C-Otter  ->  /MT                    (runtime ESTATICA, ADR 0002)
```

O linker reporta `unresolved external symbol __imp_calloc`, `__imp_getenv`, `__imp_feof` —
símbolos que a `libpq.a` espera importar da DLL da runtime. Misturar runtimes no mesmo
processo leva a corrupção de heap (memória alocada por uma, liberada pela outra).

Descoberta relevante do diagnóstico: no Windows o instalador do PostgreSQL entrega **dois
arquivos de nome parecido** — `libpq.lib` (41 KB, *import library*, exige `libpq.dll`) e
`libpq.a` (2,3 MB, estática de verdade, mas ligada à runtime dinâmica). Nenhum dos dois
serve a um binário com `/MT`.

## Decisão

**Implementar os protocolos wire nativamente**, em `lib/` — árvore separada de `src/`, sem
dependência de bibliotecas de cliente de SGBD.

```
lib/
├── pgwire/        PostgreSQL frontend/backend protocol v3
├── mywire/        MySQL/MariaDB client/server protocol  (Fase 3)
├── tds/           TDS -- SQL Server                      (Fase 3)
└── net/           socket, TLS, buffers -- comum a todos
```

`lib/` não conhece `src/`: são bibliotecas independentes, reutilizáveis e testáveis
isoladamente. `otter_db` é quem as consome e as adapta ao contrato `Holt`/`Driver`.

## Justificativa

| Benefício | Detalhe |
|-----------|---------|
| **Link estático real** | Nada de runtime alheia; `/MT` sem conflito |
| **Zero dependência externa** | Não exige PostgreSQL instalado para compilar |
| **Desbloqueia MySQL** | `libmysqlclient` é GPL e estava proibida (ADR 0002). Protocolo próprio não tem essa restrição |
| **Fetch binário sob controle** | Decidimos quando usar formato binário, por tipo — libpq não dá esse controle fino |
| **Cancelamento e timeout próprios** | Sem as limitações de `PQcancel` |
| **Depuração** | Podemos inspecionar cada mensagem do protocolo — alimenta o ADR 0008 |
| **Portabilidade** | Um socket é um socket; sem caçar biblioteca de cliente por plataforma |

## Custos, declarados

| Item | h/h |
|------|-----|
| `lib/net` — socket, TLS, buffers, timeout | 180 |
| `lib/pgwire` — protocolo v3, SCRAM-SHA-256, tipos binários | 320 |
| `lib/mywire` — protocolo MySQL, `caching_sha2_password` | 280 |
| `lib/tds` — TDS 7.4 (SQL Server) | 380 |
| Testes de conformidade por protocolo | 240 |
| **Total** | **1.400 h/h** |

Contra ~200 h/h se usássemos bibliotecas prontas. **O delta é ~1.200 h/h**, e parte dele já
estava previsto: o ADR 0002 orçava ~200 h/h extras para o protocolo do MySQL, justamente por
causa da GPL.

### Riscos

| Risco | Mitigação |
|-------|-----------|
| **SCRAM-SHA-256** é obrigatório no PostgreSQL moderno | Implementar corretamente: PBKDF2-HMAC-SHA256, prova do cliente e verificação do servidor. Não é opcional |
| **TLS** exige biblioteca de criptografia | Schannel no Windows, OpenSSL no Linux — API do SO, sem dependência nova |
| Mudança de protocolo entre versões do SGBD | Protocolo v3 do PostgreSQL é estável desde 2003; negociar versão no handshake |
| Tipos binários mal decodificados | `numeric` e `timestamp` têm representação própria; testar contra servidor real |
| Reinventar bugs já resolvidos | Testes de conformidade contra instância real na CI |

## Sequenciamento

1. **`lib/net`** — socket TCP, leitura/escrita com buffer, timeout, cancelamento
2. **`lib/pgwire`** — handshake, autenticação (`trust`, `md5`, `scram-sha-256`), query
   simples, formato texto
3. Adaptador em `otter_db` sobre `pgwire`
4. Formato binário por tipo, `Extended Query` (prepared statements), `COPY`
5. Fase 3: `mywire` e `tds`

Começamos pelo formato texto: é o que valida o handshake e a decodificação de mensagens. O
formato binário — que é a vantagem de performance — vem depois, sobre base testada.

## Consequências

- `libpq` sai do projeto; `src/db/drivers/postgres.cpp` é reescrito sobre `lib/pgwire`
- Compilar o C-Otter deixa de exigir PostgreSQL instalado
- A restrição de licença do MySQL (ADR 0002) deixa de existir
- Criptografia (SHA-256, HMAC, PBKDF2) passa a ser necessária no projeto — via API do SO,
  não implementada à mão
- **Total do projeto:** ~15.000 → **≈ 16.200 h/h**

## Referências

- [PostgreSQL Frontend/Backend Protocol](https://www.postgresql.org/docs/current/protocol.html)
- ADR 0002 — link estático e a proibição de bibliotecas GPL/LGPL
- ADR 0008 — inspeção de protocolo como recurso de diagnóstico
