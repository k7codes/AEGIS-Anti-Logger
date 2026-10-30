cmake_minimum_required(VERSION 3.20)
project(AEGISAntiLogger LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- imGui (vendored) -------------------------------------------------------
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
# Cobalt vendor ui katmani ImVec2 operator'larini kullanir (+, -).
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# ---- Cobalt UI katmani (vendor: github.com/wezy04-cloud/... ) --------------
set(COBALT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cobalt/src)

# ---- AEGIS Anti Logger ------------------------------------------------------
add_executable(aegis WIN32
    src/app.cpp
    src/config_store.cpp
    src/persistence.cpp
    src/hook_engine.cpp
    src/noise_engine.cpp
    src/ui_fx.cpp
    src/cobalt_platform.cpp
    ${COBALT_DIR}/config.cpp
    ${COBALT_DIR}/lang/lang.cpp
    ${COBALT_DIR}/ui/core.cpp
    ${COBALT_DIR}/ui/widgets.cpp
    ${COBALT_DIR}/ui/icons.cpp
    ${COBALT_DIR}/ui/flags.cpp
)

target_include_directories(aegis PRIVATE src ${COBALT_DIR})
target_link_libraries(aegis PRIVATE
    imgui
    user32
    gdi32
    d3d11
    dxgi
    d3dcompiler_47
    dwmapi
    advapi32
    shell32
    ole32
    shlwapi
    windowscodecs
)

if(MINGW)
    # Uygulama ikonu: rc'yi dogrudan windres ile derle (path'lerdeki bosluklar
    # CMake'in rc kuralini bozdugu icin manual cofig).
    get_filename_component(MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(WINDRES_EXE windres HINTS "${MINGW_BIN}" REQUIRED)
    set(APP_ICON_OBJ ${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc.o)
    add_custom_command(OUTPUT "${APP_ICON_OBJ}"
        COMMAND "${WINDRES_EXE}" -O coff "${CMAKE_CURRENT_SOURCE_DIR}/resources/app_icon.rc" -o "${APP_ICON_OBJ}"
        DEPENDS resources/app_icon.rc resources/app.ico
        COMMENT "windres: uygulama ikonu"
        VERBATIM)
    target_sources(aegis PRIVATE "${APP_ICON_OBJ}")
    set_source_files_properties("${APP_ICON_OBJ}" PROPERTIES
        EXTERNAL_OBJECT TRUE GENERATED TRUE)

    # Tek exe, çalışma zamanı DLL'i istemesin.
    target_link_options(aegis PRIVATE -static -static-libgcc -static-libstdc++)
    target_compile_options(aegis PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
