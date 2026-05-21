# BUILD_ALL.md

Exacte headless build-commando's voor alle radio-configuraties.

## Voorwaarden

Werkmap:

```powershell
cd C:\Users\berts\Documents\uv390-self_dev\V2_MOTOROLA_FORMAT
```

Voor elke clean build eerst codec/linkerdata voorbereiden:

```powershell
.\prepare.bat
```

## Basiscommand (zelfde voor alle configuraties)

```powershell
& "C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\stm32cubeidec.exe" `
  -nosplash `
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
  -data "C:\Users\berts\Documents\uv390-self_dev\V2_MOTOROLA_FORMAT" `
  -cleanBuild "MDUV380_firmware/<CONFIG>"
```

## Alle configuraties (1-op-1 uitvoerbaar)

```powershell
# 1) TYT MD-UV380
& "C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\stm32cubeidec.exe" `
  -nosplash `
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
  -data "C:\Users\berts\Documents\uv390-self_dev\V2_MOTOROLA_FORMAT" `
  -cleanBuild "MDUV380_firmware/MDUV380_FW"

# 2) TYT MD-UV380 10W+
& "C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\stm32cubeidec.exe" `
  -nosplash `
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
  -data "C:\Users\berts\Documents\uv390-self_dev\V2_MOTOROLA_FORMAT" `
  -cleanBuild "MDUV380_firmware/MDUV380_10W_PLUS_FW"

# 3) Baofeng DM-1701
& "C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\stm32cubeidec.exe" `
  -nosplash `
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
  -data "C:\Users\berts\Documents\uv390-self_dev\V2_MOTOROLA_FORMAT" `
  -cleanBuild "MDUV380_firmware/DM1701_FW"

# 4) Retevis RT-84
& "C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\stm32cubeidec.exe" `
  -nosplash `
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
  -data "C:\Users\berts\Documents\uv390-self_dev\V2_MOTOROLA_FORMAT" `
  -cleanBuild "MDUV380_firmware/RT84_FW"

# 5) TYT MD-UV380 (Japan)
& "C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\stm32cubeidec.exe" `
  -nosplash `
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
  -data "C:\Users\berts\Documents\uv390-self_dev\V2_MOTOROLA_FORMAT" `
  -cleanBuild "MDUV380_firmware/JA_MDUV380_FW"

# 6) TYT MD-UV380 10W+ (Japan)
& "C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\stm32cubeidec.exe" `
  -nosplash `
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
  -data "C:\Users\berts\Documents\uv390-self_dev\V2_MOTOROLA_FORMAT" `
  -cleanBuild "MDUV380_firmware/JA_MDUV380_10W_PLUS_FW"

# 7) Baofeng DM-1701 (Japan)
& "C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\stm32cubeidec.exe" `
  -nosplash `
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
  -data "C:\Users\berts\Documents\uv390-self_dev\V2_MOTOROLA_FORMAT" `
  -cleanBuild "MDUV380_firmware/JA_DM1701_FW"

# 8) Retevis RT-84 (Japan)
& "C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\stm32cubeidec.exe" `
  -nosplash `
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
  -data "C:\Users\berts\Documents\uv390-self_dev\V2_MOTOROLA_FORMAT" `
  -cleanBuild "MDUV380_firmware/JA_RT84_FW"
```

## Optioneel: alles achter elkaar (PowerShell)

```powershell
$configs = @(
  "MDUV380_FW",
  "MDUV380_10W_PLUS_FW",
  "DM1701_FW",
  "RT84_FW",
  "JA_MDUV380_FW",
  "JA_MDUV380_10W_PLUS_FW",
  "JA_DM1701_FW",
  "JA_RT84_FW"
)

cd C:\Users\berts\Documents\uv390-self_dev\V2_MOTOROLA_FORMAT
.\prepare.bat

foreach ($cfg in $configs) {
  & "C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\stm32cubeidec.exe" `
    -nosplash `
    -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
    -data "C:\Users\berts\Documents\uv390-self_dev\V2_MOTOROLA_FORMAT_MOTOROLA_FORMAT" `
    -cleanBuild "MDUV380_firmware/$cfg"
}
```
