# ADR 0003 — Componente do editor SQL (Notepad++ / Scintilla)

**Data:** 2026-09-21
**Status:** Proposto — decisão depende do spike da Fase 0

## Contexto

O editor SQL é o item mais caro e de maior risco do projeto: **700 h/h** estimadas, com
variação de ±400 h/h (`docs/EFFORT.md` §9). Editor de texto rico em modo imediato (ImGui) é
construção manual: realce incremental, autocomplete, multi-cursor, undo/redo, folding, IME,
seleção retangular. Nenhum desses vem de graça.

Surgiu a pergunta: **usar o Notepad++ como biblioteca, e aceitar seus plugins?**

## Achados da investigação

O ponto central é que **Notepad++ e Scintilla são coisas distintas, com licenças opostas.**

| | Notepad++ | Scintilla / Lexilla |
|---|---|---|
| O que é | Aplicação completa (Win32) | **Componente de edição embutível** |
| Licença | **GPL v3+** | **HPND** (permissiva, estilo MIT/BSD) |
| Embutível em outro app | Não — é um `.exe` | **Sim, foi feito para isso** |
| Plataformas | Windows | Win32, GTK, macOS, Qt |
| Link estático em produto fechado | **Proibido** (ver ADR 0002) | **Permitido** |
| Versão atual | 8.9.x | 5.5.8 (nov/2025) |

Notepad++ **usa** Scintilla. Toda a capacidade de edição que se admira no Notepad++ —
realce, folding, autocomplete, múltiplos cursores — **é do Scintilla**, não do Notepad++.
O Notepad++ acrescenta a janela, abas, menus, sessões e o sistema de plugins.

### Licença do Scintilla (verificada em scintilla.org/License.txt)

> Copyright 1998-2021 by Neil Hodgson. Permission to use, copy, modify, and distribute this
> software and its documentation **for any purpose and without fee** is hereby granted,
> provided that the above copyright notice appear in all copies.

Compatível com link estático e produto proprietário. A única obrigação é preservar o aviso
de copyright.

### Sobre os plugins do Notepad++

Aceitar plugins do Notepad++ exigiria replicar:

1. **A API de mensagens `NPPM_*`** via `SendMessage` do Win32 — a superfície é C-compatível,
   o que ajuda, mas são centenas de mensagens.
2. **As notificações `WM_NOTIFY`/`SCN_*`** no formato exato esperado.
3. **O modelo de janelas do Notepad++** — plugins assumem `HWND` de janela principal, dois
   controles Scintilla (painel principal e secundário), barra de menus, docking dialogs.
4. **O ciclo de vida de DLL** (`DllMain`, `setInfo`, `getFuncsArray`, `beNotified`).

Os plugins são **DLLs Win32 que assumem a arquitetura interna do Notepad++**. Isso colide
frontalmente com três decisões já tomadas:

- **ADR 0002 (link estático total)** — plugins são DLLs carregadas dinamicamente, por
  definição.
- **Portabilidade Windows+Linux** — a API de plugins é Win32 pura; não existe no Linux.
- **UI em ImGui** — plugins esperam `HWND` reais e diálogos nativos Win32, não widgets em
  modo imediato.

Além disso, replicar a API de um programa GPL para executar seus plugins coloca o produto em
zona jurídica cinzenta quanto a obra derivada — risco desnecessário.

**Conclusão sobre plugins: rejeitado.** O custo é alto, o benefício é baixo (os plugins do
Notepad++ são de edição de texto genérica: comparação de arquivos, FTP, formatação XML — não
de manipulação de banco de dados) e conflita com o núcleo da arquitetura.

## Opções para o editor

### Opção A — Editor próprio em ImGui (plano original)

| | |
|---|---|
| Custo | 700 h/h (±400) |
| Licença | Sem restrição |
| Portabilidade | Total, um só código |
| Integração com ImGui | Perfeita — mesmo loop de render, mesmo tema da lontra |
| Risco | **Alto** — é o maior risco do projeto |

### Opção B — Scintilla nativo, janela embutida (**recomendada**)

Scintilla como controle nativo (`ScintillaWin` no Windows, `ScintillaGTK` no Linux),
hospedado em janela filha ao lado da área ImGui.

| | |
|---|---|
| Custo | **~260 h/h** — integração, binding SQL, ponte de autocomplete |
| Economia | **≈ 440 h/h** frente à Opção A |
| Licença | HPND — compatível com link estático (ADR 0002) |
| Portabilidade | Win32 + GTK: **duas integrações**, mas o miolo é comum |
| O que se ganha de graça | Realce, folding, múltiplos cursores, undo, IME, acessibilidade, seleção retangular, marcadores, indentação — décadas de maturidade |
| Risco | **Baixo** — componente provado em Notepad++, Geany, SciTE |

**Custo real:** a UI deixa de ser "ImGui puro". Uma região da janela passa a ser um controle
nativo com aparência e comportamento próprios, e a composição visual com o resto da interface
exige cuidado. Também reintroduz dependência de GTK no Linux.

**Ganho colateral relevante:** Scintilla traz **acessibilidade nativa**, hoje listada como
limitação conhecida da v1 em `docs/ANALYSIS.md` §7. A Opção B elimina esse débito.

### Opção C — Scintilla renderizado dentro do ImGui

Existe `ScintillaGL` (port para OpenGL), mas está marcado como *work in progress* e é
Windows-only. A arquitetura do Scintilla permite derivar de `ScintillaBase` implementando a
camada `Surface`, então um backend ImGui é tecnicamente possível.

| | |
|---|---|
| Custo | ~450 h/h + manutenção perpétua do backend |
| Risco | **Alto** — backend não oficial, quebra a cada versão do Scintilla |

Rejeitada: combina o custo da Opção A com a dependência da Opção B.

## Decisão proposta

**Opção B — Scintilla embutido como controle nativo**, com a fronteira isolada atrás de uma
interface própria:

```cpp
namespace otter::ui {
class SqlEditor {          // interface estavel; implementacao trocavel
public:
    virtual void set_text(std::string_view) = 0;
    virtual std::string text() const = 0;
    virtual void set_completions(std::span<const Completion>) = 0;
    virtual void mark_errors(std::span<const Diagnostic>) = 0;
};
} // implementacoes: ScintillaEditor | ImGuiEditor
```

Assim o spike da Fase 0 compara as duas implementações sobre o mesmo contrato, e a decisão
final é tomada com medição, não com opinião.

**Notepad++ como biblioteca: rejeitado** — é GPL v3, é um executável, não um componente.
**Plugins do Notepad++: rejeitado** — Win32-only, exigem carregamento dinâmico de DLL e
assumem a arquitetura interna do Notepad++.

## Impacto na estimativa

| Item | Antes | Depois (Opção B) |
|------|-------|------------------|
| Editor SQL | 700 h/h | 260 h/h |
| `otter_ui` | 2.800 h/h | 2.360 h/h |
| **Total do projeto** | 13.250 h/h | **≈ 12.700 h/h** |

Redução de ~550 h/h (contingência incluída) e — mais importante que o número — **remoção do
maior risco isolado do projeto**.

## Referências

- [Scintilla](https://www.scintilla.org/) — componente e licença
- [Scintilla Design](https://scintilla.sourceforge.io/Design.html) — `ScintillaBase`, camada `Surface`
- [Notepad++ Plugin Communication](https://npp-user-manual.org/docs/plugin-communication/) — API `NPPM_*`
- [Notepad++ — Wikipedia](https://en.wikipedia.org/wiki/Notepad%2B%2B) — GPL v3
