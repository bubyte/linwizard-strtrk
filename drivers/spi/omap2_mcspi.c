/*
 * OMAP2 McSPI controller driver
 *
 * Copyright (C) 2005, 2006 Nokia Corporation
 * Author: Samuel Ortiz <samuel.ortiz@nokia.com> and
 *         Juha Yrj�l� <juha.yrjola@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/platform_device.h>

#include <linux/spi/spi.h>

#include <asm/io.h>
#include <asm/arch/mcspi.h>

#define OMAP2_MCSPI_MAX_FREQ		48000000

#define OMAP2_MCSPI_REVISION		0x00
#define OMAP2_MCSPI_SYSCONFIG		0x10
#define OMAP2_MCSPI_SYSSTATUS		0x14
#define OMAP2_MCSPI_IRQSTATUS		0x18
#define OMAP2_MCSPI_IRQENABLE		0x1c
#define OMAP2_MCSPI_WAKEUPENABLE	0x20
#define OMAP2_MCSPI_SYST		0x24
#define OMAP2_MCSPI_MODULCTRL		0x28
#define OMAP2_MCSPI_CHCONF0		0x2c
#define OMAP2_MCSPI_CHSTAT0		0x30
#define OMAP2_MCSPI_CHCTRL0		0x34
#define OMAP2_MCSPI_TX0			0x38
#define OMAP2_MCSPI_RX0			0x3c

#define OMAP2_MCSPI_SYSCONFIG_SOFTRESET	(1 << 1)

#define OMAP2_MCSPI_SYSSTATUS_RESETDONE	(1 << 0)

#define OMAP2_MCSPI_MODULCTRL_SINGLE	(1 << 0)
#define OMAP2_MCSPI_MODULCTRL_MS	(1 << 2)
#define OMAP2_MCSPI_MODULCTRL_STEST	(1 << 3)

#define OMAP2_MCSPI_CHCONF_PHA		(1 << 0)
#define OMAP2_MCSPI_CHCONF_POL		(1 << 1)
#define OMAP2_MCSPI_CHCONF_CLKD_MASK	(0x0f << 2)
#define OMAP2_MCSPI_CHCONF_EPOL		(1 << 6)
#define OMAP2_MCSPI_CHCONF_WL_MASK	(0x1f << 7)
#define OMAP2_MCSPI_CHCONF_TRM_RX_ONLY	(0x01 << 12)
#define OMAP2_MCSPI_CHCONF_TRM_TX_ONLY	(0x02 << 12)
#define OMAP2_MCSPI_CHCONF_TRM_MASK	(0x03 << 12)
#define OMAP2_MCSPI_CHCONF_DPE0		(1 << 16)
#define OMAP2_MCSPI_CHCONF_DPE1		(1 << 17)
#define OMAP2_MCSPI_CHCONF_IS		(1 << 18)
#define OMAP2_MCSPI_CHCONF_TURBO	(1 << 19)
#define OMAP2_MCSPI_CHCONF_FORCE	(1 << 20)


#define OMAP2_MCSPI_CHSTAT_RXS		(1 << 0)
#define OMAP2_MCSPI_CHSTAT_TXS		(1 << 1)
#define OMAP2_MCSPI_CHSTAT_EOT		(1 << 2)

#define OMAP2_MCSPI_CHCTRL_EN		(1 << 0)

struct omap2_mcspi {
	struct tasklet_struct   tasklet;
	spinlock_t		lock;
	struct list_head	msg_queue;
	struct spi_master	*master;
};

struct omap2_mcspi_cs {
	u8 transmit_mode;
};

#define MOD_REG_BIT(val, mask, set) do { \
	if (set) \
		val |= mask; \
	else \
		val &= ~mask; \
} while(0)


#define MASTER_PDATA(master) (struct omap2_mcspi_platform_config *)((master)->cdev.dev->platform_data)

static inline void mcspi_write_reg(const struct spi_master *master,
				   int idx, u32 val)
{
	struct omap2_mcspi_platform_config *pdata = MASTER_PDATA(master);

	__raw_writel(val, pdata->base + idx);
}

static inline u32 mcspi_read_reg(const struct spi_master *master,
				 int idx)
{
	struct omap2_mcspi_platform_config *pdata = MASTER_PDATA(master);

	return __raw_readl(pdata->base + idx);
}

static inline void mcspi_write_cs_reg(const struct spi_device *spi,
				      int idx, u32 val)
{
	struct omap2_mcspi_platform_config *pdata = MASTER_PDATA(spi->master);

	__raw_writel(val, pdata->base + spi->chip_select * 0x14 + idx);
}

static inline u32 mcspi_read_cs_reg(const struct spi_device *spi,
				    int idx)
{
	struct omap2_mcspi_platform_config *pdata = MASTER_PDATA(spi->master);

	return __raw_readl(pdata->base + spi->chip_select * 0x14 + idx);
}

static void omap2_mcspi_set_enable(const struct spi_device *spi, int enable)
{
	u32 l;

	l = mcspi_read_cs_reg(spi, OMAP2_MCSPI_CHCTRL0);
	MOD_REG_BIT(l, OMAP2_MCSPI_CHCTRL_EN, enable);
	mcspi_write_cs_reg(spi, OMAP2_MCSPI_CHCTRL0, l);
}

static void omap2_mcspi_force_cs(struct spi_device *spi, int cs_active)
{
	u32 l;

	l = mcspi_read_cs_reg(spi, OMAP2_MCSPI_CHCONF0);
	MOD_REG_BIT(l, OMAP2_MCSPI_CHCONF_FORCE, cs_active);
	mcspi_write_cs_reg(spi, OMAP2_MCSPI_CHCONF0, l);
}

static void omap2_mcspi_set_master_mode(struct spi_device *spi, int single_channel)
{
	u32 l;

	/* Need reset when switching from slave mode */
	l = mcspi_read_reg(spi->master, OMAP2_MCSPI_MODULCTRL);
	MOD_REG_BIT(l, OMAP2_MCSPI_MODULCTRL_STEST, 0);
	MOD_REG_BIT(l, OMAP2_MCSPI_MODULCTRL_MS, 0);
	MOD_REG_BIT(l, OMAP2_MCSPI_MODULCTRL_SINGLE, single_channel);
	mcspi_write_reg(spi->master, OMAP2_MCSPI_MODULCTRL, l);
}

static void omap2_mcspi_txrx(struct spi_device *spi, struct spi_transfer *xfer)
{
	unsigned int		count, c;
	u32                     l;
	unsigned long		base, tx_reg, rx_reg, chstat_reg;

	count = xfer->len;
	c = count;

	l = mcspi_read_cs_reg(spi, OMAP2_MCSPI_CHCONF0);
	l &= ~OMAP2_MCSPI_CHCONF_TRM_MASK;
	if (xfer->tx_buf == NULL)
		l |= OMAP2_MCSPI_CHCONF_TRM_RX_ONLY;
	else if (xfer->rx_buf == NULL)
		l |= OMAP2_MCSPI_CHCONF_TRM_TX_ONLY;
	mcspi_write_cs_reg(spi, OMAP2_MCSPI_CHCONF0, l);

	omap2_mcspi_set_enable(spi, 1);

	/* We store the pre-calculated register addresses on stack to speed
	 * up the transfer loop. */
	base = (MASTER_PDATA(spi->master))->base + spi->chip_select * 0x14;
	tx_reg		= base + OMAP2_MCSPI_TX0;
	rx_reg		= base + OMAP2_MCSPI_RX0;
	chstat_reg	= base + OMAP2_MCSPI_CHSTAT0;

	/* RX_ONLY mode needs dummy data in TX reg */
	if (xfer->tx_buf == NULL)
		__raw_writel(0, tx_reg);

	if (spi->bits_per_word <= 8) {
		u8		*rx;
		const u8	*tx;

		rx = xfer->rx_buf;
		tx = xfer->tx_buf;

		while (c--) {
			if (tx != NULL) {
				while (!(__raw_readl(chstat_reg) & OMAP2_MCSPI_CHSTAT_TXS));
				__raw_writel(*tx, tx_reg);
			}
			if (rx != NULL) {
				while (!(__raw_readl(chstat_reg) & OMAP2_MCSPI_CHSTAT_RXS));
				if (c == 0 && tx == NULL)
					omap2_mcspi_set_enable(spi, 0);
				*rx++ = __raw_readl(rx_reg);
			}
		}
	} else if (spi->bits_per_word <= 16) {
		u16		*rx;
		const u16	*tx;

		rx = xfer->rx_buf;
		tx = xfer->tx_buf;
		c >>= 1;
		while (c--) {
			if (tx != NULL) {
				while (!(__raw_readl(chstat_reg) & OMAP2_MCSPI_CHSTAT_TXS));
				__raw_writel(*tx++, tx_reg);
			}
			if (rx != NULL) {
				while (!(__raw_readl(chstat_reg) & OMAP2_MCSPI_CHSTAT_RXS));
				if (c == 0 && tx == NULL)
					omap2_mcspi_set_enable(spi, 0);
				*rx++ = __raw_readl(rx_reg);
			}
		}
	} else if (spi->bits_per_word <= 32) {
		u32		*rx;
		const u32	*tx;

		rx = xfer->rx_buf;
		tx = xfer->tx_buf;
		c >>= 2;
		while (c--) {
			if (tx != NULL) {
				while (!(__raw_readl(chstat_reg) & OMAP2_MCSPI_CHSTAT_TXS));
				__raw_writel(*tx++, tx_reg);
			}
			if (rx != NULL) {
				while (!(__raw_readl(chstat_reg) & OMAP2_MCSPI_CHSTAT_RXS));
				if (c == 0 && tx == NULL)
					omap2_mcspi_set_enable(spi, 0);
				*rx++ = __raw_readl(rx_reg);
			}
		}
	}

	if (xfer->tx_buf != NULL) {
		while (!(__raw_readl(chstat_reg) & OMAP2_MCSPI_CHSTAT_TXS));
		while (!(__raw_readl(chstat_reg) & OMAP2_MCSPI_CHSTAT_EOT));
		omap2_mcspi_set_enable(spi, 0);
	}
}

static int omap2_mcspi_setup(struct spi_device *spi)
{
	struct omap2_mcspi_cs *cs = spi->controller_state;
	struct omap2_mcspi_device_config *conf;
	u32 l = 0, div = 0;
	u8 word_len = spi->bits_per_word;

	if (!word_len)
		return 0;

	if (!cs) {
		cs = kzalloc(sizeof *cs, SLAB_KERNEL);
		if (!cs)
			return -ENOMEM;
		spi->controller_state = cs;
	}

	if (spi->bits_per_word > 32)
		return -EINVAL;

	conf = (struct omap2_mcspi_device_config *) spi->controller_data;

	if (conf->single_channel == 1)
		omap2_mcspi_set_master_mode(spi, 1);
	else
		omap2_mcspi_set_master_mode(spi, 0);

	if (spi->max_speed_hz) {
		while (div <= 15 && (OMAP2_MCSPI_MAX_FREQ / (1 << div)) > spi->max_speed_hz)
			div++;
	} else
		div = 15;

	if (spi->chip_select > 3 ||
	    word_len < 4 || word_len > 32 ||
	    div > 15) {
		dev_err(&spi->dev, "Invalid McSPI channel setting\n");
		return -EINVAL;
	}

	l = mcspi_read_cs_reg(spi, OMAP2_MCSPI_CHCONF0);
	l &= ~OMAP2_MCSPI_CHCONF_IS;
	l &= ~OMAP2_MCSPI_CHCONF_DPE1;
	l |= OMAP2_MCSPI_CHCONF_DPE0;
	l &= ~OMAP2_MCSPI_CHCONF_WL_MASK;
	l |= (word_len - 1) << 7;
	if (!(spi->mode & SPI_CS_HIGH))
		l |= OMAP2_MCSPI_CHCONF_EPOL;
	l &= ~OMAP2_MCSPI_CHCONF_CLKD_MASK;
	l |= div << 2;
	if (spi->mode & SPI_CPOL)
		l |= OMAP2_MCSPI_CHCONF_POL;
	if (spi->mode & SPI_CPHA)
		l |= OMAP2_MCSPI_CHCONF_PHA;
	mcspi_write_cs_reg(spi, OMAP2_MCSPI_CHCONF0, l);

	return 0;
}

static void omap2_mcspi_cleanup(const struct spi_device *spi)
{
	if (spi->controller_state != NULL)
		kfree(spi->controller_state);
}


static void omap2_mcspi_work(unsigned long arg)
{
	struct omap2_mcspi	*mcspi = (struct omap2_mcspi *) arg;
	unsigned long		flags;

	spin_lock_irqsave(&mcspi->lock, flags);
	while (!list_empty(&mcspi->msg_queue)) {
		struct spi_message		*m;
		struct spi_device		*spi;
		struct spi_transfer		*t = NULL;
		int				cs_active;
		struct omap2_mcspi_device_config *conf;
		struct omap2_mcspi_cs		*cs;
		int status = 0;

		m = container_of(mcspi->msg_queue.next, struct spi_message,
				 queue);

		list_del_init(&m->queue);
		spin_unlock_irqrestore(&mcspi->lock, flags);

		spi = m->spi;
		conf = (struct omap2_mcspi_device_config *) spi->controller_data;
		cs = (struct omap2_mcspi_cs *) spi->controller_state;

		omap2_mcspi_force_cs(spi, 1);
		cs_active = 1;

		list_for_each_entry(t, &m->transfers, transfer_list) {
			if (t->tx_buf == NULL && t->rx_buf == NULL && t->len) {
				status = -EINVAL;
				break;
			}

			if (!cs_active) {
				omap2_mcspi_force_cs(spi, 1);
				cs_active = 1;
			}

			omap2_mcspi_txrx(spi, t);

			if (t->cs_change) {
				omap2_mcspi_force_cs(spi, 0);
				cs_active = 0;
			}
		}

		if (cs_active)
			omap2_mcspi_force_cs(spi, 0);

		m->status = status;
		m->complete(m->context);

		spin_lock_irqsave(&mcspi->lock, flags);
	}
	spin_unlock_irqrestore(&mcspi->lock, flags);
}

static int omap2_mcspi_transfer(struct spi_device *spi, struct spi_message *m)
{
	struct omap2_mcspi	*mcspi;
	unsigned long		flags;

	m->actual_length = 0;
	m->status = 0;

	mcspi = class_get_devdata(&spi->master->cdev);

	spin_lock_irqsave(&mcspi->lock, flags);
	list_add_tail(&m->queue, &mcspi->msg_queue);
	spin_unlock_irqrestore(&mcspi->lock, flags);

	tasklet_hi_schedule(&mcspi->tasklet);

	return 0;
}

static int __devinit omap2_mcspi_reset(struct spi_master *master)
{
#if 0
	mcspi_write_reg(master, OMAP2_MCSPI_SYSCONFIG,
			OMAP2_MCSPI_SYSCONFIG_SOFTRESET);
	while (!(mcspi_read_reg(master, OMAP2_MCSPI_SYSSTATUS) & OMAP2_MCSPI_SYSSTATUS_RESETDONE));
#else
	return 0;
#endif
}

static int __devinit omap2_mcspi_probe(struct platform_device *pdev)
{
	struct spi_master		*master;
	struct omap2_mcspi_platform_config *pdata = pdev->dev.platform_data;
	struct omap2_mcspi		*mcspi;
	int				status = 0;

	if (!pdata)
		return -EINVAL;

	master = spi_alloc_master(&pdev->dev, sizeof *mcspi);
	if (!master) {
		dev_err(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}

	if (pdev->id != -1)
		master->bus_num = pdev->id;

	master->setup = omap2_mcspi_setup;
	master->transfer = omap2_mcspi_transfer;
	master->cleanup = omap2_mcspi_cleanup;
	master->num_chipselect = pdata->num_cs;

	if (class_device_get(&master->cdev) == NULL) {
		dev_err(&pdev->dev, "no master->cdev");
		status = -ENOMEM;
		goto err0;
	}

	dev_set_drvdata(&pdev->dev, master);

	mcspi = class_get_devdata(&master->cdev);
	mcspi->master = master;

	tasklet_init(&mcspi->tasklet, omap2_mcspi_work, (unsigned long) mcspi);

	spin_lock_init(&mcspi->lock);
	INIT_LIST_HEAD(&mcspi->msg_queue);

	if (omap2_mcspi_reset(master) < 0)
                goto err1;

	status = spi_register_master(master);
	if (status < 0)
		goto err1;

	return status;

 err1:
	class_device_put(&master->cdev);
 err0:
	return status;
}

static int __devexit omap2_mcspi_remove(struct platform_device *pdev)
{
	struct spi_master		*master;

	master = dev_get_drvdata(&pdev->dev);

	spi_unregister_master(master);

	return 0;
}

struct platform_driver omap2_mcspi_driver = {
	.driver = {
		.name =		"omap2_mcspi",
		.owner =	THIS_MODULE,
	},
	.probe =	omap2_mcspi_probe,
	.remove =	__devexit_p(omap2_mcspi_remove),
};
EXPORT_SYMBOL_GPL(omap2_mcspi_driver);


static int __init omap2_mcspi_init(void)
{
	printk(KERN_INFO "OMAP24xx McSPI driver initializing\n");
	return platform_driver_register(&omap2_mcspi_driver);
}
subsys_initcall(omap2_mcspi_init);

static void __exit omap2_mcspi_exit(void)
{
	platform_driver_unregister(&omap2_mcspi_driver);
}
module_exit(omap2_mcspi_exit);

MODULE_LICENSE("GPL");