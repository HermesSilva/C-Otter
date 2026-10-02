# Alterações locais do C-Otter neste widget

O código desta pasta é de terceiro (licença MIT, ver `LICENSE`). O que foi alterado em
relação ao original fica listado aqui, para que uma atualização do widget não desfaça a
correção sem ninguém perceber. Cada trecho alterado está marcado no código com `C-Otter`.

| Arquivo | Função | O que mudou | Por quê |
|---|---|---|---|
| `TextEditor.cpp` | `TextEditor::handleCharacter` | Digitar um caractere de fechamento (`)`, `]`, `}`, `'`, `"`) quando o próximo caractere já é esse mesmo fechamento apenas avança o cursor. | O original só pulava o fechamento se ele fosse digitado logo após o abridor. Com conteúdo no meio, `'texto'` virava `'texto''` e `${n}` virava `${n}}` — todo literal SQL saía com um caractere sobrando. Visto na tela em 2026-09-30, ao digitar um script de teste. |

Ao atualizar o widget: reaplicar o trecho, ou conferir se o original passou a tratar o caso
e então remover esta linha.
