#include <inttypes.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Usar um enum em cover e state ao inves de um uint8_t?
typedef enum {
    COVER_AGUA = 0,
    COVER_SOLO_EXPOSTO = 1,
    COVER_VEGETACAO_RASTEIRA = 2,
    COVER_FLORESTA = 3
} cover_t;

typedef enum {
    STATE_NAO_COMBUSTIVEL = 0,
    STATE_INTACTA = 1,
    STATE_EM_CHAMAS = 2,
    STATE_QUEIMADA = 3,
    STATE_CONTENCAO = 4
} state_t;

typedef struct {
    uint8_t cover;
    uint8_t humidity;
    uint8_t state;
    uint8_t burn_time;
} cell_t;

typedef struct {
    int32_t x;
    int32_t y;
} pos_t;

typedef struct {
    int32_t step;
    pos_t top;
    pos_t bottom;
} zone_t;

cell_t *current_state = NULL;
cell_t *next_state = NULL;

int32_t *activation = NULL;

pos_t *fire_centers = NULL;
zone_t *zones = NULL;
int32_t n_fires = 0;
int32_t n_zones = 0;

int32_t lines, columns, max_steps, nthreads;
uint32_t rnd_seed;
float fire_threshold;

int8_t wind_l, wind_c;
float wind_str;

// Fatores de combustível
const int32_t fator_comb[4] = {0, 0, 8, 12};
// vizinhos de Moore (8 vizinhos)
const int dr[8] = {-1, -1, -1,  0, 0,  1, 1, 1};
const int dc[8] = {-1,  0,  1, -1, 1, -1, 0, 1};

// NOTE: Talvez seja válido simplificar a lógica...
int32_t parse_input(const char *filename) {
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
        "%" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNu32 " %f",
        &lines, &columns, &max_steps, &nthreads, &rnd_seed, &fire_threshold);
    
    // Se o número de entradas lidas for diferente de 6, então retornar erro.
    if (scn_res != 6) {
        fprintf(stderr, "[Linha 1] Entrada mal formada!\n");
        fclose(input_ptr);
        return 1;
    }

    if (lines <= 0 || columns <= 0 || max_steps < 0 || nthreads <= 0 ||
        fire_threshold <= 0) {
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

    scn_res = sscanf(line_buff, "%" SCNd8 " %" SCNd8 " %f", &wind_l, &wind_c,
                     &wind_str);

    if (scn_res != 3) {
        fprintf(stderr, "[Linha 2] Entrada mal formada!\n");
        fclose(input_ptr);
        return 1;
    }

    if (wind_l < -1 || wind_l > 1 || wind_c < -1 || wind_c > 1 || 
        (wind_l == 0 && wind_c == 0) || wind_str < 0 || wind_str > 5) {
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

    scn_res = sscanf(line_buff, "%" SCNd32 " %" SCNd32, &n_fires, &n_zones);

    if (scn_res != 2) {
        fprintf(stderr, "[Linha 3] Entrada mal formada!\n");
        fclose(input_ptr);
        return 1;
    }

    if (n_fires < 0 || n_zones < 0) {
        fprintf(stderr,
                "Entrada da quantidade de focos de incêndio ou de zonas "
                "de contenção negativas!\n");
        fclose(input_ptr);
        return 1;
    }

    fire_centers = (pos_t *)malloc((size_t)n_fires * sizeof(pos_t));
    zones = (zone_t *)malloc((size_t)n_zones * sizeof(zone_t));

    for (int32_t i = 0; i < n_fires; i++) {
        if (fgets(line_buff, 100, input_ptr) == NULL) {
            perror("Erro ao ler entrada de foco de fogo");
            fclose(input_ptr);
            return 1;
        }

        scn_res = sscanf(line_buff, "%" SCNd32 " %" SCNd32, &fire_centers[i].x,
                         &fire_centers[i].y);

        if (scn_res != 2) {
            fprintf(stderr, "Entrada de foco de fogo mal formada!\n");
            fclose(input_ptr);

            return 1;
        }

        if (fire_centers[i].x < 0 || fire_centers[i].x >= lines ||
            fire_centers[i].y < 0 || fire_centers[i].y >= columns) {
            fprintf(stderr, "Foco %d fora da matriz!\n", i);
            fclose(input_ptr);

            return 1;
        }
    }

    // TODO: Melhorar eficiencia da verificação de focos repetidos (usar hash table ou algo do tipo)
    for (int32_t i = 0; i < n_fires; i++) {
        for (int32_t j = i + 1; j < n_fires; j++) {
            if (fire_centers[i].x == fire_centers[j].x &&
                fire_centers[i].y == fire_centers[j].y) {
                fprintf(stderr, "Foco repetido encontrado en (%d, %d)!\n",
                        fire_centers[i].x, fire_centers[i].y);
                fclose(input_ptr);
                return 1;
            }
        }
    }

    for (int32_t i = 0; i < n_zones; i++) {
        if (fgets(line_buff, 100, input_ptr) == NULL) {
            perror("Erro ao ler entrada de zonas");
            fclose(input_ptr);

            return 1;
        }

        scn_res =
            sscanf(line_buff,
                   "%" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32,
                   &zones[i].step, &zones[i].bottom.x, &zones[i].bottom.y,
                   &zones[i].top.x, &zones[i].top.y);

        if (scn_res != 5) {
            fprintf(stderr, "Entrada de zona mal formada!\n");
            fclose(input_ptr);

            return 1;
        }

        if (zones[i].step < 0 || zones[i].step >= max_steps) {
            fprintf(stderr, "Zona %d com passo inválido!\n", i);
            fclose(input_ptr);
            return 1;
        }

        if (zones[i].bottom.x < 0 || zones[i].bottom.x >= lines ||
            zones[i].bottom.y < 0 || zones[i].bottom.y >= columns ||
            zones[i].top.x < 0 || zones[i].top.x >= lines ||
            zones[i].top.y < 0 || zones[i].top.y >= columns) {
            fprintf(stderr, "Zona %d fora da matriz!\n", i);
            fclose(input_ptr);
            return 1;
        }

        if (zones[i].bottom.x > zones[i].top.x ||
            zones[i].bottom.y > zones[i].top.y) {
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

int32_t generate_matrix() {
    // Alocação de memória para as matrizes de células
    size_t total_cells = (size_t)lines * (size_t)columns;

    current_state = (cell_t *)malloc(total_cells * sizeof(cell_t));
    next_state = (cell_t *)malloc(total_cells * sizeof(cell_t));

    if (current_state == NULL || next_state == NULL) {
        fprintf(stderr, "Erro ao alocar memória para a matriz de células!\n");
        return 1;
    }

    // Geração sequencial da cobertura e umidade
    for (int32_t i = 0; i < lines; i++) {
        for (int32_t j = 0; j < columns; j++) {
            size_t idx = (size_t)i * (size_t)columns + (size_t)j;

            // Geração da cobertura
            int val_cob = rand_r(&rnd_seed) % 100;
            if (val_cob <= 9) {
                current_state[idx].cover = COVER_AGUA;
                current_state[idx].state = STATE_NAO_COMBUSTIVEL;
            } else if (val_cob <= 19) {
                current_state[idx].cover = COVER_SOLO_EXPOSTO;
                current_state[idx].state = STATE_NAO_COMBUSTIVEL;
            } else if (val_cob <= 54) {
                current_state[idx].cover = COVER_VEGETACAO_RASTEIRA;
                current_state[idx].state = STATE_INTACTA;
            } else {
                current_state[idx].cover = COVER_FLORESTA;
                current_state[idx].state = STATE_INTACTA;
            }

            current_state[idx].humidity = (uint8_t)(rand_r(&rnd_seed) % 101);
            current_state[idx].burn_time = 0;
        }
    }

    // Aplicação e validação dos focos iniciais de incêndio
    for (int32_t f = 0; f < n_fires; f++) {
        size_t idx = (size_t)fire_centers[f].x * (size_t)columns + (size_t)fire_centers[f].y;

        if (current_state[idx].cover == COVER_AGUA || current_state[idx].cover == COVER_SOLO_EXPOSTO) {
            fprintf(stderr, "Foco inicial em (%d, %d) posicionado sobre célula não combustível!\n",
                    fire_centers[f].x, fire_centers[f].y);
            return 1;
        }
        current_state[idx].state = STATE_EM_CHAMAS;
        if (current_state[idx].cover == COVER_VEGETACAO_RASTEIRA) {
            current_state[idx].burn_time = 2;
        } else if (current_state[idx].cover == COVER_FLORESTA) {
            current_state[idx].burn_time = 4;
        }
    }

    printf("Matriz gerada com sucesso!\n");
    return 0;
}

int32_t mapa_de_contencao() {
    size_t total_cells = (size_t)lines * (size_t)columns;

    activation = (int32_t *)malloc(total_cells * sizeof(int32_t));
    if (activation == NULL) {
        fprintf(stderr, "Erro ao alocar memória para o mapa de ativação!\n");
        return 1;
    }

    for (size_t i = 0; i < total_cells; i++) {
        activation[i] = -1;
    }

    for (int32_t z = 0; z < n_zones; z++) {
        int32_t step = zones[z].step;
        int32_t r_min = zones[z].bottom.x;
        int32_t r_max = zones[z].top.x;
        int32_t c_min = zones[z].bottom.y;
        int32_t c_max = zones[z].top.y;

        for (int32_t r = r_min; r <= r_max; r++) {
            for (int32_t c = c_min; c <= c_max; c++) {
                size_t idx = (size_t)r * (size_t)columns + (size_t)c;

                if (activation[idx] == -1 || step < activation[idx]) {
                    activation[idx] = step;
                }
            }
        }
    }
    printf("Mapa de contenção gerado com sucesso!\n");
    return 0;
}

void simulation() {
    size_t total_cells = (size_t)lines * (size_t)columns;
     
    for (int32_t p = 0; p < max_steps; p++) {
        // Passo 1: ativar as zonas programadas para p
        for (size_t i = 0; i < total_cells; i++) {
            if (activation[i] == p) {
                if (current_state[i].state == STATE_INTACTA) {
                    current_state[i].state = STATE_CONTENCAO;
                }
            }
        }
        // Passo 2: atualizar o estado das células
        for (int32_t r = 0; r < lines; r++) {
            for (int32_t c = 0; c < columns; c++) {
                size_t idx = (size_t)r * (size_t)columns + (size_t)c;
                cell_t curr = current_state[idx];
                cell_t next = curr; // Copia propriedades base (cobertura, umidade)

                switch (curr.state) {
                    case STATE_NAO_COMBUSTIVEL:
                    case STATE_QUEIMADA:
                    case STATE_CONTENCAO:
                        break;

                    case STATE_EM_CHAMAS: {
                        uint8_t novo_tempo = curr.burn_time - 1;
                        if (novo_tempo == 0) {
                            next.state = STATE_QUEIMADA;
                            next.burn_time = 0;
                        } else {
                            next.state = STATE_EM_CHAMAS;
                            next.burn_time = novo_tempo;
                        }
                        break;
                    }

                    case STATE_INTACTA: {
                        // Cálculo do potencial de ignição
                        int32_t S = 0; // Suma

                        // Por cada vizinho...
                        for (int k = 0; k < 8; k++) {
                            int32_t nr = r + dr[k];
                            int32_t nc = c + dc[k];

                            // Ignora vizinhos fora da matriz
                            if (nr < 0 || nr >= lines || nc < 0 || nc >= columns) {
                                continue;
                            }

                            size_t n_idx = (size_t)nr * (size_t)columns + (size_t)nc;
                            if (current_state[n_idx].state == STATE_EM_CHAMAS) {
                                int32_t prop_linha = r - nr;
                                int32_t prop_coluna = c - nc;
                                
                                // Vizinhos ortogonais contribuem com 10, diagonais com 7
                                int32_t abs_l = prop_linha < 0 ? -prop_linha : prop_linha;
                                int32_t abs_c = prop_coluna < 0 ? -prop_coluna : prop_coluna;
                                int32_t p_basico = (abs_l + abs_c == 1) ? 10 : 7;

                                // Alinhamento com o vento
                                int32_t A = prop_linha * wind_l + prop_coluna * wind_c;

                                // Peso do vizinho
                                int32_t p_v = p_basico + (int32_t)(wind_str * (float)A);
                                if (p_v < 1) p_v = 1;

                                S += p_v;
                            }
                        }

                        // Potencial de ignição em aritmética inteira truncada[cite: 1]
                        int32_t I = (S * fator_comb[curr.cover] * (100 - (int32_t)curr.humidity)) / 100;

                        if (I >= (int32_t)fire_threshold) {
                            next.state = STATE_EM_CHAMAS;
                            next.burn_time = (curr.cover == COVER_VEGETACAO_RASTEIRA) ? 2 : 4;
                        } else {
                            next.state = STATE_INTACTA;
                            next.burn_time = 0;
                        }
                        break;
                    }
                }
                next_state[idx] = next;
            }
        }

        // Passo 3: calcular as estatísticas do próximo estado

        // Passo 4: Trocar as matrizes
        cell_t *temp = current_state;
        current_state = next_state;
        next_state = temp;

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

    if (parse_input(argv[1]) != 0) {
        free(fire_centers);
        free(zones);
        exit(1);
    }

    uint32_t rnd_seed_original = rnd_seed;

    if (generate_matrix() != 0) {
        free(fire_centers);
        free(zones);
        free(current_state);
        free(next_state);
        exit(1);
    }

    if (mapa_de_contencao() != 0) {
        free(fire_centers);
        free(zones);
        free(current_state);
        free(next_state);
        free(activation);
        exit(1);
    }

    
    // si el nombre del archivo empieza con "tests/"
    if (strncmp(argv[1], "tests/", 6) == 0) {
        printf("Linhas: %d, Colunas: %d, Passos: %d, Threads: %d, Seed: %d, "
            "Threshold: %.2f\n",
            lines, columns, max_steps, nthreads, rnd_seed_original, fire_threshold);
        printf("Vento: (%d, %d), Intensidade: %.2f\n", wind_l, wind_c, wind_str);
        printf("Focos: %d, Zonas: %d\n", n_fires, n_zones);
        for (int32_t i = 0; i < n_fires; i++) {
            printf("Foco %d: (%d, %d)\n", i, fire_centers[i].x, fire_centers[i].y);
        }
        for (int32_t i = 0; i < n_zones; i++) {
            printf("Zona %d: Passo %d, Limites: (%d, %d) a (%d, %d)\n", i,
                zones[i].step, zones[i].bottom.x, zones[i].bottom.y,
                zones[i].top.x, zones[i].top.y);
        }

        for (int32_t i = 0; i < lines; i++) {
            for (int32_t j = 0; j < columns; j++) {
                size_t idx = (size_t)i * (size_t)columns + (size_t)j;
                // imprimir tiempo de contencao
                if (activation[idx] == -1) {
                    printf("   ");
                } else {
                    printf("%d ", activation[idx]);
                }
        
            }
            printf("\n");
        }
    }


    simulation();
    free(fire_centers);
    free(zones);
    free(current_state);
    free(next_state);
    free(activation);

    return 0;
}
