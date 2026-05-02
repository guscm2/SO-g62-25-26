#ifndef COMMON_H
#define COMMON_H

#include <unistd.h>
#include <errno.h>

#define FIFO_CONTROLLER "/tmp/controller_main"
#define FIFO_RUNNER_FMT "/tmp/runner_%d"

#define MAX_CMD_LEN 256
#define MAX_QUEUE 256
/* worst-case query: 2 section headers + MAX_QUEUE entries × ~40 chars each */
#define MAX_RESPONSE_LEN (MAX_QUEUE * 64)
#define MAX_PIPES 32
#define MAX_ARGS  64

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

static inline ssize_t read_all(int fd, void *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, (char *)buf + got, n - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) break;
        got += r;
    }
    return (ssize_t)got;
}

static inline ssize_t write_all(int fd, const void *buf, size_t n)
{
    size_t sent = 0;
    while (sent < n) {
        ssize_t r = write(fd, (const char *)buf + sent, n - sent);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += r;
    }
    return (ssize_t)sent;
}

#endif
