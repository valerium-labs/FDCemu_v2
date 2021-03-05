/*
	Terms:
	  * Sector - a tr-dos sector is 256 bytes length, has number 1-16
	  * Block - minimum amount of bytes on SD/TF card that can be read or written is 512 bytes

*/


#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wd1793.h"
#include "petitfs/PetitFS.h"
#include "print.h"
#include "lcd_lib.h"

#define  SectorLength      256U
#define  SectorsPerTrack   16U
#define  Sides				2U
#define  S_SIZE_TO_CODE(x)  ((((x)>>8)|(((x)&1024)>>9)|(((x)&1024)>>10))&3)
#define  S_CODE_TO_SIZE(x)  (1<<(7+(x)))
#define  BYTE_READ_TIME 64U

#define  BufferSize 512U
#define  BufferSizeFactor (BufferSize/SectorLength)
#define  DefaultDiskPos 0xFFFFU
#define  REVOLUTION_TIME 6U
#define  TIME_PER_SECTOR 1U
#define  STEP_TIME 6000U
#define  MIN(a,b) ((a<b)?(a):(b))

const uint8_t Turbo = 0;
uint16_t Delay;
uint8_t IndexCounter = 0;
uint16_t TimerValue;
uint16_t BufferPos = 0;
uint16_t SectorPos = 0;
uint16_t pos;
uint8_t NoDisk = 1;
uint8_t BufferUpdated = 0;
uint16_t FormatCounter = 0;
uint8_t NewDrive = 5;
const uint8_t ControlDrive = 3; // D:
//uint8_t SelectedDrive = 0; // default drive A:
//char * SelectedImage = NULL;
extern uint8_t SelectedDrive;
extern char * SelectedImage;
char Path[32] = "";



FATFS fss;
DIR dir;
FILINFO finfo;
FRESULT res;
uint8_t FilesCount = 0;
uint8_t buf[BufferSize];
uint16_t br;
uint8_t CmdType = 1;
uint16_t CurrentDiskPos = DefaultDiskPos; // Big enough to be outside of disk

WD1793_struct WD1793;


uint8_t Requests = 0;
uint8_t SIDE = 0;
uint8_t DRV = 5;
unsigned char PrevPortFF = 0xFF;

void WD1793_CmdStartIdle();

uint8_t NewCommandReceived = 0;
typedef void (*Command) ();
Command CurrentCommand = WD1793_CmdStartIdle;
Command NextCommand = NULL; 
Command CommandTable[16] = {
		WD1793_Cmd_Restore, // 00
		WD1793_Cmd_Seek, // 10
		WD1793_Cmd_Step, // 20 Step
		WD1793_Cmd_Step, // 30 Step
		WD1793_Cmd_Step, // 40 Step In
		WD1793_Cmd_Step, // 50 Step In
		WD1793_Cmd_Step, // 60 Step Out
		WD1793_Cmd_Step, // 70 Step Out
		WD1793_Cmd_ReadSector, // 80
		WD1793_Cmd_ReadSector, // 90
		WD1793_Cmd_WriteSector, // A0
		WD1793_Cmd_WriteSector, // B0
		WD1793_Cmd_ReadAddress, // C0
		WD1793_CmdStartIdle, // D0 Force interrupt
		WD1793_Cmd_ReadTrack, // E0	
		WD1793_Cmd_WriteTrack // F0
	};


uint16_t GetTimerValue()
{
	uint16_t value;
	cli();
	value = TCNT1;
	sei();
	return value;
}

void StartTimePeriod()
{
	TimerValue = GetTimerValue();
}

uint16_t HasTimePeriodExpired(uint16_t period)
{
	uint16_t t = GetTimerValue();
	if(t - TimerValue >= period) 
	{
		TimerValue = t;
		return 1;
	}
	return 0;
}

void PrintTimePeriod(char * msg)
{
	uint16_t t = (GetTimerValue() - TimerValue)/2;
	if(msg != NULL)
	{
		printf(msg);
	}
	printf("%d\n", t);
}

ISR(TIMER1_OVF_vect)
{
	if(++IndexCounter > REVOLUTION_TIME) 
	{
		IndexCounter=0;
	}
	
	if(IndexCounter == REVOLUTION_TIME) 
	{
		TCNT1 = 58752; // correction for 5rps, 200 ms per revolution
	}
}

uint8_t WD1793_GetCurrentSector()
{
	uint8_t sector;
	cli();
	sector = ((IndexCounter << 8) | (GetTimerValue()>>8))/92U;
	sei();
	
	if(sector > 15)
	{
		sector = 15;
	}
	
	// 1,9,2,10,3,11,4,12,5,13,6,14,7,15,8,16
	return ((sector & 1) << 3 | (sector >> 1)) + 1; 
}

uint8_t WD1793_GetIndexMark()
{
	uint8_t idx;
	cli();
	idx = IndexCounter == 0 && GetTimerValue() < 6000;
	sei();
	return idx; 
}


void printError(char* msg, uint8_t code) 
{

	print(msg);
	switch(code) 
	{
		case FR_DISK_ERR:
		print("FR_DISK_ERR");
		break;
		case FR_NOT_READY:
		print("FR_NOT_READY");
		break;
		case FR_NO_FILE:
		print("FR_NO_FILE");
		break;
		case FR_NOT_OPENED:
		print("FR_NOT_OPENED");
		break;
		case FR_NOT_ENABLED:
		print("FR_NOT_ENABLED");
		break;
		case FR_NO_FILESYSTEM:
		print("FR_NO_FILESYSTEM");
		break;
	}
}

void WD1793_FlushBuffer()
{
	if (BufferUpdated) // flush partially filled buffer
	{
		pf_lseek((uint32_t)CurrentDiskPos << 8); // return to start of sector on SD Card
		res = pf_write(buf, BufferSize, &br);
		if (res) 
		{
			printError("Writing error: ", res);
			return;
		}
		pf_write(0, 0, &br);
		if (res) 
		{
			printError("Writing error on finish: ", res);
			return;
		}
		BufferUpdated = 0;
#ifdef DEBUG		
		printf("Flushed buffer\n");
#endif
	}
}

// position is address/256
uint16_t ComputeDiskPosition(uint8_t Sector, uint8_t Track, uint8_t Side)
{
	uint16_t addr = (((uint16_t)Track * Sides + (uint16_t)Side) * SectorsPerTrack + (uint16_t)Sector);
#ifdef DEBUG
	printf("Addr 0x%04X_%04X\n", (uint16_t)(addr>>8), (uint16_t)(addr<<8));
#endif
   return addr;
}

void SetDiskPosition(uint16_t pos)
{
	if(pos < CurrentDiskPos || pos >= (CurrentDiskPos + BufferSizeFactor)) 
		{
		pos &= ~(BufferSizeFactor-1); // round position to buffer size
		if(pos != (CurrentDiskPos + BufferSizeFactor)) // move position if only it is needed but it saves only 100 mcs
		{
			pf_lseek((uint32_t)pos << 8);
		}

		res = pf_read(buf, BufferSize, &br);
		if (res) 
		{
			printError("Reading error: ", res);
			return;
		}
		CurrentDiskPos = pos;
	}
}


void WD1793_Reset(uint8_t drive)
{
	char disk[32];
	uint16_t i, prev_space;
	uint8_t d, j; // drive index in config
	
	WD1793_FlushBuffer();
	NoDisk = 1;
	CurrentDiskPos = DefaultDiskPos;
#ifdef DEBUG	
	print("Mount FileSystem... ");
#endif
	res = pf_mount(&fss);
	if (res) 
	{
		printError("Error mount: ", res);

		//LCD Message ===================
		LCDclr();
		LCDGotoXY(0,0);
		LCDsendString(" No SD-card or");
		LCDGotoXY(0,1);
		LCDsendString(" bad FAT FS");
		//LCD Message ====================

		return;

	}
	
	res = pf_open("IMAGES.CFG");
	if (res) { // If can't open config looking for DISK<1-4>.TRD
		printError("Error open IMAGES.CFG: ", res);

		//LCD Message ===================
		LCDclr();
		LCDGotoXY(0,0);
		LCDsendString("  Can't read");
		LCDGotoXY(0,1);
		LCDsendString("  IMAGES.CFG");
		//LCD Message ====================
		
		sprintf(disk, "DISK%i.TRD", drive+1);
		print("Open File");
		print(disk);
		res = pf_open((const char *)disk);
		
		if (res) 
		{
			printError("Error open: ", res);
		}
		else 
		{
			NoDisk = 0;
		}
		return;		
	}	
	
	res = pf_read(buf, sizeof(buf), &br);
	if (res) 
	{
		printError("pf_read", res);
	}

	// parse config
	for(i=0, d=0, j=0, prev_space=BufferSize; i<br && d<4; i++)
	{
		if(buf[i] != '\n' && buf[i] != '\r' && buf[i] != 0) 
		{
			if(d == drive) 
			{
				disk[j++] = buf[i];

				if(j >= 31) 
				{
					disk[31] = 0;
					printf("Too long file name %s", disk);
				}
			}
		} 
		else 
		{
			if(prev_space+1 != i) 
			{
				d++;
			}
			prev_space = i;
			disk[j] = 0;
			j=0;
		}
		if(d > drive) 
		{
			break;
		}
	}

	if(disk[0] == '-' && disk[1] == 0)  // empty drive
	{
		NoDisk = 1;
		return;
	}

	if(drive == ControlDrive)  // control drive
	{
		NoDisk = 0;
		
		strncpy(Path, disk, 31);
		Path[31] = 0;
		printf(Path);

		return;
	}
	print("Open File");
	print(disk);
	res = pf_open((const char *)disk);
	if (res) 
	{
		printError("Error open: ", res);
	}
	else 
	{
		NoDisk = 0;

		//LCD Message ===================
		char strbuf[20]="\0";	//temp string buffer
		LCDclr();
		LCDGotoXY(0,0);
		sprintf (strbuf, "Drive: %c\0", 'A'+drive);
		LCDsendString(strbuf);
		LCDGotoXY(0,1);
		sprintf (strbuf, "%s\0", (char *) disk);
		LCDsendString(strbuf);
		//LCD Message ====================
	}	

}

void WD1793_UpdateConfig()
{
	uint16_t i, j, prev_space, start = BufferSize, end = 0;
	uint8_t d; // drive index in config
	
	if(SelectedImage == NULL)
	{
		return;
	}
	
	WD1793_FlushBuffer();
	NoDisk = 1;
	CurrentDiskPos = DefaultDiskPos;
	print("Mount FileSystem... ");
	res = pf_mount(&fss);
	if (res) 
	{
		printError("Error mount: ", res);
		return;
	}
	
	res = pf_open("IMAGES.CFG");
	if (res) // If can't open config looking for DISK<1-4>.TRD
	{
		printError("Error open IMAGES.CFG: ", res);
		return;
	}
	
	res = pf_read(buf, sizeof(buf), &br);
	if (res) 
	{
		printError("pf_read", res);
		return;
	}

	// parse config
	for(i=0, d=0, prev_space=BufferSize; i<br && d<4; i++)
	{
		if(buf[i] != '\n' && buf[i] != '\r' && buf[i] != 0) 
		{
			if(d == SelectedDrive) 
			{
				if(start == BufferSize)
				{
					start = i;
				}
				end = i;
			}
		}
		else 
		{
			if(prev_space+1 != i) 
			{
				d++;
			}
			prev_space = i;
		}
		if(d > SelectedDrive) 
		{
			break;
		}
	}
	++end; // preserve space
	
	j = strlen(SelectedImage);
	if(j == 0) 
	{
		printf("Empty image name\n");
		SelectedImage = NULL;
		return;
	}


        memmove(buf + start + j, buf + end, br - end);	//moving the rest of config to the position AFTER a new imagename to provide a gap
	memcpy(buf + start, SelectedImage, j);		//copying the new name into the gap prepared
	br = br + j - (end-start);			//calculating new block length

#ifdef DEBUG
	printf("Write config\n");
#endif	

	res = pf_write(buf, br, &br);
	if (res) 
	{
		printError("Error write config: ", res);
	}
	res = pf_write(0, 0, &br);
	if (res) 
	{
		printError("Error on finish write config: ", res);
	}	
	SelectedImage = NULL;

}

void WD1793_ReadDir(uint16_t pos)
{
    uint8_t i = pos ? 3 : 0; // file number 	
	uint16_t bufIndex = 0;
	uint16_t startFile = pos*16; 
	uint16_t endFile = startFile + 16;
	uint8_t len;
	const char ext = 'C';

	
	if(pos == 0) 
	{
		for (i=0; i<3; i++) // Fake files as drives
		{
			bufIndex = (i * 16) % SectorLength;
			memset(buf + bufIndex, ' ', 8);			
			buf[bufIndex] = 'A' + i;
			if(i == SelectedDrive) 
			{
				strncpy(buf + bufIndex + 1, " <--", 4);
			}
			bufIndex += 8;
			buf[bufIndex] = ext;			
			buf[++bufIndex] = 0; // start addr low byte
			buf[++bufIndex] = 0x40;  // start addr high byte
			buf[++bufIndex] = 1; // length
			buf[++bufIndex] = 0;
			buf[++bufIndex] = 1; // file length in sectors
			buf[++bufIndex] = 0; // Start sector
			buf[++bufIndex] = i + 1; // Start track, each track is a separate file
		}		
	}
	


    res = pf_opendir(&dir, Path);
    if (res != FR_OK) 
	{
		printError("Error open dir: ", res);
		FilesCount = 0;
	}
	
	while (i < 128) 
	{
			
		res = pf_readdir(&dir, &finfo);
		if (res != FR_OK || finfo.fname[0] == 0) break;
		if (!(finfo.fattrib & AM_DIR))  // read files only
		{
				
			if(strstr(finfo.fname, ".TRD") != NULL) 
			{
				if(i >= startFile && i < endFile)
				{
					bufIndex = (i * 16) % SectorLength;
					memset(buf + bufIndex, ' ', 8);
					len = strlen(finfo.fname);
					strncpy(buf + bufIndex, finfo.fname, MIN(len, 8));
					bufIndex += 8;
					buf[bufIndex] = ext;
					buf[++bufIndex] = 0; // start addr low byte
					buf[++bufIndex] = 0x40;  // start addr high byte
					buf[++bufIndex] = 1; // length
					buf[++bufIndex] = 0;
					buf[++bufIndex] = 1; // file length in sectors
					buf[++bufIndex] = 0; // Start sector
					buf[++bufIndex] = i + 1; // Start track, each track is a separate file		
				}
				i++;
			}
		}
	}
		
	FilesCount = i;

}

uint8_t WD1793_GetFilesCount()
{
	uint8_t i = 3; // file number

	if(FilesCount)
	{
		return FilesCount;
	}



	res = pf_opendir(&dir, Path);
	if (res != FR_OK) 
	{
		printError("Error open dir: ", res);
		return 0;
	}
	
	while (i < 128)
	{
		res = pf_readdir(&dir, &finfo);
		if (res != FR_OK || finfo.fname[0] == 0) break;
		if (!(finfo.fattrib & AM_DIR))  // read files only
		{
			
			if(strstr(finfo.fname, ".TRD") != NULL)
			{
				i++;
			}
		}
	}
	
	return i;
}

void WD1793_ReadDiskInfo()
{
#ifdef DEBUG	
	printf("Read disk info\n");
#endif	
	uint8_t i = 225;
	uint16_t FreeSectorsCount = (160 - 4 - FilesCount) * 15;
	FilesCount = WD1793_GetFilesCount();
	buf[i++] = 0;
	buf[i++] = FilesCount + 1; // first free track
	buf[i++] = 22; // disk type
	buf[i++] = FilesCount; // files count
	buf[i++] = (uint8_t)FreeSectorsCount; // free sectors count
	buf[i++] = (uint8_t)(FreeSectorsCount >> 8); // free sectors count high byte
	buf[i++] = 16; // TR-DOS Id
	
	memset(buf + 234, 32, 8);
	memcpy(buf + 245, "SD Card", 7);
}

void WD1793_LookUpFile(uint8_t track) // logical track 1-160
{
	uint8_t i = 3; // file number

	--track; // skip track 0
#ifdef DEBUG	
	printf("Read dir starting 0x%04X\n", track);
#endif
	
	if(track >= 0 && track < 3)
	{
		SelectedDrive = track;
#ifdef DEBUG		
		printf("Selected drive %c:\n", SelectedDrive);
#endif
		SelectedImage = NULL;
		return;
	}
	


	res = pf_opendir(&dir, Path);
	if (res != FR_OK) 
	{
		printError("Error open dir: ", res);
		FilesCount = 0;
	}
	
	while (i < 128) {
		
		res = pf_readdir(&dir, &finfo);
		if (res != FR_OK || finfo.fname[0] == 0) break;
		if (!(finfo.fattrib & AM_DIR)) { // read files only
			
			if(strstr(finfo.fname, ".TRD") != NULL)
			{
				if( i == track)
				{
#ifdef DEBUG
					printf("Selected file %s\n", finfo.fname);
#endif
					SelectedImage = finfo.fname;

					return;
				}
				i++;
			}
		}
	}

}




void WD1793_CmdIdle()
{
	switch(CmdType)
	{
		case 1:
		if (!NoDisk && WD1793_GetIndexMark()) { // Speccy checks rotation
			WD1793.StatusRegister |= _BV(stsIndexImpuls);
			} else {
			WD1793.StatusRegister &= ~_BV(stsIndexImpuls);
		}
		break;
	}
}

void WD1793_CmdStartIdle()
{
	WD1793.StatusRegister &= ~_BV(stsBusy); // clear bit
	Requests &= ~_BV(rqDRQ);
	Requests |= _BV(rqINTRQ);
	
// 	if(CmdType != 1 && WD1793.StatusRegister & _BV(stsLostData))
// 	{
// 		printf("Lost data, cmd=%02X\n", WD1793.CommandRegister);		
// 	}
	
	WD1793_FlushBuffer();
	CurrentCommand = WD1793_CmdIdle;
}


void WD1793_CmdStartIdleForceInt0()
{
	WD1793.StatusRegister &= ~_BV(stsBusy); // clear bit
	Requests &= ~_BV(rqDRQ);
	Requests &= ~_BV(rqINTRQ);
	
	WD1793_FlushBuffer();
	CurrentCommand = WD1793_CmdIdle;
}

void WD1793_CmdType1Status()
{

	if (WD1793.RealTrack == 0)
	WD1793.StatusRegister |= _BV(stsTrack0);
	
	if(WD1793.CommandRegister & 0x08) // head load
	{
		WD1793.StatusRegister |= _BV(stsLoadHead);
	}

	CurrentCommand = WD1793_CmdStartIdle;
}

void WD1793_CmdDelay()
{
	if (!HasTimePeriodExpired(Delay)) return;

	CurrentCommand = NextCommand;
}


void WD1793_CmdReadingSector()
{
	if(!HasTimePeriodExpired(BYTE_READ_TIME))
	{
		return;
	}		
	if(SectorPos != 0 && Requests & _BV(rqDRQ))
	{
		WD1793.StatusRegister |= _BV(stsLostData);			
	}
	        
	if (SectorPos >= SectorLength)
	{
		if(WD1793.Multiple) 
		{
			if(BufferPos != 0 && !(BufferPos % BufferSize)) 
			{
				res = pf_read(buf, BufferSize, &br);
				if (res) {
					printError("Reading error: ", res);
				}
			}
			SectorPos = 0;
			if(++WD1793.RealSector > SectorsPerTrack)
			{
				CurrentCommand = WD1793_CmdStartIdle;
				return;
			}
			CurrentCommand = WD1793_CmdStartReadingSector;
			return;
		}
		CurrentCommand = WD1793_CmdStartIdle;
		return;
	}
	        

	WD1793.DataRegister = buf[BufferPos % BufferSize];
	Requests |= _BV(rqDRQ);
	WD1793.StatusRegister |= _BV(stsDRQ);
	//printf(" %02X ", BufferPos);
	//printf("%c ", WD1793.DataRegister);
	BufferPos++;
	SectorPos++;
}

void WD1793_CmdWritingSector()
{
	if(!HasTimePeriodExpired(BYTE_READ_TIME))
	{
		return;
	}		
	if(Requests & _BV(rqDRQ))
	{
		//printf("Lost data in writing sector\n");
		WD1793.StatusRegister |= _BV(stsLostData);			
	}

	if(!(WD1793.StatusRegister & _BV(stsLostData)))
	{
		buf[BufferPos % BufferSize] = WD1793.DataRegister;
	}
	Requests |= _BV(rqDRQ);
	WD1793.StatusRegister |= _BV(stsDRQ);
	BufferUpdated = 1;
	BufferPos++;
	SectorPos++;
	        
	if (SectorPos >= SectorLength)
	{
		CurrentCommand = WD1793_CmdStartIdle;
	}
}

void WD1793_CmdWritingTrack()
{
	if(!HasTimePeriodExpired(BYTE_READ_TIME * 3))
	{
		return;
	}
	if(Requests & _BV(rqDRQ))
	{
		printf("Lost data in writing track Pos=%d\n", BufferPos);
		WD1793.StatusRegister |= _BV(stsLostData);
		CurrentCommand = WD1793_CmdStartIdle;
		return;
	}	
#ifdef DEBUG	
	//printf("%02X ", WD1793.DataRegister);
#endif
	
	if(WD1793.DataRegister == 0x4E) 
	{
		FormatCounter++;
	}
	else 
	{
		FormatCounter  = 0;
	}
	
	if (FormatCounter  > 400)
	{
		CurrentCommand = WD1793_CmdStartIdle;
		return;
	}
	
	//buf[BufferPos % BufferSize] = WD1793.DataRegister;
	//BufferUpdated = 1;

	BufferPos++;
	Requests |= _BV(rqDRQ);
	WD1793.StatusRegister |= _BV(stsDRQ);

}
	        
void WD1793_CmdReadingAddress()
{
	if(!HasTimePeriodExpired(BYTE_READ_TIME))
	{
		return;
	}
	if(SectorPos != 0 && Requests & _BV(rqDRQ))
	{
		//printf("Lost data in addr read\n");
		WD1793.StatusRegister |= _BV(stsLostData);
	}	 
	      
	if (BufferPos >= 6)
	{
		CurrentCommand = WD1793_CmdStartIdle;
		return;
	}
		       
	switch(BufferPos) 
	{
		case 0: // track
		WD1793.DataRegister = WD1793.TrackRegister;
		WD1793.SectorRegister = WD1793.TrackRegister;
		break;
		       
		case 1: // side always 0
		WD1793.DataRegister = 0;
		break;
		       
		case 2: // sector
		WD1793.DataRegister = WD1793_GetCurrentSector();
		break;
		       
		case 3: // sector size
		WD1793.DataRegister = 1; // 256 bytes
		break;
		       
		case 4: // checksum
		case 5:
		WD1793.DataRegister = 0;
		break;
	}
#ifdef DEBUG	
	printf("0x%02X ", WD1793.DataRegister);
#endif	
	BufferPos++;
	Requests |= _BV(rqDRQ);
	WD1793.StatusRegister |= _BV(stsDRQ);

}

void WD1793_Cmd_Restore() 
{
#ifdef DEBUG	
	printf("Restore\n");
#endif
	StartTimePeriod();
	WD1793.TrackRegister = 0;
	WD1793.RealTrack = 0;
	WD1793.Direction = 0;

	Delay = STEP_TIME;
	NextCommand = WD1793_CmdType1Status;
	CurrentCommand = WD1793_CmdDelay;
	CmdType = 1;
}

void WD1793_Cmd_Seek()
{
#ifdef DEBUG	
	printf("Seek to track %i\n", WD1793.DataRegister);
#endif
	StartTimePeriod();
	if (WD1793.TrackRegister > WD1793.DataRegister)
	WD1793.Direction = 1;
	else
	WD1793.Direction = 0;

	Delay = abs(WD1793.TrackRegister - WD1793.DataRegister) * 800;
	WD1793.TrackRegister = WD1793.DataRegister;
	WD1793.RealTrack = WD1793.TrackRegister;

	NextCommand = WD1793_CmdType1Status;
	CurrentCommand = WD1793_CmdDelay;
	CmdType = 1;
}

void WD1793_Cmd_Step()
{
	StartTimePeriod();

	switch (WD1793.CommandRegister & 0xF0) // Command Decoder
	{
		case 0x40:
		case 0x50: // Step In
		WD1793.Direction = 0;
		break;

		case 0x60:
		case 0x70: // Step Out
		WD1793.Direction = 1;
		break;
	}
	
	if (WD1793.Direction == 0 && WD1793.TrackRegister < 80)
	{
		WD1793.TrackRegister++;
	}
	if (WD1793.Direction == 1 && WD1793.TrackRegister > 0)
	{
		WD1793.TrackRegister--;
	}
#ifdef DEBUG
	printf("Step %s to track %i\n", WD1793.Direction ? "in" : "out", WD1793.TrackRegister);
#endif

	Delay = STEP_TIME;
	NextCommand = WD1793_CmdType1Status;
	CurrentCommand = WD1793_CmdDelay;
	CmdType = 1;
}

void WD1793_CmdStartReadingSector()
{
	if(!Turbo && WD1793_GetCurrentSector() != WD1793.RealSector)
	{
		return;
	}

	CurrentCommand = WD1793_CmdReadingSector;
	StartTimePeriod();
}

void WD1793_Cmd_ReadSector()
{
	WD1793.Multiple = WD1793.CommandRegister & 0x10;
	pos = ComputeDiskPosition(WD1793.SectorRegister-1, WD1793.TrackRegister, SIDE);
	WD1793.RealSector = WD1793.SectorRegister;

	if(DRV == ControlDrive)
	{
		if(pos >= 0 && pos < 0x8)
		{
			memset(buf, 0, SectorLength);
			WD1793_ReadDir(pos);
		}
		else if(pos >= 0x8 && pos < 0x9)
		{
			memset(buf, 0, SectorLength);
			WD1793_ReadDiskInfo();
		}
		else 
		{
			//printf("Look up file name\n");
			WD1793_LookUpFile((WD1793.TrackRegister<<1) | SIDE);
			WD1793_UpdateConfig();
		}
		BufferPos = 0;
	}
	else
	{
		BufferPos = (pos & (BufferSizeFactor-1)) << 8; // Position in buffer
		SetDiskPosition(pos);
	}
	SectorPos = 0;
	CurrentCommand = WD1793_CmdStartReadingSector;
	CmdType = 2;

#ifdef DEBUG		
		printf("RD SEC %i, TRK %i, SIDE %i - %ld\n", WD1793.SectorRegister, WD1793.TrackRegister, SIDE, (uint32_t) (pos<<8));
#endif

}

void WD1793_CmdStartWritingSector()
{
	if(!Turbo && WD1793_GetCurrentSector() != WD1793.SectorRegister)
	{
		return;
	}
	Requests |= _BV(rqDRQ);
	WD1793.StatusRegister |= _BV(stsDRQ);
	CurrentCommand = WD1793_CmdWritingSector;
	StartTimePeriod();
}

void WD1793_Cmd_WriteSector()
{
	WD1793.Multiple = WD1793.CommandRegister & 0x10;
	CmdType = 2;
	
	if(DRV == ControlDrive)
	{
		WD1793.StatusRegister |= _BV(stsWriteProtect);
		CurrentCommand = WD1793_CmdStartIdle;
		return;
	}
	
	pos = ComputeDiskPosition(WD1793.SectorRegister-1, WD1793.TrackRegister, SIDE);
	BufferPos = (pos & (BufferSizeFactor-1)) << 8;
	SetDiskPosition(pos);
	SectorPos = 0;
	
	Delay = BYTE_READ_TIME;
	NextCommand = WD1793_CmdStartWritingSector;
	CurrentCommand = WD1793_CmdDelay;
	CmdType = 2;	
	
#ifdef DEBUG	
	printf("WR SEC %i, TRK %i, SIDE %i - %ld\n", WD1793.SectorRegister, WD1793.TrackRegister, SIDE, (uint32_t) (pos<<8));
#endif
}

void WD1793_Cmd_ReadAddress()
{
#ifdef DEBUG
	printf("Read Address\n");
#endif
	BufferPos = 0;
	CurrentCommand = WD1793_CmdReadingAddress;
	CmdType = 3;
	StartTimePeriod();
}

void WD1793_Cmd_ReadTrack()
{
#ifdef DEBUG
	printf("RD TRACK %i, TRK %i, SIDE %i - %d\n", 
		WD1793.SectorRegister, WD1793.TrackRegister, SIDE, ComputeDiskPosition(WD1793.SectorRegister - 1, WD1793.TrackRegister, SIDE) );
#endif
	CurrentCommand = WD1793_CmdStartIdle;
	CmdType = 3;
	StartTimePeriod();
}

void WD1793_CmdStartWritingTrack()
{
	if(!Turbo && WD1793_GetIndexMark())
	{
		return;
	}
	Requests |= _BV(rqDRQ);
	WD1793.StatusRegister |= _BV(stsDRQ);
	CurrentCommand = WD1793_CmdWritingTrack;
	StartTimePeriod();
}

void WD1793_Cmd_WriteTrack()
{
#ifdef DEBUG	
	printf("WR TRACK %i\n", WD1793.TrackRegister);
#endif
	BufferPos = 0;
	SectorPos = 0;
	FormatCounter = 0;
	CurrentCommand = WD1793_CmdStartWritingTrack;
	CmdType = 3;
}

void WD1793_CmdStartNewCommand()
{
	uint8_t i = WD1793.CommandRegister >> 4;

	WD1793_FlushBuffer();

	if (DRV != NewDrive) // Mount image only if drive is changed
	{
		DRV = NewDrive;
		WD1793_Reset(DRV);
	}
	
	CommandTable[i]();
	
}



void WD1793_Execute(void)
{
	if(PrevPortFF != PortFF)
	{
		PrevPortFF = PortFF;
		NewDrive = GET_DRIVE();
		SIDE = GET_SIDE();
		//printf("SIDE %d\n", SIDE);
		if(!GET_RES()) 
		{
			printf("Reset\n");
			WD1793_Write(0, 0); // Restore command
		}
	}

	if(NewCommandReceived) 
	{
		NewCommandReceived = 0; // try to avoid disabling interrupts
		if ((WD1793.CommandRegister & 0xF0) == 0xD0) // Force Interrupt
		{
			//printf("Force Interrupt\n");	// In power up 2 demo can freeze 3d part at the end
			WD1793.StatusRegister &= ~_BV(stsBusy);
			CmdType = 1; // Exception
			CurrentCommand = WD1793.CommandRegister & 0x0F ? WD1793_CmdType1Status : WD1793_CmdStartIdleForceInt0;
		}
		else if(!(WD1793.StatusRegister & _BV(stsBusy)))
		{
			WD1793.StatusRegister = 0x01; // All bits clear but "Busy" set
			Requests = 0; // clear DRQ and INTRQ
			CurrentCommand = WD1793_CmdStartNewCommand;
		}
	}
   
   CurrentCommand();
}


