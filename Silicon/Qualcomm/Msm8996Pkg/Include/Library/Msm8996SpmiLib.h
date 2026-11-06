/** @file
 *
 *  MSM8996 SPMI PMIC arbiter v2 -- library interface.
 *
 *  The g0245d DTB declares the arbiter at qcom,spmi@400f000 with ee = 0
 *  (qcom,pmic-arb-ee = <0x00>). This library implements the minimal
 *  synchronous read/write path of the v2 protocol as documented in Linux
 *  v4.9 drivers/spmi/spmi-pmic-arb.c:
 *
 *    offset  = 0x1000 * ee + 0x8000 * chan
 *    chan    = looked up in the CORE PPID->channel table
 *              (CORE + 0x800 + 4*chan, entry >> 8 & 0xFFF == PPID)
 *    cmd     = (opc << 27) | ((addr & 0xff) << 4) | (bc & 7)
 *    write:  WDATA0/WDATA1 then CMD, poll STATUS bit0 (DONE)
 *    read:   CMD, poll STATUS, then RDATA0/RDATA1
 *
 *  Copyright (c) 2026, UniXNU contributors.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __MSM8996_SPMI_LIB_H__
#define __MSM8996_SPMI_LIB_H__

#include <Uefi.h>

/**
  Read up to 8 bytes from a PMIC peripheral register over SPMI.

  @param  Sid       Slave id (0 = PM8994, 1 = PMI8994).
  @param  Addr      16-bit peripheral address (periph base + register).
  @param  Buffer    Output buffer.
  @param  Length    Number of bytes (1..8).

  @retval EFI_SUCCESS           Transaction completed.
  @retval EFI_DEVICE_ERROR      Arbitrer reported failure/denied/dropped.
  @retval EFI_TIMEOUT           Transaction did not complete in time.
  @retval EFI_NOT_FOUND         No channel assigned to this PPID.
**/
EFI_STATUS
EFIAPI
Msm8996SpmiRead (
  IN  UINT8   Sid,
  IN  UINT16  Addr,
  OUT UINT8   *Buffer,
  IN  UINT8   Length
  );

/**
  Write up to 8 bytes to a PMIC peripheral register over SPMI.

  @param  Sid       Slave id.
  @param  Addr      16-bit peripheral address (periph base + register).
  @param  Buffer    Data to write.
  @param  Length    Number of bytes (1..8).

  @retval EFI_SUCCESS           Transaction completed.
  @retval EFI_DEVICE_ERROR      Arbitrer reported failure/denied/dropped.
  @retval EFI_TIMEOUT           Transaction did not complete in time.
  @retval EFI_NOT_FOUND         No channel assigned to this PPID.
**/
EFI_STATUS
EFIAPI
Msm8996SpmiWrite (
  IN UINT8        Sid,
  IN UINT16       Addr,
  IN CONST UINT8  *Buffer,
  IN UINT8        Length
  );

/**
  Read-modify-write a single register byte.

  @param  Sid       Slave id.
  @param  Addr      16-bit peripheral address.
  @param  Mask      Bits to modify.
  @param  Value     New bit values.

  @retval EFI_SUCCESS           Transaction completed.
**/
EFI_STATUS
EFIAPI
Msm8996SpmiUpdateBits (
  IN UINT8   Sid,
  IN UINT16  Addr,
  IN UINT8   Mask,
  IN UINT8   Value
  );

#endif /* __MSM8996_SPMI_LIB_H__ */
