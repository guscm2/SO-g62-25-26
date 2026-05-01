#ifndef COMMON_H
#define COMMON_H

#define FIFO_CONTROLLER "/tmp/controller_main"
#define FIFO_RUNNER_FMT "/tmp/runner_%d"

#define MAX_CMD_LEN 256
#define MAX_QUEUE 256
/* worst-case query: 2 section headers + MAX_QUEUE entries × ~40 chars each */
#define MAX_RESPONSE_LEN (MAX_QUEUE * 64)

typedef enum {
    MSG_EXEC,
    MSG_DONE,
    MSG_QUERY,
    MSG_SHUTDOWN
} MsgType;

typedef struct {
    MsgType tipo;
    int     user_id;
    int     cmd_id;
    int     runner_pid;
    char    comando[MAX_CMD_LEN];
} MsgRequest;

typedef struct {
    int  ok;
    char dados[MAX_RESPONSE_LEN];
} MsgResponse;

#endif
