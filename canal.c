#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "canal.h"
#include "util.h"
#include "protocole.h"

#define SIZE_MAX_FRAME 100 // octets
#define DELIMITER 0x7E // 01111110
#define SIZE_FILE_MAX 4096 // 10 Ko

size_t dataMaxLen = 100; // 100 octets est la taille maximale de la trame hors CRC et en tete (donc juste données)

float probErreur = 0;
float probPerte = 0;
int delaiMax = 0;

int timeout = 10;


int verify_CRC(uint8_t *Tx, int polynome) {
    (void) Tx, (void) polynome;
    return 0;
}

/// @brief return the Cyclical Redundancy Code of the frame
/// @param CRC either 16 or 32 bits. Will contain result
/// @param frame must be only header and datas
void initiates_CRC(uint16_t *CRC_16, uint32_t *CRC_32, uint8_t *frame, int polynome) {

}

/*
    reads a txt file and puts it into bytes
*/
void reads_msg(char *datas_file_name, size_t *total_read, uint8_t *entire_msg) {
    FILE *fptr = fopen(datas_file_name, "rb");  // lecture binaire
    if (!fptr) {
        fprintf(stderr, "Erreur : impossible d’ouvrir le fichier %s\n", datas_file_name);
        exit(EXIT_FAILURE);
    }

    // Lecture du contenu brut du fichier
    size_t bytes_read = fread(entire_msg, 1, SIZE_FILE_MAX, fptr);
    if (ferror(fptr)) {
        fprintf(stderr, "Erreur de lecture dans le fichier %s\n", datas_file_name);
        fclose(fptr);
        exit(EXIT_FAILURE);
    }

    if (fclose(fptr) == EOF) {
        fprintf(stderr, "Erreur durant la fermeture du fichier %s\n", datas_file_name);
        exit(EXIT_FAILURE);
    }

    *total_read = bytes_read;
}

/*
    Structure of a frame :
        - adress : 8 bits
        - datas: unknown size
        - CRC : 16 or 32 bits
        - 2 8-bits flags to delimit beginning and ending : 01111110

        minimum size total = 8 * 4 + 16 = 48
*/

/// @brief assembles the information into a frame. Since we are dealing with bits, i chose uint8
/// @param adress destination adress
/// @param datas a file containing the datas to transmit
/// @param CRC 16 (0) or 32-bits (1) CRC
/// @return the whole frame, NULL if filename is null
uint8_t *create_frame(uint8_t adress, uint8_t *datas, size_t dataSize, unsigned char CRC) {
    // initializations
    size_t size; // en octets
    uint16_t *FCS_16 = NULL;
    uint32_t *FCS_32 = NULL;
    if (CRC) {
        size = 4;
        FCS_32 = malloc(sizeof(uint32_t));
        libereSiDoitEtreLiberer((void **) &FCS_32, EXIT_FAILURE);
    } else {
        size = 2;
        FCS_16 = malloc(sizeof(uint16_t));
        libereSiDoitEtreLiberer((void **) &FCS_16, EXIT_FAILURE);
    }

    size += 3;
    size += dataSize;

    // core of frame : header (adress + command) + msg. 
    uint8_t *core_frame = malloc(sizeof(uint8_t) * (2 + dataSize));
    libereSiDoitEtreLiberer((void **) &core_frame, EXIT_FAILURE);
    size_t size_core_frame = 0;
    core_frame[size_core_frame++] = adress;
    memcpy(&core_frame[size_core_frame], datas, dataSize);
    size_core_frame += dataSize;

    // creates the CRC
    int poly = 16;
    initiates_CRC(FCS_16, FCS_32, core_frame, poly);

    // creates and assembles the frame
    uint8_t *frame = malloc(sizeof(uint8_t) * size);
    libereSiDoitEtreLiberer((void **) &frame, EXIT_FAILURE);
    size_t size_frame = 0;
    
    frame[size_frame++] = DELIMITER;
    memcpy(&frame[size_frame], core_frame, size_core_frame);
    size_frame += size_core_frame;

    if (CRC) {
        memcpy(&frame[size_frame], FCS_32, sizeof(uint32_t));
        size_frame += sizeof(uint32_t);
    } else {
        memcpy(&frame[size_frame], FCS_16, sizeof(uint16_t));
        size_frame += sizeof(uint16_t);
    }

    frame[size_frame++] = DELIMITER;

    // free local pointer
    cleanPtr((void **) &FCS_16);
    cleanPtr((void **) &FCS_32);
    cleanPtr((void **) &core_frame);

    return frame;
}

/*
    Receives a file with datas. I can put 100B of data into a single frame.
    It will cut datas into 100B segments and frame them
*/
uint8_t **framing(char *datas_file_name, uint8_t adress, unsigned char CRC, int *nbFrame) {
    // reads message in text file
    uint8_t *entire_msg = malloc(sizeof(uint8_t) * SIZE_FILE_MAX);
    libereSiDoitEtreLiberer((void **) &entire_msg, EXIT_FAILURE);
    size_t totalRead = 0;

    reads_msg(datas_file_name, &totalRead, entire_msg);

    size_t curseur = 0;
    int nbFrameNecessary = totalRead / dataMaxLen;

    uint8_t **datas_frac = calloc(nbFrameNecessary, sizeof(uint8_t *));
    libereSiDoitEtreLiberer((void **) &datas_frac, EXIT_FAILURE);

    size_t lastFrameSize = totalRead % dataMaxLen;

    int i = 0;
    while (totalRead >= dataMaxLen) {
        memcpy(datas_frac[i++], &(entire_msg[curseur]), dataMaxLen);
        totalRead -= dataMaxLen;
        curseur += dataMaxLen;
    }
    memcpy(datas_frac[i], &(entire_msg[curseur]), lastFrameSize); // what's left of totalRead

    uint8_t **frameSequence = calloc(nbFrameNecessary, sizeof(uint8_t *));
    libereSiDoitEtreLiberer((void **) &frameSequence, EXIT_FAILURE);
    for (int i = 0 ; i < nbFrameNecessary ; ++i) {
        if (i == nbFrameNecessary - 1) {
            frameSequence[i] = create_frame(adress, datas_frac[i], lastFrameSize, CRC);
        } else {
            frameSequence[i] = create_frame(adress, datas_frac[i], dataMaxLen, CRC);
        }
    }

    *nbFrame = nbFrameNecessary;

    cleanPtr((void **) &datas_frac);
    return frameSequence;
}

/*
    expects flags, else wouldn't be able to calculate length
*/
size_t getLeng(uint8_t *frame) {
    size_t len = 1;
    while (frame[len++] != DELIMITER);

    return len;
}

/*
    return a random number between 0x00 & 0xFF
    Aims to be logical !XORed with a byte.

    Returns smth like 0b11011101. It includes 2 errors
    with something like 0b11110111 !XOR 0b11011101 => 0b
*/
uint8_t randError(int prob) {
    return 0;
}

/*
    Emulation du canal. Transmet une trame ou un ACK. Introduit des erreurs, du délai et peut perdre l'envoie.

    Pas besoin de la taille de la trame que l'on va envoyer : on a le flag de debut et fin qui permettent de delimiter
    mais pour le coup ca fait un passage dans une boucle qui n'aurait pas forcement était necessaire.
*/
tSendingFrame *send_through_channel(tSendingFrame envoi) {
    srand(time(NULL)); // seed of random calls
    float isLost = (rand() % 100) / 100; // proba erreur is 0.05 not 5
    tSendingFrame *toSend = malloc(sizeof(tSendingFrame));
    if (isLost <= probPerte) {
        printf("Paquet perdu\n");
        addFrame(toSend, NULL);
        changeSeqNum(toSend, -1);

        return toSend;
    }
    size_t lenFrame = getLeng(envoi);
    
    // find length with flags
    uint8_t *frameWError = calloc(lenFrame, sizeof(uint8_t));
    libereSiDoitEtreLiberer((void **) &frameWError, EXIT_FAILURE);

    uint8_t *cleanFrame = getFrame(envoi);

    float err;
    for (size_t i = 0 ; i < lenFrame ; ++i) {
        err = (rand() % 100) / 100; // TODO smth wrong here
        frameWError[i] = (err <= probErreur) ? !cleanFrame[i] : cleanFrame[i]; // transform 1 to 0 or 0 to 1 if error.
    }
    float delai = (rand() % delaiMax) / 1000; // sleep is in seconds. Delay is in ms
    changeSeqNum(toSend, 0);
    
    sleep(delai);

    return toSend;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("4 arguments sont requis ! executable, proba erreur, proba perte, delai max");
        exit(EXIT_FAILURE);
    }

    probErreur = atof(argv[1]);
    probPerte = atof(argv[2]);
    delaiMax = (int) argv[3]; // ms

    timeout = 2 * delaiMax; // dans protocole

    return 0;
}
