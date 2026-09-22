# Bateria de benchmarks

## Uso

```bash
bash bench/run.sh                  # 255 execucoes, ~10 min. Gera raw.csv e ambiente.txt
python3 bench/analise.py           # tabelas de tempo, speedup e eficiencia -> resultados.md
bash bench/flags.sh                # impacto de -O2 e -O3 -> flags.txt
bench/.venv/bin/python bench/graficos.py    # speedup.png/svg e eficiencia.png/svg
```

Para os graficos e preciso matplotlib:

```bash
python3 -m venv bench/.venv && bench/.venv/bin/pip install matplotlib
```

## Antes de medir

Feche editor e navegador, e suspenda o antivirus. A bateria satura as 16 threads
logicas, e qualquer processo concorrente distorce principalmente as medicoes com
8 e 16 threads.

```bash
sudo systemctl stop clamav-daemon
# ... rodar a bateria ...
sudo systemctl start clamav-daemon
```

O `run.sh` espera o load average cair abaixo de 0.6 (limite de 5 min) antes de
comecar, e grava o load de cada execucao no CSV. O `analise.py` sinaliza toda
configuracao com coeficiente de variacao acima de 5%.

## Arquivos

| Arquivo | Conteudo |
|---|---|
| `run.sh` | executa a bateria |
| `analise.py` | medianas, speedup, eficiencia, controle de qualidade |
| `graficos.py` | figuras do relatorio |
| `raw.csv` | dados brutos da ultima bateria |
| `ambiente.txt` | maquina, compilador e flags |
| `resultados.md` | tabelas geradas |
| `flags.sh` / `flags.txt` | impacto das flags de otimizacao (secao 6.5) |

## Outros graficos

`graficos.py` aceita `--csv`, `--metrica {speedup,eficiencia,tempo}`, `--cargas`,
`--schedules` e `--threads`. Para um grafico de outra natureza, monte a estrutura
`paineis` e chame `grafico_linhas()` diretamente - ela nao depende do formato do CSV.
