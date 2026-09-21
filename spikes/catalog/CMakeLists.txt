# Valida as consultas de catalogo contra um banco real.
add_executable(spike_catalog main.cpp)
target_link_libraries(spike_catalog PRIVATE otter_flags otter::otter_db)
set_target_properties(spike_catalog PROPERTIES FOLDER "spikes")
