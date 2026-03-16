# SMS implementatie - gewijzigde bestanden

Dit document beschrijft welke bronbestanden zijn toegevoegd of aangepast voor de SMS-functionaliteit.

## Nieuw toegevoegd

- application/include/functions/sms.h
  - Nieuwe publieke API en datatypes voor SMS packing/queueing.
- application/source/functions/sms.c
  - Implementatie van SMS-opbouw:
    - DMR_Standard IP/UDP payload packing (UDP/5016)
    - UTF-16BE tekst in standaard payload
    - ETSI/DMR CRC32 op het volledige transportblok
    - CSBK opbouw
    - Data header opbouw
    - CRC16-CCITT berekening
    - Queue beheer
  - Ontvangstdecode valideert nu eerst het standaard DMR_Standard/IP/UDP-formaat en valt alleen voor compatibiliteit terug op legacy ruwe UTF-16 payloads.
- application/source/user_interface/menuSMS.c
  - Nieuwe UI schermen:
    - SMS menu
    - SMS compose
  - Tekstinvoer, bestemming-validatie, queue + trigger van verzending.

## Bestaande bestanden aangepast

- application/include/user_interface/menuSystem.h
  - Nieuwe menu IDs toegevoegd:
    - MENU_SMS_MENU
    - MENU_SMS_COMPOSE
  - Nieuwe menu handlers gedeclareerd:
    - menuSMSMenu(...)
    - menuSMSCompose(...)

- application/source/user_interface/menuSystem.c
  - SMS menu-items en handlers geregistreerd in de menu-tabellen.

- application/source/user_interface/uiChannelMode.c
  - Long press op groene toets opent SMS menu (niet op MD9600).

- application/source/user_interface/uiVFOMode.c
  - Long press op groene toets opent SMS menu (niet op MD9600).

- application/include/hardware/HR-C6000.h
  - Nieuwe SMS TX API toegevoegd:
    - HRC6000StartQueuedSMS()
    - HRC6000IsSendingSMS()

- application/source/hardware/HR-C6000.c
  - Integratie van SMS verzendflow in DMR TX state machine.
  - Buffering van CSBK/data-header/data-blokken.
  - Toegevoegd:
    - hrc6000SendSMSFrame()
    - hrc6000GetSmsDataType()
    - queue-consumptie en TX-start via HRC6000StartQueuedSMS().

## Bewust niet opgenomen als SMS-implementatie

- application/source/dmr_codec/codec_interface.c
  - Deze wijziging was een build/toolchain workaround (BL -> BLX), geen onderdeel van SMS-functionaliteit.

## Opmerking

Bestanden in buildmappen (zoals MDUV380_10W_PLUS_FW), tijdelijke testbestanden en tool-output zijn niet functioneel onderdeel van de SMS-implementatie.
