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

## 5. Nada de campo que finge funcionar

Campo de UI sem implementação por trás precisa dizer isso na tela, como as abas SSH/SSL/Proxy
do diálogo de conexão fazem. Um campo que parece funcionar e não funciona é pior que um campo
ausente.

## 6. Teste que impede o defeito de voltar

Defeito corrigido ganha teste, quando testável. Exemplos no projeto:

- Contraste WCAG por tema — impede texto ilegível
- Registro do catálogo pt-BR — impede o linker descartá-lo de novo
- Vetores oficiais de SHA-256/HMAC/PBKDF2 — validam a criptografia contra o padrão

## 7. Texto de UI passa por `TR()`

Inglês é a chave de tradução e o padrão. Código novo já nasce com `TR()`; nada de literal em
português na UI. Janelas usam `TRW("Título", "###IdEstável")` — o ImGui identifica janelas
pelo nome, e traduzir o título as desancoraria do layout.

## 8. Decisão irreversível vira ADR

`docs/adr/`. Inclui o que foi rejeitado e por quê. ADR revogado é marcado, não apagado — o
raciocínio continua útil (ver ADR 0003, revogado pelo 0007).

## 9. Comentar o porquê, não o quê

O código já diz o que faz. O comentário explica a decisão, a armadilha, a restrição do
protocolo. Exemplo real:

```cpp
// Guarda o id ANTES de mover: depois do move, documents_[keep_index] e' um
// unique_ptr vazio e consulta-lo seria desreferenciar nulo.
```

## 10. Relatar o que não funcionou

Se um teste falhou, dizer com a saída. Se um passo foi pulado, dizer. Se a automação de UI
errou o clique três vezes, dizer — e não apresentar como verificado o que não foi.
