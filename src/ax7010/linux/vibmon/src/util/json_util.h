#pragma once

#include <cstdint>
#include <string>

#include "vibproto.h"

namespace vm {

using vp::JsonValue;

bool readTextFile(const std::string& path, std::string& out);
// Writes path.tmp, fsyncs, renames; the old file stays intact on failure.
bool writeFileAtomic(const std::string& path, const std::string& data);
bool makeDirs(const std::string& path);
bool loadJsonFile(const std::string& path, JsonValue& out, std::string* err);
std::string jsonPretty(const JsonValue& v, int indent = 2);

// Typed lookups with defaults (obj may be any value; non-objects yield defaults).
double jNum(const JsonValue& obj, const char* key, double def);
int64_t jInt(const JsonValue& obj, const char* key, int64_t def);
bool jBool(const JsonValue& obj, const char* key, bool def);
std::string jStr(const JsonValue& obj, const char* key, const std::string& def);
const JsonValue& jObj(const JsonValue& obj, const char* key);  // null value when absent
// Hex or decimal integer stored as string ("0x44000000") or number.
uint64_t jAddr(const JsonValue& obj, const char* key, uint64_t def);

}  // namespace vm
