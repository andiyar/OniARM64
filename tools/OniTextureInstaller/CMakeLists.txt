# tools/OniTextureInstaller/CMakeLists.txt — Oni Texture Installer (#20, #124)
# The app bundles two helper tools; onipack is declared in ../onipack.
project(OniTextureInstallerProj C)

add_executable(txmp_format_index ${CMAKE_SOURCE_DIR}/scripts/txmp-format-index.c)
set_target_properties(txmp_format_index PROPERTIES C_STANDARD 99 OUTPUT_NAME txmp-format-index)
target_compile_options(txmp_format_index PRIVATE -Wall -Wextra)

if(APPLE)
    # Ad-hoc signed dev build. Release signing goes through oni_app_release.
    add_custom_target(oni_texture_installer
        DEPENDS onipack txmp_format_index
        COMMAND ${CMAKE_SOURCE_DIR}/macos/build-installer.sh
                ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} -
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        VERBATIM USES_TERMINAL)
    # Old target name, kept for older scripts.
    add_custom_target(onimod_installer DEPENDS oni_texture_installer)
endif()
