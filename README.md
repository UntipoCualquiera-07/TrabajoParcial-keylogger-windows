\# Keylogger Evasivo en Windows



Trabajo parcial del curso \*\*Hacking Ético\*\* — Sección 16155  

Universidad Peruana de Ciencias Aplicadas (UPC)



\## Integrantes



\- Rodrigo Aragon, Alcalde Cabanillas (U202514971)

\- Sebastian Victor, Reynoso Roman (U20251F532)

\- Custodio Chavarría Gianpul Jesus



\## Descripción



Este proyecto demuestra el funcionamiento de un keylogger con técnicas de evasión en Windows, incluyendo:



\- Ejecución de shellcode directamente en memoria (sin tocar disco)

\- Persistencia mediante la clave de registro `HKCU\\Run`

\- Captura de teclado mediante hooks globales (`WH\_KEYBOARD\_LL`)

\- Captura de pantalla con GDI+

\- Exfiltración a Telegram mediante Google Apps Script como relay



Todo el desarrollo y las pruebas se realizaron en un entorno controlado con fines exclusivamente académicos.



\## Estructura del proyecto



| Archivo | Descripción |

|---------|-------------|

| `kl.cpp` | Módulo principal del keylogger |

| `c2.h` | Comunicación HTTP con Google Apps Script |

| `screenshot.h` | Captura de pantalla con GDI+ |

| `stager.cpp` | Stager que descarga y ejecuta el shellcode |

| `build.bat` | Script de compilación automatizada |

| `codigo.gs` | Google Apps Script (relay a Telegram) |

| `index.html` | Página de phishing que simula Windows Update |



\## Cadena de ataque



1\. \*\*Phishing:\*\* la víctima abre una página falsa de Windows Update.

2\. \*\*Descarga:\*\* se descarga `WindowsUpdate.exe` (stager).

3\. \*\*Persistencia:\*\* el stager se copia en `%APPDATA%` y se registra en `HKCU\\Run`.

4\. \*\*Ejecución en memoria:\*\* descarga `image.bin` y lo ejecuta con `VirtualAlloc`.

5\. \*\*Exfiltración:\*\* las teclas y capturas se envían a Telegram vía Google Apps Script.



\## Compilación



```bat

build.bat

