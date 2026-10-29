#ifndef XLSX_H
#define XLSX_H

#include "grade.h"

enum { MAX_ORIGINAIS = 2 };

/* Exportador pequeno, especifico para grades 8x6, em OOXML/ZIP sem compressao.
 * Produz arquivos .xlsx reais. Nao e um leitor/editor generico de planilhas. */
typedef struct {
    char nome[32];
    char titulo[96];
    char descricao[256];
    char parametros[256];
    int pixels[PIXELS];
    double probabilidades[PIXELS];
    int tem_probabilidades;
    /* Matrizes de antes das operacoes (ex.: digitos A e B da mistura).
     * Com tem_probabilidades, a linha 12 mostra lado a lado:
     *   originais | resultado (= A1:F8) | probabilidades */
    int n_originais;
    char rotulo_original[MAX_ORIGINAIS][48];
    int originais[MAX_ORIGINAIS][PIXELS];
} Planilha;

/* Retorna 1 no sucesso, 0 no erro. Recusa sobrescrever arquivos existentes. */
int escrever_xlsx(const char *caminho, const Planilha *folhas, int quantidade);

#endif
