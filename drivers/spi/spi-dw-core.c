// SPDX-License-Identifier: GPL-2.0-only
/*
 * Designware SPI core controller driver (refer pxa2xx_spi.c)
 *
 * Copyright (c) 2009, Intel Corporation.
 */

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/highmem.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/dmaengine.h>

#include "spi-dw.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

#define  DW_VERSION_4_04
#define PRE_FILL_SIZE		64

/* Slave spi_device related */
struct chip_data {
	u32 cr0;
	u32 rx_sample_dly;	/* RX sample delay */
};

#ifdef CONFIG_DEBUG_FS

#define DW_SPI_DBGFS_REG(_name, _off)	\
{					\
	.name = _name,			\
	.offset = _off,			\
}

static const struct debugfs_reg32 dw_spi_dbgfs_regs[] = {
	DW_SPI_DBGFS_REG("CTRLR0", DW_SPI_CTRLR0),
	DW_SPI_DBGFS_REG("CTRLR1", DW_SPI_CTRLR1),
	DW_SPI_DBGFS_REG("SSIENR", DW_SPI_SSIENR),
	DW_SPI_DBGFS_REG("SER", DW_SPI_SER),
	DW_SPI_DBGFS_REG("BAUDR", DW_SPI_BAUDR),
	DW_SPI_DBGFS_REG("TXFTLR", DW_SPI_TXFTLR),
	DW_SPI_DBGFS_REG("RXFTLR", DW_SPI_RXFTLR),
	DW_SPI_DBGFS_REG("TXFLR", DW_SPI_TXFLR),
	DW_SPI_DBGFS_REG("RXFLR", DW_SPI_RXFLR),
	DW_SPI_DBGFS_REG("SR", DW_SPI_SR),
	DW_SPI_DBGFS_REG("IMR", DW_SPI_IMR),
	DW_SPI_DBGFS_REG("ISR", DW_SPI_ISR),
	DW_SPI_DBGFS_REG("DMACR", DW_SPI_DMACR),
	DW_SPI_DBGFS_REG("DMATDLR", DW_SPI_DMATDLR),
	DW_SPI_DBGFS_REG("DMARDLR", DW_SPI_DMARDLR),
	DW_SPI_DBGFS_REG("DW_SPI_CTRLR0_EXT", DW_SPI_CTRLR0_EXT),
	DW_SPI_DBGFS_REG("RX_SAMPLE_DLY", DW_SPI_RX_SAMPLE_DLY),
};

static int dw_spi_debugfs_init(struct dw_spi *dws)
{
	char name[32];

	snprintf(name, 32, "dw_spi%d", dws->master->bus_num);
	dws->debugfs = debugfs_create_dir(name, NULL);
	if (!dws->debugfs)
		return -ENOMEM;

	dws->regset.regs = dw_spi_dbgfs_regs;
	dws->regset.nregs = ARRAY_SIZE(dw_spi_dbgfs_regs);
	dws->regset.base = dws->regs;
	debugfs_create_regset32("registers", 0400, dws->debugfs, &dws->regset);

	return 0;
}

static void dw_spi_debugfs_remove(struct dw_spi *dws)
{
	debugfs_remove_recursive(dws->debugfs);
}

#else
static inline int dw_spi_debugfs_init(struct dw_spi *dws)
{
	return 0;
}

static inline void dw_spi_debugfs_remove(struct dw_spi *dws)
{
}
#endif /* CONFIG_DEBUG_FS */

void dw_spi_dump_regs(struct dw_spi *dws)
{
	pr_err("============ DW_SPI REGISTER DUMP ===========\n");
	pr_err("CTRLR0:    0x%08x | CTRLR1:    0x%08x\n",
		   dw_readl(dws, DW_SPI_CTRLR0),
		   dw_readl(dws, DW_SPI_CTRLR1));
	pr_err("SSIENR:    0x%08x | MWCR:      0x%08x\n",
		   dw_readl(dws, DW_SPI_SSIENR),
		   dw_readl(dws, DW_SPI_MWCR));
	pr_err("SER:       0x%08x | BAUDR:     0x%08x\n",
		   dw_readl(dws, DW_SPI_SER),
		   dw_readl(dws, DW_SPI_BAUDR));
	pr_err("TXFTLR:    0x%08x | RXFTLR:    0x%08x\n",
		   dw_readl(dws, DW_SPI_TXFTLR),
		   dw_readl(dws, DW_SPI_RXFTLR));
	pr_err("TXFLR:     0x%08x | RXFLR:     0x%08x\n",
		   dw_readl(dws, DW_SPI_TXFLR),
		   dw_readl(dws, DW_SPI_RXFLR));
	pr_err("SR:        0x%08x | IMR:       0x%08x\n",
		   dw_readl(dws, DW_SPI_SR),
		   dw_readl(dws, DW_SPI_IMR));
	pr_err("ISR:       0x%08x | RISR:      0x%08x\n",
		   dw_readl(dws, DW_SPI_ISR),
		   dw_readl(dws, DW_SPI_RISR));
	pr_err("DMACR:     0x%08x | DMATDLR:   0x%08x\n",
		   dw_readl(dws, DW_SPI_DMACR),
		   dw_readl(dws, DW_SPI_DMATDLR));
	pr_err("DMARDLR:   0x%08x | IDR:       0x%08x\n",
		   dw_readl(dws, DW_SPI_DMARDLR),
		   dw_readl(dws, DW_SPI_IDR));
	pr_err("SSI_ID:    0x%08x | RX_DLY:    0x%08x\n",
		   dw_readl(dws, DW_SPI_VERSION),
		   dw_readl(dws, DW_SPI_RX_SAMPLE_DLY));
	pr_err("CTRLR0_EXT:0x%08x | TXD_EDGE:  0x%08x\n",
		   dw_readl(dws, DW_SPI_CTRLR0_EXT),
		   dw_readl(dws, DW_SPI_TXD_DRIVE_EDGE));
	pr_err("=============================================\n");
}

void dw_spi_set_cs(struct spi_device *spi, bool enable)
{
	struct dw_spi *dws = spi_controller_get_devdata(spi->controller);
	bool cs_high = !!(spi->mode & SPI_CS_HIGH);

	/*
	 * DW SPI controller demands any native CS being set in order to
	 * proceed with data transfer. So in order to activate the SPI
	 * communications we must set a corresponding bit in the Slave
	 * Enable register no matter whether the SPI core is configured to
	 * support active-high or active-low CS level.
	 */
	if (cs_high == enable)
		dw_writel(dws, DW_SPI_SER, BIT(spi->chip_select));
	else
		dw_writel(dws, DW_SPI_SER, 0);
}
EXPORT_SYMBOL_GPL(dw_spi_set_cs);

/* Return the max entries we can fill into tx fifo */
static inline u32 tx_max(struct dw_spi *dws)
{
	u32 tx_room, rxtx_gap;

	tx_room = dws->fifo_len - dw_readl(dws, DW_SPI_TXFLR);

	/*
	 * Another concern is about the tx/rx mismatch, we
	 * though to use (dws->fifo_len - rxflr - txflr) as
	 * one maximum value for tx, but it doesn't cover the
	 * data which is out of tx/rx fifo and inside the
	 * shift registers. So a control from sw point of
	 * view is taken.
	 */
	rxtx_gap = dws->fifo_len - (dws->rx_len - dws->tx_len);

	return min3((u32)dws->tx_len, tx_room, rxtx_gap);
}

/* Return the max entries we should read out of rx fifo */
static inline u32 rx_max(struct dw_spi *dws)
{
	return min_t(u32, dws->rx_len, dw_readl(dws, DW_SPI_RXFLR));
}

static void dw_writer(struct dw_spi *dws)
{
	u32 max = tx_max(dws);
	u16 txw = 0;

	while (max--) {
		if (dws->tx) {
			if (dws->n_bytes == 1)
				txw = *(u8 *)(dws->tx);
			else
				txw = *(u16 *)(dws->tx);

			dws->tx += dws->n_bytes;
		}
		dw_write_io_reg(dws, DW_SPI_DR, txw);
		--dws->tx_len;
	}
}

/*
 * Enhanced-SPI TX: the address phase consumes one 16-bit FIFO entry for
 * A23:8 and one 8-bit entry for A7:0. Feeding three 8-bit entries makes
 * the FSM take {0x00,A23:16} as the first 16-bit word, so the address
 * shifts out misaligned and the write lands at the wrong offset.
 * Payload layout: [instr 8b][addr 23:16][addr 15:8][addr 7:0][data...].
 */
static void dw_writer_quad_tx(struct dw_spi *dws)
{
	u8 *tx = dws->tx;
	unsigned int i;

	dw_write_io_reg(dws, DW_SPI_DR, tx[0]);
	dw_write_io_reg(dws, DW_SPI_DR, (u16)((tx[1] << 8) | tx[2]));
	dw_write_io_reg(dws, DW_SPI_DR, tx[3]);
	for (i = 4; i < dws->tx_len; i++)
		dw_write_io_reg(dws, DW_SPI_DR, tx[i]);

	dws->tx += dws->tx_len;
	dws->tx_len = 0;
}

static void dw_reader(struct dw_spi *dws)
{
	u32 max = rx_max(dws);
	u16 rxw;

	while (max--) {
		rxw = dw_read_io_reg(dws, DW_SPI_DR);
		if (dws->rx) {
			if (dws->n_bytes == 1)
				*(u8 *)(dws->rx) = rxw;
			else
				*(u16 *)(dws->rx) = rxw;

			dws->rx += dws->n_bytes;
		}
		--dws->rx_len;
	}
}

int dw_spi_check_status(struct dw_spi *dws, bool raw)
{
	u32 irq_status;
	int ret = 0;

	if (raw)
		irq_status = dw_readl(dws, DW_SPI_RISR);
	else
		irq_status = dw_readl(dws, DW_SPI_ISR);

	if (irq_status & SPI_INT_RXOI) {
		dev_err(&dws->master->dev, "RX FIFO overflow detected\n");
		dw_spi_dump_regs(dws);
		ret = -EIO;
	}

	if (irq_status & SPI_INT_RXUI) {
		dev_err(&dws->master->dev, "RX FIFO underflow detected\n");
		dw_spi_dump_regs(dws);
		ret = -EIO;
	}

	if (irq_status & SPI_INT_TXOI) {
		dev_err(&dws->master->dev, "TX FIFO overflow detected\n");
		dw_spi_dump_regs(dws);
		ret = -EIO;
	}

	/* Generically handle the erroneous situation */
	if (ret) {
		spi_reset_chip(dws);
		if (dws->master->cur_msg)
			dws->master->cur_msg->status = ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(dw_spi_check_status);

/*
 * Quad Tx-only transfers keep rx_len at zero, so the generic "!rx_len"
 * completion criterion in the IRQ handler would fire on the very first
 * TXE interrupt - before a single frame has been shifted out. Wait until
 * both the Tx FIFO and the shift register have fully drained instead.
 */
static int dw_spi_wait_tx_idle(struct dw_spi *dws)
{
	/*
	 * Drain budget follows the wire: worst case is FIFO(depth)+shifter
	 * frames at the widest 16-bit/1-line pace, scaled by the real SCK
	 * (BAUDR allows down to ~3kHz), plus margin. Typical quad drain
	 * at MHz clocks finishes in microseconds.
	 */
	u32 budget_us = DIV_ROUND_UP(2 * (dws->fifo_len + 1) * 16 * 1000000UL,
				     dws->current_freq) + 100;

	while ((dw_readl(dws, DW_SPI_TXFLR) ||
		(dw_readl(dws, DW_SPI_SR) & SR_BUSY)) && budget_us) {
		budget_us--;
		udelay(1);
	}

	if (!budget_us) {
		dev_err(&dws->master->dev, "TX drain timed out\n");
		return -EIO;
	}

	return 0;
}

static irqreturn_t dw_spi_transfer_handler(struct dw_spi *dws)
{
	u16 irq_status = dw_readl(dws, DW_SPI_ISR);

	if (dw_spi_check_status(dws, false)) {
		spi_finalize_current_transfer(dws->master);
		return IRQ_HANDLED;
	}

	/*
	 * Read data from the Rx FIFO every time we've got a chance executing
	 * this method. If there is nothing left to receive, terminate the
	 * procedure. Otherwise adjust the Rx FIFO Threshold level if it's a
	 * final stage of the transfer. By doing so we'll get the next IRQ
	 * right when the leftover incoming data is received.
	 */
	dw_reader(dws);
	if (!dws->rx_len && !dws->wait_tx_drain) {
		spi_mask_intr(dws, 0xff);
		spi_finalize_current_transfer(dws->master);
	} else if (dws->rx_len && dws->rx_len <= dw_readl(dws, DW_SPI_RXFTLR)) {
		dw_writel(dws, DW_SPI_RXFTLR, dws->rx_len - 1);
	}

	/*
	 * Send data out if Tx FIFO Empty IRQ is received. The IRQ will be
	 * disabled after the data transmission is finished so not to
	 * have the TXE IRQ flood at the final stage of the transfer.
	 */
	if (irq_status & SPI_INT_TXEI) {
		dw_writer(dws);
		if (!dws->tx_len)
			spi_mask_intr(dws, SPI_INT_TXEI);
	}

	/*
	 * Quad Tx-only completion: all frames are queued once tx_len hits
	 * zero; wait for the wire to drain, then finalize. No further IRQ
	 * is pending at this point (TXEI is masked when tx_len depletes).
	 */
	if (dws->wait_tx_drain && !dws->tx_len) {
		if (dw_spi_wait_tx_idle(dws) && dws->master->cur_msg)
			dws->master->cur_msg->status = -EIO;
		spi_mask_intr(dws, 0xff);
		spi_finalize_current_transfer(dws->master);
	}

	return IRQ_HANDLED;
}

static irqreturn_t dw_spi_irq(int irq, void *dev_id)
{
	struct spi_controller *master = dev_id;
	struct dw_spi *dws = spi_controller_get_devdata(master);
	u16 irq_status = dw_readl(dws, DW_SPI_ISR) & 0x3f;

	if (!irq_status)
		return IRQ_NONE;

	if (!master->cur_msg) {
		spi_mask_intr(dws, 0xff);
		return IRQ_HANDLED;
	}

	return dws->transfer_handler(dws);
}

static irqreturn_t dw_spinor_irq(int irq, void *dev_id)
{
	struct spi_controller *master = dev_id;
	struct dw_spi *dws = spi_controller_get_devdata(master);
	u16 irq_status = dw_readl(dws, DW_SPI_ISR) & 0x3f;

	if (!irq_status)
		return IRQ_NONE;

	return dws->transfer_handler(dws);
}

static u32 dw_spi_prepare_cr0(struct dw_spi *dws, struct spi_device *spi)
{
	u32 cr0 = 0;

	if (!(dws->caps & DW_SPI_CAP_DWC_SSI)) {
		/* CTRLR0[ 5: 4] Frame Format */
		cr0 |= SSI_MOTO_SPI << SPI_FRF_OFFSET;

		/*
		 * SPI mode (SCPOL|SCPH)
		 * CTRLR0[ 6] Serial Clock Phase
		 * CTRLR0[ 7] Serial Clock Polarity
		 */
		cr0 |= ((spi->mode & SPI_CPOL) ? 1 : 0) << SPI_SCOL_OFFSET;
		cr0 |= ((spi->mode & SPI_CPHA) ? 1 : 0) << SPI_SCPH_OFFSET;

		/* CTRLR0[11] Shift Register Loop */
		cr0 |= ((spi->mode & SPI_LOOP) ? 1 : 0) << SPI_SRL_OFFSET;
	} else {
		/* CTRLR0[ 7: 6] Frame Format */
		cr0 |= SSI_MOTO_SPI << DWC_SSI_CTRLR0_FRF_OFFSET;

		/*
		 * SPI mode (SCPOL|SCPH)
		 * CTRLR0[ 8] Serial Clock Phase
		 * CTRLR0[ 9] Serial Clock Polarity
		 */
		cr0 |= ((spi->mode & SPI_CPOL) ? 1 : 0) << DWC_SSI_CTRLR0_SCPOL_OFFSET;
		cr0 |= ((spi->mode & SPI_CPHA) ? 1 : 0) << DWC_SSI_CTRLR0_SCPH_OFFSET;

		/* CTRLR0[13] Shift Register Loop */
		cr0 |= ((spi->mode & SPI_LOOP) ? 1 : 0) << DWC_SSI_CTRLR0_SRL_OFFSET;

		if (dws->caps & DW_SPI_CAP_KEEMBAY_MST)
			cr0 |= DWC_SSI_CTRLR0_KEEMBAY_MST;
	}

	return cr0;
}

u8 select_data_width(u8 width)
{
	u8 w = 0;

	switch (width) {
	case 1:
		w = 0;
		break;

	case 2:
		w = 1;
		break;

	case 4:
		w = 2;
		break;
	}
	return w;
}

void dw_spictrl_update_config(struct dw_spi *dws, struct spi_device *spi,
			  struct dw_spi_cfg *cfg)
{
	struct chip_data *chip = spi_get_ctldata(spi);
	u32 cr0 = chip->cr0;
	u32 speed_hz;
	u16 clk_div;

	/* CTRLR0[ 4/3: 0] Data Frame Size */
	cr0 |= (cfg->dfs - 1);

	if (!(dws->caps & DW_SPI_CAP_DWC_SSI))
		/* CTRLR0[ 9:8] Transfer Mode */
		cr0 |= cfg->tmode << SPI_TMOD_OFFSET;
	else
		/* CTRLR0[11:10] Transfer Mode */
		cr0 |= cfg->tmode << DWC_SSI_CTRLR0_TMOD_OFFSET;

	/* CTRLR0[22:21] Frame Format: dual/quad data lines (v5 instances) */
	if (cfg->nbits > 1) {
		cr0 &= ~(0x3 << 21);
		cr0 |= (select_data_width(cfg->nbits) & 0x3) << 21;
	}

	dw_writel(dws, DW_SPI_CTRLR0, cr0);

	if (cfg->tmode == SPI_TMOD_EPROMREAD || cfg->tmode == SPI_TMOD_RO)
		dw_writel(dws, DW_SPI_CTRLR1, cfg->ndf ? cfg->ndf - 1 : 0);

	/* Note DW APB SSI clock divider doesn't support odd numbers */
	clk_div = (DIV_ROUND_UP(dws->max_freq, cfg->freq) + 1) & 0xfffe;
	speed_hz = dws->max_freq / clk_div;

	if (dws->current_freq != speed_hz) {
		spi_set_clk(dws, clk_div);
		dws->current_freq = speed_hz;
	}

	/* Update RX sample delay if required */
	if (dws->cur_rx_sample_dly != chip->rx_sample_dly) {
		dw_writel(dws, DW_SPI_RX_SAMPLE_DLY, chip->rx_sample_dly);
		dws->cur_rx_sample_dly = chip->rx_sample_dly;
	}
}
EXPORT_SYMBOL_GPL(dw_spictrl_update_config);

void dw_spi_update_config(struct dw_spi *dws, struct spi_device *spi,
			  struct dw_spi_cfg *cfg, const struct spi_mem_op *op)
{
	struct chip_data *chip = spi_get_ctldata(spi);
	u32 cr0 = chip->cr0;
	u32 speed_hz;
	u32 spi_ctrl0 = 0;
	u16 clk_div;

	/* CTRLR0 Data Frame Size */
	if (op->data.buswidth > 1 && op->data.nbytes > 4) {
		cfg->dfs = 32;
		cr0 |= 1 << 25;
	}
	cr0 |= (((cfg->dfs - 1) & 0x1F)  << 16);

	if (!(dws->caps & DW_SPI_CAP_DWC_SSI))
		/* CTRLR0[ 9:8] Transfer Mode */
		cr0 |= cfg->tmode << SPI_TMOD_OFFSET;
	else
		/* CTRLR0[11:10] Transfer Mode */
		cr0 |= cfg->tmode << DWC_SSI_CTRLR0_TMOD_OFFSET;

	cr0 |= cfg->tmode << SPI_TMOD_OFFSET;
	if (op->data.nbytes) {
		cr0 &= ~(0x3 << 21);
		cr0 |= ((select_data_width(op->data.buswidth) & 0x3) << 21);
	}

	/* only have cmd */
	if (!op->addr.nbytes && !op->data.nbytes) {
		cr0 &= ~(0x3 << 21);
		cr0 |= ((select_data_width(op->cmd.buswidth) & 0x3) << 21);
	}
	dw_writel(dws, DW_SPI_CTRLR0, cr0);

	/* Instruction Length: always 8bit inst */
	if (op->cmd.nbytes)
		spi_ctrl0 |= (2 << 8);

	/*  set Address Length */
	if (op->addr.nbytes)
		spi_ctrl0 |= ((op->addr.nbytes * 2) << 2);

	if (op->dummy.nbytes)
		spi_ctrl0 |= (op->dummy.nbytes * 8 / op->dummy.buswidth) << 11;

	if (op->cmd.buswidth && op->addr.buswidth) {
		/*
		 * Address and instruction transfer in Standard SPI Mode format
		 */
		if (op->cmd.buswidth == 1 && op->cmd.buswidth == op->addr.buswidth)
			spi_ctrl0 &= ~0x3;

		/*
		 * Address and instruction transfer format
		 * for 1-x-x, x != 1
		 */
		if (op->cmd.buswidth == 1 && op->cmd.buswidth != op->addr.buswidth)
			spi_ctrl0 |= 0x1;

		/*
		 *  QSPI mode or std mode
		 */
		if (op->cmd.buswidth != 1)
			spi_ctrl0 |= 0x2;
	}

	/* for only cmd */
	if (op->cmd.buswidth && !op->addr.buswidth)
		spi_ctrl0 |= 0x2;

	if (op->data.dtr == 1)
		spi_ctrl0 |= 1 << 16;

	dw_writel(dws, DW_SPI_CTRLR0_EXT, spi_ctrl0);

	if (cfg->tmode == SPI_TMOD_EPROMREAD || cfg->tmode == SPI_TMOD_RO) {
		int tmp = cfg->ndf;

		cfg->ndf = ((cfg->ndf * 8 + cfg->dfs - 1) & ~(cfg->dfs - 1)) / cfg->dfs;

		dw_writel(dws, DW_SPI_CTRLR1, cfg->ndf ? cfg->ndf - 1 : 0);
	}

	/* Note DW APB SSI clock divider doesn't support odd numbers */
	clk_div = (DIV_ROUND_UP(dws->max_freq, cfg->freq) + 1) & 0xfffe;
	speed_hz = dws->max_freq / clk_div;
	// dev_err(&dws->master->dev,"clk_div:%d, speed_hz:%d\n", clk_div, speed_hz);

	if (dws->current_freq != speed_hz) {
		spi_set_clk(dws, clk_div);
		dws->current_freq = speed_hz;
	}

	/* Update RX sample delay if required */
	if (dws->cur_rx_sample_dly != chip->rx_sample_dly) {
		dw_writel(dws, DW_SPI_RX_SAMPLE_DLY, chip->rx_sample_dly);
		dws->cur_rx_sample_dly = chip->rx_sample_dly;
	}
}
EXPORT_SYMBOL_GPL(dw_spi_update_config);

static void dw_spi_irq_setup(struct dw_spi *dws)
{
	u16 level;
	u8 imask;

	/*
	 * Originally Tx and Rx data lengths match. Rx FIFO Threshold level
	 * will be adjusted at the final stage of the IRQ-based SPI transfer
	 * execution so not to lose the leftover of the incoming data.
	 */
	level = min_t(u16, dws->fifo_len / 2, dws->tx_len);
	dw_writel(dws, DW_SPI_TXFTLR, level);

	/*
	 * Rx threshold derives from rx_len: quad read-only transfers have
	 * tx_len == 0 which would underflow the threshold to 0xffff.
	 */
	level = min_t(u16, dws->fifo_len / 2, dws->rx_len);
	dw_writel(dws, DW_SPI_RXFTLR, level ? level - 1 : 0);

	dws->transfer_handler = dw_spi_transfer_handler;

	imask = SPI_INT_TXEI | SPI_INT_TXOI | SPI_INT_RXUI | SPI_INT_RXOI |
		SPI_INT_RXFI;
	spi_umask_intr(dws, imask);
}

/*
 * The iterative procedure of the poll-based transfer is simple: write as much
 * as possible to the Tx FIFO, wait until the pending to receive data is ready
 * to be read, read it from the Rx FIFO and check whether the performed
 * procedure has been successful.
 *
 * Note this method the same way as the IRQ-based transfer won't work well for
 * the SPI devices connected to the controller with native CS due to the
 * automatic CS assertion/de-assertion.
 */
static int dw_spi_poll_transfer(struct dw_spi *dws,
				struct spi_transfer *transfer)
{
	struct spi_delay delay;
	u16 nbits;
	int ret;

	delay.unit = SPI_DELAY_UNIT_SCK;
	nbits = dws->n_bytes * BITS_PER_BYTE;

	do {
		dw_writer(dws);

		delay.value = nbits * (dws->rx_len - dws->tx_len);
		spi_delay_exec(&delay, transfer);

		dw_reader(dws);

		ret = dw_spi_check_status(dws, true);
		if (ret)
			return ret;
	} while (dws->rx_len);

	return 0;
}

static int dw_spi_transfer_one(struct spi_controller *master,
		struct spi_device *spi, struct spi_transfer *transfer)
{
	struct dw_spi *dws = spi_controller_get_devdata(master);
	u8 tx_nbits = transfer->tx_nbits ? transfer->tx_nbits : 1;
	u8 rx_nbits = transfer->rx_nbits ? transfer->rx_nbits : 1;
	u8 nbits = max(tx_nbits, rx_nbits);
	bool quad_ro = false;
	bool use_dma;
	struct dw_spi_cfg cfg = {
		.tmode = SPI_TMOD_TR,
		.dfs = transfer->bits_per_word,
		.freq = transfer->speed_hz,
	};
	int ret;

	dws->dma_mapped = 0;
	dws->n_bytes = DIV_ROUND_UP(transfer->bits_per_word, BITS_PER_BYTE);
	dws->tx = (void *)transfer->tx_buf;
	dws->tx_len = transfer->len / dws->n_bytes;
	dws->rx = transfer->rx_buf;
	dws->rx_len = dws->tx_len;
	dws->wait_tx_drain = false;

	/*
	 * Decide once, up front: the quad RO tail guard below needs the
	 * result before dws->dma_mapped is committed. dw_spi_can_dma() has
	 * no side effects, so evaluating it here is safe.
	 */
	use_dma = master->can_dma && master->can_dma(master, spi, transfer);

	/*
	 * Strip the SPI core's MUST_TX dummy from wide RX transfers.
	 * Once DMA init succeeds this driver sets SPI_CONTROLLER_MUST_TX,
	 * and spi_map_msg() then attaches ctlr->dummy_tx to every transfer
	 * without a TX buffer -- so a quad read arrives here looking like
	 * a full-duplex transfer, which the "quad full-duplex is
	 * physically impossible" check below rejects with -EINVAL.
	 * A quad read is RX-only on the wire; the RO-mode start kick
	 * (one temp TX word, written further below) covers the
	 * controller's TX requirement.
	 */
	if (nbits > 1 && transfer->rx_buf && transfer->tx_buf &&
	    transfer->tx_buf == master->dummy_tx) {
		transfer->tx_buf = NULL;
		dws->tx = NULL;
	}

	if (nbits > 1) {
		/* Dual/quad data phases on quad-capable (v5) instances only */
		if (!dws->quad_capable)
			return -EINVAL;
		/* quad full-duplex is physically impossible */
		if (!transfer->tx_buf == !transfer->rx_buf)
			return -EINVAL;
		/* the active direction must carry the requested width */
		if ((transfer->tx_buf && tx_nbits != nbits) ||
		    (!transfer->tx_buf && rx_nbits != nbits))
			return -EINVAL;
		/* a frame must hold a whole number of line-groups (dual %2, quad %4) */
		if (transfer->bits_per_word % nbits)
			return -EINVAL;
		/* poll-mode has no IRQ to drive quad TO/RO completion */
		if (dws->irq == IRQ_NOTCONNECTED)
			return -EINVAL;

		cfg.nbits = nbits;
		if (transfer->tx_buf) {
			cfg.tmode = SPI_TMOD_TO;
			dws->rx_len = 0;
			dws->wait_tx_drain = true;
			/*
			 * Quad TX pushes one FIFO stream [instr 8b][addr 24b]
			 * [data], and the address must be packed as two
			 * entries (A23:8 16-bit, A7:0 8-bit). The frame width
			 * is therefore fixed at 8 bits, the payload must have
			 * at least the 4-byte header, and the IRQ-fed writer
			 * cannot produce that packing -- refuse the shapes it
			 * would silently misalign.
			 */
			if (dws->n_bytes != 1 ||
			    transfer->bits_per_word != 8 ||
			    dws->tx_len < 4 || dws->tx_len > dws->fifo_len)
				return -EINVAL;
		} else {
			/* CTRLR1 NDF is 16-bit wide */
			if (dws->rx_len > SPI_NDF_MASK + 1)
				return -EINVAL;
			cfg.tmode = SPI_TMOD_RO;
			cfg.ndf = dws->rx_len;
			dws->tx_len = 0;
			quad_ro = true;

			/*
			 * A block-mode Rx DMA request only rises once RXFLR
			 * reaches the DMARDLR threshold, so a tail shorter
			 * than one dma burst is never fetched and the
			 * descriptor stays incomplete. Refuse such a length
			 * up front instead of hitting the dma timeout later.
			 */
			if (use_dma && dws->rxburst &&
			    dws->rx_len % dws->rxburst)
				return -EINVAL;
		}
	}

	/* Ensure the data above is visible for all CPUs */
	smp_mb();

	spi_enable_chip(dws, 0);

	dw_spictrl_update_config(dws, spi, &cfg);

	/*
	 * Quad instances: reprogram SPI_CTRLR0 (0xf4) on every transfer
	 * while SSI is disabled, so leftover TRANS_TYPE/WAIT from a
	 * previous spi-mem quad op cannot leak in. On v2 instances 0xf4 is
	 * the CVI-custom CTRL2 register - never touch it there.
	 *
	 * TX quad transfers (QPP-style writes) re-enable hardware phase
	 * splitting, mirroring the PLD-proven bmtest shape: the payload is
	 * one FIFO stream [instr 8b][addr 24b][data], instr/addr shifted
	 * out on a single line, data on the quad lanes. RX quad (QREAD)
	 * keeps zero -- its header arrives in separate 1-bit transfers,
	 * so the data phase is plain quad bytes.
	 */
	if (dws->quad_capable) {
		u32 ext = 0;

		if (nbits > 1 && transfer->tx_buf)
			ext = SPI_CTRLR0_INST_L8 | SPI_CTRLR0_ADDR_L_3B |
			      SPI_CTRLR0_TRANS_STD;
		dw_writel(dws, DW_SPI_CTRLR0_EXT, ext);
	}

	transfer->effective_speed_hz = dws->current_freq;

	/* Check if current transfer is a DMA transaction */
	if (use_dma)
		dws->dma_mapped = master->cur_msg_mapped;

	/* For poll mode just disable all interrupts */
	spi_mask_intr(dws, 0xff);

	if (dws->dma_mapped) {
		ret = dws->dma_ops->dma_setup(dws, transfer);
		if (ret)
			return ret;
	}

	if (quad_ro) {
		/*
		 * Drop any stale Rx data. Disabling the SSI clears the FIFOs, so
		 * this is only a safety net -- but it must run *before* the Rx
		 * DMA is armed, or a live DMAC would race the CPU for DR.
		 */
		while (dw_readl(dws, DW_SPI_RXFLR))
			dw_read_io_reg(dws, DW_SPI_DR);
	}

	/*
	 * Arm the Rx DMA before the SSI starts: a quad RO transfer free-runs
	 * the clock from the kick below, and at speed the 16-frame Rx FIFO
	 * overruns long before a descriptor submitted afterwards reaches the
	 * DMA engine. See dw_spi_dma_arm_rx().
	 */
	if (quad_ro && dws->dma_mapped) {
		ret = dw_spi_dma_arm_rx(dws, transfer);
		if (ret)
			return ret;

		/* DMAC must be live before the SSI can clock data it has to catch */
		dma_wmb();
	}

	spi_enable_chip(dws, 1);

	if (quad_ro) {
		/*
		 * Kick the transfer: in RO mode the DW_apb_ssi starts SCK only
		 * after one temporary Tx word has been written (the Tx FIFO may
		 * stay empty afterwards).
		 *
		 * wmb() first: the enable above and this kick are both relaxed
		 * MMIO stores and RVWMO does not order them -- a kick landing
		 * before the enable would leave the transfer unstarted.
		 */
		wmb();
		dw_write_io_reg(dws, DW_SPI_DR, 0);
	}

	if (dws->dma_mapped)
		return dws->dma_ops->dma_transfer(dws, transfer);
	else if (dws->irq == IRQ_NOTCONNECTED)
		return dw_spi_poll_transfer(dws, transfer);

	/*
	 * TX-only transfers that fit in the FIFO are queued in one burst
	 * before enabling IRQs, mirroring the bmtest (fill the FIFO, then
	 * assert SER). An IRQ-fed refill cannot keep up with the quad
	 * shifter at 2 clk/frame -- a starved FIFO misaligns the byte
	 * stream mid page-program. Tool chunks cap at 4-byte header + 12
	 * data = 16 entries = fifo_len, so one burst always drains tx_len;
	 * larger TO transfers (1-bit pages) keep the IRQ-fed path.
	 */
	if (cfg.tmode == SPI_TMOD_TO && dws->tx_len <= dws->fifo_len) {
		if (cfg.nbits > 1)
			dw_writer_quad_tx(dws);
		else
			dw_writer(dws);
	}

	dw_spi_irq_setup(dws);

	return 1;
}

static void dw_spi_handle_err(struct spi_controller *master,
		struct spi_message *msg)
{
	struct dw_spi *dws = spi_controller_get_devdata(master);

	if (dws->dma_mapped)
		dws->dma_ops->dma_stop(dws);

	spi_reset_chip(dws);
}

static int dw_spi_adjust_mem_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	if (op->data.dir == SPI_MEM_DATA_IN)
		op->data.nbytes = clamp_val(op->data.nbytes, 0, SPI_NDF_MASK + 1);

	return 0;
}

static bool dw_spi_supports_mem_op(struct spi_mem *mem,
				   const struct spi_mem_op *op)
{
	if (op->data.buswidth >= 1 || op->addr.buswidth >= 1 ||
	    op->dummy.buswidth >= 1 || op->cmd.buswidth >= 1)
		return true;

	return spi_mem_default_supports_op(mem, op);
}

void swap_a(u8 *a, u8 *b)
{
	u8 temp = *a;
	*a = *b;
	*b = temp;
}

bool can_dma(struct dw_spi *dws, const struct spi_mem_op *op)
{
	//FIXME: disabled temporarily, fix dma transfer later

	/* No DMA channels (SPI_DW_DMA=n or no dmas in DT): never take DMA */
	if (!dws->rxchan || !dws->txchan)
		return false;

    if ((op->data.dir == SPI_MEM_DATA_IN) && (op->data.nbytes > dws->fifo_len * dws->n_bytes / 2))
    	return true;

	return false;
}

void handle_data_for_write(u8 *out, const struct spi_mem_op *op)
{
	u8 *buf = out;
	u32 len = op->data.nbytes;
	int i;

	if (op->data.dir == SPI_MEM_DATA_IN)
		return;

	if (op->data.nbytes > 4 && op->data.buswidth > 1) {
		for (i = 0; i < len / 4; i++) {
			swap_a(&buf[0], &buf[3]);
			swap_a(&buf[1], &buf[2]);
			buf += 4;
		}
	}

	switch (len % 4) {
    case 3:
	    swap_a(&buf[0], &buf[2]);
    case 2:
	    swap_a(&buf[0], &buf[1]);
        break;
    case 1:
        default:
        break;
	}
}

static int dw_spi_init_mem_buf(struct dw_spi *dws, const struct spi_mem_op *op)
{
	unsigned int i, j, len;
	u8 *out;

	/*
	 * Calculate the total length of the EEPROM command transfer and
	 * either use the pre-allocated buffer or create a temporary one.
	 */
	len = op->cmd.nbytes + op->addr.nbytes + op->dummy.nbytes;

	if (op->data.dir == SPI_MEM_DATA_OUT)
		len += op->data.nbytes;

	if (len <= SPI_BUF_SIZE) {
		out = dws->buf;
	} else {
		out = kzalloc(len, GFP_KERNEL);
		if (!out)
			return -ENOMEM;
	}

	/*
	 * Collect the operation code, address and dummy bytes into the single
	 * buffer. If it's a transfer with data to be sent, also copy it into the
	 * single buffer in order to speed the data transmission up.
	 */
	for (i = 0; i < op->cmd.nbytes; ++i)
		out[i] = SPI_GET_BYTE(op->cmd.opcode, op->cmd.nbytes - i - 1);
	for (j = 0; j < op->addr.nbytes; ++i, ++j)
		out[i] = SPI_GET_BYTE(op->addr.val, op->addr.nbytes - j - 1);
	for (j = 0; j < op->dummy.nbytes; ++i, ++j)
		out[i] = 0x0;

	if (op->data.dir == SPI_MEM_DATA_OUT)
		memcpy(&out[i], op->data.buf.out, op->data.nbytes);

	if (op->data.buswidth > 1 && op->data.nbytes > 4)
		dws->n_bytes = 4;
	else
		dws->n_bytes = 1;

	//FIXME:
	if (op->data.dir == SPI_MEM_DATA_OUT && can_dma(dws, op)) {
		memcpy(out, op->data.buf.out, op->data.nbytes);
		handle_data_for_write(out, op);
	}

	dws->tx = out;
	dws->tx_len = len;
	if (op->data.dir == SPI_MEM_DATA_IN) {
		dws->rx = op->data.buf.in;
		dws->rx_len = op->data.nbytes;
	} else {
		dws->rx = NULL;
		dws->rx_len = 0;
	}

	return 0;
}

static void dw_spi_free_mem_buf(struct dw_spi *dws)
{
	if (dws->tx != dws->buf)
		kfree(dws->tx);
}

void write_addr_data(struct dw_spi *dws, const struct spi_mem_op *op)
{
	u8 addr[8] = {0};
	int j = 0;
	u32 addr_len = op->addr.nbytes;

	if (!op->addr.nbytes)
		return;

	for (j = 0; j < op->addr.nbytes; j++)
		addr[j] = SPI_GET_BYTE(op->addr.val, op->addr.nbytes - j - 1);

	switch (op->data.buswidth) {
	/* erase operation */
	case 0:
	case 1:
		for (j = 0; j < addr_len; j++)
			dw_write_io_reg(dws, DW_SPI_DR, addr[j]);
		break;

	case 2:
		dw_write_io_reg(dws, DW_SPI_DR, (u32)op->addr.val);
		break;

	case 4:
		dw_write_io_reg(dws, DW_SPI_DR, (u32)op->addr.val);
		break;

	default:
		pr_info("error data width\n");
	}
}

static inline u32 dw_swap(u32 x, u8 size)
{
	switch (size) {
	case 4: // 32-bit swap
		return (((x) & 0x000000FF) << 24) |
				(((x) & 0x0000FF00) << 8)  |
				(((x) & 0x00FF0000) >> 8)  |
				(((x) & 0xFF000000) >> 24);

	case 2: // 16-bit swap
		return (((x) & 0x00FF) << 8) |
				(((x) & 0xFF00) >> 8);

	default:
		pr_err("Swap error: incorrect size\n");
		return x;
	}
}

static int dw_spi_write_then_read(struct dw_spi *dws, struct spi_device *spi, const struct spi_mem_op *op)
{
	u32 room, entries, sts, val, tmp = 0;
	unsigned int len;
	void *in_buf;
	const void *out_buf;

	len = 0;
	in_buf = NULL;
	out_buf = NULL;

	if (op->data.dir == SPI_MEM_DATA_OUT) {
		out_buf = op->data.buf.out;
		len = op->data.nbytes;
	}
	/*
	 * At initial stage we just pre-fill the Tx FIFO in with no rush,
	 * since native CS hasn't been enabled yet and the automatic data
	 * transmission won't start til we do that.
	 */

	/* issue write cmd */
	if (op->cmd.nbytes)
		dw_write_io_reg(dws, DW_SPI_DR, op->cmd.opcode);

	/* write addr */
	write_addr_data(dws, op);

	room = min((dws->fifo_len - readl_relaxed(dws->regs + DW_SPI_TXFLR)), (len + dws->n_bytes - 1) / dws->n_bytes);

	/* prefill the Tx FIFO */
	while (room) {
		if (len < dws->n_bytes) {
			tmp = 0xffffffff;
			memcpy(&tmp, out_buf, len);
			tmp = dw_swap(tmp, dws->n_bytes);
			dw_write_io_reg(dws, DW_SPI_DR, tmp);
			len -= len;
			out_buf += len;
		} else {
			if (dws->n_bytes == 1) {
				dw_write_io_reg(dws, DW_SPI_DR, *(u8 *)out_buf);
			}
			else if (dws->n_bytes == 2) {
				val = dw_swap(*(u16 *)out_buf, dws->n_bytes);
				dw_write_io_reg(dws, DW_SPI_DR, val);
			} else {
				val = dw_swap(*(u32 *)out_buf, dws->n_bytes);
				dw_write_io_reg(dws, DW_SPI_DR, val);
			}
			len -= dws->n_bytes;
			out_buf += dws->n_bytes;
		}
		room -= 1;
	}
	/*
	 * After setting any bit in the SER register the transmission will
	 * start automatically. We have to keep up with that procedure
	 * otherwise the CS de-assertion will happen whereupon the memory
	 * operation will be pre-terminated.
	 */
	dw_spi_set_cs(spi, false);

	/*
	* The TX FIFO entry count must never reach zero during an ongoing transmission.
	* A zero entry count indicates the TX FIFO is empty while data transmission remains
	* incomplete. This condition triggers an inherent behavior in the DesignWare SSI IP
	* core: upon detecting an empty TX FIFO, the controller immediately deasserts the
	* Chip Select (CS) line, prematurely terminating the transfer. To maintain transmission
	* integrity, software must ensure the TX FIFO always contains pending data until the
	* entire transfer completes (i.e., the length counter decrements to zero). Failure to
	* uphold this requirement will result in corrupted transactions.
	*/
	while (len) {
		entries = readl_relaxed(dws->regs + DW_SPI_TXFLR);
		if (!entries) {
			dev_err(&dws->master->dev, "CS de-assertion on Tx\n");
			pr_err("CS de-assertion on Tx\n");
			return -EIO;
		}

		room = min(dws->fifo_len - entries, (len + dws->n_bytes - 1) / dws->n_bytes);

		/* Fill remaining data to tx fifo, after enable tx transmission */
		for (; room; --room) {
			if (len < dws->n_bytes) {
				tmp = 0xffffffff;
				memcpy(&tmp, out_buf, len);
				tmp = dw_swap(tmp, dws->n_bytes);
				dw_write_io_reg(dws, DW_SPI_DR, tmp);
				len -= len;
				out_buf += len;
			} else {
				if (dws->n_bytes == 1)
					dw_write_io_reg(dws, DW_SPI_DR, *(u8 *)out_buf);
				else if (dws->n_bytes == 2) {
					val = dw_swap(*(u16 *)out_buf, dws->n_bytes);
					dw_write_io_reg(dws, DW_SPI_DR, val);
				} else {
					val = dw_swap(*(u32 *)out_buf, dws->n_bytes);
					dw_write_io_reg(dws, DW_SPI_DR, val);
				}
				len -= dws->n_bytes;
				out_buf += dws->n_bytes;
			}
		}
	}
	/*
	 * Data fetching will start automatically if the EEPROM-read mode is
	 * activated. We have to keep up with the incoming data pace to
	 * prevent the Rx FIFO overflow causing the inbound data loss.
	 */
	if (op->data.dir == SPI_MEM_DATA_IN) {
		len = op->data.nbytes;
		in_buf = op->data.buf.in;
	}

	while (len) {
		entries = readl_relaxed(dws->regs + DW_SPI_RXFLR);
		if (!entries) {
			sts = readl_relaxed(dws->regs + DW_SPI_RISR);
			if (sts & SPI_INT_RXOI) {
				dev_err(&dws->master->dev, "FIFO overflow on Rx\n");
				return -EIO;
			}
			continue;
		}

		for (; entries; --entries) {
			if (dws->n_bytes == 4) {
				if (len >= dws->n_bytes) {
					*(u32 *)in_buf = dw_read_io_reg(dws, DW_SPI_DR);
					in_buf += dws->n_bytes;
					len -=  dws->n_bytes;
				} else {
					val = dw_read_io_reg(dws, DW_SPI_DR);
					memcpy(in_buf, &val, len);
					len -=  len;
				}
			} else {
				*(u8 *)in_buf++ = dw_read_io_reg(dws, DW_SPI_DR);
				len -= dws->n_bytes;
			}
		}
	}

	return 0;
}

static inline bool dw_spi_ctlr_busy(struct dw_spi *dws)
{
	return dw_readl(dws, DW_SPI_SR) & SR_BUSY;
}

static int dw_spi_wait_mem_op_done(struct dw_spi *dws)
{
	int retry = SPI_WAIT_RETRIES;
	struct spi_delay delay;
	unsigned long ns, us;
	u32 nents;

	nents = dw_readl(dws, DW_SPI_TXFLR);
	ns = NSEC_PER_SEC / dws->current_freq * nents;
	ns *= dws->n_bytes * BITS_PER_BYTE;
	if (ns <= NSEC_PER_USEC) {
		delay.unit = SPI_DELAY_UNIT_NSECS;
		delay.value = ns;
	} else {
		us = DIV_ROUND_UP(ns, NSEC_PER_USEC);
		delay.unit = SPI_DELAY_UNIT_USECS;
		delay.value = clamp_val(us, 0, USHRT_MAX);
	}

	while (dw_spi_ctlr_busy(dws) && retry--)
		spi_delay_exec(&delay, NULL);

	if (retry < 0) {
		dev_err(&dws->master->dev, "Mem op hanged up\n");
		return -EIO;
	}

	return 0;
}

static void dw_spi_stop_mem_op(struct dw_spi *dws, struct spi_device *spi)
{
	spi_enable_chip(dws, 0);
	dw_spi_set_cs(spi, true);
	spi_enable_chip(dws, 1);
}


#ifdef CONFIG_SPI_DW_DMA
int spi_dma_setup(struct dw_spi *dws, const struct spi_mem_op *op)
{
	u16 imr;
	int ret = 0;
	struct dw_spi_mmio *dwsmmio = container_of(dws, struct dw_spi_mmio, dws);
	u32 len = 0;
	enum dma_data_direction dir;
	u8 *buf = NULL;
	dma_addr_t dma_buffer;
	struct dma_slave_config dma_conf;
	u16 ctrl_len = 0;

	memset(&dma_conf, 0x0, sizeof(dma_conf));
	ret = dma_set_mask(dwsmmio->dev, DMA_BIT_MASK(64));
	if (ret) {
		pr_info("no usable DMA configuration\n");
		ret = -ENOMEM;
	}

	if (op->data.dir == SPI_MEM_DATA_IN) {
		buf = (u8 *)dws->rx;
		len = dws->rx_len;
		dir = DMA_FROM_DEVICE;
		imr |= SPI_INT_RXUI | SPI_INT_RXOI;

		dma_conf.direction = DMA_DEV_TO_MEM;
		dma_conf.src_addr = dws->dma_addr;
		dma_conf.src_maxburst = dws->rxburst;
		dma_conf.dst_addr_width = dws->n_bytes;
		dma_conf.src_addr_width = dws->reg_io_width;
	} else {
		buf = (u8 *)dws->tx + PRE_FILL_SIZE;
		len = dws->tx_len - PRE_FILL_SIZE;
		dir = DMA_TO_DEVICE;
		imr = SPI_INT_TXOI;

		dma_conf.direction = DMA_MEM_TO_DEV;
		dma_conf.dst_addr = dws->dma_addr;

		if (dws->n_bytes == 1)
			ctrl_len = op->cmd.nbytes + op->addr.nbytes;
		else
			ctrl_len = 2;
		dma_conf.dst_maxburst = dws->fifo_len - ctrl_len - PRE_FILL_SIZE / dws->n_bytes;/*dws->txburst;*/
		dma_conf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		dma_conf.dst_addr_width = dws->n_bytes;
		dma_conf.device_fc = false;
	}

	dma_buffer = dma_map_single(dwsmmio->dev, buf, len, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dwsmmio->dev, dma_buffer)) {
		dev_err(dwsmmio->dev, "Failed to map DMA buffer\n");
		ret = -EIO;
	}

	if (op->data.dir == SPI_MEM_DATA_OUT) {
		dma_conf.src_addr = dma_buffer;
		dmaengine_slave_config(dws->txchan, &dma_conf);
	} else {
		dma_conf.dst_addr = dma_buffer;
		dmaengine_slave_config(dws->rxchan, &dma_conf);
	}

	sg_init_one(&dws->sgl, buf, len);
	dws->res = dma_map_sg(dwsmmio->dev, &dws->sgl, 1, dir);
	if (dws->res == 0) {
		pr_info("Failed to map sg list. res=%lu\n", dws->res);
		return -ENXIO;
	}

	if (op->data.dir == SPI_MEM_DATA_IN)
		ret = dw_spi_dma_submit_rx(dws, &dws->sgl, dws->res);
	else
		ret = dw_spi_dma_submit_tx(dws, &dws->sgl, dws->res);

	if (ret)
		goto err_clear_dmac;

	spi_umask_intr(dws, imr);
	reinit_completion(&dws->dma_completion);
	dws->transfer_handler = dw_spi_dma_transfer_handler;
	return 0;

err_clear_dmac:
	dw_writel(dws, DW_SPI_DMACR, 0);
	return ret;
}

int dw_spinor_dma_transfer(struct dw_spi *dws, struct spi_device *spi, const struct spi_mem_op *op)
{
	int ret = 0;
	u32 room = 0;
	u8 *buf = NULL;
	u16 dma_ctrl;
	u32 len;
	struct dw_spi_mmio *dwsmmio = container_of(dws, struct dw_spi_mmio, dws);

	if (op->cmd.nbytes)
		dw_write_io_reg(dws, DW_SPI_DR, op->cmd.opcode);

	write_addr_data(dws, op);

	if (op->data.dir == SPI_MEM_DATA_OUT) {
		room = min((dws->fifo_len - dw_readl(dws, DW_SPI_TXFLR)), PRE_FILL_SIZE / dws->n_bytes);
		buf = (u8 *)dws->tx;
		while (room) {
			if (dws->n_bytes == 1)
				dw_write_io_reg(dws, DW_SPI_DR, *buf);
			else
				dw_write_io_reg(dws, DW_SPI_DR, *(u32 *)buf);

			buf += dws->n_bytes;
			room -= 1;
		}
		dws->tx_len -= (buf - (u8 *)dws->tx);
		dws->tx = buf;
	}

	/* Submit the DMA Rx/Tx transfer if required */
	if (op->data.dir == SPI_MEM_DATA_IN)  {
		dma_async_issue_pending(dws->rxchan);
		dma_ctrl = SPI_DMA_RDMAE;
	} else {
		dma_async_issue_pending(dws->txchan);
		dma_ctrl = SPI_DMA_TDMAE;
		dw_writel(dws, DW_SPI_DMATDLR, ((dw_readl(dws, DW_SPI_TXFLR)) / 2 - 1));
	}
	dw_writel(dws, DW_SPI_DMACR, dma_ctrl);

	dw_spi_set_cs(spi, false);
	if (op->data.dir == SPI_MEM_DATA_IN)
		len = dws->rx_len;
	else
		len = dws->tx_len;

	ret = dw_spi_dma_wait(dws, len, dws->current_freq);
	dma_unmap_sg(dwsmmio->dev, &dws->sgl, 1, DMA_BIDIRECTIONAL);
	dw_writel(dws, DW_SPI_DMACR, 0);
	return ret;
}
#else /* !CONFIG_SPI_DW_DMA */
/* Link-only stubs: unreachable at runtime, can_dma() rejects chan-less DMA */
int spi_dma_setup(struct dw_spi *dws, const struct spi_mem_op *op)
{
	return -EOPNOTSUPP;
}

int dw_spinor_dma_transfer(struct dw_spi *dws, struct spi_device *spi,
			   const struct spi_mem_op *op)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_SPI_DW_DMA */

/*
 * The SPI memory operation implementation below is the best choice for the
 * devices, which are selected by the native chip-select lane. It's
 * specifically developed to workaround the problem with automatic chip-select
 * lane toggle when there is no data in the Tx FIFO buffer. Luckily the current
 * SPI-mem core calls exec_op() callback only if the GPIO-based CS is
 * unavailable.
 */

static int dw_spi_exec_mem_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct dw_spi *dws = spi_controller_get_devdata(mem->spi->controller);
	struct dw_spi_cfg cfg;
	int ret;
	unsigned long flags;
	bool support_dma = false;

	/*
	 * Collect the outbound data into a single buffer to speed the
	 * transmission up at least on the initial stage.
	 */
	ret = dw_spi_init_mem_buf(dws, op);
	if (ret)
		return ret;

	/*
	 * DW SPI EEPROM-read mode is required only for the SPI memory Data-IN
	 * operation. Transmit-only mode is suitable for the rest of them.
	 */
	cfg.dfs = 8;
	cfg.freq = clamp(mem->spi->max_speed_hz, 0U, dws->max_mem_freq);

	if (op->data.dir == SPI_MEM_DATA_IN) {
		cfg.tmode = SPI_TMOD_EPROMREAD;
		cfg.ndf = op->data.nbytes;
	} else {
		cfg.tmode = SPI_TMOD_TO;
	}

	spi_enable_chip(dws, 0);

	dw_spi_update_config(dws, mem->spi, &cfg, op);

	spi_mask_intr(dws, 0xff);

	support_dma = can_dma(dws, op);
	if (support_dma && spi_dma_setup(dws, op)) {
		dev_err(&dws->master->dev, "DW SPINOR DMA setup failed\n");
		support_dma = false;
	}

	spi_enable_chip(dws, 1);

	/*
	 * DW APB SSI controller has very nasty peculiarities. First originally
	 * (without any vendor-specific modifications) it doesn't provide a
	 * direct way to set and clear the native chip-select signal. Instead
	 * the controller asserts the CS lane if Tx FIFO isn't empty and a
	 * transmission is going on, and automatically de-asserts it back to
	 * the high level if the Tx FIFO doesn't have anything to be pushed
	 * out. Due to that a multi-tasking or heavy IRQs activity might be
	 * fatal, since the transfer procedure preemption may cause the Tx FIFO
	 * getting empty and sudden CS de-assertion, which in the middle of the
	 * transfer will most likely cause the data loss. Secondly the
	 * EEPROM-read or Read-only DW SPI transfer modes imply the incoming
	 * data being automatically pulled in into the Rx FIFO. So if the
	 * driver software is late in fetching the data from the FIFO before
	 * it's overflown, new incoming data will be lost. In order to make
	 * sure the executed memory operations are CS-atomic and to prevent the
	 * Rx FIFO overflow we have to disable the local interrupts so to block
	 * any preemption during the subsequent IO operations.
	 *
	 * Note. At some circumstances disabling IRQs may not help to prevent
	 * the problems described above. The CS de-assertion and Rx FIFO
	 * overflow may still happen due to the relatively slow system bus or
	 * CPU not working fast enough, so the write-then-read algo implemented
	 * here just won't keep up with the SPI bus data transfer. Such
	 * situation is highly platform specific and is supposed to be fixed by
	 * manually restricting the SPI bus frequency using the
	 * dws->max_mem_freq parameter.
	 */
	if (support_dma) {
		ret = dw_spinor_dma_transfer(dws, mem->spi, op);
		if (ret)
			dev_err(&dws->master->dev, "DW SPINOR DMA transfer failed\n");
		goto out;
	}
	else {
		local_irq_save(flags);
		preempt_disable();
		ret = dw_spi_write_then_read(dws, mem->spi, op);
		local_irq_restore(flags);
		preempt_enable();
	}
	/*
	 * Wait for the operation being finished and check the controller
	 * status only if there hasn't been any run-time error detected. In the
	 * former case it's just pointless. In the later one to prevent an
	 * additional error message printing since any hw error flag being set
	 * would be due to an error detected on the data transfer.
	 */
	if (unlikely(ret))
		ret = dw_spi_check_status(dws, true);
	else
		ret = dw_spi_wait_mem_op_done(dws);

out:
	if (support_dma)
		dws->dma_ops->dma_stop(dws);
	dw_spi_stop_mem_op(dws, mem->spi);
	dw_spi_free_mem_buf(dws);
	spi_enable_chip(dws, 0);

	return ret;
}

/*
 * Initialize the default memory operations if a glue layer hasn't specified
 * custom ones. Direct mapping operations will be preserved anyway since DW SPI
 * controller doesn't have an embedded dirmap interface. Note the memory
 * operations implemented in this driver is the best choice only for the DW APB
 * SSI controller with standard native CS functionality. If a hardware vendor
 * has fixed the automatic CS assertion/de-assertion peculiarity, then it will
 * be safer to use the normal SPI-messages-based transfers implementation.
 */
static void dw_spi_init_mem_ops(struct dw_spi *dws)
{
	if (!dws->mem_ops.exec_op && !(dws->caps & DW_SPI_CAP_CS_OVERRIDE) &&
	    !dws->set_cs) {
		dws->mem_ops.adjust_op_size = dw_spi_adjust_mem_op_size;
		dws->mem_ops.supports_op = dw_spi_supports_mem_op;
		dws->mem_ops.exec_op = dw_spi_exec_mem_op;
		if (!dws->max_mem_freq)
			dws->max_mem_freq = dws->max_freq;
	}
}

/* This may be called twice for each spi dev */
static int dw_spi_setup(struct spi_device *spi)
{
	struct dw_spi *dws = spi_controller_get_devdata(spi->controller);
	struct chip_data *chip;

	/* Only alloc on first setup */
	chip = spi_get_ctldata(spi);
	if (!chip) {
		struct dw_spi *dws = spi_controller_get_devdata(spi->controller);
		u32 rx_sample_dly_ns;

		chip = kzalloc(sizeof(struct chip_data), GFP_KERNEL);
		if (!chip)
			return -ENOMEM;
		spi_set_ctldata(spi, chip);
		/* Get specific / default rx-sample-delay */
		if (device_property_read_u32(&spi->dev,
					     "rx-sample-delay-ns",
					     &rx_sample_dly_ns) != 0)
			/* Use default controller value */
			rx_sample_dly_ns = dws->def_rx_sample_dly_ns;
		chip->rx_sample_dly = DIV_ROUND_CLOSEST(rx_sample_dly_ns,
							NSEC_PER_SEC /
							dws->max_freq);
	}

	/*
	 * Update CR0 data each time the setup callback is invoked since
	 * the device parameters could have been changed, for instance, by
	 * the MMC SPI driver or something else.
	 */
	chip->cr0 = dw_spi_prepare_cr0(dws, spi);

	return 0;
}

static void dw_spi_cleanup(struct spi_device *spi)
{
	struct chip_data *chip = spi_get_ctldata(spi);

	kfree(chip);
	spi_set_ctldata(spi, NULL);
}

/* Restart the controller, disable all interrupts, clean rx fifo */
static void spi_hw_init(struct device *dev, struct dw_spi *dws)
{
	spi_reset_chip(dws);

	/*
	 * Try to detect the FIFO depth if not set by interface driver,
	 * the depth could be from 2 to 256 from HW spec
	 */
	if (!dws->fifo_len) {
		u32 fifo;

		for (fifo = 1; fifo < 256; fifo++) {
			dw_writel(dws, DW_SPI_TXFTLR, fifo);
			if (fifo != dw_readl(dws, DW_SPI_TXFTLR))
				break;
		}
		dw_writel(dws, DW_SPI_TXFTLR, 0);

		dws->fifo_len = (fifo == 1) ? 0 : fifo;
		dev_dbg(dev, "Detected FIFO size: %u bytes\n", dws->fifo_len);
	}
#ifndef DW_VERSION_4_04
	/* enable HW fixup for explicit CS deselect for Amazon's alpine chip */
	if (dws->caps & DW_SPI_CAP_CS_OVERRIDE)
		dw_writel(dws, DW_SPI_CS_OVERRIDE, 0xF);
#endif
}

int dw_spi_add_host(struct device *dev, struct dw_spi *dws)
{
	struct spi_controller *master;
	int ret;

	if (!dws)
		return -EINVAL;

	master = spi_alloc_master(dev, 0);
	if (!master)
		return -ENOMEM;

	dws->master = master;
	dws->dma_addr = (dma_addr_t)(dws->paddr + DW_SPI_DR);

	spi_controller_set_devdata(master, dws);

	/* Basic HW init */
	spi_hw_init(dev, dws);

	if (strcmp(dev_name(dev), "10000000.cvi-spif") == 0)
		ret = request_irq(dws->irq, dw_spinor_irq, IRQF_SHARED, dev_name(dev),
              master);
	else
		ret = request_irq(dws->irq, dw_spi_irq, IRQF_SHARED, dev_name(dev),
              master);
	if (ret < 0 && ret != -ENOTCONN) {
		dev_err(dev, "can not get IRQ\n");
		goto err_free_master;
	}

	dw_spi_init_mem_ops(dws);

	master->use_gpio_descriptors = true;
	//master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LOOP;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LOOP |
			    SPI_TX_QUAD | SPI_RX_QUAD;
	/* Quad data phases exist on quad-capable (v5) instances only.
	 * Dual is not advertised: only the 1-1-4 shape is verified.
	 */
	if (!dws->quad_capable)
		master->mode_bits &= ~(SPI_TX_QUAD | SPI_RX_QUAD);
	master->bits_per_word_mask =  SPI_BPW_RANGE_MASK(4, 16);
	master->bus_num = dws->bus_num;
	master->num_chipselect = dws->num_cs;
	master->setup = dw_spi_setup;
	master->cleanup = dw_spi_cleanup;
	if (dws->set_cs)
		master->set_cs = dws->set_cs;
	else
		master->set_cs = dw_spi_set_cs;
	master->transfer_one = dw_spi_transfer_one;
	master->handle_err = dw_spi_handle_err;
	if (dws->mem_ops.exec_op)
		master->mem_ops = &dws->mem_ops;
	master->max_speed_hz = dws->max_freq;
	master->dev.of_node = dev->of_node;
	master->dev.fwnode = dev->fwnode;
	master->flags = SPI_MASTER_GPIO_SS;
	master->auto_runtime_pm = true;

	/* Get default rx sample delay */
	device_property_read_u32(dev, "rx-sample-delay-ns",
				 &dws->def_rx_sample_dly_ns);

	if (dws->dma_ops && dws->dma_ops->dma_init) {
		ret = dws->dma_ops->dma_init(dev, dws);
		if (ret) {
			dev_warn(dev, "DMA init failed:%d\n", ret);
		} else {
			master->can_dma = dws->dma_ops->can_dma;
			master->flags |= SPI_CONTROLLER_MUST_TX;
		}
	}

	ret = spi_register_controller(master);
	if (ret) {
		dev_err(&master->dev, "problem registering spi master\n");
		goto err_dma_exit;
	}

	dw_spi_debugfs_init(dws);
	return 0;

err_dma_exit:
	if (dws->dma_ops && dws->dma_ops->dma_exit)
		dws->dma_ops->dma_exit(dws);
	spi_enable_chip(dws, 0);
	free_irq(dws->irq, master);
err_free_master:
	spi_controller_put(master);
	return ret;
}
EXPORT_SYMBOL_GPL(dw_spi_add_host);

void dw_spi_remove_host(struct dw_spi *dws)
{
	dw_spi_debugfs_remove(dws);

	spi_unregister_controller(dws->master);

	if (dws->dma_ops && dws->dma_ops->dma_exit)
		dws->dma_ops->dma_exit(dws);

	spi_shutdown_chip(dws);

	free_irq(dws->irq, dws->master);
}
EXPORT_SYMBOL_GPL(dw_spi_remove_host);

int dw_spi_suspend_host(struct dw_spi *dws)
{
	int ret;

	ret = spi_controller_suspend(dws->master);
	if (ret)
		return ret;

	spi_shutdown_chip(dws);
	return 0;
}
EXPORT_SYMBOL_GPL(dw_spi_suspend_host);

int dw_spi_resume_host(struct dw_spi *dws)
{
	spi_hw_init(&dws->master->dev, dws);
	return spi_controller_resume(dws->master);
}
EXPORT_SYMBOL_GPL(dw_spi_resume_host);

MODULE_AUTHOR("Feng Tang <feng.tang@intel.com>");
MODULE_DESCRIPTION("Driver for DesignWare SPI controller core");
MODULE_LICENSE("GPL v2");
