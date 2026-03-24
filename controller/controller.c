#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "common.h"

int main() {
    char buffer[MSG_SIZE];
    int fd;

    if (mkfifo(FIFO_PATH, 0666) == -1) {
        if (errno == EEXIST) {
            printf("[controller] FIFO já existe, a reutilizar.\n");
        } else {
            perror("[controller] Erro ao criar FIFO");
            exit(EXIT_FAILURE);
        }
    } else {
        printf("[controller] FIFO criado em %s\n", FIFO_PATH);
    }

    printf("[controller] À escuta... (aguarda runner)\n");

    fd = open(FIFO_PATH, O_RDONLY);
    if (fd == -1) {
        perror("[controller] Erro ao abrir FIFO");
        exit(EXIT_FAILURE);
    }

    memset(buffer, 0, MSG_SIZE);
    ssize_t bytes = read(fd, buffer, MSG_SIZE - 1);
    if (bytes == -1) {
        perror("[controller] Erro ao ler do FIFO");
        close(fd);
        exit(EXIT_FAILURE);
    }
    if (bytes == 0) {
        printf("[controller] Runner fechou a ligação sem enviar dados.\n");
        close(fd);
        exit(EXIT_FAILURE);
    }

    printf("[controller] Recebi (%zd bytes): \"%s\"\n", bytes, buffer);

    close(fd);

    printf("[controller] A terminar.\n");
    return 0;
}