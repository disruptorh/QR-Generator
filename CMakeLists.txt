cmake_minimum_required(VERSION 3.20)
project(qr_generator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(qr_core STATIC
  src/qr/qr_spec.cpp
  src/qr/qr_encoder.cpp
  src/qr/qr_decoder.cpp
  src/render/raster.cpp
  src/render/png.cpp
  src/render/svg.cpp
  src/payload/builders.cpp
  src/app/app_state.cpp
)
target_include_directories(qr_core PUBLIC src)

# zlib backs the PNG writer; the app links the same static library.
find_package(ZLIB REQUIRED)
target_link_libraries(qr_core PUBLIC ZLIB::ZLIB)
target_compile_options(qr_core PRIVATE -Wall -Wextra -Wpedantic)

# ImGui is vendored and only compiled into the GUI target. Its sources are
# listed here so the vendored tree is validated, but no windowing or GL context
# is created here: qr_core must stay usable headless.
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)

# GLFW supplies the window, the OpenGL context and the clipboard. ImGui ships its
# own OpenGL loader, so a GL/gl3.h is not required on the build machine.
find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

enable_testing()
add_executable(qr_encoder_tests tests/qr_encoder_tests.cpp)
target_link_libraries(qr_encoder_tests PRIVATE qr_core)
add_test(NAME qr_encoder_tests COMMAND qr_encoder_tests)

add_executable(qr_decoder_tests tests/qr_decoder_tests.cpp)
target_link_libraries(qr_decoder_tests PRIVATE qr_core)
add_test(NAME qr_decoder_tests COMMAND qr_decoder_tests)

add_executable(render_tests tests/render_tests.cpp)
target_link_libraries(render_tests PRIVATE qr_core)
add_test(NAME render_tests COMMAND render_tests)

add_executable(payload_tests tests/payload_tests.cpp)
target_link_libraries(payload_tests PRIVATE qr_core)
add_test(NAME payload_tests COMMAND payload_tests)

add_executable(app_state_tests tests/app_state_tests.cpp)
target_link_libraries(app_state_tests PRIVATE qr_core)
add_test(NAME app_state_tests COMMAND app_state_tests)

# ---------------------------------------------------------------------------
# The application
# ---------------------------------------------------------------------------

add_library(qr_gui STATIC
  src/app/theme.cpp
  src/platform/clipboard.cpp
  src/platform/security.cpp
  src/ui/file_dialog.cpp
  src/ui/main_window.cpp
)
target_link_libraries(qr_gui PUBLIC qr_core imgui)

add_executable(qr_generator
  src/main.cpp
)
target_link_libraries(qr_generator PRIVATE qr_gui)
