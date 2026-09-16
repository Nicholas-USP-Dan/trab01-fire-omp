#include <inttypes.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Usar um enum em cover e state ao inves de um uint8_t?

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

int32_t lines, columns, max_steps, nthreads, rnd_seed;
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
        "%" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %f",
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

    if (fgets(line_buff, 100, input_ptr) == NULL) {
        perror("Erro ao ler a linha 3");
        fclose(input_ptr);
        return 1;
    }

    int32_t n_fires, n_zones;

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

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Número de argumentos errado! Uso correto: ./fire_seq "
                        "[arquivo_entrada]\n");
        exit(1);
    }

    int32_t parse_res = parse_input(argv[1]);

    if (fire_centers != NULL)
        free(fire_centers);
    if (zones != NULL)
        free(zones);

    if (parse_res != 0) {
        exit(1);
    }

    return 0;
}
