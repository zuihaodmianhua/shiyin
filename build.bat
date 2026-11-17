@echo off
rem ===========================================================================
rem  Shiyin / Shiyin -- build script for Windows 95 target
rem
rem  Produces build\Shiyin.exe: 32-bit PE, subsystem 4.0, no CRT dependency.
rem  Toolchain: mingw-w64 i686 (msvcrt) in tools\mingw32, or any i686 gcc.
rem ===========================================================================
setlocal
set ROOT=%~dp0
set CC=%ROOT%tools\mingw32\bin\gcc.exe
if not exist "%CC%" set CC=gcc
if not exist "%ROOT%build" mkdir "%ROOT%build"

set CFLAGS=-std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter ^
 -march=i486 -mtune=i486 -mno-sse -mno-mmx -mfpmath=387 ^
 -ffreestanding -fno-builtin -fno-stack-protector ^
 -fno-asynchronous-unwind-tables -fno-unwind-tables ^
 -fno-tree-loop-distribute-patterns -fno-ident ^
 -finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE ^
 -DWINVER=0x0400 -D_WIN32_WINNT=0x0400 -D_WIN32_IE=0x0400 ^
 -I"%ROOT%src" -I"%ROOT%third_party"

set LDFLAGS=-nostdlib -nostartfiles ^
 -Wl,--entry=sy_entry ^
 -Wl,--subsystem,windows:4.0 ^
 -Wl,--major-subsystem-version,4 -Wl,--minor-subsystem-version,0 ^
 -Wl,--major-os-version,4 -Wl,--minor-os-version,0 ^
 -Wl,--major-image-version,1 -Wl,--minor-image-version,0 -Wl,--disable-dynamicbase -Wl,--nxcompat=no

set LIBS=-lwinmm -lgdi32 -luser32 -lkernel32 -lcomdlg32 -lshell32 -ladvapi32 -lgcc

set SRC=main ui audio decode tags image assoc lrc flac util

set RC=%ROOT%tools\mingw32\bin\windres.exe
if not exist "%RC%" set RC=windres

echo [rc] app.rc
"%RC%" -I"%ROOT%res" -i "%ROOT%res\app.rc" -o "%ROOT%build\app_res.o" || echo (icon skipped)

for %%f in (%SRC%) do (
  echo [cc] %%f.c
  "%CC%" %CFLAGS% -c -o "%ROOT%build\%%f.o" "%ROOT%src\%%f.c" || goto :fail
)

echo [ld] Shiyin.exe
"%CC%" %LDFLAGS% -o "%ROOT%build\Shiyin.exe" ^
 "%ROOT%build\main.o" "%ROOT%build\ui.o" "%ROOT%build\audio.o" ^
 "%ROOT%build\decode.o" "%ROOT%build\tags.o" "%ROOT%build\image.o" ^
 "%ROOT%build\assoc.o" "%ROOT%build\lrc.o" "%ROOT%build\flac.o" "%ROOT%build\util.o" "%ROOT%build\app_res.o" %LIBS% || goto :fail

echo.
echo OK: %ROOT%build\Shiyin.exe
exit /b 0

:fail
echo.
echo BUILD FAILED
exit /b 1

