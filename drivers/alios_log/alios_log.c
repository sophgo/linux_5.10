#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/log2.h>
#include <linux/delay.h>
#include <linux/sched/clock.h>
#include <linux/interrupt.h>
#include <linux/reset.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <asm/cacheflush.h>
#include <linux/dma-map-ops.h>

#define DRIVER_NAME "alios_debug"

#define UART_SHARE_BUFFER_SIZE  (g_res->end - g_res->start + 1)
#define UART_SHARE_BUFFER_READ_CONFIG    UART_SHARE_BUFFER
#define UART_SHARE_BUFFER_WRITE_CONFIG   (UART_SHARE_BUFFER + UART_SHARE_BUFFER_SIZE - 0x4)

#define UART_BUFFER_BASE        (UART_SHARE_BUFFER + 64)
#define UART_BUFFER_SIZE        (UART_SHARE_BUFFER_SIZE - 128)

static void __iomem *share_memory_base_addr;
static struct resource *g_res;
static char *share_buffer;
static uint32_t read_index = 0, read_mirror = 0;

struct cvi_kernel_work {
	struct task_struct *work_thread;
	struct delayed_work save_log_work;
	wait_queue_head_t do_queue;
	int got_event;
};

static void alios_thread_handler(struct work_struct *work)
{
	struct cvi_kernel_work *kernel_work = container_of(work, struct cvi_kernel_work,
						  save_log_work.work);
	kernel_work->got_event = 1;
	wake_up_interruptible(&kernel_work->do_queue);
	schedule_delayed_work(&kernel_work->save_log_work, msecs_to_jiffies(500));
}

static inline void mmio_write_32(void __iomem *addr, uint32_t value)
{
	iowrite32(value, addr);
}

static inline uint32_t mmio_read_32(void __iomem *addr)
{
	return ioread32(addr);
}

static void get_alios_log(struct cvi_kernel_work *data)
{
	char alios_log[1024] = {0};
	int change_flag = 0, index = 0;
	uint32_t write_index, write_mirror;

	// invailed write cache
	arch_sync_dma_for_device((uint64_t *)((uint64_t)(g_res->end - 63)), 64, DMA_FROM_DEVICE);

	write_index = mmio_read_32(share_memory_base_addr + UART_SHARE_BUFFER_SIZE - 0x4);
	if ((write_index == 0xFFFFFFFF) || (write_index > 0x3FF01)) {
		write_index = 0;
		write_mirror = 0;
	} else {
		write_mirror = write_index & 0x1;
		write_index = (write_index >> 1);
	}

	//pr_err("linux: read_index : %d, read_mirror : %d, write_index : %d, write_mirror : %d \n", read_index, read_mirror, write_index, write_mirror);
	do {
		if (write_mirror == read_mirror) {
			if (write_index < read_index) {
				//read_index = write_index;
				//change_flag = 1;
				// error
				break;
			} else if (write_index == read_index) {
				// no data
				break;
			} else {
				while (read_index < write_index) {
					if ((alios_log[index++] = share_buffer[read_index++]) == '\n') {
						break;
					}
				}

				change_flag = 1;
			}
		} else {
			if (write_index > read_index) {
				//read_index = write_index;
				//change_flag = 1;
				// error
				break;
			} else {
				while (share_buffer[read_index] != '\n') {
					alios_log[index++] = share_buffer[read_index++];
					if (read_index == UART_BUFFER_SIZE) {
						read_index = 0;
						read_mirror = (read_mirror == 0)?1:0;
					}

					if ((write_mirror == read_mirror) && (read_index == write_index)) {
						break;
					}
				}

				if (read_index != write_index) {
					alios_log[index++] = '\n';
					read_index++;
					if (read_index == UART_BUFFER_SIZE) {
						read_index = 0;
						read_mirror = (read_mirror == 0)?1:0;
					}
				}

				change_flag = 1;
			}
		}

		alios_log[index] = 0;
		//pr_err("%d-%d %d-%d: %s", read_index, read_mirror, write_index, write_mirror, alios_log);
		pr_err("%s", alios_log);
		index = 0;
	} while (1);

	if (change_flag) {
		mmio_write_32(share_memory_base_addr, ((read_index << 1) | read_mirror));

		// clean read cache
		arch_sync_dma_for_device((uint64_t *)((uint64_t)g_res->start), 64, DMA_TO_DEVICE);
	}
}

static int work_thread_main(void *data)
{
	struct cvi_kernel_work *kernel_work = (struct cvi_kernel_work *)data;

	while (!kthread_should_stop()) {
		wait_event_interruptible(kernel_work->do_queue,
					kernel_work->got_event ||
						kthread_should_stop());

		get_alios_log(kernel_work);
		kernel_work->got_event = 0;
	}

	return 0;
}

static int alios_log_probe(struct platform_device *pdev)
{
	struct cvi_kernel_work *kernel_work = NULL;
	struct device *dev = &pdev->dev;
	int ret;

	kernel_work = kzalloc(sizeof(struct cvi_kernel_work), GFP_KERNEL);
	if (!kernel_work)
		return -ENOMEM;

	g_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	share_memory_base_addr = devm_ioremap_resource(&pdev->dev, g_res);
	pr_info("res-reg: start: 0x%llx, end: 0x%llx, virt-addr(%px).\n",
					g_res->start, g_res->end, share_memory_base_addr);
	if (IS_ERR(share_memory_base_addr)) {
		ret = PTR_ERR(share_memory_base_addr);
		kfree(kernel_work);
		return ret;
	}

	share_buffer = share_memory_base_addr + 64;

	mmio_write_32(share_memory_base_addr, 0);
	arch_sync_dma_for_device((uint64_t *)((uint64_t)g_res->start), 64, DMA_TO_DEVICE);

	init_waitqueue_head(&kernel_work->do_queue);
	INIT_DELAYED_WORK(&kernel_work->save_log_work, alios_thread_handler);

	kernel_work->got_event = 0;
	kernel_work->work_thread =
		kthread_run(work_thread_main, kernel_work, "alios_log");
	if (IS_ERR(kernel_work->work_thread)) {
		pr_err("save_alios_log kthread run fail\n");
		kfree(kernel_work);
		return PTR_ERR(kernel_work->work_thread);
	}

	schedule_delayed_work(&kernel_work->save_log_work, msecs_to_jiffies(300));

	return 0;
}

static int alios_log_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id cvi_alios_log_dt_match[] = { { .compatible = "cvitek,alios_log" }, {} };
static struct platform_driver alios_log_driver = {
	.probe = alios_log_probe,
	.remove = alios_log_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = cvi_alios_log_dt_match,
	},
};

static int __init alios_debug_init(void)
{
	int rc;

	rc = platform_driver_register(&alios_log_driver);

	return 0;
}

static void __exit alios_debug_exit(void)
{
	platform_driver_unregister(&alios_log_driver);
}

module_init(alios_debug_init);
module_exit(alios_debug_exit);

MODULE_AUTHOR("CVITEK Inc");
MODULE_DESCRIPTION("cviteck save_alios_log driver");
MODULE_LICENSE("GPL");
