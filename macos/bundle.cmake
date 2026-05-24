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

# Signing identity for the oni_app_release target. Set via:
#   cmake .. -DONI_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
# Discover yours: security find-identity -v -p codesigning
set(ONI_SIGN_IDENTITY "" CACHE STRING
    "Codesign identity for oni_app_release. Empty = release target errors with setup instructions.")

# Release target: signed + notarized + stapled OniARM64.dmg ready for GitHub Releases.
# Chains build-bundle.sh (signing) -> notarize-bundle.sh (.app notarization)
# -> package-dmg.sh (DMG creation + sign + notarize + staple).
#
# Takes ~7 min: ~5s assembly + ~3min .app notarization + ~2min DMG notarization
# (Apple's notary service dominates the wall clock).
#
# One-time setup before first invocation:
#   brew install create-dmg
#   xcrun notarytool store-credentials oniarm64-notarize ...
# See README "Building a distributable release" section.
if(ONI_SIGN_IDENTITY STREQUAL "")
    add_custom_target(oni_app_release
        COMMAND ${CMAKE_COMMAND} -E echo
            "ERROR: ONI_SIGN_IDENTITY not set. Reconfigure with:"
        COMMAND ${CMAKE_COMMAND} -E echo
            "  cmake .. -DONI_SIGN_IDENTITY=\"Developer ID Application: Your Name (TEAMID)\""
        COMMAND ${CMAKE_COMMAND} -E echo
            "See README 'Building a distributable release' for full setup."
        COMMAND ${CMAKE_COMMAND} -E false
        COMMENT "oni_app_release: ONI_SIGN_IDENTITY not configured"
    )
else()
    add_custom_target(oni_app_release
        DEPENDS Oni
        COMMAND ${CMAKE_COMMAND} -E echo "Assembling signed OniARM64.app..."
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/macos/build-bundle.sh
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_BINARY_DIR}
                ${ONI_SIGN_IDENTITY}
        COMMAND ${CMAKE_COMMAND} -E echo "Notarizing OniARM64.app..."
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/macos/notarize-bundle.sh
                ${CMAKE_BINARY_DIR}
        COMMAND ${CMAKE_COMMAND} -E echo "Packaging OniARM64.dmg..."
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/macos/package-dmg.sh
                ${CMAKE_BINARY_DIR}
                ${ONI_SIGN_IDENTITY}
        COMMAND ${CMAKE_COMMAND} -E echo
                "Done: ${CMAKE_BINARY_DIR}/OniARM64.dmg"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM
        USES_TERMINAL
    )
endif()
