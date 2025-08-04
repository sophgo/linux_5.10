/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __CVITEK_BOOT_MODE_H
#define __CVITEK_BOOT_MODE_H

// RTC register
// if change this register address, be careful
// this Header file used by fsbl & u-boot & kernel & (build dts)
#define BOOT_MODE_REGISTER 0x05026ff0

/*high 24 bits is tag, low 8 bits is type*/
#define REBOOT_FLAG        0x5242C300

/* normal boot */
#define BOOT_NORMAL        (REBOOT_FLAG + 0)
/* enter bootloader rockusb mode */
#define BOOT_BL_DOWNLOAD    (REBOOT_FLAG + 1)
/* enter recovery */
#define BOOT_RECOVERY        (REBOOT_FLAG + 3)
 /* enter fastboot mode */
#define BOOT_FASTBOOT        (REBOOT_FLAG + 5)

#define BOOT_ROOTFSA        (REBOOT_FLAG + 7)

#define BOOT_ROOTFSB        (REBOOT_FLAG + 9)

#define BOOT_CHARGING       (REBOOT_FLAG + 11)

#endif
