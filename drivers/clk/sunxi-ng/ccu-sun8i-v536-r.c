// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner V536 (sun8iw16) PRCM (R-CCU) clock driver.
 *
 * Register layout taken from the BSP 4.9 driver clk-sun8iw16.c.
 *
 * Copyright (c) 2026 Jörg Thalheim <joerg@thalheim.io>
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "ccu_common.h"
#include "ccu_reset.h"

#include "ccu_div.h"
#include "ccu_gate.h"
#include "ccu_mp.h"

#include "ccu-sun8i-v536-r.h"

static const char * const r_cpus_r_apb2_parents[] = { "osc24M", "osc32k",
						      "iosc", "pll-periph0" };
static const struct ccu_mux_var_prediv r_cpus_r_apb2_predivs[] = {
	{ .index = 3, .shift = 0, .width = 5 },
};

static struct ccu_div r_cpus_clk = {
	.div		= _SUNXI_CCU_DIV_FLAGS(8, 2, CLK_DIVIDER_POWER_OF_TWO),

	.mux		= {
		.shift	= 24,
		.width	= 2,

		.var_predivs	= r_cpus_r_apb2_predivs,
		.n_var_predivs	= ARRAY_SIZE(r_cpus_r_apb2_predivs),
	},

	.common		= {
		.reg		= 0x000,
		.features	= CCU_FEATURE_VARIABLE_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS("r-cpus",
						      r_cpus_r_apb2_parents,
						      &ccu_div_ops,
						      0),
	},
};

static CLK_FIXED_FACTOR_HW(r_ahb_clk, "r-ahb", &r_cpus_clk.common.hw, 1, 1, 0);

static struct ccu_div r_apb1_clk = {
	.div		= _SUNXI_CCU_DIV(0, 2),

	.common		= {
		.reg		= 0x00c,
		.hw.init	= CLK_HW_INIT("r-apb1",
					      "r-ahb",
					      &ccu_div_ops,
					      0),
	},
};

static struct ccu_div r_apb2_clk = {
	.div		= _SUNXI_CCU_DIV_FLAGS(8, 2, CLK_DIVIDER_POWER_OF_TWO),

	.mux		= {
		.shift	= 24,
		.width	= 2,

		.var_predivs	= r_cpus_r_apb2_predivs,
		.n_var_predivs	= ARRAY_SIZE(r_cpus_r_apb2_predivs),
	},

	.common		= {
		.reg		= 0x010,
		.features	= CCU_FEATURE_VARIABLE_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS("r-apb2",
						      r_cpus_r_apb2_parents,
						      &ccu_div_ops,
						      0),
	},
};

static const struct clk_parent_data clk_parent_r_apb1[] = {
	{ .hw = &r_apb1_clk.common.hw },
};

static const struct clk_parent_data clk_parent_r_apb2[] = {
	{ .hw = &r_apb2_clk.common.hw },
};

static SUNXI_CCU_GATE_DATA(r_apb1_timer_clk, "r-apb1-timer", clk_parent_r_apb1,
			   0x11c, BIT(0), 0);

static SUNXI_CCU_GATE_DATA(r_apb1_bus_pwm_clk, "r-apb1-bus-pwm",
			   clk_parent_r_apb1, 0x13c, BIT(0), 0);

static SUNXI_CCU_GATE_DATA(r_apb2_uart_clk, "r-apb2-uart", clk_parent_r_apb2,
			   0x18c, BIT(0), 0);

static SUNXI_CCU_GATE_DATA(r_apb2_i2c_clk, "r-apb2-i2c", clk_parent_r_apb2,
			   0x19c, BIT(0), 0);

static SUNXI_CCU_GATE_DATA(r_apb2_rsb_clk, "r-apb2-rsb", clk_parent_r_apb2,
			   0x1bc, BIT(0), 0);

static const char * const r_apb1_ir_parents[] = { "osc32k", "osc24M" };
static SUNXI_CCU_MP_WITH_MUX_GATE(r_apb1_ir_clk, "r-apb1-ir",
				  r_apb1_ir_parents, 0x1c0,
				  0, 5,		/* M */
				  8, 2,		/* P */
				  24, 1,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE_DATA(r_apb1_bus_ir_clk, "r-apb1-bus-ir",
			   clk_parent_r_apb1, 0x1cc, BIT(0), 0);

static SUNXI_CCU_GATE(r_ahb_bus_rtc_clk, "r-ahb-rtc", "r-ahb",
		      0x20c, BIT(0), 0);

static struct ccu_common *sun8i_v536_r_ccu_clks[] = {
	&r_cpus_clk.common,
	&r_apb1_clk.common,
	&r_apb2_clk.common,
	&r_apb1_timer_clk.common,
	&r_apb1_bus_pwm_clk.common,
	&r_apb2_uart_clk.common,
	&r_apb2_i2c_clk.common,
	&r_apb2_rsb_clk.common,
	&r_apb1_ir_clk.common,
	&r_apb1_bus_ir_clk.common,
	&r_ahb_bus_rtc_clk.common,
};

static struct clk_hw_onecell_data sun8i_v536_r_hw_clks = {
	.hws	= {
		[CLK_R_CPUS]		= &r_cpus_clk.common.hw,
		[CLK_R_AHB]		= &r_ahb_clk.hw,
		[CLK_R_APB1]		= &r_apb1_clk.common.hw,
		[CLK_R_APB2]		= &r_apb2_clk.common.hw,
		[CLK_R_APB1_TIMER]	= &r_apb1_timer_clk.common.hw,
		[CLK_R_APB1_BUS_PWM]	= &r_apb1_bus_pwm_clk.common.hw,
		[CLK_R_APB2_UART]	= &r_apb2_uart_clk.common.hw,
		[CLK_R_APB2_I2C]	= &r_apb2_i2c_clk.common.hw,
		[CLK_R_APB2_RSB]	= &r_apb2_rsb_clk.common.hw,
		[CLK_R_APB1_IR]		= &r_apb1_ir_clk.common.hw,
		[CLK_R_APB1_BUS_IR]	= &r_apb1_bus_ir_clk.common.hw,
		[CLK_R_AHB_BUS_RTC]	= &r_ahb_bus_rtc_clk.common.hw,
	},
	.num	= CLK_NUMBER,
};

static const struct ccu_reset_map sun8i_v536_r_ccu_resets[] = {
	[RST_R_APB1_TIMER]	= { 0x11c, BIT(16) },
	[RST_R_APB1_BUS_PWM]	= { 0x13c, BIT(16) },
	[RST_R_APB2_UART]	= { 0x18c, BIT(16) },
	[RST_R_APB2_I2C]	= { 0x19c, BIT(16) },
	[RST_R_APB2_RSB]	= { 0x1bc, BIT(16) },
	[RST_R_APB1_BUS_IR]	= { 0x1cc, BIT(16) },
	[RST_R_AHB_BUS_RTC]	= { 0x20c, BIT(16) },
};

static const struct sunxi_ccu_desc sun8i_v536_r_ccu_desc = {
	.ccu_clks	= sun8i_v536_r_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun8i_v536_r_ccu_clks),

	.hw_clks	= &sun8i_v536_r_hw_clks,

	.resets		= sun8i_v536_r_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun8i_v536_r_ccu_resets),
};

static int sun8i_v536_r_ccu_probe(struct platform_device *pdev)
{
	void __iomem *reg;

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	return devm_sunxi_ccu_probe(&pdev->dev, reg, &sun8i_v536_r_ccu_desc);
}

static const struct of_device_id sun8i_v536_r_ccu_ids[] = {
	{ .compatible = "allwinner,sun8i-v536-r-ccu" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun8i_v536_r_ccu_ids);

static struct platform_driver sun8i_v536_r_ccu_driver = {
	.probe	= sun8i_v536_r_ccu_probe,
	.driver	= {
		.name	= "sun8i-v536-r-ccu",
		.suppress_bind_attrs = true,
		.of_match_table	= sun8i_v536_r_ccu_ids,
	},
};
module_platform_driver(sun8i_v536_r_ccu_driver);

MODULE_IMPORT_NS("SUNXI_CCU");
MODULE_DESCRIPTION("Support for the Allwinner V536 PRCM CCU");
MODULE_LICENSE("GPL");
