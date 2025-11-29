#ifndef CRC_H
#define CRC_H

#include <stdint.h>
#include <stddef.h>

/* Flag binaire HDLC : 01111110 */
#define CRC_FLAG_STR "01111110"

/* ===================== Conversion bits/bytes ===================== */

/* Convertit un octet en chaîne "00000000" (8 chars + '\0') */
void byte_to_bits(uint8_t b, char out[9]);

/* Convertit une séquence d'octets en chaîne de bits "0101..." (malloc).
   out_len_bits reçoit la longueur SANS le '\0'.
   Retourne NULL en cas d'erreur. */
char *bytes_to_bits(const uint8_t *data, size_t len, size_t *out_len_bits);

/* Convertit une chaîne de bits (longueur multiple de 8) en bytes (malloc).
   out_len_bytes reçoit la longueur du tableau.
   Retourne NULL si la longueur n'est pas multiple de 8 ou erreur. */
uint8_t *bits_to_bytes(const char *bits, size_t *out_len_bytes);

/* ========================= CRC-16 CCITT ========================== */

/* Calcule le CRC-16 CCITT sur data.
   poly = 0x1021, init = 0xFFFF pour reproduire crc.py. */
uint16_t crc16_ccitt(const uint8_t *data,
                     size_t len,
                     uint16_t poly,
                     uint16_t init_value);

/* Calcule le CRC-16 CCITT sur une chaîne de bits "0/1".
   Comme en Python: padding en 0 pour obtenir un multiple de 8 bits. */
uint16_t crc_bits(const char *bits);

/* ===================== Bit stuffing / destuff ==================== */

/* Applique le bit-stuffing HDLC : après cinq '1' consécutifs, insère '0'.
   Retourne une nouvelle chaîne allouée (malloc) ou NULL en cas d'erreur. */
char *stuffing(const char *bits);

/* Retire le bit-stuffing.
   En cas de corruption (bit stuffed != '0' ou manquant), retourne NULL. */
char *destuff(const char *bits);

/* ======================== Flags 01111110 ========================= */

/* Ajoute les flags (en bits) autour de la trame déjà stuffed.
   Retourne "FLAG + bits + FLAG" (malloc) ou NULL. */
char *add_flags(const char *bits);

/* Retire les flags.
   Cherche le premier et le dernier motif CRC_FLAG_STR.
   Retourne la sous-chaîne entre les deux (malloc) ou NULL si erreur. */
char *remove_flags(const char *bits_with_flags);

/* ======================= Trames avec CRC ========================= */

/* Construit la trame bits:
   FLAG | stuffing( bits(header + payload + CRC16) ) | FLAG
   header = [command, numSeq, sizePayLoad] (3 octets).
   crc_builtin doit être le CRC déjà calculé sur header+payload,
   comme dans crc.py.
   Retourne une chaîne de bits "0/1" (malloc) ou NULL. */
char *build_frame_with_crc16(uint8_t command,
                             uint8_t numSeq,
                             uint8_t sizePayLoad,
                             uint16_t crc_builtin,
                             const uint8_t *payload,
                             size_t payload_len);

/* parse_frame_with_crc16 :
   - frame_bytes : trame brute reçue (octets, avec flags, stuffing, CRC)
   Renvoie:
      *out_command, *out_seq, *out_size, *out_crc
      payload renvoyé dans *out_payload (malloc), longueur dans *out_payload_len.
   Retourne 0 si OK, -1 si erreur (flags, destuff, etc.). */
int parse_frame_with_crc16(const uint8_t *frame_bytes,
                           size_t frame_len,
                           uint8_t *out_command,
                           uint8_t *out_seq,
                           uint8_t *out_size,
                           uint16_t *out_crc,
                           uint8_t **out_payload,
                           size_t *out_payload_len);

#endif /* CRC_H */
