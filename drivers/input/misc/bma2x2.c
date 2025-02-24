/*!
 * @section LICENSE
 * (C) Copyright 2013 Bosch Sensortec GmbH All Rights Reserved
 *
 * This software program is licensed subject to the GNU General
 * Public License (GPL).Version 2,June 1991,
 * available at http://www.fsf.org/copyleft/gpl.html
 *
 * @filename bma2x2.c
 * @date    2014/02/13 15:50
 * @id       "564eaab"
 * @version  2.0
 *
 * @brief
 * This file contains all function implementations for the BMA2X2 in linux
*/

#define BMA2X2_ENABLE_INT2

#if !defined(BMA2X2_ENABLE_INT1) && !defined(BMA2X2_ENABLE_INT2)
#if defined(CONFIG_BMA_ENABLE_NEWDATA_INT) || defined(CONFIG_SIG_MOTION)
#error Please enable INT1 or INT2 to support new data int and sig-motion int!
#endif
#endif

#ifdef CONFIG_SIG_MOTION
#undef CONFIG_HAS_EARLYSUSPEND
#endif
#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <asm/irq.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/sensors.h>
#include <linux/kthread.h>
#include <linux/hqsysfs.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/unistd.h>
#include <linux/types.h>
#include <linux/string.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#endif

#include "bstclass.h"

#define ACC_NAME  "ACC"

#ifdef ENABLE_ISR_DEBUG_MSG
#define ISR_INFO(dev, fmt, arg...) dev_info(dev, fmt, ##arg)
#else
#define ISR_INFO(dev, fmt, arg...)
#endif

#define BMA2X2_SENSOR_IDENTIFICATION_ENABLE

#define SENSOR_NAME                 "bma2x2-accel"
#define ABSMIN                      -512
#define ABSMAX                      512
#define SLOPE_THRESHOLD_VALUE       32
#define SLOPE_DURATION_VALUE        1
#define INTERRUPT_LATCH_MODE        13
#define INTERRUPT_ENABLE            1
#define INTERRUPT_DISABLE           0
#define MAP_SLOPE_INTERRUPT         2
#define SLOPE_X_INDEX               5
#define SLOPE_Y_INDEX               6
#define SLOPE_Z_INDEX               7
#define BMA2X2_RANGE_SET            3 /* +/- 2G */
#define BMA2X2_RANGE_SHIFT          4 /* shift 4 bits for 2G */
#define BMA2X2_BW_SET               12 /* 125HZ  */

#define I2C_RETRY_DELAY()           usleep_range(1000, 2000)
/* wait 2ms for calibration ready */
#define WAIT_CAL_READY()            usleep_range(2000, 2500)
/* >3ms wait device ready */
#define WAIT_DEVICE_READY()         usleep_range(3000, 5000)
/* >5ms for device reset */
#define RESET_DELAY()               usleep_range(5000, 10000)
/* wait 10ms for self test  done */
#define SELF_TEST_DELAY()           usleep_range(10000, 15000)

#ifdef USE_BMA_INTERRUPT
#define LOW_G_INTERRUPT             REL_Z
#define HIGH_G_INTERRUPT            REL_HWHEEL
#define SLOP_INTERRUPT              REL_DIAL
#define DOUBLE_TAP_INTERRUPT        REL_WHEEL
#define SINGLE_TAP_INTERRUPT        REL_MISC
#define ORIENT_INTERRUPT            ABS_PRESSURE
#define FLAT_INTERRUPT              ABS_DISTANCE
#define SLOW_NO_MOTION_INTERRUPT    REL_Y
#else
/* AndroidM didn't use the dev-interrupt,bypass above defines */
#define LOW_G_INTERRUPT             REL_Z
#define HIGH_G_INTERRUPT            REL_Z
#define SLOP_INTERRUPT              REL_Z
#define DOUBLE_TAP_INTERRUPT        REL_Z
#define SINGLE_TAP_INTERRUPT        REL_Z
#define ORIENT_INTERRUPT            REL_Z
#define FLAT_INTERRUPT              REL_Z
#define SLOW_NO_MOTION_INTERRUPT    REL_Z
#endif

#define HIGH_G_INTERRUPT_X_HAPPENED                 1
#define HIGH_G_INTERRUPT_Y_HAPPENED                 2
#define HIGH_G_INTERRUPT_Z_HAPPENED                 3
#define HIGH_G_INTERRUPT_X_NEGATIVE_HAPPENED        4
#define HIGH_G_INTERRUPT_Y_NEGATIVE_HAPPENED        5
#define HIGH_G_INTERRUPT_Z_NEGATIVE_HAPPENED        6
#define SLOPE_INTERRUPT_X_HAPPENED                  7
#define SLOPE_INTERRUPT_Y_HAPPENED                  8
#define SLOPE_INTERRUPT_Z_HAPPENED                  9
#define SLOPE_INTERRUPT_X_NEGATIVE_HAPPENED         10
#define SLOPE_INTERRUPT_Y_NEGATIVE_HAPPENED         11
#define SLOPE_INTERRUPT_Z_NEGATIVE_HAPPENED         12
#define DOUBLE_TAP_INTERRUPT_HAPPENED               13
#define SINGLE_TAP_INTERRUPT_HAPPENED               14
#define UPWARD_PORTRAIT_UP_INTERRUPT_HAPPENED       15
#define UPWARD_PORTRAIT_DOWN_INTERRUPT_HAPPENED     16
#define UPWARD_LANDSCAPE_LEFT_INTERRUPT_HAPPENED    17
#define UPWARD_LANDSCAPE_RIGHT_INTERRUPT_HAPPENED   18
#define DOWNWARD_PORTRAIT_UP_INTERRUPT_HAPPENED     19
#define DOWNWARD_PORTRAIT_DOWN_INTERRUPT_HAPPENED   20
#define DOWNWARD_LANDSCAPE_LEFT_INTERRUPT_HAPPENED  21
#define DOWNWARD_LANDSCAPE_RIGHT_INTERRUPT_HAPPENED 22
#define FLAT_INTERRUPT_TRUE_HAPPENED                23
#define FLAT_INTERRUPT_FALSE_HAPPENED               24
#define LOW_G_INTERRUPT_HAPPENED                    25
#define SLOW_NO_MOTION_INTERRUPT_HAPPENED           26

#define PAD_LOWG                    0
#define PAD_HIGHG                   1
#define PAD_SLOP                    2
#define PAD_DOUBLE_TAP              3
#define PAD_SINGLE_TAP              4
#define PAD_ORIENT                  5
#define PAD_FLAT                    6
#define PAD_SLOW_NO_MOTION          7

#define BMA2X2_EEP_OFFSET                       0x16
#define BMA2X2_IMAGE_BASE                       0x38
#define BMA2X2_IMAGE_LEN                        22

#define BMA2X2_CHIP_ID_REG                      0x00
#define BMA2X2_VERSION_REG                      0x01
#define BMA2X2_X_AXIS_LSB_REG                   0x02
#define BMA2X2_X_AXIS_MSB_REG                   0x03
#define BMA2X2_Y_AXIS_LSB_REG                   0x04
#define BMA2X2_Y_AXIS_MSB_REG                   0x05
#define BMA2X2_Z_AXIS_LSB_REG                   0x06
#define BMA2X2_Z_AXIS_MSB_REG                   0x07
#define BMA2X2_TEMPERATURE_REG                  0x08
#define BMA2X2_STATUS1_REG                      0x09
#define BMA2X2_STATUS2_REG                      0x0A
#define BMA2X2_STATUS_TAP_SLOPE_REG             0x0B
#define BMA2X2_STATUS_ORIENT_HIGH_REG           0x0C
#define BMA2X2_STATUS_FIFO_REG                  0x0E
#define BMA2X2_RANGE_SEL_REG                    0x0F
#define BMA2X2_BW_SEL_REG                       0x10
#define BMA2X2_MODE_CTRL_REG                    0x11
#define BMA2X2_LOW_NOISE_CTRL_REG               0x12
#define BMA2X2_DATA_CTRL_REG                    0x13
#define BMA2X2_RESET_REG                        0x14
#define BMA2X2_INT_ENABLE1_REG                  0x16
#define BMA2X2_INT_ENABLE2_REG                  0x17
#define BMA2X2_INT_SLO_NO_MOT_REG               0x18
#define BMA2X2_INT1_PAD_SEL_REG                 0x19
#define BMA2X2_INT_DATA_SEL_REG                 0x1A
#define BMA2X2_INT2_PAD_SEL_REG                 0x1B
#define BMA2X2_INT_SRC_REG                      0x1E
#define BMA2X2_INT_SET_REG                      0x20
#define BMA2X2_INT_CTRL_REG                     0x21
#define BMA2X2_LOW_DURN_REG                     0x22
#define BMA2X2_LOW_THRES_REG                    0x23
#define BMA2X2_LOW_HIGH_HYST_REG                0x24
#define BMA2X2_HIGH_DURN_REG                    0x25
#define BMA2X2_HIGH_THRES_REG                   0x26
#define BMA2X2_SLOPE_DURN_REG                   0x27
#define BMA2X2_SLOPE_THRES_REG                  0x28
#define BMA2X2_SLO_NO_MOT_THRES_REG             0x29
#define BMA2X2_TAP_PARAM_REG                    0x2A
#define BMA2X2_TAP_THRES_REG                    0x2B
#define BMA2X2_ORIENT_PARAM_REG                 0x2C
#define BMA2X2_THETA_BLOCK_REG                  0x2D
#define BMA2X2_THETA_FLAT_REG                   0x2E
#define BMA2X2_FLAT_HOLD_TIME_REG               0x2F
#define BMA2X2_FIFO_WML_TRIG                    0x30
#define BMA2X2_SELF_TEST_REG                    0x32
#define BMA2X2_EEPROM_CTRL_REG                  0x33
#define BMA2X2_SERIAL_CTRL_REG                  0x34
#define BMA2X2_EXTMODE_CTRL_REG                 0x35
#define BMA2X2_OFFSET_CTRL_REG                  0x36
#define BMA2X2_OFFSET_PARAMS_REG                0x37
#define BMA2X2_OFFSET_X_AXIS_REG                0x38
#define BMA2X2_OFFSET_Y_AXIS_REG                0x39
#define BMA2X2_OFFSET_Z_AXIS_REG                0x3A
#define BMA2X2_GP0_REG                          0x3B
#define BMA2X2_GP1_REG                          0x3C
#define BMA2X2_FIFO_MODE_REG                    0x3E
#define BMA2X2_FIFO_DATA_OUTPUT_REG             0x3F

#define BMA2X2_CHIP_ID__POS             0
#define BMA2X2_CHIP_ID__MSK             0xFF
#define BMA2X2_CHIP_ID__LEN             8
#define BMA2X2_CHIP_ID__REG             BMA2X2_CHIP_ID_REG

#define BMA2X2_VERSION__POS          0
#define BMA2X2_VERSION__LEN          8
#define BMA2X2_VERSION__MSK          0xFF
#define BMA2X2_VERSION__REG          BMA2X2_VERSION_REG

#define BMA2x2_SLO_NO_MOT_DUR__POS   2
#define BMA2x2_SLO_NO_MOT_DUR__LEN   6
#define BMA2x2_SLO_NO_MOT_DUR__MSK   0xFC
#define BMA2x2_SLO_NO_MOT_DUR__REG   BMA2X2_SLOPE_DURN_REG

#define BMA2X2_NEW_DATA_X__POS          0
#define BMA2X2_NEW_DATA_X__LEN          1
#define BMA2X2_NEW_DATA_X__MSK          0x01
#define BMA2X2_NEW_DATA_X__REG          BMA2X2_X_AXIS_LSB_REG

#define BMA2X2_ACC_X14_LSB__POS           2
#define BMA2X2_ACC_X14_LSB__LEN           6
#define BMA2X2_ACC_X14_LSB__MSK           0xFC
#define BMA2X2_ACC_X14_LSB__REG           BMA2X2_X_AXIS_LSB_REG

#define BMA2X2_ACC_X12_LSB__POS           4
#define BMA2X2_ACC_X12_LSB__LEN           4
#define BMA2X2_ACC_X12_LSB__MSK           0xF0
#define BMA2X2_ACC_X12_LSB__REG           BMA2X2_X_AXIS_LSB_REG

#define BMA2X2_ACC_X10_LSB__POS           6
#define BMA2X2_ACC_X10_LSB__LEN           2
#define BMA2X2_ACC_X10_LSB__MSK           0xC0
#define BMA2X2_ACC_X10_LSB__REG           BMA2X2_X_AXIS_LSB_REG

#define BMA2X2_ACC_X8_LSB__POS           0
#define BMA2X2_ACC_X8_LSB__LEN           0
#define BMA2X2_ACC_X8_LSB__MSK           0x00
#define BMA2X2_ACC_X8_LSB__REG           BMA2X2_X_AXIS_LSB_REG

#define BMA2X2_ACC_X_MSB__POS           0
#define BMA2X2_ACC_X_MSB__LEN           8
#define BMA2X2_ACC_X_MSB__MSK           0xFF
#define BMA2X2_ACC_X_MSB__REG           BMA2X2_X_AXIS_MSB_REG

#define BMA2X2_NEW_DATA_Y__POS          0
#define BMA2X2_NEW_DATA_Y__LEN          1
#define BMA2X2_NEW_DATA_Y__MSK          0x01
#define BMA2X2_NEW_DATA_Y__REG          BMA2X2_Y_AXIS_LSB_REG

#define BMA2X2_ACC_Y14_LSB__POS           2
#define BMA2X2_ACC_Y14_LSB__LEN           6
#define BMA2X2_ACC_Y14_LSB__MSK           0xFC
#define BMA2X2_ACC_Y14_LSB__REG           BMA2X2_Y_AXIS_LSB_REG

#define BMA2X2_ACC_Y12_LSB__POS           4
#define BMA2X2_ACC_Y12_LSB__LEN           4
#define BMA2X2_ACC_Y12_LSB__MSK           0xF0
#define BMA2X2_ACC_Y12_LSB__REG           BMA2X2_Y_AXIS_LSB_REG

#define BMA2X2_ACC_Y10_LSB__POS           6
#define BMA2X2_ACC_Y10_LSB__LEN           2
#define BMA2X2_ACC_Y10_LSB__MSK           0xC0
#define BMA2X2_ACC_Y10_LSB__REG           BMA2X2_Y_AXIS_LSB_REG

#define BMA2X2_ACC_Y8_LSB__POS           0
#define BMA2X2_ACC_Y8_LSB__LEN           0
#define BMA2X2_ACC_Y8_LSB__MSK           0x00
#define BMA2X2_ACC_Y8_LSB__REG           BMA2X2_Y_AXIS_LSB_REG

#define BMA2X2_ACC_Y_MSB__POS           0
#define BMA2X2_ACC_Y_MSB__LEN           8
#define BMA2X2_ACC_Y_MSB__MSK           0xFF
#define BMA2X2_ACC_Y_MSB__REG           BMA2X2_Y_AXIS_MSB_REG

#define BMA2X2_NEW_DATA_Z__POS          0
#define BMA2X2_NEW_DATA_Z__LEN          1
#define BMA2X2_NEW_DATA_Z__MSK          0x01
#define BMA2X2_NEW_DATA_Z__REG          BMA2X2_Z_AXIS_LSB_REG

#define BMA2X2_ACC_Z14_LSB__POS           2
#define BMA2X2_ACC_Z14_LSB__LEN           6
#define BMA2X2_ACC_Z14_LSB__MSK           0xFC
#define BMA2X2_ACC_Z14_LSB__REG           BMA2X2_Z_AXIS_LSB_REG

#define BMA2X2_ACC_Z12_LSB__POS           4
#define BMA2X2_ACC_Z12_LSB__LEN           4
#define BMA2X2_ACC_Z12_LSB__MSK           0xF0
#define BMA2X2_ACC_Z12_LSB__REG           BMA2X2_Z_AXIS_LSB_REG

#define BMA2X2_ACC_Z10_LSB__POS           6
#define BMA2X2_ACC_Z10_LSB__LEN           2
#define BMA2X2_ACC_Z10_LSB__MSK           0xC0
#define BMA2X2_ACC_Z10_LSB__REG           BMA2X2_Z_AXIS_LSB_REG

#define BMA2X2_ACC_Z8_LSB__POS           0
#define BMA2X2_ACC_Z8_LSB__LEN           0
#define BMA2X2_ACC_Z8_LSB__MSK           0x00
#define BMA2X2_ACC_Z8_LSB__REG           BMA2X2_Z_AXIS_LSB_REG

#define BMA2X2_ACC_Z_MSB__POS           0
#define BMA2X2_ACC_Z_MSB__LEN           8
#define BMA2X2_ACC_Z_MSB__MSK           0xFF
#define BMA2X2_ACC_Z_MSB__REG           BMA2X2_Z_AXIS_MSB_REG

#define BMA2X2_TEMPERATURE__POS         0
#define BMA2X2_TEMPERATURE__LEN         8
#define BMA2X2_TEMPERATURE__MSK         0xFF
#define BMA2X2_TEMPERATURE__REG         BMA2X2_TEMP_RD_REG

#define BMA2X2_LOWG_INT_S__POS          0
#define BMA2X2_LOWG_INT_S__LEN          1
#define BMA2X2_LOWG_INT_S__MSK          0x01
#define BMA2X2_LOWG_INT_S__REG          BMA2X2_STATUS1_REG

#define BMA2X2_HIGHG_INT_S__POS          1
#define BMA2X2_HIGHG_INT_S__LEN          1
#define BMA2X2_HIGHG_INT_S__MSK          0x02
#define BMA2X2_HIGHG_INT_S__REG          BMA2X2_STATUS1_REG

#define BMA2X2_SLOPE_INT_S__POS          2
#define BMA2X2_SLOPE_INT_S__LEN          1
#define BMA2X2_SLOPE_INT_S__MSK          0x04
#define BMA2X2_SLOPE_INT_S__REG          BMA2X2_STATUS1_REG


#define BMA2X2_SLO_NO_MOT_INT_S__POS          3
#define BMA2X2_SLO_NO_MOT_INT_S__LEN          1
#define BMA2X2_SLO_NO_MOT_INT_S__MSK          0x08
#define BMA2X2_SLO_NO_MOT_INT_S__REG          BMA2X2_STATUS1_REG

#define BMA2X2_DOUBLE_TAP_INT_S__POS     4
#define BMA2X2_DOUBLE_TAP_INT_S__LEN     1
#define BMA2X2_DOUBLE_TAP_INT_S__MSK     0x10
#define BMA2X2_DOUBLE_TAP_INT_S__REG     BMA2X2_STATUS1_REG

#define BMA2X2_SINGLE_TAP_INT_S__POS     5
#define BMA2X2_SINGLE_TAP_INT_S__LEN     1
#define BMA2X2_SINGLE_TAP_INT_S__MSK     0x20
#define BMA2X2_SINGLE_TAP_INT_S__REG     BMA2X2_STATUS1_REG

#define BMA2X2_ORIENT_INT_S__POS         6
#define BMA2X2_ORIENT_INT_S__LEN         1
#define BMA2X2_ORIENT_INT_S__MSK         0x40
#define BMA2X2_ORIENT_INT_S__REG         BMA2X2_STATUS1_REG

#define BMA2X2_FLAT_INT_S__POS           7
#define BMA2X2_FLAT_INT_S__LEN           1
#define BMA2X2_FLAT_INT_S__MSK           0x80
#define BMA2X2_FLAT_INT_S__REG           BMA2X2_STATUS1_REG

#define BMA2X2_FIFO_FULL_INT_S__POS           5
#define BMA2X2_FIFO_FULL_INT_S__LEN           1
#define BMA2X2_FIFO_FULL_INT_S__MSK           0x20
#define BMA2X2_FIFO_FULL_INT_S__REG           BMA2X2_STATUS2_REG

#define BMA2X2_FIFO_WM_INT_S__POS           6
#define BMA2X2_FIFO_WM_INT_S__LEN           1
#define BMA2X2_FIFO_WM_INT_S__MSK           0x40
#define BMA2X2_FIFO_WM_INT_S__REG           BMA2X2_STATUS2_REG

#define BMA2X2_DATA_INT_S__POS           7
#define BMA2X2_DATA_INT_S__LEN           1
#define BMA2X2_DATA_INT_S__MSK           0x80
#define BMA2X2_DATA_INT_S__REG           BMA2X2_STATUS2_REG

#define BMA2X2_SLOPE_FIRST_X__POS        0
#define BMA2X2_SLOPE_FIRST_X__LEN        1
#define BMA2X2_SLOPE_FIRST_X__MSK        0x01
#define BMA2X2_SLOPE_FIRST_X__REG        BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_SLOPE_FIRST_Y__POS        1
#define BMA2X2_SLOPE_FIRST_Y__LEN        1
#define BMA2X2_SLOPE_FIRST_Y__MSK        0x02
#define BMA2X2_SLOPE_FIRST_Y__REG        BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_SLOPE_FIRST_Z__POS        2
#define BMA2X2_SLOPE_FIRST_Z__LEN        1
#define BMA2X2_SLOPE_FIRST_Z__MSK        0x04
#define BMA2X2_SLOPE_FIRST_Z__REG        BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_SLOPE_SIGN_S__POS         3
#define BMA2X2_SLOPE_SIGN_S__LEN         1
#define BMA2X2_SLOPE_SIGN_S__MSK         0x08
#define BMA2X2_SLOPE_SIGN_S__REG         BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_TAP_FIRST_X__POS        4
#define BMA2X2_TAP_FIRST_X__LEN        1
#define BMA2X2_TAP_FIRST_X__MSK        0x10
#define BMA2X2_TAP_FIRST_X__REG        BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_TAP_FIRST_Y__POS        5
#define BMA2X2_TAP_FIRST_Y__LEN        1
#define BMA2X2_TAP_FIRST_Y__MSK        0x20
#define BMA2X2_TAP_FIRST_Y__REG        BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_TAP_FIRST_Z__POS        6
#define BMA2X2_TAP_FIRST_Z__LEN        1
#define BMA2X2_TAP_FIRST_Z__MSK        0x40
#define BMA2X2_TAP_FIRST_Z__REG        BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_TAP_SIGN_S__POS         7
#define BMA2X2_TAP_SIGN_S__LEN         1
#define BMA2X2_TAP_SIGN_S__MSK         0x80
#define BMA2X2_TAP_SIGN_S__REG         BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_HIGHG_FIRST_X__POS        0
#define BMA2X2_HIGHG_FIRST_X__LEN        1
#define BMA2X2_HIGHG_FIRST_X__MSK        0x01
#define BMA2X2_HIGHG_FIRST_X__REG        BMA2X2_STATUS_ORIENT_HIGH_REG

#define BMA2X2_HIGHG_FIRST_Y__POS        1
#define BMA2X2_HIGHG_FIRST_Y__LEN        1
#define BMA2X2_HIGHG_FIRST_Y__MSK        0x02
#define BMA2X2_HIGHG_FIRST_Y__REG        BMA2X2_STATUS_ORIENT_HIGH_REG

#define BMA2X2_HIGHG_FIRST_Z__POS        2
#define BMA2X2_HIGHG_FIRST_Z__LEN        1
#define BMA2X2_HIGHG_FIRST_Z__MSK        0x04
#define BMA2X2_HIGHG_FIRST_Z__REG        BMA2X2_STATUS_ORIENT_HIGH_REG

#define BMA2X2_HIGHG_SIGN_S__POS         3
#define BMA2X2_HIGHG_SIGN_S__LEN         1
#define BMA2X2_HIGHG_SIGN_S__MSK         0x08
#define BMA2X2_HIGHG_SIGN_S__REG         BMA2X2_STATUS_ORIENT_HIGH_REG

#define BMA2X2_ORIENT_S__POS             4
#define BMA2X2_ORIENT_S__LEN             3
#define BMA2X2_ORIENT_S__MSK             0x70
#define BMA2X2_ORIENT_S__REG             BMA2X2_STATUS_ORIENT_HIGH_REG

#define BMA2X2_FLAT_S__POS               7
#define BMA2X2_FLAT_S__LEN               1
#define BMA2X2_FLAT_S__MSK               0x80
#define BMA2X2_FLAT_S__REG               BMA2X2_STATUS_ORIENT_HIGH_REG

#define BMA2X2_FIFO_FRAME_COUNTER_S__POS             0
#define BMA2X2_FIFO_FRAME_COUNTER_S__LEN             7
#define BMA2X2_FIFO_FRAME_COUNTER_S__MSK             0x7F
#define BMA2X2_FIFO_FRAME_COUNTER_S__REG             BMA2X2_STATUS_FIFO_REG

#define BMA2X2_FIFO_OVERRUN_S__POS             7
#define BMA2X2_FIFO_OVERRUN_S__LEN             1
#define BMA2X2_FIFO_OVERRUN_S__MSK             0x80
#define BMA2X2_FIFO_OVERRUN_S__REG             BMA2X2_STATUS_FIFO_REG

#define BMA2X2_RANGE_SEL__POS             0
#define BMA2X2_RANGE_SEL__LEN             4
#define BMA2X2_RANGE_SEL__MSK             0x0F
#define BMA2X2_RANGE_SEL__REG             BMA2X2_RANGE_SEL_REG

#define BMA2X2_BANDWIDTH__POS             0
#define BMA2X2_BANDWIDTH__LEN             5
#define BMA2X2_BANDWIDTH__MSK             0x1F
#define BMA2X2_BANDWIDTH__REG             BMA2X2_BW_SEL_REG

#define BMA2X2_SLEEP_DUR__POS             1
#define BMA2X2_SLEEP_DUR__LEN             4
#define BMA2X2_SLEEP_DUR__MSK             0x1E
#define BMA2X2_SLEEP_DUR__REG             BMA2X2_MODE_CTRL_REG

#define BMA2X2_MODE_CTRL__POS             5
#define BMA2X2_MODE_CTRL__LEN             3
#define BMA2X2_MODE_CTRL__MSK             0xE0
#define BMA2X2_MODE_CTRL__REG             BMA2X2_MODE_CTRL_REG

#define BMA2X2_DEEP_SUSPEND__POS          5
#define BMA2X2_DEEP_SUSPEND__LEN          1
#define BMA2X2_DEEP_SUSPEND__MSK          0x20
#define BMA2X2_DEEP_SUSPEND__REG          BMA2X2_MODE_CTRL_REG

#define BMA2X2_EN_LOW_POWER__POS          6
#define BMA2X2_EN_LOW_POWER__LEN          1
#define BMA2X2_EN_LOW_POWER__MSK          0x40
#define BMA2X2_EN_LOW_POWER__REG          BMA2X2_MODE_CTRL_REG

#define BMA2X2_EN_SUSPEND__POS            7
#define BMA2X2_EN_SUSPEND__LEN            1
#define BMA2X2_EN_SUSPEND__MSK            0x80
#define BMA2X2_EN_SUSPEND__REG            BMA2X2_MODE_CTRL_REG

#define BMA2X2_SLEEP_TIMER__POS          5
#define BMA2X2_SLEEP_TIMER__LEN          1
#define BMA2X2_SLEEP_TIMER__MSK          0x20
#define BMA2X2_SLEEP_TIMER__REG          BMA2X2_LOW_NOISE_CTRL_REG

#define BMA2X2_LOW_POWER_MODE__POS          6
#define BMA2X2_LOW_POWER_MODE__LEN          1
#define BMA2X2_LOW_POWER_MODE__MSK          0x40
#define BMA2X2_LOW_POWER_MODE__REG          BMA2X2_LOW_NOISE_CTRL_REG

#define BMA2X2_EN_LOW_NOISE__POS          7
#define BMA2X2_EN_LOW_NOISE__LEN          1
#define BMA2X2_EN_LOW_NOISE__MSK          0x80
#define BMA2X2_EN_LOW_NOISE__REG          BMA2X2_LOW_NOISE_CTRL_REG

#define BMA2X2_DIS_SHADOW_PROC__POS       6
#define BMA2X2_DIS_SHADOW_PROC__LEN       1
#define BMA2X2_DIS_SHADOW_PROC__MSK       0x40
#define BMA2X2_DIS_SHADOW_PROC__REG       BMA2X2_DATA_CTRL_REG

#define BMA2X2_EN_DATA_HIGH_BW__POS         7
#define BMA2X2_EN_DATA_HIGH_BW__LEN         1
#define BMA2X2_EN_DATA_HIGH_BW__MSK         0x80
#define BMA2X2_EN_DATA_HIGH_BW__REG         BMA2X2_DATA_CTRL_REG

#define BMA2X2_EN_SOFT_RESET__POS         0
#define BMA2X2_EN_SOFT_RESET__LEN         8
#define BMA2X2_EN_SOFT_RESET__MSK         0xFF
#define BMA2X2_EN_SOFT_RESET__REG         BMA2X2_RESET_REG

#define BMA2X2_EN_SOFT_RESET_VALUE        0xB6

#define BMA2X2_EN_SLOPE_X_INT__POS         0
#define BMA2X2_EN_SLOPE_X_INT__LEN         1
#define BMA2X2_EN_SLOPE_X_INT__MSK         0x01
#define BMA2X2_EN_SLOPE_X_INT__REG         BMA2X2_INT_ENABLE1_REG

#define BMA2X2_EN_SLOPE_Y_INT__POS         1
#define BMA2X2_EN_SLOPE_Y_INT__LEN         1
#define BMA2X2_EN_SLOPE_Y_INT__MSK         0x02
#define BMA2X2_EN_SLOPE_Y_INT__REG         BMA2X2_INT_ENABLE1_REG

#define BMA2X2_EN_SLOPE_Z_INT__POS         2
#define BMA2X2_EN_SLOPE_Z_INT__LEN         1
#define BMA2X2_EN_SLOPE_Z_INT__MSK         0x04
#define BMA2X2_EN_SLOPE_Z_INT__REG         BMA2X2_INT_ENABLE1_REG

#define BMA2X2_EN_DOUBLE_TAP_INT__POS      4
#define BMA2X2_EN_DOUBLE_TAP_INT__LEN      1
#define BMA2X2_EN_DOUBLE_TAP_INT__MSK      0x10
#define BMA2X2_EN_DOUBLE_TAP_INT__REG      BMA2X2_INT_ENABLE1_REG

#define BMA2X2_EN_SINGLE_TAP_INT__POS      5
#define BMA2X2_EN_SINGLE_TAP_INT__LEN      1
#define BMA2X2_EN_SINGLE_TAP_INT__MSK      0x20
#define BMA2X2_EN_SINGLE_TAP_INT__REG      BMA2X2_INT_ENABLE1_REG

#define BMA2X2_EN_ORIENT_INT__POS          6
#define BMA2X2_EN_ORIENT_INT__LEN          1
#define BMA2X2_EN_ORIENT_INT__MSK          0x40
#define BMA2X2_EN_ORIENT_INT__REG          BMA2X2_INT_ENABLE1_REG

#define BMA2X2_EN_FLAT_INT__POS            7
#define BMA2X2_EN_FLAT_INT__LEN            1
#define BMA2X2_EN_FLAT_INT__MSK            0x80
#define BMA2X2_EN_FLAT_INT__REG            BMA2X2_INT_ENABLE1_REG

#define BMA2X2_EN_HIGHG_X_INT__POS         0
#define BMA2X2_EN_HIGHG_X_INT__LEN         1
#define BMA2X2_EN_HIGHG_X_INT__MSK         0x01
#define BMA2X2_EN_HIGHG_X_INT__REG         BMA2X2_INT_ENABLE2_REG

#define BMA2X2_EN_HIGHG_Y_INT__POS         1
#define BMA2X2_EN_HIGHG_Y_INT__LEN         1
#define BMA2X2_EN_HIGHG_Y_INT__MSK         0x02
#define BMA2X2_EN_HIGHG_Y_INT__REG         BMA2X2_INT_ENABLE2_REG

#define BMA2X2_EN_HIGHG_Z_INT__POS         2
#define BMA2X2_EN_HIGHG_Z_INT__LEN         1
#define BMA2X2_EN_HIGHG_Z_INT__MSK         0x04
#define BMA2X2_EN_HIGHG_Z_INT__REG         BMA2X2_INT_ENABLE2_REG

#define BMA2X2_EN_LOWG_INT__POS            3
#define BMA2X2_EN_LOWG_INT__LEN            1
#define BMA2X2_EN_LOWG_INT__MSK            0x08
#define BMA2X2_EN_LOWG_INT__REG            BMA2X2_INT_ENABLE2_REG

#define BMA2X2_EN_NEW_DATA_INT__POS        4
#define BMA2X2_EN_NEW_DATA_INT__LEN        1
#define BMA2X2_EN_NEW_DATA_INT__MSK        0x10
#define BMA2X2_EN_NEW_DATA_INT__REG        BMA2X2_INT_ENABLE2_REG

#define BMA2X2_INT_FFULL_EN_INT__POS        5
#define BMA2X2_INT_FFULL_EN_INT__LEN        1
#define BMA2X2_INT_FFULL_EN_INT__MSK        0x20
#define BMA2X2_INT_FFULL_EN_INT__REG        BMA2X2_INT_ENABLE2_REG

#define BMA2X2_INT_FWM_EN_INT__POS        6
#define BMA2X2_INT_FWM_EN_INT__LEN        1
#define BMA2X2_INT_FWM_EN_INT__MSK        0x40
#define BMA2X2_INT_FWM_EN_INT__REG        BMA2X2_INT_ENABLE2_REG

#define BMA2X2_INT_SLO_NO_MOT_EN_X_INT__POS        0
#define BMA2X2_INT_SLO_NO_MOT_EN_X_INT__LEN        1
#define BMA2X2_INT_SLO_NO_MOT_EN_X_INT__MSK        0x01
#define BMA2X2_INT_SLO_NO_MOT_EN_X_INT__REG        BMA2X2_INT_SLO_NO_MOT_REG

#define BMA2X2_INT_SLO_NO_MOT_EN_Y_INT__POS        1
#define BMA2X2_INT_SLO_NO_MOT_EN_Y_INT__LEN        1
#define BMA2X2_INT_SLO_NO_MOT_EN_Y_INT__MSK        0x02
#define BMA2X2_INT_SLO_NO_MOT_EN_Y_INT__REG        BMA2X2_INT_SLO_NO_MOT_REG

#define BMA2X2_INT_SLO_NO_MOT_EN_Z_INT__POS        2
#define BMA2X2_INT_SLO_NO_MOT_EN_Z_INT__LEN        1
#define BMA2X2_INT_SLO_NO_MOT_EN_Z_INT__MSK        0x04
#define BMA2X2_INT_SLO_NO_MOT_EN_Z_INT__REG        BMA2X2_INT_SLO_NO_MOT_REG

#define BMA2X2_INT_SLO_NO_MOT_EN_SEL_INT__POS        3
#define BMA2X2_INT_SLO_NO_MOT_EN_SEL_INT__LEN        1
#define BMA2X2_INT_SLO_NO_MOT_EN_SEL_INT__MSK        0x08
#define BMA2X2_INT_SLO_NO_MOT_EN_SEL_INT__REG        BMA2X2_INT_SLO_NO_MOT_REG

#define BMA2X2_EN_INT1_PAD_LOWG__POS        0
#define BMA2X2_EN_INT1_PAD_LOWG__LEN        1
#define BMA2X2_EN_INT1_PAD_LOWG__MSK        0x01
#define BMA2X2_EN_INT1_PAD_LOWG__REG        BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_HIGHG__POS       1
#define BMA2X2_EN_INT1_PAD_HIGHG__LEN       1
#define BMA2X2_EN_INT1_PAD_HIGHG__MSK       0x02
#define BMA2X2_EN_INT1_PAD_HIGHG__REG       BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_SLOPE__POS       2
#define BMA2X2_EN_INT1_PAD_SLOPE__LEN       1
#define BMA2X2_EN_INT1_PAD_SLOPE__MSK       0x04
#define BMA2X2_EN_INT1_PAD_SLOPE__REG       BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_SLO_NO_MOT__POS        3
#define BMA2X2_EN_INT1_PAD_SLO_NO_MOT__LEN        1
#define BMA2X2_EN_INT1_PAD_SLO_NO_MOT__MSK        0x08
#define BMA2X2_EN_INT1_PAD_SLO_NO_MOT__REG        BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_DB_TAP__POS      4
#define BMA2X2_EN_INT1_PAD_DB_TAP__LEN      1
#define BMA2X2_EN_INT1_PAD_DB_TAP__MSK      0x10
#define BMA2X2_EN_INT1_PAD_DB_TAP__REG      BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_SNG_TAP__POS     5
#define BMA2X2_EN_INT1_PAD_SNG_TAP__LEN     1
#define BMA2X2_EN_INT1_PAD_SNG_TAP__MSK     0x20
#define BMA2X2_EN_INT1_PAD_SNG_TAP__REG     BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_ORIENT__POS      6
#define BMA2X2_EN_INT1_PAD_ORIENT__LEN      1
#define BMA2X2_EN_INT1_PAD_ORIENT__MSK      0x40
#define BMA2X2_EN_INT1_PAD_ORIENT__REG      BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_FLAT__POS        7
#define BMA2X2_EN_INT1_PAD_FLAT__LEN        1
#define BMA2X2_EN_INT1_PAD_FLAT__MSK        0x80
#define BMA2X2_EN_INT1_PAD_FLAT__REG        BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_LOWG__POS        0
#define BMA2X2_EN_INT2_PAD_LOWG__LEN        1
#define BMA2X2_EN_INT2_PAD_LOWG__MSK        0x01
#define BMA2X2_EN_INT2_PAD_LOWG__REG        BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_HIGHG__POS       1
#define BMA2X2_EN_INT2_PAD_HIGHG__LEN       1
#define BMA2X2_EN_INT2_PAD_HIGHG__MSK       0x02
#define BMA2X2_EN_INT2_PAD_HIGHG__REG       BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_SLOPE__POS       2
#define BMA2X2_EN_INT2_PAD_SLOPE__LEN       1
#define BMA2X2_EN_INT2_PAD_SLOPE__MSK       0x04
#define BMA2X2_EN_INT2_PAD_SLOPE__REG       BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_SLO_NO_MOT__POS        3
#define BMA2X2_EN_INT2_PAD_SLO_NO_MOT__LEN        1
#define BMA2X2_EN_INT2_PAD_SLO_NO_MOT__MSK        0x08
#define BMA2X2_EN_INT2_PAD_SLO_NO_MOT__REG        BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_DB_TAP__POS      4
#define BMA2X2_EN_INT2_PAD_DB_TAP__LEN      1
#define BMA2X2_EN_INT2_PAD_DB_TAP__MSK      0x10
#define BMA2X2_EN_INT2_PAD_DB_TAP__REG      BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_SNG_TAP__POS     5
#define BMA2X2_EN_INT2_PAD_SNG_TAP__LEN     1
#define BMA2X2_EN_INT2_PAD_SNG_TAP__MSK     0x20
#define BMA2X2_EN_INT2_PAD_SNG_TAP__REG     BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_ORIENT__POS      6
#define BMA2X2_EN_INT2_PAD_ORIENT__LEN      1
#define BMA2X2_EN_INT2_PAD_ORIENT__MSK      0x40
#define BMA2X2_EN_INT2_PAD_ORIENT__REG      BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_FLAT__POS        7
#define BMA2X2_EN_INT2_PAD_FLAT__LEN        1
#define BMA2X2_EN_INT2_PAD_FLAT__MSK        0x80
#define BMA2X2_EN_INT2_PAD_FLAT__REG        BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_NEWDATA__POS     0
#define BMA2X2_EN_INT1_PAD_NEWDATA__LEN     1
#define BMA2X2_EN_INT1_PAD_NEWDATA__MSK     0x01
#define BMA2X2_EN_INT1_PAD_NEWDATA__REG     BMA2X2_INT_DATA_SEL_REG

#define BMA2X2_EN_INT1_PAD_FWM__POS     1
#define BMA2X2_EN_INT1_PAD_FWM__LEN     1
#define BMA2X2_EN_INT1_PAD_FWM__MSK     0x02
#define BMA2X2_EN_INT1_PAD_FWM__REG     BMA2X2_INT_DATA_SEL_REG

#define BMA2X2_EN_INT1_PAD_FFULL__POS     2
#define BMA2X2_EN_INT1_PAD_FFULL__LEN     1
#define BMA2X2_EN_INT1_PAD_FFULL__MSK     0x04
#define BMA2X2_EN_INT1_PAD_FFULL__REG     BMA2X2_INT_DATA_SEL_REG

#define BMA2X2_EN_INT2_PAD_FFULL__POS     5
#define BMA2X2_EN_INT2_PAD_FFULL__LEN     1
#define BMA2X2_EN_INT2_PAD_FFULL__MSK     0x20
#define BMA2X2_EN_INT2_PAD_FFULL__REG     BMA2X2_INT_DATA_SEL_REG

#define BMA2X2_EN_INT2_PAD_FWM__POS     6
#define BMA2X2_EN_INT2_PAD_FWM__LEN     1
#define BMA2X2_EN_INT2_PAD_FWM__MSK     0x40
#define BMA2X2_EN_INT2_PAD_FWM__REG     BMA2X2_INT_DATA_SEL_REG

#define BMA2X2_EN_INT2_PAD_NEWDATA__POS     7
#define BMA2X2_EN_INT2_PAD_NEWDATA__LEN     1
#define BMA2X2_EN_INT2_PAD_NEWDATA__MSK     0x80
#define BMA2X2_EN_INT2_PAD_NEWDATA__REG     BMA2X2_INT_DATA_SEL_REG

#define BMA2X2_UNFILT_INT_SRC_LOWG__POS        0
#define BMA2X2_UNFILT_INT_SRC_LOWG__LEN        1
#define BMA2X2_UNFILT_INT_SRC_LOWG__MSK        0x01
#define BMA2X2_UNFILT_INT_SRC_LOWG__REG        BMA2X2_INT_SRC_REG

#define BMA2X2_UNFILT_INT_SRC_HIGHG__POS       1
#define BMA2X2_UNFILT_INT_SRC_HIGHG__LEN       1
#define BMA2X2_UNFILT_INT_SRC_HIGHG__MSK       0x02
#define BMA2X2_UNFILT_INT_SRC_HIGHG__REG       BMA2X2_INT_SRC_REG

#define BMA2X2_UNFILT_INT_SRC_SLOPE__POS       2
#define BMA2X2_UNFILT_INT_SRC_SLOPE__LEN       1
#define BMA2X2_UNFILT_INT_SRC_SLOPE__MSK       0x04
#define BMA2X2_UNFILT_INT_SRC_SLOPE__REG       BMA2X2_INT_SRC_REG

#define BMA2X2_UNFILT_INT_SRC_SLO_NO_MOT__POS        3
#define BMA2X2_UNFILT_INT_SRC_SLO_NO_MOT__LEN        1
#define BMA2X2_UNFILT_INT_SRC_SLO_NO_MOT__MSK        0x08
#define BMA2X2_UNFILT_INT_SRC_SLO_NO_MOT__REG        BMA2X2_INT_SRC_REG

#define BMA2X2_UNFILT_INT_SRC_TAP__POS         4
#define BMA2X2_UNFILT_INT_SRC_TAP__LEN         1
#define BMA2X2_UNFILT_INT_SRC_TAP__MSK         0x10
#define BMA2X2_UNFILT_INT_SRC_TAP__REG         BMA2X2_INT_SRC_REG

#define BMA2X2_UNFILT_INT_SRC_DATA__POS        5
#define BMA2X2_UNFILT_INT_SRC_DATA__LEN        1
#define BMA2X2_UNFILT_INT_SRC_DATA__MSK        0x20
#define BMA2X2_UNFILT_INT_SRC_DATA__REG        BMA2X2_INT_SRC_REG

#define BMA2X2_INT1_PAD_ACTIVE_LEVEL__POS       0
#define BMA2X2_INT1_PAD_ACTIVE_LEVEL__LEN       1
#define BMA2X2_INT1_PAD_ACTIVE_LEVEL__MSK       0x01
#define BMA2X2_INT1_PAD_ACTIVE_LEVEL__REG       BMA2X2_INT_SET_REG

#define BMA2X2_INT2_PAD_ACTIVE_LEVEL__POS       2
#define BMA2X2_INT2_PAD_ACTIVE_LEVEL__LEN       1
#define BMA2X2_INT2_PAD_ACTIVE_LEVEL__MSK       0x04
#define BMA2X2_INT2_PAD_ACTIVE_LEVEL__REG       BMA2X2_INT_SET_REG

#define BMA2X2_INT1_PAD_OUTPUT_TYPE__POS        1
#define BMA2X2_INT1_PAD_OUTPUT_TYPE__LEN        1
#define BMA2X2_INT1_PAD_OUTPUT_TYPE__MSK        0x02
#define BMA2X2_INT1_PAD_OUTPUT_TYPE__REG        BMA2X2_INT_SET_REG

#define BMA2X2_INT2_PAD_OUTPUT_TYPE__POS        3
#define BMA2X2_INT2_PAD_OUTPUT_TYPE__LEN        1
#define BMA2X2_INT2_PAD_OUTPUT_TYPE__MSK        0x08
#define BMA2X2_INT2_PAD_OUTPUT_TYPE__REG        BMA2X2_INT_SET_REG

#define BMA2X2_INT_MODE_SEL__POS                0
#define BMA2X2_INT_MODE_SEL__LEN                4
#define BMA2X2_INT_MODE_SEL__MSK                0x0F
#define BMA2X2_INT_MODE_SEL__REG                BMA2X2_INT_CTRL_REG

#define BMA2X2_RESET_INT__POS           7
#define BMA2X2_RESET_INT__LEN           1
#define BMA2X2_RESET_INT__MSK           0x80
#define BMA2X2_RESET_INT__REG           BMA2X2_INT_CTRL_REG

#define BMA2X2_LOWG_DUR__POS                    0
#define BMA2X2_LOWG_DUR__LEN                    8
#define BMA2X2_LOWG_DUR__MSK                    0xFF
#define BMA2X2_LOWG_DUR__REG                    BMA2X2_LOW_DURN_REG

#define BMA2X2_LOWG_THRES__POS                  0
#define BMA2X2_LOWG_THRES__LEN                  8
#define BMA2X2_LOWG_THRES__MSK                  0xFF
#define BMA2X2_LOWG_THRES__REG                  BMA2X2_LOW_THRES_REG

#define BMA2X2_LOWG_HYST__POS                   0
#define BMA2X2_LOWG_HYST__LEN                   2
#define BMA2X2_LOWG_HYST__MSK                   0x03
#define BMA2X2_LOWG_HYST__REG                   BMA2X2_LOW_HIGH_HYST_REG

#define BMA2X2_LOWG_INT_MODE__POS               2
#define BMA2X2_LOWG_INT_MODE__LEN               1
#define BMA2X2_LOWG_INT_MODE__MSK               0x04
#define BMA2X2_LOWG_INT_MODE__REG               BMA2X2_LOW_HIGH_HYST_REG

#define BMA2X2_HIGHG_DUR__POS                    0
#define BMA2X2_HIGHG_DUR__LEN                    8
#define BMA2X2_HIGHG_DUR__MSK                    0xFF
#define BMA2X2_HIGHG_DUR__REG                    BMA2X2_HIGH_DURN_REG

#define BMA2X2_HIGHG_THRES__POS                  0
#define BMA2X2_HIGHG_THRES__LEN                  8
#define BMA2X2_HIGHG_THRES__MSK                  0xFF
#define BMA2X2_HIGHG_THRES__REG                  BMA2X2_HIGH_THRES_REG

#define BMA2X2_HIGHG_HYST__POS                  6
#define BMA2X2_HIGHG_HYST__LEN                  2
#define BMA2X2_HIGHG_HYST__MSK                  0xC0
#define BMA2X2_HIGHG_HYST__REG                  BMA2X2_LOW_HIGH_HYST_REG

#define BMA2X2_SLOPE_DUR__POS                    0
#define BMA2X2_SLOPE_DUR__LEN                    2
#define BMA2X2_SLOPE_DUR__MSK                    0x03
#define BMA2X2_SLOPE_DUR__REG                    BMA2X2_SLOPE_DURN_REG

#define BMA2X2_SLO_NO_MOT_DUR__POS                    2
#define BMA2X2_SLO_NO_MOT_DUR__LEN                    6
#define BMA2X2_SLO_NO_MOT_DUR__MSK                    0xFC
#define BMA2X2_SLO_NO_MOT_DUR__REG                    BMA2X2_SLOPE_DURN_REG

#define BMA2X2_SLOPE_THRES__POS                  0
#define BMA2X2_SLOPE_THRES__LEN                  8
#define BMA2X2_SLOPE_THRES__MSK                  0xFF
#define BMA2X2_SLOPE_THRES__REG                  BMA2X2_SLOPE_THRES_REG

#define BMA2X2_SLO_NO_MOT_THRES__POS                  0
#define BMA2X2_SLO_NO_MOT_THRES__LEN                  8
#define BMA2X2_SLO_NO_MOT_THRES__MSK                  0xFF
#define BMA2X2_SLO_NO_MOT_THRES__REG           BMA2X2_SLO_NO_MOT_THRES_REG

#define BMA2X2_TAP_DUR__POS                    0
#define BMA2X2_TAP_DUR__LEN                    3
#define BMA2X2_TAP_DUR__MSK                    0x07
#define BMA2X2_TAP_DUR__REG                    BMA2X2_TAP_PARAM_REG

#define BMA2X2_TAP_SHOCK_DURN__POS             6
#define BMA2X2_TAP_SHOCK_DURN__LEN             1
#define BMA2X2_TAP_SHOCK_DURN__MSK             0x40
#define BMA2X2_TAP_SHOCK_DURN__REG             BMA2X2_TAP_PARAM_REG

#define BMA2X2_ADV_TAP_INT__POS                5
#define BMA2X2_ADV_TAP_INT__LEN                1
#define BMA2X2_ADV_TAP_INT__MSK                0x20
#define BMA2X2_ADV_TAP_INT__REG                BMA2X2_TAP_PARAM_REG

#define BMA2X2_TAP_QUIET_DURN__POS             7
#define BMA2X2_TAP_QUIET_DURN__LEN             1
#define BMA2X2_TAP_QUIET_DURN__MSK             0x80
#define BMA2X2_TAP_QUIET_DURN__REG             BMA2X2_TAP_PARAM_REG

#define BMA2X2_TAP_THRES__POS                  0
#define BMA2X2_TAP_THRES__LEN                  5
#define BMA2X2_TAP_THRES__MSK                  0x1F
#define BMA2X2_TAP_THRES__REG                  BMA2X2_TAP_THRES_REG

#define BMA2X2_TAP_SAMPLES__POS                6
#define BMA2X2_TAP_SAMPLES__LEN                2
#define BMA2X2_TAP_SAMPLES__MSK                0xC0
#define BMA2X2_TAP_SAMPLES__REG                BMA2X2_TAP_THRES_REG

#define BMA2X2_ORIENT_MODE__POS                  0
#define BMA2X2_ORIENT_MODE__LEN                  2
#define BMA2X2_ORIENT_MODE__MSK                  0x03
#define BMA2X2_ORIENT_MODE__REG                  BMA2X2_ORIENT_PARAM_REG

#define BMA2X2_ORIENT_BLOCK__POS                 2
#define BMA2X2_ORIENT_BLOCK__LEN                 2
#define BMA2X2_ORIENT_BLOCK__MSK                 0x0C
#define BMA2X2_ORIENT_BLOCK__REG                 BMA2X2_ORIENT_PARAM_REG

#define BMA2X2_ORIENT_HYST__POS                  4
#define BMA2X2_ORIENT_HYST__LEN                  3
#define BMA2X2_ORIENT_HYST__MSK                  0x70
#define BMA2X2_ORIENT_HYST__REG                  BMA2X2_ORIENT_PARAM_REG

#define BMA2X2_ORIENT_AXIS__POS                  7
#define BMA2X2_ORIENT_AXIS__LEN                  1
#define BMA2X2_ORIENT_AXIS__MSK                  0x80
#define BMA2X2_ORIENT_AXIS__REG                  BMA2X2_THETA_BLOCK_REG

#define BMA2X2_ORIENT_UD_EN__POS                  6
#define BMA2X2_ORIENT_UD_EN__LEN                  1
#define BMA2X2_ORIENT_UD_EN__MSK                  0x40
#define BMA2X2_ORIENT_UD_EN__REG                  BMA2X2_THETA_BLOCK_REG

#define BMA2X2_THETA_BLOCK__POS                  0
#define BMA2X2_THETA_BLOCK__LEN                  6
#define BMA2X2_THETA_BLOCK__MSK                  0x3F
#define BMA2X2_THETA_BLOCK__REG                  BMA2X2_THETA_BLOCK_REG

#define BMA2X2_THETA_FLAT__POS                  0
#define BMA2X2_THETA_FLAT__LEN                  6
#define BMA2X2_THETA_FLAT__MSK                  0x3F
#define BMA2X2_THETA_FLAT__REG                  BMA2X2_THETA_FLAT_REG

#define BMA2X2_FLAT_HOLD_TIME__POS              4
#define BMA2X2_FLAT_HOLD_TIME__LEN              2
#define BMA2X2_FLAT_HOLD_TIME__MSK              0x30
#define BMA2X2_FLAT_HOLD_TIME__REG              BMA2X2_FLAT_HOLD_TIME_REG

#define BMA2X2_FLAT_HYS__POS                   0
#define BMA2X2_FLAT_HYS__LEN                   3
#define BMA2X2_FLAT_HYS__MSK                   0x07
#define BMA2X2_FLAT_HYS__REG                   BMA2X2_FLAT_HOLD_TIME_REG

#define BMA2X2_FIFO_WML_TRIG_RETAIN__POS                   0
#define BMA2X2_FIFO_WML_TRIG_RETAIN__LEN                   6
#define BMA2X2_FIFO_WML_TRIG_RETAIN__MSK                   0x3F
#define BMA2X2_FIFO_WML_TRIG_RETAIN__REG                   BMA2X2_FIFO_WML_TRIG

#define BMA2X2_EN_SELF_TEST__POS                0
#define BMA2X2_EN_SELF_TEST__LEN                2
#define BMA2X2_EN_SELF_TEST__MSK                0x03
#define BMA2X2_EN_SELF_TEST__REG                BMA2X2_SELF_TEST_REG

#define BMA2X2_NEG_SELF_TEST__POS               2
#define BMA2X2_NEG_SELF_TEST__LEN               1
#define BMA2X2_NEG_SELF_TEST__MSK               0x04
#define BMA2X2_NEG_SELF_TEST__REG               BMA2X2_SELF_TEST_REG

#define BMA2X2_SELF_TEST_AMP__POS               4
#define BMA2X2_SELF_TEST_AMP__LEN               1
#define BMA2X2_SELF_TEST_AMP__MSK               0x10
#define BMA2X2_SELF_TEST_AMP__REG               BMA2X2_SELF_TEST_REG


#define BMA2X2_UNLOCK_EE_PROG_MODE__POS     0
#define BMA2X2_UNLOCK_EE_PROG_MODE__LEN     1
#define BMA2X2_UNLOCK_EE_PROG_MODE__MSK     0x01
#define BMA2X2_UNLOCK_EE_PROG_MODE__REG     BMA2X2_EEPROM_CTRL_REG

#define BMA2X2_START_EE_PROG_TRIG__POS      1
#define BMA2X2_START_EE_PROG_TRIG__LEN      1
#define BMA2X2_START_EE_PROG_TRIG__MSK      0x02
#define BMA2X2_START_EE_PROG_TRIG__REG      BMA2X2_EEPROM_CTRL_REG

#define BMA2X2_EE_PROG_READY__POS          2
#define BMA2X2_EE_PROG_READY__LEN          1
#define BMA2X2_EE_PROG_READY__MSK          0x04
#define BMA2X2_EE_PROG_READY__REG          BMA2X2_EEPROM_CTRL_REG

#define BMA2X2_UPDATE_IMAGE__POS                3
#define BMA2X2_UPDATE_IMAGE__LEN                1
#define BMA2X2_UPDATE_IMAGE__MSK                0x08
#define BMA2X2_UPDATE_IMAGE__REG                BMA2X2_EEPROM_CTRL_REG

#define BMA2X2_EE_REMAIN__POS                4
#define BMA2X2_EE_REMAIN__LEN                4
#define BMA2X2_EE_REMAIN__MSK                0xF0
#define BMA2X2_EE_REMAIN__REG                BMA2X2_EEPROM_CTRL_REG

#define BMA2X2_EN_SPI_MODE_3__POS              0
#define BMA2X2_EN_SPI_MODE_3__LEN              1
#define BMA2X2_EN_SPI_MODE_3__MSK              0x01
#define BMA2X2_EN_SPI_MODE_3__REG              BMA2X2_SERIAL_CTRL_REG

#define BMA2X2_I2C_WATCHDOG_PERIOD__POS        1
#define BMA2X2_I2C_WATCHDOG_PERIOD__LEN        1
#define BMA2X2_I2C_WATCHDOG_PERIOD__MSK        0x02
#define BMA2X2_I2C_WATCHDOG_PERIOD__REG        BMA2X2_SERIAL_CTRL_REG

#define BMA2X2_EN_I2C_WATCHDOG__POS            2
#define BMA2X2_EN_I2C_WATCHDOG__LEN            1
#define BMA2X2_EN_I2C_WATCHDOG__MSK            0x04
#define BMA2X2_EN_I2C_WATCHDOG__REG            BMA2X2_SERIAL_CTRL_REG

#define BMA2X2_EXT_MODE__POS              7
#define BMA2X2_EXT_MODE__LEN              1
#define BMA2X2_EXT_MODE__MSK              0x80
#define BMA2X2_EXT_MODE__REG              BMA2X2_EXTMODE_CTRL_REG

#define BMA2X2_ALLOW_UPPER__POS        6
#define BMA2X2_ALLOW_UPPER__LEN        1
#define BMA2X2_ALLOW_UPPER__MSK        0x40
#define BMA2X2_ALLOW_UPPER__REG        BMA2X2_EXTMODE_CTRL_REG

#define BMA2X2_MAP_2_LOWER__POS            5
#define BMA2X2_MAP_2_LOWER__LEN            1
#define BMA2X2_MAP_2_LOWER__MSK            0x20
#define BMA2X2_MAP_2_LOWER__REG            BMA2X2_EXTMODE_CTRL_REG

#define BMA2X2_MAGIC_NUMBER__POS            0
#define BMA2X2_MAGIC_NUMBER__LEN            5
#define BMA2X2_MAGIC_NUMBER__MSK            0x1F
#define BMA2X2_MAGIC_NUMBER__REG            BMA2X2_EXTMODE_CTRL_REG

#define BMA2X2_UNLOCK_EE_WRITE_TRIM__POS        4
#define BMA2X2_UNLOCK_EE_WRITE_TRIM__LEN        4
#define BMA2X2_UNLOCK_EE_WRITE_TRIM__MSK        0xF0
#define BMA2X2_UNLOCK_EE_WRITE_TRIM__REG        BMA2X2_CTRL_UNLOCK_REG

#define BMA2X2_EN_SLOW_COMP_X__POS              0
#define BMA2X2_EN_SLOW_COMP_X__LEN              1
#define BMA2X2_EN_SLOW_COMP_X__MSK              0x01
#define BMA2X2_EN_SLOW_COMP_X__REG              BMA2X2_OFFSET_CTRL_REG

#define BMA2X2_EN_SLOW_COMP_Y__POS              1
#define BMA2X2_EN_SLOW_COMP_Y__LEN              1
#define BMA2X2_EN_SLOW_COMP_Y__MSK              0x02
#define BMA2X2_EN_SLOW_COMP_Y__REG              BMA2X2_OFFSET_CTRL_REG

#define BMA2X2_EN_SLOW_COMP_Z__POS              2
#define BMA2X2_EN_SLOW_COMP_Z__LEN              1
#define BMA2X2_EN_SLOW_COMP_Z__MSK              0x04
#define BMA2X2_EN_SLOW_COMP_Z__REG              BMA2X2_OFFSET_CTRL_REG

#define BMA2X2_FAST_CAL_RDY_S__POS             4
#define BMA2X2_FAST_CAL_RDY_S__LEN             1
#define BMA2X2_FAST_CAL_RDY_S__MSK             0x10
#define BMA2X2_FAST_CAL_RDY_S__REG             BMA2X2_OFFSET_CTRL_REG

#define BMA2X2_CAL_TRIGGER__POS                5
#define BMA2X2_CAL_TRIGGER__LEN                2
#define BMA2X2_CAL_TRIGGER__MSK                0x60
#define BMA2X2_CAL_TRIGGER__REG                BMA2X2_OFFSET_CTRL_REG

#define BMA2X2_RESET_OFFSET_REGS__POS           7
#define BMA2X2_RESET_OFFSET_REGS__LEN           1
#define BMA2X2_RESET_OFFSET_REGS__MSK           0x80
#define BMA2X2_RESET_OFFSET_REGS__REG           BMA2X2_OFFSET_CTRL_REG

#define BMA2X2_COMP_CUTOFF__POS                 0
#define BMA2X2_COMP_CUTOFF__LEN                 1
#define BMA2X2_COMP_CUTOFF__MSK                 0x01
#define BMA2X2_COMP_CUTOFF__REG                 BMA2X2_OFFSET_PARAMS_REG

#define BMA2X2_COMP_TARGET_OFFSET_X__POS        1
#define BMA2X2_COMP_TARGET_OFFSET_X__LEN        2
#define BMA2X2_COMP_TARGET_OFFSET_X__MSK        0x06
#define BMA2X2_COMP_TARGET_OFFSET_X__REG        BMA2X2_OFFSET_PARAMS_REG

#define BMA2X2_COMP_TARGET_OFFSET_Y__POS        3
#define BMA2X2_COMP_TARGET_OFFSET_Y__LEN        2
#define BMA2X2_COMP_TARGET_OFFSET_Y__MSK        0x18
#define BMA2X2_COMP_TARGET_OFFSET_Y__REG        BMA2X2_OFFSET_PARAMS_REG

#define BMA2X2_COMP_TARGET_OFFSET_Z__POS        5
#define BMA2X2_COMP_TARGET_OFFSET_Z__LEN        2
#define BMA2X2_COMP_TARGET_OFFSET_Z__MSK        0x60
#define BMA2X2_COMP_TARGET_OFFSET_Z__REG        BMA2X2_OFFSET_PARAMS_REG

#define BMA2X2_FIFO_DATA_SELECT__POS                 0
#define BMA2X2_FIFO_DATA_SELECT__LEN                 2
#define BMA2X2_FIFO_DATA_SELECT__MSK                 0x03
#define BMA2X2_FIFO_DATA_SELECT__REG                 BMA2X2_FIFO_MODE_REG

#define BMA2X2_FIFO_TRIGGER_SOURCE__POS                 2
#define BMA2X2_FIFO_TRIGGER_SOURCE__LEN                 2
#define BMA2X2_FIFO_TRIGGER_SOURCE__MSK                 0x0C
#define BMA2X2_FIFO_TRIGGER_SOURCE__REG                 BMA2X2_FIFO_MODE_REG

#define BMA2X2_FIFO_TRIGGER_ACTION__POS                 4
#define BMA2X2_FIFO_TRIGGER_ACTION__LEN                 2
#define BMA2X2_FIFO_TRIGGER_ACTION__MSK                 0x30
#define BMA2X2_FIFO_TRIGGER_ACTION__REG                 BMA2X2_FIFO_MODE_REG

#define BMA2X2_FIFO_MODE__POS                 6
#define BMA2X2_FIFO_MODE__LEN                 2
#define BMA2X2_FIFO_MODE__MSK                 0xC0
#define BMA2X2_FIFO_MODE__REG                 BMA2X2_FIFO_MODE_REG


#define BMA2X2_STATUS1                             0
#define BMA2X2_STATUS2                             1
#define BMA2X2_STATUS3                             2
#define BMA2X2_STATUS4                             3
#define BMA2X2_STATUS5                             4


#define BMA2X2_RANGE_2G                 3
#define BMA2X2_RANGE_4G                 5
#define BMA2X2_RANGE_8G                 8
#define BMA2X2_RANGE_16G                12


#define BMA2X2_BW_7_81HZ        0x08
#define BMA2X2_BW_15_63HZ       0x09
#define BMA2X2_BW_31_25HZ       0x0A
#define BMA2X2_BW_62_50HZ       0x0B
#define BMA2X2_BW_125HZ         0x0C
#define BMA2X2_BW_250HZ         0x0D
#define BMA2X2_BW_500HZ         0x0E
#define BMA2X2_BW_1000HZ        0x0F

#define BMA2X2_SLEEP_DUR_0_5MS        0x05
#define BMA2X2_SLEEP_DUR_1MS          0x06
#define BMA2X2_SLEEP_DUR_2MS          0x07
#define BMA2X2_SLEEP_DUR_4MS          0x08
#define BMA2X2_SLEEP_DUR_6MS          0x09
#define BMA2X2_SLEEP_DUR_10MS         0x0A
#define BMA2X2_SLEEP_DUR_25MS         0x0B
#define BMA2X2_SLEEP_DUR_50MS         0x0C
#define BMA2X2_SLEEP_DUR_100MS        0x0D
#define BMA2X2_SLEEP_DUR_500MS        0x0E
#define BMA2X2_SLEEP_DUR_1S           0x0F

#define BMA2X2_LATCH_DUR_NON_LATCH    0x00
#define BMA2X2_LATCH_DUR_250MS        0x01
#define BMA2X2_LATCH_DUR_500MS        0x02
#define BMA2X2_LATCH_DUR_1S           0x03
#define BMA2X2_LATCH_DUR_2S           0x04
#define BMA2X2_LATCH_DUR_4S           0x05
#define BMA2X2_LATCH_DUR_8S           0x06
#define BMA2X2_LATCH_DUR_LATCH        0x07
#define BMA2X2_LATCH_DUR_NON_LATCH1   0x08
#define BMA2X2_LATCH_DUR_250US        0x09
#define BMA2X2_LATCH_DUR_500US        0x0A
#define BMA2X2_LATCH_DUR_1MS          0x0B
#define BMA2X2_LATCH_DUR_12_5MS       0x0C
#define BMA2X2_LATCH_DUR_25MS         0x0D
#define BMA2X2_LATCH_DUR_50MS         0x0E
#define BMA2X2_LATCH_DUR_LATCH1       0x0F

#define BMA2X2_MODE_NORMAL             0
#define BMA2X2_MODE_LOWPOWER1          1
#define BMA2X2_MODE_SUSPEND            2
#define BMA2X2_MODE_DEEP_SUSPEND       3
#define BMA2X2_MODE_LOWPOWER2          4
#define BMA2X2_MODE_STANDBY            5

#define BMA2X2_X_AXIS           0
#define BMA2X2_Y_AXIS           1
#define BMA2X2_Z_AXIS           2

#define BMA2X2_Low_G_Interrupt       0
#define BMA2X2_High_G_X_Interrupt    1
#define BMA2X2_High_G_Y_Interrupt    2
#define BMA2X2_High_G_Z_Interrupt    3
#define BMA2X2_DATA_EN               4
#define BMA2X2_Slope_X_Interrupt     5
#define BMA2X2_Slope_Y_Interrupt     6
#define BMA2X2_Slope_Z_Interrupt     7
#define BMA2X2_Single_Tap_Interrupt  8
#define BMA2X2_Double_Tap_Interrupt  9
#define BMA2X2_Orient_Interrupt      10
#define BMA2X2_Flat_Interrupt        11
#define BMA2X2_FFULL_INTERRUPT       12
#define BMA2X2_FWM_INTERRUPT         13

#define BMA2X2_INT1_LOWG         0
#define BMA2X2_INT2_LOWG         1
#define BMA2X2_INT1_HIGHG        0
#define BMA2X2_INT2_HIGHG        1
#define BMA2X2_INT1_SLOPE        0
#define BMA2X2_INT2_SLOPE        1
#define BMA2X2_INT1_SLO_NO_MOT   0
#define BMA2X2_INT2_SLO_NO_MOT   1
#define BMA2X2_INT1_DTAP         0
#define BMA2X2_INT2_DTAP         1
#define BMA2X2_INT1_STAP         0
#define BMA2X2_INT2_STAP         1
#define BMA2X2_INT1_ORIENT       0
#define BMA2X2_INT2_ORIENT       1
#define BMA2X2_INT1_FLAT         0
#define BMA2X2_INT2_FLAT         1
#define BMA2X2_INT1_NDATA        0
#define BMA2X2_INT2_NDATA        1
#define BMA2X2_INT1_FWM          0
#define BMA2X2_INT2_FWM          1
#define BMA2X2_INT1_FFULL        0
#define BMA2X2_INT2_FFULL        1

#define BMA2X2_SRC_LOWG         0
#define BMA2X2_SRC_HIGHG        1
#define BMA2X2_SRC_SLOPE        2
#define BMA2X2_SRC_SLO_NO_MOT   3
#define BMA2X2_SRC_TAP          4
#define BMA2X2_SRC_DATA         5

#define BMA2X2_INT1_OUTPUT      0
#define BMA2X2_INT2_OUTPUT      1
#define BMA2X2_INT1_LEVEL       0
#define BMA2X2_INT2_LEVEL       1

#define BMA2X2_LOW_DURATION            0
#define BMA2X2_HIGH_DURATION           1
#define BMA2X2_SLOPE_DURATION          2
#define BMA2X2_SLO_NO_MOT_DURATION     3

#define BMA2X2_LOW_THRESHOLD            0
#define BMA2X2_HIGH_THRESHOLD           1
#define BMA2X2_SLOPE_THRESHOLD          2
#define BMA2X2_SLO_NO_MOT_THRESHOLD     3


#define BMA2X2_LOWG_HYST                0
#define BMA2X2_HIGHG_HYST               1

#define BMA2X2_ORIENT_THETA             0
#define BMA2X2_FLAT_THETA               1

#define BMA2X2_I2C_SELECT               0
#define BMA2X2_I2C_EN                   1

#define BMA2X2_SLOW_COMP_X              0
#define BMA2X2_SLOW_COMP_Y              1
#define BMA2X2_SLOW_COMP_Z              2

#define BMA2X2_CUT_OFF                  0
#define BMA2X2_OFFSET_TRIGGER_X         1
#define BMA2X2_OFFSET_TRIGGER_Y         2
#define BMA2X2_OFFSET_TRIGGER_Z         3

#define BMA2X2_GP0                      0
#define BMA2X2_GP1                      1

#define BMA2X2_SLO_NO_MOT_EN_X          0
#define BMA2X2_SLO_NO_MOT_EN_Y          1
#define BMA2X2_SLO_NO_MOT_EN_Z          2
#define BMA2X2_SLO_NO_MOT_EN_SEL        3

#define BMA2X2_WAKE_UP_DUR_20MS         0
#define BMA2X2_WAKE_UP_DUR_80MS         1
#define BMA2X2_WAKE_UP_DUR_320MS                2
#define BMA2X2_WAKE_UP_DUR_2560MS               3

#define BMA2X2_SELF_TEST0_ON            1
#define BMA2X2_SELF_TEST1_ON            2

#define BMA2X2_EE_W_OFF                 0
#define BMA2X2_EE_W_ON                  1

#define BMA2X2_LOW_TH_IN_G(gthres, range)           ((256 * gthres) / range)


#define BMA2X2_HIGH_TH_IN_G(gthres, range)          ((256 * gthres) / range)


#define BMA2X2_LOW_HY_IN_G(ghyst, range)            ((32 * ghyst) / range)


#define BMA2X2_HIGH_HY_IN_G(ghyst, range)           ((32 * ghyst) / range)


#define BMA2X2_SLOPE_TH_IN_G(gthres, range)    ((128 * gthres) / range)


#define BMA2X2_GET_BITSLICE(regvar, bitname)\
	((regvar & bitname##__MSK) >> bitname##__POS)


#define BMA2X2_SET_BITSLICE(regvar, bitname, val)\
	((regvar & ~bitname##__MSK) | ((val<<bitname##__POS)&bitname##__MSK))

#ifdef BMA2X2_SENSOR_IDENTIFICATION_ENABLE
#define BMA2X2_SHIFT_BITWIDTH(data, bitwidth)
#else
#define BMA2X2_SHIFT_BITWIDTH(data, bitwidth)\
	(data = data >> (16 - bitwidth))
#endif

#ifdef CONFIG_BMA_ENABLE_NEWDATA_INT
#define BMA2x2_IS_NEWDATA_INT_ENABLED()	(true)
#else
#define BMA2x2_IS_NEWDATA_INT_ENABLED()	(false)
#endif

#ifdef BMA2X2_ENABLE_INT1
#define BMA2x2_IS_INT1_ENABLED()	(true)
#else
#define BMA2x2_IS_INT1_ENABLED()	(false)
#endif

#ifdef BMA2X2_ENABLE_INT2
#define BMA2x2_IS_INT2_ENABLED()	(true)
#else
#define BMA2x2_IS_INT2_ENABLED()	(false)
#endif

#define CHECK_CHIP_ID_TIME_MAX 5
#define BMA255_CHIP_ID 0XFA
#define BMA250E_CHIP_ID 0XF9
#define BMA222E_CHIP_ID 0XF8
#define BMA280_CHIP_ID 0XFB
#define BMA355_CHIP_ID 0XEA

#define BMA255_TYPE 0
#define BMA250E_TYPE 1
#define BMA222E_TYPE 2
#define BMA280_TYPE 3

#define MAX_FIFO_F_LEVEL 32
#define MAX_FIFO_F_BYTES 6
#define FIFO_FRAMESIZE_3_AXIS 6
#define FIFO_FRAMESIZE_1_AXIS 2
#define BMA_MAX_RETRY_I2C_XFER (100)

#ifdef CONFIG_DOUBLE_TAP
#define DEFAULT_TAP_JUDGE_PERIOD 1000    /* default judge in 1 second */
#endif

#define BMA2X2_SMD_DET_TIME_NS	(2200UL * 1000 * 1000)
#define BMA2X2_SMD_DET_CNT	(7)
#define BMA2X2_SMD_SLOPE_DUR    0x3
#define BMA2X2_SMD_SLOPE_TH	0x20
#define BMA2X2_SMD_NO_MOT_DUR	0x1
#define BMA2X2_SMD_NO_MOT_TH	0x20

/*! Bosch sensor unknown place*/
#define BOSCH_SENSOR_PLACE_UNKNOWN (-1)
/*! Bosch sensor remapping table size P0~P7*/
#define MAX_AXIS_REMAP_TAB_SZ 8
#define BOSCH_SENSOR_PLANE	0
#define BOSCH_SENSOR_UP	1
#define BOSCH_SENSOR_DOWN	2
#define RETRY_TIME	50
/*!
 * @brief:BMI058 feature
 *  macro definition
*/

#define BMA2X2_IS_NEWDATA_INT           BMA2X2_DATA_INT_S__MSK
#define BMA2X2_FIFO_MODE_BYPASS         0x0
#define BMA2X2_FIFO_MODE_FIFO           0x1
#define BMA2X2_FIFO_MODE_STREAM         0x2

#define BMA2X2_FIFO_DAT_SEL_XYZ         0
#define BMA2X2_FIFO_DAT_SEL_X           1
#define BMA2X2_FIFO_DAT_SEL_Y           2
#define BMA2X2_FIFO_DAT_SEL_Z           3

#define BMA2X2_FIFO_WM_INT_FLAG         BMA2X2_FIFO_WM_INT_S__MSK
#define BMA2X2_FIFO_FULL_INT_FLAG       BMA2X2_FIFO_FULL_INT_S__MSK
#define BMA2X2_IS_FIFO_INT\
	(BMA2X2_FIFO_WM_INT_FLAG | BMA2X2_FIFO_FULL_INT_FLAG)

#define BMA2x2_NEWDATA_INT_MASK	0x80
#define BMA2x2_NEWDATA_INT_FLAG	0x80

#ifdef CONFIG_SENSORS_BMI058
#define C_BMI058_One_U8X                                 1
#define C_BMI058_Two_U8X                                 2
#define BMI058_OFFSET_TRIGGER_X                BMA2X2_OFFSET_TRIGGER_Y
#define BMI058_OFFSET_TRIGGER_Y                BMA2X2_OFFSET_TRIGGER_X

/*! BMI058 X AXIS OFFSET REG definition*/
#define BMI058_OFFSET_X_AXIS_REG              BMA2X2_OFFSET_Y_AXIS_REG
/*! BMI058 Y AXIS OFFSET REG definition*/
#define BMI058_OFFSET_Y_AXIS_REG              BMA2X2_OFFSET_X_AXIS_REG

#define BMI058_FIFO_DAT_SEL_X                       BMA2X2_FIFO_DAT_SEL_Y
#define BMI058_FIFO_DAT_SEL_Y                       BMA2X2_FIFO_DAT_SEL_X

/*! BMA2x2 common slow no motion X interrupt type definition*/
#define BMA2X2_SLOW_NO_MOT_X_INT          12
/*! BMA2x2 common slow no motion Y interrupt type definition*/
#define BMA2X2_SLOW_NO_MOT_Y_INT          13
/*! BMA2x2 common High G X interrupt type definition*/
#define BMA2X2_HIGHG_X_INT          1
/*! BMA2x2 common High G Y interrupt type definition*/
#define BMA2X2_HIGHG_Y_INT          2
/*! BMA2x2 common slope X interrupt type definition*/
#define BMA2X2_SLOPE_X_INT          5
/*! BMA2x2 common slope Y interrupt type definition*/
#define BMA2X2_SLOPE_Y_INT          6

/*! this structure holds some interrupt types difference
**between BMA2x2 and BMI058.
*/
struct interrupt_map_t {
	int x;
	int y;
};
/*!*Need to use BMA2x2 Common interrupt type definition to
* instead of Some of BMI058 reversed Interrupt type
* because of HW Register.
* The reversed Interrupt types contain:
* slow_no_mot_x_int && slow_not_mot_y_int
* highg_x_int && highg_y_int
* slope_x_int && slope_y_int
**/
static const struct interrupt_map_t int_map[] = {
	{BMA2X2_SLOW_NO_MOT_X_INT, BMA2X2_SLOW_NO_MOT_Y_INT},
	{BMA2X2_HIGHG_X_INT, BMA2X2_HIGHG_Y_INT},
	{BMA2X2_SLOPE_X_INT, BMA2X2_SLOPE_Y_INT}
};

/*! high g or slope interrupt type definition for BMI058*/
/*! High G interrupt of x, y, z axis happened */
#define HIGH_G_INTERRUPT_X            HIGH_G_INTERRUPT_Y_HAPPENED
#define HIGH_G_INTERRUPT_Y            HIGH_G_INTERRUPT_X_HAPPENED
#define HIGH_G_INTERRUPT_Z            HIGH_G_INTERRUPT_Z_HAPPENED
/*! High G interrupt of x, y, z negative axis happened */
#define HIGH_G_INTERRUPT_X_N          HIGH_G_INTERRUPT_Y_NEGATIVE_HAPPENED
#define HIGH_G_INTERRUPT_Y_N          HIGH_G_INTERRUPT_X_NEGATIVE_HAPPENED
#define HIGH_G_INTERRUPT_Z_N          HIGH_G_INTERRUPT_Z_NEGATIVE_HAPPENED
/*! Slope interrupt of x, y, z axis happened */
#define SLOPE_INTERRUPT_X             SLOPE_INTERRUPT_Y_HAPPENED
#define SLOPE_INTERRUPT_Y             SLOPE_INTERRUPT_X_HAPPENED
#define SLOPE_INTERRUPT_Z             SLOPE_INTERRUPT_Z_HAPPENED
/*! Slope interrupt of x, y, z negative axis happened */
#define SLOPE_INTERRUPT_X_N           SLOPE_INTERRUPT_Y_NEGATIVE_HAPPENED
#define SLOPE_INTERRUPT_Y_N           SLOPE_INTERRUPT_X_NEGATIVE_HAPPENED
#define SLOPE_INTERRUPT_Z_N           SLOPE_INTERRUPT_Z_NEGATIVE_HAPPENED


#else

/*! high g or slope interrupt type definition*/
/*! High G interrupt of x, y, z axis happened */
#define HIGH_G_INTERRUPT_X            HIGH_G_INTERRUPT_X_HAPPENED
#define HIGH_G_INTERRUPT_Y            HIGH_G_INTERRUPT_Y_HAPPENED
#define HIGH_G_INTERRUPT_Z            HIGH_G_INTERRUPT_Z_HAPPENED
/*! High G interrupt of x, y, z negative axis happened */
#define HIGH_G_INTERRUPT_X_N          HIGH_G_INTERRUPT_X_NEGATIVE_HAPPENED
#define HIGH_G_INTERRUPT_Y_N          HIGH_G_INTERRUPT_Y_NEGATIVE_HAPPENED
#define HIGH_G_INTERRUPT_Z_N          HIGH_G_INTERRUPT_Z_NEGATIVE_HAPPENED
/*! Slope interrupt of x, y, z axis happened */
#define SLOPE_INTERRUPT_X             SLOPE_INTERRUPT_X_HAPPENED
#define SLOPE_INTERRUPT_Y             SLOPE_INTERRUPT_Y_HAPPENED
#define SLOPE_INTERRUPT_Z             SLOPE_INTERRUPT_Z_HAPPENED
/*! Slope interrupt of x, y, z negative axis happened */
#define SLOPE_INTERRUPT_X_N           SLOPE_INTERRUPT_X_NEGATIVE_HAPPENED
#define SLOPE_INTERRUPT_Y_N           SLOPE_INTERRUPT_Y_NEGATIVE_HAPPENED
#define SLOPE_INTERRUPT_Z_N           SLOPE_INTERRUPT_Z_NEGATIVE_HAPPENED


#endif/*End of CONFIG_SENSORS_BMI058*/

/*BMA power supply VDD 1.62V-3.6V VIO 1.2-3.6V */
#define BMA2x2_VDD_MIN_UV       2000000
#define BMA2x2_VDD_MAX_UV       3400000
#define BMA2x2_VIO_MIN_UV       1500000
#define BMA2x2_VIO_MAX_UV       3400000

/* Polling delay in msecs */
#define POLL_INTERVAL_MIN_MS	10
#define POLL_INTERVAL_MAX_MS	2000
#define POLL_DEFAULT_INTERVAL_MS 200

#define POLL_MS_100HZ 10

/* Interrupt delay in msecs */
#define BMA_INT_MAX_DELAY	64

#define MAX_RANGE_MAP	4

#define BMA_CAL_BUF_SIZE	99

#define CALICOUNT       	3
#define ABS(a) ( (a)>0 ? (a):(-(a)))
// 1g constant value
#define GRAVITY_1G_VALUE			16384

struct bma2x2_type_map_t {

	/*! bma2x2 sensor chip id */
	uint16_t chip_id;

	/*! bma2x2 sensor type */
	uint16_t sensor_type;

	/*! bma2x2 sensor name */
	const char *sensor_name;

	/*! bma2x2 sensor resolution */
	const char *resolution;
};

static const struct bma2x2_type_map_t sensor_type_map[] = {

	{BMA255_CHIP_ID, BMA255_TYPE, "BMA255/254", "0.00957031"},
	{BMA355_CHIP_ID, BMA255_TYPE, "BMA355", "0.00957031"},
	{BMA250E_CHIP_ID, BMA250E_TYPE, "BMA250E", "0.03828125"},
	{BMA222E_CHIP_ID, BMA222E_TYPE, "BMA222E", "0.153125"},
	{BMA280_CHIP_ID, BMA280_TYPE, "BMA280", "0.00239258"},

};

static int offset_x = 0, offset_y = 0, offset_z = 0;


/*!
 * we use a typedef to hide the detail,
 * because this type might be changed
 */
struct bosch_sensor_axis_remap {
	/* src means which source will be mapped to target x, y, z axis */
	/* if an target OS axis is remapped from (-)x,
	 * src is 0, sign_* is (-)1 */
	/* if an target OS axis is remapped from (-)y,
	 * src is 1, sign_* is (-)1 */
	/* if an target OS axis is remapped from (-)z,
	 * src is 2, sign_* is (-)1 */
	int src_x:3;
	int src_y:3;
	int src_z:3;

	int sign_x:2;
	int sign_y:2;
	int sign_z:2;
};

struct bosch_sensor_data {
	union {
		int16_t v[3];
		struct {
			int16_t x;
			int16_t y;
			int16_t z;
		};
	};
};

struct bma2x2acc {
	s16 x;
	s16 y;
	s16 z;
};

struct bma2x2acc_int {
	int x;
	int y;
	int z;
};

struct bma2x2_platform_data {
	int poll_interval;
	int gpio_int1;
	int gpio_int2;
	unsigned int int1_flag;
	unsigned int int2_flag;
	s8 place;
	bool int_en;
	bool use_int2; /* Use interrupt pin2 */
	bool use_smd;
	bool use_hrtimer;
};

struct bma2x2_suspend_state {
	bool powerEn;
};

struct bma2x2_pinctrl_data {
	struct pinctrl          *pctrl;
	struct pinctrl_state    *pins_default;
	struct pinctrl_state    *pins_sleep;
};

struct bma2x2_data {
	struct i2c_client *bma2x2_client;
	struct sensors_classdev cdev;
	struct sensors_classdev smd_cdev;
	atomic_t delay;
	atomic_t enable;
	atomic_t selftest_result;
	atomic_t cal_status;
	atomic_t fifo_enabled;
	atomic_t en_sig_motion;
	char calibrate_buf[BMA_CAL_BUF_SIZE];
	unsigned int chip_id;
	unsigned int chip_type;
	unsigned char mode;
	signed char sensor_type;
	unsigned char fifo_datasel;
	unsigned int fifo_count;
	signed char *fifo_buf;
	s64 fifo_start_ns;
	unsigned int max_latency_ms;
	struct input_dev *input;

	struct bst_dev *bst_acc;

	struct bma2x2acc value;
	struct mutex value_mutex;
	struct mutex enable_mutex;
	struct mutex mode_mutex;
	struct mutex op_lock;

	struct workqueue_struct *data_wq;
	struct delayed_work work;
	struct work_struct irq_work;
	struct hrtimer accel_timer;
	int accel_wkp_flag;
	struct task_struct *accel_task;
	bool accel_delay_change;
	wait_queue_head_t accel_wq;
	struct regulator *vdd;
	struct regulator *vio;
	bool power_enabled;
	unsigned char bandwidth;
	unsigned char range;
	unsigned int int_flag;
	int sensitivity;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	int IRQ;
	struct bma2x2_platform_data *pdata;
	struct bma2x2_suspend_state suspend_state;
	struct bma2x2_pinctrl_data *pctrl_data;

	int ref_count;
	struct input_dev *dev_interrupt;

#ifdef CONFIG_SIG_MOTION
	struct class *g_sensor_class;
	struct device *g_sensor_dev;
	struct input_dev *smd_input;

	/*struct bma250_platform_data *pdata;*/
	unsigned int smd_count;
#endif

#ifdef CONFIG_DOUBLE_TAP
	struct class *g_sensor_class_doubletap;
	struct device *g_sensor_dev_doubletap;
	atomic_t en_double_tap;
	unsigned char tap_times;
	struct mutex		tap_mutex;
	struct timer_list	tap_timer;
	int tap_time_period;
#endif
};

struct bma2x2_delay2bw {
	unsigned int delay_ms;
	unsigned int bw_config;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void bma2x2_early_suspend(struct early_suspend *h);
static void bma2x2_late_resume(struct early_suspend *h);
#endif

static int bma2x2_open_init(struct i2c_client *client,
			struct bma2x2_data *data);
static int bma2x2_set_mode(struct i2c_client *client, u8 mode);
static int bma2x2_get_mode(struct i2c_client *client, u8 *mode);
static int bma2x2_get_fifo_mode(struct i2c_client *client, u8 *fifo_mode);
static int bma2x2_set_fifo_mode(struct i2c_client *client, u8 fifo_mode);
static int bma2x2_normal_to_suspend(struct bma2x2_data *bma2x2,
				unsigned char data1, unsigned char data2);
static int bma2x2_store_state(struct i2c_client *client,
			struct bma2x2_data *data);
static int bma2x2_power_ctl(struct bma2x2_data *data, bool on);
static int bma2x2_eeprom_prog(struct i2c_client *client);
static int bma2x2_get_sensitivity(struct bma2x2_data *bma2x2, int range);
static void bma2x2_pinctrl_state(struct bma2x2_data *data, bool active);
static int bma2x2_flush_fifo(struct bma2x2_data *bma2x2);

static struct sensors_classdev sensors_cdev = {
	.name = "bma2x2-accel",
	.vendor = "bosch",
	.version = 1,
	.handle = SENSORS_ACCELERATION_HANDLE,
	.type = SENSOR_TYPE_ACCELEROMETER,
	.max_range = "156.8",	/* 16g */
	.resolution = "0.153125",	/* 15.6mg */
	.sensor_power = "0.13",	/* typical value */
	.min_delay = POLL_INTERVAL_MIN_MS * 1000, /* in microseconds */
	.max_delay = POLL_INTERVAL_MAX_MS,
	.max_latency = POLL_INTERVAL_MAX_MS,
	.fifo_reserved_event_count = 0,
	.fifo_max_event_count = 0,
	.enabled = 0,
	.delay_msec = POLL_DEFAULT_INTERVAL_MS, /* in millisecond */
	.sensors_enable = NULL,
	.sensors_poll_delay = NULL,
	.sensors_self_test = NULL,
};


#ifdef CONFIG_SIG_MOTION
static struct sensors_classdev smd_cdev = {
	.name = "bma2x2-smd",
	.vendor = "bosch",
	.version = 1,
	.handle = 0,
	.type = SENSOR_TYPE_SIGNIFICANT_MOTION,
	.max_range = "1",
	.resolution = "1.0",
	.sensor_power = "0.25",
	.min_delay = -1,
	.max_delay = 0,
	.fifo_reserved_event_count = 0,
	.fifo_max_event_count = 0,
	.flags = 5,
	.enabled = 0,
	.delay_msec = 0,
	.sensors_enable = NULL,
	.sensors_poll_delay = NULL,
};
#endif

/*Remapping for BMA2X2*/

static const struct bosch_sensor_axis_remap
bst_axis_remap_tab_dft[MAX_AXIS_REMAP_TAB_SZ] = {
	/* src_x src_y src_z  sign_x  sign_y  sign_z */
	{  0,    1,    2,     1,      1,      1 }, /* P0 */
	{  1,    0,    2,     1,     -1,      1 }, /* P1 */
	{  0,    1,    2,    -1,     -1,      1 }, /* P2 */
	{  1,    0,    2,    -1,      1,      1 }, /* P3 */

	{  0,    1,    2,    -1,      1,     -1 }, /* P4 */
	{  1,    0,    2,    -1,     -1,     -1 }, /* P5 */
	{  0,    1,    2,     1,     -1,     -1 }, /* P6 */
	{  1,    0,    2,     1,      1,     -1 }, /* P7 */
};

static const int bosch_sensor_range_map[MAX_RANGE_MAP] = {
	0, /*2G range*/
	1, /*4G range*/
	2, /*8G range*/
	3  /*16G range*/
};

/* Convert bandwidth to sampling delay */
static const struct bma2x2_delay2bw bma2x2_delay2bw_table[] = {
		{	1, BMA2X2_BW_500HZ	},
		{	2, BMA2X2_BW_250HZ	},
		{	4, BMA2X2_BW_125HZ	},
		{	8, BMA2X2_BW_62_50HZ	},
		{	16, BMA2X2_BW_31_25HZ	},
		{	32, BMA2X2_BW_15_63HZ	},
		{	64, BMA2X2_BW_7_81HZ	},
};


static inline bool bma2x2_use_data_polling(const struct bma2x2_data *bma2x2)
{
	return !bma2x2->pdata->int_en ||
			((bma2x2->pdata->int_en) &&
			!BMA2x2_IS_NEWDATA_INT_ENABLED());
}

static inline void bma2x2_set_fifo_start_time(struct bma2x2_data *bma2x2)
{
	struct timespec ts;

	get_monotonic_boottime(&ts);
	bma2x2->fifo_start_ns = timespec_to_ns(&ts);
}

static void bst_remap_sensor_data(struct bosch_sensor_data *data,
		const struct bosch_sensor_axis_remap *remap)
{
	struct bosch_sensor_data tmp;

	tmp.x = data->v[remap->src_x] * remap->sign_x;
	tmp.y = data->v[remap->src_y] * remap->sign_y;
	tmp.z = data->v[remap->src_z] * remap->sign_z;

	memcpy(data, &tmp, sizeof(*data));
}


static void bst_remap_sensor_data_dft_tab(struct bosch_sensor_data *data,
		int place)
{
	/* sensor with place 0 needs not to be remapped */
	if ((place <= 0) || (place >= MAX_AXIS_REMAP_TAB_SZ))
		return;

	bst_remap_sensor_data(data, &bst_axis_remap_tab_dft[place]);
}

static void bma2x2_remap_sensor_data(struct bma2x2acc *val,
		struct bma2x2_data *client_data)
{
	struct bosch_sensor_data bsd;

#ifdef CONFIG_SENSORS_BMI058
/*x,y need to be invesed becase of HW Register for BMI058*/
	bsd.y = val->x;
	bsd.x = val->y;
	bsd.z = val->z;
#else
	bsd.x = val->x;
	bsd.y = val->y;
	bsd.z = val->z;
#endif

	bst_remap_sensor_data_dft_tab(&bsd,
			client_data->pdata->place);

	val->x = bsd.x;
	val->y = bsd.y;
	val->z = bsd.z;

}


static int bma2x2_smbus_read_byte(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *data)
{
	s32 dummy;

	dummy = i2c_smbus_read_byte_data(client, reg_addr);
	if (dummy < 0)
		return -EIO;
	*data = dummy & 0x000000ff;

	return 0;
}

static int bma2x2_smbus_write_byte(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *data)
{
	s32 dummy;

	dummy = i2c_smbus_write_byte_data(client, reg_addr, *data);
	if (dummy < 0)
		return -EIO;
	udelay(2);
	return 0;
}

static int bma2x2_smbus_read_byte_block(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *data, unsigned char len)
{
	s32 dummy;

	dummy = i2c_smbus_read_i2c_block_data(client, reg_addr, len, data);
	if (dummy < 0)
		return -EIO;
	return 0;
}

static int bma_i2c_burst_read(struct i2c_client *client, u8 reg_addr,
		u8 *data, u16 len)
{
	int retry;

	struct i2c_msg msg[] = {
		{
		 .addr = client->addr,
		 .flags = 0,
		 .len = 1,
		 .buf = &reg_addr,
		},

		{
		 .addr = client->addr,
		 .flags = I2C_M_RD,
		 .len = len,
		 .buf = data,
		 },
	};

	for (retry = 0; retry < BMA_MAX_RETRY_I2C_XFER; retry++) {
		if (i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg)) > 0)
			break;

		I2C_RETRY_DELAY();
	}

	if (BMA_MAX_RETRY_I2C_XFER <= retry) {
		dev_err(&client->dev, "I2C xfer error");
		return -EIO;
	}

	return 0;
}

static int bma2x2_check_chip_id(struct i2c_client *client,
					struct bma2x2_data *data)
{
	int i = 0;
	int err = 0;
	unsigned char chip_id;
	unsigned char read_count = 0;
	unsigned char bma2x2_sensor_type_count = 0;

	bma2x2_sensor_type_count =
		sizeof(sensor_type_map) / sizeof(struct bma2x2_type_map_t);

	while (read_count++ < CHECK_CHIP_ID_TIME_MAX) {
		err = bma2x2_smbus_read_byte(client, BMA2X2_CHIP_ID_REG,
							&chip_id);
		if (err < 0) {
			dev_err(&client->dev,
			"Bosch Sensortec Device not found"
			"i2c bus read error, read chip_id:%d\n", chip_id);
			err = -ENODEV;
			return err;
		}
		for (i = 0; i < bma2x2_sensor_type_count; i++) {
			if (sensor_type_map[i].chip_id == chip_id) {
				data->sensor_type =
					sensor_type_map[i].sensor_type;
				data->chip_id = chip_id;
				dev_dbg(&client->dev,
					"Bosch Sensortec Device detected, HW IC name: %s\n",
					sensor_type_map[i].sensor_name);
				data->chip_type = i;
				return err;
			}
		}
		if (i < bma2x2_sensor_type_count) {
			return err;
		} else if (read_count == CHECK_CHIP_ID_TIME_MAX) {
			dev_err(&client->dev,
			"Failed!Bosch Sensortec Device"
			" not found, mismatch chip_id:%d\n",
						chip_id);
			err = -ENODEV;
			return err;
		}
		I2C_RETRY_DELAY();
	}
	return err;
}

#if defined(BMA2X2_ENABLE_INT1) || defined(BMA2X2_ENABLE_INT2)
static int bma2x2_set_newdata(struct i2c_client *client,
			unsigned char channel, unsigned char int_newdata)
{

	unsigned char data;
	int comres = 0;

	switch (channel) {
	case BMA2X2_INT1_NDATA:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT1_PAD_NEWDATA__REG, &data);
		data = BMA2X2_SET_BITSLICE(data,
				BMA2X2_EN_INT1_PAD_NEWDATA, int_newdata);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT1_PAD_NEWDATA__REG, &data);
		break;
	case BMA2X2_INT2_NDATA:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT2_PAD_NEWDATA__REG, &data);
		data = BMA2X2_SET_BITSLICE(data,
				BMA2X2_EN_INT2_PAD_NEWDATA, int_newdata);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT2_PAD_NEWDATA__REG, &data);
		break;
	default:
		comres = -1;
		break;
	}

	return comres;

}

static int bma2x2_set_fwm_int_pad_sel(struct i2c_client *client,
			unsigned char channel, unsigned char fifo_int)
{
	unsigned char data;
	int comres = 0;

	switch (channel) {
	case BMA2X2_INT1_FWM:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT1_PAD_FWM__REG, &data);
		data = BMA2X2_SET_BITSLICE(data,
				BMA2X2_EN_INT1_PAD_FWM, fifo_int);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT1_PAD_FWM__REG, &data);
		break;
	case BMA2X2_INT2_FWM:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT2_PAD_FWM__REG, &data);
		data = BMA2X2_SET_BITSLICE(data,
				BMA2X2_EN_INT2_PAD_FWM, fifo_int);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT2_PAD_FWM__REG, &data);
		break;
	default:
		comres = -1;
		break;
	}

	return comres;
}

static int bma2x2_set_ffull_int_pad_sel(struct i2c_client *client,
			unsigned char channel, unsigned char fifo_int)
{
	unsigned char data;
	int comres = 0;

	switch (channel) {
	case BMA2X2_INT1_FFULL:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT1_PAD_FFULL__REG, &data);
		data = BMA2X2_SET_BITSLICE(data,
				BMA2X2_EN_INT1_PAD_FFULL, fifo_int);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT1_PAD_FFULL__REG, &data);
		break;
	case BMA2X2_INT2_FFULL:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT2_PAD_FFULL__REG, &data);
		data = BMA2X2_SET_BITSLICE(data,
				BMA2X2_EN_INT2_PAD_FFULL, fifo_int);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT2_PAD_FFULL__REG, &data);
		break;
	default:
		comres = -1;
		break;
	}

	return comres;
}
#endif

#ifdef BMA2X2_ENABLE_INT1
static int bma2x2_set_int1_pad_sel(struct i2c_client *client, unsigned char
		int1sel)
{
	int comres = 0;
	unsigned char data;
	unsigned char state = 0x01;

	switch (int1sel) {
	case 0:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT1_PAD_LOWG__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_LOWG,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT1_PAD_LOWG__REG, &data);
		break;
	case 1:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT1_PAD_HIGHG__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_HIGHG,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT1_PAD_HIGHG__REG, &data);
		break;
	case 2:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT1_PAD_SLOPE__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_SLOPE,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT1_PAD_SLOPE__REG, &data);
		break;
	case 3:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT1_PAD_DB_TAP__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_DB_TAP,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT1_PAD_DB_TAP__REG, &data);
		break;
	case 4:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT1_PAD_SNG_TAP__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_SNG_TAP,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT1_PAD_SNG_TAP__REG, &data);
		break;
	case 5:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT1_PAD_ORIENT__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_ORIENT,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT1_PAD_ORIENT__REG, &data);
		break;
	case 6:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT1_PAD_FLAT__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_FLAT,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT1_PAD_FLAT__REG, &data);
		break;
	case 7:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT1_PAD_SLO_NO_MOT__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_SLO_NO_MOT,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT1_PAD_SLO_NO_MOT__REG, &data);
		break;

	default:
		break;
	}

	return comres;
}
#endif /* BMA2X2_ENABLE_INT1 */

#ifdef BMA2X2_ENABLE_INT2
static int bma2x2_set_int2_pad_sel(struct i2c_client *client, unsigned char
		int2sel)
{
	int comres = 0;
	unsigned char data;
	unsigned char state = 0x01;

	switch (int2sel) {
	case 0:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT2_PAD_LOWG__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_LOWG,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT2_PAD_LOWG__REG, &data);
		break;
	case 1:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT2_PAD_HIGHG__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_HIGHG,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT2_PAD_HIGHG__REG, &data);
		break;
	case 2:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT2_PAD_SLOPE__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_SLOPE,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT2_PAD_SLOPE__REG, &data);
		break;
	case 3:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT2_PAD_DB_TAP__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_DB_TAP,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT2_PAD_DB_TAP__REG, &data);
		break;
	case 4:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT2_PAD_SNG_TAP__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_SNG_TAP,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT2_PAD_SNG_TAP__REG, &data);
		break;
	case 5:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT2_PAD_ORIENT__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_ORIENT,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT2_PAD_ORIENT__REG, &data);
		break;
	case 6:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT2_PAD_FLAT__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_FLAT,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT2_PAD_FLAT__REG, &data);
		break;
	case 7:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_EN_INT2_PAD_SLO_NO_MOT__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_SLO_NO_MOT,
				state);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_EN_INT2_PAD_SLO_NO_MOT__REG, &data);
		break;
	default:
		break;
	}

	return comres;
}
#endif /* BMA2X2_ENABLE_INT2 */

static int bma2x2_set_Int_Enable(struct i2c_client *client, unsigned char
		InterruptType , unsigned char value)
{
	int comres = 0;
	unsigned char data1, data2;

	if ((11 < InterruptType) && (InterruptType < 16)) {
		switch (InterruptType) {
		case 12:
			/* slow/no motion X Interrupt  */
			comres = bma2x2_smbus_read_byte(client,
				BMA2X2_INT_SLO_NO_MOT_EN_X_INT__REG, &data1);
			data1 = BMA2X2_SET_BITSLICE(data1,
				BMA2X2_INT_SLO_NO_MOT_EN_X_INT, value);
			comres = bma2x2_smbus_write_byte(client,
				BMA2X2_INT_SLO_NO_MOT_EN_X_INT__REG, &data1);
			break;
		case 13:
			/* slow/no motion Y Interrupt  */
			comres = bma2x2_smbus_read_byte(client,
				BMA2X2_INT_SLO_NO_MOT_EN_Y_INT__REG, &data1);
			data1 = BMA2X2_SET_BITSLICE(data1,
				BMA2X2_INT_SLO_NO_MOT_EN_Y_INT, value);
			comres = bma2x2_smbus_write_byte(client,
				BMA2X2_INT_SLO_NO_MOT_EN_Y_INT__REG, &data1);
			break;
		case 14:
			/* slow/no motion Z Interrupt  */
			comres = bma2x2_smbus_read_byte(client,
				BMA2X2_INT_SLO_NO_MOT_EN_Z_INT__REG, &data1);
			data1 = BMA2X2_SET_BITSLICE(data1,
				BMA2X2_INT_SLO_NO_MOT_EN_Z_INT, value);
			comres = bma2x2_smbus_write_byte(client,
				BMA2X2_INT_SLO_NO_MOT_EN_Z_INT__REG, &data1);
			break;
		case 15:
			/* slow / no motion Interrupt select */
			comres = bma2x2_smbus_read_byte(client,
				BMA2X2_INT_SLO_NO_MOT_EN_SEL_INT__REG, &data1);
			data1 = BMA2X2_SET_BITSLICE(data1,
				BMA2X2_INT_SLO_NO_MOT_EN_SEL_INT, value);
			comres = bma2x2_smbus_write_byte(client,
				BMA2X2_INT_SLO_NO_MOT_EN_SEL_INT__REG, &data1);
		}

	return comres;
	}


	comres = bma2x2_smbus_read_byte(client, BMA2X2_INT_ENABLE1_REG, &data1);
	comres = bma2x2_smbus_read_byte(client, BMA2X2_INT_ENABLE2_REG, &data2);

	value = value & 1;
	switch (InterruptType) {
	case 0:
		/* Low G Interrupt  */
		data2 = BMA2X2_SET_BITSLICE(data2, BMA2X2_EN_LOWG_INT, value);
		break;

	case 1:
		/* High G X Interrupt */
		data2 = BMA2X2_SET_BITSLICE(data2, BMA2X2_EN_HIGHG_X_INT,
				value);
		break;

	case 2:
		/* High G Y Interrupt */
		data2 = BMA2X2_SET_BITSLICE(data2, BMA2X2_EN_HIGHG_Y_INT,
				value);
		break;

	case 3:
		/* High G Z Interrupt */
		data2 = BMA2X2_SET_BITSLICE(data2, BMA2X2_EN_HIGHG_Z_INT,
				value);
		break;

	case 4:
		/* New Data Interrupt  */
		data2 = BMA2X2_SET_BITSLICE(data2, BMA2X2_EN_NEW_DATA_INT,
				value);
		break;

	case 5:
		/* Slope X Interrupt */
		data1 = BMA2X2_SET_BITSLICE(data1, BMA2X2_EN_SLOPE_X_INT,
				value);
		break;

	case 6:
		/* Slope Y Interrupt */
		data1 = BMA2X2_SET_BITSLICE(data1, BMA2X2_EN_SLOPE_Y_INT,
				value);
		break;

	case 7:
		/* Slope Z Interrupt */
		data1 = BMA2X2_SET_BITSLICE(data1, BMA2X2_EN_SLOPE_Z_INT,
				value);
		break;

	case 8:
		/* Single Tap Interrupt */
		data1 = BMA2X2_SET_BITSLICE(data1, BMA2X2_EN_SINGLE_TAP_INT,
				value);
		break;

	case 9:
		/* Double Tap Interrupt */
		data1 = BMA2X2_SET_BITSLICE(data1, BMA2X2_EN_DOUBLE_TAP_INT,
				value);
		break;

	case 10:
		/* Orient Interrupt  */
		data1 = BMA2X2_SET_BITSLICE(data1, BMA2X2_EN_ORIENT_INT, value);
		break;

	case 11:
		/* Flat Interrupt */
		data1 = BMA2X2_SET_BITSLICE(data1, BMA2X2_EN_FLAT_INT, value);
		break;

	default:
		break;
	}
	comres = bma2x2_smbus_write_byte(client, BMA2X2_INT_ENABLE1_REG,
			&data1);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_INT_ENABLE2_REG,
			&data2);

	return comres;
}

static int bma2x2_set_watermark_int(struct i2c_client *client, bool enable)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
		BMA2X2_INT_FWM_EN_INT__REG, &data);
	data = BMA2X2_SET_BITSLICE(data,
		BMA2X2_INT_FWM_EN_INT, (unsigned char)(enable ? 1 : 0));
	comres = bma2x2_smbus_write_byte(client,
		BMA2X2_INT_FWM_EN_INT__REG, &data);

	return comres;
}

static int bma2x2_set_fifo_full_int(struct i2c_client *client, bool enable)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
		BMA2X2_INT_FFULL_EN_INT__REG, &data);
	data = BMA2X2_SET_BITSLICE(data,
		BMA2X2_INT_FFULL_EN_INT, (unsigned char)(enable ? 1 : 0));
	comres = bma2x2_smbus_write_byte(client,
		BMA2X2_INT_FFULL_EN_INT__REG, &data);

	return comres;
}

#if defined(BMA2X2_ENABLE_INT1) || defined(BMA2X2_ENABLE_INT2)
static int bma2x2_get_HIGH_first(struct i2c_client *client, unsigned char
						param, unsigned char *intstatus)
{
	int comres = 0;
	unsigned char data;

	switch (param) {
	case 0:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_STATUS_ORIENT_HIGH_REG, &data);
		data = BMA2X2_GET_BITSLICE(data, BMA2X2_HIGHG_FIRST_X);
		*intstatus = data;
		break;
	case 1:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_STATUS_ORIENT_HIGH_REG, &data);
		data = BMA2X2_GET_BITSLICE(data, BMA2X2_HIGHG_FIRST_Y);
		*intstatus = data;
		break;
	case 2:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_STATUS_ORIENT_HIGH_REG, &data);
		data = BMA2X2_GET_BITSLICE(data, BMA2X2_HIGHG_FIRST_Z);
		*intstatus = data;
		break;
	default:
		break;
	}

	return comres;
}

static int bma2x2_get_HIGH_sign(struct i2c_client *client, unsigned char
		*intstatus)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_STATUS_ORIENT_HIGH_REG,
			&data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_HIGHG_SIGN_S);
	*intstatus = data;

	return comres;
}

#ifndef CONFIG_SIG_MOTION
static int bma2x2_get_slope_first(struct i2c_client *client, unsigned char
	param, unsigned char *intstatus)
{
	int comres = 0;
	unsigned char data;

	switch (param) {
	case 0:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_STATUS_TAP_SLOPE_REG, &data);
		data = BMA2X2_GET_BITSLICE(data, BMA2X2_SLOPE_FIRST_X);
		*intstatus = data;
		break;
	case 1:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_STATUS_TAP_SLOPE_REG, &data);
		data = BMA2X2_GET_BITSLICE(data, BMA2X2_SLOPE_FIRST_Y);
		*intstatus = data;
		break;
	case 2:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_STATUS_TAP_SLOPE_REG, &data);
		data = BMA2X2_GET_BITSLICE(data, BMA2X2_SLOPE_FIRST_Z);
		*intstatus = data;
		break;
	default:
		break;
	}

	return comres;
}

static int bma2x2_get_slope_sign(struct i2c_client *client, unsigned char
		*intstatus)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_STATUS_TAP_SLOPE_REG,
			&data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_SLOPE_SIGN_S);
	*intstatus = data;

	return comres;
}
#endif /* CONFIG_SIG_MOTION */

static int bma2x2_get_orient_status(struct i2c_client *client, unsigned char
		*intstatus)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_STATUS_ORIENT_HIGH_REG,
			&data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_ORIENT_S);
	*intstatus = data;

	return comres;
}

static int bma2x2_get_orient_flat_status(struct i2c_client *client, unsigned
		char *intstatus)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_STATUS_ORIENT_HIGH_REG,
			&data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_FLAT_S);
	*intstatus = data;

	return comres;
}
#endif /* defined(BMA2X2_ENABLE_INT1)||defined(BMA2X2_ENABLE_INT2) */

static int bma2x2_set_Int_Mode(struct i2c_client *client, unsigned char Mode)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
			BMA2X2_INT_MODE_SEL__REG, &data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_INT_MODE_SEL, Mode);
	comres = bma2x2_smbus_write_byte(client,
			BMA2X2_INT_MODE_SEL__REG, &data);

	return comres;
}

static int bma2x2_set_int1_active_lvl(struct i2c_client *client,
		bool activeHigh)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
			BMA2X2_INT1_PAD_ACTIVE_LEVEL__REG, &data);
	if (comres)
		return comres;

	data = BMA2X2_SET_BITSLICE(data, BMA2X2_INT1_PAD_ACTIVE_LEVEL,
		(unsigned char)(activeHigh ? 1 : 0));
	comres = bma2x2_smbus_write_byte(client,
			BMA2X2_INT1_PAD_ACTIVE_LEVEL__REG, &data);

	return comres;
}

static int bma2x2_set_int2_active_lvl(struct i2c_client *client,
		bool activeHigh)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
			BMA2X2_INT2_PAD_ACTIVE_LEVEL__REG, &data);
	if (comres)
		return comres;

	data = BMA2X2_SET_BITSLICE(data, BMA2X2_INT2_PAD_ACTIVE_LEVEL,
		(unsigned char)(activeHigh ? 1 : 0));
	comres = bma2x2_smbus_write_byte(client,
			BMA2X2_INT2_PAD_ACTIVE_LEVEL__REG, &data);

	return comres;
}

static int bma2x2_get_Int_Mode(struct i2c_client *client, unsigned char *Mode)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
			BMA2X2_INT_MODE_SEL__REG, &data);
	data  = BMA2X2_GET_BITSLICE(data, BMA2X2_INT_MODE_SEL);
	*Mode = data;


	return comres;
}
static int bma2x2_set_slope_duration(struct i2c_client *client, unsigned char
		duration)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
			BMA2X2_SLOPE_DUR__REG, &data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_SLOPE_DUR, duration);
	comres = bma2x2_smbus_write_byte(client,
			BMA2X2_SLOPE_DUR__REG, &data);

	return comres;
}

static int bma2x2_get_slope_duration(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
			BMA2X2_SLOPE_DURN_REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_SLOPE_DUR);
	*status = data;


	return comres;
}

static int bma2x2_set_slope_no_mot_duration(struct i2c_client *client,
			unsigned char duration)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
			BMA2x2_SLO_NO_MOT_DUR__REG, &data);
	data = BMA2X2_SET_BITSLICE(data, BMA2x2_SLO_NO_MOT_DUR, duration);
	comres = bma2x2_smbus_write_byte(client,
			BMA2x2_SLO_NO_MOT_DUR__REG, &data);


	return comres;
}

static int bma2x2_get_slope_no_mot_duration(struct i2c_client *client,
			unsigned char *status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
			BMA2x2_SLO_NO_MOT_DUR__REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2x2_SLO_NO_MOT_DUR);
	*status = data;


	return comres;
}

static int bma2x2_set_slope_threshold(struct i2c_client *client,
		unsigned char threshold)
{
	int comres = 0;
	unsigned char data;

	data = threshold;
	comres = bma2x2_smbus_write_byte(client,
			BMA2X2_SLOPE_THRES__REG, &data);

	return comres;
}

static int bma2x2_get_slope_threshold(struct i2c_client *client,
		unsigned char *status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
			BMA2X2_SLOPE_THRES_REG, &data);
	*status = data;

	return comres;
}

static int bma2x2_set_slope_no_mot_threshold(struct i2c_client *client,
		unsigned char threshold)
{
	int comres = 0;
	unsigned char data;

	data = threshold;
	comres = bma2x2_smbus_write_byte(client,
			BMA2X2_SLO_NO_MOT_THRES_REG, &data);

	return comres;
}

static int bma2x2_get_slope_no_mot_threshold(struct i2c_client *client,
		unsigned char *status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
			BMA2X2_SLO_NO_MOT_THRES_REG, &data);
	*status = data;

	return comres;
}


static int bma2x2_set_low_g_duration(struct i2c_client *client, unsigned char
		duration)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_LOWG_DUR__REG, &data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_LOWG_DUR, duration);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_LOWG_DUR__REG, &data);

	return comres;
}

static int bma2x2_get_low_g_duration(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_LOW_DURN_REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_LOWG_DUR);
	*status = data;

	return comres;
}

static int bma2x2_set_low_g_threshold(struct i2c_client *client, unsigned char
		threshold)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_LOWG_THRES__REG, &data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_LOWG_THRES, threshold);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_LOWG_THRES__REG, &data);

	return comres;
}

static int bma2x2_get_low_g_threshold(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_LOW_THRES_REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_LOWG_THRES);
	*status = data;

	return comres;
}

static int bma2x2_set_high_g_duration(struct i2c_client *client, unsigned char
		duration)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_HIGHG_DUR__REG, &data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_HIGHG_DUR, duration);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_HIGHG_DUR__REG, &data);

	return comres;
}

static int bma2x2_get_high_g_duration(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_HIGH_DURN_REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_HIGHG_DUR);
	*status = data;

	return comres;
}

static int bma2x2_set_high_g_threshold(struct i2c_client *client, unsigned char
		threshold)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_HIGHG_THRES__REG, &data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_HIGHG_THRES, threshold);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_HIGHG_THRES__REG,
			&data);

	return comres;
}

static int bma2x2_get_high_g_threshold(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_HIGH_THRES_REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_HIGHG_THRES);
	*status = data;

	return comres;
}


static int bma2x2_set_tap_duration(struct i2c_client *client, unsigned char
		duration)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_TAP_DUR__REG, &data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_TAP_DUR, duration);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_TAP_DUR__REG, &data);

	return comres;
}

static int bma2x2_get_tap_duration(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_TAP_PARAM_REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_TAP_DUR);
	*status = data;

	return comres;
}

static int bma2x2_set_tap_shock(struct i2c_client *client, unsigned char setval)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_TAP_SHOCK_DURN__REG,
			&data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_TAP_SHOCK_DURN, setval);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_TAP_SHOCK_DURN__REG,
			&data);

	return comres;
}

static int bma2x2_get_tap_shock(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_TAP_PARAM_REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_TAP_SHOCK_DURN);
	*status = data;

	return comres;
}

static int bma2x2_set_tap_quiet(struct i2c_client *client, unsigned char
		duration)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_TAP_QUIET_DURN__REG,
			&data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_TAP_QUIET_DURN, duration);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_TAP_QUIET_DURN__REG,
			&data);

	return comres;
}

static int bma2x2_get_tap_quiet(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_TAP_PARAM_REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_TAP_QUIET_DURN);
	*status = data;

	return comres;
}

static int bma2x2_set_tap_threshold(struct i2c_client *client, unsigned char
		threshold)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_TAP_THRES__REG, &data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_TAP_THRES, threshold);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_TAP_THRES__REG, &data);

	return comres;
}

static int bma2x2_get_tap_threshold(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_TAP_THRES_REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_TAP_THRES);
	*status = data;

	return comres;
}

static int bma2x2_set_tap_samp(struct i2c_client *client, unsigned char samp)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_TAP_SAMPLES__REG, &data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_TAP_SAMPLES, samp);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_TAP_SAMPLES__REG,
			&data);

	return comres;
}

static int bma2x2_get_tap_samp(struct i2c_client *client, unsigned char *status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_TAP_THRES_REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_TAP_SAMPLES);
	*status = data;

	return comres;
}

static int bma2x2_set_orient_mode(struct i2c_client *client, unsigned char mode)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_ORIENT_MODE__REG, &data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_ORIENT_MODE, mode);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_ORIENT_MODE__REG,
			&data);

	return comres;
}

static int bma2x2_get_orient_mode(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_ORIENT_PARAM_REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_ORIENT_MODE);
	*status = data;

	return comres;
}

static int bma2x2_set_orient_blocking(struct i2c_client *client, unsigned char
		samp)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_ORIENT_BLOCK__REG,
			&data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_ORIENT_BLOCK, samp);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_ORIENT_BLOCK__REG,
			&data);

	return comres;
}

static int bma2x2_get_orient_blocking(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_ORIENT_PARAM_REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_ORIENT_BLOCK);
	*status = data;

	return comres;
}

static int bma2x2_set_orient_hyst(struct i2c_client *client, unsigned char
		orienthyst)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_ORIENT_HYST__REG, &data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_ORIENT_HYST, orienthyst);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_ORIENT_HYST__REG,
			&data);

	return comres;
}

static int bma2x2_get_orient_hyst(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_ORIENT_PARAM_REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_ORIENT_HYST);
	*status = data;

	return comres;
}
static int bma2x2_set_theta_blocking(struct i2c_client *client, unsigned char
		thetablk)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_THETA_BLOCK__REG, &data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_THETA_BLOCK, thetablk);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_THETA_BLOCK__REG,
			&data);

	return comres;
}

static int bma2x2_get_theta_blocking(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_THETA_BLOCK_REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_THETA_BLOCK);
	*status = data;

	return comres;
}

static int bma2x2_set_theta_flat(struct i2c_client *client, unsigned char
		thetaflat)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_THETA_FLAT__REG, &data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_THETA_FLAT, thetaflat);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_THETA_FLAT__REG, &data);

	return comres;
}

static int bma2x2_get_theta_flat(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_THETA_FLAT_REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_THETA_FLAT);
	*status = data;

	return comres;
}

static int bma2x2_set_flat_hold_time(struct i2c_client *client, unsigned char
		holdtime)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_FLAT_HOLD_TIME__REG,
			&data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_FLAT_HOLD_TIME, holdtime);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_FLAT_HOLD_TIME__REG,
			&data);

	return comres;
}

static int bma2x2_get_flat_hold_time(struct i2c_client *client, unsigned char
		*holdtime)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_FLAT_HOLD_TIME_REG,
			&data);
	data  = BMA2X2_GET_BITSLICE(data, BMA2X2_FLAT_HOLD_TIME);
	*holdtime = data;

	return comres;
}

/*!
 * brief: bma2x2 switch from normal to suspend mode
 * @param[i] bma2x2
 * @param[i] data1, write to PMU_LPW
 * @param[i] data2, write to PMU_LOW_NOSIE
 *
 * @return zero success, none-zero failed
 */
static int bma2x2_normal_to_suspend(struct bma2x2_data *bma2x2,
				unsigned char data1, unsigned char data2)
{
	unsigned char current_fifo_mode;
	unsigned char current_op_mode;

	if (bma2x2 == NULL)
		return -EINVAL;
	/* get current op mode from mode register */
	if (bma2x2_get_mode(bma2x2->bma2x2_client, &current_op_mode) < 0)
		return -EIO;
	/* only aimed at operatiom mode chang from normal/lpw1 mode
	 * to suspend state.
	*/
	if (current_op_mode == BMA2X2_MODE_NORMAL ||
			current_op_mode == BMA2X2_MODE_LOWPOWER1) {
		/* get current fifo mode from fifo config register */
		if (bma2x2_get_fifo_mode(bma2x2->bma2x2_client,
							&current_fifo_mode) < 0)
			return -EIO;

		bma2x2_smbus_write_byte(bma2x2->bma2x2_client,
				BMA2X2_LOW_NOISE_CTRL_REG, &data2);
		bma2x2_smbus_write_byte(bma2x2->bma2x2_client,
				BMA2X2_MODE_CTRL_REG, &data1);
		bma2x2_smbus_write_byte(bma2x2->bma2x2_client,
			BMA2X2_FIFO_MODE__REG, &current_fifo_mode);
		WAIT_DEVICE_READY();

		return 0;
	}

	bma2x2_smbus_write_byte(bma2x2->bma2x2_client,
				BMA2X2_LOW_NOISE_CTRL_REG, &data2);
	bma2x2_smbus_write_byte(bma2x2->bma2x2_client,
				BMA2X2_MODE_CTRL_REG, &data1);
	WAIT_DEVICE_READY();

	return 0;
}

static int bma2x2_set_mode(struct i2c_client *client, unsigned char mode)
{
	int comres = 0;
	unsigned char data1, data2;
	int ret = 0;
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	mutex_lock(&bma2x2->mode_mutex);
	if (BMA2X2_MODE_SUSPEND == mode) {
		if (bma2x2->ref_count > 0) {
			bma2x2->ref_count--;
			if (0 < bma2x2->ref_count) {
				mutex_unlock(&bma2x2->mode_mutex);
				return 0;
			}
		}
	} else {
		bma2x2->ref_count++;
		if (1 < bma2x2->ref_count) {
			mutex_unlock(&bma2x2->mode_mutex);
			return 0;
		}
	}
	mutex_unlock(&bma2x2->mode_mutex);

	if (mode < 6) {
		comres = bma2x2_smbus_read_byte(client, BMA2X2_MODE_CTRL_REG,
				&data1);
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_LOW_NOISE_CTRL_REG,
				&data2);
		switch (mode) {
		case BMA2X2_MODE_NORMAL:
			data1  = BMA2X2_SET_BITSLICE(data1,
					BMA2X2_MODE_CTRL, 0);
			data2  = BMA2X2_SET_BITSLICE(data2,
					BMA2X2_LOW_POWER_MODE, 0);
			bma2x2_smbus_write_byte(client,
					BMA2X2_MODE_CTRL_REG, &data1);
			WAIT_DEVICE_READY();
			bma2x2_smbus_write_byte(client,
				BMA2X2_LOW_NOISE_CTRL_REG, &data2);
			break;
		case BMA2X2_MODE_LOWPOWER1:
			data1  = BMA2X2_SET_BITSLICE(data1,
					BMA2X2_MODE_CTRL, 2);
			data2  = BMA2X2_SET_BITSLICE(data2,
					BMA2X2_LOW_POWER_MODE, 0);
			bma2x2_smbus_write_byte(client,
					BMA2X2_MODE_CTRL_REG, &data1);
			WAIT_DEVICE_READY();
			bma2x2_smbus_write_byte(client,
				BMA2X2_LOW_NOISE_CTRL_REG, &data2);
			break;
		case BMA2X2_MODE_SUSPEND:
			data1  = BMA2X2_SET_BITSLICE(data1,
					BMA2X2_MODE_CTRL, 4);
			data2  = BMA2X2_SET_BITSLICE(data2,
					BMA2X2_LOW_POWER_MODE, 0);
			/*aimed at anomaly resolution when switch to suspend*/
			ret = bma2x2_normal_to_suspend(bma2x2, data1, data2);
			if (ret < 0)
				dev_err(&client->dev,
				"Error switching to suspend");
			break;
		case BMA2X2_MODE_DEEP_SUSPEND:
			data1  = BMA2X2_SET_BITSLICE(data1,
						BMA2X2_MODE_CTRL, 1);
			data2  = BMA2X2_SET_BITSLICE(data2,
					BMA2X2_LOW_POWER_MODE, 1);
			bma2x2_smbus_write_byte(client,
					BMA2X2_MODE_CTRL_REG, &data1);
			WAIT_DEVICE_READY();
			bma2x2_smbus_write_byte(client,
				BMA2X2_LOW_NOISE_CTRL_REG, &data2);
			break;
		case BMA2X2_MODE_LOWPOWER2:
			data1  = BMA2X2_SET_BITSLICE(data1,
					BMA2X2_MODE_CTRL, 2);
			data2  = BMA2X2_SET_BITSLICE(data2,
					BMA2X2_LOW_POWER_MODE, 1);
			bma2x2_smbus_write_byte(client,
					BMA2X2_MODE_CTRL_REG, &data1);
			WAIT_DEVICE_READY();
			bma2x2_smbus_write_byte(client,
				BMA2X2_LOW_NOISE_CTRL_REG, &data2);
			break;
		case BMA2X2_MODE_STANDBY:
			data1  = BMA2X2_SET_BITSLICE(data1,
					BMA2X2_MODE_CTRL, 4);
			data2  = BMA2X2_SET_BITSLICE(data2,
					BMA2X2_LOW_POWER_MODE, 1);
			bma2x2_smbus_write_byte(client,
				BMA2X2_LOW_NOISE_CTRL_REG, &data2);
			WAIT_DEVICE_READY();
			bma2x2_smbus_write_byte(client,
					BMA2X2_MODE_CTRL_REG, &data1);
			break;
		}
	} else {
		comres = -1;
	}

	return comres;
}


static int bma2x2_get_mode(struct i2c_client *client, unsigned char *mode)
{
	int comres = 0;
	unsigned char data1, data2;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_MODE_CTRL_REG, &data1);
	comres = bma2x2_smbus_read_byte(client, BMA2X2_LOW_NOISE_CTRL_REG,
			&data2);

	data1  = (data1 & 0xE0) >> 5;
	data2  = (data2 & 0x40) >> 6;

	if (data2 == 0x00) {
		switch (data1) {
		case 0:
			*mode  = BMA2X2_MODE_NORMAL;
			break;
		case 1:
			*mode  = BMA2X2_MODE_DEEP_SUSPEND;
			break;
		case 2:
			*mode  = BMA2X2_MODE_LOWPOWER1;
			break;
		case 4:
		case 6:
			*mode  = BMA2X2_MODE_SUSPEND;
			break;
		default:
			comres = -ENODEV;
			break;
		}
	} else if (data2 == 0x01) {
		switch (data1) {
		case 0:
		case 1:
		case 6:
			*mode  = BMA2X2_MODE_DEEP_SUSPEND;
			break;
		case 2:
			*mode  = BMA2X2_MODE_LOWPOWER2;
			break;
		case 4:
			*mode  = BMA2X2_MODE_STANDBY;
			break;
		default:
			comres = -ENODEV;
			break;
		}
	} else {
		comres = -ENODEV;
	}

	return comres;
}

static int bma2x2_set_range(struct i2c_client *client, unsigned char Range)
{
	int comres = 0;
	unsigned char data1;
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if ((Range == 3) || (Range == 5) || (Range == 8) || (Range == 12)) {
		comres = bma2x2_smbus_read_byte(client, BMA2X2_RANGE_SEL_REG,
				&data1);
		switch (Range) {
		case BMA2X2_RANGE_2G:
			data1  = BMA2X2_SET_BITSLICE(data1,
					BMA2X2_RANGE_SEL, 3);
			break;
		case BMA2X2_RANGE_4G:
			data1  = BMA2X2_SET_BITSLICE(data1,
					BMA2X2_RANGE_SEL, 5);
			break;
		case BMA2X2_RANGE_8G:
			data1  = BMA2X2_SET_BITSLICE(data1,
					BMA2X2_RANGE_SEL, 8);
			break;
		case BMA2X2_RANGE_16G:
			data1  = BMA2X2_SET_BITSLICE(data1,
					BMA2X2_RANGE_SEL, 12);
			break;
		default:
			break;
		}
		comres += bma2x2_smbus_write_byte(client, BMA2X2_RANGE_SEL_REG,
				&data1);
		bma2x2_get_sensitivity(bma2x2, Range);
	} else {
		comres = -1;
	}

	return comres;
}

static int bma2x2_get_range(struct i2c_client *client, unsigned char *Range)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_RANGE_SEL__REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_RANGE_SEL);
	*Range = data;

	return comres;
}

static int bma2x2_set_watermark_lvl(struct i2c_client *client,
					unsigned char watermark)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
		BMA2X2_FIFO_WML_TRIG_RETAIN__REG, &data);
	data = BMA2X2_SET_BITSLICE(data,
		BMA2X2_FIFO_WML_TRIG_RETAIN, watermark);
	comres = bma2x2_smbus_write_byte(client,
		BMA2X2_FIFO_WML_TRIG_RETAIN__REG, &data);

	return comres;
}

static int bma2x2_set_bandwidth(struct i2c_client *client, unsigned char BW)
{
	int comres = 0;
	unsigned char data;
	int Bandwidth = 0;
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (BW > 7 && BW < 16) {
		switch (BW) {
		case BMA2X2_BW_7_81HZ:
			Bandwidth = BMA2X2_BW_7_81HZ;

			/*  7.81 Hz      64000 uS   */
			break;
		case BMA2X2_BW_15_63HZ:
			Bandwidth = BMA2X2_BW_15_63HZ;

			/*  15.63 Hz     32000 uS   */
			break;
		case BMA2X2_BW_31_25HZ:
			Bandwidth = BMA2X2_BW_31_25HZ;

			/*  31.25 Hz     16000 uS   */
			break;
		case BMA2X2_BW_62_50HZ:
			Bandwidth = BMA2X2_BW_62_50HZ;

			/*  62.50 Hz     8000 uS   */
			break;
		case BMA2X2_BW_125HZ:
			Bandwidth = BMA2X2_BW_125HZ;

			/*  125 Hz       4000 uS   */
			break;
		case BMA2X2_BW_250HZ:
			Bandwidth = BMA2X2_BW_250HZ;

			/*  250 Hz       2000 uS   */
			break;
		case BMA2X2_BW_500HZ:
			Bandwidth = BMA2X2_BW_500HZ;

			/*  500 Hz       1000 uS   */
			break;
		case BMA2X2_BW_1000HZ:
			Bandwidth = BMA2X2_BW_1000HZ;

			/*  1000 Hz      500 uS   */
			break;
		default:
			break;
		}
		comres = bma2x2_smbus_read_byte(client, BMA2X2_BANDWIDTH__REG,
				&data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_BANDWIDTH, Bandwidth);
		comres += bma2x2_smbus_write_byte(client, BMA2X2_BANDWIDTH__REG,
				&data);
		if (comres == 0)
			bma2x2->bandwidth = Bandwidth;
	} else {
		comres = -1;
	}

	return comres;
}

static int bma2x2_get_bandwidth(struct i2c_client *client, unsigned char *BW)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_BANDWIDTH__REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_BANDWIDTH);
	*BW = data;

	return comres;
}

static int bma2x2_get_sleep_duration(struct i2c_client *client, unsigned char
		*sleep_dur)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
			BMA2X2_SLEEP_DUR__REG, &data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_SLEEP_DUR);
	*sleep_dur = data;

	return comres;
}

static int bma2x2_set_sleep_duration(struct i2c_client *client, unsigned char
		sleep_dur)
{
	int comres = 0;
	unsigned char data;
	int sleep_duration = 0;

	if (sleep_dur > 4 && sleep_dur < 16) {
		switch (sleep_dur) {
		case BMA2X2_SLEEP_DUR_0_5MS:
			sleep_duration = BMA2X2_SLEEP_DUR_0_5MS;

			/*  0.5 MS   */
			break;
		case BMA2X2_SLEEP_DUR_1MS:
			sleep_duration = BMA2X2_SLEEP_DUR_1MS;

			/*  1 MS  */
			break;
		case BMA2X2_SLEEP_DUR_2MS:
			sleep_duration = BMA2X2_SLEEP_DUR_2MS;

			/*  2 MS  */
			break;
		case BMA2X2_SLEEP_DUR_4MS:
			sleep_duration = BMA2X2_SLEEP_DUR_4MS;

			/*  4 MS   */
			break;
		case BMA2X2_SLEEP_DUR_6MS:
			sleep_duration = BMA2X2_SLEEP_DUR_6MS;

			/*  6 MS  */
			break;
		case BMA2X2_SLEEP_DUR_10MS:
			sleep_duration = BMA2X2_SLEEP_DUR_10MS;

			/*  10 MS  */
			break;
		case BMA2X2_SLEEP_DUR_25MS:
			sleep_duration = BMA2X2_SLEEP_DUR_25MS;

			/*  25 MS  */
			break;
		case BMA2X2_SLEEP_DUR_50MS:
			sleep_duration = BMA2X2_SLEEP_DUR_50MS;

			/*  50 MS   */
			break;
		case BMA2X2_SLEEP_DUR_100MS:
			sleep_duration = BMA2X2_SLEEP_DUR_100MS;

			/*  100 MS  */
			break;
		case BMA2X2_SLEEP_DUR_500MS:
			sleep_duration = BMA2X2_SLEEP_DUR_500MS;

			/*  500 MS   */
			break;
		case BMA2X2_SLEEP_DUR_1S:
			sleep_duration = BMA2X2_SLEEP_DUR_1S;

			/*  1 SECS   */
			break;
		default:
			break;
		}
		comres = bma2x2_smbus_read_byte(client, BMA2X2_SLEEP_DUR__REG,
				&data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_SLEEP_DUR,
				sleep_duration);
		comres = bma2x2_smbus_write_byte(client, BMA2X2_SLEEP_DUR__REG,
				&data);
	} else {
		comres = -1;
	}

	return comres;
}

static int bma2x2_get_fifo_mode(struct i2c_client *client, unsigned char
		*fifo_mode)
{
	int comres;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_FIFO_MODE__REG, &data);
	*fifo_mode = BMA2X2_GET_BITSLICE(data, BMA2X2_FIFO_MODE);

	return comres;
}

static int bma2x2_set_fifo_mode(struct i2c_client *client, unsigned char
		fifo_mode)
{
	unsigned char data;
	int comres = 0;

	if (fifo_mode < 4) {
		comres = bma2x2_smbus_read_byte(client, BMA2X2_FIFO_MODE__REG,
				&data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_FIFO_MODE, fifo_mode);
		comres = bma2x2_smbus_write_byte(client, BMA2X2_FIFO_MODE__REG,
				&data);
	} else {
		comres = -1;
	}

	return comres;
}

static int bma2x2_get_fifo_trig(struct i2c_client *client, unsigned char
		*fifo_trig)
{
	int comres;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
			BMA2X2_FIFO_TRIGGER_ACTION__REG, &data);
	*fifo_trig = BMA2X2_GET_BITSLICE(data, BMA2X2_FIFO_TRIGGER_ACTION);

	return comres;
}

static int bma2x2_set_fifo_trig(struct i2c_client *client, unsigned char
		fifo_trig)
{
	unsigned char data;
	int comres = 0;

	if (fifo_trig < 4) {
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_FIFO_TRIGGER_ACTION__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_FIFO_TRIGGER_ACTION,
				fifo_trig);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_FIFO_TRIGGER_ACTION__REG, &data);
	} else {
		comres = -1;
	}

	return comres;
}

static int bma2x2_get_fifo_trig_src(struct i2c_client *client, unsigned char
		*trig_src)
{
	int comres;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
			BMA2X2_FIFO_TRIGGER_SOURCE__REG, &data);
	*trig_src = BMA2X2_GET_BITSLICE(data, BMA2X2_FIFO_TRIGGER_SOURCE);

	return comres;
}

static int bma2x2_set_fifo_trig_src(struct i2c_client *client, unsigned char
		trig_src)
{
	unsigned char data;
	int comres = 0;

	if (trig_src < 4) {
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_FIFO_TRIGGER_SOURCE__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_FIFO_TRIGGER_SOURCE,
				trig_src);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_FIFO_TRIGGER_SOURCE__REG, &data);
	} else {
		comres = -1;
	}

	return comres;
}

static int bma2x2_get_fifo_framecount(struct i2c_client *client, unsigned char
			 *framecount)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
			BMA2X2_FIFO_FRAME_COUNTER_S__REG, &data);
	*framecount = BMA2X2_GET_BITSLICE(data, BMA2X2_FIFO_FRAME_COUNTER_S);

	return comres;
}

static int bma2x2_get_fifo_data_sel(struct i2c_client *client, unsigned char
		*data_sel)
{
	int comres;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client,
			BMA2X2_FIFO_DATA_SELECT__REG, &data);
	*data_sel = BMA2X2_GET_BITSLICE(data, BMA2X2_FIFO_DATA_SELECT);

	return comres;
}

static int bma2x2_set_fifo_data_sel(struct i2c_client *client, unsigned char
		data_sel)
{
	unsigned char data;
	int comres = 0;

	if (data_sel < 4) {
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_FIFO_DATA_SELECT__REG,
				&data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_FIFO_DATA_SELECT,
				data_sel);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_FIFO_DATA_SELECT__REG,
				&data);
	} else {
		comres = -1;
	}

	return comres;
}


static int bma2x2_get_offset_target(struct i2c_client *client, unsigned char
		channel, unsigned char *offset)
{
	unsigned char data;
	int comres = 0;

	switch (channel) {
	case BMA2X2_CUT_OFF:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_COMP_CUTOFF__REG, &data);
		*offset = BMA2X2_GET_BITSLICE(data, BMA2X2_COMP_CUTOFF);
		break;
	case BMA2X2_OFFSET_TRIGGER_X:
		comres = bma2x2_smbus_read_byte(client,
			BMA2X2_COMP_TARGET_OFFSET_X__REG, &data);
		*offset = BMA2X2_GET_BITSLICE(data,
				BMA2X2_COMP_TARGET_OFFSET_X);
		break;
	case BMA2X2_OFFSET_TRIGGER_Y:
		comres = bma2x2_smbus_read_byte(client,
			BMA2X2_COMP_TARGET_OFFSET_Y__REG, &data);
		*offset = BMA2X2_GET_BITSLICE(data,
				BMA2X2_COMP_TARGET_OFFSET_Y);
		break;
	case BMA2X2_OFFSET_TRIGGER_Z:
		comres = bma2x2_smbus_read_byte(client,
			BMA2X2_COMP_TARGET_OFFSET_Z__REG, &data);
		*offset = BMA2X2_GET_BITSLICE(data,
				BMA2X2_COMP_TARGET_OFFSET_Z);
		break;
	default:
		comres = -1;
		break;
	}

	return comres;
}

static int bma2x2_set_offset_target(struct i2c_client *client, unsigned char
		channel, unsigned char offset)
{
	unsigned char data;
	int comres = 0;

	switch (channel) {
	case BMA2X2_CUT_OFF:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_COMP_CUTOFF__REG, &data);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_COMP_CUTOFF,
				offset);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_COMP_CUTOFF__REG, &data);
		break;
	case BMA2X2_OFFSET_TRIGGER_X:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_COMP_TARGET_OFFSET_X__REG,
				&data);
		data = BMA2X2_SET_BITSLICE(data,
				BMA2X2_COMP_TARGET_OFFSET_X,
				offset);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_COMP_TARGET_OFFSET_X__REG,
				&data);
		break;
	case BMA2X2_OFFSET_TRIGGER_Y:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_COMP_TARGET_OFFSET_Y__REG,
				&data);
		data = BMA2X2_SET_BITSLICE(data,
				BMA2X2_COMP_TARGET_OFFSET_Y,
				offset);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_COMP_TARGET_OFFSET_Y__REG,
				&data);
		break;
	case BMA2X2_OFFSET_TRIGGER_Z:
		comres = bma2x2_smbus_read_byte(client,
				BMA2X2_COMP_TARGET_OFFSET_Z__REG,
				&data);
		data = BMA2X2_SET_BITSLICE(data,
				BMA2X2_COMP_TARGET_OFFSET_Z,
				offset);
		comres = bma2x2_smbus_write_byte(client,
				BMA2X2_COMP_TARGET_OFFSET_Z__REG,
				&data);
		break;
	default:
		comres = -1;
		break;
	}

	return comres;
}

static int bma2x2_get_cal_ready(struct i2c_client *client,
					unsigned char *calrdy)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_FAST_CAL_RDY_S__REG,
			&data);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_FAST_CAL_RDY_S);
	*calrdy = data;

	return comres;
}

static int bma2x2_set_cal_trigger(struct i2c_client *client, unsigned char
		caltrigger)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_CAL_TRIGGER__REG, &data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_CAL_TRIGGER, caltrigger);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_CAL_TRIGGER__REG,
			&data);

	return comres;
}

static int bma2x2_write_reg(struct i2c_client *client, unsigned char addr,
		unsigned char *data)
{
	int comres = 0;
	comres = bma2x2_smbus_write_byte(client, addr, data);

	return comres;
}


static int bma2x2_set_offset_x(struct i2c_client *client, unsigned char
		offsetfilt)
{
	int comres = 0;
	unsigned char data;

	data =  offsetfilt;

#ifdef CONFIG_SENSORS_BMI058
	comres = bma2x2_smbus_write_byte(client, BMI058_OFFSET_X_AXIS_REG,
							&data);
#else
	comres = bma2x2_smbus_write_byte(client, BMA2X2_OFFSET_X_AXIS_REG,
						&data);
#endif

	return comres;
}


static int bma2x2_get_offset_x(struct i2c_client *client, unsigned char
						*offsetfilt)
{
	int comres = 0;
	unsigned char data;

#ifdef CONFIG_SENSORS_BMI058
	comres = bma2x2_smbus_read_byte(client, BMI058_OFFSET_X_AXIS_REG,
							&data);
#else
	comres = bma2x2_smbus_read_byte(client, BMA2X2_OFFSET_X_AXIS_REG,
							&data);
#endif
	*offsetfilt = data;

	return comres;
}

static int bma2x2_set_offset_y(struct i2c_client *client, unsigned char
						offsetfilt)
{
	int comres = 0;
	unsigned char data;

	data =  offsetfilt;

#ifdef CONFIG_SENSORS_BMI058
	comres = bma2x2_smbus_write_byte(client, BMI058_OFFSET_Y_AXIS_REG,
							&data);
#else
	comres = bma2x2_smbus_write_byte(client, BMA2X2_OFFSET_Y_AXIS_REG,
							&data);
#endif
	return comres;
}

static int bma2x2_get_offset_y(struct i2c_client *client, unsigned char
						*offsetfilt)
{
	int comres = 0;
	unsigned char data;

#ifdef CONFIG_SENSORS_BMI058
	comres = bma2x2_smbus_read_byte(client, BMI058_OFFSET_Y_AXIS_REG,
							&data);
#else
	comres = bma2x2_smbus_read_byte(client, BMA2X2_OFFSET_Y_AXIS_REG,
							&data);
#endif
	*offsetfilt = data;

	return comres;
}

static int bma2x2_set_offset_z(struct i2c_client *client, unsigned char
						offsetfilt)
{
	int comres = 0;
	unsigned char data;

	data =  offsetfilt;
	comres = bma2x2_smbus_write_byte(client, BMA2X2_OFFSET_Z_AXIS_REG,
						&data);

	return comres;
}

static int bma2x2_get_offset_z(struct i2c_client *client, unsigned char
						*offsetfilt)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_OFFSET_Z_AXIS_REG,
						&data);
	*offsetfilt = data;

	return comres;
}


static int bma2x2_set_selftest_st(struct i2c_client *client, unsigned char
		selftest)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_EN_SELF_TEST__REG,
			&data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_SELF_TEST, selftest);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_EN_SELF_TEST__REG,
			&data);

	return comres;
}

static int bma2x2_set_selftest_stn(struct i2c_client *client, unsigned char stn)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_NEG_SELF_TEST__REG,
			&data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_NEG_SELF_TEST, stn);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_NEG_SELF_TEST__REG,
			&data);

	return comres;
}

static int bma2x2_set_selftest_amp(struct i2c_client *client, unsigned char amp)
{
	int comres = 0;
	unsigned char data;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_SELF_TEST_AMP__REG,
			&data);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_SELF_TEST_AMP, amp);
	comres = bma2x2_smbus_write_byte(client, BMA2X2_SELF_TEST_AMP__REG,
			&data);

	return comres;
}

static int bma2x2_read_accel_x(struct i2c_client *client,
				signed char sensor_type, short *a_x)
{
	int comres = 0;
	unsigned char data[2];

	switch (sensor_type) {
	case 0:
		comres = bma2x2_smbus_read_byte_block(client,
					BMA2X2_ACC_X12_LSB__REG, data, 2);
		*a_x = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_X12_LSB)|
			(BMA2X2_GET_BITSLICE(data[1],
				BMA2X2_ACC_X_MSB)<<(BMA2X2_ACC_X12_LSB__LEN));
		*a_x = *a_x << (sizeof(short)*8-(BMA2X2_ACC_X12_LSB__LEN
					+ BMA2X2_ACC_X_MSB__LEN));
		*a_x = *a_x >> (sizeof(short)*8-(BMA2X2_ACC_X12_LSB__LEN
					+ BMA2X2_ACC_X_MSB__LEN));
		break;
	case 1:
		comres = bma2x2_smbus_read_byte_block(client,
					BMA2X2_ACC_X10_LSB__REG, data, 2);
		*a_x = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_X10_LSB)|
			(BMA2X2_GET_BITSLICE(data[1],
				BMA2X2_ACC_X_MSB)<<(BMA2X2_ACC_X10_LSB__LEN));
		*a_x = *a_x << (sizeof(short)*8-(BMA2X2_ACC_X10_LSB__LEN
					+ BMA2X2_ACC_X_MSB__LEN));
		*a_x = *a_x >> (sizeof(short)*8-(BMA2X2_ACC_X10_LSB__LEN
					+ BMA2X2_ACC_X_MSB__LEN));
		break;
	case 2:
		comres = bma2x2_smbus_read_byte_block(client,
					BMA2X2_ACC_X8_LSB__REG, data, 2);
		*a_x = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_X8_LSB)|
			(BMA2X2_GET_BITSLICE(data[1],
				BMA2X2_ACC_X_MSB)<<(BMA2X2_ACC_X8_LSB__LEN));
		*a_x = *a_x << (sizeof(short)*8-(BMA2X2_ACC_X8_LSB__LEN
					+ BMA2X2_ACC_X_MSB__LEN));
		*a_x = *a_x >> (sizeof(short)*8-(BMA2X2_ACC_X8_LSB__LEN
					+ BMA2X2_ACC_X_MSB__LEN));
		break;
	case 3:
		comres = bma2x2_smbus_read_byte_block(client,
					BMA2X2_ACC_X14_LSB__REG, data, 2);
		*a_x = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_X14_LSB)|
			(BMA2X2_GET_BITSLICE(data[1],
				BMA2X2_ACC_X_MSB)<<(BMA2X2_ACC_X14_LSB__LEN));
		*a_x = *a_x << (sizeof(short)*8-(BMA2X2_ACC_X14_LSB__LEN
					+ BMA2X2_ACC_X_MSB__LEN));
		*a_x = *a_x >> (sizeof(short)*8-(BMA2X2_ACC_X14_LSB__LEN
					+ BMA2X2_ACC_X_MSB__LEN));
		break;
	default:
		break;
	}

	return comres;
}

static int bma2x2_soft_reset(struct i2c_client *client)
{
	int comres = 0;
	unsigned char data = BMA2X2_EN_SOFT_RESET_VALUE;

	comres = bma2x2_smbus_write_byte(client, BMA2X2_EN_SOFT_RESET__REG,
					&data);

	return comres;
}

static int bma2x2_read_accel_y(struct i2c_client *client,
				signed char sensor_type, short *a_y)
{
	int comres = 0;
	unsigned char data[2];

	switch (sensor_type) {
	case 0:
		comres = bma2x2_smbus_read_byte_block(client,
				BMA2X2_ACC_Y12_LSB__REG, data, 2);
		*a_y = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_Y12_LSB)|
			(BMA2X2_GET_BITSLICE(data[1],
				BMA2X2_ACC_Y_MSB)<<(BMA2X2_ACC_Y12_LSB__LEN));
		*a_y = *a_y << (sizeof(short)*8-(BMA2X2_ACC_Y12_LSB__LEN
						+ BMA2X2_ACC_Y_MSB__LEN));
		*a_y = *a_y >> (sizeof(short)*8-(BMA2X2_ACC_Y12_LSB__LEN
						+ BMA2X2_ACC_Y_MSB__LEN));
		break;
	case 1:
		comres = bma2x2_smbus_read_byte_block(client,
				BMA2X2_ACC_Y10_LSB__REG, data, 2);
		*a_y = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_Y10_LSB)|
			(BMA2X2_GET_BITSLICE(data[1],
				BMA2X2_ACC_Y_MSB)<<(BMA2X2_ACC_Y10_LSB__LEN));
		*a_y = *a_y << (sizeof(short)*8-(BMA2X2_ACC_Y10_LSB__LEN
						+ BMA2X2_ACC_Y_MSB__LEN));
		*a_y = *a_y >> (sizeof(short)*8-(BMA2X2_ACC_Y10_LSB__LEN
						+ BMA2X2_ACC_Y_MSB__LEN));
		break;
	case 2:
		comres = bma2x2_smbus_read_byte_block(client,
				BMA2X2_ACC_Y8_LSB__REG, data, 2);
		*a_y = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_Y8_LSB)|
				(BMA2X2_GET_BITSLICE(data[1],
				BMA2X2_ACC_Y_MSB)<<(BMA2X2_ACC_Y8_LSB__LEN));
		*a_y = *a_y << (sizeof(short)*8-(BMA2X2_ACC_Y8_LSB__LEN
						+ BMA2X2_ACC_Y_MSB__LEN));
		*a_y = *a_y >> (sizeof(short)*8-(BMA2X2_ACC_Y8_LSB__LEN
						+ BMA2X2_ACC_Y_MSB__LEN));
		break;
	case 3:
		comres = bma2x2_smbus_read_byte_block(client,
				BMA2X2_ACC_Y14_LSB__REG, data, 2);
		*a_y = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_Y14_LSB)|
			(BMA2X2_GET_BITSLICE(data[1],
				BMA2X2_ACC_Y_MSB)<<(BMA2X2_ACC_Y14_LSB__LEN));
		*a_y = *a_y << (sizeof(short)*8-(BMA2X2_ACC_Y14_LSB__LEN
						+ BMA2X2_ACC_Y_MSB__LEN));
		*a_y = *a_y >> (sizeof(short)*8-(BMA2X2_ACC_Y14_LSB__LEN
						+ BMA2X2_ACC_Y_MSB__LEN));
		break;
	default:
		break;
	}

	return comres;
}

static int bma2x2_read_accel_z(struct i2c_client *client,
				signed char sensor_type, short *a_z)
{
	int comres = 0;
	unsigned char data[2];

	switch (sensor_type) {
	case 0:
		comres = bma2x2_smbus_read_byte_block(client,
				BMA2X2_ACC_Z12_LSB__REG, data, 2);
		*a_z = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_Z12_LSB)|
			(BMA2X2_GET_BITSLICE(data[1],
				BMA2X2_ACC_Z_MSB)<<(BMA2X2_ACC_Z12_LSB__LEN));
		*a_z = *a_z << (sizeof(short)*8-(BMA2X2_ACC_Z12_LSB__LEN
						+ BMA2X2_ACC_Z_MSB__LEN));
		*a_z = *a_z >> (sizeof(short)*8-(BMA2X2_ACC_Z12_LSB__LEN
						+ BMA2X2_ACC_Z_MSB__LEN));
		break;
	case 1:
		comres = bma2x2_smbus_read_byte_block(client,
				BMA2X2_ACC_Z10_LSB__REG, data, 2);
		*a_z = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_Z10_LSB)|
			(BMA2X2_GET_BITSLICE(data[1],
				BMA2X2_ACC_Z_MSB)<<(BMA2X2_ACC_Z10_LSB__LEN));
		*a_z = *a_z << (sizeof(short)*8-(BMA2X2_ACC_Z10_LSB__LEN
						+ BMA2X2_ACC_Z_MSB__LEN));
		*a_z = *a_z >> (sizeof(short)*8-(BMA2X2_ACC_Z10_LSB__LEN
						+ BMA2X2_ACC_Z_MSB__LEN));
		break;
	case 2:
		comres = bma2x2_smbus_read_byte_block(client,
				BMA2X2_ACC_Z8_LSB__REG, data, 2);
		*a_z = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_Z8_LSB)|
			(BMA2X2_GET_BITSLICE(data[1],
				BMA2X2_ACC_Z_MSB)<<(BMA2X2_ACC_Z8_LSB__LEN));
		*a_z = *a_z << (sizeof(short)*8-(BMA2X2_ACC_Z8_LSB__LEN
						+ BMA2X2_ACC_Z_MSB__LEN));
		*a_z = *a_z >> (sizeof(short)*8-(BMA2X2_ACC_Z8_LSB__LEN
						+ BMA2X2_ACC_Z_MSB__LEN));
		break;
	case 3:
		comres = bma2x2_smbus_read_byte_block(client,
				BMA2X2_ACC_Z14_LSB__REG, data, 2);
		*a_z = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_Z14_LSB)|
				(BMA2X2_GET_BITSLICE(data[1],
				BMA2X2_ACC_Z_MSB)<<(BMA2X2_ACC_Z14_LSB__LEN));
		*a_z = *a_z << (sizeof(short)*8-(BMA2X2_ACC_Z14_LSB__LEN
						+ BMA2X2_ACC_Z_MSB__LEN));
		*a_z = *a_z >> (sizeof(short)*8-(BMA2X2_ACC_Z14_LSB__LEN
						+ BMA2X2_ACC_Z_MSB__LEN));
		break;
	default:
		break;
	}

	return comres;
}


static int bma2x2_read_temperature(struct i2c_client *client,
					signed char *temperature)
{
	unsigned char data;
	int comres = 0;

	comres = bma2x2_smbus_read_byte(client, BMA2X2_TEMPERATURE_REG, &data);
	*temperature = (signed char)data;

	return comres;
}

static ssize_t bma2x2_enable_int_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int type, value;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);
#ifdef CONFIG_SENSORS_BMI058
	int i;
#endif

	if (sscanf(buf, "%3d %3d", &type, &value) != 2)
		return -EINVAL;

#ifdef CONFIG_SENSORS_BMI058
	for (i = 0; i < sizeof(int_map) / sizeof(struct interrupt_map_t); i++) {
		if (int_map[i].x == type) {
			type = int_map[i].y;
			break;
		}
		if (int_map[i].y == type) {
			type = int_map[i].x;
			break;
		}
	}
#endif

	if (bma2x2_set_Int_Enable(bma2x2->bma2x2_client, type, value) < 0)
		return -EINVAL;

	return count;
}

static int bma2x2_update_bandwidth(const struct bma2x2_data *bma2x2)
{
	int err = 0;
	int i;
	int delay = atomic_read(&bma2x2->delay);

	for (i = ARRAY_SIZE(bma2x2_delay2bw_table) - 1; i > 0; i--) {
		if (bma2x2_delay2bw_table[i].delay_ms <= delay)
			break;
	}

	err = bma2x2_set_bandwidth(bma2x2->bma2x2_client,
		bma2x2_delay2bw_table[i].bw_config);
	if (err)
		dev_err(&bma2x2->bma2x2_client->dev,
			"Update bandwidth not success,delay=%d err=%d\n",
			delay, err);

	dev_dbg(&bma2x2->bma2x2_client->dev,
		"Update bandwidth success,delay=%d config=%u\n",
		delay, bma2x2_delay2bw_table[i].bw_config);
	return err;
}

static int bma2x2_update_delay(struct bma2x2_data *bma2x2, unsigned int delay)
{
	int pre_enable = atomic_read(&bma2x2->enable);
	int err = 0;
	ktime_t ktime;
	int res = 0;

	atomic_set(&bma2x2->delay, delay);

	if (!pre_enable)
		return 0;

	/*
	  * Flush fifo data as ODR is about to change.
	  * Data acquisition and fifo buffering is not disabled during this
	  * configuration changing process, timestamp of sensor
	  * event my not correct. Set sensor to standby state during
	  * configuration update if accurate timestamp is required.
	  */
	if (atomic_read(&bma2x2->fifo_enabled)) {
		bma2x2_flush_fifo(bma2x2);
		err = bma2x2_update_bandwidth(bma2x2);
	} else if (bma2x2_use_data_polling(bma2x2)) {
		if (!bma2x2->pdata->use_hrtimer) {
			if (cancel_delayed_work_sync(&bma2x2->work) >= 0)
			{
				res = queue_delayed_work(bma2x2->data_wq,
						&bma2x2->work,
						msecs_to_jiffies(delay));
				if(res == 0) {
					queue_delayed_work(bma2x2->data_wq,
							&bma2x2->work,
							msecs_to_jiffies(delay));
				}
			}
		} else {
			hrtimer_cancel(&bma2x2->accel_timer);
			ktime = ktime_set(0,
				atomic_read(&bma2x2->delay) * NSEC_PER_MSEC);
			hrtimer_start(&bma2x2->accel_timer,
					ktime,
					HRTIMER_MODE_REL);
		}
	} else if (bma2x2->pdata->int_en && BMA2x2_IS_NEWDATA_INT_ENABLED()) {
		err = bma2x2_update_bandwidth(bma2x2);
	} else {
		dev_err(&bma2x2->bma2x2_client->dev,
			"Incorrect state! enable=%d, fifoEnable=%d, delay=%d, latency=%d\n",
			pre_enable, atomic_read(&bma2x2->fifo_enabled),
			delay, bma2x2->max_latency_ms);
	}
	return err;
}

#if defined(BMA2X2_ENABLE_INT1)
static int bma2x2_sel_int1_pad(const struct bma2x2_data *data)
{
	struct i2c_client *client = data->bma2x2_client;
	int err = 0;

	/* maps interrupt to INT1 pin */
	err |= bma2x2_set_int1_pad_sel(client, PAD_LOWG);
	err |= bma2x2_set_int1_pad_sel(client, PAD_HIGHG);
	err |= bma2x2_set_int1_pad_sel(client, PAD_SLOP);
	err |= bma2x2_set_int1_pad_sel(client, PAD_DOUBLE_TAP);
	err |= bma2x2_set_int1_pad_sel(client, PAD_SINGLE_TAP);
	err |= bma2x2_set_int1_pad_sel(client, PAD_ORIENT);
	err |= bma2x2_set_int1_pad_sel(client, PAD_FLAT);
	err |= bma2x2_set_int1_pad_sel(client, PAD_SLOW_NO_MOTION);
	err |= bma2x2_set_newdata(client, BMA2X2_INT1_NDATA, 1);
	err |= bma2x2_set_newdata(client, BMA2X2_INT2_NDATA, 0);
	err |= bma2x2_set_fwm_int_pad_sel(client, BMA2X2_INT1_FWM, 1);
	err |= bma2x2_set_fwm_int_pad_sel(client, BMA2X2_INT2_FWM, 0);
	err |= bma2x2_set_ffull_int_pad_sel(client, BMA2X2_INT1_FFULL, 1);
	err |= bma2x2_set_ffull_int_pad_sel(client, BMA2X2_INT2_FFULL, 0);

	if (err) {
		dev_err(&client->dev, "select pad int1 error, ret=%d\n", err);
		err = -EIO;
	}
	return err;
}
#else
static int bma2x2_sel_int1_pad(const struct bma2x2_data *data)
{
	return -EPERM;
}
#endif /* BMA2X2_ENABLE_INT1 */

#if defined(BMA2X2_ENABLE_INT2)
static int bma2x2_sel_int2_pad(const struct bma2x2_data *data)
{
	struct i2c_client *client = data->bma2x2_client;
	int err = 0;

	/* maps interrupt to INT2 pin */
	err |= bma2x2_set_int2_pad_sel(client, PAD_LOWG);
	err |= bma2x2_set_int2_pad_sel(client, PAD_HIGHG);
	err |= bma2x2_set_int2_pad_sel(client, PAD_SLOP);
	err |= bma2x2_set_int2_pad_sel(client, PAD_DOUBLE_TAP);
	err |= bma2x2_set_int2_pad_sel(client, PAD_SINGLE_TAP);
	err |= bma2x2_set_int2_pad_sel(client, PAD_ORIENT);
	err |= bma2x2_set_int2_pad_sel(client, PAD_FLAT);
	err |= bma2x2_set_int2_pad_sel(client, PAD_SLOW_NO_MOTION);
	err |= bma2x2_set_newdata(client, BMA2X2_INT1_NDATA, 0);
	err |= bma2x2_set_newdata(client, BMA2X2_INT2_NDATA, 1);
	err |= bma2x2_set_fwm_int_pad_sel(client, BMA2X2_INT1_FWM, 0);
	err |= bma2x2_set_fwm_int_pad_sel(client, BMA2X2_INT2_FWM, 1);
	err |= bma2x2_set_ffull_int_pad_sel(client, BMA2X2_INT1_FFULL, 0);
	err |= bma2x2_set_ffull_int_pad_sel(client, BMA2X2_INT2_FFULL, 1);

	if (err) {
		dev_err(&client->dev, "select pad int1 error, ret=%d\n", err);
		err = -EIO;
	}
	return err;
}
#else
static int bma2x2_sel_int2_pad(const struct bma2x2_data *data)
{
	return -EPERM;
}
#endif /* BMA2X2_ENABLE_INT2 */

static ssize_t bma2x2_int_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_Int_Mode(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);
}

static ssize_t bma2x2_int_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_Int_Mode(bma2x2->bma2x2_client, (unsigned char)data) < 0)
		return -EINVAL;

	return count;
}
static ssize_t bma2x2_slope_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_slope_duration(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_slope_duration_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_slope_duration(bma2x2->bma2x2_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_slope_no_mot_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_slope_no_mot_duration(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_slope_no_mot_duration_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_slope_no_mot_duration(bma2x2->bma2x2_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}


static ssize_t bma2x2_slope_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_slope_threshold(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_slope_threshold_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma2x2_set_slope_threshold(bma2x2->bma2x2_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_slope_no_mot_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_slope_no_mot_threshold(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_slope_no_mot_threshold_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma2x2_set_slope_no_mot_threshold(bma2x2->bma2x2_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_high_g_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_high_g_duration(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_high_g_duration_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_high_g_duration(bma2x2->bma2x2_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_high_g_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_high_g_threshold(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_high_g_threshold_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma2x2_set_high_g_threshold(bma2x2->bma2x2_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_low_g_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_low_g_duration(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_low_g_duration_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_low_g_duration(bma2x2->bma2x2_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_low_g_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_low_g_threshold(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_low_g_threshold_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma2x2_set_low_g_threshold(bma2x2->bma2x2_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}
static ssize_t bma2x2_tap_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_tap_threshold(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_tap_threshold_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma2x2_set_tap_threshold(bma2x2->bma2x2_client, (unsigned char)data)
			< 0)
		return -EINVAL;

	return count;
}
static ssize_t bma2x2_tap_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_tap_duration(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_tap_duration_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_tap_duration(bma2x2->bma2x2_client, (unsigned char)data)
			< 0)
		return -EINVAL;

	return count;
}
static ssize_t bma2x2_tap_quiet_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_tap_quiet(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_tap_quiet_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_tap_quiet(bma2x2->bma2x2_client, (unsigned char)data) <
			0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_tap_shock_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_tap_shock(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_tap_shock_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_tap_shock(bma2x2->bma2x2_client, (unsigned char)data) <
			0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_tap_samp_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_tap_samp(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_tap_samp_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_tap_samp(bma2x2->bma2x2_client, (unsigned char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_orient_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_orient_mode(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_orient_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_orient_mode(bma2x2->bma2x2_client, (unsigned char)data) <
			0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_orient_blocking_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_orient_blocking(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_orient_blocking_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_orient_blocking(bma2x2->bma2x2_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}
static ssize_t bma2x2_orient_hyst_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_orient_hyst(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_orient_hyst_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_orient_hyst(bma2x2->bma2x2_client, (unsigned char)data) <
			0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_orient_theta_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_theta_blocking(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_orient_theta_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_theta_blocking(bma2x2->bma2x2_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_flat_theta_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_theta_flat(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_flat_theta_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_theta_flat(bma2x2->bma2x2_client, (unsigned char)data) <
			0)
		return -EINVAL;

	return count;
}
static ssize_t bma2x2_flat_hold_time_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_flat_hold_time(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}
static ssize_t bma2x2_selftest_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{


	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		atomic_read(&bma2x2->selftest_result));

}

static ssize_t bma2x2_softreset_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_soft_reset(bma2x2->bma2x2_client) < 0)
		return -EINVAL;

	return count;
}
static ssize_t bma2x2_selftest_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{

	unsigned long data;
	unsigned char clear_value = 0;
	int error;
	short value1 = 0;
	short value2 = 0;
	short diff = 0;
	unsigned long result = 0;
	unsigned long test_result_branch = 0;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	bma2x2_soft_reset(bma2x2->bma2x2_client);
	RESET_DELAY();

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (data != 1)
		return -EINVAL;

	bma2x2_write_reg(bma2x2->bma2x2_client, 0x32, &clear_value);

	if ((bma2x2->sensor_type == BMA280_TYPE) ||
		(bma2x2->sensor_type == BMA255_TYPE)) {
#ifdef CONFIG_SENSORS_BMI058
		/*set self test amp */
		if (bma2x2_set_selftest_amp(bma2x2->bma2x2_client, 1) < 0)
			return -EINVAL;
		/* set to 8 G range */
		if (bma2x2_set_range(bma2x2->bma2x2_client,
							BMA2X2_RANGE_8G) < 0)
			return -EINVAL;
#else
		/* set to 4 G range */
		if (bma2x2_set_range(bma2x2->bma2x2_client,
							BMA2X2_RANGE_4G) < 0)
			return -EINVAL;
#endif
	}

	if ((bma2x2->sensor_type == BMA250E_TYPE) ||
			(bma2x2->sensor_type == BMA222E_TYPE)) {
		/* set to 8 G range */
		if (bma2x2_set_range(bma2x2->bma2x2_client, 8) < 0)
			return -EINVAL;
		if (bma2x2_set_selftest_amp(bma2x2->bma2x2_client, 1) < 0)
			return -EINVAL;
	}

	/* 1 for x-axis(but BMI058 is 1 for y-axis )*/
	bma2x2_set_selftest_st(bma2x2->bma2x2_client, 1);
	bma2x2_set_selftest_stn(bma2x2->bma2x2_client, 0);
	SELF_TEST_DELAY();
	bma2x2_read_accel_x(bma2x2->bma2x2_client,
					bma2x2->sensor_type, &value1);
	bma2x2_set_selftest_stn(bma2x2->bma2x2_client, 1);
	SELF_TEST_DELAY();
	bma2x2_read_accel_x(bma2x2->bma2x2_client,
					bma2x2->sensor_type, &value2);
	diff = value1-value2;

#ifdef CONFIG_SENSORS_BMI058
	dev_dbg(dev, "diff y is %d,value1 is %d, value2 is %d\n", diff,
				value1, value2);
	test_result_branch = 2;
#else
	dev_dbg(dev, "diff x is %d,value1 is %d, value2 is %d\n", diff,
				value1, value2);
	test_result_branch = 1;
#endif

	if (bma2x2->sensor_type == BMA280_TYPE) {
#ifdef CONFIG_SENSORS_BMI058
		if (abs(diff) < 819)
			result |= test_result_branch;
#else
		if (abs(diff) < 1638)
			result |= test_result_branch;
#endif
	}
	if (bma2x2->sensor_type == BMA255_TYPE) {
		if (abs(diff) < 409)
			result |= 1;
	}
	if (bma2x2->sensor_type == BMA250E_TYPE) {
		if (abs(diff) < 51)
			result |= 1;
	}
	if (bma2x2->sensor_type == BMA222E_TYPE) {
		if (abs(diff) < 12)
			result |= 1;
	}

	/* 2 for y-axis but BMI058 is 1*/
	bma2x2_set_selftest_st(bma2x2->bma2x2_client, 2);
	bma2x2_set_selftest_stn(bma2x2->bma2x2_client, 0);
	SELF_TEST_DELAY();
	bma2x2_read_accel_y(bma2x2->bma2x2_client,
					bma2x2->sensor_type, &value1);
	bma2x2_set_selftest_stn(bma2x2->bma2x2_client, 1);
	SELF_TEST_DELAY();
	bma2x2_read_accel_y(bma2x2->bma2x2_client,
					bma2x2->sensor_type, &value2);
	diff = value1-value2;

#ifdef CONFIG_SENSORS_BMI058
	dev_dbg(dev, "diff x is %d,value1 is %d, value2 is %d\n", diff,
				value1, value2);
	test_result_branch = 1;
#else
	dev_dbg(dev, "diff y is %d,value1 is %d, value2 is %d\n", diff,
				value1, value2);
	test_result_branch = 2;
#endif

	if (bma2x2->sensor_type == BMA280_TYPE) {
#ifdef CONFIG_SENSORS_BMI058
		if (abs(diff) < 819)
			result |= test_result_branch;
#else
		if (abs(diff) < 1638)
			result |= test_result_branch;
#endif
	}
	if (bma2x2->sensor_type == BMA255_TYPE) {
		if (abs(diff) < 409)
			result |= test_result_branch;
	}
	if (bma2x2->sensor_type == BMA250E_TYPE) {
		if (abs(diff) < 51)
			result |= test_result_branch;
	}
	if (bma2x2->sensor_type == BMA222E_TYPE) {
		if (abs(diff) < 12)
			result |= test_result_branch;
	}


	bma2x2_set_selftest_st(bma2x2->bma2x2_client, 3); /* 3 for z-axis*/
	bma2x2_set_selftest_stn(bma2x2->bma2x2_client, 0);
	SELF_TEST_DELAY();
	bma2x2_read_accel_z(bma2x2->bma2x2_client,
					bma2x2->sensor_type, &value1);
	bma2x2_set_selftest_stn(bma2x2->bma2x2_client, 1);
	SELF_TEST_DELAY();
	bma2x2_read_accel_z(bma2x2->bma2x2_client,
					bma2x2->sensor_type, &value2);
	diff = value1-value2;

	dev_dbg(dev, "diff z is %d,value1 is %d, value2 is %d\n", diff,
			value1, value2);

	if (bma2x2->sensor_type == BMA280_TYPE) {
#ifdef CONFIG_SENSORS_BMI058
			if (abs(diff) < 409)
				result |= 4;
#else
			if (abs(diff) < 819)
				result |= 4;
#endif
	}
	if (bma2x2->sensor_type == BMA255_TYPE) {
		if (abs(diff) < 204)
			result |= 4;
	}
	if (bma2x2->sensor_type == BMA250E_TYPE) {
		if (abs(diff) < 25)
			result |= 4;
	}
	if (bma2x2->sensor_type == BMA222E_TYPE) {
		if (abs(diff) < 6)
			result |= 4;
	}

	/* self test for bma254 */
	if ((bma2x2->sensor_type == BMA255_TYPE) && (result > 0)) {
		result = 0;
		bma2x2_soft_reset(bma2x2->bma2x2_client);
		RESET_DELAY();
		bma2x2_write_reg(bma2x2->bma2x2_client, 0x32, &clear_value);
		/* set to 8 G range */
		if (bma2x2_set_range(bma2x2->bma2x2_client, 8) < 0)
			return -EINVAL;
		if (bma2x2_set_selftest_amp(bma2x2->bma2x2_client, 1) < 0)
			return -EINVAL;

		bma2x2_set_selftest_st(bma2x2->bma2x2_client, 1); /* 1
								for x-axis*/
		bma2x2_set_selftest_stn(bma2x2->bma2x2_client, 0); /*
							positive direction*/
		SELF_TEST_DELAY();
		bma2x2_read_accel_x(bma2x2->bma2x2_client,
						bma2x2->sensor_type, &value1);
		bma2x2_set_selftest_stn(bma2x2->bma2x2_client, 1); /*
							negative direction*/
		SELF_TEST_DELAY();
		bma2x2_read_accel_x(bma2x2->bma2x2_client,
						bma2x2->sensor_type, &value2);
		diff = value1-value2;

		dev_dbg(dev, "diff x is %d,value1 is %d, value2 is %d\n",
						diff, value1, value2);
		if (abs(diff) < 204)
			result |= 1;

		bma2x2_set_selftest_st(bma2x2->bma2x2_client, 2); /* 2
								for y-axis*/
		bma2x2_set_selftest_stn(bma2x2->bma2x2_client, 0); /*
							positive direction*/
		SELF_TEST_DELAY();
		bma2x2_read_accel_y(bma2x2->bma2x2_client,
						bma2x2->sensor_type, &value1);
		bma2x2_set_selftest_stn(bma2x2->bma2x2_client, 1); /*
							negative direction*/
		SELF_TEST_DELAY();
		bma2x2_read_accel_y(bma2x2->bma2x2_client,
						bma2x2->sensor_type, &value2);
		diff = value1-value2;
		dev_dbg(dev, "diff y is %d,value1 is %d, value2 is %d\n",
						diff, value1, value2);

		if (abs(diff) < 204)
			result |= 2;

		bma2x2_set_selftest_st(bma2x2->bma2x2_client, 3); /* 3
								for z-axis*/
		bma2x2_set_selftest_stn(bma2x2->bma2x2_client, 0); /*
							positive direction*/
		SELF_TEST_DELAY();
		bma2x2_read_accel_z(bma2x2->bma2x2_client,
						bma2x2->sensor_type, &value1);
		bma2x2_set_selftest_stn(bma2x2->bma2x2_client, 1); /*
							negative direction*/
		SELF_TEST_DELAY();
		bma2x2_read_accel_z(bma2x2->bma2x2_client,
						bma2x2->sensor_type, &value2);
		diff = value1-value2;

		dev_dbg(dev, "diff z is %d,value1 is %d, value2 is %d\n",
						diff, value1, value2);
		if (abs(diff) < 102)
			result |= 4;
	}

	atomic_set(&bma2x2->selftest_result, (unsigned int)result);

	bma2x2_soft_reset(bma2x2->bma2x2_client);
	RESET_DELAY();
	dev_dbg(dev, "self test finished\n");

	return count;
}



static ssize_t bma2x2_flat_hold_time_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_flat_hold_time(bma2x2->bma2x2_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static const int bma2x2_sensor_bitwidth[] = {
	12,  10,  8, 14
};

static int bma2x2_get_sensitivity(struct bma2x2_data *bma2x2, int range)
{

	switch (range) {
	case BMA2X2_RANGE_2G:
		bma2x2->sensitivity = bosch_sensor_range_map[0];
		break;
	case BMA2X2_RANGE_4G:
		bma2x2->sensitivity = bosch_sensor_range_map[1];
		break;
	case BMA2X2_RANGE_8G:
		bma2x2->sensitivity = bosch_sensor_range_map[2];
		break;
	case BMA2X2_RANGE_16G:
		bma2x2->sensitivity = bosch_sensor_range_map[3];
		break;
	default:
		bma2x2->sensitivity = bosch_sensor_range_map[0];
		break;
	}
	return 0;
}

static int bma2x2_read_accel_xyz(struct i2c_client *client,
		signed char sensor_type, struct bma2x2acc *acc)
{
	int comres = 0;
	unsigned char data[6];
	struct bma2x2_data *client_data = i2c_get_clientdata(client);
	int bitwidth;

	comres = bma2x2_smbus_read_byte_block(client,
				BMA2X2_ACC_X12_LSB__REG, data, 6);
	if (sensor_type >= 4)
		return -EINVAL;

	acc->x = (data[1]<<8)|data[0];
	acc->y = (data[3]<<8)|data[2];
	acc->z = (data[5]<<8)|data[4];

	bitwidth = bma2x2_sensor_bitwidth[sensor_type];
	BMA2X2_SHIFT_BITWIDTH(acc->x, bitwidth);
	BMA2X2_SHIFT_BITWIDTH(acc->y, bitwidth);
	BMA2X2_SHIFT_BITWIDTH(acc->z, bitwidth);

	bma2x2_remap_sensor_data(acc, client_data);
	return comres;
}

static int bma2x2_read_accel_xyz_calibrate(struct i2c_client *client,
		signed char sensor_type, struct bma2x2acc *acc)
{
	int comres = 0;
	unsigned char data[6];
	struct bma2x2_data *client_data = i2c_get_clientdata(client);
	int bitwidth;

	comres = bma2x2_smbus_read_byte_block(client,
				BMA2X2_ACC_X12_LSB__REG, data, 6);
	if (sensor_type >= 4)
		return -EINVAL;

	acc->x = (data[1]<<8)|data[0];
	acc->y = (data[3]<<8)|data[2];
	acc->z = (data[5]<<8)|data[4];

	bitwidth = bma2x2_sensor_bitwidth[sensor_type];
	BMA2X2_SHIFT_BITWIDTH(acc->x, bitwidth);
	BMA2X2_SHIFT_BITWIDTH(acc->y, bitwidth);
	BMA2X2_SHIFT_BITWIDTH(acc->z, bitwidth);

	bma2x2_remap_sensor_data(acc, client_data);
	return comres;
}



static void bma2x2_report_axis_data(struct bma2x2_data *bma2x2,
			struct bma2x2acc *value)
{
	ktime_t ts;
	int err;

	ts = ktime_get_boottime();
	err = bma2x2_read_accel_xyz(bma2x2->bma2x2_client,
			bma2x2->sensor_type, value);
	if (err < 0) {
		dev_err(&bma2x2->bma2x2_client->dev,
			"read accel data failed! err = %d\n", err);
		return;
	}
	input_report_abs(bma2x2->input, ABS_X,
			((int)value->x << bma2x2->sensitivity)-offset_x);
	input_report_abs(bma2x2->input, ABS_Y,
			((int)value->y << bma2x2->sensitivity)-offset_y);
	input_report_abs(bma2x2->input, ABS_Z,
			((int)value->z << bma2x2->sensitivity)-offset_z);
	input_event(bma2x2->input, EV_SYN, SYN_TIME_SEC,
			ktime_to_timespec(ts).tv_sec);
	input_event(bma2x2->input, EV_SYN, SYN_TIME_NSEC,
			ktime_to_timespec(ts).tv_nsec);
	input_sync(bma2x2->input);
}

static void bma2x2_work_func(struct work_struct *work)
{
	struct bma2x2_data *bma2x2 = container_of((struct delayed_work *)work,
			struct bma2x2_data, work);
	struct bma2x2acc value;
	unsigned long delay = msecs_to_jiffies(atomic_read(&bma2x2->delay));
	int res = 0;

	bma2x2_report_axis_data(bma2x2, &value);
	mutex_lock(&bma2x2->value_mutex);
	bma2x2->value = value;
	mutex_unlock(&bma2x2->value_mutex);
	res = queue_delayed_work(bma2x2->data_wq, &bma2x2->work, delay);
	if(res == 0) {
		queue_delayed_work(bma2x2->data_wq, &bma2x2->work, delay);
	}
}

static enum hrtimer_restart accel_timer_handle(struct hrtimer *hrtimer)
{
	struct bma2x2_data *bma2x2;
	ktime_t ktime;

	bma2x2 = container_of(hrtimer, struct bma2x2_data, accel_timer);
	ktime = ktime_set(0, atomic_read(&bma2x2->delay) * NSEC_PER_MSEC);
	hrtimer_forward_now(&bma2x2->accel_timer, ktime);
	bma2x2->accel_wkp_flag = 1;
	wake_up_interruptible(&bma2x2->accel_wq);
	return HRTIMER_RESTART;
}

static int accel_poll_thread(void *data)
{
	struct bma2x2_data *bma2x2 = data;
	struct bma2x2acc value;

	while (1) {
		wait_event_interruptible(bma2x2->accel_wq,
			((bma2x2->accel_wkp_flag != 0) ||
				kthread_should_stop()));
		bma2x2->accel_wkp_flag = 0;
		if (kthread_should_stop())
			break;

		mutex_lock(&bma2x2->op_lock);
		if (bma2x2->accel_delay_change) {
			if (atomic_read(&bma2x2->delay) <= POLL_MS_100HZ)
				set_wake_up_idle(true);
			else
				set_wake_up_idle(false);
			bma2x2->accel_delay_change = false;
		}
		mutex_unlock(&bma2x2->op_lock);

		bma2x2_report_axis_data(bma2x2, &value);
		mutex_lock(&bma2x2->value_mutex);
		bma2x2->value = value;
		mutex_unlock(&bma2x2->value_mutex);
	}

	return 0;
}

static ssize_t bma2x2_register_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int address, value;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (sscanf(buf, "%3d %3d", &address, &value) != 2)
		return -EINVAL;

	if (bma2x2_write_reg(bma2x2->bma2x2_client, (unsigned char)address,
				(unsigned char *)&value) < 0)
		return -EINVAL;
	return count;
}
static ssize_t bma2x2_register_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	size_t count = 0;
	u8 reg[0x40];
	int i;

	for (i = 0; i < 0x40; i++) {
		bma2x2_smbus_read_byte(bma2x2->bma2x2_client, i, reg+i);

		count += snprintf(&buf[count], PAGE_SIZE,
			"0x%x: 0x%x\n", i, reg[i]);
	}
	return count;


}

static ssize_t bma2x2_range_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_range(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);
}

static ssize_t bma2x2_range_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma2x2_set_range(bma2x2->bma2x2_client, (unsigned char) data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_bandwidth_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_bandwidth(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_bandwidth_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2->sensor_type == BMA280_TYPE)
		if ((unsigned char) data > 14)
			return -EINVAL;

	if (bma2x2_set_bandwidth(bma2x2->bma2x2_client,
				(unsigned char) data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_mode(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d %d\n", data, bma2x2->ref_count);
}

static ssize_t bma2x2_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma2x2_set_mode(bma2x2->bma2x2_client, (unsigned char) data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_value_cache_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma2x2_data *bma2x2 = input_get_drvdata(input);
	struct bma2x2acc acc_value;

	mutex_lock(&bma2x2->value_mutex);
	acc_value = bma2x2->value;
	mutex_unlock(&bma2x2->value_mutex);

	return snprintf(buf, PAGE_SIZE, "%d %d %d\n", acc_value.x, acc_value.y,
			acc_value.z);
}

static ssize_t bma2x2_value_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma2x2_data *bma2x2 = input_get_drvdata(input);
	struct bma2x2acc acc_value;

	bma2x2_read_accel_xyz(bma2x2->bma2x2_client, bma2x2->sensor_type,
								&acc_value);

	return snprintf(buf, PAGE_SIZE, "%d %d %d\n", acc_value.x, acc_value.y,
			acc_value.z);
}

static ssize_t bma2x2_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&bma2x2->delay));

}

static ssize_t bma2x2_chip_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	return snprintf(buf, PAGE_SIZE, "%d\n", bma2x2->chip_id);

}


static ssize_t bma2x2_place_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);
	int place = BOSCH_SENSOR_PLACE_UNKNOWN;

	place = bma2x2->pdata->place;

	return snprintf(buf, PAGE_SIZE, "%d\n", place);
}


static ssize_t bma2x2_delay_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	data = clamp_val(data, POLL_INTERVAL_MIN_MS, POLL_INTERVAL_MAX_MS);

	error = bma2x2_update_delay(bma2x2, (unsigned int)data);
	if (error)
		return -EBUSY;
	else
		return count;
}


static ssize_t bma2x2_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&bma2x2->enable));

}

static int bma2x2_config_interrupt(struct bma2x2_data *data, int enable)
{
	struct i2c_client *client = data->bma2x2_client;
	int err = 0;
	bool act_high;

	if (!enable)
		/* No need reset these interrupt configurations */
		goto exit;

	if ((data->int_flag & IRQF_TRIGGER_RISING) ||
			(data->int_flag & IRQF_TRIGGER_HIGH))
		act_high = true;
	else
		act_high = false;

	if (data->pdata->use_int2) {
		err = bma2x2_sel_int2_pad(data);
		if (err) {
			dev_err(&client->dev,
				"Failed to select int2 pad, err=%d\n",
				err);
			goto exit;
		}

		err = bma2x2_set_int2_active_lvl(client, act_high);
		if (err) {
			dev_err(&client->dev,
				"Failed to select int2 level, err=%d\n",
				err);
			goto exit;
		}
	} else {
		err = bma2x2_sel_int1_pad(data);
		if (err) {
			dev_err(&client->dev,
				"Failed to select int1 pad, err=%d\n",
				err);
			goto exit;
		}
		err = bma2x2_set_int1_active_lvl(client, act_high);
		if (err) {
			dev_err(&client->dev,
				"Failed to select int2 level, err=%d\n",
				err);
			goto exit;
		}
	}

	err = bma2x2_set_Int_Mode(client, BMA2X2_LATCH_DUR_NON_LATCH);
	if (err) {
		dev_err(&client->dev,
			"Failed to set interrupt latch, err=%d\n",
			err);
		goto exit;
	}

exit:
	return err;
}

static unsigned int bma2x2_bandwidth_to_interval(struct bma2x2_data *bma2x2)
{
	unsigned int i;

	for (i = ARRAY_SIZE(bma2x2_delay2bw_table) - 1; i > 0; i--) {
		if (bma2x2_delay2bw_table[i].bw_config == bma2x2->bandwidth)
			break;
	}

	return bma2x2_delay2bw_table[i].delay_ms;
}

static int bma2x2_set_fifo_enable(struct bma2x2_data *bma2x2, bool enable)
{
	struct i2c_client *client = bma2x2->bma2x2_client;
	unsigned int interval, wml;
	unsigned int latency = bma2x2->max_latency_ms;
	int delay = atomic_read(&bma2x2->delay);
	int fifo_en = atomic_read(&bma2x2->fifo_enabled);
	int err = 0;
	printk("set fifo enable>>>>>>: %d ",enable);
	dev_dbg(&client->dev,
			"bma2x2_set_fifo_enable latency=%d,delay=%d,enable=%d,fifo_en=%d\n",
			latency, delay, enable, fifo_en);
	if (enable && !fifo_en) {
		if ((latency == 0) || (latency < delay)) {
			dev_err(&client->dev,
				"Invalid parameter! latency=%d delay=%d\n",
				latency, delay);
			err = -EINVAL;
			goto exit;
		}
		if (IS_ERR_OR_NULL(bma2x2->fifo_buf)) {
			dev_err(&client->dev,
				"Not enough memory for sensor FIFO\n");
			err = -ENOMEM;
			goto exit;
		}
		err = bma2x2_update_bandwidth(bma2x2);
		if (err)
			goto print_error;

		interval = bma2x2_bandwidth_to_interval(bma2x2);
		wml = (unsigned int)(latency / interval);
		if (wml > MAX_FIFO_F_LEVEL)
			wml = MAX_FIFO_F_LEVEL;
		err = bma2x2_set_watermark_lvl(client, wml);
		if (err)
			goto print_error;

		bma2x2->fifo_datasel = BMA2X2_FIFO_DAT_SEL_XYZ;
		err = bma2x2_set_fifo_data_sel(bma2x2->bma2x2_client,
					(unsigned char)BMA2X2_FIFO_DAT_SEL_XYZ);
		if (err)
			goto print_error;

		err = bma2x2_set_watermark_int(client, true);
		if (err)
			goto print_error;

		err = bma2x2_set_fifo_full_int(client, true);
		if (err)
			goto print_error;

		err = bma2x2_set_fifo_mode(client, BMA2X2_FIFO_MODE_FIFO);
		if (err)
			goto print_error;
		bma2x2_set_fifo_start_time(bma2x2);
		atomic_set(&bma2x2->fifo_enabled, 1);
	} else if (!enable && fifo_en) {
		bma2x2_flush_fifo(bma2x2);
		bma2x2_set_watermark_int(client, false);
		bma2x2_set_fifo_full_int(client, false);
		err = bma2x2_set_fifo_mode(client, BMA2X2_FIFO_MODE_BYPASS);
		if (err)
			goto print_error;
		atomic_set(&bma2x2->fifo_enabled, 0);
	} else {
		dev_err(&client->dev,
			"FIFO state incorrect! enable=%d, fifo enable=%d\n",
			enable, fifo_en);
		err = -EINVAL;
		goto exit;
	}

print_error:
	if (err)
		dev_err(&client->dev,
			"Set fifo error! enable=%d latency=%d, err=%d\n",
			enable, latency, err);
exit:
	return err;
}

static int bma2x2_set_enable(struct device *dev, int enable)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);
	int pre_enable = atomic_read(&bma2x2->enable);
	int en_sig_motion = atomic_read(&bma2x2->en_sig_motion);
	int err = 0;
	ktime_t ktime;
	int delay_ms;
	int res = 0;

	dev_dbg(&client->dev,
		"set enable: en=%d, en_state=%d en_SMD=%d\n",
		enable, pre_enable, en_sig_motion);

	if (atomic_read(&bma2x2->cal_status)) {
		dev_err(dev, "can not enable or disable when calibration\n");
		return -EBUSY;
	}
	printk("enter enable >>>>>> %d\n",enable);
	mutex_lock(&bma2x2->enable_mutex);
	if (enable && !pre_enable) {
		if (!en_sig_motion) {
			if (bma2x2_power_ctl(bma2x2, true)) {
				dev_err(dev, "power failed\n");
				goto mutex_exit;
			}
			if (bma2x2_open_init(client, bma2x2) < 0) {
				dev_err(dev, "set init failed\n");
				goto mutex_exit;
			}
			bma2x2_set_mode(bma2x2->bma2x2_client,
					BMA2X2_MODE_NORMAL);
		}

		if ((bma2x2->pdata->int_en) &&
			(bma2x2->max_latency_ms > 0)) {
			err = bma2x2_set_fifo_enable(bma2x2, true);
			if (err)
				goto mutex_exit;
		} else if ((bma2x2->pdata->int_en) &&
			(BMA2x2_IS_NEWDATA_INT_ENABLED())) {
			if (bma2x2_set_Int_Enable(client, BMA2X2_DATA_EN, 1)) {
				dev_err(&client->dev,
					"enable interrupt failed\n");
				goto mutex_exit;
			}
		} else {
			if (!bma2x2->pdata->use_hrtimer) {
				delay_ms = atomic_read(&bma2x2->delay);
				res = queue_delayed_work(bma2x2->data_wq,
						&bma2x2->work,
						msecs_to_jiffies(delay_ms));
				if(res == 0) {
					queue_delayed_work(bma2x2->data_wq,
							&bma2x2->work,
							msecs_to_jiffies(delay_ms));
				}
			} else {
				ktime = ktime_set(0,
						atomic_read(&bma2x2->delay)
						* NSEC_PER_MSEC);
				hrtimer_start(&bma2x2->accel_timer,
						ktime, HRTIMER_MODE_REL);
			}
		}
		if (!en_sig_motion && bma2x2->pdata->int_en) {
			err = bma2x2_config_interrupt(bma2x2, true);
			if (err) {
				dev_err(&client->dev,
					"Config interrupt failed\n");
				goto mutex_exit;
			}
			bma2x2_pinctrl_state(bma2x2, true);
			enable_irq(bma2x2->IRQ);
		}
		atomic_set(&bma2x2->enable, 1);
	} else if (!enable && pre_enable) {
		if (!en_sig_motion && bma2x2->pdata->int_en) {
			if (bma2x2_store_state(client, bma2x2) < 0) {
				dev_err(dev, "set state failed\n");
				goto mutex_exit;
			}
			bma2x2_set_mode(bma2x2->bma2x2_client,
					BMA2X2_MODE_SUSPEND);
			disable_irq(bma2x2->IRQ);
			bma2x2_pinctrl_state(bma2x2, false);
			if (bma2x2_config_interrupt(bma2x2, false)) {
				dev_err(&client->dev,
					"Deconfig interrupt failed\n");
				goto mutex_exit;
			}
		}

		if (atomic_read(&bma2x2->fifo_enabled)) {
			err = bma2x2_set_fifo_enable(bma2x2, false);
			if (err)
				goto mutex_exit;
		} else if ((bma2x2->pdata->int_en) &&
			(BMA2x2_IS_NEWDATA_INT_ENABLED())) {
			if (bma2x2_set_Int_Enable(client, BMA2X2_DATA_EN, 0)) {
				dev_err(&client->dev,
					"disable interrupt failed\n");
				goto mutex_exit;
			}
		} else {
			if (!bma2x2->pdata->use_hrtimer)
				cancel_delayed_work_sync(&bma2x2->work);
			else
				hrtimer_cancel(&bma2x2->accel_timer);
		}

		atomic_set(&bma2x2->enable, 0);
		if (!en_sig_motion) {
			if (bma2x2_power_ctl(bma2x2, false)) {
				dev_err(dev, "power failed\n");
				goto mutex_exit;
			}
		}
	} else {
		dev_err(&client->dev,
			"Incorrect enable state! enable=%d, state=%d\n",
			enable, pre_enable);
	}
mutex_exit:
	mutex_unlock(&bma2x2->enable_mutex);
	if (!!enable != atomic_read(&bma2x2->enable)) {
		dev_err(&client->dev,
		"Set enable failed! en=%d, en_state=%d, use_int=%d\n",
		enable, atomic_read(&bma2x2->enable),
		bma2x2->pdata->int_en);
		err = -EBUSY;
	}
	return err;
}



static ssize_t bma2x2_enable_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	if ((data == 0) || (data == 1)) {
		error = bma2x2_set_enable(dev, data);
		if (error)
			return -EBUSY;
	}
	printk(">>>>>>test enable <<<<< = %ld \n", data);

	return count;
}

// add for calibration
static int byteToInt4(const char buf[],int offset)
{
	return ((buf[offset]&0xff) << 24) |
		   ((buf[offset + 1] & 0xff) << 16) |
		   ((buf[offset + 2] & 0xff) << 8) |
		   ((buf[offset + 3] & 0xff));
}

#if 1
static ssize_t bma2x2_calibration_xyz_store(struct device *dev,
		struct device_attribute *attr,const char *buf, size_t count)
{	
	offset_x = byteToInt4(buf,0);
	offset_y = byteToInt4(buf,4);
	offset_z = byteToInt4(buf,8);
	printk("linson x = %d,y = %d,z = %d\n", offset_x, offset_y, offset_z);
	
	return count;
}
#endif
static int bma2x2_calibration_xyz_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i,cali_fail_cnt=0;
	struct input_dev *input = to_input_dev(dev);
	struct bma2x2_data *bma2x2 = input_get_drvdata(input);
	
	//struct i2c_client *client = to_i2c_client(dev);
	//struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);
	//int pre_enable = atomic_read(&bma2x2->enable);
	
	struct bma2x2acc acc_value={0};
	struct bma2x2acc_int acc_value_sum={0};
	struct bma2x2acc offset={0};

	for(i=0;i<CALICOUNT;i++)
	{
retry:
		bma2x2_read_accel_xyz_calibrate(bma2x2->bma2x2_client, bma2x2->sensor_type,
										&acc_value);
		if( ABS(acc_value.x) > (GRAVITY_1G_VALUE/5) || ABS(acc_value.y)>(GRAVITY_1G_VALUE/5)
			|| ABS(ABS(acc_value.z) - GRAVITY_1G_VALUE) > (GRAVITY_1G_VALUE/4)){
                  	printk("data offset is bigger than the thershold,  x = %d,y = %d,z = %d \n ", acc_value.x, acc_value.y, acc_value.z);
			cali_fail_cnt++;
			if(cali_fail_cnt > 3)
			{
				return 0;
			}
			else
			{
				mdelay(30);
				goto retry;	
			}
		}
		else{
			//printk("linson x = %d,y = %d,z = %d\n", acc_value.x, acc_value.y, acc_value.z);
			acc_value_sum.x += acc_value.x;
			acc_value_sum.y += acc_value.y;
			acc_value_sum.z += acc_value.z;
		}
		mdelay(30);
	}
	offset.x=acc_value_sum.x/CALICOUNT-0;
	offset.y=acc_value_sum.y/CALICOUNT-0;
	offset.z=ABS(acc_value_sum.z/CALICOUNT)-GRAVITY_1G_VALUE;
	
	printk("cali_final_offset x = %d,y = %d,z = %d ", offset.x, offset.y, offset.z );
    offset_x=offset.x;
    offset_y=offset.y;
    offset_z=offset.z;

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d",offset_x,offset_y,offset_z);	
}


static int bma2x2_cdev_enable(struct sensors_classdev *sensors_cdev,
				unsigned int enable)
{
	struct bma2x2_data *data = container_of(sensors_cdev,
					struct bma2x2_data, cdev);

	bma2x2_set_enable(&data->bma2x2_client->dev, enable);
	return 0;
}

static int bma2x2_is_power_enabled(struct bma2x2_data *data)
{
	return atomic_read(&data->enable);
}

static int bma2x2_cdev_poll_delay(struct sensors_classdev *sensors_cdev,
				unsigned int delay_ms)
{
	struct bma2x2_data *data = container_of(sensors_cdev,
					struct bma2x2_data, cdev);
	int err;

	delay_ms = clamp_val(delay_ms,
		POLL_INTERVAL_MIN_MS, POLL_INTERVAL_MAX_MS);

	dev_dbg(&data->bma2x2_client->dev,
			"bma2x2_cdev_poll_delay delay_ms=%d\n",
			delay_ms);
	err = bma2x2_update_delay(data, delay_ms);
	if (err)
		return -EBUSY;
	else
		return 0;
}

static int bma2x2_cdev_set_latency(struct sensors_classdev *sensors_cdev,
					unsigned int max_latency_ms)
{
	struct bma2x2_data *bma2x2 = container_of(sensors_cdev,
					struct bma2x2_data, cdev);
	struct i2c_client *client = bma2x2->bma2x2_client;
	unsigned int delay = atomic_read(&bma2x2->delay);
	int err = 0;

	dev_dbg(&client->dev,
		"bma2x2_cdev_set_latency latency=%d\n",
			max_latency_ms);
	if ((max_latency_ms > 0) && (max_latency_ms < delay)) {
		dev_err(&client->dev,
			"Latency should not less than delay! latency=%d, delay=%d\n",
			max_latency_ms, delay);
		return -EINVAL;
	}

	bma2x2->max_latency_ms = max_latency_ms;
	if (!atomic_read(&bma2x2->enable))
		return 0;

	if (max_latency_ms > 0) {
		/* Data polling and batching should not work at same time */
		if (!bma2x2->pdata->use_hrtimer)
			cancel_delayed_work_sync(&bma2x2->work);
		else
			hrtimer_cancel(&bma2x2->accel_timer);

		err = bma2x2_set_fifo_enable(bma2x2, true);
	} else {
		err = bma2x2_set_fifo_enable(bma2x2, false);
		if (!err && bma2x2_use_data_polling(bma2x2))
			mod_delayed_work(bma2x2->data_wq, &bma2x2->work,
				msecs_to_jiffies(delay));
	}
	if (err)
		dev_err(&client->dev,
			"Set latency error! latency=%d, err=%d\n",
			max_latency_ms, err);
	return err;
}

static int bma2x2_cdev_flush(struct sensors_classdev *sensors_cdev)
{
	struct bma2x2_data *bma2x2 = container_of(sensors_cdev,
					struct bma2x2_data, cdev);

	if (atomic_read(&bma2x2->fifo_enabled))
		return bma2x2_flush_fifo(bma2x2);
	else
		return 0;
}

#ifdef CONFIG_SENSORS_BMI058
static int bma2x2_select_chanel(struct i2c_client *client)
{
	unsigned char data_ore[3] = { BOSCH_SENSOR_PLANE };
	signed char tmp;
	int error, i;
	int timeout;
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);
	unsigned char bmi058_channel_tb = {BMI058_OFFSET_TRIGGER_X,
			BMI058_OFFSET_TRIGGER_Y, BMI058_OFFSET_TRIGGER_Z};

	if (bma2x2->pdata->place > 3 && bma2x2->pdata->place < 8)
		data_ore[2] = BOSCH_SENSOR_DOWN;
	else if (bma2x2->pdata->place >= 0 && bma2x2->pdata->place < 4)
		data_ore[2] = BOSCH_SENSOR_UP;
	else {
		dev_err(&client->dev, "unknown sensor place\n");
		return -EINVAL;
	}
	if (bma2x2_set_mode(client, BMA2X2_MODE_NORMAL) < 0) {
		dev_err(&client->dev, "set calibrate mode error\n");
		return -EINVAL;
	}
	if (bma2x2_set_bandwidth(client, BMA2X2_BW_1000HZ) < 0) {
		dev_err(&client->dev, "set calibrate bandwidth error\n");
		return -EINVAL;
	}
	if (bma2x2_set_range(client, BMA2X2_RANGE_SET) < 0) {
		dev_err(&client->dev, "set calibrate range error\n");
		return -EINVAL;
	}
	for (i = 0; i < 3; i++) {
		if (bma2x2_set_offset_target(client, bmi058_channel_tb[i],
					(unsigned char)data_ore[i]) < 0) {
			dev_err(&client->dev,
					"set offset target error\n");
			return -EINVAL;
		}
		if (bma2x2_set_cal_trigger(client, (i + 1)) < 0) {
			dev_err(&client->dev,
					"read calibration state error\n");
			return -EINVAL;
		}
		timeout = 0;
		do {
			WAIT_CAL_READY();
			error = bma2x2_get_cal_ready(client, &tmp);
			if (error < 0) {
				dev_err(&client->dev,
						"read cal_ready error\n");
				return error;
			}
			timeout++;
			if (timeout == RETRY_TIME) {
				dev_err(&client->dev,
					"get fast calibration ready error\n");
				return -EINVAL;
			};

		} while (tmp == 0);
	}
	bma2x2_set_bandwidth(client, bma2x2->bandwidth);
	if (error < 0) {
		dev_err(&client->dev, "restore calibrate bandwidth error\n");
		return error;
	}
	return 0;
}
#else
static int bma2x2_select_chanel(struct i2c_client *client)
{
	unsigned char data_ore[3] = { BOSCH_SENSOR_PLANE };
	signed char tmp;
	int error, i;
	int timeout;
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);
	unsigned char channel_tab[] = {BMA2X2_OFFSET_TRIGGER_X,
			BMA2X2_OFFSET_TRIGGER_Y, BMA2X2_OFFSET_TRIGGER_Z};

	if (bma2x2->pdata->place > 3 && bma2x2->pdata->place < 8)
		data_ore[2] = BOSCH_SENSOR_DOWN;
	else if (bma2x2->pdata->place >= 0 && bma2x2->pdata->place < 4)
		data_ore[2] = BOSCH_SENSOR_UP;
	else {
		dev_err(&client->dev, "unknown sensor place\n");
		return -EINVAL;
	}

	if (bma2x2_set_mode(client, BMA2X2_MODE_NORMAL) < 0) {
		dev_err(&client->dev, "set calibrate mode error\n");
		return -EINVAL;
	}
	if (bma2x2_set_bandwidth(client, BMA2X2_BW_1000HZ) < 0) {
		dev_err(&client->dev, "set calibrate bandwidth error\n");
		return -EINVAL;
	}
	if (bma2x2_set_range(client, BMA2X2_RANGE_SET) < 0) {
		dev_err(&client->dev, "set calibrate range error\n");
		return -EINVAL;
	}

	for (i = 0; i < 3; i++) {
		if (bma2x2_set_offset_target(client, channel_tab[i],
			(unsigned char)data_ore[i]) < 0) {
			dev_err(&client->dev,
					"set offset target error\n");
			return -EINVAL;
		}
		if (bma2x2_set_cal_trigger(client, (i + 1)) < 0) {
			dev_err(&client->dev,
					"read calibration state error\n");
			return -EINVAL;
		}
		timeout = 0;
		do {
			WAIT_CAL_READY();
			error = bma2x2_get_cal_ready(client, &tmp);
			if (error < 0) {
				dev_err(&client->dev,
						"read cal_ready error\n");
				return error;
			}
			timeout++;
			if (timeout == RETRY_TIME) {
				dev_err(&client->dev,
					"get fast calibration ready error\n");
				return -EINVAL;
			};

		} while (tmp == 0);
	}

	error = bma2x2_set_bandwidth(client, bma2x2->bandwidth);
	if (error < 0) {
		dev_err(&client->dev, "restore calibrate bandwidth error\n");
		return error;
	}
	return 0;
}
#endif

static int bma2x2_self_calibration_xyz(struct sensors_classdev *sensors_cdev,
		int axis, int apply_now)
{
	int error;
	bool pre_enable;
	struct bma2x2_data *data = container_of(sensors_cdev,
					struct bma2x2_data, cdev);
	struct i2c_client *client = data->bma2x2_client;

	pre_enable = atomic_read(&data->enable);
	if (pre_enable)
		bma2x2_set_enable(&client->dev, 0);
	if (atomic_cmpxchg(&data->cal_status, 0, 1)) {
		dev_err(&client->dev, "do calibration error\n");
		return -EBUSY;
	}

	error = bma2x2_power_ctl(data, true);
	if (error) {
		dev_err(&client->dev, "Failed to enable sensor power\n");
		error = -EINVAL;
		goto exit;
	}
	error = bma2x2_select_chanel(client);
	if (error < 0) {
		dev_err(&client->dev, "xyz calibration error\n");
		goto exit;
	}
	dev_dbg(&client->dev, "xyz axis fast calibration finished\n");
	error = bma2x2_eeprom_prog(client);
	if (error < 0) {
		dev_err(&client->dev, "write calibration to eeprom failed\n");
		goto exit;
	}
	snprintf(data->calibrate_buf, sizeof(data->calibrate_buf),
			"%d,%d,%d", 0, 0, 0);
	sensors_cdev->params = data->calibrate_buf;

	error = bma2x2_power_ctl(data, false);
	if (error) {
		dev_err(&client->dev, "Failed to disable sensor power\n");
		goto exit;
	}

exit:
	atomic_set(&data->cal_status, 0);
	if (pre_enable)
		bma2x2_set_enable(&client->dev, 1);

	return error;
}

static int bma2x2_eeprom_prog(struct i2c_client *client)
{
	int res = 0, timeout = 0;
	unsigned char databuf;

	res = bma2x2_smbus_read_byte(client, BMA2X2_EEPROM_CTRL_REG,
					&databuf);
	if (res < 0) {
		dev_err(&client->dev, "read eeprom control reg error1\n");
		return res;
	}
	databuf |= 0x01;
	res = bma2x2_smbus_write_byte(client, BMA2X2_EEPROM_CTRL_REG,
					&databuf);
	if (res < 0) {
		dev_err(&client->dev, "write eeprom control reg error1\n");
		return res;
	}

	res = bma2x2_smbus_read_byte(client, BMA2X2_EEPROM_CTRL_REG,
					&databuf);
	if (res < 0) {
		dev_err(&client->dev, "read eeprom control reg error2\n");
		return res;
	}
	databuf |= 0x02;
	res = bma2x2_smbus_write_byte(client, BMA2X2_EEPROM_CTRL_REG,
					&databuf);
	if (res < 0) {
		dev_err(&client->dev, "write eeprom control reg error2\n");
		return res;
	}
	do {
		WAIT_CAL_READY();
		res = bma2x2_smbus_read_byte(client, BMA2X2_EEPROM_CTRL_REG,
					&databuf);
		if (res < 0) {
			dev_err(&client->dev, "read nvm_rdy error\n");
			return res;
		}
		databuf = (databuf >> 2) & 0x01;
		if (++timeout == 50) {
			dev_err(&client->dev, "check nvm_rdy time out\n");
			break;
		}
	} while (databuf == 0);

	res = bma2x2_smbus_read_byte(client, BMA2X2_EEPROM_CTRL_REG,
					&databuf);
	if (res < 0) {
		dev_err(&client->dev, "read eeprom control reg error3\n");
		return res;
	}
	databuf &= 0xFE;
	res = bma2x2_smbus_write_byte(client, BMA2X2_EEPROM_CTRL_REG,
					&databuf);
	if (res < 0) {
		dev_err(&client->dev, "write eeprom control reg error3\n");
		return res;
	}
	return res;
}

static int bma2x2_write_cal_params(struct sensors_classdev *sensors_cdev,
		struct cal_result_t *cal_result)
{
	struct bma2x2_data *data = container_of(sensors_cdev,
					struct bma2x2_data, cdev);

	snprintf(data->calibrate_buf, sizeof(data->calibrate_buf),
			"%d,%d,%d", 0, 0, 0);
	sensors_cdev->params = data->calibrate_buf;
	return 0;
}

static ssize_t bma2x2_fast_calibration_x_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{


	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

#ifdef CONFIG_SENSORS_BMI058
	if (bma2x2_get_offset_target(bma2x2->bma2x2_client,
				BMI058_OFFSET_TRIGGER_X, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");
#else
	if (bma2x2_get_offset_target(bma2x2->bma2x2_client,
				BMA2X2_OFFSET_TRIGGER_X, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");
#endif

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_fast_calibration_x_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	signed char tmp;
	unsigned char timeout = 0;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

#ifdef CONFIG_SENSORS_BMI058
	if (bma2x2_set_offset_target(bma2x2->bma2x2_client,
			BMI058_OFFSET_TRIGGER_X, (unsigned char)data) < 0)
		return -EINVAL;
#else
	if (bma2x2_set_offset_target(bma2x2->bma2x2_client,
			BMA2X2_OFFSET_TRIGGER_X, (unsigned char)data) < 0)
		return -EINVAL;
#endif

	if (bma2x2_set_cal_trigger(bma2x2->bma2x2_client, 1) < 0)
		return -EINVAL;

	do {
		WAIT_CAL_READY();
		bma2x2_get_cal_ready(bma2x2->bma2x2_client, &tmp);

		timeout++;
		if (timeout == 50) {
			dev_err(&client->dev, "get fast calibration ready error\n");
			return -EINVAL;
		};

	} while (tmp == 0);

	dev_dbg(&client->dev, "x axis fast calibration finished\n");
	return count;
}

static ssize_t bma2x2_fast_calibration_y_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{


	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

#ifdef CONFIG_SENSORS_BMI058
	if (bma2x2_get_offset_target(bma2x2->bma2x2_client,
					BMI058_OFFSET_TRIGGER_Y, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");
#else
	if (bma2x2_get_offset_target(bma2x2->bma2x2_client,
					BMA2X2_OFFSET_TRIGGER_Y, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");
#endif

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_fast_calibration_y_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	signed char tmp;
	unsigned char timeout = 0;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

#ifdef CONFIG_SENSORS_BMI058
	if (bma2x2_set_offset_target(bma2x2->bma2x2_client,
			BMI058_OFFSET_TRIGGER_Y, (unsigned char)data) < 0)
		return -EINVAL;
#else
	if (bma2x2_set_offset_target(bma2x2->bma2x2_client,
			BMA2X2_OFFSET_TRIGGER_Y, (unsigned char)data) < 0)
		return -EINVAL;
#endif

	if (bma2x2_set_cal_trigger(bma2x2->bma2x2_client, 2) < 0)
		return -EINVAL;

	do {
		WAIT_CAL_READY();
		bma2x2_get_cal_ready(bma2x2->bma2x2_client, &tmp);

		timeout++;
		if (timeout == 50) {
			dev_err(&client->dev, "get fast calibration ready error\n");
			return -EINVAL;
		};

	} while (tmp == 0);

	dev_dbg(&client->dev, "y axis fast calibration finished\n");
	return count;
}

static ssize_t bma2x2_fast_calibration_z_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{


	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_offset_target(bma2x2->bma2x2_client, 3, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_fast_calibration_z_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	signed char tmp;
	unsigned char timeout = 0;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_offset_target(bma2x2->bma2x2_client, 3, (unsigned
					char)data) < 0)
		return -EINVAL;

	if (bma2x2_set_cal_trigger(bma2x2->bma2x2_client, 3) < 0)
		return -EINVAL;

	do {
		WAIT_CAL_READY();
		bma2x2_get_cal_ready(bma2x2->bma2x2_client, &tmp);

		timeout++;
		if (timeout == 50) {
			dev_err(&client->dev, "get fast calibration ready error\n");
			return -EINVAL;
		};

	} while (tmp == 0);

	dev_dbg(&client->dev, "z axis fast calibration finished\n");
	return count;
}


static ssize_t bma2x2_SleepDur_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_sleep_duration(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_SleepDur_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma2x2_set_sleep_duration(bma2x2->bma2x2_client,
				(unsigned char) data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_fifo_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_fifo_mode(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_fifo_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma2x2_set_fifo_mode(bma2x2->bma2x2_client,
				(unsigned char) data) < 0)
		return -EINVAL;
	return count;
}



static ssize_t bma2x2_fifo_trig_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_fifo_trig(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_fifo_trig_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma2x2_set_fifo_trig(bma2x2->bma2x2_client,
				(unsigned char) data) < 0)
		return -EINVAL;

	return count;
}



static ssize_t bma2x2_fifo_trig_src_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_fifo_trig_src(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_fifo_trig_src_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma2x2_set_fifo_trig_src(bma2x2->bma2x2_client,
				(unsigned char) data) < 0)
		return -EINVAL;

	return count;
}


/*!
 * @brief show fifo_data_sel axis definition(Android definition, not sensor HW reg).
 * 0--> x, y, z axis fifo data for every frame
 * 1--> only x axis fifo data for every frame
 * 2--> only y axis fifo data for every frame
 * 3--> only z axis fifo data for every frame
 */
static ssize_t bma2x2_fifo_data_sel_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);
	signed char place = BOSCH_SENSOR_PLACE_UNKNOWN;

	if (bma2x2_get_fifo_data_sel(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

#ifdef CONFIG_SENSORS_BMI058
/*Update BMI058 fifo_data_sel to the BMA2x2 common definition*/
	if (BMI058_FIFO_DAT_SEL_X == data)
		data = BMA2X2_FIFO_DAT_SEL_X;
	else if (BMI058_FIFO_DAT_SEL_Y == data)
		data = BMA2X2_FIFO_DAT_SEL_Y;
#endif

	/*remaping fifo_dat_sel if define virtual place in BSP files*/
	place = bma2x2->pdata->place;
	/* sensor with place 0 needs not to be remapped */
	if ((place > 0) && (place < MAX_AXIS_REMAP_TAB_SZ)) {
		/* BMA2X2_FIFO_DAT_SEL_X: 1, Y:2, Z:3;
		* but bst_axis_remap_tab_dft[i].src_x:0, y:1, z:2
		* so we need to +1*/
		if (BMA2X2_FIFO_DAT_SEL_X == data)
			data = bst_axis_remap_tab_dft[place].src_x + 1;
		else if (BMA2X2_FIFO_DAT_SEL_Y == data)
			data = bst_axis_remap_tab_dft[place].src_y + 1;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_fifo_framecount_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_fifo_framecount(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_fifo_framecount_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	bma2x2->fifo_count = (unsigned int) data;

	return count;
}

static ssize_t bma2x2_temperature_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_read_temperature(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

/*!
 * @brief store fifo_data_sel axis definition(Android definition, not sensor HW reg).
 * 0--> x, y, z axis fifo data for every frame
 * 1--> only x axis fifo data for every frame
 * 2--> only y axis fifo data for every frame
 * 3--> only z axis fifo data for every frame
 */
static ssize_t bma2x2_fifo_data_sel_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);
	signed char place;

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	/*save fifo_data_sel(android definition)*/
	bma2x2->fifo_datasel = (unsigned char) data;

	/*remaping fifo_dat_sel if define virtual place*/
	place = bma2x2->pdata->place;
	/* sensor with place 0 needs not to be remapped */
	if ((place > 0) && (place < MAX_AXIS_REMAP_TAB_SZ)) {
		/*Need X Y axis revesal sensor place: P1, P3, P5, P7 */
		/* BMA2X2_FIFO_DAT_SEL_X: 1, Y:2, Z:3;
		  * but bst_axis_remap_tab_dft[i].src_x:0, y:1, z:2
		  * so we need to +1*/
		if (BMA2X2_FIFO_DAT_SEL_X == data)
			data =  bst_axis_remap_tab_dft[place].src_x + 1;
		else if (BMA2X2_FIFO_DAT_SEL_Y == data)
			data =  bst_axis_remap_tab_dft[place].src_y + 1;
	}
#ifdef CONFIG_SENSORS_BMI058
	/*Update BMI058 fifo_data_sel to the BMA2x2 common definition*/
	if (BMA2X2_FIFO_DAT_SEL_X == data)
		data = BMI058_FIFO_DAT_SEL_X;
	else if (BMA2X2_FIFO_DAT_SEL_Y == data)
		data = BMI058_FIFO_DAT_SEL_Y;

#endif
	if (bma2x2_set_fifo_data_sel(bma2x2->bma2x2_client,
				(unsigned char) data) < 0)
		return -EINVAL;

	return count;
}


/*!
 * brief: bma2x2 single axis data remaping
 * @param[i] fifo_datasel   fifo axis data select setting
 * @param[i/o] remap_dir   remapping direction
 * @param[i] client_data   to transfer sensor place
 *
 * @return none
 */
static void bma2x2_single_axis_remaping(unsigned char fifo_datasel,
		unsigned char *remap_dir, struct bma2x2_data *client_data)
{
	signed char place = client_data->pdata->place;
	/* sensor with place 0 needs not to be remapped */
	if ((place <= 0)  || (place >= MAX_AXIS_REMAP_TAB_SZ))
		return;

	if (fifo_datasel < 1 || fifo_datasel > 3)
		return;

	switch (fifo_datasel) {
	/*P2, P3, P4, P5 X axis(andorid) need to reverse*/
	case BMA2X2_FIFO_DAT_SEL_X:
		if (-1 == bst_axis_remap_tab_dft[place].sign_x)
			*remap_dir = 1;
		else
			*remap_dir = 0;
		break;
	/*P1, P2, P5, P6 Y axis(andorid) need to reverse*/
	case BMA2X2_FIFO_DAT_SEL_Y:
		if (-1 == bst_axis_remap_tab_dft[place].sign_y)
			*remap_dir = 1;
		else
			*remap_dir = 0;
		break;
	case BMA2X2_FIFO_DAT_SEL_Z:
	/*P4, P5, P6, P7 Z axis(andorid) need to reverse*/
		if (-1 == bst_axis_remap_tab_dft[place].sign_z)
			*remap_dir = 1;
		else
			*remap_dir = 0;
		break;
	default:
		break;
	}
}

static int bma2x2_flush_fifo(struct bma2x2_data *bma2x2)
{
	struct i2c_client *client = bma2x2->bma2x2_client;
	int bitwidth, i, err, ns;
	unsigned char f_len = 0;
	unsigned char fifo_count;
	s64 interval_ns, ts_ns, sec;
	struct bma2x2acc acc;

	err = bma2x2_get_fifo_framecount(bma2x2->bma2x2_client, &fifo_count);
	if (err)
		dev_err(&client->dev,
			"Get fifo count error! err=%d\n", err);

	dev_dbg(&client->dev,
			"bma2x2_flush_fifo fifo_count=%d\n", fifo_count);
	if (fifo_count > MAX_FIFO_F_LEVEL) {
		dev_err(&client->dev,
			"Invalid fifo framecount: %d\n", fifo_count);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(bma2x2->fifo_buf)) {
		dev_err(&client->dev,
			"Not enough memory for fifo data\n");
		return -ENOMEM;
	}
	interval_ns = bma2x2_bandwidth_to_interval(bma2x2) * NSEC_PER_MSEC;
	ts_ns = bma2x2->fifo_start_ns + interval_ns;
	bma2x2_set_fifo_start_time(bma2x2);
	f_len = bma2x2->fifo_datasel ?
		FIFO_FRAMESIZE_1_AXIS : FIFO_FRAMESIZE_3_AXIS;
	if (f_len  != FIFO_FRAMESIZE_3_AXIS) {
		/* Reset FIFO if it doesn't contain X Y Z three axis data */
		dev_err(&client->dev,
			"Incorrect FIFO data select (0x%x)!\n",
			bma2x2->fifo_datasel);
		err = -EBUSY;
		goto reset_fifo;
	}
	err = bma_i2c_burst_read(client,
			BMA2X2_FIFO_DATA_OUTPUT_REG, bma2x2->fifo_buf,
			fifo_count * f_len);
	if (err < 0) {
		dev_err(&client->dev,
			"Read byte block error ret=%d\n", err);
		err = -EIO;
		goto reset_fifo;
	}

	for (i = 0; i < fifo_count; i++) {
		acc.x =
		((unsigned char)bma2x2->fifo_buf[i * f_len + 1] << 8 |
			(unsigned char)bma2x2->fifo_buf[i * f_len + 0]);
		acc.y =
		((unsigned char)bma2x2->fifo_buf[i * f_len + 3] << 8 |
			(unsigned char)bma2x2->fifo_buf[i * f_len + 2]);
		acc.z =
		((unsigned char)bma2x2->fifo_buf[i * f_len + 5] << 8 |
			(unsigned char)bma2x2->fifo_buf[i * f_len + 4]);
		bitwidth = bma2x2_sensor_bitwidth[bma2x2->sensor_type];
		BMA2X2_SHIFT_BITWIDTH(acc.x, bitwidth);
		BMA2X2_SHIFT_BITWIDTH(acc.y, bitwidth);
		BMA2X2_SHIFT_BITWIDTH(acc.z, bitwidth);

		bma2x2_remap_sensor_data(&acc, bma2x2);

		sec = ts_ns;
		ns = do_div(sec, NSEC_PER_SEC);
		ts_ns += interval_ns;
		input_report_abs(bma2x2->input, ABS_X,
				(int)acc.x << bma2x2->sensitivity);
		input_report_abs(bma2x2->input, ABS_Y,
				(int)acc.y << bma2x2->sensitivity);
		input_report_abs(bma2x2->input, ABS_Z,
				(int)acc.z << bma2x2->sensitivity);
		input_event(bma2x2->input, EV_SYN, SYN_TIME_SEC,
				(int)sec);
		input_event(bma2x2->input, EV_SYN, SYN_TIME_NSEC,
				(int)ns);
		input_sync(bma2x2->input);
	}
	return 0;

reset_fifo:
	/* Clear FIFO content and reset interrupt */
	bma2x2_set_fifo_mode(client, BMA2X2_FIFO_MODE_FIFO);
	return err;
}

static ssize_t bma2x2_fifo_data_out_frame_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int err, i, len;
	signed char fifo_data_out[MAX_FIFO_F_LEVEL * MAX_FIFO_F_BYTES] = {0};
	unsigned char f_len = 0;
	s16 value;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);
	struct bma2x2acc acc_lsb;
	unsigned char axis_dir_remap = 0;

	if (bma2x2->fifo_datasel) {
		/*Select one axis data output for every fifo frame*/
		f_len = FIFO_FRAMESIZE_1_AXIS;
	} else	{
		/*Select X Y Z axis data output for every fifo frame*/
		f_len = FIFO_FRAMESIZE_3_AXIS;
	}

	if (bma2x2->fifo_count == 0)
		return -EINVAL;

	if (bma_i2c_burst_read(bma2x2->bma2x2_client,
			BMA2X2_FIFO_DATA_OUTPUT_REG, fifo_data_out,
						bma2x2->fifo_count * f_len) < 0)
		return snprintf(buf, PAGE_SIZE, "Read byte block error\n");


	err = 0;

/* please give attation for the fifo output data format*/
	if (f_len == FIFO_FRAMESIZE_3_AXIS) {
		/* Select X Y Z axis data output for every frame */
		for (i = 0; i < bma2x2->fifo_count; i++) {
			acc_lsb.x =
			((unsigned char)fifo_data_out[i * f_len + 1] << 8 |
				(unsigned char)fifo_data_out[i * f_len + 0]);
			acc_lsb.y =
			((unsigned char)fifo_data_out[i * f_len + 3] << 8 |
				(unsigned char)fifo_data_out[i * f_len + 2]);
			acc_lsb.z =
			((unsigned char)fifo_data_out[i * f_len + 5] << 8 |
				(unsigned char)fifo_data_out[i * f_len + 4]);
#ifndef BMA2X2_SENSOR_IDENTIFICATION_ENABLE
			acc_lsb.x >>=
			(16 - bma2x2_sensor_bitwidth[bma2x2->sensor_type]);
			acc_lsb.y >>=
			(16 - bma2x2_sensor_bitwidth[bma2x2->sensor_type]);
			acc_lsb.z >>=
			(16 - bma2x2_sensor_bitwidth[bma2x2->sensor_type]);
#endif
			bma2x2_remap_sensor_data(&acc_lsb, bma2x2);
			len = snprintf(buf, PAGE_SIZE, "%d %d %d ",
				acc_lsb.x, acc_lsb.y, acc_lsb.z);
			buf += len;
			err += len;
		}
	} else {
		/* single axis data output for every frame */
		bma2x2_single_axis_remaping(bma2x2->fifo_datasel,
					&axis_dir_remap, bma2x2);
		for (i = 0; i < bma2x2->fifo_count * f_len / 2; i++)	{
			value = ((unsigned char)fifo_data_out[2 * i + 1] << 8 |
					(unsigned char)fifo_data_out[2 * i]);
#ifndef BMA2X2_SENSOR_IDENTIFICATION_ENABLE
			value >>=
			(16 - bma2x2_sensor_bitwidth[bma2x2->sensor_type]);
#endif
			if (axis_dir_remap)
				value = 0 - value;
			len = snprintf(buf, PAGE_SIZE, "%d ", value);
			buf += len;
			err += len;
		}
	}

	return err;
}

static ssize_t bma2x2_offset_x_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_offset_x(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_offset_x_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_offset_x(bma2x2->bma2x2_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_offset_y_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_offset_y(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_offset_y_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_offset_y(bma2x2->bma2x2_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma2x2_offset_z_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	if (bma2x2_get_offset_z(bma2x2->bma2x2_client, &data) < 0)
		return snprintf(buf, PAGE_SIZE, "Read error\n");

	return snprintf(buf, PAGE_SIZE, "%d\n", data);

}

static ssize_t bma2x2_offset_z_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma2x2_set_offset_z(bma2x2->bma2x2_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

#ifdef CONFIG_SIG_MOTION
static int bma2x2_set_en_slope_int(struct bma2x2_data *bma2x2,
		int en)
{
	int err;
	struct i2c_client *client = bma2x2->bma2x2_client;

	if (en) {
		/* Set the related parameters which needs to be fine tuned by
		* interfaces: slope_threshold and slope_duration
		*/
		/*dur: 192 samples ~= 3s*/
		err = bma2x2_set_slope_duration(client, BMA2X2_SMD_SLOPE_DUR);
		err += bma2x2_set_slope_threshold(client, BMA2X2_SMD_SLOPE_TH);

		/*Enable the interrupts*/
		err += bma2x2_set_Int_Enable(client, 5, 1);/*Slope X*/
		err += bma2x2_set_Int_Enable(client, 6, 1);/*Slope Y*/
		err += bma2x2_set_Int_Enable(client, 7, 1);/*Slope Z*/
	} else {
		err = bma2x2_set_Int_Enable(client, 5, 0);/*Slope X*/
		err += bma2x2_set_Int_Enable(client, 6, 0);/*Slope Y*/
		err += bma2x2_set_Int_Enable(client, 7, 0);/*Slope Z*/
	}
	dev_dbg(&bma2x2->bma2x2_client->dev,
		"bma2x2_set_en_slope_int en=%d, err=%d\n", en, err);
	return err;
}

static ssize_t bma2x2_en_sig_motion_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		atomic_read(&bma2x2->en_sig_motion));
}

static int bma2x2_set_en_sig_motion(struct bma2x2_data *bma2x2,
		int en)
{
	int err = 0;

	en = (en >= 1) ? 1 : 0;  /* set sig motion sensor status */

	if (atomic_read(&bma2x2->en_sig_motion) != en) {
		if (en) {
			err = bma2x2_set_mode(bma2x2->bma2x2_client,
					BMA2X2_MODE_NORMAL);
			err = bma2x2_set_en_slope_int(bma2x2, en);
			enable_irq_wake(bma2x2->IRQ);
		} else {
			disable_irq_wake(bma2x2->IRQ);
			err = bma2x2_set_en_slope_int(bma2x2, en);
			err = bma2x2_set_mode(bma2x2->bma2x2_client,
					BMA2X2_MODE_SUSPEND);
		}
		atomic_set(&bma2x2->en_sig_motion, en);
	}
	return err;
}

static int bma2x2_smd_enable(struct bma2x2_data *bma2x2, bool enable)
{
	struct i2c_client *client = bma2x2->bma2x2_client;
	int acc_enable = atomic_read(&bma2x2->enable);
	int smd_enable = atomic_read(&bma2x2->en_sig_motion);
	int err = 0;

	mutex_lock(&bma2x2->enable_mutex);
	if (enable && !smd_enable) {
		if (!acc_enable) {
			if (bma2x2_power_ctl(bma2x2, true)) {
				err = -EBUSY;
				goto mutex_exit;
			}
			if (bma2x2_open_init(client, bma2x2) < 0) {
				bma2x2_power_ctl(bma2x2, false);
				err = -EBUSY;
				goto mutex_exit;
			}
			bma2x2_pinctrl_state(bma2x2, true);
			bma2x2_set_mode(client, BMA2X2_MODE_NORMAL);
		}
		err = bma2x2_set_en_slope_int(bma2x2, enable);
		if (err) {
			bma2x2_set_mode(client, BMA2X2_MODE_SUSPEND);
			bma2x2_pinctrl_state(bma2x2, false);
			bma2x2_power_ctl(bma2x2, false);
			goto mutex_exit;
		}
		if (!acc_enable) {
			bma2x2_config_interrupt(bma2x2, true);
			enable_irq(bma2x2->IRQ);
		}
		enable_irq_wake(bma2x2->IRQ);
		atomic_set(&bma2x2->en_sig_motion, 1);
	} else if (!enable && smd_enable) {
		disable_irq_wake(bma2x2->IRQ);
		if (!acc_enable)
			disable_irq(bma2x2->IRQ);

		err = bma2x2_set_en_slope_int(bma2x2, enable);
		if (err)
			goto mutex_exit;
		if (!acc_enable) {
			bma2x2_store_state(client, bma2x2);
			bma2x2_set_mode(client, BMA2X2_MODE_SUSPEND);
			bma2x2_pinctrl_state(bma2x2, false);
			bma2x2_power_ctl(bma2x2, false);
		}
		atomic_set(&bma2x2->en_sig_motion, 0);
	} else {
		dev_err(&bma2x2->bma2x2_client->dev,
			"SMD state incorrect! enable=%d, state=%d\n",
			enable, smd_enable);
	}

mutex_exit:
	mutex_unlock(&bma2x2->enable_mutex);
	if (err)
		dev_err(&client->dev, "Set SMD error! enable=%d, err=%d\n",
			enable, err);
	return err;
}

static int bma2x2_smd_cdev_enable(struct sensors_classdev *sensors_cdev,
				unsigned int enable)
{
	struct bma2x2_data *bma2x2 = container_of(sensors_cdev,
					struct bma2x2_data, smd_cdev);

	return bma2x2_smd_enable(bma2x2, !!enable);
}

static ssize_t bma2x2_en_sig_motion_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if ((data == 0) || (data == 1))
		bma2x2_set_en_sig_motion(bma2x2, data);

	return count;
}
#endif /* CONFIG_SIG_MOTION */

#ifdef CONFIG_DOUBLE_TAP
static int bma2x2_set_en_single_tap_int(struct bma2x2_data *bma2x2, int en)
{
	int err;
	struct i2c_client *client = bma2x2->bma2x2_client;

	if (en) {
		/* set tap interruption parameter here if needed.
		bma2x2_set_tap_duration(client, 0xc0);
		bma2x2_set_tap_threshold(client, 0x16);
		*/

		/*Enable the single tap interrupts*/
		err = bma2x2_set_Int_Enable(client, 8, 1);
	#ifdef BMA2X2_ENABLE_INT1
		err += bma2x2_set_int1_pad_sel(client, PAD_SINGLE_TAP);
	#else
		err += bma2x2_set_int2_pad_sel(client, PAD_SINGLE_TAP);
	#endif
	} else {
		err = bma2x2_set_Int_Enable(client, 8, 0);
	}
	return err;
}

static ssize_t bma2x2_tap_time_period_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	return snprintf(buf, PAGE_SIZE, "%d\n", bma2x2->tap_time_period);
}

static ssize_t bma2x2_tap_time_period_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	bma2x2->tap_time_period = data;

	return count;
}

static ssize_t bma2x2_en_double_tap_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		atomic_read(&bma2x2->en_double_tap));
}

static int bma2x2_set_en_double_tap(struct bma2x2_data *bma2x2,
		int en)
{
	int err = 0;

	en = (en >= 1) ? 1 : 0;

	if (atomic_read(&bma2x2->en_double_tap) != en) {
		if (en) {
			err = bma2x2_set_mode(bma2x2->bma2x2_client,
					BMA2X2_MODE_NORMAL);
			err = bma2x2_set_en_single_tap_int(bma2x2, en);
		} else {
			err = bma2x2_set_en_single_tap_int(bma2x2, en);
			err = bma2x2_set_mode(bma2x2->bma2x2_client,
					BMA2X2_MODE_SUSPEND);
		}
		atomic_set(&bma2x2->en_double_tap, en);
	}
	return err;
}

static ssize_t bma2x2_en_double_tap_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma2x2_data *bma2x2 = i2c_get_clientdata(client);

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;

	if ((data == 0) || (data == 1))
		bma2x2_set_en_double_tap(bma2x2, data);

	return count;
}

static void bma2x2_tap_timeout_handle(unsigned long data)
{
	struct bma2x2_data *bma2x2 = (struct bma2x2_data *)data;

	dev_dbg(&bma2x2->bma2x2_client->dev,
		"tap interrupt handle, timeout\n");
	mutex_lock(&bma2x2->tap_mutex);
	bma2x2->tap_times = 0;
	mutex_unlock(&bma2x2->tap_mutex);

	/* if a single tap need to report, open the define */
#ifdef REPORT_SINGLE_TAP_WHEN_DOUBLE_TAP_SENSOR_ENABLED
	input_report_rel(bma2x2->dev_interrupt,
		SINGLE_TAP_INTERRUPT,
		SINGLE_TAP_INTERRUPT_HAPPENED);
	input_sync(bma2x2->dev_interrupt);
#endif

}
#endif

static DEVICE_ATTR(range, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_range_show, bma2x2_range_store);
static DEVICE_ATTR(bandwidth, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_bandwidth_show, bma2x2_bandwidth_store);
static DEVICE_ATTR(op_mode, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_mode_show, bma2x2_mode_store);
static DEVICE_ATTR(value, S_IRUSR|S_IRGRP,
		bma2x2_value_show, NULL);
static DEVICE_ATTR(value_cache, S_IRUSR|S_IRGRP,
		bma2x2_value_cache_show, NULL);
static DEVICE_ATTR(delay, S_IRUSR|S_IRGRP|S_IWUSR|S_IWGRP,
		bma2x2_delay_show, bma2x2_delay_store);
static DEVICE_ATTR(enable, S_IRUSR|S_IRGRP|S_IWUSR|S_IWGRP,
		bma2x2_enable_show, bma2x2_enable_store);
static DEVICE_ATTR(SleepDur, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_SleepDur_show, bma2x2_SleepDur_store);
static DEVICE_ATTR(fast_calibration_x, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_fast_calibration_x_show,
		bma2x2_fast_calibration_x_store);
static DEVICE_ATTR(fast_calibration_y, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_fast_calibration_y_show,
		bma2x2_fast_calibration_y_store);
static DEVICE_ATTR(fast_calibration_z, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_fast_calibration_z_show,
		bma2x2_fast_calibration_z_store);
static DEVICE_ATTR(fifo_mode, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_fifo_mode_show, bma2x2_fifo_mode_store);
static DEVICE_ATTR(fifo_framecount, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_fifo_framecount_show, bma2x2_fifo_framecount_store);
static DEVICE_ATTR(fifo_trig, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_fifo_trig_show, bma2x2_fifo_trig_store);
static DEVICE_ATTR(fifo_trig_src, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_fifo_trig_src_show, bma2x2_fifo_trig_src_store);
static DEVICE_ATTR(fifo_data_sel, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_fifo_data_sel_show, bma2x2_fifo_data_sel_store);
static DEVICE_ATTR(fifo_data_frame, S_IRUSR|S_IRGRP,
		bma2x2_fifo_data_out_frame_show, NULL);
static DEVICE_ATTR(reg, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_register_show, bma2x2_register_store);
static DEVICE_ATTR(chip_id, S_IRUSR|S_IRGRP,
		bma2x2_chip_id_show, NULL);
static DEVICE_ATTR(offset_x, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_offset_x_show,
		bma2x2_offset_x_store);
static DEVICE_ATTR(offset_y, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_offset_y_show,
		bma2x2_offset_y_store);
static DEVICE_ATTR(offset_z, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_offset_z_show,
		bma2x2_offset_z_store);
static DEVICE_ATTR(enable_int, S_IWUSR,
		NULL, bma2x2_enable_int_store);
static DEVICE_ATTR(int_mode, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_int_mode_show, bma2x2_int_mode_store);
static DEVICE_ATTR(slope_duration, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_slope_duration_show, bma2x2_slope_duration_store);
static DEVICE_ATTR(slope_threshold, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_slope_threshold_show, bma2x2_slope_threshold_store);
static DEVICE_ATTR(slope_no_mot_duration, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_slope_no_mot_duration_show,
			bma2x2_slope_no_mot_duration_store);
static DEVICE_ATTR(slope_no_mot_threshold, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_slope_no_mot_threshold_show,
			bma2x2_slope_no_mot_threshold_store);
static DEVICE_ATTR(high_g_duration, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_high_g_duration_show, bma2x2_high_g_duration_store);
static DEVICE_ATTR(high_g_threshold, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_high_g_threshold_show, bma2x2_high_g_threshold_store);
static DEVICE_ATTR(low_g_duration, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_low_g_duration_show, bma2x2_low_g_duration_store);
static DEVICE_ATTR(low_g_threshold, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_low_g_threshold_show, bma2x2_low_g_threshold_store);
static DEVICE_ATTR(tap_duration, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_tap_duration_show, bma2x2_tap_duration_store);
static DEVICE_ATTR(tap_threshold, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_tap_threshold_show, bma2x2_tap_threshold_store);
static DEVICE_ATTR(tap_quiet, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_tap_quiet_show, bma2x2_tap_quiet_store);
static DEVICE_ATTR(tap_shock, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_tap_shock_show, bma2x2_tap_shock_store);
static DEVICE_ATTR(tap_samp, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_tap_samp_show, bma2x2_tap_samp_store);
static DEVICE_ATTR(orient_mode, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_orient_mode_show, bma2x2_orient_mode_store);
static DEVICE_ATTR(orient_blocking, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_orient_blocking_show, bma2x2_orient_blocking_store);
static DEVICE_ATTR(orient_hyst, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_orient_hyst_show, bma2x2_orient_hyst_store);
static DEVICE_ATTR(orient_theta, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_orient_theta_show, bma2x2_orient_theta_store);
static DEVICE_ATTR(flat_theta, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_flat_theta_show, bma2x2_flat_theta_store);
static DEVICE_ATTR(flat_hold_time, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_flat_hold_time_show, bma2x2_flat_hold_time_store);
static DEVICE_ATTR(selftest, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_selftest_show, bma2x2_selftest_store);
static DEVICE_ATTR(softreset, S_IWUSR,
		NULL, bma2x2_softreset_store);
static DEVICE_ATTR(temperature, S_IRUSR|S_IRGRP,
		bma2x2_temperature_show, NULL);
static DEVICE_ATTR(place, S_IRUSR|S_IRGRP,
		bma2x2_place_show, NULL);
static DEVICE_ATTR(calibration, S_IRUSR|S_IRGRP|S_IWUSR|S_IWGRP,
	    bma2x2_calibration_xyz_show, bma2x2_calibration_xyz_store);
#ifdef CONFIG_SIG_MOTION
static DEVICE_ATTR(en_sig_motion, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_en_sig_motion_show, bma2x2_en_sig_motion_store);
#endif
#ifdef CONFIG_DOUBLE_TAP
static DEVICE_ATTR(tap_time_period, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_tap_time_period_show, bma2x2_tap_time_period_store);
static DEVICE_ATTR(en_double_tap, S_IRUSR|S_IRGRP|S_IWUSR,
		bma2x2_en_double_tap_show, bma2x2_en_double_tap_store);
#endif

static struct attribute *bma2x2_attributes[] = {
	&dev_attr_range.attr,
	&dev_attr_bandwidth.attr,
	&dev_attr_op_mode.attr,
	&dev_attr_value.attr,
	&dev_attr_value_cache.attr,
	&dev_attr_delay.attr,
	&dev_attr_enable.attr,
	&dev_attr_SleepDur.attr,
	&dev_attr_reg.attr,
	&dev_attr_fast_calibration_x.attr,
	&dev_attr_fast_calibration_y.attr,
	&dev_attr_fast_calibration_z.attr,
	&dev_attr_fifo_mode.attr,
	&dev_attr_fifo_framecount.attr,
	&dev_attr_fifo_trig.attr,
	&dev_attr_fifo_trig_src.attr,
	&dev_attr_fifo_data_sel.attr,
	&dev_attr_fifo_data_frame.attr,
	&dev_attr_chip_id.attr,
	&dev_attr_offset_x.attr,
	&dev_attr_offset_y.attr,
	&dev_attr_offset_z.attr,
	&dev_attr_enable_int.attr,
	&dev_attr_int_mode.attr,
	&dev_attr_slope_duration.attr,
	&dev_attr_slope_threshold.attr,
	&dev_attr_slope_no_mot_duration.attr,
	&dev_attr_slope_no_mot_threshold.attr,
	&dev_attr_high_g_duration.attr,
	&dev_attr_high_g_threshold.attr,
	&dev_attr_low_g_duration.attr,
	&dev_attr_low_g_threshold.attr,
	&dev_attr_tap_threshold.attr,
	&dev_attr_tap_duration.attr,
	&dev_attr_tap_quiet.attr,
	&dev_attr_tap_shock.attr,
	&dev_attr_tap_samp.attr,
	&dev_attr_orient_mode.attr,
	&dev_attr_orient_blocking.attr,
	&dev_attr_orient_hyst.attr,
	&dev_attr_orient_theta.attr,
	&dev_attr_flat_theta.attr,
	&dev_attr_flat_hold_time.attr,
	&dev_attr_selftest.attr,
	&dev_attr_softreset.attr,
	&dev_attr_temperature.attr,
	&dev_attr_place.attr,
	&dev_attr_calibration.attr,
#ifdef CONFIG_SIG_MOTION
	&dev_attr_en_sig_motion.attr,
#endif
#ifdef CONFIG_DOUBLE_TAP
	&dev_attr_en_double_tap.attr,
#endif

	NULL
};

static struct attribute_group bma2x2_attribute_group = {
	.attrs = bma2x2_attributes
};

#ifdef CONFIG_SIG_MOTION
static struct attribute *bma2x2_sig_motion_attributes[] = {
	&dev_attr_slope_duration.attr,
	&dev_attr_slope_threshold.attr,
	&dev_attr_en_sig_motion.attr,
	NULL
};
static struct attribute_group bma2x2_sig_motion_attribute_group = {
	.attrs = bma2x2_sig_motion_attributes
};
#endif

#ifdef CONFIG_DOUBLE_TAP
static struct attribute *bma2x2_double_tap_attributes[] = {
	&dev_attr_tap_threshold.attr,
	&dev_attr_tap_duration.attr,
	&dev_attr_tap_quiet.attr,
	&dev_attr_tap_shock.attr,
	&dev_attr_tap_samp.attr,
	&dev_attr_tap_time_period.attr,
	&dev_attr_en_double_tap.attr,
	NULL
};
static struct attribute_group bma2x2_double_tap_attribute_group = {
	.attrs = bma2x2_double_tap_attributes
};
#endif


#if defined(BMA2X2_ENABLE_INT1) || defined(BMA2X2_ENABLE_INT2)
#ifdef ENABLE_ISR_DEBUG_MSG
static unsigned char *orient[] = {"upward looking portrait upright",
	"upward looking portrait upside-down",
		"upward looking landscape left",
		"upward looking landscape right",
		"downward looking portrait upright",
		"downward looking portrait upside-down",
		"downward looking landscape left",
		"downward looking landscape right"};
#endif


static void bma2x2_high_g_interrupt_handle(struct bma2x2_data *bma2x2)
{
	unsigned char first_value = 0;
	unsigned char sign_value = 0;
	int i;

	for (i = 0; i < 3; i++) {
		bma2x2_get_HIGH_first(bma2x2->bma2x2_client, i, &first_value);
		if (first_value == 1) {
			bma2x2_get_HIGH_sign(bma2x2->bma2x2_client,
								&sign_value);
			if (sign_value == 1) {
				if (i == 0)
					input_report_rel(bma2x2->dev_interrupt,
							HIGH_G_INTERRUPT,
							HIGH_G_INTERRUPT_X_N);
				if (i == 1)
					input_report_rel(bma2x2->dev_interrupt,
							HIGH_G_INTERRUPT,
							HIGH_G_INTERRUPT_Y_N);
				if (i == 2)
					input_report_rel(bma2x2->dev_interrupt,
							HIGH_G_INTERRUPT,
							HIGH_G_INTERRUPT_Z_N);
			} else {
				if (i == 0)
					input_report_rel(bma2x2->dev_interrupt,
							HIGH_G_INTERRUPT,
							HIGH_G_INTERRUPT_X);
				if (i == 1)
					input_report_rel(bma2x2->dev_interrupt,
							HIGH_G_INTERRUPT,
							HIGH_G_INTERRUPT_Y);
				if (i == 2)
					input_report_rel(bma2x2->dev_interrupt,
							HIGH_G_INTERRUPT,
							HIGH_G_INTERRUPT_Z);
			}
		}

		ISR_INFO(&bma2x2->bma2x2_client->dev,
			"High G interrupt happened,exis is %d, first is %d,sign is %d\n",
			i, first_value, sign_value);
	}
}

#ifndef CONFIG_SIG_MOTION
static void bma2x2_slope_interrupt_handle(struct bma2x2_data *bma2x2)
{
	unsigned char first_value = 0;
	unsigned char sign_value = 0;
	int i;

	for (i = 0; i < 3; i++) {
		bma2x2_get_slope_first(bma2x2->bma2x2_client, i, &first_value);
		if (first_value == 1) {
			bma2x2_get_slope_sign(bma2x2->bma2x2_client,
								&sign_value);
			if (sign_value == 1) {
				if (i == 0)
					input_report_rel(bma2x2->dev_interrupt,
							SLOP_INTERRUPT,
							SLOPE_INTERRUPT_X_N);
				if (i == 1)
					input_report_rel(bma2x2->dev_interrupt,
							SLOP_INTERRUPT,
							SLOPE_INTERRUPT_Y_N);
				if (i == 2)
					input_report_rel(bma2x2->dev_interrupt,
							SLOP_INTERRUPT,
							SLOPE_INTERRUPT_Z_N);
			} else {
				if (i == 0)
					input_report_rel(bma2x2->dev_interrupt,
							SLOP_INTERRUPT,
							SLOPE_INTERRUPT_X);
				if (i == 1)
					input_report_rel(bma2x2->dev_interrupt,
							SLOP_INTERRUPT,
							SLOPE_INTERRUPT_Y);
				if (i == 2)
					input_report_rel(bma2x2->dev_interrupt,
							SLOP_INTERRUPT,
							SLOPE_INTERRUPT_Z);

			}
		}

		ISR_INFO(&bma2x2->bma2x2_client->dev,
			"Slop interrupt happened,exis is %d, first is %d,sign is %d\n",
			i, first_value, sign_value);
	}
}
#endif

#ifdef CONFIG_BMA_ENABLE_NEWDATA_INT
static void bma2x2_read_new_data(struct bma2x2_data *bma2x2)
{
	struct bma2x2acc value;

	bma2x2_report_axis_data(bma2x2, &value);
	mutex_lock(&bma2x2->value_mutex);
	bma2x2->value = value;
	mutex_unlock(&bma2x2->value_mutex);
}
#else
static void bma2x2_read_new_data(struct bma2x2_data *bma2x2)
{

}
#endif

#ifdef CONFIG_SIG_MOTION
static int bma2x2_register_smd(struct bma2x2_data *bma2x2, bool enable)
{
	struct input_dev *smd_input;
	int err;

	if (!enable) {
		sensors_classdev_unregister(&bma2x2->smd_cdev);
		return 0;
	}

	smd_input = devm_input_allocate_device(&bma2x2->bma2x2_client->dev);
	if (IS_ERR_OR_NULL(smd_input)) {
		dev_err(&bma2x2->bma2x2_client->dev,
			"Cannot allocate SMD device\n");
		return -ENOMEM;
	}

	smd_input->name = "bma2x2-smd";
	smd_input->id.bustype = BUS_I2C;
	input_set_capability(smd_input, EV_ABS, ABS_MISC);
	input_set_drvdata(smd_input, bma2x2);

	err = input_register_device(smd_input);
	if (err < 0) {
		dev_err(&bma2x2->bma2x2_client->dev,
			"Cannot register input interrupt device\n");
		input_free_device(smd_input);
		return err;
	}

	if (!bma2x2->pdata->int_en) {
		dev_err(&bma2x2->bma2x2_client->dev,
			"SMD need interrupt for wakeup!\n");
		input_unregister_device(smd_input);
		return -EINVAL;
	}

	bma2x2->smd_cdev = smd_cdev;
	bma2x2->smd_cdev.sensors_enable = bma2x2_smd_cdev_enable;
	err = sensors_classdev_register(&bma2x2->smd_input->dev,
			&bma2x2->smd_cdev);
	if (err)
		dev_err(&bma2x2->bma2x2_client->dev,
			"Create SMD device file failed!\n");

	bma2x2->smd_input = smd_input;

	return err;
}
#else
static inline int bma2x2_register_smd(struct bma2x2_data *bma2x2, bool enable)
{
	dev_err(&bma2x2->bma2x2_client->dev,
			"SMD feature is not enabled!\n");
	return -EINVAL;
}
#endif

#ifdef CONFIG_SIG_MOTION
static void bma2x2_report_sig_motion(struct bma2x2_data *bma2x2)
{
	ktime_t ts;

	ts = ktime_get_boottime();

	ISR_INFO(&bma2x2->bma2x2_client->dev,
		"REPORT Significant motion interrupt\n");
	pm_wakeup_event(&bma2x2->smd_input->dev, 200);
	/* report SMD event */
	input_report_abs(bma2x2->smd_input, ABS_MISC,
		bma2x2->smd_count++);
	input_event(bma2x2->smd_input, EV_SYN, SYN_TIME_SEC,
			ktime_to_timespec(ts).tv_sec);
	input_event(bma2x2->smd_input, EV_SYN, SYN_TIME_NSEC,
			ktime_to_timespec(ts).tv_nsec);
	input_sync(bma2x2->smd_input);
}

#ifdef BMA2X2_SMD_SW_ENHANCE

static bool bma2x2_detect_sig_motion(struct bma2x2_data *bma2x2)
{
	static int det_cnt;
	static s64 last_ns;
	ktime_t ts;

	ts = ktime_get_boottime();
	dev_dbg(&bma2x2->bma2x2_client->dev,
		"ts=%lld, lastns=%lld, delta=%lld, det_cnt=%d\n",
		ts.tv64, last_ns, (ts.tv64 - last_ns), det_cnt);
	if (last_ns == 0) {
		last_ns = ts.tv64;
		return false;
	}
	if (ts.tv64 - last_ns < BMA2X2_SMD_DET_TIME_NS)
		det_cnt++;
	else
		det_cnt = 0;

	last_ns = ts.tv64;

	if (det_cnt >= BMA2X2_SMD_DET_CNT) {
		det_cnt = 0;
		return true;
	} else {
		return false;
	}
}
#else
static bool bma2x2_detect_sig_motion(struct bma2x2_data *bma2x2)
{
	return true;
}
#endif /* !BMA2X2_SMD_SW_ENHANCE */
#endif

static void bma2x2_irq_work_func(struct work_struct *work)
{
	struct bma2x2_data *bma2x2 = container_of((struct work_struct *)work,
			struct bma2x2_data, irq_work);
	struct i2c_client *client = bma2x2->bma2x2_client;
	unsigned char intstatus[2] = {0};
	unsigned char first_value = 0;
	unsigned char sign_value = 0;
	int ret;

	ret = bma2x2_smbus_read_byte_block(client,
		BMA2X2_STATUS1_REG, intstatus, ARRAY_SIZE(intstatus));
	if (ret) {
		dev_err(&client->dev,
			"read interrupt status2 err, err=%d\n", ret);
		return;
	}

	ISR_INFO(&client->dev,
		"bma2x2_irq_work_func, intstatus=0x%x,0x%x\n",
		intstatus[0], intstatus[1]);

	if (intstatus[1] & BMA2X2_IS_FIFO_INT)
	{
		bma2x2_flush_fifo(bma2x2);
	}
	if (intstatus[1] & BMA2X2_IS_NEWDATA_INT)
	{
		bma2x2_read_new_data(bma2x2);
	}
	if ((intstatus[1] == 0) && (intstatus[0] == 0)) {
		/*
		 * Read new data if no other interrupt is triggered.
		 * BMA2x2 data ready flag will be cleared if new data
		 * acquisition is started, sometimes we cannot get that flag.
		 */
		bma2x2_read_new_data(bma2x2);
		return;
	}

#ifdef CONFIG_SIG_MOTION
	if (intstatus[0] & 0x04) {
		if (atomic_read(&bma2x2->en_sig_motion) == 1) {
			ISR_INFO(&bma2x2->bma2x2_client->dev,
				"Significant motion interrupt happened\n");
			if (bma2x2_detect_sig_motion(bma2x2)) {
				/*
				  * Close signification motion sensor,
				  * it will be open again if APP wants
				  */

				bma2x2_smd_enable(bma2x2, false);
				bma2x2_report_sig_motion(bma2x2);
			}
		}
	}
#endif

#ifdef CONFIG_DOUBLE_TAP
	if (intstatus[0] & 0x20) {
		if (atomic_read(&bma2x2->en_double_tap) == 1) {
			ISR_INFO(&bma2x2->bma2x2_client->dev,
				"single tap interrupt happened\n");
			bma2x2_set_Int_Enable(client, 8, 0);
			if (bma2x2->tap_times == 0)	{
				mod_timer(&bma2x2->tap_timer, jiffies +
				msecs_to_jiffies(bma2x2->tap_time_period));
				bma2x2->tap_times = 1;
			} else {
				/* only double tap is judged */
				ISR_INFO(&bma2x2->bma2x2_client->dev,
					"double tap\n");
				mutex_lock(&bma2x2->tap_mutex);
				bma2x2->tap_times = 0;
				del_timer(&bma2x2->tap_timer);
				mutex_unlock(&bma2x2->tap_mutex);
				input_report_rel(bma2x2->dev_interrupt,
					DOUBLE_TAP_INTERRUPT,
					DOUBLE_TAP_INTERRUPT_HAPPENED);
				input_sync(bma2x2->dev_interrupt);
			}
			bma2x2_set_Int_Enable(client, 8, 1);
		}
	}
#endif

	switch (intstatus[0]) {
	case 0x01:
		ISR_INFO(&bma2x2->bma2x2_client->dev,
			"Low G interrupt happened\n");
		input_report_rel(bma2x2->dev_interrupt, LOW_G_INTERRUPT,
				LOW_G_INTERRUPT_HAPPENED);
		break;

	case 0x02:
		bma2x2_high_g_interrupt_handle(bma2x2);
		break;

#ifndef CONFIG_SIG_MOTION
	case 0x04:
		bma2x2_slope_interrupt_handle(bma2x2);
		break;
#endif

	case 0x08:
		ISR_INFO(&bma2x2->bma2x2_client->dev,
			"slow/ no motion interrupt happened\n");
		input_report_rel(bma2x2->dev_interrupt,
			SLOW_NO_MOTION_INTERRUPT,
			SLOW_NO_MOTION_INTERRUPT_HAPPENED);
		break;

#ifndef CONFIG_DOUBLE_TAP
	case 0x10:
		ISR_INFO(&bma2x2->bma2x2_client->dev,
			"double tap interrupt happened\n");
		input_report_rel(bma2x2->dev_interrupt,
			DOUBLE_TAP_INTERRUPT,
			DOUBLE_TAP_INTERRUPT_HAPPENED);
		break;
	case 0x20:
		ISR_INFO(&bma2x2->bma2x2_client->dev,
			"single tap interrupt happened\n");
		input_report_rel(bma2x2->dev_interrupt,
			SINGLE_TAP_INTERRUPT,
			SINGLE_TAP_INTERRUPT_HAPPENED);
		break;
#endif

	case 0x40:
		bma2x2_get_orient_status(bma2x2->bma2x2_client,
				    &first_value);
		ISR_INFO(&bma2x2->bma2x2_client->dev,
			"orient interrupt happened,%s\n",
				orient[first_value]);
		if (first_value == 0)
			input_report_abs(bma2x2->dev_interrupt,
			ORIENT_INTERRUPT,
			UPWARD_PORTRAIT_UP_INTERRUPT_HAPPENED);
		else if (first_value == 1)
			input_report_abs(bma2x2->dev_interrupt,
				ORIENT_INTERRUPT,
				UPWARD_PORTRAIT_DOWN_INTERRUPT_HAPPENED);
		else if (first_value == 2)
			input_report_abs(bma2x2->dev_interrupt,
				ORIENT_INTERRUPT,
				UPWARD_LANDSCAPE_LEFT_INTERRUPT_HAPPENED);
		else if (first_value == 3)
			input_report_abs(bma2x2->dev_interrupt,
				ORIENT_INTERRUPT,
				UPWARD_LANDSCAPE_RIGHT_INTERRUPT_HAPPENED);
		else if (first_value == 4)
			input_report_abs(bma2x2->dev_interrupt,
				ORIENT_INTERRUPT,
				DOWNWARD_PORTRAIT_UP_INTERRUPT_HAPPENED);
		else if (first_value == 5)
			input_report_abs(bma2x2->dev_interrupt,
				ORIENT_INTERRUPT,
				DOWNWARD_PORTRAIT_DOWN_INTERRUPT_HAPPENED);
		else if (first_value == 6)
			input_report_abs(bma2x2->dev_interrupt,
				ORIENT_INTERRUPT,
				DOWNWARD_LANDSCAPE_LEFT_INTERRUPT_HAPPENED);
		else if (first_value == 7)
			input_report_abs(bma2x2->dev_interrupt,
				ORIENT_INTERRUPT,
				DOWNWARD_LANDSCAPE_RIGHT_INTERRUPT_HAPPENED);
		break;
	case 0x80:
		bma2x2_get_orient_flat_status(bma2x2->bma2x2_client,
				    &sign_value);
		ISR_INFO(&bma2x2->bma2x2_client->dev,
			"flat interrupt happened,flat status is %d\n",
				    sign_value);
		if (sign_value == 1) {
			input_report_abs(bma2x2->dev_interrupt,
				FLAT_INTERRUPT,
				FLAT_INTERRUPT_TRUE_HAPPENED);
		} else {
			input_report_abs(bma2x2->dev_interrupt,
				FLAT_INTERRUPT,
				FLAT_INTERRUPT_FALSE_HAPPENED);
		}
		break;

	default:
		break;
	}
}

static irqreturn_t bma2x2_irq_handler(int irq, void *handle)
{
	struct bma2x2_data *data = handle;

	if (data == NULL)
		return IRQ_HANDLED;
	if (data->bma2x2_client == NULL)
		return IRQ_HANDLED;

	queue_work(data->data_wq, &data->irq_work);

	return IRQ_HANDLED;
}
#else
static void bma2x2_irq_work_func(struct work_struct *work)
{
	struct bma2x2_data *bma2x2 = container_of((struct work_struct *)work,
			struct bma2x2_data, irq_work);

	dev_dbg(&bma2x2->bma2x2_client->dev,
		"Interrupt feature is not enabled!\n");
}

static irqreturn_t bma2x2_irq_handler(int irq, void *handle)
{
	struct bma2x2_data *bma2x2 = handle;

	dev_dbg(&bma2x2->bma2x2_client->dev,
		"Interrupt feature is not enabled!\n");
	return IRQ_HANDLED;
}
#endif /* defined(BMA2X2_ENABLE_INT1)||defined(BMA2X2_ENABLE_INT2) */

static int bma2x2_power_ctl(struct bma2x2_data *data, bool on)
{
	int ret = 0;
	int err = 0;

	if (!on && data->power_enabled) {
		ret = regulator_disable(data->vdd);
		if (ret) {
			dev_err(&data->bma2x2_client->dev,
				"Regulator vdd disable failed ret=%d\n", ret);
			return ret;
		}

		ret = regulator_disable(data->vio);
		if (ret) {
			dev_err(&data->bma2x2_client->dev,
				"Regulator vio disable failed ret=%d\n", ret);
			err = regulator_enable(data->vdd);
			return ret;
		}
		data->power_enabled = on;
	} else if (on && !data->power_enabled) {
		ret = regulator_enable(data->vdd);
		if (ret) {
			dev_err(&data->bma2x2_client->dev,
				"Regulator vdd enable failed ret=%d\n", ret);
			return ret;
		}

		ret = regulator_enable(data->vio);
		if (ret) {
			dev_err(&data->bma2x2_client->dev,
				"Regulator vio enable failed ret=%d\n", ret);
			err = regulator_disable(data->vdd);
			return ret;
		}
		data->power_enabled = on;
	} else {
		dev_info(&data->bma2x2_client->dev,
				"Power on=%d. enabled=%d\n",
				on, data->power_enabled);
	}

	return ret;
}

static int bma2x2_power_init(struct bma2x2_data *data)
{
	int ret;

	data->vdd = regulator_get(&data->bma2x2_client->dev, "vdd");
	if (IS_ERR(data->vdd)) {
		ret = PTR_ERR(data->vdd);
		dev_err(&data->bma2x2_client->dev,
			"Regulator get failed vdd ret=%d\n", ret);
		return ret;
	}

	if (regulator_count_voltages(data->vdd) > 0) {
		ret = regulator_set_voltage(data->vdd,
				BMA2x2_VDD_MIN_UV,
				BMA2x2_VDD_MAX_UV);
		if (ret) {
			dev_err(&data->bma2x2_client->dev,
				"Regulator set failed vdd ret=%d\n",
				ret);
			goto reg_vdd_put;
		}
	}

	data->vio = regulator_get(&data->bma2x2_client->dev, "vio");
	if (IS_ERR(data->vio)) {
		ret = PTR_ERR(data->vio);
		dev_err(&data->bma2x2_client->dev,
			"Regulator get failed vio ret=%d\n", ret);
		goto reg_vdd_set;
	}

	if (regulator_count_voltages(data->vio) > 0) {
		ret = regulator_set_voltage(data->vio,
				BMA2x2_VIO_MIN_UV,
				BMA2x2_VIO_MAX_UV);
		if (ret) {
			dev_err(&data->bma2x2_client->dev,
			"Regulator set failed vio ret=%d\n", ret);
			goto reg_vio_put;
		}
	}

	return 0;

reg_vio_put:
	regulator_put(data->vio);
reg_vdd_set:
	if (regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd, 0, BMA2x2_VDD_MAX_UV);
reg_vdd_put:
	regulator_put(data->vdd);
	return ret;
}

static int bma2x2_power_deinit(struct bma2x2_data *data)
{
	if (regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd,
				0, BMA2x2_VDD_MAX_UV);

	regulator_put(data->vdd);

	if (regulator_count_voltages(data->vio) > 0)
		regulator_set_voltage(data->vio,
				0, BMA2x2_VIO_MAX_UV);

	regulator_put(data->vio);

	return 0;
}

#ifdef CONFIG_OF
static int bma2x2_parse_dt(struct device *dev,
			struct bma2x2_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	u32 temp_val;
	int rc;

	rc = of_property_read_u32(np, "bosch,init-interval", &temp_val);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read init-interval\n");
		return rc;
	}

	pdata->poll_interval = temp_val;

	rc = of_property_read_u32(np, "bosch,place", &temp_val);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read sensor place parameter\n");
		return rc;
	}
	if (temp_val > 7) {
		dev_err(dev, "Invalid place parameter, use default value 0\n");
		pdata->place = 0;
	} else {
		pdata->place = temp_val;
	}

	pdata->int_en = of_property_read_bool(np, "bosch,use-interrupt");

	pdata->use_int2 = of_property_read_bool(np, "bosch,use-int2");

	pdata->use_smd = of_property_read_bool(np, "bosch,use-smd");

	pdata->use_hrtimer = of_property_read_bool(np, "bosch,use-hrtimer");

	pdata->gpio_int1 = of_get_named_gpio_flags(dev->of_node,
				"bosch,gpio-int1", 0, &pdata->int1_flag);

	pdata->gpio_int2 = of_get_named_gpio_flags(dev->of_node,
				"bosch,gpio-int2", 0, &pdata->int2_flag);

	return 0;
}
#else
static int bma2x2_parse_dt(struct device *dev,
			struct bma2x2_platform_data *pdata)
{
	return -EINVAL;
}
#endif

#ifdef CONFIG_DOUBLE_TAP
static void bma2x2_double_tap_disable(struct bma2x2_data *data)
{
	if (data->g_sensor_dev_doubletap) {
		sysfs_remove_group(&data->g_sensor_dev_doubletap->kobj,
			&bma2x2_double_tap_attribute_group);
		device_destroy(data->g_sensor_dev_doubletap);
		class_destroy(data->g_sensor_class_doubletap);
	}
}
#else
static void bma2x2_double_tap_disable(struct bma2x2_data *data)
{

}
#endif

#ifdef CONFIG_SIG_MOTION
static void bma2x2_sig_motion_disable(struct bma2x2_data *data)
{
	if (data->g_sensor_dev) {
		sysfs_remove_group(&data->g_sensor_dev->kobj,
			&bma2x2_sig_motion_attribute_group);
		device_destroy(data->g_sensor_class, 0);
		class_destroy(data->g_sensor_class);
	}
}
#else
static void bma2x2_sig_motion_disable(struct bma2x2_data *data)
{

}
#endif

static int bma2x2_open_init(struct i2c_client *client,
			struct bma2x2_data *data)
{
	int err;

	err = bma2x2_set_bandwidth(client, data->bandwidth);
	if (err < 0) {
		dev_err(&client->dev, "init bandwidth error\n");
		return err;
	}
	err = bma2x2_set_range(client, data->range);
	if (err < 0) {
		dev_err(&client->dev, "init bandwidth error\n");
		return err;
	}
	return 0;
}

static int bma2x2_get_interrupt_gpio(const struct bma2x2_data *data,
			const unsigned int gpio)
{
	struct i2c_client *client = data->bma2x2_client;
	int err;

	if (!gpio_is_valid(gpio)) {
		dev_err(&client->dev,
			"gpio(%d) is invalid,\n", gpio);
		return -EINVAL;
	}

	err = gpio_request(gpio, "bma2x2_gpio_int");
	if (err) {
		dev_err(&client->dev,
			"Unable to request gpio %d, err=%d\n",
			gpio, err);
		return err;
	}

	err = gpio_direction_input(gpio);
	if (err) {
		dev_err(&client->dev,
			"Unable to set gpio direction %d, err=%d\n",
			gpio, err);
		gpio_free(gpio);
		return err;
	}

	client->irq = gpio_to_irq(gpio);
	dev_dbg(&client->dev, "Interrupt gpio=%d, irq=%d\n",
			gpio, client->irq);

	return 0;
}

static int bma2x2_pinctrl_init(struct bma2x2_data *data)
{
	struct i2c_client *client = data->bma2x2_client;
	struct bma2x2_pinctrl_data *pctrl_data;
	struct pinctrl *pctrl;
	int ret = 0;

	pctrl = devm_pinctrl_get(&client->dev);
	if (IS_ERR_OR_NULL(pctrl)) {
		ret = PTR_ERR(pctrl);
		dev_err(&client->dev,
			"Failed to get pin pinctrl, err:%d\n", ret);
		goto exit;
	}
	pctrl_data = devm_kzalloc(&client->dev,
			sizeof(*pctrl_data), GFP_KERNEL);
	if (!pctrl_data) {
		ret = -ENOMEM;
		goto exit;
	}
	pctrl_data->pctrl = pctrl;

	pctrl_data->pins_default = pinctrl_lookup_state(pctrl, "default");
	if (IS_ERR_OR_NULL(pctrl_data->pins_default)) {
		ret = PTR_ERR(pctrl_data->pins_default);
		dev_err(&client->dev,
			"Could not get default pinstate, err:%d\n", ret);
		goto exit;
	}
	/* "sleep" state is optional to compatible with old config  */
	pctrl_data->pins_sleep = pinctrl_lookup_state(pctrl, "sleep");
	if (IS_ERR_OR_NULL(pctrl_data->pins_sleep)) {
		dev_info(&client->dev,
			"Could not get sleep pinstate, err:%ld\n",
			PTR_ERR(pctrl_data->pins_sleep));
		pctrl_data->pins_sleep = NULL;
	}
	data->pctrl_data = pctrl_data;

exit:
	return ret;
}

static void bma2x2_pinctrl_state(struct bma2x2_data *data,
			bool active)
{
	struct device dev = data->bma2x2_client->dev;
	int ret;

	if (!data->pctrl_data)
		return;

	if (active) {
		ret = pinctrl_select_state(data->pctrl_data->pctrl,
				data->pctrl_data->pins_default);
		if (ret)
			dev_info(&dev,
				"Select default pinstate err:%d\n", ret);
	} else {
		if (!data->pctrl_data->pins_sleep) {
			dev_dbg(&dev, "Pinstate 'sleep' is not defined\n");
		} else {
			ret = pinctrl_select_state(data->pctrl_data->pctrl,
					data->pctrl_data->pins_sleep);
			if (ret)
				dev_info(&dev, "Select sleep pinstate err:%d\n",
					ret);
		}
	}
	dev_dbg(&dev, "Select pinctrl state=%d\n", active);
}

static int bma2x2_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int err = 0;
	struct bma2x2_data *data;
	struct input_dev *dev;
	struct bst_dev  *dev_acc;
	struct bma2x2_platform_data *pdata;
	struct input_dev *dev_interrupt;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "i2c_check_functionality error\n");
		err = -EPERM;
		goto exit;
	}
	data = kzalloc(sizeof(struct bma2x2_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}
	memset(data, 0, sizeof(*data));

	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
			sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			err = -ENOMEM;
			goto kfree_exit;
		}
		err = bma2x2_parse_dt(&client->dev, pdata);
		if (err) {
			dev_err(&client->dev, "Failed to parse device tree\n");
			err = -EINVAL;
			goto pdata_free_exit;
		}
	} else {
		pdata = client->dev.platform_data;
		dev_err(&client->dev, "Use platform data\n");
	}

	if (!pdata) {
		dev_err(&client->dev, "Cannot get device platform data\n");
		err = -EINVAL;
		goto kfree_exit;
	}
	data->pdata = pdata;
	i2c_set_clientdata(client, data);
	data->bma2x2_client = client;

	err = bma2x2_power_init(data);
	if (err) {
		dev_err(&client->dev, "Failed to get sensor regulators\n");
		err = -EINVAL;
		goto free_i2c_clientdata_exit;
	}
	err = bma2x2_power_ctl(data, true);
	if (err) {
		dev_err(&client->dev, "Failed to enable sensor power\n");
		err = -EINVAL;
		goto deinit_power_exit;
	}

	RESET_DELAY();
	if (bma2x2_soft_reset(client) < 0) {
		dev_err(&client->dev,
			"i2c bus write error, pls check HW connection\n");
		err = -EINVAL;
		goto disable_power_exit;
	}
	RESET_DELAY();
	/* read and check chip id */
	if (bma2x2_check_chip_id(client, data) < 0) {
		err = -EINVAL;
		goto disable_power_exit;
	}
	mutex_init(&data->value_mutex);
	mutex_init(&data->mode_mutex);
	mutex_init(&data->enable_mutex);
	mutex_init(&data->op_lock);
	data->bandwidth = BMA2X2_BW_SET;
	data->range = BMA2X2_RANGE_SET;
	data->sensitivity = bosch_sensor_range_map[0];
	atomic_set(&data->cal_status, 0);
	data->fifo_buf = NULL;
	err = bma2x2_open_init(client, data);
	if (err < 0) {
		err = -EINVAL;
		goto disable_power_exit;
	}

	hq_regiser_hw_info(HWID_GSENSOR,"G-sensor:boshi bma253");

	if (pdata->int_en) {
		/* check interrupt feature enable state */
		err = bma2x2_pinctrl_init(data);
		if (err) {
			dev_err(&client->dev,
				"Failed to init pinctrl err=%d\n", err);
			err = -EINVAL;
			goto disable_power_exit;
		}

		if ((pdata->use_int2 && (!BMA2x2_IS_INT2_ENABLED())) ||
			(!pdata->use_int2 && (!BMA2x2_IS_INT1_ENABLED()))) {
			dev_err(&client->dev,
				"Interrupt support is not enabled, int1=%d, int2=%d use_int2=%d\n",
				BMA2x2_IS_INT1_ENABLED(),
				BMA2x2_IS_INT2_ENABLED(),
				pdata->use_int2);
			err = -EINVAL;
			goto disable_power_exit;
		}

		if (pdata->use_int2) {
			data->int_flag = pdata->int2_flag;
			err = bma2x2_get_interrupt_gpio(data,
					pdata->gpio_int2);
		} else {
			data->int_flag = pdata->int1_flag;
			err = bma2x2_get_interrupt_gpio(data,
					pdata->gpio_int1);
		}
		if (err) {
			dev_err(&client->dev,
				"Failed to get interrupt gpio, err=%d\n",
				err);
			err = -EINVAL;
			goto set_pinctrl_sleep;
		}

		data->IRQ = client->irq;
		if (!data->int_flag)
			data->int_flag = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;

		dev_dbg(&client->dev, "IRQ=%d, use_int2=%d, int_flag=0x%x\n",
			data->IRQ, pdata->use_int2, data->int_flag);
		err = request_irq(data->IRQ, bma2x2_irq_handler,
			data->int_flag, "bma2x2", data);
		if (err) {
			dev_err(&client->dev,  "Could not request irq\n");
			goto free_interrupt_gpio;
		}
		disable_irq(data->IRQ);
		device_init_wakeup(&client->dev, 1);

		INIT_WORK(&data->irq_work, bma2x2_irq_work_func);
		data->fifo_buf = devm_kmalloc(&client->dev,
			(MAX_FIFO_F_LEVEL * MAX_FIFO_F_BYTES), GFP_KERNEL);
	}

	if (!pdata->use_hrtimer) {
		INIT_DELAYED_WORK(&data->work, bma2x2_work_func);

		if (!pdata->int_en || !BMA2x2_IS_NEWDATA_INT_ENABLED())
			INIT_DELAYED_WORK(&data->work, bma2x2_work_func);

		data->data_wq = create_freezable_workqueue("bma2x2_data_work");
		if (!data->data_wq) {
			dev_err(&client->dev, "Cannot get create workqueue!\n");
			goto free_irq_exit;
		}
	} else {
		hrtimer_init(&data->accel_timer,
				CLOCK_BOOTTIME, HRTIMER_MODE_REL);
		data->accel_timer.function = accel_timer_handle;

		init_waitqueue_head(&data->accel_wq);
		data->accel_wkp_flag = 0;
		data->accel_task = kthread_run(accel_poll_thread, data,
				"bma_accel");
	}

	atomic_set(&data->delay, POLL_DEFAULT_INTERVAL_MS);
	atomic_set(&data->enable, 0);

	dev = devm_input_allocate_device(&client->dev);
	if (!dev) {
		dev_err(&client->dev,
			"Cannot allocate input device\n");
		err = -ENOMEM;
		goto destroy_workqueue_exit;
	}

	dev_interrupt = devm_input_allocate_device(&client->dev);
	if (!dev_interrupt) {
		dev_err(&client->dev,
			"Cannot allocate input interrupt device\n");
		err = -ENOMEM;
		goto destroy_workqueue_exit;
	}

	/* only value events reported */
	dev->name = SENSOR_NAME;
	dev->id.bustype = BUS_I2C;
	input_set_capability(dev, EV_ABS, ABS_MISC);
	input_set_abs_params(dev, ABS_X, ABSMIN, ABSMAX, 0, 0);
	input_set_abs_params(dev, ABS_Y, ABSMIN, ABSMAX, 0, 0);
	input_set_abs_params(dev, ABS_Z, ABSMIN, ABSMAX, 0, 0);

	input_set_drvdata(dev, data);
	err = input_register_device(dev);
	if (err < 0) {
		dev_err(&client->dev,
			"Cannot register input device\n");
		goto free_irq_exit;
	}

	/* all interrupt generated events are moved to interrupt input devices*/
	dev_interrupt->name = "bma_interrupt";
	dev_interrupt->id.bustype = BUS_I2C;
	input_set_capability(dev_interrupt, EV_REL,
		SLOW_NO_MOTION_INTERRUPT);
	input_set_capability(dev_interrupt, EV_REL,
		LOW_G_INTERRUPT);
	input_set_capability(dev_interrupt, EV_REL,
		HIGH_G_INTERRUPT);
	input_set_capability(dev_interrupt, EV_REL,
		SLOP_INTERRUPT);
	input_set_capability(dev_interrupt, EV_REL,
		DOUBLE_TAP_INTERRUPT);
	input_set_capability(dev_interrupt, EV_REL,
		SINGLE_TAP_INTERRUPT);
	input_set_capability(dev_interrupt, EV_ABS,
		ORIENT_INTERRUPT);
	input_set_capability(dev_interrupt, EV_ABS,
		FLAT_INTERRUPT);
	input_set_drvdata(dev_interrupt, data);

	err = input_register_device(dev_interrupt);
	if (err < 0) {
		dev_err(&client->dev,
			"Cannot register input interrupt device\n");
		goto free_irq_exit;
	}

	data->dev_interrupt = dev_interrupt;
	data->input = dev;

#ifdef CONFIG_SIG_MOTION
	data->g_sensor_class = class_create(THIS_MODULE, "sig_sensor");
	if (IS_ERR(data->g_sensor_class)) {
		err = PTR_ERR(data->g_sensor_class);
		data->g_sensor_class = NULL;
		dev_err(&client->dev, "could not allocate g_sensor_class\n");
		goto free_irq_exit;
	}

	data->g_sensor_dev = device_create(data->g_sensor_class,
				NULL, 0, "%s", "g_sensor");
	if (unlikely(IS_ERR(data->g_sensor_dev))) {
		err = PTR_ERR(data->g_sensor_dev);
		data->g_sensor_dev = NULL;

		dev_err(&client->dev, "could not allocate g_sensor_dev\n");
		goto destroy_g_sensor_class_exit;
	}

	dev_set_drvdata(data->g_sensor_dev, data);

	err = sysfs_create_group(&data->g_sensor_dev->kobj,
			&bma2x2_sig_motion_attribute_group);
	if (err < 0) {
		dev_err(&client->dev,
			"could not create sysfs for sig motion sensor\n");
		goto free_g_sensor_dev_exit;
	}
#endif

#ifdef CONFIG_DOUBLE_TAP
	data->g_sensor_class_doubletap =
		class_create(THIS_MODULE, "dtap_sensor");
	if (IS_ERR(data->g_sensor_class_doubletap)) {
		err = PTR_ERR(data->g_sensor_class_doubletap);
		data->g_sensor_class_doubletap = NULL;
		dev_err(&client->dev, "could not allocate g_sensor_class_doubletap\n");
		goto remove_sig_motion_sysfs_exit;
	}

	data->g_sensor_dev_doubletap = device_create(
				data->g_sensor_class_doubletap,
				NULL, 0, "%s", "g_sensor");
	if (unlikely(IS_ERR(data->g_sensor_dev_doubletap))) {
		err = PTR_ERR(data->g_sensor_dev_doubletap);
		data->g_sensor_dev_doubletap = NULL;

		dev_err(&client->dev, "could not allocate g_sensor_dev_doubletap\n");
		goto destroy_dtap_class_exit;
	}

	dev_set_drvdata(data->g_sensor_dev_doubletap, data);

	err = sysfs_create_group(&data->g_sensor_dev_doubletap->kobj,
			&bma2x2_double_tap_attribute_group);
	if (err < 0) {
		dev_err(&client->dev,
			"could not create sysfs for double tap sensor\n");
		goto destroy_dtap_dev_exit;
	}
#endif

	err = sysfs_create_group(&data->input->dev.kobj,
			&bma2x2_attribute_group);
	if (err < 0) {
		dev_err(&client->dev,
			"Cannot create sysfs for bma2x2\n");
		goto remove_dtap_sysfs_exit;
	}

	dev_acc = bst_allocate_device();
	if (!dev_acc) {
		dev_err(&client->dev,
			"Cannot allocate bst device\n");
		err = -ENOMEM;
		goto remove_bma2x2_sysfs_exit;
	}
	dev_acc->name = ACC_NAME;

	bst_set_drvdata(dev_acc, data);

	err = bst_register_device(dev_acc);
	if (err < 0) {
		dev_err(&client->dev,
			"Cannot register bst device\n");
		goto bst_free_acc_exit;
	}

	data->bst_acc = dev_acc;
	err = sysfs_create_group(&data->bst_acc->dev.kobj,
			&bma2x2_attribute_group);

	if (err < 0) {
		dev_err(&client->dev,
			"Cannot create sysfs for bst_acc.\n");
		goto bst_free_exit;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	data->early_suspend.suspend = bma2x2_early_suspend;
	data->early_suspend.resume = bma2x2_late_resume;
	register_early_suspend(&data->early_suspend);
#endif

	data->ref_count = 0;
	data->fifo_datasel = 0;
	data->fifo_count = 0;
#ifdef CONFIG_SIG_MOTION
	atomic_set(&data->en_sig_motion, 0);
#endif
#ifdef CONFIG_DOUBLE_TAP
	atomic_set(&data->en_double_tap, 0);
	data->tap_times = 0;
	data->tap_time_period = DEFAULT_TAP_JUDGE_PERIOD;
	mutex_init(&data->tap_mutex);
	setup_timer(&data->tap_timer, bma2x2_tap_timeout_handle,
			(unsigned long)data);
#endif

	data->cdev = sensors_cdev;
	data->cdev.min_delay = POLL_INTERVAL_MIN_MS * 1000;
	data->cdev.delay_msec = pdata->poll_interval;
	data->cdev.sensors_enable = bma2x2_cdev_enable;
	data->cdev.sensors_poll_delay = bma2x2_cdev_poll_delay;
	data->cdev.sensors_calibrate = bma2x2_self_calibration_xyz;
	data->cdev.sensors_write_cal_params = bma2x2_write_cal_params;
	data->cdev.resolution = sensor_type_map[data->chip_type].resolution;
	if (pdata->int_en) {
		if (BMA2x2_IS_NEWDATA_INT_ENABLED())
			data->cdev.max_delay = BMA_INT_MAX_DELAY;
		data->cdev.sensors_set_latency = bma2x2_cdev_set_latency;
		data->cdev.sensors_flush = bma2x2_cdev_flush;
		data->cdev.fifo_max_event_count = MAX_FIFO_F_LEVEL;
		data->cdev.fifo_reserved_event_count = MAX_FIFO_F_LEVEL;
	}
	err = sensors_classdev_register(&data->input->dev, &data->cdev);
	if (err) {
		dev_err(&client->dev, "Create class device file failed!\n");
		err = -EINVAL;
		goto remove_bst_acc_sysfs_exit;
	}

	if (pdata->use_smd) {
		err = bma2x2_register_smd(data, true);
		if (err)
			dev_err(&client->dev, "Register SMD device failed!\n");
	}

	dev_notice(&client->dev, "BMA2x2 driver probe successfully");

	bma2x2_pinctrl_state(data, false);
	bma2x2_power_ctl(data, false);
	return 0;

remove_bst_acc_sysfs_exit:
	sysfs_remove_group(&data->bst_acc->dev.kobj,
			&bma2x2_attribute_group);
bst_free_exit:
	bst_unregister_device(dev_acc);

bst_free_acc_exit:
	bst_free_device(dev_acc);

remove_bma2x2_sysfs_exit:
	sysfs_remove_group(&data->input->dev.kobj,
			&bma2x2_attribute_group);
remove_dtap_sysfs_exit:
#ifdef CONFIG_DOUBLE_TAP
sysfs_remove_group(&data->g_sensor_dev_doubletap->kobj,
			&bma2x2_double_tap_attribute_group);
destroy_dtap_dev_exit:
	device_destroy(data->g_sensor_dev_doubletap);
destroy_dtap_class_exit:
	class_destroy(data->g_sensor_class_doubletap);
remove_sig_motion_sysfs_exit:
#endif

#ifdef CONFIG_SIG_MOTION
sysfs_remove_group(&data->g_sensor_dev->kobj,
		&bma2x2_sig_motion_attribute_group);
free_g_sensor_dev_exit:
	device_destroy(data->g_sensor_class, 0);
destroy_g_sensor_class_exit:
	class_destroy(data->g_sensor_class);
#endif

destroy_workqueue_exit:
	if (!pdata->use_hrtimer) {
		destroy_workqueue(data->data_wq);
	} else {
		hrtimer_cancel(&data->accel_timer);
		kthread_stop(data->accel_task);
	}
free_irq_exit:
free_interrupt_gpio:
	if (pdata->int_en) {
		if (pdata->use_int2)
			gpio_free(pdata->gpio_int2);
		else
			gpio_free(pdata->gpio_int1);
	}
set_pinctrl_sleep:
	if (pdata->int_en)
		bma2x2_pinctrl_state(data, false);
disable_power_exit:
	bma2x2_power_ctl(data, false);
deinit_power_exit:
	bma2x2_power_deinit(data);
free_i2c_clientdata_exit:
	i2c_set_clientdata(client, NULL);
pdata_free_exit:
	if (pdata && (client->dev.of_node))
		devm_kfree(&client->dev, pdata);
	data->pdata = NULL;
kfree_exit:
	kfree(data);
exit:
	return err;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void bma2x2_early_suspend(struct early_suspend *h)
{
	struct bma2x2_data *data =
		container_of(h, struct bma2x2_data, early_suspend);

	mutex_lock(&data->enable_mutex);
	if (atomic_read(&data->enable) == 1) {
		bma2x2_set_mode(data->bma2x2_client, BMA2X2_MODE_SUSPEND);
		if (!data->pdata->int_en) {
			if (!data->pdata->use_hrtimer)
				cancel_delayed_work_sync(&data->work);
			else
				hrtimer_cancel(&data->accel_timer);
		}
	}
	mutex_unlock(&data->enable_mutex);
}

static void bma2x2_late_resume(struct early_suspend *h)
{
	struct bma2x2_data *data =
		container_of(h, struct bma2x2_data, early_suspend);
	int res = 0;

	mutex_lock(&data->enable_mutex);
	if (atomic_read(&data->enable) == 1) {
		bma2x2_set_mode(data->bma2x2_client, BMA2X2_MODE_NORMAL);
		if (!data->pdata->int_en) {
			if (!data->pdata->use_hrtimer) {
				res = queue_delayed_work(data->data_wq,
							&data->work,
							msecs_to_jiffies(atomic_read(&data->delay)));
				if(res == 0) {
					queue_delayed_work(data->data_wq,
							&data->work,
							msecs_to_jiffies(atomic_read(&data->delay)));
				}
			} else {
				ktime = ktime_set(0,
				atomic_read(&bma2x2->delay) * NSEC_PER_MSEC);
				hrtimer_start(&bma2x2->accle_timer,
						ktime, HRTIMER_MODE_REL);
			}
		}
	}
	mutex_unlock(&data->enable_mutex);
}
#endif

static int bma2x2_remove(struct i2c_client *client)
{
	struct bma2x2_data *data = i2c_get_clientdata(client);

	sensors_classdev_unregister(&data->cdev);
	if (data->pdata && data->pdata->use_smd)
		bma2x2_register_smd(data, false);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&data->early_suspend);
#endif

	if (data->bst_acc) {
		bst_unregister_device(data->bst_acc);
		bst_free_device(data->bst_acc);
	}

	bma2x2_double_tap_disable(data);

	bma2x2_sig_motion_disable(data);

	if (data->input)
		sysfs_remove_group(&data->input->dev.kobj,
				&bma2x2_attribute_group);

	bma2x2_set_enable(&client->dev, 0);
	if (data->pdata && !data->pdata->use_hrtimer) {
		destroy_workqueue(data->data_wq);
	} else {
		hrtimer_cancel(&data->accel_timer);
		kthread_stop(data->accel_task);
	}
	bma2x2_power_deinit(data);
	i2c_set_clientdata(client, NULL);
	if (data->pdata && (client->dev.of_node))
		devm_kfree(&client->dev, data->pdata);
	data->pdata = NULL;

	kfree(data);

	return 0;
}

static void bma2x2_shutdown(struct i2c_client *client)
{
	struct bma2x2_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->enable_mutex);
	bma2x2_set_mode(data->bma2x2_client, BMA2X2_MODE_DEEP_SUSPEND);
	mutex_unlock(&data->enable_mutex);
}

static int bma2x2_store_state(struct i2c_client *client,
				struct bma2x2_data *data)
{
	int err;

	err = bma2x2_get_bandwidth(client, &(data->bandwidth));
	if (err < 0) {
		dev_err(&client->dev, "get state bandwidth failed\n");
		return err;
	}
	err = bma2x2_get_range(client, &(data->range));
	if (err < 0) {
		dev_err(&client->dev, "get state range failed\n");
		return err;
	}
	return err;
}

#ifdef CONFIG_PM
static int bma2x2_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct bma2x2_data *data = i2c_get_clientdata(client);

	data->suspend_state.powerEn = bma2x2_is_power_enabled(data);
	bma2x2_set_enable(&client->dev, 0);
	return 0;
}

static int bma2x2_resume(struct i2c_client *client)
{
	struct bma2x2_data *data = i2c_get_clientdata(client);

	if (data->suspend_state.powerEn)
		bma2x2_set_enable(&client->dev, 1);

	return 0;
}

#else

#define bma2x2_suspend      NULL
#define bma2x2_resume       NULL

#endif /* CONFIG_PM */

static const struct i2c_device_id bma2x2_id[] = {
	{ SENSOR_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, bma2x2_id);

static const struct of_device_id bma2x2_of_match[] = {
	{ .compatible = "bosch,bma2x2", },
	{ },
};

static struct i2c_driver bma2x2_driver = {
	.driver = {
		.owner  = THIS_MODULE,
		.name   = SENSOR_NAME,
		.of_match_table = bma2x2_of_match,
	},
	.suspend    = bma2x2_suspend,
	.resume     = bma2x2_resume,
	.id_table   = bma2x2_id,
	.probe      = bma2x2_probe,
	.remove     = bma2x2_remove,
	.shutdown   = bma2x2_shutdown,
};

static int __init BMA2X2_init(void)
{
	return i2c_add_driver(&bma2x2_driver);
}

static void __exit BMA2X2_exit(void)
{
	i2c_del_driver(&bma2x2_driver);
}

MODULE_AUTHOR("contact@bosch-sensortec.com");
MODULE_DESCRIPTION("BMA2X2 ACCELEROMETER SENSOR DRIVER");
MODULE_LICENSE("GPL v2");

module_init(BMA2X2_init);
module_exit(BMA2X2_exit);

