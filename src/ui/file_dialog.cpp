#include "ui/file_dialog.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdio>

#include "imgui.h"

namespace ui {
namespace {

// Where the dialog starts when the process has no meaningful idea of a home
// directory. Nothing is created here; the listing just comes up empty.
std::filesystem::path initial_directory() {
  if (const char* home = std::getenv("HOME")) {
    if (*home != '\0') return std::filesystem::path(home);
  }
  return std::filesystem::current_path();
}

bool has_extension(const std::string& name, const std::string& extension) {
  if (extension.empty()) return true;
  if (name.size() < extension.size()) return false;
  return name.compare(name.size() - extension.size(), extension.size(), extension) == 0;
}

// The file list is the only part with no natural height, so it gets a fixed one
// and the window is sized around it.
constexpr float kEntriesHeight = 280.0f;

}  // namespace

void SaveFileDialog::open(const std::string& directory, const std::string& extension,
                          const std::string& initial_filename) {
  open_ = true;
  shown_ = false;
  queued_ = true;
  extension_ = extension;
  filename_ = initial_filename;
  error_.clear();

  std::error_code ec;
  std::filesystem::path candidate = directory.empty() ? initial_directory()
                                                      : std::filesystem::path(directory);
  // Never leave the dialog somewhere unusable, whatever path we were handed.
  while (!candidate.empty() && !std::filesystem::is_directory(candidate, ec)) {
    const std::filesystem::path parent = candidate.parent_path();
    if (parent == candidate) {
      candidate = initial_directory();
      break;
    }
    candidate = parent;
  }
  directory_ = candidate.empty() ? std::filesystem::path("/") : candidate;
  list_directory();
}

void SaveFileDialog::list_directory() {
  entries_.clear();
  error_.clear();

  std::error_code ec;
  std::filesystem::directory_iterator it(directory_, ec);
  if (ec) {
    error_ = "No se pudo leer el directorio: " + ec.message();
    return;
  }
  for (const auto& item : it) {
    std::error_code entry_ec;
    const bool is_directory = item.is_directory(entry_ec);
    const std::string name = item.path().filename().string();
    if (name.empty() || name.front() == '.') continue;  // hidden entries
    // Directories are always shown; files only when they match the extension.
    if (!is_directory && !has_extension(name, extension_)) continue;
    entries_.push_back(Entry{name, is_directory});
  }
  // Directories first, then case-insensitively by name.
  std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
    if (a.is_directory != b.is_directory) return a.is_directory;
    std::string an = a.name;
    std::string bn = b.name;
    std::transform(an.begin(), an.end(), an.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(bn.begin(), bn.end(), bn.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return an < bn;
  });
}

std::string SaveFileDialog::selected_path() const {
  if (filename_.empty()) return std::string();
  return (directory_ / filename_).string();
}

bool SaveFileDialog::draw() {
  if (!open_) return false;
  bool confirmed = false;

  // OpenPopup has to be issued from the same ID context as BeginPopupModal, and
  // the two are not called from the same place: open() runs while an editor child
  // window is current, draw() runs after it has been closed. Queuing the request
  // and issuing it here keeps the popup ID stable.
  if (queued_) {
    queued_ = false;
    ImGui::OpenPopup("Guardar c\u00f3digo QR");
  }

  // ImGui opens the popup on a later frame than the one that requested it, so
  // "not open yet" cannot be read as "dismissed".
  const bool visible = ImGui::IsPopupOpen("Guardar c\u00f3digo QR");
  if (visible) shown_ = true;
  if (shown_ && !visible) {
    open_ = false;
    return false;
  }

  // A height of zero means "fit the contents". Sizing the window this way keeps
  // the button row reachable: with a fixed height, the rows after the file list
  // (the name field, the wrapping path, the separator and the buttons) could
  // overflow the window and end up clipped, where they cannot be clicked.
  ImGui::SetNextWindowSize(ImVec2(620.0f, 0.0f), ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Appearing);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;
  if (ImGui::BeginPopupModal("Guardar c\u00f3digo QR", nullptr, flags)) {
    ImGui::TextUnformatted("Ubicaci\u00f3n");
    ImGui::Separator();

    if (!error_.empty()) {
      ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", error_.c_str());
    }

    // Path line: the current directory plus a way back up.
    ImGui::BeginChild("path", ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing()), true);
    if (ImGui::Button("..")) {
      const std::filesystem::path parent = directory_.parent_path();
      if (!parent.empty() && parent != directory_) {
        directory_ = parent;
        list_directory();
      }
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(directory_.string().c_str());
    ImGui::EndChild();

    if (ImGui::BeginChild("entries", ImVec2(0.0f, kEntriesHeight), true)) {
      for (const Entry& entry : entries_) {
        ImGui::PushID(entry.name.c_str());
        if (entry.is_directory) {
          if (ImGui::Selectable((entry.name + "/").c_str(), false,
                                ImGuiSelectableFlags_SpanAllColumns)) {
            directory_ = directory_ / entry.name;
            list_directory();
          }
        } else if (ImGui::Selectable(entry.name.c_str(), false,
                                     ImGuiSelectableFlags_SpanAllColumns)) {
          filename_ = entry.name;
        }
        ImGui::PopID();
      }
      if (entries_.empty()) {
        ImGui::TextDisabled("(directorio vac\u00edo)");
      }
    }
    ImGui::EndChild();

    ImGui::TextUnformatted("Nombre del archivo");
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s", filename_.c_str());
    if (ImGui::InputText("##filename", buffer, sizeof(buffer))) {
      filename_ = buffer;
    }

    const std::string path = selected_path();
    if (path.empty()) {
      ImGui::TextDisabled("Escribe un nombre para guardar.");
    } else {
      ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "%s", path.c_str());
    }

    ImGui::Separator();
    const bool pressed = ImGui::Button("Guardar", ImVec2(120.0f, 0.0f));
    { static int n=0; if (++n%40==0) { const ImVec2 m=ImGui::GetIO().MousePos;
        std::fprintf(stderr,"[d] win=%.0fx%.0f mouse=%.0f,%.0f rect=%.0f..%.0f,%.0f..%.0f hov=%d\n",
          ImGui::GetWindowWidth(), ImGui::GetWindowHeight(), m.x,m.y,
          ImGui::GetItemRectMin().x,ImGui::GetItemRectMax().x,ImGui::GetItemRectMin().y,ImGui::GetItemRectMax().y,
          (int)ImGui::IsItemHovered()); } }
    if (pressed && !path.empty()) {
      confirmed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(120.0f, 0.0f))) {
      open_ = false;
    }
    ImGui::EndPopup();
  }

  if (confirmed) {
    open_ = false;
    ImGui::CloseCurrentPopup();
    return true;
  }
  return false;
}

}  // namespace ui