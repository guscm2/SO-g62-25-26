#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#include "common.h"

static char runner_fifo_path[64];

static void cleanup_fifo(void)
{
    if (runner_fifo_path[0])
        unlink(runner_fifo_path);
}

/* Envia mensagem ao controller */
static void enviar(MsgRequest *req)
{
    int fd = open(FIFO_CONTROLLER, O_WRONLY);
    if (fd == -1) { perror("[runner] open controller"); exit(1); }
    write_all(fd, req, sizeof(*req));
    close(fd);
}

/* Abre o FIFO privado para leitura ANTES de enviar o pedido.
 * O_RDWR é não-bloqueante e mantém o write-end aberto, garantindo que o
 * kernel não liberta o buffer do pipe mesmo que o controller escreva e
 * feche antes de chegarmos ao read. */
static int abrir_resposta(void)
{
    char path[64];
    snprintf(path, sizeof(path), FIFO_RUNNER_FMT, (int)getpid());
    int fd = open(path, O_RDWR);
    if (fd == -1) { perror("[runner] open response"); exit(1); }
    return fd;
}

/* Lê a resposta do FIFO já aberto e fecha-o. */
static void aguardar(MsgResponse *resp, int fd)
{
    read_all(fd, resp, sizeof(*resp));
    close(fd);
}

/* Executa o comando com fork + execvp. Retorna 0 em sucesso, -1 em erro. */
static int executar(const char *cmd)
{
    char buf[MAX_CMD_LEN];
    strncpy(buf, cmd, MAX_CMD_LEN - 1);
    buf[MAX_CMD_LEN - 1] = '\0';

    /* 1. partir por '|' */
    char *segmentos[MAX_PIPES];
    int num_segmentos = 0;
    segmentos[num_segmentos++] = buf;
    for (char *p = buf; *p; p++) {
        if (*p == '|') {
            *p = '\0';
            if (num_segmentos >= MAX_PIPES) {
                write(STDERR_FILENO, "[runner] too many pipe stages\n", 30);
                return -1;
            }
            segmentos[num_segmentos++] = p + 1;
        }
    }

    pid_t pids[MAX_PIPES];
    int num_pids = 0;

    int pipe_ant[2] = {-1, -1};

    for (int i = 0; i < num_segmentos; i++) {

        /* 2. copiar segmento e tokenizar */
        char segbuf[MAX_CMD_LEN];
        strncpy(segbuf, segmentos[i], MAX_CMD_LEN - 1);
        segbuf[MAX_CMD_LEN - 1] = '\0';

        char *args[MAX_ARGS];
        int n_args = 0;
        char *redir_in = NULL, *redir_out = NULL, *redir_err = NULL;

        char *tok = strtok(segbuf, " \t");
        while (tok) {
            if (strcmp(tok, "2>") == 0)          redir_err = strtok(NULL, " \t");
            else if (strcmp(tok, ">") == 0)      redir_out = strtok(NULL, " \t");
            else if (strcmp(tok, "<") == 0)      redir_in  = strtok(NULL, " \t");
            else if (tok[0]=='2' && tok[1]=='>')  redir_err = tok + 2;
            else if (tok[0]=='>' && tok[1])       redir_out = tok + 1;
            else if (tok[0]=='<' && tok[1])       redir_in  = tok + 1;
            else {
                if (n_args >= MAX_ARGS - 1) {
                    write(STDERR_FILENO, "[runner] too many arguments\n", 28);
                    goto exec_error;
                }
                args[n_args++] = tok;
            }
            tok = strtok(NULL, " \t");
        }
        args[n_args] = NULL;
        if (n_args == 0) continue;

        /* 3. criar pipe para o próximo segmento */
        int pipe_prox[2] = {-1, -1};
        if (i < num_segmentos - 1 && pipe(pipe_prox) == -1) {
            perror("[runner] pipe");
            goto exec_error;
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("[runner] fork");
            if (pipe_prox[0] != -1) { close(pipe_prox[0]); close(pipe_prox[1]); }
            goto exec_error;
        }
        if (pid == 0) {
            /* 4. ligar pipes */
            if (pipe_ant[0] != -1) {
                dup2(pipe_ant[0], 0);
                close(pipe_ant[0]); close(pipe_ant[1]);
            }
            if (pipe_prox[1] != -1) {
                dup2(pipe_prox[1], 1);
                close(pipe_prox[0]); close(pipe_prox[1]);
            }

            /* 5. redirecionamentos — dentro do filho */
            if (redir_in) {
                int fd = open(redir_in, O_RDONLY);
                if (fd == -1) { perror(redir_in); _exit(1); }
                dup2(fd, 0); close(fd);
            }
            if (redir_out) {
                int fd = open(redir_out, O_WRONLY|O_CREAT|O_TRUNC, 0644);
                if (fd == -1) { perror(redir_out); _exit(1); }
                dup2(fd, 1); close(fd);
            }
            if (redir_err) {
                int fd = open(redir_err, O_WRONLY|O_CREAT|O_TRUNC, 0644);
                if (fd == -1) { perror(redir_err); _exit(1); }
                dup2(fd, 2); close(fd);
            }

            execvp(args[0], args);
            perror("[runner] execvp");
            _exit(1);
        }  /* fim do filho */

        pids[num_pids++] = pid;

        /* pai: fechar pipe anterior e avançar */
        if (pipe_ant[0] != -1) { close(pipe_ant[0]); close(pipe_ant[1]); }
        pipe_ant[0] = pipe_prox[0];
        pipe_ant[1] = pipe_prox[1];

    }  /* fim do for */

    /* fechar último pipe e esperar por todos */
    if (pipe_ant[0] != -1) { close(pipe_ant[0]); close(pipe_ant[1]); }
    for (int i = 0; i < num_pids; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            char warnbuf[64];
            int wlen = snprintf(warnbuf, sizeof(warnbuf),
                "[runner] stage %d exited with status %d\n",
                i, WEXITSTATUS(status));
            if (wlen > 0 && wlen <= (int)sizeof(warnbuf))
                write(STDERR_FILENO, warnbuf, wlen);
        }
    }
    return 0;

exec_error:
    if (pipe_ant[0] != -1) { close(pipe_ant[0]); close(pipe_ant[1]); }
    for (int i = 0; i < num_pids; i++) { kill(pids[i], SIGTERM); waitpid(pids[i], NULL, 0); }
    return -1;
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);

    if (argc < 2) {
        write(STDERR_FILENO, "Usage: ./runner -e <uid> <cmd>  |  -c  |  -s\n", 46);
        return 1;
    }

    /* Cria FIFO privado para receber resposta */
    snprintf(runner_fifo_path, sizeof(runner_fifo_path), FIFO_RUNNER_FMT, (int)getpid());
    if (mkfifo(runner_fifo_path, 0666) == -1 && errno != EEXIST) {
        perror("[runner] mkfifo");
        return 1;
    }
    atexit(cleanup_fifo);

    MsgRequest req;
    memset(&req, 0, sizeof(req));
    req.runner_pid = getpid();

    if (strcmp(argv[1], "-e") == 0) {
        if (argc < 4) { write(STDERR_FILENO, "Missing arguments\n", 18); return 1; }

        req.tipo    = MSG_EXEC;
        req.user_id = atoi(argv[2]);
        req.cmd_id  = getpid();   /* PID como id único por agora */

        /* Bug 7: use snprintf-offset accumulation to avoid strncat underflow
         * when the buffer is already full. */
        int off = 0;
        for (int i = 3; i < argc; i++) {
            if (off >= (int)sizeof(req.comando)) break;
            off += snprintf(req.comando + off, sizeof(req.comando) - off,
                            "%s%s", (i > 3 ? " " : ""), argv[i]);
        }

        char buf[64];
        snprintf(buf, sizeof(buf), "[runner] command %d submitted\n", req.cmd_id);
        write(STDOUT_FILENO, buf, strlen(buf));

        /* abre FIFO de resposta ANTES de enviar — elimina a race condition */
        int resp_fd = abrir_resposta();
        enviar(&req);

        MsgResponse resp;
        aguardar(&resp, resp_fd);   /* bloqueia até o controller autorizar */

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
        int resp_fd = abrir_resposta();
        enviar(&req);

        MsgResponse resp;
        aguardar(&resp, resp_fd);
        write(STDOUT_FILENO, resp.dados, strlen(resp.dados));

    } else if (strcmp(argv[1], "-s") == 0) {
        req.tipo = MSG_SHUTDOWN;
        int resp_fd = abrir_resposta();
        enviar(&req);

        write(STDOUT_FILENO, "[runner] sent shutdown notification\n", 36);
        write(STDOUT_FILENO, "[runner] waiting for controller to shutdown...\n", 47);

        MsgResponse resp;
        aguardar(&resp, resp_fd);

        write(STDOUT_FILENO, "[runner] controller exited.\n", 28);
    }

    return 0;
}