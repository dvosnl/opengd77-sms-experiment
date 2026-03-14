SMS implementatie - gewijzigde bestanden
========================================

Dit document beschrijft welke bronbestanden zijn toegevoegd of aangepast voor de SMS-functionaliteit.

Nieuw toegevoegd
---------------
- application/include/functions/sms.h
  Nieuwe publieke API en datatypes voor SMS packing/queueing.
- application/source/functions/sms.c
  Implementatie van SMS-opbouw: UTF-16BE payload, CSBK, data header, CRC16-CCITT en queuebeheer.
- application/source/user_interface/menuSMS.c
  Nieuwe UI schermen (SMS menu + compose), tekstinvoer en verzendtrigger.

Bestaande bestanden aangepast
-----------------------------
- application/include/user_interface/menuSystem.h
  Nieuwe menu IDs en handler-declaraties voor SMS.
- application/source/user_interface/menuSystem.c
  SMS menu-items/handlers toegevoegd aan menu-tabellen.
- application/source/user_interface/uiChannelMode.c
  Long press groen opent SMS menu (niet op MD9600).
- application/source/user_interface/uiVFOMode.c
  Long press groen opent SMS menu (niet op MD9600).
- application/include/hardware/HR-C6000.h
  Nieuwe SMS TX API: HRC6000StartQueuedSMS(), HRC6000IsSendingSMS().
- application/source/hardware/HR-C6000.c
  SMS verzendflow in DMR TX state machine en frame-uitsturing.

Niet opgenomen als SMS-implementatie
------------------------------------
- application/source/dmr_codec/codec_interface.c
  Dit is een build/toolchain workaround, geen SMS-functionaliteit.

Opmerking
---------
Build-output en tijdelijke testbestanden zijn niet functioneel onderdeel van de SMS-implementatie.
