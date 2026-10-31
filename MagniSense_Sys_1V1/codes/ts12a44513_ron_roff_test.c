#include "msp.h"
#include <stdint.h>

/*
 * MSP432P401R + 2x TS12A44513 (SW1 and SW2 groups)
 *
 * SW1 IN1..IN4 : P2.3, P2.4, P2.6, P2.7
 * SW2 IN1..IN4 : P5.0, P5.1, P5.2, P5.5
 *
 * Switch types (from TS12A44513 pinout):
 *   Switch 1 = NO  -> IN HIGH = ON
 *   Switch 2 = NC  -> IN LOW  = ON
 *   Switch 3 = NO  -> IN HIGH = ON
 *   Switch 4 = NC  -> IN LOW  = ON
 *
 * UART backchannel: P1.2 = RXD, P1.3 = TXD, 9600 baud, SMCLK = 3 MHz
 *
 * Terminal commands:
 *   1..4 = switch n ON in both SW1 and SW2, all other switches OFF
 *   0    = all switches OFF
 */

static const uint8_t SW1_PINS[4] = { BIT3, BIT4, BIT6, BIT7 };   /* P2.3, P2.4, P2.6, P2.7 */
static const uint8_t SW2_PINS[4] = { BIT0, BIT1, BIT2, BIT5 };   /* P5.0, P5.1, P5.2, P5.5 */

#define SW1_MASK  (BIT3 | BIT4 | BIT6 | BIT7)
#define SW2_MASK  (BIT0 | BIT1 | BIT2 | BIT5)

/* 1 = this switch is NC type (ON when IN is LOW) */
static const uint8_t IS_NC[4] = { 0, 1, 0, 1 };

void UART0_Init(void)
{
    P1->SEL0 |= (BIT2 | BIT3);
    P1->SEL1 &= ~(BIT2 | BIT3);

    EUSCI_A0->CTLW0 = 0x0081;   /* reset, SMCLK */
    EUSCI_A0->BRW = 312;        /* 3 MHz / 9600 */
    EUSCI_A0->MCTLW = 0;
    EUSCI_A0->CTLW0 &= ~BIT0;
}

void UART0_SendChar(char c)
{
    while (!(EUSCI_A0->IFG & BIT1)) { }
    EUSCI_A0->TXBUF = (uint8_t)c;
}

void UART0_Print(const char *str)
{
    while (*str != '\0') {
        UART0_SendChar(*str++);
    }
}

/* row = 1..4: switch 'row' ON in both ICs, others OFF. row = 0: all OFF */
void SetOutputs(uint8_t row)
{
    uint8_t i;

    for (i = 0; i < 4; i++) {
        uint8_t want_on = (row == (uint8_t)(i + 1));
        /* NO switch: level = want_on.  NC switch: level = !want_on */
        uint8_t level = IS_NC[i] ? !want_on : want_on;
        /*IS_NC = [0,1,0,1]*/
        if (level) {
            P2->OUT |= SW1_PINS[i];
            P5->OUT |= SW2_PINS[i];
        } else {
            P2->OUT &= ~SW1_PINS[i];
            P5->OUT &= ~SW2_PINS[i];
        }
    }
}

void PrintState(uint8_t row)
{
    UART0_Print("\r\nSwitch ");
    UART0_SendChar('0' + row);
    UART0_Print(" ON in SW1 and SW2, all other switches OFF\r\n");
}

int main(void)
{
    char command;

    WDT_A->CTL = 0x5A80;

    P2->SEL0 &= ~SW1_MASK;
    P2->SEL1 &= ~SW1_MASK;
    P5->SEL0 &= ~SW2_MASK;
    P5->SEL1 &= ~SW2_MASK;

    SetOutputs(0);              /* preload OFF levels before enabling outputs */
    P2->DIR |= SW1_MASK;
    P5->DIR |= SW2_MASK;

    UART0_Init();

    UART0_Print("\r\nMSP432 Switch Controller\r\n");
    UART0_Print("1-4: turn ON switch n in SW1 and SW2\r\n");
    UART0_Print("0  : all switches OFF\r\n");

    while (1) {
        if (EUSCI_A0->IFG & BIT0) {
            command = (char)EUSCI_A0->RXBUF;

            switch (command) {
                case '1':
                case '2':
                case '3':
                case '4':
                    SetOutputs(command - '0');
                    PrintState(command - '0');
                    break;

                case '0':
                    SetOutputs(0);
                    UART0_Print("\r\nALL SWITCHES OFF\r\n");
                    break;

                case '\r':
                case '\n':
                    break;

                default:
                    UART0_Print("\r\nInvalid command: use 0-4\r\n");
                    break;
            }
        }
    }
}
