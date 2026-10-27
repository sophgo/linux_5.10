// SPDX-License-Identifier: GPL-2.0-only
/*
 * LED Disk Activity Trigger
 *
 * Copyright 2006 Openedhand Ltd.
 *
 * Author: Richard Purdie <rpurdie@openedhand.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>
#if IS_ENABLED(CONFIG_CHIP_CPU_CV84X6)
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#endif

#define BLINK_DELAY 30

#if IS_ENABLED(CONFIG_CHIP_CPU_CV84X6)
/* 2Hz：on 250ms / off 250ms */
#define DISK_2HZ_ON_MS	250
#define DISK_2HZ_OFF_MS	250
/* Stop I/O timeout */
#define DISK_2HZ_IDLE_MS	100
#endif

DEFINE_LED_TRIGGER(ledtrig_disk);
DEFINE_LED_TRIGGER(ledtrig_disk_read);
DEFINE_LED_TRIGGER(ledtrig_disk_write);
DEFINE_LED_TRIGGER(ledtrig_ide);

#if IS_ENABLED(CONFIG_CHIP_CPU_CV84X6)
/* SE13 2Hz trigger */
DEFINE_LED_TRIGGER(ledtrig_disk_2hz);

static DEFINE_SPINLOCK(disk_2hz_lock);
static bool disk_2hz_blinking;
static struct timer_list disk_2hz_idle_timer;
static struct work_struct disk_2hz_start_work;
static struct work_struct disk_2hz_stop_work;

/* process context：start 2Hz blink */
static void disk_2hz_start_fn(struct work_struct *w)
{
	unsigned long on = DISK_2HZ_ON_MS, off = DISK_2HZ_OFF_MS;
	bool need_start = false;

	spin_lock(&disk_2hz_lock);
	if (!disk_2hz_blinking) {
		disk_2hz_blinking = true;
		need_start = true;
	}
	spin_unlock(&disk_2hz_lock);

	if (need_start)
		led_trigger_blink(ledtrig_disk_2hz, &on, &off);
}

/* process context： led_stop_software_blink + LED_FULL 常亮 */
static void disk_2hz_stop_fn(struct work_struct *w)
{
	bool need_stop = false;

	spin_lock(&disk_2hz_lock);
	if (disk_2hz_blinking) {
		disk_2hz_blinking = false;
		need_stop = true;
	}
	spin_unlock(&disk_2hz_lock);

	if (need_stop) {
		led_trigger_event(ledtrig_disk_2hz, LED_OFF);
		led_trigger_event(ledtrig_disk_2hz, LED_FULL);
	}
}

/* softirq（timer）：stop work -> workqueue */
static void disk_2hz_idle_timer_fn(struct timer_list *t)
{
	schedule_work(&disk_2hz_stop_work);
}
#endif /* CONFIG_CHIP_CPU_CV84X6 */

void ledtrig_disk_activity(bool write)
{
	unsigned long blink_delay = BLINK_DELAY;

	led_trigger_blink_oneshot(ledtrig_disk,
				  &blink_delay, &blink_delay, 0);
	led_trigger_blink_oneshot(ledtrig_ide,
				  &blink_delay, &blink_delay, 0);
	if (write)
		led_trigger_blink_oneshot(ledtrig_disk_write,
					  &blink_delay, &blink_delay, 0);
	else
		led_trigger_blink_oneshot(ledtrig_disk_read,
					  &blink_delay, &blink_delay, 0);

#if IS_ENABLED(CONFIG_CHIP_CPU_CV84X6)
	/* SE13 2Hz trigger：start work + restart idle wtd */
	schedule_work(&disk_2hz_start_work);
	mod_timer(&disk_2hz_idle_timer, jiffies + msecs_to_jiffies(DISK_2HZ_IDLE_MS));
#endif
}
EXPORT_SYMBOL(ledtrig_disk_activity);

static int __init ledtrig_disk_init(void)
{
	led_trigger_register_simple("disk-activity", &ledtrig_disk);
	led_trigger_register_simple("disk-read", &ledtrig_disk_read);
	led_trigger_register_simple("disk-write", &ledtrig_disk_write);
	led_trigger_register_simple("ide-disk", &ledtrig_ide);

#if IS_ENABLED(CONFIG_CHIP_CPU_CV84X6)
	led_trigger_register_simple("disk-activity-2hz", &ledtrig_disk_2hz);

	disk_2hz_blinking = false;
	INIT_WORK(&disk_2hz_start_work, disk_2hz_start_fn);
	INIT_WORK(&disk_2hz_stop_work, disk_2hz_stop_fn);
	timer_setup(&disk_2hz_idle_timer, disk_2hz_idle_timer_fn, 0);
#endif

	return 0;
}
device_initcall(ledtrig_disk_init);
