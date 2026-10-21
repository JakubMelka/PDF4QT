# Nová aplikace – navigace jako web

Zpět na [newapplication.md](newapplication.md).

## 1. Princip

- Každá obrazovka je **stránka s cestou** (`/settings/security`). Odkaz je jen cesta: navigace v postranním panelu, *View all*, drobečková navigace, výsledek hledání, karta toolu, odkaz v dokumentaci i MCP tool `navigate`.
- Jediný vstup je `PDFAppNavigator::navigate(route, mode)`; režim `Push`, `Replace` nebo `NewTab`. Externí adresy (`https://…`) otevře systém, nic jiného nevede mimo navigátor.
- **Neznámá cesta, neznámý parametr nebo neimplementovaná část** (tool bez GUI) ukáže stránku *Page Not Found*: co uživatel hledal (cesta jen jako vedlejší text), *Back* (historie), *Go to Home* a *Search for …*. Nikdy prázdné okno ani pád.
- Dialogy, nabídky a popovery jsou stavy stránky, ne cesty: nejsou v historii a *Back* je nezavírá.
- Stránka se při navigaci vytvoří a při odchodu zničí; data čte z databáze, nic si nedrží ([newapp_storage.md](newapp_storage.md)). Výjimka: karta dokumentu drží dokument a jeho zobrazení po celou dobu života tabu a přepíná jen své stránky (dokument, vlastnosti, tool).

## 2. Třídy (`Pdf4QtLibApp`)

| Třída | Úloha |
| --- | --- |
| `PDFAppRoute` | rozebraná cesta: segmenty, parametry vzoru, query, fragment (kotva řádku nastavení); `toString` a `parse`, odmítá `..`, prázdné segmenty a neplatné kódování |
| `PDFAppRouteTable` | tabulka vzorů `/settings/{section}` → továrna stránky, druh tabu (Home, *New tab*, dokument, tool) a id položky navigace, kterou zvýrazní (sloupec *Navigace* v kapitole 3) |
| `PDFAppPage : QWidget` | základ stránky: `bool setRoute(const PDFAppRoute&)` (false = *Page Not Found*), `getTitle`, `saveViewState` / `restoreViewState` (posun, rozbalené sekce) |
| `PDFAppPageFactory` | virtuální `createPage`, jedna třída na stránku (bez lambd) |
| `PDFAppHistory` | historie tabu: položky {cesta, stav pohledu}, nejvýš 50; Alt+←/→, tlačítka myši Zpět / Vpřed |
| `PDFAppNavigator` | rozhodne tab, vytvoří stránku, vyřídí *Not Found*, zvýrazní navigaci, nastaví titulek tabu, oznámí čtečce obrazovky změnu stránky |
| `PDFPageNotFoundPage` | stránka *Page Not Found* (`pdf4qt-not-found` 940:134845) |

Pravidlo: stránka nikdy nevolá jinou stránku přímo, jen `navigate`. Stránka smazaná během navigace (signál, dialog) se hlídá `QPointer` podle pravidel životnosti.

## 3. Cesty

### Karta Home (postranní navigace)

| Cesta | Stránka | Navigace | Figma |
| --- | --- | --- | --- |
| `/home` | Home | Home | 28:4 |
| `/open` | Open | Open | 61:1201 |
| `/recent` | Recent (`?filter=files\|folders\|workspaces\|pinned`, `?sort=last-opened\|name\|folder`, `?q=`) | Recent | 80:910, menu řazení `pdf4qt-recent-sort` 976:12914, prázdný stav `pdf4qt-recent-empty` 955:142865 |
| `/tools` | katalog (`?category=pages\|convert\|edit\|protect\|optimize\|review\|pinned`, `?q=`) | Tools | závazný 272:12821 (*Tools Overview*), 958:151754 je jeho kopie pro prototyp *Main page*; 530:2957 |
| `/tools/collections`, `/tools/collections/{collectionId}`, `/tools/collections/new` | správa kolekcí; návrhy toolů = stejná kategorie a počitadla dvojic | Tools | 283:1888 |
| `/tools/{toolId}` | úvodní stránka toolu – **jen u implementovaného toolu**, jinak *Page Not Found* (fáze 2) | Tools | 276:1737, 531:3208 |
| `/ai` | AI (běží / zastaveno / vzdálený přístup / jiné okno / obsazený port / nastavení jiného klienta jsou stavy stránky) | AI | 653:5231, 653:5814, 653:6384, 940:135321, 955:148587, 955:149138 |
| `/whats-new`, `/whats-new/{verze}`, `/whats-new/{verze}/notes` | běžící nebo zvolená verze a plné poznámky k vydání ([newapp_whatsnew.md](newapp_whatsnew.md) 4) | What's New | 103:1951, 988:13320, 988:13824, 988:152102, 989:152125 |
| `/about`, `/support`, `/sponsor` | | stejnojmenná | 110:2163, 116:2419, 122:2652 |
| `/help/{topic}` | uživatelská příručka z Markdownu (stejné soubory jako `pdf4qt://docs`); otevře se v **tabu *Help*** (jeden v okně, znovu použitý, zavíratelný – [newapplication.md](newapplication.md) O13) | žádná | `pdf4qt-help` 955:152873 |
| `/settings` → `/settings/general` | | Settings | |
| `/settings/{section}` | `general`, `viewing`, `color-modes`, `editing`, `security`, `speech`, `shortcuts`, `color-management`, `advanced`; `#<id nastavení>` posune na řádek a zvýrazní ho; `/settings?q=` filtruje sekce a řádky podle `PDFSettingDefinition` | Settings | 146:2843 … 147:4565 |
| `/search?q=` | *Show all results* globálního hledání (`?group=tools\|commands\|settings\|documents\|help`) | žádná | 941:7081 |
| (neznámá) | *Page Not Found* | žádná | 940:134845 |

**Tab *New tab*** (tlačítko „+“ a Ctrl+T, rozhodnutí vlastníka): nový tab se stránkou `/open` bez postranního panelu Home (`pdf4qt-new-tab` 955:152339). Otevřením souboru se z něj stane tab dokumentu; jde zavřít (×, Ctrl+W); může jich být víc. Má jen cestu `/open`: každý jiný odkaz (*View all*, *Manage*, globální hledání) přepne na tab Home a naviguje tam. Pracovní prostor nebo *Restore last session* otevře dokumenty ve vlastních tabech a *New tab* zavře. *Open* u dokumentu, který už je otevřený, přepne na jeho tab (podle kanonické cesty; stejně soubor z druhého spuštění aplikace, [newapplication.md](newapplication.md) 3).

Ostatní odkazy: *Manage* u Workspaces = `/recent?filter=workspaces`; *Add…* u Pinned Locations = systémový výběr složky; *Video Tutorials* = `/support`; *Third-party notices* = `/about#third-party`; release notes, GitHub, YouTube, licence a web = externí adresy.

### Karta dokumentu

| Cesta | Stránka | Figma |
| --- | --- | --- |
| `/document/{documentId}` | dokument (`?page=` číslo fyzické stránky od 1, `?panel=thumbnails\|outline\|…`, `?q=` hledání v dokumentu) | 193:756 |
| `/document/{documentId}/properties/{section}` | vlastnosti: `general` (ve Figmě *Properties*), `security`, `fonts`, `display`, `xmp`; *Back to document* = zpět v historii | 236:8882 … 236:9574 |
| `/document/{documentId}/tool/{toolId}` | tool v kartě dokumentu (fáze 2) | 291:994 |
| `/tool/{toolId}/{instanceId}` | tool s vlastní kartou: Merge, Interleave Pages, Manage Pages, Compare, Manage Certificates, Images to PDF (tab *New document*), dávkové OCR (tab *Batch OCR*) (fáze 2) | 378:1049 |

`documentId` je neprůhledné id tabu (128 bitů náhody, base32), platí po dobu života tabu; stejné id používá MCP. Heslo dokumentu (228:8709) je stav stránky dokumentu, ne cesta.

## 4. Zdroje odkazů

- Položky navigace, `PDFTextLink`, `PDFLinkRow`, karty a řádky komponent dostanou cestu jako data (`setRoute`) a při aktivaci volají navigátor. Řádek nastavení v globálním hledání vede na `/settings/security#signature-verification`.
- *Settings…* v panelu File a *Rendering settings* v panelu View přepnou na tab Home a cestu nastavení.
- Cesty se nikde neskládají ručně z řetězců: `PDFAppRoutes` má statické funkce (`settings(section, anchor)`, `document(id, page)`, `tool(toolId)`).

## 5. Testy (`UnitTestsAppNavigation`)

Tabulka všech cest: každá se otevře, je na ní správná stránka a zvýrazněná navigace; neplatné cesty a parametry (cizí sekce, neexistující dokument, `toolId` bez GUI, `%00`, `..`, 10 kB cesta) skončí na *Page Not Found*; historie a obnovení stavu pohledu; navigace z vlastního signálu stránky a smazání tabu během navigace (valgrind).
