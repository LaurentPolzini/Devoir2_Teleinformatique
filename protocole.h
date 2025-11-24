#ifndef __PROTOCOLE_H__
#define __PROTOCOLE_H__

typedef struct sSendingFrame *tSendingFrame;

tSendingFrame addFrame(tSendingFrame frameToSend, uint8_t *frame);
tSendingFrame changeSeqNum(tSendingFrame frameToSend, int num);

int getNumSeq(tSendingFrame frameReady);
uint8_t *getFrame(tSendingFrame frameReady);

#endif
