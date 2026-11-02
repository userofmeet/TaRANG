#include "msp.h"
#include <stdint.h>

/*
 * MSP432P401R: AD8403ARUZ50 digipot (SPI) + 2x TS12A44513 (GPIO) from one UART terminal
 *
 * SPI  (eUSCI_B0): P1.5 = CLK, P1.6 = SDI (SIMO), P1.7 = CS (GPIO)
 * UART (eUSCI_A0): P1.2 = RXD, P1.3 = TXD, 115200 baud, SMCLK = 3 MHz
 * SW1 IN1..IN4 : P2.3, P2.4, P2.6, P2.7
 * SW2 IN1..IN4 : P5.0, P5.1, P5.2, P5.5
 *
 * Switch types: 1 = NO (IN HIGH = ON), 2 = NC (IN LOW = ON),
 *               3 = NO, 4 = NC
 */

/* ---------------- AD8403 settings ---------------- */
#define RAB_OHMS     50000u
#define RWIPER_OHMS  50u
#define CSPIN        BIT7          /* P1.7 */

/* ---------------- Switch pins ---------------- */
static const uint8_t SW1_PINS[4] = { BIT3, BIT4, BIT6, BIT7 };   /* P2 */
static const uint8_t SW2_PINS[4] = { BIT0, BIT1, BIT2, BIT5 };   /* P5 */
#define SW1_MASK  (BIT3 | BIT4 | BIT6 | BIT7)
#define SW2_MASK  (BIT0 | BIT1 | BIT2 | BIT5)
static const uint8_t IS_NC[4] = { 0, 1, 0, 1 };

/* ---------------- Clock: DCO 3 MHz ---------------- */
static void clock_init(void)
{
    CS->KEY  = CS_KEY_VAL;
    CS->CTL0 = CS_CTL0_DCORSEL_1;
    CS->CTL1 = CS_CTL1_SELM__DCOCLK | CS_CTL1_SELS__DCOCLK | CS_CTL1_SELA__REFOCLK;
    CS->KEY  = 0;
}

/* ---------------- UART ---------------- */
static void uart_init(void)
{
    P1->SEL0 |= BIT2 | BIT3;
    P1->SEL1 &= ~(BIT2 | BIT3);

    EUSCI_A0->CTLW0  = EUSCI_A_CTLW0_SWRST;
    EUSCI_A0->CTLW0 |= EUSCI_A_CTLW0_SSEL__SMCLK;
    EUSCI_A0->BRW    = 1;                                   /* 3 MHz / 115200 */
    EUSCI_A0->MCTLW  = (10 << EUSCI_A_MCTLW_BRF_OFS) | EUSCI_A_MCTLW_OS16;
    EUSCI_A0->CTLW0 &= ~EUSCI_A_CTLW0_SWRST;
}

static void uart_putc(char c)
{
    while (!(EUSCI_A0->IFG & EUSCI_A_IFG_TXIFG));
    EUSCI_A0->TXBUF = (uint8_t)c;
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
    while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) uart_putc(tmp[--i]);
}

/* ---------------- SPI (eUSCI_B0) ---------------- */
static void spi_init(void)
{
    P1->SEL0 &= ~CSPIN;                 /* P1.7 = GPIO CS, idle high */
    P1->SEL1 &= ~CSPIN;
    P1->DIR  |= CSPIN;
    P1->OUT  |= CSPIN;

    P1->SEL0 |= BIT5 | BIT6;            /* P1.5 CLK, P1.6 SIMO */
    P1->SEL1 &= ~(BIT5 | BIT6);

    EUSCI_B0->CTLW0 = EUSCI_B_CTLW0_SWRST;
    EUSCI_B0->CTLW0 |= EUSCI_B_CTLW0_MST
                     | EUSCI_B_CTLW0_SYNC
                     | EUSCI_B_CTLW0_MSB
                     | EUSCI_B_CTLW0_CKPH          /* mode 0, CLK idle low */
                     | EUSCI_B_CTLW0_SSEL__SMCLK;
    EUSCI_B0->BRW = 30;                            /* 100 kHz */
    EUSCI_B0->CTLW0 &= ~EUSCI_B_CTLW0_SWRST;
}

static void spi_tx_byte(uint8_t b)
{
    while (!(EUSCI_B0->IFG & EUSCI_B_IFG_TXIFG));
    EUSCI_B0->TXBUF = b;
}

/* AD8403 word = A1 A0 D7..D0; chip keeps the last 10 bits clocked in */
static void ad8403_write(uint8_t channel /* 1..4 */, uint8_t code)
{
    uint8_t addr = (uint8_t)((channel - 1) & 0x03);

    P1->OUT &= ~CSPIN;
    spi_tx_byte(addr);
    spi_tx_byte(code);
    while (EUSCI_B0->STATW & EUSCI_B_STATW_SPI_BUSY);
    P1->OUT |= CSPIN;                   /* latch */
}

static uint8_t ohms_to_code(uint32_t ohms)
{
    if (ohms <= RWIPER_OHMS) return 0;
    uint32_t d = ((ohms - RWIPER_OHMS) * 256u + (RAB_OHMS / 2u)) / RAB_OHMS;
    if (d > 255u) d = 255u;
    return (uint8_t)d;
}

static uint32_t code_to_ohms(uint8_t code)
{
    return ((uint32_t)code * RAB_OHMS) / 256u + RWIPER_OHMS;
}

/* ch = 1..4 for one channel, 0 for all channels */
static void pot_set(uint8_t ch, uint32_t ohms)
{
    uint8_t code = ohms_to_code(ohms);
    uint8_t c;

    if (ch == 0) {
        for (c = 1; c <= 4; c++) ad8403_write(c, code);
        uart_puts("All channels");
    } else {
        ad8403_write(ch, code);
        uart_puts("Channel ");
        uart_putu(ch);
    }
    uart_puts(": target ");
    uart_putu(ohms);
    uart_puts(" -> code ");
    uart_putu(code);
    uart_puts(", actual ~");
    uart_putu(code_to_ohms(code));
    uart_puts(" ohms\r\n");
}

/* ---------------- Switches ---------------- */
/* row = 1..4: switch 'row' ON in both ICs, others OFF. row = 0: all OFF */
static void SetOutputs(uint8_t row)
{
    uint8_t i;
    for (i = 0; i < 4; i++) {
        uint8_t want_on = (row == (uint8_t)(i + 1));
        uint8_t level = IS_NC[i] ? !want_on : want_on;

        if (level) {
            P2->OUT |= SW1_PINS[i];
            P5->OUT |= SW2_PINS[i];
        } else {
            P2->OUT &= ~SW1_PINS[i];
            P5->OUT &= ~SW2_PINS[i];
        }
    }
}

static void switches_init(void)
{
    P2->SEL0 &= ~SW1_MASK;
    P2->SEL1 &= ~SW1_MASK;
    P5->SEL0 &= ~SW2_MASK;
    P5->SEL1 &= ~SW2_MASK;

    SetOutputs(0);                      /* preload OFF levels before enabling outputs */
    P2->DIR |= SW1_MASK;
    P5->DIR |= SW2_MASK;
}

/* ---------------- Command parser ---------------- */
static void print_help(void)
{
    uart_puts("\r\nCommands (end with Enter):\r\n");
    uart_puts("  s0..s4         : s0 = all switches OFF, sN = switch N ON in SW1 and SW2\r\n");
    uart_puts("  r<ch> <ohms>   : set digipot channel 1-4, e.g. r2 12000\r\n");
    uart_puts("  ra <ohms>      : set all 4 channels, e.g. ra 25000\r\n");
    uart_puts("  h or ?         : this help\r\n");
}

/* Parses an unsigned number at *p, skipping spaces. Returns 1 on success. */
static uint8_t parse_num(const char **p, uint32_t *out)
{
    const char *s = *p;
    uint32_t v = 0;
    uint8_t n = 0;

    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (uint32_t)(*s - '0');
        s++; n++;
        if (n > 6) return 0;
    }
    if (n == 0) return 0;
    *p = s;
    *out = v;
    return 1;
}

static void handle_line(const char *line)
{
    const char *p = line;
    uint32_t n, ohms;
    char cmd;

    while (*p == ' ') p++;
    cmd = *p++;
    if (cmd >= 'A' && cmd <= 'Z') cmd = (char)(cmd - 'A' + 'a');

    switch (cmd) {
    case 's':
        if (parse_num(&p, &n) && n <= 4) {
            SetOutputs((uint8_t)n);
            if (n == 0) {
                uart_puts("ALL SWITCHES OFF\r\n");
            } else {
                uart_puts("Switch ");
                uart_putu(n);
                uart_puts(" ON in SW1 and SW2, all others OFF\r\n");
            }
        } else {
            uart_puts("Invalid: use s0..s4\r\n");
        }
        break;

    case 'r':
        if (*p == 'a' || *p == 'A') {                       /* all channels */
            p++;
            if (parse_num(&p, &ohms)) pot_set(0, ohms);
            else uart_puts("Invalid: use ra <ohms>\r\n");
        } else if (parse_num(&p, &n) && n >= 1 && n <= 4 && parse_num(&p, &ohms)) {
            pot_set((uint8_t)n, ohms);
        } else {
            uart_puts("Invalid: use r<1-4> <ohms>\r\n");
        }
        break;

    case 'h':
    case '?':
        print_help();
        break;

    case '\0':
        break;                                              /* empty line */

    default:
        uart_puts("Unknown command, type h for help\r\n");
        break;
    }
}

/* ---------------- Main ---------------- */
void main(void)
{
    char    line[20];
    uint8_t len = 0;

    WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD;

    clock_init();
    uart_init();
    spi_init();
    switches_init();

    /* default: mid-scale on all digipot channels would be a surprise, so start at max (50k) */
    pot_set(0, RAB_OHMS);

    uart_puts("\r\nMSP432 Digipot + Switch Controller\r\n");
    print_help();

    while (1) {
        if (EUSCI_A0->IFG & EUSCI_A_IFG_RXIFG) {
            char c = (char)EUSCI_A0->RXBUF;

            if (c == '\r' || c == '\n') {
                uart_puts("\r\n");
                line[len] = '\0';
                handle_line(line);
                len = 0;
            } else if ((c == 0x08 || c == 0x7F) && len > 0) {   /* backspace */
                len--;
                uart_puts("\b \b");
            } else if (c >= ' ' && len < sizeof(line) - 1) {
                line[len++] = c;
                uart_putc(c);                                    /* echo */
            }
        }
    }
}
