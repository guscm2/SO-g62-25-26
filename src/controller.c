#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
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

ComandoAtivo em_exec[MAX_QUEUE];
ComandoAtivo em_espera[MAX_QUEUE];
int num_exec = 0;
int num_espera = 0;

/* Responde ao runner via o seu FIFO privado */
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

void tentar_escalonar(){
    while (num_exec < max_par && num_espera > 0){
        ComandoAtivo entrada = em_espera[0];

        memmove(&em_espera[0], &em_espera[1], sizeof(ComandoAtivo) * (num_espera -1));
        num_espera--;
        em_exec[num_exec++] = entrada;

        write(STDOUT_FILENO, "[controller] command authorized.\n", 33);

        responder(entrada.runner_pid, 1, "");
    }
}

/* Registra a linha de log em tmp/log.txt */
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
 
    /* Garante que a pasta tmp existe */
    mkdir("tmp", 0755);
 
    int fd = open("tmp/log.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) { perror("[controller] open log"); return; }
    write(fd, linha, len);
    close(fd);
}


int main(int argc, char *argv[]){
    if (argc < 3) {
        write(STDERR_FILENO, "Usage: ./controller <parallel> <policy>\n", 40);
        return 1;
    }

    max_par = atoi(argv[1]);

    /* Cria FIFO principal */
    if (mkfifo(FIFO_CONTROLLER, 0666) == -1 && errno != EEXIST) {
        perror("[controller] mkfifo"); return 1;
    }
    /* Loop principal — por agora sequencial, uma mensagem de cada vez */
    write(STDOUT_FILENO, "[controller] ready.\n", 20);

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
            em_espera[num_espera].cmd_id = req.cmd_id;
            em_espera[num_espera].user_id = req.user_id;
            em_espera[num_espera].runner_pid = req.runner_pid;
            strncpy(em_espera[num_espera].comando, req.comando, MAX_CMD_LEN-1);
            gettimeofday(&em_espera[num_espera].inicio, NULL);
            num_espera++;
            write(STDOUT_FILENO, "[controller] command received, scheduling...\n", 45);

            tentar_escalonar();

        } else if (req.tipo == MSG_DONE) {
            for(int i = 0; i < num_exec; i++){
                if(em_exec[i].runner_pid == req.runner_pid){
                ComandoAtivo terminado = em_exec[i];    
                memmove(&em_exec[i], &em_exec[i+1], sizeof(ComandoAtivo) * (num_exec - i - 1));
                num_exec--;
                registrar_log(&terminado);
                break;
                }
            }
            write(STDOUT_FILENO, "[controller] command finished.\n", 31);
            tentar_escalonar();

        } else if (req.tipo == MSG_QUERY) {
            char resposta[MAX_CMD_LEN * 4];
            int pos = 0;

            pos += snprintf(resposta + pos, sizeof(resposta) - pos, "---\nExecuting\n");

            for (int i = 0; i < num_exec; i++) {
                pos += snprintf(resposta + pos, sizeof(resposta) - pos,
                    "user-id %d - command-id %d\n",
                    em_exec[i].user_id, em_exec[i].cmd_id);
            }

            pos += snprintf(resposta + pos, sizeof(resposta) - pos, "---\nScheduled\n");
            
            for (int i = 0; i < num_espera; i++) {
                pos += snprintf(resposta + pos, sizeof(resposta) - pos,
                    "user-id %d - command-id %d\n",
                em_espera[i].user_id, em_espera[i].cmd_id);
            }

            responder(req.runner_pid, 1, resposta);

        } else if (req.tipo == MSG_SHUTDOWN) {
            responder(req.runner_pid, 1, "");
            break;
        }
    }
    close(fd);
    close(fd_dummy);
    unlink(FIFO_CONTROLLER);
    write(STDOUT_FILENO, "[controller] terminated.\n", 25);
    return 0;
}