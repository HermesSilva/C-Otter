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
