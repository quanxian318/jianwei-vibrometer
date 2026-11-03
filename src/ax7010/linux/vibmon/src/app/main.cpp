// vibmond - vision vibration meter main application for the AX7010.
//   vibmond [-c /etc/vibmon/vibmon.json] [-v]
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "app/app.h"
#include "util/log.h"

int main(int argc, char** argv) {
  std::string configPath = "/etc/vibmon/vibmon.json";
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "-c") && i + 1 < argc) {
      configPath = argv[++i];
    } else if (!strcmp(argv[i], "-v")) {
      vm::logSetLevel(vm::LogLevel::kDebug);
    } else {
      fprintf(stderr, "usage: %s [-c config.json] [-v]\n", argv[0]);
      return 2;
    }
  }
  vm::AppConfig cfg;
  std::string err;
  if (access(configPath.c_str(), R_OK) == 0) {
    if (!vm::loadConfig(configPath, cfg, err)) {
      LOGE("main", "config: %s", err.c_str());
      return 1;
    }
  } else {
    LOGW("main", "%s not found, using built-in defaults", configPath.c_str());
  }
  // Settings changed on the screen override the configuration file.
  if (!cfg.settingsFile.empty() && access(cfg.settingsFile.c_str(), R_OK) == 0 &&
      !vm::loadConfig(cfg.settingsFile, cfg, err)) {
    LOGW("main", "settings %s ignored: %s", cfg.settingsFile.c_str(), err.c_str());
  }
  vm::Application app(cfg);
  if (!app.init()) return 1;
  return app.run();
}
