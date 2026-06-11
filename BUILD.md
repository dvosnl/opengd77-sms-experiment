# Build-handleiding — OpenMDUV380 firmware

Dit project gebruikt **STM32CubeIDE** (getest met versie 2.1.0) en de `arm-none-eabi-gcc` toolchain
die daarin is meegeleverd. Geen losse SDK download nodig.

---

## 1. Vereisten

| Software | Versie | Opmerking |
|---|---|---|
| STM32CubeIDE | ≥ 2.1.0 | Bevat arm-none-eabi-gcc en Make |
| Git | willekeurig | Voor `DGITVERSION` define in de build |

Standaard installatiepad op Windows: `C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\`

---

voer vanuit `V2\`-map uit:

```bat
cd C:\Users\berts\Documents\uv390-self_dev\V2
prepare.bat
```

`prepare.bat` roept `tools\codec_cleaner.exe -C` aan en verwerkt de codec-binaries zodat
de linker ze kan invoegen. Uitvoer bij succes:

```
codec_cleaner (STM32) v0.0.3 ...
- Creating file codec_bin_section_1.bin
Done
```

> **Elke keer opnieuw uitvoeren** nadat de repository schoon is gemaakt (clean build).

---

## 3. Build-configuraties 

| Configuratienaam | Radio | Omschrijving | Uitvoerbestand |
|---|---|---|---|
| `MDUV380_FW` | TYT MD-UV380 | Standaard MD-UV380 | `MDUV380_FW\OpenMDUV380.bin` |
| `MDUV380_10W_PLUS_FW` | TYT MD-UV380 10W+ | 10W+ versie van UV380 | `MDUV380_10W_PLUS_FW\OpenMDUV380_10W_PLUS.bin` |
| `DM1701_FW` | Baofeng DM-1701 | Standaard DM-1701 | `DM1701_FW\OpenDM1701.bin` |
| `RT84_FW` | Retevis RT-84 | Standaard RT-84 | `RT84_FW\OpenRT84.bin` |
| `JA_MDUV380_FW` | TYT MD-UV380 | MD-UV380 Japanse variant | `JA_MDUV380_FW\OpenMDUV380_Japanese.bin` |
| `JA_MDUV380_10W_PLUS_FW` | TYT MD-UV380 10W+ | 10W+ Japanse variant | `JA_MDUV380_10W_PLUS_FW\OpenMDUV380_10W_PLUS_Japanese.bin` |
| `JA_DM1701_FW` | Baofeng DM-1701 | DM-1701 Japanse variant | `JA_DM1701_FW\OpenDM1701_Japanese.bin` |
| `JA_RT84_FW` | Retevis RT-84 | RT-84 Japanse variant | `JA_RT84_FW\OpenRT84_Japanese.bin` |

Alle paden zijn relatief t.o.v. `V2\MDUV380_firmware\`.

---

## 4. Bouwen via STM32CubeIDE GUI

1. Open STM32CubeIDE.
2. Importeer het project: **File → Open Projects from File System…** → selecteer de `V2\MDUV380_firmware\`-map.
3. Selecteer de gewenste build-configuratie: **Project → Build Configurations → Set Active → `MDUV380_10W_PLUS_FW`** (of een andere configuratie).
4. Bouw: **Project → Clean…** (vink "Start a build immediately" aan).
5. De `.bin` verschijnt in `MDUV380_firmware\MDUV380_10W_PLUS_FW\`.

---

## 5. Bouwen via de commandoregel (headless)

Dit is de aanbevolen methode voor geautomatiseerde/herhaalbare builds.

```powershell
# Stap 1 — codec linkerdata voorbereiden
cd C:\Users\berts\Documents\uv390-self_dev\V2
.\prepare.bat

# Stap 2 — clean build (vervang de configuratienaam indien nodig)
& "C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\stm32cubeidec.exe" `
    -nosplash `
    -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
    -data "C:\Users\berts\Documents\uv390-self_dev\V3_TEST" `
    -cleanBuild "MDUV380_firmware/MDUV380_10W_PLUS_FW"
```

# Build voor de MD9600_RT90-tak (specifiek V5 hardware):

cd C:\Users\berts\Documents\uv390-self_dev\MD9600_RT90
.\prepare.bat
& "C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\stm32cubeidec.exe" `
    -nosplash `
    -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
    -data "C:\Users\berts\Documents\uv390-self_dev\MD9600_RT90" `
    -cleanBuild "MD9600_firmware/MD9600_HW_V5"

Verwachte einduitvoer:
```
Build Finished. 0 errors, 0 warnings.
```

De firmware staat dan in:
```
V2\MDUV380_firmware\MDUV380_10W_PLUS_FW\OpenMDUV380_10W_PLUS.bin
```

### RT-84 / RT3S build

```powershell
.\prepare.bat
& "C:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\stm32cubeidec.exe" `
    -nosplash `
    -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
    -data "C:\Users\berts\Documents\uv390-self_dev\V2" `
    -cleanBuild "MDUV380_firmware/RT84_FW"
```

Uitvoer: `V2\MDUV380_firmware\RT84_FW\OpenRT84.bin`

---

## 6. Firmware uploaden

Gebruik het meegeleverde Python-script of het Windows-uploadprogramma in de `tools\`-map:

```
tools\FirmwareLoader           (Windows binary)
tools\opengd77_stm32_firmware_loader.py  (Python, cross-platform)
```

---

## 7. Bekende aandachtspunten

- **`--short` warning**: De `DGITVERSION` make-variabele gebruikt backtick-subshell-expansie die niet werkt in Windows CMD/PowerShell. Dit levert een waarschuwing op, maar de build slaagt gewoon — de versiestring valt dan terug op een lege waarde.
- **Codec-binaries ontbreken**: Als `prepare.bat` meldt dat `codec_cleaner.exe` niet gevonden wordt, controleer dan of `MDUV380_firmware\tools\codec_cleaner.exe` aanwezig is.
- **Stale build**: Voer altijd eerst `prepare.bat` uit bij een clean build; de linkerdata worden anders niet bijgewerkt.