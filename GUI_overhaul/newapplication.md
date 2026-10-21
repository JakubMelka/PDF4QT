# PDF4QT – nová aplikace (plán implementace)

Jedna aplikace nahradí Viewer, Editor, PageMaster, Diff i LaunchPad. Návrh je Figma `PDF4QT` (`QVGmx58DYmMq41kwfpMjtl`), stránky *Main page* (1:1734), *View Document* (176:4482), *Tools Overview* (253:11416) a stránky *Tools - …*. Komponenty jsou hotové v `Pdf4QtLibFluent` ([components.md](components.md)), katalog toolů je v [tools.md](tools.md).

Podrobné části:

- [newapp_navigation.md](newapp_navigation.md) – stránky jako web: cesty, historie, *Page Not Found*
- [newapp_storage.md](newapp_storage.md) – SQLite ve WAL místo QSettings, více instancí najednou
- [newapp_tools.md](newapp_tools.md) – `PDFDocumentTool` (jádro) a `PDFDocumentToolGUI` (příprava na fázi 2)
- [newapp_mcp.md](newapp_mcp.md) – MCP server: vlákna, bezpečnost, tooly, resources
- [newapp_whatsnew.md](newapp_whatsnew.md) – stránka What's New: jeden Markdown soubor na verzi, přepínání verzí, kontrola aktualizací

Body I-xx z [components_review.md](components_review.md) se řeší později a plán na ně nečeká.

---

## 1. Rozhodnutí

| # | Rozhodnutí |
| --- | --- |
| 1 | **Fáze 1** = aplikace bez toolů katalogu (Home, Open, Recent, dokument, anotace, Settings, katalog Tools jen jako stránka, AI/MCP). **Fáze 2** = tooly katalogu (46). |
| 2 | Staré aplikace a `Pdf4QtLibGui` slouží jen jako **seznam funkcí, které nesmí vypadnout** ([tools.md](tools.md), kapitola 6). Jejich kód se nepřebírá. Po fázi 2 se smažou: Viewer, Editor, PageMaster, Diff, LaunchPad, `Pdf4QtLibGui`, `Pdf4QtEditorPlugins` a `PDFPlugin` (pluginy v nové aplikaci nejsou, [tools.md](tools.md) 9.5). |
| 3 | Používá se `Pdf4QtLibCore` a z `Pdf4QtLibWidgets` zobrazení dokumentu: `PDFDrawWidgetProxy`, `PDFDrawSpaceController`, kompilátory stránek a textu, správci anotací a formulářů, `PDFWidgetTool`. Vazby na staré prostředí se z nich odstraní (QSettings v `pdfannotationstyle.cpp` a `pdfadvancedtools.cpp`, povinné `QAction` a `parentDialog` v `PDFToolManager`, dialogy `.ui`). |
| 4 | **Žádné QSettings.** Nastavení, historie a stav jsou v jedné databázi SQLite ve WAL; čte se vždy z databáze, nic se necachuje ([newapp_storage.md](newapp_storage.md)). |
| 5 | **Navigace jako web:** každá stránka, podstránka, sekce nastavení i tool má cestu; neznámá nebo neimplementovaná cesta ukáže *Page Not Found* ([newapp_navigation.md](newapp_navigation.md)). |
| 6 | Operace nad dokumentem jsou `PDFDocumentTool` v `Pdf4QtLibCore` (bez GUI, použitelné z MCP i z PdfTool), jejich GUI je `PDFDocumentToolGUI` v aplikaci. `PDFWidgetTool` zůstává pro režimy myši (anotace, lupa, výběr) ([newapp_tools.md](newapp_tools.md)). |
| 7 | MCP server: Streamable HTTP na `http://127.0.0.1:8765/mcp` (Figma `pdf4qt-ai` 653:5231), vlákno na spojení, požadavky vyřizuje hlavní vlákno; rychlé operace (stav, text a vykreslení jedné stránky) přímo, delší a změny nad **separátní kopií původního dokumentu**, kterou worker smí upravovat ([newapp_mcp.md](newapp_mcp.md)). |
| 8 | Pravidla kódu jako v `Pdf4QtLibFluent` (CLAUDE.md *Conventions*): bez lambd, životnost přes `QPointer`, obranné programování, widgety jen v hlavním vlákně, CRLF, bez anonymních namespace, `Q_EMIT`. |
| 9 | Okno bez systémového rámu jako Chrome: taby v titulku, vpravo tlačítka minimalizace, maximalizace a zavření (rozhodnuto 18. 10. 2026). |
| 10 | Spustitelný soubor se jmenuje `PDF4QT`. Staré nastavení a historie se nepřevádějí. |
| 11 | **Druhé spuštění** (dvojklik na PDF, *Otevřít v programu*, příkazová řádka) otevře soubory jako nové taby v okně běžící instance, která drží jméno pro předání (první spuštěná; naposledy aktivní okno ze všech instancí se nesleduje): nový proces je předá přes `QLocalServer` / `QLocalSocket` (jméno podle uživatele a relace, zprávy jen cesty, stránka a aktivační token okna, délka omezená) a skončí. **Spuštění bez souborů** (ikona, nabídka Start) otevře nové okno = samostatnou instanci, nic nepředává. `--new-window` spustí samostatnou instanci, přenosný režim se předávání neúčastní. Soubor otevřený v jiné instanci se otevře znovu bez varování (změnu na disku pozná *Automatic refresh*). Rozhodnuto 19. 10. 2026. |
| 12 | **Jedno okno na proces**, taby jde jen přeuspořádat; přetažení tabu do nového okna není ve fázi 1 (19. 10. 2026). |
| 13 | **Instalace až po fázi 2** (WiX, Flatpak, AppImage, asociace `.pdf`); ve fázi 1 se aplikace spouští z build stromu (19. 10. 2026). |

## 2. Moduly

| Cíl | Závisí na | Obsah |
| --- | --- | --- |
| `Pdf4QtLibCore` (rozšíření) | – | `PDFDocumentTool`, katalog a registr toolů, parametry → JSON Schema; veřejný getter popisků stránek (`PDFCatalog` ho nemá); `PDFDocumentTextIndex` – textová vrstva celého dokumentu postavená na pozadí z kopie dokumentu, po dokončení neměnná, sdílená hledáním v GUI i MCP |
| `Pdf4QtLibMcp` (nový, SHARED) | Core, Qt Network | protokol MCP, HTTP/1.1 + SSE, relace, rozhraní hostitele `PDFMcpHost`; bez Widgets, testovatelný bez GUI |
| `Pdf4QtLibApp` (nový, SHARED) | Core, Widgets, Fluent, Mcp, `sqlite3` (vcpkg) | úložiště, router a stránky, okno a taby, dokumentová karta, hostitel MCP, základ `PDFDocumentToolGUI` |
| `Pdf4QtApp` (nový, spustitelný `PDF4QT`) | LibApp | tenký `main.cpp` (argumenty, jedna `QApplication`) |

`vcpkg.json`: přidat `sqlite3`. Qt HttpServer se nepoužije (v instalaci Qt není a obsluhuje vše v jednom vlákně).

## 3. Okno (Figma, společné všem rámcům)

- `app-top-bar` 38 px = titulek okna bez systémového rámu (jako Chrome): tab *Home* (nezavíratelný), taby dokumentů a toolů s vlastní kartou (zámek, tečka neuložených změn, průběh), *New tab button* 48:184, volná plocha pro přesun okna (dvojklik = maximalizace) a vpravo tlačítka okna. Windows: `WM_NCCALCSIZE` / `WM_NCHITTEST` (přichycení, Snap Layouts nad tlačítkem maximalizace, změna velikosti na okraji, stín systému); Linux: `QWindow::startSystemMove` / `startSystemResize`, na Waylandu dekorace klienta.
- Karta Home: postranní panel 210 px (logo, navigace ve třech skupinách, přepínač Light / Dark / Auto) + pracovní plocha 1230 px s hlavičkou (titulek, podtitulek, globální hledání *Search tools, documents, or help…*, Ctrl+K) a patičkou (verze, MIT, Windows / Linux / macOS). Settings hlavičku s hledáním nemají.
- Tlačítka okna: komponenty `Window button` / `Window controls` (*Components*) ve všech lištách. Windows: vpravo, Maximize je cíl `HTMAXBUTTON` (Snap Layouts), stav *Inactive* u neaktivního okna; Linux stejně; macOS: systémové „semafory“ vlevo a lišta tabů začíná za nimi. Maximalizované okno má taby u horního okraje.
- Tlačítko „+“ otevře tab *New tab* se stránkou Open ([newapp_navigation.md](newapp_navigation.md) 3). Tab dokumentu chráněného heslem má ikonu hesla, zámek patří jen zámku dokumentu.
- Karta dokumentu (193:756): lišta 52 px, postranní panel 300 px se splitterem, plátno, stavový řádek 28 px. Pravý panel jen v režimu toolu.
- Přepínač tématu v postranním panelu a *Settings › General › Color scheme* jsou jedno nastavení (`appearance.colorScheme`).

### Prostředí aplikace

- **Příkazová řádka:** `PDF4QT [soubor…] [--page <n>] [--portable] [--new-window]`; relativní cesty se převedou na absolutní podle pracovní složky spouštějícího procesu. `--page` je fyzická stránka od 1 jako `?page=` ([newapp_navigation.md](newapp_navigation.md) 3), platí pro všechny soubory příkazu a má přednost před poslední stránkou z Recent; stránka mimo rozsah se ignoruje (stejně `?page=` mimo rozsah v cestě dokumentu – dokument se otevře, ne *Page Not Found*). Neplatný argument nebo chybějící soubor = zpráva v okně, které soubory otevírá (i po předání), ne pád.
- **Předání běžící instanci** (rozhodnutí 11):
  - Volba „předat, nebo naslouchat“ běží pod `QLockFile`, takže ze současně spuštěných procesů (výběr více PDF v Průzkumníku spustí proces na každý soubor) naslouchá jen jeden; zastaralý soket po pádu se odstraní (`QLocalServer::removeServer`) jen pod zámkem. Zámek i soket jsou v místě jen pro uživatele (Linux `$XDG_RUNTIME_DIR`, ne sdílený `/tmp`, kde by jméno mohl obsadit jiný uživatel; Windows `%TEMP%` uživatele a roura s ACL jen pro uživatele, `UserAccessOption`). Jméno obsahuje uživatele a relaci (Windows id relace, Linux `$WAYLAND_DISPLAY` / `$DISPLAY`), aby se soubory neotevřely na jiné obrazovce.
  - Předání přijímá instance, která drží jméno (rozhodnutí vlastníka: stačí první instance); spuštění bez souborů nic nepředává a spustí vlastní instanci s oknem; `--new-window` soubory nepředá a jméno převezme, jen když je volné. Odpověď = soubory přijaté do fronty, ne otevřené; bez odpovědi do 5 s se proces spustí samostatně a druhý server nevznikne.
  - Zpráva je nedůvěryhodný vstup: absolutní cesty, stránka, chyby argumentů a aktivační token, nic jiného (žádné volby jako `--portable`); limity délky, počtu souborů a času čtení.
  - Soubor, který už je v instanci otevřený (kanonická cesta), přepne na svůj tab jako *Open*, s `--page` i na stránku. Prázdný aktivní tab *New tab* nahradí první předaný dokument (jako pracovní prostor). Během modálního dialogu se soubory otevřou až po jeho zavření (jako `documents.open` z MCP, [newapp_mcp.md](newapp_mcp.md) 3 bod 6), okno se aktivuje hned.
  - Ukončování: server přestane naslouchat na začátku ukončování (před dotazy na neuložené změny), takže přijatý požadavek se neztratí; když uživatel ukončení zruší, zkusí naslouchat znovu (jméno mohla mezitím převzít nová instance – pak běží bez předávání).
  - Aktivace okna: minimalizované okno se obnoví. Windows: klient před odesláním zavolá `AllowSetForegroundWindow` pro proces serveru (`GetNamedPipeServerProcessId`), jinak systém jen rozbliká tlačítko na hlavním panelu. Wayland: okno přepne jen token `XDG_ACTIVATION_TOKEN` spouštějícího procesu (přečte se před vytvořením `QApplication`), X11 obdobně `DESKTOP_STARTUP_ID` (běžící instance jím ukončí i oznámení o spuštění); bez tokenu jen `QApplication::alert`. macOS: Finder posílá soubory běžící aplikaci jako `QFileOpenEvent`, předání se uplatní jen z příkazové řádky.
- **Systémové dialogy** (soubor, složka, tisk, nastavení stránky) jsou nativní. Nenativní `QFileDialog` / `QPrintDialog` Qt kreslí z palety (fialová). Na Linuxu se proto použije xdg-desktop-portal a bez portálu jde o známou mezeru. **Tisk a nastavení stránky nemají na Linuxu nativní dialog vůbec** (Qt tam kreslí vždy vlastní `QPrintDialog` / `QPageSetupDialog`, portál tisku nepoužívá): na Linuxu se použije **vlastní dialog tisku** podle Figmy (tiskárna, stránky, kopie, orientace, formát; rozhodnutí vlastníka 19. 10. 2026), na Windows nativní. Rámec se do Figmy doplní před krokem 3. E-mail: Windows MAPI, Linux `xdg-email --attach`.
- **Logy:** `qInstallMessageHandler` (volaný z libovolného vlákna) zapisuje do `AppLocalDataLocation/logs/pdf4qt-<datum>.log` (Linux podle XDG do `StateLocation/logs`), uchovává 7 dní a nejvýš 10 MB. Více instancí připisuje do souboru dne celé řádky s PID. Do logu nejde token MCP, heslo ani obsah dokumentu. V přenosném režimu se logy nezapisují, jdou jen na stderr (About to tak ukáže).
- **Překlady:** jazyk rozhraní je nastavení `appearance.language` (stránka Settings v kroku 5), výchozí angličtina, ne *Auto* (rozhodnutí O12). Nové cíle jsou v `PDF4QT_lupdate`. Markdown (příručka, What's New) je anglicky a překlad je volitelný soubor `<název>.<jazyk>.md` (`cs`, `pt_BR`; hledá se `pt_BR` → `pt` → angličtina). Asistent dostane přes `pdf4qt://docs` vždy anglický text, i když jsou to stejné soubory jako příručka `/help`.
- **Obsah, který se musí napsat:** příručka `/help` a katalog videí Support (krok 2), soubor What's New pro 1.6.0 (krok 2), dokumentace, skilly a postupy MCP a texty nastavení klientů (krok 7).

## 4. Fáze 1 – kroky

Každý krok: implementační agent → dvě nezávislé kontroly (věrnost Figmě, senior C++ a bezpečnost) → oprava → vlastní ověření (build bez varování, testy, valgrind). Stejný postup jako u komponent ([components.md](components.md) 6.5).

| Krok | Obsah | Figma |
| --- | --- | --- |
| 1 | Kostra: cíle CMake, `sqlite3`, úložiště a notifikace změn s testy více procesů, přenosný režim, příkazová řádka a předání souborů běžící instanci včetně aktivace okna (otevření dokumentu je krok 3), logy, `PDFSettingDefinition` a registr příkazů (id, název, ikona, zkratka – potřebuje je hledání v kroku 2), router + *Page Not Found*, okno bez rámu s taby, tlačítky okna a tabem *New tab* (Windows, X11, Wayland, macOS), postranní navigace, přepínač tématu, patička | shell všech rámců, 947:1504, 940:134845, `pdf4qt-new-tab` 955:152339, 941:138200 |
| 2 | Home, Open, Recent, What's New (kontrola aktualizací), About (build, cesta k nastavení a logům; v přenosném režimu, že se nic nezapisuje), Support (statický katalog videí – viz poznámka pod tabulkou), Sponsor, příručka `/help`; nedávné soubory s náhledy, složky, připnuté soubory a umístění, pracovní prostory a *Restore last session*, nabídky ⋯, přetažení souboru; globální hledání (Tools, Commands, Settings, Documents, Help) a stránka všech výsledků | 28:4, 61:1201, 80:910, 103:1951, 110:2163, 116:2419, 122:2652, 285:3026, 941:7081, 955:142130, 941:135742, 941:136915, 955:147918, 955:149658, 955:150171, 955:150880, 941:137569, `pdf4qt-help` 955:152873, `pdf4qt-recent-empty` 955:142865 |
| 3 | Dokument: otevření (heslo; z příkazové řádky a z předání, `--page`, už otevřený soubor), zobrazení, lišta, panely File / View / Go to, menu Select, postranní panel (8 panelů + výsledky hledání), stavový řádek s podpisy a chybami vykreslení, vlastnosti dokumentu (5 sekcí; úprava údajů a XMP jde přes undo/redo a u zamčeného dokumentu je zakázaná), zámek a *Edit a Copy*, Save / Save as / Print / E-mail, Automatic refresh, Undo / Redo, celá obrazovka, lupa, poslední stránka a stav pohledu dokumentu, osobní záložky, dialogy neuložených změn, podepsaného dokumentu (jednou na tab a verzi dokumentu) a chyby uložení; dialog zavření tabu s běžící operací platí ve fázi 1 pro dlouhé uložení a běžící změnu z MCP, ve fázi 2 i pro tooly. Položky kontextové nabídky náhledů, které jsou tooly (Insert Pages, Move to, Extract, Remove Pages), vedou na cestu toolu – ve fázi 1 *Page Not Found*; otáčení je příkaz; odkazy a akce v dokumentu (cíl v dokumentu, URL a spuštění aplikace s potvrzením podle Settings › Security, JavaScript formulářů); volby hledání (celá slova, velikost písmen, regulární výraz); vývojářské zobrazení (bloky a řádky textu, časy vykreslení) v Settings › Advanced. Figma (doplněno 19. 10. 2026): odkazy `pdf4qt-document-link-hover` 1028:22041, `-link-confirm` 1028:22475, `-launch-confirm` 1028:22931, `-launch-file-confirm` 1028:23389, `-link-blocked` 1028:23847; chyba otevření `-open-failed` 1028:24297; tisk pro Linux (včetně nastavení stránky) `-print` 1029:24203, `-print-no-printer` 1029:24881, `-printing` 1030:25227, `-print-error` 1030:25672; poznámka 1030:161015 | 193:756, 195:1085 … 195:3910, 196:2482, 196:2645, 196:3327, 198:4520, 198:5380, 198:5554, 198:6178, 228:8709 … 228:9097, 236:8882 … 236:9574, 266:12010, 266:12502, nové dialogy `pdf4qt-document-close-unsaved` 955:144943, `pdf4qt-quit-unsaved` 955:145378, `pdf4qt-document-close-running` 955:145813, `pdf4qt-document-refresh-unsaved` 955:146242, `pdf4qt-document-save-signed` 955:146677, `pdf4qt-document-save-error` 955:147112, 955:153156, 955:153719, poznámka 958:28033 |
| 4 | Anotace a vložení: panely Annotate a Insert (text, poznámky, tvary, razítka, webový odkaz, odkaz na stránku, položka osnovy; *Page numbers* je tool – ve fázi 1 *Page Not Found*), úprava osnovy (+ v panelu Outline), lišta aktivního nástroje, lišta nad výběrem textu, *Mark as annotations*, panel Notes, úprava anotace na stránce (výběr s úchyty, přesun, velikost, otočení, body, zarovnání, kopie na více stránek, kontextová nabídka, vlastnosti, odpovědi, vyskakovací poznámka – logika je v `PDFWidgetAnnotationManager` a `PDFAnnotationManipulator`, dialogy `.ui` se nahradí), mazání anotací, vyplňování formulářů; Figma (doplněno 19. 10. 2026): `pdf4qt-document-annotation-selected` 1015:17319, `-point-drag` 1017:17699, `-rubber-band` 1018:18079, `-multi` 1018:18584, `-menu` 1019:18817, `-multi-menu` 1019:19473, `-overlap-menu` 1019:20171, `-properties` 1022:20233 (modální dialog, jeden krok undo), `-comment` 1025:21533, `-copy-to-pages` 1024:20678, `-geometry` 1024:21252, přehledy `-kinds` 1026:21809 a `-dialog-variants` 1027:21823, poznámka 1030:161012; úprava nástrojů v `Pdf4QtLibWidgets` (bez QSettings a povinných `QAction`, styly anotací z databáze) | 877:9598, 196:3053, 198:4978, 877:132786, 207:6306, 207:6770, 877:133774, 737:9061, 737:9141, 195:1131 |
| 5 | Settings – všech 9 sekcí podle Figmy (jazyk rozhraní výchozí angličtina, O12), napojených na engine (vykreslování, CMS, řeč, zabezpečení, formuláře, autor), hledání v nastavení, *Restore defaults*, *Clear history* toolů | 146:2843 … 147:4565, 160:4203, 944:8912, 941:135918, 955:144107, 941:136741, 955:143792, `pdf4qt-settings-storage-banners` 958:28036 |
| 6 | Katalog Tools ze statického katalogu jádra (46 toolů, připnuté, kolekce a jejich správa, naposledy použité, filtrované hledání), panel Tools v dokumentu; odkazy na neimplementované tooly vedou na *Page Not Found* | `pdf4qt-tools` 958:151754 (kopie 272:12821 na *Main page*), 530:2957, 283:1888, 196:3650 |
| 7 | MCP server a stránka AI (běží / zastaveno / vzdálený přístup / jiné okno / obsazený port, oprávnění Allow / Ask / Deny, nový token), dotazy na potvrzení v dokumentu, tooly, resources a prompty fáze 1, dokumentace a skilly | 653:5231, 653:5814, 653:6384, 940:135321, 940:135813, 940:136305, `pdf4qt-ai-port-in-use` 955:148587, 955:149138, 955:150808, `pdf4qt-home-ai-waiting` 967:12672; v dokumentu 940:133693, `pdf4qt-document-ai-confirm-read` 966:150610, 940:134248, 958:27539, 958:27048, 955:153841, 955:154475, 955:151877, 955:151457, 940:33875, 955:144508, poznámka 958:28030 |
| 8 | Závěrečné ověření: test životnosti stránek (každá cesta se otevře, zavře a smaže v každém pořadí jako `UnitTestsFluentLifetime`), test více instancí, valgrind všech testů, ruční běh na Windows a Linuxu (X11 i Wayland), CLAUDE.md (nové moduly v *Architecture*) | – |

**Support (krok 2, rozhodnutí vlastníka 20. 10. 2026):**

- Videa zatím neexistují. Katalog `PDFSupportVideoCatalog` (statická tabulka v `Pdf4QtLibApp`, texty přes `QT_TRANSLATE_NOOP`) obsahuje **maketu** podle Figmy 116:2419: 12 ukázkových videí s náhledy v resources a odkazem na kanál YouTube místo odkazů na konkrétní videa. Skutečná videa se doplní do stejné tabulky, kód stránky se nemění.
- Položka videa: `id`, název, popis, délka, odkaz (jen `https://www.youtube.com/…`), téma (čip), štítky, `toolIds` (tooly katalogu, které video ukazuje). Náhledy jsou přibalené, nic se nestahuje. Tutoriál úvodní stránky toolu (fáze 2) odkazuje na `id` z tohoto katalogu.
- **Filtr *For you* je založený na použití toolů:** obsahuje videa, jejichž `toolIds` se překrývají s tooly v `tool_usage` (mechanismus v [newapp_tools.md](newapp_tools.md) 3). Čip je vždy vidět; bez použitých toolů (celá fáze 1) ukáže prázdný seznam s vysvětlujícím textem. Seznam se čte z databáze při zobrazení a po oznámení změny.
- Stav filtru je v cestě (`/support?topic=…&q=…`); karta videa a tlačítko kanálu otevřou odkaz přes navigátor (jediný výstup ven, jen https).

Testy: `UnitTestsAppStorage`, `UnitTestsAppCore` (příkazová řádka, předání souborů běžící instanci – souběžný start několika procesů, zastaralý soket po pádu, poškozené a přerostlé zprávy, `--new-window`, přenosný režim, ukončování –, logy více procesů, registr příkazů), `UnitTestsAppNavigation`, `UnitTestsAppDocument`, `UnitTestsMcp` (bez GUI, falešný hostitel), `UnitTestsAppMcp` (skutečná aplikace offscreen) – řádky v CLAUDE.md.

## 5. Příprava na fázi 2 (součást fáze 1)

- Rozhraní `PDFDocumentTool`, parametry, výsledek, registr a statický katalog 46 toolů v jádře; testovací tool jen v testech prověří cestu katalog → stránka toolu → MCP.
- Tabulky toolů v databázi (připnuté, kolekce, naposledy použité, počitadla dvojic pro *Then continue with*, poslední hodnoty parametrů); mechanismus použití a řetězení je v [newapp_tools.md](newapp_tools.md) 3.
- Vyhrazené cesty `/tools/{toolId}`, `/document/{documentId}/tool/{toolId}`, `/tool/{toolId}/{instanceId}`.
- Kostra `PDFDocumentToolGUI` (shell toolu 291:994, dialog výsledku 296:2761) se staví až s prvním toolem fáze 2 (pilot Compress, pak Merge pro vlastní kartu), [newapp_tools.md](newapp_tools.md) 5.
- Poznámky k instalaci (fáze 2): asociace `.pdf` na Windows s `MultiSelectModel=Player` (výběr více souborů = jeden proces), `.desktop` s `%F`; `PDFOCRModelManager::getApplicationDataRoot` musí mapovat i aplikaci `PDF4QT` (dnes `MelkaJ` → `PDF4QT Editor`), jinak nová aplikace neuvidí stažené modely OCR.

## 6. Doplněno ve Figmě (18. 10. 2026)

Vlastník povolil pro tento úkol doplnit chybějící části návrhu; doplněné rámce prošly kritickou kontrolou UI/UX a kontrolou shody s tímto plánem a nálezy jsou opravené (zprávy agentů v `scratchpad/newapp/figma_fixed_*.md`).

| Oblast | Rámce (*Main page*, pokud není uvedeno jinak) |
| --- | --- |
| Okno | `Window button` 947:1475 (Minimize / Maximize / Restore / Close × Default / Hover / Pressed / Inactive, Maximize Disabled), `Window controls` 947:1504 (Platform Windows / Linux / macOS, Maximized) na *Components*, ve všech 572 lištách; nové tokeny `bg/danger`, `bg/danger-pressed` (v kódu `PDFThemeColors::BgDanger`, `BgDangerPressed`) a pro „semafory“ macOS `window/macos-close`, `window/macos-minimize`, `window/macos-zoom`, `window/macos-inactive` (`WindowMacosClose` … `WindowMacosInactive`) |
| Navigace | `pdf4qt-not-found` 940:134845, `pdf4qt-new-tab` 955:152339, `pdf4qt-help` 955:152873, katalog `pdf4qt-tools` 958:151754 (kopie závazného 272:12821 pro prototyp *Main page*, starý katalog 91:1319 smazán) |
| Hledání | `pdf4qt-search-all` 941:7081, bez výsledků 955:142130 |
| Home / Open / Recent | nabídky ⋯ 941:137569, otevřené menu řazení `pdf4qt-recent-sort` 976:12914, `pdf4qt-recent-clear-history` 941:135742, `pdf4qt-recent-empty` 955:142865, uložení pracovního prostoru 941:136915 a duplicitní název 955:147918, smazání 955:149658 a přejmenování 955:150171 pracovního prostoru, přenosný režim na Recent 955:150880 |
| Settings | rozbalené seznamy 944:8912, *Restore Defaults* 941:135918 a zakázané 955:144107, historie toolů 941:136741 a po smazání 955:143792, přenosný režim 941:138200, pruhy stavu databáze 958:28036 |
| AI | 653:5231, 653:5814, 653:6384 (oprávnění Allow / Ask / Deny, aktivita se všemi stavy, odpojení asistenta), jiné okno 940:135321, nový token 940:135813, otevření dokumentu 940:136305, obsazený port 955:148587, jiný klient 955:149138, poznámky a přepínače stavů 955:150808 |
| Dokument (*View Document*) | dotaz na změnu 940:133693, uložení 940:134248, uložení kopie 958:27539, podepsaný dokument 958:27048, fronta 955:153841, požadavek v jiném tabu 955:154475, náhled na stránce 955:151877, po změně 940:33875, změny v Notes 955:151457, uloženo 955:144508; dialogy 955:144943 … 955:147112; tab dokumentu s heslem má ikonu `icon/password` |

Zůstává otevřené (vlastník):

1. Rozhodnuto koordinátorem, k potvrzení: *Ask* u *Read documents* = jeden dotaz za relaci asistenta; zavření tabu asistentem patří pod *Read documents*; *New tab* jde zavřít a odkazy mimo `/open` přepnou na tab Home; *Finished* = poslední stránka byla zobrazená.
2. Pravidla v poznámkách ke čtení: po *Deny* se další čtení zeptá znovu; *Allow* platí i pro dokumenty otevřené později.

## 7. Rozhodnuté otázky (18. 10. 2026)

| # | Otázka | Rozhodnutí |
| --- | --- | --- |
| O1 | Okno | Bez systémového rámu, taby v titulku jako Chrome, vpravo tlačítka okna (rozhodnutí 9). |
| O2 | MCP server při více instancích | Server běží v instanci, která obsadí port, asistenti vidí jen její dokumenty. Stránka AI ostatních instancí ukáže, že server je blokovaný jinou instancí ([newapp_mcp.md](newapp_mcp.md) 4). |
| O3 | Převod starého nastavení | Nepřevádí se. |
| O4 | Spustitelný soubor | `PDF4QT`. |
| O5 | Zastavení běhu toolu | Jediné tlačítko: na místě potvrzovacího tlačítka Secondary *Stop* s `icon/stop`, *Cancel* / *Close* a tlačítko Stop v liště toolu se během běhu skryjí (všech 20 rámců `*-running`, např. 295:1711, 378:3038). |
| O6 | Drobnosti | Zakázaný zaškrtnutý *Checkbox* se nemění; barva náhledu navržených anotací se dořeší při implementaci; tlačítka v 955:150808 a popisky v 941:137732 opravené. |
| O7 | Jazyk | Americká angličtina v textech Figmy, v kódu i v dokumentaci (*color*, *gray*, *center*, *canceled*). |
| O8 | Anotace a odkazy (19. 10. 2026) | Bez „Always allow for this document“ (zůstává globální přepínač v Security); Bring to Front / Send to Back ve fázi 1 ne; *Hide on Page* v kontextové nabídce; nabídka překrývajících se anotací i Alt+klik; lišta anotace nahoře uprostřed na místě lišty nástroje; vlastnosti = modální dialog s *Apply* (jeden krok undo), barva a šířka živě v liště; zvýraznění textu si ponechá dnešní možnosti (velikost, otočení, zrcadlení); text zamčené anotace a text „dokument nedovoluje změny“ jsou dva různé. |
| O9 | Tisk (19. 10. 2026) | Dokument je během tisku použitelný (tiskne se snímek); výchozí stránky *All*, při vybraných náhledech *Selected*. |
| O10 | Výchozí hodnoty (19. 10. 2026) | `bookmarks.autoGenerate` vypnuto; databáze zamčená jinou instancí se čeká bez limitu; převod na RAII guard painteru jen ve Fluent a v novém kódu, Core a Widgets se nemění. |
| O11 | Spodní okraj karty Home (20. 10. 2026) | Spodní odsazení pracovní plochy 16 px (místo 32) a přepínač Light / Dark / Auto 8 px od okraje okna (místo 24); text patičky a přepínač zůstávají na jedné ose. Promítne se do všech rámců Figmy s patičkou. |
| O12 | Jazyk rozhraní (20. 10. 2026) | Výchozí hodnota nastavení `appearance.language` je **angličtina** (`en`), ne *Auto*. *Auto* (jazyk systému) zůstává jako volba. Figma (Settings › General) ukazuje vybrané *Auto* – Figma se neopravuje, platí program. |
