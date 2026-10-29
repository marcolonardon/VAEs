#include "aleatorio.h"
#include <math.h>

double uniforme(Aleatorio *r) {
    uint32_t x = r->estado;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->estado = x;
    return ((double)x + 0.5) / 4294967296.0;
}

double normal(Aleatorio *r) {
    double u = uniforme(r);
    double v = uniforme(r);
    return sqrt(-2.0 * log(u)) * cos(6.2831853071795864769 * v);
}
