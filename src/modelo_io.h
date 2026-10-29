#ifndef MODELO_IO_H
#define MODELO_IO_H

#include "treino.h"
#include "vae.h"

/* Arquivo binario do modelo treinado:
 *   "VAEC" | versao | PIXELS, N_OCULTA, N_LATENTE | Treino | Pesos
 * Os numeros ficam no formato nativo da maquina (little-endian no Windows
 * x64). O cabecalho impede carregar pesos de outra arquitetura.
 * Retornam 1 no sucesso; 0 com mensagem em stderr. */
int modelo_salvar(const char *caminho, const Pesos *w, const Treino *t);
int modelo_carregar(const char *caminho, Pesos *w, Treino *t);

#endif
