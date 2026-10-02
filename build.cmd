@echo off
setlocal EnableDelayedExpansion
rem C-Otter -- build, testes e execucao num comando so'.
rem
rem Por que existe em .cmd e nao so' em tools\build.ps1: o vcvars64.bat e' um
rem .bat, e o cmd.exe HERDA o ambiente que ele monta. O PowerShell nao -- o
rem build.ps1 precisa rodar "cmd /c vcvars && set" e reimportar as variaveis
rem uma a uma. Aqui a chamada direta basta.
rem
rem Sem o vcvars, o cl.exe e' achado pelo PATH do CMakeCache mas INCLUDE e LIB
rem nao existem, e o build morre em "Cannot open include file: 'cstdint'" ou
rem em "LNK1181: cannot open crypt32.lib".
rem
rem   build                 compila release
rem   build debug           compila debug
rem   build test            compila e roda os testes unitarios
rem   build run             compila e abre a aplicacao
rem   build clean           apaga e reconfigura do zero
rem   build tests-live      testes que precisam de PostgreSQL/MySQL no ar

cd /d "%~dp0"

set "PRESET=win-release"
set "ACTION=build"

rem ---------------------------------------------------------------- argumentos
:parse
if "%~1"=="" goto parsed
if /i "%~1"=="debug"      ( set "PRESET=win-debug" & shift & goto parse )
if /i "%~1"=="release"    ( set "PRESET=win-release" & shift & goto parse )
if /i "%~1"=="test"       ( set "ACTION=test" & shift & goto parse )
if /i "%~1"=="tests"      ( set "ACTION=test" & shift & goto parse )
if /i "%~1"=="tests-live" ( set "ACTION=live" & shift & goto parse )
if /i "%~1"=="run"        ( set "ACTION=run" & shift & goto parse )
if /i "%~1"=="clean"      ( set "ACTION=clean" & shift & goto parse )
rem Ajuda pedida sai com 0; argumento errado sai com 1. Um script que devolve
rem erro ao ser consultado atrapalha quem o encadeia num "&&".
if /i "%~1"=="-h"         ( goto help )
if /i "%~1"=="--help"     ( goto help )
if /i "%~1"=="/?"         ( goto help )
echo [build] argumento desconhecido: %~1
goto usage

:parsed
set "BUILDDIR=build\%PRESET%"

rem ------------------------------------------------------------------- vcvars
rem VSCMD_ARG_TGT_ARCH so' existe depois do vcvars. Testa-lo evita recarregar
rem o ambiente a cada chamada, que custa alguns segundos.
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo [build] vswhere nao encontrado. Visual Studio esta' instalado?
        exit /b 1
    )
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -property installationPath`) do (
        set "VSROOT=%%i"
    )
    if not defined VSROOT (
        echo [build] nenhuma instalacao do Visual Studio encontrada.
        exit /b 1
    )
    if not exist "!VSROOT!\VC\Auxiliary\Build\vcvars64.bat" (
        echo [build] vcvars64.bat ausente em !VSROOT!
        exit /b 1
    )
    call "!VSROOT!\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
    if errorlevel 1 (
        echo [build] vcvars64.bat falhou.
        exit /b 1
    )
    echo [build] MSVC: !VSROOT!
)

rem -------------------------------------------------------------------- clean
if /i "%ACTION%"=="clean" (
    rem O link falha com o exe travado. Derrubar antes e' mais rapido que
    rem decifrar um LNK1168.
    taskkill /im c-otter.exe /f >nul 2>&1
    if exist "%BUILDDIR%" rmdir /s /q "%BUILDDIR%"
    echo [build] %BUILDDIR% apagado.
)

rem ---------------------------------------------------------------- configure
if not exist "%BUILDDIR%\CMakeCache.txt" (
    echo [build] configurando %PRESET% ...
    cmake --preset %PRESET%
    if errorlevel 1 exit /b 1
)

rem -------------------------------------------------------------------- build
rem O link nao sobrescreve um exe em uso, e o sintoma (LNK1168) nao diz que a
rem aplicacao esta' aberta.
taskkill /im c-otter.exe /f >nul 2>&1

echo [build] compilando %PRESET% ...
cmake --build "%BUILDDIR%"
if errorlevel 1 (
    echo [build] FALHOU.
    exit /b 1
)

rem ------------------------------------------------------------------- acoes
if /i "%ACTION%"=="test" (
    echo.
    "%BUILDDIR%\bin\otter_tests.exe"
    exit /b !errorlevel!
)

if /i "%ACTION%"=="live" (
    rem Precisam de PostgreSQL e MySQL no ar; falham sozinhos se nao houver.
    echo.
    "%BUILDDIR%\bin\otter_tests_live.exe"
    exit /b !errorlevel!
)

if /i "%ACTION%"=="run" (
    rem /b e o cd: sem eles a aplicacao morre junto com o cmd que a lanca
    rem quando o script roda sob "cmd /c". Verificado -- a primeira versao
    rem saia com 0 e nao deixava processo nenhum de pe'.
    start "" /b /d "%BUILDDIR%\bin" "%BUILDDIR%\bin\c-otter.exe"
    exit /b 0
)

echo [build] ok: %BUILDDIR%\bin\c-otter.exe
exit /b 0

:help
call :print_usage
exit /b 0

:usage
call :print_usage
exit /b 1

:print_usage
echo.
echo   build [debug^|release] [test^|tests-live^|run^|clean]
echo.
echo     debug ^| release   preset do CMake (padrao: release)
echo     test              roda otter_tests.exe depois de compilar
echo     tests-live        roda os testes contra os bancos locais
echo     run               abre a aplicacao depois de compilar
echo     clean             apaga o diretorio de build e reconfigura
echo.
goto :eof
