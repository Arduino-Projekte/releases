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

Ablauf auf dem Gerät: `index.json` holen → hat sich ein SHA-256 geändert, die jeweilige Datei laden und prüfen → dann übernehmen.

## Zutat (`ingredients.json`)

```json
{ "id": "rum_white", "name": { "de": "Weißer Rum" }, "cat": "spirit", "abv": 37.5, "aliases": ["Bacardi"] }
```

- `id`: `a-z`, `0-9`, `_`, 2–32 Zeichen, dauerhaft stabil. Barschrank und Statistik verweisen darauf.
- `cat`: `spirit`, `liqueur`, `wine`, `juice`, `syrup`, `soft`, `dairy`
- `abv`: Vol-%. Daraus berechnet das Gerät „alkoholfrei“ und die Gramm Alkohol.
- `aliases`: Namen, unter denen die Zutat in Fremdimporten (CSV, TheCocktailDB) oder im Barschrank auftauchen kann (R7).

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

- `pump`: 1–8 gepumpte Zutaten, Mengen in **ml** bei Größe 1,0, Gesamtmenge ≤ 400 ml
- `manual`: Zutaten und Hinweise für die Hand (werden nicht gepumpt)
- `note`, `img`, `tags`: optional
- Texte sind Objekte je Sprache (`de`, später `en`, `ru`, `fil`). Fehlt eine Sprache, zeigt das Gerät `de`.

## Veröffentlichen

```
powershell -NoProfile -ExecutionPolicy Bypass -File C:\GitHub\Cocktailmixer\tools\publish_recipes.ps1
```

Das Script prüft beide Dateien (IDs, unbekannte Zutaten, Mengen, Bilder) und schreibt `index.json` nur, wenn sich etwas geändert hat. Danach committen und pushen.
