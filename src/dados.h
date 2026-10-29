#ifndef DADOS_H
#define DADOS_H

#include "grade.h"

/* Conjunto de treino lido de um arquivo texto, uma linha por exemplo:
 *   - 48 valores 0/1 separados por espaco, tabulacao ou virgula; o rotulo
 *     e a posicao da linha (1a linha = digito 0, 2a = 1, ..., 11a = 0 ...);
 *   - ou 49 valores: o primeiro e o rotulo (0..9) e os outros 48 o desenho.
 * Linhas vazias e linhas iniciadas por '#' sao ignoradas. */
typedef struct {
    int rotulo;
    double x[PIXELS];
} Exemplo;

typedef struct {
    int n;
    Exemplo *exemplos;
} Dados;

/* Retorna 1 no sucesso; 0 com mensagem de erro em stderr. */
int dados_ler(const char *caminho, Dados *d);
void dados_liberar(Dados *d);

#endif
