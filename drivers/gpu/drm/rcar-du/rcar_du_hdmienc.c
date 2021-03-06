/*
 * R-Car Display Unit HDMI Encoder
 *
 * Copyright (C) 2014-2016 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/slab.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder_slave.h>

#include <linux/of_platform.h>

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_hdmienc.h"
#include "rcar_du_lvdsenc.h"

struct rcar_du_hdmienc {
	struct rcar_du_encoder *renc;
	struct device *dev;
	bool enabled;
	unsigned int index;
};

#define to_rcar_hdmienc(e)	(to_rcar_encoder(e)->hdmi)
#define to_slave_funcs(e)	(to_rcar_encoder(e)->slave.slave_funcs)

void rcar_du_hdmienc_disable(struct drm_encoder *encoder)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	const struct drm_encoder_slave_funcs *sfuncs = to_slave_funcs(encoder);

	const struct drm_bridge_funcs *bfuncs = encoder->bridge->funcs;

	if ((bfuncs) && (bfuncs->post_disable))
		bfuncs->post_disable(encoder->bridge);

	if ((sfuncs) && (sfuncs->dpms))
		sfuncs->dpms(encoder, DRM_MODE_DPMS_OFF);

	if (hdmienc->renc->lvds)
		rcar_du_lvdsenc_enable(hdmienc->renc->lvds, encoder->crtc,
				       false);

	hdmienc->enabled = false;
}

void rcar_du_hdmienc_enable(struct drm_encoder *encoder)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	const struct drm_encoder_slave_funcs *sfuncs = to_slave_funcs(encoder);
	const struct drm_bridge_funcs *bfuncs = encoder->bridge->funcs;

	if (hdmienc->renc->lvds)
		rcar_du_lvdsenc_enable(hdmienc->renc->lvds, encoder->crtc,
				       true);

	if ((bfuncs) && (bfuncs->enable))
		bfuncs->enable(encoder->bridge);

	if ((sfuncs) && (sfuncs->dpms))
		sfuncs->dpms(encoder, DRM_MODE_DPMS_ON);

	hdmienc->enabled = true;
}

static int rcar_du_hdmienc_atomic_check(struct drm_encoder *encoder,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	const struct drm_encoder_slave_funcs *sfuncs = to_slave_funcs(encoder);
	const struct drm_bridge_funcs *bfuncs = encoder->bridge->funcs;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	const struct drm_display_mode *mode = &crtc_state->mode;
	int ret = 0;

	if (hdmienc->renc->lvds)
		rcar_du_lvdsenc_atomic_check(hdmienc->renc->lvds,
					     adjusted_mode);

	if ((sfuncs) && (sfuncs->mode_fixup == NULL))
		return 0;

	if ((sfuncs) && (sfuncs->mode_fixup))
		ret = sfuncs->mode_fixup(encoder, mode,
				adjusted_mode) ? 0 : -EINVAL;

	if ((bfuncs) && (bfuncs->mode_fixup))
		ret = bfuncs->mode_fixup(encoder->bridge, mode,
				adjusted_mode) ? 0 : -EINVAL;
	return ret;
}

static void rcar_du_hdmienc_mode_set(struct drm_encoder *encoder,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	const struct drm_encoder_slave_funcs *sfuncs = to_slave_funcs(encoder);
	const struct drm_bridge_funcs *bfuncs = encoder->bridge->funcs;

	if ((sfuncs) && (sfuncs->mode_set))
		sfuncs->mode_set(encoder, mode, adjusted_mode);

	if ((bfuncs) && (bfuncs->mode_set))
		bfuncs->mode_set(encoder->bridge, mode, adjusted_mode);

	rcar_du_crtc_route_output(encoder->crtc, hdmienc->renc->output);
}

static const struct drm_encoder_helper_funcs encoder_helper_funcs = {
	.mode_set = rcar_du_hdmienc_mode_set,
	.disable = rcar_du_hdmienc_disable,
	.enable = rcar_du_hdmienc_enable,
	.atomic_check = rcar_du_hdmienc_atomic_check,
};

static void rcar_du_hdmienc_cleanup(struct drm_encoder *encoder)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);

	if (hdmienc->enabled)
		rcar_du_hdmienc_disable(encoder);

	drm_encoder_cleanup(encoder);
	put_device(hdmienc->dev);
}

static const struct drm_encoder_funcs encoder_funcs = {
	.destroy = rcar_du_hdmienc_cleanup,
};

static const struct dw_hdmi_mpll_config rcar_du_hdmienc_mpll_cfg[] = {
	{
		44900000, {
			{ 0x0003, 0x0000},
			{ 0x0003, 0x0000},
			{ 0x0003, 0x0000}
		},
	}, {
		90000000, {
			{ 0x0002, 0x0000},
			{ 0x0002, 0x0000},
			{ 0x0002, 0x0000}
		},
	}, {
		182750000, {
			{ 0x0001, 0x0000},
			{ 0x0001, 0x0000},
			{ 0x0001, 0x0000}
		},
	}, {
		297000000, {
			{ 0x0000, 0x0000},
			{ 0x0000, 0x0000},
			{ 0x0000, 0x0000}
		},
	}, {
		~0UL, {
			{ 0xFFFF, 0xFFFF },
			{ 0xFFFF, 0xFFFF },
			{ 0xFFFF, 0xFFFF },
		},
	}
};
static const struct dw_hdmi_curr_ctrl rcar_du_hdmienc_cur_ctr[] = {
	/*      pixelclk    bpp8    bpp10   bpp12 */
	{
		35500000,  { 0x0344, 0x0000, 0x0000 },
	}, {
		44900000,  { 0x0285, 0x0000, 0x0000 },
	}, {
		71000000,  { 0x1184, 0x0000, 0x0000 },
	}, {
		90000000,  { 0x1144, 0x0000, 0x0000 },
	}, {
		140250000, { 0x20c4, 0x0000, 0x0000 },
	}, {
		182750000, { 0x2084, 0x0000, 0x0000 },
	}, {
		297000000, { 0x0084, 0x0000, 0x0000 },
	}, {
		~0UL,      { 0x0000, 0x0000, 0x0000 },
	}
};

static const struct dw_hdmi_multi_div rcar_du_hdmienc_multi_div[] = {
	/*      pixelclk    bpp8    bpp10   bpp12 */
	{
		35500000,  { 0x0328, 0x0000, 0x0000 },
	}, {
		44900000,  { 0x0128, 0x0000, 0x0000 },
	}, {
		71000000,  { 0x0314, 0x0000, 0x0000 },
	}, {
		90000000,  { 0x0114, 0x0000, 0x0000 },
	}, {
		140250000, { 0x030a, 0x0000, 0x0000 },
	}, {
		182750000, { 0x010a, 0x0000, 0x0000 },
	}, {
		281250000, { 0x0305, 0x0000, 0x0000 },
	}, {
		297000000, { 0x0105, 0x0000, 0x0000 },
	}, {
		~0UL,      { 0x0000, 0x0000, 0x0000 },
	}
};

static const struct dw_hdmi_phy_config rcar_du_hdmienc_phy_config[] = {
	/*pixelclk   symbol   term   vlev*/
	{ 74250000,  0x8009, 0x0004, 0x0272},
	{ 148500000, 0x802b, 0x0004, 0x028d},
	{ 297000000, 0x8039, 0x0005, 0x028d},
	{ ~0UL,	     0x0000, 0x0000, 0x0000}
};

static enum drm_mode_status
rcar_du_hdmienc_mode_valid(struct drm_connector *connector,
			    struct drm_display_mode *mode)
{
	if ((mode->hdisplay > 3840) || (mode->vdisplay > 2160))
		return MODE_BAD;

	if (((mode->hdisplay == 3840) && (mode->vdisplay == 2160))
		&& (mode->vrefresh > 30))
		return MODE_BAD;

	if (mode->clock > 297000)
		return MODE_BAD;

	return MODE_OK;
}

static const struct dw_hdmi_plat_data rcar_du_hdmienc_hdmi0_drv_data = {
	.mode_valid = rcar_du_hdmienc_mode_valid,
	.mpll_cfg   = rcar_du_hdmienc_mpll_cfg,
	.cur_ctr    = rcar_du_hdmienc_cur_ctr,
	.multi_div  = rcar_du_hdmienc_multi_div,
	.phy_config = rcar_du_hdmienc_phy_config,
	.dev_type   = RCAR_HDMI,
	.index      = 0,
};

static const struct dw_hdmi_plat_data rcar_du_hdmienc_hdmi1_drv_data = {
	.mode_valid = rcar_du_hdmienc_mode_valid,
	.mpll_cfg   = rcar_du_hdmienc_mpll_cfg,
	.cur_ctr    = rcar_du_hdmienc_cur_ctr,
	.multi_div  = rcar_du_hdmienc_multi_div,
	.phy_config = rcar_du_hdmienc_phy_config,
	.dev_type   = RCAR_HDMI,
	.index      = 1,
};

static const struct of_device_id rcar_du_hdmienc_dt_ids[] = {
	{
		.data = &rcar_du_hdmienc_hdmi0_drv_data
	},
	{
		.data = &rcar_du_hdmienc_hdmi1_drv_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, rcar_du_hdmienc_dt_ids);

int rcar_du_hdmienc_init(struct rcar_du_device *rcdu,
			 struct rcar_du_encoder *renc, struct device_node *np)
{
	struct drm_encoder *encoder = rcar_encoder_to_drm_encoder(renc);
	struct drm_i2c_encoder_driver *driver;
	struct i2c_client *i2c_slave;
	struct rcar_du_hdmienc *hdmienc;
	struct resource *iores;
	struct platform_device *pdev;
	const struct dw_hdmi_plat_data *plat_data;
	int ret, irq;
	bool dw_hdmi_use = false;

	hdmienc = devm_kzalloc(rcdu->dev, sizeof(*hdmienc), GFP_KERNEL);
	if (hdmienc == NULL)
		return -ENOMEM;

	if (strcmp(renc->device_name, "rockchip,rcar-dw-hdmi") == 0)
		dw_hdmi_use = true;

	if (dw_hdmi_use) {
		if (renc->output == RCAR_DU_OUTPUT_HDMI0)
			hdmienc->index = 0;
		else if (renc->output == RCAR_DU_OUTPUT_HDMI1)
			hdmienc->index = 1;
		else
			return -EINVAL;

		np = of_parse_phandle(rcdu->dev->of_node, "hdmi",
							hdmienc->index);
		if (!np) {
			dev_err(rcdu->dev, "hdmi node not found\n");
			return -ENXIO;
		}

		pdev = of_find_device_by_node(np);
		of_node_put(np);
		if (!pdev)
			return -ENXIO;

		plat_data = rcar_du_hdmienc_dt_ids[hdmienc->index].data;
		hdmienc->dev = &pdev->dev;

		irq = platform_get_irq(pdev, 0);
		if (irq < 0)
			return irq;

		iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!iores)
			return -ENXIO;

	} else {
		/* Locate the slave I2C device and driver. */
		i2c_slave = of_find_i2c_device_by_node(np);
		if (!i2c_slave || !i2c_get_clientdata(i2c_slave))
			return -EPROBE_DEFER;

		hdmienc->dev = &i2c_slave->dev;

		if (hdmienc->dev->driver == NULL) {
			ret = -EPROBE_DEFER;
			goto error;
		}

		/* Initialize the slave encoder. */
		driver = to_drm_i2c_encoder_driver
			(to_i2c_driver(hdmienc->dev->driver));
		ret = driver->encoder_init(i2c_slave, rcdu->ddev, &renc->slave);
		if (ret < 0)
			goto error;
	}

	ret = drm_encoder_init(rcdu->ddev, encoder, &encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret < 0)
		goto error;

	drm_encoder_helper_add(encoder, &encoder_helper_funcs);

	renc->hdmi = hdmienc;
	hdmienc->renc = renc;

	if (dw_hdmi_use)
		ret = dw_hdmi_bind(rcdu->dev, NULL, rcdu->ddev, encoder,
				iores, irq, plat_data);

	return ret;

error:
	put_device(hdmienc->dev);
	return ret;
}
