/*
 * TS12A44513 Ron / Roff test  --  MSP432P401R LaunchPad (CCS)
 *
 * Wiring:
 *   P2.3 -> IN1  (pin 13)   controls switch 1 : NO1 (pin 1) <-> COM1 (pin 2)
 *   P2.4 -> IN2  (pin 5)    controls switch 2 : NC2 (pin 3) <-> COM2 (pin 4)
 *   GND  -> GND  (pin 7)    VCC -> VCC (pin 14)
 *
 * Truth table (from datasheet):
 *   IN = L : NO-COM OFF , NC-COM ON
 *   IN = H : NO-COM ON  , NC-COM OFF
 *
 * Terminal: 9600 8-N-1 (XDS110 backchannel, P1.2 RX / P1.3 TX), SMCLK = 3 MHz
 * Type one character and press Enter/Send:
 *   1 -> NO1 : measure Ron   (IN1 = H)
 *   2 -> NO1 : measure Roff  (IN1 = L)
 *   3 -> NC2 : measure Ron   (IN2 = L)
 *   4 -> NC2 : measure Roff  (IN2 = H)
 *   ? or h -> show menu
 */

#include "msp.h"

#define IN1_PIN  BIT3      /* P2.3 */
#define IN2_PIN  BIT4      /* P2.4 */

static void uart_init(void)
{
    EUSCI_A0->CTLW0 |= EUSCI_A_CTLW0_SWRST;
    EUSCI_A0->CTLW0  = EUSCI_A_CTLW0_SWRST | EUSCI_A_CTLW0_SSEL__SMCLK;
    /* 3 MHz / 9600 : BRW=19, BRF=8, BRS=0xAA, oversampling on */
    EUSCI_A0->BRW    = 19;
    EUSCI_A0->MCTLW  = (0xAA << EUSCI_A_MCTLW_BRS_OFS) |
                       (8    << EUSCI_A_MCTLW_BRF_OFS) |
                       EUSCI_A_MCTLW_OS16;
    P1->SEL0 |= BIT2 | BIT3;                 /* P1.2 RXD, P1.3 TXD */
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

static void menu(void)
{
    uart_puts("\n==== TS12A44513 Ron/Roff test ====\n");
    uart_puts(" 1 : NO1 -> measure Ron   (IN1=H)\n");
    uart_puts(" 2 : NO1 -> measure Roff  (IN1=L)\n");
    uart_puts(" 3 : NC2 -> measure Ron   (IN2=L)\n");
    uart_puts(" 4 : NC2 -> measure Roff  (IN2=H)\n");
    uart_puts(" ? : show this menu\n> ");
}

void main(void)
{
    char c;

    WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD;   /* stop watchdog */

    /* GPIO outputs, start with IN1 = L, IN2 = L */
    P2->SEL0 &= ~(IN1_PIN | IN2_PIN);
    P2->SEL1 &= ~(IN1_PIN | IN2_PIN);
    P2->DIR  |=  (IN1_PIN | IN2_PIN);
    P2->OUT  &= ~(IN1_PIN | IN2_PIN);

    uart_init();
    menu();

    while (1) {
        c = uart_getc();
        if (c == '\r' || c == '\n' || c == ' ') continue;
        uart_putc(c);
        uart_puts("\n");

        switch (c) {
        case '1':
            P2->OUT |= IN1_PIN;                 /* IN1 = H : NO1-COM1 ON */
            uart_puts("NO1 (pin1-pin2) is ON  -> MEASURE Ron  now\n> ");
            break;
        case '2':
            P2->OUT &= ~IN1_PIN;                /* IN1 = L : NO1-COM1 OFF */
            uart_puts("NO1 (pin1-pin2) is OFF -> MEASURE Roff now\n> ");
            break;
        case '3':
            P2->OUT &= ~IN2_PIN;                /* IN2 = L : NC2-COM2 ON */
            uart_puts("NC2 (pin3-pin4) is ON  -> MEASURE Ron  now\n> ");
            break;
        case '4':
            P2->OUT |= IN2_PIN;                 /* IN2 = H : NC2-COM2 OFF */
            uart_puts("NC2 (pin3-pin4) is OFF -> MEASURE Roff now\n> ");
            break;
        case '?':
        case 'h':
        case 'H':
            menu();
            break;
        default:
            uart_puts("Invalid input. Type ? for menu.\n> ");
            break;
        }
    }
}
