#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "firmware.h"
#include "compression.h"
#include "crc.h"

static void help(char *name) {
	printf("DS firmware data_gfx data extractor and injector\n");
	printf("Usage:\n");
	printf("%s firmware.bin [-e or -i] data_gfx.bin\n", name);
	printf(" firmware.bin: filename of firmware\n");
	printf(" -e: extract data_gfx data\n");
	printf(" -i: inject data_gfx data\n");
	printf(" data_gfx.bin: output or input of data_gfx data\n");
}

int main(int argc, char **argv) {
	enum {
		unspecified,
		extract,
		inject,
	} mode;
	
	unsigned char *firmware;
	
	char *firmwareFilename = NULL;
	char *arm9_gui_code_Filename = "arm9_gui_code.bin";
	char *arm7_wifi_code_Filename = "arm7_wifi_code.bin";
	char *data_gfx_Filename = "data_gfx.bin";
	
	if(argc != 3) {
		help(argv[0]);
		return 1;
	}
	
	firmwareFilename = argv[1];
	if(strcmp(argv[2], "-e") == 0) mode = extract;
	if(strcmp(argv[2], "-i") == 0) mode = inject;
	
	if(mode == unspecified) {
		help(argv[0]);
		return 1;
	}
	
	FILE *f = fopen(firmwareFilename, "rb");
	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	rewind(f);
	firmware = malloc(size);
	fread(firmware, size, 1, f);
	fclose(f);
	
	if(mode == extract) {
		unsigned int arm9_gui_code_Offset, arm7_wifi_code_Offset, data_gfx_Offset;
		unsigned char *arm9_gui_code, *arm7_wifi_code, *data_gfx;
		size_t arm9_gui_code_Size, arm7_wifi_code_Size, data_gfx_Size;
		
		arm9_gui_code_Offset = ((struct header *)firmware)->part3offset * 8;
		arm7_wifi_code_Offset = ((struct header *)firmware)->part4offset * 8;
		data_gfx_Offset = ((struct header *)firmware)->part5offset * 8;
		
		arm9_gui_code_Size = decompress(NULL, firmware + arm9_gui_code_Offset);
		arm7_wifi_code_Size = decompress(NULL, firmware + arm7_wifi_code_Offset);
		data_gfx_Size = decompress(NULL, firmware + data_gfx_Offset);

		arm9_gui_code = malloc(arm9_gui_code_Size);
		arm7_wifi_code = malloc(arm7_wifi_code_Size);
		data_gfx = malloc(data_gfx_Size);
		
		decompress(arm9_gui_code, firmware + arm9_gui_code_Offset);
		decompress(arm7_wifi_code, firmware + arm7_wifi_code_Offset);
		decompress(data_gfx, firmware + data_gfx_Offset);
		
		f = fopen(arm9_gui_code_Filename, "wb");
		fwrite(arm9_gui_code, arm9_gui_code_Size, 1, f);
		fclose(f);
		f = fopen(arm7_wifi_code_Filename, "wb");
		fwrite(arm7_wifi_code, arm7_wifi_code_Size, 1, f);
		fclose(f);
		f = fopen(data_gfx_Filename, "wb");
		fwrite(data_gfx, data_gfx_Size, 1, f);
		fclose(f);
		
		printf("Decompressed arm9_gui_code from %08x, size %d\n", arm9_gui_code_Offset, (int)arm9_gui_code_Size);
		printf("Decompressed arm7_wifi_code from %08x, size %d\n", arm7_wifi_code_Offset, (int)arm7_wifi_code_Size);
		printf("Decompressed data_gfx from %08x, size %d\n", data_gfx_Offset, (int)data_gfx_Size);
		
		free(data_gfx);
	}
	
	else {
		unsigned char *data_gfx, *compressed;
		size_t data_gfx_Size, compressedSize;
		
		unsigned char *firmware;
		unsigned int data_gfx_Offset;
		
		FILE *f = fopen(data_gfx_Filename, "rb");
		fseek(f, 0, SEEK_END);
		data_gfx_Size = ftell(f);
		rewind(f);
		data_gfx = malloc(data_gfx_Size);
		fread(data_gfx, data_gfx_Size, 1, f);
		fclose(f);
		
		compressed = malloc(256 * 1024);
		compressedSize = compress(compressed, data_gfx, data_gfx_Size);
		
		printf("Compressed data_gfx data to size %d\n", (int)compressedSize);
		
		f = fopen(firmwareFilename, "rb");
		fseek(f, 0, SEEK_END);
		size_t size = ftell(f);
		rewind(f);
		firmware = malloc(size);
		fread(firmware, size, 1, f);
		fclose(f);
		
		data_gfx_Offset = ((struct header *)firmware)->part5offset * 8;
		
		// data_gfx data is not the final section, after it comes apps like pictochat
		/*if(data_gfx_Offset + compressedSize > size) {
			printf("ERROR! Not enough space for compressed data_gfx data!\n");
			free(firmware);
			free(compressed);
			free(data_gfx);
			return 1;
		}
		
		memset(firmware + data_gfx_Offset, 0xff, 0x0003fa00 - data_gfx_Offset);
		*/
		
		memcpy(firmware + data_gfx_Offset, compressed, compressedSize);
		
		((struct header *)firmware)->part5crc = swiCRC(0xffff, (u32 *)(firmware + data_gfx_Offset), compressedSize);
		
		f = fopen(firmwareFilename, "wb");
		fwrite(firmware, size, 1, f);
		fclose(f);
		
		free(compressed);
		free(data_gfx);
	}
	
	free(firmware);
	
	return 0;
}