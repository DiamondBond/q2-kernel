#ifndef _WQ_CRC_H
#define _WQ_CRC_H

#include <linux/types.h>

uint32_t getcrc32_update(uint32_t init_vect, uint8_t *buffer, uint32_t len);
uint32_t getcrc32(uint8_t *buffer, uint32_t len);
uint32_t getcrc24_update(uint32_t init_vect, uint8_t *buffer, uint32_t len);
uint32_t getcrc24(uint8_t *buffer, uint32_t len);
uint16_t getcrc16_update(uint16_t init_vect, uint8_t *buffer, uint32_t len);
uint16_t getcrc32_h16(uint8_t *buffer, uint32_t len);
uint16_t getcrc16(uint8_t *buffer, uint32_t len);
uint16_t getcrc16_ccitt_update(uint16_t init_vect, uint8_t *buffer,
			       uint32_t len);
uint16_t getcrc16_ccitt(uint8_t *buffer, uint32_t len);
uint8_t getcrc8_update(uint8_t init_vect, uint8_t *buffer, uint32_t len);
uint8_t getcrc8(uint8_t *buffer, uint32_t len);
uint16_t get_crc16_ccitt_unix(uint8_t const *buffer, uint32_t len);

#endif // _WQ_CRC_H
