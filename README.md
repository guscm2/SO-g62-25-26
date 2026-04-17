# Orquestração de um ambiente multi-runner
## Estado atual e o que falta fazer

---

## O que já está feito

- Estrutura de pastas conforme o enunciado (`src/`, `include/`, `obj/`, `bin/`, `tmp/`)
- Makefile com os targets obrigatórios (`all`, `controller`, `runner`, `clean`)
- Protocolo de mensagens definido em `common.h` (`MsgRequest`, `MsgResponse`)
- FIFO principal (`/tmp/controller_main`) para runner → controller
- FIFO privado por runner (`/tmp/runner_<PID>`) para controller → runner
- Runner: fluxo `-e` completo (submete → aguarda autorização → executa → notifica fim)
- Runner: fluxo `-c` e `-s` no básico (envia pedido e aguarda resposta)
- Controller: loop sequencial a receber mensagens
- Controller: autoriza sempre de imediato (sem fila ainda)
- Execução de comandos simples com `fork` + `execvp`

---

## O que falta fazer

### 1. Fila de escalonamento no controller
**Ficheiro:** `src/controller.c`

O controller atualmente autoriza sempre de imediato, ignorando `max_par`.
É preciso manter duas listas internas:
- `em_execucao[]` — comandos atualmente a correr (tamanho máximo = `max_par`)
- `em_espera[]` — comandos à espera de autorização (fila FCFS)

Lógica:
- Quando chega `MSG_EXEC`: se `n_execucao < max_par`, autoriza e adiciona a `em_execucao`; caso contrário, adiciona a `em_espera` sem responder ainda
- Quando chega `MSG_DONE`: remove de `em_execucao` e promove o primeiro de `em_espera` (se houver), respondendo-lhe agora com autorização

---

### 2. Resposta real ao `-c` (query)
**Ficheiro:** `src/controller.c`  função `MSG_QUERY`

Atualmente devolve sempre uma lista vazia.
Quando chega `MSG_QUERY`, o controller deve construir uma string com:
```
---
Executing
user-id 1 - command-id 98301
---
Scheduled
user-id 4 - command-id 97650
```
com base no conteúdo real de `em_execucao[]` e `em_espera[]`.

---

### 3. Registo em ficheiro com duração (`gettimeofday`)
**Ficheiro:** `src/controller.c`

Quando chega `MSG_DONE`, guardar em `tmp/log.txt` uma linha com:
```
user=1 cmd=98301 duracao=4321ms comando="sleep 4"
```
Para isso:
- Guardar `gettimeofday` no momento em que `MSG_EXEC` chega
- No `MSG_DONE`, calcular a diferença e escrever no ficheiro com `open` + `write`

---

### 4. Shutdown gracioso
**Ficheiro:** `src/controller.c`  caso `MSG_SHUTDOWN`

Atualmente o controller termina logo ao receber `-s`, mesmo que haja runners a executar.
O correto é:
- Marcar uma flag `a_terminar = 1`
- Continuar o loop até `em_execucao` estar vazio
- Só então responder ao runner do `-s` e sair

---

### 5. Concorrência no controller (fork por mensagem)
**Ficheiro:** `src/controller.c`

Atualmente o controller é totalmente sequencial — enquanto trata uma mensagem, bloqueia todas as outras.
Para o `-c` não bloquear os `-e` em curso, o controller deve fazer `fork` para cada mensagem recebida, tratando-a num processo filho.

Atenção: as listas `em_execucao[]` e `em_espera[]` passam a ser acedidas por múltiplos processos, por isso será necessário:
- Memória partilhada (`mmap` com `MAP_SHARED` ou `shmget`) para as listas
- Semáforo ou mutex para acesso exclusivo (`sem_open` ou `semget`)

---

### 6. Suporte a pipes e redireccionamentos no runner
**Ficheiro:** `src/runner.c`  função `executar()`

Atualmente só executa comandos simples (ex: `echo hello`).
É preciso parsear a string do comando e detetar:
- `|` — criar `pipe()`, fazer `fork` para cada segmento, ligar stdout→stdin com `dup2`
- `>` — abrir ficheiro com `open(O_WRONLY|O_CREAT|O_TRUNC)` e redirecionar stdout com `dup2`
- `2>` — redirecionar stderr com `dup2`
- `<` — abrir ficheiro com `open(O_RDONLY)` e redirecionar stdin com `dup2`

Nota: não usar `system()` nem `bash` — proibido pelo enunciado.

---

### 7. Testes e política de escalonamento alternativa
**Ficheiros novos:** `scripts/teste_paralelo.sh`, etc.

O enunciado pede:
- Scripts bash que lancem múltiplos runners em paralelo com comandos de diferentes durações
- Comparação entre pelo menos duas políticas de escalonamento (ex: FCFS vs Round-Robin por utilizador)
- Resultados em tabela ou gráfico no relatório

---

## Ordem sugerida de implementação

| # | Tarefa | Dificuldade |
|---|--------|-------------|
| 1 | Fila de escalonamento | Média |
| 2 | Query com dados reais | Baixa |
| 3 | Log com gettimeofday | Baixa |
| 4 | Shutdown gracioso | Média |
| 6 | Pipes e redireccionamentos | Alta |
| 5 | Concorrência com fork + memória partilhada | Alta |
| 7 | Testes e política alternativa | Média |

---

## Como compilar e testar

```bash
make

# Terminal 1 — lança o controller (1 comando de cada vez, política 0)
./bin/controller 1 0

# Terminal 2 — executa um comando
./bin/runner -e 1 echo hello

# Terminal 2 — consulta o estado
./bin/runner -c

# Terminal 2 — termina o controller
./bin/runner -s
```
