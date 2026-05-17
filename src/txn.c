#include <stdlib.h>
#include <string.h>

#include "txn.h"

void txn_init(Txn *txn) {
    memset(txn, 0, sizeof(Txn));
}

void txn_reset(Txn *txn) {
    for (int i = 0; i < txn->count; i++) {
        resp_free(txn->cmds[i]);
        txn->cmds[i] = NULL;
    }
    txn->count  = 0;
    txn->active = 0;
    txn->error  = 0;
}

int txn_queue(Txn *txn, RespValue *cmd) {
    if (txn->count >= TXN_MAX_CMDS) return -1;
    txn->cmds[txn->count++] = cmd;
    return 0;
}