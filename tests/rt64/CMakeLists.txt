if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "RT64 renderer smoke test is currently enabled on Linux only")
    return()
endif()

add_executable(rt64_renderer_smoke renderer_smoke.cpp)
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

include(CTest)
if(BUILD_TESTING)
    add_test(NAME rt64_renderer_smoke COMMAND rt64_renderer_smoke)
    set_tests_properties(rt64_renderer_smoke PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME rt64_translator_test COMMAND rt64_translator_test)
    add_test(NAME rt64_texture_patches_test COMMAND rt64_texture_patches_test)
endif()
