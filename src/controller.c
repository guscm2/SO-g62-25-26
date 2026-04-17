#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include "common.h"

typedef struct {
    int user_id;
    int cmd_id;
    int runner_pid;
    char comando[MAX_CMD_LEN];
    struct timeval inicio;
} ComandoAtivo;

ComandoAtivo ativos[16];
int num_ativos = 0;

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


int main(int argc, char *argv[]){
    if (argc < 3) {
        write(STDERR_FILENO, "Uso: ./controller <parallel> <policy>\n", 38);
        return 1;
    }

    int max_par = atoi(argv[1]);   /* usado mais tarde */
    (void)max_par;

    /* Cria FIFO principal */
    if (mkfifo(FIFO_CONTROLLER, 0666) == -1 && errno != EEXIST) {
        perror("[controller] mkfifo"); return 1;
    }
    /* Loop principal — por agora sequencial, uma mensagem de cada vez */
    write(STDOUT_FILENO, "[controller] pronto.\n", 21);

    int fd = open(FIFO_CONTROLLER, O_RDONLY);
    if (fd == -1) { perror("[controller] open fifo"); return 1; }

    int fd_dummy = open(FIFO_CONTROLLER, O_WRONLY);
    if (fd_dummy == -1) { perror("[controller] open dummy"); return 1; }

    /* Loop principal — por agora sequencial, uma mensagem de cada vez */
    while (1) {
        MsgRequest req;
        ssize_t n = read(fd, &req, sizeof(req));

        if (n != (ssize_t)sizeof(req)) continue;

        if (req.tipo == MSG_EXEC) {
            struct timeval tv;
            gettimeofday(&tv, NULL);

            int log_fd = open("/tmp/log.txt", O_WRONLY | O_CREAT | O_APPEND, 0666);
            if (log_fd != -1) {
                char buffer[256];
                int len = snprintf(buffer, sizeof(buffer),
                    "User=%d cmd=%d comando=\"%s\"\n",
                    req.user_id, req.cmd_id, req.comando);
                write(log_fd, buffer, len);
                close(log_fd);
            }

            write(STDOUT_FILENO, "[controller] autorizar exec\n", 28);
            responder(req.runner_pid, 1, "");

        } else if (req.tipo == MSG_DONE) {
            write(STDOUT_FILENO, "[controller] comando terminado\n", 31);

        } else if (req.tipo == MSG_QUERY) {
            char resposta[MAX_CMD_LEN * 4];
            int pos = 0;

            pos += snprintf(resposta + pos, sizeof(resposta) - pos, "---\nExecuting\n");

            for (int i = 0; i < num_ativos; i++) {
                pos += snprintf(resposta + pos, sizeof(resposta) - pos,
                    "user-id %d - command-id %d\n",
                    ativos[i].user_id, ativos[i].cmd_id);
            }

            pos += snprintf(resposta + pos, sizeof(resposta) - pos, "---\nScheduled\n");
            // cenas do benji por aqui

            responder(req.runner_pid, 1, resposta);

        } else if (req.tipo == MSG_SHUTDOWN) {
            responder(req.runner_pid, 1, "");
            break;
        }
    }
    close(fd);
    close(fd_dummy);
    unlink(FIFO_CONTROLLER);
    write(STDOUT_FILENO, "[controller] terminado.\n", 24);
    return 0;
}
