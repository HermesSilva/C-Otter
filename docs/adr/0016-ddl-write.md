# ADR 0016 — DDL de escrita: diff, confirmação e o estado atual obrigatório

**Data:** 2026-09-21
**Status:** Aceito

## Contexto

Até aqui o C-Otter só lia estrutura. Criar e alterar objetos é o que falta para
substituir o DBeaver no dia a dia — e é a operação mais perigosa que a
ferramenta vai oferecer: `DROP TABLE` não tem desfazer.

Mapa do que o DBeaver cobre em `docs/DDL-WRITE.md`: 26 managers no PostgreSQL,
12 no MySQL.

## Decisão 1: gerar o script, mostrar, e só executar depois de confirmado

O DBeaver mostra o SQL pendente numa aba "Persist" antes de gravar. O C-Otter
faz o mesmo, por uma janela de conferência.

É o mesmo raciocínio do ADR 0014 para a grade editável, um grau acima: lá o
pior caso de um erro é uma linha errada; aqui é uma tabela inteira.

Rejeitado: **executar direto do formulário**. Seria dois cliques mais curto, e
transformaria qualquer engano de menu numa perda irreversível.

O comando destrutivo ganha três marcas: cor de erro, o texto
"(não tem desfazer)" ao lado, e um checkbox **que sempre recomeça desmarcado**.
Herdar o "sim" da janela anterior é como um `DROP` acidental acontece.

## Decisão 2: diff, não formulário que monta SQL

`generate_alter()` recebe o **par** (estado atual, estado desejado) e emite só
o que mudou.

Um formulário que gerasse sempre a definição inteira reescreveria atributos que
o usuário não tocou. No MySQL isso não é só ineficiente: `MODIFY COLUMN`
**substitui a definição inteira**, e o que não for repetido é **removido**.

Um `MODIFY` que esqueça o `AUTO_INCREMENT` o apaga em silêncio. O erro não
aparece na hora — aparece semanas depois, quando alguém nota que o id parou de
incrementar.

Consequência: `generate_alter()` **exige** a tabela com as colunas carregadas, e
recusa quando não estão. Recusar é melhor que gerar um `ALTER` que apaga
atributos (diretiva 6).

## Decisão 3: `std::optional` para distinguir "não mexer" de "apagar"

Um `DEFAULT` que o usuário limpou vira `DROP DEFAULT`. Um que ele não tocou não
pode aparecer no `ALTER`. String vazia não distingue os dois casos;
`std::optional<std::string>` distingue.

## Decisão 4: dizer quando a sequência não é atômica

O PostgreSQL tem DDL transacional. O MySQL não: cada comando faz commit
implícito, e um script de três `ALTER`s que falha no terceiro deixa dois
aplicados.

`Capabilities::ddl_in_transaction` já registrava a diferença. A janela agora a
**diz** — em vez de oferecer um rollback que não desfaz nada.

## Decisão 5: esconder o que o SGBD não tem, não desabilitar

`FIRST` e `AFTER` só existem no MySQL. No PostgreSQL o campo de posição
simplesmente não aparece; se alguém pedir posição via API, sai um aviso e o
comando é emitido sem ela.

Mesmo critério de `CASCADE` no `DROP TABLE` do MySQL: o servidor **aceita a
palavra e a ignora**. Emiti-la faria o usuário crer numa cascata que não
acontece — o campo que finge funcionar da diretiva 6.

## Consequências

- `spikes/alter_live` existe porque o teste unitário compara **strings**: ele
  prova que a palavra `AUTO_INCREMENT` está no comando, não que a coluna
  continua auto-incrementando depois. Só o servidor responde isso.
- A árvore é invalidada após o DDL. Sem isso a coluna nova não apareceria até o
  usuário mandar atualizar — o mesmo defeito que a grade tinha ao exibir o valor
  anterior depois de gravar.
- O alvo da recarga é guardado ao **abrir** a janela, não ao confirmar: quando o
  usuário confirma, o menu de contexto que originou a ação já fechou.
- ~~**Não verificado contra PostgreSQL.**~~ **Verificado em 2026-09-30** contra
  PostgreSQL 18.2 por `spikes/dbeaver_import/alter_live_pg.cpp` (81 verificações,
  0 falhas), usando o perfil salvo — sem senha na linha de comando. Ver
  `docs/DDL-WRITE.md` §4.
