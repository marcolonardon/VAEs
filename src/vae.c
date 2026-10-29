/* VAE didatico: 48 -> 24 -> (mu, log sigma^2 de 12) -> 24 -> 48.
 * Referencia: Kingma e Welling, Auto-Encoding Variational Bayes,
 * https://arxiv.org/abs/1312.6114 */
#include "vae.h"
#include "camada.h"
#include <math.h>
#include <string.h>

/* PARAMETROS(p) trata Pesos como um vetor de doubles: isso so vale se o
 * compilador nao inserir espacos entre os campos. */
_Static_assert(sizeof(Pesos) == N_PARAMETROS * sizeof(double), "Pesos com padding");

static void iniciar_matriz(double *W, int n_ant, int n, Aleatorio *r) {
    double escala = sqrt(6.0 / (n_ant + n));
    int i;
    for (i = 0; i < n_ant * n; ++i) {
        W[i] = (2.0 * uniforme(r) - 1.0) * escala;
    }
}

void vae_iniciar(Pesos *w, Aleatorio *r) {
    int p;
    memset(w, 0, sizeof(*w));
    iniciar_matriz(&w->W1[0][0], PIXELS, N_OCULTA, r);
    iniciar_matriz(&w->W2mu[0][0], N_OCULTA, N_LATENTE, r);
    iniciar_matriz(&w->W2lv[0][0], N_OCULTA, N_LATENTE, r);
    iniciar_matriz(&w->W3[0][0], N_LATENTE, N_OCULTA, r);
    iniciar_matriz(&w->W4[0][0], N_OCULTA, PIXELS, r);
    for (p = 0; p < PIXELS; ++p) {
        w->b4[p] = -1.5;
    }
}

void vae_codificar(const Pesos *w, const double *x, Ativacoes *a) {
    int l;
    /* Oculta 1: O1(j) = tanh(sum_i x(i) W1(i, j) + b1(j)) */
    camada_frente(x, PIXELS, &w->W1[0][0], w->b1, N_OCULTA, TANH, a->I1, a->O1);
    /* Latente: duas camadas lineares lado a lado, ambas a partir de O1. */
    camada_frente(a->O1, N_OCULTA, &w->W2mu[0][0], w->b2mu, N_LATENTE, LINEAR, a->mu, a->mu);
    camada_frente(a->O1, N_OCULTA, &w->W2lv[0][0], w->b2lv, N_LATENTE, LINEAR, a->logvar, a->logvar);
    for (l = 0; l < N_LATENTE; ++l) {
        a->sigma[l] = exp(0.5 * a->logvar[l]);
    }
}

void vae_decodificar(const Pesos *w, Ativacoes *a) {
    /* Oculta 3: O3(k) = tanh(sum_l z(l) W3(l, k) + b3(k)) */
    camada_frente(a->z, N_LATENTE, &w->W3[0][0], w->b3, N_OCULTA, TANH, a->I3, a->O3);
    /* Saida: x^(p) = O4(p) = sigmoide(sum_k O3(k) W4(k, p) + b4(p)) */
    camada_frente(a->O3, N_OCULTA, &w->W4[0][0], w->b4, PIXELS, SIGMOIDE, a->I4, a->O4);
}

Perda vae_frente(const Pesos *w, const double *x, const double *epsilon, Ativacoes *a) {
    Perda perda = {0.0, 0.0};
    int l, p;

    vae_codificar(w, x, a);
    /* Reparametrizacao: z = mu + sigma * epsilon. O sorteio fica em epsilon,
     * entao a derivada passa por mu e sigma normalmente. */
    for (l = 0; l < N_LATENTE; ++l) {
        a->epsilon[l] = epsilon[l];
        a->z[l] = a->mu[l] + a->sigma[l] * epsilon[l];
    }
    vae_decodificar(w, a);

    /* BCE = -sum_p [x log x^ + (1 - x) log(1 - x^)], calculada a partir do
     * logit I4 de forma estavel: max(I, 0) - x I + log(1 + exp(-|I|)). */
    for (p = 0; p < PIXELS; ++p) {
        double I = a->I4[p];
        perda.bce += fmax(I, 0.0) - x[p] * I + log1p(exp(-fabs(I)));
    }
    /* KL = 0.5 * sum_l [mu^2 + sigma^2 - 1 - log sigma^2] */
    for (l = 0; l < N_LATENTE; ++l) {
        perda.kl += 0.5 * (a->mu[l] * a->mu[l] + exp(a->logvar[l]) - 1.0 - a->logvar[l]);
    }
    return perda;
}

void vae_tras(const Pesos *w, const double *x, const Ativacoes *a, double beta, Pesos *g) {
    double delta4[PIXELS];
    double dO3[N_OCULTA] = {0}, delta3[N_OCULTA];
    double dz[N_LATENTE] = {0}, delta_mu[N_LATENTE], delta_lv[N_LATENTE];
    double dO1[N_OCULTA] = {0}, delta1[N_OCULTA];
    int j, l, p;

    /* Saida: com sigmoide + BCE, dL/dI4(p) = x^(p) - x(p). */
    for (p = 0; p < PIXELS; ++p) {
        delta4[p] = a->O4[p] - x[p];
    }
    camada_tras(a->O3, N_OCULTA, &w->W4[0][0], PIXELS, delta4, &g->W4[0][0], g->b4, dO3);

    /* Oculta 3: dL/dI3(k) = dL/dO3(k) * tanh'(I3(k)). */
    for (j = 0; j < N_OCULTA; ++j) {
        delta3[j] = dO3[j] * derivada(TANH, a->O3[j]);
    }
    camada_tras(a->z, N_LATENTE, &w->W3[0][0], N_OCULTA, delta3, &g->W3[0][0], g->b3, dz);

    /* Latente. z = mu + exp(logvar / 2) * epsilon, logo
     *   dz/dmu = 1   e   dz/dlogvar = 0.5 * sigma * epsilon.
     * A KL soma beta * mu em mu e beta * 0.5 * (sigma^2 - 1) em logvar. */
    for (l = 0; l < N_LATENTE; ++l) {
        delta_mu[l] = dz[l] + beta * a->mu[l];
        delta_lv[l] = dz[l] * 0.5 * a->sigma[l] * a->epsilon[l]
                    + beta * 0.5 * (exp(a->logvar[l]) - 1.0);
    }
    /* As duas camadas latentes partem de O1: seus efeitos se somam em dO1. */
    camada_tras(a->O1, N_OCULTA, &w->W2mu[0][0], N_LATENTE, delta_mu, &g->W2mu[0][0], g->b2mu, dO1);
    camada_tras(a->O1, N_OCULTA, &w->W2lv[0][0], N_LATENTE, delta_lv, &g->W2lv[0][0], g->b2lv, dO1);

    /* Oculta 1. A entrada x nao tem pesos antes dela: dO_ant = NULL. */
    for (j = 0; j < N_OCULTA; ++j) {
        delta1[j] = dO1[j] * derivada(TANH, a->O1[j]);
    }
    camada_tras(x, PIXELS, &w->W1[0][0], N_OCULTA, delta1, &g->W1[0][0], g->b1, NULL);
}
