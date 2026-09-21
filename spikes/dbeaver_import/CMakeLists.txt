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

# Verificacao do TLS contra servidor de verdade: o aperto de mao nao e'
# testavel em unidade (ver o cabecalho de tls_live.cpp).
add_executable(spike_tls_live tls_live.cpp)
target_link_libraries(spike_tls_live PRIVATE otter_flags otter::otter_base otter::otter_db)
set_target_properties(spike_tls_live PROPERTIES FOLDER "spikes")

# Qual driver cada perfil usaria, sem conectar nem pedir senha.
add_executable(spike_which_driver which_driver.cpp)
target_link_libraries(spike_which_driver PRIVATE otter_flags otter::otter_base otter::otter_db)
set_target_properties(spike_which_driver PROPERTIES FOLDER "spikes")
