@echo off
setlocal
pushd "%~dp0"
if exist build\vae.exe goto treinar
call compilar.bat
if errorlevel 1 goto falhou
:treinar
if exist modelo.bin goto executar
build\vae.exe treinar
if errorlevel 1 goto falhou
:executar
if not exist resultados mkdir resultados
if exist resultados\misturas_0_8.xlsx del resultados\misturas_0_8.xlsx
if exist resultados\gerados.xlsx del resultados\gerados.xlsx
build\vae.exe misturar --a 0 --b 8 --passos 5 --saida resultados\misturas_0_8.xlsx
if errorlevel 1 goto falhou
build\vae.exe gerar --n 10 --saida resultados\gerados.xlsx
if errorlevel 1 goto falhou
echo Abra a pasta resultados para ver as planilhas.
popd
pause
exit /b 0
:falhou
popd
pause
exit /b 1
