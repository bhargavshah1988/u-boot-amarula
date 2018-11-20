// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2006 Ben Warren, Qstreams Networks Inc.
 * With help from the common/soft_spi and arch/powerpc/cpu/mpc8260 drivers
 */

#include <common.h>

#include <malloc.h>
#include <spi.h>
#include <asm/mpc8xxx_spi.h>

enum {
	SPI_EV_NE = BIT(31 - 22),	/* Receiver Not Empty */
	SPI_EV_NF = BIT(31 - 23),	/* Transmitter Not Full */
};

enum {
	SPI_MODE_LOOP  = BIT(31 - 1),	/* Loopback mode */
	SPI_MODE_CI    = BIT(31 - 2),	/* Clock invert */
	SPI_MODE_CP    = BIT(31 - 3),	/* Clock phase */
	SPI_MODE_DIV16 = BIT(31 - 4),	/* Divide clock source by 16 */
	SPI_MODE_REV   = BIT(31 - 5),	/* Reverse mode - MSB first */
	SPI_MODE_MS    = BIT(31 - 6),	/* Always master */
	SPI_MODE_EN    = BIT(31 - 7),	/* Enable interface */

	SPI_MODE_LEN_MASK = 0xf00000,
	SPI_MODE_PM_MASK = 0xf0000,

	SPI_COM_LST = BIT(31 - 9),
};

static inline u32 to_prescale_mod(u32 val)
{
	return (min(val, (u32)15) << 16);
}

static void set_char_len(spi8xxx_t *spi, u32 val)
{
	clrsetbits_be32(&spi->mode, SPI_MODE_LEN_MASK, (val << 20));
}

#define SPI_TIMEOUT	1000

struct spi_slave *spi_setup_slave(uint bus, uint cs, uint max_hz, uint mode)
{
	struct spi_slave *slave;

	if (!spi_cs_is_valid(bus, cs))
		return NULL;

	slave = spi_alloc_slave_base(bus, cs);
	if (!slave)
		return NULL;

	/*
	 * TODO: Some of the code in spi_init() should probably move
	 * here, or into spi_claim_bus() below.
	 */

	return slave;
}

void spi_free_slave(struct spi_slave *slave)
{
	free(slave);
}

void spi_init(void)
{
	spi8xxx_t *spi = &((immap_t *)(CONFIG_SYS_IMMR))->spi;

	/*
	 * SPI pins on the MPC83xx are not muxed, so all we do is initialize
	 * some registers
	 */
	out_be32(&spi->mode, SPI_MODE_REV | SPI_MODE_MS | SPI_MODE_EN);
	/* Use SYSCLK / 8 (16.67MHz typ.) */
	clrsetbits_be32(&spi->mode, SPI_MODE_PM_MASK, to_prescale_mod(1));
	/* Clear all SPI events */
	setbits_be32(&spi->event, 0xffffffff);
	/* Mask  all SPI interrupts */
	clrbits_be32(&spi->mask, 0xffffffff);
	/* LST bit doesn't do anything, so disregard */
	out_be32(&spi->com, 0);
}

int spi_claim_bus(struct spi_slave *slave)
{
	return 0;
}

void spi_release_bus(struct spi_slave *slave)
{
}

int spi_xfer(struct spi_slave *slave, uint bitlen, const void *dout, void *din,
	     ulong flags)
{
	spi8xxx_t *spi = &((immap_t *)(CONFIG_SYS_IMMR))->spi;
	u32 tmpdin;
	int num_blks = DIV_ROUND_UP(bitlen, 32);

	debug("%s: slave %u:%u dout %08X din %08X bitlen %u\n", __func__,
	      slave->bus, slave->cs, *(uint *)dout, *(uint *)din, bitlen);

	if (flags & SPI_XFER_BEGIN)
		spi_cs_activate(slave);

	/* Clear all SPI events */
	setbits_be32(&spi->event, 0xffffffff);

	/* Handle data in 32-bit chunks */
	while (num_blks--) {
		u32 tmpdout = 0;
		uchar xfer_bitlen = (bitlen >= 32 ? 32 : bitlen);
		ulong start;

		clrbits_be32(&spi->mode, SPI_MODE_EN);

		/* Set up length for this transfer */

		if (bitlen <= 4) /* 4 bits or less */
			set_char_len(spi, 3);
		else if (bitlen <= 16) /* at most 16 bits */
			set_char_len(spi, bitlen - 1);
		else /* more than 16 bits -> full 32 bit transfer */
			set_char_len(spi, 0);

		setbits_be32(&spi->mode, SPI_MODE_EN);

		/* Shift data so it's msb-justified */
		tmpdout = *(u32 *)dout >> (32 - xfer_bitlen);

		if (bitlen > 32) {
			/* Set up the next iteration if sending > 32 bits */
			bitlen -= 32;
			dout += 4;
		}

		/* Write the data out */
		out_be32(&spi->tx, tmpdout);

		debug("*** %s: ... %08x written\n", __func__, tmpdout);

		/*
		 * Wait for SPI transmit to get out
		 * or time out (1 second = 1000 ms)
		 * The NE event must be read and cleared first
		 */
		start = get_timer(0);
		do {
			u32 event = in_be32(&spi->event);
			bool have_ne = event & SPI_EV_NE;
			bool have_nf = event & SPI_EV_NF;

			if (!have_ne)
				continue;

			tmpdin = in_be32(&spi->rx);
			setbits_be32(&spi->event, SPI_EV_NE);

			*(u32 *)din = (tmpdin << (32 - xfer_bitlen));
			if (xfer_bitlen == 32) {
				/* Advance output buffer by 32 bits */
				din += 4;
			}

			/*
			 * Only bail when we've had both NE and NF events.
			 * This will cause timeouts on RO devices, so maybe
			 * in the future put an arbitrary delay after writing
			 * the device.  Arbitrary delays suck, though...
			 */
			if (have_nf)
				break;

			mdelay(1);
		} while (get_timer(start) < SPI_TIMEOUT);

		if (get_timer(start) >= SPI_TIMEOUT)
			debug("*** %s: Time out during SPI transfer\n",
			      __func__);

		debug("*** %s: transfer ended. Value=%08x\n", __func__, tmpdin);
	}

	if (flags & SPI_XFER_END)
		spi_cs_deactivate(slave);

	return 0;
}
