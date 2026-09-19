#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <omp.h>

// cobertura definidos pela especificação
typedef enum
{
    AGUA = 0,
    SOLO_EXPOSTO = 1,
    VEG_RASTEIRA = 2,
    FLORESTA = 3
} Cobertura;

// estado possíveis para células, definidos pela especificação
typedef enum
{
    ESTADO_NAO_COMBUSTIVEL = 0,
    ESTADO_INTACTA = 1,
    ESTADO_EM_CHAMAS = 2,
    ESTADO_QUEIMADA = 3,
    ESTADO_CONTENCAO = 4
} Estado;

// Configuração geral da simulação
typedef struct
{
    int L;             // número de linhas da matriz
    int C;             // número de colunas da matriz
    int P;             // número máximo de passos
    int T;             // número de threads da versão OpenMP
    unsigned int seed; // semente usada por rand_r
    int LIMIAR;        // potencial mínimo para uma nova ignição
    int vento_linha;   // componente vertical da direção do vento
    int vento_coluna;  // componente horizontal da direção do vento
    int V;             // intensidade do vento
} Configuracao;

// Posição de um foco inicial
typedef struct
{
    int linha;
    int coluna;
} Foco;

// Zona retangular de contenção
typedef struct
{
    int passo_ativacao;
    int linha_inicial;
    int coluna_inicial;
    int linha_final;
    int coluna_final;
} Zona;

// Dados da floresta
// As matrizes são armazenadas linearmente:
// index = linha * C + coluna
typedef struct
{
    int *cobertura;      // cobertura de cada célula
    int *umidade;        // umidade de cada célula
    int *ativacao;       // -1 ou passo de ativação da zona
    int *estado_atual;   // estado no passo atual
    int *proximo_estado; // estado calculado para o próximo passo
    int *tempo_atual;    // tempo de queima no passo atual
    int *proximo_tempo;  // tempo de queima calculado para o próximo passo
} Floresta;

// Resultados calculados durante/depois da simulação.
typedef struct
{
    long long passos;
    long long nao_combustiveis;
    long long intact;
    long long em_chamas;
    long long queimadas;
    long long contencao;
    long long total_ignicoes;
    long long passo_pico;
    long long quantidade_pico;
    double percentual_queimado;
    double percentual_protegido;
    unsigned long long checksum;
    double tempo;
} Resultados;

// Fatores de combustível:
// água = 0, solo exposto = 0, vegetação rasteira = 8, floresta = 12.
const int fator_combustivel[4] = {0, 0, 8, 12};

// Os 8 vizinhos de Moore.
// Cada k representa um deslocamento (dr[k], dc[k]).
const int dr[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
const int dc[8] = {-1, 0, 1, -1, 1, -1, 0, 1};

// Converte [linha][coluna] em um índice da matriz linear.
long long indice(const Configuracao *configuracao, int linha, int coluna)
{
    return (long long)linha * configuracao->C + coluna;
}

int ler_configuracao(FILE *arquivo, Configuracao *configuracao)
{
    if (fscanf(arquivo, "%d %d %d %d %u %d",
               &configuracao->L, &configuracao->C, &configuracao->P, &configuracao->T,
               &configuracao->seed, &configuracao->LIMIAR) != 6)
        return 0;

    if (configuracao->L <= 0 || configuracao->C <= 0 || configuracao->P < 0 ||
        configuracao->T <= 0 || configuracao->LIMIAR <= 0)
        return 0;

    return 1;
}

int ler_vento(FILE *arquivo, Configuracao *configuracao)
{
    if (fscanf(arquivo, "%d %d %d",
               &configuracao->vento_linha, &configuracao->vento_coluna, &configuracao->V) != 3)
        return 0;

    if (configuracao->vento_linha < -1 || configuracao->vento_linha > 1 ||
        configuracao->vento_coluna < -1 || configuracao->vento_coluna > 1 ||
        (configuracao->vento_linha == 0 && configuracao->vento_coluna == 0) ||
        configuracao->V < 0 || configuracao->V > 5)
        return 0;

    return 1;
}

void liberar_floresta(Floresta *floresta)
{
    free(floresta->cobertura);
    free(floresta->umidade);
    free(floresta->ativacao);
    free(floresta->estado_atual);
    free(floresta->proximo_estado);
    free(floresta->tempo_atual);
    free(floresta->proximo_tempo);

    memset(floresta, 0, sizeof(*floresta));
}

int alocar_floresta(const Configuracao *configuracao, Floresta *floresta)
{
    long long total_celulas = (long long)configuracao->L * configuracao->C;

    floresta->cobertura = malloc((size_t)total_celulas * sizeof(int));
    floresta->umidade = malloc((size_t)total_celulas * sizeof(int));
    floresta->ativacao = malloc((size_t)total_celulas * sizeof(int));

    floresta->estado_atual = malloc((size_t)total_celulas * sizeof(int));
    floresta->proximo_estado = malloc((size_t)total_celulas * sizeof(int));
    floresta->tempo_atual = malloc((size_t)total_celulas * sizeof(int));
    floresta->proximo_tempo = malloc((size_t)total_celulas * sizeof(int));

    if (floresta->cobertura == NULL ||
        floresta->umidade == NULL ||
        floresta->ativacao == NULL ||
        floresta->estado_atual == NULL ||
        floresta->proximo_estado == NULL ||
        floresta->tempo_atual == NULL ||
        floresta->proximo_tempo == NULL)
    {
        liberar_floresta(floresta);
        return 0;
    }

    return 1;
}

int ler_focos_e_zonas(FILE *arquivo, const Configuracao *configuracao, Foco **focos, int *F, Zona **zonas, int *Z)
{
    if (fscanf(arquivo, "%d %d", F, Z) != 2)
        return 0;

    if (*F < 0 || *Z < 0)
        return 0;

    if (*F > 0)
    {
        *focos = malloc((size_t)*F * sizeof(Foco));
        if (*focos == NULL)
            return 0;
    }

    if (*Z > 0)
    {
        *zonas = malloc((size_t)*Z * sizeof(Zona));
        if (*zonas == NULL)
        {
            free(*focos);
            *focos = NULL;
            return 0;
        }
    }

    for (int i = 0; i < *F; i++)
    {
        if (fscanf(arquivo, "%d %d", &(*focos)[i].linha, &(*focos)[i].coluna) != 2)
            return 0;

        if ((*focos)[i].linha < 0 || (*focos)[i].linha >= configuracao->L ||
            (*focos)[i].coluna < 0 || (*focos)[i].coluna >= configuracao->C)
            return 0;

        // Focos repetidos são inválidos
        for (int j = 0; j < i; j++)
        {
            if ((*focos)[j].linha == (*focos)[i].linha &&
                (*focos)[j].coluna == (*focos)[i].coluna)
                return 0;
        }
    }

    for (int i = 0; i < *Z; i++)
    {
        if (fscanf(arquivo, "%d %d %d %d %d",
            &(*zonas)[i].passo_ativacao,
            &(*zonas)[i].linha_inicial,
            &(*zonas)[i].coluna_inicial,
            &(*zonas)[i].linha_final,
            &(*zonas)[i].coluna_final) != 5)
        return 0;

        if ((*zonas)[i].passo_ativacao < 0 ||
            (*zonas)[i].passo_ativacao >= configuracao->P)
            return 0;

        if ((*zonas)[i].linha_inicial < 0 ||
            (*zonas)[i].linha_inicial >= configuracao->L ||
            (*zonas)[i].linha_final < 0 ||
            (*zonas)[i].linha_final >= configuracao->L ||
            (*zonas)[i].coluna_inicial < 0 ||
            (*zonas)[i].coluna_inicial >= configuracao->C ||
            (*zonas)[i].coluna_final < 0 ||
            (*zonas)[i].coluna_final >= configuracao->C)
            return 0;

        if ((*zonas)[i].linha_inicial > (*zonas)[i].linha_final ||
            (*zonas)[i].coluna_inicial > (*zonas)[i].coluna_final)
            return 0;
    }

    return 1;
}

int validar_focos_combustiveis(const Configuracao *configuracao,
                               const Floresta *floresta,
                               const Foco *focos, int F)
{
    for (int i = 0; i < F; i++)
    {
        long long indice_celula = indice(configuracao, focos[i].linha, focos[i].coluna);

        if (floresta->cobertura[indice_celula] == AGUA ||
            floresta->cobertura[indice_celula] == SOLO_EXPOSTO)
            return 0;
    }

    return 1;
}

void gerar_cobertura_umidade(Configuracao *configuracao, Floresta *floresta)
{
    long long total_celulas = (long long)configuracao->L * configuracao->C;

    // A geração precisa ser sequencial e seguir a ordem linear linha/coluna
    // Para cada célula: cobertura primeiro, umidade imediatamente depois
    for (long long i = 0; i < total_celulas; i++)
    {
        int valor = rand_r(&configuracao->seed) % 100;

        if (valor < 10)
            floresta->cobertura[i] = AGUA;
        else if (valor < 20)
            floresta->cobertura[i] = SOLO_EXPOSTO;
        else if (valor < 55)
            floresta->cobertura[i] = VEG_RASTEIRA;
        else
            floresta->cobertura[i] = FLORESTA;

        floresta->umidade[i] = rand_r(&configuracao->seed) % 101;
    }
}

void inicializar_estados(const Configuracao *configuracao, Floresta *floresta)
{
    long long total_celulas = (long long)configuracao->L * configuracao->C;

    for (long long i = 0; i < total_celulas; i++)
    {
        if (floresta->cobertura[i] == AGUA ||
            floresta->cobertura[i] == SOLO_EXPOSTO)
            floresta->estado_atual[i] = ESTADO_NAO_COMBUSTIVEL;
        else
            floresta->estado_atual[i] = ESTADO_INTACTA;

        floresta->tempo_atual[i] = 0;
        floresta->proximo_estado[i] = ESTADO_NAO_COMBUSTIVEL;
        floresta->proximo_tempo[i] = 0;
    }
}

int aplicar_focos(const Configuracao *configuracao, Floresta *floresta,
                  const Foco *focos, int F)
{
    if (!validar_focos_combustiveis(configuracao, floresta, focos, F))
        return 0;

    for (int i = 0; i < F; i++)
    {
        long long indice_celula = indice(configuracao, focos[i].linha, focos[i].coluna);

        floresta->estado_atual[indice_celula] = ESTADO_EM_CHAMAS;

        if (floresta->cobertura[indice_celula] == VEG_RASTEIRA)
            floresta->tempo_atual[indice_celula] = 2;
        else
            floresta->tempo_atual[indice_celula] = 4;
    }

    return 1;
}

void construir_mapa_ativacao(const Configuracao *configuracao,
                             Floresta *floresta,
                             const Zona *zonas, int Z)
{
    long long total_celulas = (long long)configuracao->L * configuracao->C;

    for (long long i = 0; i < total_celulas; i++)
        floresta->ativacao[i] = -1;

    for (int z = 0; z < Z; z++)
    {
        for (int linha = zonas[z].linha_inicial; linha <= zonas[z].linha_final; linha++)
        {
            for (int coluna = zonas[z].coluna_inicial;
                 coluna <= zonas[z].coluna_final; coluna++)
            {
                long long indice_celula = indice(configuracao, linha, coluna);

                // Em zonas sobrepostas, guarda o menor passo_ativacao
                if (floresta->ativacao[indice_celula] == -1 ||
                    zonas[z].passo_ativacao < floresta->ativacao[indice_celula])
                    floresta->ativacao[indice_celula] = zonas[z].passo_ativacao;
            }
        }
    }
}

int calcular_potencial_ignicao(int linha, int coluna, const Configuracao *configuracao, const Floresta *floresta)
{
    int S = 0;

    // Somamos apenas as contribuições dos 8 vizinhos de Moore que estão ESTADO_EM_CHAMAS no estado_atual
    for (int k = 0; k < 8; k++)
    {
        int linha_vizinho = linha + dr[k];
        int coluna_vizinho = coluna + dc[k];

        if (linha_vizinho < 0 || linha_vizinho >= configuracao->L ||
            coluna_vizinho < 0 || coluna_vizinho >= configuracao->C)
            continue;

        long long indice_vizinho = indice(configuracao, linha_vizinho, coluna_vizinho);

        if (floresta->estado_atual[indice_vizinho] != ESTADO_EM_CHAMAS)
            continue;

        int prop_linha = linha - linha_vizinho;
        int prop_coluna = coluna - coluna_vizinho;

        int abs_prop_linha = prop_linha < 0 ? -prop_linha : prop_linha;
        int abs_prop_coluna = prop_coluna < 0 ? -prop_coluna : prop_coluna;

        // Ortogonal = 10; diagonal = 7.
        int P_basico = (abs_prop_linha + abs_prop_coluna == 1) ? 10 : 7;

        // Alinhamento entre a direção de propagação e o vento.
        int A = prop_linha * configuracao->vento_linha + prop_coluna * configuracao->vento_coluna;
        int P_v = P_basico + configuracao->V * A;

        if (P_v < 1)
            P_v = 1;

        S += P_v;
    }

    long long indice_celula = indice(configuracao, linha, coluna);
    int I = (S * fator_combustivel[floresta->cobertura[indice_celula]] * (100 - floresta->umidade[indice_celula])) / 100;

    return I;
}

void simular(const Configuracao *configuracao, Floresta *floresta, Resultados *resultado)
{
    const long long total_celulas = (long long)configuracao->L * configuracao->C;

    resultado->passos = 0;
    resultado->total_ignicoes = 0;
    resultado->passo_pico = -1;
    resultado->quantidade_pico = 0;

    // Antes de criar a região paralela, verificamos a condição especial
    // se não houver fogo após a aplicação dos focos iniciais, nenhum passo deve ser executado

    int fogo_existe = 0;

    for (long long i = 0; i < total_celulas; i++)
    {
        if (floresta->estado_atual[i] == ESTADO_EM_CHAMAS)
        {
            fogo_existe = 1;
            break;
        }
    }

    if (!fogo_existe)
        return;

    // Uma única região paralela engloba todos os passos da simulação.
    // - evita criar/destruir uma equipe de threads a cada passo;
    // - mantém a sincronização necessária entre um passo e o seguinte;
    // - atende à exigência de uma região paralela persistente.
    // O número de threads é exatamente T, conforme a entrada.

    int continuar = 1;
    long long novas_ignicoes = 0;
    long long chamas_proximo = 0;

#pragma omp parallel num_threads(configuracao->T) default(none) \
    shared(configuracao, floresta, resultado, total_celulas, continuar, novas_ignicoes, chamas_proximo)
    {
        for (int p = 0; p < configuracao->P && continuar; p++)
        {

            // PASSO 1 — ativação das zonas

            // Cada célula é tratada independentemente: a escrita ocorre
            //  somente na própria posição de estado_atual
            //  não existe duas threads escrevendo a mesma célula
            //  a  barreira implícita do omp for garante que nenhuma thread
            //  pode iniciar a atualização enquanto outra ainda estiver alterando estado_atual pela ativação das zonas.

#pragma omp for schedule(runtime)
            for (long long i = 0; i < total_celulas; i++)
            {
                if (floresta->ativacao[i] == p &&
                    floresta->estado_atual[i] == ESTADO_INTACTA)
                {
                    floresta->estado_atual[i] = ESTADO_CONTENCAO;
                }
            }

/*
 * PASSO 2 — calcular  o próximo estado.
    A atualização é feita a partir de estado_atual/tempo_atual
    e escrita em proximo_estado/proximo_tempo. 
 
 Usamos omp for simd porque:
   - omp for distribui as células entre as threads;
   - simd permite ao compilador tentar vetorizar o trabalho dentro dos blocos atribuídos a cada thread


  As variáveis de redução ficam fora da região lexical do
  omp for. Elas são compartilhadas antes da redução e recebem
  o resultado combinado pelo OpenMP ao final do for.
 

  Zeramos os acumuladores uma única vez por passo. O single
  possui barreira implícito, portanto nenhuma thread inicia o
  omp for antes de os valores estarem em zero.
 */
#pragma omp single
            {
                novas_ignicoes = 0;
                chamas_proximo = 0;
            }

/*
 * omp for distribui as linhas entre as threads. Dentro de cada
 * linha, omp simd permite a vetorização do percurso das colunas.
 *
 * A redução do omp for combina o resultado produzido por cada
 * thread. Dentro de cada linha, novas_ignicoes_linha e
 * chamas_linha são reduzidas pelo simd antes de serem somadas
 * ao acumulador privado da thread.
 */
#pragma omp for schedule(runtime) reduction(+ : novas_ignicoes, chamas_proximo)
            for (int linha = 0; linha < configuracao->L; linha++)
            {
                long long novas_ignicoes_linha = 0;
                long long chamas_linha = 0;

#pragma omp simd reduction(+ : novas_ignicoes_linha, chamas_linha)
                for (int coluna = 0; coluna < configuracao->C; coluna++)
                {
                    long long i = (long long)linha * configuracao->C + coluna;
                    int estado = floresta->estado_atual[i];

                    //Por padrão, preservamos estado e tempo atuais 
                    floresta->proximo_estado[i] = estado;
                    floresta->proximo_tempo[i] = floresta->tempo_atual[i];

                    if (estado == ESTADO_NAO_COMBUSTIVEL)
                    {
                        floresta->proximo_tempo[i] = 0;
                    }
                    else if (estado == ESTADO_INTACTA)
                    {
                        int I = calcular_potencial_ignicao(
                            linha, coluna, configuracao, floresta);

                        if (I >= configuracao->LIMIAR)
                        {
                            floresta->proximo_estado[i] = ESTADO_EM_CHAMAS;
                            novas_ignicoes_linha++;

                            if (floresta->cobertura[i] == VEG_RASTEIRA)
                                floresta->proximo_tempo[i] = 2;
                            else
                                floresta->proximo_tempo[i] = 4;
                        }
                        else
                        {
                            floresta->proximo_estado[i] = ESTADO_INTACTA;
                            floresta->proximo_tempo[i] = 0;
                        }
                    }
                    else if (estado == ESTADO_EM_CHAMAS)
                    {
                        int novo_tempo = floresta->tempo_atual[i] - 1;

                        if (novo_tempo == 0)
                        {
                            floresta->proximo_estado[i] = ESTADO_QUEIMADA;
                            floresta->proximo_tempo[i] = 0;
                        }
                        else
                        {
                            floresta->proximo_estado[i] = ESTADO_EM_CHAMAS;
                            floresta->proximo_tempo[i] = novo_tempo;
                        }
                    }
                    else if (estado == ESTADO_QUEIMADA)
                    {
                        floresta->proximo_estado[i] = ESTADO_QUEIMADA;
                        floresta->proximo_tempo[i] = 0;
                    }
                    else if (estado == ESTADO_CONTENCAO)
                    {
                        floresta->proximo_estado[i] = ESTADO_CONTENCAO;
                        floresta->proximo_tempo[i] = 0;
                    }

                    if (floresta->proximo_estado[i] == ESTADO_EM_CHAMAS)
                        chamas_linha++;
                }

                novas_ignicoes += novas_ignicoes_linha;
                chamas_proximo += chamas_linha;
            }

/*
 PASSOS 3, 4 e 5 — estatísticas, troca dos buffers e parada.
 
  O omp for simd acima possui barreira implícita. 
  quando chegamos ao single, as reduções já foram concluídas.
 
  Apenas uma thread altera os ponteiros e as estatísticas
  globais. As outras esperam na barreira implícita do single.
  Isso evita race condition sem usar critical/atomic.
 */
#pragma omp single
            {
                // PASSO 3 — estatísticas das novas ignições
                resultado->total_ignicoes += novas_ignicoes;

                if (novas_ignicoes > resultado->quantidade_pico)
                {
                    resultado->quantidade_pico = novas_ignicoes;
                    resultado->passo_pico = p;
                }

                /*
                  PASSO 4 — troca segura dos buffers
                  Não copiamos as matrizes. Apenas trocamos os ponteiros
                   evitando uma cópia de L*C elementos a cada
                  passo.
                 */
                int *temp_state = floresta->estado_atual;
                floresta->estado_atual = floresta->proximo_estado;
                floresta->proximo_estado = temp_state;

                int *temp_time = floresta->tempo_atual;
                floresta->tempo_atual = floresta->proximo_tempo;
                floresta->proximo_tempo = temp_time;

                resultado->passos++;

                /*
                  PASSO 5 — condição de parada
                  chamas_proximo foi obtido por reduction enquanto a
                  próxima matriz era calculada. Portanto, não precisamos
                  fazer uma segunda varredura da matriz apenas para saber
                  se ainda existe fogo.
                 */
                continuar = (chamas_proximo > 0);
            }
           
        }
    }
}

void calcular_resultados(const Configuracao *configuracao,const Floresta *floresta,Resultados *resultado)
{
    long long total_celulas = (long long)configuracao->L * configuracao->C;
    long long combustiveis_iniciais = 0;

    resultado->nao_combustiveis = 0;
    resultado->intact = 0;
    resultado->em_chamas = 0;
    resultado->queimadas = 0;
    resultado->contencao = 0;

    for (long long i = 0; i < total_celulas; i++)
    {
        switch (floresta->estado_atual[i])
        {
        case ESTADO_NAO_COMBUSTIVEL:
            resultado->nao_combustiveis++;
            break;
        case ESTADO_INTACTA:
            resultado->intact++;
            break;
        case ESTADO_EM_CHAMAS:
            resultado->em_chamas++;
            break;
        case ESTADO_QUEIMADA:
            resultado->queimadas++;
            break;
        case ESTADO_CONTENCAO:
            resultado->contencao++;
            break;
        }

        if (floresta->cobertura[i] == VEG_RASTEIRA ||
            floresta->cobertura[i] == FLORESTA)
            combustiveis_iniciais++;
    }

    if (combustiveis_iniciais == 0)
    {
        resultado->percentual_queimado = 0.0;
        resultado->percentual_protegido = 0.0;
    }
    else
    {
        resultado->percentual_queimado =
            100.0 * (resultado->queimadas + resultado->em_chamas) /
            combustiveis_iniciais;

        resultado->percentual_protegido = 100.0 * resultado->contencao /combustiveis_iniciais;
    }
}

unsigned long long calcular_checksum(const Configuracao *configuracao, const Floresta *floresta)
{
    long long total_celulas = (long long)configuracao->L * configuracao->C;
    unsigned long long checksum = 0;

    // O checksum é sequencial e segue a ordem linear da matriz.
    for (long long i = 0; i < total_celulas; i++)
    {
        checksum = checksum * 31ULL + (unsigned long long)floresta->estado_atual[i];
        checksum = checksum * 31ULL + (unsigned long long)floresta->tempo_atual[i];
    }

    return checksum;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usa: ./fire_omp entrada_carga_pequena.txt\n");
        return 1;
    }

    FILE *arquivo = fopen(argv[1], "r");

    if (arquivo == NULL)
    {
        perror("Erro ao abrir arquivo de entrada");
        return 1;
    }

    Configuracao configuracao = {0};
    Floresta floresta = {0};
    Resultados resultado = {0};

    Foco *focos = NULL;
    Zona *zonas = NULL;
    int F = 0; // numero de foco inicias de incendio
    int Z = 0; // qtd zona de contencao

    if (!ler_configuracao(arquivo, &configuracao) ||
        !ler_vento(arquivo, &configuracao) ||
        !ler_focos_e_zonas(arquivo, &configuracao, &focos, &F, &zonas, &Z))
    {
        fprintf(stderr, "Entrada inválida.\n");
        free(focos);
        free(zonas);
        fclose(arquivo);
        return 1;
    }

    if (!alocar_floresta(&configuracao, &floresta))
    {
        fprintf(stderr, "Erro ao alocar memória.\n");
        free(focos);
        free(zonas);
        fclose(arquivo);
        return 1;
    }

    // Tudo abaixo acontece antes do trecho cronometrado.
    gerar_cobertura_umidade(&configuracao, &floresta);
    inicializar_estados(&configuracao, &floresta);

    if (!aplicar_focos(&configuracao, &floresta, focos, F))
    {
        fprintf(stderr, "Foco inicial sobre célula não combustível.\n");
        liberar_floresta(&floresta);
        free(focos);
        free(zonas);
        fclose(arquivo);
        return 1;
    }

    construir_mapa_ativacao(&configuracao, &floresta, zonas, Z);

    // Somente a execução da simulação entra no tempo medido.
    double inicio = omp_get_wtime();
    simular(&configuracao, &floresta, &resultado);
    double fim = omp_get_wtime();

    resultado.tempo = fim - inicio;

    // Resultados finais, percentuais e checksum ficam fora do tempo.
    calcular_resultados(&configuracao, &floresta, &resultado);
    resultado.checksum = calcular_checksum(&configuracao, &floresta);

    printf("passos: %lld\n", resultado.passos);
    printf("nao_combustiveis: %lld\n", resultado.nao_combustiveis);
    printf("intactas: %lld\n", resultado.intact);
    printf("em_chamas: %lld\n", resultado.em_chamas);
    printf("queimadas: %lld\n", resultado.queimadas);
    printf("contencao: %lld\n", resultado.contencao);
    printf("total_ignicoes: %lld\n", resultado.total_ignicoes);
    printf("pico_ignicoes: %lld %lld\n",
           resultado.passo_pico, resultado.quantidade_pico);
    printf("percentual_queimado: %.2f\n", resultado.percentual_queimado);
    printf("percentual_protegido: %.2f\n", resultado.percentual_protegido);
    printf("checksum: %llu\n", resultado.checksum);
    printf("tempo: %.6f\n", resultado.tempo);

    liberar_floresta(&floresta);
    free(focos);
    free(zonas);
    fclose(arquivo);

    return 0;
}
