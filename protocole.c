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

short physique_port_local = 2000;
short physique_port_destination = 2001;

int physique_socket;

char destination[10] = "localhost";

struct sSendingFrame {
    uint8_t *frame;
    int seqNum;
};

int N = 8; // 0 à 7. n = 3, N = 2^n
int tailleFenetre = 7; // window size = N - 1

unsigned char CRC = 0; // 0 : 16 ; 1 : 32 bits CRC

tSendingFrame addFrame(tSendingFrame frameToSend, uint8_t *frame) {
    frameToSend->frame = frame;
    return frameToSend;
}

tSendingFrame changeSeqNum(tSendingFrame frameToSend, int num) {
    frameToSend->seqNum = num;
    return frameToSend;
}

uint8_t *getFrame(tSendingFrame frameReady) {
    return frameReady->frame;
}

int getNumSeq(tSendingFrame frameReady) {
    return frameReady->seqNum;
}

/*
    Network initialization, with port. To be used by emitter and receptor

    1 : emission / 0 : reception
*/
void init(int emission) {
    unsigned short port_local;
    unsigned short port_distant;

    uid_t uid = getuid();

    if (emission) {
        port_local = (short)(uid % 60000 + 3000);
        port_distant = (short)(uid % 60000 + 2000);
    } else {
        port_local = (short)(uid % 60000 + 2000);
        port_distant = (short)(uid % 60000 + 3000);
    }

    struct sockaddr_in adr_locale;

    physique_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (physique_socket < 0)
    {
        perror("socket() erreur : ");
        exit(1);
    }

    adr_locale.sin_port = htons(physique_port_local);
    adr_locale.sin_family = AF_INET;
    adr_locale.sin_addr.s_addr = INADDR_ANY;

    if (bind(physique_socket, (struct sockaddr *)&adr_locale, 
        sizeof(adr_locale)) < 0) {
            perror("bind() erreur : ");
            close(physique_socket);

            exit(1);
    }
    printf("On utilise le port local %d et le port distant %d\n",
           port_local, port_distant);
}

tSendingFrame *prepareFrames(uint8_t **frames, int nbOfFrames) {
    tSendingFrame *framesToSend = malloc(sizeof(tSendingFrame) * nbOfFrames);
    libereSiDoitEtreLiberer((void **) &framesToSend, EXIT_FAILURE);

    for (int i = 0 ; i < nbOfFrames ; ++i) {
        framesToSend[i] = malloc(sizeof(struct sSendingFrame));
        libereSiDoitEtreLiberer((void **) &(framesToSend[i]), EXIT_FAILURE);
        framesToSend[i]->frame = frames[i];
        framesToSend[i]->seqNum = i % N;
    }

    return framesToSend;
}

/*
    Utilise les ports. Recoit depuis le reseau. 
*/
void recoit_reseau(tSendingFrame *frame)
{
    int l_data;

    l_data = recvfrom(physique_socket, (tSendingFrame *)frame, sizeof(tSendingFrame *), 0, NULL, NULL);
    if (l_data < 0)
    {
        perror("recvfrom() erreur.");
        close(physique_socket);
        exit(1);
    }
    printf("Frame recue\n");
}

/*
    Permet d'envoyer sur le reseau en utilisant les ports
*/
void envoie_reseau(tSendingFrame frame) {
    struct hostent *host;
    host = gethostbyname(destination);
    if (host == NULL) {
        perror("gethostbyname() erreur : ");
        close(physique_socket);
        exit(1);
    }
    
    struct sockaddr_in adresse_dest;
    adresse_dest.sin_port = htons(physique_port_destination);
    adresse_dest.sin_family = AF_INET;

    int l_adr = sizeof(adresse_dest);
    int l_data;

    l_data = sendto(
            physique_socket,
            (tSendingFrame) frame, sizeof(tSendingFrame), 0,
            (struct sockaddr *)&adresse_dest, l_adr);

    if (l_data < 0) {
        perror("sendto() n'a pas fonctionnée.");
        close(physique_socket);
        exit(1);
    }
    printf("Frame envoyée\n");
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
void go_back_n_emetteur(char *datas_file_name, uint8_t adress) {
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

    tSendingFrame *window = malloc(sizeof(tSendingFrame) * tailleFenetre);
    for (int i = 0 ; i < tailleFenetre && i < nbOfFrameToSend; ++i) {
        window[i] = preparedFrames[i];
        ++indNextFrameToAddToWindow;
    }
    int indexFirstElemWindow = 0;
    int currSend = 0;

    uint8_t *toSend;
    tSendingFrame modifiedFrame = malloc(sizeof(struct sSendingFrame));
    libereSiDoitEtreLiberer((void **) &modifiedFrame, EXIT_FAILURE);

    tSendingFrame ACK;
    int windowSlideInd = 0;

    int nbOfChangedFrame = indNextFrameToAddToWindow;
    // aim of this var : at the end i have [4,5,X,3]. I want to send 3 4 5. X is an old frame that hasn't been
    // modified since the last of the last frame is frame n°5. I don't want to send X, that's why i have this variable

    while ((receivedFrame != nbOfFrameToSend) && (nbTry < maxTry)) {
        /*
            envoie toute la fenetre puis attend l'ack. Slide la window et continue de tout envoyer
        */
        for (int i = 0 ; i < nbOfChangedFrame ; ++i) {
            currSend = (indexFirstElemWindow + i) % tailleFenetre;

            toSend = send_through_channel(window[currSend]->frame);
            modifiedFrame->frame = toSend;
            modifiedFrame->seqNum = window[currSend]->seqNum;

            envoie_reseau(modifiedFrame);
            ++nbOfFrameSent;

            free(toSend);
        }
        // TODO : fork w/ timer
        // if fork's death exit value received before ACK => send window again.

        // Receives ACK
        recoit_reseau(&ACK);
        ++nbOfACKReceived;
        nbOfChangedFrame = 0; // re init
        /*
            slides the window
        */
        while (window[windowSlideInd]->seqNum <= ACK->seqNum) {
            windowSlideInd = (windowSlideInd + 1) % tailleFenetre;
        }
        indexFirstElemWindow = windowSlideInd;
        for (int i = 0 ; i < windowSlideInd && indNextFrameToAddToWindow < nbOfFrameToSend ; ++i) { // nb elements a remplacer
            window[(indexFirstElemWindow + 1 + i) % tailleFenetre] = preparedFrames[indNextFrameToAddToWindow++];
            ++nbOfChangedFrame;
        }
    }
    // previent d'avoir fini
    modifiedFrame->frame = NULL;
    modifiedFrame->seqNum = N;
    while (ACK->seqNum != N && (nbTry < maxTry)) {
        envoie_reseau(modifiedFrame);
        recoit_reseau(&ACK);
        ++nbTry;
    }

    free(modifiedFrame);
    free(window);
    for (int i = 0 ; i < nbOfFrameToSend ; ++i) {
        free(preparedFrames[i]);
    }
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

    srand(time(NULL));
    int nbPaq = 100;
    tSendingFrame *receivedFrames = malloc(sizeof(tSendingFrame) * nbPaq);
    libereSiDoitEtreLiberer((void **) &receivedFrames, EXIT_FAILURE);
    tSendingFrame lastCorrectFrame, tmpReceived;
    tmpReceived->seqNum = N - 1;

    int nbFramesReceived = 0;
    int nbFrameWindowReceived = 0;

    tSendingFrame ACK = malloc(sizeof(struct sSendingFrame));
    libereSiDoitEtreLiberer((void **) &ACK, EXIT_FAILURE);
    ACK->frame = NULL;

    while (tmpReceived->seqNum != N) { // N => was last frame
        // je recois max tailleFenetre trame OU jusqu'a une erreur
        while (nbFrameWindowReceived < tailleFenetre || tmpReceived->seqNum != -1) {
            recoit_reseau(&tmpReceived);
            ++nbFrameRecues;
            if (tmpReceived->seqNum == N) {
                break;
            }
            if (verify_CRC()) { // TODO
                lastCorrectFrame = tmpReceived;
                ++nbFrameWindowReceived;
                ++nbFramesReceived;
                if (nbFramesReceived == nbPaq) {
                    receivedFrames = realloc(receivedFrames, nbPaq * 2);
                    nbPaq *= 2;
                }
            }
        }
        ACK = send_through_channel(ACK); // delay or lose
        changeSeqNum(ACK, lastCorrectFrame->seqNum);
        envoie_reseau(ACK);
        ++nbACKSent;
    }
    changeSeqNum(ACK, N);
    envoie_reseau(ACK); // do not try to send multiple times the ACK
    // if emittor has sent last frame then whatever, if he doesn't receive ACK np
    printf("Fin transmission coté recepteur.\n");
    printf("%d ACK envoyés.\n", nbACKSent);
    printf("%d trames reçues.\n", nbFrameRecues);
}


int main(int argc, char *argv[]) {
    
    /*
        lance 2 threads : emetteur et recepteur qui vont communiquer via le canal émulé
        (canal crée de la latence et des erreurs, réel échange par socket)
    */
    
    return 0;
}
