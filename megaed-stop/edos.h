/* 
 * File:   edos.h
 * Author: krik
 *
 * Created on 29 Март 2012 г., 1:29
 */

#ifndef _EDOS_H
#define	_EDOS_H

//#include "segalib.h"

/*
 * saddr: address in sectors
 * slen: lenght in sectors
 * wr_slen: how many sectors will be written
 * sector size 512 bytes
 * dma reading of usb and SD available only if data destination located in rom area (0-0x3ffffff). dma reading not available for genesis ram
 * destination address for dma reading must be aligned to 512b
 * one mbyte of sdram can be mapped to one of four banks in rom space. osSetMemMap(u16) can be used for mapping configuration.
 * os code located in last mbyte of sdram and should be mapped in bank 0
 * user code located in begin of sdram. default mapping 0x210f means that end of sdram (OS) mapped to area 0-0x0fffff, first mbyte of user code mapped to 0x100000-0x1fffff,
 * memory configuration:
 * genesis address: 0x000000-0x0fffff, physical sdram address: 0xf00000-0xffffff  OS
 * genesis address: 0x100000-0x3fffff, physical sdram address: 0x000000-0x2fffff  user code (current application)
 * 0xff0000-0xff00ff OS dma code
 * first 256bytes of genesis ram must be reserved for OS
 * */


#define MEM_ERR_SPI_RD_TIMEOUT 120

#define ERR_FILE_TOO_BIG 140
#define ERR_WRON_OS_SIZE 142
#define ERR_OS_VERIFY 143
#define ERR_OS_VERIFY2 144
#define ERR_BAD_DMA_ADDRESS 145
#define ERR_MUST_NOT_RET 146

//100 - 119 fat errors
#define FAT_ERR_NOT_EXIST 100
#define FAT_ERR_EXIST 101
#define FAT_ERR_NAME 102
#define FAT_ERR_OUT_OF_FILE 103
#define FAT_ERR_BAD_BASE_CLUSTER 104;
#define FAT_ERR_NO_FRE_SPACE 105
#define FAT_ERR_NOT_FILE 106
#define FAT_ERR_FILE_MODE 107
#define FAT_ERR_ROT_OVERFLOW 108
#define FAT_ERR_OUT_OF_TABLE 109
#define FAT_ERR_INIT 110
#define FAT_LFN_BUFF_OVERFLOW 111
#define FAT_DISK_NOT_READY 112
#define FAT_ERR_SIZE 113
#define FAT_ERR_RESIZE 114


#define DISK_ERR_INIT 50
#define DISK_ERR_RD1 62
#define DISK_ERR_RD2 63

#define DISK_ERR_WR1 64
#define DISK_ERR_WR2 65
#define DISK_ERR_WR3 66
#define DISK_ERR_WR4 67
#define DISK_ERR_WR5 68

typedef struct {
    u32 entry_cluster;
    u32 size;
    u32 hdr_sector;
    u16 hdr_idx;
    u8 name[256];
    u16 is_dir;
} FatFullRecord;

typedef struct {
    u8(*diskWrite)(u32 saddr, u8 *buff, u16 slen);
    u8(*diskRead)(u32 saddr, void *buff, u16 slen);//*dst must be alligned to 512bytes and be in range 0x100000-0x3fffff. *dst can be located in genesis ram also, but transfer will be slow in this case
    u8(*usbReadByte)();//loop inside of function waiting until usbRdReady != 0
    void (*usbWriteByte)(u8 dat);//loop inside of function waiting until usbWrReady != 0
    u8(*usbReadDma)(u16 *dst, u16 slen);//*dst must be alligned to 512bytes and be in range 0x100000-0x3fffff
    u8(*usbReadPio)(u16 *dst, u16 slen);
    u8(*usbRdReady)(); //return 1 if some data comes from pc
    u8(*usbWrReady)(); //return 1 usb ready to send byte
    void (*osSetMemMap)(u16 map); //memoty configuration 4x1Mb
    u16(*osGetOsVersion)();
    u16(*osGetFirmVersion)();
    void (*osGetSerial)(u32 * buff); // 8bytes uniq serial number
    void (*VDP_setReg)(u8 reg, u8 value);//os vdp functions should be used, otherwise system may hang while udb/sd dma
    void (*VDP_vintOn)();
    void (*VDP_vintOff)();
    void (*memInitDmaCode)();//copy dma routine to genesis ram
    u8(*fatReadDir)(FatFullRecord * frec);//get next record in current dir
    void(*fatOpenDir)(u32 entry_cluster);//arg is entry cluster ot dir or 0. zero arg means that need to open root dir
    u8(*fatOpenFileByeName)(u8 *name, u32 wr_slen);//wr_slen write len, or 0 if reading
    u8(*fatOpenFile)(FatFullRecord *rec, u32 wr_slen);//wr_slen is write len, or 0 if reading
    u8(*fatReadFile)(void *dst, u32 slen);
    u8(*fatWriteFile)(void *src, u32 slen);
    u8(*fatCreateRecIfNotExist)(u8 *name, u8 is_dir);
    u8(*fatSkipSectors)(u32 slen);
} OsRoutine;


extern OsRoutine *ed_os;
void edInit();


//stereo dac. 8bit for each channel.
//any writing to this port enables dac output, any reading from this port disables dac output.
#define REG_DAC *((volatile u16*) (0xA1300E))


//stuff for direct access to usb and spi. not recommended for use. OS function should be used for best compatibility
#define _SPI_SS 0
#define _SPI_FULL_SPEED 1
#define _SPI_SPI16 2
#define _SPI_AREAD 3

//spi port (sd interface)
#define SPI_PORT *((volatile u16*) (0xA13000))
#define SPI_CFG_PORT *((volatile u16*) (0xA13002))

//16bit or 8bit spi mode
#define SPI16_ON SPI_CFG_PORT |= (1 << _SPI_SPI16)
#define SPI16_OFF SPI_CFG_PORT &= ~(1 << _SPI_SPI16)

//spi autoread. means that not need to write something to spi port before than read
#define SPI_AR_ON SPI_CFG_PORT |= (1 << _SPI_AREAD)
#define SPI_AR_OFF SPI_CFG_PORT &= ~(1 << _SPI_AREAD)

//spi chip select
#define SPI_SS_OFF SPI_CFG_PORT |= (1 << _SPI_SS)
#define SPI_SS_ON SPI_CFG_PORT &= ~(1 << _SPI_SS)

//spi speed. low speed need only for sd init
#define SPI_HI_SPEED_ON SPI_CFG_PORT |= (1 << _SPI_FULL_SPEED)
#define SPI_HI_SPEED_OFF SPI_CFG_PORT &= ~(1 << _SPI_FULL_SPEED)

//usb-serial port
#define FIFO_PORT *((volatile u16*) (0xA1300A))

//spi and usb state
#define STATE_PORT *((volatile u16*) (0xA1300C))
#define _STATE_SPI_READY 0
#define _STATE_FIFO_WR_READY 1
#define _STATE_FIFO_RD_READY 2
#define IS_FIFO_RD_READY (STATE_PORT & (1 << _STATE_FIFO_RD_READY))
#define IS_FIFO_WR_READY (STATE_PORT & (1 << _STATE_FIFO_WR_READY))


#endif	/* _EDOS_H */

