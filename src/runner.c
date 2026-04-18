#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

#include "common.h"

/* Envia mensagem ao controller */
static void enviar(MsgRequest *req)
{
    int fd = open(FIFO_CONTROLLER, O_WRONLY);
    if (fd == -1) { perror("[runner] open controller"); exit(1); }
    write(fd, req, sizeof(*req));
    close(fd);
}

/* Aguarda resposta no FIFO privado */
static void aguardar(MsgResponse *resp)
{
    char path[64];
    snprintf(path, sizeof(path), FIFO_RUNNER_FMT, (int)getpid());

    int fd = open(path, O_RDONLY);
    if (fd == -1) { perror("[runner] open response"); exit(1); }
    read(fd, resp, sizeof(*resp));
    close(fd);
}

/* Executa o comando com fork + execvp */
static void executar(const char *cmd)
{
    /* divide o comando em tokens */
    char buf[MAX_CMD_LEN];
    strncpy(buf, cmd, MAX_CMD_LEN - 1);

    char *args[64];
    int n = 0;
    char *tok = strtok(buf, " ");
    while (tok && n < 63) { args[n++] = tok; tok = strtok(NULL, " "); }
    args[n] = NULL;

    pid_t pid = fork();
    if (pid == 0) { execvp(args[0], args); perror("[runner] execvp"); exit(1); }
    waitpid(pid, NULL, 0);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        write(STDERR_FILENO, "Usage: ./runner -e <uid> <cmd>  |  -c  |  -s\n", 46);
        return 1;
    }

    /* Cria FIFO privado para receber resposta */
    char fifo_resp[64];
    snprintf(fifo_resp, sizeof(fifo_resp), FIFO_RUNNER_FMT, (int)getpid());
    mkfifo(fifo_resp, 0666);

    MsgRequest req;
    memset(&req, 0, sizeof(req));
    req.runner_pid = getpid();

    if (strcmp(argv[1], "-e") == 0) {
        if (argc < 4) { write(STDERR_FILENO, "Missing arguments\n", 18); return 1; }

        req.tipo    = MSG_EXEC;
        req.user_id = atoi(argv[2]);
        req.cmd_id  = getpid();   /* PID como id único por agora */

        /* junta o comando numa string */
        for (int i = 3; i < argc; i++) {
            if (i > 3) strncat(req.comando, " ", sizeof(req.comando) - strlen(req.comando) - 1);
            strncat(req.comando, argv[i], sizeof(req.comando) - strlen(req.comando) - 1);
        }

        char buf[64];
        snprintf(buf, sizeof(buf), "[runner] command %d submitted\n", req.cmd_id);
        write(STDOUT_FILENO, buf, strlen(buf));

        /* pede autorização */
        enviar(&req);

        MsgResponse resp;
        aguardar(&resp);   /* bloqueia até o controller autorizar */

        snprintf(buf, sizeof(buf), "[runner] executing command %d...\n", req.cmd_id);
        write(STDOUT_FILENO, buf, strlen(buf));

        executar(req.comando);

        /* avisa que terminou */
        req.tipo = MSG_DONE;
        enviar(&req);

        snprintf(buf, sizeof(buf), "[runner] command %d finished\n", req.cmd_id);
        write(STDOUT_FILENO, buf, strlen(buf));

    } else if (strcmp(argv[1], "-c") == 0) {
        req.tipo = MSG_QUERY;
        enviar(&req);

        MsgResponse resp;
        aguardar(&resp);
        write(STDOUT_FILENO, resp.dados, strlen(resp.dados));

    } else if (strcmp(argv[1], "-s") == 0) {
        req.tipo = MSG_SHUTDOWN;
        enviar(&req);

        write(STDOUT_FILENO, "[runner] sent shutdown notification\n", 36);
        write(STDOUT_FILENO, "[runner] waiting for controller to shutdown...\n", 47);

        MsgResponse resp;
        aguardar(&resp);

        write(STDOUT_FILENO, "[runner] controller exited.\n", 28);
    }

    unlink(fifo_resp);
    return 0;
}