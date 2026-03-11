// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/kernel/archtimer.c
 *
 *  Use arch timer before time_init()
 *
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/time.h>
#include <linux/time_namespace.h>
#include <linux/timex.h>
#include <linux/kernel_stat.h>
#include <linux/early_time_log.h>
#include <linux/io.h>

#define MAX_EARLY_TIME_LOGS 16

// the TIME_RECORDS_ADDR baseaddr is 0x25050010
#define TIME_RECORDS_INIT_PROCESS_START (0x25050010 + 0x14)
struct early_time_entry {
	u64 us;
	const char *name;
};

static struct early_time_entry early_time_logs[MAX_EARLY_TIME_LOGS];
static int early_time_logs_idx;

#if defined(CONFIG_ARM_ARCH_TIMER)
#include <asm/arch_timer.h>

u64 early_time_get_us(void)
{
	u64 c = __arch_counter_get_cntpct();
	u64 f = arch_timer_get_cntfrq();

	do_div(f, 1000000);

	do_div(c, f);
	return c;
}
#elif defined(CONFIG_RISCV_TIMER)
u64 early_time_get_us(void)
{
	cycles_t c = get_cycles();
	u32 f = 25000000 / 1000000;

	do_div(c, f);
	return c;
}
#endif

static void mmio_write_16(uintptr_t addr,
				      uint16_t val)
{
	void __iomem *reg;

	reg = ioremap(addr, 0x2);
	if (IS_ERR(reg)) {
		pr_err("ioremap %p failed\n", (void *)addr);
		return;
	}

	iowrite16(val, reg);

	iounmap(reg);
}

void early_time_log(const char *name)
{
	u64 now_us = early_time_get_us();

	pr_info("%s: %s: %lluus\n", __func__, name, (unsigned long long)now_us);

#if defined(CONFIG_CHIP_CPU_CV186X) 
	// Save Init process run start time
	mmio_write_16(TIME_RECORDS_INIT_PROCESS_START, DIV_ROUND_UP(now_us, 1000));
#endif
	if (early_time_logs_idx < MAX_EARLY_TIME_LOGS) {
		early_time_logs[early_time_logs_idx].us = now_us;
		early_time_logs[early_time_logs_idx].name = name;
		early_time_logs_idx++;
	}
}
EXPORT_SYMBOL_GPL(early_time_log);

static int early_time_log_proc_show(struct seq_file *m, void *v)
{
	int i;

	seq_printf(m, "now: %lluus\n", early_time_get_us());
	for (i = 0; i < early_time_logs_idx; i++) {
		seq_printf(m, "%s: %lluus\n", early_time_logs[i].name,
			   (unsigned long long)early_time_logs[i].us);
	}
	return 0;
}

static int __init early_time_log_init(void)
{
	proc_create_single("early_time_log", 0, NULL, early_time_log_proc_show);
	return 0;
}
fs_initcall(early_time_log_init);
