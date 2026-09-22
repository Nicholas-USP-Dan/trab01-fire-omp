# Resultados experimentais

## Determinismo

| Carga | Execucoes | Checksums distintos | Veredito |
|---|---|---|---|
| pequena | 80 | 1 | OK |
| media | 80 | 1 | OK |
| grande | 95 | 1 | OK |

## Tempos, speedup e eficiencia

### Carga pequena (400x500) — sequencial = 0.2415 s

| T | static (s) | speedup | eficiencia | dynamic-64 (s) | speedup | eficiencia | guided (s) | speedup | eficiencia |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 0.2563 | 0.94 | 94.2% | 0.2602 | 0.93 | 92.8% | 0.2559 | 0.94 | 94.4% |
| 2 | 0.1303 | 1.85 | 92.7% | 0.1384 | 1.74 | 87.2% | 0.1301 | 1.86 | 92.8% |
| 4 | 0.0692 | 3.49 | 87.2% | 0.0903 | 2.67 | 66.8% | 0.0686 | 3.52 | 88.0% |
| 8 | 0.0666 | 3.63 | 45.3% | 0.0891 | 2.71 | 33.9% | 0.0666 | 3.63 | 45.3% |
| 16 | 0.0860 | 2.81 | 17.6% | 0.0899 | 2.69 | 16.8% | 0.0704 | 3.43 | 21.4% |

### Carga media (1200x1500) — sequencial = 2.8141 s

| T | static (s) | speedup | eficiencia | dynamic-64 (s) | speedup | eficiencia | guided (s) | speedup | eficiencia |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 2.9748 | 0.95 | 94.6% | 3.0187 | 0.93 | 93.2% | 2.9931 | 0.94 | 94.0% |
| 2 | 1.5181 | 1.85 | 92.7% | 1.6271 | 1.73 | 86.5% | 1.5170 | 1.86 | 92.8% |
| 4 | 0.8303 | 3.39 | 84.7% | 0.9112 | 3.09 | 77.2% | 0.8142 | 3.46 | 86.4% |
| 8 | 0.7834 | 3.59 | 44.9% | 0.9256 | 3.04 | 38.0% | 0.7806 | 3.61 | 45.1% |
| 16 | 0.8200 | 3.43 | 21.4% | 0.9018 | 3.12 | 19.5% | 0.8152 | 3.45 | 21.6% |

### Carga grande (2500x2500) — sequencial = 9.8161 s

| T | static (s) | speedup | eficiencia | dynamic-64 (s) | speedup | eficiencia | guided (s) | speedup | eficiencia |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 10.4222 | 0.94 | 94.2% | 10.5852 | 0.93 | 92.7% | 10.4182 | 0.94 | 94.2% |
| 2 | 5.3043 | 1.85 | 92.5% | 5.6325 | 1.74 | 87.1% | 5.2894 | 1.86 | 92.8% |
| 4 | 2.8299 | 3.47 | 86.7% | 3.0656 | 3.20 | 80.1% | 2.8134 | 3.49 | 87.2% |
| 8 | 2.7152 | 3.62 | 45.2% | 2.8631 | 3.43 | 42.9% | 2.6671 | 3.68 | 46.0% |
| 16 | 2.7582 | 3.56 | 22.2% | 2.8823 | 3.41 | 21.3% | 2.8218 | 3.48 | 21.7% |

## dynamic,1 — o default do libgomp (carga grande)

| T | tempo (s) | speedup | vs static |
|---|---|---|---|
| 1 | 16.0849 | 0.61 | 1.54x mais lento |
| 2 | 20.1827 | 0.49 | 3.80x mais lento |
| 4 | 18.2072 | 0.54 | 6.43x mais lento |
| 8 | 16.1714 | 0.61 | 5.96x mais lento |
| 16 | 16.0566 | 0.61 | 5.82x mais lento |

## Modelo de desempenho

Sp abs = Tseq/Tp. Sp rel = Tpar_1/Tp. e(p) = (p/Sp_rel - 1)/(p - 1).
CT = p*Tp. To = CT - Tseq.

### Carga pequena (Tseq = 0.2415 s, Tpar_1 = 0.2563 s)

| p | Tp (s) | Sp abs | Sp rel | E rel | e(p) | CT | To |
|---|---|---|---|---|---|---|---|
| 1 | 0.2563 | 0.94 | 1.00 | 100.0% | - | 0.256 | +0.015 |
| 2 | 0.1303 | 1.85 | 1.97 | 98.4% | 1.66% | 0.261 | +0.019 |
| 4 | 0.0692 | 3.49 | 3.70 | 92.6% | 2.68% | 0.277 | +0.035 |
| 8 | 0.0666 | 3.63 | 3.85 | 48.1% | 15.43% | 0.533 | +0.291 |
| 16 | 0.0860 | 2.81 | 2.98 | 18.6% | 29.11% | 1.375 | +1.134 |

Amdahl com f = e(8) = 15.43%: S(16) previsto = 4.83, medido = 2.98, S(inf) = 6.5.

### Carga media (Tseq = 2.8141 s, Tpar_1 = 2.9748 s)

| p | Tp (s) | Sp abs | Sp rel | E rel | e(p) | CT | To |
|---|---|---|---|---|---|---|---|
| 1 | 2.9748 | 0.95 | 1.00 | 100.0% | - | 2.975 | +0.161 |
| 2 | 1.5181 | 1.85 | 1.96 | 98.0% | 2.07% | 3.036 | +0.222 |
| 4 | 0.8303 | 3.39 | 3.58 | 89.6% | 3.88% | 3.321 | +0.507 |
| 8 | 0.7834 | 3.59 | 3.80 | 47.5% | 15.81% | 6.267 | +3.453 |
| 16 | 0.8200 | 3.43 | 3.63 | 22.7% | 22.74% | 13.120 | +10.306 |

Amdahl com f = e(8) = 15.81%: S(16) previsto = 4.75, medido = 3.63, S(inf) = 6.3.

### Carga grande (Tseq = 9.8161 s, Tpar_1 = 10.4222 s)

| p | Tp (s) | Sp abs | Sp rel | E rel | e(p) | CT | To |
|---|---|---|---|---|---|---|---|
| 1 | 10.4222 | 0.94 | 1.00 | 100.0% | - | 10.422 | +0.606 |
| 2 | 5.3043 | 1.85 | 1.96 | 98.2% | 1.79% | 10.609 | +0.793 |
| 4 | 2.8299 | 3.47 | 3.68 | 92.1% | 2.87% | 11.320 | +1.504 |
| 8 | 2.7152 | 3.62 | 3.84 | 48.0% | 15.49% | 21.721 | +11.905 |
| 16 | 2.7582 | 3.56 | 3.78 | 23.6% | 21.56% | 44.132 | +34.316 |

Amdahl com f = e(8) = 15.49%: S(16) previsto = 4.81, medido = 3.78, S(inf) = 6.5.

### Eixo da carga de trabalho: speedup relativo por tamanho

| Carga | Celulas | p=2 | p=4 | p=8 | p=16 |
|---|---|---|---|---|---|
| pequena | 200000 | 1.97 | 3.70 | 3.85 | 2.98 |
| media | 1800000 | 1.96 | 3.58 | 3.80 | 3.63 |
| grande | 6250000 | 1.96 | 3.68 | 3.84 | 3.78 |

## Controle de qualidade das medicoes

Configuracoes com coeficiente de variacao acima de 5.0%:

| Carga | T | Schedule | cv | Valores (s) |
|---|---|---|---|---|
| pequena | 16 | guided | 9.7% | 0.0703, 0.0704, 0.0704, 0.0705, 0.0863 |

> Variabilidade alta indica interferencia de outros processos. Repita a bateria com a maquina ociosa antes de usar estes valores.

Load average durante a bateria: minimo 0.41, mediana 1.50, maximo 11.82.
