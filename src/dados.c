#include "dados.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAMANHO_LINHA 4096
#define SEPARADORES " \t,;\r\n"

/* Converte "0".."9" em numero; qualquer outra coisa vira -1. */
static int algarismo(const char *s) {
    if (s[0] >= '0' && s[0] <= '9' && s[1] == '\0') {
        return s[0] - '0';
    }
    return -1;
}

int dados_ler(const char *caminho, Dados *d) {
    char linha[TAMANHO_LINHA];
    int numero_linha = 0;
    int capacidade = 0;
    FILE *f = fopen(caminho, "r");

    d->n = 0;
    d->exemplos = NULL;
    if (!f) {
        perror(caminho);
        return 0;
    }
    while (fgets(linha, sizeof(linha), f)) {
        int valores[PIXELS + 1];
        int n_valores = 0;
        int i;
        int deslocamento;
        char *token;
        Exemplo *e;

        ++numero_linha;
        if (!strchr(linha, '\n') && !feof(f)) {
            fprintf(stderr, "%s:%d: linha longa demais.\n", caminho, numero_linha);
            goto erro;
        }
        for (token = strtok(linha, SEPARADORES); token; token = strtok(NULL, SEPARADORES)) {
            if (n_valores == 0 && token[0] == '#') {
                break;
            }
            if (n_valores == PIXELS + 1 || (valores[n_valores] = algarismo(token)) < 0) {
                fprintf(stderr, "%s:%d: esperado 48 valores 0/1 (ou rotulo + 48).\n", caminho, numero_linha);
                goto erro;
            }
            ++n_valores;
        }
        if (n_valores == 0) {
            continue;
        }
        if (n_valores != PIXELS && n_valores != PIXELS + 1) {
            fprintf(stderr, "%s:%d: %d valores; esperado 48 (ou rotulo + 48).\n", caminho, numero_linha, n_valores);
            goto erro;
        }

        if (d->n == capacidade) {
            int nova = capacidade ? 2 * capacidade : 16;
            Exemplo *novo = (Exemplo *)realloc(d->exemplos, (size_t)nova * sizeof(Exemplo));
            if (!novo) {
                fprintf(stderr, "Memoria insuficiente.\n");
                goto erro;
            }
            d->exemplos = novo;
            capacidade = nova;
        }
        e = &d->exemplos[d->n];
        deslocamento = n_valores - PIXELS;
        e->rotulo = deslocamento ? valores[0] : d->n % DIGITOS;
        for (i = 0; i < PIXELS; ++i) {
            if (valores[deslocamento + i] > 1) {
                fprintf(stderr, "%s:%d: pixel %d deve ser 0 ou 1.\n", caminho, numero_linha, i + 1);
                goto erro;
            }
            e->x[i] = valores[deslocamento + i];
        }
        ++d->n;
    }
    if (ferror(f)) {
        perror(caminho);
        goto erro;
    }
    fclose(f);
    if (d->n == 0) {
        fprintf(stderr, "%s: nenhum exemplo encontrado.\n", caminho);
        dados_liberar(d);
        return 0;
    }
    return 1;

erro:
    fclose(f);
    dados_liberar(d);
    return 0;
}

void dados_liberar(Dados *d) {
    free(d->exemplos);
    d->exemplos = NULL;
    d->n = 0;
}
