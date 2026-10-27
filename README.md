# VAE de dígitos em C, com saída para Excel

Este projeto desenha os dígitos de **0 a 9**, treina um **Variational Autoencoder (VAE)** nessas dez matrizes e mistura dois dígitos no espaço latente. O próprio programa em C grava arquivos `.xlsx` reais, sem precisar de Excel, Python, bibliotecas de aprendizado de máquina ou bibliotecas de planilhas durante a execução.

## Começar no Windows

O pacote inclui `build/vae.exe`, compilado para Windows de 64 bits. Para gerar cinco misturas entre 0 e 8, execute `gerar_exemplo.bat`, ou abra um terminal na pasta do projeto:

```powershell
.\build\vae.exe --a 0 --b 8 --steps 5 --out minha_mistura.xlsx
```

O resultado contém proporções de **0%, 25%, 50%, 75% e 100% do segundo dígito**, uma por aba. O programa treina a rede a cada execução. Com as opções padrão, o treinamento leva poucos segundos na máquina usada para validar este projeto.

Para criar somente uma mistura com partes iguais:

```powershell
.\build\vae.exe --a 2 --b 7 --alpha 0.5 --out mistura_unica.xlsx
```

Para exportar os dez desenhos originais, sem treinamento:

```powershell
.\build\vae.exe --templates meus_modelos.xlsx
```

Os diretórios de destino precisam existir. Escolha um nome novo para cada saída: o programa recusa sobrescrever um arquivo existente.

## Formato e ordem dos números

O arquivo fornecido, `gerar_num.xlsx`, desenha o zero em **A1:F8: 8 linhas × 6 colunas = 48 posições**. O vetor de 18 valores mostrado na solicitação e em H1:Y1 representa apenas as três primeiras linhas. As seis colunas de cada linha precisam ser concatenadas para representar o desenho completo.

O zero foi transcrito do anexo sem alterar nenhuma posição:

```text
0 0 0 0 0 0
0 0 1 1 0 0
0 1 0 0 1 0
0 1 0 0 1 0
0 1 0 0 1 0
0 1 0 0 1 0
0 0 1 1 0 0
0 0 0 0 0 0
```

Seu vetor completo, com separadores apenas para visualizar as linhas, é:

```text
000000 | 001100 | 010010 | 010010 | 010010 | 010010 | 001100 | 000000
```

A ordem de leitura é **A1, B1, C1, D1, E1, F1, A2, B2, ..., F8**. Em C, usando índices iniciados em zero:

```c
indice = linha * 6 + coluna;
vetor[indice] = matriz[linha][coluna];
```

Os outros nove desenhos foram definidos para esta demonstração, na mesma grade. Eles estão explicitamente escritos em `src/digitos.h`, aparecem nas dez abas de `outputs/vae_digitos/modelos_digitos.xlsx` e no arquivo `outputs/vae_digitos/desenhos_e_vetores.txt`.

Cada planilha gerada preserva o posicionamento principal do exemplo:

| Região | Conteúdo |
| --- | --- |
| A1:F8 | Desenho formado por valores numéricos 0 e 1. O fundo fica branco e os traços de 1 ficam azuis, com texto branco. Os zeros ficam ocultos pela formatação. |
| H1:BC1 | Vetor completo de 48 valores, com fórmulas ligadas ao desenho e as mesmas cores automáticas. |
| H3:H8 | Descrição, parâmetros usados e legenda das cores, conforme o tipo da aba. |
| A10 | Identificação do desenho. |
| A12:F19, nas misturas | Probabilidades contínuas produzidas pelo decoder, antes do limiar, com escala de cores de branco a azul escuro. |

Alterar uma célula do desenho no Excel atualiza o vetor. As probabilidades e os desenhos exportados são resultados daquela execução do programa: editar a descrição dos parâmetros no Excel não retreina o VAE. O programa não importa novas planilhas `.xlsx`; o zero do anexo já está incorporado ao código.

### Cores automáticas por valor

O exportador inclui **formatação condicional nativa do Excel**, que acompanha as mudanças dos valores e das fórmulas:

- **Desenho e vetor:** 0 fica branco; 1 fica azul escuro, com texto branco.
- **Probabilidades:** 0 fica branco, 0,5 fica azul claro e 1 fica azul escuro. Os valores intermediários recebem tons interpolados. A partir de 0,85, o texto fica branco para manter o contraste.

Os pontos da escala são fixos em 0, 0,5 e 1. Assim, a mesma probabilidade tem a mesma cor em todas as abas. Você pode editar uma probabilidade no Excel para observar a mudança de cor; isso não altera automaticamente o desenho binário já exportado. Para gerar um novo desenho com outro limiar, execute o programa com `--threshold`.

## Desenhos dos dez dígitos

Em todos os desenhos, `1` é traço e `0` é fundo. As linhas externas em branco são parte do vetor.

```text
   0        1        2        3        4
000000   000000   000000   000000   000000
001100   000100   001100   011100   010010
010010   001100   010010   000010   010010
010010   000100   000010   001100   011110
010010   000100   000100   000010   000010
010010   000100   001000   000010   000010
001100   001110   011110   011100   000010
000000   000000   000000   000000   000000

   5        6        7        8        9
000000   000000   000000   000000   000000
011110   001110   011110   001100   001100
010000   010000   000010   010010   010010
011100   011100   000100   001100   010010
000010   010010   000100   010010   001110
000010   010010   001000   010010   000010
011100   001100   001000   001100   011100
000000   000000   000000   000000   000000
```

## Como o VAE funciona

A rede tem **3.576 parâmetros treináveis** e a arquitetura:

```text
48 células → 32 neurônios tanh → média μ e log-variância de 4 dimensões
                              ↓ reparametrização
                         z de 4 dimensões
                              ↓
               32 neurônios tanh → 48 probabilidades sigmoid
```

Durante o treinamento:

```text
q(z|x) = Normal(μ(x), diag(exp(logvar(x))))
ε ~ Normal(0, I)
z = μ + exp(0.5 × logvar) × ε

BCE = -Σ [xᵢ log(pᵢ) + (1 - xᵢ) log(1 - pᵢ)]
KL  = 0.5 × Σ [μⱼ² + exp(logvarⱼ) - 1 - logvarⱼ]
perda = BCE + β × KL
```

A BCE soma as 48 células e a KL soma as quatro dimensões latentes; o gradiente é calculado pela média das dez amostras do lote. O código usa BCE numericamente estável a partir dos logits, retropropagação analítica, Adam, limitação da norma do gradiente a 10 e aquecimento de β. A log-variância é parametrizada como `6*tanh(raw/6)`; sua derivada também está incluída no treinamento.

O peso padrão é **β = 0,15**, escolhido para esta base pequena. `--beta 1` usa a ponderação unitária de BCE e KL. O aquecimento dura um quarto do treinamento, limitado a 500 épocas.

A mistura acontece **depois de treinar**, interpolando as médias aprendidas:

```text
z_mistura = (1 - alpha) × μ(digito_A) + alpha × μ(digito_B)
probabilidades = decoder(z_mistura)
celula = 1 se probabilidade >= limiar; caso contrário, 0
```

`alpha = 0` reconstrói A, `alpha = 1` reconstrói B e `alpha = 0.5` fica no meio dos dois vetores latentes. Os extremos também passam pelo decoder; o programa não os substitui pelos desenhos originais. Se `--noise` for maior que zero, soma-se ruído gaussiano com esse desvio-padrão ao vetor latente, inclusive nos extremos.

Essa porcentagem descreve a interpolação latente, não a porcentagem de células de cada desenho. Com o limiar, duas proporções diferentes podem produzir o mesmo desenho binário, embora as probabilidades sejam diferentes. Isso ocorre em parte do exemplo entre 2 e 7. No exemplo entre 0 e 8, a mistura de 50% tem um desenho diferente de ambos os extremos.

## Opções e experimentos

Use ponto decimal nos argumentos: `0.5`, e não `0,5`.

| Opção | Padrão | Significado |
| --- | --- | --- |
| `--a`, `--b` | 0 e 8 | Dois dígitos, de 0 a 9. |
| `--steps` | 5 | De 2 a 101 proporções igualmente espaçadas, incluindo os extremos. |
| `--alpha` | — | Uma proporção entre 0 e 1; não usar junto de `--steps`. |
| `--epochs` | 6000 | Número de épocas, de 1 a 100000. |
| `--seed` | 42 | Semente aleatória, de 1 a 4294967295. |
| `--threshold` | 0.5 | Limiar, estritamente entre 0 e 1. Menor valor tende a produzir mais células com 1. |
| `--beta` | 0.15 | Peso positivo da KL, até 10. |
| `--rate` | 0.003 | Taxa de aprendizado positiva, até 0.1. |
| `--noise` | 0 | Desvio-padrão do ruído adicional no espaço latente, de 0 a 5. |
| `--out` | misturas.xlsx | Caminho de saída com extensão `.xlsx`. |

Exemplo com mais etapas e traços mais preenchidos:

```powershell
.\build\vae.exe --a 3 --b 8 --steps 11 --threshold 0.4 --out mistura_3_8.xlsx
```

Exemplo com variação aleatória controlada:

```powershell
.\build\vae.exe --a 0 --b 8 --alpha 0.5 --noise 0.2 --seed 123 --out variacao.xlsx
```

`--print` lista os desenhos e vetores; `--help` mostra todas as opções. `--templates`, `--print` e `--self-test` são modos independentes, usados sem geração de misturas. As mesmas opções e semente reproduzem a saída na mesma compilação. Pequenas diferenças de ponto flutuante podem ocorrer entre compiladores e plataformas.

## Compilar o código

No Windows, execute:

```powershell
.\compilar.bat
```

O script procura GCC/MinGW ou Visual Studio Build Tools com compilador C/C++ e Windows SDK. O código é C11, usa somente a biblioteca padrão e fica separado em:

| Arquivo | Responsabilidade |
| --- | --- |
| `src/digitos.h` | Os dez desenhos e suas dimensões. |
| `src/vae.c` | Encoder, reparametrização, decoder, derivadas, Adam, interpolação e argumentos. |
| `src/xlsx.h`, `src/xlsx.c` | Exportação OOXML dentro de ZIP com CRC-32. |

Para GCC em Linux/macOS, a partir da pasta do projeto:

```sh
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic src/vae.c src/xlsx.c -lm -o vae
./vae --a 0 --b 8 --alpha 0.5 --out mistura.xlsx
```

Também há suporte a CMake:

```sh
cmake -S . -B build-cmake
cmake --build build-cmake --config Release
ctest --test-dir build-cmake -C Release --output-on-failure
```

Para mudar os desenhos, edite `DESENHOS` em `src/digitos.h`, mantendo dez matrizes de oito linhas com seis caracteres binários cada, e recompile. A exportação deste projeto é específica para 8 × 6; outras dimensões exigem adaptar a rede e o exportador.

## Verificação e limites

```powershell
.\build\vae.exe --self-test
python tests\test_outputs.py
```

O primeiro comando verifica as **3.576 derivadas** contra diferenças finitas, incluindo o caminho da reparametrização e da KL. O segundo usa apenas a biblioteca padrão do Python e testa os arquivos ZIP/XML, as fórmulas, as probabilidades, o limiar, a interpolação, a reprodução com semente fixa e a rejeição de argumentos inválidos. Python serve apenas para esse teste adicional.

Na validação em Windows x64 com MSVC, a compilação passou com avisos tratados como erros. Com 6.000 épocas e semente 42, a reconstrução por `z = μ` teve **0 células divergentes entre as 480 células de treino**. Isso mede reconstrução dos próprios modelos; não é uma avaliação de reconhecimento ou generalização. A base contém apenas dez desenhos artificiais, um por classe. Para aprender variedades de escrita à mão, seria necessário fornecer muitos exemplos e adaptar o carregamento e as dimensões. As misturas podem parecer ambíguas, que é justamente o efeito explorado aqui.

O exportador gera arquivos `.xlsx` sem macros e sem compressão ZIP. Ele é específico para esses desenhos e não pretende substituir uma biblioteca geral de Excel.

As 20 abas dos três arquivos de exemplo foram importadas e renderizadas para revisão. Os vetores foram recalculados e comparados com as grades, sem erros de fórmula encontrados. O desenho do zero foi também comparado diretamente com as 48 posições do anexo. Essa conferência usou um leitor e renderizador de planilhas; não foi realizada uma sessão de teste no Excel nativo.

Referência conceitual: D. P. Kingma e M. Welling, [Auto-Encoding Variational Bayes](https://arxiv.org/abs/1312.6114). A arquitetura, os desenhos de 1 a 9 e a implementação deste projeto foram preparados para este exemplo.
