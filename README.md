# opengd77-sms-experiment

This workspace contains an STM32CubeIDE project for the UV390/MDUV380 OpenGD77 firmware.

## Project Location

The buildable firmware is located in:

`V2/MDUV380_firmware`

The Eclipse/STM32CubeIDE project name is:

`MDUV380_firmware`

Available build configurations:

- `MDUV380_FW`
- `JA_MDUV380_FW`
- `DM1701_FW`
- `JA_DM1701_FW`
- `RT84_FW`
- `JA_RT84_FW`
- `MDUV380_10W_PLUS_FW`
- `JA_MDUV380_10W_PLUS_FW`

## License — **non-commercial only**

The OpenGD77 source code is distributed under a **modified BSD-3-Clause
license with an explicit non-commercial restriction** (see
[`license.txt`](license.txt) and
[`MDUV380_firmware/tools/license.txt`](MDUV380_firmware/tools/license.txt)).
Relevant clause:

> 4. Use of this source code or binary releases for commercial purposes is
> strictly forbidden. This includes, without limitation, incorporation in a
> commercial product or incorporation into a product or project which allows
> commercial use.

This restriction inherits to any fork or derivative work, including this one.

Bundled third-party components retain their own licenses:

- **CMSIS** — Apache-2.0
- **STM32F4xx HAL Driver** — BSD-3-Clause
- **ST USB Device Library** — ST SLA0044
- **FreeRTOS** — MIT (Amazon)
- **SEGGER RTT** — SEGGER custom BSD-like

See the `LICENSE`/`LICENSE.txt` files inside each subdirectory.

## Upstream and credits

- Upstream release zips: <https://www.opengd77.com/downloads/releases/>
- Community-maintained user guide:
  <https://github.com/LibreDMR/OpenGD77_UserGuide>
- Original authors and contributors are listed in
  the upstream README ([`UPSTREAM_README.md`](UPSTREAM_README.md))
  (Kai DG4KLU, Roger VK3KYY, Daniel F1RMB, Alex DL4LEX, Colin G4EML and
  many others).

If you are looking for the canonical firmware: **use
[opengd77.com](https://www.opengd77.com/)**, not this repo.

## Requirements

- Windows
- STM32CubeIDE with the built-in STM32 GCC toolchain
- Git in `PATH` is useful, because some compile units use `git rev-parse --short HEAD` for the version string

## Preparation

First run `V2/prepare.bat` from the `V2` folder.

This script checks whether the source tree is complete and then calls `MDUV380_firmware/tools/codec_cleaner.exe -C` in `application/source/linkerdata`.

Example:

```powershell
Set-Location "C:\Users\berts\Documents\uv390-self_dev\V2"
.\prepare.bat
```

If this script fails, further builds usually do not make much sense. First check:

- whether `V2/MDUV380_firmware/application/source/linkerdata` exists
- whether `V2/MDUV380_firmware/tools/codec_cleaner.exe` is present

## Building in STM32CubeIDE

1. First run `V2/prepare.bat`.
2. Open STM32CubeIDE.
3. Select `File -> Open Projects from File System...`.
4. Select the `V2/MDUV380_firmware` folder.
5. Wait for indexing and project import to finish.
6. Choose the desired configuration via `Project -> Build Configurations -> Set Active`.
7. Then use `Project -> Clean...` or `Project -> Build Project`.

For a standard UV380 build, you usually use `MDUV380_FW`.

For the 10W Plus variant, use `MDUV380_10W_PLUS_FW`.

## Headless Build on Windows

The format below matches the headless build logs already present in this workspace.

Adjust the path to `stm32cubeidec.exe` to match your installation:

First run:

```powershell
Set-Location "C:\Users\berts\Documents\uv390-self_dev\V2"
.\prepare.bat
```

```powershell
& "C:\ST\STM32CubeIDE_1.16.0\STM32CubeIDE\stm32cubeidec.exe" `
	-nosplash `
	-application org.eclipse.cdt.managedbuilder.core.headlessbuild `
	-data "C:\Users\berts\Documents\uv390-self_dev\.headless-workspace" `
	-import "C:\Users\berts\Documents\uv390-self_dev\V2\MDUV380_firmware" `
	-cleanBuild "MDUV380_firmware/MDUV380_FW"
```

For the Japanese UV380 variant:

```powershell
& "C:\ST\STM32CubeIDE_1.16.0\STM32CubeIDE\stm32cubeidec.exe" `
	-nosplash `
	-application org.eclipse.cdt.managedbuilder.core.headlessbuild `
	-data "C:\Users\berts\Documents\uv390-self_dev\.headless-workspace" `
	-import "C:\Users\berts\Documents\uv390-self_dev\V2\MDUV380_firmware" `
	-cleanBuild "MDUV380_firmware/JA_MDUV380_FW"
```

For the 10W Plus variant:

```powershell
& "C:\ST\STM32CubeIDE_1.16.0\STM32CubeIDE\stm32cubeidec.exe" `
	-nosplash `
	-application org.eclipse.cdt.managedbuilder.core.headlessbuild `
	-data "C:\Users\berts\Documents\uv390-self_dev\.headless-workspace" `
	-import "C:\Users\berts\Documents\uv390-self_dev\V2\MDUV380_firmware" `
	-cleanBuild "MDUV380_firmware/MDUV380_10W_PLUS_FW"
```

## Build Output

The configurations in `.cproject` write to the folder:

`V2/MDUV380_firmware/Debug`

Important outputs are usually:

- `.elf`
- `.bin`

After the post-build step, a cleaned firmware file is also produced via `tools/codec_cleaner`, for example:

- `OpenMDUV380.bin`
- `OpenMDUV380_Japanese.bin`
- `OpenDM1701.bin`
- `OpenDM1701_Japanese.bin`
- `OpenRT84.bin`
- `OpenRT84_Japanese.bin`
- `OpenMDUV380_10W_PLUS.bin`
- `OpenMDUV380_10W_PLUS_Japanese.bin`

## Notes

- This repo contains older documentation in `DOCUMENTATIE/BUILD.md`, but that describes a different toolchain and is not authoritative for this STM32CubeIDE workspace.
- Not everything that looks like a configuration name in `.cproject` is a build configuration; it also contains debug launch entries. The list above includes only the firmware build configurations.
- If the headless build fails because the IDE installation is missing, first check the path to `stm32cubeidec.exe`.
- If `git` is not in `PATH`, the build can still succeed, but the version string may fall back to `UNKNOWN`.
