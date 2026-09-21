# DDL de escrita: criar, alterar e remover objetos

Mapa levantado dos `*Manager.java` de `org.jkiss.dbeaver.ext.postgresql/edit/`
e `org.jkiss.dbeaver.ext.mysql/edit/`. Diretiva 1: medir o alvo antes de
implementar.

---

## 1. O que o DBeaver cobre

**26 managers no PostgreSQL, 12 no MySQL.** Cada um implementa até quatro
operações: `create`, `modify`, `rename`, `delete`.

| Objeto | PostgreSQL | MySQL | C-Otter |
|---|:---:|:---:|:---:|
| Tabela | ✅ | ✅ | 🟡 renomear, comentário, remover |
| Coluna | ✅ | ✅ | ✅ add, drop, rename, tipo, nulidade, default, comentário |
| Índice | ✅ | ✅ | ✅ criar e remover |
| Constraint (PK, UNIQUE, CHECK) | ✅ | ✅ | ✅ criar e remover |
| Foreign key | ✅ | ✅ | ✅ criar e remover |
| View | ✅ | ✅ | ⬜ |
| Materialized view | ✅ | ➖ | ⬜ |
| Sequence | ✅ | ✅ MariaDB | ⬜ |
| Trigger | ✅ | ✅ | ⬜ |
| Procedure / function | ✅ | ✅ | ⬜ |
| Schema / database | ✅ | ✅ | ⬜ |
| Tipo de dado | ✅ | ➖ | ⬜ |
| Extensão | ✅ | ➖ | ⬜ |
| Role / usuário | ✅ | ✅ | ⬜ |
| Tablespace | ✅ | ➖ | ⬜ |
| Partição | ✅ | ✅ | ⬜ |
| Política (RLS) | ✅ | ➖ | ⬜ |
| Event trigger | ✅ | ➖ | ⬜ |
| Job (pgAgent) | ✅ | ➖ | ⬜ |
| Foreign server / foreign table | ✅ | ➖ | ⬜ |
| Event (agendado) | ➖ | ✅ | ⬜ |

## 2. O que cada `ALTER` cobre

Extraído dos managers. É o conjunto que uma tela de edição de tabela precisa
oferecer para não ser um subconjunto empobrecido.

### Tabela

| Operação | PostgreSQL | MySQL |
|---|---|---|
| Renomear | `ALTER TABLE … RENAME TO` | `RENAME TABLE … TO` |
| Comentário | `COMMENT ON TABLE … IS` | `ALTER TABLE … COMMENT =` |
| Tablespace | `SET TABLESPACE` | ➖ |
| OIDs | `SET WITH/WITHOUT OIDS` | ➖ |
| Row Level Security | `ENABLE/DISABLE ROW LEVEL SECURITY` | ➖ |
| Particionamento | `PARTITION BY` | `PARTITION BY` |
| Engine | ➖ | `ALTER TABLE … ENGINE =` |
| Charset / collation | ➖ | `CONVERT TO CHARACTER SET` |
| `AUTO_INCREMENT` | ➖ | `ALTER TABLE … AUTO_INCREMENT =` |

### Coluna

| Operação | PostgreSQL | MySQL |
|---|---|---|
| Adicionar | `ADD COLUMN` | `ADD COLUMN` |
| Remover | `DROP COLUMN` | `DROP COLUMN` |
| Renomear | `RENAME COLUMN … TO` | `CHANGE COLUMN` (exige o tipo junto) |
| Mudar tipo | `ALTER COLUMN … TYPE` | `MODIFY COLUMN` (exige tudo junto) |
| `NOT NULL` | `SET/DROP NOT NULL` | `MODIFY COLUMN` |
| `DEFAULT` | `SET/DROP DEFAULT` | `ALTER COLUMN … SET/DROP DEFAULT` |
| Comentário | `COMMENT ON COLUMN` | `MODIFY COLUMN … COMMENT` |

**Diferença que decide o desenho:** o PostgreSQL altera **um atributo por
vez**; o MySQL exige repetir a **definição inteira** da coluna em
`MODIFY COLUMN` — tipo, nulidade, default, comentário, `AUTO_INCREMENT`. Um
`MODIFY` que esqueça o `AUTO_INCREMENT` o REMOVE em silêncio.

Consequência: no MySQL, alterar qualquer atributo exige ter carregado **todos**
os outros. Gerar o `ALTER` a partir de um formulário parcialmente preenchido
perderia atributos — e o defeito só apareceria depois, quando alguém notasse
que o id parou de incrementar.

## 3. Decisões antes do código

### Gerar o script, nunca executar direto

O DBeaver mostra o SQL pendente numa aba "Persist" antes de gravar, e o usuário
confirma. **O C-Otter faz o mesmo, e é inegociável:** um `DROP TABLE` disparado
por um clique errado não tem desfazer.

O mesmo raciocínio do ADR 0014 para a grade editável, um grau acima: lá o pior
caso é uma linha errada; aqui é uma tabela inteira.

### Transação quando o SGBD deixa

O PostgreSQL tem DDL transacional: `BEGIN` / `ALTER` / `ALTER` / `COMMIT`
funciona, e um erro no meio desfaz tudo. **O MySQL não tem:** cada DDL faz
commit implícito e leva junto o que estava pendente.

`Capabilities::ddl_in_transaction` já registra isso, e já vem `false` no driver
MySQL. A UI precisa **dizer** a diferença: num MySQL, avisar que a sequência de
comandos não é atômica, em vez de oferecer um rollback que não desfaz nada.

### Nada de renomear como `DROP` + `CREATE`

Perderia dados. Parece óbvio, mas é um atalho tentador quando o SGBD não tem
`RENAME` para aquele objeto.

---

## 4. Estado

| Etapa | Estado |
|---|:---:|
| Gerador de `ALTER` a partir de (estado atual, desejado) | ✅ `src/db/alter.cpp` |
| Janela de conferência com confirmação | ✅ `src/ui/ddl_dialog.cpp` |
| Confirmação EXTRA para comando destrutivo | ✅ checkbox, sempre desmarcado |
| Edição manual do script antes de executar | ✅ |
| Coluna: add, drop, rename, tipo, nulidade, default, comentário | ✅ |
| Posição da coluna (`FIRST`/`AFTER`) no MySQL | ✅ escondido no PostgreSQL |
| Tabela: renomear, comentário, `DROP` | ✅ |
| `CREATE TABLE` (gerador pronto, sem tela) | 🟡 |
| Recarga da árvore depois do DDL | ✅ |
| **Índice: criar e remover** | ✅ |
| **Constraint: PK, UNIQUE, CHECK** | ✅ |
| **Chave estrangeira: criar e remover** | ✅ |
| Índice de constraint é RECUSADO | ✅ o MySQL aceitaria e perderia a chave |
| View, sequence, trigger, procedure | ⬜ |

Verificado contra **MySQL 8.0.46 real** em 2026-09-21:

- `spikes/alter_live` — 28 verificações, 0 falhas. Prova o que o teste
  unitário **não** prova: que a coluna continua auto-incrementando depois do
  `MODIFY`, que o comentário sobreviveu à mudança de nulidade, que o `DEFAULT`
  sobreviveu ao rename, que a coluna foi para a posição pedida, e — o mais
  importante — que a constraint criada **restringe de verdade**: o duplicado
  falha, a linha órfã falha. Criar a constraint e ela não valer seria o campo
  que finge funcionar.
- Na tela: menu → formulário → conferência → execução → árvore recarregada,
  com a coluna nova aparecendo na posição certa. E o caminho destrutivo, com
  o `DROP COLUMN` em vermelho e o "Eu entendo" barrando o botão.

**Não verificado contra PostgreSQL.** O perfil local se perdeu ao reimportar e
não tinha senha salva; o `pg_hba.conf` exige `scram-sha-256` e alterá-lo seria
mexer na configuração do servidor do usuário. A geração está coberta por 20
testes unitários, mas o **efeito** no servidor não foi observado.

### A diferença que mais custa aqui

O índice é o lugar onde os dois SGBDs mais divergem:

| | PostgreSQL | MySQL |
|---|---|---|
| A quem pertence | ao **schema** | à **tabela** |
| Criar | `CREATE INDEX ... ON t` | `ALTER TABLE t ADD INDEX` |
| Posição do `USING` | **antes** das colunas | **depois** |
| Remover | `DROP INDEX schema.ix` | `ALTER TABLE t DROP INDEX ix` |
| `CONCURRENTLY` | sim, fora de transação | não existe |

E a armadilha: remover o índice de uma chave primária ou única. O PostgreSQL
**recusa**; o MySQL **aceita e remove a constraint junto, em silêncio**. Por
isso a recusa é nossa, antes de chegar ao servidor — e é testada contra ele,
provando que a chave sobrevive.

## 5. O que falta

1. Tela de `CREATE TABLE` (o gerador já existe) — ⬜
2. View, sequence, trigger, procedure — ⬜
3. Schema/database — ⬜
4. `ALTER` de índice: renomear, trocar método — ⬜
