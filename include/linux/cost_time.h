#ifndef COST_TIME_H
#define COST_TIME_H

#include <linux/ktime.h>
struct _time_records {
	uint16_t fsbl_start;
	uint16_t ddr_init_start;
	uint16_t ddr_init_end;
	uint16_t release_blcp_2nd;
	uint16_t load_loader_2nd_end;
	uint16_t fsbl_decomp_start;
	uint16_t fsbl_decomp_end;
	uint16_t fsbl_exit;
	uint16_t uboot_start;
	uint16_t bootcmd_start;
	uint16_t decompress_kernel_start;
	uint16_t kernel_start;
	uint16_t kernel_run_init_start;
} __packed;

#define TIME_RECORDS_ADDR 0x0E000010
static struct _time_records *time_records =
	(struct _time_records *)TIME_RECORDS_ADDR;

#define SYS_COUNTER_FREQ_IN_US 25
#define read_csr(reg)                                                          \
	({                                                                     \
		unsigned long __tmp;                                           \
		asm volatile("csrr %0, " #reg : "=r"(__tmp));                  \
		__tmp;                                                         \
	})

unsigned int read_count_tick(void);

unsigned int read_time_ms(void);

void print_cost_time(unsigned int rettime, const char *func);

#endif /* COST_TIME_H */