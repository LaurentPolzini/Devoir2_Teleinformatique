#ifndef __PROTOCOLE_H__
#define __PROTOCOLE_H__

#define EMISSION 1
#define RECEPTION 0

typedef struct sSendingFrame *tSendingFrame;

tSendingFrame *prepareFrames(uint8_t **frames, int nbOfFrames);

tSendingFrame createSendingFrame(uint8_t *framE, int seqnuM);
tSendingFrame addFrame(tSendingFrame frameToSend, uint8_t *frame, size_t frameSize);
tSendingFrame changeSeqNum(tSendingFrame frameToSend, int num);
void recalculateLeng(tSendingFrame frameToSend);

int getNumSeq(tSendingFrame frameReady);
uint8_t *getFrame(tSendingFrame frameReady);
size_t getFrameSize(tSendingFrame frameReady);

void go_back_n_recepteur(void);
void go_back_n_emetteur(char *datas_file_name, uint8_t adress, int CRC);

void init(int emission);
void envoie_reseau(tSendingFrame *frame);
void recoit_reseau(tSendingFrame *frame);

#endif
