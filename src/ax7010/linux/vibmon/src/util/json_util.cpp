#include "util/json_util.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace vm {

namespace {
const JsonValue kNull;
}

bool readTextFile(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
  const std::string tmp = path + ".tmp";
  int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) return false;
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = ::write(fd, data.data() + off, data.size() - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      ::close(fd);
      ::unlink(tmp.c_str());
      return false;
    }
    off += static_cast<size_t>(n);
  }
  bool ok = ::fsync(fd) == 0;
  ok = (::close(fd) == 0) && ok;
  if (!ok || ::rename(tmp.c_str(), path.c_str()) != 0) {
    ::unlink(tmp.c_str());
    return false;
  }
  return true;
}

bool makeDirs(const std::string& path) {
  if (path.empty()) return false;
  std::string cur;
  for (size_t i = 0; i < path.size(); ++i) {
    cur += path[i];
    if (path[i] == '/' || i + 1 == path.size()) {
      if (cur == "/") continue;
      if (::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
  }
  return true;
}

bool loadJsonFile(const std::string& path, JsonValue& out, std::string* err) {
  std::string text;
  if (!readTextFile(path, text)) {
    if (err) *err = "cannot read " + path;
    return false;
  }
  std::string e;
  if (!vp::jsonParse(text, out, &e, 32)) {
    if (err) *err = path + ": " + e;
    return false;
  }
  return true;
}

namespace {
void pretty(const JsonValue& v, int indent, int depth, std::string& out) {
  const std::string pad(static_cast<size_t>(indent * (depth + 1)), ' ');
  const std::string padEnd(static_cast<size_t>(indent * depth), ' ');
  if (v.isObject() && v.size() > 0) {
    out += "{\n";
    const auto& m = v.members();
    for (size_t i = 0; i < m.size(); ++i) {
      out += pad;
      vp::jsonAppendQuoted(m[i].key, out);
      out += ": ";
      pretty(m[i].value, indent, depth + 1, out);
      if (i + 1 < m.size()) out += ',';
      out += '\n';
    }
    out += padEnd + "}";
  } else if (v.isArray() && v.size() > 0) {
    bool scalars = true;
    for (const auto& it : v.items()) scalars = scalars && !it.isObject() && !it.isArray();
    if (scalars) {
      out += vp::jsonSerialize(v);
      return;
    }
    out += "[\n";
    const auto& a = v.items();
    for (size_t i = 0; i < a.size(); ++i) {
      out += pad;
      pretty(a[i], indent, depth + 1, out);
      if (i + 1 < a.size()) out += ',';
      out += '\n';
    }
    out += padEnd + "]";
  } else {
    out += vp::jsonSerialize(v);
  }
}
}  // namespace

std::string jsonPretty(const JsonValue& v, int indent) {
  std::string out;
  pretty(v, indent, 0, out);
  out += '\n';
  return out;
}

double jNum(const JsonValue& obj, const char* key, double def) {
  const JsonValue* v = obj.find(key);
  return (v && v->isNumber()) ? v->asDouble() : def;
}

int64_t jInt(const JsonValue& obj, const char* key, int64_t def) {
  const JsonValue* v = obj.find(key);
  return (v && v->isIntegral()) ? v->asInt() : def;
}

bool jBool(const JsonValue& obj, const char* key, bool def) {
  const JsonValue* v = obj.find(key);
  return (v && v->isBool()) ? v->asBool() : def;
}

std::string jStr(const JsonValue& obj, const char* key, const std::string& def) {
  const JsonValue* v = obj.find(key);
  return (v && v->isString()) ? v->asString() : def;
}

const JsonValue& jObj(const JsonValue& obj, const char* key) {
  const JsonValue* v = obj.find(key);
  return v ? *v : kNull;
}

uint64_t jAddr(const JsonValue& obj, const char* key, uint64_t def) {
  const JsonValue* v = obj.find(key);
  if (!v) return def;
  if (v->isIntegral()) return static_cast<uint64_t>(v->asInt());
  if (v->isString()) {
    const std::string& s = v->asString();
    char* end = nullptr;
    unsigned long long x = strtoull(s.c_str(), &end, 0);
    if (end && *end == 0 && !s.empty()) return x;
  }
  return def;
}

}  // namespace vm
