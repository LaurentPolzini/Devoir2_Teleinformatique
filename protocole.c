#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "protocole.h"
#include "canal.h"
#include "util.h"

#define POLY 16

int N = 8; // 0 à 7. n = 3, N = 2^n
int tailleFenetre = 7; // window size = N - 1

frame_t createFrame(uint8_t *datas, uint8_t seqnuM, uint8_t comm, size_t dataLeng) {
    frame_t fram;
    if (dataLeng > DATA_MAX_LEN) {
        fprintf(stderr, "Trame trop grande !\n");
        exit(EXIT_FAILURE);
    }
    if (dataLeng > 0) {
        memcpy(&((fram.info)[1]), datas, dataLeng);
    }
    (fram.info)[0] = DELIMITER;
    (fram.info)[DATA_MAX_LEN - 1] = DELIMITER;
    fram.num_seq = seqnuM;
    fram.lg_info = dataLeng;
    fram.commande = comm;

    fram.somme_ctrl = calculate_CRC(fram);

    return fram;
}

frame_t *framesFromFile(char *file_name, int *nbOfFramesCreated) {
    // reads message in text file
    uint8_t *entire_msg = malloc(sizeof(uint8_t) * SIZE_FILE_MAX);
    libereSiDoitEtreLiberer((void **) &entire_msg, EXIT_FAILURE);
    size_t totalRead = 0;

    file_to_bytes(file_name, &totalRead, entire_msg);

    int nbFrameNecessary = (totalRead + DATA_MAX_LEN - 1) / DATA_MAX_LEN;

    uint8_t **datas_frac = malloc(nbFrameNecessary * sizeof(uint8_t *));
    libereSiDoitEtreLiberer((void **) &datas_frac, EXIT_FAILURE);

    /*
        Allocation de la place nécessaire
    */
    for (int i = 0; i < nbFrameNecessary; ++i) {
        size_t chunkSize = ((i == nbFrameNecessary - 1) && (totalRead % DATA_MAX_LEN))
                            ? (totalRead % DATA_MAX_LEN)
                            : DATA_MAX_LEN;

        datas_frac[i] = malloc(chunkSize + 2);
        libereSiDoitEtreLiberer((void **) &(datas_frac[i]), EXIT_FAILURE);

        memcpy(&(datas_frac[i][1]), &entire_msg[i * DATA_MAX_LEN], chunkSize);
    }
    frame_t *frames_army = malloc(sizeof(struct frame_s) * nbFrameNecessary);

    for (int i = 0; i < nbFrameNecessary; ++i) {
        size_t chunkSize = ((i == nbFrameNecessary - 1) && (totalRead % DATA_MAX_LEN))
                            ? (totalRead % DATA_MAX_LEN)
                            : DATA_MAX_LEN;
        frames_army[i] = createFrame(datas_frac[i], i % N, DATA, chunkSize);
    }

    *nbOfFramesCreated = nbFrameNecessary;

    for (int i = 0; i < nbFrameNecessary; ++i) {
        free(datas_frac[i]);
    }
    free(datas_frac);

    free(entire_msg);

    return frames_army;
}


//---- GETTERS ----
uint8_t getCommande(frame_t frame) {
    return frame.commande;
}

uint8_t getNum_seq(frame_t frame) {
    return frame.num_seq;
}

uint16_t getSomme_ctrl(frame_t frame) {
    return frame.somme_ctrl;
}

uint8_t *getInfo(frame_t *frame) {
    return frame->info;
}

uint8_t getLengthInfo(frame_t frame) {
    return frame.lg_info;
}


size_t getLengDatas(uint8_t *datas) {
    if (!datas || datas[0] == 0) {
        return 0;
    }
    size_t len = 1;

    while (datas[len++] != DELIMITER && len < DATA_MAX_LEN);

    return len;  // +1 pour inclure le délimiteur
}

//---- SETTERS ----
void setCommande(frame_t *frame, uint8_t comm) {
    frame->commande = comm;
}

void setNum_seq(frame_t *frame, uint8_t numSeq) {
    frame->num_seq = numSeq;
}

void setSomme_ctrl(frame_t *frame, uint16_t ctrl_sum) {
    frame->somme_ctrl = ctrl_sum;
}

void setInfo(frame_t *frame, uint8_t *datas, size_t lengDatas) {
    if (lengDatas <= DATA_MAX_LEN) {
        memcpy(&(frame->info), datas, lengDatas);
        frame->lg_info = lengDatas;
    } else {
        fprintf(stderr, "Autant de données ne peuvent être stockée dans une trame ! (length : %zu)", lengDatas);
        exit(EXIT_FAILURE);
    }
}

void setFrameLost(frame_t *frame) {
    frame->num_seq = -1;
    frame->info[0] = 0;
}

/*
    Affiche les données de la trame
*/
void afficheFrame(frame_t frame) {
    printf("Il y a %d octets de données : %s\n", getLengthInfo(frame), getInfo(&frame));
    printf("Son numéro de commande est %d\n", getCommande(frame));
    printf("Et son numéro de contrôle est %s.\n", verify_CRC(frame) ? "correct" : "incorrect");
}

/*
    Calcul le CRC 16 sur les données de la trame
*/
uint16_t calculate_CRC(frame_t frame) {
    (void) frame;
    return 0;
}

/*
    Vérifie le checksum
*/
int verify_CRC(frame_t frame) {
    return 1;
    
    (void) frame;
}


/*
    has a window with frames in it.
    loop
        sends window_size frames to receiver.
        Waits for ACK k, then window slides to k
        adds next frames to window
    stops if last frame is ACK or tried to send too many times the same frame (poor channel)

    sends the entire window in order

    parameters : uint8_t **frames, int nbOfFrames
*/
void go_back_n_emetteur(char *datas_file_name) {
    time_t tpsDeb = time(NULL);
    int nbOfFrameSent = 0;
    int nbOfACKReceived = 0;
    int maxTry = 1000;
    int nbTry = 0;

    int nbOfFrameToSend = 0;
    frame_t *framesReadyToBeSent = framesFromFile(datas_file_name, &nbOfFrameToSend);
    printf("I have to send %d frames\n", nbOfFrameToSend);

    int indNextFrameToAddToWindow = 0;

    int receivedFrame = 0;

    frame_t window[tailleFenetre];
    for (int i = 0 ; i < tailleFenetre && i < nbOfFrameToSend; ++i) {
        window[i] = framesReadyToBeSent[i];
        ++indNextFrameToAddToWindow;
    }
    int indexFirstElemWindow = 0;
    int currSend = 0;

    frame_t ack;
    ack = createFrame(0x00, -1, ACK, 0);

    int nbOfChangedFrame = indNextFrameToAddToWindow;
    // aim of this var : at the end i have [4,5,X,3]. I want to send 3 4 5. X is an old frame that hasn't been
    // modified since the last of the last frame is frame n°5. I don't want to send X, that's why i have this variable

    int changed = 0;
    int indLastChanged = 0;

    int lenToSend = tailleFenetre;

    while ((receivedFrame < nbOfFrameToSend) && (nbTry < maxTry)) {
        /*
            envoie toute la fenetre puis attend l'ack. Slide la window et continue de tout envoyer
        */
        if (nbOfFrameToSend - receivedFrame < tailleFenetre) {
            lenToSend = nbOfFrameToSend - receivedFrame;
        }
        for (int i = 0 ; i < lenToSend ; ++i) {
            currSend = (indexFirstElemWindow + i) % tailleFenetre;

            envoie_reseau(&(window[currSend]), getPhysicalDestEmission());

            ++nbOfFrameSent;
        }
        // TODO : fork w/ timer
        // if fork's death exit value received before ACK => send window again. ACK.seq_num = -1

        // Receives ACK
        do {
            recoit_reseau(&ack);
            printf("ACK received : %d\n", getNum_seq(ack));
            ++nbOfACKReceived;
        } while (!verify_CRC(ack));

        /*
            slides the window
        */
        nbOfChangedFrame = (2 + getNum_seq(ack) - indexFirstElemWindow + tailleFenetre) % (tailleFenetre + 1);
        indexFirstElemWindow = (indexFirstElemWindow + nbOfChangedFrame) % tailleFenetre;
        
        receivedFrame += nbOfChangedFrame;

        if (nbOfChangedFrame == 0) {
            ++nbTry;
        }

        /*
            changes the received frames
        */
        while (changed < nbOfChangedFrame && indNextFrameToAddToWindow < nbOfFrameToSend) {
            printf("Paquet n°%d recu, changement de celui-ci\n", getNum_seq(window[indLastChanged]));
            window[indLastChanged] = framesReadyToBeSent[indNextFrameToAddToWindow++];
            indLastChanged = (indLastChanged + 1) % tailleFenetre;
            ++changed;
        }

        changed = 0;
    }
    // previent d'avoir fini

    frame_t frameEnd = createFrame(0, N, CON_CLOSE, 0);
    
    while (getNum_seq(ack) != N && getCommande(ack) != CON_CLOSE_ACK && (nbTry < maxTry)) {
        printf("Je viens d'envoyer : %d\n", getNum_seq(frameEnd));
        envoie_reseau(&frameEnd, getPhysicalDestEmission());
        recoit_reseau(&ack);
        ++nbTry;
    }

    free(framesReadyToBeSent);
    time_t tpsFin = time(NULL);
    printf("Fin de la transmission coté emetteur.\n");
    printf("%d trames envoyées au total. %d trames étaient nécessaires.\n", nbOfFrameSent, nbOfFrameToSend);
    printf("%d trames ont donc été retransmises.\n", nbOfFrameSent - nbOfFrameToSend);
    printf("%d ACK reçus.\n", nbOfACKReceived);
    printf("%d temps de transmission total.\n", (int) difftime(tpsDeb, tpsFin));
}

/*

    can send while receiving : 1/3 of a chance that would happen
    receiving : 1/3 do i send ACK ?
    receiving : 1/3 do i send ACK ?

    // loop
        // 1/3 chance that i send ACK for a single frame
        // accepts frame if no error
        // do not accepts frame with seq n° > errored frame
        // puts frame into received frames array
        // sending the ack is just sending the last sequence number received
*/
void go_back_n_recepteur(void) {
    int nbACKSent = 0;
    int nbFrameRecues = 0;

    int nbPaq = 100;
    frame_t *receivedFrames = malloc(sizeof(struct frame_s) * nbPaq);
    libereSiDoitEtreLiberer((void **) &receivedFrames, EXIT_FAILURE);
    
    int lastCorrectSeqNum = -1;

    frame_t tmpReceived = createFrame(0, N - 1, OTHER, 0);

    int nbFramesReceived = 0;
    int nbFrameWindowReceived = 0;

    int CRC_ok = 1;

    frame_t ack = createFrame(0, -1, ACK, 0);

    while (getNum_seq(tmpReceived) != N) { // N => was last frame
        // je recois max tailleFenetre trame OU jusqu'a une erreur
        while (nbFrameWindowReceived < tailleFenetre && CRC_ok) {
            recoit_reseau(&tmpReceived);
            printf("Just received %d\n", getNum_seq(tmpReceived));
            ++nbFrameRecues;
            if (getNum_seq(tmpReceived) == N) {
                lastCorrectSeqNum = N;
                break;
            }
            CRC_ok = verify_CRC(tmpReceived);
            if (CRC_ok) {
                receivedFrames[nbFramesReceived] = createFrame(getInfo(&tmpReceived), getNum_seq(tmpReceived), getCommande(tmpReceived), getLengthInfo(tmpReceived));

                lastCorrectSeqNum = getNum_seq(tmpReceived);
                ++nbFrameWindowReceived;
                ++nbFramesReceived;
                if (nbFramesReceived >= nbPaq) {
                    size_t newSize = nbPaq * 2;
                    frame_t *tmp = realloc(receivedFrames, newSize * sizeof(struct frame_s));
                    if (!tmp) {
                        perror("realloc");
                        exit(EXIT_FAILURE);
                    }
                    receivedFrames = tmp;
                    nbPaq = newSize;
                }
            }
            setNum_seq(&ack, lastCorrectSeqNum);
            envoie_reseau(&ack, getPhysicalDestRcpt());
            ++nbACKSent;
            nbFrameWindowReceived = 0;
        }
    }
    setNum_seq(&ack, N);
    envoie_reseau(&ack, getPhysicalDestRcpt());
    
    printf("Fin transmission coté recepteur.\n");
    printf("%d ACK envoyés.\n", nbACKSent);
    printf("%d trames reçues.\n", nbFrameRecues);
    // TODO travail sur les received frames

    free(receivedFrames);
}


