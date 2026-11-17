# Shiyin / ?? -- Windows 95 music player
#
# 32-bit PE, no CRT dependency (entry point = sy_entry), subsystem 4.0,
# OS version 4.0, i486 code generation, x87 maths.  Run: build.bat

MINGW ?= tools/mingw32
CC    := $(MINGW)/bin/gcc.exe

SRC := src/main.c src/ui.c src/audio.c src/decode.c src/tags.c src/image.c src/assoc.c src/lrc.c src/flac.c src/util.c
OBJ := $(SRC:src/%.c=build/%.o)
OUT := build/Shiyin.exe

CFLAGS := -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter ^
          -march=i486 -mtune=i486 -mno-sse -mno-mmx -mfpmath=387 ^
          -ffreestanding -fno-builtin -fno-stack-protector ^
          -fno-asynchronous-unwind-tables -fno-unwind-tables ^
          -fno-tree-loop-distribute-patterns -fno-ident ^
          -finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE ^
          -DWINVER=0x0400 -D_WIN32_WINNT=0x0400 -D_WIN32_IE=0x0400 ^
          -Isrc -Ithird_party

LDFLAGS := -nostdlib -nostartfiles ^
           -Wl,--entry=sy_entry ^
           -Wl,--subsystem,windows:4.0 ^
           -Wl,--major-subsystem-version,4 -Wl,--minor-subsystem-version,0 ^
           -Wl,--major-os-version,4 -Wl,--minor-os-version,0 ^
           -Wl,--major-image-version,1 -Wl,--minor-image-version,0

LIBS := -lwinmm -lgdi32 -luser32 -lkernel32 -lcomdlg32 -lshell32 -ladvapi32 -lgcc

all: $(OUT)

$(OUT): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

build/%.o: src/%.c src/shiyin.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	del /q build\*.o build\Shiyin.exe 2>nul

.PHONY: all clean
