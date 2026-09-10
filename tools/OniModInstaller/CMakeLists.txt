# tools/OniModInstaller/CMakeLists.txt — OniMod Installer (#20)
# The droplet bundles two helper tools; onipack is declared in ../onipack.
project(OniModInstallerProj C)

add_executable(txmp_format_index ${CMAKE_SOURCE_DIR}/scripts/txmp-format-index.c)
set_target_properties(txmp_format_index PROPERTIES C_STANDARD 99 OUTPUT_NAME txmp-format-index)
target_compile_options(txmp_format_index PRIVATE -Wall -Wextra)

if(APPLE)
    # Ad-hoc signed dev build. Release signing goes through oni_app_release.
    add_custom_target(onimod_installer
        DEPENDS onipack txmp_format_index
        COMMAND ${CMAKE_SOURCE_DIR}/macos/build-installer.sh
                ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} -
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        VERBATIM USES_TERMINAL)
endif()
