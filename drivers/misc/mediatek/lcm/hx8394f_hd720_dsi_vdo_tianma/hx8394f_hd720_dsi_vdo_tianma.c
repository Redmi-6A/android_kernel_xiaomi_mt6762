/*
 * Copyright (C) 2015 MediaTek Inc.
 * Copyright (C) 2019 XiaoMi, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef BUILD_LK
#include <linux/string.h>
#endif

#include "lcm_drv.h"
#include <linux/delay.h>
/* ---------------------------------------- */
/* Local Constants */
/* ---------------------------------------- */

#define FRAME_WIDTH	(720)
#define FRAME_HEIGHT	(1440)
#define LCM_DENSITY	(320)

/* physical size in um */
#define LCM_PHYSICAL_WIDTH    (61880)
#define LCM_PHYSICAL_HEIGHT   (123770)

#define REGFLAG_DELAY	0xFE
#define REGFLAG_END_OF_TABLE 0xFF
/* END OF REGISTERS MARKER */

/* ------------------------------------------ */
/* Local Variables */
/* ------------------------------------------ */

static struct LCM_UTIL_FUNCS lcm_util = { 0 };

#define SET_RESET_PIN(v)    (lcm_util.set_reset_pin((v)))

#define UDELAY(n)  udelay(n)
#define MDELAY(n)  mdelay(n)

#define FALSE 0u
/* ------------------------------------------- */
/* Local Functions */
/* ------------------------------------------- */
#define dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update) \
	lcm_util.dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update)
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update)\
	lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update) \
	lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd) \
	 lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums) \
	lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg lcm_util.dsi_read_reg()

#define read_reg_v2(cmd, buffer, buffer_size) \
		lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)


struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[64];
};


static struct LCM_setting_table lcm_initialization_setting[] = {

	/*
	 *  Note :

	 *  Data ID will depends on the following rule.

	 *  count of parameters > 1      => Data ID = 0x39
	 *  count of parameters = 1      => Data ID = 0x15
	 *  count of parameters = 0      => Data ID = 0x05

	 *  Structure Format :

	 * {DCS command, count of parameters, {parameter list}}
	 *  {REGFLAG_DELAY, milliseconds of time, {}},

	 *  ...

	 * Setting ending by predefined flag

	 * {REGFLAG_END_OF_TABLE, 0x00, {}}
	 */

	/* SET PASSWORD */
	{ 0xB9, 3, {0xFF, 0x83, 0x94} },

	/* sleep out */
	{ 0x11, 0, {} },
	{ REGFLAG_DELAY, 120, {} },
	{ 0xBD, 1, {0x01}},
	{ 0xB1, 1, {0x00}},
	{ 0xBD, 1, {0x00}},
	{ 0x29, 0, {} },
	{ REGFLAG_DELAY, 20, {} },
	{ 0x51, 1, {0x00}},
	{ REGFLAG_DELAY, 5, {} },
	/* write pwm frequence */
	{ 0xC9, 9, {0x13, 0x00, 0x21, 0x1E, 0x31, 0x1E, 0x00, 0x91, 0x00} },
	{ REGFLAG_DELAY, 5, {} },
	{ 0x55, 1, {0x00}},
	{ REGFLAG_DELAY, 5, {} },
	{ 0x53, 1, {0x24}},
	{ REGFLAG_DELAY, 5, {} },
};

static struct LCM_setting_table lcm_deep_sleep_setting[] = {
	/* Sleep Mode On */
	{ 0x28, 0, {} },
	{ REGFLAG_DELAY, 50, {} },

	{ 0x10, 0, {} },
	{ REGFLAG_DELAY, 120, {} },
	{ REGFLAG_END_OF_TABLE, 0x00, {} }
};

static struct LCM_setting_table bl_level[] = {
        {0x51, 1, {0xFF} },
        {REGFLAG_END_OF_TABLE, 0x00, {} }
};

static void push_table(void *cmdq, struct LCM_setting_table *table, unsigned int count,
		       unsigned char force_update)
{
	unsigned int i;

	for (i = 0; i < count; i++) {

		unsigned int cmd;

		cmd = table[i].cmd;

		switch (cmd) {

		case REGFLAG_DELAY:
			MDELAY(table[i].count);
			break;

		case REGFLAG_END_OF_TABLE:
			break;

		default:
			dsi_set_cmdq_V22(cmdq, cmd, table[i].count,
				 table[i].para_list, force_update);
		}
	}

}


/* ------------------------------------- */
/* LCM Driver Implementations */
/* ------------------------------------- */

static void lcm_set_util_funcs(const struct LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(struct LCM_UTIL_FUNCS));
}


static void lcm_get_params(struct LCM_PARAMS *params)
{
	memset(params, 0, sizeof(struct LCM_PARAMS));

	params->type = LCM_TYPE_DSI;
	params->width = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;
	//params->density = LCM_DENSITY;

	params->physical_width = LCM_PHYSICAL_WIDTH/1000;
	params->physical_height = LCM_PHYSICAL_HEIGHT/1000;
	params->physical_width_um = LCM_PHYSICAL_WIDTH;
	params->physical_height_um = LCM_PHYSICAL_HEIGHT;

	/* enable tearing-free */
	params->dbi.te_mode = LCM_DBI_TE_MODE_VSYNC_ONLY;
	params->dbi.te_edge_polarity = LCM_POLARITY_RISING;
	params->dsi.mode = SYNC_PULSE_VDO_MODE;

	/* DSI */
	/* Command mode setting */
	params->dsi.LANE_NUM = LCM_FOUR_LANE;
	/* The following defined the fomat for data coming from LCD engine. */
	params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;

	/* Highly depends on LCD driver capability. */
	/* Not support in MT6573 */
	params->dsi.packet_size = 256;
	params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;

	params->dsi.vertical_sync_active = 4;
	params->dsi.vertical_backporch = 12;
	params->dsi.vertical_frontporch = 15;
	params->dsi.vertical_active_line = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active = 30;
	params->dsi.horizontal_backporch = 30;
	params->dsi.horizontal_frontporch = 30;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;

#ifndef CONFIG_FPGA_EARLY_PORTING
	params->dsi.PLL_CLOCK = 228;
/* this value must be in MTK suggested table */
#else
	params->dsi.pll_div1 = 0;
	params->dsi.pll_div2 = 0;
	params->dsi.fbk_div = 0x1;
#endif

	params->dsi.cont_clock = 1;

	params->dsi.clk_lp_per_line_enable = 0;
	params->dsi.esd_check_enable = 1;
	params->dsi.customization_esd_check_enable = 1;

	params->dsi.lcm_esd_check_table[0].cmd = 0x09;
	params->dsi.lcm_esd_check_table[0].count = 3;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x80;
	params->dsi.lcm_esd_check_table[0].para_list[1] = 0x73;
	params->dsi.lcm_esd_check_table[0].para_list[2] = 0x04;
	params->dsi.lcm_esd_check_table[1].cmd = 0xD9;
	params->dsi.lcm_esd_check_table[1].count = 1;
	params->dsi.lcm_esd_check_table[1].para_list[0] = 0x80;


	params->vbias_level = 5700000;

}

static void lcm_setbacklight_cmdq(void *handle, unsigned int level)
{
        pr_err("%s,hx8394f backlight: level = %d\n", __func__, level);

        bl_level[0].para_list[0] = level;

        push_table(handle, bl_level, sizeof(bl_level) / sizeof(struct LCM_setting_table), 1);
}

static void lcm_init_power(void)
{
	/*pr_debug("lcm_init_power\n");*/
	display_bias_enable();
	MDELAY(15);

}

static void lcm_suspend_power(void)
{
	/*pr_debug("lcm_suspend_power\n");*/
	SET_RESET_PIN(0);
	MDELAY(1);
	display_bias_disable();
}

static void lcm_resume_power(void)
{
	/*pr_debug("lcm_resume_power\n");*/
	display_bias_enable();
	MDELAY(15);
}


#if 0
static int cabc_status = 0;
static void setCabcStatus(void)
{
	int i;
	for(i = 0; i< sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table); i++){
		if(lcm_initialization_setting[i].cmd == 0x55){
			lcm_initialization_setting[i].para_list[0] = cabc_status;
			break;
		}
	}
}
#endif

static void lcm_init(void)
{
	int a = 0;

	SET_RESET_PIN(0);
	MDELAY(2);
	SET_RESET_PIN(1);
	MDELAY(50);

	a = sizeof(lcm_initialization_setting)/sizeof(struct LCM_setting_table);
	push_table(NULL, lcm_initialization_setting, a, 1);
}


static void lcm_suspend(void)
{
	int a = 0;

	a = sizeof(lcm_deep_sleep_setting)/sizeof(struct LCM_setting_table);
	push_table(NULL, lcm_deep_sleep_setting, a, 1);
}


static void lcm_resume(void)
{
	lcm_init();
}


static unsigned int lcm_esd_recover(void)
{
#ifndef BUILD_LK
	lcm_resume_power();
	lcm_init();
	push_table(NULL, bl_level, sizeof(bl_level) / sizeof(struct LCM_setting_table), 1);
	return FALSE;
#else
	return FALSE;
#endif

}

#if 0
static struct LCM_setting_table cabc_level[] = {
        {0x55, 1, {0x00} },
        {REGFLAG_END_OF_TABLE, 0x00, {} }
};

static void lcm_set_cabc_cmdq(void *handle, unsigned int enable)
{
	if(enable == 1 || enable == 0 || enable == 3 || enable == 2){
		pr_debug("in TIANMA panel driver , cabc set to vaule %d\n", enable);
		cabc_level[0].para_list[0] = enable;
		push_table(handle, cabc_level, sizeof(cabc_level) / sizeof(struct LCM_setting_table), 1);
		cabc_status = enable;
		setCabcStatus();
	}else{
		pr_debug("in TIANMA panel driver , cabc set to invalid vaule %d\n", enable);
	}
}


static void lcm_get_cabc_status(int *status)
{
	*status = cabc_status;
	pr_debug("in TIANMA panel driver , cabc get to %d\n", cabc_status);
}
#endif

#if 0
static void lcm_get_cabc_status(int *status)
{
	unsigned char buffer[2] = {0};
	unsigned int array[16] = {0};

	array[0] = 0x00013700;	/* read id return two byte,cabc mode and 0 */
	dsi_set_cmdq(array, 1, 1);

	read_reg_v2(0x56, buffer, 1);

	pr_debug("read cabc %x,%x\n",buffer[0],buffer[1]);

	pr_debug("in TIANMA panel driver , cabc get to %d\n", buffer[0]);
	*status = buffer[0];
}
#endif


struct LCM_DRIVER hx8394f_hd720_dsi_vdo_tianma_lcm_drv = {

	.name = "hx8394f_hd720_dsi_vdo_tianma",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init_power = lcm_init_power,
	.resume_power = lcm_resume_power,
	.suspend_power = lcm_suspend_power,
	.esd_recover = lcm_esd_recover,
	.init = lcm_init,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,

	//.set_cabc_cmdq = lcm_set_cabc_cmdq,
	//.get_cabc_status = lcm_get_cabc_status,

};
