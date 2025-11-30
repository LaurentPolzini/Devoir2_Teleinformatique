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

#define NB_TRY_MAX 500

int timedOut = 0;

int N = 8; // 0 à 7. n = 3, N = 2^n
int tailleFenetre = 7; // window size = N - 1

uint16_t calculate_CRC(const uint8_t *data, size_t len) {
    if (!data || len == 0) {
        return 0;
    }
    uint16_t crc = 0xFFFF; 
    uint16_t poly = 0x1021;

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)(data[i] << 8);
        
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ poly;
            } else {
                crc = (crc << 1);
            }
            crc &= 0xFFFF;
        }
    }
    return crc;
}

/*
    Associe les paramètres aux champs d'une nouvelle trame
*/
frame_t createFrame(uint8_t *datas, uint8_t seqnuM, uint8_t comm, size_t dataLeng) {
    frame_t fram;
    if (dataLeng > DATA_MAX_LEN) {
        fprintf(stderr, "Trame trop grande !\n");
        exit(EXIT_FAILURE);
    }
    setInfo(&fram, datas, dataLeng);
    fram.num_seq = seqnuM;
    fram.commande = seqnuM == UINT8_MAX ? OTHER : comm; // seqNum max => trame perdue
    fram.lg_info = dataLeng;

    // calcul du CRC
    uint8_t *tmp = malloc(sizeof(uint8_t) * dataLeng + 3);
    tmp[0] = fram.commande;
    tmp[1] = seqnuM;
    tmp[2] = dataLeng;
    memcpy(&(tmp[3]), datas, dataLeng);

    fram.somme_ctrl = calculate_CRC(tmp, dataLeng + 3);

    free(tmp);

    return fram;
}

/*
    Transforme un fichier ASCII en une suite
    de structure "frame"

    Le nombre de trames créées iront à la valeur de 
    l'adresse du pointeur nbOfFramesCreated
*/
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


//---- COMPARATORS ----
int array_frames_equals(frame_t *sent, frame_t *received, int nbFrames) {
    for (int i = 0 ; i < nbFrames ; ++i) {
        if (!compareFrames(&(sent[i]), &(received[i]))) {
            return 0;
        }
    }
    return 1;
}

int compareCommande(frame_t sent, frame_t received) {
    return getCommande(sent) == getCommande(received);
}

int compareNumSeq(frame_t sent, frame_t received) {
    return getNum_seq(sent) == getNum_seq(received);
}

int compareCtrlSum(frame_t sent, frame_t received) {
    return getSomme_ctrl(sent) == getSomme_ctrl(received);
}

int compareLg(frame_t sent, frame_t received) {
    return getLengthInfo(sent) == getLengthInfo(received);
}

int compareInfos(frame_t *sent, frame_t *received) {
    if (!compareLg(*sent, *received)) {
        return 0;
    }
    uint8_t *infoSent = getInfo(sent);
    uint8_t *infoReceived = getInfo(received);
    for (size_t i = 0 ; i < getLengthInfo(*sent) ; ++i) {
        if (infoSent[i] != infoReceived[i]) {
            return 0;
        }
    }
    return 1;
}

int compareFrames(frame_t *sent, frame_t *received) {
    return compareCommande(*sent, *received) && compareNumSeq(*sent, *received) && 
        compareCtrlSum(*sent, *received) && 
        compareInfos(sent, received);
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
    printf("Et mon numéro de séquence est : %hhu\n", getNum_seq(*frame));
}

int isLost(frame_t frame) {
    return getNum_seq(frame) == UINT8_MAX
        && getCommande(frame) == OTHER;
}

/*
    Vérifie le checksum

    Si OTHER - perdu
*/
int verify_CRC(frame_t *frame, uint16_t attendu) {
    return getCommande(*frame) != OTHER &&
     getSomme_ctrl(*frame) == attendu;
}

static inline void write_bit(uint8_t *out, size_t *byteIndex, int *bitIndex, int bit) {
    if (bit) {
        out[*byteIndex] |= (1 << (7 - *bitIndex));
    }
    (*bitIndex)++;
    if (*bitIndex == 8) {
        *bitIndex = 0;
        (*byteIndex)++;
        out[*byteIndex] = 0;
    }
}


uint8_t *stuff(const uint8_t *bytes, size_t len, size_t *outLen) {
    uint8_t *stuffed = calloc(len * 2, 1);
    size_t bi = 0;      // byte index
    int bitpos = 0;
    int count1 = 0;

    for (size_t i = 0; i < len; i++) {
        for (int b = 7; b >= 0; b--) {
            int bit = (bytes[i] >> b) & 1;

            write_bit(stuffed, &bi, &bitpos, bit);

            if (bit == 1) {
                count1++;
                if (count1 == 5) {
                    write_bit(stuffed, &bi, &bitpos, 0);
                    count1 = 0;
                }
            } else {
                count1 = 0;
            }
        }
    }

    if (bitpos != 0) bi++;
    *outLen = bi;
    return stuffed;
}

uint8_t *destuff(const uint8_t *bytes, size_t len, size_t *outLen) {
    uint8_t *out = calloc(len, 1);
    size_t bi = 0;
    int bitpos = 0;
    int count1 = 0;

    for (size_t i = 0; i < len; i++) {
        for (int b = 7; b >= 0; b--) {
            int bit = (bytes[i] >> b) & 1;

            if (count1 == 5) {
                // skip stuffed 0
                if (bit == 0) {
                    count1 = 0;
                    continue;
                }
            }

            write_bit(out, &bi, &bitpos, bit);

            if (bit == 1) count1++;
            else count1 = 0;
        }
    }

    if (bitpos != 0) bi++;
    *outLen = bi;
    return out;
}



uint8_t *frame_to_bytes_stuffed(frame_t *frame, size_t *lenConvertedFrame) {
    size_t lenRawFrame = getLengthInfo(*frame) + 5; // headers only

    uint8_t *raw = malloc(lenRawFrame);
    libereSiDoitEtreLiberer((void **) &raw, EXIT_FAILURE);

    raw[0] = getCommande(*frame);
    raw[1] = getNum_seq(*frame);
    uint16_t value = getSomme_ctrl(*frame);
    raw[2] = (value >> 8) & 0xFF;
    raw[3] = value & 0xFF;
    raw[4] = getLengthInfo(*frame);
    if (getLengthInfo(*frame) > 0) {
        memcpy(&raw[5], getInfo(frame), getLengthInfo(*frame));
    }

    size_t stuffedLen = 0;
    uint8_t *stuffed = stuff(raw, lenRawFrame, &stuffedLen);

    // allocate frame + 2 delimiters
    uint8_t *final = malloc(stuffedLen + 2);
    final[0] = DELIMITER;
    memcpy(&final[1], stuffed, stuffedLen);
    final[stuffedLen + 1] = DELIMITER;

    *lenConvertedFrame = stuffedLen + 2;

    free(raw);
    free(stuffed);

    return final;
}

/*
    RealCRC pour stocker le CRC recu dans la séquence d'octets.
    La frame retournée possède un champ CRC calculé lors de la création
    de la trame avec tous les champs entrés. Le CRC ne doit pas être set

    Ainsi, on pourra comparé la valeur calculée et la valeur attendue
*/
frame_t bytesToFrame_destuffed(uint8_t *bytes, size_t lenBytes, uint16_t *realCRC) {
    if (!bytes || lenBytes < 2 || bytes[0] != DELIMITER || bytes[lenBytes-1] != DELIMITER)
        return createFrame(0, UINT8_MAX, OTHER, 0);

    // remove delimiters
    size_t tmpSize = 0;
    uint8_t *tmp = destuff(&(bytes[1]), lenBytes - 2, &tmpSize);

    if (tmpSize < 5) {
        free(tmp);
        return createFrame(0, UINT8_MAX, OTHER, 0);
    }

    uint8_t comm = tmp[0];
    uint8_t seq = tmp[1];
    *realCRC = (tmp[2] << 8) | tmp[3];
    uint8_t len = tmp[4];

    if (len > DATA_MAX_LEN) {
        free(tmp);
        return createFrame(0, UINT8_MAX, OTHER, 0);
    }

    uint8_t *info = calloc(len, sizeof(uint8_t));
    memcpy(info, &(tmp[5]), len);

    frame_t frame = createFrame(info, seq, comm, len);

    cleanPtr((void**)&info);
    free(tmp);

    return frame;
}

/*
    Savoir si un index est dans la fenetre d'emission actuelle
    
    Exemple : 5 dans la fenetre 3-1 ([3,4,5,6,7,0,1]) ?
*/
int isInCurrFrameSent(int deb, int end, int idx) {
    if (idx == deb || idx == end) {
        return 1;
    }
    int i = deb;

    while (1) {
        if (i == idx) {
            return 1;
        }
        if (i == end) {
            break;
        }
        i = (i + 1) % tailleFenetre;
    }

    return 0;
}

// simule le fait que le recepteur peut etre plus rapide que l'emetteur
// et envoie un ACK pendant qu'il recoit la fenetre
uint8_t randomACK(frame_t *window, int debWindow, int lastOkACK) {
    if (lastOkACK == -1) {
        return UINT8_MAX; // trame perdue
    }
    int ind;
    do {
        ind = rand() % tailleFenetre;
    } while (!isInCurrFrameSent(debWindow, lastOkACK, ind)); 

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

void protocole_go_back_n(char *datas_file_name) {
    double tpsDeb = now_ms();
    double debTimer, finTimer;
    int nbOfFrameSent = 0;
    int nbOfACKReceived = 0;
    int receivedFrame = 0;
    int nbTry = 0;

    int nbOfFrameToSend = 0;
    frame_t ack;
    frame_t *framesReadyToBeSent = framesFromFile(datas_file_name, &nbOfFrameToSend);
    printf("I have to send %d frames\n", nbOfFrameToSend);

    frame_t *receivedFrames = malloc(sizeof(frame_t) * nbOfFrameToSend);
    libereSiDoitEtreLiberer((void **) &receivedFrames, EXIT_FAILURE);
    int indRcv = 0;

    int indNextFrameToAddToWindow = 0;

    frame_t window[tailleFenetre];
    for (int i = 0 ; i < tailleFenetre && i < nbOfFrameToSend; ++i) {
        window[i] = framesReadyToBeSent[i];
        ++indNextFrameToAddToWindow;
    }
    int indexFirstElemWindow = 0;
    int currSend = 0;

    frame_t modifiedFrame, modifiedACK;

    int nbOfChangedFrame = 0;
    // aim of this var : at the end i have [4,5,X,3]. I want to send 3 4 5. X is an old frame that hasn't been
    // modified since the last of the last frame is frame n°5. I don't want to send X, that's why i have this variable

    int indLastChanged = 0;

    int lenToSend = tailleFenetre;

    int lastOKFrame = UINT8_MAX;

    int generatedErrorFrame = 0;
    int generatedErrorACK = 0;
    
    int nbLostFrames = 0;
    int nbLostAck = 0;

    // for frames <-> (uint8_t *) conversions
    size_t lnConverted = 0;
    uint8_t *convertedFrame_t = NULL;

    // when creating a frame, CRC is automatically calculated
    // when converting a seq of bytes to a frame
    // CRC is included is the sequence. it will go in this var
    uint16_t realCRC = 0;

    uint8_t *ACK_frameThroughChannel = NULL;

    while ((receivedFrame < nbOfFrameToSend) && (nbTry < NB_TRY_MAX)) {
        /*
            envoie toute la fenetre puis attend l'ack. Slide la window et continue de tout envoyer
        */
        if (nbOfFrameToSend - receivedFrame < tailleFenetre) {
            lenToSend = nbOfFrameToSend - receivedFrame;
        }
        for (int i = 0 ; i < lenToSend ; ++i) {
            currSend = (indexFirstElemWindow + i) % tailleFenetre;
            
            printf("%fs - Envoie trame n°%hhu...\n", (now_ms() - tpsDeb) / 1000, getNum_seq(window[currSend]));
            ++nbOfFrameSent;

            convertedFrame_t = frame_to_bytes_stuffed(&(window[currSend]), &lnConverted);
            ACK_frameThroughChannel = send_through_channel_byteSeq(convertedFrame_t, lnConverted);
            modifiedFrame = bytesToFrame_destuffed(ACK_frameThroughChannel, lnConverted, &realCRC);
            free(ACK_frameThroughChannel);
            
            if (isLost(modifiedFrame)) {
                printf("trame perdue\n");
                ++nbLostFrames;
                break;
            }   
            if (!verify_CRC(&modifiedFrame, realCRC)) { // it pains me to do that but it's for the sake of datas
                printf("    Error generated...\n");
                ++generatedErrorFrame;
                break; // le recepteur ne donnera pas un ACK supérieur ou égal
                // au paquet erroné, on sort
            }
        
            lastOKFrame = currSend;
        }
        if (lastOKFrame == UINT8_MAX) {
            ack = createFrame(0, UINT8_MAX, ACK, 0); // ack de la derniere trame
            printf("%fs - Erreur dans la trame recue... attente de renvoie de l'emetteur\n", (now_ms() - tpsDeb) / 1000);
            timedOut = 1;
        } else {
            // au moins une trame correctement recue
            ack = createFrame(0, getNum_seq(window[lastOKFrame]), ACK, 0); // ack de la derniere trame
            printf("%fs - Envoie ACK n°%hhu...\n", (now_ms() - tpsDeb) / 1000, getNum_seq(ack));
            convertedFrame_t = frame_to_bytes_stuffed(&ack, &lnConverted);

            debTimer = now_ms();
            ACK_frameThroughChannel = send_through_channel_byteSeq(convertedFrame_t, lnConverted);
            finTimer = now_ms();

            modifiedACK = bytesToFrame_destuffed(ACK_frameThroughChannel, lnConverted, &realCRC);
            free(ACK_frameThroughChannel);

            if (finTimer - debTimer >= getTimeOut()) {
                timedOut = 1;
                printf("Time out !\n");
            }

            if (!(isLost(modifiedACK) || timedOut)) {
                ++nbOfACKReceived;
            }

            if (isLost(modifiedACK)) {
                printf("ack perdu\n");
                ++nbLostAck;
            } else {
                if (!verify_CRC(&modifiedACK, realCRC)) {
                    printf("crc de l'ack incorrect\n");
                    ++generatedErrorACK;
                }
            }
        }

        /*
            slides the window. Bornes inclusives

            Slides if CRC ok and not lost
        */
        if (verify_CRC(&modifiedACK, realCRC) && !isLost(modifiedACK) && !timedOut) {
            printf("%fs - ACK recu n°%hhu...\n", (now_ms() - tpsDeb) / 1000, getNum_seq(modifiedACK));
            while (getNum_seq(window[indexFirstElemWindow]) != getNum_seq(modifiedACK)) {
                ++nbOfChangedFrame;
                indexFirstElemWindow = (indexFirstElemWindow + 1) % tailleFenetre;
            }
            // inclusion de la derniere borne
            if (getNum_seq(window[indexFirstElemWindow]) == getNum_seq(modifiedACK)) {
                ++nbOfChangedFrame;
                indexFirstElemWindow = (indexFirstElemWindow + 1) % tailleFenetre;
            }
        } 

        // sending the same window again
        if (nbOfChangedFrame == 0) {
            ++nbTry;
        }

        /*
            changes the received frames
        */
        for (int i = 0 ; i < nbOfChangedFrame ; ++i) {
            receivedFrames[indRcv++] = window[indLastChanged];

            window[indLastChanged] = framesReadyToBeSent[indNextFrameToAddToWindow++];
            indLastChanged = (indLastChanged + 1) % tailleFenetre;

            ++receivedFrame;
        }

         // re init des vars
        lastOKFrame = UINT8_MAX;
        nbOfChangedFrame = 0;
        timedOut = 0;
    }
    if (nbTry >= NB_TRY_MAX) {
        printf("Trop d'erreur sur le canal, sortie\n"); 
        return;
    }

    /*
        previent d'avoir fini
    */
   /*
    frame_t frameEnd = createFrame(0, N, CON_CLOSE, 0);
    uint8_t *convertedEnding;
    size_t lnconvertedEnding;
    
    do {
        printf("%fs Envoie fin connexion...\n", (now_ms() - tpsDeb) / 1000);
        convertedEnding = frame_to_bytes_stuffed(&frameEnd, &lnconvertedEnding);
        ACK_frameThroughChannel = send_through_channel_byteSeq(convertedEnding, lnconvertedEnding);
        modifiedFrame = bytesToFrame_destuffed(ACK_frameThroughChannel, lnconvertedEnding, &realCRC);
        free(ACK_frameThroughChannel);

        if (!verify_CRC(&modifiedFrame, realCRC)) {
            ++generatedErrorFrame;
            ++nbTry;
            printf("Error...\n");
        }
    } while (!verify_CRC(&modifiedFrame, realCRC));
    // ca y est, j'ai recu une frame sans erreur
    do {
        ack = createFrame(0, N, CON_CLOSE_ACK, 0);
        convertedFrame_t = frame_to_bytes_stuffed(&ack, &lnConverted);
        ACK_frameThroughChannel = send_through_channel_byteSeq(convertedFrame_t, lnConverted);
        modifiedACK = bytesToFrame_destuffed(ACK_frameThroughChannel, lnConverted, &realCRC);
        free(ACK_frameThroughChannel);

        if (!verify_CRC(&modifiedACK, realCRC)) {
            ++generatedErrorACK;
            ++nbTry;
        }
    } while (!verify_CRC(&modifiedACK, realCRC));
     */
    printf("%fs Reception, terminaison de la connexion...\n", (now_ms() - tpsDeb) / 1000);
    // ca y est, j'ai reussi a prevenir 
    // l emetteur que j'ai bien recu sa frame de fin de connexion

    printf("Voici l'ensemble des messages recus : \n");
    afficheMsgRecu(receivedFrames, nbOfFrameToSend);

    double tpsFin = now_ms();
    printf("Transmission terminée. La réception est %s de l'emission\n", array_frames_equals(receivedFrames, framesReadyToBeSent, nbOfFrameToSend) ? "egale" : "differente");
    printf("Frames envoyées : %d\n", nbOfFrameSent);
    printf("Frames retransmises : %d\n", nbOfFrameSent - nbOfFrameToSend);
    printf("ACK reçus : %d\n", nbOfACKReceived);
    printf("%d trames erronées. %d ACK erronés.\n", generatedErrorFrame, generatedErrorACK);
    printf("%d trames perdues. %d ACK perdus.\n", nbLostFrames, nbLostAck);
    printf("Durée totale de transmission : %fs.\n", (tpsFin - tpsDeb) / 1000);

    free(framesReadyToBeSent);
    free(convertedFrame_t);
    //free(convertedEnding);
}


//----------------------

// /*
//     has a window with frames in it.
//     loop
//         sends window_size frames to receiver.
//         Waits for ACK k, then window slides to k
//         adds next frames to window
//     stops if last frame is ACK or tried to send too many times the same frame (poor channel)

//     sends the entire window in order

//     parameters : uint8_t **frames, int nbOfFrames
// */
// void go_back_n_emetteur(char *datas_file_name) {
//     time_t tpsDeb = time(NULL);
//     int nbOfFrameSent = 0;
//     int nbOfACKReceived = 0;
//     int maxTry = 1000;
//     int nbTry = 0;

//     int nbOfFrameToSend = 0;
//     frame_t *framesReadyToBeSent = framesFromFile(datas_file_name, &nbOfFrameToSend);
//     printf("I have to send %d frames\n", nbOfFrameToSend);

//     int indNextFrameToAddToWindow = 0;

//     int receivedFrame = 0;

//     frame_t window[tailleFenetre];
//     for (int i = 0 ; i < tailleFenetre && i < nbOfFrameToSend; ++i) {
//         window[i] = framesReadyToBeSent[i];
//         ++indNextFrameToAddToWindow;
//     }
//     int indexFirstElemWindow = 0;
//     int currSend = 0;

//     frame_t ack;
//     ack = createFrame(0x00, -1, ACK, 0);

//     int nbOfChangedFrame = indNextFrameToAddToWindow;
//     // aim of this var : at the end i have [4,5,X,3]. I want to send 3 4 5. X is an old frame that hasn't been
//     // modified since the last of the last frame is frame n°5. I don't want to send X, that's why i have this variable

//     int changed = 0;
//     int indLastChanged = 0;

//     int lenToSend = tailleFenetre;

//     while ((receivedFrame < nbOfFrameToSend) && (nbTry < maxTry)) {
//         /*
//             envoie toute la fenetre puis attend l'ack. Slide la window et continue de tout envoyer
//         */
//         if (nbOfFrameToSend - receivedFrame < tailleFenetre) {
//             lenToSend = nbOfFrameToSend - receivedFrame;
//         }
//         for (int i = 0 ; i < lenToSend ; ++i) {
//             currSend = (indexFirstElemWindow + i) % tailleFenetre;

//             envoie_reseau(&(window[currSend]), getPhysicalDestEmission());

//             ++nbOfFrameSent;
//         }
//         // TODO : fork w/ timer
//         // if fork's death exit value received before ACK => send window again. ACK.seq_num = -1

//         // Receives ACK
//         do {
//             recoit_reseau(&ack);
//             printf("ACK received : %d\n", getNum_seq(ack));
//             ++nbOfACKReceived;
//         } while (!verify_CRC(&ack));

//         /*
//             slides the window
//         */
//         nbOfChangedFrame = (2 + getNum_seq(ack) - indexFirstElemWindow + tailleFenetre) % (tailleFenetre + 1);
//         indexFirstElemWindow = (indexFirstElemWindow + nbOfChangedFrame) % tailleFenetre;
        
//         receivedFrame += nbOfChangedFrame;

//         if (nbOfChangedFrame == 0) {
//             ++nbTry;
//         }

//         /*
//             changes the received frames
//         */
//         while (changed < nbOfChangedFrame && indNextFrameToAddToWindow < nbOfFrameToSend) {
//             printf("Paquet n°%d recu, changement de celui-ci\n", getNum_seq(window[indLastChanged]));
//             window[indLastChanged] = framesReadyToBeSent[indNextFrameToAddToWindow++];
//             indLastChanged = (indLastChanged + 1) % tailleFenetre;
//             ++changed;
//         }

//         changed = 0;
//     }
//     // previent d'avoir fini

//     frame_t frameEnd = createFrame(0, N, CON_CLOSE, 0);
    
//     while (getNum_seq(ack) != N && getCommande(ack) != CON_CLOSE_ACK && (nbTry < maxTry)) {
//         printf("Je viens d'envoyer : %d\n", getNum_seq(frameEnd));
//         envoie_reseau(&frameEnd, getPhysicalDestEmission());
//         recoit_reseau(&ack);
//         ++nbTry;
//     }

//     free(framesReadyToBeSent);
//     time_t tpsFin = time(NULL);
//     printf("Fin de la transmission coté emetteur.\n");
//     printf("%d trames envoyées au total. %d trames étaient nécessaires.\n", nbOfFrameSent, nbOfFrameToSend);
//     printf("%d trames ont donc été retransmises.\n", nbOfFrameSent - nbOfFrameToSend);
//     printf("%d ACK reçus.\n", nbOfACKReceived);
//     printf("%d temps de transmission total.\n", (int) difftime(tpsDeb, tpsFin));
// }

// /*

//     can send while receiving : 1/3 of a chance that would happen
//     receiving : 1/3 do i send ACK ?
//     receiving : 1/3 do i send ACK ?

//     // loop
//         // 1/3 chance that i send ACK for a single frame
//         // accepts frame if no error
//         // do not accepts frame with seq n° > errored frame
//         // puts frame into received frames array
//         // sending the ack is just sending the last sequence number received


//     fenetre d'anticipation de 1
// */
// void go_back_n_recepteur(void) {
//     int nbACKSent = 0;
//     int nbFrameRecues = 0;

//     int nbPaq = 100;
//     frame_t *receivedFrames = malloc(sizeof(struct frame_s) * nbPaq);
//     libereSiDoitEtreLiberer((void **) &receivedFrames, EXIT_FAILURE);
    
//     int lastCorrectSeqNum = -1;

//     frame_t tmpReceived = createFrame(0, N - 1, OTHER, 0);

//     int nbFramesReceived = 0;
//     int nbFrameWindowReceived = 0;

//     int CRC_ok = 1;

//     frame_t ack = createFrame(0, -1, ACK, 0);

//     while (getNum_seq(tmpReceived) != N) { // N => was last frame
//         // je recois max tailleFenetre trame OU jusqu'a une erreur
//         while (nbFrameWindowReceived < tailleFenetre && CRC_ok) {
//             printf("En attente...\n");
//             recoit_reseau(&tmpReceived);
//             printf("J'ai juste recu une frame \n");
//             afficheFrame(&tmpReceived);

//             ++nbFrameRecues;
//             if (getNum_seq(tmpReceived) == N) {
//                 lastCorrectSeqNum = N;
//                 break;
//             }
//             CRC_ok = verify_CRC(&tmpReceived);
//             if (CRC_ok && (getNum_seq(tmpReceived) == (lastCorrectSeqNum + 1) % N)) {
//                 receivedFrames[nbFramesReceived] = createFrame(getInfo(&tmpReceived), getNum_seq(tmpReceived), getCommande(tmpReceived), getLengthInfo(tmpReceived));

//                 lastCorrectSeqNum = getNum_seq(tmpReceived);
//                 ++nbFrameWindowReceived;
//                 ++nbFramesReceived;
//                 if (nbFramesReceived >= nbPaq) {
//                     size_t newSize = nbPaq * 2;
//                     frame_t *tmp = realloc(receivedFrames, newSize * sizeof(struct frame_s));
//                     if (!tmp) {
//                         perror("realloc");
//                         exit(EXIT_FAILURE);
//                     }
//                     receivedFrames = tmp;
//                     nbPaq = newSize;
//                 }
//             }
//             setNum_seq(&ack, lastCorrectSeqNum);
//             envoie_reseau(&ack, getPhysicalDestRcpt());
//             ++nbACKSent;
//             nbFrameWindowReceived = 0;
//         }
//     }
//     setNum_seq(&ack, N);
//     envoie_reseau(&ack, getPhysicalDestRcpt());
    
//     printf("Fin transmission coté recepteur.\n");
//     printf("%d ACK envoyés.\n", nbACKSent);
//     printf("%d trames reçues.\n", nbFrameRecues);
//     // TODO travail sur les received frames

//     free(receivedFrames);
// }



//---
// Call to Python's function
// No longer in use
//---
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

    size_t frame_len;
    uint8_t *frame_bytes = bits_to_uint8_array(buffer, &frame_len);

    // Optionally verify CRC locally
    if (verify_CRC(frame, calculate_CRC(frame_bytes, frame_len))) {
        fprintf(stderr, "Erreur: CRC incorrect pour cette trame\n");
    }

    if (lenFrame) *lenFrame = frame_len;
    return frame_bytes;
}

/*
    Python's call to a parsing function
*/
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


/*
    Calcul le CRC 16 sur les données de la trame
*/
uint16_t calculate_CRC_python(frame_t *frame) {
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

