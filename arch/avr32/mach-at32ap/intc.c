/*
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>

#include <asm/io.h>

#include "intc.h"

struct intc {
	void __iomem	*regs;
	struct irq_chip	chip;
};

extern struct platform_device at32_intc0_device;

/*
 * TODO: We may be able to implement mask/unmask by setting IxM flags
 * in the status register.
 */
static void intc_mask_irq(unsigned int irq)
{

}

static void intc_unmask_irq(unsigned int irq)
{

}

static struct intc intc0 = {
	.chip = {
		.name		= "intc",
		.mask		= intc_mask_irq,
		.unmask		= intc_unmask_irq,
	},
};

/*
 * All interrupts go via intc at some point.
 */
asmlinkage void do_IRQ(int level, struct pt_regs *regs)
{
	struct irq_desc *desc;
	struct pt_regs *old_regs;
	unsigned int irq;
	unsigned long status_reg;

	local_irq_disable();

	old_regs = set_irq_regs(regs);

	irq_enter();

	irq = intc_readl(&intc0, INTCAUSE0 - 4 * level);
	desc = irq_desc + irq;
	desc->handle_irq(irq, desc);

	/*
	 * Clear all interrupt level masks so that we may handle
	 * interrupts during softirq processing.  If this is a nested
	 * interrupt, interrupts must stay globally disabled until we
	 * return.
	 */
	status_reg = sysreg_read(SR);
	status_reg &= ~(SYSREG_BIT(I0M) | SYSREG_BIT(I1M)
			| SYSREG_BIT(I2M) | SYSREG_BIT(I3M));
	sysreg_write(SR, status_reg);

	irq_exit();

	set_irq_regs(old_regs);
}

void __init init_IRQ(void)
{
	extern void _evba(void);
	extern void irq_level0(void);
	struct resource *regs;
	struct clk *pclk;
	unsigned int i;
	u32 offset, readback;

	regs = platform_get_resource(&at32_intc0_device, IORESOURCE_MEM, 0);
	if (!regs) {
		printk(KERN_EMERG "intc: no mmio resource defined\n");
		goto fail;
	}
	pclk = clk_get(&at32_intc0_device.dev, "pclk");
	if (IS_ERR(pclk)) {
		printk(KERN_EMERG "intc: no clock defined\n");
		goto fail;
	}

	clk_enable(pclk);

	intc0.regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!intc0.regs) {
		printk(KERN_EMERG "intc: failed to map registers (0x%08lx)\n",
		       (unsigned long)regs->start);
		goto fail;
	}

	/*
	 * Initialize all interrupts to level 0 (lowest priority). The
	 * priority level may be changed by calling
	 * irq_set_priority().
	 *
	 */
	offset = (unsigned long)&irq_level0 - (unsigned long)&_evba;
	for (i = 0; i < NR_INTERNAL_IRQS; i++) {
		intc_writel(&intc0, INTPR0 + 4 * i, offset);
		readback = intc_readl(&intc0, INTPR0 + 4 * i);
		if (readback == offset)
			set_irq_chip_and_handler(i, &intc0.chip,
						 handle_simple_irq);
	}

	/* Unmask all interrupt levels */
	sysreg_write(SR, (sysreg_read(SR)
			  & ~(SR_I3M | SR_I2M | SR_I1M | SR_I0M)));

	return;

fail:
	panic("Interrupt controller initialization failed!\n");
}
