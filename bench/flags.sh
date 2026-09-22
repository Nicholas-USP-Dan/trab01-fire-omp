#!/bin/bash
# Mede o impacto das flags de otimizacao (secao 6.5 do relatorio).
# Roda fora do bench/run.sh porque compara binarios compilados de formas diferentes.
#
# Uso: bash bench/flags.sh [nucleos_fisicos]
set -u

cd "$(dirname "$0")/.." || exit 1
BENCH=bench
TMP=$BENCH/tmp
mkdir -p "$TMP"

NUCLEOS=${1:-$(lscpu | sed -n 's/^Core(s) per socket: *//p')}
REPS=3
ENTRADA=entrada_carga_grande.txt

awk -v t="$NUCLEOS" 'NR==1{$4=t} {print}' "$ENTRADA" > "$TMP/in_flags.txt"

mediana() { sort -g | sed -n "$(( (REPS + 1) / 2 ))p"; }

{
  echo "# carga grande, $NUCLEOS threads, static, mediana de $REPS execucoes"
  echo "# $(lscpu | sed -n 's/^Model name: *//p') | $(gcc --version | head -1)"
  for opt in "" "-O2" "-O3"; do
    gcc -std=c99 -fopenmp $opt fire_seq.c -o "$TMP/fs_flags" || exit 1
    gcc -std=c99 -fopenmp $opt fire_omp.c -o "$TMP/fo_flags" || exit 1
    s=$(for _ in $(seq $REPS); do ./"$TMP/fs_flags" "$ENTRADA" | sed -n 's/^tempo: //p'; done | mediana)
    o=$(for _ in $(seq $REPS); do OMP_SCHEDULE=static ./"$TMP/fo_flags" "$TMP/in_flags.txt" | sed -n 's/^tempo: //p'; done | mediana)
    echo "CFLAGS='$opt' seq=$s omp_${NUCLEOS}t=$o"
  done
} | tee "$BENCH/flags.txt"
