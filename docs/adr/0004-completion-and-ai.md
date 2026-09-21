# ADR 0004 — Code completion, IntelliSense e integração com IA

**Data:** 2026-09-21
**Status:** Proposto

## Contexto

Requisito declarado: **code completion e IntelliSense soberbos**, mais **integração com IA**.

Isto não é um detalhe de UI — é o diferencial competitivo do produto e reposiciona o
`otter_sql` de "parser para executar queries" para "motor de conhecimento sobre o schema".

Referência de esforço no DBeaver: o subpacote `semantics` de `model.sql` tem **19.995
linhas**, e `model.ai` outras **19.721** (`docs/ANALYSIS.md` §1).

## Decisão 1 — Uma única árvore, do realce à execução

O erro clássico é ter um lexer para pintar a tela e outro para entender a query. Eles
divergem, e o editor passa a realçar uma coisa enquanto o executor entende outra.

**O lexer/parser do `otter_sql` é a fonte única.** O Scintilla recebe o realce via um
`ILexer5` próprio que consome essa árvore (ADR 0003). Consequências:

- O que é realçado é exatamente o que será executado
- Diagnósticos (erro de sintaxe, coluna inexistente) saem da mesma análise
- Autocomplete e realce nunca discordam

## Decisão 2 — Análise incremental e tolerante a erro

Autocomplete útil roda sobre **texto inválido** — o usuário está no meio de digitar. Isso
impõe:

| Requisito | Implicação |
|-----------|------------|
| Parser tolerante a erro | Produz AST parcial com nós de erro, nunca falha "seco" |
| Reparse incremental | Só o statement editado é reanalisado, não o script inteiro |
| Orçamento de latência | **< 16 ms** para não perder frame; análise pesada vai para worker |
| Cancelamento | Digitar durante análise cancela a anterior (`otter_base` já tem jobs canceláveis) |

## Decisão 3 — Camadas de completion, da mais barata à mais cara

Cada camada só roda se as anteriores não bastarem. É o que separa completion soberbo de
uma lista alfabética de keywords.

| # | Camada | Fonte | Latência |
|---|--------|-------|----------|
| 1 | Keywords do dialeto | Tabela estática | < 1 ms |
| 2 | **Escopo sintático** | AST: após `FROM` → tabelas; após `SELECT` → colunas do escopo | < 5 ms |
| 3 | **Metadados** | Pocket Rock (cache local) — tabelas, colunas, tipos, chaves | < 10 ms |
| 4 | **Inferência de JOIN** | Foreign keys → sugere `ON a.id = b.a_id` pronto | < 10 ms |
| 5 | Histórico e frequência | Queries anteriores do usuário nesta conexão | < 5 ms |
| 6 | **IA** | LLM com contexto de schema | 200 ms – 2 s, assíncrono |

**Camada 4 é o diferencial.** Ao digitar `SELECT * FROM pedidos p OTTER JOIN `, o sistema já
sabe, pelas foreign keys, que `clientes` se liga por `p.cliente_id = c.id` — e oferece o
`ON` completo. Isso exige apenas metadados que já teremos em cache, e é o tipo de coisa que
faz um usuário trocar de ferramenta.

## Decisão 4 — Ranking por contexto, não alfabético

Ordenação das sugestões:

1. Colunas das tabelas **já mencionadas na query atual** (peso máximo)
2. Match de prefixo exato > match fuzzy por subsequência
3. Frequência de uso do usuário nesta conexão
4. Cardinalidade/relevância do objeto no schema
5. Alfabético (desempate apenas)

Fuzzy matching estilo `cliid` → `cliente_id`, com destaque dos caracteres casados.

## Decisão 5 — IA como camada isolada, nunca no caminho crítico

Esta é a decisão de arquitetura mais importante do ADR: **o editor funciona plenamente sem
IA.** A IA enriquece; não sustenta.

```cpp
namespace otter::ai {

class Provider {                          // interface virtual pura
public:
    virtual ~Provider() = default;
    virtual std::string_view id() const = 0;
    virtual Capabilities capabilities() const = 0;

    virtual void complete(const Request&, CompletionSink&) = 0;   // streaming
    virtual void cancel(RequestId) = 0;
};

} // implementacoes: AnthropicProvider | OpenAIProvider | LocalProvider | ...
```

### Funcionalidades de IA previstas

| Recurso | Descrição |
|---------|-----------|
| **Natural language → SQL** | "quantos pedidos por cliente no último mês" → SQL do dialeto correto |
| **Explicação de query** | SQL complexo → descrição em português |
| **Otimização** | Sugere índices e reescritas, usando `EXPLAIN` real do SGBD |
| **Correção de erro** | Erro do servidor + query → correção proposta |
| **Completion inline** | Sugestão de continuação em cinza, estilo Copilot |
| **Documentação de schema** | Gera descrição de tabelas e colunas |

### Restrições inegociáveis

1. **Privacidade explícita.** Enviar schema ou dados a um serviço externo **publica** essa
   informação. Exige consentimento por conexão, com três níveis:
   - `off` — nenhum dado sai da máquina (padrão)
   - `schema-only` — nomes de tabelas/colunas, nunca linhas
   - `full` — permite amostras de dados (exige confirmação explícita, por sessão)
2. **Provider local suportado** (Ollama, llama.cpp) para ambientes onde nada pode sair.
3. **Nunca executa automaticamente.** SQL gerado por IA é sempre proposto, jamais rodado sem
   ação do usuário. Uma sugestão de `DELETE` mal interpretada é um incidente.
4. **Indicação visual clara** do que foi gerado por IA.
5. **Custo visível** — tokens consumidos por requisição.
6. **Chaves de API no cofre do SO** (DPAPI / libsecret), nunca em arquivo de configuração.

### Modelos-alvo

Para os recursos de linguagem natural, a família Claude 5 (`claude-opus-5`,
`claude-sonnet-5`) é a referência inicial, com `claude-haiku-4-5-20251001` para completion
inline, onde latência importa mais que profundidade. A interface `Provider` mantém a escolha
trocável.

## Impacto na estimativa

Isto **aumenta** o escopo frente ao orçado em `docs/EFFORT.md`:

| Item | Antes | Depois | Δ |
|------|-------|--------|---|
| `otter_sql` — semântica | 380 | **560** | +180 |
| `otter_sql` — reparse incremental | — | **120** | +120 |
| Completion: camadas 1–5 + ranking | (em ui) | **260** | +260 |
| `otter_ai` — provider, streaming, privacidade, UI | — | **420** | +420 |
| Editor SQL (ADR 0003, Scintilla) | 700 | **300** | −400 |
| **Total do projeto** | 13.250 | **≈ 13.850 h/h** | **+600** |

A economia do Scintilla (−400 h/h) é absorvida e ultrapassada pelo completion de qualidade.
**Isto é uma escolha de produto, não um estouro:** completion soberbo é o que diferencia o
C-Otter de um cliente SQL genérico, e custa ~600 h/h a mais que o plano original.

## Viabilidade sobre o ImGuiColorTextEdit (verificada em 2026-09-21)

Após o ADR 0007 trocar Scintilla por ImGuiColorTextEdit, a pergunta obrigatória é se o
widget sustenta o completion prometido aqui. **Sustenta as camadas 1–5 por inteiro**, com
uma limitação na apresentação.

### O que a API entrega

```cpp
struct AutoCompleteState {
    std::string searchTerm;                 // prefixo digitado
    DocPos      searchTermStart, searchTermEnd;
    bool        inIdentifier, inNumber, inComment, inString;
    const Language* language;
    void*       userData;                   // ponteiro para o nosso contexto
    std::vector<std::string> suggestions;   // preenchido por nos, na nossa ordem
    bool        suggestionsPromise;         // permite resposta assincrona
};
```

| Requisito do ADR | Suportado | Como |
|---|---|---|
| Camadas 1–5 (keywords → histórico) | **Sim** | Callback próprio decide tudo |
| **Ranking contextual próprio** | **Sim** | *"the app is responsible for sorting"* — o editor não reordena |
| Fuzzy matching (`cliid` → `cliente_id`) | **Sim** | Nós filtramos; `searchTerm` é só o prefixo bruto |
| Não sugerir dentro de comentário/string | **Sim** | `triggerInComments`/`triggerInStrings`, `false` por padrão |
| Orçamento de latência < 16 ms | **Sim** | `triggerDelay` (200 ms) + callback no render |
| **Camada 6 (IA), assíncrona** | **Sim** | `suggestionsPromise=true` + `SetAutoCompleteSuggestions()` de outra thread |
| Undo/redo da inserção | **Sim** | Tratado pelo editor |
| Diagnósticos inline (erro de sintaxe) | **Sim** | API de `squiggle` e marcadores |

O ponto decisivo é que **o editor não impõe política**: ele cuida de trigger, popup,
navegação por teclado e inserção com undo; *quais* sugestões e em *que ordem* é 100% nosso.
É exatamente a divisão de responsabilidade que o completion em camadas exige.

`suggestionsPromise` merece destaque: resolve o problema difícil da camada 6. A UI não
congela esperando o LLM — o popup permanece aberto e as sugestões chegam depois, de um
worker.

### A limitação real

**`suggestions` é `std::vector<std::string>` — texto puro.** O popup não suporta, de fábrica:

- Ícone por tipo (tabela, coluna, função, keyword)
- Cor por categoria
- Painel de detalhe ao lado (comentário da coluna, assinatura da função)
- Marcação visual de sugestão vinda de IA (exigida pela §5 deste ADR)

> **Corrigido após o protótipo.** Eu havia listado também "destaque dos caracteres casados"
> como ausente. **O widget já faz isso**: digitando `cat`, o popup mostra `cat`ches com o
> prefixo casado realçado. Verificado na captura `docs/autocomplete.png`.

Um completion de fato soberbo mostra *por que* cada item está ali. Só o nome não basta.

### Mitigações, em ordem de custo

1. **Formatação no texto** (0 h/h) — `"cliente_id          int4  PK"`. Funciona porque a
   fonte é monoespaçada. Resolve o essencial, mas sem cor nem ícone.
2. **Popup próprio** (~60 h/h) — `SetAutoCompleteConfig(nullptr)` e implementamos trigger e
   popup por cima da API de cursor e posição. Liberdade total; perdemos a inserção com
   undo pronta.
3. **Patch no widget** (~40 h/h + manutenção) — estender `suggestions` para uma struct com
   ícone e detalhe, e manter o fork. MIT permite; custa divergir do upstream.

**Decisão:** começar por (1) na Fase 2, medir com uso real, e escalar para (2) se a
apresentação se mostrar insuficiente. A fronteira `SqlEditor` do ADR 0003 mantém essa troca
localizada.

### Protótipo validado (2026-09-21)

Implementado em `MainShell::suggest()` e verificado com a aplicação rodando:

- **Camadas 1 e 3** ativas: keywords do dialeto e metadados (tabelas e colunas)
- **Ranking contextual** funcionando: colunas de tabelas já citadas na query vêm primeiro
  (rank 0), chaves primárias sobem, keywords ficam por último (rank 5)
- **Fuzzy por subsequência**: `cliid` casa `cliente_id`
- **Alinhamento monoespaçado**: `catches              tabela` — mitigação (1) em uso
- **Destaque do prefixo casado** pelo próprio widget
- 143,8 fps com o popup aberto

Confirma que o ImGuiColorTextEdit sustenta o completion deste ADR. O que falta é a fonte de
dados real — `otter_sql` para o escopo sintático (camada 2) e Pocket Rock para os metadados
—, não a capacidade do editor.

### Não usaremos o TrieAutoComplete

O widget traz um autocomplete pronto (`extras/TrieAutoComplete`) que sugere keywords da
linguagem mais identificadores do documento. É rápido e **serve para nada aqui**: não conhece
schema, não conhece escopo, não infere JOIN. É a camada 1 apenas. Nosso callback o substitui.

## Sequenciamento

Completion não é um módulo que se acopla no fim — ele determina a forma do parser.

| Fase | Entrega |
|------|---------|
| 1 | Parser tolerante a erro **desde o início** (não adaptado depois) |
| 2 | Camadas 1–3: keywords, escopo sintático, metadados |
| 2 | Ranking contextual e fuzzy matching |
| 3 | Camada 4: inferência de JOIN por foreign key |
| 4 | Camada 5: histórico e frequência |
| 4–5 | `otter_ai`: provider, NL→SQL, explicação, controles de privacidade |

## Riscos

| Risco | Mitigação |
|-------|-----------|
| Latência de análise estoura 16 ms em schema grande | Índice invertido no Pocket Rock; análise pesada em worker |
| Completion sugere objeto inexistente (cache velho) | Invalidação por versão; refresh em erro do servidor |
| Vazamento de dados sensíveis para provider de IA | Padrão `off`; consentimento por conexão; provider local |
| Dependência de API externa instável | Interface `Provider`; degradação silenciosa para camadas 1–5 |
| IA gera SQL destrutivo | Nunca executa sozinha; destaque visual; confirmação para DDL/DML |

## Referências

- `docs/ANALYSIS.md` §1 — `model.sql/semantics` (19.995 linhas) e `model.ai` (19.721)
- ADR 0003 — Scintilla e a fronteira `SqlEditor`
- ADR 0002 — link estático e restrições de licença
