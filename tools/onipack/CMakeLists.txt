# Not referenced by the root CMakeLists until the .c files exist (Task 6 hookup).
# When wiring: set CMAKE_C_STANDARD 99 and -Wall -Wextra.
project(OnipackProj C)
add_executable(onipack
        onipack_format.h
        onipack_oni.c
        onipack_writer.c
        onipack_main.c)
