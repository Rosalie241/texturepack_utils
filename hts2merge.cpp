/*
 * texturepack_utils - https://github.com/Rosalie241/texturepack_utils
 *  Copyright (C) 2023 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
//#define VERBOSE
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

static void** buffers = nullptr;
static int    buffers_count = 0;
static int*   buffers_size = nullptr;

static void* get_buffer(uint8_t number, size_t size)
{
    /* allocate required buffers */
    if (buffers_count < (number + 1))
    {
        printf("get_buffer: creating buffer %i with size %lu\n", number, size);
        buffers       = (void**)realloc((void*)buffers, (number + 1) * sizeof(void*));
        buffers_size  = (int*)realloc((void*)buffers_size, (number + 1) * sizeof(int));
        /* set initial state for buffers */
        for (size_t i = buffers_count; i < (number + 1); i++)
        {
            printf("setting state for buffer %i\n", i);
            buffers[number]      = nullptr;
            buffers_size[number] = 0;
        }

        buffers_count = (number + 1);
    }

    /* allocate memory for buffer if needed */
    if (buffers_size[number] < size)
    {
        printf("get_buffer: allocating %lu for buffer %i\n", size, number);
        buffers[number]      = realloc(buffers[number], size);
        buffers_size[number] = size;
    }

    /* return usable buffer */
    return buffers[number];
}

static void free_buffers(void)
{
    for (int i = 0; i < buffers_count; i++)
    {
        free(buffers[i]);
    }

    free(buffers_size);
    free(buffers);
    buffers       = nullptr;
    buffers_size  = nullptr;
    buffers_count = 0;
}

static bool read_info(FILE* file, bool oldFormat, struct GHQTexInfo* info, uint8_t buffer_number)
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

    info->data = (uint8_t*)get_buffer(buffer_number, info->dataSize);
    if (info->data == NULL)
    {
        printf("get_buffer with size %lu failed!\n", info->dataSize);
        return false;
    }

    fread(info->data, info->dataSize, 1, file);
    return true;
}

static bool write_info(FILE* file, bool oldFormat, bool compression, struct GHQTexInfo* info)
{
    bool compress = false;

    if (compression && 
        (info->format & GL_TEXFMT_GZ) == 0)
    {
        compress = true;
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

    if (compress)
    {
    	void* dest     = NULL;
    	uLongf destLen = info->dataSize;
    	dest = get_buffer(2, destLen);

    	if (compress2((unsigned char*)dest, &destLen, info->data, info->dataSize, 1) != Z_OK)
    	{
    		return false;
    	}

    	info->dataSize = destLen;
    	info->data     = (uint8_t*)dest;
    }

    FWRITE(info->dataSize);
    fwrite(info->data, info->dataSize, 1, file);
	return true;
}

static bool check_size(struct GHQTexInfo* info, size_t size, bool compression)
{
    /* compress data when required */
    if (compression && 
        (info->format & GL_TEXFMT_GZ) == 0)
    {
        uLongf destLen = info->dataSize;
        void*  dest    = get_buffer(3, destLen);

        if (compress2((unsigned char*)dest, &destLen, info->data, info->dataSize, 1) == Z_OK)
        {
            info->dataSize = destLen;
            info->data     = (uint8_t*)dest;
            info->format  |= GL_TEXFMT_GZ;
        }
    }

    return info->dataSize <= size; 
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

static bool write_cache(FILE* file, FILE* outputFile, bool readOldFormat, bool writeOldFormat, bool compression,
                        std::unordered_multimap<uint64_t, StorageOffset>& mapping)
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
        int64_t outputFileCurrentOffset = 0;
        struct GHQTexInfo info = {0};
        struct GHQTexInfo info2 = {0};
        char filename[PATH_MAX];

        FREAD(checksum);
        FREAD(offset._data);

        /* store current offset */
        currentOffset = FTELL(file);

        /* seek to texture */
        FSEEK(file, offset._offset, SEEK_SET);

        if (!read_info(file, readOldFormat, &info, 0))
        {
        	fprintf(stderr, "Error: failed to read texture info\n");
            continue;
        }

#ifdef VERBOSE
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
#endif // VERBOSE

        std::unordered_multimap<uint64_t, StorageOffset>::iterator mappingIter = mapping.end();
        if (writeOldFormat)
        {
            mappingIter = mapping.find(checksum);
        }
        else
        {
            /* find match with format size included */
            auto range = mapping.equal_range(checksum);
            for (auto rangeIter = range.first; rangeIter != range.second; rangeIter++)
            {
                if (rangeIter->second._formatsize == offset._formatsize)
                {
                    mappingIter = rangeIter;
                    break;
                }
            }
        }

        /* try and see if we can replace
         * the existing texture */
        bool replaceTexture = false;
        if (mappingIter != mapping.end())
        {
            outputFileCurrentOffset = FTELL(outputFile);
            FSEEK(outputFile, mappingIter->second._offset, SEEK_SET);
            if (read_info(outputFile, writeOldFormat, &info2, 1) &&
                /* does the texture fit? */
                check_size(&info2, info.dataSize, compression))
            {
                replaceTexture = true;
                /* set offset to the texture */
                FSEEK(outputFile, mappingIter->second._offset, SEEK_SET);
            }
            else
            {
                /* restore offset because the texture doesn't fit */
                FSEEK(outputFile, outputFileCurrentOffset, SEEK_SET);
            }
        }

        /* update offset in mapping */
        offset._offset = FTELL(outputFile);
        if (mappingIter != mapping.end())
        {
            mappingIter->second = offset;
        }
        else
        {
            mapping.insert({checksum, offset});
        }

        write_info(outputFile, writeOldFormat, compression, &info);

        /* restore offset when we've replaced
         * a texture */
        if (replaceTexture)
        {
            FSEEK(outputFile, outputFileCurrentOffset, SEEK_SET);
        }

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

    std::unordered_multimap<uint64_t, StorageOffset> mapping;

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

    // TODO: support file 1 being new format and
    // file 2 being old format
    if (!oldFormat && oldFormat2)
    {
    	fprintf(stderr, "Error: unsupported format mismatch!\n");
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
    printf("-> Processing %s...\n", filename);
    if (!write_cache(file, outputFile, oldFormat, oldFormat, compression, mapping))
    {
        fclose(file);
        fclose(file2);
        fclose(outputFile);
    	return 1;
    }
    // write second HTS file to output file
    printf("-> Processing %s...\n", filename2);
    if (!write_cache(file2, outputFile, oldFormat2, oldFormat, compression, mapping))
    {
        fclose(file);
        fclose(file2);
        fclose(outputFile);
    	return 1;
    }

    printf("-> Writing header and mappings...\n");

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
