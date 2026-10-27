@echo off
setlocal
pushd "%~dp0"
if exist build\vae.exe goto executar
call compilar.bat
if errorlevel 1 goto falhou
:executar
if not exist resultados mkdir resultados
build\vae.exe --a 0 --b 8 --steps 5 --out resultados\misturas_0_8.xlsx
if errorlevel 1 goto falhou
echo Abra resultados\misturas_0_8.xlsx para ver as cinco misturas.
popd
pause
exit /b 0
:falhou
popd
pause
exit /b 1
