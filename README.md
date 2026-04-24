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
- Execução de comandos simples com `fork` + `execvp`
- **[1]** Fila de escalonamento FCFS (`em_exec[]` + `em_espera[]`), `tentar_escalonar()` chamado em MSG_EXEC e MSG_DONE
- **[2]** Query (`-c`) responde com listas reais de `em_exec[]` e `em_espera[]`
- **[3]** Log em `tmp/log.txt` com duração em ms via `gettimeofday` (`registrar_log()`)

---

## O que falta fazer

### ~~1. Fila de escalonamento no controller~~ ✅ DONE
`em_exec[]`/`em_espera[]` com FCFS, `tentar_escalonar()` chamado em MSG_EXEC e MSG_DONE.

---

### ~~2. Resposta real ao `-c` (query)~~ ✅ DONE
MSG_QUERY lista `em_exec[]` (Executing) e `em_espera[]` (Scheduled).

---

### ~~3. Registo em ficheiro com duração (`gettimeofday`)~~ ✅ DONE
`registrar_log()` escreve `user=X cmd=Y duracao=Zms comando="..."` em `tmp/log.txt`.

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

| # | Tarefa | Dificuldade | Responsável |
|---|--------|-------------|-------------|
| 1 | Fila de escalonamento | Média | ✅ DONE (BENJI) |
| 2 | Query com dados reais | Baixa | ✅ DONE (JORDAN) |
| 3 | Log com gettimeofday | Baixa | ✅ DONE (OSMOTICO) |
| 4 | Shutdown gracioso | Média | |
| 5 | Concorrência com fork + memória partilhada | Alta | BENJI |
| 6 | Pipes e redireccionamentos | Alta | JORDAN |
| 7 | Testes e política alternativa | Média | OSMOTICO |

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
