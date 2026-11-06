/** @file
 *
 *  MSM8996 display driver -- Graphics Output protocol over the
 *  pre-initialised contiguous-splash framebuffer.
 *
 *  SCOPE AND HONEST LIMITS
 *  -----------------------
 *  This driver does NOT program the MDP5 display pipeline. Real panel
 *  bring-up (MDP5 + DSI controller + panel-specific init sequences from
 *  LK's gcdb display layer) is a separate project. What it does instead:
 *
 *    * consumes whatever pixels the previous boot stage left in
 *      cont_splash_mem, and
 *    * publishes a valid EFI_GRAPHICS_OUTPUT_PROTOCOL describing that
 *      window, so ConSplitter/GraphicsConsoleDxe and the UniXNU payload
 *      can enumerate a display without crashing.
 *
 *  On the SBL1 -> UEFI chain the MDP5 is COLD at entry (SBL1 does not
 *  touch the display, and this firmware replaces aboot/LK, which is the
 *  component that normally initialises it), so the console output will
 *  not be visible on the panel until the MDP5 driver lands. Serial
 *  output through BLSP2_UART2 remains the authoritative console.
 *
 *  HARDWARE FACTS (all from this device's own DTB)
 *  ------------------------------------------------
 *    cont_splash_mem@83401000 { reg = <0x0 0x83401000 0x0 0x23ff000> }
 *    panel: 1080 x 1920, 32 bpp (DSC/panel nodes), XRGB8888 layout.
 *    MDP5 (qcom,mdss_mdp@900000) and the DSI blocks are NOT touched here.
 *
 *  Copyright (c) 2026, UniXNU contributors.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
// v48: the framebuffer is cacheable memory; every write must be flushed
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/Msm8996DisplayLib.h>
#include <Protocol/GraphicsOutput.h>

#include <Msm8996Regs.h>

//
// This device's panel geometry, from the DTB panel nodes.
//
#define G0245D_FB_WIDTH         1080
#define G0245D_FB_HEIGHT        1920
#define G0245D_FB_BPP           32
#define G0245D_FB_PIXEL_SIZE    4
#define G0245D_FB_LINEBYTES     (G0245D_FB_WIDTH * G0245D_FB_PIXEL_SIZE)
#define G0245D_FB_VISIBLE_SIZE  (G0245D_FB_WIDTH * G0245D_FB_HEIGHT * G0245D_FB_PIXEL_SIZE)

//
// Pixel format: XRGB8888 -- the format the vendor display stack uses for
// its splash buffer on this generation (8 bpp X bits ignored by HW).
//
#define G0245D_FB_PIXEL_FORMAT  PixelRedGreenBlueReserved8BitPerColor

typedef struct {
  UINT32                            Signature;
  EFI_GRAPHICS_OUTPUT_PROTOCOL      GraphicsOutput;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info;
} MSM8996_DISPLAY_PRIVATE;

#define MSM8996_DISPLAY_SIGNATURE  SIGNATURE_32 ('M', '9', 'D', 'P')

/**
  Query the (single) mode.

  @retval EFI_SUCCESS          Mode data returned.
  @retval EFI_NOT_STARTED      The mode index is out of range.
**/
STATIC
EFI_STATUS
EFIAPI
DisplayQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL                  *This,
  IN  UINT32                                        ModeNumber,
  OUT UINTN                                         *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION          **Info
  )
{
  if ((SizeOfInfo == NULL) || (Info == NULL) || (ModeNumber != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  *SizeOfInfo = This->Mode->SizeOfInfo;
  *Info       = AllocateCopyPool (This->Mode->SizeOfInfo, This->Mode->Info);
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  return EFI_SUCCESS;
}

/**
  Select a mode. Only mode 0 (the splash window) exists; selecting it is a
  no-op because the scanout configuration belongs to the MDP5, which this
  driver deliberately does not touch.
**/
STATIC
EFI_STATUS
EFIAPI
DisplaySetMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  IN  UINT32                        ModeNumber
  )
{
  if (ModeNumber != 0) {
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

/**
  Blt: memory-to-screen, screen-to-memory and video-fill operations on the
  splash window.

  @retval EFI_SUCCESS          Operation completed.
  @retval EFI_INVALID_PARAMETER Bad parameters.
  @retval EFI_UNSUPPORTED       Operation not supported.
**/
STATIC
EFI_STATUS
EFIAPI
DisplayBlt (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL            *This,
  IN  OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL       *BltBuffer    OPTIONAL,
  IN  EFI_GRAPHICS_OUTPUT_BLT_OPERATION       BltOperation,
  IN  UINTN                                   SourceX,
  IN  UINTN                                   SourceY,
  IN  UINTN                                   DestinationX,
  IN  UINTN                                   DestinationY,
  IN  UINTN                                   Width,
  IN  UINTN                                   Height,
  IN  UINTN                                   Delta         OPTIONAL
  )
{
  EFI_TPL                          Tpl;
  UINT32                           *Fb;
  UINTN                            Index;
  UINTN                            Row;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL    *Src;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Fb = (UINT32 *) (UINTN) This->Mode->FrameBufferBase;

  //
  // The UEFI spec requires Blt to be serialised; take the lock like the
  // reference implementations do.
  //
  Tpl = gBS->RaiseTPL (TPL_NOTIFY);

  switch (BltOperation) {
    case EfiBltVideoFill:
      if (Delta == 0) {
        Delta = Width * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
      }

      for (Row = 0; Row < Height; Row++) {
        for (Index = 0; Index < Width; Index++) {
          Fb[(DestinationY + Row) * G0245D_FB_WIDTH + DestinationX + Index] =
            *(UINT32 *) BltBuffer;
        }
      }

      break;

    case EfiBltVideoToBltBuffer:
    case EfiBltBufferToVideo:
      if (Delta == 0) {
        Delta = Width * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
      }

      for (Row = 0; Row < Height; Row++) {
        if (BltOperation == EfiBltVideoToBltBuffer) {
          //
          // Screen -> memory. Source coordinates address the framebuffer,
          // DestinationX/DestinationY address the BLT buffer.
          //
          Src = (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)((UINT8 *)BltBuffer + (DestinationY + Row) * Delta) + DestinationX;
          CopyMem (
            Src,
            (VOID *)&Fb[(SourceY + Row) * G0245D_FB_WIDTH + SourceX],
            Width * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
            );
        } else {
          //
          // Memory -> screen. Source coordinates address the BLT buffer,
          // DestinationX/DestinationY address the framebuffer.
          //
          Src = (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)((UINT8 *)BltBuffer + (SourceY + Row) * Delta) + SourceX;
          for (Index = 0; Index < Width; Index++) {
            Fb[(DestinationY + Row) * G0245D_FB_WIDTH + DestinationX + Index] = *(UINT32 *)&Src[Index];
          }
        }
      }

      break;

    case EfiBltVideoToVideo:
      CopyMem (
        (VOID *)&Fb[DestinationY * G0245D_FB_WIDTH + DestinationX],
        (VOID *)&Fb[SourceY * G0245D_FB_WIDTH + SourceX],
        Height * G0245D_FB_LINEBYTES
        );
      break;

    default:
      gBS->RestoreTPL (Tpl);
      return EFI_UNSUPPORTED;
  }

  //
  // cont_splash_mem is ordinary cacheable RAM: the pixels we just wrote can
  // still be sitting dirty in the CPU cache while the MDP5 scans that same
  // buffer through the interconnect, i.e. straight from DRAM.  Until the
  // lines are written back the panel keeps showing the previous frame, which
  // looks exactly like "the driver ignored my Blt".  Publish the rows we
  // touched on every call.
  //
  {
    UINTN  FlushRows;

    FlushRows = Height;
    if ((DestinationY + FlushRows) > G0245D_FB_HEIGHT) {
      FlushRows = (DestinationY < G0245D_FB_HEIGHT)
                    ? (G0245D_FB_HEIGHT - DestinationY) : 0;
    }

    if (FlushRows != 0) {
      WriteBackDataCacheRange (
        (VOID *)(UINTN)&Fb[(UINTN)DestinationY * G0245D_FB_WIDTH],
        FlushRows * G0245D_FB_LINEBYTES
        );
    }
  }

  gBS->RestoreTPL (Tpl);
  return EFI_SUCCESS;
}

//
// Protocol template.
//
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  mDisplayGraphicsOutput = {
  DisplayQueryMode,
  DisplaySetMode,
  DisplayBlt,
  NULL    // Mode, filled in at entry
};

/**
  Entry point: describe the splash window and publish GOP.

  @param[in] ImageHandle    Image handle.
  @param[in] SystemTable    System table.

  @retval EFI_SUCCESS       GOP installed.
  @retval others            Installation failed.
**/
EFI_STATUS
EFIAPI
DisplayDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  MSM8996_DISPLAY_PRIVATE  *Private;
  EFI_STATUS                Status;

  //
  // Cold display bring-up: MMSS GDSCs -> MDP/DSI clocks -> 14nm PLL/PHY ->
  // DSI host + ft8716 on-commands -> MDP5 scanout of the splash window ->
  // backlight. On the hot path (MDSS GDSC already on) this is a no-op and
  // the splash hand-off below works unchanged.
  //
  Status = Msm8996DisplayColdInit ();
  if (EFI_ERROR (Status)) {
    //
    // Panel bring-up failure must not brick the boot: keep the GOP over the
    // splash window so ConSplitter still installs, and let serial remain
    // the authoritative console.
    //
    DEBUG ((
      DEBUG_ERROR,
      "%a: cold display init failed: %r (GOP over splash anyway)\n",
      gEfiCallerBaseName, Status
      ));
  }

  Private = AllocateZeroPool (sizeof (MSM8996_DISPLAY_PRIVATE));
  if (Private == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Private->Signature = MSM8996_DISPLAY_SIGNATURE;

  Private->Info.Version                            = 0;
  Private->Info.HorizontalResolution               = G0245D_FB_WIDTH;
  Private->Info.VerticalResolution                 = G0245D_FB_HEIGHT;
  Private->Info.PixelFormat                        = G0245D_FB_PIXEL_FORMAT;
  Private->Info.PixelInformation.RedMask           = 0x00FF0000;
  Private->Info.PixelInformation.GreenMask         = 0x0000FF00;
  Private->Info.PixelInformation.BlueMask          = 0x000000FF;
  Private->Info.PixelInformation.ReservedMask      = 0xFF000000;
  Private->Info.PixelsPerScanLine                  = G0245D_FB_WIDTH;

  Private->Mode.MaxMode                            = 1;
  Private->Mode.Mode                               = 0;
  Private->Mode.Info                               = &Private->Info;
  Private->Mode.SizeOfInfo                         = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  //
  // v51: publish the buffer the MDP5 scanout was ACTUALLY programmed to read
  // from, not the cont_splash_mem carve-out we assume.  The cold path probes
  // the pipe (paints a pattern, reads it back), so on a board whose
  // bootloader left the scanout pointing somewhere else we now hand out the
  // right address and Blt lands on visible pixels.
  //
  Private->Mode.FrameBufferBase                    = Msm8996DisplayGetFrameBuffer ();
  Private->Mode.FrameBufferSize                    = G0245D_FB_VISIBLE_SIZE;

  mDisplayGraphicsOutput.Mode                      = &Private->Mode;
  Private->GraphicsOutput                          = mDisplayGraphicsOutput;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ImageHandle,
                  &gEfiGraphicsOutputProtocolGuid,
                  &Private->GraphicsOutput,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: GOP install failed: %r\n", gEfiCallerBaseName, Status));
    FreePool (Private);
    return Status;
  }

  DEBUG ((
    DEBUG_INFO,
    "%a: GOP over splash FB 0x%lx (%dx%d@32)\n",
    gEfiCallerBaseName,
    (UINT64)Private->Mode.FrameBufferBase,
    G0245D_FB_WIDTH,
    G0245D_FB_HEIGHT
    ));
  return EFI_SUCCESS;
}
