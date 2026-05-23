// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner V536 (sun8iw16) clock controller driver.
 *
 * Register layout taken from the BSP 4.9 driver clk-sun8iw16.c and
 * verified against a register dump of a running system. The display,
 * camera and video-engine clocks are not modelled yet because mainline
 * has no drivers for those IP blocks.
 *
 * Copyright (c) 2026 Jörg Thalheim <joerg@thalheim.io>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "ccu_common.h"
#include "ccu_reset.h"

#include "ccu_div.h"
#include "ccu_gate.h"
#include "ccu_mp.h"
#include "ccu_mult.h"
#include "ccu_nk.h"
#include "ccu_nkm.h"
#include "ccu_nkmp.h"
#include "ccu_nm.h"

#include "ccu-sun8i-v536.h"

#define SUN8I_V536_PLL_OUTPUT_ENABLE	BIT(27)
#define SUN8I_V536_PLL_LOCK		BIT(28)
#define SUN8I_V536_PLL_LOCK_ENABLE	BIT(29)
#define SUN8I_V536_PLL_ENABLE		BIT(31)

/*
 * The CPU PLL is N * 24 MHz. M ([1:0], input divider) and P ([17:16],
 * output divider) exist but are only meant for testing and frequencies
 * below 288 MHz, so they are not modelled and forced to /1.
 */
#define SUN8I_V536_PLL_CPUX_REG		0x000
static struct ccu_mult pll_cpux_clk = {
	.enable		= SUN8I_V536_PLL_OUTPUT_ENABLE,
	.lock		= SUN8I_V536_PLL_LOCK,
	.mult		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.common		= {
		.reg		= 0x000,
		.hw.init	= CLK_HW_INIT("pll-cpux", "osc24M",
					      &ccu_mult_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

/* All other PLLs are 24 MHz * N / div1 / div2. */
#define SUN8I_V536_PLL_DDR0_REG		0x010
static struct ccu_nkmp pll_ddr0_clk = {
	.enable		= SUN8I_V536_PLL_OUTPUT_ENABLE,
	.lock		= SUN8I_V536_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x010,
		.hw.init	= CLK_HW_INIT("pll-ddr0", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE |
					      CLK_IS_CRITICAL),
	},
};

#define SUN8I_V536_PLL_DDR1_REG		0x018
static struct ccu_nkmp pll_ddr1_clk = {
	.enable		= SUN8I_V536_PLL_OUTPUT_ENABLE,
	.lock		= SUN8I_V536_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x018,
		.hw.init	= CLK_HW_INIT("pll-ddr1", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE |
					      CLK_IS_CRITICAL),
	},
};

#define SUN8I_V536_PLL_PERIPH0_REG	0x020
static struct ccu_nkmp pll_periph0_clk = {
	.enable		= SUN8I_V536_PLL_OUTPUT_ENABLE,
	.lock		= SUN8I_V536_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.fixed_post_div	= 2,
	.common		= {
		.reg		= 0x020,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-periph0", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN8I_V536_PLL_PERIPH1_REG	0x028
static struct ccu_nkmp pll_periph1_clk = {
	.enable		= SUN8I_V536_PLL_OUTPUT_ENABLE,
	.lock		= SUN8I_V536_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.fixed_post_div	= 2,
	.common		= {
		.reg		= 0x028,
		.features	= CCU_FEATURE_FIXED_POSTDIV,
		.hw.init	= CLK_HW_INIT("pll-periph1", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN8I_V536_PLL_VIDEO0_REG	0x040
static struct ccu_nm pll_video0_4x_clk = {
	.enable		= SUN8I_V536_PLL_OUTPUT_ENABLE,
	.lock		= SUN8I_V536_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.common		= {
		.reg		= 0x040,
		.hw.init	= CLK_HW_INIT("pll-video0-4x", "osc24M",
					      &ccu_nm_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN8I_V536_PLL_VE_REG		0x058
static struct ccu_nkmp pll_ve_clk = {
	.enable		= SUN8I_V536_PLL_OUTPUT_ENABLE,
	.lock		= SUN8I_V536_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x058,
		.hw.init	= CLK_HW_INIT("pll-ve", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN8I_V536_PLL_DE_REG		0x060
static struct ccu_nkmp pll_de_clk = {
	.enable		= SUN8I_V536_PLL_OUTPUT_ENABLE,
	.lock		= SUN8I_V536_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x060,
		.hw.init	= CLK_HW_INIT("pll-de", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN8I_V536_PLL_ISP_REG		0x068
static struct ccu_nkmp pll_isp_clk = {
	.enable		= SUN8I_V536_PLL_OUTPUT_ENABLE,
	.lock		= SUN8I_V536_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x068,
		.hw.init	= CLK_HW_INIT("pll-isp", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

/*
 * The audio PLL has an extra 6-bit P divider at [21:16]. Modelled like
 * the BSP does: 24 MHz * N / (P + 1) / div2; div1 is left at /1.
 */
#define SUN8I_V536_PLL_AUDIO_REG	0x078
static struct ccu_nkmp pll_audio_clk = {
	.enable		= SUN8I_V536_PLL_OUTPUT_ENABLE,
	.lock		= SUN8I_V536_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(16, 6), /* P divider */
	.p		= _SUNXI_CCU_DIV(0, 1),  /* output divider */
	.common		= {
		.reg		= 0x078,
		.hw.init	= CLK_HW_INIT("pll-audio", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN8I_V536_PLL_ISE_REG		0x0d0
static struct ccu_nkmp pll_ise_clk = {
	.enable		= SUN8I_V536_PLL_OUTPUT_ENABLE,
	.lock		= SUN8I_V536_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x0d0,
		.hw.init	= CLK_HW_INIT("pll-ise", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

#define SUN8I_V536_PLL_CSI_REG		0x0e0
static struct ccu_nkmp pll_csi_clk = {
	.enable		= SUN8I_V536_PLL_OUTPUT_ENABLE,
	.lock		= SUN8I_V536_PLL_LOCK,
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 12),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x0e0,
		.hw.init	= CLK_HW_INIT("pll-csi", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static const char * const cpux_parents[] = { "osc24M", "osc32k",
					     "iosc", "pll-cpux" };
static SUNXI_CCU_MUX(cpux_clk, "cpux", cpux_parents,
		     0x500, 24, 2, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);
static SUNXI_CCU_M(axi_clk, "axi", "cpux", 0x500, 0, 2, 0);
static SUNXI_CCU_M(cpux_apb_clk, "cpux-apb", "cpux", 0x500, 8, 2, 0);

static const char * const psi_ahb1_ahb2_parents[] = { "osc24M", "osc32k",
						      "iosc", "pll-periph0" };
static SUNXI_CCU_MP_WITH_MUX(psi_ahb1_ahb2_clk, "psi-ahb1-ahb2",
			     psi_ahb1_ahb2_parents, 0x510,
			     0, 2,	/* M */
			     8, 2,	/* P */
			     24, 2,	/* mux */
			     0);

static const char * const ahb3_apb1_apb2_parents[] = { "osc24M", "osc32k",
						       "psi-ahb1-ahb2",
						       "pll-periph0" };
static SUNXI_CCU_MP_WITH_MUX(ahb3_clk, "ahb3", ahb3_apb1_apb2_parents, 0x51c,
			     0, 2,	/* M */
			     8, 2,	/* P */
			     24, 2,	/* mux */
			     0);

static SUNXI_CCU_MP_WITH_MUX(apb1_clk, "apb1", ahb3_apb1_apb2_parents, 0x520,
			     0, 2,	/* M */
			     8, 2,	/* P */
			     24, 2,	/* mux */
			     0);

static SUNXI_CCU_MP_WITH_MUX(apb2_clk, "apb2", ahb3_apb1_apb2_parents, 0x524,
			     0, 2,	/* M */
			     8, 2,	/* P */
			     24, 2,	/* mux */
			     0);

static const char * const mbus_parents[] = { "osc24M", "pll-periph0-2x",
					     "pll-ddr0", "pll-ddr1" };
static SUNXI_CCU_M_WITH_MUX_GATE(mbus_clk, "mbus", mbus_parents, 0x540,
				 0, 3,		/* M */
				 24, 2,		/* mux */
				 BIT(31),	/* gate */
				 CLK_IS_CRITICAL);

static const char * const ce_parents[] = { "osc24M", "pll-periph0-2x" };
static SUNXI_CCU_MP_WITH_MUX_GATE(ce_clk, "ce", ce_parents, 0x680,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 1,	/* mux */
				  BIT(31),	/* gate */
				  0);
static SUNXI_CCU_GATE(bus_ce_clk, "bus-ce", "psi-ahb1-ahb2",
		      0x68c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_dma_clk, "bus-dma", "psi-ahb1-ahb2",
		      0x70c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_msgbox_clk, "bus-msgbox", "psi-ahb1-ahb2",
		      0x71c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_spinlock_clk, "bus-spinlock", "psi-ahb1-ahb2",
		      0x72c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_hstimer_clk, "bus-hstimer", "psi-ahb1-ahb2",
		      0x73c, BIT(0), 0);

static SUNXI_CCU_GATE(avs_clk, "avs", "osc24M", 0x740, BIT(31), 0);

static SUNXI_CCU_GATE(bus_dbg_clk, "bus-dbg", "psi-ahb1-ahb2",
		      0x78c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_psi_clk, "bus-psi", "psi-ahb1-ahb2",
		      0x79c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_pwm_clk, "bus-pwm", "apb1", 0x7ac, BIT(0), 0);

static SUNXI_CCU_GATE(mbus_dma_clk, "mbus-dma", "mbus",
		      0x804, BIT(0), 0);
static SUNXI_CCU_GATE(mbus_ce_clk, "mbus-ce", "mbus",
		      0x804, BIT(2), 0);
static SUNXI_CCU_GATE(mbus_nand_clk, "mbus-nand", "mbus",
		      0x804, BIT(5), 0);

static const char * const dram_parents[] = { "pll-ddr0", "pll-ddr1" };
static SUNXI_CCU_M_WITH_MUX(dram_clk, "dram", dram_parents, 0x800,
			    0, 2,	/* M */
			    24, 2,	/* mux */
			    CLK_IS_CRITICAL);

static SUNXI_CCU_GATE(bus_dram_clk, "bus-dram", "psi-ahb1-ahb2",
		      0x80c, BIT(0), CLK_IS_CRITICAL);

static const char * const nand_spi_parents[] = { "osc24M", "pll-periph0",
						 "pll-periph1",
						 "pll-periph0-2x",
						 "pll-periph1-2x" };
static SUNXI_CCU_MP_WITH_MUX_GATE(nand0_clk, "nand0", nand_spi_parents, 0x810,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);
static SUNXI_CCU_MP_WITH_MUX_GATE(nand1_clk, "nand1", nand_spi_parents, 0x814,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);
static SUNXI_CCU_GATE(bus_nand_clk, "bus-nand", "ahb3", 0x82c, BIT(0), 0);

static const char * const mmc_parents[] = { "osc24M", "pll-periph0-2x",
					    "pll-periph1-2x" };
static SUNXI_CCU_MP_WITH_MUX_GATE_POSTDIV(mmc0_clk, "mmc0", mmc_parents, 0x830,
					  0, 4,		/* M */
					  8, 2,		/* P */
					  24, 2,	/* mux */
					  BIT(31),	/* gate */
					  2,		/* post-div */
					  0);
static SUNXI_CCU_MP_WITH_MUX_GATE_POSTDIV(mmc1_clk, "mmc1", mmc_parents, 0x834,
					  0, 4,		/* M */
					  8, 2,		/* P */
					  24, 2,	/* mux */
					  BIT(31),	/* gate */
					  2,		/* post-div */
					  0);
static SUNXI_CCU_MP_WITH_MUX_GATE_POSTDIV(mmc2_clk, "mmc2", mmc_parents, 0x838,
					  0, 4,		/* M */
					  8, 2,		/* P */
					  24, 2,	/* mux */
					  BIT(31),	/* gate */
					  2,		/* post-div */
					  0);
static SUNXI_CCU_GATE(bus_mmc0_clk, "bus-mmc0", "ahb3", 0x84c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_mmc1_clk, "bus-mmc1", "ahb3", 0x84c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_mmc2_clk, "bus-mmc2", "ahb3", 0x84c, BIT(2), 0);

static SUNXI_CCU_GATE(bus_uart0_clk, "bus-uart0", "apb2", 0x90c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_uart1_clk, "bus-uart1", "apb2", 0x90c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_uart2_clk, "bus-uart2", "apb2", 0x90c, BIT(2), 0);
static SUNXI_CCU_GATE(bus_uart3_clk, "bus-uart3", "apb2", 0x90c, BIT(3), 0);
static SUNXI_CCU_GATE(bus_uart4_clk, "bus-uart4", "apb2", 0x90c, BIT(4), 0);

static SUNXI_CCU_GATE(bus_i2c0_clk, "bus-i2c0", "apb2", 0x91c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_i2c1_clk, "bus-i2c1", "apb2", 0x91c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_i2c2_clk, "bus-i2c2", "apb2", 0x91c, BIT(2), 0);
static SUNXI_CCU_GATE(bus_i2c3_clk, "bus-i2c3", "apb2", 0x91c, BIT(3), 0);

static SUNXI_CCU_MP_WITH_MUX_GATE(spi0_clk, "spi0", nand_spi_parents, 0x940,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);
static SUNXI_CCU_MP_WITH_MUX_GATE(spi1_clk, "spi1", nand_spi_parents, 0x944,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);
static SUNXI_CCU_MP_WITH_MUX_GATE(spi2_clk, "spi2", nand_spi_parents, 0x948,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);
static SUNXI_CCU_MP_WITH_MUX_GATE(spi3_clk, "spi3", nand_spi_parents, 0x94c,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);
static SUNXI_CCU_GATE(bus_spi0_clk, "bus-spi0", "ahb3", 0x96c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_spi1_clk, "bus-spi1", "ahb3", 0x96c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_spi2_clk, "bus-spi2", "ahb3", 0x96c, BIT(2), 0);
static SUNXI_CCU_GATE(bus_spi3_clk, "bus-spi3", "ahb3", 0x96c, BIT(3), 0);

static SUNXI_CCU_GATE(bus_gpadc_clk, "bus-gpadc", "apb1", 0x9ec, BIT(0), 0);

static SUNXI_CCU_GATE(bus_ths_clk, "bus-ths", "apb1", 0x9fc, BIT(0), 0);

static const char * const audio_parents[] = { "pll-audio" };
static SUNXI_CCU_MP_WITH_MUX_GATE(i2s0_clk, "i2s0", audio_parents, 0xa10,
				  0, 0,		/* no M */
				  8, 2,		/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  CLK_SET_RATE_PARENT);
static SUNXI_CCU_MP_WITH_MUX_GATE(i2s1_clk, "i2s1", audio_parents, 0xa14,
				  0, 0,		/* no M */
				  8, 2,		/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  CLK_SET_RATE_PARENT);
static SUNXI_CCU_MP_WITH_MUX_GATE(i2s2_clk, "i2s2", audio_parents, 0xa18,
				  0, 0,		/* no M */
				  8, 2,		/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  CLK_SET_RATE_PARENT);
static SUNXI_CCU_GATE(bus_i2s0_clk, "bus-i2s0", "apb1", 0xa1c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_i2s1_clk, "bus-i2s1", "apb1", 0xa1c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_i2s2_clk, "bus-i2s2", "apb1", 0xa1c, BIT(2), 0);

static SUNXI_CCU_MP_WITH_MUX_GATE(dmic_clk, "dmic", audio_parents, 0xa40,
				  0, 0,		/* no M */
				  8, 2,		/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  CLK_SET_RATE_PARENT);
static SUNXI_CCU_GATE(bus_dmic_clk, "bus-dmic", "apb1", 0xa4c, BIT(0), 0);

static SUNXI_CCU_M_WITH_MUX_GATE(audio_codec_1x_clk, "audio-codec-1x",
				 audio_parents, 0xa50,
				 0, 4,		/* M */
				 24, 2,		/* mux */
				 BIT(31),	/* gate */
				 CLK_SET_RATE_PARENT);
static SUNXI_CCU_M_WITH_MUX_GATE(audio_codec_4x_clk, "audio-codec-4x",
				 audio_parents, 0xa54,
				 0, 4,		/* M */
				 24, 2,		/* mux */
				 BIT(31),	/* gate */
				 CLK_SET_RATE_PARENT);
static SUNXI_CCU_GATE(bus_audio_codec_clk, "bus-audio-codec", "apb1",
		      0xa5c, BIT(0), 0);

static SUNXI_CCU_GATE(usb_ohci0_clk, "usb-ohci0", "osc12M", 0xa70, BIT(31), 0);
static SUNXI_CCU_GATE(usb_phy0_clk, "usb-phy0", "osc24M", 0xa70, BIT(29), 0);

static SUNXI_CCU_GATE(usb_ohci1_clk, "usb-ohci1", "osc12M", 0xa74, BIT(31), 0);
static SUNXI_CCU_GATE(usb_phy1_clk, "usb-phy1", "osc24M", 0xa74, BIT(29), 0);

static SUNXI_CCU_GATE(bus_ohci0_clk, "bus-ohci0", "ahb3", 0xa8c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_ohci1_clk, "bus-ohci1", "ahb3", 0xa8c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_ehci0_clk, "bus-ehci0", "ahb3", 0xa8c, BIT(4), 0);
static SUNXI_CCU_GATE(bus_ehci1_clk, "bus-ehci1", "ahb3", 0xa8c, BIT(5), 0);
static SUNXI_CCU_GATE(bus_otg_clk, "bus-otg", "ahb3", 0xa8c, BIT(8), 0);

static CLK_FIXED_FACTOR(osc12M_clk, "osc12M", "osc24M", 2, 1, 0);

static CLK_FIXED_FACTOR_HW(pll_periph0_2x_clk, "pll-periph0-2x",
			   &pll_periph0_clk.common.hw, 1, 2, 0);
static CLK_FIXED_FACTOR_HW(pll_periph1_2x_clk, "pll-periph1-2x",
			   &pll_periph1_clk.common.hw, 1, 2, 0);
static CLK_FIXED_FACTOR_HW(pll_video0_clk, "pll-video0",
			   &pll_video0_4x_clk.common.hw, 4, 1, 0);

static struct ccu_common *sun8i_v536_ccu_clks[] = {
	&pll_cpux_clk.common,
	&pll_ddr0_clk.common,
	&pll_ddr1_clk.common,
	&pll_periph0_clk.common,
	&pll_periph1_clk.common,
	&pll_video0_4x_clk.common,
	&pll_ve_clk.common,
	&pll_de_clk.common,
	&pll_isp_clk.common,
	&pll_audio_clk.common,
	&pll_ise_clk.common,
	&pll_csi_clk.common,
	&cpux_clk.common,
	&axi_clk.common,
	&cpux_apb_clk.common,
	&psi_ahb1_ahb2_clk.common,
	&ahb3_clk.common,
	&apb1_clk.common,
	&apb2_clk.common,
	&mbus_clk.common,
	&ce_clk.common,
	&bus_ce_clk.common,
	&bus_dma_clk.common,
	&bus_msgbox_clk.common,
	&bus_spinlock_clk.common,
	&bus_hstimer_clk.common,
	&avs_clk.common,
	&bus_dbg_clk.common,
	&bus_psi_clk.common,
	&bus_pwm_clk.common,
	&mbus_dma_clk.common,
	&mbus_ce_clk.common,
	&mbus_nand_clk.common,
	&dram_clk.common,
	&bus_dram_clk.common,
	&nand0_clk.common,
	&nand1_clk.common,
	&bus_nand_clk.common,
	&mmc0_clk.common,
	&mmc1_clk.common,
	&mmc2_clk.common,
	&bus_mmc0_clk.common,
	&bus_mmc1_clk.common,
	&bus_mmc2_clk.common,
	&bus_uart0_clk.common,
	&bus_uart1_clk.common,
	&bus_uart2_clk.common,
	&bus_uart3_clk.common,
	&bus_uart4_clk.common,
	&bus_i2c0_clk.common,
	&bus_i2c1_clk.common,
	&bus_i2c2_clk.common,
	&bus_i2c3_clk.common,
	&spi0_clk.common,
	&spi1_clk.common,
	&spi2_clk.common,
	&spi3_clk.common,
	&bus_spi0_clk.common,
	&bus_spi1_clk.common,
	&bus_spi2_clk.common,
	&bus_spi3_clk.common,
	&bus_gpadc_clk.common,
	&bus_ths_clk.common,
	&i2s0_clk.common,
	&i2s1_clk.common,
	&i2s2_clk.common,
	&bus_i2s0_clk.common,
	&bus_i2s1_clk.common,
	&bus_i2s2_clk.common,
	&dmic_clk.common,
	&bus_dmic_clk.common,
	&audio_codec_1x_clk.common,
	&audio_codec_4x_clk.common,
	&bus_audio_codec_clk.common,
	&usb_ohci0_clk.common,
	&usb_phy0_clk.common,
	&usb_ohci1_clk.common,
	&usb_phy1_clk.common,
	&bus_ohci0_clk.common,
	&bus_ohci1_clk.common,
	&bus_ehci0_clk.common,
	&bus_ehci1_clk.common,
	&bus_otg_clk.common,
};

static struct clk_hw_onecell_data sun8i_v536_hw_clks = {
	.hws	= {
		[CLK_OSC12M]	= &osc12M_clk.hw,
		[CLK_PLL_CPUX]	= &pll_cpux_clk.common.hw,
		[CLK_PLL_DDR0]	= &pll_ddr0_clk.common.hw,
		[CLK_PLL_DDR1]	= &pll_ddr1_clk.common.hw,
		[CLK_PLL_PERIPH0]	= &pll_periph0_clk.common.hw,
		[CLK_PLL_PERIPH0_2X]	= &pll_periph0_2x_clk.hw,
		[CLK_PLL_PERIPH1]	= &pll_periph1_clk.common.hw,
		[CLK_PLL_PERIPH1_2X]	= &pll_periph1_2x_clk.hw,
		[CLK_PLL_VIDEO0_4X]	= &pll_video0_4x_clk.common.hw,
		[CLK_PLL_VIDEO0]	= &pll_video0_clk.hw,
		[CLK_PLL_VE]	= &pll_ve_clk.common.hw,
		[CLK_PLL_DE]	= &pll_de_clk.common.hw,
		[CLK_PLL_ISP]	= &pll_isp_clk.common.hw,
		[CLK_PLL_AUDIO]	= &pll_audio_clk.common.hw,
		[CLK_PLL_ISE]	= &pll_ise_clk.common.hw,
		[CLK_PLL_CSI]	= &pll_csi_clk.common.hw,
		[CLK_CPUX]	= &cpux_clk.common.hw,
		[CLK_AXI]	= &axi_clk.common.hw,
		[CLK_CPUX_APB]	= &cpux_apb_clk.common.hw,
		[CLK_PSI_AHB1_AHB2]	= &psi_ahb1_ahb2_clk.common.hw,
		[CLK_AHB3]	= &ahb3_clk.common.hw,
		[CLK_APB1]	= &apb1_clk.common.hw,
		[CLK_APB2]	= &apb2_clk.common.hw,
		[CLK_MBUS]	= &mbus_clk.common.hw,
		[CLK_CE]	= &ce_clk.common.hw,
		[CLK_BUS_CE]	= &bus_ce_clk.common.hw,
		[CLK_BUS_DMA]	= &bus_dma_clk.common.hw,
		[CLK_BUS_MSGBOX]	= &bus_msgbox_clk.common.hw,
		[CLK_BUS_SPINLOCK]	= &bus_spinlock_clk.common.hw,
		[CLK_BUS_HSTIMER]	= &bus_hstimer_clk.common.hw,
		[CLK_AVS]	= &avs_clk.common.hw,
		[CLK_BUS_DBG]	= &bus_dbg_clk.common.hw,
		[CLK_BUS_PSI]	= &bus_psi_clk.common.hw,
		[CLK_BUS_PWM]	= &bus_pwm_clk.common.hw,
		[CLK_MBUS_DMA]	= &mbus_dma_clk.common.hw,
		[CLK_MBUS_CE]	= &mbus_ce_clk.common.hw,
		[CLK_MBUS_NAND]	= &mbus_nand_clk.common.hw,
		[CLK_DRAM]	= &dram_clk.common.hw,
		[CLK_BUS_DRAM]	= &bus_dram_clk.common.hw,
		[CLK_NAND0]	= &nand0_clk.common.hw,
		[CLK_NAND1]	= &nand1_clk.common.hw,
		[CLK_BUS_NAND]	= &bus_nand_clk.common.hw,
		[CLK_MMC0]	= &mmc0_clk.common.hw,
		[CLK_MMC1]	= &mmc1_clk.common.hw,
		[CLK_MMC2]	= &mmc2_clk.common.hw,
		[CLK_BUS_MMC0]	= &bus_mmc0_clk.common.hw,
		[CLK_BUS_MMC1]	= &bus_mmc1_clk.common.hw,
		[CLK_BUS_MMC2]	= &bus_mmc2_clk.common.hw,
		[CLK_BUS_UART0]	= &bus_uart0_clk.common.hw,
		[CLK_BUS_UART1]	= &bus_uart1_clk.common.hw,
		[CLK_BUS_UART2]	= &bus_uart2_clk.common.hw,
		[CLK_BUS_UART3]	= &bus_uart3_clk.common.hw,
		[CLK_BUS_UART4]	= &bus_uart4_clk.common.hw,
		[CLK_BUS_I2C0]	= &bus_i2c0_clk.common.hw,
		[CLK_BUS_I2C1]	= &bus_i2c1_clk.common.hw,
		[CLK_BUS_I2C2]	= &bus_i2c2_clk.common.hw,
		[CLK_BUS_I2C3]	= &bus_i2c3_clk.common.hw,
		[CLK_SPI0]	= &spi0_clk.common.hw,
		[CLK_SPI1]	= &spi1_clk.common.hw,
		[CLK_SPI2]	= &spi2_clk.common.hw,
		[CLK_SPI3]	= &spi3_clk.common.hw,
		[CLK_BUS_SPI0]	= &bus_spi0_clk.common.hw,
		[CLK_BUS_SPI1]	= &bus_spi1_clk.common.hw,
		[CLK_BUS_SPI2]	= &bus_spi2_clk.common.hw,
		[CLK_BUS_SPI3]	= &bus_spi3_clk.common.hw,
		[CLK_BUS_GPADC]	= &bus_gpadc_clk.common.hw,
		[CLK_BUS_THS]	= &bus_ths_clk.common.hw,
		[CLK_I2S0]	= &i2s0_clk.common.hw,
		[CLK_I2S1]	= &i2s1_clk.common.hw,
		[CLK_I2S2]	= &i2s2_clk.common.hw,
		[CLK_BUS_I2S0]	= &bus_i2s0_clk.common.hw,
		[CLK_BUS_I2S1]	= &bus_i2s1_clk.common.hw,
		[CLK_BUS_I2S2]	= &bus_i2s2_clk.common.hw,
		[CLK_DMIC]	= &dmic_clk.common.hw,
		[CLK_BUS_DMIC]	= &bus_dmic_clk.common.hw,
		[CLK_AUDIO_CODEC_1X]	= &audio_codec_1x_clk.common.hw,
		[CLK_AUDIO_CODEC_4X]	= &audio_codec_4x_clk.common.hw,
		[CLK_BUS_AUDIO_CODEC]	= &bus_audio_codec_clk.common.hw,
		[CLK_USB_OHCI0]	= &usb_ohci0_clk.common.hw,
		[CLK_USB_PHY0]	= &usb_phy0_clk.common.hw,
		[CLK_USB_OHCI1]	= &usb_ohci1_clk.common.hw,
		[CLK_USB_PHY1]	= &usb_phy1_clk.common.hw,
		[CLK_BUS_OHCI0]	= &bus_ohci0_clk.common.hw,
		[CLK_BUS_OHCI1]	= &bus_ohci1_clk.common.hw,
		[CLK_BUS_EHCI0]	= &bus_ehci0_clk.common.hw,
		[CLK_BUS_EHCI1]	= &bus_ehci1_clk.common.hw,
		[CLK_BUS_OTG]	= &bus_otg_clk.common.hw,
	},
	.num	= CLK_NUMBER,
};

static const struct ccu_reset_map sun8i_v536_ccu_resets[] = {
	[RST_MBUS]	= { 0x540, BIT(30) },
	[RST_BUS_CE]	= { 0x68c, BIT(16) },
	[RST_BUS_DMA]	= { 0x70c, BIT(16) },
	[RST_BUS_MSGBOX]	= { 0x71c, BIT(16) },
	[RST_BUS_SPINLOCK]	= { 0x72c, BIT(16) },
	[RST_BUS_HSTIMER]	= { 0x73c, BIT(16) },
	[RST_BUS_DBG]	= { 0x78c, BIT(16) },
	[RST_BUS_PSI]	= { 0x79c, BIT(16) },
	[RST_BUS_PWM]	= { 0x7ac, BIT(16) },
	[RST_BUS_DRAM]	= { 0x80c, BIT(16) },
	[RST_BUS_NAND]	= { 0x82c, BIT(16) },
	[RST_BUS_MMC0]	= { 0x84c, BIT(16) },
	[RST_BUS_MMC1]	= { 0x84c, BIT(17) },
	[RST_BUS_MMC2]	= { 0x84c, BIT(18) },
	[RST_BUS_UART0]	= { 0x90c, BIT(16) },
	[RST_BUS_UART1]	= { 0x90c, BIT(17) },
	[RST_BUS_UART2]	= { 0x90c, BIT(18) },
	[RST_BUS_UART3]	= { 0x90c, BIT(19) },
	[RST_BUS_UART4]	= { 0x90c, BIT(20) },
	[RST_BUS_I2C0]	= { 0x91c, BIT(16) },
	[RST_BUS_I2C1]	= { 0x91c, BIT(17) },
	[RST_BUS_I2C2]	= { 0x91c, BIT(18) },
	[RST_BUS_I2C3]	= { 0x91c, BIT(19) },
	[RST_BUS_SPI0]	= { 0x96c, BIT(16) },
	[RST_BUS_SPI1]	= { 0x96c, BIT(17) },
	[RST_BUS_SPI2]	= { 0x96c, BIT(18) },
	[RST_BUS_SPI3]	= { 0x96c, BIT(19) },
	[RST_BUS_GPADC]	= { 0x9ec, BIT(16) },
	[RST_BUS_THS]	= { 0x9fc, BIT(16) },
	[RST_BUS_I2S0]	= { 0xa1c, BIT(16) },
	[RST_BUS_I2S1]	= { 0xa1c, BIT(17) },
	[RST_BUS_I2S2]	= { 0xa1c, BIT(18) },
	[RST_BUS_DMIC]	= { 0xa4c, BIT(16) },
	[RST_BUS_AUDIO_CODEC]	= { 0xa5c, BIT(16) },
	[RST_USB_PHY0]	= { 0xa70, BIT(30) },
	[RST_USB_PHY1]	= { 0xa74, BIT(30) },
	[RST_BUS_OHCI0]	= { 0xa8c, BIT(16) },
	[RST_BUS_OHCI1]	= { 0xa8c, BIT(17) },
	[RST_BUS_EHCI0]	= { 0xa8c, BIT(20) },
	[RST_BUS_EHCI1]	= { 0xa8c, BIT(21) },
	[RST_BUS_OTG]	= { 0xa8c, BIT(24) },
};

static const struct sunxi_ccu_desc sun8i_v536_ccu_desc = {
	.ccu_clks	= sun8i_v536_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun8i_v536_ccu_clks),

	.hw_clks	= &sun8i_v536_hw_clks,

	.resets		= sun8i_v536_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun8i_v536_ccu_resets),
};

static const u32 sun8i_v536_pll_regs[] = {
	SUN8I_V536_PLL_CPUX_REG,
	SUN8I_V536_PLL_DDR0_REG,
	SUN8I_V536_PLL_DDR1_REG,
	SUN8I_V536_PLL_PERIPH0_REG,
	SUN8I_V536_PLL_PERIPH1_REG,
	SUN8I_V536_PLL_VIDEO0_REG,
	SUN8I_V536_PLL_VE_REG,
	SUN8I_V536_PLL_DE_REG,
	SUN8I_V536_PLL_ISP_REG,
	SUN8I_V536_PLL_AUDIO_REG,
	SUN8I_V536_PLL_ISE_REG,
	SUN8I_V536_PLL_CSI_REG,
};

static struct ccu_pll_nb sun8i_v536_pll_cpu_nb = {
	.common	= &pll_cpux_clk.common,
	/* copy from pll_cpux_clk */
	.enable	= SUN8I_V536_PLL_OUTPUT_ENABLE,
	.lock	= SUN8I_V536_PLL_LOCK,
};

static struct ccu_mux_nb sun8i_v536_cpu_nb = {
	.common		= &cpux_clk.common,
	.cm		= &cpux_clk.mux,
	.delay_us	= 1, /* > 8 clock cycles at 24 MHz */
	.bypass_index	= 0, /* index of 24 MHz oscillator */
};

static int sun8i_v536_ccu_probe(struct platform_device *pdev)
{
	void __iomem *reg;
	u32 val;
	int i, ret;

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	/*
	 * Like on the A100, multiple PLLs share one power switch, so keep
	 * the PLL and lock-enable bits set and only ever toggle the output
	 * enable bit.
	 */
	for (i = 0; i < ARRAY_SIZE(sun8i_v536_pll_regs); i++) {
		val = readl(reg + sun8i_v536_pll_regs[i]);
		val |= SUN8I_V536_PLL_LOCK_ENABLE | SUN8I_V536_PLL_ENABLE;
		writel(val, reg + sun8i_v536_pll_regs[i]);
	}

	ret = devm_sunxi_ccu_probe(&pdev->dev, reg, &sun8i_v536_ccu_desc);
	if (ret)
		return ret;

	/* Gate then ungate PLL CPU after any rate changes */
	ccu_pll_notifier_register(&sun8i_v536_pll_cpu_nb);

	/* Reparent CPU during PLL CPU rate changes */
	ccu_mux_notifier_register(pll_cpux_clk.common.hw.clk,
				  &sun8i_v536_cpu_nb);

	return 0;
}

static const struct of_device_id sun8i_v536_ccu_ids[] = {
	{ .compatible = "allwinner,sun8i-v536-ccu" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun8i_v536_ccu_ids);

static struct platform_driver sun8i_v536_ccu_driver = {
	.probe	= sun8i_v536_ccu_probe,
	.driver	= {
		.name	= "sun8i-v536-ccu",
		.suppress_bind_attrs = true,
		.of_match_table	= sun8i_v536_ccu_ids,
	},
};
module_platform_driver(sun8i_v536_ccu_driver);

MODULE_IMPORT_NS("SUNXI_CCU");
MODULE_DESCRIPTION("Support for the Allwinner V536 CCU");
MODULE_LICENSE("GPL");
