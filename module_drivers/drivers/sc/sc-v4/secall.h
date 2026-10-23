#ifndef __SECALL_H__
#define __SECALL_H__

#include "jz_pdma.h"
#include "sc.h"
#include <linux/jiffies.h>

#define SC_SECALL_TIMEOUT_MS 5000 //5s

#define send_secall(func) (REG32(DMCS) = (REG32(DMCS) & 0xf) | ((func)<<8) | 0x8)

#define polling_done(args) ({ \
		while (READ_ONCE(args->retval) & 0x80000000); \
		})

static int secall(volatile struct sc_args *argsx,unsigned int func,unsigned state,unsigned int wait){
	argsx->func = (func);
	argsx->state = (state);
	argsx->retval = 0x80000000;
	wmb();
	// printk("1..mcu control:%08x, func:%x\n", *(volatile unsigned int *)0xb3421030, func);
	send_secall(1);
	// printk("2..mcu control:%08x\n", *(volatile unsigned int *)0xb3421030);
	if (wait)
	{
		unsigned long timeout = jiffies + msecs_to_jiffies(SC_SECALL_TIMEOUT_MS);
		while (argsx->retval & 0x80000000) {
			if (time_after(jiffies, timeout)) {
				printk(KERN_ERR "SC: secall timeout func=0x%x state=0x%x retval=0x%x dmcs=0x%x\n",
						func, state, argsx->retval, *(volatile unsigned int *)DMCS);
				return -1;
			}
			cpu_relax();
			// printk("dbg :%x\n", *(volatile unsigned int *)MCU_TCSM_DBG);
			// printk("tcsm bank 1:%08x\n", *(volatile unsigned int *)0xb3423000);
			// printk("3..mcu control:%08x\n", *(volatile unsigned int *)0xb3421030);
			// printk("wait secall excuted: retval:%x\n", argsx->retval);
		}
		rmb();
	}
	// printk("wait secall excuted: retval:%x\n", argsx->retval);
	return argsx->retval;
}

#define get_secall_off(x) ((unsigned int)x - TCSM_BANK(0))
#endif /* __SECALL_H__ */
