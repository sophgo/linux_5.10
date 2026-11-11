/*
 * Copyright (c) 2024, Cvitek Co. Ltd. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 * Rpmsg echo test driver for Linux (HOST side).
 *
 * This is a kernel module that creates an rpmsg endpoint named
 * "rpmsg-echo-test".  When AliOS creates the same endpoint name,
 * the rpmsg name service (NS) will match them and establish a
 * communication channel.
 *
 * The driver echoes back every message it receives from AliOS.
 * This provides a simple round-trip verification of the dual-core
 * rpmsg communication path.
 *
 * Usage:
 *   1. Load this module: insmod cvi_rpmsg_echo_test.ko
 *   2. AliOS creates endpoint "rpmsg-echo-test"
 *   3. Messages sent from AliOS are echoed back
 *   4. Check dmesg for "rpmsg-echo-test: ..." messages
 *
 * Alternatively, use CONFIG_RPMSG_CHAR and the user-space test
 * program to send/receive messages via /dev/rpmsgX.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>

#define EPT_NAME	"rpmsg-echo-test"

struct echo_test_data {
	struct rpmsg_endpoint *ept;
	int rx_count;
	int tx_count;
};

/*
 * RX callback: called when a message arrives from AliOS.
 * We echo it back to verify the full round-trip path.
 */
static int echo_test_cb(struct rpmsg_endpoint *ept, void *data, int len,
			uint32_t src, void *priv)
{
	struct echo_test_data *edata = priv;
	int ret;

	edata->rx_count++;
	pr_info("%s: rx #%d, len=%d, src=0x%x\n",
		EPT_NAME, edata->rx_count, len, src);

	/* Print first 64 bytes of payload for debugging */
	print_hex_dump_debug(EPT_NAME, DUMP_PREFIX_NONE, 16, 1,
			     data, min(len, 64), true);

	/* Echo back the same data */
	ret = rpmsg_send(ept, data, len);
	if (ret) {
		pr_err("%s: rpmsg_send failed: %d\n", EPT_NAME, ret);
	} else {
		edata->tx_count++;
		pr_info("%s: tx #%d, echoed %d bytes\n",
			EPT_NAME, edata->tx_count, len);
	}

	return 0;
}

static void echo_test_ns_bind_cb(struct rpmsg_device *rdev,
				  const char *name, uint32_t dest)
{
	struct echo_test_data *edata;
	struct rpmsg_endpoint *ept;

	pr_info("%s: ns bind, name=%s, dest=0x%x\n", EPT_NAME, name, dest);

	edata = kzalloc(sizeof(*edata), GFP_KERNEL);
	if (!edata)
		return;

	ept = rpmsg_create_ept(rdev, name, RPMSG_ADDR_ANY, dest,
			       echo_test_cb, NULL);
	if (!ept) {
		pr_err("%s: rpmsg_create_ept failed\n", EPT_NAME);
		kfree(edata);
		return;
	}

	ept->priv = edata;
	edata->ept = ept;

	pr_info("%s: endpoint created, src=0x%x, dest=0x%x\n",
		EPT_NAME, ept->addr, ept->dest_addr);
}

static void echo_test_ept_release(struct rpmsg_endpoint *ept)
{
	struct echo_test_data *edata = ept->priv;

	if (edata) {
		kfree(edata);
	}
	pr_info("%s: endpoint released\n", EPT_NAME);
}

static struct rpmsg_device_id echo_test_id_table[] = {
	{ .name = EPT_NAME },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, echo_test_id_table);

static struct rpmsg_driver echo_test_driver = {
	.drv.name	= KBUILD_MODNAME,
	.id_table	= echo_test_id_table,
	.callback	= echo_test_cb,
	.ns_bind_cb	= echo_test_ns_bind_cb,
};

module_rpmsg_driver(echo_test_driver);

MODULE_DESCRIPTION("RPMsg Echo Test Driver for Cvitek Dual-Core Communication");
MODULE_LICENSE("GPL v2");
