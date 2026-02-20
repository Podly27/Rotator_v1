# Arduino Rotátor

Rotátor pro anténní systémy s víceotáčkovým potenciometrem, TFT displejem a bezpečnostní logikou relé.

![Rotátor](rotator-ovladac.jpg)

## Verze 2.0 – dvouřídicí architektura UNO + NANO

Nová verze řeší rušení na dlouhém analogovém vedení tím, že:
- **NANO u rotátoru** čte potenciometr lokálně (A0), filtruje hodnotu a posílá ji digitálně.
- **UNO v shacku** přijímá data po 1 vodiči (open-collector UART), řídí TFT, relé, limity a piezo.

Komunikace: `P,<adc>,<crc>\n` při 9600 Bd.

## Quick start (v2)

1. Nahraj `rotator_node_nano/rotator_node_nano.ino` do **Arduino NANO**.
2. Nahraj `rotator_main/rotator_main.ino` do **Arduino UNO**.
3. Propoj 3 žíly mezi UNO a NANO:
   - GND
   - +V (5–12 V, při >5 V přes step-down na 5 V pro NANO)
   - DATA (open-collector dle `zapojeni_schema.txt`)
4. Ověř, že UNO přijímá data (na TFT stav `OK`, při výpadku `ERROR`).

## 📁 Struktura projektu

```
Rotator/
├── rotator_main/
│   └── rotator_main.ino            # UNO (shack) – hlavní program (verze 2)
├── rotator_node_nano/
│   └── rotator_node_nano.ino       # NANO (rotátor) – čtení potenciometru + TX
├── legacy/
│   ├── rotator_main_analog_v1.ino  # původní analogová verze (archiv)
│   └── zapojeni_schema_v1.txt      # původní schéma analog A0 (archiv)
├── zapojeni_schema.txt             # aktuální schéma (verze 2)
├── PROJEKT.md
└── README.md
```

## Legacy / v1

Původní analogová varianta (A0 po dlouhém kabelu + MCP6001/MCP6002 + RC filtr) byla zachována v adresáři `legacy/`.

## Dokumentace

- Kompletní popis projektu: `PROJEKT.md`
- Aktuální zapojení v2: `zapojeni_schema.txt`
- Archivní analogové zapojení: `legacy/zapojeni_schema_v1.txt`
