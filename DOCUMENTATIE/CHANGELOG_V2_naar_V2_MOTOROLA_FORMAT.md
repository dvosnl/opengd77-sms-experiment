# Changelog: V2 (oud) -> V2_MOTOROLA_FORMAT (nieuw)

Datum: 2026-04-13

## Scope
Deze changelog vergelijkt `V2` met `V2_MOTOROLA_FORMAT`.

Filter die is toegepast:
- Build-artifacts uitgesloten (`.o`, `.d`, `.su`, `.cyclo`, `.elf`, `.map`, `.list`, `.bin`, `.hex`).
- Workspace/build-mappen uitgesloten (`MDUV380_10W_PLUS_FW`, `MDUV380_FW`, `RT84_FW`, `JA_*`, `workspace*`).

## Samenvatting (gefilterde vergelijking)
- Gewijzigd: 17 bestanden
- Toegevoegd: 19 bestanden
- Verwijderd: 26 bestanden

Belangrijk: het merendeel van toegevoegd/verwijderd betreft Eclipse metadata en gegenereerde buildbestanden. De functionele codewijzigingen zitten vooral in 7 C/H-bestanden.

## Functioneel gewijzigde bronbestanden
1. `MDUV380_firmware/application/include/functions/sms.h`
2. `MDUV380_firmware/application/include/user_interface/uiGlobals.h`
3. `MDUV380_firmware/application/source/functions/sms.c`
4. `MDUV380_firmware/application/source/hardware/HR-C6000.c`
5. `MDUV380_firmware/application/source/user_interface/menuFirmwareInfoScreen.c`
6. `MDUV380_firmware/application/source/user_interface/menuSMS.c`
7. `MDUV380_firmware/application/source/user_interface/uiGlobals.c`

## Belangrijkste wijzigingen

### 1) SMS payload/protocol naar Motorola-formaat
Bestanden:
- `MDUV380_firmware/application/include/functions/sms.h`
- `MDUV380_firmware/application/source/functions/sms.c`

Wijzigingen:
- `SMS_MAX_TEXT_LENGTH` verhoogd van `64` naar `231`.
- Headerdefinitie gewijzigd van standaard naar Motorola (`SMS_MOTOROLA_HEADER_BYTES = 38`).
- Nieuwe limiet voor RX-data-blokken toegevoegd (`SMS_MAX_RX_DATA_BLOCKS = 63`).
- Decodepad uitgebreid met Motorola-signature/port-afhandeling en robuustere tekstextractie.

Effect:
- Langere SMS-berichten en betere compatibiliteit met Motorola/OEM-SMS framing.

### 2) SMS-opslag herwerkt (EEPROM + header/checksum + migratie)
Bestand:
- `MDUV380_firmware/application/source/functions/sms.c`

Wijzigingen:
- Opslaglogica opgesplitst met `smsStorageHeader_t`, header read/write en checksum-update.
- Inbox/sent CRUD leest en schrijft direct via EEPROM-adressen i.p.v. alleen RAM-buffers.
- Migratiepaden voor legacy opslagformaten aanwezig (v1/v4 -> nieuwe layout).
- `smsInboxStorageTick()` schrijft alleen wanneer radio/verwerking idle is (geen actieve slot/IRQ/SMS-send/decode pending).

Effect:
- Betere integriteit van SMS-opslag en minder kans op corruptie tijdens actieve DMR-verwerking.

### 3) RX dataverwerking in HR-C6000 aangepast
Bestand:
- `MDUV380_firmware/application/source/hardware/HR-C6000.c`

Wijzigingen:
- Data-sync frame wordt expliciet gedetecteerd en één keer gelezen in buffer.
- SMS frame-afhandeling uitgebreid naar datatypes `0x06`, `0x07` en `0x08`.
- Handler gebruikt hergebruikte buffer (`smsHandleReceivedDataFrame(...)`) i.p.v. losse ad-hoc read.

Effect:
- Betere deterministische RX-verwerking en bredere SMS frame-ondersteuning.

### 4) UI-verbeteringen voor SMS menu en berichtweergave
Bestand:
- `MDUV380_firmware/application/source/user_interface/menuSMS.c`

Wijzigingen:
- Compose-lengte is gekoppeld aan `SMS_MAX_TEXT_LENGTH` (dus 231 chars i.p.v. 64).
- Inbox/sent lijstweergave gebruikt message preview-helper en beter scrollvenster (`firstIndex`) zonder wrap-around gedrag.
- SMS view ondersteunt tekstnormalisatie, line wrapping, vertical scrolling (`smsViewTopLine`) en scrollbar.
- In `menuSMSOptions` is `KEY_GREEN` toegevoegd om direct naar root-menu terug te gaan.

Effect:
- Betere UX bij langere berichten en voorspelbaarder navigatie in lijsten en detailview.

### 5) UI globals uitgebreid met accessor-functies
Bestanden:
- `MDUV380_firmware/application/include/user_interface/uiGlobals.h`
- `MDUV380_firmware/application/source/user_interface/uiGlobals.c`

Wijzigingen:
- Toegevoegd: `uiGetPttToggledDown()`
- Toegevoegd: `uiSetPttToggledDown(bool isDown)`

Effect:
- Gecentraliseerde toegang tot PTT toggle-state.

### 6) Kleine credit-fix
Bestand:
- `MDUV380_firmware/application/source/user_interface/menuFirmwareInfoScreen.c`

Wijziging:
- Credittekst aangepast van `PD0FR - SMS` naar `PD0FR - SMS`.

## Niet-functionele/structurele wijzigingen
- Toegevoegd in `MDUV380_firmware`: `prepare` en `prepare.bat`.
- Verwijderd: meerdere gegenereerde `subdir.mk` bestanden onder `DM1701_FW`.
- Gewijzigd/toegevoegd/verwijderd: meerdere `.metadata` bestanden (Eclipse workspace-state).

## Opmerking
Deze changelog is gemaakt op basis van directory-diff (geen commit-historie), met filtering op build-artifacts om functionele verschillen zichtbaar te maken.
