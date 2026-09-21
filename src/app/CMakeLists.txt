add_executable(c-otter main.cpp)

target_link_libraries(c-otter PRIVATE otter_flags otter::otter_ui otter::otter_base)
set_target_properties(c-otter PROPERTIES FOLDER "app")

if(WIN32)
    # Aplicacao grafica, sem console atras da janela. Com GLFW o entry point
    # continua sendo main(), entao apontamos o linker para mainCRTStartup em
    # vez de esperar wWinMain (ADR 0006).
    set_target_properties(c-otter PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(c-otter PRIVATE /ENTRY:mainCRTStartup)

    # /OPT:REF e /OPT:ICF descartam o que nao e' usado -- e' o que mantem o
    # binario abaixo de 1 MB apesar dos .lib de terceiros (ver spikes/sizecheck).
    target_link_options(c-otter PRIVATE /OPT:REF /OPT:ICF)
endif()
