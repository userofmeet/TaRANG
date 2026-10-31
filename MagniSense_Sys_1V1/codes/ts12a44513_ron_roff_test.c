/*
 * TS12A44513 test -- MSP432P401R (CCS) -- ONE code for channel 1 and channel 2
 * Both ICs (IC7 and IC8) are driven together with the same logic.
 *
 *   Channel 1 = NO1 / COM1, controlled by IN1 -> measure Roff
 *   Channel 2 = NC2 / COM2, controlled by IN2 -> measure Ron
 *
 *   Type 1 : IN1 = LOW  (NO1 OFF)  -> MEASURE Roff on channel 1   (IN2 = HIGH)
 *   Type 2 : IN2 = LOW  (NC2 ON)   -> MEASURE Ron  on channel 2   (IN1 = HIGH)
 *   Type 0 : IN1 = IN2 = HIGH (idle, no test)
 *   Type ? : menu
 *
 * Pins (edit defines if wiring differs):
 *   IC7 : P2.3 -> IN1, P2.4 -> IN2
 *   IC8 : P5.0 -> IN1, P5.1 -> IN2
 * Terminal: 9600 8-N-1 (backchannel UART), SMCLK = 3 MHz
 */

#include "msp.h"

#define IC7_IN1  BIT3   /* P2.3 */
#define IC7_IN2  BIT4   /* P2.4 */
#define IC8_IN1  BIT0   /* P5.0 */
#define IC8_IN2  BIT1   /* P5.1 */

static void uart_init(void)
{
    EUSCI_A0->CTLW0 |= EUSCI_A_CTLW0_SWRST;
    EUSCI_A0->CTLW0  = EUSCI_A_CTLW0_SWRST | EUSCI_A_CTLW0_SSEL__SMCLK;
    EUSCI_A0->BRW    = 19;                       /* 3 MHz / 9600 */
    EUSCI_A0->MCTLW  = (0xAA << EUSCI_A_MCTLW_BRS_OFS) |
                       (8    << EUSCI_A_MCTLW_BRF_OFS) |
                       EUSCI_A_MCTLW_OS16;
    P1->SEL0 |= BIT2 | BIT3;
    P1->SEL1 &= ~(BIT2 | BIT3);
    EUSCI_A0->CTLW0 &= ~EUSCI_A_CTLW0_SWRST;
}

static void uart_putc(char c)
{
    while (!(EUSCI_A0->IFG & EUSCI_A_IFG_TXIFG));
    EUSCI_A0->TXBUF = c;
}

static void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

static char uart_getc(void)
{
    while (!(EUSCI_A0->IFG & EUSCI_A_IFG_RXIFG));
    return (char)EUSCI_A0->RXBUF;
}

/* level: 1 = HIGH, 0 = LOW ; applies to IC7 and IC8 together */
static void set_in1(int level)
{
    if (level) { P2->OUT |= IC7_IN1; P5->OUT |= IC8_IN1; }
    else       { P2->OUT &= ~IC7_IN1; P5->OUT &= ~IC8_IN1; }
}

static void set_in2(int level)
{
    if (level) { P2->OUT |= IC7_IN2; P5->OUT |= IC8_IN2; }
    else       { P2->OUT &= ~IC7_IN2; P5->OUT &= ~IC8_IN2; }
}

static void menu(void)
{
    uart_puts("\n==== TS12A44513 test (IC7 & IC8) ====\n");
    uart_puts(" 1 : channel 1 (NO1) -> measure Roff\n");
    uart_puts(" 2 : channel 2 (NC2) -> measure Ron\n");
    uart_puts(" 0 : idle (IN1 = IN2 = HIGH)\n");
    uart_puts(" ? : menu\n> ");
}

void main(void)
{
    char c;

    WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD;

    P2->SEL0 &= ~(IC7_IN1 | IC7_IN2);
    P2->SEL1 &= ~(IC7_IN1 | IC7_IN2);
    P2->DIR  |=  (IC7_IN1 | IC7_IN2);

    P5->SEL0 &= ~(IC8_IN1 | IC8_IN2);
    P5->SEL1 &= ~(IC8_IN1 | IC8_IN2);
    P5->DIR  |=  (IC8_IN1 | IC8_IN2);

    set_in1(1);          /* idle at boot */
    set_in2(1);

    uart_init();
    menu();

    while (1) {
        c = uart_getc();
        if (c == '\r' || c == '\n' || c == ' ') continue;
        uart_putc(c);
        uart_puts("\n");

        switch (c) {
        case '1':
            set_in1(0);  set_in2(1);
            uart_puts("CHANNEL 1: IN1 = LOW, NO1 (pin1-pin2) OFF -> MEASURE Roff\n> ");
            break;
        case '2':
            set_in2(0);  set_in1(1);
            uart_puts("CHANNEL 2: IN2 = LOW, NC2 (pin3-pin4) ON  -> MEASURE Ron\n> ");
            break;
        case '0':
            set_in1(1);  set_in2(1);
            uart_puts("IDLE: IN1 = IN2 = HIGH\n> ");
            break;
        case '?': case 'h': case 'H':
            menu();
            break;
        default:
            uart_puts("Invalid input. Type ? for menu.\n> ");
            break;
        }
    }
}
