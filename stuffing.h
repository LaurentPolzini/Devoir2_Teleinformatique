#ifndef __STUFFING_H__
#define __STUFFING_H__

#include <unistd.h>
#include <stdlib.h>
#include "protocole.h"

uint8_t *stuff(const uint8_t *bytes, size_t len, size_t *outLen);

uint8_t *destuff(const uint8_t *bytes, size_t len, size_t *outLen);

uint8_t *frame_to_bytes_stuffed(frame_t *frame, size_t *lenConvertedFrame);

frame_t bytesToFrame_destuffed(uint8_t *bytes, size_t lenBytes, uint16_t *realCRC);




#endif
