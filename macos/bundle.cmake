# Declares the oni_app target which assembles OniARM64.app from a freshly-built
# Oni binary plus committed bundle templates and assets.
#
# Usage from the command line:
#   cmake --build . --target oni_app
#   (or: make oni_app   inside the build dir)
#
# Output: ${CMAKE_BINARY_DIR}/bin/OniARM64.app/

if(NOT APPLE)
    message(WARNING "bundle.cmake: oni_app target only available on APPLE platforms")
    return()
endif()

add_custom_target(oni_app
    DEPENDS Oni
    COMMAND ${CMAKE_COMMAND} -E echo "Assembling OniARM64.app..."
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/macos/build-bundle.sh
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E echo "Done: ${CMAKE_BINARY_DIR}/bin/OniARM64.app/"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    VERBATIM
    USES_TERMINAL
)
