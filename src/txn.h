#ifndef TXN_H
#define TXN_H

#include "resp.h"

#define TXN_MAX_CMDS 128

typedef struct {
    RespValue *cmds[TXN_MAX_CMDS];
    int        count;
    int        active;  /* 1 if inside MULTI block */
    int        error;   /* 1 if a command error occurred inside MULTI */
} Txn;

void txn_init(Txn *txn);
void txn_reset(Txn *txn);
int  txn_queue(Txn *txn, RespValue *cmd);

#endif