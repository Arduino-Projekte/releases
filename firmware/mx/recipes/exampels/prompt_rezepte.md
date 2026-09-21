# Arbeitsauftrag: Rezepte für den Cocktailmixer „Tresenwerk“ schreiben

Diesen Text vollständig an einen neuen Chat oder Agenten geben. Er braucht kein weiteres Vorwissen.
Am Ende steht, welche Angaben du (der Auftraggeber) jedes Mal ergänzt.

---

## 1. Worum es geht

Tresenwerk ist ein automatischer Cocktailmixer mit **8 Pumpen** (spätere Geräte bis 16). Er pumpt flüssige Zutaten in ml ab, alles andere
(Eis, Früchte, Eiweiß, Garnitur) macht ein Mensch von Hand. Das Gerät zeigt Rezepte auf einem 7"-Display und im Browser,
in vier Sprachen: Deutsch (`de`, Pflicht), Englisch (`en`), Russisch (`ru`), Filipino (`fil`).

Jedes Rezept gibt es in **zwei Darstellungen**, zwischen denen der Admin am Gerät umschaltet:

- **Einfach:** alle gepumpten Zutaten auf einmal ins Glas, danach eine kurze Liste für die Hand. Für Gäste und Partys.
- **Experte:** geführter Ablauf Schritt für Schritt (Shaker, Shaken, Abseihen, Garnieren), mit Profi-Tipp.

**Ein Rezept deckt immer beide Modi ab.** Der Modus blendet nur aus. Daraus folgen zwei Regeln:
**Im Modus Einfach muss das Rezept für sich allein einen trinkbaren Drink ergeben.**
**`steps` sind Pflicht, sobald der Drink mehr ist als „alles ins Glas“** (Shaker, Rührglas, Reihenfolge, Handgriff
zwischen zwei Pumpvorgängen). Reine Longdrinks ohne Technik (Cuba Libre, Gin Tonic) brauchen keine `steps` – dann
zeigt auch der Modus Experte „alles ins Glas“, ergänzt um `tip` und Handzutaten mit `expert`.

## 1a. Bereits entschieden – bitte nicht nachfragen

1. **Pumpen und Belegung.** Ein Gerät hat 8 Pumpen (spätere Geräte bis 16). Welche Flasche an welcher Pumpe hängt,
   legt der Betreiber am Gerät fest und steckt bei Bedarf um. Du musst **keine gemeinsame Belegung** für eine Lieferung
   planen: Jedes Rezept darf **beliebige Zutaten aus dem Katalog** kombinieren.
   **Aber: höchstens 6 gepumpte Zutaten je Rezept** – jedes Rezept muss mit nur 6 geladenen Flaschen mixbar sein.
   Braucht das Original mehr, wandern die unwichtigsten in `manual` (zuerst Auffüller wie Cola oder Soda, dann kleine
   Sirup- oder Saftmengen), siehe `long_island` im Bestand.
2. **Zielstand ist der geplante Funktionsumfang,** nicht der heutige Firmwarestand. Schreibe `steps`, `tip`, `desc`,
   `expert` und Handzutaten mit Katalogbezug vollständig und richtig; die Firmware zieht nach.
3. **Sirup und Creme sind getrennt.** Pumpbar ist nur, was im Katalog **nicht** `"pumpable": false` hat.
   `coconut_syrup` (Kokossirup, flüssig) wird gepumpt, `coconut_cream` (gesüßte Kokoscreme, dickflüssig) nie – sie
   steht im Barschrank, kommt aber von Hand dazu: `{ "ing": "coconut_cream", "ml": 30 }` in `manual`.
4. **Bilder zeigen die Expertenversion** – also mit Schaum, Zeste, Schichten und Garnitur aus den Expertenschritten.
5. **Mengen:** Grundlage sind die klassischen Rezepte (IBA, wo vorhanden). Größe 1,0 = eine normale Portion.
   Eis zählt nie in die ml-Summe.
6. **Alles andere, was unklar ist:** triff eine vernünftige Annahme, arbeite weiter und nenne sie im Abschnitt
   **„Annahmen“** deiner Lieferung (Abschnitt 2). Frage nicht nach.

## 2. Was du lieferst

1. Für jedes Rezept **ein JSON-Objekt** für die Datei `recipes.json` (Format in Abschnitt 4).
2. Falls eine Zutat im Katalog (Abschnitt 6) fehlt: **ein JSON-Objekt** für `ingredients.json` (Format ebenfalls Abschnitt 6).
   Erfinde keine Katalog-IDs, ohne sie so zu liefern.
3. Für jedes Rezept **eine Bildbeschreibung** für die Bilderzeugung (Abschnitt 8) – oder das Bild selbst, wenn du Bilder erzeugen kannst.
4. Eine **Prüfliste** (Abschnitt 9), bei der du jeden Punkt mit „ok“ bestätigst.
5. Einen Abschnitt **„Annahmen“**: jede Entscheidung, die du ohne Vorgabe getroffen hast, in einer Zeile
   (z. B. „Mojito: Minze und Zucker von Hand, weil nicht pumpbar“). Leer lassen, wenn es keine gab.

Liefere das JSON **ohne Kommentare** (JSON kennt keine), in einem Codeblock je Datei, UTF-8, echte Umlaute.

## 3. Harte Regeln

- **IDs:** nur `a-z`, `0-9`, `_`, 2–32 Zeichen, englisch, sprechend (`whiskey_sour`, `pina_colada`). Einmal vergeben, nie ändern.
- **Mengen in ml** bei Größe 1,0. Je Zutat 1–300 ml, **Summe aller gepumpten Zutaten ≤ 400 ml**. Üblich: Shortdrink 60–120 ml, Longdrink 180–260 ml.
- **Höchstens 6 gepumpte Zutaten** (Abschnitt 1a), jede nur einmal in `pump`.
- **Gepumpt wird nur, was im Katalog steht und pumpbar ist.** Eis, Früchte, Kräuter, Zucker, Salzrand und Katalogzutaten
  mit `"pumpable": false` gehören in `manual`.
- **Eine Zutat steht entweder in `pump` oder in `manual`**, nie in beiden.
- **Handzutaten aus dem Katalog** mit Katalogbezug angeben: `{ "ing": "coconut_cream", "ml": 30 }` – das Gerät setzt den
  Namen in der richtigen Sprache ein und kennt die Zutat für Barschrank und Einkaufsliste. Freier Text nur für Dinge
  ohne Katalogeintrag: `{ "de": "Limettenspalte", "en": "Lime wedge" }`. Beide Formen dürfen `"expert": true` haben.
- **Kohlensäure** (Cola, Tonic, Soda, Prosecco …) zählt normal in `pump`; das Gerät fördert sie schonend.
- **Texte kurz – feste Obergrenzen je Sprache, das Prüfskript lehnt längere ab:** Handzutat als freier Text **30 Zeichen**
  („Eiswürfel“, „Limettenspalte“), `note`, `tip` und Schritt-`text` **60 Zeichen**, `desc` **90 Zeichen**. Das Display ist klein.
- **Sprachen:** siehe Abschnitt 3a – `de` immer, `en` fast immer, `ru`/`fil` nur wo verlangt.
- **Keine Markennamen** in Texten, wo es einen Gattungsnamen gibt (nicht „Bacardi“, sondern „weißer Rum“; nicht
  „Cointreau“, sondern „Triple Sec“). **Ausnahme:** Die Zutat ist selbst ein Markenprodukt ohne gängigen Gattungsnamen
  (Aperol, Campari) – dann heißt sie so, auch als Katalogeintrag. Übrige Marken nur als `aliases`.
- **Nichts erfinden, was es nicht gibt:** keine Felder außer den unten beschriebenen.

## 3a. Sprachen

Jeder Text ist ein Objekt je Sprache: `{ "de": "…", "en": "…" }`. Das Gerät zeigt die eingestellte Sprache;
**fehlt sie, zeigt es Deutsch.** Fehlende Übersetzungen sind also kein Fehler, doppelte Pflege aber Aufwand –
jede spätere Änderung muss in allen Sprachen nachgezogen werden. Deshalb nur übersetzen, was sich unterscheidet:

| Feld | `de` | `en` | `ru`, `fil` |
|---|---|---|---|
| `name` | Pflicht | **nur, wenn anders als `de`** | nur, wenn anders als `de` (z. B. russisch in Kyrillisch) |
| `manual` (freier Text), `note`, Schritt-`text` | Pflicht | Pflicht | nur, wenn im Auftrag verlangt (Handzutaten mit `ing` brauchen keinen Text) |
| `desc`, `tip` | Pflicht | Pflicht | weglassen, außer im Auftrag verlangt |
| neue Zutat im Katalog (`name`) | Pflicht | Pflicht | **Pflicht** (der Katalog ist vollständig viersprachig) |

- Internationale Drinknamen („Mojito“, „Cuba Libre“) heißen überall gleich: nur `de` angeben.
- Zutaten aus dem Katalog in `pump` und `manual` immer über `ing` angeben, nicht als Text – das Gerät übersetzt ihre Namen selbst. In Schritttexten dürfen sie genannt werden („30 ml Kokoscreme dazugeben“).
- Russisch in kyrillischer Schrift, Filipino darf gängige englische Barbegriffe verwenden.
- Lieber eine Sprache weglassen als unsicher übersetzen.

## 4. Rezeptformat (`recipes.json`)

```json
{ "id": "whiskey_sour",
  "name": { "de": "Whiskey Sour", "ru": "Виски сауэр" },
  "desc": { "de": "Whiskey, Zitrone und Zucker – sauer, rund und mit Eiweiß schön cremig.",
            "en": "Whiskey, lemon and sugar – tart, round and creamy with egg white." },
  "glass": "tumbler",
  "tags": ["klassiker", "stark", "sauer"],
  "pump": [ { "ing": "whiskey", "ml": 50 }, { "ing": "lemon_juice", "ml": 25 }, { "ing": "sugar_syrup", "ml": 15 } ],
  "manual": [
    { "de": "Eiswürfel", "en": "Ice cubes" },
    { "de": "1 Eiweiß", "en": "1 egg white", "expert": true },
    { "de": "Orangenzeste", "en": "Orange zest", "expert": true }
  ],
  "tip":  { "de": "Erst ohne, dann mit Eis shaken – fester Schaum.",
            "en": "Shake without, then with ice – firmer foam." },
  "img": "img/whiskey_sour.jpg",
  "steps": [
    { "t": "glass",  "sym": "shaker" },
    { "t": "pump",   "items": [ { "ing": "whiskey", "ml": 50 }, { "ing": "lemon_juice", "ml": 25 }, { "ing": "sugar_syrup", "ml": 15 } ] },
    { "t": "manual", "sym": "hand",     "text": { "de": "1 Eiweiß dazugeben", "en": "Add 1 egg white" } },
    { "t": "action", "sym": "shake",    "sec": 10, "text": { "de": "Ohne Eis kräftig shaken", "en": "Shake hard without ice" } },
    { "t": "action", "sym": "eis",      "text": { "de": "Eis in den Shaker", "en": "Add ice to the shaker" } },
    { "t": "action", "sym": "shake",    "sec": 15 },
    { "t": "action", "sym": "abseihen", "text": { "de": "In den Tumbler auf frisches Eis abseihen", "en": "Strain into the tumbler over fresh ice" } },
    { "t": "manual", "sym": "garnieren","text": { "de": "Orangenzeste darüber abspritzen", "en": "Express orange zest over the drink" } }
  ] }
```

| Feld | Pflicht | Einfach | Experte | Inhalt |
|---|---|---|---|---|
| `id` | ja | – | – | siehe Regeln |
| `name` | ja (`de`) | ✓ | ✓ | Name des Drinks |
| `desc` | nein | ✓ | ✓ | ein Satz: Geschmack, Herkunft |
| `glass` | ja | ✓ | ✓ | Glasname aus Abschnitt 7 |
| `tags` | ja | ✓ | ✓ | 1–4 aus: `klassiker`, `longdrink`, `fruchtig`, `frisch`, `cremig`, `sauer`, `bitter`, `stark`, `spritz`, `alkoholfrei`, `tropisch`, `winter` |
| `pump` | ja | ✓ | Übersicht | gepumpte Zutaten mit ml |
| `manual` | ja | ohne `expert` | alle | Handzutaten: Katalogbezug `{ "ing", "ml" }` oder freier Text; `"expert": true` = nur Experte |
| `note` | nein | ✓ | ✓ | Hinweis, der in **beiden** Modi wörtlich stimmt („Nicht umrühren“). Gilt er nur für Einfach (z. B. „im Glas umrühren“, obwohl Experte shakt), weglassen |
| `tip` | nein | – | ✓ | Profi-Tipp, nur Experte |
| `img` | ja | ✓ | ✓ | immer `img/<id>.jpg` |
| `steps` | wenn Technik nötig (Abschnitt 1) | – | ✓ | geführter Ablauf |

**`alkoholfrei`** nur, wenn **alle** Zutaten 0 % haben – gepumpte **und** Handzutaten. Alkoholisches von Hand
(„Schuss Rum“, Likör zum Beträufeln) muss deshalb immer mit Katalogbezug `{ "ing": "rum_dark", "ml": 10 }` in `manual`
stehen, nie als freier Text – sonst sieht das Gerät den Alkohol nicht (Gast-Grenze „nur alkoholfrei“, Gramm Alkohol).
Das gilt auch für `"expert": true`: ein alkoholfreier Drink bleibt in beiden Modi alkoholfrei.

### Aufteilung Einfach / Experte – so entscheidest du

- In **`manual` ohne `expert`**: was jeder Gast braucht, damit der Drink stimmt (Eis, eine einfache Garnitur).
- In **`manual` mit `expert`**: was Aufwand, Übung oder besondere Zutaten braucht (Eiweiß, Zeste abspritzen, Zuckerrand, Minze andrücken).
- **`note`**: was in beiden Modi wichtig ist („Nicht umrühren“).
- **`tip`**: Technik für Fortgeschrittene („Über den Barlöffel einlaufen lassen“).
- Braucht ein Drink im Original zwingend Shaken, gilt trotzdem: Einfach = alles direkt ins Glas mit Eis. Das ist in Ordnung.

## 5. Geführter Ablauf (`steps`)

Schrittarten:

| `t` | Bedeutung | Felder |
|---|---|---|
| `glass` | Gefäß unterstellen oder wechseln. Weiter, sobald die Waage ein Glas erkennt. | `sym` (Gefäß), optional `text` |
| `pump` | Zutaten pumpen. Weiter automatisch. | `items`: `[{ "ing", "ml" }]` |
| `manual` | Handzutat oder Hinweis. Weiter per Tippen. | `sym`, `text` |
| `action` | Tätigkeit (shaken, rühren …). Weiter per Tippen oder nach `sec`. | `sym`, optional `sec` (5–60), optional `text` |

Regeln:

- **Erster Schritt ist immer `glass`.**
- **Die Summe aller `pump`-Schritte muss genau `pump` ergeben** (gleiche Zutaten, gleiche ml). Eine Zutat darf auf mehrere Schritte verteilt sein, die Summe zählt.
- Zutaten, die nacheinander ins Glas sollen (Schichten, Grenadine zuletzt), stehen in **getrennten** `pump`-Schritten.
- Kohlensäurehaltiges (Soda, Tonic, Prosecco) kommt **nicht in den Shaker**, sondern nach dem Abseihen in einem eigenen `pump`-Schritt ins Trinkglas.
- Wird nach dem Abseihen **noch einmal gepumpt** (z. B. Soda obendrauf), braucht es davor einen `glass`-Schritt mit dem Trinkglas: das Gerät pumpt immer in das Gefäß, das unter dem Auslauf steht. Endet der Drink mit dem Abseihen, genügt der `action`-Schritt „abseihen“.
- Höchstens 12 Schritte. Ohne `text` zeigt das Gerät einen Standardtext zum Symbol.

**Symbole (`sym`)** – nur diese:

- Gefäße: `shaker`, `ruehrglas`, `longdrink`, `highball`, `tumbler`, `cocktailschale`, `hurricane`, `weinglas`, `kupferbecher`, `shot`
- Aktionen: `shake`, `ruehren`, `abseihen`, `eis`, `garnieren`, `hand` (allgemeine Handzutat)

## 6. Zutatenkatalog (`ingredients.json`)

Nur diese IDs gibt es (Stand 2026-09-21). Alle Namen liegen im Katalog auch in `en`, `ru`, `fil` vor. Kategorien: `spirit`, `liqueur`, `wine`, `juice`, `syrup`, `soft`, `dairy` und `other` (nur für nicht pumpbare Zutaten). Alle Zutaten sind pumpbar, außer der letzten Zeile.

| id | Name | Kategorie | Vol-% |
|---|---|---|---|
| `vodka` | Wodka | spirit | 37,5 |
| `rum_white` | Weißer Rum | spirit | 37,5 |
| `rum_dark` | Brauner Rum | spirit | 40 |
| `gin` | Gin | spirit | 40 |
| `tequila` | Tequila | spirit | 38 |
| `whiskey` | Whiskey | spirit | 40 |
| `cachaca` | Cachaça | spirit | 39 |
| `triple_sec` | Triple Sec | liqueur | 30 |
| `blue_curacao` | Blue Curaçao | liqueur | 21 |
| `peach_schnapps` | Pfirsichlikör | liqueur | 20 |
| `coconut_liqueur` | Kokoslikör | liqueur | 21 |
| `coffee_liqueur` | Kaffeelikör | liqueur | 20 |
| `cream_liqueur` | Sahnelikör | liqueur | 17 |
| `amaretto` | Amaretto | liqueur | 28 |
| `aperol` | Aperol | liqueur | 11 |
| `lime_juice` | Limettensaft | juice | 0 |
| `lemon_juice` | Zitronensaft | juice | 0 |
| `orange_juice` | Orangensaft | juice | 0 |
| `pineapple_juice` | Ananassaft | juice | 0 |
| `cranberry_juice` | Cranberrysaft | juice | 0 |
| `maracuja_juice` | Maracujasaft | juice | 0 |
| `grenadine` | Grenadine | syrup | 0 |
| `sugar_syrup` | Zuckersirup | syrup | 0 |
| `coconut_syrup` | Kokossirup | syrup | 0 |
| `cola` | Cola | soft | 0 |
| `tonic` | Tonic Water | soft | 0 |
| `ginger_beer` | Ginger Beer | soft | 0 |
| `soda` | Sodawasser | soft | 0 |
| `lemon_soda` | Zitronenlimonade | soft | 0 |
| `prosecco` | Prosecco | wine | 11 |
| `cream` | Sahne | dairy | 0 |
| `milk` | Milch | dairy | 0 |
| `coconut_cream` | Kokoscreme (gesüßt) – **nicht pumpbar**, nur in `manual` | other | 0 |

**Neue Zutat nötig?** Dann liefere sie so (und nutze sie erst danach im Rezept):

```json
{ "id": "orgeat", "name": { "de": "Mandelsirup", "en": "Orgeat", "ru": "Миндальный сироп", "fil": "Almond syrup" },
  "cat": "syrup", "abv": 0, "density": 1.32, "aliases": ["Orgeat", "Mandel-Sirup"] }
```

- `abv` in Vol-%, realistischer Handelswert.
- `density` (g/ml) nur, wenn sie deutlich vom Kategoriewert abweicht (spirit 0,95 · liqueur 1,10 · wine 0,99 · juice 1,05 · syrup 1,30 · soft 1,04 · dairy 1,03).
- `carbonated: true` nur bei Kohlensäure außerhalb von `soft` (z. B. Schaumwein).
- `"pumpable": false` und `"cat": "other"` für alles, was in den Barschrank gehört, aber nicht durch einen Schlauch
  geht: dickflüssig (Kokoscreme, Honig pur), fest oder stückig (Fruchtpüree mit Stücken), Eiweiß, verderblich ohne
  Kühlung. Im Zweifel: nicht pumpbar. Beispiel `egg_white` in `ingredients_example.json`.
- `name` in **allen vier Sprachen** (`de`, `en`, `ru`, `fil`) – der Katalog ist vollständig viersprachig.
- `aliases`: andere Schreibweisen und bekannte Marken, damit Importe die Zutat finden.
- Bevorzuge vorhandene Zutaten. Neue nur, wenn der Drink ohne sie nicht mehr er selbst ist – und dann bitte begründen.

## 7. Gläser (`glass`)

`longdrink`, `highball`, `tumbler`, `cocktailschale`, `hurricane`, `weinglas`, `kupferbecher`, `shot`
(`shaker` und `ruehrglas` sind Werkzeuge und nur in `steps` erlaubt.)

## 8. Bilder

Jedes Rezept hat ein Bild `img/<id>.jpg`. Das Gerät zeigt es auf Kacheln im Display und im Web.

**Technisch (Pflicht, sonst lehnt das Prüfskript ab):**

- **200 × 200 px**, quadratisch
- **JPG, baseline** (nicht „progressiv“), sRGB
- **höchstens 64 KB** (Ziel: 8–30 KB, Qualität ~80–85)
- Dateiname exakt `<id>.jpg`, Kleinbuchstaben

**Gestaltung (einheitlicher Look über alle Rezepte):**

- **Dunkler, ruhiger Hintergrund** (fast schwarz bis dunkelgrau, ca. `#1E1E22`), passend zur dunklen Oberfläche
- **Ein Glas, mittig, frontal**, füllt etwa 60–70 % der Höhe, mit dem richtigen Glastyp aus `glass`
- **Die Expertenversion abbilden** (Abschnitt 1a): Schaum, Schichten, Zeste und Garnitur so, wie der Drink nach den `steps` aussieht
- Farbe der Flüssigkeit realistisch, sichtbare Garnitur
- **Flach und klar**: kein Text, keine Logos, keine Marken, keine Menschen, keine Hände, keine Tischdeko
- Kein Weiß-Hintergrund, keine starken Verläufe, kein Unschärfe-Effekt
- Stil wahlweise flache Illustration (wie die Beispielbilder in `img/`: Whiskey Sour, Cuba Libre, Tequila Sunrise, Piña Colada) oder schlichtes Studiofoto – **innerhalb einer Lieferung einheitlich**
- Keine fremden Fotos aus dem Netz (Urheberrecht); nur selbst erzeugte Bilder

**Kannst du keine Bilder erzeugen,** liefere je Rezept eine Bildbeschreibung für einen Bildgenerator, nach diesem Muster:

> Square 1:1 flat vector illustration of a Whiskey Sour in a short tumbler glass, centered, front view, pale amber
> liquid with a thin white foam layer, two ice cubes, orange zest on the rim, dark charcoal background (#1E1E22)
> with a subtle round spotlight, clean simple shapes, no text, no logo, no people.

Danach auf 200 × 200 px verkleinern und als Baseline-JPG speichern.

## 9. Prüfliste (je Rezept mit „ok“ bestätigen)

1. `id` gültig und neu, `img` = `img/<id>.jpg`
2. Alle `ing` stehen im Katalog (oder werden als neue Zutat mitgeliefert)
3. 1–6 gepumpte Zutaten, alle pumpbar, keine doppelt, je 1–300 ml, Summe ≤ 400 ml; keine Zutat zugleich in `pump` und `manual`
3a. Handzutaten aus dem Katalog mit `ing` (nicht als freier Text), nicht pumpbare Zutaten nur dort
4. Modus Einfach ergibt allein einen trinkbaren Drink
5. Aufwendiges steht in `steps`, `tip` oder `manual` mit `expert`
6. `steps`: beginnt mit `glass`, Summe der Pumpschritte = `pump`, nur erlaubte `t` und `sym`, ≤ 12 Schritte
7. Kohlensäure nicht im Shaker
7a. `alkoholfrei` nur, wenn gepumpte und Handzutaten alle 0 % haben; Alkoholisches von Hand nur mit `ing`
7b. `note` stimmt in beiden Modi; `steps` vorhanden, sobald Technik nötig ist
8. Texte innerhalb der Grenzen (30 / 60 / 90 Zeichen), keine Marken außer Aperol & Co.; `de` und `en` vorhanden (außer bei gleichem Namen), `ru`/`fil` nur wie im Auftrag
9. `glass` aus der Liste, Bild zeigt die Expertenversion im richtigen Glastyp
10. JSON gültig (keine Kommentare, keine überzähligen Kommas)

## 10. Ablauf beim Auftraggeber (nicht deine Aufgabe)

Der Auftraggeber fügt deine Objekte in `recipes.json` / `ingredients.json` ein, legt die Bilder nach `img/` und lässt
`tools/publish_recipes.ps1` laufen. Das Skript prüft alles aus Abschnitt 9, was sich maschinell prüfen lässt
(IDs, Zutaten, Pumpbarkeit, höchstens 6 Pumpzutaten, Handzutaten, `steps` samt Summen und Symbolen, Glas, Bilder)
und schreibt `index.json`. Das Gerät holt danach nur geänderte Dateien.

Vollständige Beispiele im selben Ordner: `recipes_example.json` (Cuba Libre, Whiskey Sour, Tequila Sunrise,
Piña Colada mit Kokoscreme von Hand), `ingredients_example.json` (Dichte, Kohlensäure, nicht pumpbar) und `img/`.

---

## Auftrag (vom Auftraggeber jedes Mal auszufüllen)

- **Rezepte:** z. B. „Mojito, Caipirinha, Piña Colada“ oder „10 fruchtige Longdrinks mit dem vorhandenen Katalog“
- **Sprachen für Handzutaten, Hinweise und Schritte:** Standard „de und en“; zusätzlich `ru` / `fil`: ja oder nein
- **Neue Zutaten erlaubt:** ja / nein
- **Bilder:** „erzeugen“ oder „nur Beschreibungen“
- **Bereits vorhandene IDs (nicht erneut anlegen):** `cuba_libre`, `gin_tonic`, `vodka_o`, `screwdriver`, `moscow_mule`, `tequila_sunrise`, `sex_on_the_beach`, `swimming_pool`, `blue_lagoon`, `pina_colada`, `planters_punch`, `mai_tai`, `zombie`, `long_island`, `caipirinha`, `mojito`, `margarita`, `daiquiri`, `cosmopolitan`, `whiskey_sour`, `white_russian`, `aperol_spritz`, `malibu_ananas`, `ipanema`, `virgin_colada`, `florida`, `shirley_temple`, `maracuja_sunrise` (Stand 2026-09-21; bei späteren Aufträgen aus `recipes.json` aktualisieren)
