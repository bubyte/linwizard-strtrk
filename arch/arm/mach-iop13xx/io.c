/*
 * iop13xx custom ioremap implementation
 * Copyright (c) 2005-2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/hardware.h>
#include <asm/io.h>

void * __iomem __iop13xx_ioremap(unsigned long cookie, size_t size,
	unsigned long flags)
{
	void __iomem * retval;

	switch (cookie) {
	case IOP13XX_PCIX_LOWER_MEM_RA ... IOP13XX_PCIX_UPPER_MEM_RA:
		if (unlikely(!iop13xx_atux_mem_base))
			retval = NULL;
		else
			retval = (void *)(iop13xx_atux_mem_base +
			         (cookie - IOP13XX_PCIX_LOWER_MEM_RA));
		break;
	case IOP13XX_PCIE_LOWER_MEM_RA ... IOP13XX_PCIE_UPPER_MEM_RA:
		if (unlikely(!iop13xx_atue_mem_base))
			retval = NULL;
		else
			retval = (void *)(iop13xx_atue_mem_base +
			         (cookie - IOP13XX_PCIE_LOWER_MEM_RA));
		break;
	case IOP13XX_PBI_LOWER_MEM_RA ... IOP13XX_PBI_UPPER_MEM_RA:
		retval = __ioremap(IOP13XX_PBI_LOWER_MEM_PA +
				  (cookie - IOP13XX_PBI_LOWER_MEM_RA),
				  size, flags);
		break;
	case IOP13XX_PCIE_LOWER_IO_PA ... IOP13XX_PCIE_UPPER_IO_PA:
		retval = (void *) IOP13XX_PCIE_IO_PHYS_TO_VIRT(cookie);
		break;
	case IOP13XX_PCIX_LOWER_IO_PA ... IOP13XX_PCIX_UPPER_IO_PA:
		retval = (void *) IOP13XX_PCIX_IO_PHYS_TO_VIRT(cookie);
		break;
	case IOP13XX_PMMR_PHYS_MEM_BASE ... IOP13XX_PMMR_UPPER_MEM_PA:
		retval = (void *) IOP13XX_PMMR_PHYS_TO_VIRT(cookie);
		break;
	default:
		retval = __ioremap(cookie, size, flags);
	}

	return retval;
}
EXPORT_SYMBOL(__iop13xx_ioremap);

void __iop13xx_iounmap(void __iomem *addr)
{
	extern void __iounmap(volatile void __iomem *addr);

	if (iop13xx_atue_mem_base)
		if (addr >= (void __iomem *) iop13xx_atue_mem_base &&
	 	    addr < (void __iomem *) (iop13xx_atue_mem_base +
	 	    			     iop13xx_atue_mem_size))
		    goto skip;

	if (iop13xx_atux_mem_base)
		if (addr >= (void __iomem *) iop13xx_atux_mem_base &&
	 	    addr < (void __iomem *) (iop13xx_atux_mem_base +
	 	    			     iop13xx_atux_mem_size))
		    goto skip;

	switch ((u32) addr) {
	case IOP13XX_PCIE_LOWER_IO_VA ... IOP13XX_PCIE_UPPER_IO_VA:
	case IOP13XX_PCIX_LOWER_IO_VA ... IOP13XX_PCIX_UPPER_IO_VA:
	case IOP13XX_PMMR_VIRT_MEM_BASE ... IOP13XX_PMMR_UPPER_MEM_VA:
		goto skip;
	}
	__iounmap(addr);

skip:
	return;
}
EXPORT_SYMBOL(__iop13xx_iounmap);