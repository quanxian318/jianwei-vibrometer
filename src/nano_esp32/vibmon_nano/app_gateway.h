#pragma once
// Phone app gateway (protocol V1.1, 15_手机APP_20261025/README.md).
//
// The AX publishes snapshots ("status", "settings", "points", "records",
// "curves") with PUBLISH; the phone reads them over Wi-Fi through the same
// HTTP API v1 that vibmond serves on the LAN. Commands from the phone become
// APP_COMMAND events; the AX answers each with COMMAND_RESULT and the phone
// polls for it. Everything here is portable C++ (no Arduino), so the HTTP
// routing is tested on the PC; http_gateway.cpp adds WebServer, UDP
// discovery and mDNS.

#include <stdint.h>

#include <functional>
#include <map>
#include <string>

#include <vibproto.h>

class AppGateway {
 public:
  struct Hooks {
    std::function<uint32_t(const char* type, const vp::JsonValue& data)> postEvent;  // event id, 0 = failed
    std::function<bool()> axOnline;           // a protocol session is active
    std::function<void(vp::JsonValue&)> net;  // GET /net: Wi-Fi station + access point
    // POST /net: returns an error code or "".
    std::function<std::string(const vp::JsonValue& body)> configureNet;
    std::function<void(vp::JsonValue&)> scan;  // GET /net/scan: starts or returns a scan
  };
  struct Response {
    int status = 200;
    std::string body;
  };

  void begin(const Hooks& hooks, const std::string& nanoVersion);
  // PUBLISH from the AX; false when the topic is unknown or too large.
  bool store(const std::string& topic, const vp::JsonValue& data, uint32_t nowMs);
  // COMMAND_RESULT from the AX; false for an unknown command id.
  bool complete(int64_t cid, const vp::JsonValue& result);
  void poll(uint32_t nowMs);  // expires commands the AX did not answer
  void clear();               // new AX session: the old snapshots are stale

  Response handle(const std::string& method, const std::string& path, const std::string& query,
                  const std::string& body, const std::string& client, uint32_t nowMs);
  void describe(vp::JsonValue& obj) const;  // GET_STATUS / console
  vp::JsonValue infoJson(uint32_t nowMs) const;  // GET /info and the UDP discovery answer

  static constexpr uint32_t kCommandTimeoutMs = 15000;
  static constexpr uint32_t kStaleMs = 8000;  // status older than this: AX considered offline
  static constexpr size_t kMaxCommands = 16;
  static constexpr size_t kMaxArgsBytes = 240;  // APP_COMMAND must fit a 384-byte event

 private:
  struct Topic {
    std::string json;
    uint32_t atMs = 0;
  };
  struct Command {
    uint32_t atMs = 0;
    bool done = false;
    std::string resultJson;  // {"ok":..,"error":..,"data":..}
  };
  bool fresh(uint32_t nowMs) const;
  const Topic* topic(const char* name) const;
  Response command(const std::string& body, uint32_t nowMs);
  Response record(const std::string& id) const;
  void noteClient(const std::string& client, uint32_t nowMs);

  Hooks hooks_;
  std::string nanoVersion_;
  std::map<std::string, Topic> topics_;
  std::map<int64_t, Command> commands_;
  int64_t nextCid_ = 1;
  std::map<std::string, uint32_t> clients_;  // phone address -> last seen
  uint32_t requests_ = 0, published_ = 0, forwarded_ = 0, timeouts_ = 0;
};

extern AppGateway g_gateway;
