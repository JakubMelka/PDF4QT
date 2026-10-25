# Nová aplikace – tooly dokumentu (příprava na fázi 2)

Zpět na [newapplication.md](newapplication.md). Katalog, pravidla a chování toolů jsou v [tools.md](tools.md) (kapitoly 1, 4, 9).

## 1. Tři vrstvy

| Vrstva | Kde | Co |
| --- | --- | --- |
| `PDFDocumentTool` | `Pdf4QtLibCore` | operace nad dokumentem bez GUI: parametry, validace, náhled, běh, výsledek. Volá ji GUI, MCP i (později) PdfTool. |
| `PDFDocumentToolGUI` | `Pdf4QtLibApp` | GUI jednoho toolu uvnitř společného shellu (291:994): akce v liště, panely, plátno, vazba widgetů na parametry |
| `PDFWidgetTool` | `Pdf4QtLibWidgets` | režim myši nad stránkou (anotace, lupa, výběr). Tool fáze 2 ho smí použít pro interakci na plátně (Measure, Redact Content, Sign by Hand). |

## 2. Jádro

### Katalog a registr

- `PDFDocumentToolInfo` – data toolu podle [tools.md](tools.md) 9.5: `id` (`pages.merge`), kategorie, podskupina, název, popis, klíčová slova (`tr()`), ikona, štítky (ve Figmě je jen *Advanced*; *New* a *Beta* jsou data pro později), druh vstupu (žádný, jeden dokument, více dokumentů), druh výsledku (změna dokumentu, nový PDF, více souborů, analýza), druh karty (karta dokumentu, vlastní karta), chování u zamčeného dokumentu (dostupný, nedostupný, s omezením), dopad na podpisy, externí závislost (Tesseract, skener), dávkové spuštění, příkaz PdfTool, předvybraná volba dialogu výsledku, nejvýš tři výchozí následníci, dokumentace (Markdown v resources) a tutoriál (odkaz, název, délka, popis, náhled) – vše pro *At a glance* úvodní stránky (276:1737).
- `PDFDocumentToolCatalog` – statická tabulka všech 46 toolů s názvy, ikonami a odstíny podle Figmy (ikony řádků katalogu 272:12821 – závazný zdroj; 958:151754 na *Main page* je jeho kopie pro prototyp a musí se s ním shodovat). Existuje už ve fázi 1 a slouží katalogu, hledání a dokumentaci MCP.
- `PDFDocumentToolRegistry` – implementace podle `id`. Tool z katalogu bez implementace je v katalogu vidět, ale jeho stránka je *Page Not Found* a v MCP chybí.

### Klíčová slova a hledání

- Každý tool má v katalogu **klíčová slova** podle [tools.md](tools.md) (seznam u každého toolu v kapitole 5, pravidla v kapitole 2): anglicky, malými písmeny, synonyma, názvy z konkurence, formáty a názvy akcí starých aplikací (*United Document*, *Page Geometry* …, [tools.md](tools.md) 9.1 bod 6).
- Anglická slova se hledají vždy; překlad (`tr()`) přidává slova v jazyce rozhraní. V UI se nezobrazují, jen ve výsledku hledání jako důvod shody („Merge · matches *combine*“, Figma 285:3026, 941:7081).
- Jedno hledání toolů pro všechna místa: `PDFDocumentToolSearch` v jádře – katalog Tools, skupina Tools globálního hledání (Ctrl+K), *Another tool…* v *Then continue with* a přidání toolu do kolekce. Panel Tools dokumentu (*Search tools and actions…*) používá globální hledání se skupinami Tools a Commands. Bez ohledu na velikost písmen a diakritiku, podle začátku slova, s tolerancí jednoho překlepu, pořadí shody název → klíčová slova → popis.
- Test: každý tool má klíčová slova; ukázkové dotazy z [tools.md](tools.md) najdou svůj tool (*join*, *combine*, *bind* → Merge), překlep, diakritika, pořadí výsledků a důvod shody.

### Parametry

`PDFDocumentToolParameterDefinition`: jméno, typ (`Bool`, `Integer`, `Real`, `String`, `Enum`, `PageSelection`, `Color`, `FilePath`, `FolderPath`, `FileNameTemplate`, `Secret`), výchozí hodnota, rozsah, volby výčtu (id, název, popis, ikona), povinnost, popis. Jedna definice dává:

- JSON Schema pro `inputSchema` v MCP,
- validaci s přeloženými zprávami (stejnou pro GUI i MCP),
- uložení posledních hodnot do databáze (kromě `Secret`),
- později volby PdfTool.

`PageSelection` má syntaxi `PDFPageRange` / `PDFClosedIntervalSet::parse` (stránky od 1) a filtry komponenty *Page selection*.

### Běh

```text
class PDFDocumentTool
    getInfo, getParameters
    validate(context, parameters)                    -> chyby
    computePreview(context, parameters, control)     -> fakta (volitelné; "24.8 MB → 6.1 MB")
    execute(context, parameters, control, progress)  -> PDFDocumentToolResult
```

- `PDFDocumentToolContext`: vstupy (separátní kopie původního dokumentu, kterou tool smí upravovat – dokument v tabu se do přijetí výsledku nemění; původní data souboru pro inkrementální uložení a podpisy, název souboru, výběr stránek), prostředí (nastavení CMS a vykreslování, jméno autora, úložiště certifikátů, dočasná složka) a `PDFDocumentToolFileSink` pro tooly, které zapisují více souborů (politika existujících souborů, kontrola oprávnění).
- `PDFDocumentToolResult`: stav (`Success`, `Stopped`, `Failed`, `NothingToDo` – „File cannot be reduced“, 295:2182), změněné a nové dokumenty s příznaky `PDFModifiedDocument` a navrženým názvem, zapsané soubory, fakta pro dialog výsledku, řádky kroků (ikona, text, stav – žádné textové logy), strukturovaná data pro MCP.
- `execute` je reentrantní: nesahá na GUI ani na globální měnitelný stav, pracuje jen s kontextem, kontroluje zrušení aspoň po stránkách a smí použít `PDFExecutionPolicy`.
- Tool sám nezapisuje výsledný dokument. Uložení řeší volba dialogu výsledku (GUI), parametr `result` (MCP) nebo PdfTool. Výjimka: exporty do více souborů zapisují přes `PDFDocumentToolFileSink`.
- Logika, která je dnes jen v aplikacích, se při implementaci toolu přesune do jádra a napíše znovu (`unite`, `separate`, `remove-external-links`, stránkové operace PageMasteru, `PDFDiff` zůstává).

## 3. GUI (`Pdf4QtLibApp`)

- `PDFDocumentToolShell` (jeden pro všechny tooly, podle 291:994 a 378:1049):
  - lišta: postranní panel, odznak a název, Undo / Redo toolu, akce toolu, *Tutorial*, *Cancel*, potvrzovací tlačítko pojmenované podle výsledku; během běhu jediné tlačítko Secondary *Stop* (`icon/stop`) na místě potvrzovacího, *Cancel* je skryté;
  - levý panel dokumentu nebo vlastní panel toolu, plátno, pravý panel 320 px;
  - stavový řádek se stránkou a zoomem;
  - karta průběhu nahoře v pravém panelu, upozornění *The document is busy*, průběh na tabu, zastavení tlačítkem *Stop* nebo Esc;
  - dialog výsledku podle druhu výsledku: změna dokumentu (296:2761: *Keep in this tab* / *Save as a copy* / *Overwrite the original file*), nový dokument (378:3674: *Open in a new tab* / *Save as a file*), jen nový soubor (název, složka, otevřít kopii; existující soubor = stav *Replace File*); vždy *Then continue with* a *Discard result*, chyba zápisu jako stav dialogu (316:7293);
  - návrat stránky, zoomu a výběru po zavření toolu;
  - dokument je během běhu zamčený pro úpravy i další tooly.
- `PDFDocumentToolGUI` – jen to, čím se tool liší: `createToolActions`, `createRightPanel`, volitelně `createLeftPanel` a překryv plátna, vazba widgetů na parametry, text potvrzení, vlastní Undo / Redo kroků toolu.
- `PDFDocumentToolGUIRegistry` – `id` → továrna (jedna třída na tool, bez lambd).
- `PDFDocumentToolRunner` – běh `execute` ve vlákně pool, `PDFOperationControl` a `PDFProgress`, výsledek zpět do hlavního vlákna přes `QPointer`. Na jednom dokumentu nejvýš jeden běh.
- `PDFToolStartPage` – úvodní stránka toolu (`/tools/{toolId}`, 276:1737, 531:3208) generovaná z `PDFDocumentToolInfo` a dokumentace.

### Použití a řetězení toolů

Potvrzeno vlastníkem 25. 10. 2026 (O21).

Pravidla jsou v [tools.md](tools.md) 9.3 bod 12, tabulky v [newapp_storage.md](newapp_storage.md) 4. Vše se čte z databáze online (žádná cache), takže to vidí i ostatní instance.

- **Použití** (`tool_usage`: `last_used`, `count`) se zapíše, když uživatel v GUI přijme výsledek (potvrdí volbu dialogu výsledku, ne *Discard result*) nebo když běh skončí stavem `NothingToDo`. Zastavený, chybný a zahozený běh se nezapisuje. Podle `last_used` se řadí *Recent tools* (Tools 272:12821, Home), `count` je pro budoucí sekci *Frequently used* ([tools.md](tools.md) 8.3).
- **Řetězení** (`tool_pair`: `from_tool`, `to_tool`, `count`, `last_used`): dvojice A → B se zvýší, když B běží volbou *Then continue with* po úspěšném A, nebo když je B ručně spuštěný jako první další tool nad výsledkem A ve stejném tabu a relaci. Tab si drží `lastFinishedToolId` jen v paměti.
- **Návrhy pro *Then continue with*** (a text *Often used next* na úvodní stránce): nejvýš tři tooly s nejvyšším počitadlem. Při shodě rozhoduje novější `last_used`, pak pořadí v katalogu. Uživatelská dvojice se navrhuje od počitadla 2 (jednorázový pokus ne). Výchozí následníci z `PDFDocumentToolInfo` se při prvním zápisu dvojice toolu A vloží s hodnotou 2, do té doby se navrhují jen oni. Tool, který nad výsledkem nejde spustit (zamčený dokument, chybějící Tesseract nebo skener, nevhodný druh výsledku), se přeskočí. Nic se nepředvybírá.
- **Stárnutí:** když součet počitadel toolu A překročí 50, všechna jeho počitadla se vydělí dvěma (zaokrouhlení dolů) a nuly se smažou. Zvýšení i stárnutí běží v jedné transakci `BEGIN IMMEDIATE`, takže dvě instance nic neztratí.
- **Nastavení:** *Suggest tools I often use next* (`tools.suggestNext`). Vypnuté = jen výchozí návrhy a nic se nepočítá. *Clear history* (941:136741) smaže `tool_pair` a `tools.historyRuns`, připnuté tooly, kolekce a *Recent tools* zůstávají. Ukládají se jen `id` toolů a čísla, žádné názvy souborů.
- **MCP:** běhy z MCP se nepočítají do použití ani do dvojic, aby asistent neměnil návrhy uživatele.
- **Kód:** výpočet návrhů a stárnutí je čistá funkce v jádře (`PDFDocumentToolSuggestions`: počitadla + výchozí následníci + dostupnost → seznam), zápis a čtení je `PDFDocumentToolUsage` v `Pdf4QtLibApp`. Testy: výchozí návrhy, vytlačení výchozího po třetím použití uživatelské dvojice, jednorázový pokus, shoda, stárnutí, vypnuté nastavení, nedostupný tool a souběžný zápis dvou připojení.

## 4. MCP

Každý implementovaný tool je automaticky akcí hierarchického MCP toolu své kategorie (`optimize.compress` → tool `optimize`, `action: "compress"`). Objekt parametrů akce vzniká z definic parametrů, k tomu `documentId` (nebo `documentIds`) a `result` podle druhu výsledku: `keep`, `save_copy` + `path`, `overwrite` (změna dokumentu), `open_new_tab`, `save_file` + `path` (nový dokument). Výchozí hodnota `result` je předvybraná volba dialogu výsledku. Dlouhý běh posílá průběh a jde zrušit. Podrobnosti v [newapp_mcp.md](newapp_mcp.md) 6.

## 5. Postup fáze 2

1. Shell, dialog výsledku a runner s pilotním toolem **Compress** (Figma nejúplnější: náhled, běh, *not reduced*, výsledek, chyba zápisu).
2. **Merge** – vlastní karta, více vstupů, *Open in a new tab* / *Save as a file*, úvodní stránka s více dokumenty.
3. Ostatní tooly po kategoriích. Každý tool = jádro + GUI + dokumentace (Markdown) + test jádra bez GUI + test GUI + MCP; kontroly Figma a C++ jako ve fázi 1.
4. Kontrola pokrytí podle [tools.md](tools.md) kapitola 6 – každá funkce starých aplikací má místo. Pak smazání starých aplikací, `Pdf4QtLibGui`, pluginů a `PDFPlugin`, úprava instalátoru.
