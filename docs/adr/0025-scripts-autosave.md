# ADR 0025 — Scripts gravados sozinhos em `.script`, e reabertos ao iniciar

**Estado:** aceito (2026-10-01)

## Contexto

Pedidos do usuário, no mesmo dia:

> "Coloque um botão na header das abas principais para fechar, ao abrir o app, e não
> existir scripts, deve abrir sem aba principal."

> "Todos script, deve ser salvo, na pasta '.script', referente ao exe, ao sair do app não
> deve perguntar para salvar, porque o salvamento deve ser automático, alguns ms, após
> parar a digitação."

Até aqui um script só existia em disco se o usuário escolhesse *Save script* e um nome
de arquivo. Sair com texto digitado abria "There is work that was not saved — 1 script(s)
with unsaved text", e a saída era *Exit and discard*.

No DBeaver (levantado em `SQLEditorUtils`, `SQLPreferenceConstants`,
`SQLEditorPreferencesInitializer`):

| O que | DBeaver |
|---|---|
| Onde | `<workspace>/<projeto>/Scripts` |
| Nome | `Script.sql`, `Script-1.sql`, … (`ResourceUtils.getUniqueFile`) |
| Conexão do script | metadados do projeto (`default-datasource`) |
| Gravação | `autoSaveOnClose` ligado; `autoSaveOnChange` e `autoSaveOnExecute` desligados |
| Script vazio | apagado ao fechar (`script.delete.empty` = `DELETE_NEW`) |
| Ao reabrir | os editores abertos voltam; conecta ao ativar o editor |

## Decisão

1. **Pasta `.script`, ao lado do executável** (`ui::scripts_directory()`). Não dentro de
   `.C-Otter/`: foi o que o usuário pediu, e separa o trabalho dele (os scripts) da
   configuração com senhas. Continua portátil — copiar a pasta do programa leva os dois.
2. **Gravação automática** 0,4 s depois da última edição (`MainShell::autosave_scripts`),
   e imediatamente ao sair, ao fechar a aba e em *Save script*. A mudança é percebida pelo
   índice de desfazer do editor, não comparando o texto a cada quadro.
3. **Sair não pergunta pelos scripts.** A confirmação fica só para o que não dá para
   gravar sem pedir: célula editada e ainda não enviada ao banco, e editor de objeto com
   alteração pendente.
4. **Nomes do DBeaver**: `Script.sql`, `Script-1.sql`, … O nome é reservado quando a aba
   nasce, para o rótulo não mudar na primeira gravação.
5. **Índice `session.json`** na mesma pasta: para cada script, o arquivo, a conexão
   (chave do perfil salvo e, de reserva, o nome), o banco quando é a sessão de outro banco
   do servidor (ADR 0018), o título dado em *Rename tab*, se está fixado e se está aberto;
   e qual estava na frente.
6. **Ao iniciar, as abas abertas voltam**, cada uma na conexão dela. Sem script, o
   programa abre sem aba.
7. **Fechar a aba não apaga o script.** Ele fica em `.script`, marcado como fechado, e
   volta por *Show scripts* — que no DBeaver é a pasta Scripts do projeto. *Delete this
   script* continua apagando o arquivo.
8. **Aba vazia não vira arquivo**, e um script esvaziado é apagado — o `DELETE_NEW` do
   DBeaver, sem a espera pelo fechamento.
9. **Arquivo aberto de fora** (*Open SQL script*) é gravado **no próprio lugar**. *Save
   script as* tira o script de `.script` e passa a gravar no arquivo escolhido.
10. Gravação por arquivo temporário + `rename`: o programa pode ser encerrado a qualquer
    instante sem deixar um script pela metade.

## Divergências do DBeaver, e por quê

- **Não conecta sozinho ao reabrir.** O DBeaver conecta ao ativar o editor
  (`connectOnActivation`). Aqui a aba volta com *not connected* e um botão *Connect*: abrir
  o programa não deve abrir conexão — talvez de produção, talvez pedindo senha — sem
  ninguém ter pedido. Custa um clique.
- **Gravar a cada pausa**, e não só ao fechar: é o pedido. O DBeaver tem a opção
  (`autoSaveOnChange`), desligada por padrão.
- **Texto posto pelo programa não é gravado** até o usuário editar: "ver dados" de uma
  tabela abre uma aba com `SELECT * FROM …`, e gravar cada uma encheria a pasta de
  arquivos que ninguém escreveu. No DBeaver esse caso é o *SQL console*, que também não é
  salvo.

## Alternativas rejeitadas

- **`.C-Otter/scripts/`** — seria o lugar pela diretiva 14 como estava escrita. O usuário
  pediu `.script` ao lado do executável; a diretiva foi atualizada.
- **Perguntar ao sair, com "Save all"** — é o que existia, com outro botão. O pedido é não
  perguntar.
- **Guardar o texto dentro do `session.json`** — um arquivo só, mas o script deixaria de
  ser um `.sql` que se abre em qualquer editor, e um índice corrompido levaria tudo.
- **Renomear o arquivo em *Rename tab*** — o título fica no índice. Um título com acento
  viraria nome de arquivo, e o caminho passa hoje por `std::filesystem::path(std::string)`,
  que no Windows lê a página de código ANSI, não UTF-8.
- **Comparar o texto a cada quadro** para saber se mudou — uma cópia do script inteiro
  144 vezes por segundo.

## O que isto custa

- **Duas instâncias do programa na mesma pasta** escrevem o mesmo `session.json`: vale a
  última. Os arquivos `.sql` não se misturam (cada aba grava o seu), mas a lista de abas
  abertas sim. Não há trava.
- Um arquivo aberto de fora é regravado sem perguntar a cada pausa na digitação.
- A conferência na tela precisa de `OTTER_SCRIPT_DIR` (pasta temporária), senão o programa
  de teste reabriria — e regravaria — os scripts de quem usa a máquina.
- Um script cuja conexão foi apagada volta preso à sessão vazia, sem conexão; *Associate
  with data source* o liga a outra.

## Testes

`tests/unit/test_script_store.cpp`: ida e volta do índice com todos os campos, índice
ausente ou corrompido, a série de nomes (incluindo o nome ocupado por uma aba ainda não
gravada), gravação que substitui sem deixar o temporário, caminho relativo só dentro da
pasta, e os caracteres que o Windows recusa em nome de arquivo.

A parte do `MainShell` (o atraso, a reabertura, o foco da aba) só é conferida na
aplicação rodando — `build\script_flow.ps1`, pelo canal de comandos.
