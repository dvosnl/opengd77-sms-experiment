# ETSI DMR SMS EN ACK MECHANISME (TECHNISCHE SAMENVATTING)

Focus: ETSI DMR Tier II Air Interface Doel: Implementatie in firmware
(bijv. STM32 radio's zoals TYT UV380 / UV390)

------------------------------------------------------------------------

# 1 Overzicht van DMR Data Services

DMR ondersteunt data transmissie bovenop het spraakkanaal via TDMA.

Belangrijke eigenschappen:

-   4FSK modulatie
-   9600 bit/s bitrate
-   2-slot TDMA
-   30 ms per slot

DMR data transmissie bestaat uit:

-   CSBK (Control Signalling Block)
-   Data Header
-   Data Blocks

SMS wordt verzonden als packet data service.

------------------------------------------------------------------------

# 2 Basisstructuur van een DMR Data Transmissie

Typische volgorde:

CSBK PREAMBLE\
DATA HEADER\
DATA BLOCK 1\
DATA BLOCK 2\
...\
DATA BLOCK N

Data blocks bevatten de payload van het bericht.

------------------------------------------------------------------------

# 3 Data Header Structuur

De Data Header bevat metadata van het datapakket.

Belangrijke velden:

Group flag -- groepsbericht of individueel\
Response requested -- vraagt om ACK\
Format -- type data\
SAP -- service access point\
Destination ID -- doel radio\
Source ID -- bron radio\
Blocks to follow -- aantal datablocks\
Fragment sequence number -- volgnummer fragment

Het veld Response requested activeert het ACK mechanisme.

------------------------------------------------------------------------

# 4 Data Block Encoding

Payload blokken worden gecodeerd met:

BPTC (196,96)

Dit betekent:

-   96 data bits
-   100 foutcorrectie bits

Doel:

-   robuust transport
-   foutcorrectie bij slechte RF omstandigheden

------------------------------------------------------------------------

# 5 Fragmentatie

DMR data berichten worden opgesplitst in meerdere blokken.

Voorbeeld:

Header\
Block 1\
Block 2\
Block 3\
Block 4

De header bevat het aantal blokken.

------------------------------------------------------------------------

# 6 ACK Mechanisme in ETSI DMR

DMR ondersteunt twee transportmodi.

Unconfirmed Data

Geen bevestiging nodig.

Flow:

Radio A → Data\
Radio B → ontvangt\
Geen reply

Wordt vaak gebruikt voor korte data of telemetry.

------------------------------------------------------------------------

Confirmed Data

Hier wordt een ACK vereist.

Flow:

Radio A → Data Header (Response requested)\
Radio B → ontvangst\
Radio B → stuurt ACK

Als ACK niet wordt ontvangen:

zender → retransmission

------------------------------------------------------------------------

# 7 ACK Frame Structuur

ACK wordt verzonden als een Data Response PDU.

Belangrijke velden:

Response type -- ACK of NACK\
Destination ID -- originele zender\
Source ID -- ontvanger\
Sequence number -- welk pakket bevestigd wordt

ACK bevestigt dat:

-   header ontvangen
-   datablocks correct ontvangen

------------------------------------------------------------------------

# 8 Retransmission Mechanisme

Wanneer confirmed data gebruikt wordt:

1.  zender stuurt data
2.  ontvanger stuurt ACK
3.  geen ACK → retransmit

Retransmission timer wordt bepaald door radio implementatie.

------------------------------------------------------------------------

# 9 ACK Types

ACK -- pakket ontvangen\
NACK -- fout in ontvangst\
BUSY -- ontvanger bezet\
REJECT -- pakket geweigerd

------------------------------------------------------------------------

# 10 Sequence Numbers

Om duplicaten te voorkomen gebruikt DMR:

-   Send sequence number
-   Fragment sequence number

Dit maakt mogelijk:

-   fragment reassembly
-   retransmission detectie

------------------------------------------------------------------------

# 11 Fragment Reassembly

Ontvanger bewaart fragments totdat alle blokken ontvangen zijn.

Voorbeeld:

Fragment 0\
Fragment 1\
Fragment 2\
Fragment 3

Wanneer compleet:

payload reconstrueren\
ACK sturen

------------------------------------------------------------------------

# 12 Timeout en Retransmit

Zender start timer na verzending.

Scenario:

Data verzonden\
Timer start\
ACK ontvangen → stop\
Geen ACK → resend

Maximum retries worden door firmware bepaald.

------------------------------------------------------------------------

# 13 Verschillen met Vendor SMS

Veel commerciële radio's gebruiken:

-   vendor payloads
-   Motorola TMS
-   Hytera TMP

Maar transportlaag blijft ETSI DMR data service.

------------------------------------------------------------------------

# 14 Praktische Implementatie in Firmware

Firmware moet implementeren:

1.  Data Header generatie
2.  Fragmentatie
3.  BPTC encoding
4.  ACK parsing
5.  Retransmission logica

------------------------------------------------------------------------

# 15 Minimale SMS Implementatie

Voor basis SMS zonder ACK:

CSBK\
Data Header (unconfirmed)\
Datablocks

Voor betrouwbare SMS:

CSBK\
Data Header (confirmed)\
Datablocks\
ACK response

------------------------------------------------------------------------

# 16 Samenvatting

ETSI DMR SMS gebruikt het standaard packet data mechanisme.

Belangrijke componenten:

-   Data Header
-   Fragmentatie
-   BPTC encoding
-   Confirmed data mode
-   ACK responses

Voor firmware implementatie zijn vooral belangrijk:

-   Response requested flag
-   sequence numbers
-   retransmission logica
