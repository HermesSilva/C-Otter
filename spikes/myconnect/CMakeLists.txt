# Conexao real a um MySQL: valida lib/mywire contra o servidor de verdade.
add_executable(spike_myconnect main.cpp)
target_link_libraries(spike_myconnect PRIVATE otter_flags otter::otter_mywire)
set_target_properties(spike_myconnect PROPERTIES FOLDER "spikes")
