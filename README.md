# VAE de dígitos em C

Autoencoder variacional (VAE) escrito em C11 puro, só com a biblioteca padrão. Ele aprende os dígitos de 0 a 9 desenhados em uma grade de 8 × 6 e depois:

- **reconstrói** cada dígito;
- **mistura** dois dígitos no espaço latente;
- **gera** dígitos novos sorteando `z ~ N(0, I)`.

Os resultados aparecem no terminal e, opcionalmente, em planilhas `.xlsx` no mesmo formato da `gerar_num.xlsx` da aula.

## Arquitetura

A rede do rascunho é `48 → 24 → 12 → 24 → 48`. Para virar VAE, a camada latente determinística é trocada por dois vetores de 12 parâmetros:

```text
            W1 (48×24)          W2mu (24×12)
x (48) ───────────────▶ O1 (24) ──────────────▶ μ (12)
                        tanh    └─────────────▶ log σ² (12)
                                 W2lv (24×12)

z = μ + σ ⊙ ε,   σ = exp(log σ² / 2),   ε ~ N(0, I)

            W3 (12×24)          W4 (24×48)
z (12) ───────────────▶ O3 (24) ──────────────▶ x̂ = O4 (48)
                        tanh                     sigmoide
```

Cada camada segue a notação da aula ([src/camada.c](src/camada.c)):

```text
I_r(v) = Σ_u O_{r-1}(u) · W_r(u, v) + b_r(v)        O_r(v) = f_r(I_r(v))        O_0 = x
```

No código, `W1[i][j]` é exatamente `W1(i, j)` ([src/vae.h](src/vae.h)). A perda é

```text
BCE  = −Σ_p [ x(p) log x̂(p) + (1 − x(p)) log(1 − x̂(p)) ]
KL   = ½ Σ_l [ μ(l)² + σ(l)² − 1 − log σ(l)² ]
perda = BCE + β · KL
```

O treino usa lote completo, retropropagação analítica ([src/vae.c](src/vae.c)), Adam e limite de norma do gradiente ([src/treino.c](src/treino.c)). O β sobe de 0 até o valor escolhido no primeiro quarto do treino. São 3.288 parâmetros.

## Dados

[dados/digitos.txt](dados/digitos.txt) é o arquivo `num_vaes.txt` da aula: uma linha por exemplo, com 48 valores 0/1. A ordem de leitura é A1, B1, …, F1, A2, …, F8, ou seja, `x[linha * 6 + coluna]`. A 1ª linha é o dígito 0, a 2ª é o 1, e assim por diante.

Para acrescentar mais exemplos, basta adicionar linhas. Também dá para pôr o rótulo antes dos 48 valores (49 números na linha), e linhas iniciadas por `#` são comentários.

## Compilar

No Windows, com Visual Studio Build Tools ou MinGW:

```powershell
.\compilar.bat
```

Em Linux/macOS:

```sh
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic src/*.c -lm -o vae
```

Também há um [CMakeLists.txt](CMakeLists.txt).

## Usar

```powershell
.\build\vae.exe treinar                       # treina e grava modelo.bin (menos de 1 s)
.\build\vae.exe reconstruir                   # original × reconstrução de cada exemplo
.\build\vae.exe misturar --a 0 --b 8 --passos 5 --saida misturas.xlsx
.\build\vae.exe misturar --a 2 --b 7 --alpha 0.5
.\build\vae.exe gerar --n 10 --semente 7 --saida gerados.xlsx
.\build\vae.exe mostrar --saida digitos.xlsx  # exporta os dados de treino
.\build\vae.exe autoteste                     # confere as 3.288 derivadas
.\build\vae.exe ajuda                         # todas as opções
```

O `gerar_exemplo.bat` faz tudo de uma vez e grava as planilhas em `resultados\`.

Opções principais:

| Opção | Padrão | Uso |
| --- | --- | --- |
| `--dados` | `dados/digitos.txt` | Arquivo de exemplos. |
| `--modelo` | `modelo.bin` | Pesos treinados (gravados por `treinar`, lidos pelos outros comandos). |
| `--saida` | — | Também grava `.xlsx`. Não sobrescreve arquivos existentes. |
| `--epocas`, `--beta`, `--taxa` | 6000, 0.15, 0.003 | Hiperparâmetros do treino. |
| `--semente` | 42 | Inicialização e ε do treino; sorteios de `gerar` e `--ruido`. |
| `--a`, `--b` | 0, 8 | Dígitos da mistura. |
| `--passos` / `--alpha` | 5 / — | Várias misturas de 0% a 100% de B, ou uma só. |
| `--n` | 10 | Quantos dígitos `gerar` cria. |
| `--limiar` | 0.5 | Célula = 1 se a probabilidade for ≥ limiar. |
| `--ruido` | 0 | Desvio-padrão somado a `z` na mistura. |

Use ponto decimal (`0.5`, não `0,5`).

### Mistura

```text
μ_A = média de μ(x) dos exemplos do dígito A        (idem para B)
z   = (1 − α) · μ_A + α · μ_B
x̂   = decoder(z)
```

A porcentagem se refere à posição no espaço latente, não à quantidade de células de cada desenho. A troca de um dígito para o outro pode ser brusca.

## Planilhas

Cada aba repete o layout da `gerar_num.xlsx`:

| Região | Conteúdo |
| --- | --- |
| A1:F8 | Desenho 0/1; o 1 fica azul. |
| H1:BC1 | Vetor de 48 valores, com fórmulas ligadas ao desenho. |
| H3:H8 | Descrição e parâmetros do modelo. |
| Linhas 11–19 | Comparação lado a lado (só em resultados do VAE), descrita abaixo. |

Nas abas de resultado, a faixa da linha 12 põe as matrizes de antes da operação ao lado do que o VAE produziu. Cada bloco 8 × 6 começa 7 colunas depois do anterior:

| Comando | Blocos, da esquerda para a direita |
| --- | --- |
| `misturar` | Original A · Original B · Resultado · Probabilidades |
| `reconstruir` | Original · Resultado · Probabilidades |
| `gerar` | Exemplo de treino mais parecido · Resultado · Probabilidades |

O bloco "Resultado" usa fórmulas ligadas a A1:F8. Quando há vários exemplos do mesmo dígito, `misturar` mostra o primeiro deles. O terminal mostra as mesmas matrizes lado a lado.

A pasta [exemplos/](exemplos) tem planilhas geradas com os parâmetros padrão.

## Organização

| Arquivo | Responsabilidade |
| --- | --- |
| [src/grade.h](src/grade.h) | Dimensões da grade (8 × 6 = 48). |
| [src/dados.c](src/dados.c) | Leitura e validação do arquivo de exemplos. |
| [src/camada.c](src/camada.c) | Camada densa genérica: `I_r`, `O_r`, `f_r` e retropropagação. |
| [src/vae.c](src/vae.c) | Encoder, reparametrização, decoder, perda e gradientes. |
| [src/treino.c](src/treino.c) | Adam, avaliação e autoteste por diferenças finitas. |
| [src/modelo_io.c](src/modelo_io.c) | Gravar e carregar os pesos (com verificação da arquitetura). |
| [src/xlsx.c](src/xlsx.c) | Exportação `.xlsx` (OOXML em ZIP sem compressão). |
| [src/main.c](src/main.c) | Comandos e opções da linha de comando. |

## Testes

```powershell
.\build\vae.exe autoteste
python tests\test_outputs.py
```

O autoteste compara cada derivada analítica com `[L(θ+h) − L(θ−h)] / 2h`. O script Python (só biblioteca padrão) roda o programa e confere:

- os argumentos e os arquivos inválidos;
- o formato das planilhas e se a semente fixa reproduz os resultados;
- se o treino padrão reconstrói as 480 células sem erro;
- se as misturas começam em A e terminam em B.

## Limites

São só dez desenhos, um por dígito. A rede decora esses exemplos; isso não é uma avaliação de generalização. Para aprender variações de escrita, acrescente mais exemplos de cada dígito em `dados/digitos.txt`.

Referência: D. P. Kingma e M. Welling, [Auto-Encoding Variational Bayes](https://arxiv.org/abs/1312.6114).
