#include "platform/clipboard.hpp"

#include <GLFW/glfw3.h>

namespace platform {

Clipboard::~Clipboard() {
  // Never leave the payload on the clipboard, not even on an abnormal exit.
  clear();
}

bool Clipboard::available() {
  return glfwGetCurrentContext() != nullptr;
}

core::Result<void> Clipboard::set_with_auto_clear(const std::string& text,
                                                  std::uint32_t seconds_before_clear) {
  if (glfwGetCurrentContext() == nullptr) {
    return core::Result<void>::err("clipboard", "No hay ninguna ventana activa.");
  }
  clear();
  // A NULL window means GLFW does not tie ownership to the window, so the
  // content survives until we clear it rather than vanishing on close.
  glfwSetClipboardString(nullptr, text.c_str());
  owned_text_ = text;
  seconds_left_ = seconds_before_clear;
  if (seconds_left_ == 0) clear();
  return core::Result<void>::ok();
}

void Clipboard::clear() {
  if (glfwGetCurrentContext() != nullptr) glfwSetClipboardString(nullptr, nullptr);
  owned_text_.clear();
  seconds_left_ = 0;
}

bool Clipboard::still_ours() const {
  if (owned_text_.empty() || glfwGetCurrentContext() == nullptr) return false;
  const char* current = glfwGetClipboardString(nullptr);
  if (current == nullptr) return false;
  return owned_text_ == current;
}

void Clipboard::tick(double delta_seconds) {
  if (seconds_left_ == 0) return;
  if (delta_seconds <= 0.0) return;
  const double left = static_cast<double>(seconds_left_) - delta_seconds;
  if (left <= 0.0) {
    clear();
  } else {
    seconds_left_ = static_cast<std::uint32_t>(left);
  }
}

}  // namespace platform