/*
 * texturepack_utils - https://github.com/Rosalie241/texturepack_utils
 *  Copyright (C) 2023 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef _WIN32
#include <linux/limits.h>
#endif /* _WIN32 */
#include <png.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <zlib.h>

#define UNDEFINED_1            0x08000000
#define TXCACHE_FORMAT_VERSION UNDEFINED_1

#define GL_TEXFMT_GZ 0x80000000

typedef struct
{
    union
    {
        uint16_t _formatsize;
        struct
        {
            uint8_t _format;
            uint8_t _size;
        };
    };
} N64FormatSize;

struct GHQTexInfo
{
    uint8_t*      data;
    int32_t       width;
    int32_t       height;
    uint32_t      format;
    uint16_t      texture_format;
    uint16_t      pixel_type;
    uint8_t       is_hires_tex;
    N64FormatSize n64_format_size;
};

bool read_info(FILE* file, bool oldFormat, struct GHQTexInfo* info)
{
    uint32_t dataSize = 0;
#define FREAD(x) fread(&x, sizeof(x), 1, file)
    FREAD(info->width);
    FREAD(info->height);
    FREAD(info->format);
    FREAD(info->texture_format);
    FREAD(info->pixel_type);
    FREAD(info->is_hires_tex);
    if (!oldFormat)
    {
        FREAD(info->n64_format_size._formatsize);
    }
    FREAD(dataSize);
#undef FREAD

    info->data = malloc(dataSize);
    if (info->data == NULL)
    {
        return false;
    }

    fread(info->data, dataSize, 1, file);

    if (info->format & GL_TEXFMT_GZ)
    {
        void* dest     = NULL;
        uLongf destLen = dataSize * 2;
        int ret        = 0;
        do
        {
            dest = malloc(destLen);
            if (dest == NULL)
            {
                return false;
            }

            ret = uncompress(dest, &destLen, info->data, dataSize);
            if (ret == Z_BUF_ERROR)
            { /* increase buffer size as needed */
                free(dest);
                destLen = destLen + destLen;
            }
            else if (ret != Z_OK)
            {
                return false;
            }
        } while (ret == Z_BUF_ERROR);

        free(info->data);
        info->data = dest;
    }

    return true;
}

void get_filename_from_info(uint64_t checksum, bool oldFormat, struct GHQTexInfo* info, char* ident, char* filename)
{
    const uint32_t chksum    = checksum & 0xffffffff;
    const uint32_t palchksum = checksum >> 32;
    const uint32_t n64fmt    = info->n64_format_size._format;
    const uint32_t n64fmt_sz = info->n64_format_size._size;

    if (oldFormat)
    {
        if (palchksum == 0)
        {
            sprintf(filename, "%s#%08X#%01X#%01X_all.png", ident, chksum, 3, 0);
        }
        else
        {
            sprintf(filename, "%s#%08X#%01X#%01X#%08X_ciByRGBA.png", ident, chksum, 3, 0, palchksum);
        }
    }
    else
    {
        if (n64fmt == 0x02)
        {
            sprintf(filename, "%s#%08X#%01X#%01X_all.png", ident, chksum, n64fmt, n64fmt_sz);
        }
        else
        {
            sprintf(filename, "%s#%08X#%01X#%01X#%08X_ciByRGBA.png", ident, chksum, n64fmt, n64fmt_sz, palchksum);
        }
    }
}

bool write_info_to_png(char* filename, struct GHQTexInfo* info)
{
    FILE* file = fopen(filename, "wb");
    if (file == NULL)
    {
        perror("fopen");
        return false;
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL)
    {
        fclose(file);
        return false;
    }


    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL)
    {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(file);
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(file);
        return false;
    }

    png_init_io(png_ptr, file);

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(file);
        return false;
    }

    png_byte bit_depth  = 8;
    png_byte color_type = PNG_COLOR_TYPE_RGBA;
    png_set_IHDR(png_ptr, info_ptr, info->width, info->height, 
        bit_depth, color_type, PNG_INTERLACE_NONE, 
        PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(png_ptr, info_ptr);

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(file);
        return false;
    }

    int pixel_size = 4;
    int p = 0;
    png_bytep* row_pointers = malloc(info->height * sizeof(png_bytep));
    for (int y = 0; y < info->height; y++)
    {
        row_pointers[y] = malloc(info->width * pixel_size);
        for (int x = 0; x < info->width; x++)
        {
            row_pointers[y][x * pixel_size + 0] = info->data[p++];
            row_pointers[y][x * pixel_size + 1] = info->data[p++];
            row_pointers[y][x * pixel_size + 2] = info->data[p++];
            row_pointers[y][x * pixel_size + 3] = info->data[p++];
        }
    }

    png_write_image(png_ptr, row_pointers);

    for (int y = 0; y < info->height; y++)
    {
        free(row_pointers[y]);
    }
    free(row_pointers);

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(file);
        return false;
    }

    png_write_end(png_ptr, NULL);

    png_destroy_write_struct(&png_ptr, &info_ptr);

    fclose(file);
    return true;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("Usage: %s [HTS FILE]\n", argv[0]);
        return 1;
    }

    char filename[PATH_MAX];
    char* fname_ptr;
    char ident[PATH_MAX];
    char* base_ident;
    memset(ident, 0, PATH_MAX);

    strcpy(filename, argv[1]);

    /* make sure end of filename contains _hirestextures.hts */
    if ((fname_ptr = strstr(filename, "_HIRESTEXTURES.hts")) == NULL)
    {
        printf("filename doesn't contain _HIRESTEXTURES.hts!\n");
        return 1;
    }

    /* retrieve ident */
    strncpy(ident, filename, fname_ptr - filename);
    base_ident = basename(ident);

    /* create directory for ident */
    struct stat st;
    if (stat(ident, &st) == -1 &&
#ifdef _WIN32
        mkdir(ident) == -1)
#else
        mkdir(ident, 0700) == -1)
#endif /* _WIN32 */
    {
        perror("mkdir");
        return 1;
    }

    FILE* file = fopen(filename, "rb");
    if (file == NULL)
    {
        perror("fopen");
        return 1;
    }

    /* change directory to ident */
    if (chdir(ident) == -1)
    {
        perror("chdir");
        return 1;
    }

#define FREAD(x) fread(&x, sizeof(x), 1, file)
#ifdef _WIN32
#define FSEEK(x, y, z) _fseeki64(x, y, z)
#define FTELL(x) _ftelli64(x)
#else
#define FSEEK(x, y, z) fseek(x, y, z)
#define FTELL(x) ftell(x)
#endif
    /* read file header & mapping */
    bool    oldFormat = false;
    int32_t version   = -1;
    int32_t header    = -1;
    int64_t mappingOffset = -1;
    int32_t mappingSize   = -1;

    /* determine HTS format */
    FREAD(version);
    if (version == TXCACHE_FORMAT_VERSION)
    {
        FREAD(header);
        oldFormat = false;
    }
    else
    {
        header = version;
        oldFormat = true;
    }
    
    if (/* uncompressed HTS */
        header != 1075970048 &&
        /* compressed HTS */
        header != 1084358656)
    {
        fprintf(stderr, "expected header = 1075970048 or 1084358656\n");
        fprintf(stderr, "got header = %i\n", header);
        return 1;
    }

    FREAD(mappingOffset);

    /* seek to mapping */
    FSEEK(file, mappingOffset, SEEK_SET);

    FREAD(mappingSize);

    for (int32_t i = 0; i < mappingSize; i++)
    {
        /* write each file to PNG */
        uint64_t checksum = 0;
        int64_t  offset = 0;
        uint64_t currentOffset = 0;
        struct GHQTexInfo info = {0};
        char filename[PATH_MAX];

        FREAD(checksum);
        FREAD(offset);

        /* store current offset */
        currentOffset = FTELL(file);

        /* seek to texture */
        FSEEK(file, offset, SEEK_SET);

        if (!read_info(file, oldFormat, &info))
        {
            printf("read_info failed!\n");
            continue;
        }

        get_filename_from_info(checksum, oldFormat, &info, base_ident, filename);

        if (oldFormat)
        {
            printf("-> [%i/%i] writing %s\n"
                   "-> info.width = %i\n"
                   "-> info.height = %i\n"
                   "-> info.format = %u\n"
                   "-> info.texture_format = %i\n"
                   "-> info.pixel_type = %i\n"
                   "-> info.is_hires_tex = %i\n", 
                    (i + 1), mappingSize, filename,
                    info.width,
                    info.height,
                    info.format,
                    info.texture_format,
                    info.pixel_type,
                    info.is_hires_tex);
        }
        else
        {
            printf("-> [%i/%i] writing %s\n"
                   "-> info.width = %i\n"
                   "-> info.height = %i\n"
                   "-> info.format = %u\n"
                   "-> info.texture_format = %i\n"
                   "-> info.pixel_type = %i\n"
                   "-> info.is_hires_tex = %i\n"
                   "-> info.n64_format_size = %i\n", 
                    (i + 1), mappingSize, filename,
                    info.width,
                    info.height,
                    info.format,
                    info.texture_format,
                    info.pixel_type,
                    info.is_hires_tex,
                    info.n64_format_size._formatsize);
        }

        if (!write_info_to_png(filename, &info))
        {
            fprintf(stderr, "write_info_to_png failed!\n");
            free(info.data);
            return 1;
        }

        free(info.data);

        /* restore offset */
        FSEEK(file, currentOffset, SEEK_SET);
    }

#undef FSEEK
#undef FREAD
    
    fclose(file);
    return 0;
}
