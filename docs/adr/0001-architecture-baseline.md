# ADR 0001 — Linha de base arquitetural

**Data:** 2026-09-20
**Status:** Aceito

## Contexto

C-Otter reescreve em C++20 o que o DBeaver (916.759 linhas de Java, 156 plugins) faz em
Java/Eclipse RCP. Ver `docs/ANALYSIS.md` para as medições.

## Decisões

### 1. C++23, zero dependências no núcleo

`otter_base` depende apenas da biblioteca padrão. Dependências externas só nas camadas de
driver e de UI. Motivo: controle de performance e portabilidade; concepts e `std::expected`
permitem colapsar boa parte das 471 interfaces do núcleo do DBeaver.

> **Corrigido em 2026-09-21.** A decisão original dizia C++20. O primeiro build real
> desmentiu a premissa: `std::expected` é **C++23** (`warning STL4038` no MSVC 14.51), e ele
> é o mecanismo de erro do núcleo (decisão #4). As opções eram escrever um `expected`
> próprio ou subir o padrão; subir custa menos e ambos os toolchains-alvo já suportam
> (MSVC 19.4x / VS 2022 17.10+, Clang 17+). Verificado compilando e rodando 21 testes em
> Debug e Release.

### 2. O núcleo não conhece a UI — verificado pelo linker

Regra herdada do DBeaver (`AGENTS-Architecture.md`), lá mantida por convenção. Aqui é
mecânica: `otter_add_library()` marca alvos com `OTTER_IS_UI`, e nenhum alvo abaixo de
`otter_ui` pode linkar ImGui. Convenção não checada é convenção violada.

### 3. `ResultSet` colunar, não linha-a-linha

Cada coluna é um buffer contíguo tipado + bitmap de nulos, alocado em arena por fetch. É a
decisão de performance central: elimina boxing por célula (a razão do DBeaver consumir GBs),
permite ao grid ler sem cópia e torna export trivial.

### 4. Sem exceções no núcleo

`std::expected<T, Error>` abaixo de `otter_ui`. Exceções não cruzam fronteira de driver.
Motivo: caminhos de erro previsíveis e custo determinístico.

### 5. Registry estático de drivers, sem OSGi

Drivers se auto-registram em tempo de compilação. Ver ADR 0002 para a implicação de link.

### 6. UTF-8 internamente, sempre

Conversão apenas na fronteira de driver e de SO (UTF-16 no Win32). Evita a classe de bugs de
encoding que aparece quando cada SGBD impõe seu charset.

## Consequências

- O modelo de metadados é redesenhado a partir dos casos de uso, **não** traduzido interface
  a interface das 117 `DBS*` — tradução 1:1 produziria over-engineering severo.
- A camada `model.jdbc` do DBeaver (24.180 linhas) é descartada, não portada.
- As consultas SQL de metadados dos plugins `ext.*` são portadas quase literalmente: são o
  ativo de maior valor por linha do repositório de origem, e exigem atribuição Apache 2.0.
