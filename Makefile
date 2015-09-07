.PHONY: png.so 

LUA_INC ?=/usr/local/include

SRC := \
libpng/png.c \
libpng/pngerror.c \
libpng/pngget.c \
libpng/pngmem.c \
libpng/pngpread.c \
libpng/pngread.c \
libpng/pngrio.c \
libpng/pngrtran.c \
libpng/pngrutil.c \
libpng/pngset.c \
libpng/pngtrans.c \
libpng/pngwio.c \
libpng/pngwrite.c \
libpng/pngwtran.c \
libpng/pngwutil.c

UNAME=$(shell uname)
SYS=$(if $(filter Linux%,$(UNAME)),linux,\
	    $(if $(filter MINGW%,$(UNAME)),mingw,\
	    $(if $(filter Darwin%,$(UNAME)),macosx,\
	        undefined\
)))

all: $(SYS)

undefined:
	@echo "I can't guess your platform, please do 'make PLATFORM' where PLATFORM is one of these:"
	@echo "      linux mingw macosx"


CFLAGS=-g -Wall #-DHAVE_CONFIG_H

mingw: SHARED := -shared -fPIC
mingw: png.so

macosx: SHARED := -fPIC -dynamiclib -Wl,-undefined,dynamic_lookup
macosx: png.so

png.so: lpng.c $(SRC)
	gcc $(CFLAGS) $(SHARED) -o $@ $^ -Ilibpng -I$(LUA_INC) -lz

clean:
	rm -rf png.so png.so.*
