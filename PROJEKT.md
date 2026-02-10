# Arduino Rotátor - Dokumentace projektu

## Přehled projektu

Arduino rotátor s víceotáčkovým potenciometrem, TFT displejem a bezpečnostními limity pro ovládání anténního motoru.

## Komponenty

- **Arduino UNO**
- **TFT displej**: ST7789V (240x320 pixelů)
- **Potenciometr**: 5kΩ lineární, víceotáčkový (10 otáček), typ 3590S
- **Převodovka**: 96:16 zubů (převod 6:1)
- **2x Relé moduly**: 5V, aktivní LOW
- **Piezo bzučák**: pro zvukové varování
- **Externí spínače**: pro ovládání motoru (nezapojené přes Arduino)

## Převodový poměr

- **Velké kolo** (motor): 96 zubů
- **Malé kolo** (potenciometr): 16 zubů
- **Převod**: 96 ÷ 16 = **6:1**
- Když se motor otočí 1x → potenciometr se otočí 6x
- Potenciometr: 10 otáček = 3600°
- Motor: 3600° ÷ 6 = 600° rozsah

## Kalibrace azimutu

- **Střed potenciometru** (50%, 5 otáček) = **180° azimut** (jih)
- **Každá otáčka potenciometru** od středu = **60° azimutu**
- Azimut se normalizuje na rozsah **0-360°**

### Příklady:
- Potenciometr 50% → 180° (jih)
- Potenciometr +1.5 otáček (65%) → 180° + 90° = 270° (západ)
- Potenciometr -1.5 otáček (35%) → 180° - 90° = 90° (východ)
- Potenciometr +3 otáčky (80%) → 180° + 180° = 0°/360° (sever)

## Zapojení

### Arduino → TFT Displej ST7789V
```
Arduino Pin    TFT Pin    Popis
-----------    -------    -----
5V             VCC        Napájení
GND            GND        Zem
Pin 10         CS         Chip Select
Pin 8          DC         Data/Command
Pin 9          RST        Reset
Pin 11 (MOSI)  SDA        Serial Data
Pin 13 (SCK)   SCL        Serial Clock
```

### Arduino → Potenciometr
```
Arduino Pin    Potenciometr    Popis
-----------    -------------    -----
5V             Pin 1            Napájení
A0             Pin 2            Střední vývod (dělič)
GND            Pin 3            Zem
```

### Arduino → Relé moduly
```
Arduino Pin    Relé Pin    Popis
-----------    ---------    -----
Pin 2          IN1          Relé pro motor doleva
Pin 3          IN2          Relé pro motor doprava
5V             VCC          Napájení relé
GND            GND          Zem relé
```

### Arduino → Piezo bzučák
```
Arduino Pin    Piezo    Popis
-----------    ------    -----
Pin 7          +         Pozitivní vývod
GND            -         Negativní vývod
```

## Funkce programu

### Zobrazení
- **TFT displej** zobrazuje:
  - Bílou kružnici s červeným trojúhelníkem ukazujícím azimut
  - Velké žluté číslo (font velikost 6) uprostřed - aktuální azimut
  - Procenta potenciometru (levý dolní roh)
  - Varování "LIMIT!" při dosažení limitů (pravý dolní roh)

### Bezpečnostní limity
- **Dolní limit**: 0-10% potenciometru → vypne RELAY_LEFT (motor doleva)
- **Horní limit**: 90-100% potenciometru → vypne RELAY_RIGHT (motor doprava)
- **Bezpečná zóna**: 10-90%
- **Zvukové varování**: piezo bzučák při dosažení limitu

### Relé logika
- Relé jsou **aktivní LOW** (HIGH = vypnuto, LOW = zapnuto)
- Motor je ovládán externími spínači
- Arduino pouze vypíná relé při bezpečnostních limitech
- V bezpečné zóně jsou relé vypnutá (HIGH)


## Stabilizace analogového měření po 30 m kabelu

Pro spolehlivé čtení víceotáčkového potenciometru v přítomnosti vodičů 230 V je doporučeno kombinovat HW i SW filtraci:

- **Buffer u potenciometru (MCP6001/MCP6002 jako sledovač)**: vysoká vstupní impedance nezatěžuje jezdec potenciometru a nízká výstupní impedance lépe budí dlouhé vedení.
- **RC filtr u Arduina (220 Ω + 100 nF na A0, volitelně +1 µF)**: tlumí VF rušení a krátké špičky.
- **Software filtrace + hystereze limitů**: oversampling/průměr, EMA low-pass a limity s odděleným ON/OFF prahem zabrání „cvakání“ relé.

### Fyzické umístění součástek
- 100 nF u opampu dej co nejblíž pinům VDD/VSS.
- 220 Ω + 100 nF umísti co nejblíž pinu A0 na Arduino desce.
- Analogový signál a GND veď mimo svazek relé a mimo svorky 230 V.

### Doporučení k rozložení žil v 7žilovém kabelu
- Motorové žíly (COM + 2 směry) drž pohromadě.
- Analogový signál potenciometru veď co nejdál od motorových žil.
- Referenční GND dej vedle analogového signálu.

> Poznámka: vedení 230 V a 5 V v jednom kabelu je kompromisní řešení; dbej na izolaci, bezpečné oddělení obvodů a platné normy.

## Instalace

### Potřebné knihovny (Arduino IDE)
1. **Adafruit GFX Library**
2. **Adafruit ST7789 Library**
3. **SPI Library** (součást Arduino IDE)

### Nahrání programu
1. Otevři Arduino IDE 2.x
2. Otevři soubor `rotator_main/rotator_main.ino`
3. Vyber **Tools → Board → Arduino UNO**
4. Vyber **Tools → Port → /dev/cu.usbserial-xxx**
5. Klikni na **Upload** (šipka)

## Kalibrace při prvním použití

1. Nastav potenciometr přibližně na **střed** (5 otáček)
2. Otoč anténu směrem na **jih** (180°)
3. Jemně dolaď potenciometr, aby displej zobrazoval **180°**
4. Nyní je systém zkalibrovaný

## Technické parametry

- **Napájení Arduino**: 7-12V DC nebo USB
- **Spotřeba**: cca 200mA (bez motoru)
- **Rozsah azimutu**: 0-360° (kontinuální)
- **Rozsah potenciometru**: 10 otáček
- **Přesnost**: ±1° (závisí na mechanice)
- **Obnovovací frekvence displeje**: cca 20 Hz

## Řešení problémů

| Problém | Možná příčina | Řešení |
|---------|---------------|---------|
| Displej se nezobrazuje | Špatné zapojení SPI | Zkontroluj piny 8,9,10,11,13 |
| Nesprávné úhly | Špatná kalibrace | Nastav střed potenciometru na 180° |
| Relé nereaguje | Aktivní HIGH místo LOW | Zkontroluj typ relé modulu |
| Motor se nevypíná | Špatné zapojení relé | Zkontroluj zapojení mezi relé a motorem |
| Čísla blikají | - | Normální, standardní font je pixelový |

## Možná rozšíření

- [ ] EEPROM pro ukládání kalibrace
- [ ] WiFi/Bluetooth pro vzdálené ovládání
- [ ] Podpora pro elevation (elevaci)
- [ ] GPS kompas pro automatickou kalibraci
- [ ] Web rozhraní pro ovládání
- [ ] Podpora pro satelitní tracking

## Autor

Projekt vytvořen s pomocí AI asistenta pro amatérské radioamatérské použití.

## Licence

Open source - volně použitelné pro nekomerční účely.

---

**Verze:** 1.0  
**Datum:** Listopad 2025  
**Status:** Stabilní verze
