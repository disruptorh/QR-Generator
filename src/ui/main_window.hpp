#ifndef QR_UI_MAIN_WINDOW_HPP_
#define QR_UI_MAIN_WINDOW_HPP_

#include <string>

#include "app/app_state.hpp"
#include "app/theme.hpp"
#include "platform/clipboard.hpp"
#include "platform/security.hpp"
#include "ui/file_dialog.hpp"

namespace ui {

// The whole interface. It owns the input state, calls into the headless core on
// every change, and draws the result; it holds no encoding logic of its own.
class MainWindow {
 public:
  // Seconds a copied payload stays on the clipboard before being wiped.
  static constexpr std::uint32_t kClipboardSeconds = 20;

  // `status_message` is shown in the status bar.
  void draw(const platform::SandboxStatus& sandbox);

  void open_save_dialog();

 private:
  void draw_type_selector();
  void draw_type_inputs();
  void draw_text_inputs();
  void draw_url_inputs();
  void draw_wifi_inputs();
  void draw_vcard_inputs();
  void draw_email_inputs();
  void draw_sms_inputs();
  void draw_phone_inputs();
  void draw_geo_inputs();
  void draw_event_inputs();

  void draw_encoding_options();
  void draw_render_options();
  void draw_preview();
  void draw_actions();
  void draw_status_bar(const platform::SandboxStatus& sandbox);

  // Recomputes the symbol from the current inputs. Cheap enough to run on every
  // keystroke, and it keeps the preview impossible to desynchronize from the
  // fields.
  void refresh();

  void save_current_format();
  void copy_payload_to_clipboard();

  app::AppInputs inputs_;
  std::optional<app::Generated> generated_;
  std::string error_message_;

  platform::Clipboard clipboard_;
  SaveFileDialog dialog_;
  std::string notice_;         // last action result, shown in the status bar
  bool notice_is_error_ = false;
};

}  // namespace ui

#endif  // QR_UI_MAIN_WINDOW_HPP_