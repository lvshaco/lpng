#include "png.h"
#include "lua.h"
#include "lauxlib.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PNGSIGSIZE 4

struct png_source {
    void *data;
    size_t size;
    size_t offset;
};

static void 
png_read_cb(png_structp png_ptr, png_bytep data, png_size_t length) {
    struct png_source * source = (struct png_source *)png_get_io_ptr(png_ptr);
    if (source->offset + length <= source->size) {
        memcpy(data, source->data+source->offset, length);
        source->offset += length;
    } else {
        png_error(png_ptr, "png_read_cb failed");
    }
}

static int
lload(lua_State *L) {
    int top = lua_gettop(L);
    FILE *fp; 
    struct png_source source;
    if (top == 1) {
        const char *filename = luaL_checkstring(L,1);
        fp = fopen(filename, "rb");
        if (fp == NULL) {
            return luaL_error(L, strerror(errno));
        }
        unsigned char header[PNGSIGSIZE];
        if (fread(header, 1, PNGSIGSIZE, fp) != PNGSIGSIZE) {
            return luaL_error(L, "png invalid");
        }
        if (png_sig_cmp(header, 0, PNGSIGSIZE)) {
            return luaL_error(L, "png sig invalid");
        }
        fseek(fp, 0, SEEK_SET);
    } else if (top == 2) {
        luaL_checktype(L,1,LUA_TLIGHTUSERDATA);
        void *data = lua_touserdata(L,1);
        size_t size = luaL_checkinteger(L,2);
        if (size < PNGSIGSIZE) {
            return luaL_error(L, "png invalid");
        }
        if (png_sig_cmp(data, 0, PNGSIGSIZE)) {
            return luaL_error(L, "png sig invalid");
        }
        source.data = data;
        source.size = size;
        source.offset = 0;
    } else {
        return luaL_error(L, "invalid argument number");
    }
    
    png_structp png_ptr;
    png_infop info_ptr;
    png_uint_32 width, height;
    int bit_depth, color_type, interlace_type;
    int step;//, type;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        return 0;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return 0;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return 0;
    }

    if (top == 1)
        png_init_io(png_ptr, fp);
    else
        png_set_read_fn(png_ptr, (void *)&source, png_read_cb);

    //png_set_sig_bytes(png_ptr, PNGSIGSIZE);

    png_read_info(png_ptr, info_ptr);

    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
            &interlace_type, NULL, NULL);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        bit_depth = 8;
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS) != 0)
        png_set_tRNS_to_alpha(png_ptr);

    if (bit_depth == 16)
        png_set_strip_16(png_ptr);

    if (bit_depth < 8)
        png_set_packing(png_ptr);

    png_read_update_info(png_ptr, info_ptr);
    bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    color_type = png_get_color_type(png_ptr, info_ptr);
    switch (color_type) {
    case PNG_COLOR_TYPE_GRAY:
        // type = TEXTURE_DEPTH;
        step = 1;
        break;
    case PNG_COLOR_TYPE_RGB:
        //type = TEXTURE_RGB;
        step = 3;
        break;
    case PNG_COLOR_TYPE_RGB_ALPHA:
        //type = TEXTURE_RGBA;
        step = 4;
        break;
    default:
        return luaL_error(L, "png color type %d not support", color_type);
    } 

    png_bytep *row_pointers = (png_bytep *)malloc(height * sizeof(png_bytep));

    png_size_t rowbytes = png_get_rowbytes(png_ptr,info_ptr);

    size_t bytes = rowbytes * height;
    uint8_t *buffer = (uint8_t *)malloc(bytes);
    int i;
    for (i=0; i<height; ++i) {
        row_pointers[i] = buffer + i*rowbytes;
    }
    
    png_read_image(png_ptr, row_pointers);

    png_read_end(png_ptr, info_ptr);

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    free(row_pointers);

    switch (color_type) {
    case PNG_COLOR_TYPE_GRAY:
        lua_pushliteral(L,"GRAY");
        break;
    case PNG_COLOR_TYPE_RGB:
        lua_pushliteral(L,"RGB8");
        break;
    case PNG_COLOR_TYPE_RGBA:
        lua_pushliteral(L,"RGBA8");
        break;
    }
    lua_pushinteger(L,width);
    lua_pushinteger(L,height);
    int n = width * height * step;
    lua_createtable(L,n,0);
    for (i=0; i<n; ++i) {
        lua_pushinteger(L, buffer[i]);
        lua_rawseti(L, -2, i+1);
    }

    free(buffer);
    return 4;
}

static int
lsave(lua_State *L) {
    int color_type;
    int step;
    int bit_depth = 8;
    const char *filename = luaL_checkstring(L,1);
    const char *type = luaL_checkstring(L,2);
    if (!strcmp(type, "RGBA8")) {
        color_type = PNG_COLOR_TYPE_RGB_ALPHA;
        step = 4;
    } else if (!strcmp(type, "RGB8")) {
        color_type = PNG_COLOR_TYPE_RGB;
        step = 3;
    } else if (!strcmp(type, "GRAY")) {
        color_type = PNG_COLOR_TYPE_GRAY;
        step = 1;
    } else {
        return luaL_error(L, "png type %s not support", type);
    }

    int width = luaL_checkinteger(L,3);
    int height = luaL_checkinteger(L,4);
  
    luaL_checktype(L,5, LUA_TTABLE);
    int n = lua_rawlen(L,5);
    if (n != width * height * step) {
        return luaL_error(L, "Data number %d invalid, should be %d*%d*%d = %d", n,
                width, height, step, width * height * step);
    }
    
    FILE *fp = fopen(filename, "wb");
    if (fp == NULL) {
        return luaL_error(L, strerror(errno));
    }

    png_structp png_ptr;
    png_infop info_ptr;
    png_colorp palette;
    png_bytep *row_pointers;

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
    if (png_ptr == NULL) {
        return luaL_error(L, "png_create_write_struct fail");
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        png_destroy_write_struct(&png_ptr, NULL);
        return luaL_error(L, "png_destroy_write_struct fail");
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return 0;
    }

    uint8_t *buffer = (uint8_t *)malloc(width * height *step);
    int i;
    for (i=0; i<height *width; ++i) {
        lua_rawgeti(L,5,i*step+1);
        lua_rawgeti(L,5,i*step+2);
        lua_rawgeti(L,5,i*step+3);
        lua_rawgeti(L,5,i*step+4);
        buffer[i*step+0] = (uint8_t)lua_tointeger(L,-4);
        buffer[i*step+1] = (uint8_t)lua_tointeger(L,-3);
        buffer[i*step+2] = (uint8_t)lua_tointeger(L,-2);
        buffer[i*step+3] = (uint8_t)lua_tointeger(L,-1);
        lua_pop(L,4);
    }
    png_init_io(png_ptr, fp);

    png_set_IHDR(png_ptr, info_ptr, width, height, bit_depth, 
            color_type, 
            PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    palette = (png_colorp)png_malloc(png_ptr, PNG_MAX_PALETTE_LENGTH
            * (sizeof (png_color)));
    png_set_PLTE(png_ptr, info_ptr, palette, PNG_MAX_PALETTE_LENGTH);

    png_write_info(png_ptr, info_ptr);

    png_set_packing(png_ptr);

    row_pointers = (png_bytep*)malloc(height * sizeof(png_bytep));

    for (i = 0; i<height; i++)
        row_pointers[i] = buffer + i* width* step;

    png_write_image(png_ptr, row_pointers);

    png_write_end(png_ptr, info_ptr);

    png_free(png_ptr, palette);

    png_destroy_write_struct(&png_ptr, &info_ptr);

    free(row_pointers);
    free(buffer);

    fclose(fp);
    return 0;
}

int 
luaopen_png(lua_State *L) {
	luaL_Reg l[] = {
		{ "load", lload },
		{ "save", lsave },
		{ NULL, NULL },
	};

	luaL_newlib(L,l);
	return 1;
}
