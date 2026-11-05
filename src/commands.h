#ifndef COMMANDS_H
#define COMMANDS_H

#include "resp.h"
#include "server.h"

typedef enum {
    CMD_RESULT_DONE,     /* reply written; caller frees the request */
    CMD_RESULT_BLOCKED,  /* client must wait; caller keeps the request for retry */
    CMD_RESULT_QUEUED    /* request moved into the MULTI queue; caller must not free it */
} CommandResult;

/* Execute one parsed request (an array of bulk strings) for a client. */
CommandResult command_process(Server *srv, Client *c, RespValue *req);

/* Retry a blocked request. timed_out is non-zero once the deadline passed. */
CommandResult command_retry_blocked(Server *srv, Client *c, RespValue *req, int timed_out);

#endif
