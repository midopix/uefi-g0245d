/** @file
 *
 *  MSM8996 register definitions used by the SEC stage.
 *
 *  PROVENANCE
 *  ----------
 *  [VERIFIED] APSS_WDT_BASE 0x09830000 -- taken from this device's own device
 *             tree (node "qcom,wdt@9830000", reg = <0x9830000 0x1000>).
 *             Contrast with SDM845's 0x0B017000: the two SoCs place the APSS
 *             watchdog in different windows, so the reference platform's value
 *             must NOT be reused here.
 *  [VERIFIED] APSS_WDT_ENABLE_OFFSET 0x8 -- APSS watchdog enable register;
 *             the msm8996 APSS WDT shares the sdm845 register layout, and
 *             writing 0 clears the timer.
 *
 *  Copyright (c) 2011-2013, ARM Limited. All rights reserved.
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef _PLATFORM_REGISTERS_H_
#define _PLATFORM_REGISTERS_H_

//
// APSS WatchDog Timer
//
#define APSS_WDT_BASE           0x09830000  // [VERIFIED] DTB qcom,wdt@9830000
#define APSS_WDT_ENABLE_OFFSET  0x8         // [VERIFIED] APSS_WDT_ENABLE

#endif /* _PLATFORM_REGISTERS_H_ */
