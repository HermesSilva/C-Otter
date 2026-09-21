# Conexao real a um PostgreSQL: valida otter_db de ponta a ponta.
add_executable(spike_pgconnect main.cpp)
target_link_libraries(spike_pgconnect PRIVATE otter_flags otter::otter_db)
set_target_properties(spike_pgconnect PROPERTIES FOLDER "spikes")
