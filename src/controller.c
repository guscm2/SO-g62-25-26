#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>
#include "common.h"

static int max_par = 1;
static int sched_policy = 0;
static char log_path[64];

typedef struct {
    int user_id;
    int cmd_id;
    int runner_pid;
    char comando[MAX_CMD_LEN];
    struct timeval inicio;
} ComandoAtivo;

typedef struct {
    ComandoAtivo em_exec[MAX_QUEUE];
    ComandoAtivo em_espera[MAX_QUEUE];
    int num_exec;
    int num_espera;
    int a_terminar;
    int shutdown_pid;
    int last_user;
} EstadoPartilhado;

static EstadoPartilhado st;


static void responder(int runner_pid, int ok, const char *dados)
{
    char path[64];
    snprintf(path, sizeof(path), FIFO_RUNNER_FMT, runner_pid);

    int fd = open(path, O_WRONLY);
    if (fd == -1) {
        perror("[controller] open response");
        return;
    }

    MsgResponse resp;
    resp.ok = ok;
    strncpy(resp.dados, dados ? dados : "", sizeof(resp.dados) - 1);
    resp.dados[sizeof(resp.dados) - 1] = '\0';

    /* Bug 1: use write_all instead of bare write. */
    write_all(fd, &resp, sizeof(resp));
    close(fd);
}

static void tentar_escalonar(void)
{
    while (st.num_exec < max_par && st.num_espera > 0) {
        int idx = 0;

        if (sched_policy == 1) {
            for (int i = 0; i < st.num_espera; i++) {
                if (st.em_espera[i].user_id != st.last_user) {
                    idx = i;
                    break;
                }
            }
        }

        ComandoAtivo entrada = st.em_espera[idx];
        memmove(&st.em_espera[idx], &st.em_espera[idx + 1],
                sizeof(ComandoAtivo) * (st.num_espera - idx - 1));
        st.num_espera--;
        st.em_exec[st.num_exec++] = entrada;
        st.last_user = entrada.user_id;
        write(STDOUT_FILENO, "[controller] command authorized.\n", 33);
        responder(entrada.runner_pid, 1, "");
    }
}

static void registrar_log(const ComandoAtivo *cmd)
{
    struct timeval fim;
    gettimeofday(&fim, NULL);

    long ms = (fim.tv_sec  - cmd->inicio.tv_sec)  * 1000L
            + (fim.tv_usec - cmd->inicio.tv_usec) / 1000L;

    char linha[MAX_CMD_LEN + 128];
    int len = snprintf(linha, sizeof(linha),
        "user=%d cmd=%d duracao=%ldms comando=\"%s\"\n",
        cmd->user_id, cmd->cmd_id, ms, cmd->comando);
    if (len < 0) len = 0;
    if (len > (int)sizeof(linha)) len = (int)sizeof(linha);

    int fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) { perror("[controller] open log"); return; }
    write(fd, linha, len);
    close(fd);
}

static void handle_message(MsgRequest *req)
{
    if (req->tipo == MSG_EXEC) {
        if (st.num_espera == MAX_QUEUE) {
            responder(req->runner_pid, 0, "queue full");
            return;
        }
        st.em_espera[st.num_espera].cmd_id     = req->cmd_id;
        st.em_espera[st.num_espera].user_id    = req->user_id;
        st.em_espera[st.num_espera].runner_pid = req->runner_pid;
        strncpy(st.em_espera[st.num_espera].comando, req->comando, MAX_CMD_LEN - 1);
        st.em_espera[st.num_espera].comando[MAX_CMD_LEN - 1] = '\0';
        gettimeofday(&st.em_espera[st.num_espera].inicio, NULL);
        st.num_espera++;
        write(STDOUT_FILENO, "[controller] command received, scheduling...\n", 45);
        tentar_escalonar();

    } else if (req->tipo == MSG_DONE) {
        for (int i = 0; i < st.num_exec; i++) {
            if (st.em_exec[i].runner_pid == req->runner_pid) {
                ComandoAtivo terminado = st.em_exec[i];
                memmove(&st.em_exec[i], &st.em_exec[i + 1],
                        sizeof(ComandoAtivo) * (st.num_exec - i - 1));
                st.num_exec--;
                registrar_log(&terminado);
                break;
            }
        }
        write(STDOUT_FILENO, "[controller] command finished.\n", 31);
        tentar_escalonar();

    } else if (req->tipo == MSG_QUERY) {
        char query_resp[MAX_RESPONSE_LEN];
        query_resp[0] = '\0';
        int pos = 0;
        int rem = (int)sizeof(query_resp);
        int n;

        n = snprintf(query_resp + pos, rem, "---\nExecuting\n");
        if (n > 0 && n < rem) { pos += n; rem -= n; } else rem = 0;

        for (int i = 0; i < st.num_exec && rem > 1; i++) {
            n = snprintf(query_resp + pos, rem, "user-id %d - command-id %d\n",
                st.em_exec[i].user_id, st.em_exec[i].cmd_id);
            if (n > 0 && n < rem) { pos += n; rem -= n; } else { rem = 0; break; }
        }

        if (rem > 1) {
            n = snprintf(query_resp + pos, rem, "---\nScheduled\n");
            if (n > 0 && n < rem) { pos += n; rem -= n; } else rem = 0;
        }

        if (sched_policy == 1) {
            for (int i = 0; i < st.num_espera && rem > 1; i++) {
                if (st.em_espera[i].user_id == st.last_user) continue;
                n = snprintf(query_resp + pos, rem, "user-id %d - command-id %d\n",
                    st.em_espera[i].user_id, st.em_espera[i].cmd_id);
                if (n > 0 && n < rem) { pos += n; rem -= n; } else { rem = 0; break; }
            }
            for (int i = 0; i < st.num_espera && rem > 1; i++) {
                if (st.em_espera[i].user_id != st.last_user) continue;
                n = snprintf(query_resp + pos, rem, "user-id %d - command-id %d\n",
                    st.em_espera[i].user_id, st.em_espera[i].cmd_id);
                if (n > 0 && n < rem) { pos += n; rem -= n; } else { rem = 0; break; }
            }
        } else {
            for (int i = 0; i < st.num_espera && rem > 1; i++) {
                n = snprintf(query_resp + pos, rem, "user-id %d - command-id %d\n",
                    st.em_espera[i].user_id, st.em_espera[i].cmd_id);
                if (n > 0 && n < rem) { pos += n; rem -= n; } else { rem = 0; break; }
            }
        }

        responder(req->runner_pid, 1, query_resp);

    } else if (req->tipo == MSG_SHUTDOWN) {
        if (st.a_terminar) {
            responder(req->runner_pid, 1, "");
            return;
        }
        st.a_terminar   = 1;
        st.shutdown_pid = req->runner_pid;
        write(STDOUT_FILENO, "[controller] shutdown pending, waiting for running commands...\n", 63);
    }
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    if (argc < 3) {
        write(STDERR_FILENO, "Usage: ./controller <parallel> <policy>\n", 40);
        return 1;
    }

    max_par = atoi(argv[1]);
    if (max_par <= 0) {
        write(STDERR_FILENO, "parallel must be >= 1\n", 22);
        return 1;
    }

    sched_policy = atoi(argv[2]);
    if (sched_policy != 0 && sched_policy != 1) {
        write(STDERR_FILENO, "policy must be 0 (FCFS) or 1 (Round-Robin)\n", 43);
        return 1;
    }

    {
        const char *label = (sched_policy == 0) ? "fcfs" : "rr";
        int n = 1;
        do {
            snprintf(log_path, sizeof(log_path), "tmp/log_%s_%d.txt", label, n++);
        } while (access(log_path, F_OK) == 0);
    }

    memset(&st, 0, sizeof(st));
    st.shutdown_pid = -1;
    st.last_user    = -1;

    if (mkdir("tmp", 0755) == -1 && errno != EEXIST) {
        perror("[controller] mkdir tmp"); return 1;
    }

    if (mkfifo(FIFO_CONTROLLER, 0666) == -1 && errno != EEXIST) {
        perror("[controller] mkfifo"); return 1;
    }

    write(STDOUT_FILENO, "[controller] ready.\n", 20);

    int fd = open(FIFO_CONTROLLER, O_RDONLY);
    if (fd == -1) { perror("[controller] open fifo"); return 1; }

    int fd_dummy = open(FIFO_CONTROLLER, O_WRONLY);
    if (fd_dummy == -1) { perror("[controller] open dummy"); return 1; }

    for (;;) {
        MsgRequest req;
        ssize_t n = read_all(fd, &req, sizeof(req));
        if (n == -1) { perror("[controller] read"); break; }
        if (n == 0) break;
        if (n != (ssize_t)sizeof(req)) continue;

        if (req.tipo == MSG_QUERY) {
            if (fork() == 0) {
                handle_message(&req);
                _exit(0);
            }
        } else {
            handle_message(&req);
        }

        if (st.a_terminar && st.num_exec == 0 && st.num_espera == 0) break;
    }

    if (st.shutdown_pid != -1)
        responder(st.shutdown_pid, 1, "");

    close(fd);
    close(fd_dummy);
    unlink(FIFO_CONTROLLER);
    write(STDOUT_FILENO, "[controller] terminated.\n", 25);
    return 0;
}
