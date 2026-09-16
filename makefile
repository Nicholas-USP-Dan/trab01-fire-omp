CC=gcc
CFLAGS=-std=c99 -fopenmp

.PHONY: all test submit

all: fire_seq fire_omp

submit: fire_seq fire_omp
	zip entrega_trab01_t9_ssc0903.zip fire_seq.c fire_omp.c makefile relatorio.pdf

fire_seq: fire_seq.c
	$(CC) $(CFLAGS) fire_seq.c -o fire_seq

fire_omp: fire_omp.c
	$(CC) $(CFLAGS) fire_omp.c -o fire_omp

