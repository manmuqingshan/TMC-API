/*******************************************************************************
* Copyright © 2017 TRINAMIC Motion Control GmbH & Co. KG
* (now owned by Analog Devices Inc.),
*
* Copyright © 2024 Analog Devices Inc. All Rights Reserved.
* This software is proprietary to Analog Devices, Inc. and its licensors.
*******************************************************************************/


#ifndef TMC_IC_TMC2240_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "TMC2240_HW_Abstraction.h"


/*******************************************************************************
* API Configuration Defines
* These control optional features of the TMC-API implementation.
* These can be commented in/out here or defined from the build system.
*******************************************************************************/

// Uncomment if you want to save space.....
// and put the table into your own .c file
//#define TMC_API_EXTERNAL_CRC_TABLE 1

// To enable the cache mechanism in order to keep the copy of all registers, set TMC2240_CACHE to '1'.
// With this mechanism the value of write-only registers could be read from their shadow copies.
#ifndef TMC2240_CACHE
#define TMC2240_CACHE  1
//#define TMC2240_CACHE   0
#endif

// To use the caching mechanism already implemented by the TMC-API, set TMC2240_ENABLE_TMC_CACHE to '1'.
// Set TMC2240_ENABLE_TMC_CACHE to '0' if one wants to have their own cache implementation.
#ifndef TMC2240_ENABLE_TMC_CACHE
#define TMC2240_ENABLE_TMC_CACHE   1
//#define TMC2240_ENABLE_TMC_CACHE   0
#endif

/******************************************************************************/


typedef enum {
    IC_BUS_SPI,
    IC_BUS_UART,
    IC_BUS_WLAN,
} TMC2240BusType;

// => TMC-API wrapper
extern void tmc2240_readWriteSPI(uint16_t icID, uint8_t *data, size_t dataLength);
extern bool tmc2240_readWriteUART(uint16_t icID, uint8_t *data, size_t writeLength, size_t readLength);
extern TMC2240BusType tmc2240_getBusType(uint16_t icID);
extern uint8_t tmc2240_getNodeAddress(uint16_t icID);
// => TMC-API wrapper

int32_t tmc2240_readRegister(uint16_t icID, uint8_t address);
void tmc2240_writeRegister(uint16_t icID, uint8_t address, int32_t value);

typedef struct
{
    uint32_t mask;
    uint8_t shift;
    uint8_t address;
    bool isSigned;
} RegisterField;


static inline uint32_t tmc2240_fieldExtract(uint32_t data, RegisterField field)
{
    uint32_t value = (data & field.mask) >> field.shift;

    if (field.isSigned)
    {
        // Apply signedness conversion
        uint32_t baseMask = field.mask >> field.shift;
        uint32_t signMask = baseMask & (~baseMask >> 1);
        value = (value ^ signMask) - signMask;
    }

    return value;
}

static inline uint32_t tmc2240_fieldRead(uint16_t icID, RegisterField field)
{
    uint32_t value = tmc2240_readRegister(icID, field.address);

    return tmc2240_fieldExtract(value, field);
}

static inline uint32_t tmc2240_fieldUpdate(uint32_t data, RegisterField field, uint32_t value)
{
    return (data & (~field.mask)) | ((value << field.shift) & field.mask);
}

static inline void tmc2240_fieldWrite(uint16_t icID, RegisterField field, uint32_t value)
{
    uint32_t regValue = tmc2240_readRegister(icID, field.address);

    regValue = tmc2240_fieldUpdate(regValue, field, value);

    tmc2240_writeRegister(icID, field.address, regValue);
}

/**************************************************************** Cache Implementation *************************************************************************/
#if TMC2240_CACHE == 1
#ifdef TMC2240_ENABLE_TMC_CACHE

// By default, support one IC in the cache
#ifndef TMC2240_IC_CACHE_COUNT
#define TMC2240_IC_CACHE_COUNT 1
#endif

typedef enum {
    TMC2240_CACHE_READ,
    TMC2240_CACHE_WRITE,
    // Special operation: Put content into the cache without marking the entry as dirty.
    // Only used to initialize the cache with hardware defaults. This will allow reading
    // from write-only registers that have a value inside them on reset. When using this
    // operation, a restore will *not* rewrite that filled register!
    TMC2240_CACHE_FILL_DEFAULT,
} TMC2240CacheOp;

typedef struct{
    uint8_t address;
    uint32_t value;
} TMC2240RegisterConstants;

#define TMC2240_ACCESS_DIRTY       0x08  // Register has been written since reset -> shadow register is valid for restore
#define TMC2240_ACCESS_READ        0x01
#define TMC2240_ACCESS_W_PRESET       0x42
#define TMC2240_IS_READABLE(x)     ((x) & TMC2240_ACCESS_READ)
#define ARRAY_SIZE(x)             (sizeof(x)/sizeof(x[0]))

// Helper define:
// Most register permission arrays are initialized with 128 values.
// In those fields its quite hard to have an easy overview of available
// registers. For that, ____ is defined to 0, since 4 underscores are
// very easy to distinguish from the 2-digit hexadecimal values.
// This way, the used registers (permission != ACCESS_NONE) are easily spotted
// amongst unused (permission == ACCESS_NONE) registers.
#define ____ 0x00

// Helper define:
// Default reset values are not used if the corresponding register has a
// hardware preset. Since this is not directly visible in the default
// register reset values array, N_A is used as an indicator for a preset
// value, where any value will be ignored anyways (N_A: not available).
#define N_A 0

// Default Register values
#define R00 0x00002108  // GCONF
#define R0A 0x00000020  // DRVCONF
#define R10 0x00070A03  // IHOLD_IRUN
#define R11 0x0000000A  // TPOWERDOWN
#define R2B 0x00000001  // VSTOP
#define R3A 0x00010000  // ENC_CONST
#define R52 0x0B920F25  // OTW_OV_VTH
#define R6C 0x14410153  // CHOPCONF
#define R70 0xC44C001E  // PWMCONF

// Register access permissions:
//   0x00: none (reserved)
//   0x01: read
//   0x02: write
//   0x03: read/write
//   0x13: read/write, separate functions/values for reading or writing
//   0x23: read/write, flag register (write to clear)
//   0x42: write, has hardware presets on reset
static const uint8_t tmc2240_registerAccess[TMC2240_REGISTER_COUNT] =
{
        //  0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F
        0x03, 0x23, 0x01, 0x03, 0x03, ____, ____, ____, ____, ____, 0x03, 0x03, ____, ____, ____, ____, // 0x00 - 0x0F
        0x03, 0x03, 0x01, 0x03, 0x03, 0x03, ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, // 0x10 - 0x1F
        ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, 0x03, ____, ____, // 0x20 - 0x2F
        ____, ____, ____, ____, ____, ____, ____, ____, 0x03, 0x03, 0x03, 0x23, 0x01, ____, ____, ____, // 0x30 - 0x3F
        ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, // 0x40 - 0x4F
        0x01, 0x01, 0x03, ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, ____, // 0x50 - 0x5F
        0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x01, 0x01, 0x03, 0x03, ____, 0x01, // 0x60 - 0x6F
        0x03, 0x01, 0x01, ____, 0x03, 0x01, 0x01, ____, ____, ____, ____, ____, ____, ____, ____, ____  // 0x70 - 0x7F
};
static const int32_t tmc2240_sampleRegisterPreset[TMC2240_REGISTER_COUNT] =
{
        //  0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   A,   B,   C,   D,   E,   F
        R00, 0,   0,   0,   0,   0,   0,   0,   0,   0,   R0A, 0,   0,   0,   0,   0, // 0x00 - 0x0F
        R10, R11, 0,   0,   0,   0,   0,   0,   0,   0,   0,   R2B, 0,   0,   0,   0, // 0x10 - 0x1F
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, // 0x20 - 0x2F
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   R3A, 0,   0,   0,   0,   0, // 0x30 - 0x3F
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, // 0x40 - 0x4F
        0,   0,   R52, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, // 0x50 - 0x5F
        N_A, N_A, N_A, N_A, N_A, N_A, N_A, N_A, N_A, N_A, 0,   0,   R6C, 0,   0,   0, // 0x60 - 0x6F
        R70, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, // 0x70 - 0x7F
};


// Register constants (only required for 0x42 registers, since we do not have
// any way to find out the content but want to hold the actual value in the
// shadow register so an application (i.e. the TMCL IDE) can still display
// the values. This only works when the register content is constant.
static const TMC2240RegisterConstants tmc2240_RegisterConstants[] =
{      // Use ascending addresses!
        { 0x60, 0xAAAAB554 }, // MSLUT[0]
        { 0x61, 0x4A9554AA }, // MSLUT[1]
        { 0x62, 0x24492929 }, // MSLUT[2]
        { 0x63, 0x10104222 }, // MSLUT[3]
        { 0x64, 0xFBFFFFFF }, // MSLUT[4]
        { 0x65, 0xB5BB777D }, // MSLUT[5]
        { 0x66, 0x49295556 }, // MSLUT[6]
        { 0x67, 0x00404222 }, // MSLUT[7]
        { 0x68, 0xFFFF8056 }, // MSLUTSEL
        { 0x69, 0x00F70000 }  // MSLUTSTART
};

extern uint8_t tmc2240_dirtyBits[TMC2240_IC_CACHE_COUNT][TMC2240_REGISTER_COUNT/8];
extern int32_t tmc2240_shadowRegister[TMC2240_IC_CACHE_COUNT][TMC2240_REGISTER_COUNT];
bool tmc2240_cache(uint16_t icID, TMC2240CacheOp operation, uint8_t address, uint32_t *value);
void tmc2240_initCache(void);
void tmc2240_setDirtyBit(uint16_t icID, uint8_t index, bool value);
bool tmc2240_getDirtyBit(uint16_t icID, uint8_t index);
#endif
#endif
/***************************************************************************************************************************************************/
#endif /* TMC_IC_TMC2240_H_ */
