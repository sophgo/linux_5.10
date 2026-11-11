// SPDX-License-Identifier: GPL-2.0+
/*
 * 8250_dma.c - DMA Engine API support for 8250.c
 *
 * Copyright (C) 2013 Intel Corporation
 */
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <linux/dma-mapping.h>
#include <linux/printk.h>

#include "8250.h"

static void __dma_tx_complete(void *param)
{
	struct uart_8250_port	*p = param;
	struct uart_8250_dma	*dma = p->dma;
	struct circ_buf		*xmit = &p->port.state->xmit;
	unsigned long	flags;
	int		ret;

	dma_sync_single_for_cpu(dma->txchan->device->dev, dma->tx_addr,
				UART_XMIT_SIZE, DMA_TO_DEVICE);

	spin_lock_irqsave(&p->port.lock, flags);

	dma->tx_running = 0;

	xmit->tail += dma->tx_size;
	xmit->tail &= UART_XMIT_SIZE - 1;
	p->port.icount.tx += dma->tx_size;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&p->port);

	ret = serial8250_tx_dma(p);
	if (ret)
		serial8250_set_THRI(p);

	spin_unlock_irqrestore(&p->port.lock, flags);
}

static void __dma_rx_complete(void *param)
{

	struct uart_8250_port	*p = param;
	struct uart_8250_dma	*dma = p->dma;
	struct tty_port		*tty_port = &p->port.state->port;
	struct dma_tx_state	state;
	unsigned int new_pos;
	int	count;
	unsigned long	flags;

	if(!dma->rxchan) {
		pr_err("ttyS%d %s, receive NULL RXCHAN!!!",  serial_index(&p->port), __func__);
		return;
	}
	spin_lock_irqsave(&p->port.lock, flags);

	uart_flush_timer_reset(p);

	new_pos = 0;
	count = 0;
	//memset(dma->recv_buf, 0x0, dma->rx_size);
	dmaengine_tx_status(dma->rxchan, dma->rx_cookie, &state);

	if (state.residue > 0 && state.residue <= dma->rx_size)
			new_pos = dma->rx_size - state.residue;
	else {
		pr_info("ttyS%d %s get wrong residue %u (=0 or > rx_size)\n",  serial_index(&p->port), __func__, state.residue);
	}

	/* start_rx_dma is only useful while dma period size < buffer_size
	* it is used to notify valid data start to receive from sysDMA
	* and only update at first time to receive valid data
	*/
	if (new_pos > 0)
		dma->start_rx_dma = true;

	//pr_err("ttyS%d %s, get dma->rx!!! dma->rx_buf1:%s ",  serial_index(&p->port), __func__,(char *)(dma->rx_buf));
	int tmp_count;
	if (dma->start_rx_dma == true) {
		if (new_pos >= dma->rx_head_pos) {
			count = new_pos - dma->rx_head_pos;
			tmp_count = tty_insert_flip_string(tty_port, (dma->rx_buf+dma->rx_head_pos), count);
			if(tmp_count != count){
				pr_err("tty_insert_flip_string error, tmp_count=%d, count=%d, LINE=%d\n", tmp_count, count, __LINE__);
			}
		} else {
			int len1, len2;

			len1 = dma->rx_size - dma->rx_head_pos;
			len2 = new_pos;
			count = dma->rx_size + new_pos - dma->rx_head_pos;
			tmp_count = tty_insert_flip_string(tty_port, (dma->rx_buf+dma->rx_head_pos), len1);
			if(tmp_count != len1){
				pr_err("tty_insert_flip_string error, tmp_count=%d, len1=%d, LINE=%d\n", tmp_count, len1, __LINE__);
			}
			tmp_count = tty_insert_flip_string(tty_port,  dma->rx_buf, len2);
			if(tmp_count != len2){
				pr_err("tty_insert_flip_string error, tmp_count=%d, len2=%d, LINE=%d\n", tmp_count, len2, __LINE__);
			}
		}
		dma->rx_head_pos = new_pos;
		if (count > dma->rx_size) {
			pr_err("ttyS%d %s, Wrong received data size %d\n", serial_index(&p->port), __func__, count);
		}
	} else
		count = 0;

	p->port.icount.rx += count;
	//pr_err("ttyS%d %s, get dma->rx!!! dma->rx_buf2:%s ",  serial_index(&p->port), __func__,(char *)(dma->rx_buf+dma->rx_head_pos));

	tty_flip_buffer_push(tty_port);
	spin_unlock_irqrestore(&p->port.lock, flags);
}

int serial8250_tx_dma(struct uart_8250_port *p)
{
	struct uart_8250_dma		*dma = p->dma;
	struct circ_buf			*xmit = &p->port.state->xmit;
	struct dma_async_tx_descriptor	*desc;
	int ret;

	if (dma->tx_running)
		return 0;

	if (uart_tx_stopped(&p->port) || uart_circ_empty(xmit)) {
		/* We have been called from __dma_tx_complete() */
		serial8250_rpm_put_tx(p);
		return 0;
	}

	dma->tx_size = CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE);

	desc = dmaengine_prep_slave_single(dma->txchan,
					   dma->tx_addr + xmit->tail,
					   dma->tx_size, DMA_MEM_TO_DEV,
					   DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		ret = -EBUSY;
		goto err;
	}

	dma->tx_running = 1;
	desc->callback = __dma_tx_complete;
	desc->callback_param = p;

	dma->tx_cookie = dmaengine_submit(desc);

	dma_sync_single_for_device(dma->txchan->device->dev, dma->tx_addr,
				   UART_XMIT_SIZE, DMA_TO_DEVICE);

	dma_async_issue_pending(dma->txchan);
	if (dma->tx_err) {
		dma->tx_err = 0;
		serial8250_clear_THRI(p);
	}
	return 0;
err:
	dma->tx_err = 1;
	return ret;
}

int serial8250_rx_dma(struct uart_8250_port *p)
{
	struct uart_8250_dma		*dma = p->dma;
	if (dma->rx_running)
		return 0;

	dma->rx_running = 1;

	return 0;
}

void serial8250_rx_dma_flush(struct uart_8250_port *p)
{
	struct uart_8250_dma *dma = p->dma;

	if (dma->rx_running) {
		dmaengine_pause(dma->rxchan);
		__dma_rx_complete(p);
		dmaengine_terminate_async(dma->rxchan);
	}
}
EXPORT_SYMBOL_GPL(serial8250_rx_dma_flush);

void serial8250_rx_dma_flush2(struct uart_8250_port *p)
{
	struct uart_8250_dma *dma = p->dma;
	if (dma->rx_running) {

		if (!dma->rxchan)
			return;
		dma->rxchan->immediate_resume = true;
		dmaengine_pause(dma->rxchan);
		dma->rxchan->immediate_resume = false;
	}
}
EXPORT_SYMBOL_GPL(serial8250_rx_dma_flush2);


int serial8250_request_dma(struct uart_8250_port *p)
{
	struct uart_8250_dma	*dma = p->dma;
	phys_addr_t rx_dma_addr = dma->rx_dma_addr ?
				  dma->rx_dma_addr : p->port.mapbase;
	phys_addr_t tx_dma_addr = dma->tx_dma_addr ?
				  dma->tx_dma_addr : p->port.mapbase;
	dma_cap_mask_t		mask;
	struct dma_slave_caps	caps;
	int			ret;
	struct dma_async_tx_descriptor	*desc;
	const char *str;

	//pr_err("ttyS%d, %s\n",serial_index(&p->port), __func__);
	/* Default slave configuration parameters */
	dma->rxconf.direction		= DMA_DEV_TO_MEM;
	dma->rxconf.src_addr_width	= DMA_SLAVE_BUSWIDTH_1_BYTE;
	dma->rxconf.src_addr		= rx_dma_addr + UART_RX;
	dma->start_rx_dma		= false;

	dma->txconf.direction		= DMA_MEM_TO_DEV;
	dma->txconf.dst_addr_width	= DMA_SLAVE_BUSWIDTH_1_BYTE;
	dma->txconf.dst_addr		= tx_dma_addr + UART_TX;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* Get a channel for RX */
	dma->rxchan = dma_request_slave_channel_compat(mask,
						       dma->fn, dma->rx_param,
						       p->port.dev, "rx");

	if (!dma->rxchan) {
		pr_err("ttyS%d, get NULL RX chan\n", serial_index(&p->port));
		ret = -EINVAL;
		goto release_rx;
	}

	/* 8250 rx dma requires dmaengine driver to support pause/terminate */
	ret = dma_get_slave_caps(dma->rxchan, &caps);
	if (ret)
		goto release_rx;
	if (!caps.cmd_pause || !caps.cmd_terminate ||
	    caps.residue_granularity == DMA_RESIDUE_GRANULARITY_DESCRIPTOR) {
		ret = -EINVAL;
		goto release_rx;
	}

	dmaengine_slave_config(dma->rxchan, &dma->rxconf);

	/* Get a channel for TX */
	dma->txchan = dma_request_slave_channel_compat(mask,
						       dma->fn, dma->tx_param,
						       p->port.dev, "tx");
	if (!dma->txchan){
		pr_info("ttyS%d, get NULL TX chan\n", serial_index(&p->port));
		goto setup_rx;
	}
	
	/* 8250 tx dma requires dmaengine driver to support terminate */
	ret = dma_get_slave_caps(dma->txchan, &caps);
	if (ret)
		goto err;
	if (!caps.cmd_terminate) {
		ret = -EINVAL;
		goto err;
	}

	dmaengine_slave_config(dma->txchan, &dma->txconf);

setup_rx:
	/* RX buffer */
	dma->rx_size = DMA_RX_SIZE;

	dma->rx_buf = dma_alloc_coherent(dma->rxchan->device->dev, dma->rx_size,
					&dma->rx_addr, GFP_DMA | GFP_KERNEL);
	//pr_err("ttyS%d, rx_address=0x%llx, dma->rx_size=%ld  dma->rx_buf %s\n", serial_index(&p->port), dma->rx_addr, dma->rx_size,dma->rx_buf);
	if (!dma->rx_buf) {
		ret = -ENOMEM;
		goto err;
	}
	dma->rx_head_pos = 0;

	desc = dmaengine_prep_dma_cyclic(dma->rxchan, dma->rx_addr,
						dma->rx_size, 64, DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	if (!desc)
		return -EBUSY;

	desc->callback = __dma_rx_complete;
	desc->callback_param = p;
	dma->rx_cookie = dmaengine_submit(desc);

	/* TX buffer */
	if (dma->txchan) {
		//pr_err("ttyS%d, tx_address=0x%llx, dma->tx_size=%ld\n", serial_index(&p->port), dma->rx_addr, dma->rx_size);
		dma->tx_addr = dma_map_single(dma->txchan->device->dev,
						p->port.state->xmit.buf,
						UART_XMIT_SIZE,
						DMA_TO_DEVICE);
		if (dma_mapping_error(dma->txchan->device->dev, dma->tx_addr)) {
			dma_free_coherent(dma->rxchan->device->dev, dma->rx_size,
					  dma->rx_buf, dma->rx_addr);
			ret = -ENOMEM;
			goto err;
		}
	}

	dma->rx_running = 1;

	dma_async_issue_pending(dma->rxchan);
	
	return 0;
err:
	dma_release_channel(dma->txchan);
release_rx:
	dma_release_channel(dma->rxchan);
	return ret;
}
EXPORT_SYMBOL_GPL(serial8250_request_dma);


void serial8250_release_dma(struct uart_8250_port *p)
{
	struct uart_8250_dma *dma = p->dma;
	
	if (serial_index(&p->port) != 0)
		pr_info("ttyS%d %s\n", serial_index(&p->port), __func__);

	if (!dma)
		return;

	/* Release RX resources */
	if (dma->rxchan) {
		dmaengine_terminate_sync(dma->rxchan);
		dma_free_coherent(dma->rxchan->device->dev, dma->rx_size, dma->rx_buf,
			  	dma->rx_addr);
		dma_release_channel(dma->rxchan);
		dma->rxchan = NULL;
		dma->rx_head_pos = 0;
	}
	/* Release TX resources */
	if (dma->txchan) {
		dmaengine_terminate_sync(dma->txchan);
		dma_unmap_single(dma->txchan->device->dev, dma->tx_addr,
			 	UART_XMIT_SIZE, DMA_TO_DEVICE);
		dma_release_channel(dma->txchan);
		dma->txchan = NULL;
		dma->tx_running = 0;
	}
	dev_dbg_ratelimited(p->port.dev, "dma channels released\n");
}
EXPORT_SYMBOL_GPL(serial8250_release_dma);
