/*
 * s2mpb02-core.c - mfd core driver for the Samsung s2mpb02
 *
 * Copyright (C) 2014 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This driver is based on max77804.c
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/mfd/s2mpb02.h>
#include <linux/mfd/s2mpb02-private.h>
#include <linux/regulator/machine.h>

#if defined (CONFIG_OF)
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#endif /* CONFIG_OF */

extern unsigned int system_rev;

static struct mfd_cell s2mpb02_devs[] = {
#if defined(CONFIG_LEDS_S2MPB02)
	{ .name = "s2mpb02-led", },
#endif /* CONFIG_LEDS_S2MPB02 */
	{ .name = "s2mpb02-regulator", },
};

int s2mpb02_read_reg(struct i2c_client *i2c, u8 reg, u8 *dest)
{
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&s2mpb02->i2c_lock);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	mutex_unlock(&s2mpb02->i2c_lock);
	if (ret < 0) {
		pr_info("%s:%s reg(0x%x), ret(%d)\n", MFD_DEV_NAME, __func__, reg, ret);
		return ret;
	}

	ret &= 0xff;
	*dest = ret;
	return 0;
}
EXPORT_SYMBOL_GPL(s2mpb02_read_reg);

int s2mpb02_bulk_read(struct i2c_client *i2c, u8 reg, int count, u8 *buf)
{
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&s2mpb02->i2c_lock);
	ret = i2c_smbus_read_i2c_block_data(i2c, reg, count, buf);
	mutex_unlock(&s2mpb02->i2c_lock);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(s2mpb02_bulk_read);

int s2mpb02_write_reg(struct i2c_client *i2c, u8 reg, u8 value)
{
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&s2mpb02->i2c_lock);
	ret = i2c_smbus_write_byte_data(i2c, reg, value);
	mutex_unlock(&s2mpb02->i2c_lock);
	if (ret < 0)
		pr_info("%s:%s reg(0x%x), ret(%d)\n",
				MFD_DEV_NAME, __func__, reg, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(s2mpb02_write_reg);

int s2mpb02_bulk_write(struct i2c_client *i2c, u8 reg, int count, u8 *buf)
{
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&s2mpb02->i2c_lock);
	ret = i2c_smbus_write_i2c_block_data(i2c, reg, count, buf);
	mutex_unlock(&s2mpb02->i2c_lock);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(s2mpb02_bulk_write);

int s2mpb02_update_reg(struct i2c_client *i2c, u8 reg, u8 val, u8 mask)
{
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&s2mpb02->i2c_lock);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	if (ret >= 0) {
		u8 old_val = ret & 0xff;
		u8 new_val = (val & mask) | (old_val & (~mask));
		ret = i2c_smbus_write_byte_data(i2c, reg, new_val);
	}
	mutex_unlock(&s2mpb02->i2c_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(s2mpb02_update_reg);


static int s2mpb02_i2c_pinctrl_init(struct s2mpb02_dev *s2mpb02)
{
	int retval;

	/* Get pinctrl if target uses pinctrl */
	s2mpb02->max_pinctrl = devm_pinctrl_get(s2mpb02->dev);
	if (IS_ERR_OR_NULL(s2mpb02->max_pinctrl)) {
		dev_dbg(s2mpb02->dev, "Target does not use pinctrl\n");
		retval = PTR_ERR(s2mpb02->max_pinctrl);
		s2mpb02->max_pinctrl = NULL;
		return retval;
	}

	s2mpb02->gpio_state_active
		= pinctrl_lookup_state(s2mpb02->max_pinctrl,
					"s2mpb02_interrupt_pins_active");
	if (IS_ERR_OR_NULL(s2mpb02->gpio_state_active)) {
		dev_dbg(s2mpb02->dev, "Can not get ts default pinstate\n");
		retval = PTR_ERR(s2mpb02->gpio_state_active);
		s2mpb02->max_pinctrl = NULL;
		return retval;
	}

	s2mpb02->gpio_state_suspend
		= pinctrl_lookup_state(s2mpb02->max_pinctrl,
					"s2mpb02_interrupt_pins_suspend");

	if (IS_ERR_OR_NULL(s2mpb02->gpio_state_suspend)) {
		dev_dbg(s2mpb02->dev,
			"Can not get ts sleep pinstate\n");
		retval = PTR_ERR(s2mpb02->gpio_state_suspend);
		s2mpb02->max_pinctrl = NULL;
		return retval;
	}

	return 0;
}

static int s2mpb02_i2c_pinctrl_select(struct s2mpb02_dev *s2mpb02, bool on)
{
	struct pinctrl_state *pins_state;
	int ret;

	pins_state = on ? s2mpb02->gpio_state_active
			: s2mpb02->gpio_state_suspend;

	if (!IS_ERR_OR_NULL(pins_state)) {
		ret = pinctrl_select_state(s2mpb02->max_pinctrl, pins_state);
		if (ret) {
			pr_err("can not set %s pins\n",
				on ? "s2mpb02_interrupt_pins_active"
				: "s2mpb02_interrupt_pins_suspend");
			return ret;
		}
	} else
		pr_err("not a valid '%s' pinstate\n",
				on ? "s2mpb02_interrupt_pins_active"
				: "s2mpb02_interrupt_pins_suspend");

	return 0;
}



#if defined(CONFIG_OF)
static int of_s2mpb02_dt(struct device *dev, struct s2mpb02_platform_data *pdata)
{
	struct device_node *np_s2mpb02 = dev->of_node;

	if(!np_s2mpb02)
		return -EINVAL;

	pdata->irq_gpio = of_get_named_gpio(np_s2mpb02, "s2mpb02,irq-gpio", 0);
	pdata->wakeup = of_property_read_bool(np_s2mpb02, "s2mpb02,wakeup");

	return 0;
}
#else
static int of_s2mpb02_dt(struct device *dev, struct s2mpb02_platform_data *pdata)
{
	return 0;
}
#endif /* CONFIG_OF */

static int s2mpb02_i2c_probe(struct i2c_client *i2c,
				const struct i2c_device_id *dev_id)
{
	struct s2mpb02_dev *s2mpb02;
	struct s2mpb02_platform_data *pdata = i2c->dev.platform_data;

	int ret = 0;
	u8 reg_data;

	pr_err("<%s> start\n", __func__);

	s2mpb02 = kzalloc(sizeof(struct s2mpb02_dev), GFP_KERNEL);
	if (!s2mpb02) {
		dev_err(&i2c->dev, "%s: Failed to alloc mem for s2mpb02\n", __func__);
		return -ENOMEM;
	}

	if (i2c->dev.of_node) {
		pdata = devm_kzalloc(&i2c->dev, sizeof(struct s2mpb02_platform_data),
				GFP_KERNEL);
		if (!pdata) {
			dev_err(&i2c->dev, "Failed to allocate memory \n");
			ret = -ENOMEM;
			goto err;
		}

		ret = of_s2mpb02_dt(&i2c->dev, pdata);
		if (ret < 0){
			dev_err(&i2c->dev, "Failed to get device of_node \n");
			goto err;
		}

		i2c->dev.platform_data = pdata;
	} else
		pdata = i2c->dev.platform_data;

	s2mpb02->dev = &i2c->dev;
	s2mpb02->i2c = i2c;
	s2mpb02->irq = i2c->irq;
	if (pdata) {
		s2mpb02->pdata = pdata;

		pdata->irq_base = irq_alloc_descs(-1, 0, S2MPB02_IRQ_NR, -1);
		if (pdata->irq_base < 0) {
			pr_err("%s:%s irq_alloc_descs Fail! ret(%d)\n",
					MFD_DEV_NAME, __func__, pdata->irq_base);
			ret = -EINVAL;
			goto err;
		} else
			s2mpb02->irq_base = pdata->irq_base;

		s2mpb02->irq_gpio = pdata->irq_gpio;
		s2mpb02->wakeup = pdata->wakeup;
	} else {
		ret = -EINVAL;
		goto err;
	}

	ret = s2mpb02_i2c_pinctrl_init(s2mpb02);
	if (!ret && s2mpb02->max_pinctrl) {
		ret = s2mpb02_i2c_pinctrl_select(s2mpb02, true);
		if (ret < 0)
			goto err;
	}

	mutex_init(&s2mpb02->i2c_lock);

	i2c_set_clientdata(i2c, s2mpb02);

	if (s2mpb02_read_reg(s2mpb02->i2c, S2MPB02_REG_ID, &reg_data) < 0) {
		pr_err("device not found on this channel!!\n");
		ret = -ENODEV;
	} else {
		pr_err("%s: device id 0x%x is found\n",
							__func__, reg_data);
	}

#if 0
	ret = s2mpb02_irq_init(s2mpb02);
	if (ret < 0)
		goto err_irq_init;
#endif

	ret = mfd_add_devices(s2mpb02->dev, -1, s2mpb02_devs,
			ARRAY_SIZE(s2mpb02_devs), NULL, 0, NULL);
	if (ret < 0)
		goto err_mfd;

	device_init_wakeup(s2mpb02->dev, pdata->wakeup);

	pr_err("<%s> end\n", __func__);
	return ret;

err_mfd:
	mfd_remove_devices(s2mpb02->dev);
#if 0
err_irq_init:
	mutex_destroy(&s2mpb02->i2c_lock);
#endif
err:
	kfree(s2mpb02);
	return ret;
}

static int s2mpb02_i2c_remove(struct i2c_client *i2c)
{
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);

	mfd_remove_devices(s2mpb02->dev);
	kfree(s2mpb02);

	return 0;
}

static const struct i2c_device_id s2mpb02_i2c_id[] = {
	{ MFD_DEV_NAME, TYPE_S2MPB02 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, s2mpb02_i2c_id);

#if defined(CONFIG_OF)
static struct of_device_id s2mpb02_i2c_dt_ids[] = {
	{ .compatible = "samsung,s2mpb02" },
	{},
};
MODULE_DEVICE_TABLE(of, s2mpb02_i2c_dt_ids);
#endif /* CONFIG_OF */

#if defined(CONFIG_PM)
static int s2mpb02_suspend(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		enable_irq_wake(s2mpb02->irq);

	disable_irq(s2mpb02->irq);

	return 0;
}

static int s2mpb02_resume(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct s2mpb02_dev *s2mpb02 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		disable_irq_wake(s2mpb02->irq);

	enable_irq(s2mpb02->irq);

	return 0;
}
#else
#define s2mpb02_suspend	NULL
#define s2mpb02_resume	NULL
#endif /* CONFIG_PM */

const struct dev_pm_ops s2mpb02_pm = {
	.suspend = s2mpb02_suspend,
	.resume = s2mpb02_resume,
};

static struct i2c_driver s2mpb02_i2c_driver = {
	.driver		= {
		.name	= MFD_DEV_NAME,
		.owner	= THIS_MODULE,
#if defined(CONFIG_PM)
		.pm	= &s2mpb02_pm,
#endif /* CONFIG_PM */
#if defined(CONFIG_OF)
		.of_match_table	= s2mpb02_i2c_dt_ids,
#endif /* CONFIG_OF */
	},
	.probe		= s2mpb02_i2c_probe,
	.remove		= s2mpb02_i2c_remove,
	.id_table	= s2mpb02_i2c_id,
};

static int __init s2mpb02_i2c_init(void)
{
	pr_info("%s:%s\n", MFD_DEV_NAME, __func__);

	return i2c_add_driver(&s2mpb02_i2c_driver);
}
/* init early so consumer devices can complete system boot */
fs_initcall(s2mpb02_i2c_init);

static void __exit s2mpb02_i2c_exit(void)
{
	i2c_del_driver(&s2mpb02_i2c_driver);
}
module_exit(s2mpb02_i2c_exit);

MODULE_DESCRIPTION("SAMSUNG s2mpb02 multi-function core driver");
MODULE_AUTHOR("XXX <xxx@samsung.com>");
MODULE_LICENSE("GPL");
