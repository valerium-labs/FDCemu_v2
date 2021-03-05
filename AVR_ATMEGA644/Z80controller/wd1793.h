

#ifndef _WD1793_H_
#define _WD1793_H_

#include <stdint.h>


// Status Bits
#define stsBusy            0
#define stsDRQ             1
#define stsIndexImpuls     1
#define stsTrack0          2
#define stsLostData        2
#define stsCRCError        3
#define stsSeekError       4
#define stsRecordNotFound  4
#define stsLoadHead        5
#define stsRecordType      5
#define stsWriteError      5
#define stsWriteProtect    6
#define stsNotReady        7


#define rqDRQ   6
#define rqINTRQ 7

typedef struct
{
	union {
		uint8_t Regs[5];
		struct {
			uint8_t StatusRegister;
			uint8_t TrackRegister;
			uint8_t SectorRegister;
			uint8_t DataRegister;
			uint8_t CommandRegister;
		};
	};
	
	uint8_t RealSector;        // 
	uint8_t RealTrack;        // Текущая дорожка, на которой находится головка
	uint8_t Direction;        // 0 = к центру, 1 = от центра
	uint8_t Side;
	uint8_t Multiple;
}  WD1793_struct;


void WD1793_Reset(unsigned char drive);
void WD1793_Execute();

extern unsigned char Requests;
inline unsigned char WD1793_GetRequests() // 7th bit - INTRQ, 6th - DRQ
{
	return Requests;
}

extern unsigned char PortFF;
#define GET_DRIVE() (PortFF & 0b11)
#define SIDE_PIN 4
#define GET_SIDE() ((~PortFF & _BV(SIDE_PIN))>>SIDE_PIN)
#define RES_PIN 2
#define GET_RES()  (PortFF & _BV(RES_PIN))

void WD1793_Cmd_Restore();
void WD1793_Cmd_Seek();
void WD1793_Cmd_Step();
void WD1793_Cmd_ReadSector();
void WD1793_Cmd_WriteSector();
void WD1793_Cmd_ReadAddress();
void WD1793_Cmd_ReadTrack();
void WD1793_Cmd_WriteTrack();
void WD1793_CmdStartReadingSector();

extern WD1793_struct WD1793;
extern uint8_t NewCommandReceived;

inline void WD1793_Write(unsigned char Address, unsigned char Value)
{
	//   printf("W%02X,%02X\n", Address & 0x03, Value);

	switch (Address & 0x03)
	{
		case 0 : // Write to Command Register
		Address = 4;
		NewCommandReceived = 1;
		break;
		case 3 : // Write to Data Register
		WD1793.StatusRegister &= ~_BV(stsDRQ);
		Requests &= ~_BV(rqDRQ);
		break;
	}
	WD1793.Regs[Address] = Value;

}

// Read
///////////////////////////////////////////////////////////////////////////////////////////

inline unsigned char WD1793_Read(unsigned char Address)
{
	Address &= 0x03;

	switch (Address)
	{
		case 0 : // Read from Status Register
		Requests &= ~_BV(rqINTRQ);
		break;
		case 3 : // Read from Data Register
		WD1793.StatusRegister &= ~_BV(stsDRQ);
		Requests &= ~_BV(rqDRQ);
		break;
	}
	//   printf("R%02X,%02X\n", Address & 0x03, Value);

	return WD1793.Regs[Address];
}



#endif // _WD1793_H_
