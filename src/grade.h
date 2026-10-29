#ifndef GRADE_H
#define GRADE_H

/* Grade dos digitos: A1:F8 da planilha gerar_num.xlsx.
 * O vetor x tem 48 posicoes lidas da esquerda para a direita e de cima
 * para baixo: x[linha * COLUNAS + coluna]. 1 = traco; 0 = fundo. */
enum { LINHAS = 8, COLUNAS = 6, PIXELS = LINHAS * COLUNAS, DIGITOS = 10 };

#endif
