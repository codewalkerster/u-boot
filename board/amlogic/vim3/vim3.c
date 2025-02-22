// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <common.h>
#include <dm.h>
#include <env_internal.h>
#include <init.h>
#include <net.h>
#include <asm/io.h>
#include <asm/arch/eth.h>
#include <asm/arch/sm.h>
#include <i2c.h>
#include "khadas-mcu.h"

/*
 * The VIM3 on-board  MCU can mux the PCIe/USB3.0 shared differential
 * lines using a FUSB340TMX USB 3.1 SuperSpeed Data Switch between
 * an USB3.0 Type A connector and a M.2 Key M slot.
 * The PHY driving these differential lines is shared between
 * the USB3.0 controller and the PCIe Controller, thus only
 * a single controller can use it.
 */
int meson_ft_board_setup(void *blob, struct bd_info *bd)
{
	struct udevice *bus, *dev;
	int node, i2c_node, ret;
	unsigned int i2c_addr;
	u32 *val;

	/* Find I2C device */
	node = fdt_node_offset_by_compatible(gd->fdt_blob, -1, "khadas,mcu");
	if (node < 0) {
		printf("vim3: cannot find khadas,mcu node\n");
		return 0;
	}

	/* Get addr */
	val = (u32 *)fdt_getprop(gd->fdt_blob, node, "reg", NULL);
	if (!val) {
		printf("vim3: cannot find khadas,mcu node i2c addr\n");
		return 0;
	}
	i2c_addr = fdt32_to_cpu(*val);

	/* Get i2c device */
	i2c_node = fdt_parent_offset(gd->fdt_blob, node);
	if (node < 0) {
		printf("vim3: cannot find khadas,mcu i2c node\n");
		return 0;
	}

	ret = uclass_get_device_by_of_offset(UCLASS_I2C, i2c_node, &bus);
	if (ret < 0) {
		printf("vim3: cannot find i2c bus (%d)\n", ret);
		return 0;
	}

	ret = i2c_get_chip(bus, i2c_addr, 1, &dev);
	if (ret < 0) {
		printf("vim3: cannot find i2c chip (%d)\n", ret);
		return 0;
	}

	/* Read USB_PCIE_SWITCH_REG */
	ret = dm_i2c_reg_read(dev, KHADAS_MCU_USB_PCIE_SWITCH_REG);
	if (ret < 0) {
		printf("vim3: failed to read i2c reg (%d)\n", ret);
		return 0;
	}
	debug("MCU_USB_PCIE_SWITCH_REG: %d\n", ret);

	/*
	 * If in PCIe mode, alter DT
	 * 0：Enable USB3.0，Disable PCIE, 1：Disable USB3.0, Enable PCIE
	 */
	if (ret > 0) {
		static char data[32] __aligned(4);
		const void *ptmp;
		int len;

		/* Find USB node */
		node = fdt_node_offset_by_compatible(blob, -1, "amlogic,meson-g12a-usb-ctrl");
		if (node < 0) {
			printf("vim3: cannot find amlogic,meson-g12a-usb-ctrl node\n");
			return 0;
		}

		/* Update PHY names (mandatory to disable USB3.0) */
		len = strlcpy(data, "usb2-phy0", 32) + 1;
		len += strlcpy(&data[len], "usb2-phy1", 32 - len) + 1;
		ret = fdt_setprop(blob, node, "phy-names", data, len);
		if (ret < 0) {
			printf("vim3: failed to update usb phy names property (%d)\n", ret);
			return 0;
		}

		/* Update PHY list, by keeping the 2 first entries (optional) */
		ptmp = fdt_getprop(blob, node, "phys", &len);
		if (ptmp) {
			memcpy(data, ptmp, min_t(unsigned int, 2 * sizeof(u32), len));

			ret = fdt_setprop(blob, node, "phys", data,
					  min_t(unsigned int, 2 * sizeof(u32), len));
			if (ret < 0)
				printf("vim3: failed to update usb phys property (%d)\n", ret);
		} else
			printf("vim3: cannot find usb node phys property\n");

		/* Find PCIe node */
		node = fdt_node_offset_by_compatible(blob, -1, "amlogic,g12a-pcie");
		if (node < 0) {
			printf("vim3: cannot find amlogic,g12a-pcie node\n");
			return 0;
		}

		/* Enable PCIe */
		len = strlcpy(data, "okay", 32);
		ret = fdt_setprop(blob, node, "status", data, len);
		if (ret < 0) {
			printf("vim3: failed to enable pcie node (%d)\n", ret);
			return 0;
		}

		printf("vim3: successfully enabled PCIe\n");
	}

	return 0;
}

int misc_init_r(void)
{
	char chip_serial[16];
	char serial_string[12];
	u8 serial[6];
	u32 sid;
	u16 sid16;

	meson_eth_init(PHY_INTERFACE_MODE_RGMII, 0);

	meson_generate_serial_ethaddr();

	if (!env_get("serial#")) {
		if (!meson_sm_get_serial(chip_serial, SM_SERIAL_SIZE)) {
			sid = crc32(0, (unsigned char *)chip_serial, SM_SERIAL_SIZE);
			sid16 = crc16_ccitt(0, (unsigned char *)chip_serial,	SM_SERIAL_SIZE);

			/* Ensure the NIC specific bytes of the mac are not all 0 */
			if ((sid & 0xffffff) == 0)
				sid |= 0x800000;

			/* Non OUI  */
			serial[0] = ((sid16 >> 8) & 0xfc) | 0x02;
			serial[1] = (sid16 >>  0) & 0xff;
			serial[2] = (sid >> 24) & 0xff;
			serial[3] = (sid >> 16) & 0xff;
			serial[4] = (sid >>  8) & 0xff;
			serial[5] = (sid >>  0) & 0xff;
			sprintf(serial_string, "%02X%02X%02X%02X%02X%02X", serial[0], serial[1], serial[2],serial[3], serial[4], serial[5]);
			env_set("serial#", serial_string);

		} else
			return -EINVAL;
	}

	return 0;
}
