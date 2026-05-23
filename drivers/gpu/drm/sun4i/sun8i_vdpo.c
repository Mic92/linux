// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Allwinner V536 VDPO (Video Data Parallel Output) DRM encoder
 *
 * The VDPO block takes the video stream from the TV TCON and outputs
 * it as BT.1120 (16-bit YCbCr 4:2:2 with embedded sync) on a parallel
 * bus. On the HDZero goggle this feeds an FPGA driving the OLED panels.
 *
 * Copyright (C) 2026 Jörg Thalheim <joerg@thalheim.io>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#define SUN8I_VDPO_CTRL_REG		0x00
#define SUN8I_VDPO_CTRL_MODULE_EN		BIT(0)
#define SUN8I_VDPO_CTRL_SEPARATE_SYNC_EN	BIT(1)

#define SUN8I_VDPO_FMT_REG		0x04
#define SUN8I_VDPO_FMT_DATA_WIDTH_10BIT		BIT(0)
#define SUN8I_VDPO_FMT_INTERLACE		BIT(1)
#define SUN8I_VDPO_FMT_EMBEDDED_SYNC_BT656	BIT(4)
#define SUN8I_VDPO_FMT_DATA_SEQ(seq)		(((seq) & 0x3) << 8)

#define SUN8I_VDPO_SYNC_CTRL_REG	0x08
#define SUN8I_VDPO_SYNC_CTRL_H_BLANK_POL	BIT(0)
#define SUN8I_VDPO_SYNC_CTRL_V_BLANK_POL	BIT(1)
#define SUN8I_VDPO_SYNC_CTRL_FIELD_POL		BIT(2)
#define SUN8I_VDPO_SYNC_CTRL_DCLK_INVERT	BIT(3)
#define SUN8I_VDPO_SYNC_CTRL_DCLK_DLY(n)	(((n) & 0x3f) << 4)
#define SUN8I_VDPO_SYNC_CTRL_DCLK_DLY_EN	BIT(10)

#define SUN8I_VDPO_INT_CTRL_REG		0x0c
#define SUN8I_VDPO_LINE_INT_NUM_REG	0x10
#define SUN8I_VDPO_STATUS_REG		0x14

#define SUN8I_VDPO_HOR_SPL_REG		0x18
#define SUN8I_VDPO_HOR_SPL_CB_TYPE(t)		(((t) & 0x7) << 0)
#define SUN8I_VDPO_HOR_SPL_CR_TYPE(t)		(((t) & 0x7) << 4)

#define SUN8I_VDPO_CLAMP0_REG		0x1c
#define SUN8I_VDPO_CLAMP1_REG		0x20
#define SUN8I_VDPO_CLAMP2_REG		0x24
#define SUN8I_VDPO_CLAMP_MIN(x)			(((x) & 0xff) << 0)
#define SUN8I_VDPO_CLAMP_MAX(x)			(((x) & 0xff) << 16)

#define SUN8I_VDPO_H_TIMING_REG		0x28
#define SUN8I_VDPO_H_TIMING_BP(x)		(((x) & 0xfff) << 0)
#define SUN8I_VDPO_H_TIMING_ACTIVE(x)		(((x) & 0xfff) << 16)

#define SUN8I_VDPO_V_TIMING0_REG	0x2c
#define SUN8I_VDPO_V_TIMING0_BP(x)		(((x) & 0xfff) << 0)
#define SUN8I_VDPO_V_TIMING0_ACTIVE(x)		(((x) & 0xfff) << 16)

#define SUN8I_VDPO_V_TIMING1_REG	0x30
#define SUN8I_VDPO_V_TIMING1_TOTAL(x)		(((x) & 0xfff) << 0)
#define SUN8I_VDPO_V_TIMING1_ITL_MODE		BIT(16)

struct sun8i_vdpo {
	struct drm_connector	connector;
	struct drm_encoder	encoder;

	struct clk		*bus_clk;
	struct clk		*mod_clk;
	struct regmap		*regs;
	struct reset_control	*reset;
};

/*
 * Fixed mode list matching the vendor VDPO timing table. The sync
 * front porch / pulse split follows the vendor table rather than
 * CEA-861, so that the back porch programmed into the VDPO timing
 * registers (sync + back porch) matches the vendor driver exactly.
 * 720p90 is not in the vendor table; it reuses the 720p60 geometry
 * with a 1.5x pixel clock.
 */
static const struct drm_display_mode sun8i_vdpo_modes[] = {
	/* 720p60 */
	{ DRM_MODE("1280x720",
		   DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED, 74250,
		   1280, 1320, 1360, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 720p50 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250,
		   1280, 1320, 1360, 1980, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 720p90 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 111375,
		   1280, 1320, 1360, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1080p60 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500,
		   1920, 1964, 2052, 2200, 0, 1080, 1116, 1121, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1080p50 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500,
		   1920, 1964, 2008, 2640, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
};

static inline struct sun8i_vdpo *
drm_encoder_to_sun8i_vdpo(struct drm_encoder *encoder)
{
	return container_of(encoder, struct sun8i_vdpo, encoder);
}

static inline struct sun8i_vdpo *
drm_connector_to_sun8i_vdpo(struct drm_connector *connector)
{
	return container_of(connector, struct sun8i_vdpo, connector);
}

static void sun8i_vdpo_mode_set(struct sun8i_vdpo *vdpo,
				const struct drm_display_mode *mode)
{
	u32 h_bp, v_bp, val;

	/*
	 * The VDPO back porch registers count from the start of the
	 * sync pulse to the start of active video, i.e. they include
	 * the sync length.
	 */
	h_bp = mode->htotal - mode->hsync_start;
	v_bp = mode->vtotal - mode->vsync_start;

	regmap_write(vdpo->regs, SUN8I_VDPO_H_TIMING_REG,
		     SUN8I_VDPO_H_TIMING_ACTIVE(mode->hdisplay - 1) |
		     SUN8I_VDPO_H_TIMING_BP(h_bp - 1));

	regmap_write(vdpo->regs, SUN8I_VDPO_V_TIMING0_REG,
		     SUN8I_VDPO_V_TIMING0_ACTIVE(mode->vdisplay - 1) |
		     SUN8I_VDPO_V_TIMING0_BP(v_bp - 1));

	/* The total is expressed in half-lines, like on the TCON. */
	regmap_write(vdpo->regs, SUN8I_VDPO_V_TIMING1_REG,
		     SUN8I_VDPO_V_TIMING1_TOTAL(mode->vtotal * 2));

	/* BT.1120: 8-bit bus pair, progressive, embedded sync */
	regmap_write(vdpo->regs, SUN8I_VDPO_FMT_REG,
		     SUN8I_VDPO_FMT_DATA_SEQ(0));

	val = 0;
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		val |= SUN8I_VDPO_SYNC_CTRL_H_BLANK_POL;
	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		val |= SUN8I_VDPO_SYNC_CTRL_V_BLANK_POL;
	regmap_write(vdpo->regs, SUN8I_VDPO_SYNC_CTRL_REG, val);

	/* Default chroma sample phase */
	regmap_write(vdpo->regs, SUN8I_VDPO_HOR_SPL_REG,
		     SUN8I_VDPO_HOR_SPL_CB_TYPE(0) |
		     SUN8I_VDPO_HOR_SPL_CR_TYPE(0));

	/* Clamp to BT.601 nominal ranges */
	regmap_write(vdpo->regs, SUN8I_VDPO_CLAMP0_REG,
		     SUN8I_VDPO_CLAMP_MIN(16) | SUN8I_VDPO_CLAMP_MAX(235));
	regmap_write(vdpo->regs, SUN8I_VDPO_CLAMP1_REG,
		     SUN8I_VDPO_CLAMP_MIN(16) | SUN8I_VDPO_CLAMP_MAX(240));
	regmap_write(vdpo->regs, SUN8I_VDPO_CLAMP2_REG,
		     SUN8I_VDPO_CLAMP_MIN(16) | SUN8I_VDPO_CLAMP_MAX(240));
}

static void sun8i_vdpo_encoder_enable(struct drm_encoder *encoder,
				      struct drm_atomic_state *state)
{
	struct sun8i_vdpo *vdpo = drm_encoder_to_sun8i_vdpo(encoder);
	struct drm_crtc_state *crtc_state =
		drm_atomic_get_new_crtc_state(state, encoder->crtc);
	struct drm_display_mode *mode = &crtc_state->adjusted_mode;

	DRM_DEBUG_DRIVER("Enabling VDPO output, mode " DRM_MODE_FMT "\n",
			 DRM_MODE_ARG(mode));

	/*
	 * BT.1120 multiplexes Y and Cb/Cr on a 16-bit bus, the module
	 * clock runs at twice the pixel clock.
	 */
	clk_set_rate(vdpo->mod_clk, mode->clock * 2000UL);

	sun8i_vdpo_mode_set(vdpo, mode);

	regmap_write(vdpo->regs, SUN8I_VDPO_CTRL_REG,
		     SUN8I_VDPO_CTRL_MODULE_EN);
}

static void sun8i_vdpo_encoder_disable(struct drm_encoder *encoder,
				       struct drm_atomic_state *state)
{
	struct sun8i_vdpo *vdpo = drm_encoder_to_sun8i_vdpo(encoder);

	DRM_DEBUG_DRIVER("Disabling VDPO output\n");

	regmap_write(vdpo->regs, SUN8I_VDPO_CTRL_REG, 0);
}

static const struct drm_encoder_helper_funcs sun8i_vdpo_encoder_helper_funcs = {
	.atomic_enable	= sun8i_vdpo_encoder_enable,
	.atomic_disable	= sun8i_vdpo_encoder_disable,
};

static int sun8i_vdpo_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *drm = connector->dev;
	unsigned int i;
	int count = 0;

	for (i = 0; i < ARRAY_SIZE(sun8i_vdpo_modes); i++) {
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(drm, &sun8i_vdpo_modes[i]);
		if (!mode)
			return count;

		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
		count++;
	}

	return count;
}

static enum drm_mode_status
sun8i_vdpo_connector_mode_valid(struct drm_connector *connector,
				const struct drm_display_mode *mode)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sun8i_vdpo_modes); i++)
		if (drm_mode_equal_no_clocks(mode, &sun8i_vdpo_modes[i]))
			return MODE_OK;

	return MODE_BAD;
}

static const struct drm_connector_helper_funcs sun8i_vdpo_connector_helper_funcs = {
	.get_modes	= sun8i_vdpo_connector_get_modes,
	.mode_valid	= sun8i_vdpo_connector_mode_valid,
};

static const struct drm_connector_funcs sun8i_vdpo_connector_funcs = {
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= drm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static const struct regmap_config sun8i_vdpo_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= SUN8I_VDPO_V_TIMING1_REG,
	.name		= "vdpo",
};

static int sun8i_vdpo_bind(struct device *dev, struct device *master,
			   void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct sun8i_vdpo *vdpo;
	void __iomem *regs;
	int ret;

	vdpo = devm_kzalloc(dev, sizeof(*vdpo), GFP_KERNEL);
	if (!vdpo)
		return -ENOMEM;
	dev_set_drvdata(dev, vdpo);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return dev_err_probe(dev, PTR_ERR(regs),
				     "Couldn't map the VDPO registers\n");

	vdpo->regs = devm_regmap_init_mmio(dev, regs,
					   &sun8i_vdpo_regmap_config);
	if (IS_ERR(vdpo->regs))
		return dev_err_probe(dev, PTR_ERR(vdpo->regs),
				     "Couldn't create the VDPO regmap\n");

	vdpo->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(vdpo->reset))
		return dev_err_probe(dev, PTR_ERR(vdpo->reset),
				     "Couldn't get our reset line\n");

	vdpo->bus_clk = devm_clk_get(dev, "bus");
	if (IS_ERR(vdpo->bus_clk))
		return dev_err_probe(dev, PTR_ERR(vdpo->bus_clk),
				     "Couldn't get the VDPO bus clock\n");

	vdpo->mod_clk = devm_clk_get(dev, "mod");
	if (IS_ERR(vdpo->mod_clk))
		return dev_err_probe(dev, PTR_ERR(vdpo->mod_clk),
				     "Couldn't get the VDPO module clock\n");

	ret = reset_control_deassert(vdpo->reset);
	if (ret) {
		dev_err(dev, "Couldn't deassert our reset line\n");
		return ret;
	}

	ret = clk_prepare_enable(vdpo->bus_clk);
	if (ret) {
		dev_err(dev, "Couldn't enable the VDPO bus clock\n");
		goto err_assert_reset;
	}

	ret = clk_prepare_enable(vdpo->mod_clk);
	if (ret) {
		dev_err(dev, "Couldn't enable the VDPO module clock\n");
		goto err_disable_bus_clk;
	}

	drm_encoder_helper_add(&vdpo->encoder,
			       &sun8i_vdpo_encoder_helper_funcs);
	ret = drm_simple_encoder_init(drm, &vdpo->encoder,
				      DRM_MODE_ENCODER_DPI);
	if (ret) {
		dev_err(dev, "Couldn't initialise the VDPO encoder\n");
		goto err_disable_mod_clk;
	}

	vdpo->encoder.possible_crtcs = drm_of_find_possible_crtcs(drm,
								  dev->of_node);
	if (!vdpo->encoder.possible_crtcs) {
		ret = -EPROBE_DEFER;
		goto err_cleanup_encoder;
	}

	drm_connector_helper_add(&vdpo->connector,
				 &sun8i_vdpo_connector_helper_funcs);
	ret = drm_connector_init(drm, &vdpo->connector,
				 &sun8i_vdpo_connector_funcs,
				 DRM_MODE_CONNECTOR_DPI);
	if (ret) {
		dev_err(dev, "Couldn't initialise the VDPO connector\n");
		goto err_cleanup_encoder;
	}

	drm_connector_attach_encoder(&vdpo->connector, &vdpo->encoder);

	return 0;

err_cleanup_encoder:
	drm_encoder_cleanup(&vdpo->encoder);
err_disable_mod_clk:
	clk_disable_unprepare(vdpo->mod_clk);
err_disable_bus_clk:
	clk_disable_unprepare(vdpo->bus_clk);
err_assert_reset:
	reset_control_assert(vdpo->reset);
	return ret;
}

static void sun8i_vdpo_unbind(struct device *dev, struct device *master,
			      void *data)
{
	struct sun8i_vdpo *vdpo = dev_get_drvdata(dev);

	drm_connector_cleanup(&vdpo->connector);
	drm_encoder_cleanup(&vdpo->encoder);
	clk_disable_unprepare(vdpo->mod_clk);
	clk_disable_unprepare(vdpo->bus_clk);
	reset_control_assert(vdpo->reset);
}

static const struct component_ops sun8i_vdpo_ops = {
	.bind	= sun8i_vdpo_bind,
	.unbind	= sun8i_vdpo_unbind,
};

static int sun8i_vdpo_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sun8i_vdpo_ops);
}

static void sun8i_vdpo_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun8i_vdpo_ops);
}

static const struct of_device_id sun8i_vdpo_of_table[] = {
	{ .compatible = "allwinner,sun8i-v536-vdpo" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun8i_vdpo_of_table);

static struct platform_driver sun8i_vdpo_platform_driver = {
	.probe		= sun8i_vdpo_probe,
	.remove		= sun8i_vdpo_remove,
	.driver		= {
		.name		= "sun8i-vdpo",
		.of_match_table	= sun8i_vdpo_of_table,
	},
};
module_platform_driver(sun8i_vdpo_platform_driver);

MODULE_AUTHOR("Jörg Thalheim <joerg@thalheim.io>");
MODULE_DESCRIPTION("Allwinner V536 VDPO BT.1120 Encoder Driver");
MODULE_LICENSE("GPL");
