if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "RT64 renderer smoke test is currently enabled on Linux only")
    return()
endif()

add_executable(rt64_renderer_smoke renderer_smoke.cpp)
target_include_directories(rt64_renderer_smoke PRIVATE "${CMAKE_SOURCE_DIR}/soh")
target_link_libraries(rt64_renderer_smoke PRIVATE libultraship)
target_compile_definitions(rt64_renderer_smoke PRIVATE F3DEX_GBI_2)

add_executable(rt64_translator_test translator_test.cpp)
target_link_libraries(rt64_translator_test PRIVATE libultraship)
target_compile_definitions(rt64_translator_test PRIVATE F3DEX_GBI_2)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Keep static data below 4 GiB so tests exercise native-pointer detection
    # when a uintptr_t value also fits in the legacy segmented-address field.
    target_link_options(rt64_translator_test PRIVATE -no-pie)
endif()

add_executable(rt64_texture_patches_test texture_patches_test.cpp)
target_link_libraries(rt64_texture_patches_test PRIVATE libultraship)

add_executable(rt64_wave_field_test wave_field_test.cpp
    ../../soh/soh/Enhancements/Graphics/LakeWater/WaveField.cpp)
add_executable(rt64_wave_field_trace wave_field_trace.cpp
    ../../soh/soh/Enhancements/Graphics/LakeWater/WaveField.cpp
    ../../soh/soh/Enhancements/Graphics/LakeWater/CartridgeWaveSource.cpp)
add_executable(rt64_lake_domain_test lake_domain_test.cpp
    ../../soh/soh/Enhancements/Graphics/LakeWater/LakeDomain.cpp)
add_executable(rt64_water_mesh_test water_mesh_test.cpp)
add_executable(rt64_shore_profile_test shore_profile_test.cpp
    ../../soh/soh/Enhancements/Graphics/LakeWater/ShoreProfile.cpp)
# Assertions are the test oracle, including in the release build preset.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rt64_lake_domain_test PRIVATE -O2 -UNDEBUG -fno-fast-math -ffp-contract=off)
    target_compile_options(rt64_water_mesh_test PRIVATE -O2 -UNDEBUG)
    target_compile_options(rt64_shore_profile_test PRIVATE -O2 -UNDEBUG -fno-fast-math -ffp-contract=off)
    target_compile_options(rt64_wave_field_test PRIVATE -O2 -UNDEBUG -fno-fast-math -ffp-contract=off)
    target_compile_options(rt64_wave_field_trace PRIVATE -O2 -fno-fast-math -ffp-contract=off)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME rt64_renderer_smoke COMMAND rt64_renderer_smoke)
    set_tests_properties(rt64_renderer_smoke PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME rt64_translator_test COMMAND rt64_translator_test)
    add_test(NAME rt64_texture_patches_test COMMAND rt64_texture_patches_test)
    add_test(NAME rt64_lake_domain_test COMMAND rt64_lake_domain_test)
    add_test(NAME rt64_water_mesh_test COMMAND rt64_water_mesh_test)
    add_test(NAME rt64_shore_profile_test COMMAND rt64_shore_profile_test)
    add_test(NAME rt64_wave_field_test COMMAND rt64_wave_field_test)
endif()
