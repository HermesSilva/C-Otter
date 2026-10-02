# Conexao real a um Oracle: valida lib/orawire contra o servidor de verdade.
add_executable(spike_oraconnect main.cpp)
target_link_libraries(spike_oraconnect PRIVATE otter_flags otter::otter_orawire)
set_target_properties(spike_oraconnect PROPERTIES FOLDER "spikes")

# O driver e o catalogo, pelo mesmo caminho da interface (CatalogReader), num
# schema de rascunho criado e apagado pelo proprio spike.
add_executable(spike_oracatalog catalog.cpp)
target_link_libraries(spike_oracatalog PRIVATE otter_flags otter::otter_db)
set_target_properties(spike_oracatalog PROPERTIES FOLDER "spikes")
