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
#include <unordered_map>

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

union StorageOffset
{
    struct {
        int64_t _offset : 48;
        int64_t _formatsize : 16;
    };
    int64_t _data;
};

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
    uint32_t      dataSize;
};

#define FREAD(x) fread(&x, sizeof(x), 1, file)
#define FWRITE(x) fwrite(&x, sizeof(x), 1, file);
#ifdef _WIN32
#define FSEEK(x, y, z) _fseeki64(x, y, z)
#define FTELL(x) _ftelli64(x)
#else
#define FSEEK(x, y, z) fseek(x, y, z)
#define FTELL(x) ftell(x)
#endif

static bool read_info(FILE* file, bool oldFormat, struct GHQTexInfo* info)
{
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
    FREAD(info->dataSize);

    info->data = (uint8_t*)malloc(info->dataSize);
    if (info->data == NULL)
    {
        return false;
    }

    fread(info->data, info->dataSize, 1, file);

    if (info->format & GL_TEXFMT_GZ)
    {
        void* dest     = NULL;
        uLongf destLen = info->dataSize * 2;
        int ret        = 0;
        do
        {
            dest = malloc(destLen);
            if (dest == NULL)
            {
                return false;
            }

            ret = uncompress((unsigned char*)dest, &destLen, info->data, info->dataSize);
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
        info->data     = (uint8_t*)dest;
        info->dataSize = destLen;
    }

    return true;
}

static bool write_info(FILE* file, bool oldFormat, bool compression, struct GHQTexInfo* info)
{
	if (compression)
	{
		info->format |= GL_TEXFMT_GZ;
	}

	FWRITE(info->width);
    FWRITE(info->height);
    FWRITE(info->format);
    FWRITE(info->texture_format);
    FWRITE(info->pixel_type);
    FWRITE(info->is_hires_tex);
    if (!oldFormat)
    {
        FWRITE(info->n64_format_size._formatsize);
    }

    if (compression)
    {
    	void* dest     = NULL;
    	uLongf destLen = info->dataSize * 2;
    	dest = malloc(destLen);

    	if (compress2((unsigned char*)dest, &destLen, info->data, info->dataSize, 1) != Z_OK)
    	{
    		free(dest);
    		return false;
    	}

    	free(info->data);
    	info->dataSize = destLen;
    	info->data     = (uint8_t*)dest;
    }

    FWRITE(info->dataSize);
    fwrite(info->data, info->dataSize, 1, file);
	return true;
}

static bool check_header(FILE* file, bool* oldFormat, bool* compressed)
{
	int32_t header    = -1;
	int32_t version   = -1;

	/* determine HTS format */
    FREAD(version);
    if (version == TXCACHE_FORMAT_VERSION)
    {
        FREAD(header);
        *oldFormat = false;
    }
    else
    {
        header = version;
        *oldFormat = true;
    }
    
    if (/* uncompressed HTS */
        header != 1075970048 &&
        /* compressed HTS */
        header != 1084358656)
    {
        fprintf(stderr, "Error: expected header = 1075970048 or 1084358656\n");
        fprintf(stderr, "Error: got header %i\n", header);
        return false;
    }

    if (compressed != NULL)
    {
    	*compressed = (header == 1084358656);
    }

    return true;
}

static bool write_cache(FILE* file, FILE* outputFile, bool readOldFormat, bool writeOldFormat, bool compression, std::unordered_map<uint64_t, StorageOffset>& mapping)
{
	int64_t mappingOffset = -1;
    int32_t mappingSize   = -1;

	FREAD(mappingOffset);

    /* seek to mapping */
    FSEEK(file, mappingOffset, SEEK_SET);

    FREAD(mappingSize);

    for (int32_t i = 0; i < mappingSize; i++)
    {
        /* write each file to PNG */
        uint64_t checksum = 0;
        union StorageOffset offset = {0};
        int64_t currentOffset = 0;
        struct GHQTexInfo info = {0};
        char filename[PATH_MAX];

        FREAD(checksum);
        FREAD(offset._data);

        /* store current offset */
        currentOffset = FTELL(file);

        /* seek to texture */
        FSEEK(file, offset._offset, SEEK_SET);

        if (!read_info(file, readOldFormat, &info))
        {
        	fprintf(stderr, "Error: failed to read texture info\n");
            continue;
        }

        if (readOldFormat)
        {
            printf("-> [%i/%i]\n"
                   "-> info.width = %i\n"
                   "-> info.height = %i\n"
                   "-> info.format = %u\n"
                   "-> info.texture_format = %i\n"
                   "-> info.pixel_type = %i\n"
                   "-> info.is_hires_tex = %i\n", 
                    (i + 1), mappingSize,
                    info.width,
                    info.height,
                    info.format,
                    info.texture_format,
                    info.pixel_type,
                    info.is_hires_tex);
        }
        else
        {
            printf("-> [%i/%i]\n"
                   "-> info.width = %i\n"
                   "-> info.height = %i\n"
                   "-> info.format = %u\n"
                   "-> info.texture_format = %i\n"
                   "-> info.pixel_type = %i\n"
                   "-> info.is_hires_tex = %i\n"
                   "-> info.n64_format_size = %i\n", 
                    (i + 1), mappingSize,
                    info.width,
                    info.height,
                    info.format,
                    info.texture_format,
                    info.pixel_type,
                    info.is_hires_tex,
                    info.n64_format_size._formatsize);
        }

        /* add to mapping list with correct offset */
        if (writeOldFormat)
        {
        	offset._data = FTELL(outputFile);
        }
        else
        {
        	offset._offset = FTELL(outputFile);
    	}
        mapping[checksum] = offset;

        if (!write_info(outputFile, writeOldFormat, compression, &info))
        {
        	printf("write_info failed!\n");
        }

        free(info.data);

        /* restore offset */
        FSEEK(file, currentOffset, SEEK_SET);
    }

    return true;
}

#undef FREAD
#undef FWRITE

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        printf("Usage: %s [HTS FILE] [HTS FILE] [OUTPUT HTS FILE]\n", argv[0]);
        return 1;
    }

    char filename[PATH_MAX];
    char filename2[PATH_MAX];
    char outputFilename[PATH_MAX];

    strcpy(filename, argv[1]);
    strcpy(filename2, argv[2]);
    strcpy(outputFilename, argv[3]);

    /* make sure end of filename contains _hirestextures.hts */
    if (strstr(filename, "_HIRESTEXTURES.hts") == NULL || 
    	strstr(filename2, "_HIRESTEXTURES.hts") == NULL)
    {
        printf("Error: filename doesn't contain _HIRESTEXTURES.hts!\n");
        return 1;
    }

    FILE* file = fopen(filename, "rb");
    if (file == NULL)
    {
        perror("fopen");
        return 1;
    }
    FILE* file2 = fopen(filename2, "rb");
    if (file == NULL)
    {
        perror("fopen");
        return 1;
    }

    std::unordered_map<uint64_t, StorageOffset> mapping;

    /* read file header & mapping */
    bool oldFormat   = false;
    bool oldFormat2  = false;
    bool compression = false;
    
    FILE* outputFile = fopen(outputFilename, "wb+");

    if (!check_header(file, &oldFormat, &compression) ||
    	!check_header(file2, &oldFormat2, NULL))
    {
    	fclose(file);
    	fclose(file2);
    	fclose(outputFile);
    	return 1;
    }

    // TODO: mismatched format support
    if (oldFormat != oldFormat2)
    {
    	fprintf(stderr, "Error: format mismatch!\n");
    	fclose(file);
    	fclose(file2);
    	fclose(outputFile);
    	return 1;
    }

    int header = TXCACHE_FORMAT_VERSION;
    int config = compression ? 1084358656 : 1075970048;
    int64_t mappingOffset = 0;
    int mappingSize = 0;

#define FREAD(x) fread(&x, sizeof(x), 1, outputFile)
#define FWRITE(x) fwrite(&x, sizeof(x), 1, outputFile);

    // write header and dummy mapping offset
    if (!oldFormat)
    {
    	FWRITE(header);
	}
    FWRITE(config);
    FWRITE(mappingOffset);

    // write first HTS file to output file
    if (!write_cache(file, outputFile, oldFormat, oldFormat, compression, mapping))
    {
    	return 1;
    }
    // write second HTS file to output file
    if (!write_cache(file2, outputFile, oldFormat2, oldFormat, compression, mapping))
    {
    	return 1;
    }

    mappingOffset = FTELL(outputFile);
    mappingSize = (int)mapping.size();

    // write mappings
    FWRITE(mappingSize);
    for (const auto& m : mapping)
    {
    	FWRITE(m.first);
    	FWRITE(m.second._data);
    }

    // write correct mapping offset
    int offset = oldFormat ? sizeof(config) : sizeof(header) + sizeof(config);
    FSEEK(outputFile, offset, SEEK_SET);
    FWRITE(mappingOffset);

#undef FREAD
#undef FWRITE

    fclose(file);
    return 0;
}
