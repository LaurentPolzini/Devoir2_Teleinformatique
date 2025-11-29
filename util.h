#ifndef __UTIL_H__
#define __UTIL_H__

// if malloc failed, returns NULL but i must clear pointer to be sure
void libereSiDoitEtreLiberer(void **ptr, int exitVal);

// cleans a pointer. Free and set to NULL
void cleanPtr(void **ptr);

void file_to_bytes(char *datas_file_name, size_t *total_read, uint8_t *entire_msg);

uint8_t introduceByteError(uint8_t x, int probError);

void print_bytes(uint8_t *buf, size_t len);

double now_ms(void);

#endif
