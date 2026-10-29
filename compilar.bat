@echo off
setlocal
pushd "%~dp0"
if not exist build mkdir build
where gcc >nul 2>nul
if not errorlevel 1 goto gcc
where cl >nul 2>nul
if not errorlevel 1 goto msvc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto ausente
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -all -latest -products * -property installationPath`) do set "VSPATH=%%i"
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" goto ausente
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 goto falhou
:msvc
cl /nologo /O2 /W4 /WX /std:c11 /MT /D_CRT_SECURE_NO_WARNINGS /Fe:build\vae.exe /Fo:build\ src\*.c
if errorlevel 1 goto falhou
goto sucesso
:gcc
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror src\*.c -lm -o build\vae.exe
if errorlevel 1 goto falhou
:sucesso
echo Executavel criado: build\vae.exe
popd
exit /b 0
:ausente
echo Compilador C nao encontrado. Use GCC/MinGW ou Visual Studio Build Tools com C++ e Windows SDK.
:falhou
popd
exit /b 1
