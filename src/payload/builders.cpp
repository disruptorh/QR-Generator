#include "payload/builders.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace payload {
namespace {

bool is_blank(const std::string& s) {
  return s.find_first_not_of(" \t\r\n") == std::string::npos;
}

bool has_nul(const std::string& s) {
  return s.find('\0') != std::string::npos;
}

std::string trim(const std::string& s) {
  const std::size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  const std::size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::string lower_ascii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// vCard 3.0 escapes a backslash, a comma, a semicolon and every newline.
std::string vcard_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case ';': out += "\\;"; break;
      case ',': out += "\\,"; break;
      case '\n': out += "\\n"; break;
      case '\r': break;  // CRLF pair is emitted as a single \n escape
      default: out += c; break;
    }
  }
  return out;
}

// WIFI: escapes backslash, semicolon, comma, colon and double quote.
std::string wifi_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case ';': out += "\\;"; break;
      case ',': out += "\\,"; break;
      case ':': out += "\\:"; break;
      case '"': out += "\\\""; break;
      default: out += c; break;
    }
  }
  return out;
}

// Percent-encoding for a URI query component (RFC 3986 unreserved set kept).
std::string percent_encode(const std::string& s) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() * 3);
  for (unsigned char c : s) {
    const bool unreserved = std::isalnum(c) != 0 || c == '-' || c == '_' ||
                            c == '.' || c == '~';
    if (unreserved) {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0x0f];
    }
  }
  return out;
}

// Accepts +, digits, spaces, dashes, dots and parentheses; returns the digits
// with a single leading '+' if the number started with one.
bool normalize_phone(const std::string& raw, std::string* out) {
  bool has_plus = false;
  bool seen_digit = false;
  std::string digits;
  for (char c : raw) {
    if (c == '+' && digits.empty() && !has_plus) {
      has_plus = true;
      continue;
    }
    if (c >= '0' && c <= '9') {
      digits += c;
      seen_digit = true;
      continue;
    }
    if (c == ' ' || c == '-' || c == '.' || c == '(' || c == ')' || c == '/') {
      continue;
    }
    return false;
  }
  if (!seen_digit) return false;
  const std::size_t leading = digits.find_first_not_of('0');
  if (leading == std::string::npos) {
    digits = "0";  // all zeroes
  } else {
    digits = digits.substr(leading);
  }
  if (digits.size() > 15) return false;  // E.164
  *out = (has_plus ? "+" : "") + digits;
  return true;
}

// A very small RFC 5322 subset: local@domain with sane characters.
bool looks_like_email(const std::string& raw, std::string* reason) {
  const std::size_t at = raw.find('@');
  if (at == std::string::npos || at == 0 || at + 1 >= raw.size()) {
    *reason = "Falta el '@' en la dirección de correo.";
    return false;
  }
  if (raw.find('@', at + 1) != std::string::npos) {
    *reason = "La dirección de correo lleva más de un '@'.";
    return false;
  }
  const std::string local = raw.substr(0, at);
  const std::string domain = raw.substr(at + 1);
  for (char c : local) {
    const bool ok = std::isalnum(static_cast<unsigned char>(c)) != 0 ||
                    std::strchr(".!#$%&'*+/=?^_`{|}~-", c) != nullptr;
    if (!ok) {
      *reason = "La parte antes del '@' tiene caracteres no válidos.";
      return false;
    }
  }
  if (domain.find('.') == std::string::npos || domain.front() == '.' ||
      domain.back() == '.' || domain.find("..") != std::string::npos) {
    *reason = "El dominio no parece válido (debe contener un punto).";
    return false;
  }
  for (char c : domain) {
    const bool ok = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' ||
                    c == '-';
    if (!ok) {
      *reason = "El dominio tiene caracteres no válidos.";
      return false;
    }
  }
  return true;
}

// "YYYY-MM-DDTHH:MM" with a real calendar date and a sane clock. iCalendar
// floating local time; seconds and zone suffixes are rejected on purpose so the
// emitted DTSTART stays unambiguous.
bool valid_local_datetime(const std::string& s) {
  if (s.size() != 16) return false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (i == 4 || i == 7) {
      if (s[i] != '-') return false;
    } else if (i == 10) {
      if (s[i] != 'T') return false;
    } else if (i == 13) {
      if (s[i] != ':') return false;
    } else if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
      return false;
    }
  }
  const int year = std::atoi(s.substr(0, 4).c_str());
  const int month = std::atoi(s.substr(5, 2).c_str());
  const int day = std::atoi(s.substr(8, 2).c_str());
  const int hour = std::atoi(s.substr(11, 2).c_str());
  const int minute = std::atoi(s.substr(14, 2).c_str());
  if (month < 1 || month > 12 || day < 1 || day > 31) return false;
  if (hour > 23 || minute > 59) return false;
  if (year < 1900 || year > 2999) return false;
  return true;
}

// "YYYY-MM-DDTHH:MM" to the iCalendar form "YYYYMMDDTHHMMSS". This is floating
// local time: no zone suffix, so DTSTART stays unambiguous. Seconds are zeroed
// because the input has no second field.
std::string datetime_to_stamp(const std::string& s) {
  return s.substr(0, 4) + s.substr(5, 2) + s.substr(8, 2) + "T" + s.substr(11, 2) +
         s.substr(14, 2) + "00";
}

// Formats a double with a fixed number of decimals and a dot separator.
// snprintf("%.Nf") follows LC_NUMERIC, and under a Spanish locale that is a
// comma -- which would turn geo:40.4,-3.7 into geo:40,4,-3,7 and corrupt the
// payload. Imbue the classic locale so the output is identical everywhere.
std::string format_decimal(double value, int decimals) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::fixed << std::setprecision(decimals) << value;
  return stream.str();
}

}  // namespace

const char* content_type_label(ContentType type) {
  switch (type) {
    case ContentType::text: return "Texto";
    case ContentType::url: return "URL";
    case ContentType::wifi: return "Wi-Fi";
    case ContentType::vcard: return "Contacto (vCard)";
    case ContentType::email: return "Correo";
    case ContentType::sms: return "SMS";
    case ContentType::phone: return "Teléfono";
    case ContentType::geo: return "Ubicación";
    case ContentType::event: return "Evento";
  }
  return "Texto";
}

const char* wifi_auth_label(WifiAuth auth) {
  switch (auth) {
    case WifiAuth::wpa: return "WPA / WPA2";
    case WifiAuth::sae: return "WPA3 (SAE)";
    case WifiAuth::wep: return "WEP";
    case WifiAuth::nopass: return "Abierta (sin contraseña)";
  }
  return "WPA / WPA2";
}

BuildResult build_text(const TextInput& in) {
  if (in.body.empty()) {
    return BuildResult::err("body", "Escribe el texto que quieres codificar.");
  }
  if (has_nul(in.body)) {
    return BuildResult::err("body",
                            "El texto no puede contener bytes nulos (0x00); "
                            "no se pueden codificar en un QR.");
  }
  return BuildResult::ok(in.body);
}

BuildResult build_url(const UrlInput& in) {
  const std::string raw = trim(in.url);
  if (raw.empty()) {
    return BuildResult::err("url", "Escribe una URL.");
  }
  if (has_nul(raw)) {
    return BuildResult::err("url", "La URL contiene bytes nulos no válidos.");
  }
  for (unsigned char c : raw) {
    if (c < 0x20 || c == 0x7f) {
      return BuildResult::err("url",
                              "La URL no puede contener saltos de línea ni "
                              "caracteres de control.");
    }
  }
  const std::string lowered = lower_ascii(raw);
  const bool has_scheme =
      lowered.rfind("https://", 0) == 0 || lowered.rfind("http://", 0) == 0 ||
      lowered.rfind("ftp://", 0) == 0 || lowered.rfind("mailto:", 0) == 0 ||
      lowered.rfind("tel:", 0) == 0 || lowered.rfind("geo:", 0) == 0;
  if (has_scheme) return BuildResult::ok(raw);

  // No scheme: accept a bare host and default to https, reject anything that
  // still looks broken after that.
  const bool plausible_host =
      raw.find(' ') == std::string::npos &&
      raw.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~:/?#[]@!$&'()*+,;=%")
          == std::string::npos;
  if (!plausible_host || raw.front() == '/' || raw.back() == '/') {
    return BuildResult::err(
        "url",
        "URL no válida. Incluye el esquema (https://…) o escribe solo el "
        "dominio.");
  }
  return BuildResult::ok("https://" + raw);
}

BuildResult build_wifi(const WifiInput& in) {
  const std::string ssid = trim(in.ssid);
  if (ssid.empty()) {
    return BuildResult::err("ssid", "El nombre de la red (SSID) es obligatorio.");
  }
  if (has_nul(ssid) || has_nul(in.password)) {
    return BuildResult::err(
        "ssid", "El SSID y la contraseña no pueden contener bytes nulos.");
  }
  if (in.ssid.size() > 32) {
    return BuildResult::err("ssid", "El SSID no puede pasar de 32 bytes.");
  }
  if (ssid.size() > 32) {
    return BuildResult::err("ssid", "El SSID no puede pasar de 32 bytes.");
  }

  std::string out = "WIFI:T:";
  out += (in.auth == WifiAuth::wpa)   ? "WPA"
         : (in.auth == WifiAuth::sae) ? "SAE"
         : (in.auth == WifiAuth::wep) ? "WEP"
                                      : "nopass";
  out += ";S:";
  out += wifi_escape(ssid);
  if (in.auth != WifiAuth::nopass) {
    if (in.password.empty()) {
      return BuildResult::err(
          "password",
          in.auth == WifiAuth::wep
              ? "WEP necesita contraseña. Si la red es abierta, elige la "
                "opción 'Abierta'."
              : "Falta la contraseña de la red. Si es abierta, elige la "
                "opción 'Abierta (sin contraseña)'.");
    }
    if (in.password.size() > 63) {
      return BuildResult::err("password",
                              "La contraseña no puede pasar de 63 bytes.");
    }
    out += ";P:";
    out += wifi_escape(in.password);
  }
  out += ";H:";
  out += in.hidden ? "true" : "false";
  out += ";;";
  return BuildResult::ok(out);
}

BuildResult build_vcard(const ContactInput& in) {
  const std::string name = trim(in.full_name);
  if (name.empty()) {
    return BuildResult::err("full_name", "El nombre de contacto es obligatorio.");
  }
  for (const std::string& v : {in.full_name, in.organization, in.title, in.url,
                               in.address}) {
    if (has_nul(v)) {
      return BuildResult::err("full_name",
                              "Los campos de contacto no pueden contener bytes "
                              "nulos.");
    }
  }
  std::string out = "BEGIN:VCARD\r\nVERSION:3.0\r\n";
  out += "N:" + vcard_escape(name) + ";;;;\r\n";
  out += "FN:" + vcard_escape(name) + "\r\n";
  if (!is_blank(in.organization)) {
    out += "ORG:" + vcard_escape(trim(in.organization)) + "\r\n";
  }
  if (!is_blank(in.title)) {
    out += "TITLE:" + vcard_escape(trim(in.title)) + "\r\n";
  }
  int tel_count = 0;
  for (const std::string& phone : in.phones) {
    if (is_blank(phone)) continue;
    std::string normalized;
    if (!normalize_phone(trim(phone), &normalized)) {
      return BuildResult::err(
          "phones",
          "Teléfono no válido: '" + trim(phone) +
              "'. Usa dígitos y, opcionalmente, +, espacios o guiones.");
    }
    out += "TEL;TYPE=CELL:" + normalized + "\r\n";
    ++tel_count;
  }
  int mail_count = 0;
  for (const std::string& mail : in.emails) {
    if (is_blank(mail)) continue;
    const std::string addr = trim(mail);
    std::string reason;
    if (!looks_like_email(addr, &reason)) {
      return BuildResult::err("emails", reason);
    }
    out += "EMAIL;TYPE=INTERNET:" + vcard_escape(addr) + "\r\n";
    ++mail_count;
  }
  if (!is_blank(in.url)) {
    const UrlInput url_in{in.url};
    BuildResult url = build_url(url_in);
    if (!url) return BuildResult::err("url", url.error().message);
    out += "URL:" + vcard_escape(url.value()) + "\r\n";
  }
  if (!is_blank(in.address)) {
    out += "ADR;TYPE=HOME:;;" + vcard_escape(trim(in.address)) + "\r\n";
  }
  out += "END:VCARD\r\n";
  if (tel_count == 0 && mail_count == 0 && is_blank(in.url) &&
      is_blank(in.address) && is_blank(in.organization)) {
    return BuildResult::err(
        "full_name",
        "Añade al menos un teléfono, un correo, una URL o una dirección: "
        "solo con el nombre el código no aporta nada.");
  }
  return BuildResult::ok(out);
}

BuildResult build_email(const EmailInput& in) {
  const std::string addr = trim(in.address);
  if (addr.empty()) {
    return BuildResult::err("address", "Escribe la dirección de destino.");
  }
  std::string reason;
  if (!looks_like_email(addr, &reason)) {
    return BuildResult::err("address", reason);
  }
  if (has_nul(in.subject) || has_nul(in.body)) {
    return BuildResult::err("body",
                            "El asunto y el cuerpo no pueden contener bytes "
                            "nulos.");
  }
  std::string out = "mailto:" + addr;
  const bool has_subject = !in.subject.empty();
  const bool has_body = !in.body.empty();
  if (has_subject || has_body) {
    out += "?";
    if (has_subject) {
      out += "subject=" + percent_encode(in.subject);
    }
    if (has_subject && has_body) out += "&";
    if (has_body) {
      out += "body=" + percent_encode(in.body);
    }
  }
  return BuildResult::ok(out);
}

BuildResult build_sms(const SmsInput& in) {
  std::string number;
  if (!normalize_phone(trim(in.number), &number)) {
    return BuildResult::err(
        "number",
        "Número de teléfono no válido. Usa dígitos y, opcionalmente, '+'.");
  }
  if (has_nul(in.message)) {
    return BuildResult::err("message",
                            "El mensaje no puede contener bytes nulos.");
  }
  std::string out = "SMSTO:" + number + ":";
  for (char c : in.message) {
    if (c == '\r' || c == '\n') {
      out += ' ';  // keep the payload single-line
    } else {
      out += c;
    }
  }
  return BuildResult::ok(out);
}

BuildResult build_phone(const PhoneInput& in) {
  std::string number;
  if (!normalize_phone(trim(in.number), &number)) {
    return BuildResult::err(
        "number",
        "Número de teléfono no válido. Usa dígitos y, opcionalmente, '+'.");
  }
  return BuildResult::ok("tel:" + number);
}

BuildResult build_geo(const GeoInput& in) {
  if (std::isnan(in.latitude) || std::isnan(in.longitude) ||
      std::isinf(in.latitude) || std::isinf(in.longitude)) {
    return BuildResult::err("latitude", "Las coordenadas no son un número.");
  }
  if (in.latitude < -90.0 || in.latitude > 90.0) {
    return BuildResult::err("latitude",
                            "La latitud debe estar entre -90 y 90.");
  }
  if (in.longitude < -180.0 || in.longitude > 180.0) {
    return BuildResult::err("longitude",
                            "La longitud debe estar entre -180 y 180.");
  }
  std::string out = "geo:" + format_decimal(in.latitude, 6) + "," +
                    format_decimal(in.longitude, 6);
  if (!is_blank(in.altitude)) {
    const std::string alt = trim(in.altitude);
    char* end = nullptr;
    const double value = std::strtod(alt.c_str(), &end);
    if (end == alt.c_str() || *end != '\0' || std::isnan(value) ||
        std::isinf(value)) {
      return BuildResult::err("altitude",
                              "La altitud debe ser un número (en metros).");
    }
    out += "," + format_decimal(value, 1);
  }
  return BuildResult::ok(out);
}

BuildResult build_event(const EventInput& in) {
  const std::string summary = trim(in.summary);
  if (summary.empty()) {
    return BuildResult::err("summary", "El título del evento es obligatorio.");
  }
  if (!valid_local_datetime(in.start_local)) {
    return BuildResult::err(
        "start_local", "La fecha de inicio debe tener el formato AAAA-MM-DDTHH:MM.");
  }
  if (!valid_local_datetime(in.end_local)) {
    return BuildResult::err(
        "end_local", "La fecha de fin debe tener el formato AAAA-MM-DDTHH:MM.");
  }
  if (in.end_local < in.start_local) {
    return BuildResult::err(
        "end_local", "El evento no puede terminar antes de empezar.");
  }
  if (has_nul(in.location) || has_nul(in.description)) {
    return BuildResult::err("description",
                            "El lugar y la descripción no pueden contener "
                            "bytes nulos.");
  }
  std::string out = "BEGIN:VEVENT\r\n";
  out += "SUMMARY:" + vcard_escape(summary) + "\r\n";
  out += "DTSTART:" + datetime_to_stamp(in.start_local) + "\r\n";
  out += "DTEND:" + datetime_to_stamp(in.end_local) + "\r\n";
  if (!is_blank(in.location)) {
    out += "LOCATION:" + vcard_escape(trim(in.location)) + "\r\n";
  }
  if (!is_blank(in.description)) {
    out += "DESCRIPTION:" + vcard_escape(trim(in.description)) + "\r\n";
  }
  out += "END:VEVENT\r\n";
  return BuildResult::ok(out);
}

}  // namespace payload