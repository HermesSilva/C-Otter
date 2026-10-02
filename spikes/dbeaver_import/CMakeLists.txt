# Le' o workspace real do DBeaver. Ver ADR 0012.
add_executable(spike_dbeaver_import main.cpp)

target_link_libraries(spike_dbeaver_import PRIVATE
    otter_flags
    otter::otter_base
    otter::otter_db
)
set_target_properties(spike_dbeaver_import PROPERTIES FOLDER "spikes")

# Le' o armazenamento do PROPRIO C-Otter: separa "senha nao gravada" de
# "senha nao lida" quando a conexao falha com (using password: NO).
add_executable(spike_check_store check_store.cpp)

target_link_libraries(spike_check_store PRIVATE
    otter_flags
    otter::otter_base
    otter::otter_db
)
set_target_properties(spike_check_store PROPERTIES FOLDER "spikes")

# Consulta rapida pelo perfil salvo: confere no BANCO o que a tela mostrou,
# sem senha em linha de comando.
add_executable(spike_qq qq.cpp)
target_link_libraries(spike_qq PRIVATE otter_flags otter::otter_base otter::otter_db)
set_target_properties(spike_qq PROPERTIES FOLDER "spikes")

# Aplica um ALTER gerado contra o MySQL real: o teste unitario compara
# strings, este confere o EFEITO no servidor.
add_executable(spike_alter_live alter_live.cpp)
target_link_libraries(spike_alter_live PRIVATE otter_flags otter::otter_base otter::otter_db)
set_target_properties(spike_alter_live PROPERTIES FOLDER "spikes")

# O mesmo para o PostgreSQL, mais a gravacao da grade, o EXPLAIN ANALYZE e os
# savepoints: tudo o que gera SQL e so' o servidor confirma.
add_executable(spike_alter_live_pg alter_live_pg.cpp)
target_link_libraries(spike_alter_live_pg PRIVATE otter_flags otter::otter_base otter::otter_db)
set_target_properties(spike_alter_live_pg PROPERTIES FOLDER "spikes")

# Verificacao do TLS contra servidor de verdade: o aperto de mao nao e'
# testavel em unidade (ver o cabecalho de tls_live.cpp).
add_executable(spike_tls_live tls_live.cpp)
target_link_libraries(spike_tls_live PRIVATE otter_flags otter::otter_base otter::otter_db)
set_target_properties(spike_tls_live PROPERTIES FOLDER "spikes")

# O proxy SOCKS5 contra um proxy de verdade (tools/socks5_test_server.py).
add_executable(spike_proxy_live proxy_live.cpp)
target_link_libraries(spike_proxy_live PRIVATE otter_flags otter::otter_base otter::otter_db)
set_target_properties(spike_proxy_live PROPERTIES FOLDER "spikes")

# O tunel SSH com o cliente `ssh` do sistema (ADR 0021).
add_executable(spike_ssh_live ssh_live.cpp)
target_link_libraries(spike_ssh_live PRIVATE otter_flags otter::otter_base otter::otter_db)
set_target_properties(spike_ssh_live PROPERTIES FOLDER "spikes")

# Qual driver cada perfil usaria, sem conectar nem pedir senha.
add_executable(spike_external_import external_import.cpp)
target_link_libraries(spike_external_import PRIVATE otter_flags otter::otter_base otter::otter_db)
add_executable(spike_tds_live tds_live.cpp)
target_link_libraries(spike_tds_live PRIVATE otter_flags otter::otter_base otter::otter_tdswire)
# O TDS 5.0 contra um SQL Anywhere de verdade (senha em OTTER_SA_PASSWORD).
add_executable(spike_tds5_live tds5_live.cpp)
target_link_libraries(spike_tds5_live PRIVATE otter_flags otter::otter_base otter::otter_tdswire)
set_target_properties(spike_tds5_live PROPERTIES FOLDER "spikes")
# Consulta pelo DRIVER do SQL Anywhere: o que a grade recebe.
add_executable(spike_sa_qq sa_qq.cpp)
target_link_libraries(spike_sa_qq PRIVATE otter_flags otter::otter_base otter::otter_db)
set_target_properties(spike_sa_qq PROPERTIES FOLDER "spikes")
add_executable(spike_which_driver which_driver.cpp)
target_link_libraries(spike_which_driver PRIVATE otter_flags otter::otter_base otter::otter_db)
set_target_properties(spike_which_driver PROPERTIES FOLDER "spikes")
