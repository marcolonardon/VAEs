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

/* Colunas Excel, indice baseado em zero: 0=A, 7=H, 54=BC. */
static void coluna(int indice, char *s) {
    char inverso[8]; int n = 0, i;
    do { inverso[n++] = (char)('A' + indice % 26); indice = indice / 26 - 1; } while (indice >= 0);
    for (i = 0; i < n; ++i) s[i] = inverso[n - 1 - i];
    s[n] = 0;
}
static void celula_texto(Texto *t, const char *ref, const char *s, int estilo) {
    adicionar(t, "<c r=\"%s\" s=\"%d\" t=\"inlineStr\"><is><t>", ref, estilo);
    xml_texto(t, s); adicionar(t, "</t></is></c>");
}

static void folha_xml(Texto *t, const Planilha *p) {
    int r, c, i;
    char ref[16], col[8];
    cabecalho(t);
    adicionar(t, "<worksheet xmlns=\"%s\"><dimension ref=\"A1:BC%d\"/>"
        "<sheetViews><sheetView workbookViewId=\"0\" showGridLines=\"1\"><selection activeCell=\"H10\" sqref=\"H10\"/></sheetView></sheetViews>"
        "<sheetFormatPr defaultRowHeight=\"15\"/><cols>"
        "<col min=\"1\" max=\"6\" width=\"4.57\" customWidth=\"1\"/>"
        "<col min=\"7\" max=\"7\" width=\"3\" customWidth=\"1\"/>"
        "<col min=\"8\" max=\"55\" width=\"4.57\" customWidth=\"1\"/></cols><sheetData>", XML_NS, p->tem_probabilidades ? 19 : 10);
    for (r = 0; r < 8; ++r) {
        adicionar(t, "<row r=\"%d\">", r + 1);
        for (c = 0; c < 6; ++c)
            adicionar(t, "<c r=\"%c%d\" s=\"1\"><v>%d</v></c>", 'A' + c, r + 1, p->pixels[r * 6 + c]);
        if (r == 0) for (i = 0; i < 48; ++i) {
            coluna(i + 7, col);
            adicionar(t, "<c r=\"%s1\" s=\"%d\"><f>%c%d</f><v>%d</v></c>",
                col, (i / 6) % 2 ? 3 : 2, 'A' + i % 6, i / 6 + 1, p->pixels[i]);
        }
        if (r == 2) celula_texto(t, "H3", p->descricao, 0);
        if (r == 3) celula_texto(t, "H4", p->parametros, 0);
        if (r == 4) celula_texto(t, "H5", "Vetor de 48 valores: A1:F1, A2:F2, ..., A8:F8.", 0);
        if (r == 5) celula_texto(t, "H6", "Na grade: 1 = traco; 0 = fundo (zero oculto).", 0);
        if (r == 6 && p->tem_probabilidades)
            celula_texto(t, "H7", "A12:F19: probabilidades antes do limiar, em escala de cores fixa de 0 a 1.", 0);
        if (r == 7) celula_texto(t, "H8", p->tem_probabilidades
            ? "Cores automaticas: 0 = branco; 0.5 = azul claro; 1 = azul escuro."
            : "Cores automaticas: 0 = branco; 1 = azul escuro.", 0);
        adicionar(t, "</row>");
    }
    adicionar(t, "<row r=\"10\">"); celula_texto(t, "A10", p->titulo, 5); adicionar(t, "</row>");
    if (p->tem_probabilidades) {
        adicionar(t, "<row r=\"11\">"); celula_texto(t, "A11", "Probabilidades", 0); adicionar(t, "</row>");
        for (r = 0; r < 8; ++r) {
            adicionar(t, "<row r=\"%d\">", r + 12);
            for (c = 0; c < 6; ++c) {
                snprintf(ref, sizeof(ref), "%c%d", 'A' + c, r + 12);
                adicionar(t, "<c r=\"%s\" s=\"4\"><v>%.17g</v></c>", ref, p->probabilidades[r * 6 + c]);
            }
            adicionar(t, "</row>");
        }
    }
    adicionar(t, "</sheetData>");
    /* Regras nativas de formatacao condicional: o Excel atualiza as cores
     * quando o usuario edita os valores ou as formulas sao recalculadas. */
    adicionar(t, "<conditionalFormatting sqref=\"A1:F8 H1:BC1\">"
        "<cfRule type=\"cellIs\" dxfId=\"0\" priority=\"1\" operator=\"greaterThanOrEqual\"><formula>0.5</formula></cfRule>"
        "<cfRule type=\"cellIs\" dxfId=\"1\" priority=\"2\" operator=\"lessThan\"><formula>0.5</formula></cfRule>"
        "</conditionalFormatting>");
    if (p->tem_probabilidades) {
        /* Limites NUMERICOS fixos; a mesma probabilidade recebe a mesma cor
         * em todas as abas, sem reescalar pelo minimo/maximo de cada desenho. */
        adicionar(t, "<conditionalFormatting sqref=\"A12:F19\">"
            "<cfRule type=\"colorScale\" priority=\"3\"><colorScale>"
            "<cfvo type=\"num\" val=\"0\"/><cfvo type=\"num\" val=\"0.5\"/><cfvo type=\"num\" val=\"1\"/>"
            "<color rgb=\"FFFFFFFF\"/><color rgb=\"FF93C5FD\"/><color rgb=\"FF1D4ED8\"/>"
            "</colorScale></cfRule>"
            "<cfRule type=\"cellIs\" dxfId=\"2\" priority=\"4\" operator=\"greaterThanOrEqual\"><formula>0.85</formula></cfRule>"
            "</conditionalFormatting>");
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
        "<fonts count=\"3\"><font><sz val=\"11\"/><name val=\"Arial\"/></font><font><sz val=\"9\"/><name val=\"Arial\"/></font><font><b/><sz val=\"11\"/><name val=\"Arial\"/></font></fonts>"
        "<fills count=\"4\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFE8EBED\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFE7DDF2\"/><bgColor indexed=\"64\"/></patternFill></fill></fills>"
        "<borders count=\"2\"><border><left/><right/><top/><bottom/><diagonal/></border>"
        "<border><left style=\"thin\"><color rgb=\"FF666666\"/></left><right style=\"thin\"><color rgb=\"FF666666\"/></right>"
        "<top style=\"thin\"><color rgb=\"FF666666\"/></top><bottom style=\"thin\"><color rgb=\"FF666666\"/></bottom><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"6\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        "<xf numFmtId=\"164\" fontId=\"0\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"3\" borderId=\"0\" xfId=\"0\" applyFill=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyAlignment=\"1\"><alignment horizontal=\"center\"/></xf>"
        "<xf numFmtId=\"2\" fontId=\"1\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyFont=\"1\" applyBorder=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
        "</cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "<dxfs count=\"3\">"
        "<dxf><font><b/><color rgb=\"FFFFFFFF\"/></font><fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF1D4ED8\"/><bgColor indexed=\"64\"/></patternFill></fill></dxf>"
        "<dxf><font><color rgb=\"FF172033\"/></font><fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFFFFFFF\"/><bgColor indexed=\"64\"/></patternFill></fill></dxf>"
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
