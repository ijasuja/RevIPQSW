/*
 *  Atheros AR71xx SoC specific platform data definitions
 *
 *  Copyright (c) 2016-2017 The Linux Foundation. All rights reserved.
 *  Copyright (C) 2008-2012 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#ifndef __ASM_MACH_ATH79_PLATFORM_H
#define __ASM_MACH_ATH79_PLATFORM_H

#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/phy.h>
#include <linux/spi/spi.h>

struct ag71xx_switch_platform_data {
	u8		phy4_mii_en:1;
	u8		phy_poll_mask;
	u8		phy_classab_en;
};

struct ag71xx_platform_data {
	phy_interface_t	phy_if_mode;
	u32		phy_mask;
	int		speed;
	int		duplex;
	u32		reset_bit;
	u8		mac_addr[ETH_ALEN];
	struct device	*mii_bus_dev;

	u8		has_gbit:1;
	u8		is_ar91xx:1;
	u8		is_ar7240:1;
	u8		is_ar724x:1;
	u8		has_ar8216:1;
	u8		force_link:1;
	u8		is_qca956x:1;
	u8		is_qca9561:1;

	u8		is_qca955x:1;

	struct ag71xx_switch_platform_data *switch_data;

	void		(*ddr_flush)(void);
	void		(*set_speed)(int speed);

	u32		fifo_cfg1;
	u32		fifo_cfg2;
	u32		fifo_cfg3;

	u32		pll_10;
	u32		pll_100;
	u32		pll_1000;

	unsigned int	max_frame_len;
	unsigned int	desc_pktlen_mask;
};

struct ag71xx_mdio_platform_data {
	u32		phy_mask;
	u8		builtin_switch:1;
	u8		is_ar7240:1;
	u8		is_ar9330:1;
	u8		is_ar934x:1;
	unsigned long	mdio_clock;
	unsigned long	ref_clock;
	u32		reset_bit;

	void		(*reset)(struct mii_bus *bus);
};

#endif /* __ASM_MACH_ATH79_PLATFORM_H */
