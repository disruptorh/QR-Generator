#ifndef QR_PAYLOAD_TYPES_HPP_
#define QR_PAYLOAD_TYPES_HPP_

#include <cstdint>
#include <string>
#include <vector>

namespace payload {

// The content types the generator supports. `Text` is the fallback that accepts
// any pre-built payload verbatim.
enum class ContentType {
  text,
  url,
  wifi,
  vcard,
  email,
  sms,
  phone,
  geo,
  event,
};

// Human label shown in the type selector.
const char* content_type_label(ContentType type);

// Wi-Fi authentication as written into the WIFI: payload.
enum class WifiAuth {
  wpa,     // WPA / WPA2 (T:WPA)
  sae,     // WPA3 (T:SAE)
  wep,     // WEP (T:WEP)
  nopass,  // Open network: password omitted entirely (T:nopass)
};

const char* wifi_auth_label(WifiAuth auth);

struct TextInput {
  std::string body;  // UTF-8, may contain newlines
};

struct UrlInput {
  std::string url;
};

struct WifiInput {
  std::string ssid;
  std::string password;
  WifiAuth auth = WifiAuth::wpa;
  bool hidden = false;
};

struct ContactInput {
  std::string full_name;
  std::string organization;
  std::string title;
  std::vector<std::string> phones;
  std::vector<std::string> emails;
  std::string url;
  std::string address;  // free-form, street/city/... joined by "; " in the value
};

struct EmailInput {
  std::string address;
  std::string subject;
  std::string body;
};

struct SmsInput {
  std::string number;
  std::string message;
};

struct PhoneInput {
  std::string number;
};

struct GeoInput {
  double latitude = 0.0;
  double longitude = 0.0;
  std::string altitude;  // optional; empty means omitted
};

struct EventInput {
  std::string summary;
  std::string location;
  std::string description;
  // Compact local datetime, "YYYY-MM-DDTHH:MM" (no seconds, no zone suffix).
  std::string start_local;
  std::string end_local;
};

}  // namespace payload

#endif  // QR_PAYLOAD_TYPES_HPP_