#!/bin/bash
# Bateria de benchmarks - SSC0903 TB1
# Uso: bash bench/run.sh          (5 repeticoes)
#      REPS=7 bash bench/run.sh   (outro numero de repeticoes)
set -u

cd "$(dirname "$0")/.." || exit 1
BENCH=bench
TMP=$BENCH/tmp
mkdir -p "$TMP"

REPS=${REPS:-5}
LOADS="pequena media grande"
THREADS="1 2 4 8 16"
SCHEDULES="static dynamic,64 guided"

make >/dev/null 2>&1 || { echo "build falhou"; exit 1; }

{
  echo "data: $(date -Is)"
  echo "cpu: $(lscpu | sed -n 's/^Model name: *//p')"
  echo "nucleos_fisicos: $(lscpu | sed -n 's/^Core(s) per socket: *//p')"
  echo "threads_logicas: $(nproc)"
  echo "l3: $(lscpu | sed -n 's/^L3 cache: *//p')"
  echo "memoria: $(free -h | awk 'NR==2{print $2}')"
  echo "so: $(lsb_release -ds 2>/dev/null || sed -n 's/^PRETTY_NAME=//p' /etc/os-release)"
  echo "kernel: $(uname -r)"
  echo "gcc: $(gcc --version | head -1)"
  echo "cflags: $(sed -n 's/^CFLAGS=//p' Makefile)"
  echo "repeticoes: $REPS"
  echo "loadavg_inicial: $(cut -d' ' -f1 /proc/loadavg)"
} > "$BENCH/ambiente.txt"

carga_atual() { cut -d' ' -f1 /proc/loadavg; }

echo "carga,versao,threads,schedule,rep,tempo,checksum,loadavg" > "$BENCH/raw.csv"

executa() {
  local rotulo=$1 bin=$2 entrada=$3 sched=$4 T=$5 r=$6
  local saida tempo cks
  saida=$(OMP_SCHEDULE="$sched" ./$bin "$entrada")
  tempo=$(echo "$saida" | sed -n 's/^tempo: //p')
  cks=$(echo "$saida" | sed -n 's/^checksum: //p')
  echo "$rotulo,$T,${sched/,/-},$r,$tempo,$cks,$(carga_atual)" >> "$BENCH/raw.csv"
}

for L in $LOADS; do
  for r in $(seq "$REPS"); do
    executa "$L,seq" fire_seq "entrada_carga_$L.txt" static 1 "$r"
  done
done

for L in $LOADS; do
  for T in $THREADS; do
    awk -v t="$T" 'NR==1{$4=t} {print}' "entrada_carga_$L.txt" > "$TMP/in_${L}_${T}.txt"
    for S in $SCHEDULES; do
      for r in $(seq "$REPS"); do
        executa "$L,omp" fire_omp "$TMP/in_${L}_${T}.txt" "$S" "$T" "$r"
      done
    done
  done
done

# dynamic,1 e o default do libgomp quando OMP_SCHEDULE nao e definida
for T in $THREADS; do
  for r in $(seq 3); do
    executa "grande,omp" fire_omp "$TMP/in_grande_${T}.txt" "dynamic,1" "$T" "$r"
  done
done

echo "pronto: $BENCH/raw.csv ($(( $(wc -l < "$BENCH/raw.csv") - 1 )) execucoes)"
echo "analise: python3 $BENCH/analise.py"
