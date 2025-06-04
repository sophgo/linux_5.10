/*
 * SPI NAND Flash Controller Device Driver for DT
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/mtd/rawnand.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include "cvsnfc.h"

static int nand_show_id(struct seq_file *m, void *v)
{
	struct cvsnfc_host *host = m->private;
	struct nand_chip *nand_chip = &host->nand;
	uint32_t id = 0;

	id = nand_chip->id.data[1] << 8 | nand_chip->id.data[0];
	seq_printf(m, "%#x\n", id);
	return 0;
}

int opennand(struct inode *inode, struct file *file)
{
        return single_open(file, nand_show_id, PDE_DATA(inode));
}

const struct proc_ops nand_id_proc_ops = {
	.proc_open = opennand,
	.proc_read = seq_read,
	.proc_release = single_release,
};

struct cvsnfc_dt {
	struct cvsnfc_host cvsnfc;
};

static const struct of_device_id cvsnfc_dt_ids[] = {
	{ .compatible = "cvitek,cv1835-spinf"},
	{/* */}
};

MODULE_DEVICE_TABLE(of, cvsnfc_dt_ids);

static int cvsnfc_dt_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;
	struct cvsnfc_dt *dt;
	struct cvsnfc_host *host;
	const struct of_device_id *of_id;
	struct mtd_info *mtd;
	struct proc_dir_entry *proc_id = NULL;

	of_id = of_match_device(cvsnfc_dt_ids, &pdev->dev);

	if (of_id) {
		pdev->id_entry = of_id->data;
	} else {
		pr_err("Failed to find the right device id.\n");
		return -ENOMEM;
	}

	dt = devm_kzalloc(&pdev->dev, sizeof(*dt), GFP_KERNEL);
	if (!dt)
		return -ENOMEM;

	host = &dt->cvsnfc;
	host->dev = &pdev->dev;
	mtd = nand_to_mtd(&host->nand);
	mtd->priv = host;

	mtd->dev.of_node = pdev->dev.of_node;
	host->irq = platform_get_irq(pdev, 0);

	if (host->irq < 0) {
		dev_err(&pdev->dev, "no irq defined\n");
		return host->irq;
	}

	dev_info(host->dev, "IRQ: nr %d\n", host->irq);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->regbase = devm_ioremap_resource(host->dev, res);

	if (IS_ERR(host->regbase)) {
		dev_err(&pdev->dev, "devm_ioremap_resource res 0 failed\n");
		return PTR_ERR(host->regbase);
	}

	host->io_base_phy = res->start;
	host->nand.priv = host;
	mutex_init(&host->lock);

	cvsnfc_nand_init(&host->nand);

	ret = cvsnfc_host_init(host);
	if (ret) {
		pr_err("cvsnfc dt probe error\n");
		return ret;
	}

	ret = cvsnfc_scan_nand(host);
	if (ret) {
		pr_err("cvsnfc scan nand error\n");
		return ret;
	}

	cvsnfc_spi_nand_init(host);
	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(host->dev, "mtd parse partition error\n");
		nand_cleanup(&host->nand);
		return ret;
	}

	proc_id = proc_create_data("nandid", 0444, NULL, &nand_id_proc_ops, (void *)host);
	if (!proc_id)
		dev_err(host->dev, "Create cvsnfc nand id proc failed!\n");

	platform_set_drvdata(pdev, dt);
	return 0;
}

static int cvsnfc_dt_remove(struct platform_device *pdev)
{
	struct cvsnfc_dt *dt = platform_get_drvdata(pdev);

	cvsnfc_remove(&dt->cvsnfc);

	return 0;
}

static struct platform_driver cvsnfc_dt_driver = {
	.probe          = cvsnfc_dt_probe,
	.remove         = cvsnfc_dt_remove,
	.driver         = {
		.name   = "cvsnfc",
		.of_match_table = cvsnfc_dt_ids,
	},
};

module_platform_driver(cvsnfc_dt_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CV");
MODULE_DESCRIPTION("DT driver for SPI NAND flash controller");

