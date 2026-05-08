#!/bin/bash
# Uso: ./scripts/run_test.sh <policy> [max_par]
#   policy:  0 = FCFS  |  1 = Round-Robin por utilizador
#   max_par: máximo de comandos em paralelo (default: 2)
#
# Arranca o controller, submete o workload via runners e encerra.

set -e

POLICY=${1:?"Uso: $0 <policy> [max_par]    (policy: 0=FCFS  1=RR)"}
MAX_PAR=${2:-2}

if [ "$POLICY" != "0" ] && [ "$POLICY" != "1" ]; then
    echo "Erro: policy deve ser 0 (FCFS) ou 1 (Round-Robin)" >&2
    exit 1
fi

LABEL=$( [ "$POLICY" -eq 0 ] && echo "fcfs" || echo "rr" )
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "======================================================"
echo "  Política : ${LABEL^^}   |   Paralelo : ${MAX_PAR}"
echo "======================================================"

# --- limpeza ---
pkill -x controller 2>/dev/null || true
sleep 0.2
rm -f /tmp/controller_main /tmp/runner_*
mkdir -p tmp

# --- compilar ---
make -s

# --- arrancar controller ---
bin/controller "$MAX_PAR" "$POLICY" &
CTRL_PID=$!

# aguardar FIFO estar disponível (até 3 segundos)
for _ in $(seq 1 30); do
    [ -p /tmp/controller_main ] && break
    sleep 0.1
done

if [ ! -p /tmp/controller_main ]; then
    echo "Erro: controller não arrancou a tempo." >&2
    kill "$CTRL_PID" 2>/dev/null
    exit 1
fi

echo "Controller pronto (PID=$CTRL_PID). A submeter workload..."

# -------------------------------------------------------
# Workload: 4 utilizadores, 20 comandos, durações variadas.
# User 1 submete 6 comandos (3s+2s+1s+2s+1s+1s) — utilizador dominante.
# User 2 submete 5 comandos (2s+1s+2s+1s+1s).
# User 3 submete 5 comandos (1s+2s+1s+1s+2s).
# User 4 submete 4 comandos (1s+2s+1s+2s).
#
# Com FCFS: user 1 ocupa as slots primeiro, restantes ficam à espera.
# Com RR  : slots alternam entre utilizadores distintos.
# -------------------------------------------------------
bin/runner -e 1 sleep 3 & P1=$!
bin/runner -e 1 sleep 2 & P2=$!
bin/runner -e 1 sleep 1 & P3=$!
bin/runner -e 1 sleep 2 & P4=$!
bin/runner -e 1 sleep 1 & P5=$!
bin/runner -e 1 sleep 1 & P6=$!
bin/runner -e 2 sleep 2 & P7=$!
bin/runner -e 2 sleep 1 & P8=$!
bin/runner -e 2 sleep 2 & P9=$!
bin/runner -e 2 sleep 1 & P10=$!
bin/runner -e 2 sleep 1 & P11=$!
bin/runner -e 3 sleep 1 & P12=$!
bin/runner -e 3 sleep 2 & P13=$!
bin/runner -e 3 sleep 1 & P14=$!
bin/runner -e 3 sleep 1 & P15=$!
bin/runner -e 3 sleep 2 & P16=$!
bin/runner -e 4 sleep 1 & P17=$!
bin/runner -e 4 sleep 2 & P18=$!
bin/runner -e 4 sleep 1 & P19=$!
bin/runner -e 4 sleep 2 & P20=$!

# aguardar todos os runners concluírem
wait $P1 $P2 $P3 $P4 $P5 $P6 $P7 $P8 $P9 $P10 $P11 $P12 $P13 $P14 $P15 $P16 $P17 $P18 $P19 $P20
echo "Todos os comandos concluídos. A encerrar controller..."

# encerramento síncrono — bloqueia até o controller responder
bin/runner -s
wait "$CTRL_PID"
