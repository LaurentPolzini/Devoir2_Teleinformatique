#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "canal.h"
#include "protocole.h"
#include "util.h"

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
    uint8_t toto = 0x3E;

    uint8_t titi = 0x69;

    uint8_t tata = 0x93;

    frame_t *frames = malloc(sizeof(struct frame_s) * 3);
    frames[0] = createFrame(&toto, 0, DATA, 1);
    frames[1] = createFrame(&titi, 1, DATA, 1);
    frames[2] = createFrame(&tata, 2, DATA, 1);

    for (int i = 0 ; i < 3 ; ++i) {
        afficheFrame(frames[i]);
    }
}

void testConnection(void) {
    uint8_t toto[3];
    toto[0] = DELIMITER;
    toto[1] = 0x3E;
    toto[2] = DELIMITER;

    frame_t frameTest = createFrame(toto, 0, DATA, 0);
    frame_t receivedFrame = createFrame(0, -1, OTHER, 0);

    pid_t pidFils = -1;
    closeChannel();
    switch (pidFils = fork()) {
        case -1:
            printf("Error\n");
            exit(1);
        case 0:
            init(RECEPTION);
            recoit_reseau(&receivedFrame);
            printf("RECEP : num seq : %d, msg : %s\n\n", getNum_seq(receivedFrame), getInfo(&receivedFrame));

            exit(EXIT_SUCCESS);
        default:
            init(EMISSION);
            envoie_reseau(&frameTest, 2501);
            printf("ENVOIE num seq : %d, msg : %s\n\n", getNum_seq(frameTest), getInfo(&frameTest));
            waitpid(pidFils, NULL, 0);
    }
    closeChannel();
}

void testFraming(void) {
    int xFrames = 0;
    frame_t *frames = framesFromFile("test.txt", &xFrames);

    printf("%d frames necessary\n", xFrames);

    for (int i = 0 ; i < xFrames ; ++i) {
        printf("Frame n°%d's size : %hhu\n", i, getLengthInfo(frames[i]));
        afficheFrame(frames[i]);
    }
    
}

void testCommunication(void) {
    pid_t pidFils = -1;
    closeChannel();
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
            go_back_n_emetteur("test.txt");
            waitpid(pidFils, NULL, 0);
    }
    closeChannel();
}

frame_t send_through_channelTEST(frame_t envoi, int probPerte, int probErreur, int delayMax) {
    int isLost = rand() % 100;
    frame_t toSend = createFrame(0, getNum_seq(envoi), getCommande(envoi), 0);
    if (isLost <= probPerte) {
        printf("Frame perdu\n");
        return toSend;
    }

    uint8_t *datas = getInfo(&envoi);
    size_t lgDatas = getLengthInfo(envoi);
    
    uint8_t datasWError[lgDatas];

    for (size_t i = 0 ; i < lgDatas ; ++i) {
        datasWError[i] = introduceByteError(datas[i], probErreur); // transform some 1 to 0 or 0 to 1 if error.
    }

    setInfo(&toSend, datasWError, introduceByteError(lgDatas, probErreur));
    setNum_seq(&toSend, introduceByteError(getNum_seq(envoi), probErreur));
    setCommande(&toSend, introduceByteError(getCommande(envoi), probErreur));

    // TODO Error in CRC

    float delai = (rand() % delayMax); // delay is in ms
    usleep(delai * 1000); // en microsec

    return toSend;
}
void testSendThrough(void) {
    int probPerte = 1;
    int probErr = 5;
    int delayMax = 10; //ms
    uint8_t toto[3] = "Bop";

    printf("Creating a frame to be sent\n");
    frame_t toSend = createFrame(toto, 0, DATA, 3);
    afficheFrame(toSend);

    printf("Channeling\n");
    frame_t tes = send_through_channelTEST(toSend, probPerte, probErr, delayMax);

    afficheFrame(tes);
}


int main(void) {
    srand(time(NULL));

    printf("1- Test prepares frames\n");
    testPreparedFrame();
    printf("1- Passed\n");

    /*
    printf("\n2- Test connection\n");
    testConnection();
    printf("2- Passed\n");
    */

    printf("\n3- Test framing\n");
    testFraming();
    printf("3- Passed\n");

    /*
    printf("\n4- Test comm\n");
    testCommunication();
    printf("4- Passed\n");
    */

    printf("\n5- Test send through\n");
    testSendThrough();
    printf("5- Passed\n");
    
    return 0;
}

