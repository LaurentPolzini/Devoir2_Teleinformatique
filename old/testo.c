#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "canal.h"
#include "protocole.h"

void testSlidingWindow(void) {
    srand(time(NULL));

    int N = 8;
    int windowSize = N - 1;
    int slidingWindow[windowSize];
    for (int i = 0 ; i < windowSize ; ++i) {
        slidingWindow[i] = i;
    }
    int lastNumTrame = windowSize - 1;

    int nbTrame = 15;
    int currSend;
    int indexFirstElemWindow = 0;
    int indLastChanged = 0;

    int nbOfChangedFrame = windowSize;

    int randomACKInd;

    int totalTrameRecues = 0;

    int changed = 0;

    int i = 0;

    while (totalTrameRecues < nbTrame) {
        printf("Fenetre :\n");
        printf("[");
        fflush(stdout);
        i = indexFirstElemWindow;
        while (i != ((indexFirstElemWindow - 1 + windowSize) % windowSize)) {
            printf("%d, ", slidingWindow[i]);
            fflush(stdout);
            i = (i + 1) % windowSize;
        }
        printf("%d]\n", slidingWindow[i]);

        for (int j = 0 ; j < windowSize; ++j) {
            currSend = (indexFirstElemWindow + j) % windowSize;
            printf("J'envoie trame n°%d\n", slidingWindow[currSend]);
        }
        nbOfChangedFrame = 0;

        randomACKInd = rand() % (windowSize + 1) - 1; // if -1, no frame received

        printf("index ack : %d\n", randomACKInd);

        nbOfChangedFrame = (2 + randomACKInd - indexFirstElemWindow + windowSize) % (windowSize + 1);
        indexFirstElemWindow = (indexFirstElemWindow + nbOfChangedFrame) % windowSize;

        totalTrameRecues += nbOfChangedFrame;

        printf("%d paquets recus\n", nbOfChangedFrame);

        while (changed < nbOfChangedFrame) {
            printf("Paquet n°%d recu\n", slidingWindow[indLastChanged]);
            lastNumTrame = (lastNumTrame + 1) % N;
            slidingWindow[indLastChanged] = lastNumTrame;
            indLastChanged = (indLastChanged + 1) % windowSize;
            ++changed;
        }

        changed = 0;

        printf("\n\n");
    }

    printf("J'ai recu au total %d trames\n", totalTrameRecues);
}

size_t testGetLeng(uint8_t *frame) {
    if (!frame) {
        return 0;
    }
    size_t len = 1;

    while (frame[len++] != DELIMITER);

    return len;  // +1 pour inclure le délimiteur
}

void testPreparedFrame(void) {
    uint8_t toto[3];
    toto[0] = DELIMITER;
    toto[1] = 0x3E;
    toto[2] = DELIMITER;

    uint8_t titi[3];
    titi[0] = DELIMITER;
    titi[1] = 0x7E;
    titi[2] = DELIMITER;

    uint8_t tata[4];
    tata[0] = DELIMITER;
    tata[1] = 0x93;
    tata[2] = 0x01;
    tata[3] = DELIMITER;

    int nbFrame = 3;
    uint8_t **frame = malloc(sizeof(uint8_t) * nbFrame);
    frame[0] = toto;
    frame[1] = titi;
    frame[2] = tata;

    tSendingFrame *prepared = prepareFrames(frame, nbFrame);

    for (int i = 0 ; i < nbFrame ; ++i) {
        printf("Mon num de seq : %d\n", getNumSeq(prepared[i]));
        printf("Ma valeur : %s\n", getFrame(prepared[i]));
    } 
}

void testConnection(void) {
    uint8_t toto[3];
    toto[0] = DELIMITER;
    toto[1] = 0x3E;
    toto[2] = DELIMITER;

    tSendingFrame frameTest = createSendingFrame(toto, 2);
    tSendingFrame received = createSendingFrame(NULL, 0);

    pid_t pidFils = -1;
    switch (pidFils = fork()) {
        case -1:
            printf("Error\n");
            exit(1);
        case 0:
            init(RECEPTION);
            recoit_reseau(received);
            printf("RECEP : num seq : %d, msg : %s\n\n", getNumSeq(received), getFrame(received));

            exit(EXIT_SUCCESS);
        default:
            init(EMISSION);
            envoie_reseau(frameTest, 2501);
            printf("ENVOIE num seq : %d, msg : %s\n\n", getNumSeq(frameTest), getFrame(frameTest));
            waitpid(pidFils, NULL, 0);
    }
}

void testFraming(void) {
    int xFrames = 0;
    uint8_t **frames = framing("test.txt", 0xEE, 0, &xFrames);

    printf("%d frames necessary\n", xFrames);

    
    for (int i = 0 ; i < xFrames ; ++i) {
        printf("Frame n°%d's size : %zu\n", i, getLeng(frames[i]));
        //afficheFrame(frames[i], 0);
    }
    
}

void testCreateFrame(void) {
    uint8_t toto[3] = "Bop";

    uint8_t *frameReady = create_frame(0x12, toto, sizeof(toto), 0);

    free(frameReady);

}

void testCommunication(void) {
    pid_t pidFils = -1;
    switch (pidFils = fork()) {
        case -1:
            printf("Error\n");
            exit(1);
        case 0:
            init(RECEPTION);
            go_back_n_recepteur();
            exit(EXIT_SUCCESS);
        default:
            init(EMISSION);
            go_back_n_emetteur("test.txt", 0xEE, 0);
            waitpid(pidFils, NULL, 0);
    }
    exit(EXIT_SUCCESS);
}

void testSendThrough(void) {
    uint8_t toto[3] = "Bop";
    uint8_t *frameReady = create_frame(0x12, toto, sizeof(toto), 0);

    printf("Creating a frame to be sent\n");
    tSendingFrame toSend = createSendingFrame(frameReady, 2);
    afficheFrame(toSend, 0);

    printf("Channeling\n");
    tSendingFrame tes = send_through_channel(toSend);

    afficheFrame(tes, 0);

    free(toSend);
    free(tes);
    free(frameReady);
}

/*
int main(void) {
    srand(time(NULL));

    testCommunication();
    
    return 0;
}
*/
