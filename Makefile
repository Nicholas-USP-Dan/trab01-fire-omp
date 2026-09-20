CC=gcc
CFLAGS=-std=c99 -fopenmp -O2

.PHONY: all test submit

all: fire_seq fire_omp

submit: fire_seq fire_omp
	zip entrega_trab01_t9_ssc0903.zip fire_seq.c fire_omp.c Makefile relatorio.pdf

test: fire_seq fire_omp
	@for f in tests/*.txt entrada_carga_*.txt; do \
		s=$$(./fire_seq $$f 2>&1 | grep -v '^tempo:'); \
		o=$$(./fire_omp $$f 2>&1 | grep -v '^tempo:'); \
		if [ "$$s" = "$$o" ]; then echo "OK    $$f"; else echo "FALHA $$f"; fi; \
	done

fire_seq: fire_seq.c
	$(CC) $(CFLAGS) fire_seq.c -o fire_seq

fire_omp: fire_omp.c
	$(CC) $(CFLAGS) fire_omp.c -o fire_omp
