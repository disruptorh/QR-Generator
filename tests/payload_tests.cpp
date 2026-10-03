#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "payload/builders.hpp"
#include "qr/qr_decoder.hpp"
#include "qr/qr_encoder.hpp"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* what) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL: %s\n", what);
  }
}

void check_eq_str(const std::string& got, const std::string& want, const char* what) {
  ++g_checks;
  if (got != want) {
    ++g_failures;
    std::printf("FAIL: %s\n  got:  %s\n  want: %s\n", what, got.c_str(), want.c_str());
  }
}

void check_err_field(const payload::BuildResult& r, const char* field, const char* what) {
  ++g_checks;
  if (r.has_value()) {
    ++g_failures;
    std::printf("FAIL: %s (expected the error '%s', got '%s')\n", what, field,
                r.value().c_str());
    return;
  }
  if (r.error().field != field) {
    ++g_failures;
    std::printf("FAIL: %s (error field '%s', want '%s')\n", what, r.error().field.c_str(), field);
  }
}

std::string value_of(const payload::BuildResult& r) { return r.value_or("<error>"); }

void test_text() {
  payload::TextInput in;
  in.body = "hola";
  check_eq_str(value_of(payload::build_text(in)), "hola", "text passes through verbatim");
  in.body = "linea1\nlinea2\ttab";
  check_eq_str(value_of(payload::build_text(in)), "linea1\nlinea2\ttab",
               "text keeps newlines and tabs");
  in.body = "\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba";  // UTF-8 accents
  check_eq_str(value_of(payload::build_text(in)), "\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba",
               "UTF-8 text survives");
  in.body.clear();
  check_err_field(payload::build_text(in), "body", "empty text is rejected");
  in.body = std::string("a\0b", 3);
  check_err_field(payload::build_text(in), "body", "an embedded NUL is rejected");
}

void test_url() {
  payload::UrlInput in;
  in.url = "example.com";
  check_eq_str(value_of(payload::build_url(in)), "https://example.com",
               "a bare host gets an https scheme");
  in.url = "  example.com/ruta  ";
  check_eq_str(value_of(payload::build_url(in)), "https://example.com/ruta",
               "surrounding whitespace is trimmed");
  in.url = "http://example.com";
  check_eq_str(value_of(payload::build_url(in)), "http://example.com",
               "an explicit scheme is kept");
  in.url = "HTTPS://EXAMPLE.COM/A";
  check_eq_str(value_of(payload::build_url(in)), "HTTPS://EXAMPLE.COM/A",
               "scheme detection is case insensitive but the text is preserved");
  in.url = "mailto:ada@example.com";
  check_eq_str(value_of(payload::build_url(in)), "mailto:ada@example.com",
               "mailto is a recognized scheme");
  in.url.clear();
  check_err_field(payload::build_url(in), "url", "an empty URL is rejected");
  in.url = "two words here";
  check_err_field(payload::build_url(in), "url", "a URL with spaces is rejected");
  in.url = "example.com\nX-Injected: 1";
  check_err_field(payload::build_url(in), "url", "a URL with a newline is rejected");
}

void test_wifi() {
  payload::WifiInput in;
  in.ssid = "MiRed";
  in.password = "clave123";
  in.auth = payload::WifiAuth::wpa;
  check_eq_str(value_of(payload::build_wifi(in)), "WIFI:T:WPA;S:MiRed;P:clave123;H:false;;",
               "a WPA network is formatted with all fields");
  in.hidden = true;
  check_eq_str(value_of(payload::build_wifi(in)), "WIFI:T:WPA;S:MiRed;P:clave123;H:true;;",
               "a hidden network sets H:true");

  // The five characters WIFI: gives special meaning must be escaped, or the
  // phone reads a truncated or mis-parsed network name.
  in.ssid = "A;B,C:D\\E\"F";
  in.password = "p;q,r:s\\t\"u";
  check_eq_str(value_of(payload::build_wifi(in)),
               "WIFI:T:WPA;S:A\\;B\\,C\\:D\\\\E\\\"F;P:p\\;q\\,r\\:s\\\\t\\\"u;H:true;;",
               "SSID and password special characters are escaped");

  in.ssid = "MiRed";
  in.auth = payload::WifiAuth::sae;
  in.password = "clave";
  check_eq_str(value_of(payload::build_wifi(in)), "WIFI:T:SAE;S:MiRed;P:clave;H:true;;",
               "WPA3 is written as T:SAE");
  in.auth = payload::WifiAuth::wep;
  check_eq_str(value_of(payload::build_wifi(in)), "WIFI:T:WEP;S:MiRed;P:clave;H:true;;",
               "WEP is written as T:WEP");

  in.auth = payload::WifiAuth::nopass;
  in.password = "ignored";
  check_eq_str(value_of(payload::build_wifi(in)), "WIFI:T:nopass;S:MiRed;H:true;;",
               "an open network omits the password entirely");

  in.auth = payload::WifiAuth::wpa;
  in.password.clear();
  check_err_field(payload::build_wifi(in), "password", "a protected network needs a password");
  in.ssid.clear();
  in.password = "x";
  check_err_field(payload::build_wifi(in), "ssid", "the SSID is mandatory");
  in.ssid = std::string(33, 'a');
  check_err_field(payload::build_wifi(in), "ssid", "an SSID longer than 32 bytes is rejected");
  in.ssid = std::string(32, 'a');
  check(payload::build_wifi(in).has_value(), "a 32 byte SSID is accepted");
  in.ssid = "MiRed";
  in.password = std::string(64, 'p');
  check_err_field(payload::build_wifi(in), "password", "a password over 63 bytes is rejected");
}

void test_vcard() {
  payload::ContactInput in;
  in.full_name = "Ada Lovelace";
  check_err_field(payload::build_vcard(in), "full_name",
                  "a vCard with only a name is refused as useless");
  in.phones = {"+34600123456"};
  const std::string card = value_of(payload::build_vcard(in));
  check(card.rfind("BEGIN:VCARD\r\nVERSION:3.0\r\n", 0) == 0, "the vCard opens with its header");
  const std::string vcard_end = "END:VCARD\r\n";
  check(card.size() > vcard_end.size() &&
            card.compare(card.size() - vcard_end.size(), vcard_end.size(), vcard_end) == 0,
        "the vCard ends with END:VCARD");
  check(card.find("FN:Ada Lovelace\r\n") != std::string::npos, "the full name is present");
  check(card.find("N:Ada Lovelace;;;;\r\n") != std::string::npos, "the structured name is present");
  check(card.find("TEL;TYPE=CELL:+34600123456\r\n") != std::string::npos, "the phone is present");
  check(card.find("\r\n") != std::string::npos, "lines end with CRLF");

  in.organization = "Analitica";
  in.title = "Matematica";
  in.emails = {"ada@example.com"};
  in.url = "example.com/ada";
  in.address = "Calle Mayor 1; Madrid";
  const std::string full = value_of(payload::build_vcard(in));
  check(full.find("ORG:Analitica\r\n") != std::string::npos, "the organization is present");
  check(full.find("TITLE:Matematica\r\n") != std::string::npos, "the title is present");
  check(full.find("EMAIL;TYPE=INTERNET:ada@example.com\r\n") != std::string::npos,
        "the email is present");
  check(full.find("URL:https://example.com/ada\r\n") != std::string::npos,
        "the URL is normalized before it is embedded");
  check(full.find("ADR;TYPE=HOME:;;Calle Mayor 1\\; Madrid\r\n") != std::string::npos,
        "the semicolon inside the address is escaped");

  in.full_name = "A;B\\C";
  const std::string escaped = value_of(payload::build_vcard(in));
  check(escaped.find("FN:A\\;B\\\\C\r\n") != std::string::npos,
        "semicolons and backslashes in the name are escaped");

  in.full_name = "Ada";
  in.emails = {"no-at-sign.example.com"};
  check_err_field(payload::build_vcard(in), "emails", "a malformed email is reported");
  in.emails.clear();
  in.phones = {"abc"};
  check_err_field(payload::build_vcard(in), "phones", "a malformed phone is reported");
  in.phones.clear();
  in.full_name = "   ";
  check_err_field(payload::build_vcard(in), "full_name", "a blank name is reported");
}

void test_email() {
  payload::EmailInput in;
  in.address = "ada@example.com";
  check_eq_str(value_of(payload::build_email(in)), "mailto:ada@example.com",
               "a bare address becomes a mailto");
  in.subject = "Reunion";
  check_eq_str(value_of(payload::build_email(in)), "mailto:ada@example.com?subject=Reunion",
               "a subject is appended");
  in.body = "Hola & adios";
  check_eq_str(value_of(payload::build_email(in)),
               "mailto:ada@example.com?subject=Reunion&body=Hola%20%26%20adios",
               "the body is percent-encoded and joined with &");
  in.subject.clear();
  in.body.clear();
  check_eq_str(value_of(payload::build_email(in)), "mailto:ada@example.com",
               "no subject and no body means no query at all");
  in.address = "ada@@example.com";
  check_err_field(payload::build_email(in), "address", "a double @ is rejected");
  in.address = "ada@localhost";
  check_err_field(payload::build_email(in), "address", "a domain without a dot is rejected");
  in.address.clear();
  check_err_field(payload::build_email(in), "address", "an empty address is rejected");
  in.address = "ada@example.com";
  in.body = std::string("x\0y", 3);
  check_err_field(payload::build_email(in), "body", "an embedded NUL is rejected");
}

void test_sms_and_phone() {
  payload::SmsInput sms;
  sms.number = "+34 600 12 34 56";
  sms.message = "Hola";
  check_eq_str(value_of(payload::build_sms(sms)), "SMSTO:+34600123456:Hola",
               "phone separators are stripped from an SMS number");
  sms.message = "linea1\nlinea2";
  check_eq_str(value_of(payload::build_sms(sms)), "SMSTO:+34600123456:linea1 linea2",
               "newlines in an SMS become spaces");
  sms.message.clear();
  check_eq_str(value_of(payload::build_sms(sms)), "SMSTO:+34600123456:",
               "an SMS without a body is still valid");
  sms.number = "600123456";
  sms.message = "hola";
  check_eq_str(value_of(payload::build_sms(sms)), "SMSTO:600123456:hola",
               "a number without + keeps no plus");
  sms.number = "abc";
  check_err_field(payload::build_sms(sms), "number", "a non numeric SMS number is rejected");
  sms.number = "";
  check_err_field(payload::build_sms(sms), "number", "an empty SMS number is rejected");
  sms.number = std::string(16, '9');
  check_err_field(payload::build_sms(sms), "number", "a number over 15 digits is rejected");

  payload::PhoneInput phone;
  phone.number = "(600) 12-34-56";
  check_eq_str(value_of(payload::build_phone(phone)), "tel:600123456",
               "a formatted number is normalized for tel:");
  phone.number = "0";
  check_eq_str(value_of(payload::build_phone(phone)), "tel:0", "a single zero is kept");
  phone.number = "+34";
  check_eq_str(value_of(payload::build_phone(phone)), "tel:+34", "a short number is accepted");
  phone.number = "++3460";
  check_err_field(payload::build_phone(phone), "number", "a doubled + is rejected");
}

void test_geo() {
  payload::GeoInput in;
  in.latitude = 40.4168;
  in.longitude = -3.7038;
  check_eq_str(value_of(payload::build_geo(in)), "geo:40.416800,-3.703800",
               "coordinates use a dot decimal separator");
  in.altitude = "650.5";
  check_eq_str(value_of(payload::build_geo(in)), "geo:40.416800,-3.703800,650.5",
               "an altitude is appended when present");
  in.altitude = "  ";
  check_eq_str(value_of(payload::build_geo(in)), "geo:40.416800,-3.703800",
               "a blank altitude is omitted");
  in.altitude = "abc";
  check_err_field(payload::build_geo(in), "altitude", "a non numeric altitude is rejected");
  in.altitude.clear();
  in.latitude = 91.0;
  check_err_field(payload::build_geo(in), "latitude", "a latitude above 90 is rejected");
  in.latitude = -91.0;
  check_err_field(payload::build_geo(in), "latitude", "a latitude below -90 is rejected");
  in.latitude = 0.0 / 0.0;  // NaN
  check_err_field(payload::build_geo(in), "latitude", "a NaN latitude is rejected");
  in.latitude = 40.0;
  in.longitude = 181.0;
  check_err_field(payload::build_geo(in), "longitude", "a longitude above 180 is rejected");
  in.longitude = -3.7;
  check(payload::build_geo(in).has_value(), "valid coordinates are accepted");
}

// The geo payload is only valid with a dot, and the app is in Spanish: the
// number formatting must not follow LC_NUMERIC.
void test_geo_is_locale_independent() {
  const char* previous = std::setlocale(LC_NUMERIC, nullptr);
  const std::string saved = previous != nullptr ? previous : "C";

  for (const char* locale : {"es_ES.UTF-8", "de_DE.UTF-8", "es_ES.utf8", "C"}) {
    if (std::setlocale(LC_NUMERIC, locale) == nullptr) continue;
    payload::GeoInput in;
    in.latitude = 40.4168;
    in.longitude = -3.7038;
    const std::string got = value_of(payload::build_geo(in));
    ++g_checks;
    if (got != "geo:40.416800,-3.703800") {
      ++g_failures;
      std::printf("FAIL: geo output changed under locale %s: %s\n", locale, got.c_str());
    }
  }
  std::setlocale(LC_NUMERIC, saved.c_str());
}

void test_event() {
  payload::EventInput in;
  in.summary = "Reunion";
  check_err_field(payload::build_event(in), "start_local", "a start date is required");
  in.start_local = "2026-01-15T10:00";
  check_err_field(payload::build_event(in), "end_local", "an end date is required");
  in.end_local = "2026-01-15T11:00";
  const std::string event = value_of(payload::build_event(in));
  check(event.rfind("BEGIN:VEVENT\r\n", 0) == 0, "the event opens with BEGIN:VEVENT");
  check(event.find("SUMMARY:Reunion\r\n") != std::string::npos, "the summary is present");
  // iCalendar wants 20260115T100000, with no dashes, colons or zone suffix.
  check(event.find("DTSTART:20260115T100000\r\n") != std::string::npos,
        "DTSTART is a valid iCalendar local timestamp");
  check(event.find("DTEND:20260115T110000\r\n") != std::string::npos,
        "DTEND is a valid iCalendar local timestamp");
  check(event.find("DTSTART:2026-") == std::string::npos, "DTSTART carries no dashes");
  check(event.find("TZID") == std::string::npos, "floating local time carries no zone");
  check(event.size() > 12 && event.compare(event.size() - 12, 12, "END:VEVENT\r\n") == 0,
        "the event ends with END:VEVENT");

  in.location = "Sala 1";
  in.description = "Con el equipo";
  const std::string full = value_of(payload::build_event(in));
  check(full.find("LOCATION:Sala 1\r\n") != std::string::npos, "the location is present");
  check(full.find("DESCRIPTION:Con el equipo\r\n") != std::string::npos, "the description is present");

  in.summary = "A;B\\C";
  check(value_of(payload::build_event(in)).find("SUMMARY:A\\;B\\\\C\r\n") != std::string::npos,
        "special characters in the summary are escaped");

  in.summary = "Reunion";
  in.end_local = "2026-01-15T09:00";
  check_err_field(payload::build_event(in), "end_local",
                  "an event may not end before it starts");
  in.end_local = "2026-01-15T10:00";  // equal is allowed
  check(payload::build_event(in).has_value(), "an event may end exactly when it starts");

  for (const char* bad : {"2026-01-15", "2026-01-15T10:00:00", "20260115T100000",
                          "2026-13-15T10:00", "2026-01-15T25:00", "1899-01-15T10:00",
                          "2026/01/15T10:00", ""}) {
    in.start_local = bad;
    check_err_field(payload::build_event(in), "start_local",
                    "a malformed start date is rejected");
  }
  in.start_local = "2026-01-15T10:00";
  check(payload::build_event(in).has_value(), "the fixture is valid again");
}

void test_labels() {
  check_eq_str(payload::content_type_label(payload::ContentType::text), "Texto", "text label");
  check_eq_str(payload::content_type_label(payload::ContentType::wifi), "Wi-Fi", "Wi-Fi label");
  check_eq_str(payload::content_type_label(payload::ContentType::vcard),
               "Contacto (vCard)", "vCard label");
  check_eq_str(payload::wifi_auth_label(payload::WifiAuth::nopass), "Abierta (sin contraseña)",
               "open network label");
  check_eq_str(payload::wifi_auth_label(payload::WifiAuth::sae), "WPA3 (SAE)", "WPA3 label");
}

// Every builder's output has to be encodable and must read back byte for byte.
void test_every_payload_survives_the_encoder() {
  std::vector<payload::BuildResult> built;
  payload::TextInput text; text.body = "texto con acentos: \xc3\xa1\xc3\xa9\xc3\xad"; built.push_back(payload::build_text(text));
  payload::UrlInput url; url.url = "https://example.com/a?b=c&d=e"; built.push_back(payload::build_url(url));
  payload::WifiInput wifi; wifi.ssid = "Red;especial"; wifi.password = "clave\\con\"comillas"; built.push_back(payload::build_wifi(wifi));
  payload::ContactInput contact; contact.full_name = "Ada, Lovelace"; contact.phones = {"+34600123456"}; built.push_back(payload::build_vcard(contact));
  payload::EmailInput email; email.address = "ada@example.com"; email.subject = "asunto & otro"; email.body = "cuerpo\ncon saltos"; built.push_back(payload::build_email(email));
  payload::SmsInput sms; sms.number = "+34600123456"; sms.message = "mensaje con accents \xc3\xa9"; built.push_back(payload::build_sms(sms));
  payload::PhoneInput phone; phone.number = "+34600123456"; built.push_back(payload::build_phone(phone));
  payload::GeoInput geo; geo.latitude = -33.8688; geo.longitude = 151.2093; geo.altitude = "58"; built.push_back(payload::build_geo(geo));
  payload::EventInput event; event.summary = "Evento; con escapes"; event.start_local = "2026-02-28T23:59"; event.end_local = "2026-03-01T00:00"; built.push_back(payload::build_event(event));

  for (std::size_t i = 0; i < built.size(); ++i) {
    if (!built[i].has_value()) {
      check(false, "the fixture payload builds");
      continue;
    }
    const std::string& payload_text = built[i].value();
    const auto matrix = qr::encode(payload_text, qr::EncodeOptions{}, nullptr);
    check(matrix.has_value(), "every builder output fits in a QR");
    if (!matrix) continue;
    const auto decoded = qr::decode(*matrix, 0);
    check(decoded.has_value() && decoded->text == payload_text,
          "every builder output reads back byte for byte");
  }
}

}  // namespace

int main() {
  test_text();
  test_url();
  test_wifi();
  test_vcard();
  test_email();
  test_sms_and_phone();
  test_geo();
  test_geo_is_locale_independent();
  test_event();
  test_labels();
  test_every_payload_survives_the_encoder();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}