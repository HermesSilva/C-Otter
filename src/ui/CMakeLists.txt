set(OTTER_UI_SOURCES
    theme.cpp
    main_shell.cpp
)

if(WIN32)
    list(APPEND OTTER_UI_SOURCES app_window_win32.cpp)
else()
    # Backend GLFW/OpenGL entra com o port de Linux.
    message(WARNING "otter_ui: backend de janela ausente nesta plataforma")
endif()

# UI_LAYER marca este alvo como pertencente a camada de UI: e' o unico
# autorizado a linkar ImGui (ADR 0001 #2).
otter_add_library(otter_ui
    UI_LAYER
    SOURCES ${OTTER_UI_SOURCES}
    DEPS    otter::otter_base imgui
)
