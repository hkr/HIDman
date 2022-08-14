typedef unsigned char *PUINT8;
typedef unsigned char __xdata *PUINT8X;
typedef const unsigned char __code *PUINT8C;
typedef unsigned char __xdata UINT8X;
typedef unsigned char  __data             UINT8D;


#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "CH559.h"
#include "util.h"
#include "USBHost.h"
#include "uart.h"
#include "ps2.h"

SBIT(LED, 0x90, 6);

ps2port keyboard = {
	S_INIT,
	PORT_KEY,
	0x1C,0,0
};

void mTimer0Interrupt( void) __interrupt (1)
{	
	/*OutPort(keyboard.port, DATA, 0);
	OutPort(keyboard.port, CLOCK, 0);*/
	//ps2stuff(&keyboard);
}

void main()
{
    unsigned char s;
    initClock();
    initUART0(1000000, 1);
    DEBUG_OUT("Startup\n");
    resetHubDevices(0);
    resetHubDevices(1);
    initUSB_Host();
	
	//port2 setup
	/*PORT_CFG |= bP2_OC; // open collector
	P2_DIR = 0xff; // output
	P2_PU = 0x00; // pull up - change this to 0x00 when we add the 5v pullup
	
	//timer0 setup
	TMOD = (TMOD & 0xf0) | 0x02; // mode 2 (8bit auto reload)
	T2MOD = T2MOD & 0b01101111; // clear bTMR_CLK and bT0_CLK;
	TH0 = 0x80; // reload to 128
	TR0 = 1;// start timer0
	ET0 = 1; //enable timer0 interrupt;
	EA = 1; // enable all interrupts*/
	
	
    DEBUG_OUT("Ready\n");
	sendProtocolMSG(MSG_TYPE_STARTUP,0, 0x00, 0x00, 0x00, 0);
	
	OutPort(keyboard.port, DATA, 1);
	OutPort(keyboard.port, CLOCK, 1);
	
    while(1)
    {
        if(!(P4_IN & (1 << 6)))
            runBootloader();
        processUart();
        s = checkRootHubConnections();
        pollHIDdevice();
		//ps2stuff(&keyboard);
    }
}