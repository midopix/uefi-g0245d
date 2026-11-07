#!/usr/bin/env bash
# Apply the g0245d overlay to a Mu-Silicium checkout.
#
#   bash scripts/apply-overlay.sh /path/to/Mu-Silicium
#
# Idempotent: re-running copies the same files and skips patches that are
# already applied (both scopes: Mu_Basecore submodule and Mu-Silicium root).
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

# The two persistent patches have DIFFERENT git scopes: 0001 touches files
# inside the Mu_Basecore submodule, 0002 touches BootShim/BootShim.S at the
# Mu-Silicium root. Applying both with `git -C Mu_Basecore` silently skips
# 0002, which leaves the ARM64 image_size field wrong in a fresh checkout.
# Keep the scope explicit per patch and stay idempotent (forward-check, then
# reverse-check for "already applied").
apply_persistent_patch() {
  local git_dir="$1" patch="$2" name
  name="$(basename "${patch}")"
  if git -C "${git_dir}" apply --check "${patch}" 2>/dev/null; then
    git -C "${git_dir}" apply "${patch}"
    echo ">>> applied ${name} -> ${git_dir}"
  elif git -C "${git_dir}" apply --reverse --check "${patch}" 2>/dev/null; then
    echo ">>> ${name} already applied"
  else
    echo "warning: ${name} does not apply cleanly to ${git_dir};" >&2
    echo "         the checkout is probably at a revision other than 6b3d90b7" >&2
  fi
}

apply_persistent_patch "${MU}/Mu_Basecore" "${REPO}/patches/mu/0001-trace-symbol-definitions.patch"
apply_persistent_patch "${MU}" "${REPO}/patches/mu/0002-bootshim-image-size.patch"

if [[ ! -d "${MU}/Binaries/g0245d" ]]; then
  echo
  echo "note: Binaries/g0245d is absent. Restore the vendor DXE images from the"
  echo "      QFIL package before building; see proprietary-files.txt in the"
  echo "      device repository."
fi

echo ">>> done; build with: MU=${MU} bash scripts/linux/build-firmware.sh"
