#include <linux/limits.h>
#include <png.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

struct GHQTexInfo
{
    uint8_t* data;
    int32_t width;
    int32_t height;
    uint32_t format;
    uint16_t texture_format;
    uint16_t pixel_type;
    uint8_t is_hires_tex;
};

bool read_info(FILE* file, struct GHQTexInfo* info)
{
    uint32_t dataSize = 0;
#define FREAD(x) fread(&x, sizeof(x), 1, file)
    FREAD(info->width);
    FREAD(info->height);
    FREAD(info->format);
    FREAD(info->texture_format);
    FREAD(info->pixel_type);
    FREAD(info->is_hires_tex);
    FREAD(dataSize);
#undef FREAD

    info->data = malloc(dataSize);
    if (info->data == NULL)
    {
        return false;
    }

    fread(info->data, dataSize, 1, file);

    return true;
}

void get_filename_from_info(uint64_t checksum, struct GHQTexInfo* info, char* ident, char* filename)
{
    uint32_t chksum = checksum & 0xffffffff;
    uint32_t palchksum = checksum >> 32;

    if (palchksum == 0)
    {
        sprintf(filename, "%s#%08X#%01X#%01X_all.png", ident, chksum, 3, 0);
    }
    else
    {
        sprintf(filename, "%s#%08X#%01X#%01X#%08X_ciByRGBA.png", ident, chksum, 3, 0, palchksum);
    }
}

bool write_info_to_png(char* filename, struct GHQTexInfo* info)
{
    FILE* file = fopen(filename, "w");
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

    png_byte bit_depth = 8;
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
    png_bytep * row_pointers = (png_bytep*)malloc(info->height * sizeof(png_bytep));
    for (int y = 0; y <= info->height; y++)
    {
        row_pointers[y] = (png_byte*)malloc(info->width * pixel_size);
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
        mkdir(ident, 0700) == -1)
    {
        perror("mkdir");
        return 1;
    }

    FILE* file = fopen(filename, "r");
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
    /* read file header & mapping */
    int32_t header = -1;
    int64_t mappingOffset = -1;
    int32_t mappingSize = -1;

    FREAD(header);
    FREAD(mappingOffset);

    if (header != 1075970048)
    {
        fprintf(stderr, "header != 1075970048");
        return 1;
    }

    /* seek to mapping */
    fseek(file, mappingOffset, SEEK_SET);

    FREAD(mappingSize);

    for (int32_t i = 0; i < mappingSize; i++)
    {
        /* write each file to PNG */
        uint64_t checksum = 0, offset = 0;
        uint64_t currentOffset = 0;
        struct GHQTexInfo info;
        char filename[PATH_MAX];

        FREAD(checksum);
        FREAD(offset);

        /* store current offset */
        currentOffset = ftell(file);

        /* seek to texture */
        fseek(file, offset, SEEK_SET);

        if (!read_info(file, &info))
        {
            printf("read_info failed!\n");
            continue;
        }

        get_filename_from_info(checksum, &info, base_ident, filename);

        printf("-> [%i/%i] writing %s\n", i, mappingSize, filename);
        printf("-> info.width = %i\n", info.width);
        printf("-> info.height = %i\n", info.height);
        printf("-> info.format = %u\n", info.format);
        printf("-> info.texture_format = %i\n", info.texture_format);
        printf("-> info.pixel_type = %i\n", info.pixel_type);

        if (!write_info_to_png(filename, &info))
        {
            fprintf(stderr, "write_info_to_png failed!\n");
            free(info.data);
            return 1;
        }

        free(info.data);

        /* restore offset */
        fseek(file, currentOffset, SEEK_SET);
    }

#undef FREAD
    
    fclose(file);
    return 0;
}
