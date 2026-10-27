/* SPDX-License-Identifier: GPL-2.0-or-later
 * I2S driver on CVITEK CV1835
 *
 * Copyright 2018 CVITEK
 *
 * Author: EthanChen
 *
 *
 */
#ifndef __CVITEK_DW_HDMI_I2S_H__
#define __CVITEK_DW_HDMI_I2S_H__

#define DIV_HSP_CLK_HDMI_AUD 0x25C // 0x2810225c  clkgen

#define DWI2S_FIFO_FLUSH_VAL    0x1
#define DWI2S_DEFAULT_FIFO_LEVEL    0x3

#define DMAEN_TXBLOCK_MASK 0x00020000
#define DMAEN_RXBLOCK_MASK 0x00010000
#define DMAEN_TXBLOCK      (0x1 << 17)
#define DMAEN_RXBLOCK      (0x1 << 16)
#define DMADIS_TXBLOCK     (0x0 << 17)
#define DMADIS_RXBLOCK     (0x0 << 16)

#define DW_NO_RESOLUTION   0x0
#define DW_12_RESOLUTION   0x1
#define DW_16_RESOLUTION   0x2
#define DW_20_RESOLUTION   0x3
#define DW_24_RESOLUTION   0x4
#define DW_32_RESOLUTION   0x5

#define DW_AUD_ENABLE      (0x1 << 6)

#define CLK_SCLKG_MASK 0x00000007
#define CLK_WSS_MASK   0x00000018
#define SCLKG_SET(x)   (((x) << 0) & CLK_SCLKG_MASK)
#define WSS_SET(x)     (((x) << 3) & CLK_WSS_MASK)

/* define value of each configuration of register I2S_INT */
#define DWI2S_INT_RXDA                (0x1 << 0) /* RX FIFO data available interrupt status */
#define DWI2S_INT_RXFO                (0x1 << 1) /* RX FIFO overflow interrupt status */
#define DWI2S_INT_TXFE                (0x1 << 4) /* RX FIFO empty interrupt status */
#define DWI2S_INT_TXFO                (0x1 << 5) /* RX FIFO overflow interrupt status */

/* common register offset */
#define DWI2S_HDMI_EN                     0x00 // DW_apb_i2s Enable Register
#define DWI2S_HDMI_RX_BLOCK_EN            0x04 // I2S Receiver Block Enable Register
#define DWI2S_HDMI_TX_BLOCK_EN            0x08 // I2S Transmitter Block Enable Register
#define DWI2S_HDMI_CLK_EN                 0x0c // Clock Enable Register
#define DWI2S_HDMI_CLK_CFG                0x10 // Clock Configuration Register
#define DWI2S_HDMI_RX_FIFO_RESET          0x14 // Receiver Block FIFO Reset Register
#define DWI2S_HDMI_TX_FIFO_RESET          0x18 // Transmitter Block FIFO Reset Register
#define DWI2S_HDMI_I2S_SR                 0x1C // Status Register

#define DWI2S_HDMI_LRBR_THR(x)            (0x20 + 0x40 * (x)) // Left Receive/Transmit Buffer Register x
#define DWI2S_HDMI_RRBR_THR(x)            (0x24 + 0x40 * (x)) //  Right Transmit Holding Register x /  This specifies the Right Transmit Holding Register.

#define DWI2S_HDMI_RERX(x)                (0x28 + 0x40 * (x)) // Receive Enable Register x
#define DWI2S_HDMI_TERX(x)                (0x2C + 0x40 * (x)) // Transmit Enable Register x
#define DWI2S_HDMI_RCRX(x)                (0x30 + 0x40 * (x)) // Receive Configuration Register x
#define DWI2S_HDMI_TCRX(x)                (0x34 + 0x40 * (x)) // Transmit Configuration Register x

#define DWI2S_HDMI_ISRX(x)                (0x38 + 0x40 * (x)) // Interrupt status Register x
#define DWI2S_HDMI_IMRX(x)                (0x3C + 0x40 * (x)) // Interrupt Mask Register x
#define DWI2S_HDMI_RORX(x)                (0x40 + 0x40 * (x)) // Receive Overrun Register x
#define DWI2S_HDMI_TORX(x)                (0x44 + 0x40 * (x)) // Transmit Overrun Register x
#define DWI2S_HDMI_RFCRX(x)               (0x48 + 0x40 * (x)) // Receive FIFO Configuration Register x
#define DWI2S_HDMI_TFCRX(x)               (0x4C + 0x40 * (x)) // Transmit FIFO Configuration Register x
#define DWI2S_HDMI_RFFX(x)                (0x50 + 0x40 * (x)) // Receive FIFO Flush Register x
#define DWI2S_HDMI_TFFX(x)                (0x54 + 0x40 * (x)) // Transmit FIFO Flush Register x

#define DWI2S_HDMI_RXDMA                  0x1C0 // Receiver Block DMA Register.
#define DWI2S_HDMI_RRXDMA                 0x1C4 // Reset Receiver Block DMA Register.
#define DWI2S_HDMI_TXDMA                  0x1C8 // Transmitter Block DMA Register
#define DWI2S_HDMI_RTXDMA                 0x1CC // Reset Transmitter Block DMA Register

#define DWI2S_HDMI_COMP_PARAM_2           0x1F0 // Component Parameter Register 2
#define DWI2S_HDMI_COMP_PARAM_1           0x1F4 // Component Parameter Register 1
#define DWI2S_HDMI_COMP_VERSION           0x1F8 // I2S Component Version Register
#define DWI2S_HDMI_COMP_TYPE              0x1FC // I2S Component Type Register

#define DWI2S_HDMI_DMACR                  0x200 // DMA Control Register

#define DWI2S_HDMI_RXDMA_CHX(x)           (0x204 + 0x4 * x) // Receiver Block DMA Register
#define DWI2S_HDMI_TXDMA_CHX(x)           (0x214 + 0x4 * x) // Receiver Block DMA Register
#define DWI2S_HDMI_SLOTX(x)               (0x224 + 0x4 * x) // This specifies the Receiver/Transmit Slot x Buffer Register

#define HDMI_I2S_CHANNEL_MAX  4

#ifdef CONFIG_PM_SLEEP
struct cvi_hdmi_dwi2s_reg_context {
	u32 ier;
	u32 irer;
	u32 iter;
	u32 cer;
	u32 ccr;
	u32 rer[HDMI_I2S_CHANNEL_MAX];
	u32 ter[HDMI_I2S_CHANNEL_MAX];
	u32 rcr[HDMI_I2S_CHANNEL_MAX];
	u32 tcr[HDMI_I2S_CHANNEL_MAX];
	u32 rfcr[HDMI_I2S_CHANNEL_MAX];
	u32 tfcr[HDMI_I2S_CHANNEL_MAX];
	u32 rff[HDMI_I2S_CHANNEL_MAX];
	u32 tff[HDMI_I2S_CHANNEL_MAX];
	u32 dmacr;
};
#endif


#endif /*__CVITEK_DW_HDMI_I2S_H__*/
