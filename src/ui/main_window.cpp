#include "ui/main_window.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

#include "imgui.h"

namespace ui {
namespace {

// Wraps a long single-line string so a long payload stays readable in the
// preview instead of stretching the panel.
void text_wrapped(const char* label, const std::string& value) {
  ImGui::TextUnformatted(label);
  ImGui::TextWrapped("%s", value.c_str());
}

// One labelled row, with the label on the same line as the field.
bool field_row(const char* label, ImGuiInputTextFlags flags, std::string* value,
               float width = -1.0f) {
  ImGui::PushID(label);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(width);
  // InputText needs a mutable buffer, so the std::string is copied through a
  // fixed-size scratch buffer. Every field here is well under that limit; the
  // long ones (text body, descriptions) use their own multi-line helper.
  char buffer[1024];
  const int written = std::snprintf(buffer, sizeof(buffer), "%s", value->c_str());
  bool changed = false;
  if (written < 0 || static_cast<std::size_t>(written) >= sizeof(buffer)) {
    // Too long for the scratch buffer: leave the value alone rather than
    // truncating what the user typed.
    ImGui::TextDisabled("(demasiado largo para este campo)");
    ImGui::PopID();
    return false;
  }
  changed = ImGui::InputText("##value", buffer, sizeof(buffer), flags);
  if (changed) *value = buffer;
  ImGui::PopID();
  return changed;
}

bool multiline_field(const char* label, std::string* value, float height) {
  ImGui::PushID(label);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::PopID();

  ImGui::PushID("##multiline");
  std::vector<char> buffer(value->size() + 512);
  std::snprintf(buffer.data(), buffer.size(), "%s", value->c_str());
  const bool changed = ImGui::InputTextMultiline("##text", buffer.data(), buffer.size(),
                                                 ImVec2(-1.0f, height));
  if (changed) *value = buffer.data();
  ImGui::PopID();
  return changed;
}

// Numeric variant: ImGui edits the value in place, so an out-of-range number
// stays visible and can be reported instead of being silently clamped.
bool double_field(const char* label, double* value, const char* hint) {
  ImGui::PushID(label);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-70.0f);
  double buffer = *value;
  const bool changed = ImGui::InputScalar("##value", ImGuiDataType_Double, &buffer, nullptr,
                                          nullptr, "%.6f");
  if (changed) *value = buffer;
  ImGui::PopID();
  if (hint != nullptr) ImGui::TextDisabled("%s", hint);
  return changed;
}

const char* ecc_option_label(qr::Ecc ecc) {
  switch (ecc) {
    case qr::Ecc::low: return "Bajo (7%)";
    case qr::Ecc::medium: return "Medio (15%)";
    case qr::Ecc::quartile: return "Cuartil (25%)";
    case qr::Ecc::high: return "Alto (30%)";
  }
  return "Medio (15%)";
}

}  // namespace

void MainWindow::refresh() {
  const core::Result<app::Generated> result = app::generate(inputs_);
  if (result.has_value()) {
    generated_ = result.value();
    error_message_.clear();
  } else {
    generated_.reset();
    error_message_ = result.error().message;
  }
}

void MainWindow::draw(const platform::SandboxStatus& sandbox) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

  const ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
                                        ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (ImGui::Begin("Generador de c\u00f3digos QR", nullptr, window_flags)) {
    clipboard_.tick(ImGui::GetIO().DeltaTime);

    const float vp_w = ImGui::GetMainViewport()->WorkSize.x;
    if (vp_w < 900.0f) {
      ImGui::Columns(1, "columns", true);
    } else {
      ImGui::Columns(2, "columns", true);
    }
    if (ImGui::BeginChild("inputs", ImVec2(0.0f, 0.0f), true)) {
      draw_type_selector();
      ImGui::Separator();
      draw_type_inputs();
      ImGui::Separator();
      draw_encoding_options();
      ImGui::Separator();
      draw_render_options();
    }
    ImGui::EndChild();

    // Rebuild between the editor and the preview, so the preview, the action
    // buttons and the status bar all describe the values the user just typed
    // rather than last frame's.
    refresh();

    ImGui::NextColumn();

    if (ImGui::BeginChild("preview", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
      draw_preview();
      draw_actions();
      draw_status_bar(sandbox);
    }
    ImGui::EndChild();
    ImGui::Columns();

    // The dialog has to be drawn while a frame is being built, and its result is
    // consumed immediately so the file is written in the same frame.
    if (dialog_.is_open() && dialog_.draw()) save_current_format();
  }
  ImGui::End();
}

void MainWindow::draw_type_selector() {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Tipo de contenido");
  ImGui::SameLine();

  const payload::ContentType types[] = {
      payload::ContentType::text,  payload::ContentType::url,  payload::ContentType::wifi,
      payload::ContentType::vcard, payload::ContentType::email, payload::ContentType::sms,
      payload::ContentType::phone, payload::ContentType::geo,  payload::ContentType::event,
  };
  ImGui::SetNextItemWidth(240.0f);
  int current = 0;
  for (int i = 0; i < 9; ++i) {
    if (types[i] == inputs_.type) current = i;
  }
  if (ImGui::BeginCombo("##type", payload::content_type_label(inputs_.type))) {
    for (int i = 0; i < 9; ++i) {
      const bool selected = types[i] == inputs_.type;
      if (ImGui::Selectable(payload::content_type_label(types[i]), selected)) {
        inputs_.type = types[i];
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  if (!app::font_covers_latin1()) {
    ImGui::TextColored(app::theme_colors().warning, "%s",
                       "Aviso: la fuente no cubre los acentos; se mostrara\u0301n como bloques.");
  }
}

void MainWindow::draw_type_inputs() {
  switch (inputs_.type) {
    case payload::ContentType::text: draw_text_inputs(); break;
    case payload::ContentType::url: draw_url_inputs(); break;
    case payload::ContentType::wifi: draw_wifi_inputs(); break;
    case payload::ContentType::vcard: draw_vcard_inputs(); break;
    case payload::ContentType::email: draw_email_inputs(); break;
    case payload::ContentType::sms: draw_sms_inputs(); break;
    case payload::ContentType::phone: draw_phone_inputs(); break;
    case payload::ContentType::geo: draw_geo_inputs(); break;
    case payload::ContentType::event: draw_event_inputs(); break;
  }
  if (!error_message_.empty()) {
    ImGui::TextColored(app::theme_colors().error, "%s", error_message_.c_str());
  }
}

void MainWindow::draw_text_inputs() {
  const float th = ImGui::GetContentRegionAvail().y * 0.25f;
  multiline_field("Texto", &inputs_.text.body, th > 60.0f ? th : 60.0f);
}

void MainWindow::draw_url_inputs() {
  field_row("URL", 0, &inputs_.url.url);
}

void MainWindow::draw_wifi_inputs() {
  field_row("Red (SSID)", 0, &inputs_.wifi.ssid);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Seguridad");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(240.0f);
  if (ImGui::BeginCombo("##auth", payload::wifi_auth_label(inputs_.wifi.auth))) {
    for (const payload::WifiAuth auth : {payload::WifiAuth::wpa, payload::WifiAuth::sae,
                                         payload::WifiAuth::wep, payload::WifiAuth::nopass}) {
      const bool selected = auth == inputs_.wifi.auth;
      if (ImGui::Selectable(payload::wifi_auth_label(auth), selected)) inputs_.wifi.auth = auth;
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  if (inputs_.wifi.auth != payload::WifiAuth::nopass) {
    field_row("Contrase\u00f1a", ImGuiInputTextFlags_Password, &inputs_.wifi.password);
  }
  ImGui::Checkbox("Red oculta", &inputs_.wifi.hidden);
}

void MainWindow::draw_vcard_inputs() {
  field_row("Nombre", 0, &inputs_.contact.full_name);
  field_row("Organizaci\u00f3n", 0, &inputs_.contact.organization);
  field_row("Cargo", 0, &inputs_.contact.title);
  // A small fixed set of rows is enough for a contact card and avoids the
  // dynamic-list UI that would complicate the panel for little gain.
  if (inputs_.contact.phones.size() < 3) {
    if (ImGui::Button("A\u00f1adir tel\u00e9fono")) inputs_.contact.phones.emplace_back();
  }
  for (std::size_t i = 0; i < inputs_.contact.phones.size(); ++i) {
    field_row("Tel\u00e9fono", 0, &inputs_.contact.phones[i]);
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) {
      inputs_.contact.phones.erase(inputs_.contact.phones.begin() +
                                   static_cast<std::ptrdiff_t>(i));
      break;
    }
  }
  if (inputs_.contact.emails.size() < 3) {
    if (ImGui::Button("A\u00f1adir correo")) inputs_.contact.emails.emplace_back();
  }
  for (std::size_t i = 0; i < inputs_.contact.emails.size(); ++i) {
    field_row("Correo", 0, &inputs_.contact.emails[i]);
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) {
      inputs_.contact.emails.erase(inputs_.contact.emails.begin() +
                                   static_cast<std::ptrdiff_t>(i));
      break;
    }
  }
  field_row("Web", 0, &inputs_.contact.url);
  multiline_field("Direcci\u00f3n", &inputs_.contact.address, 72.0f);
}

void MainWindow::draw_email_inputs() {
  field_row("Destinatario", 0, &inputs_.email.address);
  field_row("Asunto", 0, &inputs_.email.subject);
  multiline_field("Mensaje", &inputs_.email.body, 110.0f);
}

void MainWindow::draw_sms_inputs() {
  field_row("N\u00famero", 0, &inputs_.sms.number);
  multiline_field("Mensaje", &inputs_.sms.message, 110.0f);
}

void MainWindow::draw_phone_inputs() {
  field_row("N\u00famero", 0, &inputs_.phone.number);
}

void MainWindow::draw_geo_inputs() {
  double_field("Latitud", &inputs_.geo.latitude, "Entre -90 y 90.");
  double_field("Longitud", &inputs_.geo.longitude, "Entre -180 y 180.");
  field_row("Altitud (m)", 0, &inputs_.geo.altitude);
  ImGui::TextDisabled("Opcional. Se escribe como geo:lat,lon,alt.");
}

void MainWindow::draw_event_inputs() {
  field_row("T\u00edtulo", 0, &inputs_.event.summary);
  field_row("Inicio", 0, &inputs_.event.start_local);
  ImGui::TextDisabled("AAAA-MM-DDTHH:MM");
  field_row("Fin", 0, &inputs_.event.end_local);
  field_row("Lugar", 0, &inputs_.event.location);
  multiline_field("Descripci\u00f3n", &inputs_.event.description, 90.0f);
}

void MainWindow::draw_encoding_options() {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Correcci\u00f3n de errores");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(240.0f);
  if (ImGui::BeginCombo("##ecc", ecc_option_label(inputs_.encode.ecc))) {
    for (const qr::Ecc ecc : {qr::Ecc::low, qr::Ecc::medium, qr::Ecc::quartile, qr::Ecc::high}) {
      const bool selected = ecc == inputs_.encode.ecc;
      if (ImGui::Selectable(ecc_option_label(ecc), selected)) inputs_.encode.ecc = ecc;
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::Checkbox("Reforzar si cabe en la misma versi\u00f3n", &inputs_.encode.boost_ecc);

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("M\u00e1scara");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(240.0f);
  if (ImGui::BeginCombo("##mask",
                        inputs_.encode.mask < 0 ? "Autom\u00e1tica"
                                                : std::to_string(inputs_.encode.mask).c_str())) {
    if (ImGui::Selectable("Autom\u00e1tica", inputs_.encode.mask < 0)) inputs_.encode.mask = -1;
    for (int i = 0; i < 8; ++i) {
      const std::string label = std::to_string(i);
      if (ImGui::Selectable(label.c_str(), inputs_.encode.mask == i)) inputs_.encode.mask = i;
    }
    ImGui::EndCombo();
  }

  int min_version = inputs_.encode.min_version;
  int max_version = inputs_.encode.max_version;
  ImGui::SetNextItemWidth(120.0f);
  ImGui::SliderInt("##minversion", &min_version, 1, 40, "Versi\u00f3n m\u00edn: %d");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160.0f);
  ImGui::SliderInt("##maxversion", &max_version, 1, 40, "Versi\u00f3n m\u00e1x: %d");
  if (min_version <= max_version) {
    inputs_.encode.min_version = min_version;
    inputs_.encode.max_version = max_version;
  }
}

void MainWindow::draw_render_options() {
  ImGui::SetNextItemWidth(200.0f);
  ImGui::SliderInt("##modulepx", &inputs_.render.module_px, 1, 32, "Px por m\u00f3dulo: %d");
  ImGui::SetNextItemWidth(200.0f);
  ImGui::SliderInt("##quiet", &inputs_.render.quiet_zone, 0, 8, "Zona de silencio: %d");

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Color del m\u00f3dulo");
  ImGui::SameLine();
  float foreground[3] = {static_cast<float>(inputs_.render.foreground.r) / 255.0f,
                         static_cast<float>(inputs_.render.foreground.g) / 255.0f,
                         static_cast<float>(inputs_.render.foreground.b) / 255.0f};
  if (ImGui::ColorEdit3("##fg", foreground, ImGuiColorEditFlags_NoInputs |
                                                ImGuiColorEditFlags_NoLabel)) {
    inputs_.render.foreground = render::Rgb{
        static_cast<std::uint8_t>(foreground[0] * 255.0f + 0.5f),
        static_cast<std::uint8_t>(foreground[1] * 255.0f + 0.5f),
        static_cast<std::uint8_t>(foreground[2] * 255.0f + 0.5f)};
  }
  ImGui::SameLine();
  ImGui::TextUnformatted("Fondo");
  ImGui::SameLine();
  float background[3] = {static_cast<float>(inputs_.render.background.r) / 255.0f,
                         static_cast<float>(inputs_.render.background.g) / 255.0f,
                         static_cast<float>(inputs_.render.background.b) / 255.0f};
  if (ImGui::ColorEdit3("##bg", background, ImGuiColorEditFlags_NoInputs |
                                                ImGuiColorEditFlags_NoLabel)) {
    inputs_.render.background = render::Rgb{
        static_cast<std::uint8_t>(background[0] * 255.0f + 0.5f),
        static_cast<std::uint8_t>(background[1] * 255.0f + 0.5f),
        static_cast<std::uint8_t>(background[2] * 255.0f + 0.5f)};
  }
}

void MainWindow::draw_preview() {
  if (!generated_.has_value()) {
    ImGui::TextDisabled("Corrige los campos para ver el c\u00f3digo.");
    return;
  }

  const app::Generated& generated = generated_.value();
  text_wrapped("Resumen", app::describe(generated));

  // The matrix is drawn straight into the draw list, snapped to whole pixels, so
  // the preview is exactly the symbol that will be written to disk -- no texture
  // scaling, no resampling, no chance of the preview and the file disagreeing.
  const qr::Matrix& matrix = generated.matrix;
  const int quiet = inputs_.render.quiet_zone;
  const int modules = matrix.size + 2 * quiet;
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  // Leave space for payload text + stats below (~40% of height max) to avoid pushing content off-screen
  const float avail_for_qr = avail.y * 0.6f;
  const float available_dim = avail.x < avail_for_qr ? avail.x : avail_for_qr;
  const float cell = available_dim / static_cast<float>(modules);
  if (cell < 1.0f) {
    ImGui::TextDisabled("La ventana es demasiado estrecha para mostrar la vista previa.");
    return;
  }

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->PushClipRect(ImVec2(origin.x, origin.y),
                          ImVec2(origin.x + cell * static_cast<float>(modules),
                                 origin.y + cell * static_cast<float>(modules)),
                          true);
  // The preview uses the same two colours the exported file will use, so what is
  // on screen is what lands on disk.
  const render::Rgb& fg = inputs_.render.foreground;
  const render::Rgb& bg = inputs_.render.background;
  draw_list->AddRectFilled(origin,
                           ImVec2(origin.x + cell * static_cast<float>(modules),
                                  origin.y + cell * static_cast<float>(modules)),
                           IM_COL32(bg.r, bg.g, bg.b, 255));
  for (int y = 0; y < matrix.size; ++y) {
    for (int x = 0; x < matrix.size; ++x) {
      if (!matrix.at(x, y)) continue;
      const float px = origin.x + cell * static_cast<float>(x + quiet);
      const float py = origin.y + cell * static_cast<float>(y + quiet);
      draw_list->AddRectFilled(ImVec2(px, py), ImVec2(px + cell, py + cell),
                               IM_COL32(fg.r, fg.g, fg.b, 255));
    }
  }
  draw_list->PopClipRect();
  ImGui::Dummy(ImVec2(cell * static_cast<float>(modules), cell * static_cast<float>(modules)));

  ImGui::Separator();
  text_wrapped("Contenido codificado:", generated.payload);

  const qr::EncodeStats& stats = generated.encode_stats;
  char usage[192];
  std::snprintf(usage, sizeof(usage),
                "Datos: %d de %d bytes (%d%%)  |  Correcci\u00f3n: %d bytes  |  "
                "Verificado: %d bloques",
                stats.used_bytes, stats.capacity_bytes,
                stats.capacity_bytes > 0 ? (stats.used_bytes * 100 / stats.capacity_bytes) : 0,
                stats.ecc_codewords, generated.verify_stats.blocks);
  ImGui::TextUnformatted(usage);
}

void MainWindow::draw_actions() {
  ImGui::Separator();
  const bool ready = generated_.has_value();

  ImGui::BeginDisabled(!ready);
  if (ImGui::Button("Guardar PNG", ImVec2(150.0f, 0.0f))) {
    inputs_.format = app::OutputFormat::png;
    open_save_dialog();
  }
  ImGui::SameLine();
  if (ImGui::Button("Guardar SVG", ImVec2(150.0f, 0.0f))) {
    inputs_.format = app::OutputFormat::svg;
    open_save_dialog();
  }
  ImGui::SameLine();
  if (ImGui::Button("Copiar contenido", ImVec2(170.0f, 0.0f))) copy_payload_to_clipboard();
  ImGui::EndDisabled();

  if (clipboard_.seconds_remaining() > 0) {
    char text[128];
    std::snprintf(text, sizeof(text),
                  clipboard_.still_ours()
                      ? "Portapapeles: se borrar\u00e1 en %u s"
                      : "Portapapeles: el contenido fue sustituido por otra aplicaci\u00f3n",
                  clipboard_.seconds_remaining());
    ImGui::TextColored(app::theme_colors().soft_green, "%s", text);
  }
}

void MainWindow::draw_status_bar(const platform::SandboxStatus& sandbox) {
  ImGui::Separator();
  ImGui::TextColored(app::theme_colors().soft_green, "%s", platform::sandbox_summary(sandbox));
  if (!notice_.empty()) {
    ImGui::TextColored(notice_is_error_ ? app::theme_colors().error : app::theme_colors().success, "%s",
                       notice_.c_str());
  }
}

void MainWindow::open_save_dialog() {
  if (!generated_.has_value()) return;
  const std::string extension =
      inputs_.format == app::OutputFormat::svg ? ".svg" : ".png";
  const char* home = std::getenv("HOME");
  dialog_.open(home != nullptr ? home : ".", extension,
               app::suggested_filename(inputs_, generated_.value()));
  ImGui::OpenPopup("Guardar c\u00f3digo QR");
}

void MainWindow::save_current_format() {
  if (!generated_.has_value()) return;
  const std::string path = dialog_.selected_path();
  if (path.empty()) return;

  if (inputs_.format == app::OutputFormat::svg) {
    const core::Result<std::string> svg = app::export_svg(inputs_, generated_.value());
    if (!svg.has_value()) {
      notice_ = svg.error().message;
      notice_is_error_ = true;
      return;
    }
    // Binary-safe enough: the SVG is UTF-8 text written verbatim, with no
    // locale-dependent conversion applied to the bytes.
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
      notice_ = "No se pudo escribir el archivo: " + path;
      notice_is_error_ = true;
      return;
    }
    file.write(svg.value().data(), static_cast<std::streamsize>(svg.value().size()));
    if (!file) {
      notice_ = "Error al escribir el archivo: " + path;
      notice_is_error_ = true;
      return;
    }
    notice_ = "Guardado: " + path;
    notice_is_error_ = false;
    return;
  }

  const core::Result<std::vector<std::uint8_t>> png = app::export_png(inputs_, generated_.value());
  if (!png.has_value()) {
    notice_ = png.error().message;
    notice_is_error_ = true;
    return;
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    notice_ = "No se pudo escribir el archivo: " + path;
    notice_is_error_ = true;
    return;
  }
  file.write(reinterpret_cast<const char*>(png.value().data()),
             static_cast<std::streamsize>(png.value().size()));
  if (!file) {
    notice_ = "Error al escribir el archivo: " + path;
    notice_is_error_ = true;
    return;
  }
  notice_ = "Guardado: " + path;
  notice_is_error_ = false;
}

void MainWindow::copy_payload_to_clipboard() {
  if (!generated_.has_value()) return;
  const core::Result<void> result =
      clipboard_.set_with_auto_clear(generated_.value().payload, kClipboardSeconds);
  if (!result.has_value()) {
    notice_ = result.error().message;
    notice_is_error_ = true;
    return;
  }
  char text[128];
  std::snprintf(text, sizeof(text), "Copiado; se borrar\u00e1 en %u s.", kClipboardSeconds);
  notice_ = text;
  notice_is_error_ = false;
}

}  // namespace ui