/*!
 * @file crc.h
 *
 * @copyright COPYRIGHT (C) 2019-2028, SCORPION, ALL RIGHTS RESERVED.
 *
 * @brief
 *
 *
 */

#ifndef INCLUDE_USYSTEM_CRYPTO_CRC_CRC_H
#define INCLUDE_USYSTEM_CRYPTO_CRC_CRC_H
#ifdef __cplusplus
extern "C"
{
#endif


enum _uCRC_TYPE
{
    uCRC8,
    uCRC8_TIU,
    uCRC8_ROHC,
    uCRC8_MAXIM,
    uCRC16_IBM,
    uCRC16_MAXIM,
    uCRC16_USB,
    uCRC16_MODBUS,
    uCRC16_CCITT,
    uCRC16_CCITT_FALSE,
    uCRC16_X25,
    uCRC16_XMODEM,
    uCRC16_DNP,
    uCRC32,
    uCRC32_MPEG2
};
typedef enum _uCRC_TYPE uCRC_TYPE, *uPCRC_TYPE;

uint32_t
uCRC_Compute(uCRC_TYPE type, const uint8_t *data, uint8_t bytes);
uint32_t
uCRC_ComputePart(uCRC_TYPE type, uint32_t initSeed, const uint8_t *data, uint8_t bytes);
uint32_t
uCRC_ComputeComplete(uCRC_TYPE type, uint32_t initSeed);

extern const uint8_t m_kCRC8_07_LSB[256];
extern const uint8_t m_kCRC8_07_MSB[256];
extern const uint8_t m_kCRC8_31_LSB[256];
extern const uint16_t m_kCRC16_8005_LSB[256];
extern const uint16_t m_kCRC16_1021_LSB[256];
extern const uint16_t m_kCRC16_1021_MSB[256];
extern const uint16_t m_kCRC16_3D65_LSB[256];
extern const uint32_t m_kCRC32_04C11DB7_LSB[256];
extern const uint32_t m_kCRC32_04C11DB7_MSB[256];

uint8_t
uCRC_ComputeCRC8(uCRC_TYPE type, const uint8_t *data, uint8_t bytes);
uint8_t
uCRC_ComputeCRC8Part(uCRC_TYPE type, uint16_t initSeed, const uint8_t *data, uint8_t bytes);
uint8_t
uCRC_ComputeCRC8Complete(uCRC_TYPE type, uint16_t initSeed);
uint16_t
uCRC_ComputeCRC16(uCRC_TYPE type, const uint8_t *data, uint8_t bytes);
uint16_t
uCRC_ComputeCRC16Part(uCRC_TYPE type, uint16_t initSeed, const uint8_t *data, uint8_t bytes);
uint16_t
uCRC_ComputeCRC16Complete(uCRC_TYPE type, uint16_t initSeed);
uint32_t
uCRC_ComputeCRC32(uCRC_TYPE type, const uint8_t *data, uint8_t bytes);
uint32_t
uCRC_ComputeCRC32Part(uCRC_TYPE type, uint32_t initSeed, const uint8_t *data, uint8_t bytes);
uint32_t
uCRC_ComputeCRC32Complete(uCRC_TYPE type, uint32_t initSeed);

#ifdef __cplusplus
}
#endif
#endif /* INCLUDE_USYSTEM_CRYPTO_CRC_CRC_H */
