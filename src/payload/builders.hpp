#ifndef QR_PAYLOAD_BUILDERS_HPP_
#define QR_PAYLOAD_BUILDERS_HPP_

#include <string>

#include "core/result.hpp"
#include "payload/types.hpp"

namespace payload {

using BuildResult = core::Result<std::string>;

// Each builder is a pure function: same input -> same payload, no I/O, no
// globals, no exceptions. A returned error names the offending field so the UI
// can highlight exactly which widget is wrong.

// Plain UTF-8 text, including newlines. Rejects embedded NUL bytes (QR payloads
// are length-prefixed strings; a NUL truncates them in most readers).
BuildResult build_text(const TextInput& in);

// Normalizes a bare domain to "https://<host>"; keeps an explicit scheme as-is.
BuildResult build_url(const UrlInput& in);

// WIFI:T:WPA;S:<ssid>;P:<pass>;H:false;; with \\ ; , : " escaped.
BuildResult build_wifi(const WifiInput& in);

// vCard 3.0, CRLF terminated.
BuildResult build_vcard(const ContactInput& in);

// mailto:<addr>?subject=<pct>&body=<pct>
BuildResult build_email(const EmailInput& in);

// SMSTO:<number>:<message>
BuildResult build_sms(const SmsInput& in);

// tel:<number>
BuildResult build_phone(const PhoneInput& in);

// geo:<lat>,<lon>[,<alt>] with dot decimal separator only.
BuildResult build_geo(const GeoInput& in);

// iCalendar VEVENT (DTSTART/DTEND as local floating time), CRLF terminated.
BuildResult build_event(const EventInput& in);

}  // namespace payload

#endif  // QR_PAYLOAD_BUILDERS_HPP_