#include "app/theme.hpp"

#include <cstdio>

#include "imgui.h"

namespace app {
namespace {

// 18 px monospace, in descending order of preference. These cover the common
// Debian/Ubuntu and Fedora font packages without adding a dependency.
const char* const kFontCandidates[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/hack/Hack-Regular.ttf",
    "/usr/share/fonts/TTF/Hack-Regular.ttf",
    "/usr/share/fonts/TTF/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/freefont/FreeMono.ttf",
};

bool g_latin1 = false;

}  // namespace

const SemanticColors& theme_colors() {
  static const SemanticColors colors;
  return colors;
}

void clear_color(float* r, float* g, float* b) {
  const ImVec4& c = theme_colors().clear_background;
  *r = c.x;
  *g = c.y;
  *b = c.z;
}

void apply_theme() {
  ImGuiStyle& style = ImGui::GetStyle();

  // Start from the stock dark palette, then adjust only metrics.
  ImGui::StyleColorsDark();

  style.WindowRounding = 6.0f;
  style.ChildRounding = 4.0f;
  style.GrabRounding = 4.0f;
  style.FrameRounding = 4.0f;
  style.PopupRounding = 4.0f;
  style.ScrollbarRounding = 4.0f;
  style.TabRounding = 4.0f;

  style.WindowPadding = ImVec2(16.0f, 16.0f);
  style.FramePadding = ImVec2(10.0f, 6.0f);
  style.ItemSpacing = ImVec2(8.0f, 7.0f);
  style.ItemInnerSpacing = ImVec2(8.0f, 7.0f);
  style.ScrollbarSize = 12.0f;
  style.IndentSpacing = 20.0f;
  style.CellPadding = ImVec2(6.0f, 4.0f);

  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;

  style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
}

std::string load_default_font() {
  ImGuiIO& io = ImGui::GetIO();
  for (const char* path : kFontCandidates) {
    ImFont* font = io.Fonts->AddFontFromFileTTF(path, 18.0f);
    if (font == nullptr) continue;  // missing or unreadable, try the next one

    // Confirm the glyphs the UI actually needs. Without this a font that loads
    // but has no Latin-1 coverage would silently render blanks for every accent.
    static const ImWchar probe[] = {0x00e1, 0x00e9, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00bf};  // áéíóúñ¿
    ImFontGlyph* glyphs = font->FindGlyph(ImWchar(probe[0]));
    g_latin1 = glyphs != nullptr && glyphs->AdvanceX > 0.0f;
    io.FontDefault = font;
    return path;
  }

  // No system font found: keep the built-in one so the app still runs.
  io.FontDefault = io.Fonts->Fonts[0];
  g_latin1 = io.Fonts->Fonts[0]->FindGlyph(0x00e1) != nullptr;
  return std::string();
}

bool font_covers_latin1() { return g_latin1; }

}  // namespace app