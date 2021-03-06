/*
 * This file is part of OMAP DSP driver (DSP Gateway version 3.3.1)
 *
 * Copyright (C) 2002-2006 Nokia Corporation. All rights reserved.
 *
 * Contact: Toshihiro Kobayashi <toshihiro.kobayashi@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <asm/delay.h>
#include <asm/arch/mailbox.h>
#include <asm/arch/dsp.h>
#include <asm/arch/dsp_common.h>
#include "dsp_mbcmd.h"
#include "dsp.h"
#include "ipbuf.h"

MODULE_AUTHOR("Toshihiro Kobayashi <toshihiro.kobayashi@nokia.com>");
MODULE_DESCRIPTION("OMAP DSP driver module");
MODULE_LICENSE("GPL");

static struct sync_seq *mbseq;
static u16 mbseq_expect_tmp;
static u16 *mbseq_expect = &mbseq_expect_tmp;

extern int dsp_mem_late_init(void);

/*
 * mailbox commands
 */
extern void mbox_wdsnd(struct mbcmd *mb);
extern void mbox_wdreq(struct mbcmd *mb);
extern void mbox_bksnd(struct mbcmd *mb);
extern void mbox_bkreq(struct mbcmd *mb);
extern void mbox_bkyld(struct mbcmd *mb);
extern void mbox_bksndp(struct mbcmd *mb);
extern void mbox_bkreqp(struct mbcmd *mb);
extern void mbox_tctl(struct mbcmd *mb);
extern void mbox_poll(struct mbcmd *mb);
#ifdef OLD_BINARY_SUPPORT
/* v3.3 obsolete */
extern void mbox_wdt(struct mbcmd *mb);
#endif
extern void mbox_suspend(struct mbcmd *mb);
static void mbox_kfunc(struct mbcmd *mb);
extern void mbox_tcfg(struct mbcmd *mb);
extern void mbox_tadd(struct mbcmd *mb);
extern void mbox_tdel(struct mbcmd *mb);
extern void mbox_dspcfg(struct mbcmd *mb);
extern void mbox_regrw(struct mbcmd *mb);
extern void mbox_getvar(struct mbcmd *mb);
extern void mbox_err(struct mbcmd *mb);
extern void mbox_dbg(struct mbcmd *mb);

static const struct cmdinfo
	cif_wdsnd    = { "WDSND",    CMD_L_TYPE_TID,    mbox_wdsnd   },
	cif_wdreq    = { "WDREQ",    CMD_L_TYPE_TID,    mbox_wdreq   },
	cif_bksnd    = { "BKSND",    CMD_L_TYPE_TID,    mbox_bksnd   },
	cif_bkreq    = { "BKREQ",    CMD_L_TYPE_TID,    mbox_bkreq   },
	cif_bkyld    = { "BKYLD",    CMD_L_TYPE_NULL,   mbox_bkyld   },
	cif_bksndp   = { "BKSNDP",   CMD_L_TYPE_TID,    mbox_bksndp  },
	cif_bkreqp   = { "BKREQP",   CMD_L_TYPE_TID,    mbox_bkreqp  },
	cif_tctl     = { "TCTL",     CMD_L_TYPE_TID,    mbox_tctl    },
	cif_poll     = { "POLL",     CMD_L_TYPE_NULL,   mbox_poll    },
#ifdef OLD_BINARY_SUPPORT
	/* v3.3 obsolete */
	cif_wdt      = { "WDT",      CMD_L_TYPE_NULL,   mbox_wdt     },
#endif
	cif_runlevel = { "RUNLEVEL", CMD_L_TYPE_SUBCMD, NULL        },
	cif_pm       = { "PM",       CMD_L_TYPE_SUBCMD, NULL        },
	cif_suspend  = { "SUSPEND",  CMD_L_TYPE_NULL,   mbox_suspend },
	cif_kfunc    = { "KFUNC",    CMD_L_TYPE_SUBCMD, mbox_kfunc   },
	cif_tcfg     = { "TCFG",     CMD_L_TYPE_TID,    mbox_tcfg    },
	cif_tadd     = { "TADD",     CMD_L_TYPE_TID,    mbox_tadd    },
	cif_tdel     = { "TDEL",     CMD_L_TYPE_TID,    mbox_tdel    },
	cif_tstop    = { "TSTOP",    CMD_L_TYPE_TID,    NULL        },
	cif_dspcfg   = { "DSPCFG",   CMD_L_TYPE_SUBCMD, mbox_dspcfg  },
	cif_regrw    = { "REGRW",    CMD_L_TYPE_SUBCMD, mbox_regrw   },
	cif_getvar   = { "GETVAR",   CMD_L_TYPE_SUBCMD, mbox_getvar  },
	cif_setvar   = { "SETVAR",   CMD_L_TYPE_SUBCMD, NULL        },
	cif_err      = { "ERR",      CMD_L_TYPE_SUBCMD, mbox_err     },
	cif_dbg      = { "DBG",      CMD_L_TYPE_NULL,   mbox_dbg     };

#define MBOX_CMD_MAX	0x80
const struct cmdinfo *cmdinfo[MBOX_CMD_MAX] = {
	[MBOX_CMD_DSP_WDSND]    = &cif_wdsnd,
	[MBOX_CMD_DSP_WDREQ]    = &cif_wdreq,
	[MBOX_CMD_DSP_BKSND]    = &cif_bksnd,
	[MBOX_CMD_DSP_BKREQ]    = &cif_bkreq,
	[MBOX_CMD_DSP_BKYLD]    = &cif_bkyld,
	[MBOX_CMD_DSP_BKSNDP]   = &cif_bksndp,
	[MBOX_CMD_DSP_BKREQP]   = &cif_bkreqp,
	[MBOX_CMD_DSP_TCTL]     = &cif_tctl,
	[MBOX_CMD_DSP_POLL]     = &cif_poll,
#ifdef OLD_BINARY_SUPPORT
	[MBOX_CMD_DSP_WDT]      = &cif_wdt, /* v3.3 obsolete */
#endif
	[MBOX_CMD_DSP_RUNLEVEL] = &cif_runlevel,
	[MBOX_CMD_DSP_PM]       = &cif_pm,
	[MBOX_CMD_DSP_SUSPEND]  = &cif_suspend,
	[MBOX_CMD_DSP_KFUNC]    = &cif_kfunc,
	[MBOX_CMD_DSP_TCFG]     = &cif_tcfg,
	[MBOX_CMD_DSP_TADD]     = &cif_tadd,
	[MBOX_CMD_DSP_TDEL]     = &cif_tdel,
	[MBOX_CMD_DSP_TSTOP]    = &cif_tstop,
	[MBOX_CMD_DSP_DSPCFG]   = &cif_dspcfg,
	[MBOX_CMD_DSP_REGRW]    = &cif_regrw,
	[MBOX_CMD_DSP_GETVAR]   = &cif_getvar,
	[MBOX_CMD_DSP_SETVAR]   = &cif_setvar,
	[MBOX_CMD_DSP_ERR]      = &cif_err,
	[MBOX_CMD_DSP_DBG]      = &cif_dbg,
};

#define list_for_each_entry_safe_natural(p,n,h,m) \
			list_for_each_entry_safe(p,n,h,m)
#define __BUILD_KFUNC(fn, dir)							\
static int __dsp_kfunc_##fn##_devices(struct omap_dsp *dsp, int type, int stage)\
{										\
	struct dsp_kfunc_device *p, *tmp;					\
	int ret, fail = 0;							\
										\
	list_for_each_entry_safe_##dir(p, tmp, dsp->kdev_list, entry) {		\
		if (type && (p->type != type))					\
			continue;						\
		if (p->fn == NULL)						\
			continue;						\
		ret = p->fn(p, stage);						\
		if (ret) {							\
			printk(KERN_ERR "%s %s failed\n", #fn, p->name);	\
			fail++;							\
		}								\
	}									\
	return fail;								\
}
#define BUILD_KFUNC(fn, dir)						\
__BUILD_KFUNC(fn, dir)							\
static inline int dsp_kfunc_##fn##_devices(struct omap_dsp *dsp)	\
{									\
	return __dsp_kfunc_##fn##_devices(dsp, 0, 0);			\
}
#define BUILD_KFUNC_CTL(fn, dir)							\
__BUILD_KFUNC(fn, dir)									\
static inline int dsp_kfunc_##fn##_devices(struct omap_dsp *dsp, int type, int stage)	\
{											\
	return __dsp_kfunc_##fn##_devices(dsp, type, stage);				\
}

BUILD_KFUNC(probe, natural)
BUILD_KFUNC(remove, reverse)
BUILD_KFUNC_CTL(enable, natural)
BUILD_KFUNC_CTL(disable, reverse)

int sync_with_dsp(u16 *adr, u16 val, int try_cnt)
{
	int try;

	if (*(volatile u16 *)adr == val)
		return 0;

	for (try = 0; try < try_cnt; try++) {
		udelay(1);
		if (*(volatile u16 *)adr == val) {
			/* success! */
			pr_info("omapdsp: sync_with_dsp(): try = %d\n", try);
			return 0;
		}
	}

	/* fail! */
	return -1;
}

static int mbcmd_sender_prepare(void *data)
{
	struct mb_exarg *arg = data;
	int i, ret = 0;
	/*
	 * even if ipbuf_sys_ad is in DSP internal memory,
	 * dsp_mem_enable() never cause to call PM mailbox command
	 * because in that case DSP memory should be always enabled.
	 * (see ipbuf_sys_hold_mem_active in ipbuf.c)
	 *
	 * Therefore, we can call this function here safely.
	 */
	dsp_mem_enable(ipbuf_sys_ad);
	if (sync_with_dsp(&ipbuf_sys_ad->s, TID_FREE, 10) < 0) {
		printk(KERN_ERR "omapdsp: ipbuf_sys_ad is busy.\n");
		ret = -EBUSY;
		goto out;
	}

	for (i = 0; i < arg->argc; i++) {
		ipbuf_sys_ad->d[i] = arg->argv[i];
	}
	ipbuf_sys_ad->s = arg->tid;
 out:
	dsp_mem_disable(ipbuf_sys_ad);
	return ret;
}

/*
 * __dsp_mbcmd_send_exarg(): mailbox dispatcher
 */
int __dsp_mbcmd_send_exarg(struct mbcmd *mb, struct mb_exarg *arg,
			   int recovery_flag)
{
	int ret = 0;

	if (unlikely(omap_dsp->enabled == 0)) {
		ret = dsp_kfunc_enable_devices(omap_dsp,
					       DSP_KFUNC_DEV_TYPE_COMMON, 0);
		if (ret == 0)
			omap_dsp->enabled = 1;
	}

	/*
	 * while MMU fault is set,
	 * only recovery command can be executed
	 */
	if (dsp_err_isset(ERRCODE_MMU) && !recovery_flag) {
		printk(KERN_ERR
		       "mbox: mmu interrupt is set. %s is aborting.\n",
		       cmd_name(*mb));
		goto out;
	}

	ret = omap_mbox_msg_send(omap_dsp->mbox,
				 *(mbox_msg_t *)mb, (void*)arg);
	if (ret)
		goto out;

	if (mbseq)
		mbseq->ad_arm++;

	mblog_add(mb, DIR_A2D);
 out:
	return ret;
}

int dsp_mbcmd_send_and_wait_exarg(struct mbcmd *mb, struct mb_exarg *arg,
				  wait_queue_head_t *q)
{
	int ret;

	DEFINE_WAIT(wait);
	prepare_to_wait(q, &wait, TASK_INTERRUPTIBLE);
	ret = dsp_mbcmd_send_exarg(mb, arg);
	if (ret < 0)
		goto out;
	schedule_timeout(DSP_TIMEOUT);
 out:
	finish_wait(q, &wait);
	return ret;
}

/*
 * mbcmd receiver
 */
static int mbcmd_receiver(void* msg)
{
	struct mbcmd *mb = (struct mbcmd *)&msg;

	if (cmdinfo[mb->cmd_h] == NULL) {
		printk(KERN_ERR
		       "invalid message (%08x) for mbcmd_receiver().\n",
		       (mbox_msg_t)msg);
		return -1;
	}

	(*mbseq_expect)++;

	mblog_add(mb, DIR_D2A);

	/* call handler for the command */
	if (cmdinfo[mb->cmd_h]->handler)
		cmdinfo[mb->cmd_h]->handler(mb);
	else
		printk(KERN_ERR "mbox: %s is not allowed from DSP.\n",
		       cmd_name(*mb));
	return 0;
}

static int mbsync_hold_mem_active;

void dsp_mbox_start(void)
{
	omap_mbox_init_seq(omap_dsp->mbox);
	mbseq_expect_tmp = 0;
}

void dsp_mbox_stop(void)
{
	mbseq = NULL;
	mbseq_expect = &mbseq_expect_tmp;
}

int dsp_mbox_config(void *p)
{
	unsigned long flags;

	if (dsp_address_validate(p, sizeof(struct sync_seq), "mbseq") < 0)
		return -1;
	if (dsp_mem_type(p, sizeof(struct sync_seq)) != MEM_TYPE_EXTERN) {
		printk(KERN_WARNING
		       "omapdsp: mbseq is placed in DSP internal memory.\n"
		       "         It will prevent DSP from idling.\n");
		mbsync_hold_mem_active = 1;
		/*
		 * dsp_mem_enable() never fails because
		 * it has been already enabled in dspcfg process and
		 * this will just increment the usecount.
		 */
		dsp_mem_enable((void *)daram_base);
	}

	local_irq_save(flags);
	mbseq = p;
	mbseq->da_arm = mbseq_expect_tmp;
	mbseq_expect = &mbseq->da_arm;
	local_irq_restore(flags);

	return 0;
}

static int __init dsp_mbox_init(void)
{
	omap_dsp->mbox = omap_mbox_get("dsp");
	if (IS_ERR(omap_dsp->mbox)) {
		printk(KERN_ERR "failed to get mailbox handler for DSP.\n");
		return -ENODEV;
	}

	omap_dsp->mbox->rxq->callback = mbcmd_receiver;
	omap_dsp->mbox->txq->callback = mbcmd_sender_prepare;

	return 0;
}

static void dsp_mbox_exit(void)
{
	omap_dsp->mbox->txq->callback = NULL;
	omap_dsp->mbox->rxq->callback = NULL;

	omap_mbox_put(omap_dsp->mbox);

	if (mbsync_hold_mem_active) {
		dsp_mem_disable((void *)daram_base);
		mbsync_hold_mem_active = 0;
	}
}

/*
 * kernel function dispatcher
 */
extern void mbox_fbctl_upd(void);
extern void mbox_fbctl_disable(struct mbcmd *mb);

static void mbox_kfunc_fbctl(struct mbcmd *mb)
{
	switch (mb->data) {
	case FBCTL_UPD:
		mbox_fbctl_upd();
		break;
	case FBCTL_DISABLE:
		mbox_fbctl_disable(mb);
		break;
	default:
		printk(KERN_ERR
		       "mbox: Unknown FBCTL from DSP: 0x%04x\n", mb->data);
	}
}

/*
 * dspgw: KFUNC message handler
 */
static void mbox_kfunc_power(unsigned short data)
{
	int ret = -1;

	switch (data) {
	case DVFS_START: /* ACK from DSP */
		/* TBD */
		break;
	case AUDIO_PWR_UP:
		ret = dsp_kfunc_enable_devices(omap_dsp,
					       DSP_KFUNC_DEV_TYPE_AUDIO, 0);
		if (ret == 0)
			ret++;
		break;
	case AUDIO_PWR_DOWN: /* == AUDIO_PWR_DOWN1 */
		ret = dsp_kfunc_disable_devices(omap_dsp,
						DSP_KFUNC_DEV_TYPE_AUDIO, 1);
		break;
	case AUDIO_PWR_DOWN2:
		ret = dsp_kfunc_disable_devices(omap_dsp,
						DSP_KFUNC_DEV_TYPE_AUDIO, 2);
		break;
	case DSP_PWR_DOWN:
		ret = dsp_kfunc_disable_devices(omap_dsp,
						DSP_KFUNC_DEV_TYPE_COMMON, 0);
		if (ret == 0)
			omap_dsp->enabled = 0;
		break;
	default:
		printk(KERN_ERR
		       "mailbox: Unknown PWR from DSP: 0x%04x\n", data);
		break;
	}

	if (unlikely(ret < 0)) {
		printk(KERN_ERR "mailbox: PWR(0x%04x) failed\n", data);
		return;
	}

	if (likely(ret == 0))
		return;

	mbcompose_send(KFUNC, KFUNC_POWER, data);
}

static void mbox_kfunc(struct mbcmd *mb)
{
	switch (mb->cmd_l) {
	case KFUNC_FBCTL:
		mbox_kfunc_fbctl(mb);
		break;
	case KFUNC_POWER:
		mbox_kfunc_power(mb->data);
		break;
	default:
		printk(KERN_ERR
		       "mbox: Unknown KFUNC from DSP: 0x%02x\n", mb->cmd_l);
	}
}

#if defined(CONFIG_ARCH_OMAP1)
static inline void dsp_clk_enable(void) {}
static inline void dsp_clk_disable(void) {}
#elif defined(CONFIG_ARCH_OMAP2)
static inline void dsp_clk_enable(void)
{
	/*XXX should be handled in mach-omap[1,2] XXX*/
	prm_write_mod_reg(OMAP24XX_FORCESTATE | (1 << OMAP_POWERSTATE_SHIFT),
			  OMAP24XX_DSP_MOD, PM_PWSTCTRL);

	cm_set_mod_reg_bits(OMAP2420_AUTO_DSP_IPI, OMAP24XX_DSP_MOD,
			    CM_AUTOIDLE);

	cm_set_mod_reg_bits(OMAP24XX_AUTOSTATE_DSP, OMAP24XX_DSP_MOD,
			    CM_CLKSTCTRL);

	clk_enable(dsp_fck_handle);
	clk_enable(dsp_ick_handle);
	__dsp_per_enable();
}
static inline void dsp_clk_disable(void)
{
	__dsp_per_disable();
	clk_disable(dsp_ick_handle);
	clk_disable(dsp_fck_handle);

	prm_write_mod_reg(OMAP24XX_FORCESTATE | (3 << OMAP_POWERSTATE_SHIFT),
			  OMAP24XX_DSP_MOD, PM_PWSTCTRL);
}
#endif

int dsp_late_init(void)
{
	int ret;

	dsp_clk_enable();
	ret = dsp_mem_late_init();
	if (ret)
		return ret;
	ret = dsp_mbox_init();
	if (ret)
		goto fail_mbox;
#ifdef CONFIG_ARCH_OMAP1
	dsp_set_idle_boot_base(IDLEPG_BASE, IDLEPG_SIZE);
#endif
	ret = dsp_kfunc_enable_devices(omap_dsp,
				       DSP_KFUNC_DEV_TYPE_COMMON, 0);
	if (ret)
		goto fail_kfunc;
	omap_dsp->enabled = 1;

	return 0;

 fail_kfunc:
	dsp_mbox_exit();
 fail_mbox:
	dsp_clk_disable();

	return ret;
}

extern int  dsp_ctl_core_init(void);
extern void dsp_ctl_core_exit(void);
extern int dsp_ctl_init(void);
extern void dsp_ctl_exit(void);
extern int  dsp_mem_init(void);
extern void dsp_mem_exit(void);
extern void mblog_init(void);
extern void mblog_exit(void);
extern int  dsp_taskmod_init(void);
extern void dsp_taskmod_exit(void);

/*
 * driver functions
 */
static int __init dsp_drv_probe(struct platform_device *pdev)
{
	int ret;
	struct omap_dsp *info;
	struct dsp_platform_data *pdata = pdev->dev.platform_data;

	dev_info(&pdev->dev, "OMAP DSP driver initialization\n");

	info = kzalloc(sizeof(struct omap_dsp), GFP_KERNEL);
	if (unlikely(info == NULL)) {
		dev_dbg(&pdev->dev, "no memory for info\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, info);
	omap_dsp = info;

	mutex_init(&info->lock);
	info->dev = &pdev->dev;
	info->kdev_list = &pdata->kdev_list;

	ret = dsp_kfunc_probe_devices(info);
	if (ret) {
		ret = -ENXIO;
		goto fail_kfunc;
	}

	ret = dsp_ctl_core_init();
	if (ret)
		goto fail_ctl_core;
	ret = dsp_mem_init();
	if (ret)
		goto fail_mem;
	ret = dsp_ctl_init();
	if (unlikely(ret))
		goto fail_ctl_init;
	mblog_init();
	ret = dsp_taskmod_init();
	if (ret)
		goto fail_taskmod;

	return 0;

 fail_taskmod:
	mblog_exit();
	dsp_ctl_exit();
 fail_ctl_init:
	dsp_mem_exit();
 fail_mem:
	dsp_ctl_core_exit();
 fail_ctl_core:
	dsp_kfunc_remove_devices(info);
 fail_kfunc:
	kfree(info);

	return ret;
}

static int dsp_drv_remove(struct platform_device *pdev)
{
	struct omap_dsp *info = platform_get_drvdata(pdev);

	dsp_cpustat_request(CPUSTAT_RESET);

	dsp_cfgstat_request(CFGSTAT_CLEAN);
	dsp_mbox_exit();
	dsp_taskmod_exit();
	mblog_exit();
	dsp_ctl_exit();
	dsp_mem_exit();

	dsp_ctl_core_exit();

#ifdef CONFIG_ARCH_OMAP2
	__dsp_per_disable();
	clk_disable(dsp_ick_handle);
	clk_disable(dsp_fck_handle);
#endif
	dsp_kfunc_remove_devices(info);
	kfree(info);

	return 0;
}

#if defined(CONFIG_PM) && defined(CONFIG_ARCH_OMAP1)
static int dsp_drv_suspend(struct platform_device *pdev, pm_message_t state)
{
	dsp_cfgstat_request(CFGSTAT_SUSPEND);

	return 0;
}

static int dsp_drv_resume(struct platform_device *pdev)
{
	dsp_cfgstat_request(CFGSTAT_RESUME);

	return 0;
}
#else
#define dsp_drv_suspend		NULL
#define dsp_drv_resume		NULL
#endif /* CONFIG_PM */

static struct platform_driver dsp_driver = {
	.probe		= dsp_drv_probe,
	.remove		= dsp_drv_remove,
	.suspend	= dsp_drv_suspend,
	.resume		= dsp_drv_resume,
	.driver		= {
		.name	= "dsp",
	},
};

static int __init omap_dsp_mod_init(void)
{
	return platform_driver_register(&dsp_driver);
}

static void __exit omap_dsp_mod_exit(void)
{
	platform_driver_unregister(&dsp_driver);
}

/* module dependency: need mailbox module that have mbox_dsp_info */
extern struct omap_mbox mbox_dsp_info;
struct omap_mbox *mbox_dep = &mbox_dsp_info;

module_init(omap_dsp_mod_init);
module_exit(omap_dsp_mod_exit);
