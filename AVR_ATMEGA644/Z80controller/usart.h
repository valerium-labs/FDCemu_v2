/*
 * usart.h
 *
 * Created: 22.03.2019 21:36:34
 *  Author: Виталий
 */ 


#ifndef USART_H_
#define USART_H_

#define BAUD 57600

void USART_Init( unsigned int baud);
void USART_Transmit( unsigned char data );
unsigned char USART_Receive();
void USART_Flush();

#endif /* USART_H_ */