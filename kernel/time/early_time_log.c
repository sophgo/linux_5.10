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
#include <linux/clk.h>
#include <linux/cost_time.h>
#include <asm/io.h>

#define MAX_EARLY_TIME_LOGS 16

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

void early_time_log(const char *name)
{
	u64 now_us = early_time_get_us();

	pr_info("%s: %s: %lluus\n", __func__, name, (unsigned long long)now_us);

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

/* Dump the cvitek time_records struct at TIME_RECORDS_ADDR (0x0C0C701C).
 * The region sits in TPU_SYS which is clocked by clk_tpu; if clk_tpu was
 * gated by clk_disable_unused we must briefly enable it around the ioremap
 * read to avoid hanging the bus. */
static const struct {
	const char *name;
	size_t offset;
} time_records_fields[] = {
	{"fsbl_start",                 offsetof(struct _time_records, fsbl_start)},
	{"ddr_init_start",             offsetof(struct _time_records, ddr_init_start)},
	{"ddr_init_end",               offsetof(struct _time_records, ddr_init_end)},
	{"release_blcp_2nd",           offsetof(struct _time_records, release_blcp_2nd)},
	{"load_loader_2nd_end",        offsetof(struct _time_records, load_loader_2nd_end)},
	{"fsbl_decomp_start",          offsetof(struct _time_records, fsbl_decomp_start)},
	{"fsbl_decomp_end",            offsetof(struct _time_records, fsbl_decomp_end)},
	{"fsbl_exit",                  offsetof(struct _time_records, fsbl_exit)},
	{"uboot_start",                offsetof(struct _time_records, uboot_start)},
	{"bootcmd_start",              offsetof(struct _time_records, bootcmd_start)},
	{"decompress_kernel_start",    offsetof(struct _time_records, decompress_kernel_start)},
	{"kernel_start",               offsetof(struct _time_records, kernel_start)},
	{"kernel_run_init_start",      offsetof(struct _time_records, kernel_run_init_start)},
};

/* Helper: dump time_records at a given physical address with clk_tpu protected
 * access. Returns 0 on success. */
static int dump_time_records_at(struct seq_file *m, const char *label,
				 phys_addr_t addr, bool need_clk_tpu)
{
	struct clk *tpu_clk = NULL;
	void __iomem *base;
	int i;

	if (need_clk_tpu) {
		tpu_clk = clk_get(NULL, "clk_tpu");
		if (IS_ERR(tpu_clk)) {
			seq_printf(m, "[%s] clk_tpu not available: %ld\n", label, PTR_ERR(tpu_clk));
			return 0;
		}
		clk_prepare_enable(tpu_clk);
	}

	base = ioremap(addr, sizeof(struct _time_records));
	if (!base) {
		seq_printf(m, "[%s] ioremap(0x%08llx) failed\n", label, (u64)addr);
		goto out_clk;
	}

	seq_printf(m, "[%s] phys=0x%08llx\n", label, (u64)addr);
	for (i = 0; i < ARRAY_SIZE(time_records_fields); i++) {
		u16 val = ioread16(base + time_records_fields[i].offset);
		seq_printf(m, "  %-26s = %6u ms\n", time_records_fields[i].name, val);
	}

	iounmap(base);
out_clk:
	if (tpu_clk) {
		clk_disable_unprepare(tpu_clk);
		clk_put(tpu_clk);
	}
	return 0;
}

static int time_records_proc_show(struct seq_file *m, void *v)
{
	/* time_records struct at 0x0C00F01C (AXI_SRAM_BASE + 0x1C, where
	 * AXI_SRAM_BASE = TPU_SRAM_ORIGIN_BASE + TPU_SRAM_SIZE = 0x0C000000 + 0xF000).
	 * This is in the last 4KB of TPU SRAM reserved for boot-time data.
	 * fsbl, u-boot, and linux all write to this same address now.
	 *
	 * The region sits in TPU_SYS which is clocked by clk_tpu; if clk_tpu was
	 * gated by clk_disable_unused we must briefly enable it around the ioremap
	 * read to avoid hanging the bus.
	 */
	dump_time_records_at(m, "time_records 0x0C00F01C", 0x0C00F01C, true);
	return 0;
}

static int __init early_time_log_init(void)
{
	proc_create_single("early_time_log", 0, NULL, early_time_log_proc_show);
	proc_create_single("time_records", 0, NULL, time_records_proc_show);
	return 0;
}
fs_initcall(early_time_log_init);
