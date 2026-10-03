#ifndef QR_APP_THEME_HPP_
#define QR_APP_THEME_HPP_

#include <string>

#include "imgui.h"

namespace app {

// The four semantic colours used for validation feedback and status lines.
struct SemanticColors {
  ImVec4 error{0.95f, 0.35f, 0.35f, 1.00f};
  ImVec4 success{0.42f, 0.88f, 0.52f, 1.00f};
  ImVec4 warning{0.92f, 0.72f, 0.25f, 1.00f};
  ImVec4 soft_green{0.55f, 0.85f, 0.55f, 1.00f};
  ImVec4 clear_background{0.06f, 0.07f, 0.09f, 1.00f};
};

const SemanticColors& theme_colors();

// The window clear colour, as three floats for glClearColor.
void clear_color(float* r, float* g, float* b);

// Applies the look: the stock dark palette, the rounded corners, the spacing and
// the 1 px window border. Calls ImGui::StyleColorsDark() first and only then
// adjusts metrics, so no palette entry is ever left half-modified.
//
// Must be called once, after ImGui::CreateContext() and before the first frame.
void apply_theme();

// Loads the 18 px monospace default font, trying a short list of paths that
// cover the common Linux font packages. Falls back to ImGui's built-in font when
// none is present, so the app always starts. Returns the path that was loaded,
// or an empty string when the fallback is in use.
std::string load_default_font();

// Whether the loaded font covers Latin-1 supplement (accented Spanish characters
// such as á, é, ñ, ¿). When false the UI warns instead of drawing blanks.
bool font_covers_latin1();

}  // namespace app

#endif  // QR_APP_THEME_HPP_