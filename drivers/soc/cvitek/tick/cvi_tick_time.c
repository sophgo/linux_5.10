#include <linux/cost_time.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/module.h>

#ifdef CONFIG_ARM
#include <asm/arch_timer.h>
#endif

#define PROC_TICK_TIME_NAME "tick"

static int tick_time_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%u", read_time_ms());
	return 0;
}

static int tick_time_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, tick_time_proc_show, NULL);
}

static const struct proc_ops tick_time_proc_ops = {
	.proc_open = tick_time_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init tick_time_proc_init(void)
{
	proc_create(PROC_TICK_TIME_NAME, 0, NULL, &tick_time_proc_ops);
	return 0;
}

static void __exit tick_time_proc_exit(void)
{
	remove_proc_entry(PROC_TICK_TIME_NAME, NULL);
}

module_init(tick_time_proc_init);
module_exit(tick_time_proc_exit);
