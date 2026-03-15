# HOW TO USE SMS

Deze handleiding is radio-onafhankelijk en geldt voor de huidige SMS-implementatie in de firmware.

## 1. Voorwaarden

- Zet de radio in DMR (digitaal) mode.
- Zorg dat je zender-ID (DMR ID) goed ingesteld is.
- Voor verzenden moet een Private Call (PC) bestemming actief zijn.

## 2. SMS-menu openen

- Open het menu en ga naar SMS.
- Je ziet nu drie onderdelen:
  - SEND SMS
  - INBOX
  - SENT

## 3. Bericht verzenden (SEND SMS)

1. Kies `SEND SMS`.
2. Typ je bericht (maximaal 64 tekens).
3. Controleer dat een Private Call bestemming actief is.
4. Druk `Groen` om te verzenden.

Wat je ziet:

- `SMS TX` bij succesvolle start van zenden.
- Het bericht wordt automatisch opgeslagen in `SENT`.

## 4. INBOX gebruiken

### Berichten bekijken

1. Ga naar `INBOX`.
2. Kies een bericht.
3. Druk `Groen` om te openen.

### Berichten verwijderen

- In de lijst: druk `#` om geselecteerd bericht te verwijderen.
- In de berichtweergave: druk `#` om het geopende bericht te verwijderen.

## 5. SENT gebruiken

### Verzonden berichten bekijken

1. Ga naar `SENT`.
2. Kies een bericht.
3. Druk `Groen` om te openen.

### Bericht opnieuw versturen (resend)

- Open een bericht in `SENT`.
- Houd `6` ingedrukt om hetzelfde bericht opnieuw te versturen.

### Verzonden berichten verwijderen

- In de lijst: druk `#` om geselecteerd bericht te verwijderen.
- In de berichtweergave: druk `#` om het geopende bericht te verwijderen.

## 6. Sneltoets-overzicht

- `Groen`: openen / bevestigen / verzenden
- `Rood`: terug
- `#`: verwijderen (INBOX en SENT)
- `6` lang indrukken: resend in SENT-berichtweergave

## 7. Foutmeldingen en betekenis

- `DMR only`: je zit niet in digitale mode.
- `Select private call`: er is geen geldige private bestemming gekozen.
- `SMS busy`: zendpad is bezig; probeer opnieuw.
- `Empty message`: lege tekst.
- `Message too long`: bericht is langer dan 64 tekens.
- `ASCII only`: alleen ASCII tekens toegestaan in deze implementatie.

## 8. Praktische tips

- Test eerst met korte berichten (bijv. "test").
- Gebruik een stabiele private call setup voor betrouwbare verzending.
- Verwijder oude berichten met `#` om overzicht te houden.
