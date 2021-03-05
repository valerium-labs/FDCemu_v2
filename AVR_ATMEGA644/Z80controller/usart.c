/*
 * usart.c
 *
 * Created: 22.03.2019 21:35:53
 *  Author: Виталий
 */ 

#include <avr/io.h>
#include "usart.h"


void USART_Init( unsigned int baud)
{
	unsigned int ubrr = (F_CPU/(16UL*baud)-1UL);
	/*Set baud rate */
	UBRR0H = (unsigned char)(ubrr>>8);
	UBRR0L = (unsigned char)ubrr;
	/* Enable transmitter */
	UCSR0B = (1<<TXEN0);  
	/* Set frame format: 8data, 2stop bit */
	UCSR0C = (1<<USBS0)|(3<<UCSZ00);
}

void USART_Transmit( unsigned char data )
{
	/* Wait for empty transmit buffer */
	while ( !( UCSR0A & (1<<UDRE0)) )
	;
	/* Put data into buffer, sends the data */
	UDR0 = data;
}
unsigned char USART_Receive()
{
	/* Wait for data to be received */
	while ( !(UCSR0A & (1<<RXC0)) )
	;
	/* Get and return received data from buffer */
	return UDR0;
}void USART_Flush()
{
	unsigned char dummy;
	while ( UCSR0A & (1<<RXC0) ) dummy = UDR0;
}

