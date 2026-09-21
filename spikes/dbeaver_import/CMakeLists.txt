# Le' o workspace real do DBeaver. Ver ADR 0012.
add_executable(spike_dbeaver_import main.cpp)

target_link_libraries(spike_dbeaver_import PRIVATE
    otter_flags
    otter::otter_base
    otter::otter_db
)
set_target_properties(spike_dbeaver_import PROPERTIES FOLDER "spikes")
