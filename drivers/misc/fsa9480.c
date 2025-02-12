/*
 * driver/misc/fsa9480.c - FSA9480 micro USB switch device driver
 *
 * Copyright (C) 2010 Samsung Electronics
 * Minkyu Kang <mk7.kang@samsung.com>
 * Wonguk Jeong <wonguk.jeong@samsung.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/fsa9480.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/switch.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <mach/param.h>
#include <plat/devs.h>

/* FSA9480 I2C registers */
#define FSA9480_REG_DEVID		0x01
#define FSA9480_REG_CTRL		0x02
#define FSA9480_REG_INT1		0x03
#define FSA9480_REG_INT2		0x04
#define FSA9480_REG_INT1_MASK		0x05
#define FSA9480_REG_INT2_MASK		0x06
#define FSA9480_REG_ADC			0x07
#define FSA9480_REG_TIMING1		0x08
#define FSA9480_REG_TIMING2		0x09
#define FSA9480_REG_DEV_T1		0x0a
#define FSA9480_REG_DEV_T2		0x0b
#define FSA9480_REG_BTN1		0x0c
#define FSA9480_REG_BTN2		0x0d
#define FSA9480_REG_CK			0x0e
#define FSA9480_REG_CK_INT1		0x0f
#define FSA9480_REG_CK_INT2		0x10
#define FSA9480_REG_CK_INTMASK1		0x11
#define FSA9480_REG_CK_INTMASK2		0x12
#define FSA9480_REG_MANSW1		0x13
#define FSA9480_REG_MANSW2		0x14

/* Control */
#define CON_SWITCH_OPEN		(1 << 4)
#define CON_RAW_DATA		(1 << 3)
#define CON_MANUAL_SW		(1 << 2)
#define CON_WAIT		(1 << 1)
#define CON_INT_MASK		(1 << 0)
#define CON_MASK		(CON_SWITCH_OPEN | CON_RAW_DATA | \
				CON_MANUAL_SW | CON_WAIT)

/* Device Type 1 */
#define DEV_USB_OTG		(1 << 7)
#define DEV_DEDICATED_CHG	(1 << 6)
#define DEV_USB_CHG		(1 << 5)
#define DEV_CAR_KIT		(1 << 4)
#define DEV_UART		(1 << 3)
#define DEV_USB			(1 << 2)
#define DEV_AUDIO_2		(1 << 1)
#define DEV_AUDIO_1		(1 << 0)

#define DEV_T1_USB_MASK		(DEV_USB_OTG | DEV_USB)
#define DEV_T1_UART_MASK	(DEV_UART)
#define DEV_T1_CHARGER_MASK	(DEV_DEDICATED_CHG | DEV_USB_CHG | DEV_CAR_KIT)

/* Device Type 2 */
#define DEV_AV			(1 << 6)
#define DEV_TTY			(1 << 5)
#define DEV_PPD			(1 << 4)
#define DEV_JIG_UART_OFF	(1 << 3)
#define DEV_JIG_UART_ON		(1 << 2)
#define DEV_JIG_USB_OFF		(1 << 1)
#define DEV_JIG_USB_ON		(1 << 0)

#define DEV_T2_USB_MASK		(DEV_JIG_USB_OFF | DEV_JIG_USB_ON)
#define DEV_T2_UART_MASK	DEV_JIG_UART_OFF
#define DEV_T2_JIG_MASK		(DEV_JIG_USB_OFF | DEV_JIG_USB_ON | \
				DEV_JIG_UART_OFF)

/*
 * Manual Switch
 * D- [7:5] / D+ [4:2]
 * 000: Open all / 001: USB / 010: AUDIO / 011: UART / 100: V_AUDIO
 */
#define SW_VAUDIO		((4 << 5) | (4 << 2))
#define SW_UART			((3 << 5) | (3 << 2))
#define SW_AUDIO		((2 << 5) | (2 << 2))
#define SW_DHOST		((1 << 5) | (1 << 2))
#define SW_AUTO			((0 << 5) | (0 << 2))

/* Interrupt 1 */
#define INT_DETACH		(1 << 1)
#define INT_ATTACH		(1 << 0)

#define USB_CABLE_50K   1
#define USB_CABLE_255K  2
#define CABLE_DISCONNECT        0

#define DRIVER_NAME		"usb_configuration"
#define WIMAX_CABLE_50K       1553
#define WIMAX_CABLE_50K_DIS   1567

struct fsa9480_usbsw {
	struct i2c_client		*client;
	struct fsa9480_platform_data	*pdata;
	int				dev1;
	int				dev2;
	int				mansw;
};

static struct fsa9480_usbsw *local_usbsw;
struct switch_dev indicator_dev;
static int micro_usb_status;
static int dock_status = 0;
static int MicroJigUARTOffStatus=0;
unsigned int adc_fsa;

extern struct switch_dev *switch_dev;

/* To support Wimax Cable */
struct switch_dev wimax_cable = {
                .name = "wimax_cable",
};

#ifdef _SUPPORT_SAMSUNG_AUTOINSTALLER_
extern int askon_status;
#endif
/*********for WIMAX USB MODEM***********/
int fsa9480_set_ctl_register(void)
{
     int ret=0;
     struct i2c_client *client = local_usbsw->client;
     if (adc_fsa == WIMAX_CABLE_50K)
	     fsa9480_manual_switching(SWITCH_PORT_USB);
     else 
	     ret = i2c_smbus_write_byte_data(client, FSA9480_REG_CTRL,0x1E);
     if (ret < 0)
	     dev_err(&client->dev, "%s: err %d\n", __func__, ret);
}
EXPORT_SYMBOL(fsa9480_set_ctl_register);
/***************************/

void UsbIndicator(u8 state)
{
     
printk(" %s ,VALUE =%d\n",__func__,state);
   switch_set_state(&indicator_dev, state);
}

static ssize_t print_switch_name(struct switch_dev *sdev, char *buf)
{
         printk(" %s ,BUF =%s\n",__func__,buf);
        return sprintf(buf, "%s\n", DRIVER_NAME);
}

static int fsa9480_get_usb_status(void)
{
          printk(" --------> %s ,micro_usb_status =%d\n",__func__,micro_usb_status);
	if (micro_usb_status)
		return 1;
	else 
		return 0;
}

int fsa9480_get_dock_status(void)
{
	if (dock_status)
		return 1;
	else
		return 0;
}
EXPORT_SYMBOL(fsa9480_get_dock_status);

void FSA9480_Enable_SPK(u8 enable)
{
	static struct regulator *esafeout2_regulator;
	struct i2c_client *client = local_usbsw->client;

	u8 data = 0;
	
	if (!enable) {
		printk("%s: Speaker Disabled\n", __func__);
		return;
	}
	
	esafeout2_regulator = regulator_get(NULL, "esafeout2");		
	if (IS_ERR(esafeout2_regulator)) {
		pr_err(" failed to get regulator esafeout2\n");
		return;
	}
	regulator_enable(esafeout2_regulator);	
	mdelay(10);

	fsa9480_manual_switching(local_usbsw->pdata->spk_switch);
}


extern int currentusbstatus;
#if 1
static ssize_t print_switch_state(struct switch_dev *sdev, char *buf)
{
        int usbstatus;

        usbstatus = fsa9480_get_usb_status();

          printk("---------->  %s ,USBSTATUS=%d,CURRENTUSBSTATUS=%d\n",__func__,usbstatus,currentusbstatus);
    if(usbstatus){
        if((currentusbstatus== USBSTATUS_UMS) || (currentusbstatus== USBSTATUS_ADB)) {
          printk(KERN_INFO " ------------> sending notification: ums online\n");
	#if defined(CONFIG_MACH_VICTORY)
           return sprintf(buf, "%s\n", "1");
         #else
            return sprintf(buf, "%s\n", "ums online");
	#endif
	}
        else{
          printk(KERN_INFO " ----------> sending notification: InsertOffline\n");
            return sprintf(buf, "%s\n", "InsertOffline");
	}
    }
    else{
        if((currentusbstatus== USBSTATUS_UMS) || (currentusbstatus== USBSTATUS_ADB)){
          printk(KERN_INFO " ---------> sending notification: ums offline\n");
	
	#if defined(CONFIG_MACH_VICTORY)
           return sprintf(buf, "%s\n", "0");
         #else
            return sprintf(buf, "%s\n", "ums offline");
	#endif
	}
        else {
          printk(KERN_INFO " --------> sending notification: RemoveOffline\n");
            return sprintf(buf, "%s\n", "RemoveOffline");
	}
    }
}

#endif

static ssize_t wimax_cable_type(struct switch_dev *sdev, char *buf)
{

	if (sdev->state == USB_CABLE_50K)
		sprintf(buf, "%s\n", "authcable");
	else if (sdev->state == USB_CABLE_255K)
		sprintf(buf, "%s\n", "wtmcable");
	else
		sprintf(buf, "%s\n", "Disconnected");

	return sprintf(buf, "%s\n", buf);
}

static ssize_t fsa9480_show_control(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct fsa9480_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	int value;

	value = i2c_smbus_read_byte_data(client, FSA9480_REG_CTRL);
	if (value < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, value);

	return sprintf(buf, "CONTROL: %02x\n", value);
}

static ssize_t fsa9480_show_device_type(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct fsa9480_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	int value;

	value = i2c_smbus_read_byte_data(client, FSA9480_REG_DEV_T1);
	if (value < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, value);

	return sprintf(buf, "DEVICE_TYPE: %02x\n", value);
}

static ssize_t fsa9480_show_manualsw(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fsa9480_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	unsigned int value;

	value = i2c_smbus_read_byte_data(client, FSA9480_REG_MANSW1);
	if (value < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, value);

	if (value == SW_VAUDIO)
		return sprintf(buf, "VAUDIO\n");
	else if (value == SW_UART)
		return sprintf(buf, "UART\n");
	else if (value == SW_AUDIO)
		return sprintf(buf, "AUDIO\n");
	else if (value == SW_DHOST)
		return sprintf(buf, "DHOST\n");
	else if (value == SW_AUTO)
		return sprintf(buf, "AUTO\n");
	else
		return sprintf(buf, "%x", value);
}

static ssize_t fsa9480_set_manualsw(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct fsa9480_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	unsigned int value;
	unsigned int path = 0;
	int ret;

	value = i2c_smbus_read_byte_data(client, FSA9480_REG_CTRL);
	if (value < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, value);

	if ((value & ~CON_MANUAL_SW) !=
			(CON_SWITCH_OPEN | CON_RAW_DATA | CON_WAIT))
		return 0;

	if (!strncmp(buf, "VAUDIO", 6)) {
		path = SW_VAUDIO;
		value &= ~CON_MANUAL_SW;
	} else if (!strncmp(buf, "UART", 4)) {
		path = SW_UART;
		value &= ~CON_MANUAL_SW;
	} else if (!strncmp(buf, "AUDIO", 5)) {
		path = SW_AUDIO;
		value &= ~CON_MANUAL_SW;
	} else if (!strncmp(buf, "DHOST", 5)) {
		path = SW_DHOST;
		value &= ~CON_MANUAL_SW;
	} else if (!strncmp(buf, "AUTO", 4)) {
		path = SW_AUTO;
		value |= CON_MANUAL_SW;
	} else {
		dev_err(dev, "Wrong command\n");
		return 0;
	}

	usbsw->mansw = path;

	ret = i2c_smbus_write_byte_data(client, FSA9480_REG_MANSW1, path);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	ret = i2c_smbus_write_byte_data(client, FSA9480_REG_CTRL, value);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return count;
}

static DEVICE_ATTR(control, S_IRUGO, fsa9480_show_control, NULL);
static DEVICE_ATTR(device_type, S_IRUGO, fsa9480_show_device_type, NULL);
static DEVICE_ATTR(switch, S_IRUGO | S_IWUSR,
		fsa9480_show_manualsw, fsa9480_set_manualsw);

static struct attribute *fsa9480_attributes[] = {
	&dev_attr_control.attr,
	&dev_attr_device_type.attr,
	&dev_attr_switch.attr,
	NULL
};

static const struct attribute_group fsa9480_group = {
	.attrs = fsa9480_attributes,
};


void fsa9480_manual_switching(int path)
{
	struct i2c_client *client = local_usbsw->client;
	unsigned int value;
	unsigned int data = 0;
	int ret;

	value = i2c_smbus_read_byte_data(client, FSA9480_REG_CTRL);
	if (value < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, value);

	if ((value & ~CON_MANUAL_SW) !=
			(CON_SWITCH_OPEN | CON_RAW_DATA | CON_WAIT))
		return;

	if (path == SWITCH_PORT_VAUDIO) {
		dev_info(&client->dev,"%s: MicroJigUARTOffStatus (%d)\n", __func__, MicroJigUARTOffStatus);
		if(MicroJigUARTOffStatus) {
			data = local_usbsw->mansw;
			value = 0x1E;
		}else {
			data = 0x90/*SW_VAUDIO*/;
			value = 0x1A/*&= ~CON_MANUAL_SW*/;
		}
	} else if (path ==  SWITCH_PORT_UART) {
		data = SW_UART;
		value &= ~CON_MANUAL_SW;
	} else if (path ==  SWITCH_PORT_AUDIO) {
		data = SW_AUDIO;
		value &= ~CON_MANUAL_SW;
	} else if (path ==  SWITCH_PORT_USB) {
		data = SW_DHOST;
		value &= ~CON_MANUAL_SW;
	} else if (path ==  SWITCH_PORT_AUTO) {
		data = SW_AUTO;
		value |= CON_MANUAL_SW;
	} else {
		printk("%s: wrong path (%d)\n", __func__, path);
		return;
	}

	local_usbsw->mansw = data;

	ret = i2c_smbus_write_byte_data(client, FSA9480_REG_MANSW1, data);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	ret = i2c_smbus_write_byte_data(client, FSA9480_REG_CTRL, value);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

}
EXPORT_SYMBOL(fsa9480_manual_switching);

#ifdef _SUPPORT_SAMSUNG_AUTOINSTALLER_
extern void askon_gadget_disconnect();
#endif

static void fsa9480_detect_dev(struct fsa9480_usbsw *usbsw)
{
	int device_type, ret;
	unsigned char val1, val2;
	struct fsa9480_platform_data *pdata = usbsw->pdata;
	struct i2c_client *client = usbsw->client;

#ifdef CONFIG_MACH_VICTORY
	adc_fsa  = i2c_smbus_read_word_data(client, FSA9480_REG_ADC);
	if (adc_fsa < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, adc_fsa);
	if ( adc_fsa == WIMAX_CABLE_50K){
		switch_set_state(&wimax_cable, USB_CABLE_50K);
	} else if (adc_fsa == WIMAX_CABLE_50K_DIS) {
		fsa9480_manual_switching(SWITCH_PORT_AUTO);
		switch_set_state(&wimax_cable, CABLE_DISCONNECT);
	} 
#endif
	device_type = i2c_smbus_read_word_data(client, FSA9480_REG_DEV_T1);
	if (device_type < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, device_type);

	val1 = device_type & 0xff;
	val2 = device_type >> 8;

	dev_info(&client->dev, "dev1: 0x%x, dev2: 0x%x\n", val1, val2);

	/* Attached */
	if (val1 || val2) {
		/* USB */
#ifdef CONFIG_MACH_VICTORY
		if (val1 & DEV_T1_USB_MASK){
#else		
			if (val1 & DEV_T1_USB_MASK || val2 & DEV_T2_USB_MASK ) {
#endif
			if (pdata->usb_cb)
				pdata->usb_cb(FSA9480_ATTACHED);

			micro_usb_status = 1;
#ifdef _SUPPORT_SAMSUNG_AUTOINSTALLER_
			askon_gadget_disconnect();
			if (!askon_status)
				UsbIndicator(1);
#else
				UsbIndicator(1);
#endif
			if (usbsw->mansw) {
				ret = i2c_smbus_write_byte_data(client,
					FSA9480_REG_MANSW1, usbsw->mansw);
				if (ret < 0)
					dev_err(&client->dev,
						"%s: err %d\n", __func__, ret);
			}
#ifdef CONFIG_MACH_VICTORY
		} else if ( val2 & DEV_T2_USB_MASK ) {
			if (pdata->wimax_cb)
				pdata->wimax_cb(FSA9480_ATTACHED);
				switch_set_state(&wimax_cable, USB_CABLE_255K);
#endif
		/* UART */
		} else if (val1 & DEV_T1_UART_MASK || val2 & DEV_T2_UART_MASK) {
			if(val2 & DEV_T2_UART_MASK)
				MicroJigUARTOffStatus = 1;

			if (pdata->uart_cb)
				pdata->uart_cb(FSA9480_ATTACHED);

			if (usbsw->mansw) {
				ret = i2c_smbus_write_byte_data(client,
					FSA9480_REG_MANSW1, SW_UART);
				if (ret < 0)
					dev_err(&client->dev,
						"%s: err %d\n", __func__, ret);
			}

			if (val2 & DEV_T2_JIG_MASK) {
				if (pdata->jig_cb)
					pdata->jig_cb(FSA9480_ATTACHED);
			}
		/* CHARGER */
		} else if (val1 & DEV_T1_CHARGER_MASK) {
			if (pdata->charger_cb)
				pdata->charger_cb(FSA9480_ATTACHED);
		/* JIG */
		} else if (val2 & DEV_T2_JIG_MASK) {
			if (pdata->jig_cb)
				pdata->jig_cb(FSA9480_ATTACHED);
		/* Desk Dock */
		} else if (val2 & DEV_AV) {
			if (pdata->deskdock_cb)
				pdata->deskdock_cb(FSA9480_ATTACHED);
				dock_status = 1;

#if defined(CONFIG_MACH_ATLAS)
			ret = i2c_smbus_write_byte_data(client,
					FSA9480_REG_MANSW1, SW_AUDIO);
			if (ret < 0)
				dev_err(&client->dev,
					"%s: err %d\n", __func__, ret);
#else
			ret = i2c_smbus_write_byte_data(client,
					FSA9480_REG_MANSW1, SW_DHOST);
			if (ret < 0)
				dev_err(&client->dev,
					"%s: err %d\n", __func__, ret);
#endif

			ret = i2c_smbus_read_byte_data(client,
					FSA9480_REG_CTRL);
			if (ret < 0)
				dev_err(&client->dev,
					"%s: err %d\n", __func__, ret);

			ret = i2c_smbus_write_byte_data(client,
					FSA9480_REG_CTRL, ret & ~CON_MANUAL_SW);
			if (ret < 0)
				dev_err(&client->dev,
					"%s: err %d\n", __func__, ret);
		/* Car Dock */
		} else if (val2 & DEV_JIG_UART_ON) {
			if (pdata->cardock_cb)
				pdata->cardock_cb(FSA9480_ATTACHED);
			dock_status = 1;

#ifdef CONFIG_MACH_ATLAS
			ret = i2c_smbus_write_byte_data(client,
					FSA9480_REG_MANSW1, SW_AUDIO);
			if (ret < 0)
				dev_err(&client->dev,
					"%s: err %d\n", __func__, ret);

			ret = i2c_smbus_read_byte_data(client,
					FSA9480_REG_CTRL);
			if (ret < 0)
				dev_err(&client->dev,
					"%s: err %d\n", __func__, ret);

			ret = i2c_smbus_write_byte_data(client,
					FSA9480_REG_CTRL, ret & ~CON_MANUAL_SW);
			if (ret < 0)
				dev_err(&client->dev,
					"%s: err %d\n", __func__, ret);
#endif
		}
	/* Detached */
	} else {
		/* USB */
#ifdef CONFIG_MACH_VICTORY
		if (usbsw->dev1 & DEV_T1_USB_MASK){
#else		
		if (usbsw->dev1 & DEV_T1_USB_MASK ||
				usbsw->dev2 & DEV_T2_USB_MASK) {
#endif
			micro_usb_status = 0;
			UsbIndicator(0);
			if (pdata->usb_cb)
				pdata->usb_cb(FSA9480_DETACHED);
#ifdef CONFIG_MACH_VICTORY		
        	/* USB JIG */
		} else if (usbsw->dev2 & DEV_T2_USB_MASK) {
			if (pdata->wimax_cb)
				pdata->wimax_cb(FSA9480_DETACHED);
			switch_set_state(&wimax_cable, CABLE_DISCONNECT);
#endif
		/* UART */
		} else if (usbsw->dev1 & DEV_T1_UART_MASK ||
				usbsw->dev2 & DEV_T2_UART_MASK) {
			if(usbsw->dev2 & DEV_T2_UART_MASK)
				MicroJigUARTOffStatus = 0;

			if (pdata->uart_cb)
				pdata->uart_cb(FSA9480_DETACHED);
			if (usbsw->dev2 & DEV_T2_JIG_MASK) {
				if (pdata->jig_cb)
					pdata->jig_cb(FSA9480_DETACHED);
			}
		/* CHARGER */
		} else if (usbsw->dev1 & DEV_T1_CHARGER_MASK) {
			if (pdata->charger_cb)
				pdata->charger_cb(FSA9480_DETACHED);
		/* JIG */
		} else if (usbsw->dev2 & DEV_T2_JIG_MASK) {
			if (pdata->jig_cb)
				pdata->jig_cb(FSA9480_DETACHED);
		/* Desk Dock */
		} else if (usbsw->dev2 & DEV_AV) {
			if (pdata->deskdock_cb)
				pdata->deskdock_cb(FSA9480_DETACHED);
			dock_status = 0;
			
			ret = i2c_smbus_read_byte_data(client,FSA9480_REG_CTRL);
			if (ret < 0)
				dev_err(&client->dev,
					"%s: err %d\n", __func__, ret);

			ret = i2c_smbus_write_byte_data(client,
					FSA9480_REG_CTRL, ret | CON_MANUAL_SW);
			if (ret < 0)
				dev_err(&client->dev,
					"%s: err %d\n", __func__, ret);
		/* Car Dock */
		} else if (usbsw->dev2 & DEV_JIG_UART_ON) {
			if (pdata->cardock_cb)
				pdata->cardock_cb(FSA9480_DETACHED);
			dock_status = 0;
#ifdef CONFIG_MACH_ATLAS
                        ret = i2c_smbus_read_byte_data(client,
                                        FSA9480_REG_CTRL);
                        if (ret < 0)
                                dev_err(&client->dev,
                                        "%s: err %d\n", __func__, ret);

                        ret = i2c_smbus_write_byte_data(client,
                                        FSA9480_REG_CTRL, ret | CON_MANUAL_SW);
                        if (ret < 0)
                                dev_err(&client->dev,
                                        "%s: err %d\n", __func__, ret);
#endif
		}
	}

	usbsw->dev1 = val1;
	usbsw->dev2 = val2;
}

static void fsa9480_reg_init(struct fsa9480_usbsw *usbsw)
{
	struct i2c_client *client = usbsw->client;
	unsigned int ctrl = CON_MASK;
	int ret;
#if defined (CONFIG_MACH_ATLAS) || defined (CONFIG_MACH_FORTE)
	/* mask interrupts (unmask attach/detach only) */
	ret = i2c_smbus_write_word_data(client, FSA9480_REG_INT1_MASK, 0x1ffc);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
#elif CONFIG_MACH_VICTORY
	/* mask interrupts (unmask attach/detach only reserved attach only) */
	ret = i2c_smbus_write_word_data(client, FSA9480_REG_INT1_MASK, 0x1dfc);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
#endif
	/* mask all car kit interrupts */
	ret = i2c_smbus_write_word_data(client, FSA9480_REG_CK_INTMASK1, 0x07ff);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	/* ADC Detect Time: 500ms */
	ret = i2c_smbus_write_byte_data(client, FSA9480_REG_TIMING1, 0x6);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	usbsw->mansw = i2c_smbus_read_byte_data(client, FSA9480_REG_MANSW1);
	if (usbsw->mansw < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, usbsw->mansw);

	if (usbsw->mansw)
		ctrl &= ~CON_MANUAL_SW;	/* Manual Switching Mode */

	ret = i2c_smbus_write_byte_data(client, FSA9480_REG_CTRL, ctrl);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
}

static irqreturn_t fsa9480_irq_thread(int irq, void *data)
{
	struct fsa9480_usbsw *usbsw = data;
	struct i2c_client *client = usbsw->client;
	int intr;
	
	/* read and clear interrupt status bits */
	intr = i2c_smbus_read_word_data(client, FSA9480_REG_INT1);
	if (intr < 0) {
		dev_err(&client->dev, "%s: err %d\n", __func__, intr);
	} else if (intr == 0) {
		/* interrupt was fired, but no status bits were set,
		so device was reset. In this case, the registers were
		reset to defaults so they need to be reinitialised. */
		fsa9480_reg_init(usbsw);
	}

	/* device detection */
	fsa9480_detect_dev(usbsw);
	
	return IRQ_HANDLED;
}

static int fsa9480_irq_init(struct fsa9480_usbsw *usbsw)
{
	struct i2c_client *client = usbsw->client;
	int ret;

	if (client->irq) {
		ret = request_threaded_irq(client->irq, NULL,
			fsa9480_irq_thread, IRQF_TRIGGER_FALLING,
			"fsa9480 micro USB", usbsw);
		if (ret) {
			dev_err(&client->dev, "failed to reqeust IRQ\n");
			return ret;
		}

		ret = enable_irq_wake(client->irq);
		if (ret < 0)
			dev_err(&client->dev,
				"failed to enable wakeup src %d\n", ret);
	}

	return 0;
}

static int __devinit fsa9480_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct fsa9480_usbsw *usbsw;
	int ret = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	usbsw = kzalloc(sizeof(struct fsa9480_usbsw), GFP_KERNEL);
	if (!usbsw) {
		dev_err(&client->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	usbsw->client = client;
	usbsw->pdata = client->dev.platform_data;
	if (!usbsw->pdata)
		goto fail1;

	i2c_set_clientdata(client, usbsw);
	
	local_usbsw = usbsw;  // temp

	if (usbsw->pdata->cfg_gpio)
		usbsw->pdata->cfg_gpio();

	fsa9480_reg_init(usbsw);

	ret = fsa9480_irq_init(usbsw);
	if (ret)
		goto fail1;

	ret = sysfs_create_group(&client->dev.kobj, &fsa9480_group);
	if (ret) {
		dev_err(&client->dev,
				"failed to create fsa9480 attribute group\n");
		goto fail2;
	}

	if (usbsw->pdata->reset_cb)
		usbsw->pdata->reset_cb();

        indicator_dev.name = DRIVER_NAME;
#if 1
        indicator_dev.print_name = print_switch_name;
        indicator_dev.print_state = print_switch_state;
#endif
        switch_dev_register(&indicator_dev);

	/* device detection */
	fsa9480_detect_dev(usbsw);
	
	// set fsa9480 init flag.
	if (usbsw->pdata->set_init_flag)
		usbsw->pdata->set_init_flag();
#if defined(CONFIG_MACH_VICTORY)
	ret = switch_dev_register(&wimax_cable);
	wimax_cable.print_state = wimax_cable_type;	
#endif
	return 0;

fail2:
	if (client->irq)
		free_irq(client->irq, usbsw);
fail1:
	i2c_set_clientdata(client, NULL);
	kfree(usbsw);
	return ret;
}

static int __devexit fsa9480_remove(struct i2c_client *client)
{
	struct fsa9480_usbsw *usbsw = i2c_get_clientdata(client);

	if (client->irq) {
		disable_irq_wake(client->irq);
		free_irq(client->irq, usbsw);
	}
	i2c_set_clientdata(client, NULL);

	sysfs_remove_group(&client->dev.kobj, &fsa9480_group);
	kfree(usbsw);
	return 0;
}

#ifdef CONFIG_PM

static int fsa9480_suspend(struct i2c_client *client, pm_message_t mesg)
{
#ifdef CONFIG_MACH_VICTORY 
	disable_irq(client->irq	);
#endif
	return 0;
}

static int fsa9480_resume(struct i2c_client *client)
{
	struct fsa9480_usbsw *usbsw = i2c_get_clientdata(client);

#ifdef CONFIG_MACH_VICTORY
	enable_irq(client->irq);
#endif
	/* device detection */
	fsa9480_detect_dev(usbsw);

	return 0;
}

#else

#define fsa9480_suspend NULL
#define fsa9480_resume NULL

#endif /* CONFIG_PM */

static const struct i2c_device_id fsa9480_id[] = {
	{"fsa9480", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, fsa9480_id);

static struct i2c_driver fsa9480_i2c_driver = {
	.driver = {
		.name = "fsa9480",
	},
	.probe = fsa9480_probe,
	.remove = __devexit_p(fsa9480_remove),
	.suspend = fsa9480_suspend,
	.resume = fsa9480_resume,
	.id_table = fsa9480_id,
};

static int __init fsa9480_init(void)
{
	return i2c_add_driver(&fsa9480_i2c_driver);
}
module_init(fsa9480_init);

static void __exit fsa9480_exit(void)
{
	i2c_del_driver(&fsa9480_i2c_driver);
}
module_exit(fsa9480_exit);

MODULE_AUTHOR("Minkyu Kang <mk7.kang@samsung.com>");
MODULE_DESCRIPTION("FSA9480 USB Switch driver");
MODULE_LICENSE("GPL");
