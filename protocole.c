#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/select.h>
#include <ctype.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

#include "protocole.h"
#include "util.h"
#include "canal.h"

/*
    Protocole Go-Back-N simplifié :
        Nous avons un canal qui simule du délai, de la perte et des erreurs.
        Chaque trame passe par le canal avant d'etre effectivement envoyée par une socket.
            (Le canal est une fonction qui introduit des erreurs et peut dormir.)

        D'un coté et de l'autre du canal nous avons un émetteur et un recepteur.

        L'emetteur a une window de taille N - 1. Ici N = 8, donc la fenetre est de taille 7.
        Il envoie toute sa fenetre, lance un timer et attend un ACK. S'il ne le recoit pas avant que le timer
        sonne, on renvoit toute la fenetre. Ma derniere trame a envoyer vaut NULL mais son numéro de séquence vaut N

        Le recepteur attend taille_fenetre trames ou jusqu'a ce qu'un paquet soit perdu.
        Il envoie un ACK du dernier numéro de sequence obtenu.
        Il n'accepte pas les trames erronées.
        Il n'a pas de fenetre, il recoit les trames une par une et la verifie.
        Effectivement, comme les trames arrivent dans l'ordre, le recepteur n'a pas besoin de fenetre
        Il n'a pas de timer et s'arrete lorsqu'il recoit une trame numérotée N.
*/



#define PORT 8080


short physique_port_local_emi = 2000;
short physique_port_destination_emi = 2001;

short physique_port_local_rcpt = 2000;
short physique_port_destination_rcpt = 2001;

int physique_socket;

char destination[10] = "localhost";

struct sSendingFrame {
    uint8_t frame[SIZE_MAX_FRAME];
    size_t frameSize;
    int seqNum;
};

int N = 8; // 0 à 7. n = 3, N = 2^n
int tailleFenetre = 7; // window size = N - 1

//CRC : 0 : 16 ; 1 : 32 bits CRC

tSendingFrame createSendingFrame(uint8_t *framE, int seqnuM) {
    tSendingFrame fram = malloc(sizeof(struct sSendingFrame));
    size_t frameLeng = getLeng(framE);
    if (frameLeng > SIZE_MAX_FRAME) {
        fprintf(stderr, "Trame trop grande !\n");
        exit(EXIT_FAILURE);
    }
    if (frameLeng > 0) {
        memcpy(fram->frame, framE, frameLeng);
    } else {
        (fram->frame)[0] = 0;
    }
    fram->seqNum = seqnuM;
    fram->frameSize = frameLeng;
    return fram;
}

tSendingFrame addFrame(tSendingFrame frameToSend, uint8_t *frame, size_t frameSize) {
    if (frameSize <= SIZE_MAX_FRAME) {
        memcpy(frameToSend->frame, frame, frameSize);
        frameToSend->frameSize = frameSize;
    }
    return frameToSend;
}

tSendingFrame changeSeqNum(tSendingFrame frameToSend, int num) {
    frameToSend->seqNum = num;
    return frameToSend;
}

void recalculateLeng(tSendingFrame frameToSend) {
    frameToSend->frameSize = getLeng(getFrame(frameToSend));
}

uint8_t *getFrame(tSendingFrame frameReady) {
    return frameReady->frame;
}

int getNumSeq(tSendingFrame frameReady) {
    return frameReady->seqNum;
}

size_t getFrameSize(tSendingFrame frameReady) {
    return frameReady->frameSize;
}

/*
    Network initialization, with port. To be used by emitter and receptor

    1 : emission / 0 : reception
*/
void init(int emission) {
    uid_t uid = getuid();

    unsigned short port_local;
    unsigned short port_distant;

    if (emission) {
        port_local = uid % 60000 + 3000;
        port_distant = uid % 60000 + 2000;

        physique_port_local_emi = port_local;
        physique_port_destination_emi = port_distant;
    } else {
        port_local = uid % 60000 + 2000;
        port_distant = uid % 60000 + 3000;

        physique_port_local_rcpt = port_local;
        physique_port_destination_rcpt = port_distant;
    }

    struct sockaddr_in adr_locale;

    physique_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (physique_socket < 0)
    {
        perror("socket() erreur : ");
        exit(1);
    }

    adr_locale.sin_port = htons(port_local);
    adr_locale.sin_family = AF_INET;
    adr_locale.sin_addr.s_addr = INADDR_ANY;

    if (bind(physique_socket, (struct sockaddr *)&adr_locale, 
        sizeof(adr_locale)) < 0) {
            perror("bind() erreur : ");
            close(physique_socket);

            exit(1);
    }

    if (emission) {
        printf("EMI : On utilise le port local %d et le port distant %d\n",
           port_local, port_distant);
    } else {
        printf("RECPT : On utilise le port local %d et le port distant %d\n",
           port_local, port_distant);
    }
    
}

tSendingFrame *prepareFrames(uint8_t **frames, int nbOfFrames) {
    tSendingFrame *framesToSend = malloc(sizeof(struct sSendingFrame) * nbOfFrames);
    libereSiDoitEtreLiberer((void **) &framesToSend, EXIT_FAILURE);

    for (int i = 0 ; i < nbOfFrames ; ++i) {
        framesToSend[i] = createSendingFrame(frames[i], i % N);
    }

    return framesToSend;
}

/*
    Utilise les ports. Recoit depuis le reseau. 
*/
void recoit_reseau(tSendingFrame *frame) {
    int l_data;

    l_data = recvfrom(physique_socket, frame, sizeof(struct sSendingFrame), 0, NULL, NULL);

    if (l_data < 0)
    {
        perror("recvfrom() erreur.");
        close(physique_socket);
        exit(1);
    }
    //printf("Frame recue\n");
}

/*
    Permet d'envoyer sur le reseau en utilisant les ports
*/
void envoie_reseau(tSendingFrame *frame) {
    struct hostent *host;
    struct sockaddr_in adresse_dest;
    int l_adr = sizeof(adresse_dest);

    host = gethostbyname(destination);
    if (host == NULL) {
        perror("gethostbyname() erreur : ");
        close(physique_socket);
        exit(1);
    }

    memcpy((void *)&(adresse_dest.sin_addr), (void *)host->h_addr_list[0], host->h_length);
    
    adresse_dest.sin_port = htons(physique_port_destination_emi);
    adresse_dest.sin_family = AF_INET;

    int l_data;

    l_data = sendto(physique_socket, frame, sizeof(struct sSendingFrame), 0, 
       (struct sockaddr *)&adresse_dest, l_adr);


    if (l_data < 0) {
        perror("sendto() n'a pas fonctionnée.");
        close(physique_socket);
        exit(1);
    }
    //printf("Frame envoyée\n");
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
void go_back_n_emetteur(char *datas_file_name, uint8_t adress, int CRC) {
    int nbOfFrameSent = 0;
    int nbOfACKReceived = 0;
    time_t tpsDeb = time(NULL);

    int nbOfFrameToSend = 0;
    uint8_t **frames = framing(datas_file_name, adress, CRC, &nbOfFrameToSend);

    tSendingFrame *preparedFrames = prepareFrames(frames, nbOfFrameToSend);
    int indNextFrameToAddToWindow = 0;

    int receivedFrame = 0;

    int maxTry = 1000;
    int nbTry = 0;

    tSendingFrame window[tailleFenetre];
    for (int i = 0 ; i < tailleFenetre && i < nbOfFrameToSend; ++i) {
        window[i] = preparedFrames[i];
        ++indNextFrameToAddToWindow;
    }
    int indexFirstElemWindow = 0;
    int currSend = 0;

    tSendingFrame modifiedFrame, ACK;

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
        printf("%d trames a envoyer :\n", lenToSend);
        for (int i = 0 ; i < lenToSend ; ++i) {
            currSend = (indexFirstElemWindow + i) % tailleFenetre;

            modifiedFrame = send_through_channel(window[currSend]);
            printDataFrame(modifiedFrame, 0);

            envoie_reseau(&modifiedFrame);
            ++nbOfFrameSent;
        }
        // TODO : fork w/ timer
        // if fork's death exit value received before ACK => send window again. ACK.seq_num = -1

        // Receives ACK
        recoit_reseau(&ACK);
        printf("ACK received : %d\n", ACK->seqNum);
        ++nbOfACKReceived;

        /*
            slides the window
        */
        nbOfChangedFrame = (2 + ACK->seqNum - indexFirstElemWindow + tailleFenetre) % (tailleFenetre + 1);
        indexFirstElemWindow = (indexFirstElemWindow + nbOfChangedFrame) % tailleFenetre;
        
        receivedFrame += nbOfChangedFrame;

        if (nbOfChangedFrame == 0) {
            ++nbTry;
        }

        /*
            changes the received frames
        */
        while (changed < nbOfChangedFrame && indNextFrameToAddToWindow < nbOfFrameToSend) {
            printf("Paquet n°%d recu\n", window[indLastChanged]->seqNum);
            window[indLastChanged] = preparedFrames[indNextFrameToAddToWindow++];
            indLastChanged = (indLastChanged + 1) % tailleFenetre;
            ++changed;
        }

        changed = 0;
    }
    // previent d'avoir fini
    
    (modifiedFrame->frame)[0] = 0;
    modifiedFrame->frameSize = 0;
    modifiedFrame->seqNum = N;
    while (ACK->seqNum != N && (nbTry < maxTry)) {
        envoie_reseau(&modifiedFrame);
        recoit_reseau(&ACK);
        ++nbTry;
    }

    free(modifiedFrame);
    for (int i = 0 ; i < nbOfFrameToSend ; ++i) {
        free(preparedFrames[i]);
        free(frames[i]);
    }
    free(preparedFrames);
    free(frames);
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
    tSendingFrame *receivedFrames = malloc(sizeof(struct sSendingFrame) * nbPaq);
    libereSiDoitEtreLiberer((void **) &receivedFrames, EXIT_FAILURE);
    
    int lastCorrectSeqNum = -1;

    tSendingFrame tmpReceived;
    tmpReceived = malloc(sizeof(struct sSendingFrame));
    libereSiDoitEtreLiberer((void **) &tmpReceived, EXIT_FAILURE);

    tmpReceived->seqNum = N - 1;

    int nbFramesReceived = 0;
    int nbFrameWindowReceived = 0;

    int CRC_ok = 1;

    tSendingFrame ACK = malloc(sizeof(struct sSendingFrame));
    libereSiDoitEtreLiberer((void **) &ACK, EXIT_FAILURE);
    (ACK->frame)[0] = 0;
    ACK->seqNum = -1;

    tSendingFrame modifiedACK;

    while (tmpReceived->seqNum != N) { // N => was last frame
        // je recois max tailleFenetre trame OU jusqu'a une erreur
        while (nbFrameWindowReceived < tailleFenetre && CRC_ok) {
            recoit_reseau(&tmpReceived);
            printf("\nRECU\n");
            printDataFrame(tmpReceived, 0);
            printf("seq : %d\n", getNumSeq(tmpReceived));
            ++nbFrameRecues;
            if (tmpReceived->seqNum == N) {
                lastCorrectSeqNum = N;
                break;
            }
            CRC_ok = verify_CRC(getFrame(tmpReceived), 1); // TODO
            if (CRC_ok) {
                receivedFrames[nbFramesReceived] = createSendingFrame(tmpReceived->frame, tmpReceived->seqNum);

                lastCorrectSeqNum = getNumSeq(tmpReceived);
                printf("last seq num : %d\n", lastCorrectSeqNum);
                ++nbFrameWindowReceived;
                ++nbFramesReceived;
                if (nbFramesReceived >= nbPaq) {
                    size_t newSize = nbPaq * 2;
                    tSendingFrame *tmp = realloc(receivedFrames, newSize * sizeof(*receivedFrames));
                    if (!tmp) {
                        perror("realloc");
                        exit(EXIT_FAILURE);
                    }
                    receivedFrames = tmp;
                    nbPaq = newSize;
                }
            }
        }
        changeSeqNum(ACK, lastCorrectSeqNum);
        modifiedACK = send_through_channel(ACK); // delay or lost
        printf("ACK envoyé : %d\n", getNumSeq(modifiedACK));
        envoie_reseau(&modifiedACK);
        ++nbACKSent;
        nbFrameWindowReceived = 0;
    }
    printf("Fin transmission coté recepteur.\n");
    printf("%d ACK envoyés.\n", nbACKSent);
    printf("%d trames reçues.\n", nbFrameRecues);
    // TODO travail sur les received frames

    for (int i = 0 ; i < nbFramesReceived ; ++i) {
        free(receivedFrames[i]->frame);
        free(receivedFrames[i]);
    }
    free(receivedFrames);
    free(ACK);
}

/*
    Si je suis dans cette fonction c'est que je suis un fils (fork)
    Mon pere attend un ACK. Si je lui retourne une valeur avant qu'il ait recu l'ack
    il devra renvoyer sa fenetre d'emission
*/
void timer(void) {
    usleep(getTimeOut());
    exit(0);
}

/*
        lance 2 threads : emetteur et recepteur qui vont communiquer via le canal émulé
        (canal crée de la latence et des erreurs, réel échange par socket)
    */
/*
int main(int argc, char *argv[]) {
    (void) argc, (void) argv;
    srand(time(NULL));
    
    
    return 0;
}
*/
