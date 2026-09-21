# Protocolos de rede implementados nativamente (ADR 0009).
#
# lib/ NAO conhece src/: sao bibliotecas independentes, testaveis isoladamente.
# A unica dependencia permitida e' otter_base (Error/Result), que nao tem
# dependencia nenhuma.

# --- otter_net: socket e criptografia ----------------------------------------
set(OTTER_NET_SOURCES
    net/socket.cpp
    net/crypto_common.cpp
)

if(WIN32)
    list(APPEND OTTER_NET_SOURCES net/crypto_win32.cpp)
else()
    list(APPEND OTTER_NET_SOURCES net/crypto_openssl.cpp)
endif()

add_library(otter_net STATIC ${OTTER_NET_SOURCES})
add_library(otter::otter_net ALIAS otter_net)

target_include_directories(otter_net
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src        # base/error.hpp
)
target_link_libraries(otter_net PUBLIC otter_flags otter::otter_base)
set_target_properties(otter_net PROPERTIES FOLDER "lib")

if(WIN32)
    # CNG (bcrypt) e Winsock: API do sistema, sem dependencia externa.
    target_link_libraries(otter_net PUBLIC bcrypt crypt32 ws2_32)
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(otter_net PUBLIC OpenSSL::Crypto)
endif()

# --- otter_pgwire: protocolo do PostgreSQL -----------------------------------
add_library(otter_pgwire STATIC
    pgwire/message.cpp
    pgwire/scram.cpp
    pgwire/connection.cpp
)
add_library(otter::otter_pgwire ALIAS otter_pgwire)

target_include_directories(otter_pgwire PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(otter_pgwire PUBLIC otter_flags otter::otter_net)
set_target_properties(otter_pgwire PROPERTIES FOLDER "lib")

# --- otter_mywire: protocolo do MySQL/MariaDB --------------------------------
#
# Existe porque libmysqlclient e' GPL e o ADR 0002 proibe (link estatico de
# GPL contaminaria o produto inteiro). Mapa do protocolo em docs/MYSQL-MAP.md.
add_library(otter_mywire STATIC
    mywire/packet.cpp
    mywire/auth.cpp
    mywire/connection.cpp
)
add_library(otter::otter_mywire ALIAS otter_mywire)

target_include_directories(otter_mywire PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(otter_mywire PUBLIC otter_flags otter::otter_net)
set_target_properties(otter_mywire PROPERTIES FOLDER "lib")

# Fase 3:
#   add_library(otter_tds ...)      # SQL Server
