/* VAE didatico em C: 48 -> 32 -> (mu, logvar de 4) -> 32 -> 48.
 * Treinamento real por backpropagation + Adam, com BCE + beta*KL.
 * Referencia: Kingma e Welling, https://arxiv.org/abs/1312.6114
 * Os dez modelos sao uma base pequena de demonstracao, nao MNIST. */
#include "digitos.h"
#include "xlsx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <locale.h>

enum { H = 32, Z = 4,
    WE = 0, BE = WE + H * PIXELS,
    WM = BE + H, BM = WM + Z * H,
    WV = BM + Z, BV = WV + Z * H,
    WD = BV + Z, BD = WD + H * Z,
    WO = BD + H, BO = WO + PIXELS * H,
    PARAMETROS = BO + PIXELS
};
typedef struct { double w[PARAMETROS]; } Modelo;
typedef struct { uint32_t estado; } Aleatorio;
typedef struct {
    double h[H], mu[Z], lv[Z], sigma[Z], z[Z], d[H], logits[PIXELS], p[PIXELS];
} Ativacoes;
typedef struct { double bce, kl; } Perda;
typedef struct {
    int a, b, epocas, passos, tem_alpha, testar, listar;
    double alpha, beta, taxa, limiar, ruido;
    uint32_t semente;
    const char *saida, *modelos;
} Opcoes;

static double uniforme(Aleatorio *r) {
    uint32_t x = r->estado;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5; r->estado = x;
    return ((double)x + 0.5) / 4294967296.0;
}
static double normal(Aleatorio *r) {
    double u = uniforme(r), v = uniforme(r);
    return sqrt(-2.0 * log(u)) * cos(6.2831853071795864769 * v);
}
static double sigmoid(double x) {
    if (x >= 0) return 1.0 / (1.0 + exp(-x));
    { double e = exp(x); return e / (1.0 + e); }
}
static void entrada(int digito, double *x) {
    int i;
    for (i = 0; i < PIXELS; ++i) x[i] = DESENHOS[digito][i / COLUNAS][i % COLUNAS] - '0';
}
static void iniciar_matriz(Modelo *m, Aleatorio *r, int offset, int saidas, int entradas) {
    int i; double escala = sqrt(6.0 / (entradas + saidas));
    for (i = 0; i < saidas * entradas; ++i) m->w[offset + i] = (2 * uniforme(r) - 1) * escala;
}
static void iniciar(Modelo *m, Aleatorio *r) {
    int i; memset(m, 0, sizeof(*m));
    iniciar_matriz(m, r, WE, H, PIXELS); iniciar_matriz(m, r, WM, Z, H);
    iniciar_matriz(m, r, WV, Z, H); iniciar_matriz(m, r, WD, H, Z);
    iniciar_matriz(m, r, WO, PIXELS, H);
    for (i = 0; i < PIXELS; ++i) m->w[BO + i] = -1.5;
}

static void codificar(const Modelo *m, const double *x, Ativacoes *a) {
    int j, k;
    for (j = 0; j < H; ++j) {
        double s = m->w[BE + j];
        for (k = 0; k < PIXELS; ++k) s += m->w[WE + j * PIXELS + k] * x[k];
        a->h[j] = tanh(s);
    }
    for (j = 0; j < Z; ++j) {
        double mu = m->w[BM + j], lv = m->w[BV + j];
        for (k = 0; k < H; ++k) {
            mu += m->w[WM + j * H + k] * a->h[k];
            lv += m->w[WV + j * H + k] * a->h[k];
        }
        a->mu[j] = mu;
        /* Parametrizacao suave, derivada tambem no backward. Evita exp enorme. */
        a->lv[j] = 6.0 * tanh(lv / 6.0);
        a->sigma[j] = exp(0.5 * a->lv[j]);
    }
}
static void decodificar(const Modelo *m, Ativacoes *a) {
    int j, k;
    for (j = 0; j < H; ++j) {
        double s = m->w[BD + j];
        for (k = 0; k < Z; ++k) s += m->w[WD + j * Z + k] * a->z[k];
        a->d[j] = tanh(s);
    }
    for (j = 0; j < PIXELS; ++j) {
        double s = m->w[BO + j];
        for (k = 0; k < H; ++k) s += m->w[WO + j * H + k] * a->d[k];
        a->logits[j] = s; a->p[j] = sigmoid(s);
    }
}

/* epsilon e explicito para permitir verificar numericamente as derivadas. */
static Perda frente(const Modelo *m, const double *x, const double *epsilon, Ativacoes *a) {
    Perda loss = {0, 0}; int i;
    codificar(m, x, a);
    for (i = 0; i < Z; ++i) a->z[i] = a->mu[i] + a->sigma[i] * epsilon[i];
    decodificar(m, a);
    for (i = 0; i < PIXELS; ++i) {
        double l = a->logits[i];
        /* BCE com logits, sem log(0) nem clamp que altere a derivada. */
        loss.bce += fmax(l, 0.0) - x[i] * l + log1p(exp(-fabs(l)));
    }
    for (i = 0; i < Z; ++i)
        loss.kl += 0.5 * (a->mu[i] * a->mu[i] + exp(a->lv[i]) - 1.0 - a->lv[i]);
    return loss;
}

static void tras(const Modelo *m, const double *x, const double *epsilon,
                 const Ativacoes *a, double beta, double *g) {
    double dd[H] = {0}, dz[Z] = {0}, dh[H] = {0};
    int i, j;
    for (i = 0; i < PIXELS; ++i) {
        double e = a->p[i] - x[i];
        g[BO + i] += e;
        for (j = 0; j < H; ++j) {
            g[WO + i * H + j] += e * a->d[j];
            dd[j] += e * m->w[WO + i * H + j];
        }
    }
    for (i = 0; i < H; ++i) {
        double e = dd[i] * (1.0 - a->d[i] * a->d[i]);
        g[BD + i] += e;
        for (j = 0; j < Z; ++j) {
            g[WD + i * Z + j] += e * a->z[j];
            dz[j] += e * m->w[WD + i * Z + j];
        }
    }
    for (i = 0; i < Z; ++i) {
        double dm = dz[i] + beta * a->mu[i];
        double dl = (dz[i] * 0.5 * a->sigma[i] * epsilon[i]
                     + 0.5 * beta * (exp(a->lv[i]) - 1.0))
                    * (1.0 - (a->lv[i] / 6.0) * (a->lv[i] / 6.0));
        g[BM + i] += dm; g[BV + i] += dl;
        for (j = 0; j < H; ++j) {
            g[WM + i * H + j] += dm * a->h[j];
            g[WV + i * H + j] += dl * a->h[j];
            dh[j] += dm * m->w[WM + i * H + j] + dl * m->w[WV + i * H + j];
        }
    }
    for (i = 0; i < H; ++i) {
        double e = dh[i] * (1.0 - a->h[i] * a->h[i]);
        g[BE + i] += e;
        for (j = 0; j < PIXELS; ++j) g[WE + i * PIXELS + j] += e * x[j];
    }
}

static void avaliar(const Modelo *m, const char *rotulo) {
    Ativacoes a; double x[PIXELS], e[Z] = {0}, bce = 0, kl = 0; int d, i, erros = 0;
    for (d = 0; d < DIGITOS; ++d) {
        Perda p; entrada(d, x); p = frente(m, x, e, &a); bce += p.bce; kl += p.kl;
        for (i = 0; i < PIXELS; ++i) if ((a.p[i] >= 0.5) != (x[i] > 0.5)) ++erros;
    }
    printf("%s (z=mu): BCE=%.6f, KL=%.6f; erros nos modelos de treino: %d/480.\n", rotulo, bce / DIGITOS, kl / DIGITOS, erros);
}

static int treinar(Modelo *m, Aleatorio *rng, const Opcoes *o) {
    double *mem = (double *)calloc(3 * PARAMETROS, sizeof(double));
    double *g, *v, *s, b1p = 1, b2p = 1;
    int epoch, i, d, aquecimento = o->epocas / 4;
    if (!mem) { fprintf(stderr, "Memoria insuficiente.\n"); return 0; }
    if (aquecimento < 1) aquecimento = 1;
    if (aquecimento > 500) aquecimento = 500;
    g = mem; v = mem + PARAMETROS; s = mem + 2 * PARAMETROS;
    avaliar(m, "Antes");
    for (epoch = 1; epoch <= o->epocas; ++epoch) {
        double beta = o->beta * fmin(1.0, (double)epoch / aquecimento);
        double norma = 0, fator, soma_bce = 0, soma_kl = 0;
        memset(g, 0, PARAMETROS * sizeof(double));
        for (d = 0; d < DIGITOS; ++d) {
            double x[PIXELS], e[Z]; Ativacoes a; Perda p;
            entrada(d, x); for (i = 0; i < Z; ++i) e[i] = normal(rng);
            p = frente(m, x, e, &a); soma_bce += p.bce; soma_kl += p.kl;
            tras(m, x, e, &a, beta, g);
        }
        for (i = 0; i < PARAMETROS; ++i) { g[i] /= DIGITOS; norma += g[i] * g[i]; }
        if (!isfinite(norma) || !isfinite(soma_bce) || !isfinite(soma_kl)) {
            fprintf(stderr, "Treino divergiu. Reduza --rate.\n"); free(mem); return 0;
        }
        fator = norma > 100.0 ? 10.0 / sqrt(norma) : 1.0;
        b1p *= 0.9; b2p *= 0.999;
        for (i = 0; i < PARAMETROS; ++i) {
            double gi = g[i] * fator;
            v[i] = 0.9 * v[i] + 0.1 * gi;
            s[i] = 0.999 * s[i] + 0.001 * gi * gi;
            m->w[i] -= o->taxa * (v[i] / (1.0 - b1p)) / (sqrt(s[i] / (1.0 - b2p)) + 1e-8);
        }
        if (epoch == 1 || epoch % 1000 == 0 || epoch == o->epocas)
            printf("Epoca %d/%d: BCE=%.5f KL=%.5f beta=%.4f perda=%.5f\n", epoch, o->epocas,
                soma_bce / DIGITOS, soma_kl / DIGITOS, beta, (soma_bce + beta * soma_kl) / DIGITOS);
    }
    free(mem); avaliar(m, "Depois"); return 1;
}

static int autoteste(void) {
    Modelo m; Aleatorio r = {123}; Ativacoes a; double x[PIXELS], e[Z] = {0.3, -0.7, 0.4, 0.2};
    double *g = (double *)calloc(PARAMETROS, sizeof(double)), maior = 0;
    int i, d, linha, c;
    if (!g) return 1;
    for (d = 0; d < DIGITOS; ++d) for (linha = 0; linha < LINHAS; ++linha) {
        if (strlen(DESENHOS[d][linha]) != COLUNAS) { free(g); return 1; }
        for (c = 0; c < COLUNAS; ++c) if (DESENHOS[d][linha][c] != '0' && DESENHOS[d][linha][c] != '1') { free(g); return 1; }
    }
    iniciar(&m, &r); entrada(8, x); frente(&m, x, e, &a); tras(&m, x, e, &a, 0.37, g);
    for (i = 0; i < PARAMETROS; ++i) {
        double anterior = m.w[i], numerico, erro; Perda mais, menos;
        m.w[i] = anterior + 1e-5; mais = frente(&m, x, e, &a);
        m.w[i] = anterior - 1e-5; menos = frente(&m, x, e, &a);
        m.w[i] = anterior;
        numerico = ((mais.bce + 0.37 * mais.kl) - (menos.bce + 0.37 * menos.kl)) / 2e-5;
        erro = fabs(numerico - g[i]) / fmax(1.0, fmax(fabs(numerico), fabs(g[i])));
        if (!isfinite(erro) || erro > 1e-5) { fprintf(stderr, "Gradiente incorreto no parametro %d.\n", i); free(g); return 1; }
        if (erro > maior) maior = erro;
    }
    free(g); printf("Autoteste OK: dez desenhos validos; %d derivadas verificadas, erro maximo %.3g.\n", PARAMETROS, maior); return 0;
}

static void listar(void) {
    int d, i, r;
    for (d = 0; d < DIGITOS; ++d) {
        printf("\nDigito %d:\n", d);
        for (r = 0; r < LINHAS; ++r) printf("%s\n", DESENHOS[d][r]);
        printf("Vetor: ");
        for (i = 0; i < PIXELS; ++i) printf("%c%s", DESENHOS[d][i / COLUNAS][i % COLUNAS], i == PIXELS - 1 ? "\n" : ",");
    }
}
static int modelos_xlsx(const char *path) {
    Planilha p[DIGITOS] = {0}; int d, i;
    for (d = 0; d < DIGITOS; ++d) {
        snprintf(p[d].nome, sizeof(p[d].nome), "Digito_%d", d);
        snprintf(p[d].titulo, sizeof(p[d].titulo), "Digito %d", d);
        snprintf(p[d].descricao, sizeof(p[d].descricao), "Modelo binario do digito %d; grade 8 x 6.", d);
        snprintf(p[d].parametros, sizeof(p[d].parametros), "%s", d == 0 ? "Zero transcrito do anexo gerar_num.xlsx." : "Desenho didatico definido em digitos.h.");
        for (i = 0; i < PIXELS; ++i) p[d].pixels[i] = DESENHOS[d][i / COLUNAS][i % COLUNAS] - '0';
    }
    return escrever_xlsx(path, p, DIGITOS);
}
static int gerar(const Modelo *m, Aleatorio *rng, const Opcoes *o) {
    int n = o->tem_alpha ? 1 : o->passos, s, j;
    Planilha *p = (Planilha *)calloc((size_t)n, sizeof(Planilha));
    Ativacoes a, b; double x[PIXELS]; int ok;
    if (!p) return 0;
    entrada(o->a, x); codificar(m, x, &a);
    entrada(o->b, x); codificar(m, x, &b);
    for (s = 0; s < n; ++s) {
        Ativacoes resultado;
        double alpha = o->tem_alpha ? o->alpha : (double)s / (n - 1);
        for (j = 0; j < Z; ++j)
            resultado.z[j] = (1 - alpha) * a.mu[j] + alpha * b.mu[j] + (o->ruido > 0 ? o->ruido * normal(rng) : 0);
        decodificar(m, &resultado);
        snprintf(p[s].nome, sizeof(p[s].nome), "Mistura_%03d", s + 1);
        snprintf(p[s].titulo, sizeof(p[s].titulo), "%d + %d | %.1f%% de %d", o->a, o->b, alpha * 100, o->b);
        snprintf(p[s].descricao, sizeof(p[s].descricao), "Mistura latente: %.2f%% de %d e %.2f%% de %d.", (1 - alpha) * 100, o->a, alpha * 100, o->b);
        snprintf(p[s].parametros, sizeof(p[s].parametros), "Limiar=%.3g; beta=%.3g; epocas=%d; taxa=%.3g; semente=%u; ruido=%.3g.",
            o->limiar, o->beta, o->epocas, o->taxa, (unsigned)o->semente, o->ruido);
        p[s].tem_probabilidades = 1;
        printf("\n%s\n", p[s].titulo);
        for (j = 0; j < PIXELS; ++j) {
            p[s].probabilidades[j] = resultado.p[j];
            p[s].pixels[j] = resultado.p[j] >= o->limiar;
            printf("%c%s", p[s].pixels[j] ? '1' : '.', (j + 1) % COLUNAS ? " " : "\n");
        }
    }
    ok = escrever_xlsx(o->saida, p, n); free(p); return ok;
}

static void ajuda(void) {
    puts("VAE de digitos em C - grade de 8 linhas x 6 colunas\n"
        "Uso: vae [opcoes]\n"
        "  --a N --b N        Digitos de origem (0..9; padrao: 0 e 8)\n"
        "  --alpha X          Uma mistura: 0=origem A, 1=origem B\n"
        "  --steps N          Sequencia de 2..101 misturas (padrao: 5)\n"
        "  --out arquivo.xlsx Arquivo de saida (padrao: misturas.xlsx)\n"
        "  --epochs N         Epocas de treinamento (padrao: 6000)\n"
        "  --seed N           Semente de 1..4294967295 (padrao: 42)\n"
        "  --threshold X      Limiar entre 0 e 1, exclusivo (padrao: 0.5)\n"
        "  --beta X           Peso KL: 0 < X <= 10 (padrao: 0.15)\n"
        "  --rate X           Taxa Adam: 0 < X <= 0.1 (padrao: 0.003)\n"
        "  --noise X          Ruido latente de 0..5 (padrao: 0)\n"
        "  --templates arq.xlsx  Exporta os dez modelos sem treinar\n"
        "  --print            Mostra os dez desenhos e vetores\n"
        "  --self-test        Verifica todas as derivadas por diferencas finitas\n"
        "  --help             Mostra esta ajuda\n"
        "Use --alpha OU --steps. Arquivos existentes nao sao sobrescritos.");
}
static int inteiro(const char *s, long min, long max, int *valor) {
    char *fim; long n; errno = 0; n = strtol(s, &fim, 10);
    if (errno || fim == s || *fim || n < min || n > max) return 0;
    *valor = (int)n; return 1;
}
static int real(const char *s, double min, double max, double *valor) {
    char *fim; double n; errno = 0; n = strtod(s, &fim);
    if (errno || fim == s || *fim || !isfinite(n) || n < min || n > max) return 0;
    *valor = n; return 1;
}
static int extensao(const char *path) {
    size_t n = strlen(path); return n > 5 && strcmp(path + n - 5, ".xlsx") == 0;
}
int main(int argc, char **argv) {
    Opcoes o = {0, 8, 6000, 5, 0, 0, 0, 0.5, 0.15, 0.003, 0.5, 0, 42, "misturas.xlsx", NULL};
    Modelo m; Aleatorio rng; int i, passos_explicitos = 0;
    setlocale(LC_NUMERIC, "C"); /* XML exige ponto decimal, mesmo em pt-BR. */
    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i], *v;
        if (!strcmp(arg, "--help")) { ajuda(); return 0; }
        if (!strcmp(arg, "--self-test")) { o.testar = 1; continue; }
        if (!strcmp(arg, "--print")) { o.listar = 1; continue; }
        if (i + 1 == argc) { fprintf(stderr, "Falta valor para %s.\n", arg); return 2; }
        v = argv[++i];
        if (!strcmp(arg, "--a")) { if (!inteiro(v, 0, 9, &o.a)) goto invalido; }
        else if (!strcmp(arg, "--b")) { if (!inteiro(v, 0, 9, &o.b)) goto invalido; }
        else if (!strcmp(arg, "--epochs")) { if (!inteiro(v, 1, 100000, &o.epocas)) goto invalido; }
        else if (!strcmp(arg, "--steps")) { if (!inteiro(v, 2, 101, &o.passos)) goto invalido; passos_explicitos = 1; }
        else if (!strcmp(arg, "--alpha")) { if (!real(v, 0, 1, &o.alpha)) goto invalido; o.tem_alpha = 1; }
        else if (!strcmp(arg, "--beta")) { if (!real(v, 1e-9, 10, &o.beta)) goto invalido; }
        else if (!strcmp(arg, "--rate")) { if (!real(v, 1e-9, 0.1, &o.taxa)) goto invalido; }
        else if (!strcmp(arg, "--threshold")) { if (!real(v, 1e-9, 1 - 1e-9, &o.limiar)) goto invalido; }
        else if (!strcmp(arg, "--noise")) { if (!real(v, 0, 5, &o.ruido)) goto invalido; }
        else if (!strcmp(arg, "--seed")) {
            char *fim; unsigned long n; errno = 0; n = strtoul(v, &fim, 10);
            if (errno || v[0] == '-' || fim == v || *fim || n == 0 || n > UINT32_MAX) goto invalido;
            o.semente = (uint32_t)n;
        }
        else if (!strcmp(arg, "--out")) { if (!extensao(v)) goto invalido; o.saida = v; }
        else if (!strcmp(arg, "--templates")) { if (!extensao(v)) goto invalido; o.modelos = v; }
        else { fprintf(stderr, "Opcao desconhecida: %s. Use --help.\n", arg); return 2; }
        continue;
invalido: fprintf(stderr, "Valor invalido para %s: %s. Use --help.\n", arg, v); return 2;
    }
    if (o.tem_alpha && passos_explicitos) { fprintf(stderr, "Escolha --alpha ou --steps.\n"); return 2; }
    if (o.testar) return autoteste();
    if (o.listar) { listar(); return 0; }
    if (o.modelos) {
        if (!modelos_xlsx(o.modelos)) return 1;
        printf("Modelos salvos em %s\n", o.modelos); return 0;
    }
    rng.estado = o.semente; iniciar(&m, &rng);
    printf("Treinando nos dez modelos binarios (%d parametros, semente %u).\n", PARAMETROS, (unsigned)o.semente);
    if (!treinar(&m, &rng, &o) || !gerar(&m, &rng, &o)) return 1;
    printf("\nArquivo salvo: %s\n", o.saida); return 0;
}
