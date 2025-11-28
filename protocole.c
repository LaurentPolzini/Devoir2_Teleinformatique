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

#define PY_GET_FRAME 0
#define PY_CALC_CRC 1
#define PY_PARSES_FRAME 2

int N = 8; // 0 à 7. n = 3, N = 2^n
int tailleFenetre = 7; // window size = N - 1

/*
    Calcul le CRC 16 sur les données de la trame
*/
uint16_t calculate_CRC(frame_t *frame) {
    uint8_t *core = getCoreFrame(frame);
    size_t len = getLengthInfo(*frame) + 3; // commande + num_seq + lg_info

    // Convert core[] en hex string
    char hex[len * 2 + 1];
    for (size_t i = 0; i < len; i++)
        sprintf(hex + i * 2, "%02X", core[i]);

    hex[len * 2] = '\0';
    // Commande Python
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "python3 crc.py %d %s", PY_CALC_CRC, hex);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        perror("popen");
        exit(1);
    }

    // Python results
    char buffer[64];
    if (!fgets(buffer, sizeof(buffer), fp)) {
        perror("fgets");
        pclose(fp);
        exit(1);
    }

    // Nettoyer la chaîne ('\n')
    buffer[strcspn(buffer, "\n")] = 0;

    // conversion en 2 octets
    uint16_t crc_value = (uint16_t) strtol(buffer, NULL, 16);

    //printf("CRC obtenu Python = %hu\n", crc_value);

    pclose(fp);
    cleanPtr((void **) &core);

    return crc_value;
}

uint8_t *bits_to_uint8_array(const char *bits, size_t *out_len)
{
    if (!bits) return NULL;

    size_t len = strlen(bits);

    // Padding pour aligner à 8 bits
    size_t padded_len = len;
    if (padded_len % 8 != 0) {
        padded_len += 8 - (padded_len % 8);
    }

    // Copie + padding
    char *padded = malloc(padded_len + 1);
    if (!padded) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    strcpy(padded, bits);
    for (size_t i = len; i < padded_len; ++i) {
        padded[i] = '0';
    }
    padded[padded_len] = '\0';

    size_t byte_count = padded_len / 8;
    uint8_t *out = malloc(byte_count);
    if (!out) {
        free(padded);
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < byte_count; ++i) {
        char byte_str[9];
        memcpy(byte_str, padded + i*8, 8);
        byte_str[8] = '\0';
        out[i] = (uint8_t) strtol(byte_str, NULL, 2);
    }

    free(padded);
    *out_len = byte_count;
    return out;
}

// Converts a frame_t to a byte sequence by calling Python
uint8_t *frame_t_to_char_seq(frame_t *frame, size_t *lenFrame) {
    if (!frame) return NULL;

    size_t info_len = getLengthInfo(*frame);

    // Prepare hex payload
    char hex[info_len * 2 + 1];
    for (size_t i = 0; i < info_len; i++) {
        sprintf(hex + i*2, "%02X", getInfo(frame)[i]);
    }
    hex[info_len*2] = '\0';

    // Call Python to build HDLC frame with CRC & flags
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "python3 crc.py %d %d %d %zu %hu %s",
             PY_GET_FRAME,
             getCommande(*frame),
             getNum_seq(*frame),
             info_len,
             getSomme_ctrl(*frame),
             hex);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        perror("popen");
        exit(EXIT_FAILURE);
    }

    char buffer[8192];  // allow large frames
    if (!fgets(buffer, sizeof(buffer), fp)) {
        perror("fgets");
        pclose(fp);
        exit(EXIT_FAILURE);
    }
    pclose(fp);

    buffer[strcspn(buffer, "\n")] = 0; // remove newline

    // Optionally verify CRC locally
    if (!verify_CRC(frame)) {
        fprintf(stderr, "Erreur: CRC incorrect pour cette trame\n");
    }

    size_t frame_len;
    uint8_t *frame_bytes = bits_to_uint8_array(buffer, &frame_len);

    if (lenFrame) *lenFrame = frame_len;
    return frame_bytes;
}

frame_t parseFlux(uint8_t *flux, size_t len) {
    frame_t frame;

    char hex[SIZE_MAX_FRAME * 2 + 1];
    size_t actPos = 0;

    for (size_t i = 0; i < len; i++) {
        actPos += sprintf(hex + actPos, "%02X", flux[i]);
    }
    hex[actPos] = '\0';

    char cmd[10000];
    snprintf(cmd, sizeof(cmd), "python3 crc.py %d %s", PY_PARSES_FRAME, hex);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        perror("popen");
        exit(1);
    }

    char output[2000];
    if (!fgets(output, sizeof(output), fp)) {
        perror("python output");
        pclose(fp);
        exit(1);
    }
    pclose(fp);

    // Remove newline
    output[strcspn(output, "\n")] = 0;

    // parsing into frame structure
    char *token = strtok(output, ":");
    frame.commande = (uint8_t) atoi(token);

    token = strtok(NULL, ":");
    frame.num_seq = (uint8_t) atoi(token);

    token = strtok(NULL, ":");
    frame.lg_info = (uint8_t) atoi(token);

    token = strtok(NULL, ":");
    frame.somme_ctrl = (uint16_t) atoi(token);

    token = strtok(NULL, ":"); // hex payload
    if (!token) {
        fprintf(stderr, "Python payload missing\n");
        exit(1);
    }

    size_t hex_len = strlen(token);
    if (hex_len % 2 != 0) {
        fprintf(stderr, "Python payload invalid length\n");
        exit(1);
    }

    size_t byte_len = hex_len / 2;
    if (byte_len != frame.lg_info) {
        fprintf(stderr, "Python payload length mismatch: expected %zu, got %zu\n",
                frame.lg_info, byte_len);
    }

    for (size_t i = 0; i < byte_len && i < DATA_MAX_LEN; i++) {
        char byteHex[3] = { token[i*2], token[i*2+1], '\0' };
        frame.info[i] = (uint8_t) strtol(byteHex, NULL, 16);
    }

    return frame;
}


frame_t createFrame(uint8_t *datas, uint8_t seqnuM, uint8_t comm, size_t dataLeng) {
    frame_t fram;
    if (dataLeng > DATA_MAX_LEN) {
        fprintf(stderr, "Trame trop grande !\n");
        exit(EXIT_FAILURE);
    }
    setInfo(&fram, datas, dataLeng);
    fram.num_seq = seqnuM;
    fram.lg_info = dataLeng;
    fram.commande = comm;

    fram.somme_ctrl = calculate_CRC(&fram);

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

        datas_frac[i] = malloc(chunkSize);
        libereSiDoitEtreLiberer((void **) &(datas_frac[i]), EXIT_FAILURE);

        memcpy(datas_frac[i], &entire_msg[i * DATA_MAX_LEN], chunkSize);
    }
    frame_t *frames_army = malloc(sizeof(struct frame_s) * nbFrameNecessary);
    libereSiDoitEtreLiberer((void **) &frames_army, EXIT_FAILURE);

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

size_t getLengthInfo(frame_t frame) {
    return frame.lg_info;
}


size_t getLengDatas(uint8_t *datas) {
    if (!datas || datas[0] == 0) {
        return 0;
    }
    size_t len = DATA_MAX_LEN - 1;

    while (datas[len] != DELIMITER && len > 0) {
        --len;
    }

    return len;  // +1 pour inclure le délimiteur
}

uint8_t *getCoreFrame(frame_t *frame) {
    size_t sizeInfo = getLengthInfo(*frame);
    uint8_t *core = malloc(sizeof(uint8_t) * (3 + sizeInfo));
    libereSiDoitEtreLiberer((void **) &core, EXIT_FAILURE);

    core[0] = getCommande(*frame);
    core[1] = getNum_seq(*frame);
    core[2] = (uint8_t) sizeInfo;
    memcpy(&(core[3]), getInfo(frame), sizeInfo);

    return core;
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

// for introducing errors.
void setLengInfo(frame_t *frame, size_t lg) {
    frame->lg_info = lg;
}

void setFrameLost(frame_t *frame) {
    frame->num_seq = -1;
    frame->info[0] = 0;
}

/*
    Affiche les données de la trame
*/
void afficheFrame(frame_t *frame) {
    printf("Il y a %zu octets de données : %s\n", getLengthInfo(*frame), getInfo(frame));
    printf("Son numéro de commande est %d\n", getCommande(*frame));
    printf("Et son numéro de contrôle est %s.\n", verify_CRC(frame) ? "correct" : "incorrect");
}

/*
    Vérifie le checksum
*/
int verify_CRC(frame_t *frame) {
    return getSomme_ctrl(*frame) == calculate_CRC(frame);
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
        } while (!verify_CRC(&ack));

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


    fenetre d'anticipation de 1
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
            printf("En attente...\n");
            recoit_reseau(&tmpReceived);
            printf("J'ai juste recu une frame \n");
            afficheFrame(&tmpReceived);

            ++nbFrameRecues;
            if (getNum_seq(tmpReceived) == N) {
                lastCorrectSeqNum = N;
                break;
            }
            CRC_ok = verify_CRC(&tmpReceived);
            if (CRC_ok && (getNum_seq(tmpReceived) == (lastCorrectSeqNum + 1) % N)) {
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



