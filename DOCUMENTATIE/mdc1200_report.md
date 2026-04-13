# Samenvatting  
MDC-1200 (Motorola Data Communications 1200 baud) is een Motorola-ontwikkeld datasysteem voor analoge portofoons, oorspronkelijk bekend als **Stat-Alert**【2†L138-L142】. Het verstuurt **digitale informatie als AFSK-signalen** over een conventioneel FM-spraakkanaal. Typische toepassingen zijn PTT-ID (zenderidentificatie), noodgevallen, oproepmeldingen en statusberichten. De datarate is 1200 bit/s met **1 kHz (mark) en 1,8 kHz (space)** frequenties【2†L138-L142】. Een *MDC1200-burst* bevat 32 bits (16 bit systemcode + 16 bit radio-ID) plus foutcorrectie, ingekaderd in een **176-bits pakket** met voorloop- en synchronisatiepatronen【57†L17-L25】【42†L539-L547】. Deze studie beschrijft het protocol (origine en Motorola-specificaties), modulatie, bitformaten, timing (pre/post ID), datavelden (ID, status, nood, oproep), en codering (NRZI, convolutioneel, CRC). Ook komen compatibiliteit met APRS/DMR, ontvangervereisten, praktische implementatie in OpenGD77 (architectuur, API, DSP-algoritmes), test-/debugmethoden en wettelijke aspecten aan bod. 

---

## Oorsprong en Motorola-specificaties  
MDC-1200 is ontworpen door Motorola als onderdeel van het **Stat-Alert** (later MDC) systeem voor conventionele radiocommunicatie【2†L138-L142】. Het werd in de jaren ’80 geïntroduceerd voor bellersidentificatie (ANI/PTT-ID) en signaalfuncties in industriële en hulpdiensten-netwerken. Het protocol is uitgebreid gedocumenteerd in Motorola/Tait-rapporten en zelfs patenten【57†L17-L25】【42†L539-L547】. Kernpunten uit officiële bronnen: een MDC1200-sequentie bevat **32 bits payload** – bestaand uit een vaste 16-bit sequencecode (bijv. `PTT ID`-prefix of `EMERGENCY`-prefix) en een 16-bit radio-ID【57†L17-L25】. De radio-ID wordt geprogrammeerd als een 4-cijferige hexadecimale waarde (`0000`–`FFFF`)【57†L17-L25】; in decimale termen dus tot 65535 (max. 5 cijfers). Optioneel worden extra bits toegevoegd voor foutdetectie/-correctie (zie verder).  

Motorola gaf aanbevelingen voor signaaloverdracht en compatibiliteit: het MDC1200-signaal moet binnen de standaard FM-breedte (ca. 12,5 kHz) blijven en is “behaviorally compatibel” met andere MDC1200-systemen【12†L539-L547】. De datasnelheid (1200 baud) en frequenties (1,200/1,800 Hz) zijn vastgelegd en komen overeen met de **FSK-modulatie** van het protocol【2†L138-L142】. Motorola-patenten beschrijven bovendien de **176-bit framing**: 24 bits preamble voor bitsynchronisatie en 40 bits vast synchronisatiewoord, gevolgd door 112 bits gecodeerde data【42†L539-L547】. De totale zendtijd is ~290 ms per burst【42†L549-L554】 (in de praktijk ~0.2–0.3 s, afhankelijk van implementatie).  

> **Belangrijk:** Vanwege de encoding (“exclusive-or” bitvoorbewerking) zijn de gegevens **NRZI-geencodeerd**【36†L163-L172】. Dat betekent dat de modulator alleen toggles (1) of stiltes (0) doorgeeft; hierbij geldt een fout in één bit oftewel een omkering voor alle volgende bits【36†L169-L177】. De ontvanger moet hiermee rekening houden (zie Codering).

---

## Modulatie en transmissiedetails  
MDC-1200 gebruikt **audio-frequency shift keying (AFSK)** op een analoog FM-kanaal. De mark-tone (logic “1”) is 1200 Hz en de space-tone (logic “0”) is 1800 Hz【2†L138-L142】. De bit-snelheid is 1200 baud (bits per seconde), passend binnen een 12,5 kHz FM-kanaal. De modulatie is quasi-2FSK (frequentieschakeling), maar Motorola’s implementatie heeft bijzondere eisen voor frequentieprecisie (MSK-tegenstrijdigheid: 1% MER) en signaal-ruisverhouding (12 dB SINAD) om hoge betrouwbaarheid te garanderen【28†L0-L3】.

Belangrijke parameters:
- **Baud:** 1200 bits/s【2†L138-L142】  
- **Tonen:** mark = 1200 Hz, space = 1800 Hz【2†L138-L142】  
- **Deviation:** zodat de FSK-bursts midden in het audiobandbreedte van FM vallen (in praktijk enkele honderden Hz naversterking).  
- **FM-kanaal:** typisch 12,5 kHz breed, pre-emphasis ~6 dB/oct, dus hoge tonen versterkt. Aangezien de MDC-tonen laagfrequent (<2 kHz) zijn, beïnvloedt pre-emphasis ze niet sterk. Ontvangers gebruiken doorgaans de *discriminator-uitgang* (flat audio vóór deFM-emphasis) om de bitfeiten te extraheren met minimale vervorming.  

Als je de MDC1200-bursts bekijkt op een oscilloscoop of spectrogram, zie je een “chirp” van 1200–1800 Hz in wisselende blokken. Figuur 1 toont een voorbeeld van een MDC1200-pakket:  


---

## Bitframing en timing  
Een MDC1200-pakket begint meestal met **PTT-ID** (aanvangs-ID) of **post-ID** (eind-ID) afhankelijk van instelling. Beide bevatten dezelfde 176 bits structuur. De timingstructuur (volgens Motorola/Tait documenten) is:  

- **Voorlooppreambule:** 24 bits “1010…” of een vaste patroon, gebruikt voor bitsynchronisatie【42†L541-L549】. Doel: ontvanger klaarmaken voor de data (vergelijkbaar met “bit sync”).  
- **Woordsynchronisatie:** 40 bits vaste synchronisatiecode. Ontvanger vergelijkt voortdurend de laatste 40 ontvangen bits met een bekende code; bij ~35/40 match wordt frame-start gedetecteerd【42†L545-L552】.  
- **Payload-codewoord:** 112 bits, dit is de convolutional/internally-encoded data (zie Codering).  

Bij 1200 baud duurt het hele pakket rond **~176/1200 ≈ 0,147 s** in signaal, maar in praktijk ongeveer 0,2–0,3 s door extra pauzes en gating【42†L549-L554】. Na het codewoord volgt een korte stilstand (codewortindeling) en mogelijk een neuraal geluidje of streepje (indien *data mute* actief). 

**Voorgeschreven delay:** Bij *leading ID* wordt de burst direct na PTT gedrukt gezonden, maar vaak met een kleine *lead-in* (typisch ~0–250 ms) om te wachten op CTCSS-decoder of repeateraudio【57†L23-L27】. Bij *trailing ID* wordt de burst bij PTT-losgelaten verzonden, wat problemen met timing vermindert【42†L539-L547】. Veel radio’s laten keuze tussen leading/trailing, en een optie om de burst hoorbaar (tonen blijven) of alleen digitaal (onhoorbare data) af te handelen【2†L153-L160】. 

---

## Datavelden en protocollen  
De **kerninhoud** van een MDC1200-burst is de 32-bit payload【57†L17-L25】:  
- **16-bit constante**: bepaalt het type bericht (bijv. reguliere PTT-ID, noodknop, oproep, etc.). Motorola definieerde standaardwaarden (bijv. `I` voor initieel ID, `E` voor emergency).  
- **16-bit radio-ID**: unieke radioidentifier (geprogrammeerde hexadecimale code “0000”–“FFFF”)【57†L17-L25】.  

Deze 32 bits (soms aangevuld met een **CRC** voor integriteitscontrole【45†L23-L27】) worden vervolgens convolutioneel gecodeerd en verspreid (ge-“interleaved”) tot 112 bits codewoord【57†L23-L27】. De uitvoering (zie Codering) zorgt dat zelfs bij storingen het pakket met hoge waarschijnlijkheid correct kan worden hersteld【42†L539-L547】.  

Naast de PTT-ID gebruikt MDC1200 ook andere formats:  
- **Noodsignaal (Emergency):** vaak dezelfde payload-indeling, maar met een speciale constant en mogelijke herhalingen totdat er een ACK komt【2†L159-L168】.  
- **Selectieve oproepen (Call):** richten zich op één of meerdere radio’s; bevatten naast eigen ID en oproep-ID (subfields en subcodes, b.v. 8205 voor Spectra)【35†L323-L333】.  
- **Statusberichten:** drukken vooraf ingestelde tekstcodes uit (bijv. “02 on duty”). Dit is een variant op het oproepprotocol.  
- **Remote Kill/Stun:** stuurt commando’s om een radio op afstand uit- of aan te schakelen.  
- **Radio Check, Inhibit, etc.:** enkele informatieve codes.  

Radio’s met MDC1200 kunnen vaak een *aliaslijst* bevatten: inkomende radio-ID’s worden omgezet in namen voor weergave【12†L442-L450】. Dit vereist dat de ontvanger (zoals een open firmware) zelf een database bijhoudt. 

---

## Codering en signaalkwaliteit  
MDC-1200 bevat een **foutcorrectielaag** voor robuuste ontvangst. Volgens officiële beschrijvingen krijgt het 32-bit kernpakket een CRC-check en convolutionele codering【57†L23-L27】. Typisch wordt dit als volgt toegepast:  

1. **Payload CRC:** Voor verzending worden de 32 payload bits aangevuld met een CRC (mogelijk 16 of 32 bits) om bitfouten te detecteren【45†L23-L27】【57†L23-L27】.   
2. **Convolutionele code:** Daarna voert men een convolutionele coder uit (vermoedelijk rate 1/2 of 7/1, exacte details Motorola-proprietary). Dit levert ongeveer 2× zoveel bits, waarna een interleaver ze herschikt【57†L23-L27】.  
3. **Encoder output:** Het resultaat is de 112-bit “codewoord”【57†L23-L27】. Deze bevat redundantie waardoor 1 bit fout niet fataal is.  

**NRZI-encoding:** Voorafgaand aan modulatie past MDC-1200 een bit-wise exclusief-OR (XOR) toe tussen opeenvolgende bits【36†L169-L177】. Dit is functioneel gelijk aan **NRZI** (Non-Return-to-Zero-Invert), waarbij een bit ‘1’ een toonwisseling impliceert en ‘0’ toonbehoud. Hierdoor ontstaat geen vaste 1010-synchronisatiepuls; in plaats daarvan draagt het preamble-woord de klokinformatie. Het nadeel is dat één fout een keteneffect op alle volgende bits kan veroorzaken【36†L169-L177】. De decoder moet deze XOR terugdraaien: gewoonlijk veronderstelt men een bekende starttoestand en herleest het originele bitsignaal door telkens toe te voegen:  
```c
bit recovered = last_bit XOR received_bit;
last_bit = recovered;
```  
waarbij `received_bit` uit de 1200/1800 AFSK-demodulatie komt.  

**Synchronisatie:** Ontvangers zoeken eerst naar de preamble (24 bits) om kloksignalering op te zetten【42†L541-L549】. Daarna detecteren ze het 40-bit synchronisatiewoord door vergelijking met de vaste code【42†L545-L552】. Pas daarna worden de 112 bits data uitgelezen en door de foutcorrectiemechanismen gehaald. 

> **Audioniveau’s:** In de praktijk zit de MDC-afsk-burst laag in het FM-spectrum. Een goede decoder gebruikt *discriminator-audio* (flat, vóór de FM-emphasisfilter) op ongeveer enkele honderden millivolt. De drempel wordt ingesteld zodanig dat achtergrondruis en spraak geen vals ‘bitje’ genereren, maar de 1200/1800-tonen duidelijk detecteerbaar zijn. Voor ruisbegrenzing kan men bandfilteren rond 1200–1800 Hz en een AGC afstemmen. Pre-emphasis (voice shaping) is meestal *uitgeschakeld* of genegeerd bij het decoderen, omdat het de laagfrequente FSK-tonen niet significant versterkt. 

---

## MDC1200 vs APRS/DMR (compatibiliteit)  
Hoewel MDC1200 en APRS beide **AFSK bij ~1200 baud** gebruiken, zijn ze technisch onvergelijkbaar. De verschillen:  

- **Tonen:** MDC1200 gebruikt 1200/1800 Hz; APRS (Bell 202) gebruikt **1200/2200 Hz**. Een APRS-modem kan MDC-tonen **niet direct** decoderen, en omgekeerd【2†L138-L142】.  
- **Frame-opbouw:** APRS stuurt langere AX.25-datapakketten continu of in bursts, met callsign-adressering en checksums; MDC1200 heeft een kort, vast frame (32-bit-ID plus code)【42†L541-L549】.  
- **Context:** APRS is aaneengesloten 1200-bps data (geen spraak); MDC1200 is *intermittent* en gekoppeld aan PTT-signalen.  

**DMR:** Digitale Mobile Radio gebruikt geheel andere modulatie (TDMA, GMSK, 9600 bps), geen analoge audio. Daardoor kan een DMR-radio MDC1200 **niet direct detecteren**. Het enige raakvlak is conceptueel: in DMR kunnen radios ID’s (groep-ID, unit-ID) meegeven via het protocol, maar dat is volledig apart van MDC. 

**Gelaagde systemen:** In enkelbandige analoge netwerken kan men zowel DTMF, MDC1200 als andere signalen (bijv. 2-tone) combineren. Sommige amateur repeaters in Nederland filteren MDC1200-ruis weg; anderen gebruiken het juist voor toegang. Overeenkomst in bitnelheid of modulatie maakt systemen niet compatibel zonder expliciete bridge. 

---

## Implementatie in OpenGD77 (architectuur)  
Een firmware-implementatie in OpenGD77 voor MDC1200 vereist meerdere onderdelen:

1. **Optie en configuratie:** SchakelOptie `MDC1200 decode` (per kanaal) en `MDC1200 encode` (per zender) in de gebruikersinterface. Tevens instelbaar: *onhoorbare* vs *hoorbare piep* (data mute), *pre-ID* of *post-ID*, *alias-tabel*, enz.  
2. **Audiopad/tappen:** Tappunt in de RX-ontvanger: bij OpenGD77 zou dit net als DTMF-decoder de **ruwe audio** (bij voorkeur dis­crimi­nator-uitgang) moeten gebruiken. Idealiter vóór alle filtering die geluid kan vervormen. Een "flat" sample rate (bijv. 8–12 kHz) is voldoende.  
3. **Demodulator (Goertzel of FFT):** Twee Goertzel-filters (of bandpassfilters) voor 1200 Hz en 1800 Hz op de audio. Door de output-energie van beide te vergelijken krijgt men bits. [Pseudocode voorbeeld zie onder].  
4. **Bit-timing:** MDC-1200 heeft vaste baud (1200 Hz), dus de MCU kan een timer (bijv. 1200 interrupt per sec) gebruiken. Bij elke bit-interval somt de software het Goertzel-outputs of voert een DFT, en beslist welk bit (mark/space) dominant was.  
5. **Bitdecodering:** Geleverd is NRZI-geencodeerd. De code inverseert de XOR zoals eerder beschreven. Eventueel *bit destuffing* is hier niet nodig; synchronisatiepatronen zitten in de preamble.  
6. **Preamblesynchronisatie:** Detecteer het begin van een burst via een langdradige 24-bit patroon: bijv. een reeks toggles/vast patroon. Dit kan door continue correlatie over de laatste 100–200 bits (vergelijk b.v. de laatste 24 bit-decoder-outputs met verwachte sequentie). Deze stap geeft klokherstel.  
7. **Syncdetectie:** Bekijk de volgende 40 bits (wanneer 35+ bits overeenkomen) om exact frame-start te vinden【42†L545-L552】.  
8. **Payloadverwerking:** Lees vervolgens 112 bits codewoord in. Gebruik een convolutionele decoder (bijv. Viterbi) met bijbehorende CRC-check om de oorspronkelijke 32-bit payload te herstellen.  
9. **Interpretatie:** Splits de 32 bits in constant + radio-ID. Bepaal type (PTT-ID, oproep, etc.). Vergelijk ID met lokale alias-lijst (indien aanwezig) en toon naam/ID op scherm. Stuur evt. events naar DSP (bij encode) of logging.

### Geheugen en CPU  
Een MCU voor OpenGD77 (bijv. STM32) heeft voldoende power voor deze DSP. Goertzel berekent 2 complex accumulators per bit: bij 1200 bps en 12 kHz input zijn dat ~10 sam­pels per bit. De rekenintensiteit is laag (≈ 2·1200 keer per sec een paar multiplies). Viterbi-decoder voor 112 bit is ook haalbaar. Gewijzigd kodepatroon en structuren passen binnen enkele kB RAM (buffer ~128 bytes, paar variabelen) en CPU-cycli (kHz). 

### API en flow  
- **BG-taak/ISR:** In de ADC/PCM-interrupt wordt audio binnengehaald. Een second-elapsed (1200 Hz) timer markeert bit-einde.  
- **Detectie-flag:** Na detectie van een geldige preamble/sync wordt de volgende code ge-accumeerd. Succesvolle decode zet een vlag en getallen voor UI.  
- **Configuratie-structuur:** In geheugen een struct `mdcConfig` per kanaal (aan-uit, pre/post, alias-tabel-index etc.).  
- **UI:** Optie-menu’s in CPS voeg _MDC1200_ toe. Bij decode toont scherm tijdelijk “MDC ID ####” of opgegeven alias.  
- **Samenwerking met DTMF:** Veel code kan gemodelleerd naar DTMF-decode; delen als audio-invoer en taakbeheer kunnen gedeeld worden. 

### Demodulatorpseudocode (voorbeeld)  
```c
// Init Goertzel voor 1200, 1800 bij fs=12000Hz
GoertzelInit(1200);
GoertzelInit(1800);
bit_count=0;
sample_count=0;
power1200=power1800=0;

// Elke audio sample:
void onNewSample(int16_t audio) {
  power1200 += GoertzelStep(1200, audio);
  power1800 += GoertzelStep(1800, audio);
  sample_count++;
  if (sample_count >= (fs/1200)) {
    // einde van bitperiode
    bool bit = (power1200 > power1800); // 1=1200Hz sterkst
    processBit(bit);
    // reset voor next bit
    power1200 = power1800 = 0;
    sample_count = 0;
    bit_count++;
  }
}
```

```c
// NRZI-decoder voorbeeld:
bool last_bit = 0;
void processBit(bool received) {
  bool decoded = last_bit ^ received;
  last_bit = decoded;
  handleDecodedBit(decoded);  // b.v. buffer in preamble/sync checker
}
```

```c
// Synchronisatie (simplistisch):
while (!sync_found) {
  shiftRegister<<=1;
  shiftRegister |= decodedBit;
  if ((shiftRegister & 0xFFFFFF) == 0xAAAAAA) {  // veronderstelde preamble
    sync_found = true;
    bit_count = 0;
  }
}
// Daarna volgende 40 bits vergelijken met knownSyncWord.
```

---

## Interoperabiliteit, beveiliging en wetgeving  
**Compatibiliteit:** MDC1200 is analoog en open; veel fabrikanten (Kenwood “G-Star”, Icom, Harris) ondersteunen het zichtbaar. Er is geen gecentraliseerd ID-registry zoals bij DMR (RadioID). Daardoor is MDC1200 alleen betekenisvol binnen een netwerk met gedeelde codekaarten【2†L153-L162】. Niet elk systeem zal bijvoorbeeld **EMERGENCY**- of **Selective Call**-codes op dezelfde manier interpreteren. Uitwisseling tussen netwerken kan dus beperkt zijn.  

**Veiligheid:** MDC1200 heeft geen encryptie. RFC-savvy gebruikers kunnen data afluisteren (of zelf spammen). In amateurgebruik is dit meestal geen probleem, maar in professioneel verkeer staat ook in manuals dat activa-straatgang mogelijk is. Er zijn meldingen dat als *Inhibit* per ongeluk onvergrendeld is, kwaadwillenden via MDC een radio kunnen stunnen【35†L271-L280】. Functioneel moet de software daarom *Inhibit*-commando’s _wanneer geactiveerd_ terugsturen naar de radio, of het decoderen uitschakelen bij verhoogd risico. Juridisch is het uitzenden van MDC1200 gewoon legaal binnen de toegewezen band. Het draait om normale spraakwetgeving; pas op voor privacy bij het loggen van  gesproken (de facto) boodschappen zoals IDs.  

**Regulatoir:** Nederland volgt internationale amateur-/PMR-regels; MDC1200 is geen afgekeurde modulatie. Wel moet je net als bij DTMF zorgen dat het modulatieschema past bij de toegewezen bandbreedte. In principe geldt dat niets van emissie uitgangen schendt, zolang je binnen 12,5 kHz blijft. 

---

## Vergelijkingstabel MDC1200, APRS en DMR  

| Kenmerk             | MDC1200 (analoog)              | APRS (AX.25 packet)                   | DMR (digitale spraak)            |
|---------------------|-----------------------------|----------------------------------|-------------------------------|
| Modulatie           | AFSK (FSK 1200/1800 Hz)【2†L138-L142】 | AFSK (Bell 202: 1200/2200 Hz)       | 4FSK (TDMA, 9600 bps data)   |
| Baudrate            | 1200 bps【2†L138-L142】        | 1200 bps                         | ~9600 bps                    |
| Kanaalgebruik       | Gecombineerd met FM-spraak     | Gecombineerd met FM-spraak (APRS op kanalen) | Apart (eigen TDMA-slot)     |
| Datapakket          | 176 bits (24b preamble + 40b sync + 112b data)【42†L541-L549】 | 300+ bits (AX.25 frame, calls, CRC) | Laagkapitaaldata in spraakkader |
| Identificatie       | Lokaal 16-bit ID (4 hex)【57†L17-L25】  | Callsign via ARRL-database       | Wereldwijd 7-cijferig ID    |
| Foutcorrectie       | CRC + convolutioneel【57†L23-L27】  | CRC in AX.25                    | FEC ingebouwd                |
| Timing             | Burst op PTT (pre/post)【42†L539-L547】  | Periodiek of handmatig (digitaal)    | Continue spraak/data        |
| Compatibiliteit     | Alleen binnen an. RF-netzen     | Op elk FM kanaal (via packet)        | DMR-ecosysteem               |

---


---

---

## Conclusie  
MDC-1200 is een beproefd Motorola-dataformaat voor analoge radio’s【2†L138-L142】【57†L17-L25】. Een succesvolle OpenGD77-implementatie vereist aandacht voor modulatie (AFSK 1200/1800 Hz), bit-structuur (preamble/sync/data【42†L541-L549】) en decoding (Goertzel/NRZI/Viterbi) binnen de beperkte resources van de radio. Door de bekende paketcodering (24+40+112 bits) en referenties in Motorola-documenten【57†L17-L25】【42†L541-L549】 te volgen, kan een betrouwbare decoder worden opgebouwd. Zorg voor configureerbaarheid (AN-ID in- of uit, toontje, kanaalprofielen) en uitgebreide tests. MDC1200 voegt identiteits- en signaalinformatie toe zonder volledige digitale omschakeling.  

---
