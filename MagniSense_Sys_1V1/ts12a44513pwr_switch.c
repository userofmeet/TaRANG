/*
 * File: fda_gain_switching.c
 *
 * Objective:
 *   Select the gain of the FDA Stage 1 by driving the control inputs of two
 *   analog switch ICs (SW1 and SW2) from an MSP432P401R LaunchPad. The gain
 *   setting is chosen from a UART terminal.
 *
 *   Terminal commands (9600 baud):
 *     1..4 : apply row Inp1..Inp4 of the input table (one SW1 input HIGH,
 *            SW2 inputs set according to the table)
 *     0    : all outputs LOW
 *
 * Pins:
 *   SW1 inputs 1..4 : P2.3, P2.4, P2.6, P2.7
 *   SW2 inputs 1..4 : P5.0, P5.1, P5.2, P5.5
 *   UART (eUSCI_A0, XDS110 USB backchannel) : P1.2 = RXD, P1.3 = TXD
 *
 * Clock: SMCLK = 3 MHz (default DCO)
 */

#include "msp.h"
#include <stdint.h>

static const uint8_t SW1_PINS[4] = { BIT3, BIT4, BIT6, BIT7 };   /* P2.3, P2.4, P2.6, P2.7 */
static const uint8_t SW2_PINS[4] = { BIT0, BIT1, BIT2, BIT5 };   /* P5.0, P5.1, P5.2, P5.5 */

#define SW1_MASK  (BIT3 | BIT4 | BIT6 | BIT7)
#define SW2_MASK  (BIT0 | BIT1 | BIT2 | BIT5)

/*
 * Input table, ordered Inp1..Inp4.
 * Row    = SW1 input that goes HIGH
 * Column = SW2 input state for that row (1 = HIGH)
 *
 *                 SW2.1 SW2.2 SW2.3 SW2.4
 */
static const uint8_t TABLE[4][4] = {
    /* Inp1 */ {   1,    1,    0,    1 },
    /* Inp2 */ {   0,    1,    0,    0 },
    /* Inp3 */ {   0,    1,    1,    1 },
    /* Inp4 */ {   0,    0,    0,    1 }
};

void UART0_Init(void)
{
    P1->SEL0 |= (BIT2 | BIT3);
    P1->SEL1 &= ~(BIT2 | BIT3);

    EUSCI_A0->CTLW0 = 0x0081;   /* hold in reset, clock source = SMCLK */
    EUSCI_A0->BRW = 312;        /* 3 MHz / 9600 */
    EUSCI_A0->MCTLW = 0;
    EUSCI_A0->CTLW0 &= ~BIT0;   /* release reset */
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

/* row = 1..4 applies that table row, 0 = everything LOW */
void SetOutputs(uint8_t row)
{
    uint8_t i;

    P2->OUT &= ~SW1_MASK;
    P5->OUT &= ~SW2_MASK;

    if (row >= 1 && row <= 4) {
        P2->OUT |= SW1_PINS[row - 1];

        for (i = 0; i < 4; i++) {
            if (TABLE[row - 1][i]) {
                P5->OUT |= SW2_PINS[i];
            }
        }
    }
}

/* Print which outputs are HIGH for the selected row */
void PrintState(uint8_t row)
{
    uint8_t i;

    UART0_Print("\r\nSW1.");
    UART0_SendChar('0' + row);
    UART0_Print(" HIGH, SW2 pattern (1..4) = ");

    for (i = 0; i < 4; i++) {
        UART0_SendChar(TABLE[row - 1][i] ? '1' : '0');
    }
    UART0_Print("\r\n");
}

int main(void)
{
    char command;

    WDT_A->CTL = 0x5A80;        /* stop watchdog */

    /* All switch control pins as GPIO outputs */
    P2->SEL0 &= ~SW1_MASK;
    P2->SEL1 &= ~SW1_MASK;
    P5->SEL0 &= ~SW2_MASK;
    P5->SEL1 &= ~SW2_MASK;

    P2->DIR |= SW1_MASK;
    P5->DIR |= SW2_MASK;

    SetOutputs(0);
    UART0_Init();

    UART0_Print("\r\nMSP432 Output Controller\r\n");
    UART0_Print("1-4: apply table row Inp1-Inp4\r\n");
    UART0_Print("0  : All outputs LOW\r\n");

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
                    UART0_Print("\r\nALL OUTPUTS LOW\r\n");
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
