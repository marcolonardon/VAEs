@echo off
setlocal
pushd "%~dp0"
rem Mistura dois digitos no VAE e abre a planilha.
rem Sem argumentos, pergunta os digitos. Tambem aceita: misturar.bat 3 5 [etapas]
rem Com a variavel SEM_ABRIR=1, nao abre o Excel no final (usado em testes).
if exist build\vae.exe goto treinar
call compilar.bat
if errorlevel 1 goto falhou
:treinar
if exist modelo.bin goto valores
echo Treinando o VAE pela primeira vez...
build\vae.exe treinar
if errorlevel 1 goto falhou
:valores
set "A=%~1"
set "B=%~2"
set "PASSOS=%~3"
if "%PASSOS%"=="" set "PASSOS=5"
if not "%B%"=="" goto misturar
echo.
set /p "A=Primeiro digito (0 a 9): "
set /p "B=Segundo digito (0 a 9): "
set /p "PASSOS=Quantas etapas de 0%% a 100%% (2 a 101, Enter = 5): "
:misturar
if not exist resultados mkdir resultados
set "SAIDA=resultados\mistura_%A%_%B%.xlsx"
if exist "%SAIDA%" del "%SAIDA%"
build\vae.exe misturar --a "%A%" --b "%B%" --passos "%PASSOS%" --saida "%SAIDA%"
if errorlevel 1 goto falhou
if "%SEM_ABRIR%"=="" start "" "%SAIDA%"
popd
exit /b 0
:falhou
echo.
echo Algo deu errado. Confira os valores digitados.
popd
pause
exit /b 1
