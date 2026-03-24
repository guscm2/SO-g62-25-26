#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include "common.h"

int main() {
    int fd;
    const char *mensagem = "ola controller";

    printf("[runner] PID=%d — a tentar ligar ao controller...\n", getpid());

    fd = open(FIFO_PATH, O_WRONLY);
    if (fd == -1) {
        perror("[runner] Erro ao abrir FIFO (o controller está a correr?)");
        exit(EXIT_FAILURE);
    }

    ssize_t bytes = write(fd, mensagem, strlen(mensagem) + 1);
    if (bytes == -1) {
        perror("[runner] Erro ao escrever no FIFO");
        close(fd);
        exit(EXIT_FAILURE);
    }

    printf("[runner] Mensagem enviada (%zd bytes): \"%s\"\n", bytes, mensagem);

    close(fd);

    printf("[runner] A terminar.\n");
    return 0;
}