#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include "common.h"

/* Responde ao runner via o seu FIFO privado */
static void responder(int runner_pid, int ok, const char *dados)
{
    char path[64];
    snprintf(path, sizeof(path), FIFO_RUNNER_FMT, runner_pid);

    int fd = open(path, O_WRONLY);
    if (fd == -1) { perror("[controller] open resposta"); return; }

    MsgResponse resp;
    resp.ok = ok;
    strncpy(resp.dados, dados ? dados : "", sizeof(resp.dados) - 1);

    write(fd, &resp, sizeof(resp));
    close(fd);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        write(STDERR_FILENO, "Uso: ./controller <parallel> <policy>\n", 38);
        return 1;
    }

    int max_par = atoi(argv[1]);   /* usado mais tarde */
    (void)max_par;                 /* evita warning por enquanto */

    /* Cria FIFO principal */
    if (mkfifo(FIFO_CONTROLLER, 0666) == -1 && errno != EEXIST) {
        perror("[controller] mkfifo"); return 1;
    }

    write(STDOUT_FILENO, "[controller] pronto.\n", 21);

    /* Loop principal — por agora sequencial, uma mensagem de cada vez */
    while (1) {
        int fd = open(FIFO_CONTROLLER, O_RDONLY);
        if (fd == -1) { perror("[controller] open fifo"); break; }

        MsgRequest req;
        ssize_t n = read(fd, &req, sizeof(req));
        close(fd);

        if (n != (ssize_t)sizeof(req)) continue;

        if (req.tipo == MSG_EXEC) {
            /* Por agora: autoriza sempre de imediato */
            write(STDOUT_FILENO, "[controller] autorizar exec\n", 28);
            responder(req.runner_pid, 1, "");

        } else if (req.tipo == MSG_DONE) {
            write(STDOUT_FILENO, "[controller] comando terminado\n", 31);

        } else if (req.tipo == MSG_QUERY) {
            /* Por agora devolve lista vazia */
            responder(req.runner_pid, 1, "---\nExecuting\n---\nScheduled\n");

        } else if (req.tipo == MSG_SHUTDOWN) {
            responder(req.runner_pid, 1, "");
            break;
        }
    }

    unlink(FIFO_CONTROLLER);
    write(STDOUT_FILENO, "[controller] terminado.\n", 24);
    return 0;
}
