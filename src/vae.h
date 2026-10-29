#ifndef VAE_H
#define VAE_H

#include "aleatorio.h"
#include "grade.h"

/* Arquitetura do professor, adaptada para VAE:
 *
 *   x (48) --W1--> O1 (24, tanh) --W2mu--> mu (12)
 *                                --W2lv--> logvar = log sigma^2 (12)
 *   z = mu + sigma * epsilon,  sigma = exp(logvar / 2),  epsilon ~ N(0, I)
 *   z (12) --W3--> O3 (24, tanh) --W4--> x^ = O4 (48, sigmoide)
 *
 * Perda = BCE(x, x^) + beta * KL(N(mu, sigma^2) || N(0, I)) */
enum { N_OCULTA = 24, N_LATENTE = 12 };

/* Todos os parametros treinaveis. W1[i][j] e exatamente W1(i, j) da aula.
 * A mesma estrutura guarda os gradientes e os momentos do Adam. */
typedef struct {
    double W1[PIXELS][N_OCULTA];      /* W1(i, j): 48 x 24 */
    double b1[N_OCULTA];
    double W2mu[N_OCULTA][N_LATENTE]; /* W2(j, l) para mu: 24 x 12 */
    double b2mu[N_LATENTE];
    double W2lv[N_OCULTA][N_LATENTE]; /* W2(j, l) para log sigma^2: 24 x 12 */
    double b2lv[N_LATENTE];
    double W3[N_LATENTE][N_OCULTA];   /* W3(l, k): 12 x 24 */
    double b3[N_OCULTA];
    double W4[N_OCULTA][PIXELS];      /* W4(k, p): 24 x 48 */
    double b4[PIXELS];
} Pesos;

enum {
    N_PARAMETROS = PIXELS * N_OCULTA + N_OCULTA
                 + 2 * (N_OCULTA * N_LATENTE + N_LATENTE)
                 + N_LATENTE * N_OCULTA + N_OCULTA
                 + N_OCULTA * PIXELS + PIXELS
};

/* Ver Pesos como vetor de N_PARAMETROS numeros (para Adam e arquivo). */
#define PARAMETROS(p) ((double *)(p))
#define PARAMETROS_CONST(p) ((const double *)(p))

/* Valores intermediarios de uma passada, guardados para a retropropagacao. */
typedef struct {
    double I1[N_OCULTA], O1[N_OCULTA];
    double mu[N_LATENTE], logvar[N_LATENTE], sigma[N_LATENTE];
    double epsilon[N_LATENTE], z[N_LATENTE];
    double I3[N_OCULTA], O3[N_OCULTA];
    double I4[PIXELS], O4[PIXELS];  /* I4 = logits; O4 = x^ (probabilidades) */
} Ativacoes;

typedef struct {
    double bce, kl;
} Perda;

/* Xavier/Glorot uniforme nos W; b = 0, exceto b4 = -1.5 (fundo e maioria). */
void vae_iniciar(Pesos *w, Aleatorio *r);

/* Encoder: x -> O1 -> (mu, logvar, sigma). */
void vae_codificar(const Pesos *w, const double *x, Ativacoes *a);

/* Decoder: a->z -> O3 -> O4 = x^. */
void vae_decodificar(const Pesos *w, Ativacoes *a);

/* Passada completa com epsilon dado (o autoteste usa epsilon fixo). */
Perda vae_frente(const Pesos *w, const double *x, const double *epsilon, Ativacoes *a);

/* Soma em g o gradiente de BCE + beta * KL, a partir de uma passada. */
void vae_tras(const Pesos *w, const double *x, const Ativacoes *a, double beta, Pesos *g);

#endif
