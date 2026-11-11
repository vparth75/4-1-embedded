#pragma once
#include <stdint.h>
#include <stddef.h>

// CRC-8/SMBUS: poly 0x07, init 0x00, no reflection, no final XOR.
uint8_t crc8(const uint8_t* data, size_t len);
