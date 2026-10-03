# Linux uses RT64's SDL/Vulkan presentation path. RT64 is kept static so the
# renderer can be linked into the existing Ship/LUS executable.
get_target_property(_lus_sources libultraship SOURCES)
list(FILTER _lus_sources EXCLUDE REGEX "(^|/)fast/backends/.*\\.cpp$")
set_property(TARGET libultraship PROPERTY SOURCES "${_lus_sources}")

get_target_property(_lus_compile_definitions libultraship COMPILE_DEFINITIONS)
if(_lus_compile_definitions)
    list(REMOVE_ITEM _lus_compile_definitions ENABLE_OPENGL)
    set_property(TARGET libultraship PROPERTY COMPILE_DEFINITIONS "${_lus_compile_definitions}")
endif()

set(RT64_STATIC ON CACHE BOOL "Build RT64 as a static library" FORCE)
set(RT64_SDL_WINDOW_VULKAN ON CACHE BOOL "Use SDL's Vulkan window on Linux" FORCE)
set(PLUME_BUILD_EXAMPLES OFF CACHE BOOL "Do not build Plume examples" FORCE)
# The portal backend avoids making GTK3 a mandatory build dependency.
set(NFD_PORTAL ON CACHE BOOL "Use xdg-desktop-portal for Linux file dialogs" FORCE)

add_subdirectory("${CMAKE_SOURCE_DIR}/rt64" "${CMAKE_BINARY_DIR}/rt64")

# LUS owns the ImGui instance used by the application (1.91.9b docking). RT64
# vendors and compiles its own 1.90 core, which would create a second ImGui
# context and duplicate symbols when both static libraries are linked. Reuse
# LUS's core and its SDL platform backend while retaining RT64's Vulkan
# renderer backend.
get_target_property(_rt64_sources rt64 SOURCES)
list(FILTER _rt64_sources EXCLUDE REGEX "/src/contrib/imgui/imgui(_demo|_draw|_tables|_widgets)?\\.cpp$")
list(FILTER _rt64_sources EXCLUDE REGEX "/src/contrib/imgui/backends/imgui_impl_vulkan\\.cpp$")
list(FILTER _rt64_sources EXCLUDE REGEX "/src/imgui/imgui_impl_sdl2_custom\\.cpp$")
set_property(TARGET rt64 PROPERTY SOURCES "${_rt64_sources}")

# RT64 includes its core as "imgui/imgui.h". Supply a tiny build-tree shim so
# this path resolves to the same header used by LUS. The upstream RT64 checkout
# remains untouched.
set(_rt64_imgui_compat_dir "${CMAKE_CURRENT_BINARY_DIR}/rt64-imgui-compat")
file(MAKE_DIRECTORY "${_rt64_imgui_compat_dir}/imgui")
configure_file("${CMAKE_CURRENT_LIST_DIR}/rt64-imgui-compat/imgui.h"
    "${_rt64_imgui_compat_dir}/imgui/imgui.h" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/rt64-imgui-compat/imgui_internal.h"
    "${_rt64_imgui_compat_dir}/imgui/imgui_internal.h" COPYONLY)
file(MAKE_DIRECTORY "${_rt64_imgui_compat_dir}/imgui/backends")
configure_file("${CMAKE_CURRENT_LIST_DIR}/rt64-imgui-compat/imgui_impl_vulkan.h"
    "${_rt64_imgui_compat_dir}/imgui/backends/imgui_impl_vulkan.h" COPYONLY)

get_target_property(_lus_imgui_include_dirs ImGui INTERFACE_INCLUDE_DIRECTORIES)
list(GET _lus_imgui_include_dirs 0 _lus_imgui_source_dir)
target_sources(rt64 PRIVATE "${_lus_imgui_source_dir}/backends/imgui_impl_vulkan.cpp")
target_include_directories(rt64 BEFORE PUBLIC
    "${_rt64_imgui_compat_dir}"
    ${_lus_imgui_include_dirs}
)
target_include_directories(rt64 PUBLIC
    "${CMAKE_SOURCE_DIR}/rt64/src"
    "${CMAKE_SOURCE_DIR}/rt64/src/contrib"
    "${CMAKE_SOURCE_DIR}/rt64/src/contrib/hlslpp/include"
)
target_compile_definitions(rt64 PUBLIC
    HLSL_CPU
    FFX_GCC
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    RT64_SDL_WINDOW_VULKAN
    PLUME_SDL_VULKAN_ENABLED
)
# RT64 embeds stb_image in rt64_texture_cache.cpp while LUS also provides its
# own stb_image archive. Keep RT64's implementation private to that translation
# unit to avoid duplicate symbols when the two static libraries are linked.
target_compile_definitions(rt64 PRIVATE STB_IMAGE_STATIC)
target_link_libraries(rt64 ImGui)

# Propagate RT64's public headers and final link dependency to the executable
# and other LUS consumers.
target_link_libraries(libultraship PUBLIC rt64)
target_compile_definitions(libultraship PUBLIC SHIP_USE_RT64)
target_sources(libultraship PRIVATE
    "${CMAKE_SOURCE_DIR}/libultraship/src/fast/rt64/DisplayListTranslator.cpp"
    "${CMAKE_SOURCE_DIR}/libultraship/src/fast/rt64/Renderer.cpp"
    "${CMAKE_SOURCE_DIR}/libultraship/src/fast/rt64/SdlVulkanWindow.cpp"
    "${CMAKE_SOURCE_DIR}/libultraship/src/fast/rt64/TexturePatches.cpp"
)

add_subdirectory("${CMAKE_SOURCE_DIR}/tests/rt64" "${CMAKE_BINARY_DIR}/tests/rt64")
