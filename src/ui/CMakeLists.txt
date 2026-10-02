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
        icons.cpp
        main_shell.cpp
        navigator.cpp
        commands.cpp
        editor_commands.cpp
        app_commands.cpp
        app_windows.cpp
        grid_commands.cpp
        object_editor.cpp
        data_transfer.cpp
        app_settings.cpp
        window_placement.cpp
        script_store.cpp
        script_session.cpp
        platform_open.cpp
        icon_images.cpp
        hint.cpp
        icon_assets.cpp
        connection_dialog.cpp
        ddl_dialog.cpp
        sql_document.cpp
        session.cpp
        app_window_glfw.cpp
        # Os dois entram sempre: cada um se anula por #ifdef fora da sua
        # plataforma. Listar so' o "certo" por if(WIN32) foi o que deixou o
        # Linux sem AppWindow, descoberto apenas no link.
        file_dialog_win32.cpp
        file_dialog_posix.cpp
    DEPS
        otter::otter_base
        otter::otter_db
        otter::otter_sql
        imgui
        texteditor
        nanosvg
)


