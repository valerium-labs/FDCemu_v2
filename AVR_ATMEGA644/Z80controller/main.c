/*
 * Z80controller.c
 *
 * Originally created 27.05.2018 18:06:44
 * by : Helbr
 * Russia, 2018
 * https://zx-pk.ru/threads/30269-emulyator-kontrollera-diskovoda-beta-disk-na-avr.html
 *
 * remake for atmega644+LCD and some fixes by valerium, 
 * Russia, 2021
 * valerium@rambler.ru
 *
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>
#include <string.h>
#include "print.h"
#include "petitfs/PetitFS.h"
#include "wd1793.h"


// MCU connection ===================
// data bus - PA
#define GET_DATA() PINA

// address lines A6 and A5 inputs (PD5 & PD4)
#define GET_ADDR() ((PIND>>4) & 0b11)

// address line A7 input PD3
#define A7_PIN 3
#define GET_ADDR_A7() (PIND & _BV(A7_PIN))

// WR and RD inputs - PD7 and PD6
#define WR_PIN 7
#define GET_WR()   (PIND & _BV(WR_PIN))
#define RD_PIN 6
#define GET_RD()   (PIND & _BV(RD_PIN))

// INEN line from TRDOS selection trigger PD2
#define INEN_PIN 2
#define GET_INEN()  (PIND & _BV(INEN_PIN)) 

// WAIT line output PD0
#define WAIT_PIN 0
#define START_WAIT()  (PORTD &= ~_BV(WAIT_PIN)) 
#define STOP_WAIT()   (PORTD |= _BV(WAIT_PIN))

// encoder button pin to PB2
#define SW_PIN 2
#define ENC_SW()  (PINB & _BV(SW_PIN))
// encoder rotor pins PC2 and PC3
#define DT_PIN 2
#define ENC_DT()  (PINC & _BV(DT_PIN) ? 1 : 0)
#define CLK_PIN 3
#define ENC_CLK()  (PINC & _BV(CLK_PIN) ? 1 : 0)

uint8_t ENC_BUTTON = 1;	//last encoder button state
uint8_t ENC_STATE = 0;	//last encoder rotor state
int ENC_COUNT = 1;      //encoder positin count
uint8_t BUTTON;		//current encoder button state


//directory and fileinfo structures to get filelist to 
DIR dir;
FILINFO finfo;
FRESULT res;

//temporary string handling buffers
char sbuf[20];
char sbuf2[20];

//menu state and result variables
uint8_t SETUPSTATE=0; //0 - Idle, 1 - Drive setup, 2 - File setup, 3 - Confirmation
uint8_t SETUPDRIVE=1; //0 - Cancel, 1 - A, 2 - B, 3 - C
uint8_t SETUPSAVE=0; //0 - do not save, 1 - save to IMAGES.CFG

//filelist buffer to display
uint8_t filelist[256][8];
uint16_t filecount = 0;

//SD images data for exchange with WD1793 library
uint8_t SelectedDrive = 0;		//default selected drive A:
char * SelectedImage = NULL;	//pointer to TRD-image filename

unsigned char PortFF = 0;	//last PortFF value

inline void SET_DATA(uint8_t d) {
	 PORTA = d; 
}

// Faster function sets databus bits 7,6 only
inline void SET_DATA_PORTFF(uint8_t d){
	PORTA = (PORTA & 0b00111111) | (d & 0b11000000);
}

// Set data bus to OUTPUT mode
inline void SET_DATA_OUT(){
	DDRA = 0b11111111;
}

// Set databus bits 7,6 to OUTPUT mode
inline void SET_DATA_OUT_PORTFF(){
	DDRA |= 0b11000000;
}

// Set data bus to INPUT mode
inline void SET_DATA_IN(){
	DDRA = 0;
}

void initPorts()
{
	DDRD = _BV(WAIT_PIN);

	PORTA = 0xff; // Databus mode - input with pull ups	

	DDRB &= 0b11110111;	//ENC_SW pin
	PORTB = 0xff; // Input with pull ups	

	DDRC &= 0b11110011;	//ENC_CLK and ENC_DT pins
	PORTC |= 0b00001100; // Input with pull ups
}

void initTimer()
{
	TCCR1B = _BV(CS11); // clk/8
	TIMSK1 = _BV(TOIE1); // Overflow interrupt
}

ISR(INT0_vect) // On low level of INEN of WD1793
{
	unsigned char data;

	if(!GET_RD()) {	
			
		if(GET_ADDR_A7()) { // PortFF
			data = WD1793_GetRequests();
			SET_DATA_PORTFF(data);
			SET_DATA_OUT_PORTFF();
			//printf("RD 0x%02X %c from %i\n", data, data, GET_ADDR());
		}
		else {
			data = WD1793_Read(GET_ADDR());
			SET_DATA(data);
			SET_DATA_OUT();
			//printf("RD 0x%02X %c from %i\n", data, data, GET_ADDR());
		}
		STOP_WAIT();
		while(!GET_INEN()); // wait release of /INEN
		SET_DATA_IN();
	}
	else if(!GET_WR()) {
		if(GET_ADDR_A7()) { // PortFF
			PortFF = GET_DATA();
			//printf("WR 0x%02X %c to %i\n", PortFF, PortFF, GET_ADDR());
		}
		else {
			//printf("WR 0x%02X to %i\n", GET_DATA(), GET_ADDR());
			WD1793_Write(GET_ADDR(), GET_DATA());
		}
		STOP_WAIT();
		while(!GET_INEN()); // wait release of /INEN
	}
	else // There are select signals without RD/WR operation specified
	{
		STOP_WAIT();
		while(!GET_INEN()); // wait release of /INEN
	}
	
	START_WAIT();

}


void Controls()
{
		//encoder button reading and dispatching
		BUTTON = ENC_SW() ? 1 : 0;
		if (ENC_BUTTON != BUTTON )
			{
			ENC_BUTTON = BUTTON;
			if (!BUTTON)
		
			switch (SETUPSTATE)
				{
				case 0:
					//idle -> disk selecction state
					LCDclr();
					LCDGotoXY(0,0);
					LCDsendString("Select Drive:");
					ENC_COUNT=1;
					SETUPSTATE=1;
					SETUPDRIVE = ENC_COUNT;
					LCDGotoXY(0,1);
					sprintf (sbuf, "<      %c       >", (SETUPDRIVE - 1 + 'A') );
					LCDsendString(sbuf);
					break;
				case 1:
					//disk selection, filelist aquiring and sorting -> file selection
					if (SETUPDRIVE == 0)
						{
							SETUPSTATE = 0;
							WD1793_Reset(GET_DRIVE());
							break;
						}
					SETUPSTATE = 2;
					ENC_COUNT = 1;
 					LCDclr();
					LCDGotoXY(0,0);
					LCDsendString("Select File:");
					
				        //filelist aquiring
					res = pf_opendir(&dir, "\0");
					if (res != FR_OK) 
					{
						printError("Error open dir: ", res);
					}
					else
					{
						filecount = 0;
						while (filecount < 256) 
						{
		
							res = pf_readdir(&dir, &finfo);
							if (res != FR_OK || finfo.fname[0] == 0) break;
							if (!(finfo.fattrib & AM_DIR))  // read files only
							{
					
								if(strstr(finfo.fname, ".TRD") != NULL) 
								{
									strncpy (sbuf, finfo.fname, 8);
									if (strchr (sbuf, '.') != NULL) *strchr (sbuf, '.') =  0;
									strncpy (filelist[filecount++], sbuf, 8);
								}
							}
						}
						//filelist sorting
						for (int i=0; i< filecount; i++)
							for (int j=0; j< filecount-1; j++)
							{
						        	if (strncmp (filelist[j],filelist[j+1],8) >0 )
								{
								strncpy (sbuf, filelist[j], 8);
								strncpy (filelist[j], filelist[j+1],8);
								strncpy (filelist[j+1], sbuf, 8);
								}
							}

					}

					LCDGotoXY(0,1);
					LCDsendString("<");
					LCDGotoXY(15,1);
					LCDsendString(">");
					LCDGotoXY(2,1);
					LCDsendString("             ");
					LCDGotoXY(2,1);
					strncpy (sbuf2, filelist[ENC_COUNT-1], 8);
					sbuf2[8]=0;
					sprintf (sbuf, "%.8s.TRD\0", sbuf2);
					LCDsendString(sbuf);

					break;
				case 2:
					//File selection confirmation -> Setup save confirmation
					LCDclr();
					LCDGotoXY(0,0);
					LCDsendString("Save ?");
					ENC_COUNT=0;
					SETUPSTATE=3;
					SETUPSAVE=0;
					LCDGotoXY(0,1);
					LCDsendString("<    Cancel    >");
					break;

				case 3:
					//Setup save confirmed -> Idle state
					LCDclr();
					LCDGotoXY(0,1);
					sprintf (sbuf2, "%s %c", sbuf, SETUPDRIVE - 1 + 'A');
					LCDsendString(sbuf2);
					ENC_COUNT=1;
					SETUPSTATE=0;
					LCDGotoXY(0,0);
					if (SETUPSAVE)
					{
						LCDsendString("Saved !");
						SelectedDrive = SETUPDRIVE-1;
						SelectedImage = &sbuf;
						WD1793_UpdateConfig();	
						_delay_ms (1000);
						WD1793_Reset(GET_DRIVE());
					}
					else	
					{
						LCDsendString("NOT saved !");
						_delay_ms (1000);
						WD1793_Reset(GET_DRIVE());
					}
					break;
				}
			}

		//encoder rotor reading and dispatching
		unsigned char ENC_NEWSTATE = (ENC_CLK()<<1) | (ENC_DT());
		if (ENC_STATE != ENC_NEWSTATE)
			{

			switch (SETUPSTATE)
				{			
				case 1:
					switch ((ENC_STATE<<2)|ENC_NEWSTATE)
					{
						case 0x01:
						case 0x0e:
							if (ENC_COUNT> 0)
							{
								ENC_COUNT--;
								SETUPDRIVE = ENC_COUNT;
								LCDGotoXY(0,1);
								if (SETUPDRIVE == 0) sprintf (sbuf, "<    Cancel    >");
								else sprintf (sbuf, "<      %c       >", (SETUPDRIVE - 1 + 'A'));
								LCDsendString(sbuf);
							}
							break;
			
						case 0x04:
						case 0x0b:
							if (ENC_COUNT < 3)
							{
								ENC_COUNT++;
								SETUPDRIVE = ENC_COUNT;
								LCDGotoXY(0,1);
								if (SETUPDRIVE == 0) sprintf (sbuf, "<    Cancel    >");
								else sprintf (sbuf, "<      %c       >", (SETUPDRIVE - 1 + 'A'));
								LCDsendString(sbuf);
							}
							break;
					}

					break;
				case 2:
					switch ((ENC_STATE<<2)|ENC_NEWSTATE)
					{
						case 0x01:
						case 0x0e:
							if (ENC_COUNT> 1)
							{
								ENC_COUNT--;
								if (filecount >0 )
								{
									LCDGotoXY(0,1);
									LCDsendString("<");
									LCDGotoXY(15,1);
									LCDsendString(">");
									LCDGotoXY(2,1);
									LCDsendString("             ");
									LCDGotoXY(2,1);
									strncpy (sbuf2, filelist[ENC_COUNT-1], 8);
									sbuf2[8]=0;
									sprintf (sbuf, "%.8s.TRD\0", sbuf2);
									LCDsendString(sbuf);
								}
							}
							break;
			
						case 0x04:
						case 0x0b:
							if (ENC_COUNT < filecount)
							{
								ENC_COUNT++;
								if (filecount >0 )
								{
									LCDGotoXY(0,1);
									LCDsendString("<");
									LCDGotoXY(15,1);
									LCDsendString(">");
									LCDGotoXY(2,1);
									LCDsendString("             ");
									LCDGotoXY(2,1);
									strncpy (sbuf2, filelist[ENC_COUNT-1], 8);
									sbuf2[8]=0;
									sprintf (sbuf, "%.8s.TRD\0", sbuf2);
									LCDsendString(sbuf);
								}
							}
							break;
					}
					break;
				case 3:
					switch ((ENC_STATE<<2)|ENC_NEWSTATE)
					{
						case 0x01:
						case 0x0e:
							if (ENC_COUNT> 0)
							{
								ENC_COUNT--;
								SETUPSAVE = 0;
								LCDGotoXY(0,1);
								LCDsendString("<    Cancel    >");
							}
							break;
			
						case 0x04:
						case 0x0b:
							if (ENC_COUNT < 1)
							{
								ENC_COUNT++;
								SETUPSAVE = 1;
								LCDGotoXY(0,1);
								LCDsendString("<    Save      >");
							}
							break;
					}
					break;
				}
			ENC_STATE = ENC_NEWSTATE;
			}
}




int main(void)
{

	initPorts();

	cli();
	LCDinit();

	print_init();
	initTimer();

	START_WAIT();	
	EIMSK = 1; // enable INT0
	sei();
	
	
    while (1) 
    {
		WD1793_Execute();
		Controls();
    }
}

//------------------------------------------------------------------------------
        