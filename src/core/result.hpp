#ifndef QR_CORE_RESULT_HPP_
#define QR_CORE_RESULT_HPP_

#include <optional>
#include <string>
#include <utility>

namespace core {

// Field-level validation failure. `field` is a stable identifier the UI maps
// to the exact widget it belongs to (e.g. "ssid", "password", "latitude"), so
// the user is told precisely which input is wrong. `message` is the text shown
// to the user.
struct ValidationError {
  std::string field;
  std::string message;
};

// Minimal value-or-error carrier used across the whole core. Never throws, so
// invalid user input can never unwind into the render loop.
template <class T>
class Result {
 public:
  static Result ok(T value) {
    Result r;
    r.value_.emplace(std::move(value));
    return r;
  }

  static Result err(std::string field, std::string message) {
    Result r;
    r.error_.emplace(ValidationError{std::move(field), std::move(message)});
    return r;
  }

  static Result err(ValidationError error) {
    Result r;
    r.error_.emplace(std::move(error));
    return r;
  }

  bool has_value() const { return value_.has_value(); }
  explicit operator bool() const { return has_value(); }

  // Precondition: has_value().
  const T& value() const { return *value_; }
  T& value() { return *value_; }

  // Precondition: !has_value().
  const ValidationError& error() const { return *error_; }

  T value_or(T fallback) const {
    return value_.has_value() ? *value_ : std::move(fallback);
  }

 private:
  std::optional<T> value_;
  std::optional<ValidationError> error_;
};

// Value-less result, for operations that either succeed or explain themselves.
template <>
class Result<void> {
 public:
  static Result ok() { return Result(); }

  static Result err(std::string field, std::string message) {
    Result r;
    r.error_.emplace(ValidationError{std::move(field), std::move(message)});
    return r;
  }

  static Result err(ValidationError error) {
    Result r;
    r.error_ = std::move(error);
    return r;
  }

  bool has_value() const { return !error_.has_value(); }
  explicit operator bool() const { return has_value(); }

  // Precondition: !has_value().
  const ValidationError& error() const { return *error_; }

 private:
  std::optional<ValidationError> error_;
};

}  // namespace core

#endif  // QR_CORE_RESULT_HPP_