# Mede o custo real do Scintilla no executavel final, apos descarte do linker.
add_executable(spike_sizecheck main.cpp)
target_link_libraries(spike_sizecheck PRIVATE otter_flags scintilla lexilla)
set_target_properties(spike_sizecheck PROPERTIES FOLDER "spikes")

if(MSVC)
    # /OPT:REF e /OPT:ICF descartam funcoes e dobram identicas -- e' o que torna
    # o binario final muito menor que a soma dos .lib.
    target_link_options(spike_sizecheck PRIVATE /OPT:REF /OPT:ICF)
endif()
