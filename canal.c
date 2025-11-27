#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>

#include "util.h"
#include "canal.h"
#include "protocole.h"

frame_t send_through_channel(frame_t envoi);

short physique_port_local_emi = 2000;
short physique_port_destination_emi = 2001;

short physique_port_local_rcpt = 2000;
short physique_port_destination_rcpt = 2001;

int physique_socket;

char destination[10] = "localhost";

int probErreur = -1;
int probPerte = -1;
int delaiMax = 10;

int timeout = 20;


int getTimeOut(void) {
    return timeout;
}

int getPhysicalLocalEmission(void) {
    return physique_port_local_emi;
}

int getPhysicalLocalRcpt(void) {
    return physique_port_local_rcpt;
}

int getPhysicalDestEmission(void) {
    return physique_port_destination_emi;
}

int getPhysicalDestRcpt(void) {
    return physique_port_destination_rcpt;
}

int getPhysiqueSocket(void) {
    return physique_socket;
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

    if (emission) {
        printf("EMI : On utilise le port local %d et le port distant %d\n",
           port_local, port_distant);
    } else {
        printf("RECPT : On utilise le port local %d et le port distant %d\n",
           port_local, port_distant);
    }

    if (bind(physique_socket, (struct sockaddr *)&adr_locale, 
        sizeof(adr_locale)) < 0) {
            perror("bind() erreur : ");
            close(physique_socket);

            exit(1);
    }    
}

void closeChannel(void) {
    close(physique_socket);
}

/*
    Utilise les ports. Recoit depuis le reseau. 
*/
void recoit_reseau(frame_t *frame) {
    int l_data;

    l_data = recvfrom(physique_socket, (char *) frame, sizeof(struct frame_s), 0, NULL, NULL);

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
void envoie_reseau(frame_t *frame, short physicalPortDest) {
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
    
    adresse_dest.sin_port = htons(physicalPortDest);
    adresse_dest.sin_family = AF_INET;

    int l_data;

    *frame = send_through_channel(*frame);

    l_data = sendto(physique_socket, (char *) frame, sizeof(struct frame_s), 0, 
       (struct sockaddr *)&adresse_dest, l_adr);


    if (l_data < 0) {
        perror("sendto() n'a pas fonctionnée.");
        close(physique_socket);
        exit(1);
    }
    //printf("Frame envoyée\n");
}

/*
    Introduit des erreurs, du délai et peut perdre l'envoie.

    Emulation du canal. Transmet une trame ou un ACK.

    Pas besoin de la taille de la trame que l'on va envoyer : on a le flag de debut et fin qui permettent de delimiter
    mais pour le coup ca fait un passage dans une boucle qui n'aurait pas forcement était necessaire.
*/
frame_t send_through_channel(frame_t envoi) {
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

    float delai = (rand() % delaiMax); // delay is in ms
    usleep(delai * 1000); // en microsec

    return toSend;
}
/*
int main(void) {
    srand(time(NULL));

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
            go_back_n_emetteur("test.txt");
            waitpid(pidFils, NULL, 0);
    }
    exit(EXIT_SUCCESS);
}
*/
