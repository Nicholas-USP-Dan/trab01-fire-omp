#!/usr/bin/env python3
"""Gera os graficos do relatorio a partir de bench/raw.csv.

Uso:
    python3 bench/graficos.py                      # speedup e eficiencia
    python3 bench/graficos.py --metrica tempo      # tempo absoluto
    python3 bench/graficos.py --csv outro.csv --cargas grande

A funcao grafico_linhas() e independente do CSV: para um grafico novo a partir
de outros dados, monte a estrutura `paineis` e chame-a.
"""
import argparse
import csv
import os
import statistics as st
import sys
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter
except ImportError:
    sys.exit("matplotlib ausente. Instale com:\n"
             "  python3 -m venv bench/.venv && bench/.venv/bin/pip install matplotlib\n"
             "e rode com bench/.venv/bin/python bench/graficos.py")

BENCH = os.path.dirname(os.path.abspath(__file__))

# Paleta categorica validada (CVD-safe). Atribuir na ordem, nunca ciclar.
PALETA = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100",
          "#e87ba4", "#008300", "#4a3aa7", "#e34948"]
MARCADORES = ["o", "s", "^", "D", "v", "P", "X", "*"]
ESTILOS = ["-"] * 8

# Series que coincidem: desenhar uma delas como faixa larga translucida por baixo
# mantem as duas legiveis sem quebrar a continuidade de nenhuma das linhas.
FAIXA = dict(lw=7, alpha=0.30, marker="", zorder=2)

SUPERFICIE = "#fcfcfb"
TINTA = "#0b0b0b"
TINTA_FRACA = "#52514e"
TINTA_MUDA = "#8a8a85"

NOMES = {"static": "static", "dynamic-64": "dynamic,64", "guided": "guided",
         "dynamic-1": "dynamic,1"}


def grafico_linhas(paineis, ylabel, arquivo, titulo=None, subtitulo=None,
                   referencia=None, marca_vertical=None, ylim=None,
                   formato_y=None, largura=11, altura=4.4, loc_legenda="upper left",
                   estilos=None, estilos_serie=None):
    """Desenha um ou mais paineis de linhas com rotulos diretos.

    paineis: lista de (titulo_do_painel, {nome_serie: (xs, ys)})
    referencia: (xs, ys, rotulo) desenhado em cinza tracejado em todos os paineis
    marca_vertical: (x, rotulo) linha vertical de anotacao
    """
    n = len(paineis)
    fig, eixos = plt.subplots(1, n, figsize=(largura, altura), sharey=True)
    if n == 1:
        eixos = [eixos]
    fig.patch.set_facecolor(SUPERFICIE)

    ordem = list(dict.fromkeys(k for _, s in paineis for k in s))
    estilos = estilos or ESTILOS
    estilos_serie = estilos_serie or {}

    for ax, (nome_painel, series) in zip(eixos, paineis):
        ax.set_facecolor(SUPERFICIE)

        if referencia:
            rx, ry, rot = referencia
            ax.plot(rx, ry, color=TINTA_MUDA, lw=1.4, ls=(0, (4, 3)), zorder=1)
            ax.annotate(rot, (rx[-1], ry[-1]), textcoords="offset points",
                        xytext=(4, 2), fontsize=8, color=TINTA_MUDA)

        if marca_vertical:
            mx, mrot = marca_vertical
            ax.axvline(mx, color=TINTA_MUDA, lw=1, ls=":", zorder=0)

        finais = []
        for s, nome in enumerate(ordem):
            if nome not in series:
                continue
            xs, ys = series[nome]
            cor = PALETA[s % len(PALETA)]
            kw = dict(color=cor, lw=2, ls=estilos[s % len(estilos)],
                      marker=MARCADORES[s % len(MARCADORES)],
                      ms=6.5, mew=1.5, mec=SUPERFICIE, zorder=3,
                      label=NOMES.get(nome, nome), solid_capstyle="round")
            kw.update(estilos_serie.get(nome, {}))
            ax.plot(xs, ys, **kw)
            finais.append((ys[-1], xs[-1], NOMES.get(nome, nome)))

        # Rotulos diretos com afastamento minimo, para series coincidentes.
        if ylim:
            ax.set_ylim(*ylim)
        passo = (ax.get_ylim()[1] - ax.get_ylim()[0]) * 0.052
        finais.sort()
        for i in range(1, len(finais)):
            if finais[i][0] - finais[i - 1][0] < passo:
                finais[i] = (finais[i - 1][0] + passo, finais[i][1], finais[i][2])
        for y, x, rot in finais:
            ax.annotate(rot, (x, y), textcoords="offset points", xytext=(9, -3),
                        fontsize=8.5, color=TINTA_FRACA, zorder=4,
                        annotation_clip=False)

        if marca_vertical:
            ax.annotate(marca_vertical[1], (marca_vertical[0], ax.get_ylim()[1]),
                        textcoords="offset points", xytext=(3, -10),
                        fontsize=7.5, color=TINTA_MUDA, ha="left", va="top")

        ax.set_xscale("log", base=2)
        ax.set_xticks(sorted({x for _, s in paineis for xs, _ in s.values() for x in xs}))
        ax.get_xaxis().set_major_formatter(FuncFormatter(lambda v, _: f"{int(v)}"))
        ax.set_xlabel("threads", fontsize=9, color=TINTA_FRACA)
        ax.set_title(nome_painel, fontsize=10, color=TINTA, pad=8, loc="left")
        ax.grid(axis="y", color="#e6e6e2", lw=0.8, zorder=0)
        ax.set_axisbelow(True)
        for lado in ("top", "right"):
            ax.spines[lado].set_visible(False)
        for lado in ("left", "bottom"):
            ax.spines[lado].set_color("#d8d8d3")
        ax.tick_params(colors=TINTA_FRACA, labelsize=8.5)
        if ylim:
            ax.set_ylim(*ylim)
        if formato_y:
            ax.get_yaxis().set_major_formatter(FuncFormatter(formato_y))
        ax.margins(x=0.16)

    eixos[0].set_ylabel(ylabel, fontsize=9, color=TINTA_FRACA)
    eixos[0].legend(frameon=False, fontsize=8.5, labelcolor=TINTA_FRACA, loc=loc_legenda)

    if titulo:
        fig.suptitle(titulo, fontsize=12, color=TINTA, x=0.008, ha="left", y=0.99)
    if subtitulo:
        fig.text(0.008, 0.925, subtitulo, fontsize=8.5, color=TINTA_FRACA, ha="left")

    fig.tight_layout(rect=(0, 0, 1, 0.90 if titulo else 1))
    for ext in ("png", "svg"):
        caminho = os.path.join(BENCH, f"{arquivo}.{ext}")
        fig.savefig(caminho, dpi=200, facecolor=SUPERFICIE)
        print(f"  gravado: {caminho}")
    plt.close(fig)


def carregar(caminho):
    """Le o CSV da bateria e devolve as medianas por configuracao."""
    amostras = defaultdict(list)
    with open(caminho) as f:
        for r in csv.DictReader(f):
            chave = (r["carga"], r["versao"], int(r["threads"]), r["schedule"])
            amostras[chave].append(float(r["tempo"]))
    return {k: st.median(v) for k, v in amostras.items()}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--csv", default=os.path.join(BENCH, "raw.csv"))
    p.add_argument("--metrica", default="ambos",
                   choices=["speedup", "eficiencia", "tempo", "ambos"])
    p.add_argument("--cargas", nargs="+", default=["media", "grande"])
    p.add_argument("--schedules", nargs="+",
                   default=["static", "dynamic-64", "guided"])
    p.add_argument("--threads", nargs="+", type=int, default=[1, 2, 4, 8, 16])
    p.add_argument("--nucleos-fisicos", type=int, default=8)
    args = p.parse_args()

    if not os.path.exists(args.csv):
        sys.exit(f"nao encontrei {args.csv} - rode antes: bash bench/run.sh")

    med = carregar(args.csv)
    dims = {"pequena": "400x500", "media": "1200x1500", "grande": "2500x2500"}
    T = args.threads

    def paineis_de(transformacao):
        out = []
        for carga in args.cargas:
            base = med.get((carga, "seq", 1, "static"))
            series = {}
            for s in args.schedules:
                pontos = [(t, med[(carga, "omp", t, s)]) for t in T
                          if (carga, "omp", t, s) in med]
                if pontos:
                    xs = [t for t, _ in pontos]
                    series[s] = (xs, [transformacao(base, t, v) for t, v in pontos])
            if series:
                rotulo = f"{carga} ({dims.get(carga, '')})"
                out.append((rotulo, series))
        return out

    marca = (args.nucleos_fisicos, f"{args.nucleos_fisicos} nucleos fisicos")

    if args.metrica in ("speedup", "ambos"):
        print("speedup:")
        grafico_linhas(
            paineis_de(lambda base, t, v: base / v),
            ylabel="speedup", arquivo="speedup",
            titulo="Speedup por numero de threads",
            subtitulo="AMD Ryzen 7 5700X (8 nucleos / 16 threads) · gcc 13.3 -O2 · mediana de 5 execucoes",
            referencia=(T, T, "linear ideal"), estilos_serie={"guided": FAIXA},
            marca_vertical=marca, ylim=(0, 9))

    if args.metrica in ("eficiencia", "ambos"):
        print("eficiencia:")
        grafico_linhas(
            paineis_de(lambda base, t, v: base / v / t * 100),
            ylabel="eficiencia", arquivo="eficiencia",
            titulo="Eficiencia por numero de threads",
            subtitulo="Eficiencia = speedup / threads. Acima de 8 threads nao ha nucleos fisicos adicionais.",
            referencia=(T, [100] * len(T), "100%"),
            marca_vertical=marca, ylim=(0, 118), loc_legenda="lower left",
            estilos_serie={"guided": FAIXA},
            formato_y=lambda v, _: f"{v:.0f}%")

    if args.metrica == "tempo":
        print("tempo:")
        grafico_linhas(
            paineis_de(lambda base, t, v: v),
            ylabel="tempo (s)", arquivo="tempo",
            titulo="Tempo do nucleo da simulacao",
            marca_vertical=marca)


if __name__ == "__main__":
    main()
