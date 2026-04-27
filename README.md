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
- **[4]** Shutdown gracioso: flag `a_terminar` aguarda `num_exec == 0` antes de responder ao `-s` e sair
- **[5]** Concorrência: `fork` por mensagem, estado em `mmap(MAP_SHARED|MAP_ANONYMOUS)`, exclusão mútua com `sem_init(pshared=1)`

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

### ~~4. Shutdown gracioso~~ ✅ DONE
**Ficheiro:** `src/controller.c`  caso `MSG_SHUTDOWN`

Flag `a_terminar` + `shutdown_pid` guardados ao receber `-s`. O loop continua a processar `MSG_DONE` até `num_exec == 0`, só então responde ao runner e termina.

---

### ~~5. Concorrência no controller (fork por mensagem)~~ ✅ DONE
**Ficheiro:** `src/controller.c`

`fork` por mensagem recebida; filho trata a mensagem e termina. Estado partilhado (`em_exec[]`, `em_espera[]`, flags) em `mmap(MAP_SHARED|MAP_ANONYMOUS)`; acesso exclusivo via `sem_init(sem, 1, 1)`. Parent usa `select` com timeout de 100ms para rever a condição de saída sem bloquear no `read`. MSG_QUERY responde fora do lock para não bloquear o semáforo enquanto abre o FIFO do runner.

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
| 4 | Shutdown gracioso | Média | ✅ DONE (BENJI) |
| 5 | Concorrência com fork + memória partilhada | Alta | ✅ DONE (BENJI) |
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
