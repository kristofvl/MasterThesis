/*
 * N1_Final.c
 *
 * Created: 15-Mar-19 12:00:52 AM
 * Author : Frederic Philips
 */ 

#include <avr/io.h>
#include <avr/sfr_defs.h>
#include <avr/power.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "config.h"

#include <util/setbaud.h>
#include <util/delay.h>
#include <avr/interrupt.h>

#include "nrf.h"
#include "NRF24L01p.h"
#include "SPI.h"
#include "BNO055.h"
#include "i2cmaster.h"




uint8_t N1_address[NRF_ADDR_LEN] = {0x11, 0x12, 0x13, 0x14, 0x15};
uint8_t BS_address[NRF_ADDR_LEN] = {0x11, 0x12, 0x13, 0x14, 0x15};

uint8_t payload_TX[PAYLOAD_MAX_LEN];
uint8_t payload_RX[PAYLOAD_MAX_LEN];

uint8_t quatPacket[30];


//Function Prototypes
void AVR_Init(void);
void UART_Init(void);
void UART_Tx(unsigned char data);
void UART_Put_String(char *s);

/************************************************************************************
** AVR_Init function:
** - Resets the Clock Prescalar factor to 1x
** - Start-up delay
** - Initializes the I/O peripherals
** - Plays LED sequence
*************************************************************************************/
void AVR_Init(void)
{
	//Set the Clock Prescaler division factor to 1 (F_CPU = 8MHz)
	clock_prescale_set(clock_div_1);

	DDRD |= _BV(1);			//Set TX as output
	DDRD &= ~(_BV(0));		//Set RX as input

	//Make LED pins as output
	DDRC |= _BV(6);			//Makes PORTC, bit 6 as Output
	DDRC |= _BV(7);			//Makes PORTC, bit 7 as Output

	//Start-up LED sequence loop
	for (int i = 5; i != 0; i--)
	{
		PORTC &= ~(_BV(6));	//Turns OFF LED in Port C pin 6
		PORTC |= _BV(7);	//Turns ON LED in Port C pin 7
		_delay_ms(100);		//0.1 second delay

		PORTC |= _BV(6);	//Turns ON LED in Port C pin 6
		PORTC &= ~(_BV(7));	//Turns OFF LED in Port C pin 7
		_delay_ms(100);		//0.1 second delay
	}

	PORTC &= ~(_BV(6));		//Turns OFF LED in Port C pin 6
	PORTC &= ~(_BV(7));		//Turns OFF LED in Port C pin 7

	_delay_ms(750);			//Short pause after BNO055 Power-On Reset(Mandatory)
}

/************************************************************************************
** USART Reference:
** - ATmega32U4 Datasheet - Rev. CORP072610(Pg.186)
** - AVR Microcontroller and Embedded Systems - Mazidi(Pg.395)
** - Embedded C Programming and the Atmel AVR - Barnett(Pg.132)
*************************************************************************************
** To initialize the UART, the following steps are to be followed:
** - Set the Baud rate(use <util/setbaud.h>, which depends on the macros F_CPU & BAUD)
** - Disable double speed(2x) mode
** - Set the no. of data bits(8/9 bits), stop bit(1/2) and parity bit(None/Odd/Even)
** - Set the USART mode(Synchronous/Asynchronous/Asynchronous 2x)
** - Enable Receiver & Transmitter(Set RXEN & TXEN bits in UCSRB register)
*************************************************************************************/
void UART_Init(void)
{
	//Set the BAUD rate(Ref. ATmega32U4 Datasheet Pg.189, Table 18-1)
	//To hard-code the Baud rate, Ref. Tables 18-9 to 18-12 in Pages 210 - 213
	UBRR1 = ((F_CPU / (16UL * BAUD)) - 1);

	//Disables 2x speed
	UCSR1A &= ~(_BV(U2X1));

	//Enable 8-bit character size, one stop-bit, no parity & asynchronous mode
	UCSR1C |= _BV(UCSZ11) | _BV(UCSZ10);

	//Enable Transmitter & Receiver
	UCSR1B |= _BV(TXEN1) | _BV(RXEN1);
}

/************************************************************************************
** UART_Tx function:
** - Transmits the TWI data via the USB Serial
** - The data is received & displayed in a Hyperterminal
*************************************************************************************/
void UART_Tx(unsigned char data)
{
	loop_until_bit_is_set(UCSR1A, UDRE1);		//Wait until buffer is empty
	UDR1 = data;					//Send TWI data via UART
}

void UART_Put_String(char *s)
{
	//Loop through entire string
	while(*s)
	{
	    UART_Tx(*s);
	    s++;
	}
}



void INT6_Init(void)
{
	EICRB &= ~(1 << ISC60) | (1 << ISC61);	//INT6 active when low
	EIMSK |= (1 << INT6);			//Enable INT6
	sei();					//Enable global interrupts
}

ISR(INT6_vect)
{
	cli();					//Disable global interrupt

	nrf_stopListening();
	
	uint8_t len, pipe;
	nrf_readRXData(payload_RX, &len, &pipe);
	
	// RX_Payload_cnt = len;
	
	// Reset status register
	SPI_Write_Byte(STATUS, (1 << RX_DR));
}

void initPacket()
{
	quatPacket[0] = NODE_ID;
	quatPacket[1] = 1;
}

uint8_t modeIsValid(uint8_t mode)
{
	// TODO: enhance (is a bit simplified for now)
	return mode < 2;
}


/************************************************************************************
** Main function:
** - Contains an endless loop
** - Sets the BNO055 in NDOF mode and fetches the quaternion data
*************************************************************************************/
int main(void)
{
	AVR_Init();
	i2c_init();
	UART_Init();
	SPI_Init();
	nrf_init(0x69, DR_1M, NRF_ADDR_LEN, 1);
	//nrf_openTXPipe(BS_address, PAYLOAD_QUAT_LEN, 1, 0);
	nrf_openDynamicTXPipe(BS_address, 1, 0);
	//INT6_Init();
	BNO_Init();
	
	// default: quaternion only, mode = 1 -> quaternion + lin. acceleration
	uint8_t mode = 0;
	initPacket();
	
	// Disable global interrupt
	cli();

	//Configure as receiver
	nrf_setModeRX();
	nrf_maskIRQ(1, 1, 1);
	
	nrf_flushRX();
	nrf_flushTX();
	nrf_resetIRQFlags();
	
	nrf_startListening();
	_delay_us(150);
	
	uint8_t rxLen = 0;
	uint8_t rxPipe;
	uint8_t rx, tx_done, max_retry;
	
	// timer
	TCCR1B |= _BV(CS11);
	
	uint8_t pid = 0;
	
	TCNT1 = 0;
	
	//Endless Loop
	while(1)
	{
		if (mode == 1)
		{
			// process quaternions + linear acceleration
			BNO_Read_Quaternion_LinAcc(quatPacket + 2);
			
			// flush RX to enable packet sending and write data
			nrf_flushRX();
			nrf_writeAckData(0, quatPacket, 16);
		}
		else
		{
			// default: only process quaternions
			BNO_Read_Quaternion(quatPacket + 2);
		
			// flush RX to enable packet sending and write data
			nrf_flushRX();
			nrf_writeAckData(0, quatPacket, 10);
		}
		
		while (TCNT1 < 10000)
		{
			_delay_us(1);
		}
		
		// reset timer
		TCNT1 = 0;
		
		rxLen = 0;
		if (nrf_getIRQStatus(&rx, &tx_done, &max_retry))
		{
			nrf_resetIRQFlags();
			if (rx)
			{
				// next tx_done will signal current ack payload was transmitted
				// do not flush TX until ack packet was sent (at least 500 us or so)
				nrf_stopListening();

				while (nrf_dataAvailable())
				{
					nrf_readRXData(payload_RX, &rxLen, &rxPipe);
				}
				
				nrf_startListening();
				
				// check payload and change mode if required
				uint8_t newMode = payload_RX[0];
				if (newMode != mode && modeIsValid(newMode))
				{
					mode = newMode;
				}
			}
			if (tx_done)
			{
				// last ack packet was received by PTX
			}
		}
	}
}

