/** @file
  CrashLogDxe - capture the full CPU context on any fatal exception and persist
  it to the tail of the userdata partition, so it can be read back from Android
  over adb with no serial port required.

  Methodology borrowed from ios-sweet / HackiOS: when the debug UART is not
  wired out, the only reliable post-mortem channel is storage. We register a
  CPU exception hook, and on any synchronous abort / SError we snapshot the
  general registers, the exception syndrome, and a frame-pointer backtrace,
  then write the record to a fixed region at the end of the userdata
  partition and cold-reset back into Android.

  Copyright (c) 2026, UniXNU contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Protocol/Cpu.h>
#include <Protocol/BlockIo.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/DevicePathToText.h>

#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

#define CRASH_MAGIC          0x48535243   // 'CRSH'
#define CRASH_VERSION        1
#define CRASH_REGION_BYTES   (16 * 4096)  // 64 KiB reserved at userdata tail
#define MAX_BACKTRACE        32
#define MAX_IMAGES           32
#define LOG_BUF_SIZE         1536

typedef struct {
  UINT64  Base;
  UINT64  Size;
  CHAR16  Path[64];
} CRASH_IMAGE;

//
// On-disk record. Keep it a plain POD struct; the decode tool reads it raw.
//
typedef struct {
  UINT32  Magic;          // 'CRSH'
  UINT32  Version;
  UINT32  RegionBlocks;   // CRASH_REGION_BYTES / BlockSize
  UINT32  BlockSize;      // device logical block size at write time
  UINT64  WriteLba;      // LBA of the record start (for verification)
  UINT32  ExceptionType;  // EFI_EXCEPTION_TYPE
  UINT32  Reserved0;
  UINT64  Timestamp;
  UINT64  X[29];          // X0..X28
  UINT64  FP;             // X29
  UINT64  LR;             // X30
  UINT64  SP;             // X31
  UINT64  ELR;
  UINT64  SPSR;
  UINT64  FPSR;
  UINT64  ESR;
  UINT64  FAR;
  UINT32  BacktraceCount;
  UINT32  Reserved1;
  UINT64  Backtrace[MAX_BACKTRACE];
  UINT32  ImageCount;
  UINT32  Reserved2;
  CRASH_IMAGE Images[MAX_IMAGES];
  UINT32  LogSize;
  UINT8   Log[LOG_BUF_SIZE];
} CRASH_RECORD;

//
// Cached at entry (normal context) so the handler does the minimum possible
// work and never calls LocateHandle/AllocatePool.
//
static EFI_BLOCK_IO_PROTOCOL  *mBlk           = NULL;
static UINT32                  mMediaId       = 0;
static UINT64                  mWriteLba      = 0;
static UINT32                  mRegionBlocks  = 0;
static UINT32                  mBlockSize     = 0;
static CRASH_RECORD           *mRec           = NULL;
static BOOLEAN                 mReady         = FALSE;
static volatile BOOLEAN        mCrashed       = FALSE;

//
// Append a line to the in-record log ring (best-effort, no allocation).
//
STATIC
VOID
CrashLogAppend (
  IN CONST CHAR8  *Msg
  )
{
  UINTN   Len;
  UINTN   Room;

  if (mRec == NULL) {
    return;
  }
  Len  = AsciiStrLen (Msg);
  Room = LOG_BUF_SIZE - mRec->LogSize;
  if (Len == 0 || Len + 1 > Room) {
    return;
  }
  CopyMem (&mRec->Log[mRec->LogSize], Msg, Len);
  mRec->LogSize += (UINT32)Len;
  mRec->Log[mRec->LogSize++] = '\n';
}

//
// Unwind the call stack using the AArch64 frame-pointer convention:
//   frame record at [FP]   = saved FP (previous frame)
//   frame record at [FP+8] = return address (LR)
// Best-effort: a bad FP just stops the walk.
//
STATIC
VOID
CaptureBacktrace (
  IN UINT64  Fp
  )
{
  UINTN   Count;
  UINT64  SavedFp;
  UINT64  Lr;

  Count = 0;
  while (Count < MAX_BACKTRACE) {
    //
    // Sanity: must look like a kernel/UEFI address and be 16-byte aligned.
    //
    if (Fp < 0x80000000ULL || (Fp & 0xF) != 0) {
      break;
    }
    CopyMem (&SavedFp, (VOID *)(UINTN)Fp,      sizeof (SavedFp));
    CopyMem (&Lr,     (VOID *)(UINTN)(Fp + 8), sizeof (Lr));
    if (Lr == 0) {
      break;
    }
    mRec->Backtrace[Count++] = Lr;
    Fp = SavedFp;
  }
  mRec->BacktraceCount = (UINT32)Count;
}

//
// Snapshot currently loaded images (Base / Size / path) into the record.
// Done at entry in normal context so the handler never allocates.
//
STATIC
VOID
SnapshotLoadedImages (
  VOID
  )
{
  EFI_STATUS              Status;
  EFI_HANDLE             *Handles;
  UINTN                   Count;
  UINTN                   Index;
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;
  EFI_DEVICE_PATH_TO_TEXT_PROTOCOL *Dp2Txt;
  EFI_DEVICE_PATH        *Dp;
  CHAR16                 *Txt;

  Status = gBS->LocateHandleBuffer (
                 ByProtocol,
                 &gEfiLoadedImageProtocolGuid,
                 NULL,
                 &Count,
                 &Handles
                 );
  if (EFI_ERROR (Status)) {
    return;
  }

  Dp2Txt = NULL;
  gBS->LocateProtocol (&gEfiDevicePathToTextProtocolGuid, NULL, (VOID **)&Dp2Txt);

  for (Index = 0; Index < Count && mRec->ImageCount < MAX_IMAGES; Index++) {
    Status = gBS->HandleProtocol (
                   Handles[Index],
                   &gEfiLoadedImageProtocolGuid,
                   (VOID **)&Loaded
                   );
    if (EFI_ERROR (Status) || Loaded == NULL) {
      continue;
    }
    mRec->Images[mRec->ImageCount].Base = (UINT64)(UINTN)Loaded->ImageBase;
    mRec->Images[mRec->ImageCount].Size = Loaded->ImageSize;
    mRec->Images[mRec->ImageCount].Path[0] = L'\0';
    Dp = Loaded->FilePath;
    if (Dp != NULL && Dp2Txt != NULL) {
      Txt = Dp2Txt->ConvertDevicePathToText (Dp, FALSE, FALSE);
      if (Txt != NULL) {
        CopyMem (
          mRec->Images[mRec->ImageCount].Path,
          Txt,
          StrLen (Txt) * sizeof (CHAR16) < sizeof (mRec->Images[0].Path)
            ? StrLen (Txt) * sizeof (CHAR16)
            : sizeof (mRec->Images[0].Path) - sizeof (CHAR16)
          );
        mRec->Images[mRec->ImageCount].Path[63] = L'\0';
        FreePool (Txt);
      }
    }
    mRec->ImageCount++;
  }

  FreePool (Handles);
}

//
// Locate the userdata partition's raw BlockIo and cache everything we need.
// Falls back to the logical partition with the largest LastBlock.
//
STATIC
EFI_STATUS
LocateUserData (
  VOID
  )
{
  EFI_STATUS                 Status;
  EFI_HANDLE                *Handles;
  UINTN                      Count;
  UINTN                      Index;
  EFI_BLOCK_IO_PROTOCOL     *Blk;
  EFI_PARTITION_INFO_PROTOCOL *PartInfo;
  BOOLEAN                    Found;
  UINT64                     BestLast;

  Status = gBS->LocateHandleBuffer (
                 ByProtocol,
                 &gEfiPartitionInfoProtocolGuid,
                 NULL,
                 &Count,
                 &Handles
                 );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Found    = FALSE;
  BestLast = 0;
  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (
                   Handles[Index],
                   &gEfiPartitionInfoProtocolGuid,
                   (VOID **)&PartInfo
                   );
    if (EFI_ERROR (Status) || PartInfo == NULL) {
      continue;
    }
    if (PartInfo->Type != PARTITION_TYPE_GPT) {
      continue;
    }
    //
    // Prefer an exact "userdata" GPT partition.
    //
    if (!Found && StrnCmp (PartInfo->Info.Gpt.PartitionName, L"userdata", 8) == 0) {
      Status = gBS->HandleProtocol (
                     Handles[Index],
                     &gEfiBlockIoProtocolGuid,
                     (VOID **)&Blk
                     );
      if (!EFI_ERROR (Status) && Blk != NULL) {
        mBlk      = Blk;
        mMediaId  = Blk->Media->MediaId;
        mBlockSize = Blk->Media->BlockSize;
        mWriteLba = Blk->Media->LastBlock - mRegionBlocks + 1;
        Found     = TRUE;
        break;
      }
    }
  }

  if (!Found) {
    //
    // Fallback: largest logical partition (typically userdata anyway).
    //
    for (Index = 0; Index < Count; Index++) {
      Status = gBS->HandleProtocol (
                     Handles[Index],
                     &gEfiPartitionInfoProtocolGuid,
                     (VOID **)&PartInfo
                     );
      if (EFI_ERROR (Status) || PartInfo == NULL) {
        continue;
      }
      if (PartInfo->Type != PARTITION_TYPE_GPT) {
        continue;
      }
      Status = gBS->HandleProtocol (
                     Handles[Index],
                     &gEfiBlockIoProtocolGuid,
                     (VOID **)&Blk
                     );
      if (EFI_ERROR (Status) || Blk == NULL) {
        continue;
      }
      if (Blk->Media->LastBlock > BestLast) {
        BestLast  = Blk->Media->LastBlock;
        mBlk      = Blk;
        mMediaId  = Blk->Media->MediaId;
        mBlockSize = Blk->Media->BlockSize;
        mWriteLba = Blk->Media->LastBlock - mRegionBlocks + 1;
      }
    }
  }

  FreePool (Handles);
  return (mBlk != NULL) ? EFI_SUCCESS : EFI_NOT_FOUND;
}

//
// v80: find which loaded image contains Addr (uses the image snapshot taken
// at entry point time). Returns the module path or NULL.
//
STATIC
CHAR16 *
FindModuleForAddr (
  IN  UINT64  Addr,
  OUT UINT64  *Offset
  )
{
  UINT32  Index;

  if (mRec == NULL) {
    return NULL;
  }
  for (Index = 0; Index < mRec->ImageCount; Index++) {
    if (Addr >= mRec->Images[Index].Base &&
        Addr < mRec->Images[Index].Base + mRec->Images[Index].Size) {
      if (Offset != NULL) {
        *Offset = Addr - mRec->Images[Index].Base;
      }
      return mRec->Images[Index].Path;
    }
  }
  return NULL;
}

//
// v80: render a plain-text crash summary through ConOut (the same
// GraphicsConsole path the UniXNU menu draws through, so colors/geometry are
// already proven correct on this panel). If ConOut is not up (crash before
// console init) this silently does nothing.
//
STATIC
VOID
RenderCrashScreen (
  IN UINTN                            ExceptionType,
  IN CONST EFI_SYSTEM_CONTEXT_AARCH64 *Ctx
  )
{
  CHAR16  Line[128];
  CHAR16  *Mod;
  UINT64  Off;
  UINT32  i;

  if (gST == NULL || gST->ConOut == NULL) {
    return;
  }

  gST->ConOut->Reset (gST->ConOut, FALSE);
  gST->ConOut->SetAttribute (gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);
  gST->ConOut->ClearScreen (gST->ConOut);

  UnicodeSPrint (Line, sizeof (Line),
                 L"*** FATAL EXCEPTION type=%d ***\r\n", (UINT32)ExceptionType);
  gST->ConOut->OutputString (gST->ConOut, Line);
  UnicodeSPrint (Line, sizeof (Line),
                 L"ELR=%016lx\r\nFAR=%016lx\r\n", Ctx->ELR, Ctx->FAR);
  gST->ConOut->OutputString (gST->ConOut, Line);
  UnicodeSPrint (Line, sizeof (Line),
                 L"ESR=%016lx SPSR=%016lx\r\n", Ctx->ESR, Ctx->SPSR);
  gST->ConOut->OutputString (gST->ConOut, Line);

  Off = 0;
  Mod = FindModuleForAddr (Ctx->ELR, &Off);
  if (Mod != NULL) {
    UnicodeSPrint (Line, sizeof (Line), L"IMG: %s +0x%lx\r\n", Mod, Off);
  } else {
    UnicodeSPrint (Line, sizeof (Line), L"IMG: (no module at ELR)\r\n");
  }
  gST->ConOut->OutputString (gST->ConOut, Line);

  if (mRec != NULL) {
    for (i = 0; i < mRec->BacktraceCount && i < 4; i++) {
      UnicodeSPrint (Line, sizeof (Line), L"BT%u: %016lx\r\n", i, mRec->Backtrace[i]);
      gST->ConOut->OutputString (gST->ConOut, Line);
      Off = 0;
      Mod = FindModuleForAddr (mRec->Backtrace[i], &Off);
      if (Mod != NULL) {
        UnicodeSPrint (Line, sizeof (Line), L"     %s +0x%lx\r\n", Mod, Off);
        gST->ConOut->OutputString (gST->ConOut, Line);
      }
    }
  }

  UnicodeSPrint (Line, sizeof (Line),
                 L"record saved to userdata tail - photo this screen\r\n");
  gST->ConOut->OutputString (gST->ConOut, Line);
}

//
// The handler. Registered for synchronous exceptions (0) and SError (3).
//
STATIC
VOID
EFIAPI
CrashHandler (
  IN CONST EFI_EXCEPTION_TYPE     InterruptType,
  IN CONST EFI_SYSTEM_CONTEXT     SystemContext
  )
{
  CONST EFI_SYSTEM_CONTEXT_AARCH64 *Ctx;

  //
  // Re-entrancy guard: if we crash while handling a crash, just reset.
  //
  if (mCrashed) {
    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  }
  mCrashed = TRUE;

  if (mRec == NULL) {
    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  }

  Ctx = SystemContext.SystemContextAArch64;

  ZeroMem (mRec, sizeof (CRASH_RECORD));
  mRec->Magic         = CRASH_MAGIC;
  mRec->Version       = CRASH_VERSION;
  mRec->RegionBlocks  = mRegionBlocks;
  mRec->BlockSize     = mBlockSize;
  mRec->WriteLba      = mWriteLba;
  mRec->ExceptionType = (UINT32)InterruptType;
  mRec->Timestamp     = 0;

  //
  // Copy the general registers. The AArch64 context struct exposes them as
  // X0..X28 individually; assign explicitly to stay field-name correct.
  //
  mRec->X[0]  = Ctx->X0;  mRec->X[1]  = Ctx->X1;  mRec->X[2]  = Ctx->X2;
  mRec->X[3]  = Ctx->X3;  mRec->X[4]  = Ctx->X4;  mRec->X[5]  = Ctx->X5;
  mRec->X[6]  = Ctx->X6;  mRec->X[7]  = Ctx->X7;  mRec->X[8]  = Ctx->X8;
  mRec->X[9]  = Ctx->X9;  mRec->X[10] = Ctx->X10; mRec->X[11] = Ctx->X11;
  mRec->X[12] = Ctx->X12; mRec->X[13] = Ctx->X13; mRec->X[14] = Ctx->X14;
  mRec->X[15] = Ctx->X15; mRec->X[16] = Ctx->X16; mRec->X[17] = Ctx->X17;
  mRec->X[18] = Ctx->X18; mRec->X[19] = Ctx->X19; mRec->X[20] = Ctx->X20;
  mRec->X[21] = Ctx->X21; mRec->X[22] = Ctx->X22; mRec->X[23] = Ctx->X23;
  mRec->X[24] = Ctx->X24; mRec->X[25] = Ctx->X25; mRec->X[26] = Ctx->X26;
  mRec->X[27] = Ctx->X27; mRec->X[28] = Ctx->X28;
  mRec->FP    = Ctx->FP;   mRec->LR   = Ctx->LR;   mRec->SP   = Ctx->SP;
  mRec->ELR   = Ctx->ELR;  mRec->SPSR = Ctx->SPSR; mRec->FPSR = Ctx->FPSR;
  mRec->ESR   = Ctx->ESR;  mRec->FAR  = Ctx->FAR;

  CrashLogAppend ("CrashLogDxe: exception captured");
  CaptureBacktrace (Ctx->FP);

  //
  // v80: draw the summary on screen FIRST (before the storage write, in case
  // the write itself faults) -- the on-screen ELR/IMG line is the primary
  // diagnostic channel on this device (no serial, adb pull unreliable).
  //
  RenderCrashScreen (InterruptType, Ctx);

  //
  // Persist to storage. Best-effort; if it faults we still reset below.
  //
  if (mBlk != NULL) {
    mBlk->WriteBlocks (mBlk, mMediaId, mWriteLba,
                       (UINTN)mRegionBlocks * mBlockSize, mRec);
  }

  //
  // Hold the crash screen long enough for a photo, then cold-reset.
  //
  gBS->Stall (8000000);
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
}

EFI_STATUS
EFIAPI
CrashLogDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_CPU_ARCH_PROTOCOL *Cpu;
  VOID                 *Buf;

  DEBUG ((DEBUG_INFO, "CrashLogDxe: entry\n"));

  mRegionBlocks = CRASH_REGION_BYTES / 512;   // safe upper bound; corrected below

  //
  // Allocate an aligned write buffer. We align to 4096 which covers any
  // plausible UFS logical block size (512 or 4096).
  //
  Buf = AllocatePool (CRASH_REGION_BYTES + 4096);
  if (Buf == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  mRec = (CRASH_RECORD *)(((UINTN)Buf + 4095) & ~(UINTN)4095);
  ZeroMem (mRec, sizeof (CRASH_RECORD));

  //
  // Locate the userdata partition and learn its block size.
  //
  Status = LocateUserData ();
  if (EFI_ERROR (Status) || mBlk == NULL) {
    DEBUG ((DEBUG_WARN, "CrashLogDxe: no userdata BlockIo (%r)\n", Status));
    CrashLogAppend ("CrashLogDxe: userdata BlockIo not found");
  } else {
    //
    // Recompute region blocks from the real block size.
    //
    mRegionBlocks = (UINT32)((CRASH_REGION_BYTES + mBlockSize - 1) / mBlockSize);
    mWriteLba     = mBlk->Media->LastBlock - mRegionBlocks + 1;
    CrashLogAppend ("CrashLogDxe: userdata LBA base cached");
  }

  SnapshotLoadedImages ();

  //
  // Register the exception hook. Only fault-class exceptions: synchronous (0)
  // and SError (3). IRQ/FIQ (1/2) are left to the normal handler.
  //
  Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&Cpu);
  if (!EFI_ERROR (Status) && Cpu != NULL) {
    Cpu->RegisterInterruptHandler (Cpu, EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS, CrashHandler);
    Cpu->RegisterInterruptHandler (Cpu, EXCEPT_AARCH64_SERROR, CrashHandler);
    mReady = TRUE;
    DEBUG ((DEBUG_INFO, "CrashLogDxe: handlers registered\n"));
  } else {
    DEBUG ((DEBUG_WARN, "CrashLogDxe: EFI_CPU_ARCH_PROTOCOL unavailable (%r)\n", Status));
  }

  return EFI_SUCCESS;
}
