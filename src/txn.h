#ifndef TXN_H
#define TXN_H

#include "resp.h"

#define TXN_MAX_CMDS 1024

/* Per-connection MULTI/EXEC state. Queued requests are owned by the Txn. */
typedef struct {
    RespValue *cmds[TXN_MAX_CMDS];
    int        count;
    int        active;  /* 1 if inside MULTI block */
    int        error;   /* 1 if a command was rejected while queuing */
} Txn;

void txn_init(Txn *txn);
void txn_reset(Txn *txn);
/* Takes ownership of cmd on success. Returns -1 if the queue is full. */
int  txn_queue(Txn *txn, RespValue *cmd);

#endif
