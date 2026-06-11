# Bevindingen opslag/flash/EEPROM + voorstel SMS-opslag


### Settings-opslag gaat via settingsStorage -> EEPROM -> SPI Flash
- Settings API:
  - [V2/MDUV380_firmware/application/include/interfaces/settingsStorage.h](V2/MDUV380_firmware/application/include/interfaces/settingsStorage.h)
  - [V2/MDUV380_firmware/application/source/interfaces/settingsStorage.c](V2/MDUV380_firmware/application/source/interfaces/settingsStorage.c)
- `settingsStorageRead/Write()` gebruikt `EEPROM_Read/Write()`.
- `EEPROM_Read/Write()` is ge-emuleerd op SPI flash:
  - [V2/MDUV380_firmware/application/source/hardware/EEPROM.c](V2/MDUV380_firmware/application/source/hardware/EEPROM.c)
  - Intern: `SPI_Flash_read/write(...)`
- Conclusie: er is al een werkende persistente opslaglaag aanwezig die we kunnen hergebruiken.

## 3) Voorstel: persistente SMS-inbox in flash

## Doel
- Inbox behouden over reboot.
- Schrijffrequentie beheersen (flash-slijtage).
- Veilige recovery bij onverwachte reset (power loss).

## Datamodel (compact)
- Header:
  - magic
  - versie
  - sequence counter
  - aantal records
  - CRC32
- Record per SMS:
  - sourceId (uint32)
  - timestamp (optioneel)
  - text[65]
  - flags (read/deleted)
  - CRC16 of CRC32 per record (optioneel)

## Opslagstrategie
- Gebruik een **ring/journal** in gereserveerde SPI-flash regio.
- Schrijf events append-only:
  - NEW_MESSAGE
  - DELETE_MESSAGE
  - CLEAR_INBOX
- Periodiek compacteren (garbage collect) naar nieuw segment.
- Bij boot: journal replay -> RAM inbox opbouwen.

## Schrijfbeleid (wear control)
- Niet elke UI-actie direct full rewrite.
- Debounce/flush timer (bijv. 1-3 s) of batch op events.
- Hard flush bij shutdown-pad indien beschikbaar.

## Fouttolerantie
- Header + entry CRC.
- Sequence counter voor nieuwste geldige snapshot.
- Bij corruptie: laatste valide snapshot gebruiken en veilig doorstarten.

## 4) Waar implementeren

### Nieuwe module
- Voeg toe:
  - `application/include/functions/smsStorage.h`
  - `application/source/functions/smsStorage.c`

### Koppelpunt met huidige SMS-logica
- In [V2/MDUV380_firmware/application/source/functions/sms.c](V2/MDUV380_firmware/application/source/functions/sms.c):
  - Na `smsStoreInboxMessage(...)` -> persist event NEW_MESSAGE.
  - In `smsDeleteInboxMessage(...)` -> persist event DELETE_MESSAGE.
  - In `smsClearInbox(...)` -> persist event CLEAR_INBOX.
  - In `smsInit()` -> `smsStorageLoadInbox(...)` om RAM inbox te herstellen.

### Storage backend
- Primair via bestaande EEPROM API (`EEPROM_Read/Write`) of direct `SPI_Flash_*` voor grotere, gesegmenteerde opslag.
- Definitieve regio-adressering eerst valideren tegen bestaande codeplug/settings/gps logging gebieden.

## 5) Aanbevolen iteratieplan

1. MVP:
- Full snapshot save/load van inbox (max 8 berichten).
- Laden bij boot, opslaan bij add/delete/clear.

2. Verbetering:
- Journaling + CRC + sequence.
- Debounced flush en recovery tests.

3. Productiehardening:
- Flash wear test.
- Power-cut test tijdens write.
- Backward compatibility met oudere firmware layouts.

## 6) Kernconclusie
- De infrastructuur voor persistente opslag is al aanwezig.
- De SMS-inbox zelf is nu RAM-only en dus niet persistent.
- Met een kleine storage-laag bovenop de bestaande flash/EEPROM route is persistente SMS-opslag goed haalbaar zonder grote architectuurwijziging.
