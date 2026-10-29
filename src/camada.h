#ifndef CAMADA_H
#define CAMADA_H

/* Camada densa generica, na notacao da aula:
 *
 *   I_r(v) = sum_u O_{r-1}(u) * W_r(u, v) + b_r(v)
 *   O_r(v) = f_r(I_r(v))
 *
 * W_r tem n_ant linhas (u) e n colunas (v), guardada linha a linha:
 *   W_r(u, v) = W[u * n + v]
 * Assim, uma matriz declarada como double W1[48][24] pode ser passada
 * diretamente como &W1[0][0]. */

typedef enum { LINEAR, TANH, SIGMOIDE } Ativacao;

/* f_r(I) */
double ativacao(Ativacao f, double I);

/* f_r'(I), escrita em funcao da saida O = f_r(I):
 * linear -> 1; tanh -> 1 - O^2; sigmoide -> O (1 - O). */
double derivada(Ativacao f, double O);

/* Calculo direto (forward) de uma camada. Para camada LINEAR, I e O podem
 * ser o mesmo vetor. */
void camada_frente(const double *O_ant, int n_ant,
                   const double *W, const double *b, int n, Ativacao f,
                   double *I, double *O);

/* Retropropagacao de uma camada. Recebe delta(v) = dL/dI_r(v) e SOMA:
 *   dL/dW_r(u, v) = O_{r-1}(u) * delta(v)       em gW
 *   dL/db_r(v)    = delta(v)                    em gb
 *   dL/dO_{r-1}(u) = sum_v W_r(u, v) * delta(v) em dO_ant (se != NULL)
 * Os vetores de destino devem ser zerados por quem chama. */
void camada_tras(const double *O_ant, int n_ant,
                 const double *W, int n, const double *delta,
                 double *gW, double *gb, double *dO_ant);

#endif
