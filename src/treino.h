#ifndef TREINO_H
#define TREINO_H

#include "dados.h"
#include "vae.h"
#include <stdint.h>

/* Hiperparametros do treinamento; ficam gravados junto com os pesos. */
typedef struct {
    int epocas;
    double beta;   /* peso da KL na perda */
    double taxa;   /* taxa de aprendizado do Adam */
    uint32_t semente;
} Treino;

/* Treina w (ja iniciado) em todos os exemplos, em lote completo, com Adam.
 * beta sobe de 0 ate t->beta no primeiro quarto do treino (maximo 500 epocas).
 * Retorna 1 no sucesso; 0 se o treino divergir. */
int treinar(Pesos *w, const Dados *d, const Treino *t);

/* Reconstroi cada exemplo com z = mu e imprime BCE, KL e celulas erradas.
 * Retorna o total de celulas erradas (limiar 0.5). */
int avaliar(const Pesos *w, const Dados *d, const char *rotulo);

/* Compara todas as derivadas de vae_tras com diferencas finitas.
 * Retorna 0 se todas conferem. */
int autoteste(void);

#endif
