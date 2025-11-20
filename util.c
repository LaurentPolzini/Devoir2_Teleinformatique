#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "util.h"


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
