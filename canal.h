#ifndef __CANAL_H__
#define __CANAL_H__

uint8_t *send_through_channel(uint8_t *envoi);

uint8_t **framing(char *datas_file_name, uint8_t adress, unsigned char CRC, int *nbFrame);

#endif
