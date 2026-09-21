otter_add_library(otter_base
    SOURCES
        aes.cpp
        error.cpp
        arena.cpp
        i18n.cpp
        i18n_pt_br.cpp
        json.cpp
)

# bcrypt: BCryptGenRandom, para o IV do AES (base/aes.cpp).
if(WIN32)
    target_link_libraries(otter_base PRIVATE bcrypt)
endif()
