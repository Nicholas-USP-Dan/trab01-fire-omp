# Resultados experimentais

## Determinismo

| Carga | Execucoes | Checksums distintos | Veredito |
|---|---|---|---|
| pequena | 80 | 1 | OK |
| media | 80 | 1 | OK |
| grande | 95 | 1 | OK |

## Tempos, speedup e eficiencia

### Carga pequena (400x500) — sequencial = 0.1896 s

| T | static (s) | speedup | eficiencia | dynamic-64 (s) | speedup | eficiencia | guided (s) | speedup | eficiencia |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 0.1855 | 1.02 | 102.2% | 0.1872 | 1.01 | 101.3% | 0.1858 | 1.02 | 102.0% |
| 2 | 0.0935 | 2.03 | 101.3% | 0.1002 | 1.89 | 94.6% | 0.0928 | 2.04 | 102.2% |
| 4 | 0.0483 | 3.93 | 98.2% | 0.0626 | 3.03 | 75.7% | 0.0489 | 3.88 | 97.0% |
| 8 | 0.0300 | 6.32 | 79.0% | 0.0394 | 4.81 | 60.1% | 0.0277 | 6.84 | 85.5% |
| 16 | 0.0265 | 7.14 | 44.6% | 0.0693 | 2.74 | 17.1% | 0.0282 | 6.73 | 42.0% |

### Carga media (1200x1500) — sequencial = 2.1593 s

| T | static (s) | speedup | eficiencia | dynamic-64 (s) | speedup | eficiencia | guided (s) | speedup | eficiencia |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 2.1244 | 1.02 | 101.6% | 2.1440 | 1.01 | 100.7% | 2.1172 | 1.02 | 102.0% |
| 2 | 1.0621 | 2.03 | 101.7% | 1.1458 | 1.88 | 94.2% | 1.0614 | 2.03 | 101.7% |
| 4 | 0.5415 | 3.99 | 99.7% | 0.6060 | 3.56 | 89.1% | 0.5423 | 3.98 | 99.6% |
| 8 | 0.3145 | 6.87 | 85.8% | 0.4080 | 5.29 | 66.2% | 0.3129 | 6.90 | 86.3% |
| 16 | 0.3033 | 7.12 | 44.5% | 0.4576 | 4.72 | 29.5% | 0.3045 | 7.09 | 44.3% |

### Carga grande (2500x2500) — sequencial = 7.5232 s

| T | static (s) | speedup | eficiencia | dynamic-64 (s) | speedup | eficiencia | guided (s) | speedup | eficiencia |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 7.3788 | 1.02 | 102.0% | 7.4772 | 1.01 | 100.6% | 7.3744 | 1.02 | 102.0% |
| 2 | 3.7042 | 2.03 | 101.5% | 3.9409 | 1.91 | 95.5% | 3.6986 | 2.03 | 101.7% |
| 4 | 1.8945 | 3.97 | 99.3% | 2.1018 | 3.58 | 89.5% | 1.8898 | 3.98 | 99.5% |
| 8 | 1.1277 | 6.67 | 83.4% | 1.2556 | 5.99 | 74.9% | 1.1190 | 6.72 | 84.0% |
| 16 | 1.0762 | 6.99 | 43.7% | 1.3291 | 5.66 | 35.4% | 1.0812 | 6.96 | 43.5% |

## dynamic,1 — o default do libgomp (carga grande)

| T | tempo (s) | speedup | vs static |
|---|---|---|---|
| 1 | 9.2595 | 0.81 | 1.25x mais lento |
| 2 | 10.6666 | 0.71 | 2.88x mais lento |
| 4 | 9.4409 | 0.80 | 4.98x mais lento |
| 8 | 8.7057 | 0.86 | 7.72x mais lento |
| 16 | 8.8693 | 0.85 | 8.24x mais lento |

## Modelo de desempenho

Sp abs = Tseq/Tp. Sp rel = Tpar_1/Tp. e(p) = (p/Sp_rel - 1)/(p - 1).
CT = p*Tp. To = CT - Tseq.

### Carga pequena (Tseq = 0.1896 s, Tpar_1 = 0.1855 s)

| p | Tp (s) | Sp abs | Sp rel | E rel | e(p) | CT | To |
|---|---|---|---|---|---|---|---|
| 1 | 0.1855 | 1.02 | 1.00 | 100.0% | - | 0.186 | -0.004 |
| 2 | 0.0935 | 2.03 | 1.98 | 99.2% | 0.83% | 0.187 | -0.003 |
| 4 | 0.0483 | 3.93 | 3.84 | 96.1% | 1.35% | 0.193 | +0.003 |
| 8 | 0.0300 | 6.32 | 6.19 | 77.3% | 4.19% | 0.240 | +0.050 |
| 16 | 0.0265 | 7.14 | 6.99 | 43.7% | 8.59% | 0.425 | +0.235 |

Amdahl com f = e(8) = 4.19%: S(16) previsto = 9.83, medido = 6.99, S(inf) = 23.9.

### Carga media (Tseq = 2.1593 s, Tpar_1 = 2.1244 s)

| p | Tp (s) | Sp abs | Sp rel | E rel | e(p) | CT | To |
|---|---|---|---|---|---|---|---|
| 1 | 2.1244 | 1.02 | 1.00 | 100.0% | - | 2.124 | -0.035 |
| 2 | 1.0621 | 2.03 | 2.00 | 100.0% | -0.02% | 2.124 | -0.035 |
| 4 | 0.5415 | 3.99 | 3.92 | 98.1% | 0.65% | 2.166 | +0.007 |
| 8 | 0.3145 | 6.87 | 6.75 | 84.4% | 2.63% | 2.516 | +0.357 |
| 16 | 0.3033 | 7.12 | 7.00 | 43.8% | 8.56% | 4.852 | +2.693 |

Amdahl com f = e(8) = 2.63%: S(16) previsto = 11.47, medido = 7.00, S(inf) = 38.0.

### Carga grande (Tseq = 7.5232 s, Tpar_1 = 7.3788 s)

| p | Tp (s) | Sp abs | Sp rel | E rel | e(p) | CT | To |
|---|---|---|---|---|---|---|---|
| 1 | 7.3788 | 1.02 | 1.00 | 100.0% | - | 7.379 | -0.144 |
| 2 | 3.7042 | 2.03 | 1.99 | 99.6% | 0.40% | 7.408 | -0.115 |
| 4 | 1.8945 | 3.97 | 3.89 | 97.4% | 0.90% | 7.578 | +0.055 |
| 8 | 1.1277 | 6.67 | 6.54 | 81.8% | 3.18% | 9.022 | +1.498 |
| 16 | 1.0762 | 6.99 | 6.86 | 42.9% | 8.89% | 17.220 | +9.697 |

Amdahl com f = e(8) = 3.18%: S(16) previsto = 10.83, medido = 6.86, S(inf) = 31.4.

### Eixo da carga de trabalho: speedup relativo por tamanho

| Carga | Celulas | p=2 | p=4 | p=8 | p=16 |
|---|---|---|---|---|---|
| pequena | 200000 | 1.98 | 3.84 | 6.19 | 6.99 |
| media | 1800000 | 2.00 | 3.92 | 6.75 | 7.00 |
| grande | 6250000 | 1.99 | 3.89 | 6.54 | 6.86 |

## Controle de qualidade das medicoes

Configuracoes com coeficiente de variacao acima de 5.0%:

| Carga | T | Schedule | cv | Valores (s) |
|---|---|---|---|---|
| pequena | 16 | guided | 69.0% | 0.0278, 0.0281, 0.0282, 0.0948, 0.1027 |
| pequena | 8 | static | 23.0% | 0.0277, 0.0292, 0.0300, 0.0337, 0.0466 |
| pequena | 8 | guided | 10.6% | 0.0275, 0.0276, 0.0277, 0.0303, 0.0348 |
| media | 1 | static | 10.3% | 2.1423, 2.1531, 2.1593, 2.1621, 2.6737 |
| media | 16 | guided | 9.1% | 0.2996, 0.3014, 0.3045, 0.3132, 0.3680 |

> Variabilidade alta indica interferencia de outros processos. Repita a bateria com a maquina ociosa antes de usar estes valores.

Load average durante a bateria: minimo 0.59, mediana 1.58, maximo 8.09.
