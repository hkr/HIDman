#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "ch559.h"
#include "util.h"
#include "usbhost.h"
#include "uart.h"
#include "ps2protocol.h"
#include "ps2.h"
#include "parsedescriptor.h"
#include "menu.h"
#include "mouse.h"
#include "pwm.h"

#if defined(BOARD_MICRO) // Pinouts for HIDman-micro
SBIT(KEY_CLOCK, 0x90, 7);
#if defined(OPT_SWAP_KBD_MSC) // Makes it easier to direct solder combo PS/2 port
SBIT(KEY_DATA, 0x90, 6);
SBIT(MOUSE_CLOCK, 0x90, 4);
#else
SBIT(KEY_DATA, 0x90, 4);
SBIT(MOUSE_CLOCK, 0x90, 6);
#endif
SBIT(MOUSE_DATA, 0x90, 5);
#else // Default pinouts (HIDman-AXD, HIDman-mini)
SBIT(KEY_CLOCK, 0x80, 5);
SBIT(KEY_DATA, 0x80, 3);

SBIT(MOUSE_CLOCK, 0xB0, 7);
SBIT(MOUSE_DATA, 0xC1, 3);
#endif

__xdata uint8_t repeatDiv = 0;
uint16_t ResetCounter;

// green LED on by default
uint8_t LEDStatus = 0x02;

void mTimer2Interrupt(void) __interrupt(5);

// timer should run at 48MHz divided by (0xFFFF - (TH0TL0))
// i.e. 60khz
void mTimer0Interrupt(void) __interrupt(1)
{
	// Reload to 60KHz

	PS2ProcessPort(PORT_KEY);
	PS2ProcessPort(PORT_MOUSE);

	// now handle keyboard typematic repeat timers
	// divide down to 15KHz to make maths easier
	if (++repeatDiv == 4)
	{
		RepeatTimer();
		repeatDiv = 0;

		if (!(P4_IN & (1 << 6)))
		{
			ResetCounter++;
			if (ResetCounter > 10000)
			{
				runBootloader();
			}
		}
		else
			ResetCounter = 0;

		// turn current LED on if we've seen no activity in a while
		if (LEDDelay)
			LEDDelay--;
		else
		{
#if defined(BOARD_MICRO)
			P2 |= 0b00100000;
#else
			if (LEDStatus & 0x01)
				SetPWM1Dat(0x18);
			if (LEDStatus & 0x02)
				SetPWM2Dat(0x30);
			if (LEDStatus & 0x04)
			{
				T3_FIFO_L = 0xFF; // blue needs to be brighter
				T3_FIFO_H = 0;
			}
#endif
		}
	}
}

uint8_t DetectCountdown = 0;
uint8_t PrevRTSState = 0;
uint8_t PrevButtons = 0;

void main()
{

	InitSystem();

	SetPWMClk(12); //Set the clock division factor of PWM1&2 to 12
	InitPWM1(1);   //PWM1 initialization, active low
	InitPWM2(1);   //PWM2 initialization, active high
	InitPWM3(1);
	SetPWMCycle(0xff);
	SetPWM1Dat(0x00);
	SetPWM2Dat(0x00);

	T3_CK_SE_L = 0x20;
	T3_CK_SE_H = 0;
	T3_END_H = 0;
	T3_END_L = 255;
	T3_FIFO_L = 0;
	T3_FIFO_H = 0;

#if defined(BOARD_MICRO) // Pinouts for HIDman-micro
	//port1 setup
	P1_DIR |= 0b11110000; // 0.4, 0.5, 0.6, 0.7 are keyboard/mouse outputs
	PORT_CFG |= bP1_OC;	  // open collector
	P1_PU = 0x00;		  // no pullups
	P1 = 0b11110000;	  // default pin states

	//port2 setup
	P2_DIR |= 0b00100000; // 2.5 is LED output
	PORT_CFG |= bP2_OC;	  // open collector
	P2_PU = 0x00;		  // no pullups
	P2 = 0b00100000;	  // LED off by default (i.e. high)
#else					  // Default pinouts (HIDman-AXD, HIDman-mini)
	//port0 setup
	P0_DIR = 0b11101010; // 0.3, 0.5, 0.6, 0.7 are all keyboard outputs, 0.4 is CTS (i.e. RTS on host), 0.1 is RTS (i.e. CTS on host)
	PORT_CFG |= bP0_OC;	 // open collector
	P0_PU = 0x00;		 // no pullups
	P0 = 0b11111010;	 // default pin states

	//port2 setup
	P2_DIR = 0b00110000; // 2.4, 2.5 are RED/GREEN LED outputs
	PORT_CFG |= bP2_OC;	 // open collector
	P2_PU = 0x00;		 // no pullups
	P2 = 0b00110000;	 // LEDs off by default (i.e. high)

	//port3 setup
	P3_DIR = 0b11100010; // 5,6,7 are PS2 outputs, 1 is UART0 TXD
	PORT_CFG |= bP3_OC;	 // open collector
	P3_PU = 0x00;		 // no pullups
	P3 = 0b11100010;	 // default pin states

	//port4 setup
	P4_DIR = 0b00010100; //4.0 is RXD, 4.2 is Blue LED, 4.3 is MOUSE DATA (actually input, since we're faking open drain), 4.4 is TXD, 4.6 is SWITCH
	P4_PU = 0b01000000;	 // pullup on switch
	P4_OUT = 0b00000100; //LEDs off (i.e. HIGH), MOUSE DATA low (since it's switched by toggling input on and off, i.e. faking open drain)
#endif

	// timer0 setup
	TMOD = (TMOD & 0xf0) | 0x02; // mode 1 (8bit auto reload)
	TH0 = 0xBD;					 // 60khz

	TR0 = 1; // start timer0
	ET0 = 1; //enable timer0 interrupt;
	EA = 1;	 // enable all interrupts

#if !defined(BOARD_MICRO)
	printf("Ready\n");

	uint32_t serialMouseBps = 1200; // can do 19200 with custom mouse driver
	CH559UART1Init(20, 1, 1, serialMouseBps, 7);
#endif

	memset(SendBuffer, 0, 255);
	//SendKeyboardString("We are go\n");
	uint8_t Buttons;
	int16_t X, Y;
	while (1)
	{
		if (!(P4_IN & (1 << 6)))
			MenuActive = 1;

		if (MenuActive)
			Menu_Task();

		ProcessUsbHostPort();
		ProcessKeyboardLed();
		HandleRepeats();

		uint8_t byte1, byte2, byte3;

		// Send PS/2 Mouse Packet if neccessary
		// make sure there's space in the buffer before we pop any mouse updates
		if ((ports[PORT_MOUSE].sendBuffEnd + 1) % 8 != ports[PORT_MOUSE].sendBuffStart)
		{
			if (GetMouseUpdate(0, -255, 255, &X, &Y, &Buttons))
			{

				// ps2 is inverted compared to USB
				Y = -Y;

				byte1 = 0b00001000 |			   //bit3 always set
						((Y >> 10) & 0b00100000) | // Y sign bit
						((X >> 11) & 0b00010000) | // X sign bit
						(Buttons & 0x07);

				byte2 = (X & 0xFF);
				byte3 = (Y & 0xFF);

				SendMouse3(byte1, byte2, byte3);
			}
		}

#if !defined(BOARD_MICRO)
		// falling edge of RTS (P0.4) means host is resetting mouse
		if (!(P0 & 0b00010000) && PrevRTSState)
		{
			DetectCountdown = 20;
		}

		PrevRTSState = P0 & 0b00010000;

		char serialMouseType = 'M'; // Logitech 3 button: '3', Microsoft: 'M'

		// send a bunch of "M"s to identify MS mouse
		if (DetectCountdown)
		{
			if (SER1_LSR & bLSR_T_FIFO_EMP)
			{
				CH559UART1SendByte(serialMouseType);
				DetectCountdown--;
			}
		}
		// Send Serial Mouse Packet if neccessary
		// make sure there's space in the fifo before we pop any mouse updates
		else if (/*CH559UART1_FIFO_CNT >= 3 || */ SER1_LSR & bLSR_T_FIFO_EMP)
		{
			if (GetMouseUpdate(1, -127, 127, &X, &Y, &Buttons))
			{
				byte1 = 0b11000000 |			  // bit6 always set
						((Buttons & 0x01) << 5) | // left button
						((Buttons & 0x02) << 3) | // right button
						((Y >> 4) & 0b00001100) | // top two bits of Y
						((X >> 6) & 0b00000011);  // top two bits of X

				byte2 = 0b10000000 | (X & 0x3F); // rest of X
				byte3 = 0b10000000 | (Y & 0x3F); // rest of Y

				CH559UART1SendByte(byte1);
				CH559UART1SendByte(byte2);
				CH559UART1SendByte(byte3);

				if (serialMouseType == '3')
				{
					if (Buttons & 0x04)
						CH559UART1SendByte(0b10100000);
					else if (PrevButtons & 0x04)
						CH559UART1SendByte(0b10000000);

					PrevButtons = Buttons;
				}
			}
		}
#endif
	}
}
