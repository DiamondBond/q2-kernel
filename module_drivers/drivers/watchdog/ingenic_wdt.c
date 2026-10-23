/*
 *  Copyright (C) 2017, bo.liu <bo.liu@ingenic.com>
 *  ingenic Watchdog driver
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include "ingenic_wdt.h"

//#define WDT_DEBUG

static  void wdt_set_half_and_full(struct ingenic_wdt_drvdata *drvdata, unsigned int value,char ch);

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static unsigned int heartbeat = DEFAULT_HEARTBEAT;
module_param(heartbeat, uint, 0);
MODULE_PARM_DESC(heartbeat,
		"Watchdog heartbeat period in seconds from 1 to "
		__MODULE_STRING(MAX_HEARTBEAT) ", default "
		__MODULE_STRING(DEFAULT_HEARTBEAT));

static bool wdt_half_irq;
module_param(wdt_half_irq, bool, 0644);
MODULE_PARM_DESC(wdt_half_irq,
		"Enable one-shot watchdog half interrupt on next ping");

static unsigned int wdt_half_irq_count;
module_param(wdt_half_irq_count, uint, 0644);
MODULE_PARM_DESC(wdt_half_irq_count,
		"Watchdog half interrupt count");

#if 0
static int ingenic_restart_handler(struct notifier_block *this, unsigned long mode,
		void *cmd)
{
	struct ingenic_wdt_drvdata *drvdata = container_of(this,struct ingenic_wdt_drvdata, restart_handler);
	writew(INGENIC_WDT_CLOCK_RTC | INGENIC_WDT_CLOCK_CLRZ ,drvdata->base + INGENIC_REG_WDT_TIMER_CONTROL);
	wdt_set_half_and_full(drvdata,0xa0,'F');
	writeb(0x1, drvdata->base + INGENIC_REG_WDT_COUNTER_ENABLE);

	return NOTIFY_DONE;
}
#endif

#ifdef WDT_DEBUG
static void ingenic_wdt_pr_debug(struct ingenic_wdt_drvdata *drvdata)
{
	printk("\n*********************************************\n");
	printk("half-full addr %08x value is %08x\n",(unsigned int)(drvdata->base + INGENIC_REG_WDT_TIMER_DATA),
			readl(drvdata->base +INGENIC_REG_WDT_TIMER_DATA));
	printk("enable    addr %x value is %08x\n",(unsigned int)(drvdata->base + INGENIC_REG_WDT_COUNTER_ENABLE),
			readl(drvdata->base +INGENIC_REG_WDT_COUNTER_ENABLE));
	printk("counter   addr %x value is %08x\n",(unsigned int)(drvdata->base + INGENIC_REG_WDT_TIMER_COUNTER),
			readl(drvdata->base +INGENIC_REG_WDT_TIMER_COUNTER));
	printk("control   addr %x value is %08x\n",(unsigned int)(drvdata->base + INGENIC_REG_WDT_TIMER_CONTROL),
			readl(drvdata->base +INGENIC_REG_WDT_TIMER_CONTROL));
	printk("flag      addr %x value is %08x\n",(unsigned int)(drvdata->base + INGENIC_REG_WDT_TIMER_FLAG_RD),
			readl(drvdata->base +INGENIC_REG_WDT_TIMER_FLAG_RD));
	printk("mask      addr %x value is %08x\n",(unsigned int)(drvdata->base + INGENIC_REG_WDT_TIMER_MASK_RD),
			readl(drvdata->base +INGENIC_REG_WDT_TIMER_MASK_RD));
	printk("stop      addr %x value is %08x\n",(unsigned int)(drvdata->base + INGENIC_REG_WDT_TIMER_STOP_RD),
			readl(drvdata->base +INGENIC_REG_WDT_TIMER_STOP_RD));
	printk("\n*********************************************\n");
}
#endif

static irqreturn_t ingenic_wdt_interrupt(int irq, void *dev_id)
{
	struct ingenic_wdt_drvdata *drvdata = dev_id;
	unsigned long flags;
	unsigned int irq_count;

	spin_lock_irqsave(&drvdata->lock, flags);
	if(readl(drvdata->base + INGENIC_REG_WDT_TIMER_FLAG_RD) & INGENIC_WDT_TIMER_FLAG){
		writel(INGENIC_WDT_TIMER_MASK,
				drvdata->base + INGENIC_REG_WDT_TIMER_MASK_SET);
		writel(INGENIC_WDT_TIMER_FLAG,
				drvdata->base + INGENIC_REG_WDT_TIMER_FLAG_CLR);
		writel(readl(drvdata->base + INGENIC_REG_WDT_TIMER_CONTROL) |
				INGENIC_WDT_CLOCK_CLRZ,
				drvdata->base + INGENIC_REG_WDT_TIMER_CONTROL);
		wdt_half_irq_count++;
		wdt_half_irq = false;
		irq_count = wdt_half_irq_count;
		spin_unlock_irqrestore(&drvdata->lock, flags);
		pr_info("ingenic watchdog half irq triggered, count=%u\n",
				irq_count);
		return IRQ_HANDLED;
	}
	spin_unlock_irqrestore(&drvdata->lock, flags);

	return IRQ_NONE;
}

static int ingenic_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct ingenic_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);
	unsigned long flags;
	bool half_irq = wdt_half_irq;

	spin_lock_irqsave(&drvdata->lock, flags);
	if (half_irq) {
		writel(INGENIC_WDT_TIMER_FLAG,
				drvdata->base + INGENIC_REG_WDT_TIMER_FLAG_CLR);
		writel(INGENIC_WDT_TIMER_MASK,
				drvdata->base + INGENIC_REG_WDT_TIMER_MASK_CLR);
	} else {
		writel(INGENIC_WDT_TIMER_MASK,
				drvdata->base + INGENIC_REG_WDT_TIMER_MASK_SET);
	}
	writel(readl(drvdata->base + INGENIC_REG_WDT_TIMER_CONTROL) |
			INGENIC_WDT_CLOCK_CLRZ,
			drvdata->base + INGENIC_REG_WDT_TIMER_CONTROL);
	spin_unlock_irqrestore(&drvdata->lock, flags);

	return 0;
}

static  void wdt_set_half_and_full(struct ingenic_wdt_drvdata *drvdata, unsigned int value,char ch)
{
	int tmp ;
	if (value > INGENIC_WDT_FULL_MAX)
		value = INGENIC_WDT_FULL_MAX;
	tmp =readl(drvdata->base + INGENIC_REG_WDT_TIMER_DATA);
	if('H' == ch){
		tmp &= ~(INGENIC_WDT_FULL_MAX << INGENIC_WDT_OFFSET_16);
		tmp |= value << INGENIC_WDT_OFFSET_16;
	}
	if('F' == ch){
		tmp &= ~(INGENIC_WDT_FULL_MAX);
		tmp |= value;
	}
	writel(tmp,drvdata->base + INGENIC_REG_WDT_TIMER_DATA);
}

static int ingenic_wdt_set_timeout(struct watchdog_device *wdt_dev,
		unsigned int new_timeout)
{
	struct ingenic_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);
	unsigned int rtc_clk_rate;
	unsigned int timeout_value;
	short clock_div = INGENIC_WDT_CLOCK_DIV_1;

	if(drvdata->timeout != new_timeout){

		writeb(0x0, drvdata->base + INGENIC_REG_WDT_COUNTER_ENABLE);
		rtc_clk_rate = clk_get_rate(drvdata->wdt_clk);

		/*new_timeout max = 2047s */
		clock_div = INGENIC_WDT_CLOCK_DIV_1024;
		timeout_value = rtc_clk_rate * new_timeout / 1024;

		if(timeout_value > INGENIC_WDT_FULL_MAX){
			timeout_value = INGENIC_WDT_FULL_MAX;
		}

		writel(INGENIC_WDT_TIMER_FLAG,drvdata->base + INGENIC_REG_WDT_TIMER_FLAG_CLR);
		writel(INGENIC_WDT_TIMER_STOP,drvdata->base + INGENIC_REG_WDT_TIMER_STOP_CLR);

		/*Reach this value set to reboot*/
		wdt_set_half_and_full(drvdata,timeout_value,'F');

		/*When the set value is reached, the flag bit will be set to 1 and an interrupt will be triggered.*/
		wdt_set_half_and_full(drvdata,timeout_value * 2/3,'H');

		/*set prescal clzr rtc_en*/
		writew(clock_div | INGENIC_WDT_CLOCK_RTC | INGENIC_WDT_CLOCK_CLRZ ,drvdata->base + INGENIC_REG_WDT_TIMER_CONTROL);
		writeb(0x1, drvdata->base + INGENIC_REG_WDT_COUNTER_ENABLE);

		drvdata->timeout = wdt_dev->timeout = new_timeout;
	}
	return 0;
}

static int ingenic_wdt_start(struct watchdog_device *wdt_dev)
{
	struct ingenic_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);
	writel(INGENIC_WDT_TIMER_FLAG,drvdata->base + INGENIC_REG_WDT_TIMER_FLAG_CLR);
	writeb(0x1, drvdata->base + INGENIC_REG_WDT_COUNTER_ENABLE);
	//ingenic_wdt_set_timeout(wdt_dev, wdt_dev->timeout);

	return 0;
}

static int ingenic_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct ingenic_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);
	writeb(0x0, drvdata->base + INGENIC_REG_WDT_COUNTER_ENABLE);
	return 0;
}

static const struct watchdog_info ingenic_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "ingenic Watchdog",
};

static const struct watchdog_ops ingenic_wdt_ops = {
	.owner = THIS_MODULE,
	.start = ingenic_wdt_start,
	.stop = ingenic_wdt_stop,
	.ping = ingenic_wdt_ping,
	.set_timeout = ingenic_wdt_set_timeout,
};


static const struct of_device_id ingenic_wdt_of_matches[] = {
	{ .compatible = "ingenic,watchdog", .data = NULL },
	{},
};
MODULE_DEVICE_TABLE(of, ingenic_wdt_of_matches)

#ifdef CONFIG_PM_SLEEP 
static int ingenic_wdt_suspend(struct device *dev)
{
	struct ingenic_wdt_drvdata *drvdata = dev_get_drvdata(dev);
	writeb(0x0, drvdata->base + INGENIC_REG_WDT_COUNTER_ENABLE);
	return 0;
}

static int ingenic_wdt_resume(struct device *dev)
{
	struct ingenic_wdt_drvdata *drvdata = dev_get_drvdata(dev);
	if(drvdata->timeout){
		writel(INGENIC_WDT_TIMER_FLAG,drvdata->base + INGENIC_REG_WDT_TIMER_FLAG_CLR);
		writeb(0x1, drvdata->base + INGENIC_REG_WDT_COUNTER_ENABLE);
		//ping
		writel(readl(drvdata->base + INGENIC_REG_WDT_TIMER_CONTROL) | INGENIC_WDT_CLOCK_CLRZ , drvdata->base + INGENIC_REG_WDT_TIMER_CONTROL);
	}
	return 0;
}

static SIMPLE_DEV_PM_OPS(ingenic_wdt_pm_ops,ingenic_wdt_suspend,ingenic_wdt_resume);
#endif	// CONFIG_PM_SLEEP
static int ingenic_wdt_probe(struct platform_device *pdev)
{
	struct ingenic_wdt_drvdata *drvdata;
	struct watchdog_device *ingenic_wdt;
	struct resource	*res;
	int ret;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct ingenic_wdt_drvdata),
			GFP_KERNEL);
	if (!drvdata) {
		dev_err(&pdev->dev, "Unable to alloacate watchdog device\n");
		return -ENOMEM;
	}
	spin_lock_init(&drvdata->lock);

	drvdata->irq = platform_get_irq(pdev,0);
	if (drvdata->irq < 0) {
		dev_err(&pdev->dev, "no irq for watchdog tick\n");
		ret = drvdata->irq;
		goto err_nosrc;
	}

	if (heartbeat < 1 || heartbeat > MAX_HEARTBEAT)
		heartbeat = DEFAULT_HEARTBEAT;

	ingenic_wdt = &drvdata->wdt;
	ingenic_wdt->info = &ingenic_wdt_info;
	ingenic_wdt->ops = &ingenic_wdt_ops;
	ingenic_wdt->timeout = heartbeat;
	ingenic_wdt->min_timeout = 1;
	ingenic_wdt->max_timeout = MAX_HEARTBEAT;
	watchdog_set_nowayout(ingenic_wdt, nowayout);
	watchdog_set_drvdata(ingenic_wdt, drvdata);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	drvdata->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(drvdata->base)) {
		ret = PTR_ERR(drvdata->base);
		goto err_out;
	}

	drvdata->wdt_clk = devm_clk_get(&pdev->dev, "mux_wdt");

	if (IS_ERR(drvdata->wdt_clk)) {
		dev_err(&pdev->dev, "cannot find WDT clock\n");
		ret = PTR_ERR(drvdata->wdt_clk);
		goto err_out;
	}

#if ((defined CONFIG_SOC_X2600) || (defined CONFIG_SOC_AD100)) 
	drvdata->tcu_clk = devm_clk_get(&pdev->dev, "gate_tcu0");
#else
	drvdata->tcu_clk = devm_clk_get(&pdev->dev, "gate_tcu");
#endif
	if (IS_ERR(drvdata->tcu_clk)) {
		dev_err(&pdev->dev, "cannot find TCU clock\n");
		ret = PTR_ERR(drvdata->tcu_clk);
		goto err_out;
	}

	clk_prepare_enable(drvdata->wdt_clk);
	clk_prepare_enable(drvdata->tcu_clk);

	ret = watchdog_register_device(&drvdata->wdt);
	if (ret < 0)
		goto err_disable_clk;
#if 0
	drvdata->timeout = 0;
	drvdata->restart_handler.notifier_call = ingenic_restart_handler;
	drvdata->restart_handler.priority = 128;
	ret = register_restart_handler(&drvdata->restart_handler);
	if (ret)
		dev_err(&pdev->dev, "cannot register restart handler\n");
#endif

	writel(INGENIC_WDT_TIMER_MASK,drvdata->base + INGENIC_REG_WDT_TIMER_MASK_SET);
	writel(INGENIC_WDT_TIMER_FLAG,drvdata->base + INGENIC_REG_WDT_TIMER_FLAG_CLR);
	ret = request_irq(drvdata->irq,ingenic_wdt_interrupt,
			IRQF_SHARED | IRQF_TRIGGER_LOW,"watchdog-int",drvdata);
	if (ret) {
		pr_debug("IRQ %d already in use.\n", drvdata->irq);
		dev_err(&pdev->dev, "--request_irq error------irq = %d\n",drvdata->irq);
		watchdog_unregister_device(&drvdata->wdt);
		goto err_disable_clk;
	}
	platform_set_drvdata(pdev, drvdata);

	printk("ingenic watchdog probe success\n");
	return 0;
err_nosrc:
	kfree(drvdata);
err_disable_clk:
	clk_put(drvdata->wdt_clk);
	clk_put(drvdata->tcu_clk);
err_out:
	return ret;
}

static int ingenic_wdt_remove(struct platform_device *pdev)
{
	struct ingenic_wdt_drvdata *drvdata = platform_get_drvdata(pdev);

	ingenic_wdt_stop(&drvdata->wdt);
	unregister_restart_handler(&drvdata->restart_handler);
	free_irq(drvdata->irq,drvdata);
	watchdog_unregister_device(&drvdata->wdt);
	clk_put(drvdata->wdt_clk);
	clk_put(drvdata->tcu_clk);

	return 0;
}

static struct platform_driver ingenic_wdt_driver = {
	.probe = ingenic_wdt_probe,
	.remove = ingenic_wdt_remove,
	.driver = {
		.name = "ingenic-watchdog",
		.of_match_table = of_match_ptr(ingenic_wdt_of_matches),
#ifdef CONFIG_PM_SLEEP 
		.pm = &ingenic_wdt_pm_ops,
#endif
	},
};

module_platform_driver(ingenic_wdt_driver);

MODULE_AUTHOR("bo liu <bo.liu@ingenic.com>");
MODULE_DESCRIPTION("ingenic xburst2 Watchdog Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform:ingenic-wdt");
