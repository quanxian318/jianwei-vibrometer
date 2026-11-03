#include "vp_json.h"

#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace vp {

namespace {
const std::string kEmptyString;
const JsonValue kNullValue;
}  // namespace

JsonValue JsonValue::makeArray() {
  JsonValue v;
  v.type_ = kArray;
  return v;
}

JsonValue JsonValue::makeObject() {
  JsonValue v;
  v.type_ = kObject;
  return v;
}

bool JsonValue::isIntegral() const {
  if (type_ == kInt) return true;
  if (type_ != kDouble) return false;
  return std::isfinite(d_) && d_ == std::floor(d_) && std::fabs(d_) <= 9007199254740992.0;
}

int64_t JsonValue::asInt(int64_t def) const {
  if (type_ == kInt) return i_;
  if (type_ == kDouble && isIntegral()) return static_cast<int64_t>(d_);
  return def;
}

double JsonValue::asDouble(double def) const {
  if (type_ == kInt) return static_cast<double>(i_);
  if (type_ == kDouble) return d_;
  return def;
}

const std::string& JsonValue::asString() const { return type_ == kString ? s_ : kEmptyString; }

const JsonValue* JsonValue::find(const char* key) const {
  if (type_ != kObject || key == nullptr) return nullptr;
  for (size_t i = 0; i < o_.size(); ++i) {
    if (o_[i].key == key) return &o_[i].value;
  }
  return nullptr;
}

JsonValue* JsonValue::find(const char* key) {
  if (type_ != kObject || key == nullptr) return nullptr;
  for (size_t i = 0; i < o_.size(); ++i) {
    if (o_[i].key == key) return &o_[i].value;
  }
  return nullptr;
}

JsonValue& JsonValue::set(const std::string& key, const JsonValue& value) {
  if (type_ != kObject) {
    JsonValue fresh = makeObject();
    *this = fresh;
  }
  for (size_t i = 0; i < o_.size(); ++i) {
    if (o_[i].key == key) {
      o_[i].value = value;
      return o_[i].value;
    }
  }
  JsonMember m;
  m.key = key;
  m.value = value;
  o_.push_back(m);
  return o_.back().value;
}

void JsonValue::push(const JsonValue& value) {
  if (type_ != kArray) {
    JsonValue fresh = makeArray();
    *this = fresh;
  }
  a_.push_back(value);
}

size_t JsonValue::size() const {
  if (type_ == kArray) return a_.size();
  if (type_ == kObject) return o_.size();
  return 0;
}

const JsonValue& JsonValue::at(size_t index) const {
  if (type_ != kArray || index >= a_.size()) return kNullValue;
  return a_[index];
}

bool JsonValue::getString(const char* key, std::string& out) const {
  const JsonValue* v = find(key);
  if (v == nullptr || !v->isString()) return false;
  out = v->s_;
  return true;
}

bool JsonValue::getBool(const char* key, bool& out) const {
  const JsonValue* v = find(key);
  if (v == nullptr || !v->isBool()) return false;
  out = v->b_;
  return true;
}

bool JsonValue::getInt(const char* key, int64_t& out) const {
  const JsonValue* v = find(key);
  if (v == nullptr || !v->isIntegral()) return false;
  out = v->asInt();
  return true;
}

bool utf8IsValid(const char* s, size_t n) {
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
  size_t i = 0;
  while (i < n) {
    unsigned char c = p[i];
    if (c < 0x80) {
      ++i;
      continue;
    }
    size_t need;
    uint32_t cp;
    if (c >= 0xC2 && c <= 0xDF) {
      need = 1;
      cp = c & 0x1F;
    } else if (c >= 0xE0 && c <= 0xEF) {
      need = 2;
      cp = c & 0x0F;
    } else if (c >= 0xF0 && c <= 0xF4) {
      need = 3;
      cp = c & 0x07;
    } else {
      return false;
    }
    if (i + need >= n) return false;  // truncated sequence
    for (size_t k = 1; k <= need; ++k) {
      unsigned char cc = p[i + k];
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    if ((need == 2 && cp < 0x800) || (need == 3 && cp < 0x10000)) return false;  // overlong
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;                             // surrogate
    if (cp > 0x10FFFF) return false;
    i += need + 1;
  }
  return true;
}

class JsonParser {
 public:
  JsonParser(const char* p, size_t n, int maxDepth)
      : begin_(p), p_(p), end_(p + n), maxDepth_(maxDepth) {}

  bool run(JsonValue& out, std::string* err) {
    bool ok = true;
    if (!utf8IsValid(begin_, static_cast<size_t>(end_ - begin_))) {
      ok = fail("invalid UTF-8");
    } else {
      skipWs();
      ok = parseValue(out, 0);
      if (ok) {
        skipWs();
        if (p_ != end_) ok = fail("trailing data");
      }
    }
    if (!ok && err != nullptr) {
      char buf[48];
      snprintf(buf, sizeof(buf), " at offset %u", static_cast<unsigned>(p_ - begin_));
      *err = error_ + buf;
    }
    return ok;
  }

 private:
  const char* begin_;
  const char* p_;
  const char* end_;
  int maxDepth_;
  std::string error_;

  bool fail(const char* msg) {
    if (error_.empty()) error_ = msg;
    return false;
  }

  void skipWs() {
    while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
  }

  bool literal(const char* word, size_t len) {
    if (static_cast<size_t>(end_ - p_) < len || memcmp(p_, word, len) != 0) {
      return fail("invalid literal");
    }
    p_ += len;
    return true;
  }

  bool parseValue(JsonValue& v, int depth) {
    if (p_ >= end_) return fail("unexpected end");
    char c = *p_;
    if (c == '{') return parseObject(v, depth + 1);
    if (c == '[') return parseArray(v, depth + 1);
    if (c == '"') {
      v = JsonValue();
      v.type_ = JsonValue::kString;
      return parseString(v.s_);
    }
    if (c == 't') {
      if (!literal("true", 4)) return false;
      v = JsonValue(true);
      return true;
    }
    if (c == 'f') {
      if (!literal("false", 5)) return false;
      v = JsonValue(false);
      return true;
    }
    if (c == 'n') {
      if (!literal("null", 4)) return false;
      v = JsonValue();
      return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(v);
    return fail("unexpected character");
  }

  bool parseObject(JsonValue& v, int depth) {
    if (depth > maxDepth_) return fail("nesting too deep");
    ++p_;  // '{'
    v = JsonValue::makeObject();
    skipWs();
    if (p_ < end_ && *p_ == '}') {
      ++p_;
      return true;
    }
    for (;;) {
      skipWs();
      if (p_ >= end_ || *p_ != '"') return fail("expected object key");
      std::string key;
      if (!parseString(key)) return false;
      for (size_t i = 0; i < v.o_.size(); ++i) {
        if (v.o_[i].key == key) return fail("duplicate key");
      }
      skipWs();
      if (p_ >= end_ || *p_ != ':') return fail("expected ':'");
      ++p_;
      skipWs();
      JsonMember member;
      member.key.swap(key);
      v.o_.push_back(member);
      if (!parseValue(v.o_.back().value, depth)) return false;
      skipWs();
      if (p_ < end_ && *p_ == ',') {
        ++p_;
        continue;
      }
      if (p_ < end_ && *p_ == '}') {
        ++p_;
        return true;
      }
      return fail("expected ',' or '}'");
    }
  }

  bool parseArray(JsonValue& v, int depth) {
    if (depth > maxDepth_) return fail("nesting too deep");
    ++p_;  // '['
    v = JsonValue::makeArray();
    skipWs();
    if (p_ < end_ && *p_ == ']') {
      ++p_;
      return true;
    }
    for (;;) {
      skipWs();
      v.a_.push_back(JsonValue());
      if (!parseValue(v.a_.back(), depth)) return false;
      skipWs();
      if (p_ < end_ && *p_ == ',') {
        ++p_;
        continue;
      }
      if (p_ < end_ && *p_ == ']') {
        ++p_;
        return true;
      }
      return fail("expected ',' or ']'");
    }
  }

  static int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  bool readHex4(uint32_t& out) {
    if (end_ - p_ < 4) return fail("short \\u escape");
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      int h = hexValue(p_[i]);
      if (h < 0) return fail("bad \\u escape");
      v = (v << 4) | static_cast<uint32_t>(h);
    }
    p_ += 4;
    out = v;
    return true;
  }

  static void appendUtf8(uint32_t cp, std::string& out) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  bool parseString(std::string& out) {
    ++p_;  // opening quote
    out.clear();
    for (;;) {
      if (p_ >= end_) return fail("unterminated string");
      unsigned char c = static_cast<unsigned char>(*p_);
      if (c == '"') {
        ++p_;
        return true;
      }
      if (c < 0x20) return fail("control character in string");
      if (c != '\\') {
        out += static_cast<char>(c);
        ++p_;
        continue;
      }
      ++p_;
      if (p_ >= end_) return fail("unterminated escape");
      char e = *p_++;
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          uint32_t cp;
          if (!readHex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (end_ - p_ < 6 || p_[0] != '\\' || p_[1] != 'u') return fail("lone surrogate");
            p_ += 2;
            uint32_t lo;
            if (!readHex4(lo)) return false;
            if (lo < 0xDC00 || lo > 0xDFFF) return fail("lone surrogate");
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return fail("lone surrogate");
          }
          appendUtf8(cp, out);
          break;
        }
        default:
          return fail("invalid escape");
      }
    }
  }

  static bool isDigit(char c) { return c >= '0' && c <= '9'; }

  bool parseNumber(JsonValue& v) {
    const char* start = p_;
    bool negative = false;
    if (*p_ == '-') {
      negative = true;
      ++p_;
    }
    if (p_ >= end_) return fail("bad number");
    if (*p_ == '0') {
      ++p_;
    } else if (*p_ >= '1' && *p_ <= '9') {
      while (p_ < end_ && isDigit(*p_)) ++p_;
    } else {
      return fail("bad number");
    }
    bool integral = true;
    if (p_ < end_ && *p_ == '.') {
      integral = false;
      ++p_;
      if (p_ >= end_ || !isDigit(*p_)) return fail("bad fraction");
      while (p_ < end_ && isDigit(*p_)) ++p_;
    }
    if (p_ < end_ && (*p_ == 'e' || *p_ == 'E')) {
      integral = false;
      ++p_;
      if (p_ < end_ && (*p_ == '+' || *p_ == '-')) ++p_;
      if (p_ >= end_ || !isDigit(*p_)) return fail("bad exponent");
      while (p_ < end_ && isDigit(*p_)) ++p_;
    }
    std::string token(start, p_);
    if (integral) {
      // Accumulate the magnitude; fall back to double when it exceeds int64.
      uint64_t mag = 0;
      bool overflow = false;
      for (size_t i = negative ? 1 : 0; i < token.size(); ++i) {
        uint64_t digit = static_cast<uint64_t>(token[i] - '0');
        if (mag > (UINT64_MAX - digit) / 10) {
          overflow = true;
          break;
        }
        mag = mag * 10 + digit;
      }
      const uint64_t limit = negative ? (static_cast<uint64_t>(INT64_MAX) + 1) : static_cast<uint64_t>(INT64_MAX);
      if (!overflow && mag <= limit) {
        int64_t iv = negative ? static_cast<int64_t>(0 - mag) : static_cast<int64_t>(mag);
        v = JsonValue(static_cast<long long>(iv));
        return true;
      }
    }
    char* endp = nullptr;
    double d = strtod(token.c_str(), &endp);
    if (endp == nullptr || *endp != '\0' || !std::isfinite(d)) return fail("number out of range");
    v = JsonValue(d);
    return true;
  }
};

bool jsonParse(const char* text, size_t len, JsonValue& out, std::string* error, int maxDepth) {
  JsonParser parser(text, len, maxDepth);
  JsonValue result;
  if (!parser.run(result, error)) return false;
  out = result;
  return true;
}

void jsonAppendQuoted(const std::string& text, std::string& out) {
  static const char kHex[] = "0123456789abcdef";
  out += '"';
  for (size_t i = 0; i < text.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          out += "\\u00";
          out += kHex[c >> 4];
          out += kHex[c & 0x0F];
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

namespace {

void appendInt(int64_t v, std::string& out) {
  char buf[24];
  char* p = buf + sizeof(buf);
  *--p = '\0';
  uint64_t mag = v < 0 ? (0 - static_cast<uint64_t>(v)) : static_cast<uint64_t>(v);
  do {
    *--p = static_cast<char>('0' + (mag % 10));
    mag /= 10;
  } while (mag != 0);
  if (v < 0) *--p = '-';
  out += p;
}

// Shortest of %.15g/%.16g/%.17g that round-trips, then ".0" for integral
// values, which matches Python float repr for ordinary magnitudes.
void appendDouble(double d, std::string& out) {
  char buf[40];
  for (int prec = 15; prec <= 17; ++prec) {
    snprintf(buf, sizeof(buf), "%.*g", prec, d);
    if (strtod(buf, nullptr) == d) break;
  }
  out += buf;
  if (strpbrk(buf, ".eEni") == nullptr) out += ".0";
}

bool serializeImpl(const JsonValue& v, std::string& out) {
  bool ok = true;
  switch (v.type()) {
    case JsonValue::kNull:
      out += "null";
      break;
    case JsonValue::kBool:
      out += v.asBool() ? "true" : "false";
      break;
    case JsonValue::kInt:
      appendInt(v.asInt(), out);
      break;
    case JsonValue::kDouble: {
      double d = v.asDouble();
      if (!std::isfinite(d)) {
        out += "null";
        ok = false;
      } else {
        appendDouble(d, out);
      }
      break;
    }
    case JsonValue::kString:
      jsonAppendQuoted(v.asString(), out);
      break;
    case JsonValue::kArray: {
      out += '[';
      const std::vector<JsonValue>& items = v.items();
      for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ',';
        if (!serializeImpl(items[i], out)) ok = false;
      }
      out += ']';
      break;
    }
    case JsonValue::kObject: {
      out += '{';
      const std::vector<JsonMember>& members = v.members();
      for (size_t i = 0; i < members.size(); ++i) {
        if (i) out += ',';
        jsonAppendQuoted(members[i].key, out);
        out += ':';
        if (!serializeImpl(members[i].value, out)) ok = false;
      }
      out += '}';
      break;
    }
  }
  return ok;
}

}  // namespace

void jsonSerializeTo(const JsonValue& value, std::string& out) { serializeImpl(value, out); }

bool jsonSerializeChecked(const JsonValue& value, std::string& out) {
  out.clear();
  return serializeImpl(value, out);
}

std::string jsonSerialize(const JsonValue& value) {
  std::string out;
  serializeImpl(value, out);
  return out;
}

}  // namespace vp
