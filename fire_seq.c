#include <inttypes.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

uint32_t *activation = NULL;

pos_t *fire_centers = NULL;
zone_t *zones = NULL;
int32_t n_fires = 0;
int32_t n_zones = 0;

int32_t lines, columns, max_steps, nthreads;
uint32_t rnd_seed;
float fire_threshold;

int8_t wind_l, wind_c;
float wind_str;

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

    return 0;
}

// TODO: Implementar a construção do mapa de contenção
int32_t mapa_de_contencao() {}



int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Número de argumentos errado! Uso correto: ./fire_seq "
                        "[arquivo_entrada]\n");
        exit(1);
    }

    int32_t parse_res = parse_input(argv[1]);
    if (parse_res != 0) {
        if (fire_centers != NULL)
            free(fire_centers);
        if (zones != NULL)
            free(zones);
        exit(1);
    }

    uint32_t rnd_seed_original = rnd_seed;

    int32_t gen_res = generate_matrix();
    if (gen_res != 0) {
        if (fire_centers != NULL)
            free(fire_centers);
        if (zones != NULL)
            free(zones);
        exit(1);
    }

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

    if (fire_centers != NULL)
        free(fire_centers);
    if (zones != NULL)
        free(zones);



    
    return 0;
}
