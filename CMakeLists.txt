cmake_minimum_required(VERSION 3.26.0 FATAL_ERROR)

set(CMAKE_SYSTEM_VERSION 10.0 CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD 20 CACHE STRING "The C++ standard to use")
set(CMAKE_C_STANDARD 23 CACHE STRING "The C standard to use")

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")

project(Ship VERSION 9.2.3 LANGUAGES C CXX)
include(CMake/soh-cvars.cmake)
include(CMake/lus-cvars.cmake)
set(SPDLOG_LEVEL_TRACE 0)
set(SPDLOG_LEVEL_OFF 6)
set(SPDLOG_MIN_CUTOFF SPDLOG_LEVEL_TRACE CACHE STRING "cutoff at trace")

option(SUPPRESS_WARNINGS "Suppress warnings in LUS and src (decomp)" ON)
if(SUPPRESS_WARNINGS)
    MESSAGE("Suppressing warnings in LUS and src")
    if(MSVC)
        set(WARNING_OVERRIDE /w)
    else()
        set(WARNING_OVERRIDE -w)
    endif()
else()
    MESSAGE("Skipping warning suppression")
endif()

set(NATO_PHONETIC_ALPHABET
  "Alfa" "Bravo" "Charlie" "Delta" "Echo" "Foxtrot" "Golf" "Hotel"
  "India" "Juliett" "Kilo" "Lima" "Mike" "November" "Oscar" "Papa"
  "Quebec" "Romeo" "Sierra" "Tango" "Uniform" "Victor" "Whiskey"
  "Xray" "Yankee" "Zulu"
)

# Get the patch version number from the project version
math(EXPR PATCH_INDEX "${PROJECT_VERSION_PATCH}")

# Use the patch number to select the correct word
list(GET NATO_PHONETIC_ALPHABET ${PATCH_INDEX} PROJECT_PATCH_WORD)

set(PROJECT_BUILD_NAME "Ackbar ${PROJECT_PATCH_WORD}" CACHE STRING "" FORCE)
set(PROJECT_TEAM "github.com/harbourmasters" CACHE STRING "" FORCE)

execute_process(
    COMMAND git branch --show-current
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(CMAKE_PROJECT_GIT_BRANCH "${GIT_BRANCH}" CACHE STRING "Git branch" FORCE)

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get only the first 7 characters of the hash
string(SUBSTRING "${GIT_COMMIT_HASH}" 0 7 SHORT_COMMIT_HASH)

set(CMAKE_PROJECT_GIT_COMMIT_HASH "${SHORT_COMMIT_HASH}" CACHE STRING "Git commit hash" FORCE)

execute_process(
    COMMAND git describe --tags --abbrev=0 --exact-match HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT GIT_COMMIT_TAG)
    set(GIT_COMMIT_TAG "" CACHE STRING "Git commit tag" FORCE)
endif()

set(CMAKE_PROJECT_GIT_COMMIT_TAG "${GIT_COMMIT_TAG}" CACHE STRING "Git commit tag" FORCE)

set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT soh)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/MP>)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    include(CMake/automate-vcpkg.cmake)

    set(VCPKG_TRIPLET x64-windows-static)
    set(VCPKG_TARGET_TRIPLET x64-windows-static)

    vcpkg_bootstrap()
    vcpkg_install_packages(zlib bzip2 libzip libpng sdl2 sdl2-net glew glfw3 nlohmann-json tinyxml2 spdlog libogg libvorbis opus opusfile)
    if (CMAKE_C_COMPILER_LAUNCHER MATCHES "ccache|sccache")
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
    endif()
endif()

################################################################################
# Set target arch type if empty. Visual studio solution generator provides it.
################################################################################
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(NOT CMAKE_VS_PLATFORM_NAME)
        set(CMAKE_VS_PLATFORM_NAME "x64")
    endif()
    message("${CMAKE_VS_PLATFORM_NAME} architecture in use")

    if(NOT ("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64"
        OR "${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32"))
        message(FATAL_ERROR "${CMAKE_VS_PLATFORM_NAME} arch is not supported!")
    endif()
endif()

################################################################################
# Global configuration types
################################################################################
if (CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
set(CMAKE_C_FLAGS_DEBUG "-g -ffast-math -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -ffast-math -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -ffast-math -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -ffast-math -DNDEBUG")
else()
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_OBJCXX_FLAGS_RELEASE "-O2 -DNDEBUG")
endif()

if(NOT CMAKE_BUILD_TYPE )
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
endif()

################################################################################
# Common utils
################################################################################
include(CMake/Utils.cmake)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    get_linux_lsb_release_information()
    message(STATUS "Linux ${LSB_RELEASE_ID_SHORT} ${LSB_RELEASE_VERSION_SHORT} ${LSB_RELEASE_CODENAME_SHORT}")
else()
    message(STATUS ${CMAKE_SYSTEM_NAME})
endif()

################################################################################
# Additional Global Settings(add specific info there)
################################################################################
include(CMake/GlobalSettingsInclude.cmake OPTIONAL)

################################################################################
# Use solution folders feature
################################################################################
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

################################################################################
# Set LUS vars
################################################################################

# Enable the Gfx debugger in LUS to use libgfxd from ZAPDTR
set(GFX_DEBUG_DISASSEMBLER ON)

# Tell LUS we're using F3DEX_GBI_2 (in a way that doesn't break libgfxd)
set(GBI_UCODE F3DEX_GBI_2)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")
include(CTest)

# Enable MPQ and OTR support
set(INCLUDE_MPQ_SUPPORT ON)

################################################################################
# Set CONTROLLERBUTTONS_T
################################################################################
add_compile_definitions(CONTROLLERBUTTONS_T=uint32_t)

################################################################################
# Sub-projects
################################################################################
add_subdirectory(libultraship ${CMAKE_BINARY_DIR}/libultraship)
target_compile_options(libultraship PRIVATE "${WARNING_OVERRIDE}")
target_compile_definitions(libultraship PUBLIC INCLUDE_MPQ_SUPPORT)

# RT64 is built as a static library and expects to receive an SDL-created
# Vulkan window on Linux. Keep it in the build graph next to LUS so consumers
# of libultraship inherit RT64's headers and link dependency.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CMake/rt64-linux.cmake)
endif()

# Dense cartridge water also exercises the interpreter in Debug builds.
# Keep symbols and strict floating-point behavior while optimizing only the
# display-list hot paths; the rest of the game remains normally debuggable.
if(TARGET rt64 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/rt64/src/hle/rt64_rsp.cpp"
        "${CMAKE_SOURCE_DIR}/rt64/src/hle/rt64_interpreter.cpp"
        "${CMAKE_SOURCE_DIR}/rt64/src/gbi/rt64_gbi_f3dex2.cpp"
        TARGET_DIRECTORY rt64
        PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>;$<$<CONFIG:Debug>:-fno-fast-math>;$<$<CONFIG:Debug>:-ffp-contract=off>")
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/libultraship/src/fast/rt64/DisplayListTranslator.cpp"
        TARGET_DIRECTORY libultraship
        PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>;$<$<CONFIG:Debug>:-fno-fast-math>;$<$<CONFIG:Debug>:-ffp-contract=off>")
endif()

add_subdirectory(ZAPDTR/ZAPD ${CMAKE_BINARY_DIR}/ZAPD)
add_subdirectory(OTRExporter)
add_subdirectory(soh)

# Cartridge formulas depend on the original float operation boundaries and
# finite-input checks. Release's global fast-math must not rewrite this port.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/soh/soh/Enhancements/Graphics/LakeWater/WaveField.cpp"
        "${CMAKE_SOURCE_DIR}/soh/soh/Enhancements/Graphics/LakeWater/CartridgeWaveSource.cpp"
        "${CMAKE_SOURCE_DIR}/soh/soh/Enhancements/Graphics/LakeWater/CartridgeWaterRenderer.cpp"
        "${CMAKE_SOURCE_DIR}/soh/soh/Enhancements/Graphics/LakeWater/LakeDomain.cpp"
        "${CMAKE_SOURCE_DIR}/soh/soh/Enhancements/Graphics/LakeWater/ShoreProfile.cpp"
        "${CMAKE_SOURCE_DIR}/soh/soh/Enhancements/Graphics/LakeWater/LakeWaterSimulation.cpp"
        TARGET_DIRECTORY soh
        PROPERTIES COMPILE_OPTIONS "-O2;-fno-fast-math;-ffp-contract=off")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    get_target_property(_soh_compile_definitions soh COMPILE_DEFINITIONS)
    if(_soh_compile_definitions)
        list(REMOVE_ITEM _soh_compile_definitions ENABLE_OPENGL)
        set_property(TARGET soh PROPERTY COMPILE_DEFINITIONS "${_soh_compile_definitions}")
    endif()
endif()

set_property(TARGET soh PROPERTY APPIMAGE_DESKTOP_FILE_TERMINAL YES)
set_property(TARGET soh PROPERTY APPIMAGE_DESKTOP_FILE "${CMAKE_SOURCE_DIR}/scripts/linux/appimage/soh.desktop")
set_property(TARGET soh PROPERTY APPIMAGE_ICON_FILE "${CMAKE_BINARY_DIR}/sohIcon.png")

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
install(FILES "${CMAKE_BINARY_DIR}/soh/soh.o2r" DESTINATION . COMPONENT ship)
install(TARGETS ZAPD DESTINATION ./assets/extractor COMPONENT extractor)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/soh/assets/extractor/" DESTINATION ./assets COMPONENT extractor)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/soh/assets/xml/" DESTINATION ./assets/xml COMPONENT extractor)
endif()

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/soh/assets/extractor/" DESTINATION ./assets/ COMPONENT ship)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/soh/assets/xml/" DESTINATION ./assets/xml COMPONENT ship)
endif()

find_package(Python3 COMPONENTS Interpreter)

# Cartridge data is generated locally from the user's ROM, never source assets.
set(WR64_WATER_ROM "" CACHE FILEPATH "Wave Race 64 USA Rev A big-endian ROM for original water extraction")
set(WR64_WATER_ASSET_DIR "${CMAKE_SOURCE_DIR}/build/cartridge-water" CACHE PATH
    "Local generated Wave Race cartridge water cache")
set(_wr64_water_texture "${WR64_WATER_ASSET_DIR}/drake_lake_water_32x32.rgba")
set(_wr64_sunny_texture "${WR64_WATER_ASSET_DIR}/sunny_beach_water_32x32.rgba")
set(_wr64_water_manifest "${WR64_WATER_ASSET_DIR}/water_manifest.json")
set(_wr64_sunny_body_dl "${WR64_WATER_ASSET_DIR}/sunny_beach_body.gfx.bin")
set(_wr64_sunny_body_vertices "${WR64_WATER_ASSET_DIR}/sunny_beach_body.vtx.bin")
set(_wr64_sunny_source "${WR64_WATER_ASSET_DIR}/sunny_beach_wave_sources.bin")
set(_wr64_wave_source "${WR64_WATER_ASSET_DIR}/drake_lake_wave_source.bin")
if(WR64_WATER_ROM)
    if(NOT Python3_Interpreter_FOUND)
        message(FATAL_ERROR "WR64_WATER_ROM requires Python 3 for local asset extraction")
    endif()
    add_custom_command(OUTPUT "${_wr64_sunny_body_dl}" "${_wr64_sunny_body_vertices}" "${_wr64_sunny_source}" "${_wr64_sunny_texture}" "${_wr64_water_texture}" "${_wr64_water_manifest}" "${_wr64_wave_source}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/extract_wr64_cartridge_water.py"
            "${WR64_WATER_ROM}" --out "${WR64_WATER_ASSET_DIR}"
        DEPENDS "${WR64_WATER_ROM}" "${CMAKE_SOURCE_DIR}/tools/extract_wr64_cartridge_water.py"
            "${CMAKE_SOURCE_DIR}/tools/verify_wr64_water.py"
        COMMENT "Extracting original Wave Race water assets from local ROM"
        VERBATIM)
endif()
if(WR64_WATER_ROM OR (EXISTS "${_wr64_sunny_source}" AND EXISTS "${_wr64_sunny_texture}" AND EXISTS "${_wr64_water_texture}" AND EXISTS "${_wr64_water_manifest}" AND EXISTS "${_wr64_wave_source}"))
    add_custom_target(StageWaveRaceWater
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:soh>/cartridge-water"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_wr64_sunny_source}"
            "$<TARGET_FILE_DIR:soh>/cartridge-water/sunny_beach_wave_sources.bin"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_wr64_sunny_texture}"
            "$<TARGET_FILE_DIR:soh>/cartridge-water/sunny_beach_water_32x32.rgba"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_wr64_water_texture}"
            "$<TARGET_FILE_DIR:soh>/cartridge-water/drake_lake_water_32x32.rgba"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_wr64_water_manifest}"
            "$<TARGET_FILE_DIR:soh>/cartridge-water/water_manifest.json"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_wr64_wave_source}"
            "$<TARGET_FILE_DIR:soh>/cartridge-water/drake_lake_wave_source.bin"
        DEPENDS "${_wr64_sunny_source}" "${_wr64_sunny_texture}" "${_wr64_water_texture}" "${_wr64_water_manifest}" "${_wr64_wave_source}"
        VERBATIM)
    add_dependencies(soh StageWaveRaceWater)
endif()

if(BUILD_TESTING AND TARGET rt64_wave_field_trace AND WR64_WATER_ROM AND Python3_Interpreter_FOUND)
    add_test(NAME rt64_cartridge_water_parity COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/verify_wr64_water.py" "${WR64_WATER_ROM}"
        --port $<TARGET_FILE:rt64_wave_field_trace>
        --out "${CMAKE_BINARY_DIR}/cartridge-water/reference.json")
    set_tests_properties(rt64_cartridge_water_parity PROPERTIES TIMEOUT 240)
endif()

# Target to generate OTRs
add_custom_target(
    ExtractAssets
    COMMAND ${CMAKE_COMMAND} -E rm -f oot.o2r oot-mq.o2r soh.o2r

    # copy LUS default shaders into assets/custom
    COMMAND ${CMAKE_COMMAND} -E rm -r -f ${CMAKE_CURRENT_SOURCE_DIR}/soh/assets/custom/shaders/
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/src/fast/shaders/ ${CMAKE_CURRENT_SOURCE_DIR}/soh/assets/custom/shaders/

    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/OTRExporter/extract_assets.py -z "$<TARGET_FILE:ZAPD>" --non-interactive --xml-root assets/xml --custom-otr-file soh.o2r "--custom-assets-path" ${CMAKE_CURRENT_SOURCE_DIR}/soh/assets/custom --port-ver "${CMAKE_PROJECT_VERSION}"
    COMMAND ${CMAKE_COMMAND} -DSYSTEM_NAME=${CMAKE_SYSTEM_NAME} -DTARGET_DIR="$<TARGET_FILE_DIR:ZAPD>" -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DBINARY_DIR=${CMAKE_BINARY_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/copy-existing-otrs.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/soh
    COMMENT "Running asset extraction..."
    DEPENDS ZAPD
    BYPRODUCTS oot.o2r ${CMAKE_SOURCE_DIR}/oot.o2r oot-mq.o2r ${CMAKE_SOURCE_DIR}/oot-mq.o2r ${CMAKE_SOURCE_DIR}/soh.o2r
)

# Target to generate headers
add_custom_target(
    ExtractAssetHeaders
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/OTRExporter/extract_assets.py -z "$<TARGET_FILE:ZAPD>" --non-interactive --xml-root assets/xml --gen-headers
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/soh
    COMMENT "Generating asset headers..."
    DEPENDS ZAPD
)

# Target to generate only soh.o2r
add_custom_target(
    GenerateSohOtr
    COMMAND ${CMAKE_COMMAND} -E rm -f soh.o2r

    # copy LUS default shaders into assets/custom
    COMMAND ${CMAKE_COMMAND} -E rm -r -f ${CMAKE_CURRENT_SOURCE_DIR}/soh/assets/custom/shaders/
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/src/fast/shaders/ ${CMAKE_CURRENT_SOURCE_DIR}/soh/assets/custom/shaders/

    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/OTRExporter/extract_assets.py -z "$<TARGET_FILE:ZAPD>" --norom --custom-otr-file soh.o2r "--custom-assets-path" ${CMAKE_CURRENT_SOURCE_DIR}/soh/assets/custom --port-ver "${CMAKE_PROJECT_VERSION}"
    COMMAND ${CMAKE_COMMAND} -DSYSTEM_NAME=${CMAKE_SYSTEM_NAME} -DTARGET_DIR="$<TARGET_FILE_DIR:ZAPD>" -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DBINARY_DIR=${CMAKE_BINARY_DIR} -DONLYSOHOTR=On -P ${CMAKE_CURRENT_SOURCE_DIR}/copy-existing-otrs.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/soh
    COMMENT "Generating soh.o2r..."
    DEPENDS ZAPD
)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(ImageMagick COMPONENTS convert)
    if (ImageMagick_FOUND)
        execute_process (
            COMMAND ${ImageMagick_convert_EXECUTABLE} ${CMAKE_SOURCE_DIR}/soh/macosx/sohIcon.png -resize 512x512 ${CMAKE_BINARY_DIR}/sohIcon.png
            OUTPUT_VARIABLE outVar
        )
    endif()
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
add_custom_target(CreateOSXIcons
    COMMAND mkdir -p ${CMAKE_BINARY_DIR}/macosx/soh.iconset
    COMMAND sips -z 16 16     ${CMAKE_SOURCE_DIR}/soh/macosx/sohIcon.png --out ${CMAKE_BINARY_DIR}/macosx/soh.iconset/icon_16x16.png
    COMMAND sips -z 32 32     ${CMAKE_SOURCE_DIR}/soh/macosx/sohIcon.png --out ${CMAKE_BINARY_DIR}/macosx/soh.iconset/icon_16x16@2x.png
    COMMAND sips -z 32 32     ${CMAKE_SOURCE_DIR}/soh/macosx/sohIcon.png --out ${CMAKE_BINARY_DIR}/macosx/soh.iconset/icon_32x32.png
    COMMAND sips -z 64 64     ${CMAKE_SOURCE_DIR}/soh/macosx/sohIcon.png --out ${CMAKE_BINARY_DIR}/macosx/soh.iconset/icon_32x32@2x.png
    COMMAND sips -z 128 128   ${CMAKE_SOURCE_DIR}/soh/macosx/sohIcon.png --out ${CMAKE_BINARY_DIR}/macosx/soh.iconset/icon_128x128.png
    COMMAND sips -z 256 256   ${CMAKE_SOURCE_DIR}/soh/macosx/sohIcon.png --out ${CMAKE_BINARY_DIR}/macosx/soh.iconset/icon_128x128@2x.png
    COMMAND sips -z 256 256   ${CMAKE_SOURCE_DIR}/soh/macosx/sohIcon.png --out ${CMAKE_BINARY_DIR}/macosx/soh.iconset/icon_256x256.png
    COMMAND sips -z 512 512   ${CMAKE_SOURCE_DIR}/soh/macosx/sohIcon.png --out ${CMAKE_BINARY_DIR}/macosx/soh.iconset/icon_256x256@2x.png
    COMMAND sips -z 512 512   ${CMAKE_SOURCE_DIR}/soh/macosx/sohIcon.png --out ${CMAKE_BINARY_DIR}/macosx/soh.iconset/icon_512x512.png
    COMMAND cp                ${CMAKE_SOURCE_DIR}/soh/macosx/sohIcon.png ${CMAKE_BINARY_DIR}/macosx/soh.iconset/icon_512x512@2x.png
    COMMAND iconutil -c icns -o ${CMAKE_BINARY_DIR}/macosx/soh.icns ${CMAKE_BINARY_DIR}/macosx/soh.iconset
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Creating OSX icons ..."
    )
add_dependencies(soh CreateOSXIcons)

install(TARGETS ZAPD DESTINATION ${CMAKE_BINARY_DIR}/assets)

set(PROGRAM_PERMISSIONS_EXECUTE OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/soh/assets/extractor/" DESTINATION ./assets/)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/soh/assets/xml/" DESTINATION ./assets/xml)

# Rename the installed soh binary to drop the macos suffix
INSTALL(CODE "FILE(RENAME \${CMAKE_INSTALL_PREFIX}/../MacOS/soh-macos \${CMAKE_INSTALL_PREFIX}/../MacOS/soh)")

install(CODE "
    include(BundleUtilities)
    fixup_bundle(\"\${CMAKE_INSTALL_PREFIX}/../MacOS/soh\" \"\" \"${dirs}\")
    ")

endif()

if(CMAKE_SYSTEM_NAME MATCHES "Windows|NintendoSwitch|CafeOS")
install(FILES ${CMAKE_SOURCE_DIR}/README.md DESTINATION . COMPONENT ship RENAME readme.txt )
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(CPACK_GENERATOR "External")
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows|NintendoSwitch|CafeOS")
    set(CPACK_GENERATOR "ZIP")
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(CPACK_GENERATOR "Bundle")
endif()

set(CPACK_PROJECT_CONFIG_FILE ${CMAKE_SOURCE_DIR}/CMake/Packaging-2.cmake)
include(CMake/Packaging.cmake)
