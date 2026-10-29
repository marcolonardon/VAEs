#ifndef XLSX_H
#define XLSX_H

#include "grade.h"

enum { MAX_ORIGINAIS = 2 };

/* Exportador pequeno, especifico para grades 8x6, em OOXML/ZIP sem compressao.
 * Produz arquivos .xlsx reais. Nao e um leitor/editor generico de planilhas.
 *
 * Cada aba mostra no topo, lado a lado e pintadas, as matrizes de antes da
 * operacao (ex.: os digitos A e B da mistura) e depois o resultado. */
typedef struct {
    char nome[32];
    char titulo[96];
    char descricao[256];
    char parametros[256];
    int n_originais;
    char rotulo_original[MAX_ORIGINAIS][48];
    int originais[MAX_ORIGINAIS][PIXELS];
    char rotulo_resultado[48];
    int pixels[PIXELS];
    int tem_probabilidades;
    double probabilidades[PIXELS];
} Planilha;

/* Retorna 1 no sucesso, 0 no erro. Recusa sobrescrever arquivos existentes. */
int escrever_xlsx(const char *caminho, const Planilha *folhas, int quantidade);

#endif
