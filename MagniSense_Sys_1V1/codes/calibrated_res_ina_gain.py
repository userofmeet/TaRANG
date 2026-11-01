#include "msp.h"
#include <stdint.h>

/* ---------- AD8403ARUZ50 settings ---------- */
#define RAB_OHMS      50000u     /* 50k version                  */
#define RWIPER_OHMS   50u        /* typical wiper resistance     */
#define CSPIN         BIT7       /* P1.7 = GPIO chip-select      */

/* ---------- Clock: DCO 3 MHz ---------- */
static void clock_init(void)
{
    CS->KEY  = CS_KEY_VAL;
    CS->CTL0 = CS_CTL0_DCORSEL_1;
    CS->CTL1 = CS_CTL1_SELM__DCOCLK | CS_CTL1_SELS__DCOCLK | CS_CTL1_SELA__REFOCLK;
    CS->KEY  = 0;
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

static char uart_getc(void)
{
    while (!(EUSCI_A0->IFG & EUSCI_A_IFG_RXIFG));
    return (char)EUSCI_A0->RXBUF;
}

/* read a line with echo and backspace support */
static void uart_getline(char *buf, int max)
{
    int i = 0;
    while (1) {
        char c = uart_getc();
        if (c == '\r' || c == '\n') {
            uart_puts("\r\n");
            buf[i] = 0;
            return;
        } else if ((c == 8 || c == 127) && i > 0) {
            i--;
            uart_puts("\b \b");
        } else if (c >= ' ' && i < max - 1) {
            buf[i++] = c;
            uart_putc(c);
        }
    }
}

static uint32_t parse_uint(const char *s)
{
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return v;
}

/* ---------- SPI (eUSCI_B0) : CLK=P1.5, SIMO=P1.6, CS=P1.7 (GPIO) ---------- */
static void spi_init(void)
{
    /* P1.7 = chip select, plain GPIO output, idle HIGH */
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
    EUSCI_B0->BRW = 30;                            /* 3 MHz / 30 = 100 kHz (safe for first test) */
    EUSCI_B0->CTLW0 &= ~EUSCI_B_CTLW0_SWRST;
}

static void spi_tx_byte(uint8_t b)
{
    while (!(EUSCI_B0->IFG & EUSCI_B_IFG_TXIFG));
    EUSCI_B0->TXBUF = b;
}

/* AD8403 word = A1 A0 D7..D0 (10 bits). Two bytes are sent; the chip keeps the last 10 bits. */
static void ad8403_write(uint8_t channel /*1..4*/, uint8_t code)
{
    uint8_t addr = (uint8_t)((channel - 1) & 0x03);

    P1->OUT &= ~CSPIN;                                   /* CS low   */
    spi_tx_byte(addr);
    spi_tx_byte(code);
    while (EUSCI_B0->STATW & EUSCI_B_STATW_SPI_BUSY);    /* wait until last bit is out */
    P1->OUT |= CSPIN;                                    /* CS high -> latch into RDAC */
}

/* R(W-B) = (D/256)*50k + Rw */
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

/* ---------- Main ---------- */
#define CH_ALL  0u      /* channel value meaning "write to all four" */

static void write_all(uint8_t code)
{
    uint8_t ch;
    for (ch = 1; ch <= 4; ch++)
        ad8403_write(ch, code);
}

void main(void)
{
    char line[32];
    uint32_t channel = CH_ALL;      /* start in ALL mode */

    WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD;

    clock_init();
    uart_init();
    spi_init();

    uart_puts("\r\n=== AD8403 Digital Pot Control (MSP432P401R) ===\r\n");
    uart_puts("Mode: ALL channels (1-4). Enter 'c' at the prompt to change mode.\r\n");

    while (1) {
        if (channel == CH_ALL)
            uart_puts("[ALL] Enter resistance in ohms (50..50000), or 'c' to select channel: ");
        else {
            uart_puts("[CH");
            uart_putu(channel);
            uart_puts("] Enter resistance in ohms (50..50000), or 'c' to change channel: ");
        }
        uart_getline(line, sizeof(line));

        /* ---- change channel / mode ---- */
        if (line[0] == 'c' || line[0] == 'C') {
            uint32_t sel = 99;
            while (sel > 4) {
                uart_puts("Select channel (1-4), or 0/'a' for ALL: ");
                uart_getline(line, sizeof(line));
                if (line[0] == 'a' || line[0] == 'A')
                    sel = CH_ALL;
                else if (line[0] >= '0' && line[0] <= '9') {
                    sel = parse_uint(line);
                    if (sel > 4) uart_puts("Invalid channel!\r\n");
                } else {
                    uart_puts("Invalid channel!\r\n");
                }
            }
            channel = sel;
            continue;
        }

        /* ---- set resistance ---- */
        uint32_t ohms = parse_uint(line);
        if (ohms == 0) {
            uart_puts("Invalid value!\r\n");
            continue;
        }

        uint8_t code = ohms_to_code(ohms);

        if (channel == CH_ALL) {
            write_all(code);
            uart_puts("CH1-4 -> code ");
        } else {
            ad8403_write((uint8_t)channel, code);
            uart_puts("CH");
            uart_putu(channel);
            uart_puts(" -> code ");
        }
        uart_putu(code);
        uart_puts(", actual ~");
        uart_putu(code_to_ohms(code));
        uart_puts(" ohms\r\n");
    }
}
