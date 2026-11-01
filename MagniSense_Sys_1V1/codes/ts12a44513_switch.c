#include "msp.h"
#include <stdint.h>

/*
 * MSP432P401R + 2x TS12A44513
 *
 * IC 1 IN1..IN4 : P2.3, P2.4, P2.6, P2.7
 * IC 2 IN1..IN4 : P5.0, P5.1, P5.2, P5.5
 * UART (USB backchannel): P1.2 = RXD, P1.3 = TXD, 9600 8N1
 *
 * TS12A44513 switch types:
 *   Switch 1 = NO (ON when IN = HIGH)    Switch 2 = NC (ON when IN = LOW)
 *   Switch 3 = NO (ON when IN = HIGH)    Switch 4 = NC (ON when IN = LOW)
 *
 * Usage (terminal, 9600 baud):
 *   Type any combination of 1..4, then press Enter.
 *   Example: "13" + Enter -> switches 1 and 3 ON, 2 and 4 OFF (both ICs).
 *   "0" + Enter (or just Enter) -> all switches OFF.
 */

static const uint8_t SW1_PINS[4] = { BIT3, BIT4, BIT6, BIT7 };   /* P2 */
static const uint8_t SW2_PINS[4] = { BIT0, BIT1, BIT2, BIT5 };   /* P5 */

#define SW1_MASK  (BIT3 | BIT4 | BIT6 | BIT7)
#define SW2_MASK  (BIT0 | BIT1 | BIT2 | BIT5)

/* 1 = NC type (ON when IN is LOW) */
static const uint8_t IS_NC[4] = { 0, 1, 0, 1 };

static void Clock_Init(void)
{
    CS->KEY  = CS_KEY_VAL;              /* unlock clock registers          */
    CS->CTL0 = 0;
    CS->CTL0 = CS_CTL0_DCORSEL_3;       /* DCO = 12 MHz                    */
    CS->CTL1 = CS_CTL1_SELA_2 |         /* ACLK  = REFOCLK                 */
               CS_CTL1_SELS_3 |         /* SMCLK = DCO, divider 1          */
               CS_CTL1_SELM_3;          /* MCLK  = DCO, divider 1          */
    CS->KEY  = 0;                       /* lock clock registers            */
}

static void UART0_Init(void)
{
    P1->SEL0 |=  (BIT2 | BIT3);
    P1->SEL1 &= ~(BIT2 | BIT3);

    EUSCI_A0->CTLW0 = 0x0081;           /* reset on, clock = SMCLK         */
    EUSCI_A0->BRW   = 78;               /* 12 MHz / (16 x 9600)            */
    EUSCI_A0->MCTLW = (2 << 4) | BIT0;  /* BRF = 2, BRS = 0, oversampling  */
    EUSCI_A0->CTLW0 &= ~BIT0;           /* release reset                   */
}

static void UART0_SendChar(char c)
{
    while (!(EUSCI_A0->IFG & BIT1)) { } /* wait until TX buffer is free    */
    EUSCI_A0->TXBUF = (uint8_t)c;
}

static void UART0_Print(const char *s)
{
    while (*s) UART0_SendChar(*s++);
}

/* mask bit i = 1 -> switch (i+1) ON.  mask = 0 -> all OFF.
 * Final pin levels are computed first, then each port is written once. */
static void SetOutputs(uint8_t mask)
{
    uint8_t v1 = 0, v2 = 0;
    uint8_t i;

    for (i = 0; i < 4; i++) {
        uint8_t want_on = (mask >> i) & 1;
        uint8_t level   = IS_NC[i] ? !want_on : want_on;   /* NC is inverted */

        if (level) {
            v1 |= SW1_PINS[i];
            v2 |= SW2_PINS[i];
        }
    }

    P2->OUT = (P2->OUT & ~SW1_MASK) | v1;
    P5->OUT = (P5->OUT & ~SW2_MASK) | v2;
}

static void PrintState(uint8_t mask)
{
    uint8_t i;

    UART0_Print("\r\nON: ");
    if (!mask) {
        UART0_Print("none");
    } else {
        for (i = 0; i < 4; i++) {
            if (mask & (1 << i)) {
                UART0_SendChar('1' + i);
                UART0_SendChar(' ');
            }
        }
    }
    UART0_Print("\r\n");
}

int main(void)
{
    char    c;
    uint8_t pending = 0;                /* mask currently being typed */

    WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD;    /* stop watchdog */

    Clock_Init();

    /* Pins as plain GPIO */
    P2->SEL0 &= ~SW1_MASK;  P2->SEL1 &= ~SW1_MASK;
    P5->SEL0 &= ~SW2_MASK;  P5->SEL1 &= ~SW2_MASK;

    /* Preload a valid state BEFORE enabling outputs so the switches never
     * see an undefined pattern. 0x00 = all switches OFF at power-up. */
    SetOutputs(0x00);

    P2->DIR |= SW1_MASK;
    P5->DIR |= SW2_MASK;

    UART0_Init();
    UART0_Print("\r\nType switch numbers (e.g. 13), then Enter. 0 = all OFF\r\n");

    while (1) {
        if (EUSCI_A0->IFG & BIT0) {                /* character received */
            c = (char)EUSCI_A0->RXBUF;

            if (c >= '1' && c <= '4') {
                pending |= (uint8_t)(1 << (c - '1'));
                UART0_SendChar(c);                 /* echo */
            } else if (c == '0') {
                pending = 0;
                UART0_SendChar(c);
            } else if (c == '\r' || c == '\n') {
                SetOutputs(pending);               /* apply all at once */
                PrintState(pending);
                pending = 0;
            } else {
                UART0_Print("\r\nInvalid: use 0-4, then Enter\r\n");
                pending = 0;
            }
        }
    }
}
