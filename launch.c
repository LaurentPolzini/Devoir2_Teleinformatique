#include <stdio.h>
#include <sys/time.h>
#include "protocole.h"
#include "util.h"
#include "canal.h"

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("\n\nUsage : ./exec_name proba_erreur_par_bit proba_perte_trame channel_delay\n");
        printf("Will use default this time :\n");
        printf("    - Proba erreur : %d\n", getProbErr());
        printf("    - Proba perte trame : %d\n", getProbLost());
        printf("    - Delay : %d\n\n", getDelay());
    } else {
        int prbErr = atoi(argv[1]);
        int prbLst = atoi(argv[2]);
        int delay = atoi(argv[3]);

        setPrbErr(prbErr);
        setPrbLst(prbLst);
        setDelay(delay);
    }

    // random seed
    srand(time(NULL));

    protocole_go_back_n("test.txt");
}
