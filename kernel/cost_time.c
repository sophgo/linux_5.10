#include <linux/cost_time.h>

#ifdef CONFIG_ARM
#include <asm/arch_timer.h>
#endif

unsigned int read_count_tick(void)
{
#if defined(CONFIG_ARM) || defined(__arm__) || defined(__aarch64__)
	u64 c = __arch_counter_get_cntpct();
	u64 f = arch_timer_get_cntfrq();

	do_div(f, 1000000);

	do_div(c, f);
	return c;
#else
	return read_csr(time) / SYS_COUNTER_FREQ_IN_US;
#endif
}

unsigned int read_time_ms(void)
{
	return DIV_ROUND_UP(read_count_tick(), 1000);
}

/**
 * @brief print cost time
 *
 * @param t  last time get
 * @param func  func name
 */
void print_cost_time(unsigned int rettime, const char *func)
{
	unsigned int duration;

	duration = read_time_ms() - rettime;
	printk(KERN_DEBUG "[%s]: cost %u msecs\n", func, duration);
}
