#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "canal.h"
#include "util.h"
#include "protocole.h"

size_t dataMaxLen = 100; // 100 octets est la taille maximale de la trame hors CRC et en tete (donc juste données)

int probErreur = -1;
int probPerte = -1;
int delaiMax = 10;

int timeout = 20;

int getTimeOut(void) {
    return timeout;
}

int verify_CRC(uint8_t *Tx, int polynome) {
    return 1;
    if (!Tx || Tx[0] == 0) {
        return 0;
    }
    (void) Tx, (void) polynome;
    return 1;
}

/// @brief return the Cyclical Redundancy Code of the frame
/// @param CRC either 16 or 32 bits. Will contain result
/// @param frame must be only header and datas
void initiates_CRC(uint16_t *CRC_16, uint32_t *CRC_32, uint8_t *frame, int polynome) {
    (void) CRC_16, (void) CRC_32, (void) frame, (void) polynome;
    *CRC_16 = 0x0102;
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

    size += 3; // adress + 2 DELIMITERS
    size += dataSize;

    // core of frame : adress + msg. 
    uint8_t *core_frame = malloc(sizeof(uint8_t) * (1 + dataSize));
    libereSiDoitEtreLiberer((void **) &core_frame, EXIT_FAILURE);
    size_t size_core_frame = 0;
    core_frame[size_core_frame++] = adress;
    memcpy(&core_frame[size_core_frame], datas, dataSize);
    size_core_frame += dataSize;

    // TODO STUFFING PAR LA (double stuffing meme : ca va mettre un 0 tous les 5 "1")
    // (mais j'aimerai n'avoir que des octets, donc a la fin du corps de la trame j'ajoute des 0
    // jusqu'a obtenir un octet complet)

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

    int nbFrameNecessary = (totalRead + dataMaxLen - 1) / dataMaxLen;

    uint8_t **datas_frac = malloc(nbFrameNecessary * sizeof(uint8_t *));
    if (!datas_frac) exit(EXIT_FAILURE);

    for (int i = 0; i < nbFrameNecessary; ++i) {
        size_t chunkSize = ((i == nbFrameNecessary - 1) && (totalRead % dataMaxLen))
                            ? (totalRead % dataMaxLen)
                            : dataMaxLen;

        datas_frac[i] = malloc(chunkSize);
        if (!datas_frac[i]) exit(EXIT_FAILURE);

        memcpy(datas_frac[i], &entire_msg[i * dataMaxLen], chunkSize);
    }


    uint8_t **frameSequence = malloc(nbFrameNecessary * sizeof(uint8_t *));
    for (int i = 0; i < nbFrameNecessary; ++i) {
        size_t chunkSize = ((i == nbFrameNecessary - 1) && (totalRead % dataMaxLen))
                            ? (totalRead % dataMaxLen)
                            : dataMaxLen;
        frameSequence[i] = create_frame(adress, datas_frac[i], chunkSize, CRC);
    }


    *nbFrame = nbFrameNecessary;

    for (int i = 0; i < nbFrameNecessary; ++i) {
        free(datas_frac[i]);
    }
    free(datas_frac);

    free(entire_msg);

    return frameSequence;
}

/*
    expects flags, else wouldn't be able to calculate length
*/
size_t getLeng(uint8_t *frame) {
    if (!frame || frame[0] == 0) {
        return 0;
    }
    size_t len = 1;

    while (frame[len++] != DELIMITER && len < SIZE_MAX_FRAME);

    return len;  // +1 pour inclure le délimiteur
}

/*
    Introduces errors in a byte
*/
uint8_t introduceByteError(uint8_t x) {
    uint8_t xWerror = x;

    for (int i = 0; i < 8; i++) {
        if ((rand() % 100) <= probErreur) {
            xWerror ^= (1 << i);
        }
    }
    return xWerror;
}

/*
    Emulation du canal. Transmet une trame ou un ACK. Introduit des erreurs, du délai et peut perdre l'envoie.

    Pas besoin de la taille de la trame que l'on va envoyer : on a le flag de debut et fin qui permettent de delimiter
    mais pour le coup ca fait un passage dans une boucle qui n'aurait pas forcement était necessaire.
*/
tSendingFrame send_through_channel(tSendingFrame envoi) {
    int isLost = rand() % 100; // proba erreur is 50 not 0.5
    tSendingFrame toSend = createSendingFrame(0, getNumSeq(envoi));
    if (isLost <= probPerte) {
        printf("Paquet perdu\n");
        return toSend;
    }

    uint8_t *cleanFrame = getFrame(envoi);
    size_t lenFrame = getFrameSize(envoi);
    
    uint8_t frameWError[lenFrame];

    for (size_t i = 0 ; i < lenFrame ; ++i) {
        frameWError[i] = introduceByteError(cleanFrame[i]); // transform some 1 to 0 or 0 to 1 if error.
    }
    float delai = (rand() % delaiMax); // delay is in ms

    addFrame(toSend, frameWError, lenFrame);
    usleep(delai * 1000); // en microsec

    return toSend;
}

void printDataFrame(tSendingFrame frame, int CRC) {
    size_t dataLen = getFrameSize(frame) - 1 - (CRC ? 4 : 2) - 2; // longueur du champ data
    if (dataLen > 0) {
        char *buffer = malloc(dataLen + 1);
        memcpy(buffer, &(getFrame(frame)[2]), dataLen);
        buffer[dataLen] = '\0';

        printf("%s\n", buffer);

        free(buffer);
    } else {
        printf("Taille nulle - pas de données\n");
    }
}

void afficheFrame(tSendingFrame frame, int CRC) {
    size_t frameSize = getFrameSize(frame);
    uint8_t *fram = getFrame(frame);
    printf("Frame size : %zu\n", frameSize);
    if (frameSize > 0) {
        printf("Adress : %hhu\n", fram[1]);

        size_t endData = frameSize - 1 - (CRC ? 4 : 2); // -2 : end DELIMITER + indexing
        printf("Size data : %zu\n", endData - 2);
        printf("Data : \n");

        printDataFrame(frame, CRC);    

        printf("CRC : \n");
        for (size_t i = endData ; i < frameSize - 1 ; ++i) {
            printf("%hhu ", fram[i]);
            fflush(stdout);
        }
        printf("\n");
    } else {
        printf("Taille nulle - pas de données\n");
    }
}

/*
int main(int argc, char *argv[]) {
    srand(time(NULL)); // seed of random calls
    if (argc != 4) {
        printf("4 arguments sont requis ! executable, proba erreur, proba perte, delai max");
        exit(EXIT_FAILURE);
    }

    probErreur = atof(argv[1]) * 100;
    probPerte = atof(argv[2]) * 100;
    delaiMax = atoi(argv[3]); // ms

    timeout = 2 * delaiMax; // dans protocole

    return 0;
}
*/
