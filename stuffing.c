#include <stdio.h>
#include <string.h>
#include "stuffing.h"
#include "protocole.h"
#include "util.h"

static inline void write_bit(uint8_t *out, size_t *byteIndex, int *bitIndex, int bit) {
    if (bit) {
        out[*byteIndex] |= (1 << (7 - *bitIndex));
    }
    (*bitIndex)++;
    if (*bitIndex == 8) {
        *bitIndex = 0;
        (*byteIndex)++;
        out[*byteIndex] = 0;
    }
}

uint8_t *stuff(const uint8_t *bytes, size_t len, size_t *outLen) {
    uint8_t *stuffed = calloc(len * 2, 1);
    size_t bi = 0;      // byte index
    int bitpos = 0;
    int count1 = 0;

    for (size_t i = 0; i < len; i++) {
        for (int b = 7; b >= 0; b--) {
            int bit = (bytes[i] >> b) & 1;

            write_bit(stuffed, &bi, &bitpos, bit);

            if (bit == 1) {
                count1++;
                if (count1 == 5) {
                    write_bit(stuffed, &bi, &bitpos, 0);
                    count1 = 0;
                }
            } else {
                count1 = 0;
            }
        }
    }

    if (bitpos != 0) bi++;
    *outLen = bi;
    return stuffed;
}

uint8_t *destuff(const uint8_t *bytes, size_t len, size_t *outLen) {
    uint8_t *out = calloc(len, 1);
    size_t bi = 0;
    int bitpos = 0;
    int count1 = 0;

    for (size_t i = 0; i < len; i++) {
        for (int b = 7; b >= 0; b--) {
            int bit = (bytes[i] >> b) & 1;

            if (count1 == 5) {
                // skip stuffed 0
                if (bit == 0) {
                    count1 = 0;
                    continue;
                }
            }

            write_bit(out, &bi, &bitpos, bit);

            if (bit == 1) count1++;
            else count1 = 0;
        }
    }

    if (bitpos != 0) bi++;
    *outLen = bi;
    return out;
}

uint8_t *frame_to_bytes_stuffed(frame_t *frame, size_t *lenConvertedFrame) {
    size_t lenRawFrame = getLengthInfo(*frame) + 5; // headers only

    uint8_t *raw = malloc(lenRawFrame);
    libereSiDoitEtreLiberer((void **) &raw, EXIT_FAILURE);

    raw[0] = getCommande(*frame);
    raw[1] = getNum_seq(*frame);
    uint16_t value = getSomme_ctrl(*frame);
    raw[2] = (value >> 8) & 0xFF;
    raw[3] = value & 0xFF;
    raw[4] = getLengthInfo(*frame);
    if (getLengthInfo(*frame) > 0) {
        memcpy(&raw[5], getInfo(frame), getLengthInfo(*frame));
    }

    size_t stuffedLen = 0;
    uint8_t *stuffed = stuff(raw, lenRawFrame, &stuffedLen);

    // allocate frame + 2 delimiters
    uint8_t *final = malloc(stuffedLen + 2);
    final[0] = DELIMITER;
    memcpy(&final[1], stuffed, stuffedLen);
    final[stuffedLen + 1] = DELIMITER;

    *lenConvertedFrame = stuffedLen + 2;

    free(raw);
    free(stuffed);

    return final;
}

/*
    RealCRC pour stocker le CRC recu dans la séquence d'octets.
    La frame retournée possède un champ CRC calculé lors de la création
    de la trame avec tous les champs entrés. Le CRC ne doit pas être set

    Ainsi, on pourra comparé la valeur calculée et la valeur attendue
*/
frame_t bytesToFrame_destuffed(uint8_t *bytes, size_t lenBytes, uint16_t *realCRC) {
    if (!bytes || lenBytes < 2 || bytes[0] != DELIMITER || bytes[lenBytes-1] != DELIMITER)
        return createFrame(0, UINT8_MAX, OTHER, 0);

    // remove delimiters
    size_t tmpSize = 0;
    uint8_t *tmp = destuff(&(bytes[1]), lenBytes - 2, &tmpSize);

    if (tmpSize < 5) {
        free(tmp);
        return createFrame(0, UINT8_MAX, OTHER, 0);
    }

    uint8_t comm = tmp[0];
    uint8_t seq = tmp[1];
    *realCRC = (tmp[2] << 8) | tmp[3];
    uint8_t len = tmp[4];

    if (len > DATA_MAX_LEN) {
        free(tmp);
        return createFrame(0, UINT8_MAX, OTHER, 0);
    }

    uint8_t *info = calloc(len, sizeof(uint8_t));
    memcpy(info, &(tmp[5]), len);

    frame_t frame = createFrame(info, seq, comm, len);

    cleanPtr((void**)&info);
    free(tmp);

    return frame;
}


