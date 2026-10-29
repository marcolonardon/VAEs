#include "xlsx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>

#define MAX_FOLHAS 101
#define MAX_ENTRADAS (MAX_FOLHAS + 5)
#define XML_NS "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
#define REL_NS "http://schemas.openxmlformats.org/package/2006/relationships"
#define DOC_REL "http://schemas.openxmlformats.org/officeDocument/2006/relationships"

typedef struct { char *s; size_t n, cap; int ok; } Texto;
typedef struct { char nome[96]; uint32_t crc, tamanho, pos; } Entrada;
typedef struct { FILE *f; Entrada entradas[MAX_ENTRADAS]; int n, ok; } Zip;

static void adicionar(Texto *t, const char *fmt, ...) {
    va_list args;
    int len;
    char *novo;
    if (!t->ok) return;
    va_start(args, fmt);
    len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (len < 0) { t->ok = 0; return; }
    if (t->n + (size_t)len + 1 > t->cap) {
        size_t cap = (t->n + (size_t)len + 1) * 2;
        novo = (char *)realloc(t->s, cap);
        if (!novo) { t->ok = 0; return; }
        t->s = novo; t->cap = cap;
    }
    va_start(args, fmt);
    vsnprintf(t->s + t->n, t->cap - t->n, fmt, args);
    va_end(args);
    t->n += (size_t)len;
}

static void xml_texto(Texto *t, const char *s) {
    for (; *s; ++s) {
        switch (*s) {
            case '&': adicionar(t, "&amp;"); break;
            case '<': adicionar(t, "&lt;"); break;
            case '>': adicionar(t, "&gt;"); break;
            case '"': adicionar(t, "&quot;"); break;
            case '\'': adicionar(t, "&apos;"); break;
            default: adicionar(t, "%c", *s); break;
        }
    }
}

static void limpar(Texto *t) { free(t->s); memset(t, 0, sizeof(*t)); t->ok = 1; }
static void cabecalho(Texto *t) { adicionar(t, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"); }

static uint32_t crc32_bytes(const unsigned char *s, size_t n) {
    uint32_t crc = UINT32_C(0xffffffff);
    size_t i;
    int b;
    for (i = 0; i < n; ++i) {
        crc ^= s[i];
        for (b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
    }
    return ~crc;
}

static void bytes(Zip *z, const void *s, size_t n) {
    if (z->ok && fwrite(s, 1, n, z->f) != n) z->ok = 0;
}
static void u16(Zip *z, uint16_t v) {
    unsigned char b[2] = { (unsigned char)v, (unsigned char)(v >> 8) };
    bytes(z, b, 2);
}
static void u32(Zip *z, uint32_t v) {
    u16(z, (uint16_t)(v & 0xffff)); u16(z, (uint16_t)(v >> 16));
}
static uint32_t posicao(Zip *z) {
    long p = ftell(z->f);
    if (p < 0) { z->ok = 0; return 0; }
    return (uint32_t)p;
}

static void zip_adicionar(Zip *z, const char *nome, Texto *t) {
    Entrada *e;
    if (!t->ok || !z->ok || z->n == MAX_ENTRADAS || t->n > UINT32_MAX || strlen(nome) >= 96) {
        z->ok = 0; limpar(t); return;
    }
    e = &z->entradas[z->n++];
    strcpy(e->nome, nome); e->tamanho = (uint32_t)t->n;
    e->crc = crc32_bytes((const unsigned char *)t->s, t->n); e->pos = posicao(z);
    u32(z, UINT32_C(0x04034b50)); u16(z, 20); u16(z, 0); u16(z, 0);
    u16(z, 0); u16(z, 33); /* Data ZIP fixa: 1980-01-01, para reproducibilidade. */
    u32(z, e->crc); u32(z, e->tamanho); u32(z, e->tamanho);
    u16(z, (uint16_t)strlen(nome)); u16(z, 0);
    bytes(z, nome, strlen(nome)); bytes(z, t->s, t->n);
    limpar(t);
}

static void zip_finalizar(Zip *z) {
    uint32_t inicio = posicao(z), fim;
    int i;
    for (i = 0; i < z->n; ++i) {
        const Entrada *e = &z->entradas[i];
        u32(z, UINT32_C(0x02014b50)); u16(z, 20); u16(z, 20);
        u16(z, 0); u16(z, 0); u16(z, 0); u16(z, 33);
        u32(z, e->crc); u32(z, e->tamanho); u32(z, e->tamanho);
        u16(z, (uint16_t)strlen(e->nome)); u16(z, 0); u16(z, 0);
        u16(z, 0); u16(z, 0); u32(z, 0); u32(z, e->pos);
        bytes(z, e->nome, strlen(e->nome));
    }
    fim = posicao(z);
    u32(z, UINT32_C(0x06054b50)); u16(z, 0); u16(z, 0);
    u16(z, (uint16_t)z->n); u16(z, (uint16_t)z->n);
    u32(z, fim - inicio); u32(z, inicio); u16(z, 0);
}

/* Layout de cada aba (linhas do Excel):
 *   1       titulo
 *   2       descricao
 *   3       rotulos dos blocos
 *   4..11   originais e resultado lado a lado, um bloco 8 x 6 a cada 7 colunas
 *   13..21  probabilidades do resultado (so em resultados do VAE)
 *   23..24  vetor de 48 valores do resultado, com formulas ligadas ao bloco
 *   26..27  parametros e legenda
 * As cores sao gravadas em cada celula (aparecem em qualquer leitor) e
 * tambem como formatacao condicional (o Excel recolore se o valor mudar). */
enum {
    LINHA_TITULO = 1, LINHA_DESCRICAO = 2, LINHA_ROTULOS = 3, LINHA_GRADES = 4,
    LINHA_PROB_ROTULO = 13, LINHA_PROB = 14,
    LINHA_VETOR_ROTULO = 23, LINHA_VETOR = 24,
    LINHA_PARAMETROS = 26, LINHA_LEGENDA = 27
};
/* Estilos definidos em cellXfs (ver escrever_xlsx). */
enum {
    ESTILO_TEXTO = 0, ESTILO_GRADE_0 = 1, ESTILO_GRADE_1 = 2,
    ESTILO_VETOR_0 = 3, ESTILO_VETOR_1 = 4, ESTILO_TITULO = 5,
    ESTILO_PROB = 6, FAIXAS_PROB = 5  /* estilos 6..10: probabilidade 0-0.2, ..., 0.8-1 */
};

/* Colunas Excel, indice baseado em zero: 0=A, 7=H, 47=AV. */
static void coluna(int indice, char *s) {
    char inverso[8]; int n = 0, i;
    do { inverso[n++] = (char)('A' + indice % 26); indice = indice / 26 - 1; } while (indice >= 0);
    for (i = 0; i < n; ++i) s[i] = inverso[n - 1 - i];
    s[n] = 0;
}
/* Referencia da celula i (0..47) do bloco b, a partir da linha inicial. */
static void celula_bloco(int b, int i, int linha_inicial, char *ref) {
    char col[8];
    coluna(7 * b + i % COLUNAS, col);
    sprintf(ref, "%s%d", col, linha_inicial + i / COLUNAS);
}
/* Faixa inteira do bloco b, por exemplo "H4:M11". */
static void faixa_bloco(int b, int linha_inicial, char *s) {
    char inicio[16], fim[16];
    celula_bloco(b, 0, linha_inicial, inicio);
    celula_bloco(b, PIXELS - 1, linha_inicial, fim);
    sprintf(s, "%s:%s", inicio, fim);
}
static void celula_texto(Texto *t, const char *ref, const char *s, int estilo) {
    adicionar(t, "<c r=\"%s\" s=\"%d\" t=\"inlineStr\"><is><t>", ref, estilo);
    xml_texto(t, s); adicionar(t, "</t></is></c>");
}
static void linha_texto(Texto *t, int linha, const char *s, int estilo) {
    char ref[16];
    sprintf(ref, "A%d", linha);
    adicionar(t, "<row r=\"%d\">", linha); celula_texto(t, ref, s, estilo); adicionar(t, "</row>");
}
static int estilo_probabilidade(double p) {
    int faixa = (int)(p * FAIXAS_PROB);
    if (faixa < 0) faixa = 0;
    if (faixa >= FAIXAS_PROB) faixa = FAIXAS_PROB - 1;
    return ESTILO_PROB + faixa;
}

static void folha_xml(Texto *t, const Planilha *p) {
    int blocos = p->n_originais + 1, resultado = p->n_originais;
    int r, c, b, i;
    char ref[16], faixa[32], texto[160];
    cabecalho(t);
    adicionar(t, "<worksheet xmlns=\"%s\"><dimension ref=\"A1:AV%d\"/>"
        "<sheetViews><sheetView workbookViewId=\"0\" showGridLines=\"0\"><selection activeCell=\"A1\" sqref=\"A1\"/></sheetView></sheetViews>"
        "<sheetFormatPr defaultRowHeight=\"15\"/><cols><col min=\"1\" max=\"48\" width=\"4.57\" customWidth=\"1\"/></cols><sheetData>",
        XML_NS, LINHA_LEGENDA);
    linha_texto(t, LINHA_TITULO, p->titulo, ESTILO_TITULO);
    linha_texto(t, LINHA_DESCRICAO, p->descricao, ESTILO_TEXTO);

    /* Rotulos e grades: originais primeiro, resultado por ultimo. */
    adicionar(t, "<row r=\"%d\">", LINHA_ROTULOS);
    for (b = 0; b < blocos; ++b) {
        celula_bloco(b, 0, LINHA_ROTULOS, ref);
        celula_texto(t, ref, b < resultado ? p->rotulo_original[b] : p->rotulo_resultado, ESTILO_TITULO);
    }
    adicionar(t, "</row>");
    for (r = 0; r < LINHAS; ++r) {
        adicionar(t, "<row r=\"%d\">", LINHA_GRADES + r);
        for (b = 0; b < blocos; ++b) for (c = 0; c < COLUNAS; ++c) {
            int v = b < resultado ? p->originais[b][r * COLUNAS + c] : p->pixels[r * COLUNAS + c];
            celula_bloco(b, r * COLUNAS + c, LINHA_GRADES, ref);
            adicionar(t, "<c r=\"%s\" s=\"%d\"><v>%d</v></c>", ref, v ? ESTILO_GRADE_1 : ESTILO_GRADE_0, v);
        }
        adicionar(t, "</row>");
    }

    if (p->tem_probabilidades) {
        linha_texto(t, LINHA_PROB_ROTULO, "Probabilidades do resultado, antes do limiar", ESTILO_TITULO);
        for (r = 0; r < LINHAS; ++r) {
            adicionar(t, "<row r=\"%d\">", LINHA_PROB + r);
            for (c = 0; c < COLUNAS; ++c) {
                double prob = p->probabilidades[r * COLUNAS + c];
                celula_bloco(0, r * COLUNAS + c, LINHA_PROB, ref);
                adicionar(t, "<c r=\"%s\" s=\"%d\"><v>%.17g</v></c>", ref, estilo_probabilidade(prob), prob);
            }
            adicionar(t, "</row>");
        }
    }

    /* Vetor de 48 valores, lido linha a linha do bloco do resultado. */
    faixa_bloco(resultado, LINHA_GRADES, faixa);
    snprintf(texto, sizeof(texto), "Vetor de 48 valores do resultado (%s, linha a linha)", faixa);
    linha_texto(t, LINHA_VETOR_ROTULO, texto, ESTILO_TITULO);
    adicionar(t, "<row r=\"%d\">", LINHA_VETOR);
    for (i = 0; i < PIXELS; ++i) {
        char col[8], origem[16];
        coluna(i, col);
        celula_bloco(resultado, i, LINHA_GRADES, origem);
        adicionar(t, "<c r=\"%s%d\" s=\"%d\"><f>%s</f><v>%d</v></c>", col, LINHA_VETOR,
            p->pixels[i] ? ESTILO_VETOR_1 : ESTILO_VETOR_0, origem, p->pixels[i]);
    }
    adicionar(t, "</row>");
    linha_texto(t, LINHA_PARAMETROS, p->parametros, ESTILO_TEXTO);
    linha_texto(t, LINHA_LEGENDA, p->tem_probabilidades
        ? "Cores: 1 = azul (traco); 0 = branco (fundo). Probabilidades: quanto mais escuro, mais perto de 1."
        : "Cores: 1 = azul (traco); 0 = branco (fundo).", ESTILO_TEXTO);
    adicionar(t, "</sheetData>");

    /* Formatacao condicional: mesmas cores, recalculadas pelo Excel. */
    adicionar(t, "<conditionalFormatting sqref=\"");
    for (b = 0; b < blocos; ++b) {
        faixa_bloco(b, LINHA_GRADES, faixa);
        adicionar(t, "%s ", faixa);
    }
    adicionar(t, "A%d:AV%d\">"
        "<cfRule type=\"cellIs\" dxfId=\"0\" priority=\"1\" operator=\"greaterThanOrEqual\"><formula>0.5</formula></cfRule>"
        "<cfRule type=\"cellIs\" dxfId=\"1\" priority=\"2\" operator=\"lessThan\"><formula>0.5</formula></cfRule>"
        "</conditionalFormatting>", LINHA_VETOR, LINHA_VETOR);
    if (p->tem_probabilidades) {
        /* Limites NUMERICOS fixos; a mesma probabilidade recebe a mesma cor
         * em todas as abas, sem reescalar pelo minimo/maximo de cada desenho. */
        faixa_bloco(0, LINHA_PROB, faixa);
        adicionar(t, "<conditionalFormatting sqref=\"%s\">"
            "<cfRule type=\"colorScale\" priority=\"3\"><colorScale>"
            "<cfvo type=\"num\" val=\"0\"/><cfvo type=\"num\" val=\"0.5\"/><cfvo type=\"num\" val=\"1\"/>"
            "<color rgb=\"FFFFFFFF\"/><color rgb=\"FF93C5FD\"/><color rgb=\"FF1D4ED8\"/>"
            "</colorScale></cfRule>"
            "<cfRule type=\"cellIs\" dxfId=\"2\" priority=\"4\" operator=\"greaterThanOrEqual\"><formula>0.6</formula></cfRule>"
            "</conditionalFormatting>", faixa);
    }
    adicionar(t, "<pageMargins left=\"0.5\" right=\"0.5\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/></worksheet>");
}

int escrever_xlsx(const char *caminho, const Planilha *folhas, int quantidade) {
    Zip z = {0}; Texto t = {0}; int i; char nome[96]; FILE *existe;
    if (quantidade < 1 || quantidade > MAX_FOLHAS) return 0;
    existe = fopen(caminho, "rb");
    if (existe) { fclose(existe); fprintf(stderr, "Arquivo ja existe: %s. Escolha outro nome.\n", caminho); return 0; }
    z.f = fopen(caminho, "wb");
    if (!z.f) { perror(caminho); return 0; }
    z.ok = t.ok = 1;
    cabecalho(&t);
    adicionar(&t, "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>");
    for (i = 1; i <= quantidade; ++i) adicionar(&t, "<Override PartName=\"/xl/worksheets/sheet%d.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>", i);
    adicionar(&t, "</Types>"); zip_adicionar(&z, "[Content_Types].xml", &t);
    cabecalho(&t); adicionar(&t, "<Relationships xmlns=\"%s\"><Relationship Id=\"rId1\" Type=\"%s/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>", REL_NS, DOC_REL);
    zip_adicionar(&z, "_rels/.rels", &t);
    cabecalho(&t); adicionar(&t, "<workbook xmlns=\"%s\" xmlns:r=\"%s\"><sheets>", XML_NS, DOC_REL);
    for (i = 0; i < quantidade; ++i) {
        adicionar(&t, "<sheet name=\""); xml_texto(&t, folhas[i].nome);
        adicionar(&t, "\" sheetId=\"%d\" r:id=\"rId%d\"/>", i + 1, i + 1);
    }
    adicionar(&t, "</sheets><calcPr calcId=\"191029\" fullCalcOnLoad=\"1\"/></workbook>");
    zip_adicionar(&z, "xl/workbook.xml", &t);
    cabecalho(&t); adicionar(&t, "<Relationships xmlns=\"%s\">", REL_NS);
    for (i = 1; i <= quantidade; ++i) adicionar(&t, "<Relationship Id=\"rId%d\" Type=\"%s/worksheet\" Target=\"worksheets/sheet%d.xml\"/>", i, DOC_REL, i);
    adicionar(&t, "<Relationship Id=\"rId%d\" Type=\"%s/styles\" Target=\"styles.xml\"/></Relationships>", quantidade + 1, DOC_REL);
    zip_adicionar(&z, "xl/_rels/workbook.xml.rels", &t);
    cabecalho(&t); adicionar(&t,
        "<styleSheet xmlns=\"%s\"><numFmts count=\"1\"><numFmt numFmtId=\"164\" formatCode=\"0;-0;;@\"/></numFmts>"
        "<fonts count=\"5\"><font><sz val=\"11\"/><name val=\"Arial\"/></font><font><sz val=\"9\"/><name val=\"Arial\"/></font>"
        "<font><b/><sz val=\"11\"/><name val=\"Arial\"/></font><font><b/><sz val=\"11\"/><color rgb=\"FFFFFFFF\"/><name val=\"Arial\"/></font>"
        "<font><sz val=\"9\"/><color rgb=\"FFFFFFFF\"/><name val=\"Arial\"/></font></fonts>"
        /* Preenchimentos: 2 = branco, 3 = azul do traco, 4..8 = escala das probabilidades. */
        "<fills count=\"9\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFFFFFFF\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF1D4ED8\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFE9F3FF\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFBEDCFE\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF93C5FD\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF6495EE\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF3566DF\"/><bgColor indexed=\"64\"/></patternFill></fill></fills>"
        "<borders count=\"2\"><border><left/><right/><top/><bottom/><diagonal/></border>"
        "<border><left style=\"thin\"><color rgb=\"FF999999\"/></left><right style=\"thin\"><color rgb=\"FF999999\"/></right>"
        "<top style=\"thin\"><color rgb=\"FF999999\"/></top><bottom style=\"thin\"><color rgb=\"FF999999\"/></bottom><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        /* Na ordem do enum ESTILO_*: texto, grade 0/1, vetor 0/1, titulo, 5 faixas de probabilidade. */
        "<cellXfs count=\"11\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        "<xf numFmtId=\"164\" fontId=\"0\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\"/></xf>"
        "<xf numFmtId=\"164\" fontId=\"3\" fillId=\"3\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"3\" fillId=\"3\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
        "<xf numFmtId=\"2\" fontId=\"1\" fillId=\"4\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\"/>"
        "<xf numFmtId=\"2\" fontId=\"1\" fillId=\"5\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\"/>"
        "<xf numFmtId=\"2\" fontId=\"1\" fillId=\"6\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\"/>"
        "<xf numFmtId=\"2\" fontId=\"4\" fillId=\"7\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\"/>"
        "<xf numFmtId=\"2\" fontId=\"4\" fillId=\"8\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\"/>"
        "</cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "<dxfs count=\"3\">"
        /* Em dxf (formatacao condicional) a cor do preenchimento solido vai em bgColor. */
        "<dxf><font><b/><color rgb=\"FFFFFFFF\"/></font><fill><patternFill patternType=\"solid\"><bgColor rgb=\"FF1D4ED8\"/></patternFill></fill></dxf>"
        "<dxf><font><color rgb=\"FF172033\"/></font><fill><patternFill patternType=\"solid\"><bgColor rgb=\"FFFFFFFF\"/></patternFill></fill></dxf>"
        "<dxf><font><color rgb=\"FFFFFFFF\"/></font></dxf>"
        "</dxfs></styleSheet>", XML_NS);
    zip_adicionar(&z, "xl/styles.xml", &t);
    for (i = 0; i < quantidade; ++i) {
        folha_xml(&t, &folhas[i]); snprintf(nome, sizeof(nome), "xl/worksheets/sheet%d.xml", i + 1);
        zip_adicionar(&z, nome, &t);
    }
    zip_finalizar(&z);
    if (fclose(z.f) != 0) z.ok = 0;
    limpar(&t);
    if (!z.ok) { fprintf(stderr, "Erro ao gravar %s. Arquivo incompleto removido.\n", caminho); remove(caminho); }
    return z.ok;
}
