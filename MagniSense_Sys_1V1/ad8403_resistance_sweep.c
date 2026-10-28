/*
 * File: ad8403_resistance_sweep.c
 *
 * Description:
 * Controls all four channels of the AD8403 digital potentiometer using SPI
 * on the MSP432P401R. Supports automatic resistance sweeping from 1 kOhm
 * to 50 kOhm in 1 kOhm steps, manual adjustment using onboard buttons,
 * and resistance input through UART. The selected resistance, digital
 * potentiometer code, and estimated resistance are printed over UART.
 */

#include "msp.h"
#include <stdint.h>

#define RAB_OHMS       50000u
#define RWIPER_OHMS    50u
#define CSPIN          BIT7

#define SWEEP_START_OHMS   1000u
#define SWEEP_STOP_OHMS    50000u
#define SWEEP_STEP_OHMS    1000u
#define SWEEP_DWELL_MS     1000u

/* Configure the DCO clock to 3 MHz. */
static void clock_init(void)
{
    CS->KEY  = CS_KEY_VAL;
    CS->CTL0 = CS_CTL0_DCORSEL_1;
    CS->CTL1 = CS_CTL1_SELM__DCOCLK | CS_CTL1_SELS__DCOCLK | CS_CTL1_SELA__REFOCLK;
    CS->KEY  = 0;
}

/* Generate millisecond delays using SysTick. */
static void delay_ms(uint32_t ms)
{
    SysTick->LOAD = 3000u - 1u;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;

    while (ms--) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
    }

    SysTick->CTRL = 0;
}

/* Initialize UART communication through eUSCI_A0. */
static void uart_init(void)
{
    P1->SEL0 |= BIT2 | BIT3;
    P1->SEL1 &= ~(BIT2 | BIT3);

    EUSCI_A0->CTLW0 = EUSCI_A_CTLW0_SWRST;
    EUSCI_A0->CTLW0 |= EUSCI_A_CTLW0_SSEL__SMCLK;
    EUSCI_A0->BRW   = 1;
    EUSCI_A0->MCTLW = (10 << EUSCI_A_MCTLW_BRF_OFS) | EUSCI_A_MCTLW_OS16;
    EUSCI_A0->CTLW0 &= ~EUSCI_A_CTLW0_SWRST;
}

static void uart_putc(char c)
{
    while (!(EUSCI_A0->IFG & EUSCI_A_IFG_TXIFG));
    EUSCI_A0->TXBUF = c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

static void uart_putu(uint32_t v)
{
    char tmp[11];
    int i = 0;

    if (v == 0) {
        uart_putc('0');
        return;
    }

    while (v > 0) {
        tmp[i++] = '0' + (v % 10);
        v /= 10;
    }

    while (i > 0) uart_putc(tmp[--i]);
}

/* Configure S1 and S2 as active-low inputs with pull-up resistors. */
static void buttons_init(void)
{
    P1->SEL0 &= ~(BIT1 | BIT4);
    P1->SEL1 &= ~(BIT1 | BIT4);
    P1->DIR  &= ~(BIT1 | BIT4);
    P1->REN  |=  (BIT1 | BIT4);
    P1->OUT  |=  (BIT1 | BIT4);
}

/* Detect a button press after 20 consecutive milliseconds. */
static uint8_t btn_event(uint8_t pin, uint8_t *held)
{
    if (!(P1->IN & pin)) {
        if (*held < 255) (*held)++;
        if (*held == 20) return 1;
    } else {
        *held = 0;
    }

    return 0;
}

/* Initialize SPI for the AD8403 using eUSCI_B0. */
static void spi_init(void)
{
    P1->SEL0 &= ~CSPIN;
    P1->SEL1 &= ~CSPIN;
    P1->DIR  |= CSPIN;
    P1->OUT  |= CSPIN;

    P1->SEL0 |= BIT5 | BIT6;
    P1->SEL1 &= ~(BIT5 | BIT6);

    EUSCI_B0->CTLW0 = EUSCI_B_CTLW0_SWRST;
    EUSCI_B0->CTLW0 |= EUSCI_B_CTLW0_MST
                     | EUSCI_B_CTLW0_SYNC
                     | EUSCI_B_CTLW0_MSB
                     | EUSCI_B_CTLW0_CKPH
                     | EUSCI_B_CTLW0_SSEL__SMCLK;

    EUSCI_B0->BRW = 30;
    EUSCI_B0->CTLW0 &= ~EUSCI_B_CTLW0_SWRST;
}

static void spi_tx_byte(uint8_t b)
{
    while (!(EUSCI_B0->IFG & EUSCI_B_IFG_TXIFG));
    EUSCI_B0->TXBUF = b;
}

/* Send a channel address and resistance code to the AD8403. */
static void ad8403_write(uint8_t channel, uint8_t code)
{
    uint8_t addr = (uint8_t)((channel - 1) & 0x03);

    P1->OUT &= ~CSPIN;
    spi_tx_byte(addr);
    spi_tx_byte(code);

    while (EUSCI_B0->STATW & EUSCI_B_STATW_SPI_BUSY);

    P1->OUT |= CSPIN;
}

/* Apply the same resistance code to all four channels. */
static void write_all(uint8_t code)
{
    uint8_t ch;

    for (ch = 1; ch <= 4; ch++)
        ad8403_write(ch, code);
}

/* Convert the target resistance into an 8-bit potentiometer code. */
static uint8_t ohms_to_code(uint32_t ohms)
{
    if (ohms <= RWIPER_OHMS) return 0;

    uint32_t d = ((ohms - RWIPER_OHMS) * 256u + (RAB_OHMS / 2u)) / RAB_OHMS;

    if (d > 255u) d = 255u;

    return (uint8_t)d;
}

/* Estimate the resistance represented by a potentiometer code. */
static uint32_t code_to_ohms(uint8_t code)
{
    return ((uint32_t)code * RAB_OHMS) / 256u + RWIPER_OHMS;
}

/* Set the resistance on all channels and report the result over UART. */
static void apply_ohms(uint32_t ohms)
{
    uint8_t code = ohms_to_code(ohms);

    write_all(code);

    uart_puts("Target ");
    uart_putu(ohms);
    uart_puts(" -> code ");
    uart_putu(code);
    uart_puts(", actual ~");
    uart_putu(code_to_ohms(code));
    uart_puts(" ohms\r\n");
}

/* Variables for receiving a numeric resistance value over UART. */
static uint32_t rx_val = 0;
static uint8_t rx_digits = 0;

/* Read a resistance value terminated by Enter. */
static uint8_t uart_poll_value(uint32_t *ohms)
{
    if (!(EUSCI_A0->IFG & EUSCI_A_IFG_RXIFG)) return 0;

    char c = (char)EUSCI_A0->RXBUF;

    if (c >= '0' && c <= '9' && rx_digits < 6) {
        rx_val = rx_val * 10u + (uint32_t)(c - '0');
        rx_digits++;
        uart_putc(c);
    } else if ((c == '\r' || c == '\n') && rx_digits > 0) {
        *ohms = rx_val;
        rx_val = 0;
        rx_digits = 0;
        uart_puts("\r\n");
        return 1;
    }

    return 0;
}

void main(void)
{
    uint32_t ohms = SWEEP_START_OHMS;
    uint32_t tick = 0;
    uint8_t manual = 0;
    uint8_t held1 = 0, held2 = 0;
    uint32_t typed;

    WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD;

    clock_init();
    uart_init();
    spi_init();
    buttons_init();

    uart_puts("\r\nS1: toggle AUTO/MANUAL | S2: step +1k (manual) | or type ohms + Enter\r\n");
    uart_puts("Mode: AUTO\r\n");

    apply_ohms(ohms);

    while (1) {
        delay_ms(1);

        if (btn_event(BIT1, &held1)) {
            manual ^= 1;
            tick = 0;
            rx_val = 0;
            rx_digits = 0;

            uart_puts(manual ? "Mode: MANUAL\r\n" : "Mode: AUTO\r\n");
        }

        if (manual) {
            if (btn_event(BIT4, &held2)) {
                ohms += SWEEP_STEP_OHMS;

                if (ohms > SWEEP_STOP_OHMS)
                    ohms = SWEEP_START_OHMS;

                apply_ohms(ohms);
            }

            if (uart_poll_value(&typed)) {
                ohms = typed;
                apply_ohms(ohms);
            }
        } else {
            if (++tick >= SWEEP_DWELL_MS) {
                tick = 0;
                ohms += SWEEP_STEP_OHMS;

                if (ohms > SWEEP_STOP_OHMS)
                    ohms = SWEEP_START_OHMS;

                apply_ohms(ohms);
            }
        }
    }
}
