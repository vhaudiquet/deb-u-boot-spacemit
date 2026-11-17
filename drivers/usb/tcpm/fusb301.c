// SPDX-License-Identifier: GPL-2.0+
/*
 * fusb301 typec controller
 */

#include <common.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <errno.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <i2c.h>
#include <usb/tcpm.h>
#include <usb/typec_switch.h>

/* Register Map */
#define FUSB301_REG_DEVICEID			0x01
#define FUSB301_REG_MODES			0x02
#define FUSB301_REG_CONTROL			0x03
#define FUSB301_REG_MANUAL			0x04
#define FUSB301_REG_RESET			0x05
#define FUSB301_REG_MASK			0x10
#define FUSB301_REG_STATUS			0x11
#define FUSB301_REG_TYPE			0x12
#define FUSB301_REG_INTERRUPT			0x13

/* Register Bits */
#define FUSB301_DEVICE_ID_VERSION_ID		GENMASK(7, 4)
#define FUSB301_DEVICE_ID_REVISON_ID		GENMASK(3, 0)

#define FUSB301_MODES_MASK			GENMASK(5, 0)
#define   FUSB301_MODES_DRP_ACC			BIT(5)
#define   FUSB301_MODES_DRP			BIT(4)
#define   FUSB301_MODES_SNK_ACC			BIT(3)
#define   FUSB301_MODES_SNK			BIT(2)
#define   FUSB301_MODES_SRC_ACC			BIT(1)
#define   FUSB301_MODES_SRC			BIT(0)

#define FUSB301_CONTROL_TGL_MASK		GENMASK(5, 4)
#define   FUSB301_CONTROL_TGL_35MS		0
#define   FUSB301_CONTROL_TGL_30MS		1
#define   FUSB301_CONTROL_TGL_25MS		2
#define   FUSB301_CONTROL_TGL_20MS		3
#define FUSB301_CONTROL_HOST_CUR_MASK		GENMASK(2, 1)
#define   FUSB301_HOST_CUR_0			0
#define   FUSB301_HOST_CUR_DEFAULT		1
#define   FUSB301_HOST_CUR_1500MA		2
#define   FUSB301_HOST_CUR_3000MA		3
#define FUSB301_CONTROL_INT_MASK		BIT(0)

#define FUSB301_MANUAL_MASK			GENMASK(3, 0)
#define FUSB301_MANUAL_UNATT_SNK		BIT(3)
#define FUSB301_MANUAL_UNATT_SRC		BIT(2)
#define FUSB301_MANUAL_DISABLED			BIT(1)
#define FUSB301_MANUAL_ERR_RECOVERY		BIT(0)

#define FUSB301_RESET_SW_RES			BIT(0)

#define FUSB301_MASK_M_ACC_CH			BIT(3)
#define FUSB301_MASK_M_BCLVL			BIT(2)
#define FUSB301_MASK_M_DETACH			BIT(1)
#define FUSB301_MASK_M_ATTACH			BIT(0)

#define FUSB301_STATUS_ORIENT_MASK		GENMASK(5, 4)
#define   FUSB301_STATUS_ORIENT_FAULT_CC	3
#define   FUSB301_STATUS_ORIENT_CC2		2
#define   FUSB301_STATUS_ORIENT_CC1		1
#define   FUSB301_STATUS_ORIENT_NO_CONN		0
#define FUSB301_STATUS_VBUS_OK			BIT(3)
#define   FUSB301_STATUS_BC_LVL_MASK		GENMASK(2, 1)
#define   FUSB301_STATUS_SNK_0MA		0
#define   FUSB301_STATUS_SNK_DEFAULT		1
#define   FUSB301_STATUS_SNK_1500MA		2
#define   FUSB301_STATUS_SNK_3000MA		3
#define FUSB301_STATUS_ATTACH			BIT(0)

#define FUSB301_TYPE_SNK			BIT(4)
#define FUSB301_TYPE_SRC			BIT(3)
#define FUSB301_TYPE_PWR_ACC			BIT(2)
#define FUSB301_TYPE_DBG_ACC			BIT(1)
#define FUSB301_TYPE_AUD_ACC			BIT(0)
#define FUSB301_TYPE_PWR_DBG_ACC		(FUSB301_TYPE_PWR_ACC | FUSB301_TYPE_DBG_ACC)
#define FUSB301_TYPE_PWR_AUD_ACC		(FUSB301_TYPE_PWR_ACC | FUSB301_TYPE_AUD_ACC)
#define FUSB301_TYPE_INVALID			0

#define FUSB301_INT_ACC				BIT(3)
#define FUSB301_INT_BCLVL			BIT(2)
#define FUSB301_INT_DETACH			BIT(1)
#define FUSB301_INT_ATTACH			BIT(0)

#define FUSB301_REV10				0x10
#define FUSB301_REV11				0x11
#define FUSB301_REV12				0x12

struct fusb301_chip {
	struct udevice *dev;
	struct udevice *sw;
	u8 dev_id;
	u8 mode;
	u8 pwr_mode;
	u8 dttime;
	u8 state;
	enum typec_orientation orient;
	bool vbus_present;
};

enum fusb301_state {
	FUSB_STATE_DISABLED,
	FUSB_STATE_ERROR_RECOVERY,
	FUSB_STATE_UNATTACHED_SNK,
	FUSB_STATE_UNATTACHED_SRC,
	FUSB_STATE_ATTACHWAIT_SNK,
	FUSB_STATE_ATTACHWAIT_SRC,
	FUSB_STATE_ATTACHED_SNK,
	FUSB_STATE_ATTACHED_SRC,
	FUSB_STATE_AUDIO_ACCESSORY,
	FUSB_STATE_DEBUG_ACCESSORY,
	FUSB_STATE_TRY_SNK,
	FUSB_STATE_TRYWAIT_SRC,
	FUSB_STATE_TRY_SRC,
	FUSB_STATE_TRYWAIT_SNK,
};

static const char * const fusb301_pwr_mode_name[] = {
	[FUSB301_HOST_CUR_0]		= "none",
	[FUSB301_HOST_CUR_DEFAULT]	= "default",
	[FUSB301_HOST_CUR_1500MA]	= "1.5A",
	[FUSB301_HOST_CUR_3000MA]	= "3.0A",
};

static const char *const fusb301_toggle_name[] = {
	[FUSB301_CONTROL_TGL_35MS]	= "Toggle_35ms",
	[FUSB301_CONTROL_TGL_30MS]	= "Toggle_30ms",
	[FUSB301_CONTROL_TGL_25MS]	= "Toggle_25ms",
	[FUSB301_CONTROL_TGL_20MS]	= "Toggle_20ms",
};

static const char *const fusb301_mode_name[] = {
	[FUSB301_MODES_DRP_ACC]		= "Drp_Acc",
	[FUSB301_MODES_DRP]		= "Drp",
	[FUSB301_MODES_SNK_ACC]		= "Snk_Acc",
	[FUSB301_MODES_SNK]		= "Snk",
	[FUSB301_MODES_SRC_ACC]		= "Src_Acc",
	[FUSB301_MODES_SRC]		= "Src",
};

static const char *const fusb301_state_name[] = {
	[FUSB_STATE_DISABLED]		= "Disabled",
	[FUSB_STATE_ERROR_RECOVERY]	= "Error_Recovery",
	[FUSB_STATE_UNATTACHED_SNK]	= "Unattached_Snk",
	[FUSB_STATE_UNATTACHED_SRC]	= "Unattached_Src",
	[FUSB_STATE_ATTACHWAIT_SNK]	= "AttachWait_Snk",
	[FUSB_STATE_ATTACHWAIT_SRC]	= "AttachWait_Src",
	[FUSB_STATE_ATTACHED_SNK]	= "Attached_Snk",
	[FUSB_STATE_ATTACHED_SRC]	= "Attached_Src",
	[FUSB_STATE_AUDIO_ACCESSORY]	= "Audio_Accessory",
	[FUSB_STATE_DEBUG_ACCESSORY]	= "Debug_Accessory",
	[FUSB_STATE_TRY_SNK]		= "Try_Snk",
	[FUSB_STATE_TRYWAIT_SRC]	= "TryWait_Src",
	[FUSB_STATE_TRY_SRC]		= "Try_Src",
	[FUSB_STATE_TRYWAIT_SNK]	= "TryWait_Snk",
};

static int fusb301_i2c_write(struct udevice *dev, u8 offset, u8 data)
{
	int ret;

	ret = dm_i2c_reg_write(dev, offset, data);
	if (ret)
		dev_err(dev, "cannot write 0x%02x to 0x%02x, ret=%d\n",
			data, offset, ret);

	return ret;
}

static int fusb301_i2c_read(struct udevice *dev, u8 offset, u8 *data)
{
	int ret, retries;

	for (retries = 0; retries < 3; retries++) {
		ret = dm_i2c_reg_read(dev, offset);
		if (ret >= 0) {
			*data = (u8)ret;
			return 0;
		}
	}

	dev_err(dev, "cannot read %02x, ret=%d\n", offset, ret);

	return ret;
}

static int fusb301_i2c_write_bits(struct udevice *dev, u8 offset, u8 clr, u8 set)
{
	int ret;

	ret = dm_i2c_reg_clrset(dev, offset, clr, set);
	if (ret)
		dev_err(dev, "cannot write bits 0x%02x to 0x%02x, ret=%d\n",
			set, offset, ret);

	return ret;
}

static int fusb301_check_device_id(struct fusb301_chip *chip)
{
	struct udevice *dev = chip->dev;
	u8 device_id;
	int ret;

	ret = fusb301_i2c_read(dev, FUSB301_REG_DEVICEID, &device_id);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read device id: %d\n", ret);
		return ret;
	}
	dev_info(chip->dev, "Device ID = 0x%02x\n", device_id);

	if ((device_id != FUSB301_REV10) &&
	    (device_id != FUSB301_REV11) &&
	    (device_id != FUSB301_REV12))
		return -ENODEV;

	chip->dev_id = device_id;
	return 0;
}

static int fusb301_update_status(struct fusb301_chip *chip)
{
	struct udevice *dev = chip->dev;
	u8 ctrl, mode, status;
	int ret;

	ret = fusb301_i2c_read(dev, FUSB301_REG_MODES, &mode);
	if (ret < 0)
		return ret;

	ret = fusb301_i2c_read(dev, FUSB301_REG_CONTROL, &ctrl);
	if (ret < 0)
		return ret;

	chip->mode = FIELD_GET(FUSB301_MODES_MASK, mode);
	chip->pwr_mode = FIELD_GET(FUSB301_CONTROL_HOST_CUR_MASK, ctrl);
	chip->dttime = FIELD_GET(FUSB301_CONTROL_TGL_MASK, ctrl);

	dev_info(dev, "mode[0x%02x], host_cur[0x%02x], dttime[0x%02x]\n",
		 chip->mode, chip->pwr_mode, chip->dttime);

	ret = fusb301_i2c_read(dev, FUSB301_REG_STATUS, &status);
	if (ret < 0)
		return ret;
	chip->vbus_present = !!(status & FUSB301_STATUS_VBUS_OK);

	return 0;
}

/*
 * spec lets transitioning to below states from any state
 *  FUSB_STATE_DISABLED
 *  FUSB_STATE_ERROR_RECOVERY
 *  FUSB_STATE_UNATTACHED_SNK
 *  FUSB_STATE_UNATTACHED_SRC
 */
static int fusb301_set_chip_state(struct fusb301_chip *chip, enum fusb301_state state)
{
	struct udevice *dev = chip->dev;
	u8 manual;
	int ret;

	switch (state) {
	case FUSB_STATE_DISABLED:
		manual = FUSB301_MANUAL_DISABLED;
		break;
	case FUSB_STATE_ERROR_RECOVERY:
		manual = FUSB301_MANUAL_ERR_RECOVERY;
		break;
	case FUSB_STATE_UNATTACHED_SNK:
		manual = FUSB301_MANUAL_UNATT_SNK;
		break;
	case FUSB_STATE_UNATTACHED_SRC:
		manual = FUSB301_MANUAL_UNATT_SRC;
		break;
	default:
		dev_err(dev, "unexpected state: 0x%02x\n", state);
		return -EINVAL;
	}
	ret = fusb301_i2c_write_bits(dev, FUSB301_REG_MANUAL,
				     FUSB301_MANUAL_MASK,
				     manual);
	if (ret)
		return ret;

	chip->state = state;
	dev_info(chip->dev, "fusb301 set state: %s\n", fusb301_state_name[state]);

	return 0;
}

static int fusb301_set_mode(struct fusb301_chip *chip, u8 mode)
{
	struct udevice *dev = chip->dev;
	int ret;

	switch (mode) {
	case FUSB301_MODES_DRP_ACC:
	case FUSB301_MODES_DRP:
	case FUSB301_MODES_SNK_ACC:
	case FUSB301_MODES_SNK:
	case FUSB301_MODES_SRC_ACC:
	case FUSB301_MODES_SRC:
		break;
	default:
		dev_err(dev, "unexpected mode: 0x%02x\n", mode);
		return -EINVAL;
	}
	ret = fusb301_i2c_write_bits(dev, FUSB301_REG_MODES,
				     FUSB301_MODES_MASK,
				     mode);
	if (ret)
		return ret;

	chip->mode = mode;
	dev_info(dev, "fusb301 set mode: %s\n", fusb301_mode_name[mode]);

	return 0;
}

/* Set output current indicator */
static int fusb301_set_pwr_mode(struct fusb301_chip *chip, u8 pwr_mode)
{
	struct udevice *dev = chip->dev;
	int ret;

	switch (pwr_mode) {
	case FUSB301_HOST_CUR_0:
	case FUSB301_HOST_CUR_DEFAULT:
	case FUSB301_HOST_CUR_1500MA:
	case FUSB301_HOST_CUR_3000MA:
		break;
	default:
		dev_err(dev, "unexpected pwr mode: 0x%02x\n", pwr_mode);
		return -EINVAL;
	}
	ret = fusb301_i2c_write_bits(dev, FUSB301_REG_CONTROL,
				     FUSB301_CONTROL_HOST_CUR_MASK,
				     FIELD_PREP(FUSB301_CONTROL_HOST_CUR_MASK,
				     pwr_mode));
	if (ret)
		return ret;

	chip->pwr_mode = pwr_mode;
	dev_info(dev, "fusb301 set pwr_mode: %s\n", fusb301_pwr_mode_name[pwr_mode]);

	return ret;
}

static int fusb301_set_toggle_time(struct fusb301_chip *chip, u8 toggle_time)
{
	struct udevice *dev = chip->dev;
	int ret;

	switch (toggle_time) {
	case FUSB301_CONTROL_TGL_35MS:
	case FUSB301_CONTROL_TGL_30MS:
	case FUSB301_CONTROL_TGL_25MS:
	case FUSB301_CONTROL_TGL_20MS:
		break;
	default:
		dev_err(dev, "unexpected toggle_time: 0x%02x\n", toggle_time);
		return -EINVAL;
	}

	ret = fusb301_i2c_write_bits(dev, FUSB301_REG_CONTROL,
				     FUSB301_CONTROL_TGL_MASK,
				     FIELD_PREP(FUSB301_CONTROL_TGL_MASK,
				     toggle_time));
	if (ret)
		return ret;

	chip->dttime = toggle_time;
	dev_info(dev, "fusb301 set toggle time: %s\n", fusb301_toggle_name[toggle_time]);

	return 0;
}

static int fusb301_init_reg(struct fusb301_chip *chip)
{
	struct udevice *dev = chip->dev;
	int ret;

	/* change current */
	ret = fusb301_set_pwr_mode(chip, FUSB301_HOST_CUR_1500MA);
	if (ret)
		dev_err(dev, "%s: failed to force dfp power\n", __func__);

	/* change toggle time */
	ret = fusb301_set_toggle_time(chip, FUSB301_CONTROL_TGL_35MS);
	if (ret)
		dev_err(dev, "%s: failed to set toggle time\n", __func__);

	/* change mode */
	ret = fusb301_set_mode(chip, FUSB301_MODES_DRP_ACC);
	if (ret)
		dev_err(dev, "%s: failed to set mode\n", __func__);

	/* set error recovery state */
	ret = fusb301_set_chip_state(chip, FUSB_STATE_ERROR_RECOVERY);
	if (ret)
		dev_err(dev, "%s: failed to set error recovery state\n", __func__);
	return ret;
}

static int fusb301_reset_device(struct fusb301_chip *chip)
{
	struct udevice *dev = chip->dev;
	int ret;

	ret = fusb301_i2c_write(dev, FUSB301_REG_RESET, FUSB301_RESET_SW_RES);
	if (ret)
		return ret;

	mdelay(10);

	ret = fusb301_init_reg(chip);
	if (ret)
		dev_err(dev, "failed to init reg\n");

	ret = fusb301_update_status(chip);
	if (ret)
		dev_err(dev, "failed to read status\n");

	return ret;
}

static int fusb301_get_cc_orientation(struct fusb301_chip *chip)
{
	enum typec_orientation orientation;
	u8 status;
	int ret;

	ret = fusb301_i2c_read(chip->dev, FUSB301_REG_STATUS, &status);
	if (ret < 0)
		return ret;

	switch (FIELD_GET(FUSB301_STATUS_ORIENT_MASK, status)) {
	case FUSB301_STATUS_ORIENT_CC1:
		orientation = TYPEC_ORIENTATION_NORMAL;
		break;
	case FUSB301_STATUS_ORIENT_CC2:
		orientation = TYPEC_ORIENTATION_REVERSE;
		break;
	default:
		orientation = TYPEC_ORIENTATION_NONE;
		break;
	}

	chip->orient = orientation;
	dev_info(chip->dev, "get orientation: %d\n", orientation);

	return 0;
}

static void fusb301_interrupt_handle(struct udevice *dev)
{
	struct fusb301_chip *chip = dev_get_priv(dev);
	u8 int_sts, status;
	bool vbus_present;
	int ret;

	ret = fusb301_i2c_read(dev, FUSB301_REG_INTERRUPT, &int_sts);
	if (ret < 0)
		return;

	ret = fusb301_i2c_read(dev, FUSB301_REG_STATUS, &status);
	if (ret < 0)
		return;

	dev_dbg(dev, "%s: int_sts[0x%02x] status[0x%02x]\n",
		 __func__, int_sts, status);

	vbus_present = !!(status & FUSB301_STATUS_VBUS_OK);
	if (vbus_present != chip->vbus_present) {
		dev_info(dev, "vbus=%s\n", vbus_present ? "On" : "Off");
		chip->vbus_present = vbus_present;
		tcpm_vbus_change(dev);
	}

	if (int_sts & FUSB301_INT_ATTACH) {
		dev_info(dev, "IRQ: ATTACH detected\n");
		tcpm_cc_change(dev);
	}

	if (int_sts & FUSB301_INT_DETACH) {
		dev_info(dev, "IRQ: DETACH detected\n");
		tcpm_cc_change(dev);
	}
}

static void fusb301_poll_event(struct udevice *dev)
{
	fusb301_interrupt_handle(dev);
}

static int fusb301_pd_transmit(struct udevice *dev, enum tcpm_transmit_type type,
			       const struct pd_message *msg, unsigned int negotiated_rev)
{
	return 0;
}

static int fusb301_start_toggling(struct udevice *dev,
				  enum typec_port_type port_type,
				  enum typec_cc_status cc)
{
	struct fusb301_chip *chip = dev_get_priv(dev);
	u8 pwr_mode;
	int ret;

	if (port_type != TYPEC_PORT_DRP)
		return -EOPNOTSUPP;

	switch (cc) {
	case TYPEC_CC_RP_DEF:
		pwr_mode = FUSB301_HOST_CUR_DEFAULT;
		break;
	case TYPEC_CC_RP_1_5:
		pwr_mode = FUSB301_HOST_CUR_1500MA;
		break;
	case TYPEC_CC_RP_3_0:
		pwr_mode = FUSB301_HOST_CUR_3000MA;
		break;
	default:
		pwr_mode = FUSB301_HOST_CUR_0;
		break;
	}

	ret = fusb301_set_pwr_mode(chip, pwr_mode);
	if (ret)
		dev_err(dev, "cannot set pwr_mode %s: %d\n",
			typec_cc_status_name[cc], ret);

	ret = fusb301_set_mode(chip, FUSB301_MODES_DRP_ACC);
	if (ret)
		dev_err(dev, "cannot set drp mode: %d\n", ret);

	return ret;
}

static int fusb301_set_roles(struct udevice *dev, bool attached,
			     enum typec_role pwr, enum typec_data_role data)
{
	return 0;
}

static int fusb301_set_pd_rx(struct udevice *dev, bool on)
{
	u8 type;
	int ret;

	if (!on)
		return 0;

	ret = fusb301_i2c_read(dev, FUSB301_REG_TYPE, &type);
	if (ret < 0)
		return ret;

	/*
	 * Only skip negotiation on the sink path. A partner sinking power means
	 * we are the source, and tcpm_src_attach() treats an error here as
	 * fatal: it would skip enabling VBUS and leave port->attached false.
	 */
	if (!(type & FUSB301_TYPE_SNK))
		return -EOPNOTSUPP;

	return 0;
}

static int fusb301_set_vbus(struct udevice *dev, bool on, bool charge)
{
	return 0;
}

static int fusb301_set_vconn(struct udevice *dev, bool on)
{
	return 0;
}

static int fusb301_get_cc(struct udevice *dev, enum typec_cc_status *cc1,
			  enum typec_cc_status *cc2)
{
	struct fusb301_chip *chip = dev_get_priv(dev);
	enum typec_cc_status cc = TYPEC_CC_OPEN;
	u8 type, status;
	int ret;

	*cc1 = TYPEC_CC_OPEN;
	*cc2 = TYPEC_CC_OPEN;

	ret = fusb301_i2c_read(dev, FUSB301_REG_TYPE, &type);
	if (ret < 0) {
		dev_err(dev, "%s: failed to read type\n", __func__);
		return ret;
	}

	if (type & FUSB301_TYPE_SRC) {
		ret = fusb301_i2c_read(dev, FUSB301_REG_STATUS, &status);
		if (ret < 0) {
			dev_err(dev, "%s: failed to read status\n", __func__);
			return ret;
		}

		switch (FIELD_GET(FUSB301_STATUS_BC_LVL_MASK, status)) {
		case FUSB301_STATUS_SNK_DEFAULT:
			cc = TYPEC_CC_RP_DEF;
			break;
		case FUSB301_STATUS_SNK_1500MA:
			cc = TYPEC_CC_RP_1_5;
			break;
		case FUSB301_STATUS_SNK_3000MA:
			cc = TYPEC_CC_RP_3_0;
			break;
		default:
			break;
		}
	}

	if (type & FUSB301_TYPE_SNK)
		cc = TYPEC_CC_RD;

	ret = fusb301_get_cc_orientation(chip);
	if (ret)
		return ret;

	if (chip->orient == TYPEC_ORIENTATION_NORMAL)
		*cc1 = cc;
	else if (chip->orient == TYPEC_ORIENTATION_REVERSE)
		*cc2 = cc;

	dev_info(dev, "get cc1 = %s, cc2 = %s\n", typec_cc_status_name[*cc1],
		 typec_cc_status_name[*cc2]);

	return 0;
}

static int fusb301_set_cc(struct udevice *dev, enum typec_cc_status cc)
{
	struct fusb301_chip *chip = dev_get_priv(dev);
	u8 mode, pwr_mode;
	int ret;

	dev_info(dev, "set cc = %s\n", typec_cc_status_name[cc]);

	switch (cc) {
	case TYPEC_CC_OPEN:
		mode = FUSB301_MODES_SRC;
		pwr_mode = FUSB301_HOST_CUR_0;
		break;
	case TYPEC_CC_RD:
		mode = FUSB301_MODES_SNK;
		pwr_mode = FUSB301_HOST_CUR_0;
		break;
	case TYPEC_CC_RP_DEF:
		mode = FUSB301_MODES_SRC;
		pwr_mode = FUSB301_HOST_CUR_DEFAULT;
		break;
	case TYPEC_CC_RP_1_5:
		mode = FUSB301_MODES_SRC;
		pwr_mode = FUSB301_HOST_CUR_1500MA;
		break;
	case TYPEC_CC_RP_3_0:
		mode = FUSB301_MODES_SRC;
		pwr_mode = FUSB301_HOST_CUR_3000MA;
		break;
	default:
		dev_err(dev, "unsupported CC value: %s\n",
			typec_cc_status_name[cc]);
		return -EINVAL;
	}

	ret = fusb301_set_pwr_mode(chip, pwr_mode);
	if (ret)
		dev_err(dev, "cannot set pwr_mode %s: %d\n",
			typec_cc_status_name[cc], ret);

	ret = fusb301_set_mode(chip, mode);
	if (ret)
		dev_err(dev, "cannot set mode %d: %d\n", mode, ret);

	return ret;
}

static int fusb301_set_polarity(struct udevice *dev,
				enum typec_cc_polarity polarity)
{
	struct fusb301_chip *chip = dev_get_priv(dev);
	struct udevice *sw_dev = chip->sw;
	enum typec_orientation orientation;

	if (!sw_dev)
		return 0;

	if (polarity == TYPEC_POLARITY_CC1)
		orientation = TYPEC_ORIENTATION_NORMAL;
	else
		orientation = TYPEC_ORIENTATION_REVERSE;

	return typec_switch_set(sw_dev, orientation);
}

static int fusb301_get_vbus(struct udevice *dev)
{
	u8 status;
	int ret;

	ret = fusb301_i2c_read(dev, FUSB301_REG_STATUS, &status);
	if (ret < 0) {
		/*
		 * Callers treat the return value as a boolean, so a negative
		 * errno would read as "VBUS present". Report absent instead.
		 */
		dev_err(dev, "%s: failed to read status\n", __func__);
		return 0;
	}
	dev_info(dev, "get vbus: %s\n", (status & FUSB301_STATUS_VBUS_OK) ? "On" : "Off");

	return !!(status & FUSB301_STATUS_VBUS_OK);
}

static int fusb301_init(struct udevice *dev)
{
	struct fusb301_chip *chip = dev_get_priv(dev);
	int ret;

	chip->dev = dev;
	ret = fusb301_check_device_id(chip);
	if (ret < 0) {
		dev_err(dev, "fusb301 not found\n");
		return -ENODEV;
	}

	chip->state = FUSB_STATE_ERROR_RECOVERY;

	ret = fusb301_reset_device(chip);
	if (ret) {
		dev_err(dev, "failed to reset device, ret: %d\n", ret);
		return ret;
	}

	/* orientation switch is optional */
	ret = typec_switch_get(dev, &chip->sw);
	if (ret) {
		chip->sw = NULL;
		dev_dbg(dev, "no typec switch: %d\n", ret);
	}

	return 0;
}

static int fusb301_get_connector_node(struct udevice *dev, ofnode *connector_node)
{
	*connector_node = dev_read_subnode(dev, "connector");
	if (!ofnode_valid(*connector_node)) {
		dev_err(dev, "'connector' node is not found\n");
		return -ENODEV;
	}

	return 0;
}

static struct dm_tcpm_ops fusb301_ops = {
	.get_connector_node = fusb301_get_connector_node,
	.init = fusb301_init,
	.get_vbus = fusb301_get_vbus,
	.set_cc = fusb301_set_cc,
	.get_cc = fusb301_get_cc,
	.set_polarity = fusb301_set_polarity,
	.set_vconn = fusb301_set_vconn,
	.set_vbus = fusb301_set_vbus,
	.set_pd_rx = fusb301_set_pd_rx,
	.set_roles = fusb301_set_roles,
	.start_toggling = fusb301_start_toggling,
	.pd_transmit = fusb301_pd_transmit,
	.poll_event = fusb301_poll_event,
};

static const struct udevice_id fusb301_ids[] = {
	{ .compatible = "fcs,fusb301" },
	{ }
};

U_BOOT_DRIVER(fusb301) = {
	.name		= "fusb301",
	.id		= UCLASS_TCPM,
	.of_match	= fusb301_ids,
	.ops 		= &fusb301_ops,
	.priv_auto	= sizeof(struct fusb301_chip),
};
