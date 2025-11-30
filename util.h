#ifndef __UTIL_H__
#define __UTIL_H__

// if malloc failed, returns NULL but i must clear pointer to be sure
// 4 line or so less in the real code (not if NULL etc)
void libereSiDoitEtreLiberer(void **ptr, int exitVal);

// cleans a pointer. Free and set to NULL
void cleanPtr(void **ptr);

// transforms the content of a file names datas_file_name into entire_msg.
// Writes the size in total_read
void file_to_bytes(char *datas_file_name, size_t *total_read, uint8_t *entire_msg);

// pour chaque bit de l'octet x, il peut être inversé à probErreur %
uint8_t introduceByteError(uint8_t x, int probError);

// affiche des octets avec 02X
void print_bytes(uint8_t *buf, size_t len);

// temps précis
double now_ms(void);

#endif
