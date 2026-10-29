#include "treino.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Constantes usuais do Adam (Kingma e Ba, 2014). */
#define ADAM_B1 0.9
#define ADAM_B2 0.999
#define ADAM_EPS 1e-8
/* Se a norma do gradiente passar disso, ele e reduzido para essa norma. */
#define NORMA_MAXIMA 10.0

int avaliar(const Pesos *w, const Dados *d, const char *rotulo) {
    double zero[N_LATENTE] = {0};
    double bce = 0.0, kl = 0.0;
    int erros = 0;
    int k, p;
    for (k = 0; k < d->n; ++k) {
        const double *x = d->exemplos[k].x;
        Ativacoes a;
        /* epsilon = 0 => z = mu: a reconstrucao "media", sem sorteio. */
        Perda perda = vae_frente(w, x, zero, &a);
        bce += perda.bce;
        kl += perda.kl;
        for (p = 0; p < PIXELS; ++p) {
            if ((a.O4[p] >= 0.5) != (x[p] > 0.5)) {
                ++erros;
            }
        }
    }
    printf("%s (z = mu): BCE=%.5f  KL=%.5f  celulas erradas: %d/%d\n",
           rotulo, bce / d->n, kl / d->n, erros, d->n * PIXELS);
    return erros;
}

int treinar(Pesos *w, const Dados *d, const Treino *t) {
    /* g = gradiente medio; m e v = medias moveis do Adam. */
    Pesos *memoria = (Pesos *)calloc(3, sizeof(Pesos));
    double *g, *m, *v, *theta = PARAMETROS(w);
    double b1_t = 1.0, b2_t = 1.0;
    int aquecimento = t->epocas / 4;
    Aleatorio rng;
    int epoca, i, k, l;

    if (!memoria) {
        fprintf(stderr, "Memoria insuficiente.\n");
        return 0;
    }
    g = PARAMETROS(&memoria[0]);
    m = PARAMETROS(&memoria[1]);
    v = PARAMETROS(&memoria[2]);
    rng.estado = t->semente;
    if (aquecimento < 1) aquecimento = 1;
    if (aquecimento > 500) aquecimento = 500;

    avaliar(w, d, "Antes ");
    for (epoca = 1; epoca <= t->epocas; ++epoca) {
        double beta = t->beta * fmin(1.0, (double)epoca / aquecimento);
        double soma_bce = 0.0, soma_kl = 0.0, norma = 0.0, fator;

        /* 1. Gradiente medio sobre todos os exemplos (lote completo). */
        memset(&memoria[0], 0, sizeof(Pesos));
        for (k = 0; k < d->n; ++k) {
            double epsilon[N_LATENTE];
            Ativacoes a;
            Perda perda;
            for (l = 0; l < N_LATENTE; ++l) {
                epsilon[l] = normal(&rng);
            }
            perda = vae_frente(w, d->exemplos[k].x, epsilon, &a);
            soma_bce += perda.bce;
            soma_kl += perda.kl;
            vae_tras(w, d->exemplos[k].x, &a, beta, &memoria[0]);
        }
        for (i = 0; i < N_PARAMETROS; ++i) {
            g[i] /= d->n;
            norma += g[i] * g[i];
        }
        if (!isfinite(norma) || !isfinite(soma_bce) || !isfinite(soma_kl)) {
            fprintf(stderr, "O treino divergiu na epoca %d. Reduza --taxa.\n", epoca);
            free(memoria);
            return 0;
        }
        norma = sqrt(norma);
        fator = norma > NORMA_MAXIMA ? NORMA_MAXIMA / norma : 1.0;

        /* 2. Passo do Adam: theta -= taxa * m^ / (sqrt(v^) + eps). */
        b1_t *= ADAM_B1;
        b2_t *= ADAM_B2;
        for (i = 0; i < N_PARAMETROS; ++i) {
            double gi = g[i] * fator;
            m[i] = ADAM_B1 * m[i] + (1.0 - ADAM_B1) * gi;
            v[i] = ADAM_B2 * v[i] + (1.0 - ADAM_B2) * gi * gi;
            theta[i] -= t->taxa * (m[i] / (1.0 - b1_t)) / (sqrt(v[i] / (1.0 - b2_t)) + ADAM_EPS);
        }

        if (epoca == 1 || epoca % 1000 == 0 || epoca == t->epocas) {
            printf("Epoca %5d/%d: BCE=%.5f  KL=%.5f  beta=%.4f  perda=%.5f\n",
                   epoca, t->epocas, soma_bce / d->n, soma_kl / d->n, beta,
                   (soma_bce + beta * soma_kl) / d->n);
        }
    }
    free(memoria);
    avaliar(w, d, "Depois");
    return 1;
}

int autoteste(void) {
    const double beta = 0.37, h = 1e-5;
    double x[PIXELS], epsilon[N_LATENTE];
    double maior = 0.0;
    Pesos *w = (Pesos *)calloc(2, sizeof(Pesos));
    Pesos *g;
    double *theta, *grad;
    Aleatorio rng = {123};
    Ativacoes a;
    int i;

    if (!w) {
        fprintf(stderr, "Memoria insuficiente.\n");
        return 1;
    }
    g = &w[1];
    theta = PARAMETROS(w);
    grad = PARAMETROS(g);
    /* Entrada binaria e epsilon fixos, sorteados uma unica vez. */
    for (i = 0; i < PIXELS; ++i) x[i] = uniforme(&rng) < 0.4 ? 1.0 : 0.0;
    for (i = 0; i < N_LATENTE; ++i) epsilon[i] = normal(&rng);
    vae_iniciar(w, &rng);

    vae_frente(w, x, epsilon, &a);
    vae_tras(w, x, &a, beta, g);
    for (i = 0; i < N_PARAMETROS; ++i) {
        double original = theta[i], numerico, erro;
        Perda mais, menos;
        theta[i] = original + h;
        mais = vae_frente(w, x, epsilon, &a);
        theta[i] = original - h;
        menos = vae_frente(w, x, epsilon, &a);
        theta[i] = original;
        /* Derivada central: [L(theta + h) - L(theta - h)] / 2h */
        numerico = ((mais.bce + beta * mais.kl) - (menos.bce + beta * menos.kl)) / (2 * h);
        erro = fabs(numerico - grad[i]) / fmax(1.0, fmax(fabs(numerico), fabs(grad[i])));
        if (!isfinite(erro) || erro > 1e-5) {
            fprintf(stderr, "Gradiente incorreto no parametro %d: analitico %.8g, numerico %.8g.\n",
                    i, grad[i], numerico);
            free(w);
            return 1;
        }
        if (erro > maior) maior = erro;
    }
    free(w);
    printf("Autoteste OK: %d derivadas conferidas por diferencas finitas; erro maximo %.3g.\n",
           N_PARAMETROS, maior);
    return 0;
}
