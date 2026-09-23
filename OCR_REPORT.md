# OCR v PDF4QT Editoru: zpráva o implementaci

Tato zpráva doplňuje zadání [OCR.md](OCR.md). Popisuje, co bylo implementováno, jaká technická rozhodnutí padla, jak se funkce ovládá, co je ověřeno testy, jaké jsou naměřené hodnoty kvality a výkonu a co zůstává otevřené. Identifikátory v závorkách odkazují na požadavky zadání.

Stav k 21. 9. 2026: implementace P0 je hotová a automaticky otestovaná na Windows. **Ruční a meziplatformní přejímka podle kapitoly 16.4 zadání neproběhla**, takže definice hotového P0 z kapitoly 16.5 zatím splněna není. Otevřené body jsou vyjmenované v kapitole 8.

## 1. Shrnutí

- Editor má nový nástroj *Tools / Recognize Text (OCR)* a správu jazyků *Manage OCR Languages*. Prohlížeč (Viewer) akci rozpoznání nenabízí.
- Prvním enginem je Tesseract 5 použitý přímo přes C++ API. Rozhraní enginu je obecné a obsahuje vestavěný testovací engine, takže další engine (PaddleOCR, P1) se přidá bez zásahu do dialogu, session a zapisovače.
- Rozpoznání, korektury a zápis do PDF jsou tři oddělené kroky. Dokument se mění až po potvrzení souhrnu, vždy jako jeden krok historie editoru.
- Výsledkem je neviditelná textová vrstva se standardním režimem vykreslení textu 3 a vloženým fontem bez glyfů. Původní obsah stránky se nepřekresluje ani nepřekomprimovává.
- S aplikací se dodává devět modelů `tessdata_fast`. Další jazyky se stahují do uživatelských dat programu vedle certifikátů.

## 2. Technická rozhodnutí

Kapitola 17 zadání požaduje rozhodnout tyto body před implementací.

| Bod | Rozhodnutí |
| --- | --- |
| Verze knihoven | Tesseract 5.5.2 a Leptonica 1.87.0 z vcpkg. Manifest Flatpaku je připíná archivem a kontrolním součtem. |
| Verze modelů | `tessdata_fast` na commitu `87416418657359cb625c412a48b6e1d6d41c29bd`, `tessdata` na commitu `ced78752cc61322fb554c280d13360b35b8684e4`, `tessdata_best` na commitu `e12c65a915945e4c28e237a9b52bc4a8f39a0cec`. Katalog [ocr/catalog/tesseract-catalog.json](ocr/catalog/tesseract-catalog.json) má 484 položek s velikostí a SHA-256: 161 v profilu *Fast*, 162 v profilu *Standard* a 161 v profilu *Quality*. Chybí v něm modely bez složky LSTM, tedy `equ` a staré fraktury `dan_frak`, `deu_frak` a `slk_frak`, a také `frk` z `tessdata_fast`, který je v repozitáři jen symbolickým odkazem na `deu_latf`. Katalog, manifest vestavěné sady i její licenci vyrábí skript [ocr/tools/generate_catalog.py](ocr/tools/generate_catalog.py). |
| Umístění cíle CMake | Samostatná sdílená knihovna [Pdf4QtOcrTesseract/](Pdf4QtOcrTesseract/). Jen ona smí vkládat hlavičky Tesseractu a Leptonicy. Volba `PDF4QT_ENABLE_OCR`, definice `PDF4QT_OCR_TESSERACT`. Bez nalezeného Tesseractu se volba sama vypne s varováním a zbytek projektu se sestaví. |
| Vložený font | Font bez glyfů `GlyphLessFont` z projektu Tesseract (572 bajtů), zapsaný jako Type0 s Identity-H a CIDFontType2, `/CIDToGIDMap` posílá všechny CID na glyf 1, `/DW 500`, identické `/ToUnicode`. Text se kóduje jako UTF-16BE, znaky mimo BMP jako dvojice CID. Font je v dokumentu jeden a sdílí ho všechny stránky. |
| Vlákna, nebo proces | Vlákna. Rušení přes callback Tesseractu trvá na referenčním stroji 56 až 63 ms, což je hluboko pod cílem 2 s (JOB-06). Každé pracovní vlákno má vlastní instanci enginu. Výjimka vyhozená enginem skončí chybou stránky `WorkerCrashed` a vlákno si vytvoří novou instanci. Pád na úrovni procesu (porušení paměti v knihovně) izolovaný není. |
| Schéma projektu | JSON, identifikátor formátu a verze 1, uložení přes `QSaveFile`. Novější nebo neplatná verze se odmítne. |
| Identita vlastní vrstvy | Slovník stránky `/PieceInfo /PDF4QT_OCR` s privátními daty: verze, identifikátor vrstvy, generace, engine, modely, otisk stránky, odkazy na proud obsahu, font, datový proud a izolační proudy. |

Další rozhodnutí přijatá během implementace:

- **Kanonický souřadnicový prostor** je neotočený uživatelský prostor PDF. Engine vrací surové pixelové souřadnice. Zpětné mapování dělá `PDFOCRPagePreparer` přes matice `pageToRaster` a `rasterToEngine` (GEOM-01 až GEOM-03). Čtyřúhelníky jsou orientované vizuálně, slova jsou uložená v logickém pořadí čtení.
- **Izolace grafického stavu.** Cizí obsah stránky se uzavírá mezi dva sdílené proudy `q` a `Q`. Skenery běžně zapisují `w 0 0 h 0 0 cm /Im0 Do` bez `q`/`Q` a textová vrstva by bez izolace zdědila transformaci i ořez.
- **Nedůvěryhodná metadata.** Odkazy v `/PieceInfo` jsou vstup ze souboru. Objekt se ze stránky odebere nebo z dokumentu smaže jen tehdy, když se ověří, že je skutečně náš. Podvržená metadata tak nemohou smazat cizí obsah ani zapsat mimo pole objektů.
- **Otisk stránky** je SHA-256 přes boxy, rotaci, `UserUnit`, dekódované proudy obsahu bez vlastní vrstvy a výtah zdrojů. Výtah nezávisí na tom, zda je slovník přímý, nebo nepřímý, protože zapisovač nepřímé slovníky zdrojů mění na přímé.
- **Cesty s diakritikou.** Tesseract otevírá soubory úzkými funkcemi, které na Windows používají kódovou stránku ANSI. Modely se proto načítají přes vlastní čtečku souborů postavenou na Qt. Uživatelská slova a vzory se předávají už do inicializace, protože pozdější nastavení nemá v Tesseractu účinek.

## 3. Architektura

Část nezávislá na enginu je v `Pdf4QtLibCore` a nezávisí na Qt Widgets.

| Třída | Úloha |
| --- | --- |
| `PDFOCRPageResult` a související | Datový model: stránka, oblasti, bloky, řádky, slova, jistota, stav revize, provenience (DATA-01 až DATA-03). |
| `PDFOCREngine`, `PDFOCREngineFactory`, `PDFOCREngineRegistry` | Rozhraní enginu, deklarované schopnosti, registr podle stabilního identifikátoru, testovací engine `test` (ARCH-01 až ARCH-05). |
| `PDFOCRPagePreparer` | Analýza stránky, pravidla pro existující text, rasterizace s maskami, předzpracování, otisky, zpětné mapování výstupu. |
| `PDFOCRJobController` | Fronta stránek, pracovní vlákna, rozpočet paměti na rastry, rušení, stavy stránek, chyby (JOB-01 až JOB-10). |
| `PDFOCRSession` | Výsledky, korektury, lokální historie s nejvýše 200 kroky, kandidáti opakovaného rozpoznání, hledání a nahrazování. |
| `PDFOCRTextLayerWriter` | Zápis, čtení a odstranění vlastní vrstvy (PDF-03 až PDF-11, PDF-14, PDF-15). |
| `PDFOCRModelManager` | Katalog, vestavěné, stažené a importované modely, stahování přes https, atomické běhové sady, úklid. |
| `PDFOCRProject` | Projekt OCR a export prostého textu. |

Dialogy `PDFOCRDocumentDialog`, `PDFOCRLanguagesDialog` a pohled `PDFOCRPageView` jsou v `Pdf4QtLibGui`. Napojení na editor je v `PDFProgramController`: změna dokumentu jde přes `PDFModifiedDocument` s příznakem `PageContents`.

## 4. Uživatelská dokumentace

Rozhraní je v angličtině a všechny texty jsou přeložitelné.

### 4.1 Dialog Recognize Text (OCR)

1. **Výběr stránek.** Levý seznam s náhledy a zaškrtávacími poli je jediný výběr pro rozpoznání, zápis, export i odstranění vrstvy. Tlačítka *All*, *None* a *Invert* mění zaškrtnutí. Na kartě *Settings / Pages* lze zvolit všechny stránky, aktuální stránku, viditelné stránky nebo vlastní rozsah, například `1, 3-5, 9`, s filtrem sudých a lichých. Čísla jsou vždy fyzická od 1, nikoli štítky stránek. Pomocný výběr zaškrtne stránky podle heuristiky, například stránky bez textu nebo stránky s chybou.
2. **Nastavení rozpoznání.** Engine, kvalita modelu *Fast*, *Standard* nebo *Quality*, rozvržení stránky, zacházení s existujícím textem a jazyky v pořadí, v jakém je uživatel seřadil. Výchozí jazyk se řídí jazykem rozhraní. Nastavení lze uložit jako pojmenovaný profil. Jedna stránka může mít výjimku v jazycích, rozvržení a rotaci.
3. **Obraz a oblasti.** Rozlišení 200, 300, 400 nebo 600 DPI, případně vlastní hodnota 150 až 1200 DPI. Dále ruční rotace, automatická orientace, narovnání mírně šikmých stránek, převod do šedé, odstranění šumu, inverze a detekce prázdných stránek. Všechny úpravy se týkají jen pracovního obrazu, původní stránka se nemění. Do stránky lze nakreslit oblast k rozpoznání a vyloučenou oblast, například logo nebo razítko.
4. **Rozpoznání.** Tlačítko *Recognize* spustí úlohu na pozadí, *Stop* ji zastaví. Hotové stránky zůstávají. Zastavené nebo neúspěšné opakované rozpoznání nezničí dřívější výsledek stránky. Stránky s ručními opravami se nikdy nepřepíší bez dotazu: lze je přeskočit, nebo nový výsledek po doběhnutí porovnat a rozhodnout pro každou stránku.
5. **Kontrola a opravy.** Strom bloků, řádků a slov je svázaný s obrazem stránky. U slova se zobrazuje jistota enginu s úrovní, na které ji engine poskytl. Hodnota nedostupná se zobrazí jako *n/a*, nikoli jako nula. Práh pro kontrolu je 80 a lze ho měnit bez nového rozpoznání. Opravit lze jakékoli slovo včetně slov s vysokou jistotou. K dispozici je úprava textu slova a celého řádku, potvrzení, označení *not text*, spojení a rozdělení slov, změna pořadí, úprava rámečku slova, doplnění chybějícího řádku, hledání a hromadné nahrazení s náhledem a opakované rozpoznání jednoho řádku nebo slova. Undo a Redo jsou lokální pro dialog.
6. **Zápis do PDF.** Karta *Output* volí mezi úpravou aktuálního dokumentu, vytvořením kopie a pouhým exportem. *Apply* nejprve ukáže souhrn: cíl, stránky, nahrazované vrstvy, počet nezkontrolovaných a nejistých slov a vyloučené stránky s důvodem. Chyba kterékoli stránky zruší celý zápis. Volba *Keep data for detailed review in the document* ukládá do PDF původní texty a skóre a je ve výchozím stavu vypnutá.
7. **Znovuotevření.** Dokument s vlastní vrstvou lze v dialogu znovu otevřít. Vrstva se načte na pozadí a text jde dále opravovat bez nového rozpoznání. Bez uložených revizních dat se jistota zobrazí jako neznámá.
8. **Projekt a export.** Projekt OCR uchovává nastavení, výsledky, opravy a otisky stránek. Při otevření se načtou jen stránky, jejichž obsah se nezměnil. Export do TXT v UTF-8 nabízí zachování řádků, spojení slov dělených na konci řádku, normalizaci NFC a oddělovač stránek. Zpráva exportu uvádí vynechané stránky.

Chování u zvláštních dokumentů:

- Dokument bez oprávnění k úpravám nelze zapsat ani do kopie, text lze exportovat, pokud to dovolují oprávnění ke kopírování obsahu.
- U podepsaného dokumentu se zobrazí varování.
- Dokument s deklarací PDF/A nebo PDF/UA nelze upravit na místě. Kopie se zapíše bez této deklarace, protože shodu nelze ověřit.
- U tagovaného dokumentu se vrstva označí jako artefakt a strukturní strom se nemění.

### 4.2 Jazyky a modely

- Vestavěná sada profilu *Fast*: čeština, angličtina, slovenština, němčina, španělština, ruština, zjednodušená a tradiční čínština a data orientace `osd`. Manifest [ocr/tesseract/fast/manifest.json](ocr/tesseract/fast/manifest.json) uvádí verzi, SHA-256 a licenci.
- Profily *Standard* a *Quality* žádné vestavěné modely nemají. Všechny jejich jazyky se stahují z katalogu ve správci jazyků. Důvodem je měření v kapitole 7.4: větší modely jsou výrazně pomalejší a na zkušebním korpusu nejsou přesnější. Data orientace si tyto profily berou z vestavěné sady *Fast*.
- Modely pocházejí z oficiálních repozitářů `tesseract-ocr/tessdata_fast` a `tesseract-ocr/tessdata_best` na GitHubu. Stahují se z `raw.githubusercontent.com` na připnutých commitech.
- Uživatelské úložiště je `<AppDataLocation>/ocr`, tedy vedle složky `certificates`. Obsahuje složky `tesseract/fast`, `tesseract/best`, `tesseract/custom`, `tesseract/runtime` a `downloads`.
- Stažení vyžaduje výslovné potvrzení s výčtem jazyků, velikostí a cílovou složkou. Přijímá se jen https. Kontroluje se velikost, SHA-256, to, že server nevrátil stránku HTML, a nakonec načtení modelu enginem. Neúspěšné stažení se nikdy nedotkne funkční starší verze.
- Vlastní model lze importovat ze souboru. Dostane příponu `@id importu` a označení, že jeho původ není ověřen.
- Engine nikdy nečte přímo z úložiště. Pro každou kombinaci jazyků se připraví běhová sada, tedy kopie jen pro čtení, která se zveřejní atomicky až po ověření kontrolních součtů. Běžící rozpoznání tak není ovlivněno stahováním ani odstraněním modelu.
- Úklid proběhne jednou při prvním načtení správce, pokud zámek nedrží jiná instance. Vrátí poslední funkční verzi po pádu uprostřed aktivace, smaže dočasné soubory starší než jeden den a běhové sady nepoužité déle než 30 dní.
- Bez sítě aplikace pracuje s vestavěnými a dříve staženými jazyky. Jiná síťová komunikace než výslovně vyžádané stažení neexistuje. OCR nic neloguje.

## 5. Pokrytí požadavků P0

Stav po opravách z interního auditu. *Splněno* znamená implementováno a kryto automatickým testem tam, kde to jde. *Částečně* má zbývající část popsanou v kapitole 8.

| Skupina | Stav | Poznámka |
| --- | --- | --- |
| UI-01 až UI-07 | Splněno, UI-05 částečně | Pamatuje se geometrie a hlavní dělič. Barvy překryvu jsou pevné. |
| PAGE-01 až PAGE-06 | Splněno | Výběr z editoru je neaktivní, protože postranní panel editoru vícenásobný výběr nemá. |
| INPUT-01 až INPUT-05 | Splněno | Sken s krátkým digitálním nebo cizím neviditelným textem je smíšená stránka a vyžaduje rozhodnutí. Vlastní vrstva se pozná podle vazby na obsah přes SHA-256, vrstvu změněnou jiným nástrojem program nikdy neodstraní. Automatické maskování smíšených stránek je P1. |
| REGION-01 až REGION-05 | Splněno | Překrývající se oblasti se shodným nastavením se sjednotí, rotace oblasti se uplatní. Příznaky vyloučených oblastí se přepočítají po každé změně a znovu při zápisu. |
| LANG-01 až LANG-13 | Splněno | Závislosti modelů se stahují a skládají do běhové sady, nenačitatelný model a model pro jinou hlavní verzi enginu mají stav *Incompatible*. Běžící úloha drží zámek své běhové sady. |
| REC-01 až REC-03 | Splněno | Parametry enginu mají typové schéma s rozsahy, nepovolený parametr se odmítne. |
| IMAGE-01 až IMAGE-07 | Splněno | Rastr respektuje `/UserUnit` a rozměrový limit enginu. Detekovaná orientace se použije jen při dostatečné jistotě. |
| CONF-01 až CONF-05 | Splněno | |
| EDIT-01 až EDIT-11 | Částečně | Řádky lze v dialogu spojit, rozdělit a přesunout do jiného bloku, hledání najde frázi přes více slov, statistika ukazuje oblasti bez rozpoznaného textu. Úprava účaří v dialogu chybí, zapisovač ale účaří používá. |
| PDF-01 až PDF-15 | Splněno | Vnoření obsahu stránky ověřuje lexer i po zápisu, nevyvážený cizí obsah dostane vlastní izolaci. Deklarace PDF/A a PDF/UA se hledá a odstraňuje podle jmenného prostoru, kopie se nezapíše, když odstranění selže. Certifikační podpis DocMDP s oprávněním 1 zakáže zápis, s oprávněním 2 a 3 dovolí jen kopii. |
| EXPORT-01 až EXPORT-05 | Splněno, EXPORT-02 částečně | Vynechané stránky se uvádějí fyzickým číslem bez štítku. EXPORT-06 je P1. |
| JOB-01 až JOB-10 | Částečně | Otisky, analýza, sestavení běhové sady modelů i ověření staženého modelu běží mimo hlavní vlákno. Časový limit stránky je společný pro všechny fáze. Rozpočet paměti je tvrdá mez a zahrnuje paměť modelů. Náhledy nemají mezipaměť. |
| ARCH-01 až ARCH-09 | Splněno | Schopnost `maximumImageSize` se vynucuje při rasterizaci. |
| DATA-01 až DATA-03 | Splněno | Surový výsledek rozpoznání se ukládá vedle upravené verze i do projektu. Projekt má limity velikosti a počtů. |
| GEOM-01 až GEOM-05 | Splněno | |
| OPS-01 až OPS-06 | Částečně | Viz balení v kapitole 9. |
| QA-01 až QA-05 | Splněno kromě profilu *Best* | Viz kapitola 7. |

## 6. Akceptační testy

Automatické testy jsou v [UnitTests/tst_ocrtest.cpp](UnitTests/tst_ocrtest.cpp) a [UnitTests/tst_ocrdialogtest.cpp](UnitTests/tst_ocrdialogtest.cpp) a běží v ctest. Testy závislé na Tesseractu nebo na vestavěných modelech se bez nich přeskočí. Přeskočený test ctest ukáže jako úspěšný, proto je třeba u balíků kontrolovat výpis QtTestu.

| Test | Automaticky | Testovací funkce | Zbývá |
| --- | --- | --- | --- |
| AT-01 | ano | `pageRangeParsing` | Neplatný rozsah zadaný v dialogu. |
| AT-02 | částečně | `modelManagerBuiltIn`, `tesseractRecognition`, `workflowWithTesseract` | Důkaz, že nevznikl žádný síťový požadavek. Čistá instalace ručně. |
| AT-03 | ano | `modelDownload` | Výchozí umístění vedle certifikátů ručně. |
| AT-04 | částečně | `modelDownload` | Plný disk. Chyba zápisu se hlásí samostatně, ale test ji neumí vyvolat. |
| AT-05 | částečně | `modelDownload` | Stará sada přežije aktualizaci. Dvě skutečné instance ručně. |
| AT-06 | ano | `confidenceStatistics` | |
| AT-07 | ano | `editingOperations`, `lineTextEditing`, `sessionRegressions` | |
| AT-08 | částečně | `replaceAllAndCandidates`, `sessionRegressions` | Dotaz dialogu před přepsáním oprav. |
| AT-09 | ano | `textLayerRoundTrip` | Cizí prohlížeče ručně. |
| AT-10 | ano | `renderPreservation` | Pixelová shoda a bajtová shoda obrazových dat v paměti. |
| AT-11 | ano | `renderedGeometryRoundTrip`, `preprocessingPipeline`, `textLayerRobustness` | Rotace, CropBox, UserUnit, narovnání, orientace, zbytková transformace cizího obsahu. |
| AT-12 | ano | `idempotentApplyAndRemove`, `textLayerRobustness` | |
| AT-13 | částečně | `pageAnalysisAndPolicy` | Přeskakování stránek a zpráva v dialogu. |
| AT-14 | částečně | `jobCancellation`, `stoppedRerunKeepsResults` | Rušení během analýzy a vykreslování. |
| AT-15 | ano | `pageErrorAndRetry` | |
| AT-16 | částečně | `workflowWithTestEngine` | Správce historie editoru je v testu nahrazen přímým nastavením dokumentu. |
| AT-17 | ano | `projectRoundTrip`, `sessionRegressions` | |
| AT-18 | částečně | `textExport`, `textLayerRobustness` | Starší inkrementální revize souboru. |
| AT-19 | částečně | `annotationsAndRedactions` | Vrstvy OCG a ořez. |
| AT-20 | ne | | Odkazy, formuláře, přílohy a záložky po zápisu. Podpisy ručně. |
| AT-21 | ano | `genericAdapter` | |
| AT-22 | částečně | `tesseractRecognition`, `modelDownload` | Cesta s diakritikou, restart se staženými modely. Instalátor a přenosný balík ručně. |
| AT-23 | ne | | P1, PaddleOCR. |
| AT-24 | ano | `invalidEngineOutput` | Vnější proces neexistuje, protokol se netestuje. |

Výsledek posledního běhu na referenčním stroji:

| Sada | Výsledek |
| --- | --- |
| `UnitTestsOCR` | 26 prošlo, 1 přeskočen (benchmark běží jen na vyžádání) |
| `UnitTestsOCRDialog` | 5 prošlo |
| celý `ctest` | 27 z 27 prošlo |

## 7. Kvalita a výkon

### 7.1 Metodika

- Měří testovací funkce `qualityAndPerformanceBenchmark`. Spouští se proměnnou `PDF4QT_OCR_BENCHMARK=1` s nativním platformním pluginem, protože potřebuje systémová písma. Počet stran určuje `PDF4QT_OCR_BENCHMARK_PAGES`, výstupní soubor `PDF4QT_OCR_BENCHMARK_REPORT`. Pro dlouhý běh je nutné zvýšit `QTEST_FUNCTION_TIMEOUT`, jinak QtTest funkci po 300 s ukončí.
- Korpus: syntetický čistý tisk, 11 pt, písma Times New Roman a Arial, A4, 300 DPI. Deset řádků na jazyk a písmo, český text s diakritikou.
- CER je Levenshteinova vzdálenost po znacích, WER po slovech oddělených mezerou. Jediná normalizace je sloučení bílých znaků do jedné mezery. Diakritika ani interpunkce se neodstraňují a vynechaný text se počítá jako chyba (QA-01).
- Benchmark selže, pokud CER přesáhne 2 % nebo WER 5 % (QA-02).
- Propustnost: dokument s N stranami sdílejícími jeden obraz, 2 pracovní vlákna, rozpočet rastrů 1 GiB. Přírůstek velikosti se měří na prvních deseti stranách.

Referenční stroj: AMD Ryzen 9 7950X, 16 jader a 32 logických procesorů, 63 GB RAM, Windows 11 Pro 10.0.26200, sestavení MSVC Release, Qt 6.11.2, Tesseract 5.5.2, profil *Fast*.

### 7.2 Kvalita

| Sada | CER | WER | Rozsah |
| --- | --- | --- | --- |
| čeština (`ces`) | 0,78 % | 3,49 % | 1154 znaků, 172 slov |
| angličtina (`eng`) | 0,00 % | 0,00 % | 1276 znaků, 212 slov |
| smíšená (`ces+eng`) | 0,00 % | 0,00 % | 694 znaků, 102 slov |

Cíl QA-02 je splněn pro oba jazyky. Korpus je malý a syntetický. Pro nekvalitní skeny a rukopis tato čísla neplatí.

### 7.3 Výkon a paměť

| Veličina | 4 strany | 1000 stran |
| --- | --- | --- |
| Celkový čas | 2,1 s | 490,7 s |
| Propustnost, 2 vlákna | 116,8 stran/min | 122,3 stran/min |
| Čas stránky p50 | 998 ms | 904 ms |
| Čas stránky p95 | 998 ms | 1238 ms |
| Špička pracovní sady procesu | 300 MB | 455 MB |
| Přírůstek PDF na stránku | 16 126 bajtů | 15 897 bajtů |
| Odezva zrušení | 63 ms | 56 ms |

- Jedna stránka včetně studené inicializace enginu trvá asi 570 ms.
- Úloha 1000 stran A4 při 300 DPI doběhla bez pádu. Paměť s počtem stran neroste, rastry se uvolňují hned po rozpoznání (QA-05).
- Přírůstek odpovídá textu, fontu a metadatům. Na stránku připadá 318 slov, tedy asi 50 bajtů na slovo po kompresi.
- Čísla platí pro tento stroj, model a korpus. Nejsou příslibem obecné rychlosti.

### 7.4 Srovnání profilů

Měřeno angličtinou na stejném stroji, 60 stran, 2 pracovní vlákna. Profil volí proměnná `PDF4QT_OCR_BENCHMARK_PROFILE`. Benchmark nic nestahuje. Modely větších profilů je před měřením třeba dočasně uložit do zdrojového stromu příkazem `python ocr/tools/generate_catalog.py --builtin-standard eng --builtin-best eng --update-builtin` a potom stejným příkazem s prázdnými seznamy zase odebrat.

| Veličina | Fast | Standard | Quality |
| --- | --- | --- | --- |
| Velikost modelu `eng` | 4,1 MB | 23,5 MB | 15,4 MB |
| Propustnost | 106,7 stran/min | 78,6 stran/min | 50,2 stran/min |
| Čas stránky p50 | 1111 ms | 1542 ms | 2434 ms |
| Špička pracovní sady procesu | 350 MB | 332 MB | 349 MB |
| CER a WER, čistý tisk 300 DPI | 0 % a 0 % | 0 % a 0 % | 0 % a 0 % |
| CER a WER, sken převzorkovaný na 80 DPI | 0,31 % a 1,42 % | 0,08 % a 0,47 % | 0,08 % a 0,47 % |
| CER a WER, 70 DPI | 0,39 % a 1,42 % | 0,71 % a 3,77 % | 0,63 % a 3,30 % |
| CER a WER, 60 DPI | 1,41 % a 4,25 % | 1,25 % a 4,72 % | 1,10 % a 3,77 % |

- Zhoršený sken vzniká převzorkováním stránky na dané rozlišení a zpět, přidáním šedého šumu se směrodatnou odchylkou 8 a snížením kontrastu. Zapíná ho `PDF4QT_OCR_BENCHMARK_DEGRADED_DPI`, šum mění `PDF4QT_OCR_BENCHMARK_DEGRADED_NOISE`.
- Rozdíl v rychlosti je velký a stálý: *Standard* je asi o čtvrtinu a *Quality* asi o polovinu pomalejší než *Fast*.
- Rozdíl v přesnosti je na tomto korpusu v řádu jednotlivých slov z 212 a nemá stálý směr. Při 70 DPI vyšel nejlépe profil *Fast*.
- Při silném šumu, od směrodatné odchylky 20, selhávají všechny tři profily stejně, kolem 50 % CER. Selhává rozbor stránky, nikoli jazykový model, takže volba profilu nepomůže.
- Korpus je malý a syntetický. Skutečné skeny, malá písma a jazyky s diakritikou mohou dopadnout jinak. Čeština v profilech *Standard* a *Quality* změřena není.
- Závěr: distribuce obsahuje jen modely profilu *Fast*, větší modely si uživatel stáhne.

## 8. Známé mezery a odchylky

Skutečné mezery vůči P0:

- **Ruční přejímka neproběhla.** Linux a macOS nebyly sestaveny ani vyzkoušeny. Hledání, označování a kopírování textu nebylo ověřeno v Acrobat Readeru, PDFiu ani Poppleru. Neověřeno je i ovládání klávesnicí, škálování displeje a české překlady nových textů.
- **Profily *Standard* a *Quality* jsou změřeny jen pro angličtinu** (QA-04), viz kapitola 7.4.
- **Profil *Standard* je nad rámec zadání.** Zadání zná jen profily Rychlý a Kvalitní. Modely repozitáře `tessdata` obsahují i data původního enginu, režimy OEM 0 a 2 ale zůstávají odmítnuté u všech profilů.
- **Úpravy účaří a orientace** nejsou v dialogu (EDIT-04). Zapisovač účaří řádku používá.
- **Vertikální text** nemá vlastní režim zápisu, píše se ve směru své geometrie s varováním. Text zprava doleva je ošetřen a testován na úrovni geometrie.
- **Detekce orientace** Tesseractu nejde přerušit. Obraz se před ní zmenší na delší stranu 2000 pixelů a termín stránky se kontroluje před ní i po ní (JOB-06).
- **Chyba vykreslení** stránky znamená chybu rozpoznání stránky, i když by se stránka zobrazila přijatelně.
- **Osiřelé objekty.** Po odstranění poslední vrstvy zůstane v dokumentu sdílený font a dva izolační proudy, dohromady asi 1 kB.
- **Pád nativní knihovny** Tesseractu není izolován v samostatném procesu. Zachytí se jen výjimky C++ (OPS-05).
- **Testy.** AT-20 nemá automatický test. Přeskočené testy se v ctest tváří jako úspěšné.

Vědomě odloženo na P1 a P2 podle zadání: PaddleOCR a AT-23, automatické maskování smíšených stránek, exporty hOCR, TSV a ALTO, dávky více souborů, příkazová řádka v PdfTool, regulární výrazy a kontrola pravopisu, detekce log a šablony oblastí.

## 9. Sestavení a balení

- Závislosti jsou v [vcpkg.json](vcpkg.json) a [vcpkg_with_qt.json](vcpkg_with_qt.json). Vestavěné modely se kopírují do stromu sestavení a instalují jen při zapnutém OCR. Když modely ve zdrojovém stromu chybějí, CMake vypíše varování.
- **Modely.** Vestavěné modely jsou v repozitáři jako archivy xz, jeden model na archiv, v `ocr/tesseract/fast/tessdata/<jazyk>.tar.xz`. Zabírají 13 MB místo 35 MB, protože se soubor `traineddata` komprimuje zhruba na třetinu. CMake je při konfiguraci rozbalí do stromu sestavení, odkud se také instalují. Sestavení tedy nic nestahuje a funguje bez sítě, což je podmínka pro Flatpak a pro sestavení ze zdrojového archivu. Model se rozbalí znovu jen po změně svého archivu. Chybějící nebo poškozený archiv obnoví `python ocr/tools/generate_catalog.py --fetch-builtin`, které stáhne model podle manifestu a ověří jeho velikost a SHA-256. Běh bez tohoto přepínače přegeneruje katalog a k tomu stahuje do mezipaměti všechny modely, asi 3 GB. Na novější verzi modelů se přechází přepínači `--fast-commit`, `--standard-commit` a `--best-commit`, které přijmou commit, větev nebo značku. Skript přečte seznam souborů commitu z API GitHubu, stáhne všechny modely do mezipaměti, ověří je proti velikosti a git SHA-1 commitu a zapíše jejich SHA-256 do katalogu. Opakovaný běh se stejnými commity soubory repozitáře nezmění. Sadu vestavěných jazyků profilu mění přepínače `--builtin-fast`, `--builtin-standard` a `--builtin-best`, po nich je třeba spustit `--update-builtin`, aby se přepsaly archivy. Soubory instalátoru pro Windows jsou vyjmenované v [WixInstaller/Product.wxs.in](WixInstaller/Product.wxs.in) a je třeba je upravit ručně. Model bez složky LSTM, tedy starší data pouze pro původní engine, skript do katalogu nezapíše.
- **Windows.** Komponenty OCR v [WixInstaller/Product.wxs.in](WixInstaller/Product.wxs.in) generuje CMake ze skutečného stromu sestavení: adaptér, knihovny Tesseractu a Leptonicy s jejich závislostmi, vestavěné modely, manifesty a licenční texty. Při vypnutém OCR se do instalátoru nedostanou. Tesseract 5.5.2 a Leptonica 1.87.0 jsou v manifestech vcpkg připnuté přes `builtin-baseline` a `overrides`. Volba `PDF4QT_OCR_REQUIRED` změní chybějící Tesseract nebo archiv modelu z varování na chybu konfigurace, je určená pro vydání. Instalátor nebyl sestaven ani vyzkoušen.
- **Flatpak.** [Flatpak/io.github.JakubMelka.Pdf4qt.json](Flatpak/io.github.JakubMelka.Pdf4qt.json) má moduly Leptonica 1.87.0 a Tesseract 5.5.2 s kontrolními součty. Tesseract se zde sestavuje bez curl a libarchive, Windows balík je obsahuje. Manifest nebyl sestaven.
- **macOS a AppImage** nebyly řešeny.
- **Licence.** Tesseract a modely tessdata mají Apache 2.0, Leptonica BSD-2. Texty jsou ve složce [3rdparty_licenses/](3rdparty_licenses/), která se podle dosavadní praxe projektu neinstaluje. Dialog *About* uvádí verze Tesseractu a Leptonicy zjištěné za běhu a identifikátor vestavěné sady. Přechodné závislosti Windows balíku, tedy libarchive, libcurl, giflib, libtiff, libwebp, liblzma, lz4 a zstd, v dialogu *About* uvedené nejsou.
