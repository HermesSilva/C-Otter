# Traduções

O C-Otter é **inglês por padrão** e detecta o idioma do sistema na inicialização.
Trocar em tempo real: **Help → Language**.

## Como acrescentar um idioma

Sem tocar em código: crie um arquivo `.lang` neste diretório, ao lado do executável.

```
# code: es-ES
# name: Spanish (Spain)
# native: Español (España)

Connect=Conectar
New connection=Nueva conexión
%zu rows=%zu filas
```

**Formato:** `texto em inglês=tradução`, uma por linha.
Escapes: `\n`, `\t`, `\\`, `\=`.

O texto em inglês **é a chave**. Isso significa que:

- Sem tradução para um item, a UI mostra o inglês — nunca uma chave crua como
  `dialog.connection.host`
- Um arquivo parcial funciona: traduza o que precisar, o resto fica em inglês
- Não existe passo de compilação nem geração de código

## Marcadores de formatação

Preservem a ordem e o tipo dos marcadores `%s`, `%zu`, `%d`:

```
%zu row(s), %zu column(s)=%zu linha(s), %zu coluna(s)
```

Trocar `%zu` por `%s` causa comportamento indefinido em tempo de execução.

## Identificadores de janela

Alguns textos terminam em `###AlgumId`:

```
New connection###ConnDialog=Nueva conexión###ConnDialog
```

**Mantenha o sufixo intacto.** Ele é a identidade interna da janela; só a parte
antes dele é exibida. Sem ele, o painel perde a posição no layout ao trocar de
idioma.

## Idiomas embutidos

`en` e `pt-BR` são compilados no binário (`src/base/i18n_pt_br.cpp`). Um arquivo
`.lang` com o mesmo código **substitui** o embutido — útil para corrigir uma
tradução sem recompilar.

## Cobertura

A aplicação registra todo texto exibido sem tradução. Em código:

```cpp
otter::i18n::export_template("lang/template.lang");
```

Gera um arquivo com todas as chaves vistas na sessão, prontas para traduzir.
