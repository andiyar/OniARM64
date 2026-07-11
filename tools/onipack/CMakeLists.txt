project(OnipackProj C)
add_executable(onipack
        onipack_format.h
        onipack_oni.c
        onipack_writer.c
        onipack_main.c)
set_target_properties(onipack PROPERTIES C_STANDARD 99)
target_compile_options(onipack PRIVATE -Wall -Wextra)
