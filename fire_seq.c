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
    estado_t estado;
    uint8_t tempo_queima;
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
celula_t *matriz_atual = NULL;
celula_t *matriz_prox = NULL;
int32_t *ativacao = NULL;
pos_t *focos = NULL;
zona_t *zonas = NULL;

int32_t n_focos = 0;
int32_t n_zonas = 0;

int32_t linhas, colunas, max_passos, nthreads;
uint64_t total_celulas;
uint32_t rnd_semente;
int32_t limiar_ignicao;
uint64_t total_ignicoes = 0;

int8_t vento_l, vento_c;
float vento_int;

// Fatores de combustível
const int32_t fator_comb[4] = {0, 0, 8, 12};
// vizinhos de Moore (8 vizinhos)
const int dl[8] = {-1, -1, -1,  0, 0,  1, 1, 1};
const int dc[8] = {-1,  0,  1, -1, 1, -1, 0, 1};

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

    total_celulas = (uint64_t)(linhas * colunas);
    
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

    focos = (pos_t *)malloc((size_t)n_focos * sizeof(pos_t));
    zonas = (zona_t *)malloc((size_t)n_zonas * sizeof(zona_t));

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
        printf("Arquivo lido com sucesso!\n");
    }

    fclose(input_ptr);
    return 0;
}

int32_t gerar_matriz() {
    // Alocação de memória para as matrizes de células
    matriz_atual = (celula_t *)malloc(total_celulas * sizeof(celula_t));
    matriz_prox = (celula_t *)malloc(total_celulas * sizeof(celula_t));

    if (matriz_atual == NULL || matriz_prox == NULL) {
        fprintf(stderr, "Erro ao alocar memória para a matriz de células!\n");
        return 1;
    }

    // Geração sequencial da cobertura e umidade
    for (int32_t i = 0; i < linhas; i++) {
        for (int32_t j = 0; j < colunas; j++) {
            size_t idx = (size_t)i * (size_t)colunas + (size_t)j;

            // Geração da cobertura
            int val_cob = rand_r(&rnd_semente) % 100;
            if (val_cob <= 9) {
                matriz_atual[idx].cobertura = AGUA;
                matriz_atual[idx].estado = NAO_COMBUSTIVEL;
            } else if (val_cob <= 19) {
                matriz_atual[idx].cobertura = SOLO_EXPOSTO;
                matriz_atual[idx].estado = NAO_COMBUSTIVEL;
            } else if (val_cob <= 54) {
                matriz_atual[idx].cobertura = VEGETACAO_RASTEIRA;
                matriz_atual[idx].estado = INTACTA;
            } else {
                matriz_atual[idx].cobertura = FLORESTA;
                matriz_atual[idx].estado = INTACTA;
            }

            matriz_atual[idx].umidade = (uint8_t)(rand_r(&rnd_semente) % 101);
            matriz_atual[idx].tempo_queima = 0;
        }
    }

    // Aplicação e validação dos focos iniciais de incêndio
    for (int32_t f = 0; f < n_focos; f++) {
        size_t idx = (size_t)focos[f].x * (size_t)colunas + (size_t)focos[f].y;

        if (matriz_atual[idx].cobertura == AGUA || matriz_atual[idx].cobertura == SOLO_EXPOSTO) {
            fprintf(stderr, "Foco inicial em (%d, %d) posicionado sobre célula não combustível!\n",
                    focos[f].x, focos[f].y);
            return 1;
        } else if (matriz_atual[idx].estado == EM_CHAMAS) {
            fprintf(stderr, "Foco inicial em (%d, %d) repetido!\n",
                    focos[f].x, focos[f].y);
            return 1;
        }

        matriz_atual[idx].estado = EM_CHAMAS;

        if (matriz_atual[idx].cobertura == VEGETACAO_RASTEIRA) {
            matriz_atual[idx].tempo_queima = 2;
        } else if (matriz_atual[idx].cobertura == FLORESTA) {
            matriz_atual[idx].tempo_queima = 4;
        }
    }

    printf("Matriz gerada com sucesso!\n");
    return 0;
}

int32_t mapa_de_contencao() {
    ativacao = (int32_t *)malloc(total_celulas * sizeof(int32_t));
    if (ativacao == NULL) {
        fprintf(stderr, "Erro ao alocar memória para o mapa de ativação!\n");
        return 1;
    }

    for (size_t i = 0; i < total_celulas; i++) {
        ativacao[i] = -1;
    }

    for (int32_t z = 0; z < n_zonas; z++) {
        int32_t passo = zonas[z].passo_ativacao;
        int32_t r_min = zonas[z].fundo.x;
        int32_t r_max = zonas[z].topo.x;
        int32_t c_min = zonas[z].fundo.y;
        int32_t c_max = zonas[z].topo.y;

        for (int32_t r = r_min; r <= r_max; r++) {
            for (int32_t c = c_min; c <= c_max; c++) {
                size_t idx = (size_t)r * (size_t)colunas + (size_t)c;

                if (ativacao[idx] == -1 || passo < ativacao[idx]) {
                    ativacao[idx] = passo;
                }
            }
        }
    }
    printf("Mapa de contenção gerado com sucesso!\n");
    return 0;
}

void simulacao() {
    for (int32_t p = 0; p < max_passos; p++) {
        // Passo 1: ativar as zonas programadas para p
        for (size_t i = 0; i < total_celulas; i++) {
            if (ativacao[i] == p) {
                if (matriz_atual[i].estado == INTACTA) {
                    matriz_atual[i].estado = CONTENCAO;
                }
            }
        }
        
        size_t idx = 0;
        
        // Passo 2: atualizar o estado das células
        for (int32_t l = 0; l < linhas; l++) {
            for (int32_t c = 0; c < colunas; c++) {
                celula_t curr = matriz_atual[idx];
                celula_t next = curr; // Copia propriedades base (cobertura, umidade)

                switch (curr.estado) {
                    case NAO_COMBUSTIVEL:
                    case QUEIMADA:
                    case CONTENCAO:
                        break;

                    case EM_CHAMAS: {
                        uint8_t novo_tempo = curr.tempo_queima - 1;
                        if (novo_tempo == 0) {
                            next.estado = QUEIMADA;
                            next.tempo_queima = 0;
                        } else {
                            next.estado = EM_CHAMAS;
                            next.tempo_queima = novo_tempo;
                        }
                        break;
                    }

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
                            if (matriz_atual[n_idx].estado == EM_CHAMAS) {
                                // [Nota para Nicholas] NOTE: na verdade prop_linha eh
                                // dl[k] e dc[k] respectivamente, mas fica que nem no
                                // documento
                                // int32_t prop_linha = l - nl;
                                // int32_t prop_coluna = c - nc;
                                int32_t prop_linha = (int32_t) dl[k];
                                int32_t prop_coluna = (int32_t) dc[k];
                                
                                // Vizinhos ortogonais contribuem com 10, diagonais com 7
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

                        // int32_t I = (S * fator_comb[curr.cobertura] * (100 - (int32_t)curr.umidade)) / 100;
                        
                        int32_t J = (S * fator_comb[curr.cobertura] * (100 - (int32_t)curr.umidade));

                        if (J >= limiar_ignicao * 100) {
                            next.estado = EM_CHAMAS;
                            next.tempo_queima = (curr.cobertura == VEGETACAO_RASTEIRA) ? 2 : 4;
                            total_ignicoes++;
                        } else {
                            next.estado = INTACTA;
                            next.tempo_queima = 0; // NOTE: Parece que nao preciso disso
                        }
                        break;
                    }
                }

                matriz_prox[idx] = next;
                idx++;
            }
        }

        // Passo 3: calcular as estatísticas do próximo estado

        // Passo 4: Trocar as matrizes
        celula_t *temp = matriz_atual;
        matriz_atual = matriz_prox;
        matriz_prox = temp;

        // Passo 5: verificar a condição de parada

        printf("Passo %d concluído.\n", p);
    }
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Número de argumentos errado! Uso correto: ./fire_seq "
                        "[arquivo_entrada]\n");
        exit(1);
    }

    if (ler_entradas(argv[1]) != 0) {
        free(focos);
        free(zonas);
        exit(1);
    }

    uint32_t rnd_seed_original = rnd_semente;

    if (gerar_matriz() != 0) {
        free(focos);
        free(zonas);
        free(matriz_atual);
        free(matriz_prox);
        exit(1);
    }

    if (mapa_de_contencao() != 0) {
        free(focos);
        free(zonas);
        free(matriz_atual);
        free(matriz_prox);
        free(ativacao);
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


    simulacao();
    free(focos);
    free(zonas);
    free(matriz_atual);
    free(matriz_prox);
    free(ativacao);

    return 0;
}
