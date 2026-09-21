# Backend unico para todas as plataformas (ADR 0006): GLFW + OpenGL 3.3.
# Nao ha' ramificacao por sistema operacional aqui -- essa era a fonte do furo
# em que o Linux compilava sem implementacao de AppWindow e falhava no link.
#
# UI_LAYER marca este alvo como pertencente a camada de UI: e' o unico
# autorizado a linkar ImGui (ADR 0001 #2).
otter_add_library(otter_ui
    UI_LAYER
    SOURCES
        theme.cpp
        main_shell.cpp
        connection_dialog.cpp
        sql_document.cpp
        session.cpp
        app_window_glfw.cpp
    DEPS
        otter::otter_base
        otter::otter_db
        otter::otter_sql
        imgui
        texteditor
)

