# Diretivas do projeto C-Otter

Regras de trabalho, não documentação de código. Valem para toda contribuição.

---

## 1. Mapear antes de implementar

**Antes de construir qualquer elemento novo de UI ou de modelo, levantar no repositório do
DBeaver (`D:\Tootega\Source\dbeaver`) todas as variedades que ele oferece para aquele
elemento.** Extrair do código, não estimar de memória.

Por quê: o C-Otter tem entregado versões empobrecidas por não olhar o alvo primeiro. Dois
casos concretos:

- O diálogo de conexão nasceu com 5 campos; o do DBeaver tem catálogo de ~50 drivers e 8
  abas de propriedades.
- O Navigator mostra 4 tipos de nó; a árvore do DBeaver tem ~70.

Onde procurar:

| O que | Onde |
|-------|------|
| Comandos, atalhos, menus, diálogos | `plugin.xml` de cada plugin |
| Estrutura da árvore de objetos | elemento `<tree>` do `plugin.xml` do driver |
| Campos de diálogo | `*Page*.java`, `*Dialog.java` |
| Rótulos traduzidos | `OSGI-INF/l10n/bundle.properties` |
| Tipos de objeto do SGBD | classes `Postgre*.java` do modelo |
| Mapa já extraído | `docs/DBEAVER-MAP.md`, `docs/NAVIGATOR-TREE.md` |

O mapa vai ao documento apropriado **antes** do código, com o estado de cada item. Implementar
um subconjunto é legítimo; implementar sem saber o tamanho do conjunto, não.

## 2. Verificar na aplicação rodando, não no build

Build limpo não prova que funciona. Vários defeitos só apareceram ao capturar a tela:

- Texto claro sobre fundo claro no tema Light
- Painéis desancorando ao trocar de idioma
- Indicador de modificado que nunca aparecia
- Item de menu "Localizar" sem ação

Quando o efeito for visual, capturar e olhar. Quando for de dados, executar contra o banco.

## 3. Estado no mesmo commit

Funcionalidade concluída muda de ⬜ para ✅ em `docs/ELEMENTS.md`, `docs/PARITY.md` ou
`docs/NAVIGATOR-TREE.md` **no mesmo commit** que a implementa. Inventário desatualizado dá
falsa confiança — é pior que nenhum.

## 4. Métrica com o denominador certo

Medir o produto contra si mesmo produz número que sobe sem o produto se aproximar do alvo.
`ELEMENTS.md` chegou a reportar 63% quando a cobertura real era 2,5%.

Cobertura é sempre contra o DBeaver.

## 5. Ícone: o original do DBeaver

**Cada nó da árvore e cada ação da barra usa o ícone que o DBeaver usa para aquilo.** Quem
vem de lá reconhece o ícone antes de ler o rótulo — é a diretiva 12 aplicada ao desenho.

Como acrescentar um:

1. Achar o arquivo no DBeaver (`icon=` do `plugin.xml`, resolvido por `DBIcon.java` /
   `UIIcon.java`).
2. Pôr a linha no `MAP` de `tools/embed_icons.py` e rodar a ferramenta — ela copia o
   original para `assets/icons/dbeaver/`, atualiza o `NOTICE` e gera `src/ui/icon_assets.cpp`.
3. Para pasta, a regra fica em `folder_icon()` (`src/ui/icon_images.cpp`).

Só se desenha em `src/ui/icons.cpp` (traço fino, caixa normalizada −0.5..0.5 de `Canvas`) o
que o DBeaver **não tem**, e o equivalente vetorial de cada ícone novo: os vetoriais são o
conjunto "C-Otter" (*Help → Icons*) e o que aparece sem textura. Nada de fonte de ícones.

Os SVG são rasterizados no tamanho exato pelo nanosvg; um SVG que ele não lê sai vazio em
silêncio, e o teste `icons_dbeaver_originals_all_rasterize` existe para isso. Decisão e
alternativas rejeitadas no ADR 0019.

## 6. Nada de campo que finge funcionar

Campo de UI sem implementação por trás precisa dizer isso na tela, como as abas SSH/SSL/Proxy
do diálogo de conexão fazem. Um campo que parece funcionar e não funciona é pior que um campo
ausente.

## 7. Teste que impede o defeito de voltar

Defeito corrigido ganha teste, quando testável. Exemplos no projeto:

- Contraste WCAG por tema — impede texto ilegível
- Registro do catálogo pt-BR — impede o linker descartá-lo de novo
- Vetores oficiais de SHA-256/HMAC/PBKDF2 — validam a criptografia contra o padrão

## 8. Texto de UI passa por `TR()`

Inglês é a chave de tradução e o padrão. Código novo já nasce com `TR()`; nada de literal em
português na UI. Janelas usam `TRW("Título", "###IdEstável")` — o ImGui identifica janelas
pelo nome, e traduzir o título as desancoraria do layout.

## 9. Decisão irreversível vira ADR

`docs/adr/`. Inclui o que foi rejeitado e por quê. ADR revogado é marcado, não apagado — o
raciocínio continua útil (ver ADR 0003, revogado pelo 0007).

## 10. Comentar o porquê, não o quê

O código já diz o que faz. O comentário explica a decisão, a armadilha, a restrição do
protocolo. Exemplo real:

```cpp
// Guarda o id ANTES de mover: depois do move, documents_[keep_index] e' um
// unique_ptr vazio e consulta-lo seria desreferenciar nulo.
```

## 11. Relatar o que não funcionou

Se um teste falhou, dizer com a saída. Se um passo foi pulado, dizer. Se a automação de UI
errou o clique três vezes, dizer — e não apresentar como verificado o que não foi.


## 11-A. Conferir na tela sem tomar a máquina de quem está usando

O usuário trabalha na mesma máquina enquanto a conferência roda. **Nada de clique,
tecla ou foco roubado** — já aconteceu de um clique direito cair na janela do navegador
dele, e de uma captura fotografar a janela errada.

| Para | Usar |
|---|---|
| Abrir o programa | `build\start_app.ps1` (`OTTER_NO_FOCUS`: a janela nasce sem foco) |
| Acionar a tela | `tools\cmd.ps1 "<linha>" ...` — o canal `OTTER_COMMAND_FILE`, que executa pelo MESMO caminho do menu |
| Clicar sem o mouse dele | `tools\postclick.ps1 -X -Y [-Hover]` (PostMessage só à janela de teste; coordenadas da captura) |
| Ver a tela | `tools\screenshot.ps1` (PrintWindow: fotografa a janela mesmo coberta) |
| Conferir o SQL de uma revisão | `ddl dump` grava o script em `<arquivo de comandos>.ddl` — texto, não pixels |
| Conferir o efeito | `spike_qq.exe <perfil> --db <banco> "<sql>"` |

Funcionalidade nova que só se alcança por clique ganha uma linha no canal de comandos
(`poll_command_file`, `object_command`). O canal prova que o comando funciona; **não**
prova que a tecla ou o clique chegam a ele — isso se diz no relato (diretiva 11).

## 12. Elemento de tela parecido com o do DBeaver — e validado como tal

**Todo elemento de tela e de diálogo deve ser semelhante ao equivalente do DBeaver: o
mesmo tipo de controle, no mesmo lugar, com o mesmo rótulo.** E isso precisa ser
**validado na tela**, contra o DBeaver rodando ou contra uma captura dele — não contra a
lembrança de como ele é.

Por quê, nas palavras do usuário:

> "Precisamos de paridade de funcionamento com o DBeaver, porque quem está acostumado com
> ele terá dificuldade se for totalmente diferente."

Isso amplia a diretiva 1. Mapear o que o DBeaver **oferece** não basta; é preciso mapear
**onde** ele oferece. Um campo que existe mas está em outro lugar não tem paridade — quem
procura onde está acostumado não acha, e conclui que a funcionalidade não existe.

O caso que produziu esta diretiva: o **nome da conexão** ficava na aba "Geral", a oitava do
diálogo. A faixa do topo apenas o exibia. O usuário viu o nome na tela, clicou nele, nada
aconteceu, e relatou "não está editando nome da conexão". O campo funcionava; estava no
lugar errado. No DBeaver o nome fica no topo, sempre visível.

O que validar, em ordem:

| Pergunta | Como responder |
|---|---|
| O controle existe no DBeaver? | `plugin.xml`, `*Page*.java`, `*Dialog.java` |
| Está no mesmo lugar? | captura do DBeaver ao lado da nossa |
| O rótulo é o mesmo? | `OSGI-INF/l10n/bundle.properties` |
| Chega-se a ele pelo mesmo caminho? | mesma aba, mesmo menu, mesmo atalho |

Para a comparação lado a lado, com as duas janelas já abertas na tela a conferir:

```powershell
tools\compare_ui.ps1 -Out dialogo.png    # DBeaver à esquerda, C-Otter à direita
```

Divergir é legítimo quando há razão — e a razão vai no comentário ou no ADR. Divergir sem
perceber, não.

Mapa do diálogo de conexão em `docs/DIALOG-PARITY.md`; da árvore, em
`docs/NAVIGATOR-TREE.md`.

## 13. Evoluir o DBeaver, não regredir — e com acabamento

Nas palavras do usuário:

> "Este projeto pode e deve evoluir o DBeaver, não regredir."

> "Estamos numa era moderna que tudo deve ser elegante, suave, simples e funcional."

Paridade (diretiva 12) é o piso, não o teto. Onde o DBeaver carrega uma herança ruim do
Eclipse, o C-Otter oferece o caminho melhor **sem tirar o antigo**: é o que os perfis de
atalho fazem (DBeaver e C-Otter, em *Help → Keymap*). Um comando novo entra na tabela de
`src/ui/commands.cpp` com as teclas dos dois perfis, e `docs/EDITOR-COMMANDS.md` é gerado
dela (`python tools/commands_doc.py`).

Acabamento é requisito:

- **Dica** passa por `src/ui/hint.hpp` (`Hint(...).row(...).show()` ou `hint(texto)`).
  Nunca `ImGui::SetTooltip`: é o que mantém o cartão igual na interface inteira.
- **Popup e janela flutuante são opacos.** Texto atrás de texto não se lê.
- **Distâncias da árvore** saem de `tree_arrow_gap()` / `tree_label_gap()`, não de
  literais em cada nó.
- Capturar e **ampliar** antes de dar por pronto: sobreposição de dois pixels e atalho
  colado no rótulo só aparecem no zoom (`tools/crop.py` faz o recorte ampliado).

## 14. O produto é portátil — sempre

Nas palavras do usuário:

> "Este produto será sempre um modelo portátil."

**Tudo o que o programa grava fica em `.C-Otter/`, ao lado do executável**: preferências
(`settings.json`), disposição das janelas (`layout.ini`), conexões e senhas. Nada no perfil
do usuário, nada no registro, nada no diretório de trabalho.

- Caminho de dados se pede a `otter::data_directory()` (`src/base/paths.hpp`). Nunca
  `getenv("APPDATA")`, `HOME` ou um caminho relativo ao diretório corrente.
- Arquivo novo de configuração nasce **com os padrões gravados**, todas as opções — o
  arquivo é também a lista do que se pode configurar. O tema padrão é o **âmbar**.
- Teste não deixa arquivo fora da pasta temporária (um contexto ImGui de teste precisa de
  `io.IniFilename = nullptr`).
- O diálogo de conexão só abre sozinho quando não há conexão salva.
- **Na primeira execução** (pasta ausente ou vazia) as conexões salvas do DBeaver, do
  pgAdmin e do SSMS são copiadas para `.C-Otter/` — só leitura do lado delas, e só
  dessa vez (ADR 0023). Nas palavras do usuário: "Ao abrir e a pasta não existir ou
  estiver vazia deve copiar as conexões salvas, se existirem, do DBeaver, do pgAdmin e
  do MS SQL Server Management Studio."
- O que o programa gera por conta própria também fica lá: `exports/`, `scripts/`,
  `diagnostics.txt`.
- **Os scripts SQL do usuário ficam em `.script/`, também ao lado do executável**, e são
  gravados sozinhos — sair nunca pergunta por eles (ADR 0025). Nas palavras do usuário:
  "Todos script, deve ser salvo, na pasta '.script', referente ao exe, ao sair do app não
  deve perguntar para salvar, porque o salvamento deve ser automático, alguns ms, após
  parar a digitação." Caminho por `ui::scripts_directory()`; a conferência na tela usa
  `OTTER_SCRIPT_DIR` (o `build\start_app.ps1` já põe) para não mexer nos scripts dele.

Decisão, alternativas rejeitadas e o que isso custa (as senhas viajam com a pasta) no
ADR 0020.
