#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

// Platform-specific header inclusion
#ifdef _WIN32
#include <io.h>         // Windows mkdir declaration
#include <sys/stat.h>   // Windows stat structure definition
#else
#include <sys/stat.h>   // Linux/macOS system call definitions
#endif

#include "firmware.h"
#include "compression.h"
#include "crc.h"

// Platform-specific macro definitions
#ifdef _WIN32
#define MKDIR_FUNC(path)    mkdir(path)                // Windows mkdir doesn't require permission
#define PATH_SEPARATORS     "/\\"                      // Windows supports both separators
#else
#define MKDIR_FUNC(path)    mkdir(path, 0755)          // Linux/macOS need permission (0755 = rwxr-xr-x)
#define PATH_SEPARATORS     "/"                         // POSIX uses only '/'
#endif

// Calculate size aligned to specified value (round up)
static size_t align_size(size_t size, size_t alignment) {
    if (size % alignment == 0) {
        return size;
    }
    return (size / alignment + 1) * alignment;
}

// Create directory (recursive creation support, cross-platform)
static int create_directory(const char *path) {
    char tmp[256];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    
    // Remove trailing path separator
    if (len > 0 && strchr(PATH_SEPARATORS, tmp[len - 1])) {
        tmp[len - 1] = '\0';
    }
    
    // Recursively create parent directories
    for (p = tmp + 1; *p; p++) {
        if (strchr(PATH_SEPARATORS, *p)) {
            *p = '\0';  // Temporarily truncate to current directory
            
            // Platform-specific mkdir call; ignore if directory exists
            if (MKDIR_FUNC(tmp) != 0 && errno != EEXIST) {
                return -1;
            }
            
            *p = '/';  // Restore separator (unify to '/')
        }
    }
    
    // Create target directory
    if (MKDIR_FUNC(tmp) != 0 && errno != EEXIST) {
        return -1;
    }
    
    return 0;
}

// Create full path from base, subfolder and filename
static char* get_full_path(const char* base, const char* subfolder, const char* filename) {
    size_t base_len = strlen(base);
    size_t sub_len = strlen(subfolder);
    size_t file_len = strlen(filename);
    // Allocate memory: base + subfolder + filename + 2 separators + null terminator
    char* path = malloc(base_len + sub_len + file_len + 3);
    if (!path) return NULL;  // Return NULL on allocation failure
    
    snprintf(path, base_len + sub_len + file_len + 3, 
             "%s/%s/%s", base, subfolder, filename);
    return path;
}

// Print command line help
static void help(char *name) {
    printf("DS Firmware Data Extractor and Injector\n");
    printf("Usage:\n");
    printf("  Extract mode: %s firmware.bin -e base_folder\n", name);
    printf("  Inject mode:  %s firmware.bin -i base_folder output_firmware.bin\n", name);
    printf("Folder structure:\n");
    printf("  base_folder/\n");
    printf("  й└йд01_raw/\n");
    printf("  й└йд02_decompressed/\n");
    printf("  й└йд03_modified_decompressed/\n");
    printf("  й╕йд04_modified_compressed/\n");
}

// Comparison function for qsort (sorts offsets ascending)
static int compare_offsets(const void *a, const void *b) {
    u32 offset_a = *(const u32*)a;
    u32 offset_b = *(const u32*)b;
    if (offset_a < offset_b) return -1;
    if (offset_a > offset_b) return 1;
    return 0;
}

int main(int argc, char **argv) {
    // Operation mode and path constants
    enum { unspecified, extract, inject } mode = unspecified;
    const char *subfolders[4] = {
        "01_raw",
        "02_decompressed",
        "03_modified_decompressed",
        "04_modified_compressed"
    };
    const char *filenames[3] = {
        "arm9_gui_code.bin",
        "arm7_wifi_code.bin",
        "data_gfx.bin"
    };
    const char *full_part_names[3] = {
        "ARM9 GUI code partition (Part3)",
        "ARM7 WiFi code partition (Part4)",
        "Graphics data partition (Part5)"
    };
    const size_t ALIGNMENT = 8;  // 8-byte alignment required

    // Input/output parameters
    char *firmwareFilename = NULL;
    char *base_folder = NULL;
    char *output_firmwareFilename = NULL;
    
    // Firmware data
    u8 *firmware = NULL;
    size_t firmware_size = 0;
    FILE *f = NULL;
    
    // Raw partition offsets in firmware (added "raw" prefix to clarify these are addresses within firmware)
    u32 raw_arm9_gui_code_offset = 0;  // Part3 offset in firmware
    u32 raw_arm7_wifi_code_offset = 0; // Part4 offset in firmware
    u32 raw_data_gfx_offset = 0;       // Part5 offset in firmware
    
    // Path variables
    char *raw_arm9_gui_code_path = NULL, *raw_arm7_wifi_code_path = NULL, *raw_data_gfx_path = NULL;
    char *decomp_arm9_gui_code_path = NULL, *decomp_arm7_wifi_code_path = NULL, *decomp_data_gfx_path = NULL;
    char *modified_decomp_arm9_gui_code_path = NULL, *modified_decomp_arm7_wifi_code_path = NULL, *modified_decomp_data_gfx_path = NULL;
    char *modified_comp_arm9_gui_code_path = NULL, *modified_comp_arm7_wifi_code_path = NULL, *modified_comp_data_gfx_path = NULL;
    
    // Data buffers
    u8 *raw_arm9_gui_code = NULL, *raw_arm7_wifi_code = NULL, *raw_data_gfx = NULL;
    size_t raw_arm9_gui_code_size = 0, raw_arm7_wifi_code_size = 0, raw_data_gfx_size = 0;
    
    u8 *decompressed_arm9_gui_code = NULL, *decompressed_arm7_wifi_code = NULL, *decompressed_data_gfx = NULL;
    size_t decompressed_arm9_gui_code_size = 0, decompressed_arm7_wifi_code_size = 0, decompressed_data_gfx_size = 0;
    
    u8 *modified_decompressed_arm9_gui_code = NULL, *modified_decompressed_arm7_wifi_code = NULL, *modified_decompressed_data_gfx = NULL;
    size_t modified_decompressed_arm9_gui_code_size = 0, modified_decompressed_arm7_wifi_code_size = 0, modified_decompressed_data_gfx_size = 0;
    
    u8 *compressed_arm9_gui_code = NULL, *compressed_arm7_wifi_code = NULL, *compressed_data_gfx = NULL;
    size_t compressed_arm9_gui_code_size = 0, compressed_arm7_wifi_code_size = 0, compressed_data_gfx_size = 0;
    size_t aligned_arm9_gui_code_size = 0, aligned_arm7_wifi_code_size = 0, aligned_data_gfx_size = 0;

    // Parse command line arguments
    if (argc == 4 && strcmp(argv[2], "-e") == 0) {
        mode = extract;
        firmwareFilename = argv[1];
        base_folder = argv[3];
    }
    else if (argc == 5 && strcmp(argv[2], "-i") == 0) {
        mode = inject;
        firmwareFilename = argv[1];
        base_folder = argv[3];
        output_firmwareFilename = argv[4];
    }
    else {
        help(argv[0]);
        return 1;
    }

    // Create all required subdirectories
    for (int i = 0; i < 4; i++) {
        char *folder_path = malloc(strlen(base_folder) + strlen(subfolders[i]) + 2);
        snprintf(folder_path, strlen(base_folder) + strlen(subfolders[i]) + 2, 
                 "%s/%s", base_folder, subfolders[i]);
        
        if (create_directory(folder_path) != 0) {
            perror("Failed to create directory");
            free(folder_path);
            return 1;
        }
        free(folder_path);
    }

    // Generate full paths for all files
    raw_arm9_gui_code_path = get_full_path(base_folder, subfolders[0], filenames[0]);
    raw_arm7_wifi_code_path = get_full_path(base_folder, subfolders[0], filenames[1]);
    raw_data_gfx_path = get_full_path(base_folder, subfolders[0], filenames[2]);
    
    decomp_arm9_gui_code_path = get_full_path(base_folder, subfolders[1], filenames[0]);
    decomp_arm7_wifi_code_path = get_full_path(base_folder, subfolders[1], filenames[1]);
    decomp_data_gfx_path = get_full_path(base_folder, subfolders[1], filenames[2]);
    
    modified_decomp_arm9_gui_code_path = get_full_path(base_folder, subfolders[2], filenames[0]);
    modified_decomp_arm7_wifi_code_path = get_full_path(base_folder, subfolders[2], filenames[1]);
    modified_decomp_data_gfx_path = get_full_path(base_folder, subfolders[2], filenames[2]);
    
    modified_comp_arm9_gui_code_path = get_full_path(base_folder, subfolders[3], filenames[0]);
    modified_comp_arm7_wifi_code_path = get_full_path(base_folder, subfolders[3], filenames[1]);
    modified_comp_data_gfx_path = get_full_path(base_folder, subfolders[3], filenames[2]);

    // Read firmware file (binary mode)
    f = fopen(firmwareFilename, "rb");
    if (!f) {
        perror("Failed to open firmware file");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    firmware_size = ftell(f);
    rewind(f);
    firmware = malloc(firmware_size);
    if (!firmware) {
        perror("Failed to allocate firmware buffer");
        fclose(f);
        return 1;
    }
    fread(firmware, firmware_size, 1, f);
    fclose(f);
    f = NULL;

    // Get raw partition offsets from firmware header (these are addresses within the original firmware)
    raw_arm9_gui_code_offset = ((struct header *)firmware)->part3offset * 8;
    raw_arm7_wifi_code_offset = ((struct header *)firmware)->part4offset * 8;
    raw_data_gfx_offset = ((struct header *)firmware)->part5offset * 8;

    // Calculate raw partition sizes (sort offsets first)
    u32 raw_offsets[3] = {raw_arm9_gui_code_offset, raw_arm7_wifi_code_offset, raw_data_gfx_offset};
    u32 sorted_raw_offsets[3];
    memcpy(sorted_raw_offsets, raw_offsets, sizeof(raw_offsets));
    qsort(sorted_raw_offsets, 3, sizeof(u32), compare_offsets);

    // Calculate size of ARM9 GUI code partition
    if (raw_arm9_gui_code_offset == sorted_raw_offsets[0]) {
        raw_arm9_gui_code_size = sorted_raw_offsets[1] - raw_arm9_gui_code_offset;
    } else if (raw_arm9_gui_code_offset == sorted_raw_offsets[1]) {
        raw_arm9_gui_code_size = sorted_raw_offsets[2] - raw_arm9_gui_code_offset;
    } else {
        raw_arm9_gui_code_size = firmware_size - raw_arm9_gui_code_offset;
    }

    // Calculate size of ARM7 WiFi code partition
    if (raw_arm7_wifi_code_offset == sorted_raw_offsets[0]) {
        raw_arm7_wifi_code_size = sorted_raw_offsets[1] - raw_arm7_wifi_code_offset;
    } else if (raw_arm7_wifi_code_offset == sorted_raw_offsets[1]) {
        raw_arm7_wifi_code_size = sorted_raw_offsets[2] - raw_arm7_wifi_code_offset;
    } else {
        raw_arm7_wifi_code_size = firmware_size - raw_arm7_wifi_code_offset;
    }

    // Calculate size of Graphics data partition
    if (raw_data_gfx_offset == sorted_raw_offsets[0]) {
        raw_data_gfx_size = sorted_raw_offsets[1] - raw_data_gfx_offset;
    } else if (raw_data_gfx_offset == sorted_raw_offsets[1]) {
        raw_data_gfx_size = sorted_raw_offsets[2] - raw_data_gfx_offset;
    } else {
        raw_data_gfx_size = firmware_size - raw_data_gfx_offset;
    }

    // -------------------------- Extract Mode --------------------------
    if (mode == extract) {
        printf("Starting extraction...\n");

        // Extract raw compressed data to 01_raw
        raw_arm9_gui_code = firmware + raw_arm9_gui_code_offset;
        f = fopen(raw_arm9_gui_code_path, "wb");
        fwrite(raw_arm9_gui_code, raw_arm9_gui_code_size, 1, f);
        fclose(f);

        raw_arm7_wifi_code = firmware + raw_arm7_wifi_code_offset;
        f = fopen(raw_arm7_wifi_code_path, "wb");
        fwrite(raw_arm7_wifi_code, raw_arm7_wifi_code_size, 1, f);
        fclose(f);

        raw_data_gfx = firmware + raw_data_gfx_offset;
        f = fopen(raw_data_gfx_path, "wb");
        fwrite(raw_data_gfx, raw_data_gfx_size, 1, f);
        fclose(f);

        // Decompress data to 02_decompressed
        decompressed_arm9_gui_code_size = decompress(NULL, raw_arm9_gui_code);
        decompressed_arm9_gui_code = malloc(decompressed_arm9_gui_code_size);
        decompress(decompressed_arm9_gui_code, raw_arm9_gui_code);
        
        f = fopen(decomp_arm9_gui_code_path, "wb");
        fwrite(decompressed_arm9_gui_code, decompressed_arm9_gui_code_size, 1, f);
        fclose(f);

        decompressed_arm7_wifi_code_size = decompress(NULL, raw_arm7_wifi_code);
        decompressed_arm7_wifi_code = malloc(decompressed_arm7_wifi_code_size);
        decompress(decompressed_arm7_wifi_code, raw_arm7_wifi_code);
        
        f = fopen(decomp_arm7_wifi_code_path, "wb");
        fwrite(decompressed_arm7_wifi_code, decompressed_arm7_wifi_code_size, 1, f);
        fclose(f);

        decompressed_data_gfx_size = decompress(NULL, raw_data_gfx);
        decompressed_data_gfx = malloc(decompressed_data_gfx_size);
        decompress(decompressed_data_gfx, raw_data_gfx);
        
        f = fopen(decomp_data_gfx_path, "wb");
        fwrite(decompressed_data_gfx, decompressed_data_gfx_size, 1, f);
        fclose(f);

        // Create modifiable copies in 03_modified_decompressed
        modified_decompressed_arm9_gui_code = malloc(decompressed_arm9_gui_code_size);
        memcpy(modified_decompressed_arm9_gui_code, decompressed_arm9_gui_code, decompressed_arm9_gui_code_size);
        f = fopen(modified_decomp_arm9_gui_code_path, "wb");
        fwrite(modified_decompressed_arm9_gui_code, decompressed_arm9_gui_code_size, 1, f);
        fclose(f);

        modified_decompressed_arm7_wifi_code = malloc(decompressed_arm7_wifi_code_size);
        memcpy(modified_decompressed_arm7_wifi_code, decompressed_arm7_wifi_code, decompressed_arm7_wifi_code_size);
        f = fopen(modified_decomp_arm7_wifi_code_path, "wb");
        fwrite(modified_decompressed_arm7_wifi_code, decompressed_arm7_wifi_code_size, 1, f);
        fclose(f);

        modified_decompressed_data_gfx = malloc(decompressed_data_gfx_size);
        memcpy(modified_decompressed_data_gfx, decompressed_data_gfx, decompressed_data_gfx_size);
        f = fopen(modified_decomp_data_gfx_path, "wb");
        fwrite(modified_decompressed_data_gfx, decompressed_data_gfx_size, 1, f);
        fclose(f);

        printf("Extraction complete:\n");
        printf("  Raw data saved to: %s/%s/\n", base_folder, subfolders[0]);
        printf("  Decompressed data saved to: %s/%s/\n", base_folder, subfolders[1]);
        printf("  Modifiable decompressed copies saved to: %s/%s/\n", base_folder, subfolders[2]);
    }
    // -------------------------- Inject Mode --------------------------
    else {
        printf("Starting injection...\n");

        // Read modified decompressed files from 03_modified_decompressed
        f = fopen(modified_decomp_arm9_gui_code_path, "rb");
        if (!f) { perror("Failed to open modified ARM9 GUI code file"); goto cleanup; }
        fseek(f, 0, SEEK_END);
        modified_decompressed_arm9_gui_code_size = ftell(f);
        rewind(f);
        modified_decompressed_arm9_gui_code = malloc(modified_decompressed_arm9_gui_code_size);
        fread(modified_decompressed_arm9_gui_code, modified_decompressed_arm9_gui_code_size, 1, f);
        fclose(f);

        f = fopen(modified_decomp_arm7_wifi_code_path, "rb");
        if (!f) { perror("Failed to open modified ARM7 WiFi code file"); goto cleanup; }
        fseek(f, 0, SEEK_END);
        modified_decompressed_arm7_wifi_code_size = ftell(f);
        rewind(f);
        modified_decompressed_arm7_wifi_code = malloc(modified_decompressed_arm7_wifi_code_size);
        fread(modified_decompressed_arm7_wifi_code, modified_decompressed_arm7_wifi_code_size, 1, f);
        fclose(f);

        f = fopen(modified_decomp_data_gfx_path, "rb");
        if (!f) { perror("Failed to open modified graphics data file"); goto cleanup; }
        fseek(f, 0, SEEK_END);
        modified_decompressed_data_gfx_size = ftell(f);
        rewind(f);
        modified_decompressed_data_gfx = malloc(modified_decompressed_data_gfx_size);
        fread(modified_decompressed_data_gfx, modified_decompressed_data_gfx_size, 1, f);
        fclose(f);

        // Compress modified data
        compressed_arm9_gui_code = malloc(256 * 1024);
        compressed_arm9_gui_code_size = compress(compressed_arm9_gui_code, modified_decompressed_arm9_gui_code, modified_decompressed_arm9_gui_code_size);
        
        compressed_arm7_wifi_code = malloc(256 * 1024);
        compressed_arm7_wifi_code_size = compress(compressed_arm7_wifi_code, modified_decompressed_arm7_wifi_code, modified_decompressed_arm7_wifi_code_size);
        
        compressed_data_gfx = malloc(256 * 1024);
        compressed_data_gfx_size = compress(compressed_data_gfx, modified_decompressed_data_gfx, modified_decompressed_data_gfx_size);

        // Calculate 8-byte aligned sizes
        aligned_arm9_gui_code_size = align_size(compressed_arm9_gui_code_size, ALIGNMENT);
        aligned_arm7_wifi_code_size = align_size(compressed_arm7_wifi_code_size, ALIGNMENT);
        aligned_data_gfx_size = align_size(compressed_data_gfx_size, ALIGNMENT);

        printf("Compressed sizes (original/aligned):\n");
        printf("  %s: %" PRIuPTR " / %" PRIuPTR " bytes\n",
               full_part_names[0], (uintptr_t)compressed_arm9_gui_code_size, (uintptr_t)aligned_arm9_gui_code_size);
        printf("  %s: %" PRIuPTR " / %" PRIuPTR " bytes\n",
               full_part_names[1], (uintptr_t)compressed_arm7_wifi_code_size, (uintptr_t)aligned_arm7_wifi_code_size);
        printf("  %s: %" PRIuPTR " / %" PRIuPTR " bytes\n",
               full_part_names[2], (uintptr_t)compressed_data_gfx_size, (uintptr_t)aligned_data_gfx_size);

        // Create aligned buffers with zero-padding
        u8 *aligned_arm9_gui_code = malloc(aligned_arm9_gui_code_size);
        u8 *aligned_arm7_wifi_code = malloc(aligned_arm7_wifi_code_size);
        u8 *aligned_data_gfx = malloc(aligned_data_gfx_size);

        // Copy compressed data and pad with zeros
        memcpy(aligned_arm9_gui_code, compressed_arm9_gui_code, compressed_arm9_gui_code_size);
        memset(aligned_arm9_gui_code + compressed_arm9_gui_code_size, 0, aligned_arm9_gui_code_size - compressed_arm9_gui_code_size);
        
        memcpy(aligned_arm7_wifi_code, compressed_arm7_wifi_code, compressed_arm7_wifi_code_size);
        memset(aligned_arm7_wifi_code + compressed_arm7_wifi_code_size, 0, aligned_arm7_wifi_code_size - compressed_arm7_wifi_code_size);
        
        memcpy(aligned_data_gfx, compressed_data_gfx, compressed_data_gfx_size);
        memset(aligned_data_gfx + compressed_data_gfx_size, 0, aligned_data_gfx_size - compressed_data_gfx_size);

        // Save aligned compressed data
        f = fopen(modified_comp_arm9_gui_code_path, "wb");
        fwrite(aligned_arm9_gui_code, aligned_arm9_gui_code_size, 1, f);
        fclose(f);
        
        f = fopen(modified_comp_arm7_wifi_code_path, "wb");
        fwrite(aligned_arm7_wifi_code, aligned_arm7_wifi_code_size, 1, f);
        fclose(f);
        
        f = fopen(modified_comp_data_gfx_path, "wb");
        fwrite(aligned_data_gfx, aligned_data_gfx_size, 1, f);
        fclose(f);

        // Inject aligned data into firmware at original raw offsets
        memcpy(firmware + raw_arm9_gui_code_offset, aligned_arm9_gui_code, aligned_arm9_gui_code_size);
        memcpy(firmware + raw_arm7_wifi_code_offset, aligned_arm7_wifi_code, aligned_arm7_wifi_code_size);
        memcpy(firmware + raw_data_gfx_offset, aligned_data_gfx, aligned_data_gfx_size);

        // Update CRC checksums
        ((struct header *)firmware)->part34crc = swiCRC(0xffff, 
            (u32*)(firmware + raw_arm9_gui_code_offset), compressed_arm9_gui_code_size);
        ((struct header *)firmware)->part34crc = swiCRC(((struct header *)firmware)->part34crc, 
            (u32*)(firmware + raw_arm7_wifi_code_offset), compressed_arm7_wifi_code_size);
        ((struct header *)firmware)->part5crc = swiCRC(0xffff, 
            (u32*)(firmware + raw_data_gfx_offset), compressed_data_gfx_size);

        // Save modified firmware
        f = fopen(output_firmwareFilename, "wb");
        fwrite(firmware, firmware_size, 1, f);
        fclose(f);

        // Print injection addresses (clearly indicating these are raw addresses in firmware)
        printf("\nInjection addresses (raw offsets in firmware):\n");
        printf("  %s: 0x%06X\n", full_part_names[0], raw_arm9_gui_code_offset);
        printf("  %s: 0x%06X\n", full_part_names[1], raw_arm7_wifi_code_offset);
        printf("  %s: 0x%06X\n", full_part_names[2], raw_data_gfx_offset);

        printf("\nInjection complete:\n");
        printf("  Aligned compressed data saved to: %s/%s/\n", base_folder, subfolders[3]);
        printf("  New firmware saved to: %s\n", output_firmwareFilename);

        // Free aligned buffers
        free(aligned_arm9_gui_code);
        free(aligned_arm7_wifi_code);
        free(aligned_data_gfx);
    }

// Cleanup section
cleanup:
    // Free all path strings
    free(raw_arm9_gui_code_path);
    free(raw_arm7_wifi_code_path);
    free(raw_data_gfx_path);
    
    free(decomp_arm9_gui_code_path);
    free(decomp_arm7_wifi_code_path);
    free(decomp_data_gfx_path);
    
    free(modified_decomp_arm9_gui_code_path);
    free(modified_decomp_arm7_wifi_code_path);
    free(modified_decomp_data_gfx_path);
    
    free(modified_comp_arm9_gui_code_path);
    free(modified_comp_arm7_wifi_code_path);
    free(modified_comp_data_gfx_path);
    
    // Free all data buffers
    free(decompressed_arm9_gui_code);
    free(decompressed_arm7_wifi_code);
    free(decompressed_data_gfx);
    
    free(modified_decompressed_arm9_gui_code);
    free(modified_decompressed_arm7_wifi_code);
    free(modified_decompressed_data_gfx);
    
    free(compressed_arm9_gui_code);
    free(compressed_arm7_wifi_code);
    free(compressed_data_gfx);
    
    // Free main firmware buffer
    free(firmware);
    
    // Close any open file pointer
    if (f) fclose(f);
    
    return 0;
}
    