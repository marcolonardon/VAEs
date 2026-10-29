#ifndef ALEATORIO_H
#define ALEATORIO_H

#include <stdint.h>

/* Gerador xorshift32: pequeno e reproduzivel em qualquer compilador.
 * A mesma semente gera sempre a mesma sequencia. Semente deve ser != 0. */
typedef struct {
    uint32_t estado;
} Aleatorio;

double uniforme(Aleatorio *r);  /* em (0, 1) */
double normal(Aleatorio *r);    /* N(0, 1), por Box-Muller */

#endif
