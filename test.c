#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "canal.h"
#include "protocole.h"
#include "util.h"

int probPerteT = 0;
int probErreurT = 0;
int delaiMaxT = 100;

void TEST_go_back_n(char *datas_file_name);

frame_t send_through_channel(frame_t envoi) {
    int isLost = rand() % 100;
    frame_t toSend = createFrame(0, getNum_seq(envoi), getCommande(envoi), 0);
    if (isLost <= probPerteT) {
        printf("Frame perdu\n");
        return toSend;
    }

    uint8_t *datas = getInfo(&envoi);
    size_t lgDatas = getLengthInfo(envoi);
    
    uint8_t datasWError[lgDatas];

    for (size_t i = 0 ; i < lgDatas ; ++i) {
        datasWError[i] = introduceByteError(datas[i], probErreurT); // transform some 1 to 0 or 0 to 1 if error.
    }

    setInfo(&toSend, datasWError, introduceByteError(lgDatas, probErreurT));
    setNum_seq(&toSend, introduceByteError(getNum_seq(envoi), probErreurT));
    setCommande(&toSend, introduceByteError(getCommande(envoi), probErreurT));

    // TODO Error in CRC

    float delai = (rand() % delaiMaxT); // delay is in ms
    usleep(delai * 1000); // en microsec

    return toSend;
}

/*
uint8_t *send_through_channe_BYTES(frame_t *envoi) {
    int isLost = rand() % 100;
    size_t len;
    uint8_t *frames_to_bytes = frame_t_to_char_seq(envoi, &len);
    if (isLost <= probPerteT) {
        printf("Frame perdu\n");
        return NULL;
    }
    uint8_t datasWError[len];

    for (size_t i = 0 ; i < len ; ++i) {
        datasWError[i] = introduceByteError(frames_to_bytes[i], probErreurT); // transform some 1 to 0 or 0 to 1 if error.
    }

    // TODO Error in CRC

    float delai = (rand() % delaiMaxT); // delay is in ms
    usleep(delai * 1000); // en microsec

    return datasWError;
}
*/

void printBytes(uint8_t *bytes, size_t sizeInfo) {
    for (size_t i = 0; i < sizeInfo + 3; i++)
        printf("%02X ", bytes[i]);
    printf("\n");
}

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
        afficheFrame(&(frames[i]));
    }
}

void testConnection(void) {
    uint8_t toto[3] = "Bop";

    frame_t frameTest = createFrame(toto, 0, DATA, 3);
    frame_t receivedFrame = createFrame(0, -1, OTHER, 0);

    pid_t pidFils = -1;
    
    switch (pidFils = fork()) {
        case -1:
            printf("Error\n");
            exit(1);
        case 0:
            closeChannel();
            init(RECEPTION);
            recoit_reseau(&receivedFrame);
            printf("RECPT : \n");
            afficheFrame(&receivedFrame);

            exit(EXIT_SUCCESS);
        default:
            closeChannel();
            init(EMISSION);
            envoie_reseau(&frameTest, 2501);
            printf("EMISSION : \n");
            afficheFrame(&frameTest);
            waitpid(pidFils, NULL, 0);
    }
    closeChannel();
}

void testFraming(void) {
    int xFrames = 0;
    frame_t *frames = framesFromFile("test.txt", &xFrames);

    printf("%d frames necessary\n", xFrames);

    for (int i = 0 ; i < xFrames ; ++i) {
        printf("Frame n°%d's size : %zu\n", i, getLengthInfo(frames[i]));
        afficheFrame(&(frames[i]));
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
    afficheFrame(&toSend);

    printf("Channeling\n");
    frame_t tes = send_through_channelTEST(toSend, probPerte, probErr, delayMax);

    afficheFrame(&tes);
}
/*
void testByteSeq(void) {
    uint8_t blop[16] = "Toto titi tata";
    frame_t newFrame = createFrame(blop, 0, DATA, sizeof(blop));
    uint8_t *byteSeq = frame_t_to_byte_seq(newFrame);

    free(byteSeq);
}
*/

void testCalcCRC(void) {
    int nbOfFrames = 0;
    frame_t *frames = framesFromFile("test.txt", &nbOfFrames);
    printf("%d frames.\n", nbOfFrames);

    for (int i = 0 ; i < nbOfFrames ; ++i) {
        printf("Frame de longueur %zu. Son CRC calculé : %hu\n", getLengthInfo(frames[i]), calculate_CRC(&(frames[i])));
        printf("Son CRC dans sa structure : %hu\n", getSomme_ctrl(frames[i]));
    }
}

void testVerifyCRC(void) {
    int nbOfFrames = 0;
    frame_t *frames = framesFromFile("test.txt", &nbOfFrames);
    printf("%d frames.\n", nbOfFrames);

    for (int i = 0 ; i < nbOfFrames ; ++i) {
        printf("Le CRC est %s\n", verify_CRC(&(frames[i])) ? "correc" : "incorrec");
    }

    printf("Introducing a byte a error\n");
    setNum_seq(&(frames[0]), introduceByteError(getNum_seq(frames[0]), 100));

    printf("Apres introduction d'erreur, le CRC est %s\n", verify_CRC(&(frames[0])) ? "correc" : "incorrec");
}

void testFrameToCharSeq(void) {
    int nbOfFrames = 0;
    frame_t *frames = framesFromFile("test.txt", &nbOfFrames);
    printf("%d frames.\n", nbOfFrames);

    afficheFrame(&(frames[0]));
    size_t siz;

    uint8_t *frame_0_byteSeq = frame_t_to_char_seq(&(frames[0]), &siz);

    printf("Bytes sequence (size : %zu) :\n", siz);
    printBytes(frame_0_byteSeq, siz);

    cleanPtr((void **) &frame_0_byteSeq);
}

void testGetCoreFrame(void) {
    uint8_t toto[5] = "toto";
    frame_t toSend = createFrame(toto, 0, DATA, 5);

    uint8_t *core = getCoreFrame(&toSend); 

    for (size_t i = 0; i < 8; i++)
        printf("%02X ", core[i]);
    printf("\n");
    cleanPtr((void **)&core);

    printf("associated : %hu\n", getSomme_ctrl(toSend));
    printf("Calculated : %hu\n", calculate_CRC(&toSend));

    afficheFrame(&toSend);
}

void testParseFlux(void) {
    int nbOfFrames = 0;
    frame_t *frames = framesFromFile("test.txt", &nbOfFrames);

    size_t siz;
    uint8_t *frame_0_byteSeq = frame_t_to_char_seq(&(frames[1]), &siz);
    printf("Taille seq : %zu. CRC : %hu\n", siz, getSomme_ctrl(frames[1]));
    printBytes(frame_0_byteSeq, siz);
    
    frame_t frameParsed = parseFlux(frame_0_byteSeq, siz);

    afficheFrame(&frameParsed);


    cleanPtr((void **) &frame_0_byteSeq);
}

int main(void) {
    srand(time(NULL));
    //testParseFlux();
    //testFrameToCharSeq();

    TEST_go_back_n("test.txt");

    /*
    testCalcCRC();
    testVerifyCRC();
    
    
    testGetCoreFrame();

    
    printf("1- Test prepares frames\n");
    testPreparedFrame();
    printf("1- Passed\n");

    //printf("\n2- Test connection\n");
    //testConnection();
    //printf("2- Passed\n");
    
    printf("\n3- Test framing\n");
    testFraming();
    printf("3- Passed\n");

    printf("\n5- Test send through\n");
    testSendThrough();
    printf("5- Passed\n");
    */

    return 0;
}
int tailleFenetreT = 7;
int NT = 8;

int isInCurrFrameSent(int deb, int end, int idx) {
    int i = deb;

    while (1) {
        if (i == idx)
            return 1;

        if (i == end)
            break;

        i = (i + 1) % tailleFenetreT;
    }

    return 0;
}


int randomACK(frame_t *window, int debWindow, int lastOkACK) {
    int ind = rand() % tailleFenetreT;
    
    while (!isInCurrFrameSent(debWindow, lastOkACK, ind)) {
        ind = rand() % tailleFenetreT;
    }

    return getNum_seq(window[ind]);
}

void afficheMsgRecu(frame_t *frames, int nbFrames) {
    printf("\n");
    for (int i = 0 ; i < nbFrames ; ++i) {
        printf("%s ", getInfo(&(frames[i])));
        fflush(stdout);
    }
    printf("\n");
}

int getIndexFromFramSeq(frame_t *window, int nbFrame, int seq) {
    for (int i = 0 ; i < nbFrame ; ++i) {
        if (getNum_seq(window[i]) == seq) {
            return i;
        }
    }
    return -1;
}

void TEST_go_back_n(char *datas_file_name) {
    time_t tpsDeb = time(NULL);
    int nbOfFrameSent = 0;
    int nbOfACKReceived = 0;
    int maxTry = 1000;
    int nbTry = 0;

    int nbOfFrameToSend = 0;
    frame_t *framesReadyToBeSent = framesFromFile(datas_file_name, &nbOfFrameToSend);
    //printf("I have to send %d frames\n", nbOfFrameToSend);

    frame_t *receivedFrames = malloc(sizeof(frame_t) * nbOfFrameToSend);
    libereSiDoitEtreLiberer((void **) &receivedFrames, EXIT_FAILURE);
    int indRcv = 0;

    int indNextFrameToAddToWindow = 0;

    int receivedFrame = 0;

    frame_t window[tailleFenetreT];
    for (int i = 0 ; i < tailleFenetreT && i < nbOfFrameToSend; ++i) {
        window[i] = framesReadyToBeSent[i];
        ++indNextFrameToAddToWindow;
    }
    int indexFirstElemWindow = 0;
    int currSend = 0;

    frame_t ack;
    //frame_t modifiedFrame, modifiedACK;
    ack = createFrame(0x00, -1, ACK, 0);

    int nbOfChangedFrame = indNextFrameToAddToWindow;
    // aim of this var : at the end i have [4,5,X,3]. I want to send 3 4 5. X is an old frame that hasn't been
    // modified since the last of the last frame is frame n°5. I don't want to send X, that's why i have this variable

    int changed = 0;
    int indLastChanged = 0;

    int lenToSend = tailleFenetreT;

    int lastOKFrame = -1;


    //int generatedErrorFrame = 0;
    //int generatedErrorACK = 0;

    while ((receivedFrame < nbOfFrameToSend) && (nbTry < maxTry)) {
        /*
            envoie toute la fenetre puis attend l'ack. Slide la window et continue de tout envoyer
        */
        if (nbOfFrameToSend - receivedFrame < tailleFenetreT) {
            lenToSend = nbOfFrameToSend - receivedFrame;
        }
        for (int i = 0 ; i < lenToSend ; ++i) {
            currSend = (indexFirstElemWindow + i) % tailleFenetreT;
            
            //printf("J'envoie : \n");
            //afficheFrame(&(window[currSend]));
            /*
            //modified = send_through_channe_BYTES(&(window[currSend]));
            do {
                modifiedFrame = send_through_channel(window[currSend]);
                if (!verify_CRC(&modifiedFrame)) { // it pains me to do that but it's for the sake of datas
                    ++generatedErrorFrame;
                }
            } while (!verify_CRC(&modifiedFrame));

            */
            lastOKFrame = currSend;
            ++nbOfFrameSent;
        }
        // TODO : fork w/ timer
        // if fork's death exit value received before ACK => send window again. ACK.seq_num = -1

        // Receives ACK (random)
        setNum_seq(&ack, randomACK(window, indexFirstElemWindow, lastOKFrame));
        /*
        do {
            setNum_seq(&ack, randomACK(window, indexFirstElemWindow, lastOKFrame));
            modifiedFrame = send_through_channel(ack);
            if (!verify_CRC(&modifiedACK)) {
                ++generatedErrorACK;
            }
        } while (!(verify_CRC(&modifiedACK)));
        // finally got a correct ACK
        */
        ++nbOfACKReceived;
        //printf("Le recepteur a recu jusqu'a la frame n°%d\n", getNum_seq(ack));

        /*
            slides the window
        */
        int indACK = getIndexFromFramSeq(window, tailleFenetreT, getNum_seq(ack));
        nbOfChangedFrame = (indACK - indexFirstElemWindow + tailleFenetreT) % tailleFenetreT;
        if (nbOfChangedFrame == 0 && indACK == lastOKFrame) {
            nbOfChangedFrame = tailleFenetreT;
        }
        indexFirstElemWindow = (indexFirstElemWindow + nbOfChangedFrame) % tailleFenetreT;
        
        receivedFrame += nbOfChangedFrame;

        if (nbOfChangedFrame == 0) {
            ++nbTry;
        }

        /*
            changes the received frames
        */
        while (changed < nbOfChangedFrame && indNextFrameToAddToWindow < nbOfFrameToSend) {
            receivedFrames[indRcv++] = window[indLastChanged];
    
            window[indLastChanged] = framesReadyToBeSent[indNextFrameToAddToWindow++];
            indLastChanged = (indLastChanged + 1) % tailleFenetreT;
            ++changed;
        }

        changed = 0;
    }
    // previent d'avoir fini

    frame_t frameEnd = createFrame(0, NT, CON_CLOSE, 0);
    uint8_t *recpt;
    
    while (getNum_seq(ack) != NT && getCommande(ack) != CON_CLOSE_ACK && (nbTry < maxTry)) {
        //printf("J'envoie le paquet de fin de connection : %d\n", getNum_seq(frameEnd));
        recpt = send_through_channel_byteSeq(getInfo(&frameEnd), getLengthInfo(frameEnd));
        ++nbTry;
        if (recpt) {
            //printf("Le recepteur a recu le paquet de fin de connection\n");
            setNum_seq(&ack, NT);
        }
        
    }
    //printf("Voici l'ensemble des messages recus : \n");
    //afficheMsgRecu(receivedFrames, nbOfFrameToSend);

    free(framesReadyToBeSent);
    time_t tpsFin = time(NULL);
    printf("Transmission terminée.\n");
    printf("Frames envoyées : %d\n", nbOfFrameSent);
    printf("Frames retransmises : %d\n", nbOfFrameSent - nbOfFrameToSend);
    printf("ACK reçus : %d\n", nbOfACKReceived);
    //printf("%d trames erronées. %d ACK erronés.\n", generatedErrorFrame, generatedErrorACK);
    printf("Durée total de transmission : %fs.\n", difftime(tpsFin, tpsDeb));
}

