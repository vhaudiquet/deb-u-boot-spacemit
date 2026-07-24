// SPDX-License-Identifier: GPL-2.0+
/*
 * Generic GPIO based SBU switch
 *
 */

#include <common.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <asm/gpio.h>
#include <usb/tcpm.h>
#include <usb/typec_switch.h>

struct gpio_sbu_switch {
	struct gpio_desc *enable_gpio;
	struct gpio_desc *select_gpio;

	bool enabled;
	bool swapped;
};

static int gpio_sbu_switch_set(struct udevice *dev,
			       enum typec_orientation orientation)
{
	struct gpio_sbu_switch *sbu_switch = dev_get_priv(dev);
	bool enabled;
	bool swapped;

	enabled = sbu_switch->enabled;
	swapped = sbu_switch->swapped;

	switch (orientation) {
	case TYPEC_ORIENTATION_NONE:
		enabled = false;
		break;
	case TYPEC_ORIENTATION_NORMAL:
		enabled = true;
		swapped = false;
		break;
	case TYPEC_ORIENTATION_REVERSE:
		enabled = true;
		swapped = true;
		break;
	}

	if (enabled != sbu_switch->enabled)
		dm_gpio_set_value(sbu_switch->enable_gpio, enabled);

	if (swapped != sbu_switch->swapped)
		dm_gpio_set_value(sbu_switch->select_gpio, swapped);

	sbu_switch->enabled = enabled;
	sbu_switch->swapped = swapped;

	dev_info(dev, "set enabled: %d swapped: %d\n", enabled, swapped);

	return 0;
}

static int gpio_sbu_switch_probe(struct udevice *dev)
{
	struct gpio_sbu_switch *sbu_switch = dev_get_priv(dev);
	struct gpio_desc *enable_gpio;
	struct gpio_desc *select_gpio;

	enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_IS_OUT | GPIOD_IS_OUT_ACTIVE);
	if (IS_ERR(enable_gpio))
		return PTR_ERR(enable_gpio);

	select_gpio = devm_gpiod_get(dev, "select", GPIOD_IS_OUT | GPIOD_IS_OUT_ACTIVE);
	if (IS_ERR(select_gpio))
		return PTR_ERR(select_gpio);

	sbu_switch->enable_gpio = enable_gpio;
	sbu_switch->select_gpio = select_gpio;

	return 0;
}

static struct dm_typec_switch_ops gpio_sbu_switch_ops = {
        .set = gpio_sbu_switch_set,
};

static const struct udevice_id gpio_sbu_switch_ids[] = {
	{ .compatible = "gpio-sbu-switch" },
	{ }
};

U_BOOT_DRIVER(gpio_sbu_switch) = {
	.name		= "gpio_sbu_switch",
	.id		= UCLASS_TYPEC_SWITCH,
	.of_match	= gpio_sbu_switch_ids,
	.ops 		= &gpio_sbu_switch_ops,
	.probe		= gpio_sbu_switch_probe,
	.priv_auto	= sizeof(struct gpio_sbu_switch),
};
