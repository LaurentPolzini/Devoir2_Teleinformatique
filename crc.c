#include "crc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===================== Conversion bits/bytes ===================== */

void byte_to_bits(uint8_t b, char out[9]) {
    for (int i = 7; i >= 0; --i) {
        out[7 - i] = (b & (1u << i)) ? '1' : '0';
    }
    out[8] = '\0';
}

char *bytes_to_bits(const uint8_t *data, size_t len, size_t *out_len_bits) {
    if (!data && len > 0) return NULL;

    size_t total_bits = len * 8;
    char *bits = (char *) malloc(total_bits + 1);
    if (!bits) return NULL;

    size_t pos = 0;
    for (size_t i = 0; i < len; ++i) {
        char tmp[9];
        byte_to_bits(data[i], tmp);
        for (int j = 0; j < 8; ++j) {
            bits[pos++] = tmp[j];
        }
    }
    bits[pos] = '\0';
    if (out_len_bits) *out_len_bits = total_bits;
    return bits;
}

uint8_t *bits_to_bytes(const char *bits, size_t *out_len_bytes) {
    if (!bits) return NULL;
    size_t len_bits = strlen(bits);

    if (len_bits % 8 != 0) {
        /* même comportement que bits_to_bytes() Python:
           lève une erreur si pas multiple de 8 */
        fprintf(stderr, "bits_to_bytes: longueur non multiple de 8\n");
        return NULL;
    }

    size_t nb_bytes = len_bits / 8;
    uint8_t *out = (uint8_t *) malloc(nb_bytes);
    if (!out) return NULL;

    for (size_t i = 0; i < nb_bytes; ++i) {
        uint8_t val = 0;
        for (int j = 0; j < 8; ++j) {
            char c = bits[i * 8 + j];
            if (c != '0' && c != '1') {
                fprintf(stderr, "bits_to_bytes: bit invalide '%c'\n", c);
                free(out);
                return NULL;
            }
            val = (uint8_t) ((val << 1) | (c == '1' ? 1u : 0u));
        }
        out[i] = val;
    }
    if (out_len_bytes) *out_len_bytes = nb_bytes;
    return out;
}

/* ========================= CRC-16 CCITT ========================== */

uint16_t crc16_ccitt(const uint8_t *data,
                     size_t len,
                     uint16_t poly,
                     uint16_t init_value)
{
    uint16_t crc = init_value;

    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)(data[i] << 8);
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = (uint16_t)(((crc << 1) & 0xFFFFu) ^ poly);
            } else {
                crc = (uint16_t)((crc << 1) & 0xFFFFu);
            }
        }
    }
    return crc;
}

uint16_t crc_bits(const char *bits) {
    if (!bits) return 0;
    size_t len_bits = strlen(bits);

    /* padding à un multiple de 8, comme dans crc.py */
    size_t padding = (8 - (len_bits % 8)) % 8;
    size_t total = len_bits + padding;

    uint8_t *bytes = (uint8_t *) malloc(total / 8);
    if (!bytes) return 0;

    size_t idx_byte = 0;
    uint8_t current = 0;
    int bit_count = 0;

    for (size_t i = 0; i < total; ++i) {
        char c;
        if (i < len_bits) {
            c = bits[i];
            if (c != '0' && c != '1') {
                fprintf(stderr, "crc_bits: bit invalide '%c'\n", c);
                free(bytes);
                return 0;
            }
        } else {
            /* padding = '0' */
            c = '0';
        }

        current = (uint8_t)((current << 1) | (c == '1' ? 1u : 0u));
        bit_count++;

        if (bit_count == 8) {
            bytes[idx_byte++] = current;
            current = 0;
            bit_count = 0;
        }
    }

    uint16_t crc = crc16_ccitt(bytes, idx_byte, 0x1021, 0xFFFF);
    free(bytes);
    return crc;
}

/* ===================== Bit stuffing / destuff ==================== */

char *stuffing(const char *bits) {
    if (!bits) return NULL;
    size_t len = strlen(bits);

    /* dans le pire cas, on ajoute ~1/5 de bits => approx *2 pour être large */
    size_t cap = len * 2 + 1;
    char *out = (char *) malloc(cap);
    if (!out) return NULL;

    int count_ones = 0;
    size_t pos = 0;

    for (size_t i = 0; i < len; ++i) {
        char b = bits[i];
        if (b != '0' && b != '1') {
            fprintf(stderr, "stuffing: bit invalide '%c'\n", b);
            free(out);
            return NULL;
        }

        out[pos++] = b;
        if (b == '1') {
            count_ones++;
            if (count_ones == 5) {
                /* insérer '0' */
                out[pos++] = '0';
                count_ones = 0;
            }
        } else {
            count_ones = 0;
        }

        /* agrandit si besoin (très peu probable mais safe) */
        if (pos + 2 >= cap) {
            cap *= 2;
            char *tmp = (char *) realloc(out, cap);
            if (!tmp) {
                free(out);
                return NULL;
            }
            out = tmp;
        }
    }
    out[pos] = '\0';
    return out;
}

char *destuff(const char *bits) {
    if (!bits) return NULL;
    size_t len = strlen(bits);

    char *out = (char *) malloc(len + 1); /* au plus même taille */
    if (!out) return NULL;

    size_t pos = 0;
    int count_ones = 0;
    size_t i = 0;

    while (i < len) {
        char b = bits[i];
        if (b != '0' && b != '1') {
            fprintf(stderr, "destuff: bit invalide '%c'\n", b);
            free(out);
            return NULL;
        }

        out[pos++] = b;

        if (b == '1') {
            count_ones++;
            if (count_ones == 5) {
                size_t stuffed_index = i + 1;

                if (stuffed_index >= len) {
                    fprintf(stderr, "Trame corrompue : bit stuffed manquant après 5 bits à '1'\n");
                    free(out);
                    return NULL;
                }

                char stuffed_bit = bits[stuffed_index];
                if (stuffed_bit != '0') {
                    fprintf(stderr,
                            "Trame corrompue : bit stuffed différent de '0' "
                            "(reçu '%c' à index '%zu')\n",
                            stuffed_bit, stuffed_index);
                    free(out);
                    return NULL;
                }

                /* on saute le stuffed bit */
                i = stuffed_index;
                count_ones = 0;
            }
        } else {
            count_ones = 0;
        }

        i++;
    }

    out[pos] = '\0';
    return out;
}

/* ======================== Flags 01111110 ========================= */

char *add_flags(const char *bits) {
    if (!bits) return NULL;
    const char *flag = CRC_FLAG_STR;
    size_t len_flag = strlen(flag);
    size_t len_bits = strlen(bits);

    char *out = (char *) malloc(len_flag + len_bits + len_flag + 1);
    if (!out) return NULL;

    memcpy(out, flag, len_flag);
    memcpy(out + len_flag, bits, len_bits);
    memcpy(out + len_flag + len_bits, flag, len_flag);
    out[len_flag + len_bits + len_flag] = '\0';
    return out;
}

char *remove_flags(const char *bits_with_flags) {
    if (!bits_with_flags) return NULL;
    const char *flag = CRC_FLAG_STR;
    size_t len_flag = strlen(flag);

    const char *start = strstr(bits_with_flags, flag);
    const char *end   = NULL;

    if (!start) {
        fprintf(stderr, "remove_flags: flag de début introuvable\n");
        return NULL;
    }

    /* dernier flag = rfind */
    const char *p = bits_with_flags;
    const char *last = NULL;
    while ((p = strstr(p, flag)) != NULL) {
        last = p;
        p += len_flag;
    }

    if (!last || last <= start) {
        fprintf(stderr, "remove_flags: flags invalides (fin manquante ou avant début)\n");
        return NULL;
    }

    end = last;
    size_t useful_len = (size_t)(end - (start + len_flag));

    char *out = (char *) malloc(useful_len + 1);
    if (!out) return NULL;

    memcpy(out, start + len_flag, useful_len);
    out[useful_len] = '\0';
    return out;
}

/* ======================= Trames avec CRC ========================= */

char *build_frame_with_crc16(uint8_t command,
                             uint8_t numSeq,
                             uint8_t sizePayLoad,
                             uint16_t crc_builtin,
                             const uint8_t *payload,
                             size_t payload_len)
{
    /* core = [command, numSeq, sizePayLoad] + payload */
    size_t core_len = 3 + payload_len;
    uint8_t *core = (uint8_t *) malloc(core_len);
    if (!core) return NULL;

    core[0] = command;
    core[1] = numSeq;
    core[2] = sizePayLoad;

    if (payload_len > 0 && payload) {
        memcpy(core + 3, payload, payload_len);
    }

    /* Vérif éventuelle du CRC fourni (comme dans crc.py) */
    uint16_t crc_calc = crc16_ccitt(core, core_len, 0x1021, 0xFFFF);
    if (crc_calc != crc_builtin) {
        fprintf(stderr, "build_frame_with_crc16: CRC fourni différent du CRC calculé (%u != %u)\n",
                (unsigned)crc_builtin, (unsigned)crc_calc);
        /* tu peux décider de continuer ou d'échouer. Ici on continue mais on utilise crc_builtin. */
    }

    /* Ajout CRC en 2 octets big-endian */
    size_t core_crc_len = core_len + 2;
    uint8_t *core_with_crc = (uint8_t *) malloc(core_crc_len);
    if (!core_with_crc) {
        free(core);
        return NULL;
    }
    memcpy(core_with_crc, core, core_len);
    core_with_crc[core_len]     = (uint8_t)((crc_builtin >> 8) & 0xFF);
    core_with_crc[core_len + 1] = (uint8_t)(crc_builtin & 0xFF);

    free(core);

    /* core_with_crc -> bits */
    size_t bits_len = 0;
    char *core_bits = bytes_to_bits(core_with_crc, core_crc_len, &bits_len);
    free(core_with_crc);
    if (!core_bits) return NULL;

    /* stuffing */
    char *stuffed = stuffing(core_bits);
    free(core_bits);
    if (!stuffed) return NULL;

    /* flags */
    char *framed_bits = add_flags(stuffed);
    free(stuffed);
    return framed_bits; /* malloc */
}

int parse_frame_with_crc16(const uint8_t *frame_bytes,
                           size_t frame_len,
                           uint8_t *out_command,
                           uint8_t *out_seq,
                           uint8_t *out_size,
                           uint16_t *out_crc,
                           uint8_t **out_payload,
                           size_t *out_payload_len)
{
    if (!frame_bytes || frame_len == 0 ||
        !out_command || !out_seq || !out_size ||
        !out_crc || !out_payload || !out_payload_len) {
        return -1;
    }

    /* bytes -> bits */
    size_t bits_len = 0;
    char *framed_bits = bytes_to_bits(frame_bytes, frame_len, &bits_len);
    if (!framed_bits) return -1;

    /* remove flags */
    char *core_stuffed = remove_flags(framed_bits);
    free(framed_bits);
    if (!core_stuffed) return -1;

    /* destuff */
    char *core_bits = destuff(core_stuffed);
    free(core_stuffed);
    if (!core_bits) return -1;

    /* bits -> bytes */
    size_t core_len = 0;
    uint8_t *core_bytes = bits_to_bytes(core_bits, &core_len);
    free(core_bits);
    if (!core_bytes) return -1;

    if (core_len < 5) {
        /* min : 3 octets header + 2 CRC */
        fprintf(stderr, "parse_frame_with_crc16: core trop court (%zu)\n", core_len);
        free(core_bytes);
        return -1;
    }

    size_t data_len = core_len - 2;
    uint8_t *data_part = core_bytes;
    uint8_t *crc_part  = core_bytes + data_len;

    uint16_t crc_recv = (uint16_t)((crc_part[0] << 8) | crc_part[1]);

    uint8_t command = data_part[0];
    uint8_t seq     = data_part[1];
    uint8_t size    = data_part[2];

    size_t payload_len = (data_len > 3) ? (data_len - 3) : 0;

    /* payload copie */
    uint8_t *payload = NULL;
    if (payload_len > 0) {
        payload = (uint8_t *) malloc(payload_len);
        if (!payload) {
            free(core_bytes);
            return -1;
        }
        memcpy(payload, data_part + 3, payload_len);
    }

    free(core_bytes);

    /* remplissage des sorties */
    *out_command = command;
    *out_seq     = seq;
    *out_size    = size;
    *out_crc     = crc_recv;
    *out_payload = payload;
    *out_payload_len = payload_len;

    return 0;
}
