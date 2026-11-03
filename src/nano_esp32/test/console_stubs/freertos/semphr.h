#pragma once
#include <mutex>
using SemaphoreHandle_t = std::mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { static std::mutex m; return &m; }
inline void xSemaphoreTake(SemaphoreHandle_t m, unsigned) { m->lock(); }
inline void xSemaphoreGive(SemaphoreHandle_t m) { m->unlock(); }
