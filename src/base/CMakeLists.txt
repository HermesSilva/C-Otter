otter_add_library(otter_base
    SOURCES
        aes.cpp
        error.cpp
        arena.cpp
        i18n.cpp
        i18n_pt_br.cpp
        json.cpp
        os_secret.cpp
        paths.cpp
        process.cpp
        version.cpp
)

# So' no version.cpp, e nao no alvo: o numero muda a cada entrega, e uma
# definicao no alvo recompilaria a biblioteca inteira por causa dele.
set_source_files_properties(version.cpp PROPERTIES COMPILE_DEFINITIONS
    "OTTER_VERSION=\"${PROJECT_VERSION}\";OTTER_BUILD_NUMBER=${OTTER_BUILD_NUMBER}")

# bcrypt: BCryptGenRandom, para o IV do AES (base/aes.cpp).
# advapi32: CredReadW, para ler a chave do pgAdmin (base/os_secret.cpp).
if(WIN32)
    target_link_libraries(otter_base PRIVATE bcrypt advapi32)
endif()
