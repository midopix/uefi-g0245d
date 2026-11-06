/** @file
 *
 *  MSM8996 SerialPortLib implementation.
 *
 *  This library provides the very first output path used during SEC/PEI, long
 *  before any driver is dispatched. It therefore has to be self-contained: no
 *  allocations, no protocol dependencies, no interrupt routing -- just a
 *  polling read/write against the BLSP2_UART2 register window.
 *
 *  HARDWARE FACTS (verified from this device's own device tree)
 *  ------------------------------------------------------------
 *    serial@075b0000                  <- base used here, status = "ok"
 *      compatible = "qcom,msm-lsuart-v14"
 *      reg        = <0x075b0000 0x1000>
 *      interrupts = <0 0x72 0>        <- SPI 114, unused by the polling path
 *    clocks      = <&clock_gcc ...>    <- core_clk + iface_clk, GCC-managed
 *
 *    The stock kernel cmdline is `console=ttyHSL0,115200,n8`, which independently
 *    corroborates 115200 baud on this UART.
 *
 *  Copyright (c) Microsoft Corporation.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/SerialPortLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseLib.h>   // CpuPause()

#include <Msm8996Regs.h>

/**
  Return the MMIO base of the debug UART.

  PcdUartBase is authoritative so a board can move the console without
  recompiling; the baked-in MSM8996_UART_BASE is only the documented default.
**/
STATIC
UINTN
SerialPortBase (
  VOID
  )
{
  return (UINTN)PcdGet32 (PcdUartBase);
}

//
// Bounded polling.  The console is a debug convenience, never a boot
// dependency: if any register access proves the UART is not answering
// (clock vote ignored, TX never ready, RX FIFO never drains despite the
// status register claiming data), we latch mUartUsable = FALSE and every
// later call returns immediately.  Without this a board with no serial
// cable hangs in the very first DEBUG() and the secure watchdog drops it
// into EDL -- which is exactly how this port died before.
//
#define UART_SPIN_LIMIT  200000

STATIC BOOLEAN  mUartUsable = TRUE;

/**
  Best-effort cold-start of the BLSP2_UART2 clock domain.

  Our UEFI replaces aboot in the boot chain (SBL1 jumps straight into the
  firmware volume), so unlike the original "inherit from aboot" assumption
  there may be no bootloader that has clocked the console UART. SBL1 usually
  brings the debug UART up for its own logging, so the safe strategy is:

    1. If BLSP2_UART2_APPS_CBCR shows the clock running, do nothing
       (hot hand-off path, registers keep SBL1's configuration).
    2. Otherwise: vote BLSP2_AHB/BLSP2_SLEEP through the GCC clock-vote
       register, program blsp2_uart2_apps_clk_src for 7.3728 MHz out of
       GPLL0 (M=192, N=15625 -- Linux ftbl_blsp1_uart1_apps_clk_src),
       enable the branch, and cold-configure the UARTDM for 115200 8N1.

  All of the register values here come from Linux v4.9 gcc-msm8996.c and
  v5.10 msm_serial.c, cross-checked against the DTB clock plan.
**/
STATIC
VOID
UartColdInit (
  VOID
  )
{
  UINTN  Base;
  UINT32 Cbcr;
  UINT32 Vote;

  Base = SerialPortBase ();
  Cbcr = MSM8996_GCC_BASE + GCC_BLSP2_UART2_APPS_CBCR;

  //
  // Hot path: branch enabled and un-halted -> SBL1 already did the work.
  //
  if (((MmioRead32 (Cbcr) & 0x1) != 0) &&
      ((MmioRead32 (Cbcr) & CBCR_CLK_OFF) == 0)) {
    return;
  }

  //
  // Cold path. 1) vote the AHB/Sleep interfaces on.
  //
  Vote = MmioRead32 (MSM8996_GCC_BASE + GCC_APCS_CLOCK_BRANCH_ENA_VOTE);
  Vote |= GCC_VOTE_BLSP2_AHB | GCC_VOTE_BLSP2_SLEEP;
  MmioWrite32 (MSM8996_GCC_BASE + GCC_APCS_CLOCK_BRANCH_ENA_VOTE, Vote);

  //
  // 2) RCG2: 7.3728 MHz = 600 MHz (GPLL0) * 192 / 15625.
  //    CFG: SRC=GPLL0 (0x2), pre-div=1 (field 1), dual-edge MND mode.
  //    M=0xC0, N=~(15625-192)&0xFFFF, D=~15625&0xFFFF.
  //
  {
    UINTN Cmd = MSM8996_GCC_BASE + GCC_BLSP2_UART2_APPS_CMD_RCGR;
    UINTN Poll;

    MmioWrite32 (Cmd + RCG2_M_OFFSET, 192);
    MmioWrite32 (Cmd + RCG2_N_OFFSET, (~(15625 - 192)) & 0xFFFF);
    MmioWrite32 (Cmd + RCG2_D_OFFSET, (~(15625)) & 0xFFFF);
    MmioWrite32 (Cmd + RCG2_CFG_OFFSET,
                 (RCG2_SRC_GPLL0 << 8) | 0x1 | (0x2 << 12));
    MmioWrite32 (Cmd, RCG2_CMD_UPDATE | RCG2_CMD_ROOT_EN);

    for (Poll = UART_SPIN_LIMIT; Poll > 0; Poll--) {
      if ((MmioRead32 (Cmd) & RCG2_CMD_UPDATE) == 0) {
        break;
      }
    }

    if (Poll == 0) {
      //
      // GCC never acknowledged the update: the clock vote did not land, so
      // the UART will never clock.  Give up on the console instead of
      // spinning here forever.
      //
      mUartUsable = FALSE;
      return;
    }
  }

  //
  // 3) Enable the branch and wait for the halt bit to clear.
  //
  MmioWrite32 (Cbcr, MmioRead32 (Cbcr) | 0x1);
  {
    UINTN Wait;

    for (Wait = UART_SPIN_LIMIT; Wait > 0; Wait--) {
      if ((MmioRead32 (Cbcr) & CBCR_CLK_OFF) == 0) {
        break;
      }
    }

    if (Wait == 0) {
      mUartUsable = FALSE;
      return;
    }
  }

  //
  // 4) Cold-configure the UARTDM: protection, resets, 115200 8N1.
  //    (Sequence from msm_serial.c msm_set_baud_rate/msm_reset.)
  //
  MmioWrite32 (Base + UARTDM_CR, UARTDM_CR_CMD_PROTECTION_EN);
  MmioWrite32 (Base + UARTDM_CR, UARTDM_CR_CMD_RESET_RX);
  MmioWrite32 (Base + UARTDM_CR, UARTDM_CR_CMD_RESET_TX);
  MmioWrite32 (Base + UARTDM_CR, UARTDM_CR_CMD_RESET_ERR);
  MmioWrite32 (Base + UARTDM_CR, UARTDM_CR_CMD_RESET_BREAK_INT);
  MmioWrite32 (Base + UARTDM_CR, UARTDM_CR_CMD_RESET_CTS);
  MmioWrite32 (Base + UARTDM_CR, UARTDM_CR_CMD_RESET_RFR);

  MmioWrite32 (Base + UARTDM_CSR, UARTDM_CSR_115200);
  MmioWrite32 (Base + UARTDM_MR2, UARTDM_MR2_8N1);
  MmioWrite32 (Base + UARTDM_IPR, UARTDM_IPR_RXSTALE_6);

  MmioWrite32 (Base + UARTDM_CR, UARTDM_CR_TX_ENABLE | UARTDM_CR_RX_ENABLE);
}

/**
  Initialise the serial device hardware.

  @return RETURN_SUCCESS  The serial device was initialised.
**/
RETURN_STATUS
EFIAPI
SerialPortInitialize (
  VOID
  )
{
  UINTN  Base;
  UINTN  Drain;

  Base = SerialPortBase ();

  UartColdInit ();

  if (!mUartUsable) {
    return RETURN_SUCCESS;
  }

  //
  // Drain anything the bootloader left in the RX FIFO so the first
  // DebugClearMemory / ASSERT output is not interleaved with aboot's log.
  //
  // Bounded: an unpowered UART block reads back 0xff for every register, so
  // "RX ready" stays asserted forever and an unbounded loop here would hang
  // before a single line of output.  64 reads is far more than any FIFO.
  //
  for (Drain = 64; Drain > 0; Drain--) {
    if ((MmioRead32 (Base + UARTDM_SR) & UARTDM_SR_RX_READY) == 0) {
      break;
    }

    (VOID)MmioRead32 (Base + UARTDM_RF);
  }

  if (Drain == 0) {
    mUartUsable = FALSE;
  }

  return RETURN_SUCCESS;
}

/**
  Write data from buffer to serial device.

  @param  Buffer           Point of data buffer which need to be written.
  @param  NumberOfBytes    Number of output bytes which are cached in Buffer.

  @retval 0                Write data failed.
  @retval >0               Actual number of bytes written to serial device.
**/
UINTN
EFIAPI
SerialPortWrite (
  IN UINT8  *Buffer,
  IN UINTN  NumberOfBytes
  )
{
  UINTN  Base;
  UINTN  Index;
  UINTN  Spin;

  if (!mUartUsable) {
    //
    // Console already written off: swallow the data, never block the boot.
    //
    return NumberOfBytes;
  }

  Base  = SerialPortBase ();
  Index = 0;

  while (Index < NumberOfBytes) {
    //
    // Spin until the TX FIFO has room. A hang here means either the base is
    // wrong or the UART clocks are gated -- the two classic bring-up faults.
    // Both are detected: after UART_SPIN_LIMIT tries the console is written
    // off for good and boot continues headless.
    //
    for (Spin = UART_SPIN_LIMIT; Spin > 0; Spin--) {
      if ((MmioRead32 (Base + UARTDM_SR) & UARTDM_SR_TX_READY) != 0) {
        break;
      }

      CpuPause ();
    }

    if (Spin == 0) {
      mUartUsable = FALSE;
      return Index;
    }

    MmioWrite32 (Base + UARTDM_TF, Buffer[Index]);
    Index++;
  }

  return NumberOfBytes;
}

/**
  Read data from serial device and save the data in buffer.

  @param  Buffer           Point of data buffer which need to be written.
  @param  NumberOfBytes    Size of Buffer.

  @retval 0                Read data failed.
  @retval >0               Actual number of bytes read from serial device.
**/
UINTN
EFIAPI
SerialPortRead (
  OUT UINT8 *Buffer,
  IN  UINTN NumberOfBytes
  )
{
  UINTN Base;
  UINTN Index;

  if (!mUartUsable) {
    return 0;
  }

  Base  = SerialPortBase ();
  Index = 0;

  while (Index < NumberOfBytes) {
    if ((MmioRead32 (Base + UARTDM_SR) & UARTDM_SR_RX_READY) == 0) {
      break;
    }

    Buffer[Index] = (UINT8)MmioRead32 (Base + UARTDM_RF);
    Index++;
  }

  return Index;
}

/**
  Polls a serial device to see if there is any data waiting to be read.

  @retval TRUE              Data is waiting to be read from the serial device.
  @retval FALSE             There is no data waiting to be read from the serial device.
**/
BOOLEAN
EFIAPI
SerialPortPoll (
  VOID
  )
{
  UINTN Base;

  if (!mUartUsable) {
    return FALSE;
  }

  Base = SerialPortBase ();

  return (MmioRead32 (Base + UARTDM_SR) & UARTDM_SR_RX_READY) != 0;
}

/**
  Sets the control bits on a serial device.

  @param  Control           Sets the bits of Control that are settable.

  @retval RETURN_SUCCESS    The new control bits were set on the serial device.
**/
RETURN_STATUS
EFIAPI
SerialPortSetControl (
  IN UINT32 Control
  )
{
  //
  // The polling console has no flow-control or loop-back capability, so any
  // caller-requested control change is a no-op. Returning success keeps
  // TerminalDxe happy rather than failing an otherwise-working console.
  //
  return RETURN_SUCCESS;
}

/**
  Retrieve the status of the control bits on a serial device.

  @param  Control           A pointer to return the current control signals.

  @retval RETURN_SUCCESS    The control bits were read from the serial device.
**/
RETURN_STATUS
EFIAPI
SerialPortGetControl (
  OUT UINT32 *Control
  )
{
  if (Control == NULL) {
    return RETURN_INVALID_PARAMETER;
  }

  //
  // Report the line as always ready: there is no carrier-detect or
  // clear-to-send wired to the BLSP pins used for the console.
  //
  *Control = EFI_SERIAL_DATA_SET_READY | EFI_SERIAL_CLEAR_TO_SEND;

  return RETURN_SUCCESS;
}

/**
  Sets the baud rate, receive FIFO depth, transmit/receive time out, parity,
  data bits, and stop bits on a serial device.

  @retval RETURN_SUCCESS    The new attributes were set on the serial device.
**/
RETURN_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64              *BaudRate,
  IN OUT UINT32              *ReceiveFifoDepth,
  IN OUT UINT32              *Timeout,
  IN OUT EFI_PARITY_TYPE     *Parity,
  IN OUT UINT8               *DataBits,
  IN OUT EFI_STOP_BITS_TYPE  *StopBits
  )
{
  //
  // The console inherits the baud rate programmed by aboot. Silently accept
  // the requested attributes and echo back the values we honour.
  //
  if (BaudRate != NULL) {
    *BaudRate = PcdGet64 (PcdUartDefaultBaudRate);
  }

  if (ReceiveFifoDepth != NULL) {
    *ReceiveFifoDepth = 0;
  }

  if (Timeout != NULL) {
    *Timeout = 0;
  }

  if (Parity != NULL) {
    *Parity = NoParity;
  }

  if (DataBits != NULL) {
    *DataBits = 8;
  }

  if (StopBits != NULL) {
    *StopBits = OneStopBit;
  }

  return RETURN_SUCCESS;
}
