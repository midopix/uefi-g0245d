#!/usr/bin/env bash
# Apply the g0245d overlay to a Mu-Silicium checkout.
#
#   bash scripts/apply-overlay.sh /path/to/Mu-Silicium
#
# Idempotent: re-running copies the same files and skips an already applied
# Mu_Basecore patch.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MU="${1:-}"
[[ -n "${MU}" && -d "${MU}/Mu_Basecore" ]] || {
  echo "usage: bash scripts/apply-overlay.sh /path/to/Mu-Silicium" >&2
  exit 1
}
MU="$(cd "${MU}" && pwd)"

if [[ ! -f "${MU}/build_uefi.py" ]]; then
  echo "error: ${MU} does not look like a Mu-Silicium checkout" >&2
  exit 1
fi

echo ">>> overlay -> ${MU}"
mkdir -p "${MU}/Platforms/Gree" "${MU}/Silicon/Qualcomm" "${MU}/Resources/Configs"
cp -a "${REPO}/Platforms/Gree/g0245dPkg" "${MU}/Platforms/Gree/"
cp -a "${REPO}/Silicon/Qualcomm/Msm8996Pkg" "${MU}/Silicon/Qualcomm/"
cp -a "${REPO}/Resources/Configs/g0245d.toml" "${MU}/Resources/Configs/"

PATCH="${REPO}/patches/mu/0001-trace-symbol-definitions.patch"
if git -C "${MU}/Mu_Basecore" apply --check "${PATCH}" 2>/dev/null; then
  git -C "${MU}/Mu_Basecore" apply "${PATCH}"
  echo ">>> applied $(basename "${PATCH}")"
elif git -C "${MU}/Mu_Basecore" apply --reverse --check "${PATCH}" 2>/dev/null; then
  echo ">>> $(basename "${PATCH}") already applied"
else
  echo "warning: $(basename "${PATCH}") does not apply cleanly;" >&2
  echo "         Mu_Basecore is probably at a different revision than 6b3d90b7" >&2
fi

if [[ ! -d "${MU}/Binaries/g0245d" ]]; then
  echo
  echo "note: Binaries/g0245d is absent. Restore the vendor DXE images from the"
  echo "      QFIL package before building; see proprietary-files.txt in the"
  echo "      device repository."
fi

echo ">>> done; build with: MU=${MU} bash scripts/linux/build-firmware.sh"
