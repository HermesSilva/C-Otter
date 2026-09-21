add_subdirectory(base)
add_subdirectory(sql)
add_subdirectory(db)
add_subdirectory(ui)
add_subdirectory(app)

# Camadas seguintes entram conforme as fases do plano (docs/PLAN.md):
#   add_subdirectory(db)
#   add_subdirectory(meta)
#   add_subdirectory(sql)
#   add_subdirectory(registry)
#   add_subdirectory(transfer)
