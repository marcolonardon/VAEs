#include "camada.h"
#include <math.h>

double ativacao(Ativacao f, double I) {
    switch (f) {
        case TANH:
            return tanh(I);
        case SIGMOIDE:
            /* Duas formas equivalentes, para nunca calcular exp de numero grande. */
            if (I >= 0) {
                return 1.0 / (1.0 + exp(-I));
            } else {
                double e = exp(I);
                return e / (1.0 + e);
            }
        case LINEAR:
        default:
            return I;
    }
}

double derivada(Ativacao f, double O) {
    switch (f) {
        case TANH:
            return 1.0 - O * O;
        case SIGMOIDE:
            return O * (1.0 - O);
        case LINEAR:
        default:
            return 1.0;
    }
}

void camada_frente(const double *O_ant, int n_ant,
                   const double *W, const double *b, int n, Ativacao f,
                   double *I, double *O) {
    int u, v;
    for (v = 0; v < n; ++v) {
        double soma = b[v];
        for (u = 0; u < n_ant; ++u) {
            soma += O_ant[u] * W[u * n + v];
        }
        I[v] = soma;
        O[v] = ativacao(f, soma);
    }
}

void camada_tras(const double *O_ant, int n_ant,
                 const double *W, int n, const double *delta,
                 double *gW, double *gb, double *dO_ant) {
    int u, v;
    for (v = 0; v < n; ++v) {
        gb[v] += delta[v];
    }
    for (u = 0; u < n_ant; ++u) {
        double soma = 0.0;
        for (v = 0; v < n; ++v) {
            gW[u * n + v] += O_ant[u] * delta[v];
            soma += W[u * n + v] * delta[v];
        }
        if (dO_ant) {
            dO_ant[u] += soma;
        }
    }
}
