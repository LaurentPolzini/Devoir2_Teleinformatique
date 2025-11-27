#ifndef __CANAL_H__
#define __CANAL_H__
#include "protocole.h"

#define SIZE_MAX_FRAME 107 // octets. Data max length = 100. 2 delimiters + adress + CRC 32 (4 bytes)
#define DELIMITER 0x7E // 01111110
#define SIZE_FILE_MAX 4096 // 10 Ko

static const size_t DATA_MAX_LEN = 100; // 100 octets est la taille maximale de la trame hors CRC et en tete (donc juste données)

tSendingFrame send_through_channel(tSendingFrame envoi);

uint8_t *create_frame(uint8_t adress, uint8_t *datas, size_t dataSize, unsigned char CRC);
uint8_t **framing(char *datas_file_name, uint8_t adress, unsigned char CRC, int *nbFrame);

void initiates_CRC(uint16_t *CRC_16, uint32_t *CRC_32, uint8_t *datas, int polynome);

int getTimeOut(void);

size_t getLeng(uint8_t *frame);

int verify_CRC(uint8_t *Tx, int polynome);

void printDataFrame(tSendingFrame frame, int CRC);
void afficheFrame(tSendingFrame frame, int CRC);

#endif
