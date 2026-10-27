#ifndef XLSX_H
#define XLSX_H

/* Exportador pequeno, especifico para grades 8x6, em OOXML/ZIP sem compressao.
 * Produz arquivos .xlsx reais. Nao e um leitor/editor generico de planilhas. */
typedef struct {
    char nome[32];
    char titulo[96];
    char descricao[256];
    char parametros[256];
    int pixels[48];
    double probabilidades[48];
    int tem_probabilidades;
} Planilha;

/* Retorna 1 no sucesso, 0 no erro. Recusa sobrescrever arquivos existentes. */
int escrever_xlsx(const char *caminho, const Planilha *folhas, int quantidade);

#endif
