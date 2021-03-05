/*
 * print.c
 *
 * Created: 18.03.2019 21:31:20
 *  Author: Виталий
 */ 

#include <avr/sfr_defs.h>
#include <avr/io.h>
#include <stdio.h>
#include "usart.h"
#include "print.h"



static int uart_putchar(char c, FILE *stream);
static FILE mystdout = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_WRITE);

static int uart_putchar(char c, FILE *stream)
{
	if (c == '\n')
	uart_putchar('\r', stream);
	loop_until_bit_is_set(UCSR0A, UDRE0);
	UDR0 = c;
	return 0;
}

void print_init()
{
	USART_Init(BAUD);
	stdout = &mystdout;
	//printf("Hello, world!\n");	
}

void print(char * msg)
{
	printf("%s\n", msg);	
}


