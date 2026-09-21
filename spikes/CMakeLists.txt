# Spikes da Fase 0 -- existem para matar incerteza antes de construir sobre ela.
# Ver docs/PLAN.md secao 3.
#
# sizecheck: removido junto com o Scintilla (ADR 0007). Ele provou que o custo
# real de um .lib no binario final e' muito menor que o tamanho do .lib -- licao
# que vale para qualquer dependencia futura.
#
#   add_subdirectory(grid)     # 1M x 20 celulas a 144 fps
#   add_subdirectory(libpq)    # fetch binario colunar
