/* Platform layer expected by ST's VL53L1X ULD (STSW-IMG009, API 3.5.x).
 * Replaces ST's empty template; implemented in vl53l1_platform_linux.cpp on
 * top of vm::I2cBus. "dev" is the 7-bit I2C address. */
#ifndef VL53L1_PLATFORM_H_
#define VL53L1_PLATFORM_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t dummy;
} VL53L1_Dev_t;
typedef VL53L1_Dev_t* VL53L1_DEV;

int8_t VL53L1_WriteMulti(uint16_t dev, uint16_t index, uint8_t* pdata, uint32_t count);
int8_t VL53L1_ReadMulti(uint16_t dev, uint16_t index, uint8_t* pdata, uint32_t count);
int8_t VL53L1_WrByte(uint16_t dev, uint16_t index, uint8_t data);
int8_t VL53L1_WrWord(uint16_t dev, uint16_t index, uint16_t data);
int8_t VL53L1_WrDWord(uint16_t dev, uint16_t index, uint32_t data);
int8_t VL53L1_RdByte(uint16_t dev, uint16_t index, uint8_t* pdata);
int8_t VL53L1_RdWord(uint16_t dev, uint16_t index, uint16_t* pdata);
int8_t VL53L1_RdDWord(uint16_t dev, uint16_t index, uint32_t* pdata);
int8_t VL53L1_WaitMs(uint16_t dev, int32_t wait_ms);

#ifdef __cplusplus
}
#endif

#endif
