#include <inttypes.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    AGUA = 0,
    SOLO_EXPOSTO = 1,
    VEGETACAO_RASTEIRA = 2,
    FLORESTA = 3
} cobertura_t;

typedef enum {
    NAO_COMBUSTIVEL = 0,
    INTACTA = 1,
    EM_CHAMAS = 2,
    QUEIMADA = 3,
    CONTENCAO = 4
} estado_t;

typedef struct {
    cobertura_t cobertura;
    uint8_t umidade;
    // estado_t estado;
} celula_t;

typedef struct {
    int32_t x;
    int32_t y;
} pos_t;

// Em zona_t, topo e fundo representam duas extremidades da zona para
// determina-la.
// 
// Exemplo. na zona
// x--------+
// |--------|
// +--------x
//
// Os pontos x determinam a zona. A invariante que deve-se ter eh que
// as coordenadas de topo devem ser menores do que a do fundo
typedef struct {
    int32_t passo_ativacao;
    pos_t topo;
    pos_t fundo;
} zona_t;

// Ponteiros para memoria
pos_t *focos = NULL;
zona_t *zonas = NULL;
celula_t *dados_celulas = NULL;
int32_t *ativacao = NULL;
estado_t *estado_atual = NULL;
estado_t *proximo_estado = NULL;
int32_t *tempo_atual = NULL;
int32_t *proximo_tempo = NULL;

int32_t n_focos = 0;
int32_t n_zonas = 0;
int32_t linhas, colunas, max_passos, nthreads;
uint32_t rnd_semente;
int32_t limiar_ignicao;

int8_t vento_l, vento_c;
float vento_int;

// Fatores de combustível
const int32_t fator_comb[4] = {0, 0, 8, 12};
// vizinhos de Moore (8 vizinhos)
const int dl[8] = {-1, -1, -1,  0, 0,  1, 1, 1};

const int dc[8] = {-1,  0,  1, -1, 1, -1, 0, 1};

// Variaveis para os resultados
int64_t total_ignicoes = 0;
int32_t pico_ignicoes_passo = -1;
int64_t pico_ignicoes_quant = 0;
int32_t combustiveis_iniciais = 0;
int32_t contencao = 0;

int32_t passo;

uint64_t calc_checksum(void) {
    uint64_t checksum = 0;

    for (int64_t i = 0; i < linhas * colunas; i++) {
        checksum = checksum * UINT64_C(31) + (uint64_t)estado_atual[i];

        checksum = checksum * UINT64_C(31) + (uint64_t)tempo_atual[i];
    }

    return checksum;
}

int32_t alocar_memoria() {
    focos = (pos_t *)malloc((size_t)n_focos * sizeof(pos_t));
    zonas = (zona_t *)malloc((size_t)n_zonas * sizeof(zona_t));
    // NOTE: Lembre-se de posteriormente remover matriz_atual e prox
    int64_t total_celulas = linhas * colunas;
    dados_celulas = (celula_t *)malloc(total_celulas * sizeof(celula_t));
    ativacao = (int32_t *)malloc(total_celulas * sizeof(int32_t));
    estado_atual = (estado_t *)malloc(total_celulas * sizeof(estado_t));
    proximo_estado = (estado_t *)malloc(total_celulas * sizeof(estado_t));
    tempo_atual = (int32_t *)calloc((size_t)total_celulas, sizeof(int32_t));
    proximo_tempo = (int32_t *)calloc((size_t)total_celulas, sizeof(int32_t));

    if (focos == NULL || zonas == NULL || dados_celulas == NULL ||
        ativacao == NULL || estado_atual == NULL || proximo_estado == NULL ||
        tempo_atual == NULL || proximo_tempo == NULL) {
        return 1;
    }

    return 0;
}

void limpar_memoria() {
    if (focos) free(focos);
    if (zonas) free(zonas);
    if (dados_celulas) free(dados_celulas);
    if (ativacao) free(ativacao);
    if (estado_atual) free(estado_atual);
    if (proximo_estado) free(proximo_estado);
    if (tempo_atual) free(tempo_atual);
    if (proximo_tempo) free(proximo_tempo);
}

// [Nicholas] NOTE: Talvez seja válido simplificar a lógica...
int32_t ler_entradas(const char *filename) {
    FILE *input_ptr;
    if ((input_ptr = fopen(filename, "r")) == NULL) {
        perror("Abrindo arquivo de entrada");
        return 1;
    }

    char line_buff[100];
    int scn_res;

    // Leitura da primeira linha
    if (fgets(line_buff, 100, input_ptr) == NULL) {
        perror("Erro ao ler a linha 1");
        fclose(input_ptr);
        return 1;
    }

    // OBS: O parsing das entradas não detecta under/over flow das entradas
    scn_res = sscanf(
        line_buff,
        "%" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNu32 " %" SCNd32,
        &linhas, &colunas, &max_passos, &nthreads, &rnd_semente, &limiar_ignicao);
    
    // Se o número de entradas lidas for diferente de 6, então retornar erro.
    if (scn_res != 6) {
        fprintf(stderr, "[Linha 1] Entrada mal formada!\n");
        fclose(input_ptr);
        return 1;
    }

    if (linhas <= 0 || colunas <= 0 || max_passos < 0 || nthreads <= 0 ||
        limiar_ignicao <= 0) {
        fprintf(stderr, "Entradas inválidas (fora do domínio da aplicação)!\n");
        fclose(input_ptr);
        return 1;
    }

    // Leitura da segunda linha
    if (fgets(line_buff, 100, input_ptr) == NULL) {
        perror("Erro ao ler a linha 2");
        fclose(input_ptr);
        return 1;
    }

    scn_res = sscanf(line_buff, "%" SCNd8 " %" SCNd8 " %f", &vento_l, &vento_c,
                     &vento_int);

    if (scn_res != 3) {
        fprintf(stderr, "[Linha 2] Entrada mal formada!\n");
        fclose(input_ptr);
        return 1;
    }

    if (vento_l < -1 || vento_l > 1 || vento_c < -1 || vento_c > 1 || 
        (vento_l == 0 && vento_c == 0) || vento_int < 0 || vento_int > 5) {
        fprintf(stderr, "Entradas de vento inválidas (fora do domínio da "
                        "aplicação)!\n");
        fclose(input_ptr);
        return 1;
    }

    // Leitura da terceira linha
    if (fgets(line_buff, 100, input_ptr) == NULL) {
        perror("Erro ao ler a linha 3");
        fclose(input_ptr);
        return 1;
    }

    scn_res = sscanf(line_buff, "%" SCNd32 " %" SCNd32, &n_focos, &n_zonas);

    if (scn_res != 2) {
        fprintf(stderr, "[Linha 3] Entrada mal formada!\n");
        fclose(input_ptr);
        return 1;
    }

    if (n_focos < 0 || n_zonas < 0) {
        fprintf(stderr,
                "Entrada da quantidade de focos de incêndio ou de zonas "
                "de contenção negativas!\n");
        fclose(input_ptr);
        return 1;
    }

    // Alocacao de memoria (unificado)
    if (alocar_memoria() != 0) {
        perror("Erro ao alocar memória inicial");
        fclose(input_ptr);
        return 1;
    }

    for (int32_t i = 0; i < n_focos; i++) {
        if (fgets(line_buff, 100, input_ptr) == NULL) {
            perror("Erro ao ler entrada de foco de fogo");
            fclose(input_ptr);
            return 1;
        }

        scn_res = sscanf(line_buff, "%" SCNd32 " %" SCNd32, &focos[i].x,
                         &focos[i].y);

        if (scn_res != 2) {
            fprintf(stderr, "Entrada de foco de fogo mal formada!\n");
            fclose(input_ptr);

            return 1;
        }

        if (focos[i].x < 0 || focos[i].x >= linhas ||
            focos[i].y < 0 || focos[i].y >= colunas) {
            fprintf(stderr, "Foco %d fora da matriz!\n", i);
            fclose(input_ptr);

            return 1;
        }
    }

    for (int32_t i = 0; i < n_zonas; i++) {
        if (fgets(line_buff, 100, input_ptr) == NULL) {
            perror("Erro ao ler entrada de zonas");
            fclose(input_ptr);

            return 1;
        }

        scn_res =
            sscanf(line_buff,
                   "%" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32,
                   &zonas[i].passo_ativacao, &zonas[i].fundo.x, &zonas[i].fundo.y,
                   &zonas[i].topo.x, &zonas[i].topo.y);

        if (scn_res != 5) {
            fprintf(stderr, "Entrada de zona mal formada!\n");
            fclose(input_ptr);

            return 1;
        }

        if (zonas[i].passo_ativacao < 0 || zonas[i].passo_ativacao >= max_passos) {
            fprintf(stderr, "Zona %d com passo inválido!\n", i);
            fclose(input_ptr);
            return 1;
        }

        if (zonas[i].fundo.x < 0 || zonas[i].fundo.x >= linhas ||
            zonas[i].fundo.y < 0 || zonas[i].fundo.y >= colunas ||
            zonas[i].topo.x < 0 || zonas[i].topo.x >= linhas ||
            zonas[i].topo.y < 0 || zonas[i].topo.y >= colunas) {
            fprintf(stderr, "Zona %d fora da matriz!\n", i);
            fclose(input_ptr);
            return 1;
        }

        if (zonas[i].fundo.x > zonas[i].topo.x ||
            zonas[i].fundo.y > zonas[i].topo.y) {
            fprintf(stderr, "Zona %d com limites iniciais superiores aos finais!\n", i);
            fclose(input_ptr);
            return 1;
        }
    }

    if (fgetc(input_ptr) != EOF) {
        fprintf(
            stderr,
            "Arquivo não exaustado - formatado incorretamente!\nComportamento "
            "não esperado pode ocorrer.\n");
    } else {
        // printf("Arquivo lido com sucesso!\n");
    }

    fclose(input_ptr);
    return 0;
}

int32_t gerar_matriz() {
    // Geração sequencial da cobertura e umidade
    for (int32_t i = 0; i < linhas; i++) {
        for (int32_t j = 0; j < colunas; j++) {
            size_t idx = (size_t)i * (size_t)colunas + (size_t)j;

            // Geração da cobertura
            int val_cob = rand_r(&rnd_semente) % 100;
            if (val_cob <= 9) {
                dados_celulas[idx].cobertura = AGUA;
                estado_atual[idx] = NAO_COMBUSTIVEL;
            } else if (val_cob <= 19) {
                dados_celulas[idx].cobertura = SOLO_EXPOSTO;
                estado_atual[idx] = NAO_COMBUSTIVEL;
            } else if (val_cob <= 54) {
                dados_celulas[idx].cobertura = VEGETACAO_RASTEIRA;
                estado_atual[idx] = INTACTA;
                combustiveis_iniciais++;
            } else {
                dados_celulas[idx].cobertura = FLORESTA;
                estado_atual[idx] = INTACTA;
                combustiveis_iniciais++;
            }

            dados_celulas[idx].umidade = (uint8_t)(rand_r(&rnd_semente) % 101);
        }
    }

    // Aplicação e validação dos focos iniciais de incêndio
    for (int32_t f = 0; f < n_focos; f++) {
        size_t idx = (size_t)focos[f].x * (size_t)colunas + (size_t)focos[f].y;

        if (dados_celulas[idx].cobertura == AGUA || dados_celulas[idx].cobertura == SOLO_EXPOSTO) {
            fprintf(stderr, "Foco inicial em (%d, %d) posicionado sobre célula não combustível!\n",
                    focos[f].x, focos[f].y);
            return 1;
        } else if (estado_atual[idx] == EM_CHAMAS) {
            fprintf(stderr, "Foco inicial em (%d, %d) repetido!\n",
                    focos[f].x, focos[f].y);
            return 1;
        }

        estado_atual[idx] = EM_CHAMAS;

        if (dados_celulas[idx].cobertura == VEGETACAO_RASTEIRA) {
            tempo_atual[idx] = 2;
        } else if (dados_celulas[idx].cobertura == FLORESTA) {
            tempo_atual[idx] = 4;
        }
    }

    // printf("Matriz gerada com sucesso!\n");
    return 0;
}

int32_t mapa_de_contencao() {
    for (size_t i = 0; i < linhas * colunas; i++) {
        ativacao[i] = -1;
    }

    for (int32_t z = 0; z < n_zonas; z++) {
        int32_t passo = zonas[z].passo_ativacao;
        int32_t l_min = zonas[z].fundo.x;
        int32_t l_max = zonas[z].topo.x;
        int32_t c_min = zonas[z].fundo.y;
        int32_t c_max = zonas[z].topo.y;

        for (int32_t l = l_min; l <= l_max; l++) {
            for (int32_t c = c_min; c <= c_max; c++) {
                size_t idx = (size_t)l * (size_t)colunas + (size_t)c;

                if (ativacao[idx] == -1)
                    contencao++;
                if (ativacao[idx] == -1 || passo < ativacao[idx]) {
                    ativacao[idx] = passo;
                }
            }
        }
    }

    // printf("Mapa de contenção gerado com sucesso!\n");
    return 0;
}

void imprime_estado() {
    int64_t k = 0;

    for (int64_t idx = 0; idx < linhas * colunas; idx++) {
        char c;
        switch (estado_atual[idx]) {
            case NAO_COMBUSTIVEL:
                c = 'n';
                break;
            case INTACTA:
                c = 'i';
                break;
            case EM_CHAMAS:
                c = 'a';
                break;
            case QUEIMADA:
                c = 'q';
                break;
            case CONTENCAO:
                c = 'c';
                break;
        }

        if (++k < colunas) {
            printf("%c", c);
        } else {
            printf("%c\n", c);
            k = 0;
        }
    }
}

void simulacao() {
    // Verificar no comeco se temos focos iniciais para comeco de conversa
    // (ou seja, se n_focos > 0)
    if (n_focos <= 0) {
        return;
    }
    
    for (passo = 0; passo < max_passos; passo++) {
        // Passo 1: ativar as zonas programadas para p
        // WARNING: Estamos lendo e escrevendo estado_atual, tomar cuidado
        //
        // NOTE: Para o codigo sequencial, eu acho que a gente nem precisa
        // ativar as zonas, bastaria verificar o vetor de ativacao na hora
        // de espalhar o fogo
        for (size_t idx = 0; idx < linhas * colunas; idx++) {
            if (ativacao[idx] == passo && estado_atual[idx] == INTACTA) {
                estado_atual[idx] = CONTENCAO;
            }
        }

        // Passo 2: atualizar o estado das células
        int64_t novas_ignicoes = 0;

        for (int32_t l = 0; l < linhas; l++) {
            for (int32_t c = 0; c < colunas; c++) {
                size_t idx = l * colunas + c;

                switch (estado_atual[idx]) {
                    case NAO_COMBUSTIVEL:
                    case QUEIMADA:
                        proximo_estado[idx] = estado_atual[idx];
                        break;
                    case CONTENCAO:
                        // printf("Estou vendo conteção em linha %" PRId32 " e coluna %" PRId32 "\n", l+1, c+1);
                        proximo_estado[idx] = CONTENCAO;
                        break;
                    case INTACTA: {
                        // Cálculo do potencial de ignição
                        int32_t S = 0; // Soma

                        // Por cada vizinho...
                        for (int k = 0; k < 8; k++) {
                            int32_t nl = l + dl[k];
                            int32_t nc = c + dc[k];

                            // Ignora vizinhos fora da matriz
                            if (nl < 0 || nl >= linhas || nc < 0 || nc >= colunas) {
                                continue;
                            }

                            size_t n_idx = (size_t)nl * (size_t)colunas + (size_t)nc;
                            if (estado_atual[n_idx] == EM_CHAMAS) {
                                // [Nota para Nicholas] NOTE: na verdade prop_linha eh
                                // dl[k] e dc[k] respectivamente, mas fica que nem no
                                // documento
                                int32_t prop_linha = (int32_t) -dl[k];
                                int32_t prop_coluna = (int32_t) -dc[k];
                                
                                // Vizinhos ortogonais contribuem com 10, diagonais com 7
                                //
                        
                                int32_t abs_l = prop_linha < 0 ? -prop_linha : prop_linha;
                                int32_t abs_c = prop_coluna < 0 ? -prop_coluna : prop_coluna;
                                int32_t p_basico = (abs_l + abs_c == 1) ? 10 : 7;

                                // Alinhamento com o vento
                                int32_t A = prop_linha * vento_l + prop_coluna * vento_c;

                                // Peso do vizinho
                                int32_t p_v = p_basico + (int32_t)(vento_int * (float)A);
                                if (p_v < 1) p_v = 1;

                                S += p_v;
                            }
                        }

                        // Potencial de ignição em aritmética inteira truncada[cite: 1]

                        // Ao inves de I = J/100, e I >= LIMIAR, fazer
                        // J >= LIMIAR * 100,
                        // pois garanto valores inteiros nas operacoes, ao
                        // inves de arredondamento por divisao

                        // int32_t I = (S * fator_comb[dados_celulas[idx].cobertura] * (100 - (int32_t)dados_celulas[idx].umidade))/100;
                        int32_t J = (S * fator_comb[dados_celulas[idx].cobertura] * (100 - (int32_t)dados_celulas[idx].umidade));

                        if (J >= limiar_ignicao * 100) {
                            proximo_estado[idx] = EM_CHAMAS;
                            proximo_tempo[idx] = (dados_celulas[idx].cobertura == VEGETACAO_RASTEIRA) ? 2 : 4;
                            novas_ignicoes++;
                        } else {
                            proximo_estado[idx] = INTACTA;
                            proximo_tempo[idx] = 0;
                        }
                        break;
                    }

                    case EM_CHAMAS: {
                        proximo_tempo[idx] = tempo_atual[idx] - 1;
                        if (proximo_tempo[idx] == 0) {
                            proximo_estado[idx] = QUEIMADA;
                        } else {
                            proximo_estado[idx] = EM_CHAMAS;
                        }
                        break;
                    }
                }
            }
        }

        // Passo 3: calcular as estatísticas do próximo estado
        total_ignicoes += novas_ignicoes;
        if (novas_ignicoes > pico_ignicoes_quant) {
            pico_ignicoes_quant = novas_ignicoes;
            pico_ignicoes_passo = passo;
        }

        // Passo 4: Trocar as matrizes
        int32_t *temp_tempo = tempo_atual;
        tempo_atual = proximo_tempo;
        proximo_tempo = temp_tempo;

        estado_t *temp_estado = estado_atual;
        estado_atual = proximo_estado;
        proximo_estado = temp_estado;

        // Passo DEBUG: imprimir estado
        // imprime_estado();
        // fflush(0);

        // Passo 5: verificar a condição de parada
        //


        // printf("Passo %d concluído.\n", passo);
    }
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Número de argumentos errado! Uso correto: ./fire_seq "
                        "[arquivo_entrada]\n");
        exit(1);
    }

    if (ler_entradas(argv[1]) != 0) {
        limpar_memoria();
        exit(1);
    }

    uint32_t rnd_seed_original = rnd_semente;

    if (gerar_matriz() != 0) {
        limpar_memoria();
        exit(1);
    }

    if (mapa_de_contencao() != 0) {
        limpar_memoria();
        exit(1);
    }

    
    // si el nombre del archivo empieza con "tests/"
    // se o nome do arquivo começar com "tests/"
    if (strncmp(argv[1], "tests/", 6) == 0) {
        printf("Linhas: %d, Colunas: %d, Passos: %d, Threads: %d, Seed: %d, "
            "Threshold: %d\n",
            linhas, colunas, max_passos, nthreads, rnd_seed_original, limiar_ignicao);
        printf("Vento: (%d, %d), Intensidade: %.2f\n", vento_l, vento_c, vento_int);
        printf("Focos: %d, Zonas: %d\n", n_focos, n_zonas);
        for (int32_t i = 0; i < n_focos; i++) {
            printf("Foco %d: (%d, %d)\n", i, focos[i].x, focos[i].y);
        }
        for (int32_t i = 0; i < n_zonas; i++) {
            printf("Zona %d: Passo %d, Limites: (%d, %d) a (%d, %d)\n", i,
                zonas[i].passo_ativacao, zonas[i].fundo.x, zonas[i].fundo.y,
                zonas[i].topo.x, zonas[i].topo.y);
        }

        for (int32_t i = 0; i < linhas; i++) {
            for (int32_t j = 0; j < colunas; j++) {
                size_t idx = (size_t)i * (size_t)colunas + (size_t)j;
                // imprimir tiempo de contencao
                if (ativacao[idx] == -1) {
                    printf("   ");
                } else {
                    printf("%d ", ativacao[idx]);
                }
        
            }
            printf("\n");
        }
    }


    fflush(0);

    // imprime_estado();
    // // printf("Configuracao inicial concluído.\n");
    // fflush(0);
    double inicio = omp_get_wtime();
    simulacao();
    double fim = omp_get_wtime();

    printf("passos: %" PRId32 "\n", passo);
    printf("total_ignicoes: %" PRId64 "\n", total_ignicoes);
    printf("pico_ignicoes: %" PRId32 " %" PRId64 "\n", pico_ignicoes_passo, pico_ignicoes_quant);
    printf("tempo: %.6lf\n", fim - inicio);
    limpar_memoria();

    return 0;
}
