# Beispiele für Rezepte

Dieser Ordner wird **nicht veröffentlicht** und vom Gerät nicht geladen – `publish_recipes.ps1` liest nur
`recipes.json`, `ingredients.json` und `img/` eine Ebene höher.

| Datei | Inhalt |
|---|---|
| `recipes_example.json` | vier vollständige Rezepte: **Cuba Libre** (nur Einfach, ohne `steps`), **Whiskey Sour** (Shaker, Eiweiß, Dry Shake, Abseihen), **Tequila Sunrise** (Schichten über zwei Pumpschritte), **Piña Colada** (Kokossirup gepumpt, Kokoscreme von Hand mit Katalogbezug) |
| `ingredients_example.json` | neue Zutaten mit `density` (Mandelsirup), `carbonated` (Champagner) und `pumpable: false` (Eiweiß) |
| `img/*.jpg` | Beispielbilder aller vier Rezepte in der Expertenversion: 200 × 200, Baseline-JPG, je ca. 4 KB, dunkler Hintergrund, Glas mittig |
| `prompt_rezepte.md` | **Arbeitsauftrag** für andere Chats oder Agenten, die Rezepte schreiben – vollständig, mit Katalog, Regeln, Bildvorgaben und Prüfliste |
| `auftrag_2026-09-24_zusatz24.md` | erledigter Einzelauftrag (24 Zusatzrezepte, 24.09.2026) – nur als Vorlage für den Auftragsteil künftiger Ergänzungen |

## Einfach und Experte am Beispiel Whiskey Sour

| | Einfach | Experte |
|---|---|---|
| Ablauf | Whiskey, Zitrone, Zuckersirup direkt in den Tumbler | Shaker → Eiweiß → ohne Eis shaken → Eis → shaken → abseihen → Zeste |
| Von Hand | Eiswürfel | Eiswürfel, 1 Eiweiß, Orangenzeste |
| Hinweis | – | Tipp zum Dry Shake |

## So nutzt du den Prompt

1. `prompt_rezepte.md` komplett in einen neuen Chat kopieren.
2. Unten den Abschnitt **„Auftrag“** ausfüllen (welche Rezepte, Sprachen, neue Zutaten ja/nein, Bilder).
3. Die gelieferten Objekte in `../recipes.json` bzw. `../ingredients.json` einfügen, Bilder nach `../img/`.
4. Erst nur prüfen: `powershell -NoProfile -ExecutionPolicy Bypass -File C:\GitHub\Cocktailmixer\tools\publish_recipes.ps1 -CheckOnly`
5. Ist alles grün, ohne `-CheckOnly` laufen lassen (schreibt `index.json`), dann committen und pushen.

Das Skript prüft alles, was maschinell geht: höchstens 6 Pumpzutaten, Pumpbarkeit, Handzutaten mit Katalogbezug,
`steps` (erster Schritt Glas, Arten, Symbole, `sec`, Summe der Pumpschritte = `pump`), Glas, Texte mit `de`,
viersprachiger Katalog, Bilder.

**Stand der Firmware (V2026.9.21.3):** Rezeptmodus Einfach/Experte, `tip`, `desc`, `expert`, Handzutaten mit
Katalogbezug (`ing`, Menge, Alkohol), `pumpable` und im Modus Experte der geführte Ablauf nach `steps`
werden ausgewertet. Rezepte mit `{ "ing", "ml" }`
in `manual` erst veröffentlichen, wenn die Geräte mindestens V2026.9.21.3 haben.
