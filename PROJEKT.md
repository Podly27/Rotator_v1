# Arduino Rotátor - Dokumentace projektu

## Přehled

Projekt řídí anténní rotátor s víceotáčkovým potenciometrem, TFT displejem, bezpečnostními limity a relé interlockem.

## Verze 2.0 – UNO + NANO

### Proč změna
V původní verzi byl analogový signál potenciometru veden do UNO na A0 přes dlouhý kabel (~30 m), často společně s vodiči motoru/230 V. To zvyšuje riziko rušení.

Verze 2.0 proto používá:
- **Arduino NANO u rotátoru**: měří potenciometr lokálně.
- **Arduino UNO v shacku**: přijímá již digitální data a provádí UI + bezpečnostní logiku.

### Komunikační protokol
- Baudrate: **9600 Bd**
- Formát: `P,<adc>,<crc>\n`
  - `<adc>`: 0..1023
  - `<crc>`: XOR všech znaků payloadu `P,<adc>`
- UNO používá paket až po ověření CRC.
- Nevalidní pakety jsou ignorovány a počítány.

### FAIL-SAFE (UNO)
- Pokud nepřijde validní paket déle než `1000 ms`:
  - stav `ERROR` na TFT,
  - blokace obou směrů (obě relé do bezpečného stavu).
- Hystereze limitů zůstává:
  - low ON ≤ 9 %, OFF ≥ 12 %
  - high ON ≥ 91 %, OFF ≤ 88 %
- Piezo pípá až po 200 ms trvání limitu (anti-cvakání).

## Zapojení v2 (shrnutí)

### UNO (shack)
- RX data z linky: **D8** (SoftwareSerial)
- TX diagnostika: **D9** (volitelné)
- Relé: D2, D3 (aktivní LOW)
- Piezo: D7
- TFT ST7789: CS=10, DC=A1, RST=A2, MOSI=11, SCK=13

### NANO (rotátor)
- Potenciometr: A0
- TX open-collector: D8 -> 1k -> báze NPN
- Open-collector tranzistor: emitor na GND, kolektor na DATA

### 3žilové propojení UNO ↔ NANO
1. GND (společná)
2. +V (5–12 V; při >5 V step-down na 5 V pro NANO)
3. DATA (1-wire UART open-collector)

Podrobnosti v `zapojeni_schema.txt`.

## Kalibrace azimutu (zachováno)
- 50 % potenciometru = 180° azimut (jih)
- Převod zůstává 6:1 (96:16)
- Výpočet se normalizuje na 0–360°

## Legacy / v1

Původní analogová varianta nebyla odstraněna, je archivována:
- `legacy/rotator_main_analog_v1.ino`
- `legacy/zapojeni_schema_v1.txt`

Použij ji jen pokud potřebuješ původní analogovou architekturu s A0 po kabelu.

## Bezpečnostní upozornění

Vedení 230 V a nízkonapěťových signálů ve společném kabelu zvyšuje riziko:
- rušení,
- indukovaných špiček,
- nebezpečné poruchy izolace.

Montáž prováděj podle platných norem, s důsledným oddělením obvodů a kvalitní izolací.
