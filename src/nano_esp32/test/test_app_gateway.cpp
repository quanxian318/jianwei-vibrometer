// Host test for app_gateway.cpp (phone app gateway routing and command relay):
//   g++ -std=c++11 -Wall -Wextra -I../vibmon_nano -I../../shared/vibproto/src test_app_gateway.cpp
//       ../vibmon_nano/app_gateway.cpp ../../shared/vibproto/src/vp_json.cpp -o t && ./t
#include <stdio.h>

#include <string>
#include <vector>

#include "app_gateway.h"

static int g_fail = 0;
#define CHECK(c)                                            \
  do {                                                      \
    if (!(c)) {                                             \
      fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); \
      ++g_fail;                                             \
    }                                                       \
  } while (0)

struct Posted {
  std::string type;
  vp::JsonValue data;
};

static vp::JsonValue parse(const std::string& s) {
  vp::JsonValue v;
  vp::jsonParse(s, v);
  return v;
}

int main() {
  std::vector<Posted> events;
  bool online = false;
  std::string netBody;
  AppGateway g;
  AppGateway::Hooks h;
  h.postEvent = [&events](const char* t, const vp::JsonValue& d) -> uint32_t {
    events.push_back({t, d});
    return static_cast<uint32_t>(events.size());
  };
  h.axOnline = [&online]() { return online; };
  h.net = [](vp::JsonValue& o) { o.set("station", "x"); };
  h.configureNet = [&netBody](const vp::JsonValue& b) -> std::string {
    netBody = vp::jsonSerialize(b);
    return b.find("ssid") ? std::string() : std::string("nothing_to_change");
  };
  g.begin(h, "0.2.0");

  // Nothing published yet, AX offline.
  AppGateway::Response r = g.handle("GET", "/api/v1/info", "", "", "10.0.0.2", 1000);
  vp::JsonValue info = parse(r.body);
  CHECK(r.status == 200 && info.find("data")->find("kind")->asString() == "gateway");
  CHECK(!info.find("data")->find("ax_online")->asBool(true));
  CHECK(g.handle("GET", "/api/v1/live", "", "", "", 1000).status == 503);
  CHECK(g.handle("POST", "/api/v1/command", "", "{\"cmd\":\"mute\"}", "", 1000).status == 503);

  // The AX publishes; the phone reads the snapshot with the gateway's age.
  online = true;
  vp::JsonValue st = parse("{\"dev\":\"vibmon-07\",\"ver\":\"vibmon 0.2.0\",\"ms\":\"preview\",\"lv\":\"normal\"}");
  CHECK(g.store("status", st, 2000));
  CHECK(!g.store("bogus", st, 2000));
  r = g.handle("GET", "/api/v1/live", "", "", "10.0.0.2", 2500);
  vp::JsonValue live = parse(r.body);
  CHECK(r.status == 200 && live.find("data")->find("ms")->asString() == "preview");
  CHECK(live.find("data")->find("gw")->find("age_ms")->asInt() == 500);
  CHECK(live.find("data")->find("gw")->find("ax_online")->asBool());
  CHECK(parse(g.handle("GET", "/api/v1/info", "", "", "", 2500).body).find("data")->find("device_id")->asString() ==
        "vibmon-07");
  // A stale snapshot reports the AX as offline.
  CHECK(!parse(g.handle("GET", "/api/v1/live", "", "", "", 2000 + AppGateway::kStaleMs + 1).body)
             .find("data")->find("gw")->find("ax_online")->asBool(true));
  // The first request of a phone is announced once.
  size_t clients = 0;
  for (const Posted& e : events) clients += e.type == "APP_CLIENT";
  CHECK(clients == 1);

  // Command relay: event out, result back, timeout.
  events.clear();
  r = g.handle("POST", "/api/v1/command", "", "{\"cmd\":\"set_point\",\"id\":\"P-1\",\"name\":\"泵\"}", "10.0.0.2", 3000);
  CHECK(r.status == 202);
  const int64_t cid = parse(r.body).find("data")->find("cid")->asInt();
  CHECK(events.size() == 1 && events[0].type == "APP_COMMAND" && events[0].data.find("cid")->asInt() == cid);
  CHECK(events[0].data.find("args")->find("id")->asString() == "P-1" && !events[0].data.find("args")->find("cmd"));
  vp::JsonValue p = parse(g.handle("GET", "/api/v1/command/" + std::to_string(cid), "", "", "", 3100).body);
  CHECK(!p.find("data")->find("done")->asBool(true));
  CHECK(g.complete(cid, parse("{\"cid\":1,\"ok\":true}")));
  CHECK(!g.complete(999, parse("{\"ok\":true}")));
  p = parse(g.handle("GET", "/api/v1/command/" + std::to_string(cid), "", "", "", 3200).body);
  CHECK(p.find("data")->find("done")->asBool() && p.find("data")->find("result")->find("ok")->asBool());
  r = g.handle("POST", "/api/v1/command", "", "{\"cmd\":\"mute\"}", "", 4000);
  const int64_t cid2 = parse(r.body).find("data")->find("cid")->asInt();
  g.poll(4000 + AppGateway::kCommandTimeoutMs + 1);
  p = parse(g.handle("GET", "/api/v1/command/" + std::to_string(cid2), "", "", "", 19100).body);
  CHECK(p.find("data")->find("result")->find("error")->asString() == "timeout");
  CHECK(g.handle("POST", "/api/v1/command", "", "not json", "", 5000).status == 400);
  CHECK(g.handle("POST", "/api/v1/command", "", "{\"cmd\":\"set\",\"x\":\"" + std::string(300, 'a') + "\"}", "", 5000)
            .status == 413);

  // Records: the list and the newest curves; older detail is LAN only.
  CHECK(g.store("records", parse("{\"items\":[{\"id\":\"r2\",\"hz\":7.1},{\"id\":\"r1\",\"hz\":6}]}"), 6000));
  CHECK(g.store("curves", parse("{\"id\":\"r2\",\"u\":\"mm\",\"wt\":[0,0.1],\"wv\":[1,-1],\"sf\":[7],\"sa\":[2]}"), 6000));
  vp::JsonValue list = parse(g.handle("GET", "/api/v1/records", "", "", "", 6100).body);
  CHECK(list.find("data")->size() == 2);
  vp::JsonValue det = parse(g.handle("GET", "/api/v1/records/r2", "", "", "", 6100).body);
  CHECK(det.find("data")->find("partial")->asBool() && det.find("data")->find("summary")->find("hz")->asDouble() == 7.1);
  CHECK(det.find("data")->find("spectrum")->find("f")->at(0).asDouble() == 7);
  CHECK(g.handle("GET", "/api/v1/records/r1", "", "", "", 6100).status == 404);
  CHECK(g.handle("GET", "/api/v1/preview.bmp", "", "", "", 6100).status == 404);

  // Network settings go to the Nano's own hook.
  CHECK(g.handle("POST", "/api/v1/net", "", "{\"ssid\":\"lab\",\"password\":\"12345678\"}", "", 7000).status == 200);
  CHECK(netBody.find("lab") != std::string::npos);
  CHECK(g.handle("POST", "/api/v1/net", "", "{}", "", 7000).status == 400);

  // A new AX session drops the snapshots.
  g.clear();
  CHECK(g.handle("GET", "/api/v1/live", "", "", "", 8000).status == 503);

  if (g_fail == 0) printf("app_gateway: all checks passed\n");
  return g_fail ? 1 : 0;
}
