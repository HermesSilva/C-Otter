# Protocolos de rede implementados nativamente (ADR 0009).
#
# lib/ NAO conhece src/: sao bibliotecas independentes, testaveis isoladamente.
# A unica dependencia permitida e' otter_base (Error/Result), que nao tem
# dependencia nenhuma.

# --- otter_net: socket e criptografia ----------------------------------------
set(OTTER_NET_SOURCES
    net/socket.cpp
    net/socks.cpp
    net/crypto_common.cpp
)

if(WIN32)
    list(APPEND OTTER_NET_SOURCES net/crypto_win32.cpp net/tls_win32.cpp net/sspi_win32.cpp)
else()
    list(APPEND OTTER_NET_SOURCES net/crypto_openssl.cpp net/tls_openssl.cpp net/sspi_stub.cpp)
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
    target_link_libraries(otter_net PUBLIC bcrypt crypt32 secur32 ws2_32)
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

# --- otter_tdswire: protocolo do SQL Server (TDS 7.4) ------------------------
#
# Existe porque o FreeTDS e' LGPL e o driver da Microsoft exige ODBC instalado
# (ADR 0002 e ADR 0024). Mapa do protocolo em docs/MSSQL-MAP.md.
add_library(otter_tdswire STATIC
    tdswire/value.cpp
    tdswire/browser.cpp
    tdswire/connection.cpp
    # TDS 5.0, o ramo da Sybase: SQL Anywhere (ADR 0026, docs/SQLANYWHERE-MAP.md).
    tdswire/tds5.cpp
)
add_library(otter::otter_tdswire ALIAS otter_tdswire)

target_include_directories(otter_tdswire PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(otter_tdswire PUBLIC otter_flags otter::otter_net)
set_target_properties(otter_tdswire PROPERTIES FOLDER "lib")
