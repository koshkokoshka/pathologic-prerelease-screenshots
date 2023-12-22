/**
 * compile commands:
 * unix - gcc vfs_unpacker.c -o vfs_unpacker
 * windows - cl.exe vfs_unpacker.c /link /out:vfs_unpacker.exe
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h> // mkdir

typedef struct
{
    char signature[4];
    char unknown[4];
    uint32_t entriesCount;

} VFS_Header;

typedef struct
{
    uint32_t contentLength;
    uint32_t contentOffset;
    uint16_t unknown1;
    uint16_t unknown2;
    uint16_t unknown3;
    uint16_t unknown4;

} VFS_EntryPayload;

typedef struct
{
    uint8_t nameLength;
    char *name;

    VFS_EntryPayload payload;

} VFS_Entry;

int VFS_CheckSignature(VFS_Header *header)
{
    return (strncmp(header->signature, "LP1C", 4) == 0);
}

VFS_Entry *VFS_ReadEntries(VFS_Header *header, FILE *file)
{
    VFS_Entry *entries = calloc(header->entriesCount, sizeof(VFS_Entry));
    if (entries) {

        for (int i = 0; i < header->entriesCount; ++i) {
            VFS_Entry *entry = &entries[i];

            // read entry name length
            if (fread(&entry->nameLength, sizeof(uint8_t), 1, file) != 1) {
                printf("Warning: failed to read entry #%d\n", i);
                continue;
            }

            // read entry name
            entry->name = malloc(entry->nameLength);
            if (fread(entry->name, entry->nameLength, 1, file) != 1) {
                printf("Warning: failed to entry #%d name\n", i);
                continue;
            }

            // read rest entry payload
            if (fread(&entry->payload, sizeof(VFS_EntryPayload), 1, file) != 1) {
                printf("Warning: failed to entry #%d payload\n", i);
                continue;
            }
        }

        return entries;
    }

    return NULL;
}

void VFS_DestroyEntries(VFS_Header *header, VFS_Entry *entries)
{
    for (int i = 0; i < header->entriesCount; ++i) {
        VFS_Entry *entry = &entries[i];
        free(entry->name);
    }
}

void PathJoin(char *path, ...)
{
}

int VFS_Entry_Unpack(VFS_Entry *entry, FILE *file, const char *dirname, size_t dirnameLength)
{
    if (entry == NULL || dirname == NULL) {
        return 1;
    }
    if (dirnameLength == 0) {
        dirnameLength = strlen(dirname);
    }

    const size_t pathSize = 255;
    char path[255];
    snprintf(path, pathSize, "%.*s/%.*s", (int)dirnameLength, dirname, (int)entry->nameLength, entry->name);

    const size_t bufferSize = 1024;
    char buffer[1024];

    FILE *out = fopen(path, "wb");
    if (out) {

        fseek(file, entry->payload.contentOffset, SEEK_SET);

        size_t contentLength = entry->payload.contentLength;
        while (contentLength > 0)
        {
            size_t toRead = contentLength;
            if (toRead > bufferSize) {
                toRead = bufferSize;
            }
            size_t read = fread(buffer, 1, toRead, file);

            contentLength -= read;

            if (fwrite(buffer, read, 1, out) == 0) {
                break;
            }
        }

        fclose(out);

        return 0;
    }
    return 1;
}

int main(int argc, char *argv[])
{
    // parse arguments
    if (argc < 2) {
        printf("Usage: %s <path-to-vfs>\n", argv[0]);
        return 0;
    }

    int error = 1;

    char *path = argv[1];
    printf("Path: \"%s\"\n", path);

    // create output directory
    char output[255];
    memcpy(output, path, strlen(path));

    char *indexOfVFS = strstr(output, ".vfs");
    if (indexOfVFS) {
        output[indexOfVFS - output] = '\0';
    }

    struct stat st;
    if (stat(output, &st) != 0 && ((st.st_mode) & S_IFMT) != S_IFDIR) {
        if (mkdir(output, 0755) != 0) {
            printf("Error: failed to create output directory\n");
            return 1;
        }
    }

    printf("Output: \"%s\"\n", path);

    // open file for reading
    FILE *file = fopen(path, "rb");
    if (file) {

        // read header
        VFS_Header header;
        if (fread(&header, sizeof(VFS_Header), 1, file) == 1) {

            // verify file
            if (VFS_CheckSignature(&header)) {

                printf("Entries count: %d\n", header.entriesCount);

                // read entries
                VFS_Entry *entries = VFS_ReadEntries(&header, file);
                if (entries) {

                    for (int i = 0; i < header.entriesCount; ++i) {
                        VFS_Entry *entry = &entries[i];

                        printf("[%d/%d] %.*s (strlen: %d) { length: %d, offset: %#08x }\n",
                               i, header.entriesCount,
                               entry->nameLength, entry->name,
                               entry->nameLength,
                               entry->payload.contentLength,
                               entry->payload.contentOffset);

                        if (VFS_Entry_Unpack(entry, file, output, 0) != 0) {
                            printf("Error: unpack failed!\n");
                        }
                    }

                    // clean up
                    VFS_DestroyEntries(&header, entries);

                    error = 0; // success
                }

            } else {
                printf("Error: signature check failed\n");
            }
        } else {
            printf("Error: failed to read file header\n");
        }

        fclose(file); // close opened file

    } else {
        printf("Error: unable to open file for reading\n");
    }

    return error;
}