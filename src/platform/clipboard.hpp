#ifndef QR_PLATFORM_CLIPBOARD_HPP_
#define QR_PLATFORM_CLIPBOARD_HPP_

#include <cstdint>
#include <string>

#include "core/result.hpp"

namespace platform {

// Clipboard access, used because the app targets X11 through GLFW and needs no
// extra dependency for the single operation it performs.
//
// GLFW owns the X11 selection for us: it serves SelectionRequest from its event
// pump, which raw Xlib code in a render loop cannot do correctly without also
// owning the event loop. Ownership is deliberately not tied to the window (a NULL
// handle), so clearing happens exactly when we decide and not when the window is
// closed.
//
// Clipboard contents are readable by every process on the machine, so a payload
// is never left behind: set_with_auto_clear() starts a countdown and clear() wipes
// it. The destructor calls clear() too, so an abnormal exit still does not leave
// a Wi-Fi password behind.
class Clipboard {
 public:
  Clipboard() = default;
  ~Clipboard();
  Clipboard(const Clipboard&) = delete;
  Clipboard& operator=(const Clipboard&) = delete;

  // True when a GLFW window exists, which is the precondition for clipboard use.
  static bool available();

  // Places `text` on the clipboard and starts the auto-clear countdown, replacing
  // any earlier countdown. Pass 0 to clear immediately after the paste window.
  core::Result<void> set_with_auto_clear(const std::string& text,
                                         std::uint32_t seconds_before_clear);

  // Wipes the clipboard now and stops the countdown.
  void clear();

  // Seconds left on the countdown, or 0 when nothing is pending.
  std::uint32_t seconds_remaining() const { return seconds_left_; }

  // Whether the clipboard still holds exactly what we put there. Another app or
  // the user replacing it is normal, so this only drives a status hint.
  bool still_ours() const;

  // Advances the countdown. Called once per frame by the UI, which is the only
  // thing that knows how much time has passed.
  void tick(double delta_seconds);

 private:
  std::string owned_text_;
  std::uint32_t seconds_left_ = 0;
};

}  // namespace platform

#endif  // QR_PLATFORM_CLIPBOARD_HPP_