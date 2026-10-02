# Lexer, dialetos, splitter e analise de escopo.
#
# Fonte unica de verdade para realce, execucao e completion: nunca dois lexers
# divergentes (ADR 0007).
otter_add_library(otter_sql
    SOURCES
        dialect.cpp
        editing.cpp
        format.cpp
        lexer.cpp
        paging.cpp
        script.cpp
    DEPS
        otter::otter_base
)
