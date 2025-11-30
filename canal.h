#ifndef __CANAL_H__
#define __CANAL_H__

#include <unistd.h>
#include "protocole.h"

#define SIZE_MAX_FRAME 130 // octets. Data max length = 100. 2 delimiters + header (CRC, command, numSeq)
#define SIZE_FILE_MAX 10000 // 10 Ko

#define EMISSION 1
#define RECEPTION 0

// getters on channel parameters
int getTimeOut(void);

int getProbErr(void);

int getProbLost(void);

int getDelay(void);

// setters
void setPrbErr(int prbErr);

void setPrbLst(int prbLst);

void setDelay(int delay);


// simulation du canal : introduit de la perte, des erreurs et du delay
uint8_t *send_through_channel_byteSeq(uint8_t *envoi, size_t frameSiz);


/// ------- for real channel with a socket, not in use anymore
/*
    Few getters on channel information
*/
int getTimeOut(void);
int getPhysicalDestRcpt(void);
int getPhysicalDestEmission(void);
int getPhysicalLocalRcpt(void);
int getPhysicalLocalEmission(void);

/*
    Connecte les sockets et les ports
*/
void init(int emission);

void closeChannel(void);

/*
    Envoie via les ports de communication

    Attend une structure frame transformée en une sequence de bytes.
*/
void envoie_reseau(frame_t *frame, short physicalPortDest);

/*
    Recoit depuis le port d'entree

    Attend une structure de frame. Va parser le flux recu pour l'integrer dans la strucutre
*/
void recoit_reseau(frame_t *frame);

frame_t send_through_channel(frame_t envoi);

#endif
