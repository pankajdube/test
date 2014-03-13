/*
 * PRCM reset driver for TI SoC's
 *
 * Copyright 2014 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/reset/reset_ti.h>
#include <linux/slab.h>

#include <linux/reset.h>
#include <linux/reset-controller.h>

struct ti_reset_reg_data {
	struct list_head link;
	void __iomem *reg_base;
	u32	rstctrl_offs;
	u32	rstst_offs;
	u8	rstctrl_bit;
	u8	rstst_bit;
	u8  id;
};

struct ti_reset_data {
	struct	ti_reset_reg_data *reg_data;
	struct reset_controller_dev	rcdev;
	struct list_head link;
	const char *name;
};

static LIST_HEAD(reset_list);

static struct ti_reset_reg_data *ti_reset_get_data(unsigned long id)
{
	struct ti_reset_reg_data *r;

	list_for_each_entry(r, &reset_list, link) {
		if (!r) {
			pr_err("%s: Reset Controller is not valid\n", __func__);
			return NULL;
		}
		if (r->id == id)
			break;
	}

	return r;
}

static int ti_reset_reset(struct reset_controller_dev *rcdev,
				  unsigned long id)
{
	struct ti_reset_reg_data *r;
	void __iomem *reg;
	u32 val = 0;
	u8 bit = 0;

	r = ti_reset_get_data(id);
	if (!r) {
		pr_err("%s: Cannot find the data\n", __func__);
		return -ENODEV;
	}

	/* TODO: Block return until the device is reset */
	reg = r->reg_base + r->rstctrl_offs;
	bit = r->rstctrl_bit;
	val = readl(reg);
	val &= ~(1 << bit);
	val |= 1 << bit;
	writel(val, reg);

	return 0;
}

static int ti_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct ti_reset_reg_data *r;
	void __iomem *reg;
	void __iomem *status_reg;
	u32 val = 0;
	u8 bit = 0;
	u8 status_bit = 0;

	r = ti_reset_get_data(id);
	if (!r) {
		pr_err("%s: Cannot find the data\n", __func__);
		return -ENODEV;
	}

	/* Clear the reset status bit to reflect the current status */
	status_reg = r->reg_base + r->rstst_offs;
	status_bit = r->rstst_bit;
	writel((1 << status_bit), status_reg);

	reg = r->reg_base + r->rstctrl_offs;
	bit = r->rstctrl_bit;
	val = readl(reg);
	if (!(val & (1 << bit))) {
		val |= (1 << bit);
		writel(val, reg);
	}

	return 0;
}

static int ti_reset_deassert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct ti_reset_reg_data *r;
	void __iomem *reg;
	void __iomem *status_reg;
	u32 val = 0;
	u8 bit = 0;
	u8 status_bit = 0;

	r = ti_reset_get_data(id);
	if (!r) {
		pr_err("%s: Cannot find the data\n", __func__);
		return -ENODEV;
	}

	/* Clear the reset status bit to reflect the current status */
	status_reg = r->reg_base + r->rstst_offs;
	status_bit = r->rstst_bit;
	writel((1 << status_bit), status_reg);

	reg = r->reg_base + r->rstctrl_offs;
	bit = r->rstctrl_bit;
	val = readl(reg);
	if (val & (1 << bit)) {
		val &= ~(1 << bit);
		writel(val, reg);
	}

	return 0;
}

static int ti_reset_xlate(struct reset_controller_dev *rcdev,
			const struct of_phandle_args *reset_spec)
{
	struct device_node *resets;

	for_each_child_of_node(reset_spec->np, resets) {
		if (resets->phandle == reset_spec->args[0])
				return resets->phandle;
	}

	return -EINVAL;
}

static struct reset_control_ops ti_reset_ops = {
	.reset = ti_reset_reset,
	.assert = ti_reset_assert,
	.deassert = ti_reset_deassert,
};

void ti_dt_reset_init(struct device_node *parent)
{
	struct ti_reset_data *ti_data;
	struct ti_reset_reg_data *reset_reg_data;
	struct device_node *resets;
	struct device_node *np;
	int num_resets = 0, dt_err;
	u32 reg_val;
	u8 bit_val;

	/* get resets for this parent */
	resets = of_get_child_by_name(parent, "resets");
	if (!resets) {
		pr_debug("%s missing 'resets' child node.\n", parent->name);
		return;
	}

	ti_data = kzalloc(sizeof(*ti_data), GFP_KERNEL);
	if (!ti_data)
		return;

	for_each_child_of_node(resets, np) {
		pr_debug("%s: initializing reset: %s\n", __func__, np->name);

		reset_reg_data = kzalloc(sizeof(*reset_reg_data), GFP_KERNEL);
		if (!reset_reg_data)
			goto err_fail_child_node;

		dt_err = of_property_read_u32(np, "rstctrl_offs", &reg_val);
		if (dt_err < 0) {
			pr_err("%s: No entry in %s for rstctrl_offs\n", __func__, np->name);
			goto err_failed_dt_node;
		}
		reset_reg_data->rstctrl_offs = reg_val;

		dt_err = of_property_read_u8(np, "ctrl_bit-shift", &bit_val);
		if (dt_err < 0) {
			pr_err("%s: No entry in %s for ctrl_bit-shift\n",
					__func__, np->name);
			goto err_failed_dt_node;
		}
		reset_reg_data->rstctrl_bit = bit_val;

		dt_err = of_property_read_u32(np, "rstst_offs", &reg_val);
		if (dt_err < 0) {
			pr_err("%s: No entry in %s for rstst_offs\n", __func__, np->name);
			goto err_failed_dt_node;
		}
		reset_reg_data->rstst_offs = reg_val;

		dt_err = of_property_read_u8(np, "sts_bit-shift", &bit_val);
		if (dt_err < 0) {
			pr_err("%s: No entry in %s for sts_bit-shift\n",
					__func__, np->name);
			goto err_failed_dt_node;
		}
		reset_reg_data->rstst_bit = bit_val;

		reset_reg_data->id = np->phandle;
		reset_reg_data->reg_base = of_iomap(parent, 0);

		list_add(&reset_reg_data->link, &reset_list);
		num_resets++;

	}

	ti_data->rcdev.owner = THIS_MODULE;
	ti_data->rcdev.nr_resets = num_resets + 1;
	ti_data->rcdev.of_reset_n_cells = 1;
	ti_data->rcdev.of_xlate = &ti_reset_xlate;
	ti_data->rcdev.of_node = resets;
	ti_data->rcdev.ops = &ti_reset_ops;

	reset_controller_register(&ti_data->rcdev);
	return;

err_failed_dt_node:
	/* TODO: Need to free each data set in the list */
	kfree(reset_reg_data);
err_fail_child_node:
	kfree(ti_data);

}

MODULE_DESCRIPTION("PRCM reset driver for TI SoC's");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
