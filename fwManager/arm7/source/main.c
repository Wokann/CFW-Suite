#include <stdlib.h>
#include <nds.h>
#include <dswifi7.h>
#include <maxmod7.h>

#include "interrupts.h"
#include "nvram.h"

static void wait(int vblanks) {
	// without any delay at all, it will not flash correctly
	// but 5 vblanks may not be necessary
	// reduce this number at your own risk
	
	int i;
	for(i = 0; i < vblanks; i++) swiWaitForVBlank();
}
//////////////////////////////////////////////////////////////////////

#define SERIAL_CR      (*(vuint16*)0x040001C0)
#define SERIAL_CR32    (*(vuint32*)0x040001C0)
#define SERIAL_DATA    (*(vuint16*)0x040001C2)
#define SERIAL_DATA8   (*(vuint8*)0x040001C2)

#define SERIAL_ENABLE   0x8000
#define SERIAL_BUSY     0x80

//////////////////////////////////////////////////////////////////////
////////////////////////////
// SPI
////////////////////////////

#define SPI_EN		0x8000
#define SPI_I		0x4000
//#define SPI_BUSY	0x0080
#define SPI_SEL		0x0800
#define SPI_POWER	0
#define SPI_FW		0x0100
#define SPI_TOUCH	0x0200
#define SPI_MIC		0x0300

#define PM_SOUND		0x01
#define PM_BOTTOMLIGHT	0x04
#define PM_TOPLIGHT		0x08
#define PM_WIFI			0x10
#define PM_OFF			0x40

/////////////////////////
unsigned char writereadSPI(u8 data) {
	while (SERIAL_CR & SERIAL_BUSY);
	SERIAL_DATA = data;
	while (SERIAL_CR & SERIAL_BUSY);
	return SERIAL_DATA;
	
}
unsigned char PMwrite(unsigned char channel, unsigned char data) {
	unsigned char ret;
	SERIAL_CR = SPI_EN|SPI_SEL|SPI_POWER|2;
	writereadSPI(channel);
	SERIAL_CR = SPI_EN|SPI_POWER|2;
	ret=writereadSPI(data);	
	SERIAL_CR = 0;
	return ret;
}

void flash(unsigned char *firmware, unsigned int address, unsigned int endAddress) {
	unsigned int i = address;
	int result = 0;
	int lastPercent = -1;
	int currentPercent = 0;
	while(i < endAddress) {
		result = writeFirmwarePage(i, firmware + i);
		if(!result) {	//data not same
			i += 256;
		}
		else if (result == 2){ //data same
			i += 256;
		}
		else {
			wait(5);
		}

		currentPercent = (int)((double)i / endAddress * 100);
		if (currentPercent >= 0 && currentPercent > lastPercent) {
			fifoSendValue32(FIFO_USER_02, i);
			lastPercent = currentPercent;
			wait(1);
		}
	}
}

int main(void) {
	readUserSettings();
	
	irqInit();
	initClockIRQ();
	fifoInit();
	
	mmInstall(FIFO_MAXMOD);
	
	SetYtrigger(80);
	
	installWifiFIFO();
	installSoundFIFO();
	
	installSystemFIFO();
	
	setInterrupts();
	
	while(!quit) {
		if(fifoCheckValue32(FIFO_USER_01)) {
			unsigned char *firmware = (unsigned char *)fifoGetValue32(FIFO_USER_01);
			unsigned int address = fifoGetValue32(FIFO_USER_01);
			unsigned int endAddress = fifoGetValue32(FIFO_USER_01);
			flash(firmware, address, endAddress);
		}

		if(fifoCheckValue32(FIFO_USER_03)) {
			unsigned int address = fifoGetValue32(FIFO_USER_03);
			unsigned char *destination = (unsigned char *)fifoGetValue32(FIFO_USER_03);
			size_t length = fifoGetValue32(FIFO_USER_03);
			
			readFirmware(address, destination, length);

			fifoSendValue32(FIFO_USER_04, 1);
		}
		
		if(fifoCheckValue32(FIFO_USER_05)) {
			unsigned char channel = (unsigned char)fifoGetValue32(FIFO_USER_05);
			unsigned char data = (unsigned char)fifoGetValue32(FIFO_USER_05);
			unsigned char console = PMwrite(channel,data);
			fifoSendValue32(FIFO_USER_06,(u32)console);
		}
		
		swiIntrWait(1, IRQ_FIFO_NOT_EMPTY | IRQ_VBLANK);
	}
	return 0;
}
