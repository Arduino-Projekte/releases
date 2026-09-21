# Tresenwerk – Rezeptdatenbank

Das Gerät lädt die Rezepte von hier (Pflichtenheft R4, R8):

```
https://raw.githubusercontent.com/Arduino-Projekte/releases/refs/heads/main/firmware/mx/recipes/index.json
```

## Dateien

| Datei | Inhalt |
|---|---|
| `index.json` | Version, Anzahl, Größe und SHA-256 der anderen Dateien. **Wird vom Script erzeugt, nicht von Hand ändern.** |
| `recipes.json` | Rezepte |
| `ingredients.json` | Zutatenkatalog mit Kategorie, Vol-% und Aliasnamen |
| `img/<id>.jpg` | optionale Bilder, ca. 200 × 200 px, JPG (baseline, nicht progressiv) |
| `img/t/<id>.jpg`, `thumbs.json` | Vorschaubilder 128 × 128 für die Kacheln am Display. **Erzeugt das Script aus `img/`**, nicht von Hand ändern. |

Ablauf auf dem Gerät: `index.json` holen → hat sich ein SHA-256 geändert, die jeweilige Datei laden und prüfen → dann übernehmen.

## Zutat (`ingredients.json`)

```json
{ "id": "rum_white", "name": { "de": "Weißer Rum" }, "cat": "spirit", "abv": 37.5, "aliases": ["Bacardi"] }
```

- `id`: `a-z`, `0-9`, `_`, 2–32 Zeichen, dauerhaft stabil. Barschrank und Statistik verweisen darauf.
- `cat`: `spirit`, `liqueur`, `wine`, `juice`, `syrup`, `soft`, `dairy`
- `abv`: Vol-%. Daraus berechnet das Gerät „alkoholfrei“ und die Gramm Alkohol.
- `aliases`: Namen, unter denen die Zutat in Fremdimporten (CSV, TheCocktailDB) oder im Barschrank auftauchen kann (R7).
- `density`: optional, g/ml (K1b). Fehlt es, gilt der Wert der Kategorie: spirit 0,95 · liqueur 1,10 · wine 0,99 · juice 1,05 · syrup 1,30 · soft 1,04 · dairy 1,03.
- `carbonated`: optional, `true` bei Kohlensäure außerhalb der Kategorie `soft` (z. B. Prosecco) – wird schonend gefördert (X10).
- `pumpable`: optional, `false` = kommt nie in eine Pumpe (dickflüssig, stückig, Eiweiß …), steht aber im Barschrank und in der Einkaufsliste. Dann `"cat": "other"`. Beispiel: `coconut_cream` (gesüßte Kokoscreme) – im Gegensatz zu `coconut_syrup`.

## Rezept (`recipes.json`)

```json
{
  "id": "cuba_libre",
  "name": { "de": "Cuba Libre" },
  "glass": "longdrink",
  "tags": ["klassiker", "longdrink"],
  "pump":   [ { "ing": "rum_white", "ml": 40 }, { "ing": "cola", "ml": 150 } ],
  "manual": [ { "de": "Eiswürfel" }, { "de": "Limettenspalte" } ],
  "note":   { "de": "Grenadine zuletzt" },
  "img":    "img/cuba_libre.jpg"
}
```

- `pump`: **1–6** gepumpte, pumpbare Zutaten (jedes Rezept muss mit 6 geladenen Flaschen gehen; Geräte haben 8 bis 16 Pumpen), Mengen in **ml** bei Größe 1,0, Gesamtmenge ≤ 400 ml
- `manual`: Zutaten und Hinweise für die Hand (werden nicht gepumpt). Katalogzutaten mit Bezug `{ "ing": "coconut_cream", "ml": 30 }`, sonst freier Text `{ "de": "Limettenspalte" }`
- `note`, `img`, `tags`: optional
- `desc`: optional, ein Satz zum Drink (Geschmack, Herkunft) – höchstens 90 Zeichen je Sprache; `note`, `tip`, Schritttexte höchstens 60, Handzutaten als Text höchstens 30 (prüft das Skript)
- `tip`: optional, Profi-Hinweis (Technik, Garnitur) – **nur im Modus Experte** sichtbar
- `manual`-Einträge mit `"expert": true` sind **nur im Modus Experte** sichtbar, z. B. `{ "de": "1 Eiweiß", "expert": true }`
- **Alkohol:** „alkoholfrei“ gilt nur, wenn gepumpte **und** Handzutaten 0 % haben. Alkoholisches von Hand deshalb immer mit `ing`, nie als freier Text.
- `steps`: optional, geführter Ablauf (siehe unten) – **nur im Modus Experte**
- Texte sind Objekte je Sprache (`de`, `en`, `ru`, `fil`). Fehlt eine Sprache, zeigt das Gerät `de`. Regel: Katalog in allen vier Sprachen; Rezepttexte `de` + `en`, `ru`/`fil` nur bei Bedarf; `name` nur, wenn er sich vom deutschen unterscheidet (Einzelheiten: `exampels/prompt_rezepte.md`, Abschnitt 3a).

## Einfach und Experte (Pflichtenheft G10)

Der Admin wählt am Gerät, wie Rezepte gezeigt werden. **Jedes Rezept enthält immer alles**, der Modus blendet nur aus:

| | Einfach | Experte |
|---|---|---|
| Ablauf | alles pumpen, danach Handzutaten | Schritt für Schritt nach `steps` |
| `manual` ohne `expert` | sichtbar | sichtbar |
| `manual` mit `"expert": true` | ausgeblendet | sichtbar |
| `tip` | ausgeblendet | sichtbar |

Ein Rezept muss deshalb **im Modus Einfach für sich allein funktionieren**: `pump` und die normalen `manual`-Einträge ergeben einen trinkbaren Drink. Was nur mit Technik oder Zusatzaufwand geht (Eiweiß, Shaken, Zeste, Schichten), gehört in `steps`, `tip` oder `manual` mit `expert`.

Vollständige Beispiele, ein Beispielbild und der Arbeitsauftrag für Agenten, die Rezepte schreiben: `exampels/`. Der Ordner wird weder veröffentlicht noch vom Gerät geladen.

## Geführter Ablauf (`steps`, Pflichtenheft G1–G9)

Ab Firmware **V2026.9.21.3**, nur im Modus Experte. Ältere Firmware und der Modus Einfach übergehen das Feld.

Hat ein Rezept `steps`, führt das Gerät Schritt für Schritt durch; `pump` und `manual` auf oberster Ebene bleiben trotzdem Pflicht (Übersicht, Mixbarkeit, ältere Firmware). Die Summe aller Pumpschritte muss genau `pump` entsprechen.

```json
"steps": [
  { "t": "glass",  "sym": "shaker" },
  { "t": "pump",   "items": [ { "ing": "whiskey", "ml": 50 }, { "ing": "lemon_juice", "ml": 25 } ] },
  { "t": "manual", "sym": "hand",    "text": { "de": "1 Eiweiß dazugeben" } },
  { "t": "action", "sym": "shake",   "sec": 15 },
  { "t": "glass",  "sym": "tumbler", "text": { "de": "Mit Eis abseihen, Glas unterstellen" } },
  { "t": "pump",   "items": [ { "ing": "sugar_syrup", "ml": 15 } ] },
  { "t": "manual", "sym": "garnieren", "text": { "de": "Orangenzeste" } }
]
```

| Feld | Bedeutung |
|---|---|
| `t` | `glass` (Gefäß unterstellen/wechseln), `pump`, `manual` (Handzutat/Hinweis), `action` (shaken, rühren …) |
| `sym` | Symbolname. Gefäße: `shaker`, `ruehrglas`, `longdrink`, `highball`, `tumbler`, `cocktailschale`, `hurricane`, `weinglas`, `kupferbecher`, `shot`. Aktionen: `shake`, `ruehren`, `abseihen`, `eis`, `garnieren`, `hand` |
| `text` | optional; ohne Text zeigt das Gerät den Standardtext zum Symbol aus der Sprachdatei |
| `items` | nur bei `pump`: Zutaten mit ml bei Größe 1,0 |
| `sec` | nur bei `action`: Countdown in Sekunden, weiter auch per Tippen |

Weiter geht es bei `glass` über die Waage (Gefäß erkannt) oder per Tippen, bei `pump` automatisch, sonst per Tippen. Pumpen laufen nur in `pump`-Schritten.

## Veröffentlichen

```
powershell -NoProfile -ExecutionPolicy Bypass -File C:\GitHub\Cocktailmixer\tools\publish_recipes.ps1
```

Das Script prüft beide Dateien (IDs, unbekannte Zutaten, Mengen, Bilder) und schreibt `index.json` nur, wenn sich etwas geändert hat. Danach committen und pushen.
