#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "canal.h"
#include "util.h"

#define SIZE_MAX_TRAME 100 // octets
#define DELIMITER 0x7E // 01111110
#define SIZE_FILE_MAX 4096 // 10 Ko

int verify_CRC(uint8_t *Tx, int polynome) {
    (void) Tx, (void) polynome;
    return 0;
}

/// @brief return the Cyclical Redundancy Code of the trame
/// @param CRC either 16 or 32 bits. Will contain result
/// @param trame must be only header and datas
void initiates_CRC(uint16_t *CRC_16, uint32_t *CRC_32, uint8_t *trame, int polynome) {

}

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
    Structure of a trame :
        - adress : 8 bits
        - command : 8 bits
        - datas: unknown size
        - CRC : 16 or 32 bits
        - 2 8-bits flags to delimit beginning and ending : 01111110

        minimum size total = 8 * 4 + 16 = 48
*/

/// @brief assembles the information into a trame. Since we are dealing with bits, i chose uint8
/// @param adress destination adress
/// @param command what type of trame is it
/// @param datas a file containing the datas to transmit
/// @param CRC 16 (0) or 32-bits (1) CRC
/// @return the whole trame, NULL if filename is null
uint8_t *create_trame(uint8_t adress, uint8_t command, unsigned char *datas_file_name, unsigned char CRC) {
    if (!datas_file_name) {
        return NULL;
    }
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

    size += 6;

    // reads message in text file
    uint8_t *entire_msg = malloc(sizeof(uint8_t) * SIZE_FILE_MAX);
    libereSiDoitEtreLiberer((void **) &entire_msg, EXIT_FAILURE);
    size_t totalRead = 0;

    reads_msg(datas_file_name, &totalRead, entire_msg);
    size += totalRead;

    // core of trame : header (adress + command) + msg. 
    uint8_t *core_trame = malloc(sizeof(uint8_t) * (2 + totalRead));
    libereSiDoitEtreLiberer((void **) &core_trame, EXIT_FAILURE);
    size_t size_core_trame = 0;
    core_trame[size_core_trame++] = adress;
    core_trame[size_core_trame++] = command;
    memcpy(&core_trame[size_core_trame], entire_msg, totalRead);
    size_core_trame += totalRead;

    // creates the CRC
    int poly = 16;
    initiates_CRC(FCS_16, FCS_32, core_trame, poly);

    // creates and assembles the trame
    uint8_t *trame = malloc(sizeof(uint8_t) * size);
    libereSiDoitEtreLiberer((void **) &trame, EXIT_FAILURE);
    size_t size_trame = 0;
    
    trame[size_trame++] = DELIMITER;
    memcpy(&trame[size_trame], core_trame, size_core_trame);
    size_trame += size_core_trame;

    if (CRC) {
        memcpy(&trame[size_trame], FCS_32, sizeof(uint32_t));
        size_trame += sizeof(uint32_t);
    } else {
        memcpy(&trame[size_trame], FCS_16, sizeof(uint16_t));
        size_trame += sizeof(uint16_t);
    }

    trame[size_trame++] = DELIMITER;

    // free local pointer
    cleanPtr((void **) &entire_msg);
    cleanPtr((void **) &FCS_16);
    cleanPtr((void **) &FCS_32);
    cleanPtr((void **) &core_trame);

    return trame;
}

void emulation(void) {
    return;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("4 arguments sont requis ! executable, proba erreur, proba perte, delai max");
        exit(EXIT_FAILURE);
    }

    int probErreur = (int) argv[1];
    int probPerte = (int) argv[2];
    int delaiMax = (int) argv[3]; // ms

    return 0;
}
