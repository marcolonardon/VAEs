/* Linha de comando do VAE de digitos 8 x 6. */
#include "dados.h"
#include "modelo_io.h"
#include "treino.h"
#include "vae.h"
#include "xlsx.h"
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_FOLHAS = 101 };

typedef struct {
    const char *comando;
    const char *dados, *modelo, *saida;
    Treino treino;
    int a, b, passos, n, tem_alpha, tem_passos;
    double alpha, limiar, ruido;
} Opcoes;

static void ajuda(void) {
    puts("VAE de digitos em C: 48 -> 24 -> (mu, log sigma^2 de 12) -> 24 -> 48\n"
         "\n"
         "Uso: vae <comando> [opcoes]\n"
         "\n"
         "Comandos:\n"
         "  treinar      Treina com --dados e grava os pesos em --modelo\n"
         "  reconstruir  Passa cada exemplo pelo VAE (z = mu) e compara com o original\n"
         "  misturar     Interpola no espaco latente entre os digitos --a e --b\n"
         "  gerar        Cria --n digitos novos sorteando z ~ N(0, I)\n"
         "  mostrar      Mostra os desenhos e vetores de --dados\n"
         "  autoteste    Confere todas as derivadas por diferencas finitas\n"
         "  ajuda        Mostra esta mensagem\n"
         "\n"
         "Arquivos:\n"
         "  --dados arq.txt    Exemplos de treino (padrao: dados/digitos.txt)\n"
         "  --modelo arq.bin   Pesos treinados (padrao: modelo.bin)\n"
         "  --saida arq.xlsx   Tambem grava o resultado em Excel (nao sobrescreve)\n"
         "\n"
         "Treinamento:\n"
         "  --epocas N   1..100000 (padrao: 6000)\n"
         "  --beta X     Peso da KL, 0 < X <= 10 (padrao: 0.15)\n"
         "  --taxa X     Taxa do Adam, 0 < X <= 0.1 (padrao: 0.003)\n"
         "  --semente N  1..4294967295 (padrao: 42); tambem usada por gerar e --ruido\n"
         "\n"
         "Resultados:\n"
         "  --a N --b N   Digitos a misturar, 0..9 (padrao: 0 e 8)\n"
         "  --passos N    Misturas de 0% a 100% de B, 2..101 (padrao: 5)\n"
         "  --alpha X     Uma unica mistura: 0 = A, 1 = B (nao usar com --passos)\n"
         "  --n N         Quantos digitos gerar, 1..101 (padrao: 10)\n"
         "  --limiar X    Celula = 1 se probabilidade >= X, 0 < X < 1 (padrao: 0.5)\n"
         "  --ruido X     Desvio-padrao somado a z na mistura, 0..5 (padrao: 0)\n"
         "\n"
         "Exemplo:\n"
         "  vae treinar\n"
         "  vae misturar --a 0 --b 8 --passos 5 --saida misturas.xlsx");
}

/* ---------- leitura dos argumentos ---------- */

static int ler_inteiro(const char *s, long min, long max, long *valor) {
    char *fim;
    long n;
    errno = 0;
    n = strtol(s, &fim, 10);
    if (errno || fim == s || *fim || n < min || n > max) return 0;
    *valor = n;
    return 1;
}

static int ler_real(const char *s, double min, double max, double *valor) {
    char *fim;
    double n;
    errno = 0;
    n = strtod(s, &fim);
    if (errno || fim == s || *fim || !isfinite(n) || n < min || n > max) return 0;
    *valor = n;
    return 1;
}

/* long tem 32 bits no Windows: a semente (ate 2^32 - 1) precisa de mais. */
static int ler_semente(const char *s, uint32_t *valor) {
    char *fim;
    unsigned long long n;
    errno = 0;
    n = strtoull(s, &fim, 10);
    if (errno || s[0] == '-' || fim == s || *fim || n == 0 || n > UINT32_MAX) return 0;
    *valor = (uint32_t)n;
    return 1;
}

static int termina_com(const char *s, const char *final) {
    size_t n = strlen(s), m = strlen(final);
    return n > m && strcmp(s + n - m, final) == 0;
}

/* Retorna 1 se ok; 0 com mensagem de erro. */
static int ler_opcoes(int argc, char **argv, Opcoes *o) {
    int i;
    for (i = 2; i < argc; ++i) {
        const char *nome = argv[i], *valor;
        long inteiro = 0;
        int ok;
        if (i + 1 == argc) {
            fprintf(stderr, "Falta o valor de %s.\n", nome);
            return 0;
        }
        valor = argv[++i];
        if (!strcmp(nome, "--dados")) {
            o->dados = valor;
            ok = 1;
        } else if (!strcmp(nome, "--modelo")) {
            o->modelo = valor;
            ok = 1;
        } else if (!strcmp(nome, "--saida")) {
            o->saida = valor;
            ok = termina_com(valor, ".xlsx");
        } else if (!strcmp(nome, "--epocas")) {
            ok = ler_inteiro(valor, 1, 100000, &inteiro);
            o->treino.epocas = (int)inteiro;
        } else if (!strcmp(nome, "--semente")) {
            ok = ler_semente(valor, &o->treino.semente);
        } else if (!strcmp(nome, "--a")) {
            ok = ler_inteiro(valor, 0, 9, &inteiro);
            o->a = (int)inteiro;
        } else if (!strcmp(nome, "--b")) {
            ok = ler_inteiro(valor, 0, 9, &inteiro);
            o->b = (int)inteiro;
        } else if (!strcmp(nome, "--passos")) {
            ok = ler_inteiro(valor, 2, MAX_FOLHAS, &inteiro);
            o->passos = (int)inteiro;
            o->tem_passos = 1;
        } else if (!strcmp(nome, "--n")) {
            ok = ler_inteiro(valor, 1, MAX_FOLHAS, &inteiro);
            o->n = (int)inteiro;
        } else if (!strcmp(nome, "--beta")) {
            ok = ler_real(valor, 1e-9, 10, &o->treino.beta);
        } else if (!strcmp(nome, "--taxa")) {
            ok = ler_real(valor, 1e-9, 0.1, &o->treino.taxa);
        } else if (!strcmp(nome, "--alpha")) {
            ok = ler_real(valor, 0, 1, &o->alpha);
            o->tem_alpha = 1;
        } else if (!strcmp(nome, "--limiar")) {
            ok = ler_real(valor, 1e-9, 1 - 1e-9, &o->limiar);
        } else if (!strcmp(nome, "--ruido")) {
            ok = ler_real(valor, 0, 5, &o->ruido);
        } else {
            fprintf(stderr, "Opcao desconhecida: %s. Use: vae ajuda\n", nome);
            return 0;
        }
        if (!ok) {
            fprintf(stderr, "Valor invalido para %s: %s. Use: vae ajuda\n", nome, valor);
            return 0;
        }
    }
    if (o->tem_alpha && o->tem_passos) {
        fprintf(stderr, "Use --alpha OU --passos, nao os dois.\n");
        return 0;
    }
    return 1;
}

/* ---------- saida no terminal e no Excel ---------- */

/* Desenha uma ou mais grades lado a lado: '#' = 1, '.' = 0. */
static void imprimir_grades(const int (*grades)[PIXELS], int quantidade) {
    int linha, coluna, g;
    for (linha = 0; linha < LINHAS; ++linha) {
        for (g = 0; g < quantidade; ++g) {
            for (coluna = 0; coluna < COLUNAS; ++coluna) {
                putchar(grades[g][linha * COLUNAS + coluna] ? '#' : '.');
            }
            printf(g + 1 < quantidade ? "   " : "\n");
        }
    }
}

static void binarizar(const double *probabilidades, double limiar, int *pixels) {
    int p;
    for (p = 0; p < PIXELS; ++p) {
        pixels[p] = probabilidades[p] >= limiar;
    }
}

/* Preenche uma aba com o desenho binario e as probabilidades do decoder. */
static void folha_resultado(Planilha *f, const Ativacoes *a, double limiar) {
    int p;
    f->tem_probabilidades = 1;
    for (p = 0; p < PIXELS; ++p) {
        f->probabilidades[p] = a->O4[p];
    }
    binarizar(a->O4, limiar, f->pixels);
}

/* Acrescenta a aba uma matriz de antes das operacoes, para comparacao. */
static void anexar_original(Planilha *f, const char *rotulo, const double *x) {
    int p, b = f->n_originais++;
    snprintf(f->rotulo_original[b], sizeof(f->rotulo_original[b]), "%s", rotulo);
    for (p = 0; p < PIXELS; ++p) {
        f->originais[b][p] = (int)x[p];
    }
}

/* Mostra no terminal os originais e o resultado da aba, lado a lado. */
static void imprimir_folha(const Planilha *f) {
    int grades[MAX_ORIGINAIS + 1][PIXELS];
    int b;
    printf("\n%s\n", f->titulo);
    for (b = 0; b < f->n_originais; ++b) {
        printf("%s | ", f->rotulo_original[b]);
        memcpy(grades[b], f->originais[b], sizeof(grades[b]));
    }
    printf("%s\n", f->rotulo_resultado);
    memcpy(grades[f->n_originais], f->pixels, sizeof(grades[0]));
    imprimir_grades((const int (*)[PIXELS])grades, f->n_originais + 1);
}

static void descrever_modelo(Planilha *f, const Opcoes *o, const Treino *t) {
    snprintf(f->parametros, sizeof(f->parametros),
             "Limiar=%.3g; beta=%.3g; epocas=%d; taxa=%.3g; semente do treino=%u.",
             o->limiar, t->beta, t->epocas, t->taxa, (unsigned)t->semente);
}

static int gravar(const char *caminho, const Planilha *folhas, int quantidade) {
    if (!caminho) return 1;
    if (!escrever_xlsx(caminho, folhas, quantidade)) return 0;
    printf("\nArquivo salvo: %s\n", caminho);
    return 1;
}

/* ---------- comandos ---------- */

static int cmd_treinar(const Opcoes *o) {
    Dados d;
    Pesos *w;
    Aleatorio rng;
    int ok;
    if (!dados_ler(o->dados, &d)) return 1;
    w = (Pesos *)malloc(sizeof(Pesos));
    if (!w) {
        dados_liberar(&d);
        return 1;
    }
    rng.estado = o->treino.semente;
    vae_iniciar(w, &rng);
    printf("Treinando com %d exemplos de %s (%d parametros, semente %u).\n",
           d.n, o->dados, N_PARAMETROS, (unsigned)o->treino.semente);
    ok = treinar(w, &d, &o->treino) && modelo_salvar(o->modelo, w, &o->treino);
    if (ok) printf("Modelo salvo em %s\n", o->modelo);
    free(w);
    dados_liberar(&d);
    return ok ? 0 : 1;
}

static int cmd_reconstruir(const Opcoes *o, const Pesos *w, const Treino *t) {
    Dados d;
    Planilha *folhas;
    double zero[N_LATENTE] = {0};
    int k, p, quantidade, total = 0, ok;
    if (!dados_ler(o->dados, &d)) return 1;
    quantidade = d.n < MAX_FOLHAS ? d.n : MAX_FOLHAS;
    folhas = (Planilha *)calloc((size_t)quantidade, sizeof(Planilha));
    if (!folhas) {
        dados_liberar(&d);
        return 1;
    }
    for (k = 0; k < d.n; ++k) {
        /* Alem das primeiras MAX_FOLHAS abas, so mostra no terminal. */
        Planilha extra, *f = k < quantidade ? &folhas[k] : &extra;
        Ativacoes a;
        int erros = 0;
        memset(f, 0, sizeof(*f));
        vae_frente(w, d.exemplos[k].x, zero, &a);
        folha_resultado(f, &a, o->limiar);
        anexar_original(f, "Original", d.exemplos[k].x);
        for (p = 0; p < PIXELS; ++p) {
            erros += f->originais[0][p] != f->pixels[p];
        }
        total += erros;
        snprintf(f->nome, sizeof(f->nome), "Exemplo_%03d", k + 1);
        snprintf(f->titulo, sizeof(f->titulo), "Reconstrucao do digito %d (nao e mistura)", d.exemplos[k].rotulo);
        snprintf(f->rotulo_resultado, sizeof(f->rotulo_resultado), "Reconstrucao");
        snprintf(f->descricao, sizeof(f->descricao),
                 "Exemplo %d passado pelo VAE com z = mu; %d celula(s) diferente(s) do original.",
                 k + 1, erros);
        descrever_modelo(f, o, t);
        imprimir_folha(f);
        printf("%d celula(s) diferente(s)\n", erros);
    }
    printf("\nTotal: %d de %d celulas diferentes.\n", total, d.n * PIXELS);
    if (o->saida && d.n > MAX_FOLHAS) {
        printf("O Excel recebe apenas os primeiros %d exemplos.\n", MAX_FOLHAS);
    }
    ok = gravar(o->saida, folhas, quantidade);
    free(folhas);
    dados_liberar(&d);
    return ok ? 0 : 1;
}

/* Indice do primeiro exemplo do digito (mu_do_digito ja garantiu que existe). */
static int primeiro_exemplo(const Dados *d, int digito) {
    int k = 0;
    while (k < d->n - 1 && d->exemplos[k].rotulo != digito) {
        ++k;
    }
    return k;
}

/* Exemplo de treino com menos celulas diferentes do desenho dado. */
static int mais_parecido(const Dados *d, const int *pixels, int *diferencas) {
    int k, p, melhor = 0;
    *diferencas = PIXELS + 1;
    for (k = 0; k < d->n; ++k) {
        int n = 0;
        for (p = 0; p < PIXELS; ++p) {
            n += (int)d->exemplos[k].x[p] != pixels[p];
        }
        if (n < *diferencas) {
            *diferencas = n;
            melhor = k;
        }
    }
    return melhor;
}

/* mu medio de todos os exemplos com esse rotulo. */
static int mu_do_digito(const Pesos *w, const Dados *d, int digito, double *mu) {
    int k, l, contagem = 0;
    memset(mu, 0, N_LATENTE * sizeof(double));
    for (k = 0; k < d->n; ++k) {
        Ativacoes a;
        if (d->exemplos[k].rotulo != digito) continue;
        vae_codificar(w, d->exemplos[k].x, &a);
        for (l = 0; l < N_LATENTE; ++l) mu[l] += a.mu[l];
        ++contagem;
    }
    if (!contagem) {
        fprintf(stderr, "Nao ha exemplos do digito %d nos dados.\n", digito);
        return 0;
    }
    for (l = 0; l < N_LATENTE; ++l) mu[l] /= contagem;
    return 1;
}

static int cmd_misturar(const Opcoes *o, const Pesos *w, const Treino *t) {
    Dados d;
    double mu_a[N_LATENTE], mu_b[N_LATENTE];
    int quantidade = o->tem_alpha ? 1 : o->passos;
    Planilha *folhas;
    Aleatorio rng;
    char rotulo_a[48], rotulo_b[48];
    const double *x_a, *x_b;
    int s, l, ok;

    if (!dados_ler(o->dados, &d)) return 1;
    if (!mu_do_digito(w, &d, o->a, mu_a) || !mu_do_digito(w, &d, o->b, mu_b)) {
        dados_liberar(&d);
        return 1;
    }
    folhas = (Planilha *)calloc((size_t)quantidade, sizeof(Planilha));
    if (!folhas) {
        dados_liberar(&d);
        return 1;
    }
    /* Para comparacao, o primeiro exemplo de cada digito. */
    x_a = d.exemplos[primeiro_exemplo(&d, o->a)].x;
    x_b = d.exemplos[primeiro_exemplo(&d, o->b)].x;
    snprintf(rotulo_a, sizeof(rotulo_a), "A: digito %d", o->a);
    snprintf(rotulo_b, sizeof(rotulo_b), "B: digito %d", o->b);
    rng.estado = o->treino.semente;

    for (s = 0; s < quantidade; ++s) {
        Planilha *f = &folhas[s];
        Ativacoes a;
        double alpha = o->tem_alpha ? o->alpha : (double)s / (quantidade - 1);
        /* z = (1 - alpha) mu(A) + alpha mu(B) [+ ruido] */
        for (l = 0; l < N_LATENTE; ++l) {
            a.z[l] = (1 - alpha) * mu_a[l] + alpha * mu_b[l];
            if (o->ruido > 0) a.z[l] += o->ruido * normal(&rng);
        }
        vae_decodificar(w, &a);
        snprintf(f->nome, sizeof(f->nome), "Mistura_%03d", s + 1);
        snprintf(f->titulo, sizeof(f->titulo), "Mistura do %d com o %d: %.4g%% do %d + %.4g%% do %d",
                 o->a, o->b, (1 - alpha) * 100, o->a, alpha * 100, o->b);
        snprintf(f->rotulo_resultado, sizeof(f->rotulo_resultado), "Mistura (%.4g%% do %d)", alpha * 100, o->b);
        snprintf(f->descricao, sizeof(f->descricao),
                 "O VAE codifica A e B em mu(A) e mu(B) e decodifica z = %.4g%% mu(A) + %.4g%% mu(B)%s.",
                 (1 - alpha) * 100, alpha * 100, o->ruido > 0 ? " + ruido" : "");
        descrever_modelo(f, o, t);
        folha_resultado(f, &a, o->limiar);
        anexar_original(f, rotulo_a, x_a);
        anexar_original(f, rotulo_b, x_b);
        imprimir_folha(f);
    }
    ok = gravar(o->saida, folhas, quantidade);
    free(folhas);
    dados_liberar(&d);
    return ok ? 0 : 1;
}

static int cmd_gerar(const Opcoes *o, const Pesos *w, const Treino *t) {
    Dados d;
    Planilha *folhas;
    Aleatorio rng;
    int s, l, ok;
    /* Os dados servem so para mostrar o exemplo de treino mais parecido. */
    if (!dados_ler(o->dados, &d)) return 1;
    folhas = (Planilha *)calloc((size_t)o->n, sizeof(Planilha));
    if (!folhas) {
        dados_liberar(&d);
        return 1;
    }
    rng.estado = o->treino.semente;
    for (s = 0; s < o->n; ++s) {
        Planilha *f = &folhas[s];
        Ativacoes a;
        char rotulo[48];
        int k, diferencas;
        /* Um VAE treinado aprende a decodificar amostras da priori N(0, I). */
        for (l = 0; l < N_LATENTE; ++l) {
            a.z[l] = normal(&rng);
        }
        vae_decodificar(w, &a);
        snprintf(f->nome, sizeof(f->nome), "Gerado_%03d", s + 1);
        snprintf(f->titulo, sizeof(f->titulo), "Digito gerado %d (nao e mistura: z sorteado)", s + 1);
        snprintf(f->rotulo_resultado, sizeof(f->rotulo_resultado), "Gerado");
        snprintf(f->descricao, sizeof(f->descricao),
                 "z sorteado de N(0, I) com semente %u e decodificado.", (unsigned)o->treino.semente);
        descrever_modelo(f, o, t);
        folha_resultado(f, &a, o->limiar);
        k = mais_parecido(&d, f->pixels, &diferencas);
        snprintf(rotulo, sizeof(rotulo), "Mais parecido: digito %d (%d dif.)",
                 d.exemplos[k].rotulo, diferencas);
        anexar_original(f, rotulo, d.exemplos[k].x);
        imprimir_folha(f);
    }
    ok = gravar(o->saida, folhas, o->n);
    free(folhas);
    dados_liberar(&d);
    return ok ? 0 : 1;
}

static int cmd_mostrar(const Opcoes *o) {
    Dados d;
    Planilha *folhas;
    int k, p, quantidade, ok;
    if (!dados_ler(o->dados, &d)) return 1;
    quantidade = d.n < MAX_FOLHAS ? d.n : MAX_FOLHAS;
    folhas = (Planilha *)calloc((size_t)quantidade, sizeof(Planilha));
    if (!folhas) {
        dados_liberar(&d);
        return 1;
    }
    printf("%d exemplos em %s\n", d.n, o->dados);
    for (k = 0; k < d.n; ++k) {
        int grade[PIXELS];
        for (p = 0; p < PIXELS; ++p) grade[p] = (int)d.exemplos[k].x[p];
        printf("\nExemplo %d: digito %d\n", k + 1, d.exemplos[k].rotulo);
        imprimir_grades((const int (*)[PIXELS])grade, 1);
        printf("Vetor: ");
        for (p = 0; p < PIXELS; ++p) printf("%d", grade[p]);
        printf("\n");
        if (k < quantidade) {
            Planilha *f = &folhas[k];
            snprintf(f->nome, sizeof(f->nome), "Exemplo_%03d", k + 1);
            snprintf(f->titulo, sizeof(f->titulo), "Digito %d", d.exemplos[k].rotulo);
            snprintf(f->rotulo_resultado, sizeof(f->rotulo_resultado), "Digito %d", d.exemplos[k].rotulo);
            snprintf(f->descricao, sizeof(f->descricao), "Exemplo %d de treino; grade 8 x 6.", k + 1);
            snprintf(f->parametros, sizeof(f->parametros), "Arquivo de dados: %s", o->dados);
            memcpy(f->pixels, grade, sizeof(grade));
        }
    }
    ok = gravar(o->saida, folhas, quantidade);
    free(folhas);
    dados_liberar(&d);
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    Opcoes o;
    Pesos *w;
    Treino t;
    int resultado;

    setlocale(LC_NUMERIC, "C"); /* ponto decimal no XML, mesmo em pt-BR */
    memset(&o, 0, sizeof(o));
    o.dados = "dados/digitos.txt";
    o.modelo = "modelo.bin";
    o.treino.epocas = 6000;
    o.treino.beta = 0.15;
    o.treino.taxa = 0.003;
    o.treino.semente = 42;
    o.a = 0;
    o.b = 8;
    o.passos = 5;
    o.n = 10;
    o.limiar = 0.5;

    if (argc < 2 || !strcmp(argv[1], "ajuda") || !strcmp(argv[1], "--help")) {
        ajuda();
        return argc < 2 ? 2 : 0;
    }
    o.comando = argv[1];
    if (!ler_opcoes(argc, argv, &o)) return 2;

    if (!strcmp(o.comando, "autoteste")) return autoteste();
    if (!strcmp(o.comando, "treinar")) return cmd_treinar(&o);
    if (!strcmp(o.comando, "mostrar")) return cmd_mostrar(&o);
    if (strcmp(o.comando, "reconstruir") && strcmp(o.comando, "misturar") && strcmp(o.comando, "gerar")) {
        fprintf(stderr, "Comando desconhecido: %s. Use: vae ajuda\n", o.comando);
        return 2;
    }

    /* Os demais comandos usam um modelo ja treinado. */
    w = (Pesos *)malloc(sizeof(Pesos));
    if (!w) return 1;
    if (!modelo_carregar(o.modelo, w, &t)) {
        free(w);
        return 1;
    }
    if (!strcmp(o.comando, "reconstruir")) resultado = cmd_reconstruir(&o, w, &t);
    else if (!strcmp(o.comando, "misturar")) resultado = cmd_misturar(&o, w, &t);
    else resultado = cmd_gerar(&o, w, &t);
    free(w);
    return resultado;
}
