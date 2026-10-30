/*
 * File: ad8403_sweep.c
 *
 * Objective:
 *   Control an AD8403ARUZ50 (50 kOhm, 4-channel, 8-bit) digital potentiometer
 *   from an MSP432P401R LaunchPad over SPI, setting all four channels to the
 *   same resistance (gain-setting resistor).
 *
 *   Modes:
 *     AUTO   - sweeps 1 kOhm to 50 kOhm in 1 kOhm steps, with a dwell per step.
 *     MANUAL - S2 steps +1 kOhm, or type a value in ohms over UART and press Enter.
 *     S1 toggles between AUTO and MANUAL.
 *
 * Pins:
 *   P1.5 -> CLK (eUSCI_B0)    P1.6 -> SDI (eUSCI_B0 SIMO)    P1.7 -> CS (GPIO)
 *   P1.2/P1.3 -> UART RX/TX (eUSCI_A0, 115200 baud via XDS110 USB)
 *   P1.1 (S1), P1.4 (S2) -> push buttons, active low
 *
 * Clock: DCO at 3 MHz (MCLK = SMCLK)
 */

#include "msp.h"
#include <stdint.h>

/* ---------- AD8403ARUZ50 settings ---------- */
#define RAB_OHMS      50000u     /* 50k version */
#define RWIPER_OHMS   50u        /* typical wiper resistance */
#define CSPIN         BIT7       /* P1.7 = GPIO chip-select */

/* ---------- Sweep settings ---------- */
#define SWEEP_START_OHMS   1000u
#define SWEEP_STOP_OHMS    50000u
#define SWEEP_STEP_OHMS    1000u
#define SWEEP_DWELL_MS     1000u      /* dwell per step in ms (15000u for 15 s) */

/* ---------- Clock: DCO 3 MHz ---------- */
static void clock_init(void)
{
    CS->KEY  = CS_KEY_VAL;
    CS->CTL0 = CS_CTL0_DCORSEL_1;
    CS->CTL1 = CS_CTL1_SELM__DCOCLK | CS_CTL1_SELS__DCOCLK | CS_CTL1_SELA__REFOCLK;
    CS->KEY  = 0;
}

/* ---------- Millisecond delay using SysTick (core clock = 3 MHz) ---------- */
static void delay_ms(uint32_t ms)
{
    SysTick->LOAD = 3000u - 1u;            /* 3000 ticks @ 3 MHz = 1 ms */
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;

    while (ms--) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));   /* flag clears on read */
    }
    SysTick->CTRL = 0;
}

/* ---------- UART (eUSCI_A0 -> XDS110 USB) ---------- */
static void uart_init(void)
{
    P1->SEL0 |= BIT2 | BIT3;                 /* P1.2 RX, P1.3 TX */
    P1->SEL1 &= ~(BIT2 | BIT3);

    EUSCI_A0->CTLW0 = EUSCI_A_CTLW0_SWRST;
    EUSCI_A0->CTLW0 |= EUSCI_A_CTLW0_SSEL__SMCLK;
    EUSCI_A0->BRW   = 1;                     /* 3 MHz / 115200 */
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
    if (v == 0) { uart_putc('0'); return; }
    while (v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) uart_putc(tmp[--i]);
}

/* ---------- Buttons: S1 = P1.1, S2 = P1.4 (active low, internal pull-ups) ---------- */
static void buttons_init(void)
{
    P1->SEL0 &= ~(BIT1 | BIT4);
    P1->SEL1 &= ~(BIT1 | BIT4);
    P1->DIR  &= ~(BIT1 | BIT4);
    P1->REN  |=  (BIT1 | BIT4);
    P1->OUT  |=  (BIT1 | BIT4);
}

/* Call once per 1 ms. Returns 1 once after the button has been low for 20 ms. */
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

/* ---------- SPI (eUSCI_B0): CLK = P1.5, SIMO = P1.6, CS = P1.7 (GPIO) ---------- */
static void spi_init(void)
{
    /* P1.7 = chip select, GPIO output, idle high */
    P1->SEL0 &= ~CSPIN;
    P1->SEL1 &= ~CSPIN;
    P1->DIR  |= CSPIN;
    P1->OUT  |= CSPIN;

    /* P1.5 CLK, P1.6 SIMO -> eUSCI_B0 */
    P1->SEL0 |= BIT5 | BIT6;
    P1->SEL1 &= ~(BIT5 | BIT6);

    EUSCI_B0->CTLW0 = EUSCI_B_CTLW0_SWRST;
    EUSCI_B0->CTLW0 |= EUSCI_B_CTLW0_MST
                     | EUSCI_B_CTLW0_SYNC
                     | EUSCI_B_CTLW0_MSB
                     | EUSCI_B_CTLW0_CKPH          /* SPI mode 0: CLK idle low */
                     | EUSCI_B_CTLW0_SSEL__SMCLK;
    EUSCI_B0->BRW = 30;                            /* 3 MHz / 30 = 100 kHz */
    EUSCI_B0->CTLW0 &= ~EUSCI_B_CTLW0_SWRST;
}

static void spi_tx_byte(uint8_t b)
{
    while (!(EUSCI_B0->IFG & EUSCI_B_IFG_TXIFG));
    EUSCI_B0->TXBUF = b;
}

/* AD8403 word = A1 A0 D7..D0 (10 bits). Two bytes are sent; the chip keeps the last 10 bits. */
static void ad8403_write(uint8_t channel /* 1..4 */, uint8_t code)
{
    uint8_t addr = (uint8_t)((channel - 1) & 0x03);

    P1->OUT &= ~CSPIN;                                   /* CS low */
    spi_tx_byte(addr);
    spi_tx_byte(code);
    while (EUSCI_B0->STATW & EUSCI_B_STATW_SPI_BUSY);    /* wait until last bit is out */
    P1->OUT |= CSPIN;                                    /* CS high: latch into RDAC */
}

static void write_all(uint8_t code)
{
    uint8_t ch;
    for (ch = 1; ch <= 4; ch++)
        ad8403_write(ch, code);
}

/* R(W-B) = (D/256) * RAB + Rw */
static uint8_t ohms_to_code(uint32_t ohms)
{
    if (ohms <= RWIPER_OHMS) return 0;
    uint32_t d = ((ohms - RWIPER_OHMS) * 256u + (RAB_OHMS / 2u)) / RAB_OHMS;   /* rounded */
    if (d > 255u) d = 255u;
    return (uint8_t)d;
}

static uint32_t code_to_ohms(uint8_t code)
{
    return ((uint32_t)code * RAB_OHMS) / 256u + RWIPER_OHMS;
}

/* ---------- Apply a resistance to all four channels and report it ---------- */
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

/* ---------- Non-blocking UART number entry (manual mode) ---------- */
static uint32_t rx_val = 0;
static uint8_t  rx_digits = 0;

/* Returns 1 and sets *ohms when a full line has been received. */
static uint8_t uart_poll_value(uint32_t *ohms)
{
    if (!(EUSCI_A0->IFG & EUSCI_A_IFG_RXIFG)) return 0;

    char c = (char)EUSCI_A0->RXBUF;

    if (c >= '0' && c <= '9' && rx_digits < 6) {
        rx_val = rx_val * 10u + (uint32_t)(c - '0');
        rx_digits++;
        uart_putc(c);                       /* echo */
    } else if ((c == '\r' || c == '\n') && rx_digits > 0) {
        *ohms = rx_val;
        rx_val = 0;
        rx_digits = 0;
        uart_puts("\r\n");
        return 1;
    }
    return 0;
}

/* ---------- Main ---------- */
void main(void)
{
    uint32_t ohms = SWEEP_START_OHMS;
    uint32_t tick = 0;
    uint8_t  manual = 0;
    uint8_t  held1 = 0, held2 = 0;
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
        delay_ms(1);                                   /* 1 ms tick */

        if (btn_event(BIT1, &held1)) {                 /* S1: toggle mode */
            manual ^= 1;
            tick = 0;
            rx_val = 0; rx_digits = 0;
            uart_puts(manual ? "Mode: MANUAL\r\n" : "Mode: AUTO\r\n");
        }

        if (manual) {
            if (btn_event(BIT4, &held2)) {             /* S2: step up */
                ohms += SWEEP_STEP_OHMS;
                if (ohms > SWEEP_STOP_OHMS) ohms = SWEEP_START_OHMS;
                apply_ohms(ohms);
            }
            if (uart_poll_value(&typed)) {             /* typed value */
                ohms = typed;
                apply_ohms(ohms);
            }
        } else {
            if (++tick >= SWEEP_DWELL_MS) {            /* auto sweep */
                tick = 0;
                ohms += SWEEP_STEP_OHMS;
                if (ohms > SWEEP_STOP_OHMS) ohms = SWEEP_START_OHMS;
                apply_ohms(ohms);
            }
        }
    }
}
