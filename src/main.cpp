// Entry point for the QR generator.
//
// Order matters here: the sandbox is installed before anything else opens a file
// or a window, and every failure after that is reported in the UI rather than
// thrown, because a window that closes on startup is worse than one that admits
// it could not lock itself down.

#include <cstdio>
#include <cstdlib>

#include <GLFW/glfw3.h>

#include "app/app_state.hpp"
#include "app/theme.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "platform/security.hpp"
#include "ui/main_window.hpp"

namespace {

void on_glfw_error(int code, const char* description) {
  std::fprintf(stderr, "GLFW error %d: %s\n", code, description != nullptr ? description : "?");
}

}  // namespace

int main() {
  // First, before anything allocates a socket or opens a file.
  const core::Result<platform::SandboxStatus> sandbox = platform::harden_process();
  platform::SandboxStatus sandbox_status{};
  if (sandbox.has_value()) sandbox_status = sandbox.value();

  glfwSetErrorCallback(on_glfw_error);
  if (glfwInit() != GLFW_TRUE) {
    std::fprintf(stderr, "No se pudo inicializar GLFW.\n");
    return 1;
  }

  // Core profile 3.3, which is what the ImGui backend asks for. Resizable window
  // on a desktop; the UI lays itself out from the framebuffer size each frame.
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __linux__
  // A generator window has no business appearing in a task switcher list.
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#endif

  GLFWwindow* window = glfwCreateWindow(1100, 760, "Generador de c\u00f3digos QR", nullptr,
                                        nullptr);
  if (window == nullptr) {
    std::fprintf(stderr, "No se pudo crear la ventana (OpenGL 3.3 no disponible).\n");
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);  // vsync

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;   // never write an imgui.ini next to the binary
  io.LogFilename = nullptr;
  app::apply_theme();
  const std::string font_path = app::load_default_font();
  if (font_path.empty()) {
    std::fprintf(stderr,
                 "Aviso: no se encontro una fuente monoespaciada del sistema; "
                 "se usara la fuente integrada.\n");
  }

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  ui::MainWindow window_ui;

  while (glfwWindowShouldClose(window) == 0) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    window_ui.draw(sandbox_status);

    ImGui::Render();

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    app::clear_color(&r, &g, &b);
    glViewport(0, 0, width, height);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}