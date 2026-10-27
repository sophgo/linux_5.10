#ifdef ENV_CVITEST
#include <common.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include "system_common.h"
#include "timer.h"
#elif defined(ENV_EMU)
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "emu/command.h"
#else
#include <linux/types.h>
#include <linux/delay.h>

#endif	// ENV_CVITEST

#include "vip_common.h"
#include "scaler.h"
//#include "vip_sys_reg.h"
#include "scaler_reg.h"
#include "vpss_compat.h"
#include "cmdq.h"
#include "reg_sc_core.h"
#include "reg_sc_top.h"
#include "reg_img_in.h"
#include "reg_sc_odma.h"
/* oenc.h is only needed by the GOP path (sclr_gop_odec_set_cfg_from_oenc),
 * which is disabled with #if 0 below for the kernel build.
 */
#if 0
#ifndef HDMI_BUILD
#include "oenc.h"
#endif
#endif


/****************************************************************************
 * Global parameters
 ****************************************************************************/
static struct sclr_core_cfg g_sc_cfg[SCL_MAX_INST];
static struct sclr_gop_cfg g_gop_cfg[SCL_MAX_INST][SCL_MAX_GOP_INST];
static struct sclr_img_cfg g_img_cfg[IMG_MAX_INST];
static struct sclr_cir_cfg g_cir_cfg[SCL_MAX_INST];
static struct sclr_border_cfg g_bd_cfg[SCL_MAX_INST];
static struct sclr_border_vpp_cfg g_bd_vpp_cfg[SCL_MAX_INST][BORDER_VPP_MAX];
static struct sclr_odma_cfg g_odma_cfg[SCL_MAX_INST];
static struct sclr_fbd_cfg g_fbd_cfg[SCL_MAX_INST];
static uintptr_t reg_base = 0;
/****************************************************************************
 * Initial info
 ****************************************************************************/
#define DEFINE_CSC_COEF0(a, b, c) \
		.coef[0][0] = a, .coef[0][1] = b, .coef[0][2] = c,
#define DEFINE_CSC_COEF1(a, b, c) \
		.coef[1][0] = a, .coef[1][1] = b, .coef[1][2] = c,
#define DEFINE_CSC_COEF2(a, b, c) \
		.coef[2][0] = a, .coef[2][1] = b, .coef[2][2] = c,
static struct sclr_csc_matrix csc_mtrx[SCL_CSC_MAX] = {
	// none
	{
		DEFINE_CSC_COEF0(BIT(10),	0,		0)
		DEFINE_CSC_COEF1(0,		BIT(10),	0)
		DEFINE_CSC_COEF2(0,		0,		BIT(10))
		.sub[0] = 0,   .sub[1] = 0,   .sub[2] = 0,
		.add[0] = 0,   .add[1] = 0,   .add[2] = 0
	},
	// yuv2rgb
	// 601 Limited
	//	R = Y				+ 1.402* Pr 		   //
	//	G = Y - 0.344 * Pb	- 0.792* Pr 			//
	//	B = Y + 1.772 * Pb							//
	{
		DEFINE_CSC_COEF0(BIT(10),	0,		1436)
		DEFINE_CSC_COEF1(BIT(10),	BIT(13) | 352,	BIT(13) | 731)
		DEFINE_CSC_COEF2(BIT(10),	1815,		0)
		.sub[0] = 0,   .sub[1] = 128, .sub[2] = 128,
		.add[0] = 0,   .add[1] = 0,   .add[2] = 0
	},
	// 601 Full
	//	R = 1.164 *(Y - 16) 					+ 1.596 *(Cr - 128) 					//
	//	G = 1.164 *(Y - 16) - 0.392 *(Cb - 128) - 0.812 *(Cr - 128) //
	//	B = 1.164 *(Y - 16) + 2.016 *(Cb - 128) 					//
	{
		DEFINE_CSC_COEF0(1192,	0,		1634)
		DEFINE_CSC_COEF1(1192,	BIT(13) | 401,	BIT(13) | 833)
		DEFINE_CSC_COEF2(1192,	2065,		0)
		.sub[0] = 16,  .sub[1] = 128, .sub[2] = 128,
		.add[0] = 0,   .add[1] = 0,   .add[2] = 0
	},
	// 709 Limited
	// R = Y				   + 1.540(Cr – 128)
	// G = Y - 0.183(Cb – 128) – 0.459(Cr – 128)
	// B = Y + 1.816(Cb – 128)
	{
		DEFINE_CSC_COEF0(BIT(10),	0,		1577)
		DEFINE_CSC_COEF1(BIT(10),	BIT(13) | 187,	BIT(13) | 470)
		DEFINE_CSC_COEF2(BIT(10),	1860,		0)
		.sub[0] = 0,   .sub[1] = 128, .sub[2] = 128,
		.add[0] = 0,   .add[1] = 0,   .add[2] = 0
	},
	// 709 Full
	//	R = 1.164 *(Y - 16) 					+ 1.792 *(Cr - 128) 					//
	//	G = 1.164 *(Y - 16) - 0.213 *(Cb - 128) - 0.534 *(Cr - 128) //
	//	B = 1.164 *(Y - 16) + 2.114 *(Cb - 128) 					//
	{
		DEFINE_CSC_COEF0(1192,	0,		1836)
		DEFINE_CSC_COEF1(1192,	BIT(13) | 218,	BIT(13) | 547)
		DEFINE_CSC_COEF2(1192,	2166,		0)
		.sub[0] = 16,  .sub[1] = 128, .sub[2] = 128,
		.add[0] = 0,   .add[1] = 0,   .add[2] = 0
	},
	// rgb2yuv
	// 601 Limited
	//	Y = 0.299 * R + 0.587 * G + 0.114 * B		//
	// Pb =-0.169 * R - 0.331 * G + 0.500 * B		//
	// Pr = 0.500 * R - 0.419 * G - 0.081 * B		//
	{
		DEFINE_CSC_COEF0(306,		601,		117)
		DEFINE_CSC_COEF1(BIT(13)|173,	BIT(13)|339,	512)
		DEFINE_CSC_COEF2(512,		BIT(13)|429,	BIT(13)|83)
		.sub[0] = 0,   .sub[1] = 0,   .sub[2] = 0,
		.add[0] = 0,   .add[1] = 128, .add[2] = 128
	},
	// 601 Full
	//	Y = 16	+ 0.257 * R + 0.504 * g + 0.098 * b //
	// Cb = 128 - 0.148 * R - 0.291 * g + 0.439 * b //
	// Cr = 128 + 0.439 * R - 0.368 * g - 0.071 * b //
	{
		DEFINE_CSC_COEF0(263,		516,		100)
		DEFINE_CSC_COEF1(BIT(13)|152,	BIT(13)|298,	450)
		DEFINE_CSC_COEF2(450,		BIT(13)|377,	BIT(13)|73)
		.sub[0] = 0,   .sub[1] = 0,   .sub[2] = 0,
		.add[0] = 16,  .add[1] = 128, .add[2] = 128
	},
	// 709 Limited
	//	 Y =	   0.2126	0.7152	 0.0722
	//	Cb = 128 - 0.1146  -0.3854	 0.5000
	//	Cr = 128 + 0.5000  -0.4542	-0.0468
	{
		DEFINE_CSC_COEF0(218,		732,		74)
		DEFINE_CSC_COEF1(BIT(13)|117,	BIT(13)|395,	512)
		DEFINE_CSC_COEF2(512,		BIT(13)|465,	BIT(13)|48)
		.sub[0] = 0,   .sub[1] = 0,   .sub[2] = 0,
		.add[0] = 0,   .add[1] = 128, .add[2] = 128
	},
	// 709 Full
	//	Y = 16	+ 0.183 * R + 0.614 * g + 0.062 * b //
	// Cb = 128 - 0.101 * R - 0.339 * g + 0.439 * b //
	// Cr = 128 + 0.439 * R - 0.399 * g - 0.040 * b //
	{
		DEFINE_CSC_COEF0(187,		629,		63)
		DEFINE_CSC_COEF1(BIT(13)|103,	BIT(13)|347,	450)
		DEFINE_CSC_COEF2(450,		BIT(13)|408,	BIT(13)|41)
		.sub[0] = 0,   .sub[1] = 0,   .sub[2] = 0,
		.add[0] = 16,  .add[1] = 128, .add[2] = 128
	},
	{
		DEFINE_CSC_COEF0(BIT(12),	0,		0)
		DEFINE_CSC_COEF1(0,		BIT(12),	0)
		DEFINE_CSC_COEF2(0,		0,		BIT(12))
		.sub[0] = 0,   .sub[1] = 0,   .sub[2] = 0,
		.add[0] = 0,   .add[1] = 0,   .add[2] = 0
	},
};

static int scl_coef_bicubic[128][4] = {
	{-4,	1024,	4,	0},
	{-8,	1024,	8,	0},
	{-12,	1024,	12, 0},
	{-16,	1024,	16, 0},
	{-19,	1023,	20, 0},
	{-23,	1023,	25, -1},
	{-26,	1022,	29, -1},
	{-30,	1022,	33, -1},
	{-34,	1022,	37, -1},
	{-37,	1021,	42, -2},
	{-40,	1020,	46, -2},
	{-44,	1020,	50, -2},
	{-47,	1019,	55, -3},
	{-50,	1018,	59, -3},
	{-53,	1017,	63, -3},
	{-56,	1016,	68, -4},
	{-59,	1015,	72, -4},
	{-62,	1014,	77, -5},
	{-65,	1013,	81, -5},
	{-68,	1012,	86, -6},
	{-71,	1011,	90, -6},
	{-74,	1010,	95, -7},
	{-76,	1008,	100,	-8},
	{-79,	1007,	104,	-8},
	{-81,	1005,	109,	-9},
	{-84,	1004,	113,	-9},
	{-86,	1002,	118,	-10},
	{-89,	1001,	123,	-11},
	{-91,	999,	128,	-12},
	{-94,	998,	132,	-12},
	{-96,	996,	137,	-13},
	{-98,	994,	142,	-14},
	{-100,	992,	147,	-15},
	{-102,	990,	152,	-16},
	{-104,	988,	157,	-17},
	{-106,	986,	161,	-17},
	{-108,	984,	166,	-18},
	{-110,	982,	171,	-19},
	{-112,	980,	176,	-20},
	{-114,	978,	181,	-21},
	{-116,	976,	186,	-22},
	{-117,	973,	191,	-23},
	{-119,	971,	196,	-24},
	{-121,	969,	201,	-25},
	{-122,	966,	206,	-26},
	{-124,	964,	211,	-27},
	{-125,	961,	216,	-28},
	{-127,	959,	221,	-29},
	{-128,	956,	226,	-30},
	{-130,	954,	231,	-31},
	{-131,	951,	237,	-33},
	{-132,	948,	242,	-34},
	{-133,	945,	247,	-35},
	{-134,	942,	252,	-36},
	{-136,	940,	257,	-37},
	{-137,	937,	262,	-38},
	{-138,	934,	267,	-39},
	{-139,	931,	273,	-41},
	{-140,	928,	278,	-42},
	{-141,	925,	283,	-43},
	{-142,	922,	288,	-44},
	{-142,	918,	294,	-46},
	{-143,	915,	299,	-47},
	{-144,	912,	304,	-48},
	{-145,	909,	309,	-49},
	{-145,	905,	315,	-51},
	{-146,	902,	320,	-52},
	{-147,	899,	325,	-53},
	{-147,	895,	330,	-54},
	{-148,	892,	336,	-56},
	{-148,	888,	341,	-57},
	{-149,	885,	346,	-58},
	{-149,	881,	352,	-60},
	{-150,	878,	357,	-61},
	{-150,	874,	362,	-62},
	{-150,	870,	367,	-63},
	{-151,	867,	373,	-65},
	{-151,	863,	378,	-66},
	{-151,	859,	383,	-67},
	{-151,	855,	389,	-69},
	{-151,	851,	394,	-70},
	{-152,	848,	399,	-71},
	{-152,	844,	405,	-73},
	{-152,	840,	410,	-74},
	{-152,	836,	415,	-75},
	{-152,	832,	421,	-77},
	{-152,	828,	426,	-78},
	{-152,	824,	431,	-79},
	{-151,	819,	437,	-81},
	{-151,	815,	442,	-82},
	{-151,	811,	447,	-83},
	{-151,	807,	453,	-85},
	{-151,	803,	458,	-86},
	{-151,	799,	463,	-87},
	{-150,	794,	469,	-89},
	{-150,	790,	474,	-90},
	{-150,	786,	479,	-91},
	{-149,	781,	485,	-93},
	{-149,	777,	490,	-94},
	{-149,	773,	495,	-95},
	{-148,	768,	501,	-97},
	{-148,	764,	506,	-98},
	{-147,	759,	511,	-99},
	{-147,	755,	516,	-100},
	{-146,	750,	522,	-102},
	{-146,	746,	527,	-103},
	{-145,	741,	532,	-104},
	{-144,	736,	537,	-105},
	{-144,	732,	543,	-107},
	{-143,	727,	548,	-108},
	{-142,	722,	553,	-109},
	{-142,	718,	558,	-110},
	{-141,	713,	563,	-111},
	{-140,	708,	569,	-113},
	{-140,	704,	574,	-114},
	{-139,	699,	579,	-115},
	{-138,	694,	584,	-116},
	{-137,	689,	589,	-117},
	{-136,	684,	594,	-118},
	{-135,	679,	600,	-120},
	{-135,	675,	605,	-121},
	{-134,	670,	610,	-122},
	{-133,	665,	615,	-123},
	{-132,	660,	620,	-124},
	{-131,	655,	625,	-125},
	{-130,	650,	630,	-126},
	{-129,	645,	635,	-127},
	{-128,	640,	640,	-128},
};

static int scl_coef_bicubic_opencv[128][4] = {
{-3, 1024, 3, 0},
{-6, 1024, 6, 0},
{-9, 1024, 9, 0},
{-12, 1023, 12, 0},
{-14, 1023, 16, 0},
{-17, 1023, 19, 0},
{-20, 1022, 22, -1},
{-23, 1022, 25, -1},
{-25, 1021, 29, -1},
{-28, 1021, 32, -1},
{-30, 1020, 36, -1},
{-33, 1019, 39, -2},
{-35, 1018, 43, -2},
{-38, 1017, 46, -2},
{-40, 1016, 50, -2},
{-42, 1015, 54, -3},
{-44, 1014, 57, -3},
{-47, 1013, 61, -4},
{-49, 1012, 65, -4},
{-51, 1011, 69, -4},
{-53, 1009, 73, -5},
{-55, 1008, 77, -5},
{-57, 1006, 80, -6},
{-59, 1005, 84, -6},
{-61, 1003, 88, -7},
{-63, 1002, 93, -7},
{-65, 1000, 97, -8},
{-67, 998, 101, -8},
{-68, 996, 105, -9},
{-70, 994, 109, -9},
{-72, 992, 113, -10},
{-74, 990, 118, -10},
{-75, 988, 122, -11},
{-77, 986, 126, -12},
{-78, 984, 130, -12},
{-80, 982, 135, -13},
{-81, 980, 139, -14},
{-83, 977, 144, -14},
{-84, 975, 148, -15},
{-85, 973, 153, -16},
{-87, 970, 157, -17},
{-88, 968, 162, -17},
{-89, 965, 166, -18},
{-91, 962, 171, -19},
{-92, 960, 176, -20},
{-93, 957, 180, -20},
{-94, 954, 185, -21},
{-95, 951, 190, -22},
{-96, 949, 194, -23},
{-97, 946, 199, -24},
{-98, 943, 204, -24},
{-99, 940, 209, -25},
{-100, 937, 213, -26},
{-101, 933, 218, -27},
{-102, 930, 223, -28},
{-103, 927, 228, -29},
{-103, 924, 233, -30},
{-104, 921, 238, -30},
{-105, 917, 243, -31},
{-106, 914, 248, -32},
{-106, 911, 253, -33},
{-107, 907, 258, -34},
{-107, 904, 263, -35},
{-108, 900, 268, -36},
{-109, 896, 273, -37},
{-109, 893, 278, -38},
{-110, 889, 283, -39},
{-110, 885, 288, -40},
{-110, 882, 294, -41},
{-111, 878, 299, -42},
{-111, 874, 304, -43},
{-112, 870, 309, -44},
{-112, 866, 314, -45},
{-112, 862, 319, -46},
{-112, 858, 325, -47},
{-113, 854, 330, -48},
{-113, 850, 335, -49},
{-113, 846, 340, -50},
{-113, 842, 346, -51},
{-113, 838, 351, -52},
{-114, 834, 356, -53},
{-114, 830, 362, -54},
{-114, 825, 367, -55},
{-114, 821, 372, -56},
{-114, 817, 377, -57},
{-114, 813, 383, -58},
{-114, 808, 388, -59},
{-114, 804, 394, -60},
{-114, 799, 399, -61},
{-114, 795, 404, -62},
{-113, 790, 410, -63},
{-113, 786, 415, -64},
{-113, 781, 420, -65},
{-113, 777, 426, -66},
{-113, 772, 431, -67},
{-112, 768, 436, -68},
{-112, 763, 442, -68},
{-112, 758, 447, -69},
{-112, 753, 453, -70},
{-111, 749, 458, -71},
{-111, 744, 463, -72},
{-111, 739, 469, -73},
{-110, 734, 474, -74},
{-110, 730, 480, -75},
{-110, 725, 485, -76},
{-109, 720, 490, -77},
{-109, 715, 496, -78},
{-108, 710, 501, -79},
{-108, 705, 507, -80},
{-107, 700, 512, -81},
{-107, 695, 517, -82},
{-106, 690, 523, -83},
{-106, 685, 528, -84},
{-105, 680, 534, -84},
{-105, 675, 539, -85},
{-104, 670, 544, -86},
{-103, 665, 550, -87},
{-103, 660, 555, -88},
{-102, 655, 560, -89},
{-102, 650, 566, -90},
{-101, 644, 571, -90},
{-100, 639, 576, -91},
{-100, 634, 582, -92},
{-99, 629, 587, -93},
{-98, 624, 592, -94},
{-97, 618, 597, -94},
{-97, 613, 603, -95},
{-96, 608, 608, -96},
};

/****************************************************************************
 * Interfaces
 ****************************************************************************/
void sclr_set_base_addr(void *base)
{
	reg_base = (uintptr_t)base;
}

/**
 * sclr_top_set_cfg - set scl-top's configurations.
 *
 * @param cfg: scl-top's settings.
 */
void sclr_top_set_cfg(u8 inst, bool sc_enable, bool fbd_enable)
{
	union sclr_top_cfg_01 cfg_01;
	u8 qos_en;

	cfg_01.raw = 0;
	cfg_01.b.sc_enable = sc_enable;
	cfg_01.b.fbd_enable = fbd_enable;
	qos_en = BIT(2); //img_in
	if (sc_enable)
		qos_en |= BIT(4);
	if (fbd_enable)
		qos_en |= BIT(7);
	else
		g_fbd_cfg[inst].enable = false;

	cfg_01.b.qos_en = qos_en;
	_reg_write(reg_base + REG_SCL_TOP_CFG1(inst), cfg_01.raw);
	sclr_top_reg_done(inst);
}

/**
 * sclr_rt_set_cfg - configure sc's channels real-time.
 *
 * @param cfg: sc's channels real-time settings.
 */
void sclr_rt_set_cfg(u8 inst, union sclr_rt_cfg cfg)
{
	_reg_write(reg_base + REG_SCL_TOP_AXI(inst), cfg.raw);
}

union sclr_rt_cfg sclr_rt_get_cfg(u8 inst)
{
	union sclr_rt_cfg cfg;

	cfg.raw = _reg_read(reg_base + REG_SCL_TOP_AXI(inst));
	return cfg;
}

/**
 * sclr_top_reg_done - to mark all sc-reg valid for update.
 *
 */
void sclr_top_reg_done(u8 inst)
{
	_reg_write_mask(reg_base + REG_SCL_TOP_CFG0(inst), BIT(0), BIT(0));
}

/**
 * sclr_top_reg_force_up - trigger reg update by sw.
 *
 */
void sclr_top_reg_force_up(u8 inst)
{
	// _reg_write_mask(reg_base + REG_SCL_TOP_SHD(inst), 0x001, 0x1);
}

u8 sclr_top_pg_late_get_bus(u8 inst)
{
	return (_reg_read(reg_base + REG_SCL_TOP_PG(inst)) >> 8) & 0xff;
}

void sclr_top_pg_late_clr(u8 inst)
{
	_reg_write_mask(reg_base + REG_SCL_TOP_PG(inst), 0x0f0000, 0x80000);
}

void sclr_top_get_sb_default(struct sclr_top_sb_cfg *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
}

void sclr_top_set_src_share(u8 inst, bool isShare)
{
	u32 val = isShare ? 0x1 : 0x0;

	_reg_write(reg_base + REG_SCL_TOP_SRC_SHARE(inst), val);

	//if (!isShare)
		//_reg_write(reg_base + REG_SCL_IMG_CFG(inst), 2);
}

/**
 * sclr_init - setup sclr's scaling coef
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 */
void sclr_init(u8 inst, enum sclr_algorithm coef)
{
	u8 i = 0;

	if (inst >= SCL_MAX_INST) {
		uartlog("[bm-vip][sc] %s: no enough sclr-instance ", __func__);
		uartlog("for the requirement(%d)\n", inst);
		return;
	}

	//_reg_write(reg_base + REG_SCL_TOP_SHD(inst), 0x200);
	if (coef == SCL_BICUBIC) {
		for (i = 0; i < 128; ++i) {
			_reg_write(reg_base + REG_SCL_COEF1(inst),
				(scl_coef_bicubic[i][1] << 16) |
				(scl_coef_bicubic[i][0] & 0x0fff));
			_reg_write(reg_base + REG_SCL_COEF2(inst),
				(scl_coef_bicubic[i][3] << 16) |
				(scl_coef_bicubic[i][2] & 0x0fff));
			_reg_write(reg_base + REG_SCL_COEF0(inst), (0x5 << 8) | i);
		}
	} else if (coef == SCL_NEAREST) {
		int nearest_coef[4] = {0, 1024, 0, 0};

		for (i = 0; i < 128; ++i) {
			_reg_write(reg_base + REG_SCL_COEF1(inst),
				   (nearest_coef[1] << 16) |
				   (nearest_coef[0] & 0x0fff));
			_reg_write(reg_base + REG_SCL_COEF2(inst),
				   (nearest_coef[3] << 16) |
				   (nearest_coef[2] & 0x0fff));
			_reg_write(reg_base + REG_SCL_COEF0(inst), (0x5 << 8) | i);
		}
	} else if (coef == SCL_BILINEAR) {
		int bilinear_coef[4] = {0, 1024, 0, 0};

		for (i = 0; i < 128; ++i) {
			bilinear_coef[1] -= 4;
			bilinear_coef[2] += 4;

			_reg_write(reg_base + REG_SCL_COEF1(inst),
				   (bilinear_coef[1] << 16) |
				   (bilinear_coef[0] & 0x0fff));
			_reg_write(reg_base + REG_SCL_COEF2(inst),
				   (bilinear_coef[3] << 16) |
				   (bilinear_coef[2] & 0x0fff));
			_reg_write(reg_base + REG_SCL_COEF0(inst), (0x5 << 8) | i);
		}
	} else if (coef == SCL_BICUBIC_OPENCV) {
		for (i = 0; i < 128; ++i) {
			_reg_write(reg_base + REG_SCL_COEF1(inst),
				(scl_coef_bicubic_opencv[i][1] << 16) |
				(scl_coef_bicubic_opencv[i][0] & 0x0fff));
			_reg_write(reg_base + REG_SCL_COEF2(inst),
				(scl_coef_bicubic_opencv[i][3] << 16) |
				(scl_coef_bicubic_opencv[i][2] & 0x0fff));
			_reg_write(reg_base + REG_SCL_COEF0(inst), (0x5 << 8) | i);
		}
	}
}

/**
 * sclr_set_cfg - set scl's configurations.
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @param sc_bypass: if bypass scaling-engine.
 * @param gop_bypass: if bypass gop-engine.
 * @param cir_bypass: if bypass circle-engine.
 * @param odma_bypass: if bypass odma-engine.
 */
void sclr_set_cfg(u8 inst, bool sc_bypass, bool gop_bypass,
		  bool cir_bypass, bool odma_bypass)
{
	g_sc_cfg[inst].sc_bypass = sc_bypass;
	g_sc_cfg[inst].gop_bypass = gop_bypass;
	g_sc_cfg[inst].cir_bypass = cir_bypass;
	g_sc_cfg[inst].odma_bypass = odma_bypass;

	_reg_write(reg_base + REG_SCL_CFG(inst), g_sc_cfg[inst].raw);
}

/**
 * sclr_get_cfg - get scl_core's cfg
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @return: scl_core's cfg
 */
struct sclr_core_cfg *sclr_get_cfg(u8 inst)
{
	if (inst >= SCL_MAX_INST)
		return NULL;
	return &g_sc_cfg[inst];
}

/**
 * sclr_reg_shadow_sel - control the read reg-bank.
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @param read_shadow: true(shadow); false(working)
 */
void sclr_reg_shadow_sel(u8 inst, bool read_shadow)
{
	_reg_write(reg_base + REG_SCL_SHD(inst), (read_shadow ? 0x0 : 0x2));
}

/**
 * sclr_reg_force_up - trigger reg update by sw.
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 */
void sclr_reg_force_up(u8 inst)
{
	_reg_write_mask(reg_base + REG_SCL_TOP_SHD(inst), BIT(0), BIT(0));
}

struct sclr_status sclr_get_status(u8 inst)
{
	struct sclr_status status;

	u32 tmp = _reg_read(reg_base + REG_SCL_STATUS(inst));

	memcpy(&status, &tmp, sizeof(tmp));

	return status;
}

/**
 * sclr_set_input_size - update scl's input-size.
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @param src_rect: size of input-src
 */
void sclr_set_input_size(u8 inst, struct sclr_size src_rect)
{
	_reg_write(reg_base + REG_SCL_SRC_SIZE(inst),
		   ((src_rect.h - 1) << 16) | (src_rect.w - 1));

	g_sc_cfg[inst].sc.src = src_rect;
}

/**
 * sclr_set_crop - update scl's crop-size/position.
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @param crop_rect: to specify pos/size of crop
 */
void sclr_set_crop(u8 inst, struct sclr_rect crop_rect, bool is_update)
{
	_reg_write(reg_base + REG_SCL_CROP_OFFSET(inst),
		   (crop_rect.y << 16) | crop_rect.x);
	_reg_write(reg_base + REG_SCL_CROP_SIZE(inst),
		   ((crop_rect.h - 1) << 16) | (crop_rect.w - 1));
	if(is_update)
		g_sc_cfg[inst].sc.crop = crop_rect;
}

/**
 * sclr_set_output_size - update scl's output-size.
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @param src_rect: size of output-src
 */
void sclr_set_output_size(u8 inst, struct sclr_size size)
{
	_reg_write(reg_base + REG_SCL_OUT_SIZE(inst),
		   ((size.h - 1) << 16) | (size.w - 1));

	g_sc_cfg[inst].sc.dst = size;

}

void sclr_set_scale_mir(u8 inst, bool enable)
{
	u32 tmp = 0;

	g_sc_cfg[inst].sc.mir_enable = enable;
	tmp = (enable) ? 0x03 : 0x02;
	// enable cb_mode of scaling, since it only works on fac > 4
	_reg_write_mask(reg_base + REG_SCL_SC_CFG(inst), 0x3, tmp);
}

/**
 * sclr_set_scale_phase - Used in tile mode to decide the phase of first data in scaling.
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @param h_ph: initial horizontal phase
 * @param v_ph: initial vertical phase
 */
void sclr_set_scale_phase(u8 inst, u32 h_pos, u32 v_pos)
{
	_reg_write(reg_base + REG_SCL_SC_H_INI_PH(inst), h_pos);
	_reg_write(reg_base + REG_SCL_SC_V_INI_PH(inst), v_pos);
}

/**
 * sclr_set_scale_mode - control sclr's scaling coef.
 *
 * @param inst: 0~2
 * @param mir_enable: mirror enable or not
 * @param cb_enable: cb mode or not. cb only work if scaling smaller than 1/4.
 *					 In cb mode, use it's own coeff rather than ones update by sclr_update_coef().
 * @param update: update parameter or not
 */
void sclr_set_scale_mode(u8 inst, bool mir_enable, bool cb_enable, bool update)
{
		u32 tmp = 0;

		if (update) {
				g_sc_cfg[inst].sc.mir_enable = mir_enable;
				//g_sc_cfg[inst].sc.cb_enable = cb_enable;
		}

		// mir/cb isn't suggested to enable together
		if (g_sc_cfg[inst].sc.tile_enable) {
				tmp = mir_enable ? 0x03 : 0x02;
		} else {
				tmp = cb_enable ? 0x13 : 0x03;
		}

		// enable cb_mode of scaling, since it only works on fac > 4
		_reg_write(reg_base + REG_SCL_SC_CFG(inst), tmp);
}

/**
 * sclr_set_scale - update scl's scaling settings by current crop/dst values.
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 */
void sclr_set_scale(u8 inst)
{
	u64 fix_fac;
	struct sclr_fac_cfg fac;
	bool isFacOver4 = false;
	fac.h_pos = 0;
	fac.v_pos = 0;

	if (g_sc_cfg[inst].sc.src.w == 0 || g_sc_cfg[inst].sc.src.h == 0)
		return;
	if (g_sc_cfg[inst].sc.crop.w == 0 || g_sc_cfg[inst].sc.crop.h == 0)
		return;
	if (g_sc_cfg[inst].sc.dst.w == 0 || g_sc_cfg[inst].sc.dst.h == 0)
		return;

	if(g_sc_cfg[inst].sc.algorithm == SCL_BICUBIC){
		// scale_x, 18.13
		fix_fac = (u64)(g_sc_cfg[inst].sc.crop.w) << 13;
		fac.h_fac = fix_fac / (g_sc_cfg[inst].sc.dst.w);
		isFacOver4 |= (fac.h_fac >= (4 << 13));
		// scale_y, 18.13
		fix_fac = (u64)(g_sc_cfg[inst].sc.crop.h) << 13;
		fac.v_fac = fix_fac / (g_sc_cfg[inst].sc.dst.h);
		isFacOver4 |= (fac.v_fac >= (4 << 13));
		sclr_set_scale_mode(inst, g_sc_cfg[inst].sc.mir_enable, isFacOver4, true);
	} else {
		// scale_x, 8.23
		fix_fac = (u64)(g_sc_cfg[inst].sc.crop.w) << 23;
		fac.h_fac = fix_fac / (g_sc_cfg[inst].sc.dst.w);
		// scale_y, 8.23
		fix_fac = (u64)(g_sc_cfg[inst].sc.crop.h) << 23;
		fac.v_fac = fix_fac / (g_sc_cfg[inst].sc.dst.h);
		//23bit open
		sclr_set_scale_mode(inst, true, false, true);
		_reg_write_mask(reg_base + REG_SCL_SC_CFG(inst), 0xc00, 0xc00);
		if(g_sc_cfg[inst].sc.algorithm != SCL_NEAREST){
			fac.h_pos = (fac.h_fac >= (1 << 23)) ? ((fac.h_fac - (1 << 23)) >> 1) : (((1 << 23) - fac.h_fac) >> 1);
			fac.v_pos = (fac.v_fac >= (1 << 23)) ? ((fac.v_fac - (1 << 23)) >> 1) : (((1 << 23) - fac.v_fac) >> 1);
			if(fac.h_fac < (1 << 23)){
				_reg_write_mask(reg_base + REG_SCL_SC_CFG(inst), BIT(6), BIT(6));
				fac.h_pos |= BIT(31);
				fac.h_pos = (fac.h_pos ^ 0x7fffffff) + 1;
			}
			if(fac.v_fac < (1 << 23)){
				_reg_write_mask(reg_base + REG_SCL_SC_CFG(inst), BIT(7), BIT(7));
				fac.v_pos |= BIT(31);
				fac.v_pos = (fac.v_pos ^ 0x7fffffff) + 1;
			}
			//lut round
			_reg_write_mask(reg_base + REG_SCL_SC_CFG(inst), 0x300, 0x300);
		} else {
			fac.h_pos = 1 << 22;
			fac.v_pos = 1 << 22;
			_reg_write_mask(reg_base + REG_SCL_SC_CFG(inst), BIT(6), BIT(6));
			fac.h_pos |= BIT(31);
			fac.h_pos = (fac.h_pos ^ 0x7fffffff) + 1;
			_reg_write_mask(reg_base + REG_SCL_SC_CFG(inst), BIT(7), BIT(7));
			fac.v_pos |= BIT(31);
			fac.v_pos = (fac.v_pos ^ 0x7fffffff) + 1;
		}
	}
	_reg_write_mask(reg_base + REG_SCL_SC_H_CFG(inst), 0x7fffffff, fac.h_fac);
	_reg_write_mask(reg_base + REG_SCL_SC_V_CFG(inst), 0x7fffffff, fac.v_fac);
	g_sc_cfg[inst].sc.fac = fac;
	sclr_set_scale_phase(inst, fac.h_pos, fac.v_pos);
	//uartlog("h_fac(%#x) v_fac(%#x)\n", fac.h_fac, fac.v_fac);
	//uartlog("h_pos(%#x) v_pos(%#x)\n", fac.h_pos, fac.v_pos);
}

union sclr_intr sclr_get_intr_mask(u8 inst)
{
	union sclr_intr intr_mask;

	intr_mask.raw = _reg_read(reg_base + REG_SCL_TOP_INTR_MASK(inst));
	return intr_mask;
}

/**
 * sclr_set_intr_mask - sclr's interrupt mask. Only enable ones will be
 *			integrated to vip_subsys.  check 'union sclr_intr'
 *			for each bit mask.
 *
 * @param intr_mask: On/Off ctrl of the interrupt.
 */
void sclr_set_intr_mask(u8 inst, union sclr_intr intr_mask)
{
	_reg_write(reg_base + REG_SCL_TOP_INTR_MASK(inst), intr_mask.raw);
}

/**
 * sclr_intr_ctrl - sclr's interrupt on(1)/off(0)
 *					check 'union sclr_intr' for each bit mask
 *
 * @param intr_mask: On/Off ctrl of the interrupt.
 */
void sclr_intr_ctrl(u8 inst, union sclr_intr intr_mask)
{
	_reg_write(reg_base + REG_SCL_TOP_INTR_ENABLE(inst), intr_mask.raw);
}

/**
 * sclr_intr_clr - clear sclr's interrupt
 *				   check 'union sclr_intr' for each bit mask
 *
 * @param intr_mask: On/Off ctrl of the interrupt.
 */
void sclr_intr_clr(u8 inst, union sclr_intr intr_mask)
{
	_reg_write(reg_base + REG_SCL_TOP_INTR_STATUS(inst), intr_mask.raw);
	if(g_fbd_cfg[inst].enable)
		_reg_write(reg_base + REG_SCL_MAP_CONV_STATUS(inst), BIT(0));
}

/**
 * sclr_intr_status - sclr's interrupt status
 *					  check 'union sclr_intr' for each bit mask
 *
 * @return: The interrupt's status
 */
union sclr_intr sclr_intr_status(u8 inst)
{
	union sclr_intr status;

	status.raw = (_reg_read(reg_base + REG_SCL_TOP_INTR_STATUS(inst)) & 0xffffffff);
	return status;
}

/****************************************************************************
 * SCALER IMG
 ****************************************************************************/
/**
 * sclr_img_set_cfg - set img's configurations.
 *
 * @param img_inst: (0~1), the instance of img-in which want to be configured.
 * @param cfg: img's settings.
 */
void sclr_img_set_cfg(u8 img_inst, struct sclr_img_cfg *cfg)
{
	enum sclr_format in_fmt = cfg->fmt;
	if(in_fmt == SCL_FMT_YUV444_PACKED) in_fmt = SCL_FMT_RGB_PACKED;
	else if(in_fmt == SCL_FMT_YUV444) in_fmt = SCL_FMT_RGB_PLANAR;
	_reg_write(reg_base + REG_SCL_IMG_CFG(img_inst),
		   (cfg->csc_en << 12) | (cfg->burst << 8) |
		   (in_fmt << 4) | cfg->src);
	uartlog("%s: cfg->fifo_y %d\n", __func__, cfg->fifo_y);
	_reg_write_mask(reg_base + REG_SCL_IMG_FIFO_THR(img_inst), 0xff00ff, ((cfg->fifo_c) << 16 | (cfg->fifo_y)));
	_reg_write(reg_base + REG_SCL_IMG_OUTSTANDING(img_inst), cfg->outstanding);
	sclr_img_set_mem(img_inst, &cfg->mem);

	if (cfg->csc == SCL_CSC_NONE) {
		sclr_img_csc_en(img_inst, false);
	} else {
		sclr_img_csc_en(img_inst, true);
		sclr_img_set_csc(img_inst, &csc_mtrx[cfg->csc]);
	}

	sclr_img_dup2fancy_bypass(img_inst, cfg->dup2fancy_enable);

	g_img_cfg[img_inst] = *cfg;
}

/**
 * sclr_img_set_trig - set img's src of job_start.
 *
 * @param inst: (0~1), the instance of img-in which want to be configured.
 * @param trig_src: img's src of job_start.
 */
void sclr_img_set_trig(u8 inst, enum sclr_img_trig_src trig_src)
{
	u32 mask = BIT(13) | BIT(9);
	u32 val = 0;

	switch (trig_src) {
	case SCL_IMG_TRIG_SRC_SW:
		break;
	case SCL_IMG_TRIG_SRC_DISP:
		val |= BIT(9);
		break;
	case SCL_IMG_TRIG_SRC_ISP:
		val |= BIT(13);
		break;
	default:
		break;
	}
	_reg_write_mask(reg_base + REG_SCL_TOP_IMG_CTRL(inst), mask, val);
}

/**
 * sclr_img_get_cfg - get scl_img's cfg
 *
 * @param inst: 0~1
 * @return: scl_img's cfg
 */
struct sclr_img_cfg *sclr_img_get_cfg(u8 inst)
{
	if (inst >= VPSS_MAX)
		return NULL;
	return &g_img_cfg[inst];
}

/**
 * sclr_img_reg_shadow_sel - control scl_img's the read reg-bank.
 *
 * @param read_shadow: true(shadow); false(working)
 */
void sclr_img_reg_shadow_sel(u8 inst, bool read_shadow)
{
	uartlog("REG_SCL_IMG_SHD=0x%x\n", REG_SCL_IMG_SHD(inst));

	_reg_write(reg_base + REG_SCL_IMG_SHD(inst), (read_shadow ? 0x0 : 0x4));
}

/**
 * sclr_img_reg_shadow_mask - reg won't be update by sw/hw until unmask.
 *
 * @param mask: true(mask); false(unmask)
 * @return: mask status before this modification.
 */
bool sclr_img_reg_shadow_mask(u8 inst, bool mask)
{
	bool is_masked = (_reg_read(reg_base + REG_SCL_IMG_SHD(inst)) & BIT(1));

	if (is_masked != mask)
		_reg_write_mask(reg_base + REG_SCL_IMG_SHD(inst), BIT(1),
				(mask ? 0x0 : BIT(1)));

	return is_masked;
}

/**
 * sclr_img_reg_force_up - trigger reg update by sw.
 *
 * @param inst: 0~1
 */
void sclr_img_reg_force_up(u8 inst)
{
	//_reg_write_mask(reg_base + REG_SCL_TOP_SHD(inst), BIT(0), BIT(0));//bit1
}

void sclr_img_reset(u8 inst)
{
	uartlog("----------- REG_SCL_IMG_DBG(%d) ---------------\n", _reg_read(reg_base + REG_SCL_IMG_DBG(inst)));
	_reg_write_mask(reg_base + REG_SCL_IMG_DBG(inst), 0x00040000, BIT(18));
	_reg_write_mask(reg_base + REG_SCL_MAP_CONV_CTRL(inst), BIT(2), BIT(2));
	uartlog("----------- REG_SCL_IMG_DBG(%d) ---------------\n", _reg_read(reg_base + REG_SCL_IMG_DBG(inst)));// reset看idle
}

void sclr_get_sw_cap(u8 inst)
{
	/*
	 * Disabled for the kernel (soph_drm) build: this debug helper accesses
	 * VD-domain registers (0x22060xxx) that are OUTSIDE the ioremap'd vpss0
	 * (0x67110000) window, so going through the vpss_compat.h translation
	 * macros would produce an out-of-range access. Not used by the
	 * disp-online path. Kept as an empty stub.
	 */
#if 0
	uartlog("----------- reg_cap_val_intr(%d) ---------------\n", _reg_read(0x22060104));
	uartlog("----------- reg_cap_val_intr(%d) ---------------\n", _reg_read(0x22060204));
	uartlog("----------- reg_cap_val_sw(%d) ---------------\n", _reg_read(0x22060180));
	uartlog("----------- reg_cap_val_sw(%d) ---------------\n", _reg_read(0x22060280));
#endif
}

void sclr_get_intr_cap(u8 inst)
{
	/*
	 * Disabled for the kernel build: accesses VD-domain (0x22060xxx) and
	 * sysc (0x22000008) registers outside the vpss0 window. Not used by the
	 * disp-online path. Kept as an empty stub.
	 */
#if 0
	//uartlog("----------- before reg_cap_val_1(%d) ---------------\n", _reg_read(0x22060104));
	//uartlog("----------- reg_cap_val_32(%d) ---------------\n", _reg_read(0x22060180));
	//_reg_write_mask(0x22060000, BIT(1), 0);//reg_cap_sig_mask set 0
	_reg_write_mask(0x22060004, BIT(0), 0);//reg_sw_cap_sig set 0
	_reg_write_mask(0x22000008, BIT(8), 0);//reg_clk_en_sysc set 0
	//uartlog("----------- seb reg_cap_sig_mask(%d) ---------------\n", _reg_read(0x22060000));
	//uartlog("----------- set reg_clk_en_sysc(%d) ---------------\n", _reg_read(0x22000008));
#endif
}

void read_handshake_vpss(void)
{
	uartlog("----------- REG_SCL_IMG_HANDSKCNT(%d) ---------------\n", _reg_read(reg_base + REG_SCL_IMG_HANDSKCNT(0)));
}

void read_maxframe_vpss(void)
{
	uartlog("----------- REG_SCL_IMG_MAXFRAME(%d) ---------------\n",_reg_read(reg_base + REG_SCL_IMG_MAXFRAME(0)));
}

void read_curframe_vpss(void)
{
	uartlog("----------- REG_SCL_IMG_CURFRAME(%d) ---------------\n",_reg_read(reg_base + REG_SCL_IMG_CURFRAME(0)));
}

void set_maxruntime_vpss(void)
{
	uartlog("set_maxruntime before %d\n",_reg_read(reg_base + REG_SCL_IMG_MAXFRAME_CLR(0)));
	_reg_write(reg_base + REG_SCL_IMG_MAXFRAME_CLR(0), 1);
	uartlog("set_maxruntime %d\n",_reg_read(reg_base + REG_SCL_IMG_MAXFRAME_CLR(0)));
}

void sclr_img_start(u8 inst)
{
	int i;
	/*
	uartlog("----------- get clk(%d) ---------------\n", _reg_read(0x28102004));
	uartlog("----------- get clk(%d) ---------------\n", _reg_read(0x28102008));
	_reg_write_mask(0x28102004, BIT(30), 0);
	_reg_write_mask(0x28102004, BIT(31), 0);
	_reg_write(0x28102008, 0xFFC00000);
	uartlog("----------- set clk(%d) ---------------\n", _reg_read(0x28102004));
	uartlog("----------- set clk(%d) ---------------\n", _reg_read(0x28102008));
	*/
    // _reg_write_mask(0x22000008, BIT(8), BIT(8));//reg_clk_en_sysc set 1
    // _reg_write_mask(0x22060004, BIT(0), BIT(0));//reg_sw_cap_sig set 1

	//uartlog("----------- seb clk(%d) ---------------\n", _reg_read(0x22060000));
	//_reg_write_mask(0x22060000, BIT(1), BIT(1));//reg_cap_sig_mask set 1
	//uartlog("----------- set clk(%d) ---------------\n", _reg_read(0x22060000));
	// uartlog("----------- reg_cap_val_32(%d) ---------------\n", _reg_read(0x22060180));
	// _reg_write(reg_base + 0x680be010, 0x0);
	sclr_img_reset(inst);
	_reg_write_mask(reg_base + REG_SCL_TOP_IMG_CTRL(inst), 0x00000003, BIT(1));
	for(i = 0; i < 3; i++)
	{
		read_curframe_vpss(); // test_debugmode
	}
	if(g_fbd_cfg[inst].enable){
		_reg_write_mask(reg_base + REG_SCL_MAP_CONV_CTRL(inst), BIT(2), 0);
		_reg_write_mask(reg_base + REG_SCL_MAP_CONV_CTRL(inst), BIT(0), BIT(0));
	}
}

void sclr_debug_set_odma_lathch(u8 inst)
{
	_reg_write(reg_base + REG_SCL_ODMA_LATCH_LINE_CNT(inst), 1);
	u8 tmp = _reg_read(reg_base + REG_SCL_ODMA_LATCH_LINE_CNT(inst));
	uartlog("----------- sclr_debug_set_odma_lathch(%d) ---------------\n", tmp);
}

u16 sclr_debug_get_odma_lathch(u8 inst)
{
	//u8 tmp = _reg_read(reg_base + REG_SCL_ODMA_LATCH_LINE_CNT(inst)) & 0x1;
	//uartlog("----------- sclr_debug_set_odma_lathch before get(%d) ---------------\n", tmp);
	//return ((_reg_read(reg_base + REG_SCL_ODMA_LATCH_LINE_CNT(inst)) >> 8) & 0x3fff);
	u8 tmp = _reg_read(reg_base + REG_SCL_IMG_HANDSKCNT(inst));
	uartlog("----------- afterIRQ REG_SCL_IMG_HANDSKCNT(%d) ---------------\n", tmp);
	return 0;
}

void sclr_multi_start(u8 inst)
{
	sclr_img_set_trig(inst, SCL_IMG_TRIG_SRC_ISP);
	sclr_reg_force_up(inst);
}

void sclr_multi_init(u8 inst)
{
	_reg_write_mask(reg_base + REG_SCL_TOP_IMG_CTRL(inst), BIT(13), 0);
}

/**
 * sclr_img_set_fmt - set scl_img's input data format
 *
 * @param inst: 0~1
 * @param fmt: scl_img's input format
 */
void sclr_img_set_fmt(u8 inst, enum sclr_format fmt)
{
	_reg_write_mask(reg_base + REG_SCL_IMG_CFG(inst), 0x000000f0, fmt << 4);

	g_img_cfg[inst].fmt = fmt;
}

/**
 * sclr_img_set_mem - setup img's mem settings. Only work if img from mem.
 *
 * @param mem: mem settings for img
 */
void sclr_img_set_mem(u8 inst, struct sclr_mem *mem)
{
	_reg_write(reg_base + REG_SCL_IMG_OFFSET(inst),
		   (mem->start_y << 16) | mem->start_x);
	_reg_write(reg_base + REG_SCL_IMG_SIZE(inst),
		   ((mem->height - 1) << 16) | (mem->width - 1));
	_reg_write(reg_base + REG_SCL_IMG_PITCH_Y(inst), mem->pitch_y);
	_reg_write(reg_base + REG_SCL_IMG_PITCH_C(inst), mem->pitch_c);

	sclr_img_set_addr(inst, mem->addr0, mem->addr1, mem->addr2);

	g_img_cfg[inst].mem = *mem;
}

/**
 * sclr_img_set_addr - setup img's mem address. Only work if img from mem.
 *
 * @param addr0: address of planar0
 * @param addr1: address of planar1
 * @param addr2: address of planar2
 */
void sclr_img_set_addr(u8 inst, u64 addr0, u64 addr1, u64 addr2)
{
	_reg_write(reg_base + REG_SCL_IMG_ADDR0_L(inst), addr0);
	_reg_write(reg_base + REG_SCL_IMG_ADDR0_H(inst), addr0 >> 32);
	_reg_write(reg_base + REG_SCL_IMG_ADDR1_L(inst), addr1);
	_reg_write(reg_base + REG_SCL_IMG_ADDR1_H(inst), addr1 >> 32);
	_reg_write(reg_base + REG_SCL_IMG_ADDR2_L(inst), addr2);
	_reg_write(reg_base + REG_SCL_IMG_ADDR2_H(inst), addr2 >> 32);

	g_img_cfg[inst].mem.addr0 = addr0;
	g_img_cfg[inst].mem.addr1 = addr1;
	g_img_cfg[inst].mem.addr2 = addr2;
}

void sclr_img_csc_en(u8 inst, bool enable)
{
	_reg_write_mask(reg_base + REG_SCL_IMG_CFG(inst), BIT(12),
			enable ? BIT(12) : 0);
}

/**
 * sclr_img_set_csc - configure scl-img's input CSC's coefficient/offset
 *
 * @param inst: (0~1)
 * @param cfg: The settings for CSC
 */
void sclr_img_set_csc(u8 inst, struct sclr_csc_matrix *cfg)
{
	_reg_write(reg_base + REG_SCL_IMG_CSC_COEF0(inst),
		   (cfg->coef[0][1] << 16) | cfg->coef[0][0]);
	_reg_write(reg_base + REG_SCL_IMG_CSC_COEF1(inst),
		   cfg->coef[0][2]);
	_reg_write(reg_base + REG_SCL_IMG_CSC_COEF2(inst),
		   (cfg->coef[1][1] << 16) | cfg->coef[1][0]);
	_reg_write(reg_base + REG_SCL_IMG_CSC_COEF3(inst), cfg->coef[1][2]);
	_reg_write(reg_base + REG_SCL_IMG_CSC_COEF4(inst),
		   (cfg->coef[2][1] << 16) | cfg->coef[2][0]);
	_reg_write(reg_base + REG_SCL_IMG_CSC_COEF5(inst), cfg->coef[2][2]);
	_reg_write(reg_base + REG_SCL_IMG_CSC_SUB(inst),
		   (cfg->sub[2] << 16) | (cfg->sub[1] << 8) | cfg->sub[0]);
	_reg_write(reg_base + REG_SCL_IMG_CSC_ADD(inst),
		   (cfg->add[2] << 16) | (cfg->add[1] << 8) | cfg->add[0]);
}

union sclr_img_dbg_status sclr_img_get_dbg_status(u8 inst, bool clr)
{
	union sclr_img_dbg_status status;

	status.raw = _reg_read(reg_base + REG_SCL_IMG_DBG(inst));

	if (clr) {
		status.b.err_fwr_clr = 1;
		status.b.err_erd_clr = 1;
		status.b.ip_clr = 1;
		status.b.ip_int_clr = 1;
		_reg_write(reg_base + REG_SCL_IMG_DBG(inst), status.raw);
	}

	return status;
}

void sclr_img_checksum_en(u8 inst, bool enable)
{
	_reg_write_mask(reg_base + REG_SCL_IMG_CHECKSUM0(inst), BIT(31),
					enable ? BIT(31) : 0);
}

void sclr_img_get_checksum_status(u8 inst, struct sclr_img_checksum_status *status)
{
	status->checksum_base.raw = _reg_read(reg_base + REG_SCL_IMG_CHECKSUM0(inst));
	status->axi_read_data = _reg_read(reg_base + REG_SCL_IMG_CHECKSUM1(inst));
}

int sclr_img_validate_cb_cfg(struct sclr_img_in_sb_cfg *cfg)
{
	/* 0 : disable, 1 : free-run mode, 2 : frame base mode */
	if (cfg->sb_mode > 2)
		return -1;

	/* 0 : 64 line, 1 : 128 line, 2 : 192 line, 3 : 256 line */
	if (cfg->sb_size > 3)
		return -1;

	return 0;
}

void sclr_img_get_sb_default(struct sclr_img_in_sb_cfg *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	//cfg->mode = 0;	// disable
	//cfg->size = 0;	// 64 line
	cfg->sb_nb = 3;
	cfg->sb_sw_rptr = 0;
	//cfg->set_str = 0;
	//cfg->sw_clr = 0;
}

/**
 * sclr_img_dup2fancy_bypass - dup2fancy bypass
 *
 */
void sclr_img_dup2fancy_bypass(u8 inst, bool enable)
{
	//default 0: enable dup2fancy   1: disable dup2fancy
	_reg_write_mask(reg_base + REG_SCL_IMG_DUP2FANCY(inst), BIT(0),
					enable ? 0 : BIT(0));
}

/****************************************************************************
 * SCALER CIRCLE
 ****************************************************************************/
/**
 * sclr_cir_set_cfg - configure scl-circle
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @param cfg: cir's settings.
 */
void sclr_cir_set_cfg(u8 inst, struct sclr_cir_cfg *cfg)
{
	if (cfg->mode == SCL_CIR_DISABLE) {
		_reg_write_mask(reg_base + REG_SCL_CFG(inst), 0x20, 0x20);
	} else {
		_reg_write(reg_base + REG_SCL_CIR_CFG(inst),
			   (cfg->line_width << 8) | cfg->mode);
		_reg_write(reg_base + REG_SCL_CIR_CENTER_X(inst),
			   cfg->center.x);
		_reg_write(reg_base + REG_SCL_CIR_CENTER_Y(inst),
			   cfg->center.y);
		_reg_write(reg_base + REG_SCL_CIR_RADIUS(inst), cfg->radius);
		_reg_write(reg_base + REG_SCL_CIR_SIZE(inst),
			   ((cfg->rect.h - 1) << 16) | (cfg->rect.w - 1));
		_reg_write(reg_base + REG_SCL_CIR_OFFSET(inst),
			   (cfg->rect.y << 16) | cfg->rect.x);
		_reg_write(reg_base + REG_SCL_CIR_COLOR(inst),
			   (cfg->color_b << 16) | (cfg->color_g << 8) |
			   cfg->color_r);

		_reg_write_mask(reg_base + REG_SCL_CFG(inst), 0x20, 0x00);
	}

	g_cir_cfg[inst] = *cfg;
}

/****************************************************************************
 * SCALER BORDER
 ****************************************************************************/
/**
 * sclr_border_set_cfg - configure scaler's odma
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @param cfg: new border config
 */
void sclr_border_set_cfg(u8 inst, struct sclr_border_cfg *cfg)
{
	if ((g_sc_cfg[inst].sc.dst.w + cfg->start.x) >
		g_odma_cfg[inst].mem.width) {
		uartlog("[bm-vip][sc] %s: sc_width(%d) + offset(%d)", __func__,
			   g_sc_cfg[inst].sc.dst.w, cfg->start.x);
		uartlog(" > odma_width(%d)\n", g_odma_cfg[inst].mem.width);
		cfg->start.x = g_odma_cfg[inst].mem.width -
				   g_sc_cfg[inst].sc.dst.w;
	}

	if ((g_sc_cfg[inst].sc.dst.h + cfg->start.y) >
		g_odma_cfg[inst].mem.height) {
		uartlog("[bm-vip][sc] %s: sc_height(%d) + offset(%d)", __func__,
			   g_sc_cfg[inst].sc.dst.h, cfg->start.y);
		uartlog(" > odma_height(%d)\n", g_odma_cfg[inst].mem.height);
		cfg->start.y = g_odma_cfg[inst].mem.height -
				   g_sc_cfg[inst].sc.dst.h;
	}

	_reg_write(reg_base + REG_SCL_BORDER_CFG(inst), cfg->cfg.raw);
	_reg_write(reg_base + REG_SCL_BORDER_OFFSET(inst),
		   (cfg->start.y << 16) | cfg->start.x);

	g_bd_cfg[inst] = *cfg;
}

void sclr_border_vpp_set_cfg(u8 inst, u8 border_idx, struct sclr_border_vpp_cfg *cfg)
{
	if(cfg->inside_start.x > cfg->inside_end.x || cfg->inside_start.y > cfg->inside_end.y){
		uartlog("[bm-vpss][sc] %s: idx(%d), inside_start.x(%d), inside_start.y(%d), inside_end.x(%d), inside_end.y(%d)", __func__,
				border_idx, cfg->inside_start.x, cfg->inside_start.y, cfg->inside_end.x, cfg->inside_end.y);
	}
	if(cfg->outside_start.x > cfg->outside_end.x || cfg->outside_start.y > cfg->outside_end.y){
		uartlog("[bm-vpss][sc] %s: idx(%d), outside_start.x(%d), outside_start.y(%d), outside_end.x(%d), outside_end.y(%d)", __func__,
				border_idx, cfg->outside_start.x, cfg->outside_start.y, cfg->outside_end.x, cfg->outside_end.y);
	}
	_reg_write(reg_base + REG_SCL_BORDER_VPP_CFG(inst, border_idx), cfg->cfg.raw);
	_reg_write(reg_base + REG_SCL_BORDER_VPP_INX(inst, border_idx),
		   (cfg->inside_end.x << 16) | cfg->inside_start.x);
	_reg_write(reg_base + REG_SCL_BORDER_VPP_INY(inst, border_idx),
		   (cfg->inside_end.y << 16) | cfg->inside_start.y);
	_reg_write(reg_base + REG_SCL_BORDER_VPP_OUTX(inst, border_idx),
		   (cfg->outside_end.x << 16) | cfg->outside_start.x);
	_reg_write(reg_base + REG_SCL_BORDER_VPP_OUTY(inst, border_idx),
		   (cfg->outside_end.y << 16) | cfg->outside_start.y);
	g_bd_vpp_cfg[inst][border_idx] = *cfg;
}

struct sclr_border_vpp_cfg * sclr_border_vpp_get_cfg(u8 inst, u8 border_idx)
{
	if (inst >= SCL_MAX_INST)
		return NULL;
	return &g_bd_vpp_cfg[inst][border_idx];
}

/**
 * sclr_border_get_cfg - get scl_border's cfg
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @return: scl_border's cfg
 */
struct sclr_border_cfg *sclr_border_get_cfg(u8 inst)
{
	if (inst >= SCL_MAX_INST)
		return NULL;
	return &g_bd_cfg[inst];
}

/****************************************************************************
 * SCALER ODMA
 ****************************************************************************/
/**
 * sclr_odma_set_cfg - configure scaler's odma
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @param burst: dma's burst length
 * @param fmt: dma's format
 */
void sclr_odma_set_cfg(u8 inst, struct sclr_odma_cfg *cfg)
{
	enum sclr_format odma_fmt = cfg->fmt;

	if(odma_fmt == SCL_FMT_YUV444_PACKED) odma_fmt = SCL_FMT_RGB_PACKED;
	else if(odma_fmt == SCL_FMT_YUV444) odma_fmt = SCL_FMT_RGB_PLANAR;
	_reg_write(reg_base + REG_SCL_ODMA_CFG(inst),
		   (cfg->flip << 16) |	(odma_fmt << 8) | cfg->burst | BIT(1));
	//uartlog("vpss(%d) odma.fmt(%d)\n", inst, odma_fmt);

	sclr_odma_set_mem(inst, &cfg->mem);
	sclr_ctrl_set_output(inst, &cfg->csc_cfg, cfg->fmt);

	g_odma_cfg[inst] = *cfg;
}

/**
 * sclr_odma_get_cfg - get scl_odma's cfg
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @return: scl_odma's cfg
 */
struct sclr_odma_cfg *sclr_odma_get_cfg(u8 inst)
{
	if (inst >= SCL_MAX_INST)
		return NULL;
	return &g_odma_cfg[inst];
}

/**
 * sclr_odma_set_fmt - set scl_odma's output data format
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @param fmt: scl_odma's output format
 */
void sclr_odma_set_fmt(u8 inst, enum sclr_format fmt)
{
	u32 tmp = fmt << 8;

	if (fmt == SCL_FMT_BF16)
		tmp |= BIT(23);
	_reg_write_mask(reg_base + REG_SCL_ODMA_CFG(inst), 0x0080ff00, tmp);

	g_odma_cfg[inst].fmt = fmt;
}

/**
 * sclr_odma_set_mem - setup odma's mem settings.
 *
 * @param mem: mem settings for odma
 */
void sclr_odma_set_mem(u8 inst, struct sclr_mem *mem)
{
	_reg_write(reg_base + REG_SCL_ODMA_OFFSET_X(inst), mem->start_x);
	_reg_write(reg_base + REG_SCL_ODMA_OFFSET_Y(inst), mem->start_y);
	_reg_write(reg_base + REG_SCL_ODMA_WIDTH(inst), mem->width - 1);
	_reg_write(reg_base + REG_SCL_ODMA_HEIGHT(inst), mem->height - 1);
	_reg_write(reg_base + REG_SCL_ODMA_PITCH_Y(inst), mem->pitch_y);
	_reg_write(reg_base + REG_SCL_ODMA_PITCH_C(inst), mem->pitch_c);

	sclr_odma_set_addr(inst, mem->addr0, mem->addr1, mem->addr2);

	g_odma_cfg[inst].mem = *mem;
}

/**
 * sclr_odma_set_addr - setup odma's mem address.
 *
 * @param addr0: address of planar0
 * @param addr1: address of planar1
 * @param addr2: address of planar2
 */
void sclr_odma_set_addr(u8 inst, u64 addr0, u64 addr1, u64 addr2)
{
	_reg_write(reg_base + REG_SCL_ODMA_ADDR0_L(inst), addr0);
	_reg_write(reg_base + REG_SCL_ODMA_ADDR0_H(inst), addr0 >> 32);
	_reg_write(reg_base + REG_SCL_ODMA_ADDR1_L(inst), addr1);
	_reg_write(reg_base + REG_SCL_ODMA_ADDR1_H(inst), addr1 >> 32);
	_reg_write(reg_base + REG_SCL_ODMA_ADDR2_L(inst), addr2);
	_reg_write(reg_base + REG_SCL_ODMA_ADDR2_H(inst), addr2 >> 32);

	g_odma_cfg[inst].mem.addr0 = addr0;
	g_odma_cfg[inst].mem.addr1 = addr1;
	g_odma_cfg[inst].mem.addr2 = addr2;
}

union sclr_odma_dbg_status sclr_odma_get_dbg_status(u8 inst)
{
	union sclr_odma_dbg_status status;

	status.raw = _reg_read(reg_base + REG_SCL_ODMA_DBG(inst));

	return status;
}

/**
 * sclr_set_csc_ctrl - configure scl's output CSC's, including hsv/quant/coefficient/offset
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @param cfg: The settings for CSC
 */
void sclr_set_csc_ctrl(u8 inst, struct sclr_csc_cfg *cfg)
{
	u32 tmp = 0;

	if(cfg->datatype != SCL_DISABLE){
		// tmp |= BIT(11) | BIT(1) | BIT(0);
		// sclr_set_csc(inst, &csc_mtrx[SCL_CSC_DATATYPE]);
		tmp |= BIT(1);
	}

	if (cfg->mode == SCL_OUT_HSV) {
		tmp |= BIT(4);
		if(cfg->datatype != SCL_DISABLE){
			uartlog("hsv can not enable in fp32/fp16/bf16/int8\n");
		}
		if (cfg->hsv_round == SCL_HSV_ROUNDING_TOWARD_ZERO)
			tmp |= BIT(5);
		if (!cfg->work_on_border)
			tmp |= BIT(9);
		sclr_set_csc(inst, &csc_mtrx[SCL_CSC_NONE]);
	} else if (cfg->mode == SCL_OUT_QUANT) {
		tmp |= (cfg->quant_round << 2) | BIT(1) | BIT(0);
		if (!cfg->work_on_border)
			tmp |= BIT(8);
		if (cfg->quant_gain_mode == SCL_QUANT_GAIN_MODE_2)
			tmp |= BIT(11);
		if(cfg->datatype == SCL_OUT_BF16)
			if (cfg->quant_border_type == SCL_QUANT_BORDER_TYPE_127)
				tmp |= BIT(10);
		sclr_set_quant(inst, &cfg->quant_form);
	} else if (cfg->mode == SCL_OUT_CONVERT_TO) {
		tmp |= BIT(11) | BIT(1) | BIT(0);
		sclr_set_csc(inst, &csc_mtrx[SCL_CSC_DATATYPE]);
		sclr_set_convert_to(inst, &cfg->convert_to_cfg);
	} else if (cfg->mode == SCL_OUT_CSC){
		tmp |= BIT(0);
		if (!cfg->work_on_border)
			tmp |= BIT(8);
		sclr_set_csc(inst, &csc_mtrx[cfg->csc_type]);
	} else
		if (!cfg->work_on_border)
			tmp |= BIT(8);

	_reg_write_mask(reg_base + REG_SCL_CSC_EN(inst), 0x00000fff, tmp);

	if (cfg->datatype == SCL_OUT_BF16) {
		_reg_write_mask(reg_base + REG_SCL_ODMA_CFG(inst), 0x0F800000, BIT(23));
	} else if (cfg->datatype == SCL_OUT_FP16) {
		_reg_write_mask(reg_base + REG_SCL_ODMA_CFG(inst), 0x0F800000, BIT(24));
	} else if (cfg->datatype == SCL_OUT_FP32) {
		_reg_write_mask(reg_base + REG_SCL_ODMA_CFG(inst), 0x0F800000, BIT(25));
	} else if (cfg->datatype == SCL_OUT_INT8) {
		_reg_write_mask(reg_base + REG_SCL_ODMA_CFG(inst), 0x0F800000, BIT(26));
	} else if (cfg->datatype == SCL_OUT_U8) {
		_reg_write_mask(reg_base + REG_SCL_ODMA_CFG(inst), 0x0F800000, BIT(27));
	}

	g_odma_cfg[inst].csc_cfg = *cfg;
}

struct sclr_csc_cfg *sclr_get_csc_ctrl(u8 inst)
{
	if (inst < SCL_MAX_INST)
		return &g_odma_cfg[inst].csc_cfg;

	return NULL;
}

/**
 * sclr_set_csc - configure scl's output CSC's coefficient/offset
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @param cfg: The settings for CSC
 */
void sclr_set_csc(u8 inst, struct sclr_csc_matrix *cfg)
{
	_reg_write(reg_base + REG_SCL_CSC_COEF0(inst),
		   (cfg->coef[0][1] << 16) | cfg->coef[0][0]);
	_reg_write(reg_base + REG_SCL_CSC_COEF1(inst),
		   (cfg->coef[1][0] << 16) | cfg->coef[0][2]);
	_reg_write(reg_base + REG_SCL_CSC_COEF2(inst),
		   (cfg->coef[1][2] << 16) | cfg->coef[1][1]);
	_reg_write(reg_base + REG_SCL_CSC_COEF3(inst),
		   (cfg->coef[2][1] << 16) | cfg->coef[2][0]);
	_reg_write(reg_base + REG_SCL_CSC_COEF4(inst), cfg->coef[2][2]);

	_reg_write(reg_base + REG_SCL_CSC_OFFSET(inst),
		   (cfg->add[2] << 16) | (cfg->add[1] << 8) | cfg->add[0]);
	_reg_write(reg_base + REG_SCL_CSC_FRAC0(inst), 0);
	_reg_write(reg_base + REG_SCL_CSC_FRAC1(inst), 0);
}

/**
 * sclr_set_quant - set quantization by csc module
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @param cfg: The settings of quantization
 */
void sclr_set_quant(u8 inst, struct sclr_quant_formula *cfg)
{
	_reg_write(reg_base + REG_SCL_CSC_COEF0(inst), cfg->sc_frac[0]);
	_reg_write(reg_base + REG_SCL_CSC_COEF1(inst), 0);
	_reg_write(reg_base + REG_SCL_CSC_COEF2(inst), cfg->sc_frac[1]);
	_reg_write(reg_base + REG_SCL_CSC_COEF3(inst), 0);
	_reg_write(reg_base + REG_SCL_CSC_COEF4(inst), cfg->sc_frac[2]);

	_reg_write(reg_base + REG_SCL_CSC_OFFSET(inst), (cfg->sub[2] << 16) | (cfg->sub[1] << 8) | cfg->sub[0]);
	_reg_write(reg_base + REG_SCL_CSC_FRAC0(inst), (cfg->sub_frac[1] << 16) | cfg->sub_frac[0]);
	_reg_write(reg_base + REG_SCL_CSC_FRAC1(inst), cfg->sub_frac[2]);
}

void sclr_set_convert_to(u8 inst, struct sclr_convertto_formula *cfg)
{
	_reg_write_mask(reg_base + REG_SCL_ODMA_CFG(inst), BIT(28), BIT(28));
	_reg_write(reg_base + REG_SCL_CONVERT_TO_A0(inst), cfg->a_frac[0]);
	_reg_write(reg_base + REG_SCL_CONVERT_TO_A1(inst), cfg->a_frac[1]);
	_reg_write(reg_base + REG_SCL_CONVERT_TO_A2(inst), cfg->a_frac[2]);
	_reg_write(reg_base + REG_SCL_CONVERT_TO_B0(inst), cfg->b_frac[0]);
	_reg_write(reg_base + REG_SCL_CONVERT_TO_B1(inst), cfg->b_frac[1]);
	_reg_write(reg_base + REG_SCL_CONVERT_TO_B2(inst), cfg->b_frac[2]);
	g_odma_cfg[inst].csc_cfg.convert_to_cfg = *cfg;
}

/**
 * sclr_get_csc - get current CSC's coefficient/offset
 *
 * @param inst: (0~3), the instance of scaler which want to be configured.
 * @param cfg: The settings of CSC
 */
void sclr_get_csc(u8 inst, struct sclr_csc_matrix *cfg)
{
	u32 tmp;

	tmp = _reg_read(reg_base + REG_SCL_CSC_COEF0(inst));
	cfg->coef[0][0] = tmp;
	cfg->coef[0][1] = tmp >> 16;
	tmp = _reg_read(reg_base + REG_SCL_CSC_COEF1(inst));
	cfg->coef[0][2] = tmp;
	cfg->coef[1][0] = tmp >> 16;
	tmp = _reg_read(reg_base + REG_SCL_CSC_COEF2(inst));
	cfg->coef[1][1] = tmp;
	cfg->coef[1][2] = tmp >> 16;
	tmp = _reg_read(reg_base + REG_SCL_CSC_COEF3(inst));
	cfg->coef[2][0] = tmp;
	cfg->coef[2][1] = tmp >> 16;
	tmp = _reg_read(reg_base + REG_SCL_CSC_COEF4(inst));
	cfg->coef[2][2] = tmp;

	tmp = _reg_read(reg_base + REG_SCL_CSC_OFFSET(inst));
	cfg->add[0] = tmp;
	cfg->add[1] = tmp >> 8;
	cfg->add[2] = tmp >> 16;
	memset(cfg->sub, 0, sizeof(cfg->sub));
}

void sclr_core_set_cfg(u8 inst, struct sclr_core_cfg *cfg)
{
	sclr_ctrl_set_scale(inst, &cfg->sc);
	sclr_init(inst, cfg->sc.algorithm);
}

void sclr_core_checksum_en(u8 inst, bool enable)
{
	_reg_write_mask(reg_base + REG_SCL_CHECKSUM0(inst), BIT(31),
					enable ? BIT(31) : 0);
}

void sclr_core_get_checksum_status(u8 inst, struct sclr_core_checksum_status *status)
{
	status->checksum_base.raw = _reg_read(reg_base + REG_SCL_CHECKSUM0(inst));
	status->axi_read_gop0_data = _reg_read(reg_base + REG_SCL_CHECKSUM1(inst));
	status->axi_read_gop1_data = _reg_read(reg_base + REG_SCL_CHECKSUM2(inst));
	status->axi_write_data = _reg_read(reg_base + REG_SCL_CHECKSUM3(inst));
}

/****************************************************************************
 * SCALER GOP
 ****************************************************************************/
/**
 * sclr_gop_set_cfg - configure gop
 *
 * @param inst: (0~3), the instance of gop which want to be configured.
 *		0~3 is on scl, 4 is on disp.
 * @param layer: (0~1) 0 is layer 0(gop0). 1 is layer 1(gop1).
 * @param cfg: gop's settings
 */
void sclr_gop_set_cfg(u8 inst, u8 layer, struct sclr_gop_cfg *cfg)
{
	if (inst < SCL_MAX_INST) {
		if (layer == 0) {
			_reg_write(reg_base + REG_SCL_GOP0_CFG(inst), cfg->gop_ctrl.raw);
			_reg_write(reg_base + REG_SCL_GOP0_FONTCOLOR(inst),
					   (cfg->font_fg_color << 16) | cfg->font_bg_color);
			_reg_write(reg_base + REG_SCL_GOP0_FONTCOLOR_INV(inst),
					   (cfg->font_fg_inv_color << 16) | cfg->font_bg_inv_color);
			if (cfg->gop_ctrl.b.colorkey_en)
			_reg_write(reg_base + REG_SCL_GOP0_COLORKEY(inst),
					   cfg->colorkey);
			_reg_write(reg_base + REG_SCL_GOP0_FONTBOX_CTRL(inst), cfg->fb_ctrl.raw);

		} else if (layer == 1) {
			_reg_write(reg_base + REG_SCL_GOP1_CFG(inst), cfg->gop_ctrl.raw);
			_reg_write(reg_base + REG_SCL_GOP1_FONTCOLOR(inst),
					   (cfg->font_fg_color << 16) | cfg->font_bg_color);
			_reg_write(reg_base + REG_SCL_GOP1_FONTCOLOR_INV(inst),
					   (cfg->font_fg_inv_color << 16) | cfg->font_bg_inv_color);
			if (cfg->gop_ctrl.b.colorkey_en)
				_reg_write(reg_base + REG_SCL_GOP1_COLORKEY(inst), cfg->colorkey);
			_reg_write(reg_base + REG_SCL_GOP1_FONTBOX_CTRL(inst), cfg->fb_ctrl.raw);

		} else
			uartlog("[bm-vip][sc] %s: only 0 or 1 layer, no such layer(%d). ", __func__, layer);

		g_gop_cfg[inst][layer] = *cfg;
	}
}

/**
 * sclr_gop_get_cfg - get gop's configurations.
 *
 * @param inst: (0~3), the instance of gop which want to be configured.
 *		0~3 is on scl, 4 is on disp.
 * @param layer: (0~1) 0 is layer 0(gop0). 1 is layer 1(gop1).
 */
struct sclr_gop_cfg *sclr_gop_get_cfg(u8 inst, u8 layer)
{
	if (inst < SCL_MAX_INST && layer < SCL_MAX_GOP_INST)
		return &g_gop_cfg[inst][layer];

	return NULL;
}

/**
 * sclr_gop_setup 256LUT - setup gop's Look-up table
 *
 * @param inst: (0~3), the instance of gop which want to be configured.
 *		0~3 is on scl, 4 is on disp.
 * @param layer: (0~1) 0 is layer 0(gop0). 1 is layer 1(gop1).
 * @param length: update 256LUT-table for index 0 ~ length.
 *		There should be smaller than 256 instances.
 * @param data: values of 256LUT-table. There should be 256 instances.
 */
int sclr_gop_setup_256LUT(u8 inst, u8 layer, u8 length, u16 *data)
{
	u16 i = 0;

	if (length > 255)
		return -1;

	if (inst < SCL_MAX_INST) {
		uartlog("[bm-vip][sc] update 256LUT in gop%d of sc_%d. Length is %d.\n", layer, inst, length);
		if (layer == 0) {
			for (i = 0; i < length; ++i) {
				_reg_write(reg_base + REG_SCL_GOP0_256LUT0(inst),
						   (i << 16) | *(data + i));
				_reg_write(reg_base + REG_SCL_GOP0_256LUT1(inst), ~BIT(16));
				_reg_write(reg_base + REG_SCL_GOP0_256LUT1(inst), BIT(16));
			}
		} else if (layer == 1) {
			for (i = 0; i < length; ++i) {
				_reg_write(reg_base + REG_SCL_GOP1_256LUT0(inst),
						   (i << 16) | *(data + i));
				_reg_write(reg_base + REG_SCL_GOP1_256LUT1(inst), ~BIT(16));
				_reg_write(reg_base + REG_SCL_GOP1_256LUT1(inst), BIT(16));
			}
		} else {
			uartlog("[bm-vip][sc] %s: only 0 or 1 layer, no such layer(%d). ", __func__, layer);
		}
	}

	return 0;
}

/**
 * sclr_gop_update_256LUT - update gop's Look-up table by index.
 *
 * @param inst: (0~3), the instance of gop which want to be configured.
 *		0~3 is on scl, 4 is on disp.
 * @param layer: (0~1) 0 is layer 0(gop0). 1 is layer 1(gop1).
 * @param index: start address of 256LUT-table. There should be 256 instances.
 * @param data: value of 256LUT-table.
 */
int sclr_gop_update_256LUT(u8 inst, u8 layer, u8 index, u16 data)
{
	if (index > 255)
		return -1;

	if (inst < SCL_MAX_INST) {
		uartlog("[bm-vip][sc] update 256LUT in gop%d of sc_%d. Index is %d.\n", layer, inst, index);
		if (layer == 0) {
			_reg_write(reg_base + REG_SCL_GOP0_256LUT0(inst),
					   (index << 16) | data);
			_reg_write(reg_base + REG_SCL_GOP0_256LUT1(inst), ~BIT(16));
			_reg_write(reg_base + REG_SCL_GOP0_256LUT1(inst), BIT(16));
		} else if (layer == 1) {
			_reg_write(reg_base + REG_SCL_GOP1_256LUT0(inst),
					   (index << 16) | data);
			_reg_write(reg_base + REG_SCL_GOP1_256LUT1(inst), ~BIT(16));
			_reg_write(reg_base + REG_SCL_GOP1_256LUT1(inst), BIT(16));
		} else
			uartlog("[bm-vip][sc] %s: only 0 or 1 layer, no such layer(%d). ", __func__, layer);
	}

	return 0;
}

/**
 * sclr_gop_setup 16LUT - setup gop's Look-up table
 *
 * @param inst: (0~3), the instance of gop which want to be configured.
 *		0~3 is on scl, 4 is on disp.
 * @param layer: (0~1) 0 is layer 0(gop0). 1 is layer 1(gop1).
 * @param length: update 16LUT-table for index 0 ~ length.
 *		There should be smaller than 16 instances.
 * @param data: values of 16LUT-table. There should be 16 instances.
 */
int sclr_gop_setup_16LUT(u8 inst, u8 layer, u8 length, u16 *data)
{
	u16 i = 0;
	if (length > 15)
		return -1;

	if (inst < SCL_MAX_INST) {
		uartlog("[bm-vip][sc] update 16LUT in gop%d of sc_%d. Length is %d.\n", layer, inst, length);
		if (layer == 0) {
			for (i = 0; i < length; i += 2 ) {
				_reg_write(reg_base + REG_SCL_GOP0_16LUT(inst, i),
						   ((*(data + i + 1) << 16) | (*(data + i))));
			}
		} else if (layer == 1) {
			for (i = 0; i <= length; i += 2) {
				_reg_write(reg_base + REG_SCL_GOP1_16LUT(inst, i),
						   ((*(data + i + 1) << 16) | (*(data + i))));
			}
		} else
			uartlog("[bm-vip][sc] %s: only 0 or 1 layer, no such layer(%d). ", __func__, layer);
	}

	return 0;
}

/**
 * sclr_gop_update_16LUT - update gop's Look-up table by index.
 *
 * @param inst: (0~3), the instance of gop which want to be configured.
 *		0~3 is on scl, 4 is on disp.
 * @param layer: (0~1) 0 is layer 0(gop0). 1 is layer 1(gop1).
 * @param index: start address of 16LUT-table. There should be 16 instances.
 * @param data: value of 16LUT-table.
 */
int sclr_gop_update_16LUT(u8 inst, u8 layer, u8 index, u16 data)
{
	u16 tmp;
	if (index > 15)
		return -1;

	if (inst < SCL_MAX_INST) {
		uartlog("[bm-vip][sc] update 16LUT in gop%d of sc_%d. Index is %d.\n", layer, inst, index);
		if (layer == 0) {
			if (index % 2 == 0) {
				tmp = _reg_read(reg_base + REG_SCL_GOP0_16LUT(inst, index + 1));
				_reg_write(reg_base + REG_SCL_GOP0_16LUT(inst, index),
						   ((tmp << 16) | data));
			} else {
				tmp = _reg_read(reg_base + REG_SCL_GOP0_16LUT(inst, index - 1));
				_reg_write(reg_base + REG_SCL_GOP0_16LUT(inst, index - 1),
						   ((data << 16) | tmp));
			}
		} else if (layer == 1) {
			if (index % 2 == 0) {
				tmp = _reg_read(reg_base + REG_SCL_GOP1_16LUT(inst, index + 1));
				_reg_write(reg_base + REG_SCL_GOP1_16LUT(inst, index),
						   ((tmp << 16) | data));
			} else {
				tmp = _reg_read(reg_base + REG_SCL_GOP1_16LUT(inst, index - 1));
				_reg_write(reg_base + REG_SCL_GOP1_16LUT(inst, index - 1),
						   ((data << 16) | tmp));
			}
		} else
			uartlog("[bm-vip][sc] %s: only 0 or 1 layer, no such layer(%d). ", __func__, layer);
	}

	return 0;
}

/**
 * sclr_gop_ow_set_cfg - set gop's osd-window configurations.
 *
 * @param inst: (0~3), the instance of gop which want to be configured.
 *		0~3 is on scl, 4 is on disp.
 * @param layer: (0~1) 0 is layer 0(gop0). 1 is layer 1(gop1).
 * @param ow_inst: (0~7), the instance of ow which want to be configured.
 * @param cfg: ow's settings.
 */
void sclr_gop_ow_set_cfg(u8 inst, u8 layer, u8 ow_inst, struct sclr_gop_ow_cfg *ow_cfg)
{
	//OW Format
	//4'b0000: ARGB8888
	//4'b0100: ARGB4444
	//4'b0101: ARGB1555
	//4'b1000: 256LUT-ARGB4444
	//4'b1010: 16-LUT-ARGB4444
	//4'b1100: Font-base"
	static const u8 reg_map_fmt[SCL_GOP_FMT_MAX] = {0, 0x4, 0x5, 0x8, 0xa, 0xc};

	if (ow_inst >= SCL_MAX_GOP_OW_INST)
		return;

	if (inst < SCL_MAX_INST) {
		if (layer == 0) {
			_reg_write(reg_base + REG_SCL_GOP0_FMT(inst, ow_inst),
					   reg_map_fmt[ow_cfg->fmt]);
			_reg_write(reg_base + REG_SCL_GOP0_H_RANGE(inst, ow_inst),
					   (ow_cfg->end.x << 16) | ow_cfg->start.x);
			_reg_write(reg_base + REG_SCL_GOP0_V_RANGE(inst, ow_inst),
					   (ow_cfg->end.y << 16) | ow_cfg->start.y);
			_reg_write(reg_base + REG_SCL_GOP0_ADDR_L(inst, ow_inst),
					   ow_cfg->addr);
			_reg_write(reg_base + REG_SCL_GOP0_ADDR_H(inst, ow_inst),
					   ow_cfg->addr >> 32);
			_reg_write(reg_base + REG_SCL_GOP0_CROP_PITCH(inst, ow_inst),
					   (ow_cfg->crop_pixels << 16) | ow_cfg->pitch);
			_reg_write(reg_base + REG_SCL_GOP0_SIZE(inst, ow_inst),
					   (ow_cfg->mem_size.h << 16) | ow_cfg->mem_size.w);
			g_gop_cfg[inst][layer].ow_cfg[ow_inst] = *ow_cfg;
		} else if (layer == 1) {
			_reg_write(reg_base + REG_SCL_GOP1_FMT(inst, ow_inst),
					   reg_map_fmt[ow_cfg->fmt]);
			_reg_write(reg_base + REG_SCL_GOP1_H_RANGE(inst, ow_inst),
					   (ow_cfg->end.x << 16) | ow_cfg->start.x);
			_reg_write(reg_base + REG_SCL_GOP1_V_RANGE(inst, ow_inst),
					   (ow_cfg->end.y << 16) | ow_cfg->start.y);
			_reg_write(reg_base + REG_SCL_GOP1_ADDR_L(inst, ow_inst),
					   ow_cfg->addr);
			_reg_write(reg_base + REG_SCL_GOP1_ADDR_H(inst, ow_inst),
					   ow_cfg->addr >> 32);
			_reg_write(reg_base + REG_SCL_GOP1_CROP_PITCH(inst, ow_inst),
					   (ow_cfg->crop_pixels << 16) | ow_cfg->pitch);
			_reg_write(reg_base + REG_SCL_GOP1_SIZE(inst, ow_inst),
					   (ow_cfg->mem_size.h << 16) | ow_cfg->mem_size.w);
			g_gop_cfg[inst][layer].ow_cfg[ow_inst] = *ow_cfg;
		} else {
			uartlog("[bm-vip][sc] %s: only 0 or 1 layer, no such layer(%d). ", __func__, layer);

		}
	}
}

/*
 * sclr_gop_odec_set_cfg_from_oenc references struct oenc_cfg / oenc_get_cfg()
 * from oenc.h, which is part of the GOP/VGOP feature that is not needed for
 * the disp-online path and is not ported to the kernel build. Disable it.
 */
#if 0
void sclr_gop_odec_set_cfg_from_oenc(u8 inst, u8 oenc_inst, u8 layer, struct sclr_gop_odec_cfg *odec_cfg, u8 oenc_num)
{
#ifndef HDMI_BUILD
	struct oenc_cfg *oenc_cfg = oenc_get_cfg(oenc_inst);

	if (inst < SCL_MAX_INST) {
		if (layer == 0) {
			if (oenc_cfg[oenc_inst].bso_sz != 0) {

				_reg_write(reg_base + REG_SCL_GOP0_DEC_CTRL(inst), odec_cfg->odec_ctrl.raw);
				// _reg_write(reg_base + REG_SCL_GOP0_ADDR_L(inst, odec_cfg->odec_ctrl.b.odec_attached_idx),
				//            oenc_cfg[inst].bso_adr);
				// _reg_write(reg_base + REG_SCL_GOP0_ADDR_H(inst, odec_cfg->odec_ctrl.b.odec_attached_idx),
				//            0x1);
				// _reg_write(reg_base + REG_SCL_GOP0_SIZE(inst, odec_cfg->odec_ctrl.b.odec_attached_idx),
				//            (oenc_cfg[inst].bso_mem_size.h << 16) | oenc_cfg[inst].bso_mem_size.w);
				for(int i = 0; i< oenc_num; i++){
						_reg_write(REG_SCL_GOP0_ADDR_L(inst, i),
						oenc_cfg[oenc_inst].bso_adr);
					_reg_write(REG_SCL_GOP0_ADDR_H(inst, i), 0x10);
					_reg_write(REG_SCL_GOP0_SIZE(inst, i),
						(oenc_cfg[oenc_inst].bso_mem_size.h << 16) | oenc_cfg[oenc_inst].bso_mem_size.w);
				}
				printf("oenc_cfg[inst].bso_mem_size.h:%x\n",oenc_cfg[oenc_inst].bso_mem_size.h);
				printf("oenc_cfg[inst].bso_mem_size.w:%x\n",oenc_cfg[oenc_inst].bso_mem_size.w);
				printf("oenc_cfg[inst].bso_mem_size.h << 16 | oenc_cfg[inst].bso_mem_size.w:%x\n",
					(oenc_cfg[oenc_inst].bso_mem_size.h << 16) | oenc_cfg[oenc_inst].bso_mem_size.w);
			}
		} else if (layer == 1) {
			if (oenc_cfg[oenc_inst].bso_sz != 0) {
				_reg_write(reg_base + REG_SCL_GOP1_DEC_CTRL(inst), odec_cfg->odec_ctrl.raw);
				// _reg_write(reg_base + REG_SCL_GOP1_ADDR_L(inst, odec_cfg->odec_ctrl.b.odec_attached_idx),
				//            oenc_cfg->bso_adr);
				// _reg_write(reg_base + REG_SCL_GOP1_ADDR_H(inst, odec_cfg->odec_ctrl.b.odec_attached_idx),
				//            0x1);
				// _reg_write(reg_base + REG_SCL_GOP1_SIZE(inst, odec_cfg->odec_ctrl.b.odec_attached_idx),
				//            (oenc_cfg->bso_mem_size.h << 16) | oenc_cfg->bso_mem_size.w);
				for(int i = 0; i<oenc_num; i++){
						_reg_write(REG_SCL_GOP1_ADDR_L(inst, i),
						oenc_cfg[oenc_inst].bso_adr);
					_reg_write(REG_SCL_GOP1_ADDR_H(inst, i), 0x1);
					_reg_write(REG_SCL_GOP1_SIZE(inst, i),
						(oenc_cfg[oenc_inst].bso_mem_size.h << 16) | oenc_cfg[oenc_inst].bso_mem_size.w);
				}
			}
		} else {
			uartlog("[bm-vip][sc] %s: only 0 or 1 layer, no such layer(%d). ", __func__, layer);
		}
	}
#endif
}
#endif /* sclr_gop_odec_set_cfg_from_oenc (oenc / GOP path disabled) */

/**
 * sclr_gop_fb_set_cfg - setup fontbox
 *
 * @param inst: (0~3), the instance of gop which want to be configured.
 *		0~3 is on scl, 4 is on disp.
 * @param layer: (0~1) 0 is layer 0(gop0). 1 is layer 1(gop1).
 * @param fb_inst: (0~1), the instance of ow which want to be configured.
 * @param cfg: fontbox configuration
 */
void sclr_gop_fb_set_cfg(u8 inst, u8 layer, u8 fb_inst, struct sclr_gop_fb_cfg *fb_cfg)
{
	if (fb_inst >= SCL_MAX_GOP_FB_INST)
		return;

	if (fb_cfg->fb_ctrl.b.sample_rate > 3)
		fb_cfg->fb_ctrl.b.sample_rate = 3;
	if (fb_cfg->fb_ctrl.b.pix_thr > 31)
		fb_cfg->fb_ctrl.b.pix_thr = 31;

	if (inst < SCL_MAX_INST) {
		if (layer == 0) {
			_reg_write(reg_base + REG_SCL_GOP0_FONTBOX_CFG(inst, fb_inst), fb_cfg->fb_ctrl.raw);
			_reg_write(reg_base + REG_SCL_GOP0_FONTBOX_INIT(inst, fb_inst), fb_cfg->init_st);
		} else if (layer == 1) {
			_reg_write(reg_base + REG_SCL_GOP1_FONTBOX_CFG(inst, fb_inst), fb_cfg->fb_ctrl.raw);
			_reg_write(reg_base + REG_SCL_GOP1_FONTBOX_INIT(inst, fb_inst), fb_cfg->init_st);
		} else
			uartlog("[bm-vip][sc] %s: only 0 or 1 layer, no such layer(%d). ", __func__, layer);
	}

	return;
}

/**
 * sclr_gop_fb_get_record - get fontbox's record
 *
 * @param inst: (0~3), the instance of gop which want to be configured.
 *		0~3 is on scl, 4 is on disp.
 * @param layer: (0~1) 0 is layer 0(gop0). 1 is layer 1(gop1).
 * @param fb_inst: (0~1), the instance of ow which want to be configured.
 * @return: fontbox's record
 */
u32 sclr_gop_fb_get_record(u8 inst, u8 layer, u8 fb_inst)
{
	if (inst < SCL_MAX_INST) {
		if (layer == 0)
			return _reg_read(reg_base + REG_SCL_GOP0_FONTBOX_REC(inst, fb_inst));
		else if (layer == 1)
			return _reg_read(reg_base + REG_SCL_GOP1_FONTBOX_REC(inst, fb_inst));
		else {
			uartlog("[bm-vip][sc] %s: only 0 or 1 layer, no such layer(%d). ", __func__, layer);
			return -1;
		}
	} else
		return -1;
}

/**
 * sclr_cover_set_cfg - setup cover
 *
 * @param inst: (0~3), the instance of gop which want to be configured.
 *		0~3 is on scl, 4 is on disp.
 * @param cover_cfg: cover_cfg configuration
 */
void sclr_cover_set_cfg(u8 inst, u8 cover_w_inst, struct sclr_cover_cfg *cover_cfg)
{
	if (inst < SCL_MAX_INST) {
		_reg_write(reg_base + REG_SCL_COVER_CFG(inst, cover_w_inst), cover_cfg->start.raw);
		_reg_write(reg_base + REG_SCL_COVER_SIZE(inst, cover_w_inst),
				   (cover_cfg->img_size.h << 16) | cover_cfg->img_size.w);
		_reg_write(reg_base + REG_SCL_COVER_COLOR(inst, cover_w_inst), cover_cfg->color.raw);
	}
}

/****************************************************************************
 * SCALER PRICAY MASK
 ****************************************************************************/
/**
 * sclr_pri_set_cfg - configure privacy mask
 *
 * @param inst: (0~3), the instance of gop which want to be configured.
 *		0~3 is on scl, 4 is on disp.
 * @param cfg: privacy mask's settings
 */
void sclr_pri_set_cfg(u8 inst, struct sclr_privacy_cfg *cfg)
{
	u16 grid_line_per_frame, grid_num_per_line;
	u16 w, h;
	u16 pitch;

	if (inst < SCL_MAX_INST) {
		_reg_write(reg_base + REG_SCL_PRI_CFG(inst), cfg->cfg.raw);

		if (cfg->cfg.b.fit_picture) {
			w = g_sc_cfg[inst].sc.dst.w;
			h = g_sc_cfg[inst].sc.dst.h;
		} else {
			_reg_write(reg_base + REG_SCL_PRI_START(inst), (cfg->start.y << 16) | cfg->start.x);
			_reg_write(reg_base + REG_SCL_PRI_END(inst), (cfg->end.y << 16) | cfg->end.x);
			w = cfg->end.x - cfg->start.x + 1;
			h = cfg->end.y - cfg->start.y + 1;
		}

		_reg_write(reg_base + REG_SCL_PRI_ALPHA(inst),
			   (cfg->map_cfg.alpha_factor << 8) | cfg->map_cfg.no_mask_idx);
		_reg_write(reg_base + REG_SCL_PRI_MAP_ADDR_L(inst), cfg->map_cfg.base);
		_reg_write(reg_base + REG_SCL_PRI_MAP_ADDR_H(inst), cfg->map_cfg.base >> 32);

		if (cfg->cfg.b.mode == 0) {
			u8 grid_shift = cfg->cfg.b.grid_size ? 4 : 3;

			grid_num_per_line = UPPER(w, grid_shift);
			grid_line_per_frame = UPPER(h, grid_shift);
			_reg_write(reg_base + REG_SCL_PRI_GRID_CFG(inst),
				   (grid_num_per_line << 16) | (grid_line_per_frame - 1));
			pitch = ALIGN(grid_num_per_line, 16);
		} else {
			_reg_write(reg_base + REG_SCL_PRI_GRID_CFG(inst),
				   (w << 16) | (h - 1));
			pitch = ALIGN(w, 16);
		}

		_reg_write(reg_base + REG_SCL_PRI_MAP_AXI_CFG(inst), (cfg->map_cfg.axi_burst << 16) | pitch);
	}
}

/****************************************************************************
 * SCALER CTRL
 ****************************************************************************/
/**
 * sclr_ctrl_init - setup all sc instances.
 *
 */
void sclr_ctrl_init(u8 inst)
{
	union sclr_intr intr_mask;
	union sclr_rt_cfg rt_cfg;
	unsigned int i;
	// init variables
	memset(&g_sc_cfg[inst], 0, sizeof(g_sc_cfg[0]));
	memset(&g_bd_cfg[inst], 0, sizeof(g_bd_cfg[0]));
	memset(&g_cir_cfg[inst], 0, sizeof(g_cir_cfg[0]));
	memset(&g_img_cfg[inst], 0, sizeof(g_img_cfg[0]));
	memset(&g_odma_cfg[inst], 0, sizeof(g_odma_cfg[0]));
	memset(&g_gop_cfg[inst], 0, sizeof(g_gop_cfg[0]));

	for (i = 0; i < SCL_MAX_GOP_INST; ++i) {
		g_gop_cfg[inst][i].fb_ctrl.b.hi_thr = 0x30;
		g_gop_cfg[inst][i].fb_ctrl.b.lo_thr = 0x20;
		g_gop_cfg[inst][i].fb_ctrl.b.detect_fnum = 1;
		g_gop_cfg[inst][i].fb_ctrl.b.hi_thr = 0x30;
		g_gop_cfg[inst][i].fb_ctrl.b.lo_thr = 0x20;
		g_gop_cfg[inst][i].fb_ctrl.b.detect_fnum = 1;
	}
	g_odma_cfg[inst].flip = SCL_FLIP_NO;
	g_odma_cfg[inst].burst = false;
	g_img_cfg[inst].burst = 7;
	g_img_cfg[inst].src = SCL_INPUT_MEM;

	sclr_img_reg_shadow_sel(inst, false);

	rt_cfg.b.sc_d_rt = 0;
	rt_cfg.b.sc_v_rt = 0;
	rt_cfg.b.sc_rot_rt = 0;
	rt_cfg.b.img_d_rt = 0;
	rt_cfg.b.img_v_rt = 0;
	rt_cfg.b.img_d_sel = 1;

	intr_mask.raw = 0;
	//intr_mask.b.img_in_frame_end = true;
	intr_mask.b.scl_frame_end = true;
	//intr_mask.b.prog_too_late = true;
	intr_mask.b.cmdq = true;
	//intr_mask.b.scl_line_target_hit = true;

	sclr_rt_set_cfg(inst, rt_cfg);
	sclr_set_intr_mask(inst, intr_mask);
	sclr_reg_shadow_sel(inst, false);
	// sclr_init(inst);
	sclr_set_cfg(inst, false, true, true, false);
	// sclr_reg_force_up(inst);

	sclr_top_reg_done(inst);
	sclr_top_reg_force_up(inst);
	sclr_top_pg_late_clr(inst);
}

static void _tile_cal_size(u8 inst, struct sclr_scale_cfg *cfg)
{
	struct sclr_size in_size = cfg->src;
	struct sclr_size out_size = cfg->dst;
	u8 fix = (cfg->algorithm == SCL_BICUBIC) ? 13 : 23;
	u32 out_l_width = (out_size.w >> 1);
	u32 h_sc_fac = g_sc_cfg[inst].sc.fac.h_fac;
	u32 h_pos = g_sc_cfg[inst].sc.fac.h_pos;
	if(cfg->algorithm != SCL_BICUBIC && cfg->algorithm != SCL_NEAREST && (in_size.w < out_size.w))
		h_pos = (((1 << 23) - h_sc_fac) >> 1);
	else if (cfg->algorithm == SCL_NEAREST)
		h_pos = 1 << 22;
	u64 L_last_phase, R_first_phase;
	u16 L_last_pixel, R_first_pixel;

	L_last_phase = (u64)out_l_width * h_sc_fac;
	L_last_pixel = (L_last_phase >> fix) + ((cfg->mir_enable) ? 0 : 1);
	cfg->tile.src_l_width = L_last_pixel + 1
				+ (L_last_phase ? 1 : 0);
	cfg->tile.out_l_width = out_l_width;

	// right tile no mirror
	R_first_phase = L_last_phase;
	R_first_pixel = (R_first_phase >> fix) + ((cfg->mir_enable) ? 0 : 1);
	cfg->tile.r_ini_phase = R_first_phase - ((R_first_pixel - 2) << fix) + h_pos * ((in_size.w < out_size.w || cfg->algorithm == SCL_NEAREST) ? -1 : 1);
	cfg->tile.src_r_offset = R_first_pixel - 2;
	cfg->tile.src_r_width = in_size.w - cfg->tile.src_r_offset;

	// TODO: crop?
	cfg->tile.src = in_size;
	cfg->tile.out = out_size;
	g_sc_cfg[inst].sc.tile = cfg->tile;

	uartlog("h_sc_fac:%d\n", h_sc_fac);
	uartlog("L last phase(%ld) last pixel(%d)\n", L_last_phase, L_last_pixel);
	uartlog("R first phase(%ld) first pixel(%d)\n", R_first_phase, R_first_pixel);
	uartlog("tile cfg: Left width src(%d) out(%d)\n", cfg->tile.src_l_width, cfg->tile.out_l_width);
	uartlog("tile cfg: Right src offset(%d) src(%d) phase(%d)\n", cfg->tile.src_r_offset, cfg->tile.src_r_width, cfg->tile.r_ini_phase);
}

void _v_tile_cal_size(u8 inst, struct sclr_scale_cfg *cfg)
{
	struct sclr_size in_size = cfg->src;
	struct sclr_size out_size = cfg->dst;
	u8 fix = (cfg->algorithm == SCL_BICUBIC) ? 13 : 23;
	u32 out_l_width = (out_size.h >> 1);
	u32 v_sc_fac = g_sc_cfg[inst].sc.fac.v_fac;
	u32 v_pos = g_sc_cfg[inst].sc.fac.v_pos;
	if(cfg->algorithm != SCL_BICUBIC && cfg->algorithm != SCL_NEAREST && (in_size.h < out_size.h))
		v_pos = (((1 << 23) - v_sc_fac) >> 1);
	else if (cfg->algorithm == SCL_NEAREST)
		v_pos = 1 << 22;
	u64 L_last_phase, R_first_phase;
	u16 L_last_pixel, R_first_pixel;

	L_last_phase = (u64)out_l_width * v_sc_fac;
	L_last_pixel = (L_last_phase >> fix);
	cfg->v_tile.src_l_width = L_last_pixel + 1
				+ (L_last_phase ? 1 : 0);
	cfg->v_tile.out_l_width = out_l_width;

	// right tile no mirror
	R_first_phase = L_last_phase;
	R_first_pixel = (R_first_phase >> fix);
	cfg->v_tile.r_ini_phase = R_first_phase - ((R_first_pixel - 2) << fix) + v_pos * ((in_size.h < out_size.h || cfg->algorithm == SCL_NEAREST) ? -1 : 1);
	cfg->v_tile.src_r_offset = R_first_pixel - 2;
	cfg->v_tile.src_r_width = in_size.h - cfg->v_tile.src_r_offset;

	// TODO: crop?
	cfg->v_tile.src = in_size;
	cfg->v_tile.out = out_size;
	g_sc_cfg[inst].sc.v_tile = cfg->v_tile;

	uartlog("v_sc_fac:%d\n", v_sc_fac);
	uartlog("L last phase(%ld) last pixel(%d)\n", L_last_phase, L_last_pixel);
	uartlog("R first phase(%ld) first pixel(%d)\n", R_first_phase, R_first_pixel);
	uartlog("tile cfg: up height src(%d) out(%d)\n", cfg->v_tile.src_l_width, cfg->v_tile.out_l_width);
	uartlog("tile cfg: down src offset(%d) src(%d) phase(%d)\n", cfg->v_tile.src_r_offset, cfg->v_tile.src_r_width, cfg->v_tile.r_ini_phase);
}

void sclr_left_tile(u8 inst)
{
	struct sclr_scale_cfg *sc;
	struct sclr_odma_cfg *odma_cfg;
	struct sclr_rect crop;
	struct sclr_size dst;

	if (inst >= SCL_MAX_INST) {
		uartlog("[bm-vip][sc] %s: no enough sclr-instance ", __func__);
		uartlog("for the requirement(%d)\n", inst);
		return;
	}
	sc = &(sclr_get_cfg(inst)->sc);
	odma_cfg = sclr_odma_get_cfg(inst);

	crop.x = 0;
	crop.y = 0;
	crop.w = sc->tile.src_l_width;
	crop.h = sc->tile.src.h;
	dst.w = sc->tile.out_l_width;
	dst.h = sc->tile.out.h;

	sclr_set_crop(inst, crop, true);
	sclr_set_output_size(inst, dst);

	odma_cfg->mem.start_x = 0;
	odma_cfg->mem.width = dst.w;
	sclr_odma_set_mem(inst, &odma_cfg->mem);

	uartlog("---vpss(%d) left tile---\n", inst);
	uartlog("crop(%d %d %d %d)\n", crop.x, crop.y, crop.w, crop.h);
	uartlog("dst(%d %d)\n", dst.w, dst.h);
	uartlog("odma(%d %d %d %d)\n", odma_cfg->mem.start_x, odma_cfg->mem.start_y,
		odma_cfg->mem.width, odma_cfg->mem.height);
}

void sclr_top_tile(u8 inst)
{
	struct sclr_scale_cfg *sc;
	struct sclr_odma_cfg *odma_cfg;
	struct sclr_rect crop;
	struct sclr_size dst;

	if (inst >= SCL_MAX_INST) {
		uartlog("[bm-vip][sc] %s: no enough sclr-instance ", __func__);
		uartlog("for the requirement(%d)\n", inst);
		return;
	}
	sc = &(sclr_get_cfg(inst)->sc);
	odma_cfg = sclr_odma_get_cfg(inst);

	crop.x = g_sc_cfg[inst].sc.crop.x;
	crop.y = 0;
	crop.w = g_sc_cfg[inst].sc.crop.w;
	crop.h = sc->v_tile.src_l_width;
	dst.w = g_sc_cfg[inst].sc.dst.w;
	dst.h = sc->v_tile.out_l_width;

	sclr_set_crop(inst, crop, true);
	sclr_set_output_size(inst, dst);

	odma_cfg->mem.start_y = 0;
	odma_cfg->mem.height = dst.h;
	sclr_odma_set_mem(inst, &odma_cfg->mem);

	uartlog("---vpss(%d) top tile---\n", inst);
	uartlog("crop(%d %d %d %d)\n", crop.x, crop.y, crop.w, crop.h);
	uartlog("dst(%d %d)\n", dst.w, dst.h);
	uartlog("odma(%d %d %d %d)\n", odma_cfg->mem.start_x, odma_cfg->mem.start_y,
		odma_cfg->mem.width, odma_cfg->mem.height);
}

void sclr_right_tile(u8 inst)
{
	struct sclr_scale_cfg *sc;
	struct sclr_odma_cfg *odma_cfg;
	struct sclr_rect crop;
	struct sclr_size dst;

	if (inst >= SCL_MAX_INST) {
		uartlog("[bm-vip][sc] %s: no enough sclr-instance ", __func__);
		uartlog("for the requirement(%d)\n", inst);
		return;
	}
	sc = &(sclr_get_cfg(inst)->sc);
	odma_cfg = sclr_odma_get_cfg(inst);

	crop.x = sc->tile.src_r_offset;
	crop.y = 0;
	crop.w = sc->tile.src_r_width;
	crop.h = sc->src.h;
	dst.w = sc->tile.out.w - sc->tile.out_l_width;
	dst.h = sc->tile.out.h;

	sclr_set_crop(inst, crop, true);
	sclr_set_output_size(inst, dst);
	sclr_set_scale_phase(inst, sc->tile.r_ini_phase, sc->fac.v_pos);

	odma_cfg->mem.start_x = sc->tile.out_l_width;
	odma_cfg->mem.width = dst.w;
	sclr_odma_set_mem(inst, &odma_cfg->mem);

	uartlog("---vpss(%d) right tile---\n", inst);
	uartlog("crop(%d %d %d %d)\n", crop.x, crop.y, crop.w, crop.h);
	uartlog("dst(%d %d)\n", dst.w, dst.h);
	uartlog("odma(%d %d %d %d)\n", odma_cfg->mem.start_x, odma_cfg->mem.start_y,
		odma_cfg->mem.width, odma_cfg->mem.height);
}

void sclr_down_tile(u8 inst)
{
	struct sclr_scale_cfg *sc;
	struct sclr_odma_cfg *odma_cfg;
	struct sclr_rect crop;
	struct sclr_size dst;

	if (inst >= SCL_MAX_INST) {
		uartlog("[bm-vip][sc] %s: no enough sclr-instance ", __func__);
		uartlog("for the requirement(%d)\n", inst);
		return;
	}
	sc = &(sclr_get_cfg(inst)->sc);
	odma_cfg = sclr_odma_get_cfg(inst);

	crop.x = g_sc_cfg[inst].sc.crop.x;
	crop.y = sc->v_tile.src_r_offset;
	crop.w = g_sc_cfg[inst].sc.crop.w;
	crop.h = sc->v_tile.src_r_width;
	dst.w = g_sc_cfg[inst].sc.dst.w;
	dst.h = sc->v_tile.out.h - sc->v_tile.out_l_width;

	sclr_set_crop(inst, crop, true);
	sclr_set_output_size(inst, dst);
	if(sc->tile_enable && (crop.x != 0)) //if right
		sclr_set_scale_phase(inst, sc->tile.r_ini_phase, sc->v_tile.r_ini_phase);
	else
		sclr_set_scale_phase(inst, sc->fac.h_pos, sc->v_tile.r_ini_phase);

	odma_cfg->mem.start_y = sc->v_tile.out_l_width;
	odma_cfg->mem.height = dst.h;
	sclr_odma_set_mem(inst, &odma_cfg->mem);

	uartlog("---vpss(%d) down tile---\n", inst);
	uartlog("crop(%d %d %d %d)\n", crop.x, crop.y, crop.w, crop.h);
	uartlog("dst(%d %d)\n", dst.w, dst.h);
	uartlog("odma(%d %d %d %d)\n", odma_cfg->mem.start_x, odma_cfg->mem.start_y,
		odma_cfg->mem.width, odma_cfg->mem.height);
}

/**
 * sclr_ctrl_set_scale - setup scaling
 *
 * @param inst: (0~3), the instance of sc
 * @param cfg: scaling settings, include in/crop/out size.
 */
void sclr_ctrl_set_scale(u8 inst, struct sclr_scale_cfg *cfg)
{
	if (inst >= SCL_MAX_INST) {
		uartlog("[bm-vip][sc] %s: no enough sclr-instance ", __func__);
		uartlog("for the requirement(%d)\n", inst);
		return;
	}
	if (memcmp(cfg, &g_sc_cfg[inst].sc, sizeof(*cfg)) == 0)
		return;

	sclr_set_input_size(inst, cfg->src);

	// if crop invalid, use src-size
	if (cfg->crop.w + cfg->crop.x > cfg->src.w) {
		cfg->crop.x = 0;
		cfg->crop.w = cfg->src.w;
	}
	if (cfg->crop.h + cfg->crop.y > cfg->src.h) {
		cfg->crop.y = 0;
		cfg->crop.h = cfg->src.h;
	}
	sclr_set_crop(inst, cfg->crop, true);
	sclr_set_output_size(inst, cfg->dst);

	//g_sc_cfg[inst].sc.mir_enable = cfg->mir_enable;
	//g_sc_cfg[inst].sc.tile_enable = cfg->tile_enable;
	//g_sc_cfg[inst].sc.tile = cfg->tile;
	g_sc_cfg[inst].sc = *cfg;
	sclr_set_scale(inst);
	cfg->fac = g_sc_cfg[inst].sc.fac;
	if (cfg->tile_enable)
		_tile_cal_size(inst, cfg);
	if (cfg->v_tile_enable)
		_v_tile_cal_size(inst, cfg);
}

/**
 * sclr_ctrl_set_input - setup input
 *
 * @param img_inst: (0~1), the instance of img_in
 * @param input: input mux
 * @param fmt: color format
 * @param csc: csc which used to convert from yuv to rgb.
 * @return: 0 if success
 */
int sclr_ctrl_set_input(u8 inst, enum sclr_input input,
			enum sclr_format fmt, enum sclr_csc csc)
{
	int ret = 0;

	// always transfer to rgb for sc
	if ((inst >= VPSS_MAX) || (csc >= SCL_CSC_601_LIMIT_RGB2YUV))
		return -EINVAL;

	g_img_cfg[inst].src = input;

	g_img_cfg[inst].fmt = fmt;
	sclr_img_set_cfg(inst, &g_img_cfg[inst]);

	g_img_cfg[inst].csc = csc;
	if (csc == SCL_CSC_NONE) {
		sclr_img_csc_en(inst, false);
	} else {
		sclr_img_csc_en(inst, true);
		sclr_img_set_csc(inst, &csc_mtrx[csc]);
	}

	return ret;
}

/**
 * sclr_ctrl_set_output - setup output of sc
 *
 * @param inst: (0~3), the instance of sc
 * @param cfg: csc config, including hvs/quant settings if any
 * @param fmt: color format
 * @return: 0 if success
 */
int sclr_ctrl_set_output(u8 inst, struct sclr_csc_cfg *cfg,
			 enum sclr_format fmt)
{
	if (inst >= SCL_MAX_INST)
		return -EINVAL;

	if (cfg->mode == SCL_OUT_CSC) {
		// Use yuv for csc
		// sc's data is always rgb
		if (!IS_YUV_FMT(fmt) ||
			((cfg->csc_type < SCL_CSC_601_LIMIT_RGB2YUV) &&
			 (cfg->csc_type != SCL_CSC_NONE))) {
			uartlog("Invalid parameter, csc_type or fmt\n");
			return -EINVAL;
		}
	} else {
		// Use rgb for quant/hsv
		if (IS_YUV_FMT(fmt)) {
			uartlog("quant/hsv not support yuv format\n");
			return -EINVAL;
		}
	}

	sclr_set_csc_ctrl(inst, cfg);

	return 0;
}

int sclr_set_fbd(u8 inst, struct sclr_fbd_cfg *cfg)
{
	if (inst >= SCL_MAX_INST)
		return -EINVAL;

	_reg_write(reg_base + REG_SCL_MAP_CONV_OFF_BASE_Y(inst), cfg->offset_base_y & 0xfffffff0);
	_reg_write(reg_base + REG_SCL_MAP_CONV_OFF_BASE_C(inst), cfg->offset_base_c & 0xfffffff0);
	_reg_write(reg_base + REG_SCL_MAP_CONV_COMP_BASE_Y(inst), cfg->comp_base_y & 0xfffffff0);
	_reg_write(reg_base + REG_SCL_MAP_CONV_COMP_BASE_C(inst), cfg->comp_base_c & 0xfffffff0);
	_reg_write(reg_base + REG_SCL_MAP_CONV_OFF_BASE_Y_35_32(inst), (cfg->offset_base_y >> 32) & 0xF);
	_reg_write(reg_base + REG_SCL_MAP_CONV_OFF_BASE_C_35_32(inst), (cfg->offset_base_c >> 32) & 0xF);
	_reg_write(reg_base + REG_SCL_MAP_CONV_COMP_BASE_Y_35_32(inst), (cfg->comp_base_y >> 32) & 0xF);
	_reg_write(reg_base + REG_SCL_MAP_CONV_COMP_BASE_C_35_32(inst), (cfg->comp_base_c >> 32) & 0xF);
	_reg_write(reg_base + REG_SCL_TOP_FBD_HIGNDDR(inst), cfg->offset_base_y >> 32);
	_reg_write(reg_base + REG_SCL_MAP_CONV_CROP_POS(inst), (cfg->crop.x << 16) | (cfg->crop.y));
	_reg_write(reg_base + REG_SCL_MAP_CONV_CROP_SIZE(inst), (cfg->crop.w << 16) | (cfg->crop.h));
	_reg_write(reg_base + REG_SCL_MAP_CONV_COMP_STRIDE(inst), (cfg->stride_y << 16) | (cfg->stride_c));
	_reg_write(reg_base + REG_SCL_MAP_CONV_OFF_STRIDE(inst), (cfg->height_y << 16) | (cfg->height_c));
	_reg_write(reg_base + REG_SCL_MAP_CONV_ENDIAN(inst), (cfg->endian << 4) | (cfg->endian));
	_reg_write(reg_base + REG_SCL_MAP_CONV_BIT_DEPTH(inst), (cfg->mono_en << 12) | (cfg->otbg_64x64_en << 8) | (cfg->depth_y << 2) | (cfg->depth_c));
	_reg_write_mask(reg_base + REG_SCL_MAP_CONV_OUT_CTRL(inst), 0x770000, (cfg->out_mode_y << 20) | (cfg->out_mode_c) << 16);
	g_fbd_cfg[inst] = *cfg;
	return 0;
}

static void _map_ctrl_to_img(struct sclr_ctrl_cfg *cfg,
				 struct sclr_img_cfg *img_cfg)
{
	img_cfg->burst = g_img_cfg[cfg->img_inst].burst;
	img_cfg->fmt = cfg->src_fmt;
	img_cfg->csc = cfg->src_csc;
	img_cfg->src = cfg->input;
	img_cfg->mem.addr0 = cfg->src_addr0;
	img_cfg->mem.addr1 = cfg->src_addr1;
	img_cfg->mem.addr2 = cfg->src_addr2;
	img_cfg->mem.start_x = 0;
	img_cfg->mem.start_y = 0;
	img_cfg->mem.width = cfg->src.w;
	img_cfg->mem.height = cfg->src.h;
	img_cfg->mem.pitch_y = IS_PACKED_FMT(cfg->src_fmt)
				? VIP_ALIGN(3 * cfg->src.w)
				: VIP_ALIGN(cfg->src.w);
	img_cfg->mem.pitch_c = IS_YUV_FMT(cfg->src_fmt)
				? VIP_ALIGN(cfg->src.w >> 1)
				: VIP_ALIGN(cfg->src.w);
}

static void _map_ctrl_to_sc(struct sclr_ctrl_cfg *cfg,
				struct sclr_core_cfg *sc_cfg)
{
	sc_cfg->sc_bypass = false;
	sc_cfg->gop_bypass = false;
	sc_cfg->cir_bypass = true;
	sc_cfg->odma_bypass = false;
	sc_cfg->sc.src = cfg->src;
	sc_cfg->sc.crop = cfg->src_crop;
	sc_cfg->sc.dst = cfg->dst.frame;
}

static void _map_ctrl_to_odma(struct sclr_ctrl_cfg *cfg,
				  struct sclr_odma_cfg *odma_cfg, u8 inst)
{
	odma_cfg->flip = SCL_FLIP_NO;
	odma_cfg->burst = g_odma_cfg[inst].burst;
	odma_cfg->fmt = cfg->dst.fmt;
	odma_cfg->csc_cfg.csc_type = cfg->dst.csc;
	odma_cfg->mem.addr0 = cfg->dst.addr0;
	odma_cfg->mem.addr1 = cfg->dst.addr1;
	odma_cfg->mem.addr2 = cfg->dst.addr2;
	odma_cfg->mem.start_x = cfg->dst.offset.x;
	odma_cfg->mem.start_y = cfg->dst.offset.y;
	odma_cfg->mem.width = cfg->dst.window.w;
	odma_cfg->mem.height = cfg->dst.window.h;
	odma_cfg->mem.pitch_y = cfg->dst.pitch_y;
	odma_cfg->mem.pitch_c = cfg->dst.pitch_c;
}

int _map_img_to_cmdq(struct sclr_ctrl_cfg *ctrl_cfg, union cmdq_set *cmd_start,
			 u16 cmd_idx)
{
	struct sclr_img_cfg img_cfg;
	struct sclr_csc_matrix *mtrx;
	u8 img_inst = ctrl_cfg->img_inst;

	if (img_inst >= VPSS_MAX)
		return cmd_idx;

	_map_ctrl_to_img(ctrl_cfg, &img_cfg);
	mtrx = &csc_mtrx[img_cfg.csc];

	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_IMG_CFG(img_inst),
			 (img_cfg.burst << 8) | (img_cfg.fmt << 4) |
			 img_cfg.src);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_OFFSET(img_inst),
			 (img_cfg.mem.start_y << 16) | img_cfg.mem.start_x);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_IMG_SIZE(img_inst),
			 ((img_cfg.mem.height - 1) << 16) |
			 (img_cfg.mem.width - 1));
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_PITCH_Y(img_inst), img_cfg.mem.pitch_y);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_PITCH_C(img_inst), img_cfg.mem.pitch_c);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_ADDR0_L(img_inst), img_cfg.mem.addr0);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_ADDR0_H(img_inst),
			 img_cfg.mem.addr0 >> 32);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_ADDR1_L(img_inst), img_cfg.mem.addr1);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_ADDR1_H(img_inst),
			 img_cfg.mem.addr1 >> 32);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_ADDR2_L(img_inst), img_cfg.mem.addr2);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_ADDR2_H(img_inst),
			 img_cfg.mem.addr2 >> 32);

	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_CSC_COEF0(img_inst),
			 (mtrx->coef[0][1] << 16) | mtrx->coef[0][0]);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_CSC_COEF1(img_inst), mtrx->coef[0][2]);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_CSC_COEF2(img_inst),
			 (mtrx->coef[1][1] << 16) | mtrx->coef[1][0]);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_CSC_COEF3(img_inst), mtrx->coef[1][2]);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_CSC_COEF4(img_inst),
			 (mtrx->coef[2][1] << 16) | mtrx->coef[2][0]);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_CSC_COEF5(img_inst), mtrx->coef[2][2]);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_CSC_SUB(img_inst),
			 (mtrx->sub[2] << 16) | (mtrx->sub[1] << 8)
			  | mtrx->sub[0]);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			 REG_SCL_IMG_CSC_ADD(img_inst),
			 (mtrx->add[2] << 16) | (mtrx->add[1] << 8)
			  | mtrx->add[0]);

	return cmd_idx;
}

int _map_sc_to_cmdq(struct sclr_ctrl_cfg *ctrl_cfg,
			union cmdq_set *cmd_start, u16 cmd_idx)
{
	u32 tmp = 0;
	u8 inst = ctrl_cfg->dst.inst;
	struct sclr_core_cfg sc_cfg;
	struct sclr_odma_cfg odma_cfg;

	if (inst >= SCL_MAX_INST)
		return cmd_idx;

	_map_ctrl_to_sc(ctrl_cfg, &sc_cfg);
	_map_ctrl_to_odma(ctrl_cfg, &odma_cfg, inst);

	// sc core
	tmp = 0;
	if (sc_cfg.sc_bypass)
		tmp |= 0x02;
	if (sc_cfg.gop_bypass)
		tmp |= 0x04;
	if (sc_cfg.cir_bypass)
		tmp |= 0x20;
	if (sc_cfg.odma_bypass)
		tmp |= 0x40;

	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_CFG(inst), tmp);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_SRC_SIZE(inst),
			 ((sc_cfg.sc.src.h - 1) << 16) | (sc_cfg.sc.src.w - 1));
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_CROP_OFFSET(inst),
			 (sc_cfg.sc.crop.y << 16) | sc_cfg.sc.crop.x);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_CROP_SIZE(inst),
			 ((sc_cfg.sc.crop.h - 1) << 16)
			  | (sc_cfg.sc.crop.w - 1));
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_OUT_SIZE(inst),
			 ((sc_cfg.sc.dst.h - 1) << 16) | (sc_cfg.sc.dst.w - 1));
	tmp = ((sc_cfg.sc.crop.w) << 13) / (sc_cfg.sc.dst.w);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_SC_H_CFG(inst),
			 tmp);
	tmp = ((sc_cfg.sc.crop.h) << 13) / (sc_cfg.sc.dst.h);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_SC_V_CFG(inst),
			 tmp);

	// sc odma
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_ODMA_CFG(inst),
			 (odma_cfg.flip << 16) |  (odma_cfg.fmt << 8) |
			 odma_cfg.burst);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_ODMA_OFFSET_X(inst),
			 odma_cfg.mem.start_x);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_ODMA_OFFSET_Y(inst),
			 odma_cfg.mem.start_y);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_ODMA_WIDTH(inst),
			 odma_cfg.mem.width - 1);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_ODMA_HEIGHT(inst),
			 odma_cfg.mem.height - 1);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_ODMA_PITCH_Y(inst),
			 odma_cfg.mem.pitch_y);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_ODMA_PITCH_C(inst),
			 odma_cfg.mem.pitch_c);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_ODMA_ADDR0_L(inst),
			 odma_cfg.mem.addr0);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_ODMA_ADDR0_H(inst),
			 odma_cfg.mem.addr0 >> 32);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_ODMA_ADDR1_L(inst),
			 odma_cfg.mem.addr1);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_ODMA_ADDR1_H(inst),
			 odma_cfg.mem.addr1 >> 32);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_ODMA_ADDR2_L(inst),
			 odma_cfg.mem.addr2);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_ODMA_ADDR2_H(inst),
			 odma_cfg.mem.addr2 >> 32);

	// sc csc
	if (odma_cfg.csc_cfg.csc_type == SCL_CSC_NONE) {
		cmdQ_set_package(&cmd_start[cmd_idx++].reg,
				 REG_SCL_CSC_EN(inst), 0);
	} else {
		struct sclr_csc_matrix *cfg = &csc_mtrx[odma_cfg.csc_cfg.csc_type];

		cmdQ_set_package(&cmd_start[cmd_idx++].reg,
				 REG_SCL_CSC_EN(inst), 1);

		cmdQ_set_package(&cmd_start[cmd_idx++].reg,
				 REG_SCL_CSC_COEF0(inst),
				 (cfg->coef[0][1] << 16) | cfg->coef[0][0]);
		cmdQ_set_package(&cmd_start[cmd_idx++].reg,
				 REG_SCL_CSC_COEF1(inst),
				 (cfg->coef[1][0] << 16) | cfg->coef[0][2]);
		cmdQ_set_package(&cmd_start[cmd_idx++].reg,
				 REG_SCL_CSC_COEF2(inst),
				 (cfg->coef[1][2] << 16) | cfg->coef[1][1]);
		cmdQ_set_package(&cmd_start[cmd_idx++].reg,
				 REG_SCL_CSC_COEF3(inst),
				 (cfg->coef[2][1] << 16) | cfg->coef[2][0]);
		cmdQ_set_package(&cmd_start[cmd_idx++].reg,
				 REG_SCL_CSC_COEF4(inst), cfg->coef[2][2]);

		cmdQ_set_package(&cmd_start[cmd_idx++].reg,
				 REG_SCL_CSC_OFFSET(inst),
				 (cfg->add[2] << 16) | (cfg->add[1] << 8) |
				 cfg->add[0]);
	}

	return cmd_idx;
}

int _map_border_to_cmdq(struct sclr_ctrl_cfg *ctrl_cfg,
			union cmdq_set *cmd_start, u16 cmd_idx){
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			REG_SCL_BORDER_CFG(ctrl_cfg->img_inst), ctrl_cfg->border_cfg.cfg.raw);
	cmdQ_set_package(&cmd_start[cmd_idx++].reg,
			REG_SCL_BORDER_OFFSET(ctrl_cfg->img_inst), (ctrl_cfg->border_cfg.start.y << 16) | ctrl_cfg->border_cfg.start.x);
	return cmd_idx;
}
/**
 * sclr_engine_cmdq - trigger sc  by cmdq
 *
 * @param cfgs: settings for cmdq
 * @param cnt: the number of settings
 * @param cmdq_addr: memory-address to put cmdq
 */
void sclr_engine_cmdq(u8 inst, struct sclr_ctrl_cfg *cfgs, u8 cnt, u64 cmdq_addr)
{
	u8 i = 0;
	u16 cmd_idx = 0;
	union cmdq_set *cmd_start = (union cmdq_set *)cmdq_addr;
	//u32 tmp = 0, inst_enable = 0;
	u32 flag_num = 1;	// 0: sc_str_flag, sc_stp_flag
	union sclr_top_cfg_01 cfg_01;

	memset((void *)cmdq_addr, 0, sizeof(u32)*0x100);

	_reg_write(reg_base + REG_SCL_TOP_CMDQ_START(inst), 0x18000);
	_reg_write(reg_base + REG_SCL_TOP_CMDQ_STOP(inst), 0x18000);

	for (i = 0; i < cnt; ++i) {
		struct sclr_ctrl_cfg *cfg = (cfgs + i);
		//u8 img_inst = cfg->img_inst;

		cmd_idx = _map_img_to_cmdq(cfg, cmd_start, cmd_idx);

		cmd_idx = _map_sc_to_cmdq(cfg, cmd_start, cmd_idx);

		cmd_idx = _map_border_to_cmdq(cfg, cmd_start, cmd_idx);
		cmdQ_set_package(&cmd_start[cmd_idx++].reg,
				 REG_SCL_TOP_CMDQ_STOP(inst), 0x100ff);

		cfg_01.raw = 0;
		cfg_01.b.sc_enable = 1;
		cfg_01.b.fbd_enable = 0;
		cfg_01.b.qos_en = BIT(2) | BIT(4);

		//tmp = _reg_read(reg_base + REG_SCL_TOP_CFG1(inst));
		//tmp &= ~0x1f;
		//tmp |= inst_enable;
		cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_TOP_CFG1(inst),
				 cfg_01.raw);

		// start sclr
		cmdQ_set_package(&cmd_start[cmd_idx++].reg,
				 REG_SCL_TOP_IMG_CTRL(inst), BIT(1));

		// wait end-inter(1)
		cmdQ_set_wait(&cmd_start[cmd_idx++], /*is_timer*/false, flag_num, 0);
	}
	// clear intr
	cmdQ_set_package(&cmd_start[cmd_idx++].reg, REG_SCL_TOP_INTR_STATUS(inst),
			 0x1000ffff);

	cmd_start[cmd_idx-1].reg.intr_end = 1;
	cmd_start[cmd_idx-1].reg.intr_last = 1;

	cmdQ_intr_ctrl(REG_SCL_CMDQ_BASE(inst), 0x02);
	cmdQ_engine(REG_SCL_CMDQ_BASE(inst), (uintptr_t)cmdq_addr,
			REG_SCL_TOP_BASE(inst) >> 22, true, false, cmd_idx);
}

void sclr_dump_top_register(u8 inst)
{
	u32 val;
	u32 top_base = reg_base + REG_SCL_TOP_BASE(inst);

	uartlog("top base address=0x%08x\n", top_base);

	val = _reg_read(top_base + SC_TOP_REG_00);
	uartlog("SC_TOP_REG_00=0x%08x\n", val);
	uartlog("	 ip_trig_src=%d, force_clk_enable=%d\n",
		(val & SC_TOP_REG_IP_TRIG_SRC_MASK) >> SC_TOP_REG_IP_TRIG_SRC_OFFSET,
		(val & SC_TOP_REG_FORCE_CLK_ENABLE_MASK) >> SC_TOP_REG_FORCE_CLK_ENABLE_OFFSET);

	val = _reg_read(top_base + SC_TOP_REG_01);
	uartlog("SC_TOP_REG_01=0x%08x\n", val);
	uartlog("	 sc_d_en=%d, sc_v1_en=%d, sc_v2_en=%d, sc_v3_en=%d\n",
		(val & SC_TOP_REG_SC_D_EN_MASK) >> SC_TOP_REG_SC_D_EN_OFFSET,
		(val & SC_TOP_REG_SC_V1_EN_MASK) >> SC_TOP_REG_SC_V1_EN_OFFSET,
		(val & SC_TOP_REG_SC_V2_EN_MASK) >> SC_TOP_REG_SC_V2_EN_OFFSET,
		(val & SC_TOP_REG_SC_V3_EN_MASK) >> SC_TOP_REG_SC_V3_EN_OFFSET);
	uartlog("	 disp_en=%d, disp_src_sel=%d, sc_d_src_sel=%d\n",
		(val & SC_TOP_REG_DISP_EN_MASK) >> SC_TOP_REG_DISP_EN_OFFSET,
		(val & SC_TOP_REG_DISP_SRC_SEL_MASK) >> SC_TOP_REG_DISP_SRC_SEL_OFFSET,
		(val & SC_TOP_REG_SC_D_SRC_SEL_MASK) >> SC_TOP_REG_SC_D_SRC_SEL_OFFSET);
	uartlog("	 sc_rot_sw_rst=%d, sc_debug_en=%d, qos_en=%d\n",
		(val & SC_TOP_REG_SC_ROT_SW_RST_MASK) >> SC_TOP_REG_SC_ROT_SW_RST_OFFSET,
		(val & SC_TOP_REG_SC_DEBUG_EN_MASK) >> SC_TOP_REG_SC_DEBUG_EN_OFFSET,
		(val & SC_TOP_REG_QOS_EN_MASK) >> SC_TOP_REG_QOS_EN_OFFSET);

	uartlog("SC_TOP_REG_02=0x%08x\n", _reg_read(top_base + SC_TOP_REG_02));
	uartlog("SC_TOP_REG_03=0x%08x\n", _reg_read(top_base + SC_TOP_REG_03));

	val = _reg_read(top_base + SC_TOP_REG_04);
	uartlog("SC_TOP_REG_04=0x%08x\n", val);
	uartlog("	 sw_force_up_b0=%d, sw_force_up_b1=%d, sw_force_up_b2=%d\n",
		(val & SC_TOP_REG_SW_FORCE_UP_B0_MASK) >> SC_TOP_REG_SW_FORCE_UP_B0_OFFSET,
		(val & SC_TOP_REG_SW_FORCE_UP_B1_MASK) >> SC_TOP_REG_SW_FORCE_UP_B1_OFFSET,
		(val & SC_TOP_REG_SW_FORCE_UP_B2_MASK) >> SC_TOP_REG_SW_FORCE_UP_B2_OFFSET);
	uartlog("	 sw_force_up_b3=%d, sw_force_up_b4=%d, sw_force_up_b5=%d\n",
		(val & SC_TOP_REG_SW_FORCE_UP_B3_MASK) >> SC_TOP_REG_SW_FORCE_UP_B3_OFFSET,
		(val & SC_TOP_REG_SW_FORCE_UP_B4_MASK) >> SC_TOP_REG_SW_FORCE_UP_B4_OFFSET,
		(val & SC_TOP_REG_SW_FORCE_UP_B5_MASK) >> SC_TOP_REG_SW_FORCE_UP_B5_OFFSET);
	uartlog("	 sw_force_up_b6=%d, sw_force_up_b7=%d, sw_mask_up=%d, shrd_sel=%d\n",
		(val & SC_TOP_REG_SW_FORCE_UP_B6_MASK) >> SC_TOP_REG_SW_FORCE_UP_B6_OFFSET,
		(val & SC_TOP_REG_SW_FORCE_UP_B7_MASK) >> SC_TOP_REG_SW_FORCE_UP_B7_OFFSET,
		(val & SC_TOP_REG_SW_MASK_UP_MASK) >> SC_TOP_REG_SW_MASK_UP_OFFSET,
		(val & SC_TOP_REG_SHRD_SEL_MASK) >> SC_TOP_REG_SHRD_SEL_OFFSET);

	uartlog("SC_TOP_REG_05=0x%08x\n", _reg_read(top_base + SC_TOP_REG_05));
	uartlog("SC_TOP_REG_06=0x%08x\n", _reg_read(top_base + SC_TOP_REG_06));

	val = _reg_read(top_base + SC_TOP_REG_SB_CBAR);
	uartlog("SC_TOP_REG_SB_CBAR=0x%08x\n", val);
	uartlog("	 sb_wr_ctrl0=%d, sb_wr_ctrl1=%d, sb_rd_ctrl=%d\n",
		(val & SC_TOP_REG_SB_WR_CTRL0_MASK) >> SC_TOP_REG_SB_WR_CTRL0_OFFSET,
		(val & SC_TOP_REG_SB_WR_CTRL1_MASK) >> SC_TOP_REG_SB_WR_CTRL1_OFFSET,
		(val & SC_TOP_REG_SB_RD_CTRL_MASK) >> SC_TOP_REG_SB_RD_CTRL_OFFSET);

	uartlog("SC_TOP_REG_08=0x%08x\n", _reg_read(top_base + SC_TOP_REG_08));

	val = _reg_read(top_base + SC_TOP_REG_0C);
	uartlog("SC_TOP_REG_0C=0x%08x\n", val);
	uartlog("	 int_mask=0x%08x\n", val);

	val = _reg_read(top_base + SC_TOP_REG_0D);
	uartlog("SC_TOP_REG_0D=0x%08x\n", val);
	uartlog("	 int_status=0x%08x\n", val);

	val = _reg_read(top_base + SC_TOP_REG_0E);
	uartlog("SC_TOP_REG_0E=0x%08x\n", val);
	uartlog("	 int_enable=0x%08x\n", val);

	uartlog("SC_TOP_REG_0F=0x%08x\n", _reg_read(top_base + SC_TOP_REG_0F));
	uartlog("SC_TOP_REG_10=0x%08x\n", _reg_read(top_base + SC_TOP_REG_10));
	uartlog("SC_TOP_REG_11=0x%08x\n", _reg_read(top_base + SC_TOP_REG_11));
	uartlog("SC_TOP_REG_12=0x%08x\n", _reg_read(top_base + SC_TOP_REG_12));
	uartlog("SC_TOP_REG_13=0x%08x\n", _reg_read(top_base + SC_TOP_REG_13));
	uartlog("SC_TOP_REG_LVDSTX=0x%08x\n", _reg_read(top_base + SC_TOP_REG_LVDSTX));
	uartlog("SC_TOP_REG_BT_ENC_0=0x%08x\n", _reg_read(top_base + SC_TOP_REG_BT_ENC_0));
	uartlog("SC_TOP_RGG_BT_SYNC_CODE=0x%08x\n", _reg_read(top_base + SC_TOP_RGG_BT_SYNC_CODE));
	uartlog("SC_TOP_REG_BK_BLK_DATA=0x%08x\n", _reg_read(top_base + SC_TOP_REG_BK_BLK_DATA));
	uartlog("SC_TOP_REG_VO_MUX=0x%08x\n", _reg_read(top_base + SC_TOP_REG_VO_MUX));
	uartlog("SC_TOP_REG_SC_074=0x%08x\n", _reg_read(top_base + SC_TOP_REG_SC_074));
	uartlog("SC_TOP_REG_SC_078=0x%08x\n", _reg_read(top_base + SC_TOP_REG_SC_078));
	uartlog("SC_TOP_REG_VO_MUX_EXT=0x%08x\n", _reg_read(top_base + SC_TOP_REG_VO_MUX_EXT));
	uartlog("SC_TOP_REG_VO_MUX_0=0x%08x\n", _reg_read(top_base + SC_TOP_REG_VO_MUX_0));
	uartlog("SC_TOP_REG_VO_MUX_1=0x%08x\n", _reg_read(top_base + SC_TOP_REG_VO_MUX_1));
	uartlog("SC_TOP_REG_VO_MUX_2=0x%08x\n", _reg_read(top_base + SC_TOP_REG_VO_MUX_2));
	uartlog("SC_TOP_REG_VO_MUX_3=0x%08x\n", _reg_read(top_base + SC_TOP_REG_VO_MUX_3));
	uartlog("SC_TOP_REG_VO_MUX_4=0x%08x\n", _reg_read(top_base + SC_TOP_REG_VO_MUX_4));
	uartlog("SC_TOP_REG_VO_MUX_5=0x%08x\n", _reg_read(top_base + SC_TOP_REG_VO_MUX_5));
	uartlog("SC_TOP_REG_VO_MUX_6=0x%08x\n", _reg_read(top_base + SC_TOP_REG_VO_MUX_6));
	uartlog("SC_TOP_REG_VO_MUX_7=0x%08x\n", _reg_read(top_base + SC_TOP_REG_VO_MUX_7));
	uartlog("SC_TOP_REG_VO_MUX_8=0x%08x\n", _reg_read(top_base + SC_TOP_REG_VO_MUX_8));
	uartlog("SC_TOP_REG_VO_MUX_9=0x%08x\n", _reg_read(top_base + SC_TOP_REG_VO_MUX_9));
	uartlog("SC_TOP_REG_VO_MUX_A=0x%08x\n", _reg_read(top_base + SC_TOP_REG_VO_MUX_A));
	uartlog("SC_TOP_REG_RMX_CTRL=0x%08x\n", _reg_read(top_base + SC_TOP_REG_RMX_CTRL));
	uartlog("SC_TOP_REG_FAB_LP=0x%08x\n", _reg_read(top_base + SC_TOP_REG_FAB_LP));
	uartlog("SC_TOP_REG_CLK_EN=0x%08x\n", _reg_read(top_base + SC_TOP_REG_CLK_EN));
	uartlog("SC_TOP_REG_DBG_VALID=0x%08x\n", _reg_read(top_base + SC_TOP_REG_DBG_VALID));
	uartlog("SC_TOP_REG_DBG_READY=0x%08x\n", _reg_read(top_base + SC_TOP_REG_DBG_READY));

	val = _reg_read(top_base + SC_TOP_REG_BLD_CTRL);
	uartlog("SC_TOP_REG_BLD_CTRL=0x%08x\n", val);
	uartlog("	 bld_en=%d, bld_fix_alpha=%d, bld_blend_y_only=%d\n",
		(val & SC_TOP_REG_BLD_EN_MASK) >> SC_TOP_REG_BLD_EN_OFFSET,
		(val & SC_TOP_REG_BLD_FIX_ALPHA_MASK) >> SC_TOP_REG_BLD_FIX_ALPHA_OFFSET,
		(val & SC_TOP_REG_BLD_BLEND_Y_ONLY_MASK) >> SC_TOP_REG_BLD_BLEND_Y_ONLY_OFFSET);
	uartlog("	 bld_y2r_enable=%d, bld_alpha_factor=%d\n",
		(val & SC_TOP_REG_BLD_Y2R_ENABLE_MASK) >> SC_TOP_REG_BLD_Y2R_ENABLE_OFFSET,
		(val & SC_TOP_REG_BLD_ALPHA_FACTOR_MASK) >> SC_TOP_REG_BLD_ALPHA_FACTOR_OFFSET);

	val = _reg_read(top_base + SC_TOP_REG_BLD_SIZE);
	uartlog("SC_TOP_REG_BLD_SIZE=0x%08x\n", val);
	uartlog("	 bld_alpha_stp=%d, bld_wd=%d\n",
		(val & SC_TOP_REG_BLD_ALPHA_STP_MASK) >> SC_TOP_REG_BLD_ALPHA_STP_OFFSET,
		(val & SC_TOP_REG_BLD_WD_MASK) >> SC_TOP_REG_BLD_WD_OFFSET);

	uartlog("SC_TOP_PRI_M_REG_0=0x%08x\n", _reg_read(top_base + SC_TOP_PRI_M_REG_0));
	uartlog("SC_TOP_PRI_M_REG_1=0x%08x\n", _reg_read(top_base + SC_TOP_PRI_M_REG_1));
	uartlog("SC_TOP_PRI_M_REG_2=0x%08x\n", _reg_read(top_base + SC_TOP_PRI_M_REG_2));
	uartlog("SC_TOP_PRI_M_REG_3=0x%08x\n", _reg_read(top_base + SC_TOP_PRI_M_REG_3));
	uartlog("SC_TOP_PRI_M_REG_4=0x%08x\n", _reg_read(top_base + SC_TOP_PRI_M_REG_4));
	uartlog("SC_TOP_PRI_M_REG_5=0x%08x\n", _reg_read(top_base + SC_TOP_PRI_M_REG_5));
	uartlog("SC_TOP_PRI_M_REG_6=0x%08x\n", _reg_read(top_base + SC_TOP_PRI_M_REG_6));
	uartlog("SC_TOP_PRI_M_REG_8=0x%08x\n", _reg_read(top_base + SC_TOP_PRI_M_REG_8));
}

void sclr_dump_img_in_register(int img_inst)
{
	u32 img_base = reg_base + REG_SCL_IMG_BASE(img_inst);
	u32 val;

	if (img_inst >= 2)
		uartlog("img(%d) out of range\n", img_inst);

	uartlog("img(%d) base addr 0x%08x\n", img_inst, img_base);

	val = _reg_read(img_base + IMG_IN_REG_00);
	uartlog("IMG_IN_REG_00=0x%08x\n", val);
	uartlog("	 src_sel=%d, fmt_sel=%d, burst_len=%d, img_csc_en=%d\n",
		(val & IMG_IN_REG_SRC_SEL_MASK) >> IMG_IN_REG_SRC_SEL_OFFSET,
		(val & IMG_IN_REG_FMT_SEL_MASK) >> IMG_IN_REG_FMT_SEL_OFFSET,
		(val & IMG_IN_REG_BURST_LN_MASK) >> IMG_IN_REG_BURST_LN_OFFSET,
		(val & IMG_IN_REG_IMG_CSC_EN_MASK) >> IMG_IN_REG_IMG_CSC_EN_OFFSET);
	uartlog("	 auto_csc_en=%d, 64b_align=%d, force_clk_enable=%d\n",
		(val & IMG_IN_REG_AUTO_CSC_EN_MASK) >> IMG_IN_REG_AUTO_CSC_EN_OFFSET,
		(val & IMG_IN_REG_64B_ALIGN_MASK) >> IMG_IN_REG_64B_ALIGN_OFFSET,
		(val & IMG_IN_REG_FORCE_CLK_ENABLE_MASK) >> IMG_IN_REG_FORCE_CLK_ENABLE_OFFSET);

	val = _reg_read(img_base + IMG_IN_REG_01);
	uartlog("IMG_IN_REG_01=0x%08x\n", val);
	uartlog("	 src_x_str=%d, src_y_str=%d\n",
		(val & IMG_IN_REG_SRC_X_STR_MASK) >> IMG_IN_REG_SRC_X_STR_OFFSET,
		(val & IMG_IN_REG_SRC_Y_STR_MASK) >> IMG_IN_REG_SRC_Y_STR_OFFSET);

	val = _reg_read(img_base + IMG_IN_REG_02);
	uartlog("IMG_IN_REG_02=0x%08x\n", val);
	uartlog("	 src_wd=%d, src_ht=%d\n",
		(val & IMG_IN_REG_SRC_WD_MASK) >> IMG_IN_REG_SRC_WD_OFFSET,
		(val & IMG_IN_REG_SRC_HT_MASK) >> IMG_IN_REG_SRC_HT_OFFSET);

	val = _reg_read(img_base + IMG_IN_REG_03);
	uartlog("IMG_IN_REG_03=0x%08x\n", val);
	uartlog("	 src_y_pitch=%d\n",
		(val & IMG_IN_REG_SRC_Y_PITCH_MASK) >> IMG_IN_REG_SRC_Y_PITCH_OFFSET);

	val = _reg_read(img_base + IMG_IN_REG_04);
	uartlog("IMG_IN_REG_04=0x%08x\n", val);
	uartlog("	 src_c_pitch=%d\n",
		(val & IMG_IN_REG_SRC_C_PITCH_MASK) >> IMG_IN_REG_SRC_C_PITCH_OFFSET);

	val = _reg_read(img_base + IMG_IN_REG_05);
	uartlog("IMG_IN_REG_05=0x%08x\n", val);
	uartlog("	 sw_force_up=%d, sw_mask_up=%d, shrd_sel=%d\n",
		(val & IMG_IN_REG_SW_FORCE_UP_MASK) >> IMG_IN_REG_SW_FORCE_UP_OFFSET,
		(val & IMG_IN_REG_SW_MASK_UP_MASK) >> IMG_IN_REG_SW_MASK_UP_OFFSET,
		(val & IMG_IN_REG_SHRD_SEL_MASK) >> IMG_IN_REG_SHRD_SEL_OFFSET);

	uartlog("IMG_IN_REG_06=0x%08x\n", _reg_read(img_base + IMG_IN_REG_06));
	uartlog("IMG_IN_REG_07=0x%08x\n", _reg_read(img_base + IMG_IN_REG_07));
	uartlog("IMG_IN_REG_08=0x%08x\n", _reg_read(img_base + IMG_IN_REG_08));
	uartlog("IMG_IN_REG_Y_BASE_0=0x%08x\n", _reg_read(img_base + IMG_IN_REG_Y_BASE_0));
	uartlog("IMG_IN_REG_Y_BASE_1=0x%08x\n", _reg_read(img_base + IMG_IN_REG_Y_BASE_1));
	uartlog("IMG_IN_REG_U_BASE_0=0x%08x\n", _reg_read(img_base + IMG_IN_REG_U_BASE_0));
	uartlog("IMG_IN_REG_U_BASE_1=0x%08x\n", _reg_read(img_base + IMG_IN_REG_U_BASE_1));
	uartlog("IMG_IN_REG_V_BASE_0=0x%08x\n", _reg_read(img_base + IMG_IN_REG_V_BASE_0));
	uartlog("IMG_IN_REG_V_BASE_1=0x%08x\n", _reg_read(img_base + IMG_IN_REG_V_BASE_1));
	uartlog("IMG_IN_REG_040=0x%08x\n", _reg_read(img_base + IMG_IN_REG_040));
	uartlog("IMG_IN_REG_044=0x%08x\n", _reg_read(img_base + IMG_IN_REG_044));
	uartlog("IMG_IN_REG_048=0x%08x\n", _reg_read(img_base + IMG_IN_REG_048));
	uartlog("IMG_IN_REG_04C=0x%08x\n", _reg_read(img_base + IMG_IN_REG_04C));
	uartlog("IMG_IN_REG_050=0x%08x\n", _reg_read(img_base + IMG_IN_REG_050));
	uartlog("IMG_IN_REG_054=0x%08x\n", _reg_read(img_base + IMG_IN_REG_054));
	uartlog("IMG_IN_REG_058=0x%08x\n", _reg_read(img_base + IMG_IN_REG_058));
	uartlog("IMG_IN_REG_05C=0x%08x\n", _reg_read(img_base + IMG_IN_REG_05C));
	uartlog("IMG_IN_REG_060=0x%08x\n", _reg_read(img_base + IMG_IN_REG_060));
	uartlog("IMG_IN_REG_064=0x%08x\n", _reg_read(img_base + IMG_IN_REG_064));
	uartlog("IMG_IN_REG_068=0x%08x\n", _reg_read(img_base + IMG_IN_REG_068));
	uartlog("IMG_IN_REG_AXI_ST=0x%08x\n", _reg_read(img_base + IMG_IN_REG_AXI_ST));
	uartlog("IMG_IN_REG_BW_LIMIT=0x%08x\n", _reg_read(img_base + IMG_IN_REG_BW_LIMIT));
	uartlog("IMG_IN_REG_CATCH=0x%08x\n", _reg_read(img_base + IMG_IN_REG_CATCH));
	uartlog("IMG_IN_REG_CHK_CTRL=0x%08x\n", _reg_read(img_base + IMG_IN_REG_CHK_CTRL));
	uartlog("IMG_IN_CHKSUM_AXI_RD=0x%08x\n", _reg_read(img_base + IMG_IN_CHKSUM_AXI_RD));

	val = _reg_read(img_base + IMG_IN_SB_REG_CTRL);
	uartlog("IMG_IN_SB_REG_CTRL=0x%08x\n", val);
	uartlog("	 sb_mode=%d, sb_size=%d, sb_nb=%d, sb_sw_rptr=%d\n",
		(val & IMG_IN_REG_SB_MODE_MASK) >> IMG_IN_REG_SB_MODE_OFFSET,
		(val & IMG_IN_REG_SB_SIZE_MASK) >> IMG_IN_REG_SB_SIZE_OFFSET,
		(val & IMG_IN_REG_SB_NB_MASK) >> IMG_IN_REG_SB_NB_OFFSET,
		(val & IMG_IN_REG_SB_SW_RPTR_MASK) >> IMG_IN_REG_SB_SW_RPTR_OFFSET);

	uartlog("IMG_IN_SB_REG_C_STAT=0x%08x\n", _reg_read(img_base + IMG_IN_SB_REG_C_STAT));
	uartlog("IMG_IN_SB_REG_Y_STAT=0x%08x\n", _reg_read(img_base + IMG_IN_SB_REG_Y_STAT));
}

void sclr_dump_core_register(int inst)
{
	u32 val;
	u32 core_base = reg_base + REG_SCL_CORE_BASE(inst);

	if (inst >= SCL_MAX_INST)
		uartlog("sc(%d) out of range\n", inst);

	uartlog("sc(%d) core base address=0x%08x\n", inst, core_base);

	val = _reg_read(core_base + SC_CORE_SC_WRAP_REG_0);
	uartlog("SC_CORE_SC_WRAP_REG_0=0x%08x\n", val);
	uartlog("	 sc_core_bypass_en=%d, vgop_bypass_en=%d, dbg_en=%d\n",
		(val & SC_CORE_REG_SC_CORE_BYPASS_EN_MASK) >> SC_CORE_REG_SC_CORE_BYPASS_EN_OFFSET,
		(val & SC_CORE_REG_VGOP_BYPASS_EN_MASK) >> SC_CORE_REG_VGOP_BYPASS_EN_OFFSET,
		(val & SC_CORE_REG_DBG_EN_MASK) >> SC_CORE_REG_DBG_EN_OFFSET);
	uartlog("	 cir_bypass_en=%d, dma_bypass_en=%d, force_clk_enable=%d\n",
		(val & SC_CORE_REG_CIR_BYPASS_EN_MASK) >> SC_CORE_REG_CIR_BYPASS_EN_OFFSET,
		(val & SC_CORE_REG_DMA_BYPASS_EN_MASK) >> SC_CORE_REG_DMA_BYPASS_EN_OFFSET,
		(val & SC_CORE_REG_FORCE_CLK_ENABLE_MASK) >> SC_CORE_REG_FORCE_CLK_ENABLE_OFFSET);

	val = _reg_read(core_base + SC_CORE_SC_WRAP_REG_1);
	uartlog("SC_CORE_SC_WRAP_REG_1=0x%08x\n", val);
	uartlog("	 shdw_read_sel=%d\n",
		(val & SC_CORE_REG_SHDW_READ_SEL_MASK) >> SC_CORE_REG_SHDW_READ_SEL_OFFSET);

	val = _reg_read(core_base + SC_CORE_SC_WRAP_REG_2);
	uartlog("SC_CORE_SC_WRAP_REG_2=0x%08x\n", val);
	uartlog("	 crop_idle=%d, hscale_idle=%d, vscale_idle=%d, vgop_idle=%d, sc_wdma_idle=%d\n",
		(val & SC_CORE_REG_CROP_IDLE_MASK) >> SC_CORE_REG_CROP_IDLE_OFFSET,
		(val & SC_CORE_REG_HSCALE_IDLE_MASK) >> SC_CORE_REG_HSCALE_IDLE_OFFSET,
		(val & SC_CORE_REG_VSCALE_IDLE_MASK) >> SC_CORE_REG_VSCALE_IDLE_OFFSET,
		(val & SC_CORE_REG_VGOP_IDLE_MASK) >> SC_CORE_REG_VGOP_IDLE_OFFSET,
		(val & SC_CORE_REG_SC_WDMA_IDLE_MASK) >> SC_CORE_REG_SC_WDMA_IDLE_OFFSET);

	uartlog("SC_CORE_SC_CROP_REG_0=0x%08x\n", _reg_read(core_base + SC_CORE_SC_CROP_REG_0));
	uartlog("SC_CORE_SC_CROP_REG_1=0x%08x\n", _reg_read(core_base + SC_CORE_SC_CROP_REG_1));
	uartlog("SC_CORE_SC_CROP_REG_2=0x%08x\n", _reg_read(core_base + SC_CORE_SC_CROP_REG_2));
	uartlog("SC_CORE_SC_CROP_REG_3=0x%08x\n", _reg_read(core_base + SC_CORE_SC_CROP_REG_3));

	val = _reg_read(core_base + SC_CORE_SC_CORE_CFG);
	uartlog("SC_CORE_SC_CORE_CFG=0x%08x\n", val);
	uartlog("	 tap number=%d, Version=%d, line buffer width=%d\n",
		(val & 0xf0000000) >> 28,
		(val & 0x0f000000) >> 24,
		(val & 0xffff));

	val = _reg_read(core_base + SC_CORE_SC_2TAP_CFG);
	uartlog("SC_CORE_SC_2TAP_CFG=0x%08x\n", val);
	uartlog("	 resize_blnr_mode=%d, resize_area_fast=%d, resize_dbg_sel=%d\n",
		(val & SC_CORE_REG_RESIZE_BLNR_MODE_MASK) >> SC_CORE_REG_RESIZE_BLNR_MODE_OFFSET,
		(val & SC_CORE_REG_RESIZE_AREA_FAST_MASK) >> SC_CORE_REG_RESIZE_AREA_FAST_OFFSET,
		(val & SC_CORE_REG_RESIZE_DBG_SEL_MASK) >> SC_CORE_REG_RESIZE_DBG_SEL_OFFSET);

	val = _reg_read(core_base + SC_CORE_SC_2TAP_NOR);
	uartlog("SC_CORE_SC_2TAP_NOR=0x%08x\n", val);
	uartlog("	 resize_h_nor=0x%04x, resize_v_nor=0x%04x\n",
		(val & SC_CORE_REG_RESIZE_H_NOR_MASK) >> SC_CORE_REG_RESIZE_H_NOR_OFFSET,
		(val & SC_CORE_REG_RESIZE_V_NOR_MASK) >> SC_CORE_REG_RESIZE_V_NOR_OFFSET);

	uartlog("SC_CORE_SC_2_TAP_DBG=0x%08x\n", _reg_read(core_base + SC_CORE_SC_2_TAP_DBG));

	val = _reg_read(core_base + SC_CORE_BORDER_0);
	uartlog("SC_CORE_BORDER_0=0x%08x\n", val);
	uartlog("	 bd_r=%d, bd_g=%d, bd_b=%d, border_en=%d\n",
		(val & SC_CORE_REG_BD_R_MASK) >> SC_CORE_REG_BD_R_OFFSET,
		(val & SC_CORE_REG_BD_G_MASK) >> SC_CORE_REG_BD_G_OFFSET,
		(val & SC_CORE_REG_BD_B_MASK) >> SC_CORE_REG_BD_B_OFFSET,
		(val & SC_CORE_REG_BORDER_EN_MASK) >> SC_CORE_REG_BORDER_EN_OFFSET);

	val = _reg_read(core_base + SC_CORE_BORDER_1);
	uartlog("SC_CORE_BORDER_1=0x%08x\n", val);
	uartlog("	 border_x_str=%d, border_y_str=%d\n",
		(val & SC_CORE_REG_BORDER_X_STR_MASK) >> SC_CORE_REG_BORDER_X_STR_OFFSET,
		(val & SC_CORE_REG_BORDER_Y_STR_MASK) >> SC_CORE_REG_BORDER_Y_STR_OFFSET);

	uartlog("SC_CORE_REG_CHK_CTRL=0x%08x\n", _reg_read(core_base + SC_CORE_REG_CHK_CTRL));
	uartlog("SC_CORE_CHK_AXI_VGOP_0=0x%08x\n", _reg_read(core_base + SC_CORE_CHK_AXI_VGOP_0));
	uartlog("SC_CORE_CHK_AXI_VGOP_1=0x%08x\n", _reg_read(core_base + SC_CORE_CHK_AXI_VGOP_1));
	uartlog("SC_CORE_CHK_AXI_WDATA=0x%08x\n", _reg_read(core_base + SC_CORE_CHK_AXI_WDATA));
	uartlog("SC_CORE_SC_CORE_REG_0=0x%08x\n", _reg_read(core_base + SC_CORE_SC_CORE_REG_0));
	uartlog("SC_CORE_SC_CORE_REG_1=0x%08x\n", _reg_read(core_base + SC_CORE_SC_CORE_REG_1));
	uartlog("SC_CORE_SC_CORE_REG_2=0x%08x\n", _reg_read(core_base + SC_CORE_SC_CORE_REG_2));
	uartlog("SC_CORE_SC_CORE_REG_3=0x%08x\n", _reg_read(core_base + SC_CORE_SC_CORE_REG_3));
	uartlog("SC_CORE_SC_CORE_REG_4=0x%08x\n", _reg_read(core_base + SC_CORE_SC_CORE_REG_4));
	uartlog("SC_CORE_SC_CORE_REG_5=0x%08x\n", _reg_read(core_base + SC_CORE_SC_CORE_REG_5));
	uartlog("SC_CORE_SC_CORE_REG_6=0x%08x\n", _reg_read(core_base + SC_CORE_SC_CORE_REG_6));

	uartlog("SC_CORE_SC_CORE_REG_7=0x%08x\n", _reg_read(core_base + SC_CORE_SC_CORE_REG_7));
	uartlog("	 sc_h_str_mir=%d, sc_v_str_mir=%d, sc_cb_mode_en=%d\n",
		(val & SC_CORE_REG_SC_H_STR_MIR_MASK) >> SC_CORE_REG_SC_H_STR_MIR_OFFSET,
		(val & SC_CORE_REG_SC_V_STR_MIR_MASK) >> SC_CORE_REG_SC_V_STR_MIR_OFFSET,
		(val & SC_CORE_REG_SC_CB_MODE_EN_MASK) >> SC_CORE_REG_SC_CB_MODE_EN_OFFSET);

	val = _reg_read(core_base + SC_CORE_SC_CORE_REG_8);
	uartlog("SC_CORE_SC_CORE_REG_8=0x%08x\n", val);
	uartlog("	 h_sc_fac=0x%x\n",
		(val & SC_CORE_REG_H_SC_FAC_MASK) >> SC_CORE_REG_H_SC_FAC_OFFSET);

	val = _reg_read(core_base + SC_CORE_SC_CORE_REG_9);
	uartlog("SC_CORE_SC_CORE_REG_9=0x%08x\n", val);
	uartlog("	 v_sc_fac=0x%x\n",
		(val & SC_CORE_REG_V_SC_FAC_MASK) >> SC_CORE_REG_V_SC_FAC_OFFSET);

	val = _reg_read(core_base + SC_CORE_SC_CORE_REG_10);
	uartlog("SC_CORE_SC_CORE_REG_10=0x%08x\n", val);
	uartlog("	 sc_core_wd=%d, sc_core_ht=%d\n",
		(val & SC_CORE_REG_SC_CORE_WD_MASK) >> SC_CORE_REG_SC_CORE_WD_OFFSET,
		(val & SC_CORE_REG_SC_CORE_HT_MASK) >> SC_CORE_REG_SC_CORE_HT_OFFSET);

	uartlog("SC_CORE_SC_CORE_REG_11=0x%08x\n", _reg_read(core_base + SC_CORE_SC_CORE_REG_11));

	val =  _reg_read(core_base + SC_CORE_SC_CORE_REG_12);
	uartlog("SC_CORE_SC_CORE_REG_12=0x%08x\n", val);
	uartlog("	 h_ini_ph=0x%04x, v_ini_ph=0x%04x\n",
		(val & SC_CORE_REG_H_INI_PH_MASK) >> SC_CORE_REG_H_INI_PH_OFFSET,
		(val & SC_CORE_REG_V_INI_PH_MASK) >> SC_CORE_REG_V_INI_PH_OFFSET);

	uartlog("SC_CORE_BLT_CIR_REG_0=0x%08x\n", _reg_read(core_base + SC_CORE_BLT_CIR_REG_0));
	uartlog("SC_CORE_BLT_CIR_REG_1=0x%08x\n", _reg_read(core_base + SC_CORE_BLT_CIR_REG_1));
	uartlog("SC_CORE_BLT_CIR_REG_2=0x%08x\n", _reg_read(core_base + SC_CORE_BLT_CIR_REG_2));
	uartlog("SC_CORE_BLT_CIR_REG_3=0x%08x\n", _reg_read(core_base + SC_CORE_BLT_CIR_REG_3));
	uartlog("SC_CORE_BLT_CIR_REG_4=0x%08x\n", _reg_read(core_base + SC_CORE_BLT_CIR_REG_4));
	uartlog("SC_CORE_BLT_CIR_REG_5=0x%08x\n", _reg_read(core_base + SC_CORE_BLT_CIR_REG_5));
	uartlog("SC_CORE_BLT_CIR_REG_6=0x%08x\n", _reg_read(core_base + SC_CORE_BLT_CIR_REG_6));

	val = _reg_read(core_base + SC_CORE_PRI_M_REG_0);
	uartlog("SC_CORE_PRI_M_REG_0=0x%08x\n", val);
	uartlog("	 pri_m_en=%d, pri_m_pixel_md=%d, pri_force_alpha=%d\n",
		(val & SC_CORE_REG_PRI_M_EN_MASK) >> SC_CORE_REG_PRI_M_EN_OFFSET,
		(val & SC_CORE_REG_PRI_M_PIXEL_MD_MASK) >> SC_CORE_REG_PRI_M_PIXEL_MD_OFFSET,
		(val & SC_CORE_REG_PRI_FORCE_ALPHA_MASK) >> SC_CORE_REG_PRI_FORCE_ALPHA_OFFSET);
	uartlog("	 pri_mask_rgb332=%d, pri_blend_y_only=%d, pri_y2r_enable=%d\n",
		(val & SC_CORE_REG_PRI_MASK_RGB332_MASK) >> SC_CORE_REG_PRI_MASK_RGB332_OFFSET,
		(val & SC_CORE_REG_PRI_BLEND_Y_ONLY_MASK) >> SC_CORE_REG_PRI_BLEND_Y_ONLY_OFFSET,
		(val & SC_CORE_REG_PRI_Y2R_ENABLE_MASK) >> SC_CORE_REG_PRI_Y2R_ENABLE_OFFSET);
	uartlog(" pri_grid_size=%d, pri_m_fit_p=%d\n",
		(val & SC_CORE_REG_PRI_GRID_SIZE_MASK) >> SC_CORE_REG_PRI_GRID_SIZE_OFFSET,
		(val & SC_CORE_REG_PRI_M_FIT_P_MASK) >> SC_CORE_REG_PRI_M_FIT_P_OFFSET);

	uartlog("SC_CORE_PRI_M_REG_1=0x%08x\n", _reg_read(core_base + SC_CORE_PRI_M_REG_1));
	uartlog("SC_CORE_PRI_M_REG_2=0x%08x\n", _reg_read(core_base + SC_CORE_PRI_M_REG_2));
	uartlog("SC_CORE_PRI_M_REG_3=0x%08x\n", _reg_read(core_base + SC_CORE_PRI_M_REG_3));
	uartlog("SC_CORE_PRI_M_REG_4=0x%08x\n", _reg_read(core_base + SC_CORE_PRI_M_REG_4));
	uartlog("SC_CORE_PRI_M_REG_5=0x%08x\n", _reg_read(core_base + SC_CORE_PRI_M_REG_5));
	uartlog("SC_CORE_PRI_M_REG_6=0x%08x\n", _reg_read(core_base + SC_CORE_PRI_M_REG_6));
	uartlog("SC_CORE_PRI_M_REG_7=0x%08x\n", _reg_read(core_base + SC_CORE_PRI_M_REG_7));
}

void sclr_dump_odma_register(u8 inst)
{
	u32 val;
	u32 odma_base = reg_base + REG_SCL_ODMA_BASE(inst);

	if (inst >= SCL_MAX_INST)
		uartlog("sc(%d) out of range\n", inst);

	uartlog("sc(%d) odma base address=0x%08x\n", inst, odma_base);

	val = _reg_read(odma_base + SC_ODMA_ODMA_REG_00);
	uartlog("SC_ODMA_ODMA_REG_00=0x%08x\n", val);
	uartlog("	 blen=%d, fmt_sel=%d, hflip=%d, vflip=%d\n",
		(val & SC_ODMA_REG_DMA_BLEN_MASK) >> SC_ODMA_REG_DMA_BLEN_OFFSET,
		(val & SC_ODMA_REG_FMT_SEL_MASK) >> SC_ODMA_REG_FMT_SEL_OFFSET,
		(val & SC_ODMA_REG_SC_ODMA_HFLIP_MASK) >> SC_ODMA_REG_SC_ODMA_HFLIP_OFFSET,
		(val & SC_ODMA_REG_SC_ODMA_VFLIP_MASK) >> SC_ODMA_REG_SC_ODMA_VFLIP_OFFSET);
	uartlog("	 422_avg=%d, 420_avg=%d, c_round_mode=%d, bf16_en=%d\n",
		(val & SC_ODMA_REG_SC_422_AVG_MASK) >> SC_ODMA_REG_SC_422_AVG_OFFSET,
		(val & SC_ODMA_REG_SC_420_AVG_MASK) >> SC_ODMA_REG_SC_420_AVG_OFFSET,
		(val & SC_ODMA_REG_C_ROUND_MODE_MASK) >> SC_ODMA_REG_C_ROUND_MODE_OFFSET,
		(val & SC_ODMA_REG_BF16_EN_MASK) >> SC_ODMA_REG_BF16_EN_OFFSET);

	val = _reg_read(odma_base + SC_ODMA_ODMA_REG_01);
	uartlog("SC_ODMA_ODMA_REG_01=0x%08x\n", val);
	uartlog ("	  reg_dma_y_base_low_part=0x%08x\n", val);

	uartlog("SC_ODMA_ODMA_REG_02=0x%08x\n", _reg_read(odma_base + SC_ODMA_ODMA_REG_02));

	val = _reg_read(odma_base + SC_ODMA_ODMA_REG_03);
	uartlog("SC_ODMA_ODMA_REG_03=0x%08x\n", val);
	uartlog("	 reg_dma_u_base_low_part=0x%08x\n", val);

	uartlog("SC_ODMA_ODMA_REG_04=0x%08x\n", _reg_read(odma_base + SC_ODMA_ODMA_REG_04));

	val = _reg_read(odma_base + SC_ODMA_ODMA_REG_05);
	uartlog("SC_ODMA_ODMA_REG_05=0x%08x\n", val);
	uartlog("	 reg_dma_v_base_low_part0x%08x\n", val);

	uartlog("SC_ODMA_ODMA_REG_06=0x%08x\n", _reg_read(odma_base + SC_ODMA_ODMA_REG_06));

	val = _reg_read(odma_base + SC_ODMA_ODMA_REG_07);
	uartlog("SC_ODMA_ODMA_REG_07=0x%08x\n", val);
	uartlog("	 reg_dma_y_pitch=%d\n",
		(val & SC_ODMA_REG_DMA_Y_PITCH_MASK) >> SC_ODMA_REG_DMA_Y_PITCH_OFFSET);

	val = _reg_read(odma_base + SC_ODMA_ODMA_REG_08);
	uartlog("SC_ODMA_ODMA_REG_08=0x%08x\n", val);
	uartlog("	 reg_dma_c_pitch=%d\n",
		(val & SC_ODMA_REG_DMA_Y_PITCH_MASK) >> SC_ODMA_REG_DMA_Y_PITCH_OFFSET);

	val = _reg_read(odma_base + SC_ODMA_ODMA_REG_09);
	uartlog("SC_ODMA_ODMA_REG_09=0x%08x\n", val);
	uartlog("	 reg_dma_x_str=%d\n",
		(val & SC_ODMA_REG_DMA_X_STR_MASK) >> SC_ODMA_REG_DMA_X_STR_OFFSET);

	val = _reg_read(odma_base + SC_ODMA_ODMA_REG_10);
	uartlog("SC_ODMA_ODMA_REG_10=0x%08x\n", val);
	uartlog("	 reg_dma_y_str=%d\n",
		(val & SC_ODMA_REG_DMA_Y_STR_MASK) >> SC_ODMA_REG_DMA_Y_STR_OFFSET);

	val = _reg_read(odma_base + SC_ODMA_ODMA_REG_11);
	uartlog("SC_ODMA_ODMA_REG_11=0x%08x\n", val);
	uartlog("	 reg_dma_wd=%d\n",
		(val & SC_ODMA_REG_DMA_WD_MASK) >> SC_ODMA_REG_DMA_WD_OFFSET);

	val = _reg_read(odma_base + SC_ODMA_ODMA_REG_12);
	uartlog("SC_ODMA_ODMA_REG_12=0x%08x\n", val);
	uartlog("	 reg_dma_ht=%d\n",
		(val & SC_ODMA_REG_DMA_HT_MASK) >> SC_ODMA_REG_DMA_HT_OFFSET);

	uartlog("SC_ODMA_ODMA_REG_13=0x%08x\n", _reg_read(odma_base + SC_ODMA_ODMA_REG_13));
	uartlog("SC_ODMA_ODMA_REG_14=0x%08x\n", _reg_read(odma_base + SC_ODMA_ODMA_REG_14));
	uartlog("SC_ODMA_ODMA_REG_15=0x%08x\n", _reg_read(odma_base + SC_ODMA_ODMA_REG_15));
	uartlog("SC_ODMA_ODMA_REG_16=0x%08x\n", _reg_read(odma_base + SC_ODMA_ODMA_REG_16));

	val = _reg_read(odma_base + SC_ODMA_SB_REG_CTRL);
	uartlog("SC_ODMA_SB_REG_CTRL=0x%08x\n", val);
	uartlog("	 sb_mode=%d, sb_size=%d, sb_nb=%d, sb_full_nb=%d, sb_sw_wptr=%d\n",
		(val & SC_ODMA_REG_SB_MODE_MASK) >> SC_ODMA_REG_SB_MODE_OFFSET,
		(val & SC_ODMA_REG_SB_SIZE_MASK) >> SC_ODMA_REG_SB_SIZE_OFFSET,
		(val & SC_ODMA_REG_SB_NB_MASK) >> SC_ODMA_REG_SB_NB_OFFSET,
		(val & SC_ODMA_REG_SB_FULL_NB_MASK) >> SC_ODMA_REG_SB_FULL_NB_OFFSET,
		(val & SC_ODMA_REG_SB_SW_WPTR_MASK) >> SC_ODMA_REG_SB_SW_WPTR_OFFSET);

	val = _reg_read(odma_base + SC_ODMA_SB_REG_C_STAT);
	uartlog("SC_ODMA_SB_REG_C_STAT=0x%08x\n", val);
	uartlog("	 u_sb_wptr_ro=%d, u_sb_full=%d, u_sb_empty=%d, u_sb_dptr_ro=%d\n",
		(val & SC_ODMA_REG_U_SB_WPTR_RO_MASK) >> SC_ODMA_REG_U_SB_WPTR_RO_OFFSET,
		(val & SC_ODMA_REG_U_SB_FULL_MASK) >> SC_ODMA_REG_U_SB_FULL_OFFSET,
		(val & SC_ODMA_REG_U_SB_EMPTY_MASK) >> SC_ODMA_REG_U_SB_EMPTY_OFFSET,
		(val & SC_ODMA_REG_U_SB_DPTR_RO_MASK) >> SC_ODMA_REG_U_SB_DPTR_RO_OFFSET);
	uartlog("	 v_sb_wptr_ro=%d, v_sb_full=%d, v_sb_empty=%d, v_sb_dptr_ro=%d\n",
		(val & SC_ODMA_REG_V_SB_WPTR_RO_MASK) >> SC_ODMA_REG_V_SB_WPTR_RO_OFFSET,
		(val & SC_ODMA_REG_V_SB_FULL_MASK) >> SC_ODMA_REG_V_SB_FULL_OFFSET,
		(val & SC_ODMA_REG_V_SB_EMPTY_MASK) >> SC_ODMA_REG_V_SB_EMPTY_OFFSET,
		(val & SC_ODMA_REG_V_SB_DPTR_RO_MASK) >> SC_ODMA_REG_V_SB_DPTR_RO_OFFSET);

	uartlog("SC_ODMA_SB_REG_Y_STAT=0x%08x\n", _reg_read(odma_base + SC_ODMA_SB_REG_Y_STAT));
	uartlog("	 y_sb_wptr_ro=%d, y_sb_full=%d, y_sb_empty=%d, y_sb_dptr_ro=%d, sb_full=%d\n",
		(val & SC_ODMA_REG_Y_SB_WPTR_RO_MASK) >> SC_ODMA_REG_Y_SB_WPTR_RO_OFFSET,
		(val & SC_ODMA_REG_Y_SB_FULL_MASK) >> SC_ODMA_REG_Y_SB_FULL_OFFSET,
		(val & SC_ODMA_REG_Y_SB_EMPTY_MASK) >> SC_ODMA_REG_Y_SB_EMPTY_OFFSET,
		(val & SC_ODMA_REG_Y_SB_DPTR_RO_MASK) >> SC_ODMA_REG_Y_SB_DPTR_RO_OFFSET,
		(val & SC_ODMA_REG_SB_FULL_MASK) >> SC_ODMA_REG_SB_FULL_OFFSET);

	val = _reg_read(odma_base + SC_ODMA_SC_CSC_REG_00);
	uartlog("SC_ODMA_SC_CSC_REG_00=0x%08x\n", val);
	uartlog("	 csc_en=%d, csc_q_mode=%d, csc_q_drop=%d, hsv_en=%d, hsv_floor_en=%d\n",
		(val & SC_ODMA_REG_SC_CSC_EN_MASK) >> SC_ODMA_REG_SC_CSC_EN_OFFSET,
		(val & SC_ODMA_REG_SC_CSC_Q_MODE_MASK) >> SC_ODMA_REG_SC_CSC_Q_MODE_OFFSET,
		(val & SC_ODMA_REG_SC_CSC_Q_DROP_MASK) >> SC_ODMA_REG_SC_CSC_Q_DROP_OFFSET,
		(val & SC_ODMA_REG_SC_HSV_EN_MASK) >> SC_ODMA_REG_SC_HSV_EN_OFFSET,
		(val & SC_ODMA_REG_SC_HSV_FLOOR_EN_MASK) >> SC_ODMA_REG_SC_HSV_FLOOR_EN_OFFSET);
	uartlog("	 csc_bd_dis=%d, hsv_bd_dis=%d, bf16_bd_type=%d, csc_q_gain_mode=%d\n",
		(val & SC_ODMA_REG_SC_CSC_BD_DIS_MASK) >> SC_ODMA_REG_SC_CSC_BD_DIS_OFFSET,
		(val & SC_ODMA_REG_SC_HSV_BD_DIS_MASK) >> SC_ODMA_REG_SC_HSV_BD_DIS_OFFSET,
		(val & SC_ODMA_REG_BF16_BD_TYPE_MASK) >> SC_ODMA_REG_BF16_BD_TYPE_OFFSET,
		(val & SC_ODMA_REG_SC_CSC_Q_GAIN_MODE_MASK) >> SC_ODMA_REG_SC_CSC_Q_GAIN_MODE_OFFSET);

	uartlog("SC_ODMA_SC_CSC_REG_01=0x%08x\n", _reg_read(odma_base + SC_ODMA_SC_CSC_REG_01));
	uartlog("SC_ODMA_SC_CSC_REG_02=0x%08x\n", _reg_read(odma_base + SC_ODMA_SC_CSC_REG_02));
	uartlog("SC_ODMA_SC_CSC_REG_03=0x%08x\n", _reg_read(odma_base + SC_ODMA_SC_CSC_REG_03));
	uartlog("SC_ODMA_SC_CSC_REG_04=0x%08x\n", _reg_read(odma_base + SC_ODMA_SC_CSC_REG_04));
	uartlog("SC_ODMA_SC_CSC_REG_05=0x%08x\n", _reg_read(odma_base + SC_ODMA_SC_CSC_REG_05));
	uartlog("SC_ODMA_SC_CSC_REG_06=0x%08x\n", _reg_read(odma_base + SC_ODMA_SC_CSC_REG_06));
	uartlog("SC_ODMA_SC_CSC_REG_07=0x%08x\n", _reg_read(odma_base + SC_ODMA_SC_CSC_REG_07));
	uartlog("SC_ODMA_SC_CSC_REG_08=0x%08x\n", _reg_read(odma_base + SC_ODMA_SC_CSC_REG_08));
}



void sclr_dump_vgop0_register(u8 inst)
{
	// u32 vgop0_base = reg_base + REG_SCL_GOP0_BASE(inst);

	if (inst >= SCL_MAX_INST)
		uartlog("sc(%d) out of range\n", inst);

	uartlog("REG_SCL_GOP0_FMT=0x%08x\n", _reg_read(REG_SCL_GOP0_FMT(inst, 0)));
	uartlog("REG_SCL_GOP0_H_RANGE=0x%08x\n", _reg_read(REG_SCL_GOP0_H_RANGE(inst, 0)));
	uartlog("REG_SCL_GOP0_V_RANGE=0x%08x\n", _reg_read(REG_SCL_GOP0_V_RANGE(inst, 0)));
	uartlog("REG_SCL_GOP0_ADDR_L=0x%08x\n", _reg_read(REG_SCL_GOP0_ADDR_L(inst, 0)));
	uartlog("REG_SCL_GOP0_ADDR_H=0x%08x\n", _reg_read(REG_SCL_GOP0_ADDR_H(inst, 0)));
	uartlog("REG_SCL_GOP0_CROP_PITCH=0x%08x\n", _reg_read(REG_SCL_GOP0_CROP_PITCH(inst, 0)));
	uartlog("REG_SCL_GOP0_SIZE=0x%08x\n", _reg_read(REG_SCL_GOP0_SIZE(inst, 0)));
	uartlog("REG_SCL_GOP0_CFG=0x%08x\n", _reg_read(REG_SCL_GOP0_CFG(inst)));
	uartlog("REG_SCL_GOP0_256LUT0=0x%08x\n", _reg_read(REG_SCL_GOP0_256LUT0(inst)));
	uartlog("REG_SCL_GOP0_256LUT1=0x%08x\n", _reg_read(REG_SCL_GOP0_256LUT1(inst)));
	uartlog("REG_SCL_GOP0_COLORKEY=0x%08x\n", _reg_read(REG_SCL_GOP0_COLORKEY(inst)));

	uartlog("REG_SCL_GOP0_FONTCOLOR=0x%08x\n", _reg_read(REG_SCL_GOP0_FONTCOLOR(inst)));

	uartlog("REG_SCL_GOP0_FONTCOLOR_INV=0x%08x\n", _reg_read(REG_SCL_GOP0_FONTCOLOR_INV(inst)));

	uartlog("REG_SCL_GOP0_BUSY=0x%08x\n", _reg_read(REG_SCL_GOP0_BUSY(inst)));

	uartlog("REG_SCL_GOP0_FONTBOX_CTRL=0x%08x\n", _reg_read(REG_SCL_GOP0_FONTBOX_CTRL(inst)));
	uartlog("REG_SCL_GOP0_FONTBOX_CFG=0x%08x\n", _reg_read(REG_SCL_GOP0_FONTBOX_CFG(inst, 0)));
	uartlog("REG_SCL_GOP0_FONTBOX_INIT=0x%08x\n", _reg_read(REG_SCL_GOP0_FONTBOX_INIT(inst, 0)));
	uartlog("REG_SCL_GOP0_FONTBOX_REC=0x%08x\n", _reg_read(REG_SCL_GOP0_FONTBOX_REC(inst, 0)));
	uartlog("REG_SCL_GOP0_BW_LIMIT=0x%08x\n", _reg_read(REG_SCL_GOP0_BW_LIMIT(inst)));
	uartlog("REG_SCL_GOP0_LV2=0x%08x\n", _reg_read(REG_SCL_GOP0_LV2(inst)));
	uartlog("REG_SCL_GOP0_LV2_PARA=0x%08x\n", _reg_read(REG_SCL_GOP0_LV2_PARA(inst)));
	uartlog("REG_SCL_GOP0_DEC_CTRL=0x%08x\n", _reg_read(REG_SCL_GOP0_DEC_CTRL(inst)));
	uartlog("REG_SCL_GOP0_DEC_DEBUG=0x%08x\n", _reg_read(REG_SCL_GOP0_DEC_DEBUG(inst)));
	uartlog("REG_SCL_GOP0_16LUT=0x%08x\n", _reg_read(REG_SCL_GOP0_16LUT(inst, 0)));
}

