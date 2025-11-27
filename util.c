#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "util.h"
#include "canal.h"


void libereSiDoitEtreLiberer(void **ptr, int exitVal) {
    if (!(*ptr)) {
        cleanPtr(ptr);
        printf("Erreur allocation mémoire");
        exit(exitVal);
    }
}

void cleanPtr(void **ptr) {
    if (*ptr) {
        free(*ptr);
        *ptr = NULL;
    }
}

/*
    reads a txt file and puts it into bytes
*/
void file_to_bytes(char *datas_file_name, size_t *total_read, uint8_t *entire_msg) {
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
    Introduces errors in a byte
*/
uint8_t introduceByteError(uint8_t x, int probError) {
    uint8_t xWerror = x;

    for (int i = 0; i < 8; i++) {
        if ((rand() % 100) <= probError) {
            xWerror ^= (1 << i);
        }
    }
    return xWerror;
}
