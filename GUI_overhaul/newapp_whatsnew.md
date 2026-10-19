# Nová aplikace – stránka What's New

Patří k [newapplication.md](newapplication.md) (fáze 1, krok 2). Návrh ve Figmě: `pdf4qt-whatsnew` 103:1951 na *Main page* a stavy v kapitole 7.

## 1. Co je ve Figmě

| Část | Komponenty | Data |
| --- | --- | --- |
| Karta vydání | logo, *Tag* „Up to date“, *Button* Primary *Read full release notes*, Secondary *Explore new tools* | verze, datum, souhrn (odstavec) |
| Highlights | *Highlight card* 103:1894 (`Title`, `Description`, `Icon`, `Hue`), mřížka 3 sloupce | 0 až n karet |
| New tools in this release | *Tool row* 89:1330 s *Tool icon* a *Star button* (připnutí) | tooly z katalogu |
| Fixed issues | *Issue row* 103:1931 (`Number`, `Title`, `Description`, `Date`, ikona stavu), posuvník uvnitř karty, odkaz *View all on GitHub* | opravené issues |
| Update status | karta stavu, *Last checked …*, Secondary *Check for updates* | kontrola aktualizací |
| Links | 5 × *Link row* (Full changelog, GitHub repository, Discussions, Report an issue, Request a feature) | statické odkazy |
| Version history | *Version row* 103:1950 (`Version`, `Date`, `Summary`, `Current`, `Show line`), odkaz *View all* | seznam verzí |
| Thank you! | statická karta | – |

## 2. Zdroj dat: jeden Markdown soubor na verzi

- Soubory jsou v resources aplikace: `Pdf4QtLibApp/resources/whatsnew/<verze>.md`, název podle čísla verze z [RELEASES.txt](../RELEASES.txt) a `PDF4QT_VERSION` (`1.6.0.0.md`). Zobrazuje se verze o třech číslech (`1.6.0`).
- Seznam verzí = soubory `<verze>.md` ve složce `:/whatsnew/` (přeložené `<verze>.<jazyk>.md` se nepočítají – `QVersionNumber::fromString` by z `1.6.0.0.cs` udělal tutéž verzi), řazený podle `QVersionNumber` sestupně. Žádný index, novou verzi přidá nový soubor.
- Texty jsou anglicky (americká angličtina). Překlad je volitelný soubor `<verze>.<jazyk>.md` se stejnou strukturou; když chybí, použije se anglický (pořadí hledání jazyka v [newapplication.md](newapplication.md) 3, *Překlady*).
- Zdrojem pravdy o opravených issues zůstává [RELEASES.txt](../RELEASES.txt). Soubor verze z něj přebírá čísla a názvy a přidává jen popis a datum (kostru vygeneruje skript při vydání, test kontroluje shodu).

### Pevná forma souboru

```markdown
---
version: 1.6.0.0
date: 2026-11-20
summary: One unified app, faster loading, timestamped signatures.
---

## Overview

This release merges Editor, Viewer, PageMaster and Diff into one application, …

## Highlights

### One app for everything
icon: layout · hue: blue

Editor, Viewer, PageMaster and Diff are now a single application with tabs.

## New tools

- review.measure
- optimize.bitonal

## Fixed issues

- #430 · 2026-10-12 · Text layout optimization
  Large documents lay out text faster and use less memory.

## Release notes

Free Markdown: everything else, shown by *Read full release notes*.
```

| Část | Pravidla | Kam jde |
| --- | --- | --- |
| hlavička `---` | klíče `version` (shodná s názvem souboru), `date` (ISO), `summary` (jedna věta); jiný klíč je chyba | karta vydání, *Version row* (`Summary`) |
| `## Overview` | jeden odstavec prostého textu | text karty vydání |
| `## Highlights` | každá karta = `### Title`, řádek `icon: <název ikony> · hue: <odstín>`, odstavec popisu | *Highlight card* |
| `## New tools` | jen `id` toolů z `PDFDocumentToolCatalog`; název, popis, ikonu a odstín bere stránka z katalogu, takže se nerozejdou | *Tool row*, hvězdička = `tool_pin` |
| `## Fixed issues` | `- #číslo · datum · název`, odsazený řádek popisu | *Issue row*, číslo je odkaz na issue na GitHubu |
| `## Release notes` | volný Markdown | stránka plných poznámek |

- Pořadí sekcí je pevné, každá sekce kromě `## Overview` je volitelná. Prázdnou sekci stránka skryje.
- Texty strukturovaných sekcí jsou prostý text (data komponent, bez inline Markdownu). Volný Markdown je jen v `## Release notes`.
- Neznámá sekce, klíč, ikona, odstín nebo `id` toolu je chyba s číslem řádku. Test to hlídá u všech přibalených souborů. Za běhu se poškozený soubor nezobrazí (zápis do logu), aplikace nespadne.

## 3. Kód (`Pdf4QtLibApp`)

- `PDFWhatsNewRelease` – data jedné verze (struktura podle tabulky výše).
- `PDFWhatsNewParser` – čistá funkce text → `PDFWhatsNewRelease` + seznam chyb s řádky. Bez GUI, bez sítě. Volný Markdown se jen uloží.
- `PDFWhatsNewCatalog` – načte a seřadí soubory z resources, vrátí seznam verzí a verzi podle čísla.
- `PDFWhatsNewPage : PDFAppPage` – sestaví stránku z komponent Fluent podle Figmy.
- `PDFUpdateChecker` – kontrola aktualizací (kapitola 5).

## 4. Přepínání verzí

| Cesta | Obsah |
| --- | --- |
| `/whats-new` | verze, která běží |
| `/whats-new/{verze}` | zvolená verze (`/whats-new/1.5.2`, 988:13320); neznámá verze = *Page Not Found* |
| `/whats-new/{verze}/notes` | plné poznámky k vydání (`## Release notes`) stejným vykreslením Markdownu jako příručka `/help` (989:152125) |

- Řádky *Version history* jsou odkazy na `/whats-new/{verze}`. Zobrazená verze je vybraná, běžící verze má `Current` (tečka). *View all* rozbalí seznam všech verzí na místě (stav pohledu stránky).
- Karta vydání starší verze: místo „Up to date“ štítek *Previous release*, věta „Released Aug 12, 2026 · You are running 1.5.3“. *Explore new tools* jen když verze má nové tooly.
- *View all on GitHub* vede na release zobrazené verze na GitHubu (`https://github.com/JakubMelka/PDF4QT/releases/tag/v<verze>`).

## 5. Kontrola aktualizací

- `PDFUpdateChecker` se ptá GitHubu na poslední vydání (`QNetworkAccessManager`). Bez cookies a bez identifikace kromě User-Agent `PDF4QT/<verze>`.
- **Automaticky nejvýš jednou za den** (rozhodnutí vlastníka), ne při každém spuštění: po startu a pak každou hodinu se zkontroluje `update.lastCheck` a když je starší než 24 hodin, proběhne kontrola. Při více instancích ji provede jen jedna: zápis `update.lastCheck` v transakci `BEGIN IMMEDIATE` před dotazem rezervuje kontrolu, ostatní instance výsledek přečtou z databáze.
- Automatická kontrola **jen ukazuje**, zda je verze aktuální (karta *Update status*, štítek karty vydání). Nic nestahuje, neinstaluje a neotevírá okna ani oznámení.
- Vypnutí: Settings › General, přepínač *Check for updates automatically* (`update.autoCheck`, výchozí zapnuto). Vypnutá = karta ukazuje *Automatic check is off* a kontroluje se jen tlačítkem *Check for updates*.
- Výsledek v databázi: `update.lastCheck` (už v [newapp_storage.md](newapp_storage.md)), `update.latestVersion`, `update.lastError`. Ostatní instance ho vidí hned.
- Stavy karty: *Up to date*, *Checking…*, *Update available* (verze a odkaz na stažení z GitHubu), *Could not check* (důvod, *Try Again*), *Automatic check is off*.
- Novější verze, než je nainstalovaná, nemá soubor v resources. Aplikace poznámky nestahuje, jen nabídne odkaz na release na GitHubu.

## 6. Testy

- Parser: platný soubor, každá chyba s číslem řádku (neznámá sekce, klíč, ikona, odstín, `id` toolu, chybějící klíč, špatné datum nebo verze, verze jiná než název souboru, duplicitní issue, špatné pořadí sekcí), volitelné sekce, prázdný soubor, náhodný text bez pádu.
- Všechny přibalené soubory jsou platné, jejich issues odpovídají sekci verze v [RELEASES.txt](../RELEASES.txt), přeložený soubor má stejnou strukturu jako anglický.
- Stránka: běžící a starší verze, přepnutí z *Version history*, *Page Not Found* pro neznámou verzi, skryté prázdné sekce, připnutí toolu hvězdičkou, stavy kontroly aktualizací proti serveru testu na loopbacku (jako `UnitTestsTimestamp`).

## 7. Doplněno ve Figmě (19. 10. 2026)

| Co | Kde |
| --- | --- |
| *Version row* 103:1950: vlastnosti `Selected` (zobrazená verze: `bg/accent-subtle`, verze `text/accent`) a `State` (Default / Hover / Pressed / Focus), `Current` je jen tečka běžící verze; 16 variant, sada přesunutá na *Components* na x 2160, y 6700 | *Components* |
| Základní stránka: v1.5.3 je `Selected`, v *New tools* je místo „Read Aloud“ tool katalogu *Add Timestamp* | 103:1951 |
| Starší verze 1.5.2 (*Previous release*, bez *Explore new tools* a bez sekce *New tools*) | `pdf4qt-whatsnew-previous` 988:13320 |
| Rozbalená *Version history* (*View all* → *Show less*) | `pdf4qt-whatsnew-all-versions` 988:13824 |
| Dostupná aktualizace (štítek *Update available*, karta s Primary *Download from GitHub* pod *Check for updates*) | `pdf4qt-whatsnew-update-available` 988:152102 |
| Všech pět stavů karty *Update status* (*Up to date*, *Checking…*, *Update available*, *Could not check*, *Automatic check is off*) | `pdf4qt-whatsnew-update-states` 989:14257 |
| Plné poznámky k vydání (`/whats-new/{verze}/notes`, rozvržení příručky, *Version history* a odkazy vpravo) | `pdf4qt-whatsnew-notes` 989:152125 |
| Settings › General › *Updates*: *Check for updates automatically* | 146:2843 (a varianty 941:136741, 941:138200, 955:143792) |

Prototyp: řádky verzí, *View all* / *Show less*, *Read full release notes*, drobečková navigace poznámek, *Check for updates* (→ dostupná aktualizace) a *Turn on in Settings*. Čísla issues v ukázkách jsou smyšlená, starší verze v rozbaleném seznamu (1.4.0 a starší) odpovídají [RELEASES.txt](../RELEASES.txt).
