/** @file
 *
 *  MSM8996 SPMI PMIC arbiter v2 implementation (polling, no interrupts).
 *
 *  Register semantics follow Linux v4.9 drivers/spmi/spmi-pmic-arb.c
 *  (SPMI v2): channels are per-PPID, found in the CORE mapping table;
 *  rd transactions use the observer space, wr transactions the channels
 *  space, both windowed by 0x1000 * ee + 0x8000 * chan.
 *
 *  Copyright (c) 2026, UniXNU contributors.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/IoLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>

#include <Library/Msm8996SpmiLib.h>
#include <Msm8996Regs.h>

//
// The arbiter's DTB node caps peripherals at 256 (qcom,pmic-arb-max-periph-
// erals = <0x100>), but the v2 channel table is scanned until an entry is
// zero; keep the Linux bound as a safety limit.
//
#define SPMI_MAX_CHAN            SPMI_MAX_PERIPHS
#define SPMI_TIMEOUT_POLLS       1000   // ~1 ms at the 1 us loop

/**
  Find the channel assigned to a PPID by scanning the CORE mapping table.

  @param  Ppid    Slave id << 8 | peripheral page.
  @param  Chan    Output channel number.

  @retval EFI_SUCCESS      Channel found.
  @retval EFI_NOT_FOUND    No channel in the table for this PPID.
**/
STATIC
EFI_STATUS
SpmiFindChannel (
  IN  UINT16  Ppid,
  OUT UINT16  *Chan
  )
{
  UINT16  Ch;

  for (Ch = 0; Ch < SPMI_MAX_CHAN; Ch++) {
    UINT32 Entry = MmioRead32 (MSM8996_SPMI_CORE_BASE + SPMI_REG_CHNL (Ch));

    if (Entry == 0) {
      continue;
    }

    if (((Entry >> 8) & SPMI_PPID_MASK) == Ppid) {
      *Chan = Ch;
      return EFI_SUCCESS;
    }
  }

  return EFI_NOT_FOUND;
}

/**
  Format and issue a command, then wait for completion.

  @param  RdBase   Base to use for the transaction (observer for reads).
  @param  WrBase   Base to use for writes (channels space).
  @param  Chan     Channel number.
  @param  Opc      SPMI operation code.
  @param  Addr     Register address (low byte goes into the command).
  @param  Bc       Byte count - 1.
  @param  IsRead   TRUE for read transactions.

  @retval EFI_SUCCESS      Done, no error bits.
**/
STATIC
EFI_STATUS
SpmiTransact (
  IN UINTN   RdBase,
  IN UINTN   WrBase,
  IN UINT16  Chan,
  IN UINT32  Opc,
  IN UINT16  Addr,
  IN UINT8   Bc,
  IN BOOLEAN IsRead
  )
{
  UINTN  Off = (UINTN)Chan * SPMI_V2_CHAN_STRIDE;
  UINTN  CmdBase = IsRead ? (RdBase + Off) : (WrBase + Off);
  UINT32 Cmd;
  UINTN  Poll;

  Cmd = (Opc << 27) | ((Addr & 0xFF) << 4) | (Bc & 0x7);

  if (IsRead) {
    MmioWrite32 (CmdBase + SPMI_CH_CMD, Cmd);
  } else {
    MmioWrite32 (CmdBase + SPMI_CH_CMD, Cmd);
  }

  for (Poll = 0; Poll < SPMI_TIMEOUT_POLLS; Poll++) {
    UINT32 Status = MmioRead32 (CmdBase + SPMI_CH_STATUS);

    if ((Status & SPMI_STATUS_DONE) != 0) {
      if ((Status & (SPMI_STATUS_FAILURE | SPMI_STATUS_DENIED |
                     SPMI_STATUS_DROPPED)) != 0) {
        DEBUG ((
          DEBUG_ERROR,
          "%a: SPMI chan %u status 0x%x\n",
          gEfiCallerBaseName,
          Chan,
          Status
          ));
        return EFI_DEVICE_ERROR;
      }

      return EFI_SUCCESS;
    }
  }

  return EFI_TIMEOUT;
}

/**
  Read up to 8 bytes from a PMIC peripheral register over SPMI.
**/
EFI_STATUS
EFIAPI
Msm8996SpmiRead (
  IN  UINT8   Sid,
  IN  UINT16  Addr,
  OUT UINT8   *Buffer,
  IN  UINT8   Length
  )
{
  EFI_STATUS  Status;
  UINT16      Chan;
  UINT16      Ppid;
  UINTN       Off;
  UINT8       Bc;
  UINT32      Data0;
  UINT32      Data1;

  if ((Buffer == NULL) || (Length == 0) || (Length > 8)) {
    return EFI_INVALID_PARAMETER;
  }

  Ppid = (UINT16)(((Sid & 0xF) << 8) | (Addr >> 8));
  Status = SpmiFindChannel (Ppid, &Chan);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Off  = (UINTN)Chan * SPMI_V2_CHAN_STRIDE;
  Bc   = Length - 1;

  Status = SpmiTransact (
             MSM8996_SPMI_OBSRVR_BASE,
             MSM8996_SPMI_CHNLS_BASE,
             Chan,
             SPMI_OP_READ,
             Addr,
             Bc,
             TRUE
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Data0 = MmioRead32 (MSM8996_SPMI_OBSRVR_BASE + Off + SPMI_CH_RDATA0);
  CopyMem (Buffer, &Data0, (Bc < 4) ? Length : 4);

  if (Bc > 3) {
    Data1 = MmioRead32 (MSM8996_SPMI_OBSRVR_BASE + Off + SPMI_CH_RDATA1);
    CopyMem (Buffer + 4, &Data1, Length - 4);
  }

  return EFI_SUCCESS;
}

/**
  Write up to 8 bytes to a PMIC peripheral register over SPMI.
**/
EFI_STATUS
EFIAPI
Msm8996SpmiWrite (
  IN UINT8        Sid,
  IN UINT16       Addr,
  IN CONST UINT8  *Buffer,
  IN UINT8        Length
  )
{
  EFI_STATUS  Status;
  UINT16      Chan;
  UINT16      Ppid;
  UINTN       Off;
  UINT8       Bc;
  UINT32      Data0 = 0;
  UINT32      Data1 = 0;

  if ((Buffer == NULL) || (Length == 0) || (Length > 8)) {
    return EFI_INVALID_PARAMETER;
  }

  Ppid = (UINT16)(((Sid & 0xF) << 8) | (Addr >> 8));
  Status = SpmiFindChannel (Ppid, &Chan);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Off  = (UINTN)Chan * SPMI_V2_CHAN_STRIDE;
  Bc   = Length - 1;

  CopyMem (&Data0, Buffer, (Bc < 4) ? Length : 4);
  MmioWrite32 (MSM8996_SPMI_CHNLS_BASE + Off + SPMI_CH_WDATA0, Data0);

  if (Bc > 3) {
    CopyMem (&Data1, Buffer + 4, Length - 4);
    MmioWrite32 (MSM8996_SPMI_CHNLS_BASE + Off + SPMI_CH_WDATA1, Data1);
  }

  return SpmiTransact (
           MSM8996_SPMI_OBSRVR_BASE,
           MSM8996_SPMI_CHNLS_BASE,
           Chan,
           SPMI_OP_WRITE,
           Addr,
           Bc,
           FALSE
           );
}

/**
  Read-modify-write a single register byte.
**/
EFI_STATUS
EFIAPI
Msm8996SpmiUpdateBits (
  IN UINT8   Sid,
  IN UINT16  Addr,
  IN UINT8   Mask,
  IN UINT8   Value
  )
{
  EFI_STATUS  Status;
  UINT8       Reg;

  Status = Msm8996SpmiRead (Sid, Addr, &Reg, 1);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Reg = (UINT8)((Reg & ~Mask) | (Value & Mask));

  return Msm8996SpmiWrite (Sid, Addr, &Reg, 1);
}
