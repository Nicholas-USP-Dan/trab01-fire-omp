#!/usr/bin/env python3
"""Gera as tabelas de tempo, speedup e eficiencia a partir de bench/raw.csv."""
import csv
import os
import statistics as st
import sys
from collections import defaultdict

BENCH = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.join(BENCH, "raw.csv")
SAIDA = os.path.join(BENCH, "resultados.md")

LOADS = ["pequena", "media", "grande"]
DIMS = {"pequena": "400x500", "media": "1200x1500", "grande": "2500x2500"}
SCHEDULES = ["static", "dynamic-64", "guided"]
THREADS = [1, 2, 4, 8, 16]
CV_LIMITE = 5.0

if not os.path.exists(RAW):
    sys.exit(f"nao encontrei {RAW} - rode antes: bash bench/run.sh")

amostras = defaultdict(list)
cargas_maquina = []
with open(RAW) as f:
    for r in csv.DictReader(f):
        amostras[(r["carga"], r["versao"], int(r["threads"]), r["schedule"])].append(float(r["tempo"]))
        if "loadavg" in r and r["loadavg"]:
            cargas_maquina.append(float(r["loadavg"]))

checksums = defaultdict(set)
with open(RAW) as f:
    for r in csv.DictReader(f):
        checksums[r["carga"]].add(r["checksum"])

med = {k: st.median(v) for k, v in amostras.items()}
cv = {k: (st.stdev(v) / st.mean(v) * 100 if len(v) > 1 else 0.0) for k, v in amostras.items()}

out = []
w = out.append

w("# Resultados experimentais\n")

w("## Determinismo\n")
w("| Carga | Execucoes | Checksums distintos | Veredito |")
w("|---|---|---|---|")
for L in LOADS:
    n = sum(len(v) for k, v in amostras.items() if k[0] == L)
    d = len(checksums[L])
    w(f"| {L} | {n} | {d} | {'OK' if d == 1 else 'FALHOU'} |")
w("")

w("## Tempos, speedup e eficiencia\n")
for L in LOADS:
    base = med.get((L, "seq", 1, "static"))
    if base is None:
        continue
    w(f"### Carga {L} ({DIMS[L]}) — sequencial = {base:.4f} s\n")
    w("| T | " + " | ".join(f"{s} (s) | speedup | eficiencia" for s in SCHEDULES) + " |")
    w("|---|" + "---|" * (3 * len(SCHEDULES)))
    for T in THREADS:
        celulas = []
        for s in SCHEDULES:
            t = med.get((L, "omp", T, s))
            if t is None:
                celulas += ["-", "-", "-"]
            else:
                celulas += [f"{t:.4f}", f"{base / t:.2f}", f"{base / t / T * 100:.1f}%"]
        w(f"| {T} | " + " | ".join(celulas) + " |")
    w("")

d1 = [(T, med.get(("grande", "omp", T, "dynamic-1"))) for T in THREADS]
if any(t for _, t in d1):
    base = med[("grande", "seq", 1, "static")]
    w("## dynamic,1 — o default do libgomp (carga grande)\n")
    w("| T | tempo (s) | speedup | vs static |")
    w("|---|---|---|---|")
    for T, t in d1:
        if t is None:
            continue
        s = med.get(("grande", "omp", T, "static"))
        w(f"| {T} | {t:.4f} | {base / t:.2f} | {t / s:.2f}x mais lento |")
    w("")

w("## Controle de qualidade das medicoes\n")
ruins = sorted((k for k in cv if cv[k] > CV_LIMITE), key=lambda k: -cv[k])
if ruins:
    w(f"Configuracoes com coeficiente de variacao acima de {CV_LIMITE}%:\n")
    w("| Carga | T | Schedule | cv | Valores (s) |")
    w("|---|---|---|---|---|")
    for k in ruins:
        vals = ", ".join(f"{v:.4f}" for v in sorted(amostras[k]))
        w(f"| {k[0]} | {k[2]} | {k[3]} | {cv[k]:.1f}% | {vals} |")
    w("\n> Variabilidade alta indica interferencia de outros processos. "
      "Repita a bateria com a maquina ociosa antes de usar estes valores.\n")
else:
    w(f"Nenhuma configuracao passou de {CV_LIMITE}% de coeficiente de variacao.\n")

if cargas_maquina:
    w(f"Load average durante a bateria: minimo {min(cargas_maquina):.2f}, "
      f"mediana {st.median(cargas_maquina):.2f}, maximo {max(cargas_maquina):.2f}.\n")

texto = "\n".join(out)
with open(SAIDA, "w") as f:
    f.write(texto)
print(texto)
print(f"\n[gravado em {SAIDA}]")
