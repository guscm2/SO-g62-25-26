#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <semaphore.h>
#include <errno.h>
#include <sys/time.h>
#include "common.h"

static int max_par = 1;

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
} EstadoPartilhado;

static void responder(int runner_pid, int ok, const char *dados)
{
    char path[64];
    snprintf(path, sizeof(path), FIFO_RUNNER_FMT, runner_pid);

    int fd = open(path, O_WRONLY);
    if (fd == -1) { perror("[controller] open response"); return; }

    MsgResponse resp;
    resp.ok = ok;
    strncpy(resp.dados, dados ? dados : "", sizeof(resp.dados) - 1);

    write(fd, &resp, sizeof(resp));
    close(fd);
}

static void tentar_escalonar(EstadoPartilhado *st)
{
    while (st->num_exec < max_par && st->num_espera > 0) {
        ComandoAtivo entrada = st->em_espera[0];
        memmove(&st->em_espera[0], &st->em_espera[1],
                sizeof(ComandoAtivo) * (st->num_espera - 1));
        st->num_espera--;
        st->em_exec[st->num_exec++] = entrada;
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

    char linha[MAX_CMD_LEN + 64];
    int len = snprintf(linha, sizeof(linha),
        "user=%d cmd=%d duracao=%ldms comando=\"%s\"\n",
        cmd->user_id, cmd->cmd_id, ms, cmd->comando);

    mkdir("tmp", 0755);

    int fd = open("tmp/log.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) { perror("[controller] open log"); return; }
    write(fd, linha, len);
    close(fd);
}

static void handle_message(MsgRequest *req, EstadoPartilhado *st, sem_t *sem)
{
    char query_resp[MAX_CMD_LEN * 4];
    int  query_pid = -1;

    sem_wait(sem);

    if (req->tipo == MSG_EXEC) {
        st->em_espera[st->num_espera].cmd_id     = req->cmd_id;
        st->em_espera[st->num_espera].user_id    = req->user_id;
        st->em_espera[st->num_espera].runner_pid = req->runner_pid;
        strncpy(st->em_espera[st->num_espera].comando, req->comando, MAX_CMD_LEN - 1);
        gettimeofday(&st->em_espera[st->num_espera].inicio, NULL);
        st->num_espera++;
        write(STDOUT_FILENO, "[controller] command received, scheduling...\n", 45);
        tentar_escalonar(st);

    } else if (req->tipo == MSG_DONE) {
        for (int i = 0; i < st->num_exec; i++) {
            if (st->em_exec[i].runner_pid == req->runner_pid) {
                ComandoAtivo terminado = st->em_exec[i];
                memmove(&st->em_exec[i], &st->em_exec[i + 1],
                        sizeof(ComandoAtivo) * (st->num_exec - i - 1));
                st->num_exec--;
                registrar_log(&terminado);
                break;
            }
        }
        write(STDOUT_FILENO, "[controller] command finished.\n", 31);
        tentar_escalonar(st);

    } else if (req->tipo == MSG_QUERY) {
        int pos = 0;
        pos += snprintf(query_resp + pos, sizeof(query_resp) - pos, "---\nExecuting\n");
        for (int i = 0; i < st->num_exec; i++)
            pos += snprintf(query_resp + pos, sizeof(query_resp) - pos,
                "user-id %d - command-id %d\n",
                st->em_exec[i].user_id, st->em_exec[i].cmd_id);
        pos += snprintf(query_resp + pos, sizeof(query_resp) - pos, "---\nScheduled\n");
        for (int i = 0; i < st->num_espera; i++)
            pos += snprintf(query_resp + pos, sizeof(query_resp) - pos,
                "user-id %d - command-id %d\n",
                st->em_espera[i].user_id, st->em_espera[i].cmd_id);
        query_pid = req->runner_pid;

    } else if (req->tipo == MSG_SHUTDOWN) {
        st->a_terminar   = 1;
        st->shutdown_pid = req->runner_pid;
        write(STDOUT_FILENO, "[controller] shutdown pending, waiting for running commands...\n", 63);
    }

    sem_post(sem);

    /* respond to -c outside the lock so we don't block while opening the runner FIFO */
    if (query_pid != -1)
        responder(query_pid, 1, query_resp);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        write(STDERR_FILENO, "Usage: ./controller <parallel> <policy>\n", 40);
        return 1;
    }

    max_par = atoi(argv[1]);

    EstadoPartilhado *st = mmap(NULL, sizeof(EstadoPartilhado),
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (st == MAP_FAILED) { perror("[controller] mmap state"); return 1; }
    memset(st, 0, sizeof(*st));
    st->shutdown_pid = -1;

    sem_t *sem = mmap(NULL, sizeof(sem_t),
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sem == MAP_FAILED) { perror("[controller] mmap sem"); return 1; }
    sem_init(sem, 1, 1);

    if (mkfifo(FIFO_CONTROLLER, 0666) == -1 && errno != EEXIST) {
        perror("[controller] mkfifo"); return 1;
    }

    write(STDOUT_FILENO, "[controller] ready.\n", 20);

    int fd = open(FIFO_CONTROLLER, O_RDONLY);
    if (fd == -1) { perror("[controller] open fifo"); return 1; }

    int fd_dummy = open(FIFO_CONTROLLER, O_WRONLY);
    if (fd_dummy == -1) { perror("[controller] open dummy"); return 1; }

    while (1) {
        if (st->a_terminar && st->num_exec == 0) break;

        /* use select with a short timeout so we can recheck the exit condition
           even when no messages are arriving (e.g. last MSG_DONE was just processed) */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = {0, 100000}; /* 100 ms */

        if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0) continue;

        MsgRequest req;
        ssize_t n = read(fd, &req, sizeof(req));
        if (n != (ssize_t)sizeof(req)) continue;

        pid_t pid = fork();
        if (pid == 0) {
            close(fd);
            close(fd_dummy);
            handle_message(&req, st, sem);
            exit(0);
        }
        /* reap any children that already finished without blocking */
        waitpid(-1, NULL, WNOHANG);
    }

    /* wait for all handler children to finish before responding to -s */
    while (waitpid(-1, NULL, 0) > 0);

    if (st->shutdown_pid != -1)
        responder(st->shutdown_pid, 1, "");

    sem_destroy(sem);
    munmap(sem, sizeof(sem_t));
    munmap(st, sizeof(EstadoPartilhado));

    close(fd);
    close(fd_dummy);
    unlink(FIFO_CONTROLLER);
    write(STDOUT_FILENO, "[controller] terminated.\n", 25);
    return 0;
}
