/* Copyright (c) 2012-2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/spmi.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/platform_device.h>

#define KELVINMIL_DEGMIL	273160
#define QPNP_VADC_LDO_VOLTAGE_MIN	1800000
#define QPNP_VADC_LDO_VOLTAGE_MAX	1800000
#define QPNP_VADC_OK_VOLTAGE_MIN	1000000
#define QPNP_VADC_OK_VOLTAGE_MAX	1000000
#define PMI_CHG_SCALE_1		-138890
#define PMI_CHG_SCALE_2		391750000000
#define QPNP_VADC_HC_VREF_CODE		0x4000
#define QPNP_VADC_HC_VDD_REFERENCE_MV	1875
/* Clamp negative ADC code to 0 */
#define QPNP_VADC_HC_MAX_CODE		0x7FFF

/* Units for temperature below (on x axis) is in 0.1DegC as
   required by the battery driver. Note the resolution used
   here to compute the table was done for DegC to milli-volts.
   In consideration to limit the size of the table for the given
   temperature range below, the result is linearly interpolated
   and provided to the battery driver in the units desired for
   their framework which is 0.1DegC. True resolution of 0.1DegC
   will result in the below table size to increase by 10 times */

static const struct qpnp_vadc_map_pt adcmap_btm_threshold[] = {
	{-300,	1652},
	{-200,	1569},
	{-100,	1457},
	{-90,	1444},
	{-80,	1431},
	{-70,	1418},
	{-60,	1404},
	{-50,	1390},
	{-40,	1376},
	{-30,	1362},
	{-20,	1346},
	{-10,	1332},
	{0,	1317},
	{10,	1302},
	{20,	1286},
	{30,	1271},
	{40,	1255},
	{50,	1239},
	{60,	1223},
	{70,	1206},
	{80,	1190},
	{90,	1173},
	{100,	1156},
	{110,	1139},
	{120,	1123},
	{130,	1105},
	{140,	1088},
	{150,	1071},
	{160,	1054},
	{170,	1037},
	{180,	1020},
	{190,	1002},
	{200,	985},
	{210,	968},
	{220,	951},
	{230,	934},
	{240,	917},
	{250,	900},
	{260,	883},
	{270,	866},
	{280,	850},
	{290,	833},
	{300,	817},
	{310,	801},
	{320,	785},
	{330,	769},
	{340,	753},
	{350,	738},
	{360,	722},
	{370,	707},
	{380,	692},
	{390,	677},
	{400,	663},
	{410,	648},
	{420,	634},
	{430,	620},
	{440,	606},
	{450,	593},
	{460,	580},
	{470,	566},
	{480,	554},
	{490,	541},
	{500,	529},
	{510,	516},
	{520,	505},
	{530,	493},
	{540,	481},
	{550,	470},
	{560,	459},
	{570,	548},
	{580,	438},
	{590,	428},
	{600,	417},
	{610,	408},
	{620,	398},
	{630,	388},
	{640,	379},
	{650,	370},
	{700,	328},
	{750,	290},
	{790,	264},
};

static const struct qpnp_vadc_map_pt adcmap_qrd_btm_threshold[] = {
	{-200,	1540},
	{-180,	1517},
	{-160,	1492},
	{-140,	1467},
	{-120,	1440},
	{-100,	1412},
	{-80,	1383},
	{-60,	1353},
	{-40,	1323},
	{-20,	1292},
	{0,	1260},
	{20,	1228},
	{40,	1196},
	{60,	1163},
	{80,	1131},
	{100,	1098},
	{120,	1066},
	{140,	1034},
	{160,	1002},
	{180,	971},
	{200,	941},
	{220,	911},
	{240,	882},
	{260,	854},
	{280,	826},
	{300,	800},
	{320,	774},
	{340,	749},
	{360,	726},
	{380,	703},
	{400,	681},
	{420,	660},
	{440,	640},
	{460,	621},
	{480,	602},
	{500,	585},
	{520,	568},
	{540,	552},
	{560,	537},
	{580,	523},
	{600,	510},
	{620,	497},
	{640,	485},
	{660,	473},
	{680,	462},
	{700,	452},
	{720,	442},
	{740,	433},
	{760,	424},
	{780,	416},
	{800,	408},
};

static const struct qpnp_vadc_map_pt adcmap_qrd_skuaa_btm_threshold[] = {
	{-200,	1476},
	{-180,	1450},
	{-160,	1422},
	{-140,	1394},
	{-120,	1365},
	{-100,	1336},
	{-80,	1306},
	{-60,	1276},
	{-40,	1246},
	{-20,	1216},
	{0,	1185},
	{20,	1155},
	{40,	1126},
	{60,	1096},
	{80,	1068},
	{100,	1040},
	{120,	1012},
	{140,	986},
	{160,	960},
	{180,	935},
	{200,	911},
	{220,	888},
	{240,	866},
	{260,	844},
	{280,	824},
	{300,	805},
	{320,	786},
	{340,	769},
	{360,	752},
	{380,	737},
	{400,	722},
	{420,	707},
	{440,	694},
	{460,	681},
	{480,	669},
	{500,	658},
	{520,	648},
	{540,	637},
	{560,	628},
	{580,	619},
	{600,	611},
	{620,	603},
	{640,	595},
	{660,	588},
	{680,	582},
	{700,	575},
	{720,	569},
	{740,	564},
	{760,	559},
	{780,	554},
	{800,	549},
};

static const struct qpnp_vadc_map_pt adcmap_qrd_skug_btm_threshold[] = {
	{-200,	1338},
	{-180,	1307},
	{-160,	1276},
	{-140,	1244},
	{-120,	1213},
	{-100,	1182},
	{-80,	1151},
	{-60,	1121},
	{-40,	1092},
	{-20,	1063},
	{0,	1035},
	{20,	1008},
	{40,	982},
	{60,	957},
	{80,	933},
	{100,	910},
	{120,	889},
	{140,	868},
	{160,	848},
	{180,	830},
	{200,	812},
	{220,	795},
	{240,	780},
	{260,	765},
	{280,	751},
	{300,	738},
	{320,	726},
	{340,	714},
	{360,	704},
	{380,	694},
	{400,	684},
	{420,	675},
	{440,	667},
	{460,	659},
	{480,	652},
	{500,	645},
	{520,	639},
	{540,	633},
	{560,	627},
	{580,	622},
	{600,	617},
	{620,	613},
	{640,	608},
	{660,	604},
	{680,	600},
	{700,	597},
	{720,	593},
	{740,	590},
	{760,	587},
	{780,	585},
	{800,	582},
};

static const struct qpnp_vadc_map_pt adcmap_qrd_skuh_btm_threshold[] = {
	{-200,	1531},
	{-180,	1508},
	{-160,	1483},
	{-140,	1458},
	{-120,	1432},
	{-100,	1404},
	{-80,	1377},
	{-60,	1348},
	{-40,	1319},
	{-20,	1290},
	{0,	1260},
	{20,	1230},
	{40,	1200},
	{60,	1171},
	{80,	1141},
	{100,	1112},
	{120,	1083},
	{140,	1055},
	{160,	1027},
	{180,	1000},
	{200,	973},
	{220,	948},
	{240,	923},
	{260,	899},
	{280,	876},
	{300,	854},
	{320,	832},
	{340,	812},
	{360,	792},
	{380,	774},
	{400,	756},
	{420,	739},
	{440,	723},
	{460,	707},
	{480,	692},
	{500,	679},
	{520,	665},
	{540,	653},
	{560,	641},
	{580,	630},
	{600,	619},
	{620,	609},
	{640,	600},
	{660,	591},
	{680,	583},
	{700,	575},
	{720,	567},
	{740,	560},
	{760,	553},
	{780,	547},
	{800,	541},
	{820,	535},
	{840,	530},
	{860,	524},
	{880,	520},
};

static const struct qpnp_vadc_map_pt adcmap_qrd_skut1_btm_threshold[] = {
	{-400,	1759},
	{-350,	1742},
	{-300,	1720},
	{-250,	1691},
	{-200,	1654},
	{-150,	1619},
	{-100,	1556},
	{-50,	1493},
	{0,	1422},
	{50,	1345},
	{100,	1264},
	{150,	1180},
	{200,	1097},
	{250,	1017},
	{300,	942},
	{350,	873},
	{400,	810},
	{450,	754},
	{500,	706},
	{550,	664},
	{600,	627},
	{650,	596},
	{700,	570},
	{750,	547},
	{800,	528},
	{850,	512},
	{900,	499},
	{950,	487},
	{1000,	477},
};

static const struct qpnp_vadc_map_pt adcmap_qrd_skuc_btm_threshold[] = {
	{-200,	1539},
	{-180,	1515},
	{-160,	1491},
	{-140,	1465},
	{-120,	1438},
	{-100,	1410},
	{-80,	1381},
	{-60,	1352},
	{-40,	1322},
	{-20,	1291},
	{0,	1260},
	{20,	1229},
	{40,	1197},
	{60,	1166},
	{80,	1134},
	{100,	1103},
	{120,	1072},
	{140,	1042},
	{160,	1012},
	{180,	982},
	{200,	954},
	{220,	926},
	{240,	899},
	{260,	873},
	{280,	847},
	{300,	823},
	{320,	800},
	{340,	777},
	{360,	756},
	{380,	735},
	{400,	715},
	{420,	696},
	{440,	679},
	{460,	662},
	{480,	645},
	{500,	630},
	{520,	615},
	{540,	602},
	{560,	588},
	{580,	576},
	{600,	564},
	{620,	553},
	{640,	543},
	{660,	533},
	{680,	523},
	{700,	515},
	{720,	506},
	{740,	498},
	{760,	491},
	{780,	484},
	{800,	477},
};

static const struct qpnp_vadc_map_pt adcmap_qrd_skue_btm_threshold[] = {
	{-200,	1385},
	{-180,	1353},
	{-160,	1320},
	{-140,	1287},
	{-120,	1253},
	{-100,	1218},
	{-80,	1184},
	{-60,	1149},
	{-40,	1115},
	{-20,	1080},
	{0,	1046},
	{20,	1013},
	{40,	980},
	{60,	948},
	{80,	917},
	{100,	887},
	{120,	858},
	{140,	830},
	{160,	803},
	{180,	777},
	{200,	752},
	{220,	729},
	{240,	706},
	{260,	685},
	{280,	664},
	{300,	645},
	{320,	626},
	{340,	609},
	{360,	593},
	{380,	577},
	{400,	563},
	{420,	549},
	{440,	536},
	{460,	524},
	{480,	512},
	{500,	501},
	{520,	491},
	{540,	481},
	{560,	472},
	{580,	464},
	{600,	456},
	{620,	448},
	{640,	441},
	{660,	435},
	{680,	428},
	{700,	423},
	{720,	417},
	{740,	412},
	{760,	407},
	{780,	402},
	{800,	398},
};

/* Voltage to temperature */
static const struct qpnp_vadc_map_pt adcmap_100k_104ef_104fb[] = {
	{1758,	-40},
	{1742,	-35},
	{1719,	-30},
	{1691,	-25},
	{1654,	-20},
	{1608,	-15},
	{1551,	-10},
	{1483,	-5},
	{1404,	0},
	{1315,	5},
	{1218,	10},
	{1114,	15},
	{1007,	20},
	{900,	25},
	{795,	30},
	{696,	35},
	{605,	40},
	{522,	45},
	{448,	50},
	{383,	55},
	{327,	60},
	{278,	65},
	{237,	70},
	{202,	75},
	{172,	80},
	{146,	85},
	{125,	90},
	{107,	95},
	{92,	100},
	{79,	105},
	{68,	110},
	{59,	115},
	{51,	120},
	{44,	125}
};

/* Voltage to temperature */
static const struct qpnp_vadc_map_pt adcmap_150k_104ef_104fb[] = {
	{1738,	-40},
	{1714,	-35},
	{1682,	-30},
	{1641,	-25},
	{1589,	-20},
	{1526,	-15},
	{1451,	-10},
	{1363,	-5},
	{1266,	0},
	{1159,	5},
	{1048,	10},
	{936,	15},
	{825,	20},
	{720,	25},
	{622,	30},
	{533,	35},
	{454,	40},
	{385,	45},
	{326,	50},
	{275,	55},
	{232,	60},
	{195,	65},
	{165,	70},
	{139,	75},
	{118,	80},
	{100,	85},
	{85,	90},
	{73,	95},
	{62,	100},
	{53,	105},
	{46,	110},
	{40,	115},
	{34,	120},
	{30,	125}
};

static const struct qpnp_vadc_map_pt adcmap_smb_batt_therm[] = {
	{-300,	1625},
	{-200,	1515},
	{-100,	1368},
	{0,	1192},
	{10,	1173},
	{20,	1154},
	{30,	1135},
	{40,	1116},
	{50,	1097},
	{60,	1078},
	{70,	1059},
	{80,	1040},
	{90,	1020},
	{100,	1001},
	{110,	982},
	{120,	963},
	{130,	944},
	{140,	925},
	{150,	907},
	{160,	888},
	{170,	870},
	{180,	851},
	{190,	833},
	{200,	815},
	{210,	797},
	{220,	780},
	{230,	762},
	{240,	745},
	{250,	728},
	{260,	711},
	{270,	695},
	{280,	679},
	{290,	663},
	{300,	647},
	{310,	632},
	{320,	616},
	{330,	602},
	{340,	587},
	{350,	573},
	{360,	559},
	{370,	545},
	{380,	531},
	{390,	518},
	{400,	505},
	{410,	492},
	{420,	480},
	{430,	465},
	{440,	456},
	{450,	445},
	{460,	433},
	{470,	422},
	{480,	412},
	{490,	401},
	{500,	391},
	{510,	381},
	{520,	371},
	{530,	362},
	{540,	352},
	{550,	343},
	{560,	335},
	{570,	326},
	{580,	318},
	{590,	309},
	{600,	302},
	{610,	294},
	{620,	286},
	{630,	279},
	{640,	272},
	{650,	265},
	{660,	258},
	{670,	252},
	{680,	245},
	{690,	239},
	{700,	233},
	{710,	227},
	{720,	221},
	{730,	216},
	{740,	211},
	{750,	205},
	{760,	200},
	{770,	195},
	{780,	190},
	{790,	186}
};

/* Voltage to temperature */
static const struct qpnp_vadc_map_pt adcmap_ncp03wf683[] = {
	{1742,	-40},
	{1718,	-35},
	{1687,	-30},
	{1647,	-25},
	{1596,	-20},
	{1534,	-15},
	{1459,	-10},
	{1372,	-5},
	{1275,	0},
	{1169,	5},
	{1058,	10},
	{945,	15},
	{834,	20},
	{729,	25},
	{630,	30},
	{541,	35},
	{461,	40},
	{392,	45},
	{332,	50},
	{280,	55},
	{236,	60},
	{199,	65},
	{169,	70},
	{142,	75},
	{121,	80},
	{102,	85},
	{87,	90},
	{74,	95},
	{64,	100},
	{55,	105},
	{47,	110},
	{40,	115},
	{35,	120},
	{30,	125}
};

/*
 * Voltage to temperature table for 100k pull up for NTCG104EF104 with
 * 1.875V reference.
 */
static const struct qpnp_vadc_map_pt adcmap_100k_104ef_104fb_1875_vref[] = {
	{ 1831,	-40 },
	{ 1814,	-35 },
	{ 1791,	-30 },
	{ 1761,	-25 },
	{ 1723,	-20 },
	{ 1675,	-15 },
	{ 1616,	-10 },
	{ 1545,	-5 },
	{ 1463,	0 },
	{ 1370,	5 },
	{ 1268,	10 },
	{ 1160,	15 },
	{ 1049,	20 },
	{ 937,	25 },
	{ 828,	30 },
	{ 726,	35 },
	{ 630,	40 },
	{ 544,	45 },
	{ 467,	50 },
	{ 399,	55 },
	{ 340,	60 },
	{ 290,	65 },
	{ 247,	70 },
	{ 209,	75 },
	{ 179,	80 },
	{ 153,	85 },
	{ 130,	90 },
	{ 112,	95 },
	{ 96,	100 },
	{ 82,	105 },
	{ 71,	110 },
	{ 62,	115 },
	{ 53,	120 },
	{ 46,	125 },
};

static int32_t qpnp_adc_map_voltage_temp(const struct qpnp_vadc_map_pt *pts,
		uint32_t tablesize, int32_t input, int64_t *output)
{
	bool descending = 1;
	uint32_t i = 0;

	if (pts == NULL)
		return -EINVAL;

	/* Check if table is descending or ascending */
	if (tablesize > 1) {
		if (pts[0].x < pts[1].x)
			descending = 0;
	}

	while (i < tablesize) {
		if ((descending == 1) && (pts[i].x < input)) {
			/* table entry is less than measured
				value and table is descending, stop */
			break;
		} else if ((descending == 0) &&
				(pts[i].x > input)) {
			/* table entry is greater than measured
				value and table is ascending, stop */
			break;
		} else {
			i++;
		}
	}

	if (i == 0)
		*output = pts[0].y;
	else if (i == tablesize)
		*output = pts[tablesize-1].y;
	else {
		/* result is between search_index and search_index-1 */
		/* interpolate linearly */
		*output = (((int32_t) ((pts[i].y - pts[i-1].y)*
			(input - pts[i-1].x))/
			(pts[i].x - pts[i-1].x))+
			pts[i-1].y);
	}

	return 0;
}

static int32_t qpnp_adc_map_temp_voltage(const struct qpnp_vadc_map_pt *pts,
		uint32_t tablesize, int32_t input, int64_t *output)
{
	bool descending = 1;
	uint32_t i = 0;

	if (pts == NULL)
		return -EINVAL;

	/* Check if table is descending or ascending */
	if (tablesize > 1) {
		if (pts[0].y < pts[1].y)
			descending = 0;
	}

	while (i < tablesize) {
		if ((descending == 1) && (pts[i].y < input)) {
			/* table entry is less than measured
				value and table is descending, stop */
			break;
		} else if ((descending == 0) && (pts[i].y > input)) {
			/* table entry is greater than measured
				value and table is ascending, stop */
			break;
		} else {
			i++;
		}
	}

	if (i == 0) {
		*output = pts[0].x;
	} else if (i == tablesize) {
		*output = pts[tablesize-1].x;
	} else {
		/* result is between search_index and search_index-1 */
		/* interpolate linearly */
		*output = (((int32_t) ((pts[i].x - pts[i-1].x)*
			(input - pts[i-1].y))/
			(pts[i].y - pts[i-1].y))+
			pts[i-1].x);
	}

	return 0;
}

static void qpnp_adc_scale_with_calib_param(int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		int64_t *scale_voltage)
{
	*scale_voltage = (adc_code -
		chan_properties->adc_graph[chan_properties->calib_type].adc_gnd)
		* chan_properties->adc_graph[chan_properties->calib_type].dx;
	*scale_voltage = div64_s64(*scale_voltage,
		chan_properties->adc_graph[chan_properties->calib_type].dy);

	if (chan_properties->calib_type == CALIB_ABSOLUTE)
		*scale_voltage +=
		chan_properties->adc_graph[chan_properties->calib_type].dx;

	if (*scale_voltage < 0)
		*scale_voltage = 0;
}

int32_t qpnp_adc_scale_pmic_therm(struct qpnp_vadc_chip *vadc,
		int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		struct qpnp_vadc_result *adc_chan_result)
{
	int64_t pmic_voltage = 0;

	if (!chan_properties || !chan_properties->offset_gain_numerator ||
		!chan_properties->offset_gain_denominator || !adc_properties
		|| !adc_chan_result)
		return -EINVAL;

	if (adc_properties->adc_hc) {
		/* (ADC code * vref_vadc (1.875V)) / 0x4000 */
		if (adc_code > QPNP_VADC_HC_MAX_CODE)
			adc_code = 0;
		pmic_voltage = (int64_t) adc_code;
		pmic_voltage *= (int64_t) (adc_properties->adc_vdd_reference
							* 1000);
		pmic_voltage = div64_s64(pmic_voltage,
					QPNP_VADC_HC_VREF_CODE);
	} else {
		if (!chan_properties->adc_graph[CALIB_ABSOLUTE].dy)
			return -EINVAL;
		qpnp_adc_scale_with_calib_param(adc_code, adc_properties,
					chan_properties, &pmic_voltage);
	}

	if (pmic_voltage > 0) {
		/* 2mV/K */
		adc_chan_result->measurement = pmic_voltage*
			chan_properties->offset_gain_denominator;

		do_div(adc_chan_result->measurement,
			chan_properties->offset_gain_numerator * 2);
	} else
		adc_chan_result->measurement = 0;

	/* Change to .001 deg C */
	adc_chan_result->measurement -= KELVINMIL_DEGMIL;
	adc_chan_result->physical = (int32_t) adc_chan_result->measurement;

	return 0;
}
EXPORT_SYMBOL(qpnp_adc_scale_pmic_therm);

int32_t qpnp_adc_scale_millidegc_pmic_voltage_thr(struct qpnp_vadc_chip *chip,
		struct qpnp_adc_tm_btm_param *param,
		uint32_t *low_threshold, uint32_t *high_threshold)
{
	struct qpnp_vadc_linear_graph btm_param;
	int64_t low_output = 0, high_output = 0;
	int rc = 0, sign = 0;

	/* Convert to Kelvin and account for voltage to be written as 2mV/K */
	low_output = (param->low_temp + KELVINMIL_DEGMIL) * 2;
	/* Convert to Kelvin and account for voltage to be written as 2mV/K */
	high_output = (param->high_temp + KELVINMIL_DEGMIL) * 2;

	if (param->adc_tm_hc) {
		low_output *= QPNP_VADC_HC_VREF_CODE;
		do_div(low_output, (QPNP_VADC_HC_VDD_REFERENCE_MV * 1000));
		high_output *= QPNP_VADC_HC_VREF_CODE;
		do_div(high_output, (QPNP_VADC_HC_VDD_REFERENCE_MV * 1000));
	} else {
		rc = qpnp_get_vadc_gain_and_offset(chip, &btm_param,
							CALIB_ABSOLUTE);
		if (rc < 0) {
			pr_err("Could not acquire gain and offset\n");
			return rc;
		}

		/* Convert to voltage threshold */
		low_output = (low_output - QPNP_ADC_625_UV) * btm_param.dy;
		if (low_output < 0) {
			sign = 1;
			low_output = -low_output;
		}
		do_div(low_output, QPNP_ADC_625_UV);
		if (sign)
			low_output = -low_output;
		low_output += btm_param.adc_gnd;

		sign = 0;
		/* Convert to voltage threshold */
		high_output = (high_output - QPNP_ADC_625_UV) * btm_param.dy;
		if (high_output < 0) {
			sign = 1;
			high_output = -high_output;
		}
		do_div(high_output, QPNP_ADC_625_UV);
		if (sign)
			high_output = -high_output;
		high_output += btm_param.adc_gnd;
	}

	*low_threshold = (uint32_t) low_output;
	*high_threshold = (uint32_t) high_output;

	pr_debug("high_temp:%d, low_temp:%d\n", param->high_temp,
				param->low_temp);
	pr_debug("adc_code_high:%x, adc_code_low:%x\n", *high_threshold,
				*low_threshold);

	return 0;
}
EXPORT_SYMBOL(qpnp_adc_scale_millidegc_pmic_voltage_thr);

/* Scales the ADC code to degC using the mapping
 * table for the XO thermistor.
 */
int32_t qpnp_adc_tdkntcg_therm(struct qpnp_vadc_chip *chip,
		int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		struct qpnp_vadc_result *adc_chan_result)
{
	int64_t xo_thm_voltage = 0;

	if (!chan_properties || !chan_properties->offset_gain_numerator ||
		!chan_properties->offset_gain_denominator || !adc_properties
		|| !adc_chan_result)
		return -EINVAL;

	if (adc_properties->adc_hc) {
		/* (ADC code * vref_vadc (1.875V) * 1000) / (0x4000 * 1000) */
		if (adc_code > QPNP_VADC_HC_MAX_CODE)
			adc_code = 0;
		xo_thm_voltage = (int64_t) adc_code;
		xo_thm_voltage *= (int64_t) (adc_properties->adc_vdd_reference
							* 1000);
		xo_thm_voltage = div64_s64(xo_thm_voltage,
					QPNP_VADC_HC_VREF_CODE * 1000);
		qpnp_adc_map_voltage_temp(adcmap_100k_104ef_104fb_1875_vref,
			ARRAY_SIZE(adcmap_100k_104ef_104fb_1875_vref),
			xo_thm_voltage, &adc_chan_result->physical);
	} else {
		qpnp_adc_scale_with_calib_param(adc_code,
			adc_properties, chan_properties, &xo_thm_voltage);

		if (chan_properties->calib_type == CALIB_ABSOLUTE)
			do_div(xo_thm_voltage, 1000);

		qpnp_adc_map_voltage_temp(adcmap_100k_104ef_104fb,
			ARRAY_SIZE(adcmap_100k_104ef_104fb),
			xo_thm_voltage, &adc_chan_result->physical);
	}

	return 0;
}
EXPORT_SYMBOL(qpnp_adc_tdkntcg_therm);

int32_t qpnp_adc_scale_batt_therm(struct qpnp_vadc_chip *chip,
		int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		struct qpnp_vadc_result *adc_chan_result)
{
	int64_t bat_voltage = 0;

	qpnp_adc_scale_with_calib_param(adc_code,
			adc_properties, chan_properties, &bat_voltage);

	adc_chan_result->measurement = bat_voltage;
	pr_err("temp_debug----bat_voltage = %d\n",(int)bat_voltage);
	return qpnp_adc_map_temp_voltage(
			adcmap_btm_threshold,
			ARRAY_SIZE(adcmap_btm_threshold),
			bat_voltage,
			&adc_chan_result->physical);
}
EXPORT_SYMBOL(qpnp_adc_scale_batt_therm);

int32_t qpnp_adc_scale_qrd_batt_therm(struct qpnp_vadc_chip *chip,
		int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		struct qpnp_vadc_result *adc_chan_result)
{
	int64_t bat_voltage = 0;

	qpnp_adc_scale_with_calib_param(adc_code,
			adc_properties, chan_properties, &bat_voltage);

	adc_chan_result->measurement = bat_voltage;

	return qpnp_adc_map_temp_voltage(
			adcmap_qrd_btm_threshold,
			ARRAY_SIZE(adcmap_qrd_btm_threshold),
			bat_voltage,
			&adc_chan_result->physical);
}
EXPORT_SYMBOL(qpnp_adc_scale_qrd_batt_therm);

int32_t qpnp_adc_scale_qrd_skuaa_batt_therm(struct qpnp_vadc_chip *chip,
		int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		struct qpnp_vadc_result *adc_chan_result)
{
	int64_t bat_voltage = 0;

	qpnp_adc_scale_with_calib_param(adc_code,
			adc_properties, chan_properties, &bat_voltage);

	adc_chan_result->measurement = bat_voltage;

	return qpnp_adc_map_temp_voltage(
			adcmap_qrd_skuaa_btm_threshold,
			ARRAY_SIZE(adcmap_qrd_skuaa_btm_threshold),
			bat_voltage,
			&adc_chan_result->physical);
}
EXPORT_SYMBOL(qpnp_adc_scale_qrd_skuaa_batt_therm);

int32_t qpnp_adc_scale_qrd_skug_batt_therm(struct qpnp_vadc_chip *chip,
		int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		struct qpnp_vadc_result *adc_chan_result)
{
	int64_t bat_voltage = 0;

	qpnp_adc_scale_with_calib_param(adc_code,
			adc_properties, chan_properties, &bat_voltage);
	adc_chan_result->measurement = bat_voltage;

	return qpnp_adc_map_temp_voltage(
			adcmap_qrd_skug_btm_threshold,
			ARRAY_SIZE(adcmap_qrd_skug_btm_threshold),
			bat_voltage,
			&adc_chan_result->physical);
}
EXPORT_SYMBOL(qpnp_adc_scale_qrd_skug_batt_therm);

int32_t qpnp_adc_scale_qrd_skuh_batt_therm(struct qpnp_vadc_chip *chip,
		int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		struct qpnp_vadc_result *adc_chan_result)
{
	int64_t bat_voltage = 0;

	qpnp_adc_scale_with_calib_param(adc_code,
			adc_properties, chan_properties, &bat_voltage);

	return qpnp_adc_map_temp_voltage(
			adcmap_qrd_skuh_btm_threshold,
			ARRAY_SIZE(adcmap_qrd_skuh_btm_threshold),
			bat_voltage,
			&adc_chan_result->physical);
}
EXPORT_SYMBOL(qpnp_adc_scale_qrd_skuh_batt_therm);

int32_t qpnp_adc_scale_qrd_skut1_batt_therm(struct qpnp_vadc_chip *chip,
		int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		struct qpnp_vadc_result *adc_chan_result)
{
	int64_t bat_voltage = 0;

	qpnp_adc_scale_with_calib_param(adc_code,
			adc_properties, chan_properties, &bat_voltage);

	return qpnp_adc_map_temp_voltage(
			adcmap_qrd_skut1_btm_threshold,
			ARRAY_SIZE(adcmap_qrd_skut1_btm_threshold),
			bat_voltage,
			&adc_chan_result->physical);
}
EXPORT_SYMBOL(qpnp_adc_scale_qrd_skut1_batt_therm);

int32_t qpnp_adc_scale_qrd_skuc_batt_therm(struct qpnp_vadc_chip *chip,
			int32_t adc_code,
			const struct qpnp_adc_properties *adc_properties,
			const struct qpnp_vadc_chan_properties *chan_properties,
			struct qpnp_vadc_result *adc_chan_result)
{
	int64_t bat_voltage = 0;

	qpnp_adc_scale_with_calib_param(adc_code,
			adc_properties, chan_properties, &bat_voltage);

	return qpnp_adc_map_temp_voltage(
			adcmap_qrd_skuc_btm_threshold,
			ARRAY_SIZE(adcmap_qrd_skuc_btm_threshold),
			bat_voltage,
			&adc_chan_result->physical);
}
EXPORT_SYMBOL(qpnp_adc_scale_qrd_skuc_batt_therm);

int32_t qpnp_adc_scale_qrd_skue_batt_therm(struct qpnp_vadc_chip *chip,
			int32_t adc_code,
			const struct qpnp_adc_properties *adc_properties,
			const struct qpnp_vadc_chan_properties *chan_properties,
			struct qpnp_vadc_result *adc_chan_result)
{
	int64_t bat_voltage = 0;

	qpnp_adc_scale_with_calib_param(adc_code,
			adc_properties, chan_properties, &bat_voltage);

	return qpnp_adc_map_temp_voltage(
			adcmap_qrd_skue_btm_threshold,
			ARRAY_SIZE(adcmap_qrd_skue_btm_threshold),
			bat_voltage,
			&adc_chan_result->physical);
}
EXPORT_SYMBOL(qpnp_adc_scale_qrd_skue_batt_therm);

int32_t qpnp_adc_scale_smb_batt_therm(struct qpnp_vadc_chip *chip,
		int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		struct qpnp_vadc_result *adc_chan_result)
{
	int64_t bat_voltage = 0;

	qpnp_adc_scale_with_calib_param(adc_code,
			adc_properties, chan_properties, &bat_voltage);

	return qpnp_adc_map_temp_voltage(
			adcmap_smb_batt_therm,
			ARRAY_SIZE(adcmap_smb_batt_therm),
			bat_voltage,
			&adc_chan_result->physical);
}
EXPORT_SYMBOL(qpnp_adc_scale_smb_batt_therm);

int32_t qpnp_adc_scale_therm_pu1(struct qpnp_vadc_chip *chip,
		int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		struct qpnp_vadc_result *adc_chan_result)
{
	int64_t therm_voltage = 0;

	qpnp_adc_scale_with_calib_param(adc_code,
			adc_properties, chan_properties, &therm_voltage);

	qpnp_adc_map_voltage_temp(adcmap_150k_104ef_104fb,
		ARRAY_SIZE(adcmap_150k_104ef_104fb),
		therm_voltage, &adc_chan_result->physical);

	return 0;
}
EXPORT_SYMBOL(qpnp_adc_scale_therm_pu1);

int32_t qpnp_adc_scale_therm_pu2(struct qpnp_vadc_chip *chip,
		int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		struct qpnp_vadc_result *adc_chan_result)
{
	int64_t therm_voltage = 0;

	if (!chan_properties || !chan_properties->offset_gain_numerator ||
		!chan_properties->offset_gain_denominator || !adc_properties)
		return -EINVAL;

	if (adc_properties->adc_hc) {
		/* (ADC code * vref_vadc (1.875V) * 1000) / (0x4000 * 1000) */
		if (adc_code > QPNP_VADC_HC_MAX_CODE)
			adc_code = 0;
		therm_voltage = (int64_t) adc_code;
		therm_voltage *= (int64_t) (adc_properties->adc_vdd_reference
							* 1000);
		therm_voltage = div64_s64(therm_voltage,
					(QPNP_VADC_HC_VREF_CODE * 1000));

		qpnp_adc_map_voltage_temp(adcmap_100k_104ef_104fb_1875_vref,
			ARRAY_SIZE(adcmap_100k_104ef_104fb_1875_vref),
			therm_voltage, &adc_chan_result->physical);
	} else {
		qpnp_adc_scale_with_calib_param(adc_code,
			adc_properties, chan_properties, &therm_voltage);

		if (chan_properties->calib_type == CALIB_ABSOLUTE)
			do_div(therm_voltage, 1000);

		qpnp_adc_map_voltage_temp(adcmap_100k_104ef_104fb,
			ARRAY_SIZE(adcmap_100k_104ef_104fb),
			therm_voltage, &adc_chan_result->physical);
	}

	return 0;
}
EXPORT_SYMBOL(qpnp_adc_scale_therm_pu2);

int32_t qpnp_adc_tm_scale_voltage_therm_pu2(struct qpnp_vadc_chip *chip,
		const struct qpnp_adc_properties *adc_properties,
					uint32_t reg, int64_t *result)
{
	int64_t adc_voltage = 0;
	struct qpnp_vadc_linear_graph param1;
	int negative_offset = 0;

	if (adc_properties->adc_hc) {
		/* (ADC code * vref_vadc (1.875V)) / 0x4000 */
		if (reg > QPNP_VADC_HC_MAX_CODE)
			reg = 0;
		adc_voltage = (int64_t) reg;
		adc_voltage *= QPNP_VADC_HC_VDD_REFERENCE_MV;
		adc_voltage = div64_s64(adc_voltage,
					QPNP_VADC_HC_VREF_CODE);
		qpnp_adc_map_voltage_temp(adcmap_100k_104ef_104fb_1875_vref,
			ARRAY_SIZE(adcmap_100k_104ef_104fb_1875_vref),
			adc_voltage, result);
	} else {
		qpnp_get_vadc_gain_and_offset(chip, &param1, CALIB_RATIOMETRIC);

		adc_voltage = (reg - param1.adc_gnd) * param1.adc_vref;
		if (adc_voltage < 0) {
			negative_offset = 1;
			adc_voltage = -adc_voltage;
		}

		do_div(adc_voltage, param1.dy);

		qpnp_adc_map_voltage_temp(adcmap_100k_104ef_104fb,
			ARRAY_SIZE(adcmap_100k_104ef_104fb),
			adc_voltage, result);
		if (negative_offset)
			adc_voltage = -adc_voltage;
	}

	return 0;
}
EXPORT_SYMBOL(qpnp_adc_tm_scale_voltage_therm_pu2);

int32_t qpnp_adc_tm_scale_therm_voltage_pu2(struct qpnp_vadc_chip *chip,
			const struct qpnp_adc_properties *adc_properties,
				struct qpnp_adc_tm_config *param)
{
	struct qpnp_vadc_linear_graph param1;
	int rc;

	if (adc_properties->adc_hc) {
		rc = qpnp_adc_map_temp_voltage(
			adcmap_100k_104ef_104fb_1875_vref,
			ARRAY_SIZE(adcmap_100k_104ef_104fb_1875_vref),
			param->low_thr_temp, &param->low_thr_voltage);
		if (rc)
			return rc;
		param->low_thr_voltage *= QPNP_VADC_HC_VREF_CODE;
		do_div(param->low_thr_voltage, QPNP_VADC_HC_VDD_REFERENCE_MV);

		rc = qpnp_adc_map_temp_voltage(
			adcmap_100k_104ef_104fb_1875_vref,
			ARRAY_SIZE(adcmap_100k_104ef_104fb_1875_vref),
			param->high_thr_temp, &param->high_thr_voltage);
		if (rc)
			return rc;
		param->high_thr_voltage *= QPNP_VADC_HC_VREF_CODE;
		do_div(param->high_thr_voltage, QPNP_VADC_HC_VDD_REFERENCE_MV);
	} else {
		qpnp_get_vadc_gain_and_offset(chip, &param1, CALIB_RATIOMETRIC);

		rc = qpnp_adc_map_temp_voltage(adcmap_100k_104ef_104fb,
			ARRAY_SIZE(adcmap_100k_104ef_104fb),
			param->low_thr_temp, &param->low_thr_voltage);
		if (rc)
			return rc;

		param->low_thr_voltage *= param1.dy;
		do_div(param->low_thr_voltage, param1.adc_vref);
		param->low_thr_voltage += param1.adc_gnd;

		rc = qpnp_adc_map_temp_voltage(adcmap_100k_104ef_104fb,
			ARRAY_SIZE(adcmap_100k_104ef_104fb),
			param->high_thr_temp, &param->high_thr_voltage);
		if (rc)
			return rc;

		param->high_thr_voltage *= param1.dy;
		do_div(param->high_thr_voltage, param1.adc_vref);
		param->high_thr_voltage += param1.adc_gnd;
	}

	return 0;
}
EXPORT_SYMBOL(qpnp_adc_tm_scale_therm_voltage_pu2);

int32_t qpnp_adc_scale_therm_ncp03(struct qpnp_vadc_chip *chip,
		int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		struct qpnp_vadc_result *adc_chan_result)
{
	int64_t therm_voltage = 0;

	qpnp_adc_scale_with_calib_param(adc_code,
			adc_properties, chan_properties, &therm_voltage);

	qpnp_adc_map_voltage_temp(adcmap_ncp03wf683,
		ARRAY_SIZE(adcmap_ncp03wf683),
		therm_voltage, &adc_chan_result->physical);

	return 0;
}
EXPORT_SYMBOL(qpnp_adc_scale_therm_ncp03);

int32_t qpnp_adc_scale_batt_id(struct qpnp_vadc_chip *chip,
		int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		struct qpnp_vadc_result *adc_chan_result)
{
	int64_t batt_id_voltage = 0;

	qpnp_adc_scale_with_calib_param(adc_code,
			adc_properties, chan_properties, &batt_id_voltage);

	adc_chan_result->physical = batt_id_voltage;
	adc_chan_result->physical = adc_chan_result->measurement;

	return 0;
}
EXPORT_SYMBOL(qpnp_adc_scale_batt_id);

int32_t qpnp_adc_scale_default(struct qpnp_vadc_chip *vadc,
		int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		struct qpnp_vadc_result *adc_chan_result)
{
	int64_t scale_voltage = 0;

	if (!chan_properties || !chan_properties->offset_gain_numerator ||
		!chan_properties->offset_gain_denominator || !adc_properties
		|| !adc_chan_result)
		return -EINVAL;

	if (adc_properties->adc_hc) {
		/* (ADC code * vref_vadc (1.875V)) / 0x4000 */
		if (adc_code > QPNP_VADC_HC_MAX_CODE)
			adc_code = 0;
		scale_voltage = (int64_t) adc_code;
		scale_voltage *= (adc_properties->adc_vdd_reference * 1000);
		scale_voltage = div64_s64(scale_voltage,
						QPNP_VADC_HC_VREF_CODE);
	} else {
		qpnp_adc_scale_with_calib_param(adc_code, adc_properties,
					chan_properties, &scale_voltage);
		if (!chan_properties->calib_type == CALIB_ABSOLUTE)
			scale_voltage *= 1000;
	}


	scale_voltage *= chan_properties->offset_gain_denominator;
	scale_voltage = div64_s64(scale_voltage,
				chan_properties->offset_gain_numerator);
	adc_chan_result->measurement = scale_voltage;
	/*
	 * Note: adc_chan_result->measurement is in the unit of
	 * adc_properties.adc_reference. For generic channel processing,
	 * channel measurement is a scale/ratio relative to the adc
	 * reference input
	 */
	adc_chan_result->physical = adc_chan_result->measurement;

	return 0;
}
EXPORT_SYMBOL(qpnp_adc_scale_default);

int32_t qpnp_adc_usb_scaler(struct qpnp_vadc_chip *chip,
		struct qpnp_adc_tm_btm_param *param,
		uint32_t *low_threshold, uint32_t *high_threshold)
{
	struct qpnp_vadc_linear_graph usb_param;

	qpnp_get_vadc_gain_and_offset(chip, &usb_param, CALIB_RATIOMETRIC);

	*low_threshold = param->low_thr * usb_param.dy;
	do_div(*low_threshold, usb_param.adc_vref);
	*low_threshold += usb_param.adc_gnd;

	*high_threshold = param->high_thr * usb_param.dy;
	do_div(*high_threshold, usb_param.adc_vref);
	*high_threshold += usb_param.adc_gnd;

	pr_debug("high_volt:%d, low_volt:%d\n", param->high_thr,
				param->low_thr);
	return 0;
}
EXPORT_SYMBOL(qpnp_adc_usb_scaler);

int32_t qpnp_adc_absolute_rthr(struct qpnp_vadc_chip *chip,
		struct qpnp_adc_tm_btm_param *param,
		uint32_t *low_threshold, uint32_t *high_threshold)
{
	struct qpnp_vadc_linear_graph vbatt_param;
	int rc = 0, sign = 0;
	int64_t low_thr = 0, high_thr = 0;

	if (param->adc_tm_hc) {
		low_thr = (param->low_thr/param->gain_den);
		low_thr *= param->gain_num;
		low_thr *= QPNP_VADC_HC_VREF_CODE;
		do_div(low_thr, (QPNP_VADC_HC_VDD_REFERENCE_MV * 1000));
		*low_threshold = low_thr;

		high_thr = (param->high_thr/param->gain_den);
		high_thr *= param->gain_num;
		high_thr *= QPNP_VADC_HC_VREF_CODE;
		do_div(high_thr, (QPNP_VADC_HC_VDD_REFERENCE_MV * 1000));
		*high_threshold = high_thr;
	} else {
		rc = qpnp_get_vadc_gain_and_offset(chip, &vbatt_param,
							CALIB_ABSOLUTE);
		if (rc < 0)
			return rc;

		low_thr = (((param->low_thr/param->gain_den) -
				QPNP_ADC_625_UV) * vbatt_param.dy);
		if (low_thr < 0) {
			sign = 1;
			low_thr = -low_thr;
		}
		low_thr = low_thr * param->gain_num;
		do_div(low_thr, QPNP_ADC_625_UV);
		if (sign)
			low_thr = -low_thr;
		*low_threshold = low_thr + vbatt_param.adc_gnd;

		sign = 0;
		high_thr = (((param->high_thr/param->gain_den) -
				QPNP_ADC_625_UV) * vbatt_param.dy);
		if (high_thr < 0) {
			sign = 1;
			high_thr = -high_thr;
		}
		high_thr = high_thr * param->gain_num;
		do_div(high_thr, QPNP_ADC_625_UV);
		if (sign)
			high_thr = -high_thr;
		*high_threshold = high_thr + vbatt_param.adc_gnd;
	}

	pr_debug("high_volt:%d, low_volt:%d\n", param->high_thr,
				param->low_thr);
	pr_debug("adc_code_high:%x, adc_code_low:%x\n", *high_threshold,
				*low_threshold);
	return 0;
}
EXPORT_SYMBOL(qpnp_adc_absolute_rthr);

int32_t qpnp_adc_vbatt_rscaler(struct qpnp_vadc_chip *chip,
		struct qpnp_adc_tm_btm_param *param,
		uint32_t *low_threshold, uint32_t *high_threshold)
{
	return qpnp_adc_absolute_rthr(chip, param, low_threshold,
							high_threshold);
}
EXPORT_SYMBOL(qpnp_adc_vbatt_rscaler);

int32_t qpnp_vadc_absolute_rthr(struct qpnp_vadc_chip *chip,
		const struct qpnp_vadc_chan_properties *chan_prop,
		struct qpnp_adc_tm_btm_param *param,
		uint32_t *low_threshold, uint32_t *high_threshold)
{
	struct qpnp_vadc_linear_graph vbatt_param;
	int rc = 0, sign = 0;
	int64_t low_thr = 0, high_thr = 0;

	if (!chan_prop || !chan_prop->offset_gain_numerator ||
		!chan_prop->offset_gain_denominator)
		return -EINVAL;

	rc = qpnp_get_vadc_gain_and_offset(chip, &vbatt_param, CALIB_ABSOLUTE);
	if (rc < 0)
		return rc;

	low_thr = (((param->low_thr)/(int)chan_prop->offset_gain_denominator
					- QPNP_ADC_625_UV) * vbatt_param.dy);
	if (low_thr < 0) {
		sign = 1;
		low_thr = -low_thr;
	}
	low_thr = low_thr * chan_prop->offset_gain_numerator;
	do_div(low_thr, QPNP_ADC_625_UV);
	if (sign)
		low_thr = -low_thr;
	*low_threshold = low_thr + vbatt_param.adc_gnd;

	sign = 0;
	high_thr = (((param->high_thr)/(int)chan_prop->offset_gain_denominator
					- QPNP_ADC_625_UV) * vbatt_param.dy);
	if (high_thr < 0) {
		sign = 1;
		high_thr = -high_thr;
	}
	high_thr = high_thr * chan_prop->offset_gain_numerator;
	do_div(high_thr, QPNP_ADC_625_UV);
	if (sign)
		high_thr = -high_thr;
	*high_threshold = high_thr + vbatt_param.adc_gnd;

	pr_debug("high_volt:%d, low_volt:%d\n", param->high_thr,
				param->low_thr);
	pr_debug("adc_code_high:%x, adc_code_low:%x\n", *high_threshold,
				*low_threshold);
	return 0;
}
EXPORT_SYMBOL(qpnp_vadc_absolute_rthr);

int32_t qpnp_adc_btm_scaler(struct qpnp_vadc_chip *chip,
		struct qpnp_adc_tm_btm_param *param,
		uint32_t *low_threshold, uint32_t *high_threshold)
{
	struct qpnp_vadc_linear_graph btm_param;
	int64_t low_output = 0, high_output = 0;
	int rc = 0;

	if (param->adc_tm_hc) {
		pr_err("Update scaling for VADC_TM_HC\n");
		return -EINVAL;
	}

	qpnp_get_vadc_gain_and_offset(chip, &btm_param, CALIB_RATIOMETRIC);

	pr_debug("warm_temp:%d and cool_temp:%d\n", param->high_temp,
				param->low_temp);
	rc = qpnp_adc_map_voltage_temp(
		adcmap_btm_threshold,
		ARRAY_SIZE(adcmap_btm_threshold),
		(param->low_temp),
		&low_output);
	if (rc) {
		pr_debug("low_temp mapping failed with %d\n", rc);
		return rc;
	}

	pr_debug("low_output:%lld\n", low_output);
	low_output *= btm_param.dy;
	do_div(low_output, btm_param.adc_vref);
	low_output += btm_param.adc_gnd;

	rc = qpnp_adc_map_voltage_temp(
		adcmap_btm_threshold,
		ARRAY_SIZE(adcmap_btm_threshold),
		(param->high_temp),
		&high_output);
	if (rc) {
		pr_debug("high temp mapping failed with %d\n", rc);
		return rc;
	}

	pr_debug("high_output:%lld\n", high_output);
	high_output *= btm_param.dy;
	do_div(high_output, btm_param.adc_vref);
	high_output += btm_param.adc_gnd;

	/* btm low temperature correspondes to high voltage threshold */
	*low_threshold = high_output;
	/* btm high temperature correspondes to low voltage threshold */
	*high_threshold = low_output;

	pr_debug("high_volt:%d, low_volt:%d\n", *high_threshold,
				*low_threshold);
	return 0;
}
EXPORT_SYMBOL(qpnp_adc_btm_scaler);

int32_t qpnp_adc_qrd_skuh_btm_scaler(struct qpnp_vadc_chip *chip,
		struct qpnp_adc_tm_btm_param *param,
		uint32_t *low_threshold, uint32_t *high_threshold)
{
	struct qpnp_vadc_linear_graph btm_param;
	int64_t low_output = 0, high_output = 0;
	int rc = 0;

	if (param->adc_tm_hc) {
		pr_err("Update scaling for VADC_TM_HC\n");
		return -EINVAL;
	}

	qpnp_get_vadc_gain_and_offset(chip, &btm_param, CALIB_RATIOMETRIC);

	pr_debug("warm_temp:%d and cool_temp:%d\n", param->high_temp,
				param->low_temp);
	rc = qpnp_adc_map_voltage_temp(
		adcmap_qrd_skuh_btm_threshold,
		ARRAY_SIZE(adcmap_qrd_skuh_btm_threshold),
		(param->low_temp),
		&low_output);
	if (rc) {
		pr_debug("low_temp mapping failed with %d\n", rc);
		return rc;
	}

	pr_debug("low_output:%lld\n", low_output);
	low_output *= btm_param.dy;
	do_div(low_output, btm_param.adc_vref);
	low_output += btm_param.adc_gnd;

	rc = qpnp_adc_map_voltage_temp(
		adcmap_qrd_skuh_btm_threshold,
		ARRAY_SIZE(adcmap_qrd_skuh_btm_threshold),
		(param->high_temp),
		&high_output);
	if (rc) {
		pr_debug("high temp mapping failed with %d\n", rc);
		return rc;
	}

	pr_debug("high_output:%lld\n", high_output);
	high_output *= btm_param.dy;
	do_div(high_output, btm_param.adc_vref);
	high_output += btm_param.adc_gnd;

	/* btm low temperature correspondes to high voltage threshold */
	*low_threshold = high_output;
	/* btm high temperature correspondes to low voltage threshold */
	*high_threshold = low_output;

	pr_debug("high_volt:%d, low_volt:%d\n", *high_threshold,
				*low_threshold);
	return 0;
}
EXPORT_SYMBOL(qpnp_adc_qrd_skuh_btm_scaler);

int32_t qpnp_adc_qrd_skut1_btm_scaler(struct qpnp_vadc_chip *chip,
		struct qpnp_adc_tm_btm_param *param,
		uint32_t *low_threshold, uint32_t *high_threshold)
{
	struct qpnp_vadc_linear_graph btm_param;
	int64_t low_output = 0, high_output = 0;
	int rc = 0;

	if (param->adc_tm_hc) {
		pr_err("Update scaling for VADC_TM_HC\n");
		return -EINVAL;
	}

	qpnp_get_vadc_gain_and_offset(chip, &btm_param, CALIB_RATIOMETRIC);

	pr_debug("warm_temp:%d and cool_temp:%d\n", param->high_temp,
				param->low_temp);
	rc = qpnp_adc_map_voltage_temp(
		adcmap_qrd_skut1_btm_threshold,
		ARRAY_SIZE(adcmap_qrd_skut1_btm_threshold),
		(param->low_temp),
		&low_output);
	if (rc) {
		pr_debug("low_temp mapping failed with %d\n", rc);
		return rc;
	}

	pr_debug("low_output:%lld\n", low_output);
	low_output *= btm_param.dy;
	do_div(low_output, btm_param.adc_vref);
	low_output += btm_param.adc_gnd;

	rc = qpnp_adc_map_voltage_temp(
		adcmap_qrd_skut1_btm_threshold,
		ARRAY_SIZE(adcmap_qrd_skut1_btm_threshold),
		(param->high_temp),
		&high_output);
	if (rc) {
		pr_debug("high temp mapping failed with %d\n", rc);
		return rc;
	}

	pr_debug("high_output:%lld\n", high_output);
	high_output *= btm_param.dy;
	do_div(high_output, btm_param.adc_vref);
	high_output += btm_param.adc_gnd;

	/* btm low temperature correspondes to high voltage threshold */
	*low_threshold = high_output;
	/* btm high temperature correspondes to low voltage threshold */
	*high_threshold = low_output;

	pr_debug("high_volt:%d, low_volt:%d\n", *high_threshold,
				*low_threshold);
	return 0;
}
EXPORT_SYMBOL(qpnp_adc_qrd_skut1_btm_scaler);

int32_t qpnp_adc_qrd_skue_btm_scaler(struct qpnp_vadc_chip *chip,
		struct qpnp_adc_tm_btm_param *param,
		uint32_t *low_threshold, uint32_t *high_threshold)
{
	struct qpnp_vadc_linear_graph btm_param;
	int64_t low_output = 0, high_output = 0;
	int rc = 0;

	qpnp_get_vadc_gain_and_offset(chip, &btm_param, CALIB_RATIOMETRIC);

	pr_debug("warm_temp:%d and cool_temp:%d\n", param->high_temp,
				param->low_temp);
	rc = qpnp_adc_map_voltage_temp(
		adcmap_qrd_skue_btm_threshold,
		ARRAY_SIZE(adcmap_qrd_skue_btm_threshold),
		(param->low_temp),
		&low_output);
	if (rc) {
		pr_debug("low_temp mapping failed with %d\n", rc);
		return rc;
	}

	pr_debug("low_output:%lld\n", low_output);
	low_output *= btm_param.dy;
	do_div(low_output, btm_param.adc_vref);
	low_output += btm_param.adc_gnd;

	rc = qpnp_adc_map_voltage_temp(
		adcmap_qrd_skue_btm_threshold,
		ARRAY_SIZE(adcmap_qrd_skue_btm_threshold),
		(param->high_temp),
		&high_output);
	if (rc) {
		pr_debug("high temp mapping failed with %d\n", rc);
		return rc;
	}

	pr_debug("high_output:%lld\n", high_output);
	high_output *= btm_param.dy;
	do_div(high_output, btm_param.adc_vref);
	high_output += btm_param.adc_gnd;

	/* btm low temperature correspondes to high voltage threshold */
	*low_threshold = high_output;
	/* btm high temperature correspondes to low voltage threshold */
	*high_threshold = low_output;

	pr_debug("high_volt:%d, low_volt:%d\n", *high_threshold,
				*low_threshold);
	return 0;
}
EXPORT_SYMBOL(qpnp_adc_qrd_skue_btm_scaler);

int32_t qpnp_adc_smb_btm_rscaler(struct qpnp_vadc_chip *chip,
		struct qpnp_adc_tm_btm_param *param,
		uint32_t *low_threshold, uint32_t *high_threshold)
{
	struct qpnp_vadc_linear_graph btm_param;
	int64_t low_output = 0, high_output = 0;
	int rc = 0;

	if (param->adc_tm_hc) {
		pr_err("Update scaling for VADC_TM_HC\n");
		return -EINVAL;
	}

	qpnp_get_vadc_gain_and_offset(chip, &btm_param, CALIB_RATIOMETRIC);

	pr_debug("warm_temp:%d and cool_temp:%d\n", param->high_temp,
				param->low_temp);
	rc = qpnp_adc_map_voltage_temp(
		adcmap_smb_batt_therm,
		ARRAY_SIZE(adcmap_smb_batt_therm),
		(param->low_temp),
		&low_output);
	if (rc) {
		pr_debug("low_temp mapping failed with %d\n", rc);
		return rc;
	}

	pr_debug("low_output:%lld\n", low_output);
	low_output *= btm_param.dy;
	do_div(low_output, btm_param.adc_vref);
	low_output += btm_param.adc_gnd;

	rc = qpnp_adc_map_voltage_temp(
		adcmap_smb_batt_therm,
		ARRAY_SIZE(adcmap_smb_batt_therm),
		(param->high_temp),
		&high_output);
	if (rc) {
		pr_debug("high temp mapping failed with %d\n", rc);
		return rc;
	}

	pr_debug("high_output:%lld\n", high_output);
	high_output *= btm_param.dy;
	do_div(high_output, btm_param.adc_vref);
	high_output += btm_param.adc_gnd;

	/* btm low temperature correspondes to high voltage threshold */
	*low_threshold = high_output;
	/* btm high temperature correspondes to low voltage threshold */
	*high_threshold = low_output;

	pr_debug("high_volt:%d, low_volt:%d\n", *high_threshold,
				*low_threshold);
	return 0;
}
EXPORT_SYMBOL(qpnp_adc_smb_btm_rscaler);

int32_t qpnp_adc_scale_pmi_chg_temp(struct qpnp_vadc_chip *vadc,
		int32_t adc_code,
		const struct qpnp_adc_properties *adc_properties,
		const struct qpnp_vadc_chan_properties *chan_properties,
		struct qpnp_vadc_result *adc_chan_result)
{
	int rc = 0;

	rc = qpnp_adc_scale_default(vadc, adc_code, adc_properties,
			chan_properties, adc_chan_result);
	if (rc < 0)
		return rc;

	pr_debug("raw_code:%x, v_adc:%lld\n", adc_code,
						adc_chan_result->physical);
	adc_chan_result->physical = (int64_t) ((PMI_CHG_SCALE_1) *
					(adc_chan_result->physical * 2));
	adc_chan_result->physical = (int64_t) (adc_chan_result->physical +
							PMI_CHG_SCALE_2);
	adc_chan_result->physical = (int64_t) adc_chan_result->physical;
	adc_chan_result->physical = div64_s64(adc_chan_result->physical,
								1000000);

	return 0;
}
EXPORT_SYMBOL(qpnp_adc_scale_pmi_chg_temp);

int32_t qpnp_adc_enable_voltage(struct qpnp_adc_drv *adc)
{
	int rc = 0;

	if (adc->hkadc_ldo) {
		rc = regulator_enable(adc->hkadc_ldo);
		if (rc < 0) {
			pr_err("Failed to enable hkadc ldo\n");
			return rc;
		}
	}

	if (adc->hkadc_ldo_ok) {
		rc = regulator_enable(adc->hkadc_ldo_ok);
		if (rc < 0) {
			pr_err("Failed to enable hkadc ok signal\n");
			return rc;
		}
	}

	return rc;
}
EXPORT_SYMBOL(qpnp_adc_enable_voltage);

void qpnp_adc_disable_voltage(struct qpnp_adc_drv *adc)
{
	if (adc->hkadc_ldo)
		regulator_disable(adc->hkadc_ldo);

	if (adc->hkadc_ldo_ok)
		regulator_disable(adc->hkadc_ldo_ok);

}
EXPORT_SYMBOL(qpnp_adc_disable_voltage);

void qpnp_adc_free_voltage_resource(struct qpnp_adc_drv *adc)
{
	if (adc->hkadc_ldo)
		regulator_put(adc->hkadc_ldo);

	if (adc->hkadc_ldo_ok)
		regulator_put(adc->hkadc_ldo_ok);
}
EXPORT_SYMBOL(qpnp_adc_free_voltage_resource);

int qpnp_adc_get_revid_version(struct device *dev)
{
	struct pmic_revid_data *revid_data;
	struct device_node *revid_dev_node;

	revid_dev_node = of_parse_phandle(dev->of_node,
						"qcom,pmic-revid", 0);
	if (!revid_dev_node) {
		pr_debug("Missing qcom,pmic-revid property\n");
		return -EINVAL;
	}

	revid_data = get_revid_data(revid_dev_node);
	if (IS_ERR(revid_data)) {
		pr_debug("revid error rc = %ld\n", PTR_ERR(revid_data));
		return -EINVAL;
	}

	if ((revid_data->rev1 == PM8941_V3P1_REV1) &&
		(revid_data->rev2 == PM8941_V3P1_REV2) &&
		(revid_data->rev3 == PM8941_V3P1_REV3) &&
		(revid_data->rev4 == PM8941_V3P1_REV4) &&
		(revid_data->pmic_subtype == PM8941_SUBTYPE))
			return QPNP_REV_ID_8941_3_1;
	else if ((revid_data->rev1 == PM8941_V3P0_REV1) &&
		(revid_data->rev2 == PM8941_V3P0_REV2) &&
		(revid_data->rev3 == PM8941_V3P0_REV3) &&
		(revid_data->rev4 == PM8941_V3P0_REV4) &&
		(revid_data->pmic_subtype == PM8941_SUBTYPE))
			return QPNP_REV_ID_8941_3_0;
	else if ((revid_data->rev1 == PM8941_V2P0_REV1) &&
		(revid_data->rev2 == PM8941_V2P0_REV2) &&
		(revid_data->rev3 == PM8941_V2P0_REV3) &&
		(revid_data->rev4 == PM8941_V2P0_REV4) &&
		(revid_data->pmic_subtype == PM8941_SUBTYPE))
			return QPNP_REV_ID_8941_2_0;
	else if ((revid_data->rev1 == PM8226_V2P2_REV1) &&
		(revid_data->rev2 == PM8226_V2P2_REV2) &&
		(revid_data->rev3 == PM8226_V2P2_REV3) &&
		(revid_data->rev4 == PM8226_V2P2_REV4) &&
		(revid_data->pmic_subtype == PM8226_SUBTYPE))
			return QPNP_REV_ID_8026_2_2;
	else if ((revid_data->rev1 == PM8226_V2P1_REV1) &&
		(revid_data->rev2 == PM8226_V2P1_REV2) &&
		(revid_data->rev3 == PM8226_V2P1_REV3) &&
		(revid_data->rev4 == PM8226_V2P1_REV4) &&
		(revid_data->pmic_subtype == PM8226_SUBTYPE))
			return QPNP_REV_ID_8026_2_1;
	else if ((revid_data->rev1 == PM8226_V2P0_REV1) &&
		(revid_data->rev2 == PM8226_V2P0_REV2) &&
		(revid_data->rev3 == PM8226_V2P0_REV3) &&
		(revid_data->rev4 == PM8226_V2P0_REV4) &&
		(revid_data->pmic_subtype == PM8226_SUBTYPE))
			return QPNP_REV_ID_8026_2_0;
	else if ((revid_data->rev1 == PM8226_V1P0_REV1) &&
		(revid_data->rev2 == PM8226_V1P0_REV2) &&
		(revid_data->rev3 == PM8226_V1P0_REV3) &&
		(revid_data->rev4 == PM8226_V1P0_REV4) &&
		(revid_data->pmic_subtype == PM8226_SUBTYPE))
			return QPNP_REV_ID_8026_1_0;
	else if ((revid_data->rev1 == PM8110_V1P0_REV1) &&
		(revid_data->rev2 == PM8110_V1P0_REV2) &&
		(revid_data->rev3 == PM8110_V1P0_REV3) &&
		(revid_data->rev4 == PM8110_V1P0_REV4) &&
		(revid_data->pmic_subtype == PM8110_SUBTYPE))
			return QPNP_REV_ID_8110_1_0;
	else if ((revid_data->rev1 == PM8110_V2P0_REV1) &&
		(revid_data->rev2 == PM8110_V2P0_REV2) &&
		(revid_data->rev3 == PM8110_V2P0_REV3) &&
		(revid_data->rev4 == PM8110_V2P0_REV4) &&
		(revid_data->pmic_subtype == PM8110_SUBTYPE))
			return QPNP_REV_ID_8110_2_0;
	else if ((revid_data->rev1 == PM8916_V1P0_REV1) &&
		(revid_data->rev2 == PM8916_V1P0_REV2) &&
		(revid_data->rev3 == PM8916_V1P0_REV3) &&
		(revid_data->rev4 == PM8916_V1P0_REV4) &&
		(revid_data->pmic_subtype == PM8916_SUBTYPE))
			return QPNP_REV_ID_8916_1_0;
	else if ((revid_data->rev1 == PM8916_V1P1_REV1) &&
		(revid_data->rev2 == PM8916_V1P1_REV2) &&
		(revid_data->rev3 == PM8916_V1P1_REV3) &&
		(revid_data->rev4 == PM8916_V1P1_REV4) &&
		(revid_data->pmic_subtype == PM8916_SUBTYPE))
			return QPNP_REV_ID_8916_1_1;
	else if ((revid_data->rev1 == PM8916_V2P0_REV1) &&
		(revid_data->rev2 == PM8916_V2P0_REV2) &&
		(revid_data->rev3 == PM8916_V2P0_REV3) &&
		(revid_data->rev4 == PM8916_V2P0_REV4) &&
		(revid_data->pmic_subtype == PM8916_SUBTYPE))
			return QPNP_REV_ID_8916_2_0;
	else if ((revid_data->rev1 == PM8909_V1P0_REV1) &&
		(revid_data->rev2 == PM8909_V1P0_REV2) &&
		(revid_data->rev3 == PM8909_V1P0_REV3) &&
		(revid_data->rev4 == PM8909_V1P0_REV4) &&
		(revid_data->pmic_subtype == PM8909_SUBTYPE))
			return QPNP_REV_ID_8909_1_0;
	else if ((revid_data->rev1 == PM8909_V1P1_REV1) &&
		(revid_data->rev2 == PM8909_V1P1_REV2) &&
		(revid_data->rev3 == PM8909_V1P1_REV3) &&
		(revid_data->rev4 == PM8909_V1P1_REV4) &&
		(revid_data->pmic_subtype == PM8909_SUBTYPE))
			return QPNP_REV_ID_8909_1_1;
	else if ((revid_data->rev4 == PM8950_V1P0_REV4) &&
		(revid_data->pmic_subtype == PM8950_SUBTYPE))
			return QPNP_REV_ID_PM8950_1_0;
	else
		return -EINVAL;
}
EXPORT_SYMBOL(qpnp_adc_get_revid_version);

int32_t qpnp_adc_get_devicetree_data(struct spmi_device *spmi,
			struct qpnp_adc_drv *adc_qpnp)
{
	struct device_node *node = spmi->dev.of_node;
	struct resource *res;
	struct device_node *child;
	struct qpnp_adc_amux *adc_channel_list;
	struct qpnp_adc_properties *adc_prop;
	struct qpnp_adc_amux_properties *amux_prop;
	int count_adc_channel_list = 0, decimation = 0, rc = 0, i = 0;
	int decimation_tm_hc = 0, fast_avg_setup_tm_hc = 0, cal_val_hc = 0;
	bool adc_hc;

	if (!node)
		return -EINVAL;

	for_each_child_of_node(node, child)
		count_adc_channel_list++;

	if (!count_adc_channel_list) {
		pr_err("No channel listing\n");
		return -EINVAL;
	}

	adc_qpnp->spmi = spmi;

	adc_prop = devm_kzalloc(&spmi->dev, sizeof(struct qpnp_adc_properties),
					GFP_KERNEL);
	if (!adc_prop)
		return -ENOMEM;

	adc_channel_list = devm_kzalloc(&spmi->dev,
		((sizeof(struct qpnp_adc_amux)) * count_adc_channel_list),
				GFP_KERNEL);
	if (!adc_channel_list)
		return -ENOMEM;

	amux_prop = devm_kzalloc(&spmi->dev,
		sizeof(struct qpnp_adc_amux_properties) +
		sizeof(struct qpnp_vadc_chan_properties), GFP_KERNEL);
	if (!amux_prop) {
		dev_err(&spmi->dev, "Unable to allocate memory\n");
		return -ENOMEM;
	}

	adc_qpnp->adc_channels = adc_channel_list;
	adc_qpnp->amux_prop = amux_prop;
	adc_hc = adc_qpnp->adc_hc;
	adc_prop->adc_hc = adc_hc;

	if (of_device_is_compatible(node, "qcom,qpnp-adc-tm-hc")) {
		rc = of_property_read_u32(node, "qcom,decimation",
						&decimation_tm_hc);
		if (rc) {
			pr_err("Invalid decimation property\n");
			return -EINVAL;
		}

		rc = of_property_read_u32(node,
			"qcom,fast-avg-setup", &fast_avg_setup_tm_hc);
		if (rc) {
			pr_err("Invalid fast average setup with %d\n", rc);
			return -EINVAL;
		}

		if ((fast_avg_setup_tm_hc) > ADC_FAST_AVG_SAMPLE_16) {
			pr_err("Max average support is 2^16\n");
			return -EINVAL;
		}
	}

	for_each_child_of_node(node, child) {
		int channel_num, scaling = 0, post_scaling = 0;
		int fast_avg_setup, calib_type = 0, rc, hw_settle_time = 0;
		const char *calibration_param, *channel_name;

		channel_name = of_get_property(child,
				"label", NULL) ? : child->name;
		if (!channel_name) {
			pr_err("Invalid channel name\n");
			return -EINVAL;
		}

		rc = of_property_read_u32(child, "reg", &channel_num);
		if (rc) {
			pr_err("Invalid channel num\n");
			return -EINVAL;
		}

		if (!of_device_is_compatible(node, "qcom,qpnp-iadc")) {
			rc = of_property_read_u32(child,
				"qcom,hw-settle-time", &hw_settle_time);
			if (rc) {
				pr_err("Invalid channel hw settle time property\n");
				return -EINVAL;
			}
			rc = of_property_read_u32(child,
				"qcom,pre-div-channel-scaling", &scaling);
			if (rc) {
				pr_err("Invalid channel scaling property\n");
				return -EINVAL;
			}
			rc = of_property_read_u32(child,
				"qcom,scale-function", &post_scaling);
			if (rc) {
				pr_err("Invalid channel post scaling property\n");
				return -EINVAL;
			}
			rc = of_property_read_string(child,
				"qcom,calibration-type", &calibration_param);
			if (rc) {
				pr_err("Invalid calibration type\n");
				return -EINVAL;
			}
			if (!strcmp(calibration_param, "absolute")) {
				if (adc_hc)
					calib_type = ADC_HC_ABS_CAL;
				else
					calib_type = CALIB_ABSOLUTE;
			} else if (!strcmp(calibration_param, "ratiometric")) {
				if (adc_hc)
					calib_type = ADC_HC_RATIO_CAL;
				else
					calib_type = CALIB_RATIOMETRIC;
			} else if (!strcmp(calibration_param, "no_cal")) {
				if (adc_hc)
					calib_type = ADC_HC_NO_CAL;
				else {
					pr_err("%s: Invalid calibration property\n",
						__func__);
					return -EINVAL;
				}
			} else {
				pr_err("%s: Invalid calibration property\n",
						__func__);
				return -EINVAL;
			}
		}

		/* ADC_TM_HC fast avg setting is common across channels */
		if (!of_device_is_compatible(node, "qcom,qpnp-adc-tm-hc")) {
			rc = of_property_read_u32(child,
				"qcom,fast-avg-setup", &fast_avg_setup);
			if (rc) {
				pr_err("Invalid channel fast average setup\n");
				return -EINVAL;
			}
		} else {
			fast_avg_setup = fast_avg_setup_tm_hc;
		}

		/* ADC_TM_HC decimation setting is common across channels */
		if (!of_device_is_compatible(node, "qcom,qpnp-adc-tm-hc")) {
			rc = of_property_read_u32(child,
				"qcom,decimation", &decimation);
			if (rc) {
				pr_err("Invalid decimation\n");
				return -EINVAL;
			}
		} else {
			decimation = decimation_tm_hc;
		}

		if (of_device_is_compatible(node, "qcom,qpnp-vadc-hc")) {
			rc = of_property_read_u32(child, "qcom,cal-val",
							&cal_val_hc);
			if (rc) {
				pr_debug("Use calibration value from timer\n");
				adc_channel_list[i].cal_val = ADC_TIMER_CAL;
			} else {
				adc_channel_list[i].cal_val = cal_val_hc;
			}
		}

		/* Individual channel properties */
		adc_channel_list[i].name = (char *)channel_name;
		adc_channel_list[i].channel_num = channel_num;
		adc_channel_list[i].adc_decimation = decimation;
		adc_channel_list[i].fast_avg_setup = fast_avg_setup;
		if (!of_device_is_compatible(node, "qcom,qpnp-iadc")) {
			adc_channel_list[i].chan_path_prescaling = scaling;
			adc_channel_list[i].adc_scale_fn = post_scaling;
			adc_channel_list[i].hw_settle_time = hw_settle_time;
			adc_channel_list[i].calib_type = calib_type;
		}
		i++;
	}

	/* Get the ADC VDD reference voltage and ADC bit resolution */
	rc = of_property_read_u32(node, "qcom,adc-vdd-reference",
			&adc_prop->adc_vdd_reference);
	if (rc) {
		pr_err("Invalid adc vdd reference property\n");
		return -EINVAL;
	}
	rc = of_property_read_u32(node, "qcom,adc-bit-resolution",
			&adc_prop->bitresolution);
	if (rc) {
		pr_err("Invalid adc bit resolution property\n");
		return -EINVAL;
	}
	adc_qpnp->adc_prop = adc_prop;

	/* Get the peripheral address */
	res = spmi_get_resource(spmi, 0, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("No base address definition\n");
		return -EINVAL;
	}

	adc_qpnp->slave = spmi->sid;
	adc_qpnp->offset = res->start;

	/* Register the ADC peripheral interrupt */
	adc_qpnp->adc_irq_eoc = spmi_get_irq_byname(spmi, NULL,
						"eoc-int-en-set");
	if (adc_qpnp->adc_irq_eoc < 0) {
		pr_err("Invalid irq\n");
		return -ENXIO;
	}

	init_completion(&adc_qpnp->adc_rslt_completion);

	if (of_get_property(node, "hkadc_ldo-supply", NULL)) {
		adc_qpnp->hkadc_ldo = regulator_get(&spmi->dev,
				"hkadc_ldo");
		if (IS_ERR(adc_qpnp->hkadc_ldo)) {
			pr_err("hkadc_ldo-supply node not found\n");
			return -EINVAL;
		}

		rc = regulator_set_voltage(adc_qpnp->hkadc_ldo,
				QPNP_VADC_LDO_VOLTAGE_MIN,
				QPNP_VADC_LDO_VOLTAGE_MAX);
		if (rc < 0) {
			pr_err("setting voltage for hkadc_ldo failed\n");
			return rc;
		}

		rc = regulator_set_optimum_mode(adc_qpnp->hkadc_ldo,
				100000);
		if (rc < 0) {
			pr_err("hkadc_ldo optimum mode failed%d\n", rc);
			return rc;
		}
	}

	if (of_get_property(node, "hkadc_ok-supply", NULL)) {
		adc_qpnp->hkadc_ldo_ok = regulator_get(&spmi->dev,
				"hkadc_ok");
		if (IS_ERR(adc_qpnp->hkadc_ldo_ok)) {
			pr_err("hkadc_ok node not found\n");
			return -EINVAL;
		}

		rc = regulator_set_voltage(adc_qpnp->hkadc_ldo_ok,
				QPNP_VADC_OK_VOLTAGE_MIN,
				QPNP_VADC_OK_VOLTAGE_MAX);
		if (rc < 0) {
			pr_err("setting voltage for hkadc-ldo-ok failed\n");
			return rc;
		}
	}

	return 0;
}
EXPORT_SYMBOL(qpnp_adc_get_devicetree_data);
