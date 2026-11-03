// Strict JSON value, parser and compact serializer used by protocol V1.
// C++11, no exceptions thrown by this code, no RTTI.
//
// Parser rules (match protocol_reference.py): UTF-8 input only, duplicate
// object keys rejected, no NaN/Infinity, no raw control characters inside
// strings, nesting depth limited. The serializer writes the same bytes as
// Python json.dumps(obj, ensure_ascii=False, separators=(',', ':')) for
// strings, integers, booleans, null, arrays and objects (insertion order).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace vp {

struct JsonMember;

class JsonValue {
 public:
  enum Type : uint8_t { kNull, kBool, kInt, kDouble, kString, kArray, kObject };

  JsonValue() : type_(kNull), b_(false), i_(0), d_(0) {}
  // Implicit on purpose: set("ok", true) must produce a JSON boolean, not 1.
  JsonValue(bool v) : type_(kBool), b_(v), i_(0), d_(0) {}
  JsonValue(int v) : type_(kInt), b_(false), i_(v), d_(v) {}
  JsonValue(unsigned v) : type_(kInt), b_(false), i_(v), d_(v) {}
  JsonValue(long v) : type_(kInt), b_(false), i_(v), d_(static_cast<double>(v)) {}
  JsonValue(unsigned long v) : type_(kInt), b_(false), i_(static_cast<int64_t>(v)), d_(static_cast<double>(v)) {}
  JsonValue(long long v) : type_(kInt), b_(false), i_(v), d_(static_cast<double>(v)) {}
  JsonValue(unsigned long long v) : type_(kInt), b_(false), i_(static_cast<int64_t>(v)), d_(static_cast<double>(v)) {}
  JsonValue(double v) : type_(kDouble), b_(false), i_(0), d_(v) {}
  JsonValue(const char* s) : type_(kString), b_(false), i_(0), d_(0), s_(s ? s : "") {}
  JsonValue(const std::string& s) : type_(kString), b_(false), i_(0), d_(0), s_(s) {}

  static JsonValue makeArray();
  static JsonValue makeObject();

  Type type() const { return type_; }
  bool isNull() const { return type_ == kNull; }
  bool isBool() const { return type_ == kBool; }
  bool isNumber() const { return type_ == kInt || type_ == kDouble; }
  // True for integers and for doubles holding an exact integer value.
  bool isIntegral() const;
  bool isString() const { return type_ == kString; }
  bool isArray() const { return type_ == kArray; }
  bool isObject() const { return type_ == kObject; }

  bool asBool(bool def = false) const { return type_ == kBool ? b_ : def; }
  int64_t asInt(int64_t def = 0) const;
  double asDouble(double def = 0) const;
  const std::string& asString() const;

  // Object access. find() returns nullptr when absent or when this is not an object.
  const JsonValue* find(const char* key) const;
  JsonValue* find(const char* key);
  // Replaces an existing key or appends a new one. Converts null to object.
  JsonValue& set(const std::string& key, const JsonValue& value);
  const std::vector<JsonMember>& members() const { return o_; }

  // Array access. Converts null to array on push.
  void push(const JsonValue& value);
  size_t size() const;
  const JsonValue& at(size_t index) const;
  const std::vector<JsonValue>& items() const { return a_; }

  // Typed lookups on objects; return false when the key is absent or the type differs.
  bool getString(const char* key, std::string& out) const;
  bool getBool(const char* key, bool& out) const;
  bool getInt(const char* key, int64_t& out) const;

 private:
  friend class JsonParser;
  Type type_;
  bool b_;
  int64_t i_;
  double d_;
  std::string s_;
  std::vector<JsonValue> a_;
  std::vector<JsonMember> o_;
};

struct JsonMember {
  std::string key;
  JsonValue value;
};

// Parses exactly one JSON value surrounded by optional whitespace.
bool jsonParse(const char* text, size_t len, JsonValue& out, std::string* error = nullptr,
               int maxDepth = 16);
inline bool jsonParse(const std::string& text, JsonValue& out, std::string* error = nullptr,
                      int maxDepth = 16) {
  return jsonParse(text.data(), text.size(), out, error, maxDepth);
}

void jsonSerializeTo(const JsonValue& value, std::string& out);
std::string jsonSerialize(const JsonValue& value);
// Like jsonSerialize but returns false when a non-finite number was found
// (written as null), mirroring json.dumps(..., allow_nan=False).
bool jsonSerializeChecked(const JsonValue& value, std::string& out);
// Appends a JSON string literal (with quotes) for raw UTF-8 text.
void jsonAppendQuoted(const std::string& text, std::string& out);

bool utf8IsValid(const char* s, size_t n);

}  // namespace vp
