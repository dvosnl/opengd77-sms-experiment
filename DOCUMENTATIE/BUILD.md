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

## 2. Codec linkerdata voorbereiden (`prepare.bat`)

De firmware maakt gebruik van twee binaire secties met de DMR-codec.  
Deze kunnen **niet** in de repository worden opgenomen vanwege licentieredenen.

**Vereiste bestanden** (handmatig aanleveren of uit een eerdere build kopiëren):

```
MDUV380_firmware/application/source/linkerdata/codec_bin_section_1.bin
MDUV380_firmware/application/source/linkerdata/codec_bin_section_2.bin
```

Zodra de twee `.bin`-bestanden aanwezig zijn, voer dan vanuit de `V2\`-map uit:

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

| Configuratienaam | Radio | Uitvoerbestand |
|---|---|---|
| `MDUV380_10W_PLUS_FW` | TYT MD-UV380 10W+ | `OpenMDUV380_10W_PLUS.bin` |
| `RT84_FW` | Retevis RT-84 / RT3S | `OpenRT84.bin` |
| `DM1701_FW` | Baofeng DM-1701 | *(zie .cproject)* |
| `JA_MDUV380_FW` | MD-UV380 (JA variant) | *(zie .cproject)* |
| `JA_RT84_FW` | RT-84 (JA variant) | *(zie .cproject)* |

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
    -data "C:\Users\berts\Documents\uv390-self_dev\V2" `
    -cleanBuild "MDUV380_firmware/MDUV380_10W_PLUS_FW"
```

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