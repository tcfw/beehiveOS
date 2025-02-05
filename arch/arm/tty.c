#include <kernel/stdint.h>
#include <kernel/unistd.h>

#define PL011_DR_OFFSET 0x000
#define PL011_FR_OFFSET 0x018
#define PL011_IBRD_OFFSET 0x024
#define PL011_FBRD_OFFSET 0x028
#define PL011_LCR_OFFSET 0x02c
#define PL011_CR_OFFSET 0x030
#define PL011_IMSC_OFFSET 0x038
#define PL011_DMACR_OFFSET 0x048

#define PL011_FR_BUSY (1 << 5)

#define PL011_CR_TXEN (1 << 8)
#define PL011_CR_UARTEN (1 << 0)

#define PL011_LCR_FEN (1 << 4)
#define PL011_LCR_STP2 (1 << 3)

struct pl011
{
    uint64_t base_address;
    uint64_t base_clock;
    uint32_t baudrate;
    uint32_t data_bits;
    uint32_t stop_bits;
};

static void pl011_regwrite(const struct pl011 *dev, uint32_t offset, uint32_t data)
{
    uint64_t addr = dev->base_address + offset;
    *(uint32_t *)addr = data;
}

static uint32_t pl011_regread(const struct pl011 *dev, uint32_t offset)
{
    uint64_t *addr = dev->base_address + offset;

    return *addr;
}

static void pl011_wait_tx_complete(const struct pl011 *dev)
{
    while ((pl011_regread(dev, PL011_FR_OFFSET) & PL011_FR_BUSY) != 0)
    {
    }
}

static void pl011_calculate_divisors(const struct pl011 *dev, uint32_t *integer, uint32_t *fractional)
{
    // 64 * F_UARTCLK / (16 * B) = 4 * F_UARTCLK / B
    const uint32_t div = 833; // 4 * dev->base_clock / dev->baudrate;

    *fractional = div & 0x3f;
    *integer = (div >> 6) & 0xffff;
}

static int pl011_reset(const struct pl011 *dev)
{
    uint32_t cr = pl011_regread(dev, PL011_CR_OFFSET);
    uint32_t lcr = pl011_regread(dev, PL011_LCR_OFFSET);
    uint32_t ibrd, fbrd;

    // Disable UART before anything else
    pl011_regwrite(dev, PL011_CR_OFFSET, cr & PL011_CR_UARTEN);

    // Wait for any ongoing transmissions to complete
    pl011_wait_tx_complete(dev);

    // Flush FIFOs
    pl011_regwrite(dev, PL011_LCR_OFFSET, lcr & ~PL011_LCR_FEN);

    // Set frequency divisors (UARTIBRD and UARTFBRD) to configure the speed
    pl011_calculate_divisors(dev, &ibrd, &fbrd);
    pl011_regwrite(dev, PL011_IBRD_OFFSET, ibrd);
    pl011_regwrite(dev, PL011_FBRD_OFFSET, fbrd);

    // Configure data frame format according to the parameters (UARTLCR_H).
    // We don't actually use all the possibilities, so this part of the code
    // can be simplified.
    lcr = 0x0;
    // WLEN part of UARTLCR_H, you can check that this calculation does the
    // right thing for yourself
    lcr |= ((dev->data_bits - 1) & 0x3) << 5;
    // Configure the number of stop bits
    if (dev->stop_bits == 2)
        lcr |= PL011_LCR_STP2;

    // Mask all interrupts by setting corresponding bits to 1
    // pl011_regwrite(dev, PL011_IMSC_OFFSET, 0x7ff);

    // Disable DMA by setting all bits to 0
    pl011_regwrite(dev, PL011_DMACR_OFFSET, 0x0);

    // I only need transmission, so that's the only thing I enabled.
    pl011_regwrite(dev, PL011_CR_OFFSET, PL011_CR_TXEN);

    // Finally enable UART
    pl011_regwrite(dev, PL011_CR_OFFSET, PL011_CR_TXEN | PL011_CR_UARTEN);

    return 0;
}

static int pl011_send(const struct pl011 *dev, const char *data, size_t size)
{
    // make sure that there is no outstanding transfer just in case
    pl011_wait_tx_complete(dev);

    for (size_t i = 0; i < size; ++i)
    {
        if (data[i] == '\n')
        {
            pl011_regwrite(dev, PL011_DR_OFFSET, '\r');
            pl011_wait_tx_complete(dev);
        }
        pl011_regwrite(dev, PL011_DR_OFFSET, data[i]);
        pl011_wait_tx_complete(dev);
    }

    return 0;
}

static int pl011_setup(struct pl011 *dev, uint64_t base_address, uint64_t base_clock)
{
    dev->base_address = base_address;
    dev->base_clock = base_clock;

    dev->baudrate = 115200;
    dev->data_bits = 8;
    dev->stop_bits = 1;
    
    return pl011_reset(dev);
}

static struct pl011 serial;

void terminal_initialize(void)
{
    pl011_setup(&serial, /* base_address = */ 0x09000000, /* base_clock = */ 24000000);
}

void terminal_putchar(char c)
{
    pl011_send(&serial, &c, 1);
}

void terminal_writestring(const char *str)
{
    while (*str)
        terminal_putchar(*str++);
}

void terminal_write(const char *data, size_t size)
{
    for (size_t i = 0; i < size; i++)
        terminal_putchar(data[i]);
}