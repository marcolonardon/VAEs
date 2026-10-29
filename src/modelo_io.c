#include "modelo_io.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char ASSINATURA[4] = {'V', 'A', 'E', 'C'};
enum { VERSAO = 1 };

typedef struct {
    char assinatura[4];
    int32_t versao, pixels, oculta, latente;
} Cabecalho;

static int escrever(FILE *f, const void *dados, size_t tamanho) {
    return fwrite(dados, 1, tamanho, f) == tamanho;
}

static int ler(FILE *f, void *dados, size_t tamanho) {
    return fread(dados, 1, tamanho, f) == tamanho;
}

int modelo_salvar(const char *caminho, const Pesos *w, const Treino *t) {
    Cabecalho c;
    int ok;
    FILE *f = fopen(caminho, "wb");
    if (!f) {
        perror(caminho);
        return 0;
    }
    memcpy(c.assinatura, ASSINATURA, sizeof(ASSINATURA));
    c.versao = VERSAO;
    c.pixels = PIXELS;
    c.oculta = N_OCULTA;
    c.latente = N_LATENTE;
    ok = escrever(f, &c, sizeof(c))
      && escrever(f, &t->epocas, sizeof(t->epocas))
      && escrever(f, &t->beta, sizeof(t->beta))
      && escrever(f, &t->taxa, sizeof(t->taxa))
      && escrever(f, &t->semente, sizeof(t->semente))
      && escrever(f, w, sizeof(*w));
    if (fclose(f) != 0) ok = 0;
    if (!ok) {
        fprintf(stderr, "Erro ao gravar %s.\n", caminho);
        remove(caminho);
    }
    return ok;
}

int modelo_carregar(const char *caminho, Pesos *w, Treino *t) {
    Cabecalho c;
    int ok;
    char sobra;
    FILE *f = fopen(caminho, "rb");
    if (!f) {
        perror(caminho);
        fprintf(stderr, "Treine um modelo antes: vae treinar --modelo %s\n", caminho);
        return 0;
    }
    ok = ler(f, &c, sizeof(c));
    if (ok && memcmp(c.assinatura, ASSINATURA, sizeof(ASSINATURA)) != 0) {
        fprintf(stderr, "%s nao e um modelo deste programa.\n", caminho);
        fclose(f);
        return 0;
    }
    if (ok && (c.versao != VERSAO || c.pixels != PIXELS || c.oculta != N_OCULTA || c.latente != N_LATENTE)) {
        fprintf(stderr, "%s foi gravado para outra arquitetura (%d -> %d -> %d). Treine de novo.\n",
                caminho, (int)c.pixels, (int)c.oculta, (int)c.latente);
        fclose(f);
        return 0;
    }
    ok = ok
      && ler(f, &t->epocas, sizeof(t->epocas))
      && ler(f, &t->beta, sizeof(t->beta))
      && ler(f, &t->taxa, sizeof(t->taxa))
      && ler(f, &t->semente, sizeof(t->semente))
      && ler(f, w, sizeof(*w))
      && fread(&sobra, 1, 1, f) == 0;
    fclose(f);
    if (!ok) {
        fprintf(stderr, "%s esta incompleto ou corrompido.\n", caminho);
    }
    return ok;
}
