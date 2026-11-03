// Host test for the shared protocol code. Build (Linux/WSL):
//   g++ -std=c++11 -Wall -Wextra -O1 -I../src test_vibproto.cpp ../src/*.cpp -o test_vibproto
//   ./test_vibproto <protocol_vectors.json> [random_vectors.json]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "vibproto.h"

using namespace vp;

static int g_failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                    \
    }                                                                  \
  } while (0)

static std::string readFile(const char* path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static std::string fromHex(const std::string& hex) {
  std::string out;
  int hi = -1;
  for (char c : hex) {
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else continue;
    if (hi < 0) {
      hi = v;
    } else {
      out += static_cast<char>((hi << 4) | v);
      hi = -1;
    }
  }
  return out;
}

static std::vector<Frame> decodeAll(StreamDecoder& d, const std::string& wire, size_t piece) {
  std::vector<Frame> got;
  for (size_t off = 0; off < wire.size(); off += piece) {
    size_t n = std::min(piece, wire.size() - off);
    d.feed(reinterpret_cast<const uint8_t*>(wire.data()) + off, n,
           [&](const Frame& f) { got.push_back(f); });
  }
  return got;
}

static bool sameFrame(const Frame& a, const Frame& b) {
  return a.kind == b.kind && a.opcode == b.opcode && a.seq == b.seq && a.payloadJson == b.payloadJson;
}

static void testJson() {
  JsonValue v;
  std::string err;
  CHECK(jsonParse(std::string("{\"a\":1,\"b\":[true,false,null],\"c\":\"x\\u00e9\\ud83d\\ude00\"}"), v, &err));
  CHECK(v.isObject() && v.find("a")->asInt() == 1);
  CHECK(v.find("c")->asString() == "x\xc3\xa9\xf0\x9f\x98\x80");
  CHECK(!jsonParse(std::string("{\"a\":1,\"a\":2}"), v));            // duplicate key
  CHECK(!jsonParse(std::string("{\"a\":NaN}"), v));                  // NaN literal
  CHECK(!jsonParse(std::string("{\"a\":Infinity}"), v));
  CHECK(!jsonParse(std::string("{\"a\":1,}"), v));                   // trailing comma
  CHECK(!jsonParse(std::string("{\"a\":\"\x01\"}"), v));             // raw control char
  CHECK(!jsonParse(std::string("{\"a\":\"\xc3\x28\"}"), v));         // bad UTF-8
  CHECK(!jsonParse(std::string("{\"a\":\"\xed\xa0\x80\"}"), v));     // encoded surrogate
  CHECK(!jsonParse(std::string("{\"a\":01}"), v));                   // leading zero
  CHECK(!jsonParse(std::string("{\"a\":1} x"), v));                  // trailing data
  CHECK(jsonParse(std::string(" \r\n{ \"a\" : -0.5e2 } \t"), v) && v.find("a")->asDouble() == -50.0);
  CHECK(jsonParse(std::string("{\"big\":123456789012345678901234567890}"), v) &&
        v.find("big")->type() == JsonValue::kDouble);
  CHECK(jsonParse(std::string("{\"m\":-9223372036854775808}"), v) &&
        v.find("m")->asInt() == INT64_MIN);
  std::string deep;
  for (int i = 0; i < 20; ++i) deep += "[";
  for (int i = 0; i < 20; ++i) deep += "]";
  CHECK(!jsonParse(deep, v));  // depth limit

  JsonValue o = JsonValue::makeObject();
  o.set("ok", true);
  o.set("n", 3);
  o.set("d", 2.0);
  o.set("e", 1e-05);
  o.set("s", std::string("q\"\\\n\x1f测"));
  CHECK(jsonSerialize(o) == "{\"ok\":true,\"n\":3,\"d\":2.0,\"e\":1e-05,\"s\":\"q\\\"\\\\\\n\\u001f测\"}");
  std::string out;
  JsonValue bad = JsonValue::makeObject();
  bad.set("x", std::nan(""));
  CHECK(!jsonSerializeChecked(bad, out));
}

static void testVectors(const char* path, std::vector<Frame>& expected, std::string& wire) {
  JsonValue root;
  std::string err;
  std::string text = readFile(path);
  CHECK(jsonParse(text, root, &err, 32));
  CHECK(root.isArray() && root.size() == 4);
  for (size_t i = 0; i < root.size(); ++i) {
    const JsonValue& c = root.at(i);
    std::string bytes = fromHex(c.find("hex")->asString());
    std::string mine;
    CHECK(encodeFrame(static_cast<uint8_t>(c.find("kind")->asInt()), static_cast<uint8_t>(c.find("opcode")->asInt()),
                      static_cast<uint16_t>(c.find("seq")->asInt()), *c.find("payload"), mine));
    if (mine != bytes) fprintf(stderr, "vector %s differs\n", c.find("name")->asString().c_str());
    CHECK(mine == bytes);
    Frame f;
    CHECK(decodeFrame(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), f) == kDecodeOk);
    expected.push_back(f);
    wire += bytes;
  }
}

static void testStream(const std::vector<Frame>& expected, const std::string& wire) {
  {
    StreamDecoder d;
    std::vector<Frame> got = decodeAll(d, wire, wire.size());
    CHECK(got.size() == expected.size());
    for (size_t i = 0; i < got.size() && i < expected.size(); ++i) CHECK(sameFrame(got[i], expected[i]));
  }
  std::mt19937 rng(7);
  for (int seed = 0; seed < 50; ++seed) {
    StreamDecoder d;
    std::vector<Frame> got;
    size_t off = 0;
    while (off < wire.size()) {
      size_t n = 1 + rng() % 39;
      if (off + n > wire.size()) n = wire.size() - off;
      d.feed(reinterpret_cast<const uint8_t*>(wire.data()) + off, n, [&](const Frame& f) { got.push_back(f); });
      off += n;
    }
    CHECK(got.size() == expected.size());
  }
  {  // noise + corrupted copy of the first frame + clean wire
    std::string first = wire.substr(0, kFrameOverhead + expected[0].payloadJson.size());
    std::string bad = first;
    bad[10] ^= 1;
    StreamDecoder d;
    std::vector<Frame> got = decodeAll(d, std::string("noise\xa5") + bad + wire, 7);
    CHECK(got.size() == expected.size());
    CHECK(d.totalErrors() > 0);
  }
  {  // oversize header must be skipped
    std::string over;
    over += '\xa5'; over += '\x5a'; over += '\x01'; over += '\x01'; over += '\x01'; over += '\x00';
    over += '\x01'; over += '\x00'; over += static_cast<char>(1025 & 0xFF); over += static_cast<char>(1025 >> 8);
    std::string first = wire.substr(0, kFrameOverhead + expected[0].payloadJson.size());
    StreamDecoder d;
    std::vector<Frame> got = decodeAll(d, over + first, 1000);
    CHECK(got.size() == 1 && sameFrame(got[0], expected[0]));
  }
  {  // partial frame then deadline expiry
    std::string first = wire.substr(0, kFrameOverhead + expected[0].payloadJson.size());
    StreamDecoder d;
    CHECK(decodeAll(d, first.substr(0, 11), 64).empty());
    CHECK(d.expirePartial());
    std::vector<Frame> got = decodeAll(d, first, 64);
    CHECK(got.size() == 1);
  }
  {  // max payload edge
    JsonValue p = JsonValue::makeObject();
    p.set("x", std::string(1016, 'a'));
    std::string frame;
    CHECK(encodeFrame(kKindRequest, 1, 9, p, frame) && frame.size() == 1036);
    Frame f;
    CHECK(decodeFrame(reinterpret_cast<const uint8_t*>(frame.data()), frame.size(), f) == kDecodeOk);
    p.set("x", std::string(1017, 'a'));
    CHECK(!encodeFrame(kKindRequest, 1, 9, p, frame));
    JsonValue n = JsonValue::makeObject();
    n.set("x", std::nan(""));
    CHECK(!encodeFrame(kKindRequest, 1, 1, n, frame));
    CHECK(!encodeFrame(kKindEvent, 0x40, 1, JsonValue::makeObject(), frame));   // events need seq 0
    CHECK(!encodeFrame(kKindRequest, 1, 0, JsonValue::makeObject(), frame));    // requests need seq >= 1
  }
  {  // duplicate keys rejected inside a CRC-valid frame
    std::string dup;
    CHECK(encodeFrameRaw(kKindRequest, 1, 1, "{\"x\":1,\"x\":2}", dup));
    std::string first = wire.substr(0, kFrameOverhead + expected[0].payloadJson.size());
    StreamDecoder d;
    std::vector<Frame> got = decodeAll(d, dup + first, 3);
    CHECK(got.size() == 1 && sameFrame(got[0], expected[0]));
    CHECK(d.stats().jsonErrors == 1);
  }
  {  // continuous noise keeps nothing
    StreamDecoder d;
    std::string noise(100000, 'x');
    CHECK(decodeAll(d, noise, 4096).empty());
    CHECK(!d.hasPartial());
  }
}

static void testRandom(const char* path) {
  JsonValue root;
  std::string err;
  CHECK(jsonParse(readFile(path), root, &err, 32));
  size_t n = root.size();
  std::string wire;
  std::vector<std::string> bodies;
  for (size_t i = 0; i < n; ++i) {
    const JsonValue& c = root.at(i);
    std::string bytes = fromHex(c.find("hex")->asString());
    const std::string& body = c.find("payload_json")->asString();
    Frame f;
    DecodeResult r = decodeFrame(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), f);
    CHECK(r == kDecodeOk);
    if (r != kDecodeOk) continue;
    CHECK(f.payloadJson == body);
    // Re-serializing the parsed payload must reproduce Python's bytes.
    std::string again = jsonSerialize(f.payload);
    if (again != body) fprintf(stderr, "reserialize mismatch:\n  py : %s\n  c++: %s\n", body.c_str(), again.c_str());
    CHECK(again == body);
    std::string mine;
    CHECK(encodeFrame(f.kind, f.opcode, f.seq, f.payload, mine) && mine == bytes);
    wire += bytes;
    bodies.push_back(body);
  }
  StreamDecoder d;
  std::vector<Frame> got = decodeAll(d, wire, 13);
  CHECK(got.size() == bodies.size());
  printf("random vectors: %zu frames cross-checked\n", bodies.size());
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s protocol_vectors.json [random_vectors.json]\n", argv[0]);
    return 2;
  }
  const char* known = "123456789";
  CHECK(crc16CcittFalse(reinterpret_cast<const uint8_t*>(known), 9) == 0x29B1);
  testJson();
  std::vector<Frame> expected;
  std::string wire;
  testVectors(argv[1], expected, wire);
  if (expected.size() == 4) testStream(expected, wire);
  if (argc > 2) testRandom(argv[2]);
  if (g_failures) {
    printf("FAILED: %d check(s)\n", g_failures);
    return 1;
  }
  printf("all vibproto checks passed\n");
  return 0;
}
