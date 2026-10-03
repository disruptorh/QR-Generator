#ifndef QR_UI_FILE_DIALOG_HPP_
#define QR_UI_FILE_DIALOG_HPP_

#include <filesystem>
#include <string>
#include <vector>

namespace ui {

// A small save dialog written against ImGui directly.
//
// The app deliberately does not shell out to zenity/kdialog and does not link a
// native dialog: neither is guaranteed to be installed, and the external ones
// inherit the whole desktop theme. This one looks exactly like the rest of the
// window and needs nothing beyond what is already linked.
//
// It never writes anything itself; the caller decides what to do with the path.
class SaveFileDialog {
 public:
  struct Entry {
    std::string name;
    bool is_directory = false;
  };

  // Opens the dialog on `directory`, filtering by `extension` (with the dot, e.g.
  // ".png"). `initial_filename` seeds the name field.
  void open(const std::string& directory, const std::string& extension,
            const std::string& initial_filename);

  // True while the dialog wants to be shown.
  bool is_open() const { return open_; }

  // Draws the dialog. Returns true on the frame the user confirms.
  bool draw();

  std::string current_directory() const { return directory_.string(); }
  std::string selected_path() const;

 private:
  void list_directory();

  bool open_ = false;
  // Set once the request has been handed to ImGui, and once ImGui has actually
  // shown the popup, so neither is mistaken for a dismissal.
  bool queued_ = false;
  bool shown_ = false;
  std::string error_;
  std::filesystem::path directory_;
  std::string extension_;
  std::string filename_;
  std::vector<Entry> entries_;
};

}  // namespace ui

#endif  // QR_UI_FILE_DIALOG_HPP_