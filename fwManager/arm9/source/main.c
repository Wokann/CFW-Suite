#include <stdio.h>
#include <stdlib.h>
#include <nds.h>
#include <fat.h>

#include "firmware.h"
#include "fileSelector.h"

#include "main.h"

PrintConsole *console;

static unsigned char flashing = 0;

void firmwareRead(unsigned int address, unsigned char *destination, size_t length) {
	fifoSendValue32(FIFO_USER_03, address);
	fifoSendValue32(FIFO_USER_03, (u32)destination);
	fifoSendValue32(FIFO_USER_03, length);
    // 等待并获取响应
    while(!fifoCheckValue32(FIFO_USER_04)) {
        swiWaitForVBlank();
    }
	fifoGetValue32(FIFO_USER_04);
}

#define readPM(chan) writePM((chan)|0x80,0)
unsigned char writePM(unsigned char channel, unsigned char data) {
	fifoSendValue32(FIFO_USER_05, (u32)channel);
	fifoSendValue32(FIFO_USER_05, (u32)data);
    // 等待并获取响应
    while(!fifoCheckValue32(FIFO_USER_06)) {
        swiWaitForVBlank();
    }
    return (unsigned char)fifoGetValue32(FIFO_USER_06);
}

void startFlash(unsigned char *firmware) {
	consoleClear();
	printf("\n Flashing firmware!\n\n Keep SL1 terminal shorted to\n progress.\n");
	flashing = 1;
	fifoSendValue32(FIFO_USER_01, (u32)firmware);
}

unsigned char is512firmware(unsigned char system) {
	if (system == 0x43 || system == 0x63 || system == 0x35)
		return 1;
	return 0;
}

int main(void) {
	console = consoleDemoInit();
	
	printf("\n fwManager - CTurt\n");
	printf(" =================\n\n");
	if (isDSiMode()) {
		printf(" Cannot use on DSi/3DS!\n");
		while(1) swiWaitForVBlank();
	} //let's be honest, we all know this is for fat DSes and lites, this check only gives me errors when compiling the soft
	
	printf(" Warning!\n This tool may damage your\n system! Use at your own risk!\n\n");
	
	if(!fatInitDefault()) {
		printf(" Could not init FAT!\n");
	}
	
	printf("\n Press A to continue.");
	do {
		scanKeys();
		swiWaitForVBlank();
	} while(keysHeld() & KEY_A);
	do {
		scanKeys();
		swiWaitForVBlank();
	} while(!(keysHeld() & KEY_A));
	
	do {
		scanKeys();
		swiWaitForVBlank();
	} while(keysHeld() & KEY_A);

FirmwareSelect:
	consoleClear();
	char *firmwareFilename = selectFirmware();
	if(!firmwareFilename) {
		while(1) swiWaitForVBlank();
	}

	// 定义烧录选项枚举
	typedef enum {
		RESERVE_WIFI_CALIBRATION,   		// 保留wifi校准数据
		RESERVE_WIFI_CA_WIFI_SETTINGS, 		// 保留wifi校准数据、WiFi设置
		RESERVE_WIFI_CA_USER_SETTINGS, 		// 保留wifi校准数据、user设置
		RESERVE_WIFI_CA_WIFI_USER_SETTINGS, // 保留wifi校准数据、WiFi及user设置
		FLASH_COMPLETE,    					// 烧录全部
		FLASH_OPTION_COUNT     				// 选项总数
	} FlashOption;

	printf("\n Select flash option:\n\n");
	printf(" >Reserve Wifi Calibration only\n");
	printf("  Reserve Wifi Ca. & Wifi Settings\n");
	printf("  Reserve Wifi Ca. & User Settings\n");
	printf("  Reserve Wifi Ca. & Wifi&User Settings\n");
	printf("  Flash complete firmware\n\n");

	int modeSelect = RESERVE_WIFI_CALIBRATION;
	// 菜单循环
	while(1) {
		scanKeys();
		u16 keys = keysDown();
		
		// 处理上下键 - 支持循环切换
		if (keys & KEY_UP) {
			modeSelect = (modeSelect - 1 + FLASH_OPTION_COUNT) % FLASH_OPTION_COUNT;
		}
		if (keys & KEY_DOWN) {
			modeSelect = (modeSelect + 1) % FLASH_OPTION_COUNT;
		}
		
		// 确认选择
		if (keys & KEY_A) {
			break;
		}
		// 确认选择
		if (keys & KEY_B) {
			goto FirmwareSelect;
		}
		
		// 更新菜单显示
		consoleClear();
		printf("\n Select flash option:\n\n");
		
		// 显示所有选项，当前选中的前面加">"
		for (int i = 0; i < FLASH_OPTION_COUNT; i++) {
			printf(" %s", i == modeSelect ? ">" : " ");
			
			switch(i) {
				case RESERVE_WIFI_CALIBRATION:
					printf("Reserve Wifi Calibration only\n");
					break;
				case RESERVE_WIFI_CA_WIFI_SETTINGS:
					printf("Reserve Wifi Ca. & Wifi Settings\n");
					break;
				case RESERVE_WIFI_CA_USER_SETTINGS:
					printf("Reserve Wifi Ca. & User Settings\n");
					break;
				case RESERVE_WIFI_CA_WIFI_USER_SETTINGS:
					printf("Reserve Wifi Ca. & Wifi&User Settings\n");
					break;
				case FLASH_COMPLETE:
					printf("Flash complete firmware\n");
					break;
			}
		}
		swiWaitForVBlank();  // 重要：等待垂直空白，确保显示更新
	}
	
	consoleClear();

	FILE *f = fopen(firmwareFilename, "rb");
	if(!f) {
		printf(" Could not open file!\n");
		while(1) swiWaitForVBlank();
	}
	
	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	if(size < 0x3fe00) {
		printf(" Firmware is too small!\n");
		while(1) swiWaitForVBlank();
	}
	if(size > 0x80000) {
		printf(" Firmware is too large!\n");
		while(1) swiWaitForVBlank();
	}
	
	//read originalFirmware
	unsigned char *originalFirmware = malloc(0x80000); 
	if(!originalFirmware) {
		printf(" Malloc failed!\n");
		while(1) swiWaitForVBlank();
	}
	memset(originalFirmware, 0xFF, 0x80000);
	// 输出分配的内存地址
	printf("oFW loca: 0x%08lX\n", (unsigned long)originalFirmware);
	firmwareRead(0, originalFirmware, 0x200);
	//for(int i = 0; i < 60 * 1 ; i++)
	//	swiWaitForVBlank();
	// 输出前0x20字节的十六进制值
	printf("First 0x20 bytes:\n");
	for (int j = 0; j < 0x20; j++) {
		printf("%02X", originalFirmware[j]);
	}
	size_t originalsize = is512firmware(originalFirmware[0x1D]) == 0 ? 0x40000 : 0x80000;
	// 输出固件的大小
	printf("oFW size: %s\n", originalsize == 0x40000 ? "256KB (0x40000)" : "512KB (0x80000)");
	firmwareRead(0, originalFirmware, originalsize);
	//for(int i = 0; i < 60 * 2 ; i++)
	//	swiWaitForVBlank();
	// 输出wifi部分0x20字节的十六进制值
	printf("oFW wifi 0x20 bytes:\n");
	for (int j = 0; j < 0x20; j++) {
		printf("%02X", originalFirmware[j + originalsize - 0x600 + 0xE0]);
	}

	//read firmware
	unsigned char *firmware = malloc(0x80000); 
	if(!firmware) {
		printf(" Malloc failed!\n");
		while(1) swiWaitForVBlank();
	}
	memset(firmware, 0xFF, 0x80000);
	// 输出分配的内存地址
	printf("nFW loca: 0x%08lX\n", (unsigned long)firmware);
	rewind(f);
	fread(firmware, size, 1, f);
	fclose(f);
	// 输出前0x20字节的十六进制值
	printf("First 0x20 bytes:\n");
	for (int j = 0; j < 0x20; j++) {
		printf("%02X", firmware[j]);
	}
	size_t newsize = is512firmware(firmware[0x1D]) == 0 ? 0x40000 : 0x80000;
	// 输出固件的大小
	printf("nFW size: %s\n", newsize == 0x40000 ? "256KB (0x40000)" : "512KB (0x80000)");

	//reserve wi-fi and settings
	int i;
	if (modeSelect != FLASH_COMPLETE) {
		for(i = 0; i < (0x163 - 0x2A); i++) {
			firmware[i + 0x2A] = originalFirmware[i + 0x2A];
		}
	}
	if (modeSelect == RESERVE_WIFI_CA_WIFI_SETTINGS || 
		modeSelect == RESERVE_WIFI_CA_WIFI_USER_SETTINGS) {
		for(i = 0; i < 0x400; i++) {
			firmware[i + newsize - 0x600] = originalFirmware[i + originalsize - 0x600];
		}
		// 输出wifi部分0x20字节的十六进制值
		//printf("oFW wifi 0x20 bytes:\n");
		//for (int j = 0; j < 0x20; j++) {
		//	printf("%02X", originalFirmware[j + originalsize - 0x600 + 0xE0]);
		//}
		// 输出wifi部分0x20字节的十六进制值
		printf("nFW wifi 0x20 bytes:\n");
		for (int j = 0; j < 0x20; j++) {
			printf("%02X", firmware[j + newsize - 0x600 + 0xE0]);
		}
	}
	if (modeSelect == RESERVE_WIFI_CA_USER_SETTINGS || 
		modeSelect == RESERVE_WIFI_CA_WIFI_USER_SETTINGS) {
		for(i = 0; i < 0x200; i++) {
			firmware[i + newsize - 0x200] = originalFirmware[i + originalsize - 0x200];
		}
		// 输出wifi部分0x20字节的十六进制值
		//printf("oFW wifi 0x20 bytes:\n");
		//for (int j = 0; j < 0x20; j++) {
		//	printf("%02X", originalFirmware[j + originalsize - 0x600 + 0xE0]);
		//}
		// 输出wifi部分0x20字节的十六进制值
		printf("nFW user 0x20 bytes:\n");
		for (int j = 0; j < 0x20; j++) {
			printf("%02X", firmware[j + newsize - 0x200]);
		}
	}

	//Check console
	unsigned int Hconsole = readPM(4);
			printf("\nconsloe:%08X\n", Hconsole);
	if(((struct header *)firmware)->console == 0x57 ||    //DSi
		(((struct header *)firmware)->console != 0xFF &&  //DS phat
		 ((struct header *)firmware)->console != 0x20 &&  //DS lite
		 ((struct header *)firmware)->console != 0x43 &&  //iQue phat
		 ((struct header *)firmware)->console != 0x63 &&  //iQue lite
		 ((struct header *)firmware)->console != 0x35)) {	//Kor lite
		printf(" This firmware is not for this console!\n");
		printf(" This firmware is for type %d (%s)\n", ((struct header *)firmware)->console, ((struct header *)firmware)->console == 0x57 ? "DSi" : "Unknown");
		while(1) swiWaitForVBlank();
	}
	
	// To do: check boot CRC is correct
	/*
	unsigned short crc;
	// decrypt and decompress arm 7 and 9 binaries
	crc = swiCRC16(0xffff, part1, part1size);
	crc = swiCRC16(crc, part2, part2size);
	if(((struct header *)firmware)->part12crc != crc) {
		printf(" Incorrect boot CRC!\n");
		while(1) swiWaitForVBlank();
	}
	*/

	do {
		scanKeys();
		swiWaitForVBlank();
	} while(keysHeld() & KEY_START);
	
	do {
		scanKeys();
		swiWaitForVBlank();
	} while(!(keysHeld() & KEY_START));
	
	do {
		scanKeys();
		swiWaitForVBlank();
	} while(keysHeld() & KEY_START);
	printf("\n Press Start to flash:\n\n %s\n\n", firmwareFilename);
	
	do {
		scanKeys();
		swiWaitForVBlank();
	} while(keysHeld() & KEY_START);
	
	do {
		scanKeys();
		swiWaitForVBlank();
	} while(!(keysHeld() & KEY_START));
	
	do {
		scanKeys();
		swiWaitForVBlank();
	} while(keysHeld() & KEY_START);
	
	startFlash(firmware);
	
	while(1) {
		if(flashing) {
			if(fifoCheckValue32(FIFO_USER_02)) {
				unsigned int progress = fifoGetValue32(FIFO_USER_02);
				
				console->cursorX = 1;
				console->cursorY = 6;
				printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n\n", firmware[0x36],firmware[0x37],firmware[0x38],firmware[0x39],firmware[0x3A],firmware[0x3B]);
				printf(" oFirmware type: %s\n", originalFirmware[0x1D] == 0xFF ? "DS phat" : originalFirmware[0x1D] == 0x20 ? "DS lite" : originalFirmware[0x1D] == 0x43 ? "iQue phat" : originalFirmware[0x1D] == 0x63 ? "iQue lite" : originalFirmware[0x1D] == 0x35 ? "Kor lite" : "Unknown");
				printf(" oFirmware size: %s\n", is512firmware(originalFirmware[0x1D]) == 0 ? "256KB (0x40000)" : "512KB (0x80000)");
				printf(" Firmware type: %s\n", firmware[0x1D] == 0xFF ? "DS phat" : firmware[0x1D] == 0x20 ? "DS lite" : firmware[0x1D] == 0x43 ? "iQue phat" : firmware[0x1D] == 0x63 ? "iQue lite" : firmware[0x1D] == 0x35 ? "Kor lite" : "Unknown");
				printf(" Firmware size: %s\n", is512firmware(firmware[0x1D]) == 0 ? "256KB (0x40000)" : "512KB (0x80000)");
				printf("\n Progress: %d%% (%d/%d)", (int)((double)progress / newsize * 100), progress, newsize);
				
				if(progress == newsize) {
					printf("\n\n Done!\n");
					flashing = 0;
				}
			}
		}
		
		swiWaitForVBlank();
		scanKeys();
	}
	
	free(originalFirmware);
	free(firmware);
	
	return 0;
}
