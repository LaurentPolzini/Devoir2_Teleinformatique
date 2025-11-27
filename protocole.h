#ifndef __PROTOCOLE_H__
#define __PROTOCOLE_H__

#include <unistd.h>
#include <stdlib.h>

#define DELIMITER 0x7E // 01111110
static const size_t DATA_MAX_LEN = 100; // 100 octets est la taille maximale de la trame hors CRC et en tete (donc juste données)

typedef struct frame_s {
    uint8_t commande;     /* type de paquet, cf. ci-dessous */
    uint8_t num_seq;      /* numéro de séquence */
    uint16_t somme_ctrl;   /* somme de contrôle */
    uint8_t info[DATA_MAX_LEN + 2];  /* données utiles du paquet + DELIMITERS */
    size_t lg_info;      /* longueur du champ info */
} frame_t;

#define DATA          1  /* données de l'application */
#define ACK           2  /* accusé de réception des données */
#define CON_REQ       3  /* demande d'établissement de connexion */
#define CON_ACCEPT    4  /* acceptation de connexion */
#define CON_REFUSE    5  /* refus d'établissement de connexion */
#define CON_CLOSE     6  /* notification de déconnexion */
#define CON_CLOSE_ACK 7  /* accusé de réception de la déconnexion */
#define OTHER         8  /* extensions */

void go_back_n_recepteur(void);
void go_back_n_emetteur(char *datas_file_name);

frame_t createFrame(uint8_t *datas, uint8_t seqnuM, uint8_t comm, size_t dataLeng);

//---- GETTERS ----
uint8_t getCommande(frame_t frame);

uint8_t getNum_seq(frame_t frame);

uint16_t getSomme_ctrl(frame_t frame);

uint8_t *getInfo(frame_t *frame);

uint8_t getLengthInfo(frame_t frame);


size_t getLengDatas(uint8_t *datas);

//---- SETTERS ----
void setCommande(frame_t *frame, uint8_t comm);

void setNum_seq(frame_t *frame, uint8_t numSeq);

void setSomme_ctrl(frame_t *frame, uint16_t ctrl_sum);

void setInfo(frame_t *frame, uint8_t *datas, size_t lengthDatas);

void setFrameLost(frame_t *frame);

frame_t *framesFromFile(char *file_name, int *nbOfFramesCreated);

uint16_t calculate_CRC(frame_t frame);
int verify_CRC(frame_t frame);

void afficheFrame(frame_t frame);

#endif
