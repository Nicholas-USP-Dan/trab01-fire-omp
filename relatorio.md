# Simulação paralela da propagação direcional de incêndio com zonas de contenção

SSC0903 - Computação de Alto Desempenho
Primeiro Trabalho Prático (TB1), turma G09

| NUSP | Nome |
|------|------|
| 15618344 | Gabriel Demba |
| 14600749 | Nicholas Eiti Dan |
| 18518661 | Rodrigo Li Chumpitaz |
| 12543565 | Samuel de Assunção Ferreira |
| 15595392 | Wiltord Nyakeruma Mosingi |

---

## 1. Introdução

Este trabalho implementa a simulação da propagação de incêndio em uma área florestal
representada por uma matriz de `L × C` células, sob influência de vento direcional e de zonas
de contenção ativadas em passos pré-determinados. Foram desenvolvidas duas versões: uma
sequencial (`fire_seq.c`) e uma paralela em OpenMP (`fire_omp.c`).

O modelo é um autômato celular determinístico. O estado de cada célula no passo `p+1` depende
apenas do estado da vizinhança de Moore no passo `p`, e é essa propriedade que sustenta toda a
estratégia de paralelização descrita adiante.

---

## 2. Solução sequencial

### 2.1 Estruturas de dados

A área é representada por vetores lineares de `L × C` posições, indexados por
`indice = linha * C + coluna`. São mantidos:

- `cobertura[]` e `umidade[]`, constantes durante toda a simulação;
- `estado_atual[]` e `proximo_estado[]`, o duplo buffer de estados;
- `tempo_atual[]` e `proximo_tempo[]`, o duplo buffer de tempo de queima;
- `ativacao[]`, com o passo de ativação da zona de contenção ou `-1`.

Usamos vetores separados em vez de um vetor de `struct` porque assim cada campo é percorrido de
forma contígua, o que ajuda a vetorização do laço interno.

### 2.2 Preparação

1. Leitura e validação da entrada, cobrindo as restrições da seção 5 do enunciado.
2. Geração da cobertura e da umidade, percorrendo a matriz em ordem linear crescente. Para cada
   célula gera-se primeiro `rand_r(&seed) % 100` (cobertura) e logo em seguida
   `rand_r(&seed) % 101` (umidade). Essa ordem é obrigatória, porque qualquer alteração produz
   uma floresta diferente.
3. Aplicação dos focos iniciais, com tempo de queima 2 para vegetação rasteira e 4 para floresta.
4. Construção do mapa de ativação, guardando o menor passo de ativação quando há zonas
   sobrepostas.

Nenhuma dessas etapas entra no trecho cronometrado.

### 2.3 Laço de simulação

Cada passo `p` executa, nesta ordem:

1. ativação das zonas programadas para `p` (apenas células intactas tornam-se contenção);
2. cálculo de `proximo_estado`/`proximo_tempo` a partir de `estado_atual`/`tempo_atual`;
3. acumulação das estatísticas do passo (novas ignições, células em chamas);
4. troca dos buffers por permuta de ponteiros;
5. verificação da condição de parada.

O potencial de ignição de uma célula intacta é calculado sobre os oito vizinhos de Moore,
considerando apenas os que estão em chamas, com peso básico 10 (ortogonal) ou 7 (diagonal),
ajustado pelo alinhamento com o vento `A = prop_linha · vento_linha + prop_coluna · vento_coluna`
e pela intensidade `V`, resultando em `Pv = max(1, P_basico + V · A)`. Toda a aritmética é
inteira, conforme exigido.

A troca dos buffers é feita por permuta de ponteiros em vez de cópia, o que evita `2 · L · C`
escritas a cada passo.

---

## 3. Solução paralela

### 3.1 Estratégia

A decisão central foi usar uma única região paralela persistente, que engloba todo o laço de
passos:

```c
#pragma omp parallel num_threads(T) default(none) shared(...)
{
    for (int p = 0; p < P && continuar; p++)
    {
        #pragma omp for schedule(runtime)      /* ativação das zonas  */
        #pragma omp single                     /* zera acumuladores   */
        #pragma omp for schedule(runtime) reduction(+:...)  /* atualização */
        #pragma omp single                     /* estatísticas, troca, parada */
    }
}
```

A alternativa mais imediata seria abrir um `#pragma omp parallel for` dentro do laço de passos.
Ela produz o mesmo resultado, mas cria e destrói a equipe de threads uma vez por passo, o que
com `P = 100` significa 100 criações desnecessárias.

Todas as threads percorrem o laço de passos. O que se repete `T` vezes é apenas o controle do
laço, ou seja, o incremento e o teste da condição, cujo custo é desprezível. O trabalho efetivo
fica a cargo dos `omp for`.

### 3.2 Ausência de condições de corrida

O duplo buffer elimina a dependência entre células dentro de um mesmo passo. Todas as leituras
ocorrem em `estado_atual` e `tempo_atual`, e todas as escritas em `proximo_estado` e
`proximo_tempo`, sempre na posição da própria célula. Nenhuma célula é escrita por mais de uma
thread.

As barreiras implícitas garantem a ordem entre as fases:

- ao final do `omp for` de ativação das zonas, nenhuma thread inicia a atualização enquanto
  outra ainda modifica `estado_atual`;
- ao final do `omp for` de atualização, as reduções já estão consolidadas quando o `single`
  seguinte é executado;
- ao final do `single`, a troca dos ponteiros e o novo valor de `continuar` são visíveis a
  todas as threads antes do teste da condição do laço.

Não usamos `critical` nem `atomic` dentro do laço principal. Os contadores de novas ignições e
de células em chamas vêm de `reduction(+:...)`, e a parte serial (acumulação global, pico de
ignições, permuta de ponteiros e condição de parada) fica em `omp single`, cuja barreira
implícita dispensa qualquer sincronização explícita.

### 3.3 Vetorização

O laço de atualização tem dois níveis. O `omp for` distribui as linhas entre as threads e o
`omp simd` percorre as colunas dentro de cada linha, com reduções locais que são somadas aos
acumuladores da thread ao final da linha. Como o percurso por colunas é contíguo em memória,
ele favorece tanto a vetorização quanto a localidade de cache.

### 3.4 Condição de parada

A condição de parada reaproveita o contador `chamas_proximo`, que já vem da redução feita
durante o cálculo do próximo estado. Assim não é preciso varrer a matriz uma segunda vez só
para saber se ainda existe fogo.

Antes de criar a região paralela há uma verificação adicional: se não houver nenhuma célula em
chamas após a aplicação dos focos iniciais, nenhum passo é executado, conforme a seção 9 do
enunciado.

### 3.5 Escalonamento

Ambos os `omp for` usam `schedule(runtime)`, permitindo comparar políticas de escalonamento por
variável de ambiente sem recompilar:

```
OMP_SCHEDULE="dynamic,64" ./fire_omp entrada.txt
```

Quando `OMP_SCHEDULE` não é definida, o programa fixa `static` como padrão via
`omp_set_schedule()`. A justificativa dessa escolha está na seção 7.2.

---

## 4. Validação

A especificação exige que as versões sequencial e paralela produzam valores idênticos em todos
os campos, exceto o tempo. Foram verificados três níveis de equivalência.

**4.1 Sequencial contra paralela.** Nas três cargas fornecidas as duas versões produzem saída
idêntica, incluindo o `checksum`:

| Carga | Dimensões | checksum |
|-------|-----------|----------|
| pequena | 400 × 500 | 17073017104646724864 |
| média | 1200 × 1500 | 17187382406529706254 |
| grande | 2500 × 2500 | 14825089465781038537 |

**4.2 Independência do número de threads e do escalonamento.** A versão paralela foi executada
com 1, 2, 4, 8 e 16 threads, sob os escalonamentos `static`, `dynamic,64` e `guided`. Todas as
combinações produziram saída idêntica, o que confirma que não há condição de corrida nem
dependência da ordem de execução.

**4.3 Casos de validação de entrada.** O diretório `tests/` contém casos que exercitam as
restrições da seção 5: foco fora da matriz, zona fora da matriz, passo de ativação inválido,
zonas sobrepostas e estouro dos parâmetros da primeira linha. O alvo `make test` executa as duas
versões sobre todas as entradas e compara as saídas ignorando a linha de tempo.

---

## 5. Ambiente experimental

| Item | Configuração |
|------|--------------|
| Processador | AMD Ryzen 7 5700X |
| Núcleos | 8 físicos / 16 lógicos (SMT2) |
| Cache L3 | 32 MiB |
| Nós NUMA | 1 |
| Memória | 31 GiB |
| Sistema | Ubuntu 24.04.4 LTS, kernel 7.0.0-31 |
| Compilador | gcc 13.3.0 |
| Flags | `-std=c99 -fopenmp -O2` (idênticas nas duas versões) |
| Medição | `omp_get_wtime()` sobre o núcleo da simulação |
| Metodologia | 5 execuções por configuração; valor reportado é a mediana |

A distinção entre 8 núcleos físicos e 16 threads lógicas importa para a leitura dos resultados.
Acima de 8 threads não há recursos de execução novos, apenas compartilhamento SMT dos mesmos
núcleos.

---

## 6. Resultados

Todas as medições seguem a metodologia da seção 5. Os dados brutos estão em `bench/raw.csv` e
as tabelas completas em `bench/resultados.md`, reprodutíveis com `bash bench/run.sh`.

### 6.1 Determinismo

A equivalência entre as versões foi verificada sobre as 255 execuções da bateria, cobrindo
1, 2, 4, 8 e 16 threads e os escalonamentos `static`, `dynamic,64`, `guided` e `dynamic,1`:

| Carga | Execucoes | Checksums distintos |
|---|---|---|
| pequena | 80 | 1 |
| media | 80 | 1 |
| grande | 95 | 1 |

Ter um único checksum por carga em todas as execuções confirma que o resultado não depende do
número de threads nem da política de escalonamento.

### 6.2 Tempos e speedup

Baseline sequencial (mediana de 5 execuções): pequena 0,1896 s; média 2,1593 s; grande 7,5232 s.

**Carga grande (2500 × 2500), sequencial = 7,5232 s**

| T | static (s) | speedup | guided (s) | speedup | dynamic,64 (s) | speedup |
|---|---|---|---|---|---|---|
| 1 | 7,3788 | 1,02 | 7,3744 | 1,02 | 7,4772 | 1,01 |
| 2 | 3,7042 | 2,03 | 3,6986 | 2,03 | 3,9409 | 1,91 |
| 4 | 1,8945 | 3,97 | 1,8898 | 3,98 | 2,1018 | 3,58 |
| 8 | 1,1277 | 6,67 | 1,1190 | 6,72 | 1,2556 | 5,99 |
| 16 | 1,0762 | 6,99 | 1,0812 | 6,96 | 1,3291 | 5,66 |

**Carga média (1200 × 1500), sequencial = 2,1593 s**

| T | static (s) | speedup | guided (s) | speedup | dynamic,64 (s) | speedup |
|---|---|---|---|---|---|---|
| 1 | 2,1244 | 1,02 | 2,1172 | 1,02 | 2,1440 | 1,01 |
| 2 | 1,0621 | 2,03 | 1,0614 | 2,03 | 1,1458 | 1,88 |
| 4 | 0,5415 | 3,99 | 0,5423 | 3,98 | 0,6060 | 3,56 |
| 8 | 0,3145 | 6,87 | 0,3129 | 6,90 | 0,4080 | 5,29 |
| 16 | 0,3033 | 7,12 | 0,3045 | 7,09 | 0,4576 | 4,72 |

![Speedup por número de threads](bench/speedup.png)

Figura 1. Speedup em função do número de threads. A linha tracejada cinza marca o speedup
linear ideal e a pontilhada vertical marca o limite de 8 núcleos físicos. As curvas de `static`
e `guided` são praticamente coincidentes, por isso `guided` aparece como faixa larga sob a
linha de `static`.

A carga pequena (400 × 500) foi medida, mas ficou de fora da análise de escalabilidade. Seu
tempo sequencial é de 0,19 s, de modo que as medições com 8 e 16 threads caem na faixa de
0,03 s, onde a criação da equipe de threads e o ruído do sistema pesam mais que o trabalho útil.
O coeficiente de variação chegou a 69% nessa carga, contra menos de 5% na carga grande. Os dados
continuam disponíveis em `bench/raw.csv`.

### 6.3 Eficiência

| T | grande, static | grande, guided | grande, dynamic,64 |
|---|---|---|---|
| 1 | 102,0% | 102,0% | 100,6% |
| 2 | 101,5% | 101,7% | 95,5% |
| 4 | 99,3% | 99,5% | 89,5% |
| 8 | 83,4% | 84,0% | 74,9% |
| 16 | 43,7% | 43,5% | 35,4% |

![Eficiência por número de threads](bench/eficiencia.png)

Figura 2. Eficiência em função do número de threads.

A eficiência em 16 threads é calculada sobre 16, mas a máquina tem 8 núcleos físicos com SMT2.
Tomando os núcleos físicos como referência, a eficiência em T = 16 é de 6,99 / 8, ou seja,
87,4%. O programa aproveita 87% da capacidade física disponível, e não metade dela como a
tabela sugere à primeira vista.

### 6.4 Escalonamento `dynamic,1`, o padrão do libgomp

| T | tempo (s) | speedup | vs `static` |
|---|---|---|---|
| 1 | 9,2595 | 0,81 | 1,25× mais lento |
| 2 | 10,6666 | 0,71 | 2,88× mais lento |
| 4 | 9,4409 | 0,80 | 4,98× mais lento |
| 8 | 8,7057 | 0,86 | 7,72× mais lento |
| 16 | 8,8693 | 0,85 | 8,24× mais lento |

### 6.5 Impacto das flags de otimização

Carga grande, 8 threads, `static`, mediana de 3 execuções:

| CFLAGS | sequencial (s) | paralelo (s) | speedup |
|---|---|---|---|
| (nenhuma) | 24,889 | 3,480 | 7,15 |
| `-O2` | 7,545 | 1,152 | 6,55 |
| `-O3` | 5,738 | 0,912 | 6,29 |

---

## 7. Análise

### 7.1 Escalabilidade

O escalonamento é praticamente linear até 4 threads, com 99,3% de eficiência na carga grande.
Em 8 threads cai para 83% e a partir daí o speedup satura em torno de 7,0.

Esse teto vem da baixa intensidade aritmética do núcleo da simulação. Para atualizar uma célula
o programa lê até nove posições de `estado_atual` (a própria e os oito vizinhos de Moore), mais
`tempo_atual`, `cobertura` e `umidade`, e escreve em `proximo_estado` e `proximo_tempo`. Contra
esse volume de acessos há pouca conta a fazer: somas, comparações e uma divisão inteira. O
desempenho acaba limitado pela largura de banda de memória, e não pela capacidade de cálculo.
Como os 8 núcleos dividem o mesmo controlador de memória e os mesmos 32 MiB de L3, a banda
satura antes das unidades de execução.

A matriz da carga grande tem cerca de 6,25 milhões de células. Somando os dois buffers de estado,
os dois de tempo, a cobertura e a umidade, o conjunto de trabalho passa folgadamente do L3, o
que obriga tráfego contínuo com a memória principal a cada passo.

### 7.2 Efeito do SMT

Ao passar de 8 para 16 threads o comportamento depende do escalonamento:

| Carga | `static` | `guided` | `dynamic,64` |
|---|---|---|---|
| média | +3,6% | +2,7% | -12,2% |
| grande | +4,6% | +3,4% | -5,9% |

Com `static` e `guided` o SMT traz um ganho modesto. As duas threads lógicas de um mesmo núcleo
se revezam durante as esperas por memória e, como a carga é limitada por banda, esse
preenchimento de bolhas rende alguns pontos percentuais. O ganho é pequeno porque não existem
unidades de execução novas, apenas melhor aproveitamento das que já havia.

Com `dynamic,64` o efeito se inverte. Dobrar o número de threads dobra a disputa pelo contador
compartilhado que distribui os blocos, e o custo dessa sincronização supera o que se ganha
sobrepondo latência.

### 7.3 O custo do escalonamento dinâmico de granularidade unitária

O resultado mais chamativo do experimento é que `dynamic,1` deixa a versão paralela mais lenta
que a sequencial em todas as contagens de threads, com speedup entre 0,71 e 0,86. Esse é
justamente o escalonamento que o libgomp adota quando `OMP_SCHEDULE` não está definida.

Dois efeitos se somam aí. O primeiro é o custo por iteração. Com uma única thread, onde não
existe disputa possível, `dynamic,1` já é 25% mais lento que `static`, 9,26 s contra 7,38 s.
Esse custo vem da mecânica do escalonamento dinâmico, em que a thread consulta um contador
compartilhado a cada iteração para saber qual é o próximo índice, em vez de percorrer um
intervalo calculado de antemão.

O segundo é a contenção. O pior tempo absoluto aparece com 2 threads, 10,67 s e speedup 0,71,
pior até que com uma thread só. Daí em diante a disputa satura e o tempo se estabiliza entre
8,7 s e 9,4 s qualquer que seja o número de threads, que é o comportamento típico de um gargalo
serializado.

O laço de ativação das zonas amplifica o problema, porque percorre todas as `L × C` células a
cada passo. Na carga grande são 6,25 milhões de aquisições do contador por passo, ou 625 milhões
ao longo da simulação, para um corpo de laço que não faz mais que duas comparações.

Foi por isso que o programa passou a definir `static` como padrão através de
`omp_set_schedule()` quando `OMP_SCHEDULE` não está presente, mantendo a possibilidade de
trocar a política por variável de ambiente durante os experimentos.

### 7.4 Comparação entre escalonamentos

`static` e `guided` são equivalentes em todas as configurações medidas e superam `dynamic,64`
de forma consistente. Em 8 threads na carga grande os tempos são 1,128 s e 1,119 s contra
1,256 s.

A explicação está na regularidade da carga de trabalho. Toda célula executa o mesmo bloco de
atualização, e apenas as intactas avaliam a vizinhança de Moore, distribuídas de maneira
razoavelmente uniforme pela matriz. Sem desbalanceamento não há o que um escalonamento dinâmico
corrija, e seu custo fica sem contrapartida. A divisão em blocos contíguos do `static` já
equilibra o trabalho e ainda preserva a localidade de cache, porque cada thread percorre linhas
vizinhas.

O `guided` chega perto do `static` porque seus primeiros blocos são grandes, o que o aproxima de
uma divisão estática quando não existe desequilíbrio a corrigir.

### 7.5 Impacto das flags de otimização

Compilar sem otimização é o que produz o maior speedup, 7,15 contra 6,55 com `-O2`. O resultado
serve de alerta contra tratar o speedup como métrica suficiente. Sem otimização o baseline
sequencial fica artificialmente lento, e a paralelização recupera de graça uma folga que o
próprio compilador eliminaria. O tempo absoluto mostra o quadro real, já que a versão paralela
sem otimização leva 3,480 s, três vezes mais que os 1,152 s da versão com `-O2`.

Há ainda uma questão de correção. A especificação pede o uso de `simd`, e sem otimização o gcc
não vetoriza, o que faria a diretiva não ter efeito nenhum no binário entregue.

Ficamos com `-O2`. A medição mostra que `-O3` seria cerca de 24% mais rápido nas duas versões,
sem alterar os resultados, mas preferimos o nível mais conservador por ter comportamento mais
previsível entre versões de compilador.

### 7.6 Ameaças à validade

**Eficiência acima de 100% em uma thread.** Com T = 1 a versão paralela é sempre 1 a 2% mais
rápida que `fire_seq`, o que dá eficiência de 101% a 102%. Não se trata de speedup superlinear.
Os dois arquivos são implementações independentes do mesmo algoritmo, compiladas em separado, e
a diferença está na geração de código. Testamos se a diretiva `simd` era responsável, e não é:
desabilitando-a com `-fno-openmp-simd` o tempo fica em 7,41 s contra 7,44 s. O efeito é menor
que a diferença entre os escalonamentos analisados e não muda nenhuma conclusão.

**Carga pequena.** Conforme discutido em 6.2, a carga de 400 × 500 é curta demais para medir
escalabilidade e foi retirada da análise.

**Variabilidade residual.** A bateria rodou com a máquina recém-reiniciada e com o daemon de
antivírus suspenso. Na carga grande nenhuma configuração passou de 5% de coeficiente de
variação. O valor reportado é sempre a mediana de 5 execuções, e o `load average` de cada
medição ficou registrado em `bench/raw.csv` para auditoria.

---

## 8. Conclusão

Na carga de 2500 × 2500 a versão paralela chega a speedup de 6,99 com 16 threads sobre 8 núcleos
físicos, o que corresponde a 87% da capacidade física da máquina. O escalonamento é praticamente
linear até 4 threads e satura em seguida por limitação de largura de banda de memória,
comportamento esperado para um autômato celular com baixa intensidade aritmética.

A equivalência entre as versões foi verificada em 255 execuções, com um único checksum por
carga, o que confirma que o resultado não depende do número de threads nem da política de
escalonamento.

Entre os escalonamentos avaliados, `static` e `guided` ficaram equivalentes e acima de
`dynamic,64`, o que é coerente com a regularidade da carga de trabalho. O achado mais útil do
estudo, porém, foi negativo. O escalonamento `dynamic,1`, que o libgomp adota por padrão quando
`OMP_SCHEDULE` não está definida, deixa a versão paralela mais lenta que a sequencial. Neste
problema, a escolha da política de escalonamento pesou mais no desempenho final do que o número
de threads empregado.

---

## Apêndice: reprodução

```bash
make                 # compila fire_seq e fire_omp com -O2
make test            # valida seq × omp em todas as entradas
./fire_seq entrada_carga_grande.txt
./fire_omp entrada_carga_grande.txt
OMP_SCHEDULE="guided" ./fire_omp entrada_carga_grande.txt
```

O número de threads é lido do quarto campo da primeira linha do arquivo de entrada.
