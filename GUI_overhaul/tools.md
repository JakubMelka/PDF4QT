# PDF4QT – katalog nástrojů (Tools)

Podklad pro stránku **Tools** nové sjednocené aplikace PDF4QT. Popisuje, co je „tool“, do jakých kategorií tooly patří, jakou mají barvu, co přesně dělají, podle jakých klíčových slov je uživatel najde a co nám proti konkurenci chybí.

Stav dokumentu: návrh k připomínkám, 2. 10. 2026. Upraveno 3. 10. 2026 podle přijatých bodů UX revize ([tools_review.md](tools_review.md), triáž v [tools_review_triage.md](tools_review_triage.md)). Odkazy typu „(C5)“ odkazují na body triáže. Figma se tímto dokumentem nemění. Co je v ní potřeba upravit, shrnuje část [Úkoly do Figmy](#úkoly-do-figmy).

Zdroje: zdrojový kód větve `master` (Editor, Viewer, PageMaster, Diff, všech devět pluginů a PdfTool), větev `origin/branches/issue85` (OCR), stávající návrh stránky `pdf4qt-tools` ve Figmě a rešerše konkurenčních aplikací v kapitole 7.

## Obsah

- [Úkoly do Figmy](#úkoly-do-figmy)

1. [Co je a co není tool](#1-co-je-a-co-není-tool)
2. [Zásady katalogu](#2-zásady-katalogu)
3. [Kategorie a barvy](#3-kategorie-a-barvy)
4. [Přehled toolů](#4-přehled-toolů)
5. [Podrobný popis toolů](#5-podrobný-popis-toolů)
6. [Mapa pokrytí dnešních aplikací](#6-mapa-pokrytí-dnešních-aplikací)
7. [Rešerše konkurence](#7-rešerše-konkurence)
8. [Co chybí – návrhy nových toolů](#8-co-chybí--návrhy-nových-toolů)
9. [Doporučení pro UI katalogu](#9-doporučení-pro-ui-katalogu)
10. [Rozdíly proti současné Figmě a rozhodnutí](#10-rozdíly-proti-současné-figmě-a-rozhodnutí)

---

## Úkoly do Figmy

Úpravy Figmy (`PDF4QT`, stránky *Main page*, *View Document*, *Components*), které vyplývají z tohoto dokumentu a z přijatých bodů revize. Nejde o otevřené otázky, ale o práci na návrhu. Sloupec **Body** odkazuje na body triáže, sloupec **Zde** na místo v tomto dokumentu, kde je zadání popsané podrobně, sloupec **Stav** říká, co už je ve Figmě hotové (obecné úpravy provedeny 3. 10. 2026, katalog a stránky toolů čekají).

| # | Úkol | Rámec ve Figmě | Body | Zde | Stav |
| --- | --- | --- | --- | --- | --- |
| 1 | **Katalog Tools podle tohoto dokumentu:** 46 toolů v šesti kategoriích, včetně *Add Image as Page*. Kategorie *Optimize* (bez „& Repair“), chip *Protect & Sign*, prohození odstínů Edit (Green) a Optimize (Violet). *Remove Hidden Data* místo Sanitize, *Convert Images to Grayscale*, popis *Sign by Hand* „Draw, type or insert a signature“. Popisy karet nejvýš na dva řádky. Plánované tooly (kapitola 8) se nezobrazují. | `pdf4qt-tools` | C7, C8, D3, D5, D6, D7, D8, D9 | §3, §4, §10 | **hotovo** – nový rámec `pdf4qt-tools` na stránce *Tools Overview*: 46 toolů v šesti kartách kategorií s podskupinami, štítek *Advanced* u toolů tiskové přípravy a vývojářských (komponenta *Tool row* má novou vlastnost *Show tag*), chipy All 46 · Pinned · Pages 12 · Convert 8 · Edit 2 · Protect & Sign 11 · Optimize 5 · Review 8, Edit zelená a Optimize fialová, připnuté tooly, kolekce a naposledy použité přejmenované. Původní `pdf4qt-tools` na *Main page* zůstává beze změny |
| 2 | **Dlaždice na Home:** *Sanitize* přejmenovat na *Remove Hidden Data*. | `pdf4qt-home` | D6 | §4.4 | **hotovo** – dlaždice *Remove Hidden Data*, popis „Strip metadata“ (delší popis se do dlaždice nevejde) |
| 3 | **Dvě pole hledání na stránce Tools:** obě zůstávají. Katalogové hledání zobrazuje jen tooly, při aktivním chipu ukáže i počet shod v ostatních kategoriích. | `pdf4qt-tools` | C4 | §9.1 | **částečně** – rámec `pdf4qt-search-results` na *Tools Overview*: panel výsledků globálního hledání pod polem (skupiny Tools · Commands · Settings · Documents · Help, tooly v barvě kategorie, důvod shody „matches …“, klávesy ↑↓ Enter Esc, *Show all results*). Počet shod v ostatních kategoriích u katalogového hledání čeká |
| 4 | **Panel File:** u *Settings…* odebrat zkratku Ctrl+K (patří globálnímu hledání). Přidat přepínač zámku dokumentu. | `pdf4qt-document-panel-file` | A5, F1 | §4.7, §9.1 | **hotovo** – Ctrl+K odebráno; zaškrtávací položka *Lock document* (ikona zámku) za *Automatic refresh*, stejný vzor jako *Automatic refresh* |
| 5 | **Tab dokumentu:** ikonka zámku u zamčeného dokumentu a značka neuložených změn. Průběh dlouhé operace na tabu. | všechny rámce s taby | F1, G2, G5 | §4.7, §9.3 | **hotovo** – komponenta *Tab* má nové vlastnosti *Locked* (jantarový zámek za názvem), *Unsaved* (tečka místo křížku) a *Progress* (pruh u spodního okraje); ukázka *Example – Tab states* v *Components* |
| 6 | **Zamčený dokument:** zakázané ikonky pro úpravy (Save, Undo, Redo, Insert a další). Akce *Edit a Copy*. | `pdf4qt-document` | F1, F2, F4 | §4.7 | **hotovo** – nový rámec `pdf4qt-document-locked`: zamčený tab, zakázané Save, Undo, Redo a Insert (komponenta *Toolbar menu button* má nový stav *Disabled*), popover u zámku s tlačítky *Unlock* a *Edit a Copy* |
| 7 | **Splitter** pro změnu šířky postranního panelu. | `pdf4qt-document` | A1 | §6.2 | **hotovo** – úchytka `splitter` (6 px, tři tečky) mezi postranním panelem a stránkou; jen v `pdf4qt-document`, ostatní rámce dokumentu ji zatím nemají |
| 8 | **Úvodní stránka toolu** (spuštění ze stránky Tools): výběr souboru (otevřené taby jedním kliknutím, soubor z disku, přetažení), u toolů s více vstupy výběr více souborů, krátká dokumentace toolu a odkaz na tutoriál na YouTube. | nový rámec | E4, E5 | §9.2, §9.3 | **částečně** – ukázka `pdf4qt-tool-start` (Compress) na stránce *Tools Overview*: drobečková navigace, výběr otevřeného dokumentu jedním kliknutím (včetně poznámky o neuložených změnách), *Open a file…*, přetažení, pod seznamem dokumentace toolu vykreslená z Markdownu (odstavce, tabulka předvoleb, postup, seznamy, poznámky, ukázka příkazové řádky), vpravo nahoře zvýrazněná karta tutoriálu na YouTube v barvě kategorie, pod ní tabulka *At a glance* a odkazy na manuál. Varianta pro tooly s více vstupy čeká |
| 9 | **Stránka toolu:** cíl („Smlouva.pdf · 3 pages“), volby, náhled výsledku, potvrzovací tlačítko pojmenované podle výsledku na stálém místě, průběh, souhrn s *Open result*, chyba zápisu s ponechanými volbami. V režimu toolu vlastní akce v hlavní liště včetně Undo a Redo, které platí jen pro tool. | nový rámec | C5, G1–G8 | §9.3 | čeká |
| 10 | **Konkrétní stránky toolů:** Redact („N areas marked – not removed yet“, *Create Redacted Copy*), Compress (předvolby, naměřená velikost, rozpad velikosti v panelu), Remove Hidden Data (tlačítka *Before sending*, *Before publishing*), Remove External Links (seznam odkazů s možností je vynechat), Interleave Pages (náhled prvních stran). | nové rámce | C5, H4, H5, H7, H10 | §5 | **částečně** – stránka *Tools - Optimize*: všech pět toolů kategorie Optimize, jeden řádek na tool (Compress 6 rámců, Optimize Images 4, Optimize Structure 3, Convert to Black & White 7, Convert Images to Grayscale 3), propojené prototypovými vazbami. Toolbar toolu nese název, Undo/Redo, akce toolu, odkaz *Tutorial*, *Cancel* a potvrzovací tlačítko; navigace stránek a zoom jsou v režimu toolu ve stavovém řádku. Nabídky toolbaru mají ikony a vzorky, po dokončení se u všech toolů zobrazí jednotný dialog výsledku (§9.3 bod 11), chyba zápisu je stav tohoto dialogu (`optimize-compress-done-write-error`, bod 16). Optimize Structure ukazuje provedené kroky jako seznam s ikonou, výsledkem a stavem, ne jako textový log. Stránka *Tools - Create and Convert*: sedm toolů kategorie Create & Convert bez Recognize Text (OCR), jeden řádek na tool, 51 rámců (Images to PDF 8, Blank PDF 3, Scan to PDF 11, PDF to Images 9, Extract Text 5, Extract Images 7, Create Audio Book 8), propojené prototypovými vazbami. Návrh prošel kritickou revizí UI/UX a rozhodnutí z ní jsou zapsaná v §9.3 (body 11, 15 a 18 až 20): Blank PDF nemá dialog výsledku, export jednoho souboru se ptá na cíl až v dialogu, export více souborů má složku a šablonu názvu v pravém panelu, *Stop* a *Cancel* mají různý význam, Scan to PDF má variantu pro WIA. Stránka *Tools - Organize Pages*: všech dvanáct toolů kategorie Organize Pages, jeden řádek na tool, 79 rámců `pages-<tool>[-state]` (Merge 10, Split 9, Extract Pages 7, Interleave Pages 5, Manage Pages 14, Insert Pages 7, Add Image as Page 4, Remove Pages 3, Rotate Pages 3, Crop Pages 7, Resize Pages 5, Set Page Boxes 5), propojené prototypovými vazbami. Merge, Interleave Pages a Manage Pages mají vlastní kartu, sestava (skupiny, kontrolní body, *Open Assembly* a *Save Assembly*) je jen v Manage Pages, ostatní tooly běží v kartě dokumentu. Tooly na sebe neodkazují, dialogy výsledku mají volbu *Then continue with* (komponenty *Continue with* a *Tool choice* v *Components*, ukázka výběru v `pages-merge-done-continue`). Stejnou komponentou je nahrazen původní řádek *Next:* i na stránkách *Tools - Optimize* (6 dialogů) a *Tools - Create and Convert* (4 dialogy). Přepínač *Suggest tools I often use next* a *Clear history* na stránce Settings čekají. Resize Pages má navíc volbu orientace cílového formátu (*Auto*, *Portrait*, *Landscape*), kterou dnešní Page Geometry nemá. Tokeny názvů souborů jsou sjednocené na všech stránkách toolů: `#` číslo výstupu, `@` číslo stránky, `%` index vstupu. Každá ikona má jediný význam (nové ikony v *Components / Icons*). Návrh prošel kritickou revizí UI/UX (4. 10. 2026). Stránka *Tools - Protect and Sign*: všech jedenáct toolů kategorie Protect & Sign, jeden řádek na tool, 108 rámců `protect-<tool>[-state]` (Protect with Password 11, Restrict Permissions 10, Encrypt with Certificate 10, Remove Password 6, Sign with Certificate 15, Add Timestamp 7, Sign by Hand 8, Manage Certificates 11, Redact Content 15, Remove Hidden Data 9, Remove External Links 6), propojené prototypovými vazbami (hlavní flow na tool a zvláštní flow pro okrajové stavy). Čtyři šifrovací tooly sdílejí stejnou stavbu (stav dokumentu vlevo, volby uprostřed, souhrn vpravo) a dialog pro heslo vlastníka. Zaškrtávací seznamy (oprávnění, skrytá data, odkazy) mají zaškrtávací políčko vlevo. Sign with Certificate, Add Timestamp a Redact Content zapisují nový soubor a jeho název se zadává v dialogu výsledku, existující soubor a chyba zápisu jsou stavy tohoto dialogu. Manage Certificates má vlastní kartu. Vlastní barva výplně redakce používá paletu barev anotací, výchozí je černá. Nové ikony jsou v *Components / Icons* (`icon/unlock` až `icon/chain`). Návrh prošel kritickou revizí UI/UX (4. 10. 2026). Stránka *Tools - Review and Inspect*: všech osm toolů kategorie Review & Inspect, jeden řádek na tool, 119 rámců `review-<tool>[-state]` (Compare 26, Measure 26, Output Preview 15, Ink Coverage 9, Soft Proofing 9, Document Statistics 7, Object Inspector 8, Document Report 19), propojené prototypovými vazbami (hlavní flow na tool a zvláštní flow pro okrajové stavy). Compare má vlastní kartu se vstupy *Original* a *Revised* (soubor, přetažení, otevřené taby, prohození, heslo k zašifrovanému souboru), stavy *not compared yet*, *no differences* a chybu rozsahu stránek; filtry, pohledy, zobrazení a volby porovnání jsou nabídky toolbaru, barvy rozdílů jsou v pravém panelu. Measure má osm druhů měření v nabídce, měřítko s předvolbami, kalibrací a správou předvoleb, jednotky a vzhled popisků v pravém panelu. Output Preview má šest režimů, výběr obsahu, tabulku barev s hodnotou pod kurzorem a plochou na stránce. Ink Coverage má výběr stránek, průběh se *Stop* a export tabulky. Soft Proofing má přepínače *Soft Proof* a *Gamut Check*, profil a záměr; *Keep Proofing On* nechá náhled zapnutý i po zavření toolu. Document Report je nové GUI nad příkazy `info-*` a `xml` (devět sekcí, formát Text, HTML a XML, formát dat, *Structure as XML*). Barvy se vybírají kulatými vzorky přímo v pravém panelu. Nové ikony jsou v *Components / Icons* (`icon/compare` až `icon/snap`), nové proměnné barev `ink/*`, `heat/*` a `diff/*` v kolekci *Color*. Návrh prošel kritickou revizí UI/UX (4. 10. 2026). Kategorie Edit a tool Recognize Text (OCR) čekají |
| 11 | **Společná komponenta pro výběr stránek** v knihovně komponent. | *Components* | E3 | §9.6 | **hotovo** – komponenta *Page selection* (varianty *Mode* = All, Current, Selected, Range; vlastnosti Label, Total, Count, Error, Error text; filtry Even, Odd, Portrait, Landscape); ukázka chybného rozsahu vedle komponenty |
| 12 | **Panel Thumbnails:** výběr více stránek, operace nad výběrem (kontextová nabídka), akce *Extract to New Document*. | `pdf4qt-document` (Thumbnails) | E2 | §5.1 | **hotovo** – nový rámec `pdf4qt-document-thumbnails-selection` (tři vybrané stránky, „3 of 1,592 pages selected“) a komponenta *Context menu / Thumbnails* (Rotate Left, Rotate Right, Insert Pages…, Move to…, Extract to New Document, Remove Pages) |
| 13 | **Karta *Assembly*** (sestava stránek z více zdrojů) a **karta Compare** (vstupy *Original* a *Revised*, prohození, výběr z otevřených tabů, stavy *not compared yet*, *no differences*, *error*). | nové rámce | A6, E1, H6 | §5.1, §5.6 | **částečně** – sestava (*Assembly*) je nakreslená na stránce *Tools - Organize Pages* v řádku Manage Pages (zdroje, náhledy, skupiny, kontrolní body, odebrané stránky, otevření a uložení sestavy, chybějící zdroje, výstup do jednoho nebo více souborů). Merge a Interleave Pages mají vlastní kartu bez sestavy. Karta Compare je nakreslená na stránce *Tools - Review and Inspect* v řádku Compare (26 rámců `review-compare-*`) |
| 14 | **Stavový řádek:** položka s počtem podpisů a jejich chybami („2 signatures · 1 needs attention“), kliknutím otevře panel Signatures. | `pdf4qt-document` | H3 | – | **hotovo** – položka `status-signatures` (*Status bar item*, tón Warning, ikona shield-check) před položkou rendering issues |
| 15 | **Kolekce:** sedm vestavěných kolekcí (Prepare for sharing, Scan to searchable PDF, Fix a scan, Sign a contract, Archive documents, Prepare for print, Extract content) a správa vlastních kolekcí. | `pdf4qt-tools`, nový rámec | J2 | §9.1 | **hotovo** – kolekce v panelu Collections katalogu na *Tools Overview*; rámec `pdf4qt-manage-collections` (seznam vestavěných a vlastních kolekcí, editor: název, ikona, tooly s přesunem a odebráním, přidání toolu hledáním s návrhy, *Reset to default* u vestavěných, *Delete* jen u vlastních) |

---

## 1. Co je a co není tool

**Tool** je položka katalogu, která pojmenovává jeden uživatelský úkol („sloučit PDF“, „zaheslovat“, „porovnat dvě verze“). Má název, jednořádkový popis, ikonu, kategorii s barvou a klíčová slova pro hledání. Spouští se z karty na stránce Tools, z hledání, nebo z dokumentu.

Tool **není** totéž co `PDFWidgetTool`. `PDFWidgetTool` je technická třída pro interaktivní režim myši nad stránkou (výběr textu, lupa, kreslení anotace). Tyhle režimy se spouštějí přímo na stránce dokumentu, hlavně z položek Select a Annotate, a v katalogu se neopakují:

| Vrstva | Příklad | Kde žije v novém UI |
| --- | --- | --- |
| **Tool** (tento dokument) | Merge, Compress, Redact Content | Stránka Tools, hledání, připnuté tooly |
| **Příkaz dokumentu** | Zoom, Fit Width, Find, Print, Save, Undo | Lišta a panely dokumentu (File, View, Go To) |
| **Režim myši** (`PDFWidgetTool`) | Select Text, Select Table, Magnifier, Screenshot, všechny anotace | Lišta dokumentu, položky Select a Annotate |
| **Chování dokumentu** | Vyplňování formulářů, odkazy, ověření podpisů při otevření | Přímo ve stránce, bez spouštění |
| **Panel** | Outline, Thumbnails, Layers, Attachments, Speech, Signatures, Notes | Postranní panel dokumentu |
| **Nastavení** | Rendering, Color management, Shortcuts | Stránka Settings |

Pravidlo pro zařazení: tool má **vlastní obrazovku nebo dialog s nastavením a s výsledkem** (změněný dokument, nový soubor, zpráva), nebo jde spustit **bez otevřeného dokumentu**. Co se dělá přímo na stránce nebo v panelu, tool není. Proto v katalogu nejsou anotace, vyplňování formulářů, osnova, přílohy, čtení nahlas, ověření podpisů ani výběr tabulky. Kapitola 6 mapuje každou dnešní akci do jedné z těchto vrstev, takže nic nevypadne.

Čtyři tooly jsou vědomá výjimka. **Measure**, **Redact Content** a **Sign by Hand** jsou uvnitř režimy myši, ale mají vlastní lištu a závěrečný krok s výsledkem. **Soft Proofing** je přepínač pohledu s vlastním nastavením a patří k ostatním toolům tiskové přípravy.

### Stavy toolů

| Stav | Význam |
| --- | --- |
| **GUI** | Dnes existuje jako samostatná akce, dialog nebo plugin. |
| **GUI·split** | Funkce dnes existuje, ale je schovaná ve větším celku. V katalogu dostává vlastní vstupní bod. |
| **CLI** | Umí to jen `PdfTool` z příkazové řádky. GUI chybí. |
| **Branch** | Hotovo v jiné větvi, do `master` ještě nezačleněno. |
| **New** | Chybí. Návrh, viz kapitola 8. |

---

## 2. Zásady katalogu

Vychází z doporučení Nielsen Norman Group pro pojmenování příkazů a informační architekturu a z toho, jak katalogy řeší Acrobat, Stirling-PDF, PDF24, Smallpdf a iLovePDF. Zdroje jsou v kapitole 7.

1. **Název říká úkol, ne technologii.** Sloveso, nejvýš čtyři slova: *Compress*, ne *Optimizer*. Předmět se přidává tam, kde bez něj není jasné, čeho se tool týká: *Rotate Pages*, *Optimize Images*. Slovo „PDF“ se v názvu neuvádí, protože v aplikaci jen pro PDF nic neříká. Zůstává jen tam, kde udává směr převodu (*PDF to Images*, *Images to PDF*). Odborný termín patří do klíčových slov (*bitonal*, *linearize*, *sanitize*). Výjimkou jsou názvy pohledů zavedené v oboru (*Output Preview*, *Ink Coverage*, *Object Inspector*).
2. **Jeden tool, jeden úkol.** Dialog s mnoha účely se v katalogu rozpadne na víc vstupních bodů, i když pod kapotou zůstane jedna obrazovka s jinou předvolbou. Dnešní dialog Encryption tak dává čtyři tooly a PageMaster dvanáct.
3. **Klíčová slova pokrývají slovník uživatele.** Synonyma, názvy z konkurence, formáty souborů, britský i americký pravopis a slova, kterými by úkol popsal laik. *Merge* se musí najít přes *join*, *combine*, *assemble*, *concatenate* i *bind*.
4. **Kategorií je šest.** Kategorie odpovídají šesti úkolům, se kterými uživatel přichází: uspořádat stránky, vytvořit nebo převést, upravit, zabezpečit a podepsat, zmenšit, zkontrolovat. Spotřebitelské katalogy mají pět až osm kategorií podle úkolu, náš návrh je nejblíž iLovePDF (část 7.2). Na každou kategorii připadá jeden z šesti odstínů, které ve Figmě máme. Jemnější dělení uvnitř kategorie řeší podskupiny bez vlastní barvy. Žádná kategorie se nejmenuje „Other“ ani „Advanced“.
5. **Barva nese kategorii, ne význam jednotlivého toolu.** Všechny tooly kategorie mají stejný odstín. Barva nikdy není jediný nosič informace, vždy ji doprovází název kategorie.
6. **Každý tool patří v mřížce katalogu právě do jedné kategorie.** Co by patřilo do dvou, se dohledá přes klíčová slova. Karta ve dvou kategoriích by měla dvě barvy. Sekundární vstupy mimo mřížku jsou povolené: Quick Actions na Home, panel Insert v dokumentu (Page numbers), panel Tools v dokumentu a volba dalšího toolu v dialogu výsledku (9.3 bod 12). Vstup nese vždy barvu primární kategorie toolu.
7. **Běžné napřed, odborné nakonec.** V každé kategorii jsou nejdřív tooly pro běžného uživatele, pak podskupina pro pokročilé (prepress, vývojářské nástroje).
8. **Popis na kartě má nejvýš dva řádky karty** a říká výsledek, ne postup.

### Klíčová slova – pravidla

- Píší se anglicky, malými písmeny, oddělená čárkou. Hledání ignoruje velikost písmen a diakritiku.
- Anglická klíčová slova se hledají vždy, i když je aplikace přeložená. Překlad přidává další slova v jazyce rozhraní, takže keywords procházejí přes `tr()`.
- Hledá se podle začátku slova a s tolerancí jednoho překlepu. Pořadí shody: název, klíčová slova, popis.
- Klíčová slova se v UI nezobrazují. Výjimkou je výsledek hledání, kde se ukáže slovo, které rozhodlo o shodě („Merge – odpovídá *combine*“).

---

## 3. Kategorie a barvy

Barvy jsou stávající odstíny z kolekce Color ve Figmě (`accent/<hue>/bg`, `bg-hover`, `bg-pressed`, `border`, `border-strong`, `icon`) a varianta `Hue` komponenty **Tool icon**. Odstín **Neutral** není barva kategorie. Je vyhrazený pro příkazy, panely a nastavení ve výsledcích hledání.

| # | Kategorie | Krátký štítek (chip) | Odstín | Popis kategorie na stránce | Proč tahle barva |
| --- | --- | --- | --- | --- | --- |
| 1 | **Organize Pages** | Pages | **Amber** | Merge, split, reorder and resize pages | Barva papíru a pořadačů. Největší a nejpoužívanější skupina dostává nejteplejší barvu. |
| 2 | **Create & Convert** | Convert | **Blue** | Create PDFs and get content out of them | Neutrální „vstup a výstup“. Modrá je primární barva aplikace, tyhle tooly bývají první krok práce. |
| 3 | **Edit** | Edit | **Green** | Change the content of pages | Nejčastější každodenní práce s dokumentem. Zelená se dobře odliší od modré i červené sousedních kategorií. |
| 4 | **Protect & Sign** | Protect & Sign | **Red** | Protect, sign and remove sensitive data | Zabezpečení a nevratné zásahy. Červená zvyšuje pozornost u redakce a šifrování. |
| 5 | **Optimize** | Optimize | **Violet** | Make files smaller, cleaner and healthier | Technické zásahy do souboru. Fialová je odliší od červené u zabezpečení i od tyrkysové u kontroly. |
| 6 | **Review & Inspect** | Review | **Teal** | Compare, measure, proof and look inside | Analytická práce, při které se dokument většinou nemění. Výjimkou je Measure, který umí uložit měření jako anotace. Klidný studený odstín. |

Pořadí kategorií odpovídá typickému postupu práce: uspořádat, vytvořit nebo převést, upravit, zabezpečit, zmenšit, zkontrolovat.

Kategorie 5 se jmenuje *Optimize*, jak je ve Figmě. Na *Optimize & Repair* se přejmenuje, až bude existovat tool Repair PDF (kapitola 8), protože dnes žádný z jejích toolů nic neopravuje.

Ustálená konvence barev u konkurence neexistuje, viz část 7.3. Společný je jen princip „jedna barva na kategorii“.

### Podskupiny

Podskupiny slouží jen k řazení a k nadpisům uvnitř kategorie. Nemají vlastní barvu ani vlastní chip.

| Kategorie | Podskupiny v tomto pořadí |
| --- | --- |
| Organize Pages | Combine & split · Arrange · Page size |
| Create & Convert | Create · Recognize · Export |
| Edit | bez podskupin |
| Protect & Sign | Encryption · Signatures · Privacy |
| Optimize | File size · Color |
| Review & Inspect | Review · Print production · Developer |

---

## 4. Přehled toolů

Celkem **46 toolů**, které pokrývají dnešní funkce (včetně OCR z větve a funkcí dostupných jen v CLI). Návrhy nových toolů jsou zvlášť v kapitole 8.

### 4.1 Organize Pages · Amber · 12 toolů

| ID | Název | Popis na kartě | Podskupina | Stav |
| --- | --- | --- | --- | --- |
| `pages.merge` | **Merge** | Combine several files into one PDF | Combine & split | GUI |
| `pages.split` | **Split** | Separate a document into several files | Combine & split | GUI |
| `pages.extract` | **Extract Pages** | Save selected pages as a new PDF | Combine & split | GUI·split |
| `pages.interleave` | **Interleave Pages** | Mix front and back sides of a scan | Combine & split | GUI·split |
| `pages.manage` | **Manage Pages** | Reorder, group and arrange pages | Arrange | GUI |
| `pages.insert` | **Insert Pages** | Add PDF pages, images or blank pages | Arrange | GUI·split |
| `pages.add-image` | **Add Image as Page** | Insert a picture as a new page | Arrange | GUI·split |
| `pages.remove` | **Remove Pages** | Delete unwanted pages | Arrange | GUI·split |
| `pages.rotate` | **Rotate Pages** | Turn pages in 90° steps | Arrange | GUI·split |
| `pages.crop` | **Crop Pages** | Trim margins or set a crop area | Page size | GUI |
| `pages.resize` | **Resize Pages** | Change paper size, margins and scale | Page size | GUI |
| `pages.boxes` | **Set Page Boxes** | Media, crop, bleed, trim and art box | Page size | GUI·split |

### 4.2 Create & Convert · Blue · 8 toolů

| ID | Název | Popis na kartě | Podskupina | Stav |
| --- | --- | --- | --- | --- |
| `convert.images-to-pdf` | **Images to PDF** | Turn photos and scans into a PDF | Create | GUI·split |
| `convert.blank` | **Blank PDF** | Start a new document with empty pages | Create | GUI·split |
| `convert.scan` | **Scan to PDF** | Scan paper pages from a scanner | Create | GUI |
| `convert.ocr` | **Recognize Text (OCR)** | Make scanned pages searchable | Recognize | Branch |
| `convert.pdf-to-images` | **PDF to Images** | Save pages as PNG, JPEG or TIFF | Export | GUI |
| `convert.extract-text` | **Extract Text** | Save the document text as a text file | Export | CLI |
| `convert.extract-images` | **Extract Images** | Save embedded pictures to a folder | Export | CLI |
| `convert.audiobook` | **Create Audio Book** | Turn the document text into MP3 | Export | GUI |

### 4.3 Edit · Green · 2 tooly

| ID | Název | Popis na kartě | Podskupina | Stav |
| --- | --- | --- | --- | --- |
| `edit.content` | **Edit Content** | Edit or add text, images and graphics | – | GUI |
| `edit.page-numbers` | **Add Page Numbers** | Number pages in a chosen style | – | GUI |

### 4.4 Protect & Sign · Red · 11 toolů

| ID | Název | Popis na kartě | Podskupina | Stav |
| --- | --- | --- | --- | --- |
| `protect.password` | **Protect with Password** | Require a password to open the PDF | Encryption | GUI·split |
| `protect.permissions` | **Restrict Permissions** | Limit printing, copying and editing | Encryption | GUI·split |
| `protect.certificate-encrypt` | **Encrypt with Certificate** | Only the certificate holder can open it | Encryption | GUI·split |
| `protect.unlock` | **Remove Password** | Remove encryption you have access to | Encryption | GUI·split |
| `protect.sign` | **Sign with Certificate** | Add a digital signature | Signatures | GUI |
| `protect.timestamp` | **Add Timestamp** | Prove the document existed at a time | Signatures | GUI·split |
| `protect.sign-by-hand` | **Sign by Hand** | Draw, type or insert a signature | Signatures | GUI |
| `protect.certificates` | **Manage Certificates** | Create and trust digital IDs | Signatures | GUI |
| `protect.redact` | **Redact Content** | Permanently remove sensitive content | Privacy | GUI |
| `protect.sanitize` | **Remove Hidden Data** | Strip metadata, comments and more | Privacy | GUI |
| `protect.remove-links` | **Remove External Links** | Delete links that leave the document | Privacy | GUI |

### 4.5 Optimize · Violet · 5 toolů

| ID | Název | Popis na kartě | Podskupina | Stav |
| --- | --- | --- | --- | --- |
| `optimize.compress` | **Compress** | Reduce the file size in one step | File size | GUI·split |
| `optimize.images` | **Optimize Images** | Downsample and recompress images | File size | GUI |
| `optimize.structure` | **Optimize Structure** | Clean up and recompress PDF objects | File size | GUI |
| `optimize.bitonal` | **Convert to Black & White** | Make scans 1-bit and much smaller | Color | GUI |
| `optimize.grayscale` | **Convert Images to Grayscale** | Remove color from images | Color | GUI·split |

### 4.6 Review & Inspect · Teal · 8 toolů

| ID | Název | Popis na kartě | Podskupina | Stav |
| --- | --- | --- | --- | --- |
| `review.compare` | **Compare** | Find differences between two PDFs | Review | GUI |
| `review.measure` | **Measure** | Distances, areas, perimeters, angles | Review | GUI |
| `review.output-preview` | **Output Preview** | Separations, overprint, color warnings | Print production | GUI |
| `review.ink-coverage` | **Ink Coverage** | Ink usage of every page | Print production | GUI |
| `review.soft-proof` | **Soft Proofing** | Simulate print colors, check gamut | Print production | GUI |
| `review.statistics` | **Document Statistics** | What takes up space in the file | Developer | GUI |
| `review.object-inspector` | **Object Inspector** | Browse the internal PDF objects | Developer | GUI |
| `review.report` | **Document Report** | Structure, scripts, destinations as a report | Developer | CLI |

### 4.7 Zamčený dokument (jen pro čtení)

Režim jen pro čtení je **zámek dokumentu**. Zamčený dokument má u tabu ikonku zámku a nejde upravit, aby si ho uživatel omylem nepřepsal. Zámek zapíná a vypíná uživatel ikonkou u tabu nebo přepínačem v panelu File. Zámek nahrazuje dnešní samostatný Viewer.

- U zamčeného dokumentu jsou ikonky pro úpravy zakázané (Save, Undo, Redo, Insert a další). Nic dalšího se v rozhraní nemění.
- Oprávnění PDF (heslo vlastníka, *Restrict Permissions*) se nově neřeší, zůstává dnešní chování. Zámek s nimi nesouvisí.
- Když soubor nebo složka nejsou zapisovatelné, dokument se nezamyká. Blokuje se jen Save a aplikace nabídne Save As.
- U podepsaného dokumentu aplikace před uložením změny upozorní na dopad na podpisy a nabídne inkrementální uložení, které dosavadní podpisy zachová.

Pro zamčený dokument jsou dostupné jen tooly, které ho **nemění**. Tool, který zapisuje nový soubor a otevřený dokument nechá beze změny, se počítá jako neměnící.

| Kategorie | Dostupné pro zamčený dokument | Nedostupné |
| --- | --- | --- |
| Organize Pages | Merge, Split, Extract Pages, Interleave Pages | Manage Pages, Insert Pages, Add Image as Page, Remove Pages, Rotate Pages, Crop Pages, Resize Pages, Set Page Boxes |
| Create & Convert | Scan to PDF (jen do nového dokumentu), PDF to Images, Extract Text, Extract Images, Create Audio Book | Recognize Text (OCR) |
| Edit | – | Edit Content, Add Page Numbers |
| Protect & Sign | – | Protect with Password, Restrict Permissions, Encrypt with Certificate, Remove Password, Sign with Certificate, Add Timestamp, Sign by Hand, Redact Content, Remove Hidden Data, Remove External Links |
| Optimize | – | Compress, Optimize Images, Optimize Structure, Convert to Black & White, Convert Images to Grayscale |
| Review & Inspect | Compare, Measure, Output Preview, Ink Coverage, Soft Proofing, Document Statistics, Object Inspector, Document Report | – |

Dostupných je 17 toolů, nedostupných 26. Tři tooly dokument nepotřebují a zámek se jich netýká: Images to PDF, Blank PDF a Manage Certificates.

Poznámky k hraničním případům:

- **Measure** měří a exportuje do CSV. Převod měření na anotace je u zamčeného dokumentu vypnutý.
- **Scan to PDF** nabízí u zamčeného dokumentu jen volbu *New document*. Volba *Add to open document* je vypnutá.
- **Recognize Text (OCR)** je nedostupný jako celek, včetně režimu „jen export textu“.
- **Sign with Certificate** a **Add Timestamp** ukládají podepsaný dokument do nového souboru a otevřený dokument nemění. Podpis je ale z pohledu uživatele úprava dokumentu, proto vyžaduje dokument otevřený pro úpravy.
- **Redact Content** je nedostupný z technického důvodu: značky redakce jsou anotace v otevřeném dokumentu (viz 5.4).
- Nedostupný tool se v katalogu neschovává. Karta ukáže důvod *Document is locked* a nabídne dvě cesty: odemknout dokument, nebo *Edit a Copy*. *Edit a Copy* otevře novou kartu s neuloženou kopií dokumentu, originál zůstane zamčený.

---

## 5. Podrobný popis toolů

U každého toolu je uvedeno, co dělá, s čím pracuje, kde je ta funkce dnes a podle čeho se hledá. Názvy, popisy na kartě a klíčová slova jsou anglicky, protože jde o texty rozhraní.

### 5.1 Organize Pages (Amber)

Všech dvanáct toolů dnes pokrývá aplikace **PageMaster** a dialog **Page Geometry**. V nové aplikaci se dělí podle toho, s čím pracují (E1):

- **Operace nad otevřeným dokumentem** (Rotate, Remove, Insert, Add Image as Page, Extract, Crop, Resize, Page Boxes, Add Page Numbers) běží v kartě dokumentu. Nová karta se kvůli nim neotevírá.
- **Tooly s více vstupy** (Merge, Interleave Pages, Manage Pages) mají vlastní kartu s náhledy stránek. Vlastní kartu má ještě Compare (5.6).
- **Sestava (*Assembly*) existuje jen v Manage Pages.** Jen tam jsou skupiny, kontrolní body, odebrané stránky a ukládání a načítání rozpracovaného stavu. Karta Merge a karta Interleave Pages jsou jen seznam vstupů s náhledem výsledku, nic se z nich neukládá. Split pracuje vždy s jedním otevřeným dokumentem.
- **Tooly na sebe neodkazují.** Žádný tool neobsahuje položku ani odkaz, který by otevřel jiný tool. Zmínka jiného toolu v textu je v pořádku. Jiný tool jde spustit až po dokončení, volbou *Then continue with* v dialogu výsledku (9.3 bod 12). Proto se v sestavě neořezává ani nemění formát stránek: Crop Pages a Resize Pages se použijí na výsledný dokument.

Název *Assembly* odlišuje pracovní plochu PageMasteru od *Workspaces* na stránce Open, což jsou uložené sady otevřených dokumentů (A6).

**Operace v panelu Thumbnails (E2).** Panel Thumbnails v dokumentu umožní vybrat více stránek a nad výběrem nabídne Rotate, Remove, Extract, Insert a přesun, z kontextové nabídky i klávesami (9.7). Navíc má akci *Extract to New Document*: otevře stránku toolu Extract Pages s vybranými stránkami a výsledek otevře jako nový dokument v nové kartě. Původní dokument zůstane beze změny, proto akce funguje i u zamčeného dokumentu. Stránka toolu spuštěného z katalogu (například Rotate Pages) převezme výběr z Thumbnails jako předvolený rozsah stránek (9.6).

#### `pages.merge` – Merge

- **Co dělá:** Sloučí několik PDF a obrázků do jednoho dokumentu v pořadí, které uživatel nastaví.
- **Funkce:** přidání souborů dialogem i přetažením, řazení podle názvu souboru, zdroje, čísla stránky a typu, obrácení pořadí, náhledy stránek. Volba osnovy výsledku: *No Outline*, *Join Outlines*, *Document Parts*. Volitelná optimalizace obrázků ve výstupu. Šablona názvu výstupního souboru, kontrola přepsání existujících souborů a náhled toho, co vznikne. Jako vstup jdou přidat i otevřené taby (9.3). Nepodporované, zašifrované a poškozené vstupy stránka toolu ukáže před spuštěním, ne až po něm. Částečný výsledek (některý vstup se nepodařilo použít) je v souhrnu výslovně označený (G8).
- **Vstup → výstup:** dva a více souborů (PDF, obrázky) → jeden nový PDF.
- **Dnes:** PageMaster, *Make → United Document* (F5), dialog *Assemble Documents*. CLI `unite`.
- **Klíčová slova:** `merge, join, combine, assemble, unite, united document, merge pdf, concatenate, connect, append, bind, glue, stitch, put together, add files, one file, binder, collate`

#### `pages.split` – Split

- **Co dělá:** Rozdělí dokument na víc souborů podle zvoleného pravidla.
- **Funkce:** režimy *Every page*, *Every N pages*, *At selected page numbers*, *At top-level bookmarks*, *By approximate output file size*, *Odd and even pages* (dva soubory). Split nezná skupiny: dělení podle skupin (jeden soubor na skupinu nebo na stránku) je výstup toolu Manage Pages. Šablona názvu se zástupnými znaky `#` (číslo výstupu) a `@` (číslo stránky). Před zápisem se ukáže, kolik souborů vznikne.
- **Vstup → výstup:** jeden otevřený PDF → několik nových PDF ve zvolené složce. Rozdělení více souborů najednou stejným pravidlem patří do dávkového zpracování (8.3) a zatím se nenavrhuje.
- **Dnes:** PageMaster, *Make → Split…*, *Separate to Multiple Documents* (F6). Dělení celého workspace podle pravidla a *Separate to Multiple Documents (Grouped)* (F7) přebírá Manage Pages. CLI `separate`.
- **Klíčová slova:** `split, split pdf, separate, separate to multiple documents, regroup, divide, break apart, disassemble, unbind, burst, cut, chapters, by bookmarks, by outline, by size, every page, single pages, even odd, page ranges, parts, chunk`

#### `pages.extract` – Extract Pages

- **Co dělá:** Uloží vybrané stránky jako nový dokument a původní nechá beze změny.
- **Funkce:** výběr stránek společnou komponentou (9.6): myší v panelu Thumbnails, rozsahem (`1-3, 8, 10-12`), sudé, liché, na výšku, na šířku. Výstup jako jeden soubor nebo každá stránka zvlášť. Ze Thumbnails se spouští akcí *Extract to New Document* a výsledek se otevře v nové kartě.
- **Vstup → výstup:** otevřený dokument a výběr stránek → nový PDF.
- **Dnes:** v PageMasteru jen nepřímo: vybrat, ostatní odebrat a spustit *United Document*. Samostatná akce chybí.
- **Klíčová slova:** `extract pages, export pages, save pages as, pull out, page range, subset, copy pages, selected pages, take out, pick pages`
- **Poznámka:** Uživatelé „extract“ hledají odděleně od „split“. Konkurence to má jako samostatný tool.

#### `pages.interleave` – Interleave Pages

- **Co dělá:** Prolne stránky dvou dokumentů nastřídačku. Typické použití je sken lichých a sudých stran na jednostranném skeneru.
- **Funkce:** střídání stránek ze dvou zdrojů, varianta s obráceným pořadím druhého zdroje (zadní strany skenované od konce). Stránka toolu ukáže náhled prvních stran výsledku. Když mají zdroje různý počet stran, vysvětlí, co se stane se stranami navíc (H7).
- **Vstup → výstup:** dva PDF (přední a zadní strany) → jeden PDF.
- **Dnes:** PageMaster, *Regroup → Regroup by Alternating Pages* a *Regroup by Alternating Pages (Reversed Order)*, pak *United Document*.
- **Klíčová slova:** `interleave, regroup by alternating pages, alternate, mix, alternate mix, collate, zip, zipper, duplex scan, double sided, front and back, odd even merge, shuffle`

#### `pages.manage` – Manage Pages

- **Co dělá:** Sestava (*Assembly*) s náhledy, na které se stránky z jednoho nebo více zdrojů přeskupují, seskupují a připravují pro výstup. U jediného otevřeného dokumentu stačí panel Thumbnails (viz úvod 5.1).
- **Funkce:** přesun přetažením i bez tažení (9.7), vyjmout, kopírovat a vložit, klonování výběru, seskupení a zrušení skupiny, přejmenování položky nebo skupiny, vlastnosti položky (zdroj, původní číslo stránky, rozměr, orientace, rotace). Řazení podle názvu souboru, zdroje, čísla stránky a typu, obrácení pořadí. Výběr: vše, nic, rozsah, sudé, liché, na výšku, na šířku, viditelné, invertovat. Hledání v sestavě (včetně slov *grouped*, *portrait*, *landscape*). Zvětšení náhledů, zobrazení názvu dokumentu u položek, zobrazení Details. Zpět a znovu s popisem kroku. Pojmenované kontrolní body (checkpoint), obnovení odebraných položek.
- **Skupiny a výstup:** nabídka *Regroup* vytvoří skupiny automaticky: sudé a liché stránky, dvojice, podle záložek, každých N stran (*Every N pages…*), na zadaných číslech stran (*At page numbers…*), střídání dvou souborů, obrácené pořadí. Nabídka *Output* určuje, co vznikne: *One PDF*, *One PDF per group or item*, *One PDF per page*. Výstup po skupinách nahrazuje dělení celého workspace podle pravidla z PageMasteru, Manage Pages kvůli němu neodkazuje na Split. Dělení sestavy podle velikosti souboru se nenabízí. Výstup do více souborů má složku, šablonu názvu (`#` číslo výstupu, `@` číslo stránky, `%` index vstupu, `{group_name}` a další) a volbu pro existující soubory. Ořez a formát stránek se v sestavě nenastavují, použije se Crop Pages a Resize Pages na výsledný dokument.
- **Ukládání a načítání sestavy:** *Open Assembly…* otevře uloženou sestavu (nabídka u tlačítka uložení, úvodní stav toolu se seznamem posledních sestav, stránka Open). *Save Assembly* (Ctrl+S) uloží rozpracovanou sestavu, ne PDF. PDF vznikne až tlačítkem *Create PDF*, které je jasně odlišené od uložení sestavy. Když při otevření uložené sestavy chybí nebo se změnil některý zdroj, aplikace nabídne jeho nahrazení jiným souborem. Soubory `.pagemaster` z dnešního PageMasteru se neotevírají, zpětná kompatibilita se neřeší.
- **Vstup → výstup:** jeden nebo víc souborů → upravený dokument, nebo nové soubory přes Merge a Split.
- **Dnes:** PageMaster, hlavní okno.
- **Klíčová slova:** `organize, manage, arrange, reorder, rearrange, resequence, move pages, sort, page order, thumbnails, group, duplicate, clone, reverse order, drag and drop, page sorter, reorganize pages, assembly, pagemaster, workspace`

#### `pages.insert` – Insert Pages

- **Co dělá:** Přidá do dokumentu stránky z jiného PDF, obrázky nebo prázdné stránky.
- **Funkce:** *Insert PDF* (celý dokument), *Insert PDF Pages* (rozsah jako `1-5, 7, 10-, odd, even` s náhledem výběru), *Insert Image*, *Insert Empty Page*, *Replace Selection* (nahradí vybrané stránky). Soubory jde vložit i přetažením na konkrétní místo. Jako zdroj jdou vybrat i otevřené taby (9.3). Nepodporované, zašifrované a poškozené soubory stránka toolu ukáže před spuštěním, částečný výsledek je v souhrnu výslovně označený (G8).
- **Vstup → výstup:** otevřený dokument a další soubory → upravený dokument.
- **Dnes:** PageMaster, nabídka *Insert* a *Edit → Replace Selection*.
- **Klíčová slova:** `insert, insert pdf, insert pdf pages, insert empty page, add pages, append, import pages, blank page, empty page, replace pages, replace selection, paste pages, attach pages, add pdf`

#### `pages.add-image` – Add Image as Page

- **Co dělá:** Vloží obrázek jako novou stránku na zvolené místo v dokumentu.
- **Funkce:** předvolba Insert Pages s režimem *Insert Image*: výběr jednoho nebo více obrázků, místo vložení (před nebo za stránku, na začátek, na konec), formát nové stránky. Popis na kartě musí jasně říct, že vzniká nová stránka v otevřeném dokumentu. Tím se tool liší od *Images to PDF* (nový dokument) a od vložení obrázku na existující stránku přes *Edit Content*. Ve výsledcích hledání je rozliší název a popis.
- **Vstup → výstup:** otevřený dokument a obrázky → upravený dokument.
- **Dnes:** PageMaster, *Insert → Insert Image*.
- **Klíčová slova:** `add image, image as page, insert image, picture page, photo page, scan page, add picture, jpg, png`

#### `pages.remove` – Remove Pages

- **Co dělá:** Odstraní vybrané stránky z dokumentu.
- **Funkce:** odebrání výběru (Del), výběr rozsahem nebo filtrem (sudé, liché, na výšku, na šířku), obnovení odebraných stránek.
- **Vstup → výstup:** otevřený dokument a výběr stránek → upravený dokument.
- **Dnes:** PageMaster, *Edit → Remove Selection* a *Restore Removed Items*.
- **Klíčová slova:** `remove, delete, discard, drop pages, erase page, cut out, get rid of, trash, delete pages, remove selection`

#### `pages.rotate` – Rotate Pages

- **Co dělá:** Trvale otočí vybrané stránky po 90°.
- **Funkce:** otočení vlevo a vpravo, *Reset Rotation*, použití na výběr (všechny, sudé, liché, jen na šířku).
- **Vstup → výstup:** otevřený dokument a výběr stránek → upravený dokument.
- **Dnes:** PageMaster, *Edit → Rotate Left / Rotate Right / Reset Rotation*. V Editoru a Vieweru *View → Rotate* otáčí jen pohled a do souboru se neukládá.
- **Klíčová slova:** `rotate, turn, orientation, landscape, portrait, upside down, sideways, 90, 180, clockwise, counterclockwise, fix orientation, reset rotation`
- **Poznámka:** Rozdíl mezi otočením pohledu a otočením stránky musí být v UI zřejmý. Viz kapitola 9.

#### `pages.crop` – Crop Pages

- **Co dělá:** Ořízne stránky, typicky bílé okraje. U PDF stránek nastaví ořezový rámec (CropBox): obsah mimo ořez se jen nezobrazuje a v souboru zůstává. Pro skutečné odstranění obsahu je tool Redact Content, stránka toolu na to upozorní.
- **Funkce:** *Crop margins equally* (stejný okraj ze všech stran), *Set crop box manually* (levý, horní, šířka, výška), referenční rozměr stránky, volba, na které stránky se ořez použije, zobrazení výsledného rozměru. PDF stránky se ořezávají nastavením CropBoxu ve výstupu, obrázky před vložením do stránky.
- **Vstup → výstup:** otevřený dokument a výběr stránek → upravený dokument.
- **Dnes:** PageMaster, *Edit → Crop Pages…*
- **Klíčová slova:** `crop, trim, cut margins, crop box, remove white margins, clip, shrink margins, cut edges, visible area`

#### `pages.resize` – Resize Pages

- **Co dělá:** Změní formát stránek, okraje a umístění obsahu.
- **Funkce:** cílový formát (A5, A4, A3, Letter, Legal, vlastní v mm), okraje, kotva umístění obsahu (devět poloh), posun X a Y, *Scale content*, *Preserve aspect ratio*, *Scale comments and form fields*. Výběr stránek rozsahem a podmnožinou (všechny, liché, sudé, na výšku, na šířku).
- **Vstup → výstup:** otevřený dokument → upravený dokument.
- **Dnes:** Editor *Edit → Page Geometry…*, PageMaster *Edit → Page Geometry…*
- **Klíčová slova:** `resize, page size, paper size, scale, a4, a3, a5, letter, legal, margins, fit to page, enlarge, shrink, change format, page geometry, add margins`

#### `pages.boxes` – Set Page Boxes

- **Co dělá:** Nastaví rámce stránky pro tisk bez zásahu do obsahu.
- **Funkce:** referenční rámec (Media, Crop, Bleed, Trim, Art) a volba, do kterých rámců se výsledek zapíše. Při vypnutém škálování obsahu je úprava nedestruktivní.
- **Vstup → výstup:** otevřený dokument → upravený dokument.
- **Dnes:** stejný dialog *Page Geometry* jako u `pages.resize`. CLI `info-page-boxes` rámce jen vypisuje.
- **Klíčová slova:** `page boxes, media box, crop box, bleed box, trim box, art box, bleed, prepress, mediabox, cropbox, trimbox`
- **Poznámka:** Pokročilý tool pro tiskovou přípravu, v kategorii je řazený poslední.

### 5.2 Create & Convert (Blue)

#### `convert.images-to-pdf` – Images to PDF

- **Co dělá:** Vytvoří PDF z obrázků, jeden obrázek na stránku.
- **Funkce:** výběr více obrázků nebo přetažení, řazení, otočení, ořez obrázku před vložením, formát stránky přes Page Geometry, volitelná optimalizace obrázků ve výstupu. Podporované jsou rastrové formáty, které umí Qt.
- **Vstup → výstup:** obrázky → nový PDF.
- **Dnes:** PageMaster, *Insert → Insert Image* a *United Document*.
- **Klíčová slova:** `images to pdf, jpg to pdf, jpeg to pdf, png to pdf, tiff to pdf, bmp, photo to pdf, picture, convert image, create pdf, pdf from images, photos`

#### `convert.blank` – Blank PDF

- **Co dělá:** Založí nový dokument s prázdnými stránkami.
- **Funkce:** počet stránek, formát a orientace, pak pokračování v Manage Pages nebo Edit Content.
- **Vstup → výstup:** nic → nový PDF.
- **Dnes:** PageMaster, *Insert → Insert Empty Page* a *United Document*.
- **Klíčová slova:** `blank pdf, new pdf, empty document, new document, create pdf, empty page, insert empty page, from scratch, white page, start new`

#### `convert.scan` – Scan to PDF

- **Co dělá:** Naskenuje papírové stránky do nového PDF, nebo je připojí na konec otevřeného dokumentu.
- **Funkce:** volba cíle *New document* nebo *Add to open document* (jen s otevřeným a nezamčeným dokumentem). Výběr zařízení a zdroje, barevný režim *Color*, *Grayscale*, *Lineart*, rozlišení v DPI, počet stránek, opětovné načtení seznamu zařízení. Backend WIA na Windows a SANE na Linuxu. S backendem WIA se skener a zdroj papíru volí v systémovém dialogu Windows, který se otevře pro každou stránku. Stránka toolu to říká předem a nabídku *Source* má vypnutou (`convert-scan-wia`).
- **Vstup → výstup:** skener → nový PDF, nebo nové stránky na konci otevřeného dokumentu.
- **Dnes:** plugin Scanner, *Scan Pages…* Bez otevřeného dokumentu sestaví nový dokument, s otevřeným dokumentem připojí stránky na konec.
- **Klíčová slova:** `scan, scan pages, scanner, acquire, twain, wia, sane, paper, digitize, document feeder, adf, flatbed, scan to pdf`

#### `convert.ocr` – Recognize Text (OCR)

- **Co dělá:** Rozpozná text ve skenovaných stránkách a přidá neviditelnou textovou vrstvu, takže dokument jde prohledávat a text kopírovat.
- **Funkce:** výběr stránek (všechny, aktuální, viditelné, rozsah, sudé a liché), engine Tesseract 5, kvalita modelu *Fast*, *Standard*, *Quality*, víc jazyků v zadaném pořadí, rozvržení stránky, zacházení s existujícím textem, pojmenované profily nastavení. Příprava obrazu: 150 až 1200 DPI, rotace, automatická orientace, narovnání, převod do šedé, odstranění šumu, inverze, detekce prázdných stránek, oblasti k rozpoznání. Rozpoznání běží na pozadí a dá se zastavit. Kontrola výsledku: strom bloků, řádků a slov svázaný s obrazem, jistota slov, práh pro kontrolu, opravy textu, hledání a nahrazování. Zápis do aktuálního dokumentu, do kopie, nebo jen export. Export prostého textu (TXT, UTF-8). Projekt OCR pro pozdější pokračování. Odstranění vlastní vrstvy. Správa jazyků: vestavěné modely, stahování dalších, import vlastního modelu.
- **Vstup → výstup:** otevřený dokument → dokument s textovou vrstvou, nebo textový soubor.
- **Dnes:** větev `issue85`, *Tools → Recognize Text (OCR)* a *Manage OCR Languages*. Jen Editor.
- **Klíčová slova:** `ocr, recognize text, text recognition, searchable pdf, scanned, make searchable, image to text, tesseract, read text from image, selectable text, text layer, digitize`

#### `convert.pdf-to-images` – PDF to Images

- **Co dělá:** Vykreslí stránky do obrázkových souborů.
- **Funkce:** všechny stránky nebo rozsah, šablona názvu a cílová složka, rozlišení v DPI nebo v pixelech, formát obrázku podle toho, co umí Qt, podtyp, gamma, kvalita, komprese, *Optimized write*, *Progressive scan write*.
- **Vstup → výstup:** otevřený dokument → obrázky ve složce.
- **Dnes:** Editor, *File → Render to Images…* CLI `render`.
- **Klíčová slova:** `pdf to images, pdf to jpg, pdf to jpeg, pdf to png, pdf to tiff, render, render to images, rasterize, export images, convert to image, picture, page as image, thumbnails, bitmap`

#### `convert.extract-text` – Extract Text

- **Co dělá:** Uloží text dokumentu do textového souboru.
- **Funkce:** výběr stránek, výstup jako prostý text. V GUI dnes jde text jen vybrat a zkopírovat ručně (*Select All*, *Copy text*).
- **Vstup → výstup:** otevřený dokument → soubor TXT nebo schránka.
- **Dnes:** CLI `fetch-text`. V GUI jen výběr a kopírování textu. Větev OCR přidává export TXT rozpoznaného textu.
- **Klíčová slova:** `extract text, pdf to text, pdf to txt, export text, copy all text, copy text, select all, fetch text, plain text, get text, text file, content`

#### `convert.extract-images` – Extract Images

- **Co dělá:** Uloží obrázky vložené v dokumentu jako soubory.
- **Funkce:** výběr stránek, cílová složka, šablona názvu. V GUI dnes existuje jen režim myši *Extract Image*, který zkopíruje jeden kliknutý obrázek do schránky.
- **Vstup → výstup:** otevřený dokument → obrázky ve složce.
- **Dnes:** CLI `fetch-images`. Editor *Tools → Extract Image* pro jeden obrázek.
- **Klíčová slova:** `extract images, extract image, fetch images, save pictures, export embedded images, photos, get images, pull images, image extraction, original images, figures`

#### `convert.audiobook` – Create Audio Book

- **Co dělá:** Převede text dokumentu na zvukový soubor.
- **Funkce:** vytvoření textového proudu z dokumentu, výběr položek obdélníkem, podle obsaženého textu, regulárním výrazem a seznamem stránek, aktivace a deaktivace položek (záhlaví, čísla stránek), úprava textu a obnovení původního, změna pořadí, synchronizace výběru mezi tabulkou a stránkou. Výstup do MP3 zvoleným hlasem.
- **Vstup → výstup:** otevřený dokument → soubor MP3.
- **Dnes:** plugin AudioBook. CLI `audio-book` a `audio-book-voices`.
- **Klíčová slova:** `audiobook, audio book, create audio book, create text stream, mp3, text to speech, tts, narrate, listen, voice, export audio, speech, read to me`

### 5.3 Edit (Green)

Kategorie obsahuje jen úpravy, které mají vlastní režim nebo dialog. **Anotace** (poznámky, zvýraznění, tvary, textová pole, razítka a odkazy) se přidávají přímo v dokumentu položkou Annotate a v katalogu nejsou. Stejně tak **vyplňování formulářů** (kliknutí do pole), **osnova** (panel Outline) a **přílohy** (panel Attachments). Návrhy dalších toolů této kategorie jsou v kapitole 8.

#### `edit.content` – Edit Content

- **Co dělá:** Upraví existující obsah stránky (text, obrázky, vektorové cesty a stínování) a přidá nový text, obrázky a tvary jako trvalou součást stránky.
- **Funkce:** výběr prvku na stránce, přesun, změna velikosti, smazání. Úprava textu, náhrada obrázku (*Load Image*), pero a výplň (styl, šířka, barva), transformace (posun, měřítko, rotace, zkosení). Přidání nových prvků: textový štítek, křivka od ruky, značka souhlasu a nesouhlasu, obdélník, zaoblený obdélník, vodorovná, svislá a obecná čára, tečka, obrázek (SVG i rastr). Režim *Create Multiple Elements* pro opakované vkládání stejně velkých prvků. Zarovnání (nahoru, na střed, dolů, vlevo, vpravo), stejná šířka, výška a velikost, vystředění na stránku, rozložení do řady, sloupce, formuláře a mřížky. Zpět a znovu, *Clear All Graphics*. Změny se do dokumentu zapíšou až po potvrzení. U skenovaných stránek stránka toolu krátce upozorní, že OCR přidá neviditelný text, ale obraz stránky se tím upravit nedá (H8).
- **Vstup → výstup:** otevřený dokument → upravený dokument.
- **Dnes:** plugin Editor, *Edit page content* a panel *Editor Toolbox*.
- **Klíčová slova:** `edit pdf, edit page content, editor toolbox, edit text, change text, modify, fix typo, correct, replace image, move object, delete object, edit content, vector graphics, add text, type on pdf, write on pdf, add image, insert picture, place logo, svg`

#### `edit.page-numbers` – Add Page Numbers

- **Co dělá:** Očísluje stránky textem ve zvoleném stylu.
- **Funkce:** styl číslování (arabské, římské velké a malé, písmena velká a malá), formát (*1*, *1 / N*, *Page 1*, *Page 1 of N* nebo vlastní), počáteční číslo, písmo, barva, zarovnání vlevo, na střed, vpravo, rozsah stránek (všechny, sudé, liché, vlastní, viditelné), náhled. Oblast pro číslo se na stránce určí myší.
- **Vstup → výstup:** otevřený dokument → upravený dokument.
- **Dnes:** Editor, *Insert → Insert Page Numbers…*
- **Klíčová slova:** `page numbers, insert page numbers, numbering, number pages, pagination, page x of y, roman numerals, footer number, folio`

### 5.4 Protect & Sign (Red)

Dnešní dialog *Encryption Settings* řeší čtyři různé úkoly. V katalogu jsou to čtyři tooly, které otevřou stejnou obrazovku s jinou předvolbou.

#### `protect.password` – Protect with Password

- **Co dělá:** Zašifruje dokument tak, že jde otevřít jen s heslem.
- **Funkce:** algoritmus *AES 256-bit* (doporučený), *AES 128-bit*, *RC4 128-bit* pro zpětnou kompatibilitu. Heslo pro otevření, ukazatel síly hesla. Rozsah šifrování: celý dokument včetně metadat, vše kromě metadat, jen přílohy.
- **Vstup → výstup:** otevřený dokument → zašifrovaný dokument.
- **Dnes:** Editor, *Edit → Encryption…* CLI `encrypt`.
- **Klíčová slova:** `password, protect, lock, secure, encrypt, encryption, encryption settings, encrypt pdf, aes, aes-256, open password, user password, password protect`

#### `protect.permissions` – Restrict Permissions

- **Co dělá:** Omezí, co smí čtenář s dokumentem dělat.
- **Funkce:** heslo vlastníka a oprávnění: tisk v nízkém a vysokém rozlišení, vyplňování formulářů, přístupnost, změna obsahu, skládání dokumentu (vkládání, otáčení, mazání stránek), úprava interaktivních prvků, kopírování obsahu.
- **Vstup → výstup:** otevřený dokument → zašifrovaný dokument s omezeními.
- **Dnes:** Editor, *Edit → Encryption…*, část *Permissions*.
- **Klíčová slova:** `permissions, restrict, encryption, encrypt pdf, prevent printing, prevent copying, disable copy, no editing, owner password, read only, lock editing, rights`

#### `protect.certificate-encrypt` – Encrypt with Certificate

- **Co dělá:** Zašifruje dokument certifikátem, takže ho otevře jen držitel soukromého klíče.
- **Funkce:** výběr certifikátu se soukromým klíčem ze správce certifikátů.
- **Vstup → výstup:** otevřený dokument → zašifrovaný dokument.
- **Dnes:** Editor, *Edit → Encryption…*, algoritmus *Certificate Encryption*.
- **Klíčová slova:** `certificate encryption, encryption, encrypt pdf, public key, pki, recipient, private key, digital id, encrypt for, certificate security`

#### `protect.unlock` – Remove Password

- **Co dělá:** Odstraní šifrování z dokumentu, ke kterému má uživatel plný přístup.
- **Funkce:** vyžaduje oprávnění vlastníka, jinak si vyžádá znovu heslo. Neprolamuje neznámá hesla.
- **Vstup → výstup:** zašifrovaný dokument a heslo → nezašifrovaný dokument.
- **Dnes:** Editor, *Edit → Encryption…*, algoritmus *None*. CLI `decrypt`.
- **Klíčová slova:** `remove password, unlock, decrypt, encryption, remove security, remove restrictions, remove encryption, unprotect, open locked`

#### `protect.sign` – Sign with Certificate

- **Co dělá:** Podepíše dokument digitálním podpisem.
- **Funkce:** viditelný nebo neviditelný podpis, typ *Signature* nebo *Signature with timestamp*, certifikát a jeho heslo. Odborné parametry (adresa časové autority, důvod podpisu, kontakt) jsou na stránce toolu v části *Details* (H9). Vzhled viditelného podpisu se nakreslí nástroji panelu *Signature Toolbox* (text, křivka od ruky, značky, tvary, obrázek). Podepsaný dokument se uloží jako nový soubor. Potvrzovací tlačítko je *Save Signed Copy*.
- **Bez certifikátu:** když uživatel žádný certifikát nemá, otevře se nejdřív správce certifikátů (Manage Certificates) a po jeho zavření dialog podpisu. Tak to funguje i dnes.
- **Vstup → výstup:** otevřený dokument a certifikát → podepsaný PDF.
- **Dnes:** plugin Signature, *Sign Digitally With Certificate*.
- **Klíčová slova:** `sign, sign document, sign digitally with certificate, signature, digital signature, certificate, pkcs12, pfx, p12, pades, digital id, electronic signature, esign, approve, qualified signature`

#### `protect.timestamp` – Add Timestamp

- **Co dělá:** Přidá dokumentové časové razítko, které dokládá existenci dokumentu v daném čase.
- **Funkce:** volba *Document timestamp only*, adresa časové autority (RFC 3161).
- **Vstup → výstup:** otevřený dokument → dokument s časovým razítkem.
- **Dnes:** plugin Signature, dialog podpisu, typ *Document timestamp only*.
- **Klíčová slova:** `timestamp, time stamp, sign document, tsa, rfc 3161, trusted time, document timestamp, proof of existence, long term`

#### `protect.sign-by-hand` – Sign by Hand

- **Co dělá:** Umístí na stránku vlastnoruční podpis nebo značku. Nejde o kryptografický podpis.
- **Funkce:** text, křivka od ruky, značka souhlasu a nesouhlasu, tvary, čáry, obrázek. Po potvrzení (*Sign Electronically*) se grafika stane součástí stránky. Stránka toolu má jednu větu: „Not a digital ID signature – use Sign with Certificate.“
- **Vstup → výstup:** otevřený dokument → upravený dokument.
- **Dnes:** plugin Signature, *Activate signature creator* a *Sign Electronically*.
- **Klíčová slova:** `draw signature, sign electronically, signature creator, sign document, handwritten, sign by hand, fill and sign, fill & sign, initials, e-sign, electronic signature, autograph, signature image, tick, cross`

#### `protect.certificates` – Manage Certificates

- **Co dělá:** Spravuje osobní certifikáty pro podpis a šifrování.
- **Funkce:** seznam certifikátů, vytvoření certifikátu podepsaného sebou samým (jméno, organizace, útvar, e-mail, země, délka klíče, platnost, soubor), seznam důvěryhodných certifikátů a jeho úprava.
- **Vstup → výstup:** nic → úložiště certifikátů.
- **Dnes:** Editor a Viewer, *Tools → Certificates…*, plugin Signature *Certificates Manager*. CLI `cert-store`, `cert-store-install`.
- **Klíčová slova:** `certificates, certificates manager, digital id, self-signed, create certificate, trusted certificates, keystore, pfx, p12, key, certificate store`

#### `protect.redact` – Redact Content

- **Co dělá:** Trvale odstraní citlivý obsah. Odstraněný obsah ve výsledném souboru fyzicky nezůstane.
- **Dva kroky:** (1) Označení. Značky redakce jsou anotace Redact v otevřeném dokumentu, obsah pod nimi je stále v souboru. (2) Vytvoření redigované kopie. Výsledek se zapíše do nového souboru, otevřený dokument se značkami zůstane beze změny. Dnes se výsledek neotevře. Nově ho souhrn nabídne otevřít v nové kartě, která se stane aktivní (9.3).
- **Funkce:** označení obdélníkem (*Redact Rectangle*), tažením přes text (*Redact Text*), z výsledků pokročilého hledání včetně regulárních výrazů (*Redact Text Selection*), celých stránek (*Redact Page(s)*). Vytvoření redigovaného dokumentu: výstupní soubor, barva výplně, volitelné převzetí názvu, metadat a osnovy.
- **Stránka toolu (H4):** stav „N areas marked – not removed yet“, dokud kopie nevznikne. Primární tlačítko je *Create Redacted Copy*. Při hledání „black out“ nebo „redact“ se jako první výsledek nesmí ukázat obdélník z panelu Insert, ale tento tool.
- **Vstup → výstup:** otevřený dokument → nový redigovaný PDF.
- **Dnes:** plugin Redact, *Create Redacted Document*. CLI `redact`.
- **Klíčová slova:** `redact, create redacted document, redact rectangle, redact text, redact page, black out, blackout, censor, remove sensitive, hide text, gdpr, anonymize, obscure, cover up, personal data, erase text`

#### `protect.sanitize` – Remove Hidden Data

- **Co dělá:** Odstraní z dokumentu údaje, které nejsou na první pohled vidět.
- **Funkce:** volby: informace o dokumentu, všechna metadata, osnova, přílohy, vložený vyhledávací index, komentáře a ostatní anotace, náhledy stránek, popisky stránek, neviditelný text (vrstva OCR). Protokol s velikostí před a po.
- **Rychlé akce (H10):** tlačítka s předvolbami, například *Before sending* a *Before publishing*. Tlačítko jen nastaví zaškrtávací volby podle předvolby, nic samo nespustí. Uživatel volby může dál upravit a potvrdit.
- **Vstup → výstup:** otevřený dokument → upravený dokument.
- **Dnes:** Editor, *Edit → Sanitize…*
- **Klíčová slova:** `sanitize, sanitise, remove hidden data, remove hidden information, hidden data, remove metadata, strip metadata, clean, privacy, remove comments, remove author, scrub, remove ocr text, document info, exif`

#### `protect.remove-links` – Remove External Links

- **Co dělá:** Smaže z dokumentu odkazy vedoucí ven.
- **Funkce:** stránka toolu zobrazí seznam odstraňovaných odkazů se stránkou, na které jsou, a umožní jednotlivé odkazy vynechat. Odkazy se odstraní až po potvrzení (C5). Souhrn s počtem odstraněných odkazů. Dnes jde o jeden krok bez nastavení a bez dialogu.
- **Vstup → výstup:** otevřený dokument → upravený dokument.
- **Dnes:** Editor, *Edit → Remove External Links*. CLI `remove-external-links`.
- **Klíčová slova:** `remove links, external links, url, hyperlinks, web links, strip links, phishing, tracking links, disable links`

### 5.5 Optimize (Violet)

#### `optimize.compress` – Compress

- **Co dělá:** Zmenší soubor jedním krokem bez nutnosti rozumět kompresi.
- **Funkce:** předvolby (například *Smaller file*, *Balanced*, *Best quality*), které spustí optimalizaci obrázků a optimalizaci struktury dohromady. Stránka toolu ukáže skutečně naměřenou výslednou velikost a úsporu v procentech. Když se soubor zvolenými předvolbami zmenšit nedá, řekne to: „File cannot be reduced with these settings“. Rozpad velikosti podle obrázků, písem, obsahu stránek a struktury ukazuje přímo pravý panel (H5), tlačítko „What takes up space?“ ani odkaz na Document Statistics na stránce nejsou. Compress je záměrně jednoduchý: sdružuje `optimize.images` a `optimize.structure` a neodkazuje na ně, protože přechod do jiného toolu by rozpracované nastavení zahodil. Podrobné nastavení má jen předvolba *Custom…* (rozlišení, kvalita JPEG, cíl, struktura ano/ne); kdo potřebuje víc, spustí oba tooly z katalogu.
- **Vstup → výstup:** otevřený dokument → menší dokument.
- **Dnes:** samostatně neexistuje. Skládá se z *Optimize…* a *Optimize Images…* v Editoru. CLI `optimize`.
- **Klíčová slova:** `compress, compress pdf, reduce size, shrink, smaller, make smaller, downsize, optimize, minimize, file size, email size, mb, too large, lighten, reduce`
- **Poznámka:** Nejhledanější tool této kategorie. Dnešní dva odborné dialogy běžný uživatel pod slovem „compress“ nenajde.

#### `optimize.images` – Optimize Images

- **Co dělá:** Převzorkuje a znovu zkomprimuje obrázky v dokumentu s plnou kontrolou nad výsledkem.
- **Funkce:** režim *Auto* nebo *Custom*, barevný režim (*Auto*, *Preserve*, *Color (RGB)*, *Grayscale*, *Bitonal*), cíl (*Prefer quality*, *Minimum size*), *Keep original if larger*, *Preserve transparency*. Zvlášť profily pro barevné, šedé a bitonální obrázky: algoritmus (*Auto*, *Flate*, *JPEG*, *JPEG2000*, *RunLength*), cílové DPI, filtr převzorkování (*Nearest*, *Bilinear*, *Bicubic*, *Lanczos*), kvalita JPEG, poměr JPEG2000, PNG prediktor, práh pro bitonální převod. Seznam obrázků s výběrem, zapnutím komprese a vlastním nastavením pro jednotlivé obrázky. Náhled před a po a souhrn úspory.
- **Vstup → výstup:** otevřený dokument → dokument s menšími obrázky.
- **Dnes:** Editor, *Edit → Optimize Images…* PageMaster, volba *Optimize images in output PDFs* a *Image Optimization Settings…*
- **Klíčová slova:** `optimize images, image optimization settings, downsample, resample, recompress, jpeg quality, dpi, image compression, jpeg2000, reduce image size, resolution, 150 dpi`

#### `optimize.structure` – Optimize Structure

- **Co dělá:** Zmenší soubor úklidem vnitřní struktury bez změny vzhledu.
- **Funkce:** vložení jednoduchých objektů namísto odkazů, odstranění prázdných objektů ze slovníků, odstranění nepoužitých objektů, sloučení shodných objektů, zhuštění úložiště objektů, rekomprese proudů Flate maximální kompresí. Protokol s časem, velikostí před a po a kompresním poměrem.
- **Vstup → výstup:** otevřený dokument → menší dokument.
- **Dnes:** Editor, *Edit → Optimize…* CLI `optimize`.
- **Klíčová slova:** `optimize structure, optimize, clean up, remove unused objects, merge identical objects, recompress, flate, garbage collect, lossless, object streams`
- **Poznámka:** Pokročilý tool, bezeztrátový.

#### `optimize.bitonal` – Convert to Black & White

- **Co dělá:** Převede obrázky nebo celé stránky na jednobitové černobílé. U skenů textu to dává výrazně menší soubor.
- **Funkce:** převod jen obrázků, nebo celých stránek. Metoda: automatická (Otsu), ruční práh 0 až 255, adaptivní prahování, rozptyl Floyd–Steinberg. Rozlišení v DPI. Komprese: automaticky nejmenší výsledek, *Flate*, *RunLength*, *CCITT Group 4*, *JBIG2*. Pro každou položku režim: převést, převést inverzně, ponechat, vyplnit černou, vyplnit bílou. *Detect Blank Pages* s nabídkou nahradit prázdné stránky bílou výplní. Náhled originálu a výsledku vedle sebe.
- **Vstup → výstup:** otevřený dokument → černobílý dokument.
- **Dnes:** Editor, *Edit → Create Bitonal Document…* CLI `bitonal`.
- **Klíčová slova:** `black and white, bitonal, create bitonal document, bitonal images, monochrome, 1-bit, b&w, bw, fax, jbig2, ccitt, threshold, scan cleanup, two colors, lineart`

#### `optimize.grayscale` – Convert Images to Grayscale

- **Co dělá:** Převede barevné obrázky v dokumentu do odstínů šedi.
- **Funkce:** předvolba `optimize.images` s barevným režimem *Grayscale*.
- **Vstup → výstup:** otevřený dokument → dokument s šedými obrázky.
- **Dnes:** Editor, *Edit → Optimize Images…*, *Color mode → Grayscale*.
- **Klíčová slova:** `grayscale, greyscale, convert to grayscale, gray, grey, remove color, desaturate, black and white photo, colorless, save ink`
- **Poznámka:** Převádějí se jen obrázky. Barevný text a vektorová grafika zůstávají barevné, proto má tool v názvu slovo *Images*. Až vznikne Convert Colors (kapitola 8), který převede celý obsah, název se znovu posoudí. Odlišení od barevného režimu *Grayscale* v panelu View, který dokument nemění, viz 9.4.

### 5.6 Review & Inspect (Teal)

#### `review.compare` – Compare

- **Co dělá:** Najde a ukáže rozdíly mezi dvěma dokumenty.
- **Vstupy (H6):** dokumenty se jmenují *Original* a *Revised*, ne levý a pravý. Jdou prohodit a vybrat i z otevřených tabů (9.3). Stránka rozlišuje stavy *not compared yet*, *no differences* a *error*. Compare má vlastní kartu (5.1, E1).
- **Funkce:** výběr stránek u každého dokumentu. Volby: porovnat text jako vektorovou grafiku, porovnávat znaky místo slov. Filtry rozdílů: text, vektorová grafika, obrázky, stínování, přesun stránek. Pohledy: rozdíly vedle sebe, jen Original, jen Revised, překryv s plynulým prolnutím, s přizpůsobením velikosti stránek, měřítkem každé strany a posunem. Seznam rozdílů, předchozí a další rozdíl, jen stránky s rozdíly, synchronizace pohledu se seznamem, zobrazení značek. Barvy pro odebrané, přidané, nahrazené a přesunuté. Zpráva o porovnání do PDF, export rozdílů do XML.
- **Vstup → výstup:** dva PDF → přehled rozdílů, zpráva PDF nebo XML.
- **Dnes:** aplikace Diff. CLI `diff`.
- **Klíčová slova:** `compare, compare pdfs, compare report, diff, differences, changes, versions, revision, side by side, overlay, what changed, redline, track changes, before after`

#### `review.measure` – Measure

- **Co dělá:** Měří na stránce délky, obvody, plochy a úhly ve skutečných jednotkách.
- **Funkce:** vodorovný, svislý a obecný rozměr, obvod, obvod obdélníku, plocha, plocha obdélníku, úhel. Měřítko: kalibrace dvěma body o známé vzdálenosti, předvolby měřítek, vlastní měřítko, měřítko pamatované pro každý dokument. Jednotky délek, ploch a úhlů. Zobrazení a smazání měření, převod na měřicí anotace uložené v dokumentu, export měření do CSV. Vzhled popisků (písmo, barvy).
- **Vstup → výstup:** otevřený dokument → měření na obrazovce, anotace nebo CSV.
- **Dnes:** plugin Dimensions.
- **Klíčová slova:** `measure, dimensions, distance, length, area, perimeter, angle, scale, ruler, dimension, calibrate, floor plan, drawing, takeoff, blueprint, cad`

#### `review.output-preview` – Output Preview

- **Co dělá:** Ukáže, jak se dokument vytiskne: separace, přetisk a problémová místa.
- **Funkce:** režimy *Separations*, *Color Warnings – Ink Coverage*, *Color Warnings – Rich Black*, *Ink Coverage*, *Shape Channel*, *Opacity Channel*. Seznam procesních a přímých barev se zapínáním. Simulace separací a barvy papíru. Zapínání druhů obsahu: obrázky, text, vektorová grafika, stínování, vzory. Limit krytí, limit sytě černé, barva výstrahy.
- **Vstup → výstup:** otevřený dokument → náhled.
- **Dnes:** plugin OutputPreview, *Output Preview*.
- **Klíčová slova:** `output preview, separations, overprint, spot colors, cmyk, prepress, rich black, color warnings, plates, pantone, print preview, transparency`

#### `review.ink-coverage` – Ink Coverage

- **Co dělá:** Spočítá spotřebu barev pro každou stránku a celý dokument.
- **Funkce:** tabulka krytí procesních a přímých barev po stránkách.
- **Vstup → výstup:** otevřený dokument → tabulka.
- **Dnes:** plugin OutputPreview, *Ink Coverage*. CLI `ink-coverage` a `info-inks`.
- **Klíčová slova:** `ink coverage, ink usage, toner, tac, total area coverage, cmyk coverage, print cost, inks, ink consumption`

#### `review.soft-proof` – Soft Proofing

- **Co dělá:** Simuluje na obrazovce barvy cílového tiskového zařízení a upozorní na barvy mimo gamut.
- **Funkce:** zapnutí soft proofingu, kontrola gamutu, profil a záměr proofingu, barva výstrahy.
- **Vstup → výstup:** otevřený dokument → náhled.
- **Dnes:** plugin SoftProofing.
- **Klíčová slova:** `soft proof, soft proofing, gamut, gamut check, gamut checking, icc profile, color profile, cmyk simulation, proof colors, out of gamut, color management`

#### `review.statistics` – Document Statistics

- **Co dělá:** Ukáže, co v souboru zabírá místo.
- **Funkce:** statistika podle funkce objektu (stránky, proudy obsahu, grafické stavy, barevné prostory, vzory, stínování, obrázky, formuláře, písma, akce, anotace, ostatní) a podle typu objektu: podíl, počet, velikost v bajtech. Tabulka a graf.
- **Vstup → výstup:** otevřený dokument → přehled.
- **Dnes:** plugin ObjectInspector, *Object Statistics*. CLI `statistics`.
- **Klíčová slova:** `statistics, object statistics, space usage, audit, what takes space, file size analysis, object count, size breakdown, why so big`

#### `review.object-inspector` – Object Inspector

- **Co dělá:** Prochází vnitřní objekty PDF.
- **Funkce:** strom objektů v pohledech *Document*, *Pages*, *Content streams*, *Graphic states*, *Color spaces*, *Patterns*, *Shadings*, *Images*, *Forms*, *Fonts*, *Actions*, *Annotations*, *Object List*. Detail objektu: odkaz, typ, popis, obsah včetně dekomprimovaných proudů a obrázků. Připnutí objektu pro porovnání.
- **Vstup → výstup:** otevřený dokument → prohlížeč objektů.
- **Dnes:** plugin ObjectInspector, *Object Inspector*.
- **Klíčová slova:** `object inspector, internal structure, objects, streams, dictionary, debug, pdf syntax, cos, raw, developer, low level, xref`

#### `review.report` – Document Report

- **Co dělá:** Sestaví zprávu o dokumentu, kterou jde uložit.
- **Funkce:** základní informace, metadata (XMP), JavaScript v dokumentu, pojmenované cíle, rámce stránek, strom struktury tagovaného dokumentu, použité barvy. Export celé vnitřní struktury do XML. Výstup jako text, XML nebo HTML.
- **Vstup → výstup:** otevřený dokument → zpráva.
- **Dnes:** jen CLI: `info`, `info-metadata`, `info-javascript`, `info-dests`, `info-page-boxes`, `info-struct-tree`, `info-inks`, `xml`.
- **Klíčová slova:** `report, document info, javascript, scripts, named destinations, structure tree, tags, tagged pdf, xmp, metadata, xml export, analyze, audit`

---

## 6. Mapa pokrytí dnešních aplikací

Kontrola, že nová aplikace neztratí nic z toho, co dnes umí Editor, Viewer, PageMaster a Diff. Každá akce dnešního rozhraní je přiřazena buď toolu, nebo jiné vrstvě podle kapitoly 1. Seznam akcí je vytažený ze souborů `.ui`, z výčtu `PDFActionManager::Action` a ze zdrojáků pluginů.

Zkratky ve sloupci „Kam“: **Tool** (ID toolu), **Příkaz** (lišta nebo panel dokumentu), **Režim** (režim myši z položek Select a Annotate), **Dokument** (chování přímo ve stránce), **Panel** (postranní panel), **Nastavení**, **Aplikace** (stránka nebo funkce na úrovni celé aplikace).

### 6.1 Editor

Viewer je podmnožina Editoru. Akce, které má i Viewer, jsou označené **V**.

| Nabídka | Akce | Kam |
| --- | --- | --- |
| File | Open **V**, Close **V**, Quit **V** | Aplikace: Open, karty dokumentů |
| File | Automatic Document Refresh **V** | Příkaz: panel File |
| File | Save, Save As | Příkaz: panel File |
| File | Send by E-Mail **V**, Print **V** | Příkaz: panel File |
| File | Render to Images | Tool `convert.pdf-to-images` |
| File | Properties **V** | Příkaz: obrazovka Properties včetně části Fonts |
| File | Clear Recent File History **V** | Aplikace: stránka Recent |
| Edit | Undo, Redo | Příkaz |
| Edit | Find, Find Previous, Find Next **V** | Příkaz: hledání v dokumentu |
| Edit | Pokročilé hledání (panel *Advanced Find*: celá slova, velikost písmen, regulární výrazy, zástupné znaky, měkké dělení, tabulka výsledků) | Příkaz: hledání v dokumentu. Zdroj výběru pro `protect.redact` |
| Edit | Select text, Select All, Deselect, Copy text **V** | Režim a Příkaz. Hromadně Tool `convert.extract-text` |
| Edit | Select table **V** | Režim: Select |
| Edit | Encryption | Tooly `protect.password`, `protect.permissions`, `protect.certificate-encrypt`, `protect.unlock` |
| Edit | Optimize | Tool `optimize.structure`, součást `optimize.compress` |
| Edit | Optimize Images | Tool `optimize.images`, předvolby `optimize.compress` a `optimize.grayscale` |
| Edit | Sanitize | Tool `protect.sanitize` |
| Edit | Remove External Links | Tool `protect.remove-links` |
| Edit | Page Geometry | Tooly `pages.resize`, `pages.boxes` |
| Edit | Create Bitonal Document | Tool `optimize.bitonal` |
| View | Page Layout: Single Page, Continuous, Two Pages, Two columns, First page on right side **V** | Příkaz: panel View |
| View | Fullscreen Mode **V** | Příkaz: panel View |
| View | Rendering Options: Antialiasing, Text Antialiasing, Smooth Pictures, Ignore Optional Content Settings, Display Annotations, Display Render Times **V** | Příkaz: panel View, a Nastavení |
| View | Rotate Right, Rotate Left (jen pohled) **V** | Příkaz: panel View. Trvalé otočení je Tool `pages.rotate` |
| View | Zoom In, Zoom Out, Fit Page, Fit Width, Fit Height **V** | Příkaz: lišta dokumentu |
| View | Color: Inverted, Grayscale, High Contrast, Monochromatic, Custom **V** | Příkaz: panel View (barevné režimy) |
| View | Sidebar (zobrazení a skrytí postranního panelu, plovoucí panel) **V** | Příkaz: tlačítko pro skrytí postranního panelu v liště dokumentu, šířka panelu splitterem. Odpojení panelu do plovoucího okna se vědomě nenahrazuje |
| Insert | Sticky Note (7 druhů) | Režim: Annotate |
| Insert | Hyperlink, Hyperlink to this PDF (9 typů cíle) | Režim: Annotate |
| Insert | Inline text | Režim: Annotate |
| Insert | Straight Line, Polyline, Rectangle, Polygon, Ellipse, Freehand Curve | Režim: Annotate |
| Insert | Stamp (14 razítek) | Režim: Annotate |
| Insert | Text Highlight: Highlight, Underline, Strikeout, Squiggly | Režim: Annotate |
| Insert | Delete Annotations | Režim: Annotate |
| Insert | Insert Page Numbers | Tool `edit.page-numbers` |
| Go To | Document start, Document end, Next page, Previous page, Next line, Previous line **V** | Příkaz: panel Go To |
| Go To | Bookmark Page, Next Bookmark, Previous Bookmark, Export, Import, Generate Automatically **V** | Panel Bookmarks |
| Tools | Magnifier **V** | Režim |
| Tools | Screenshot | Režim (výřez stránky do schránky) |
| Tools | Extract Image | Režim. Hromadně Tool `convert.extract-images` |
| Tools | Rendering Errors **V** | Příkaz: stavový řádek, obrazovka Rendering issues |
| Tools | Options **V**, Reset to Factory Settings **V** | Nastavení |
| Tools | Certificates **V** | Tool `protect.certificates` |
| Help | Get Source **V**, Become a Sponsor **V**, About **V** | Aplikace: stránky About, Sponsor |
| Developer | Show Text Blocks, Show Text Lines | Příkaz: panel View, jen ve vývojářském režimu |

Funkce Editoru bez položky v nabídce:

| Funkce | Kam |
| --- | --- |
| Panel Outline: procházení **V**, úpravy osnovy | Panel |
| Panel Thumbnails: náhledy, velikost, synchronizace s aktuální stránkou **V** | Panel. Nově výběr více stránek a operace nad ním, viz úvod 5.1 |
| Panel Visibility: vrstvy (optional content) **V** | Panel Layers |
| Panel Attachments: otevřít, uložit **V** | Panel |
| Panel Speech: čtení nahlas **V** | Panel |
| Panel Signatures: ověření podpisů, důvěra certifikátu **V** | Dokument (ověření při otevření) a Panel |
| Panel Bookmarks: osobní záložky **V** | Panel |
| Panel Notes: seznam anotací, hledání | Panel |
| Vyplňování formulářů | Dokument: kliknutí do pole |
| Práce s anotací: výběr, přesun, velikost, otočení, zarovnání, kopírování na víc stránek, vlastnosti, odpovědi, geometrie | Dokument: přímo na stránce a kontextová nabídka |
| Otevření zaheslovaného dokumentu **V** | Aplikace: obrazovka hesla |
| Odkazy a akce v dokumentu: přechod na cíl, otevření URL a spuštění aplikace s potvrzením **V** | Příkaz. Povolení v Nastavení (Security) |
| Dotaz na uložení při zavření | Aplikace |

Pluginy Editoru:

| Plugin | Akce | Kam |
| --- | --- | --- |
| AudioBook | Create Text Stream, výběry (Rectangle, Contained Text, Regular Expression, Page List), Activate, Deactivate, Restore Original Text, Move Up, Move Down, synchronizace výběru, Create Audio Book, Clear Text Stream | Tool `convert.audiobook` |
| Dimensions | Horizontal, Vertical, Linear Dimension, Perimeter, Rectangle Perimeter, Area, Rectangle Area, Angle, Calibrate Scale, Scale, Show, Clear, Convert to Annotations, Export Measurements, Settings | Tool `review.measure` |
| Editor | Edit page content, Create Text Label, Freehand Curve, Accept Mark, Reject Mark, Rectangle, Rounded Rectangle, Horizontal Line, Vertical Line, Line, Dot, SVG Image, Multiple Elements, Undo, Redo, Clear All Graphics, panel Editor Toolbox | Tool `edit.content` |
| ObjectInspector | Object Inspector | Tool `review.object-inspector` |
| ObjectInspector | Object Statistics | Tool `review.statistics` |
| OutputPreview | Output Preview | Tool `review.output-preview` |
| OutputPreview | Ink Coverage | Tool `review.ink-coverage` |
| Redact | Redact Rectangle, Redact Text, Redact Text Selection, Redact Page(s), Create Redacted Document | Tool `protect.redact` |
| Scanner | Scan Pages | Tool `convert.scan` |
| Signature | Activate signature creator, kreslicí nástroje, Clear All Graphics, Sign Electronically | Tool `protect.sign-by-hand` |
| Signature | Sign Digitally With Certificate | Tooly `protect.sign`, `protect.timestamp` |
| Signature | Certificates Manager | Tool `protect.certificates` |
| SoftProofing | Soft Proofing, Gamut Checking, Soft Proofing Settings | Tool `review.soft-proof` |

Nastavení (dialog *Options*) přechází na stránku Settings: Engine, Rendering, Shading, Cache, Shortcuts, Color management, Color postprocessing, Security, Author identity, UI, Speech, Form, Digital signature verification. Větev OCR přidává *Text Recognition (OCR)* a *Manage OCR Languages*. Stránka *Plugins* zaniká, protože pluginy v nové aplikaci nebudou. Jejich funkce jsou vestavěné tooly podle tabulky výše.

### 6.2 PageMaster

| Nabídka | Akce | Kam |
| --- | --- | --- |
| File | Add Documents | Tooly `pages.merge`, `pages.manage` (přidání zdrojů) |
| File | Open Workspace, Save Workspace | Tool `pages.manage`: *Open Assembly*, *Save Assembly*. Soubory `.pagemaster` bez zpětné kompatibility |
| File | Save Checkpoint, Load Checkpoint | Tool `pages.manage` |
| File | Recent (soubory, pracovní plochy, složky), Clear Recent | Aplikace: stránka Recent (soubory, sestavy, složky) |
| File | Clear, Close | Tool `pages.manage`, Aplikace |
| Edit | Undo, Redo | Příkaz v rámci toolu |
| Edit | Clone Selection, Cut, Copy, Paste | Tool `pages.manage` |
| Edit | Remove Selection, Restore Removed Items | Tool `pages.remove` |
| Edit | Replace Selection | Tool `pages.insert` |
| Edit | Page Geometry | Tooly `pages.resize`, `pages.boxes` |
| Edit | Rotate Left, Rotate Right, Reset Rotation | Tool `pages.rotate` |
| Edit | Group, Ungroup, Rename Item or Group, Properties | Tool `pages.manage` |
| Edit | Crop Pages | Tool `pages.crop` |
| Edit | Sort: by File Name, by Source, by Page Number, by Type, Reverse Order | Tool `pages.manage` |
| Insert | Insert PDF, Insert PDF Pages, Insert Empty Page | Tool `pages.insert`. Prázdný dokument Tool `convert.blank` |
| Insert | Insert Image | Tool `pages.add-image` (předvolba `pages.insert`). Dokument jen z obrázků Tool `convert.images-to-pdf` |
| Regroup | by Even/Odd Pages, by Page Pairs, by Outline, by Reverse | Tool `pages.manage` (nabídka *Regroup*) |
| Regroup | by Alternating Pages, by Alternating Pages (Reversed Order) | Tool `pages.interleave` |
| View | Select None, All, Page Range, Even, Odd, Portrait, Landscape, Visible, Invert Selection | Společná komponenta pro výběr stránek ve všech toolech (9.6) |
| View | Zoom In, Zoom Out, Show Document Title in Items, Details View | Tool `pages.manage` |
| View | Hledání na pracovní ploše, Clear Search | Tool `pages.manage` (hledání v sestavě) |
| Make | United Document | Tool `pages.merge`. Pro výběr stránek Tool `pages.extract` |
| Make | Separate to Multiple Documents, Separate to Multiple Documents (Grouped), Split | Tool `pages.split` pro jeden dokument. Nad sestavou výstup *One PDF per page* a *One PDF per group or item* toolu `pages.manage` |
| Make | Volba Optimize images in output PDFs, Image Optimization Settings | Volba výstupu v `pages.merge` a `pages.split`, jinak Tool `optimize.images` |
| Toolbars | Zobrazení lišt | Lišty v novém UI nejsou. Kontrolu nad plochou stránky zachovává tlačítko pro skrytí postranního panelu, splitter pro změnu jeho šířky, celá obrazovka a nastavení *Show sidebar when opening documents*. Přesouvání lišt a odpojování panelů do plovoucích oken se vědomě nenahrazuje |
| Help | Get Source, Become a Sponsor, About | Aplikace |
| Help | Prepare Icon Theme | Vývojářská akce, zaniká |

### 6.3 Diff

| Nabídka | Akce | Kam |
| --- | --- | --- |
| File | Open Left, Open Right, Close | Tool `review.compare` |
| Compare | Compare | Tool `review.compare` |
| Compare | Create Compare Report, Save Differences to XML | Tool `review.compare` (výstupy) |
| View | Previous Difference, Next Difference | Tool `review.compare` |
| View | View Differences, View Left, View Right, View Overlay | Tool `review.compare` |
| View | Filter: Text, Vector Graphics, Images, Shading, Page Movement | Tool `review.compare` |
| View | Show Pages with Differences, Synchronize View with Differences, Display Differences, Display Markers | Tool `review.compare` |
| Panel Settings | výběr stránek levého a pravého dokumentu, volby porovnání, nastavení překryvu, barvy | Tool `review.compare` |
| Panel Differences | seznam rozdílů | Tool `review.compare` |
| View, Toolbars | Zobrazení panelů Settings a Differences, zobrazení lišt | Panely jsou pevnou součástí karty Compare. Lišty v novém UI nejsou, přesouvání lišt a odpojování panelů se vědomě nenahrazuje (stejně jako v 6.2) |
| Help | Get Source, Become a Sponsor, About | Aplikace |

### 6.4 PdfTool (příkazová řádka)

PdfTool do zadání nepatří, ale ukazuje funkce, které knihovna umí a GUI je nenabízí. Ty jsou v katalogu ve stavu **CLI**.

| Příkaz | Tool nebo jiné místo | V GUI dnes |
| --- | --- | --- |
| `unite` | `pages.merge` | ano |
| `separate` | `pages.split` | ano |
| `render` | `convert.pdf-to-images` | ano |
| `fetch-text` | `convert.extract-text` | **ne** |
| `fetch-images` | `convert.extract-images` | jen jeden obrázek |
| `audio-book`, `audio-book-voices` | `convert.audiobook` | ano |
| `attachments` | Panel Attachments | ano |
| `encrypt` | `protect.password`, `protect.permissions` | ano |
| `decrypt` | `protect.unlock` | ano |
| `verify-signatures` | Panel Signatures | ano |
| `cert-store`, `cert-store-install` | `protect.certificates` | ano |
| `redact` | `protect.redact` | ano |
| `remove-external-links` | `protect.remove-links` | ano |
| `optimize` | `optimize.structure`, `optimize.compress` | ano |
| `bitonal` | `optimize.bitonal` | ano |
| `diff` | `review.compare` | ano |
| `ink-coverage`, `info-inks` | `review.ink-coverage` | ano |
| `info-fonts` | Obrazovka Properties, část Fonts | ano |
| `statistics` | `review.statistics` | ano |
| `info`, `info-metadata`, `info-javascript`, `info-dests`, `info-page-boxes`, `info-struct-tree`, `xml` | `review.report` | **ne** (jen část v Properties) |
| `color-profiles` | Nastavení (Color management) | ano |
| `benchmark` | bez toolu, vývojářská funkce | ne |

### 6.5 Větev OCR

| Akce | Kam |
| --- | --- |
| Tools → Recognize Text (OCR) | Tool `convert.ocr` |
| Manage OCR Languages | Tool `convert.ocr` (správa jazyků), Nastavení |

---

## 7. Rešerše konkurence

Rešerše proběhla 2. 10. 2026 nad oficiálními stránkami, manuály a zdrojovým kódem výrobců. Odkazy jsou v části 7.7.

### 7.1 Co bylo zkoumáno a s jakou jistotou

| Zkratka | Produkt | Zdroj a spolehlivost |
| --- | --- | --- |
| AA | Adobe Acrobat Pro | Jednotlivé stránky nápovědy. Úplný seznam „All tools“ Adobe jako text nezveřejňuje a názvy kategorií nového zobrazení se nepodařilo ověřit. |
| FX | Foxit PDF Editor 13 | Uživatelský manuál, ověřeno. |
| PX | PDF-XChange Editor 10 | Manuál, ověřeno. Obsah karty Accessibility nezjištěn. |
| NI | Nitro PDF Pro | Ověřen jen seznam karet, příkazy na kartách ne. |
| PS, PSE | PDFsam Basic, PDFsam Enhanced | Stránky produktu, ověřeno. |
| ST | Stirling-PDF | Registr toolů ve zdrojovém kódu, přesné. |
| SM, IL | Smallpdf, iLovePDF | Webové katalogy, ověřeno. |
| P24 | PDF24 | Webový katalog. Dlaždice desktopové aplikace neověřeny. |
| PG | PDFgear | Webový katalog. Desktopová aplikace neověřena. |
| MP | Master PDF Editor | Obsah nápovědy. |
| SJ | Sejda | Web a desktop, ověřeno. |
| BB | Bluebeam Revu 21 | Manuál, ověřeno. |
| PP | Tungsten Power PDF 5.1 | Manuál, ověřeno. |
| – | qpdf, pdfcpu, mutool, poppler-utils | Dokumentace příkazové řádky. |

Když u funkce níže některý produkt chybí, znamená to „nepotvrzeno ve zdrojích“, ne „produkt to neumí“.

### 7.2 Jak konkurence člení katalog

| Produkt | Počet kategorií | Kategorie |
| --- | --- | --- |
| Acrobat, klasické Tools center | 5 | Create & Edit, Forms & Signatures, Share & Review, Protect & Standardize, Customize |
| Acrobat online | 5 | Generative AI, Convert, Reduce file size, Edit, Sign & protect |
| iLovePDF | 6 a Workflows | Organize, Optimize, Convert, Edit, Security, Intelligence |
| Smallpdf | 8 | Compress, Convert, Organize, Edit, Fill & Sign, Protect, AI PDF, Scan |
| PDFgear online | 5 | Edit & Read, Convert from PDF, Convert to PDF, Organize, Sign |
| PDF24 | 11 a tři osobní sekce | Create, Invoices, Edit, Organize, Optimize & repair, Security & privacy, View & check, Convert to PDF, Convert from PDF, Convert images, Desktop |
| Sejda | 8 až 10 | Merge, Split, Edit & Sign, Compress, Scans, Security, Convert from PDF, Convert to PDF, Others, Automate |
| Stirling-PDF | 3 úrovně × 12 podkategorií | Recommended, Standard, Advanced. Podkategorie Signing, Document Security, Verification, Document Review, Page Formatting, Extraction, Removal, Automation, General, Advanced Formatting, Developer Tools, AI |
| Foxit, PDF-XChange, Nitro, Power PDF | 9 až 13 karet pásu | Home, Convert, Edit, Organize, Comment, View, Form, Protect, Share, Accessibility a další |
| Bluebeam | nabídky | Document (jeden soubor), Batch (víc souborů), Tools (značky, měření, formuláře) |

Co z toho plyne:

1. **Spotřebitelské katalogy se sbíhají k pěti až osmi kategoriím podle úkolu:** Organize, Edit, Convert, Optimize, Security, Sign. Náš návrh se šesti kategoriemi je nejblíž iLovePDF.
2. **Profesionální aplikace přidávají druhou vrstvu:** Forms, Review, Accessibility, Print production, Automate. Pro profesionální editor rešerše doporučuje sedm až devět kategorií. Šest volíme proto, že odpovídají úkolům, se kterými uživatel přichází, a forms, review a print production se u nás vejdou do podskupin. Viz zásada 4 v kapitole 2 a rozhodnutí v kapitole 10.
3. **Tři zařazení jsou mezi produkty sporná.** Podpis je pod Security v iLovePDF a PDF24, ale samostatně ve Smallpdf, Acrobatu a Stirlingu. Vodoznak je Edit v iLovePDF, Security v Sejdě a Stirlingu. Flatten je Protect ve Smallpdf a Optimize i Security v PDF24.
4. **Sběrné kategorie jsou varování.** Sejda má v „Others“ patnáct toolů včetně Rotate a Crop. Stirling má v „General“ vedle sebe Merge, OCR a Add Text. Proto návrh nemá žádnou kategorii „Other“ ani „Advanced“.
5. **Dělení Convert na „do PDF“ a „z PDF“ je téměř všude.** U nás to řeší podskupiny Create a Export.
6. **Jeden tool s režimy, nebo mnoho karet.** Stirling má jeden Split s osmi metodami a jeden Convert. PDF24 a Smallpdf rozpadají převody na 20 až 30 karet, což je dané SEO webu. Sejda má pět karet Split. Náš návrh drží režimy uvnitř toolu (Split) a dělí jen tam, kde jde o jiný úkol (heslo proti oprávněním).
7. **Anotace a formuláře jako tool.** Webové katalogy mají karty „Annotate“ a „Fill Forms“, protože katalog je u nich jediný vstup do aplikace. Desktopové aplikace s lištou dokumentu (Foxit, PDF-XChange, Bluebeam) je mají přímo u dokumentu. Náš návrh je proto v katalogu nemá.

### 7.3 Barvy u konkurence

| Produkt | Pravidlo |
| --- | --- |
| iLovePDF | Jedna barva na kategorii: Organize oranžovočervená, Optimize zelená, Edit fialovorůžová, Security modrá, Intelligence fialová. Převody mají barvu cílové aplikace. |
| Smallpdf | Jedna barva na kategorii: Compress a Convert červená, Organize fialová, Edit tyrkysová, Protect korálová, Sign růžová, AI a Scan tmavě modrá. Převody mají barvu druhého formátu (Word modrá, Excel zelená, PowerPoint oranžová). |
| Stirling-PDF | Osm barev pro dvanáct podkategorií: Signing zelená, Security a Verification oranžová, Review a General modrá, Page Formatting fialová, Extraction azurová, Removal červená, AI a Automation růžová, Developer šedá. |
| Acrobat | Barvy ikon se z textových zdrojů nepodařilo ověřit. |

Ustálená konvence neexistuje. Princip „jedna barva na kategorii“ používají všechny tři ověřené produkty. Naše volba se s iLovePDF shoduje jen zhruba u Organize (teplá barva). U Edit a Optimize máme barvy obráceně než iLovePDF: Edit je zelená a Optimize fialová.

### 7.4 Co má konkurence lépe

Seřazeno podle dopadu na návrh katalogu. U každého bodu je, co si z toho vzít.

| # | Co | Kdo | Co z toho pro nás |
| --- | --- | --- | --- |
| 1 | **Hledání se skrytými synonymy.** Každá karta má seznam slov, podle kterých se najde. | P24 (atribut `data-tags`), ST (pole `synonyms`) | Klíčová slova v kapitole 5 jsou přesně tohle. Jejich seznamy jsou doplněné o slova, která používají PDF24 a Stirling. |
| 2 | **Hledání příkazů z kteréhokoli místa.** | FX (Alt+Q), PP („Find a Tool“), PX („Quick Launch“), AA, NI | Jedno hledání pro tooly, příkazy a nastavení, viz 9.1. |
| 3 | **Oblíbené, naposledy použité a často používané.** | P24 (Favorites, Last used, Frequently used), ST (hvězdička, Quick Access), PP, AA (přeuspořádání) | Pinned a Recent ve Figmě jsou. Chybí „často používané“. |
| 4 | **Tool jako panel vedle dokumentu.** | AA (toolset v levém panelu), ST (postranní panel nebo celoplošný katalog) | Návrh běhu toolu nad dokumentem, viz 9.3. |
| 5 | **Dávky jako plnohodnotná část UI.** | BB (nabídka Batch zrcadlí nabídku Document), FX (Batch print, Batch Encrypt), AA (OCR ve více souborech), PP (Watched Folders), ST (sledování složky) | Nemáme vůbec. Viz 8.3. |
| 6 | **Řetězení toolů.** | AA (Use guided actions), FX (Action Wizard), ST (Automate), IL a SJ (Workflows), PP, BB (Script) | Collections ve Figmě zůstávají skupinami toolů. Řetězení toolů nenavrhujeme, viz 8.4. |
| 7 | **Tool popsaný daty.** Registr toolu nese synonyma, počet souborů, podporované formáty, příznak pro automatizaci a stav alpha nebo beta. | ST | Viz 9.5. |
| 8 | **Vysvětlený nedostupný stav.** | ST („Unavailable – required tool missing“), IL (štítek „New!“) | Viz 9.2. |
| 9 | **Komprese pro laiky.** Acrobat přejmenoval „Optimize PDF“ na „Compress a PDF“. PDFgear má i „Compress PDF to 100KB“. | AA, FX, SM, P24, PG | Tool `optimize.compress` s předvolbami. Zvážit i předvolbu cílové velikosti. |
| 10 | **Redakce hledáním vzorů.** | AA, FX (i Smart Redact), PX, ST, PP | Máme regulární výrazy a redakci výběru, chybí hotové vzory. Viz 8.2. |
| 11 | **Vodoznak, záhlaví a zápatí, Batesovo číslování, pozadí.** | AA, FX, PX, MP, SJ a další | Nemáme. Viz 8.2. |
| 12 | **Flatten.** | AA, FX, PX, ST, SM, PG, P24, SJ, BB, PP | Nemáme. Viz 8.2. |
| 13 | **Tvorba formulářů a rozpoznání polí.** | AA, FX, PX, ST, IL, P24, BB, PP | Umíme jen vyplňovat. Viz 8.2. |
| 14 | **Převody z a do Office.** | všechny komerční produkty, ST přes LibreOffice | Nemáme. Viz 8.2. |
| 15 | **PDF/A: převod a kontrola.** | AA, FX, ST, SM, IL, P24, MP, BB | Nemáme. Viz 8.2. |
| 16 | **Oprava poškozeného PDF.** | ST, IL, P24, SJ, BB | Nemáme jako tool. Viz 8.2. |
| 17 | **Úklid skenů:** narovnání, automatické otočení, vylepšení. | PX, AA, SJ, ST | Větev OCR to umí jen na pracovním obrazu. Viz 8.2. |
| 18 | **Bohatší práce se stránkami:** N-up, brožura, půlení stránek, překryv stránek, hledání duplicitních stránek, výměna dvou stránek. | ST, P24, SJ, PX, FX | Viz 8.2. |
| 19 | **Generování záložek (osnovy).** PDF-XChange má pět generátorů: z textu stránky, každá N-tá stránka, z obsahu, ze zvýraznění, z textového souboru. | PX, FX, SJ | Viz 8.2. |
| 20 | **Souhrn, import a export komentářů.** | AA, FX, PX, BB, PP | Viz 8.2. |
| 21 | **Úprava metadat.** | AA, FX, ST, P24, SJ | Vlastnosti dokumentu máme jen pro čtení. Viz 8.2. |
| 22 | **Měření navíc:** poloměr, objem, počítání prvků. | BB | Okrajové, nezařazeno. |
| 23 | **Přístupnost:** kontrola, automatické tagování, pořadí čtení. | AA, FX, PX, NI | Viz 8.2. |

### 7.5 Kde je PDF4QT na úrovni konkurence nebo před ní

Tyhle tooly si zaslouží v katalogu viditelné místo, protože je konkurence většinou nemá.

| Oblast | Stav u konkurence | PDF4QT |
| --- | --- | --- |
| Tisková příprava: náhled výstupu, separace, krytí barev | V rešerši jen Acrobat. | Output Preview, Ink Coverage, Soft Proofing, Set Page Boxes. |
| Rozbor velikosti souboru | Jen Acrobat (Audit space usage). | Document Statistics a Object Inspector. |
| Převod do zvuku | Čtení nahlas AA, FX, PP. Uložení do MP3 jen Power PDF. | Čtení nahlas v panelu Speech a tool Create Audio Book s úpravou textu před převodem. |
| Převod na černobílé s JBIG2 a CCITT | V rešerši nenalezeno jako samostatný tool. | Convert to Black & White s volbou metody a náhledem. |
| Porovnání dokumentů | Porovnání mají AA, FX, PX, ST, IL, P24, MP, BB a PP. Překryv jako způsob porovnání má Bluebeam. | Compare s překryvem, filtry druhů rozdílů a zprávou. |
| Šifrování certifikátem | AA, FX, PX, PP, MP. | Encrypt with Certificate. |
| Časové razítko | AA, FX, PX, ST, PP. | Add Timestamp. |
| Prolnutí stránek | FX, PS, SJ, P24. | Interleave Pages. |
| Měření s kalibrací, úhlem a exportem | AA, FX, PX, MP, BB, PP. | Measure, export do CSV, převod na anotace. |
| Kontrola výsledku OCR | Foxit má „Suspect Results“. | Větev OCR má kontrolu slov podle jistoty a opravy. |
| Práce bez cloudu a bez účtu | Smallpdf, iLovePDF a z části Acrobat pracují přes server. | Vše lokálně. |

### 7.6 Zásady z literatury, o které se návrh opírá

| Zásada | Zdroj | Kde je v návrhu |
| --- | --- | --- |
| Popisek příkazu začíná slovesem, má dvě až čtyři slova a podstatné jméno ho upřesní („Delete Folder“, ne „Delete“). Stejný popisek všude, kde se příkaz objeví. | NN/g, UI Copy | Názvy toolů, zásada 1 v kapitole 2. |
| Názvy kategorií mají být známá slova, ne vymyšlené nebo abstraktní pojmy. | NN/g, Menu-Design Checklist a Top 10 IA Mistakes | Názvy kategorií. Bez „Smart Tools“ a „Advanced Processing“. |
| Popisek musí předpovědět obsah (information scent). Pomáhá jednořádkový popis pod názvem. | NN/g, Information Scent | Popis na kartě u každého toolu. |
| Položku lze zařadit do dvou kategorií, když ji tam lidé hledají, ale jen výjimečně. | NN/g, Polyhierarchy | Zásada 6. Kvůli barvě držíme jednu kategorii a zbytek řeší hledání. |
| Rozpoznání je snazší než vybavení. Historie a oblíbené ho podporují. | NN/g, Recognition and Recall | Viditelná mřížka, Pinned a Recent. |
| Hledání bez synonym selhává. | Baymard, studie hledání v e-shopech | Klíčová slova u každého toolu. |
| Kategorie se mají ověřit tříděním karet s uživateli. | NN/g, Card Sorting | Třídění karet se nedělá. Kategorie vycházejí z úkolů uživatele a z rešerše (7.2), viz zásada 4. |

### 7.7 Zdroje

Adobe Acrobat
- https://helpx.adobe.com/acrobat/desktop/get-started/learn-the-basics/workspace.html
- https://helpx.adobe.com/acrobat/using/print-production-tools-overview-acrobat.html
- https://helpx.adobe.com/acrobat/using/action-wizard-acrobat-pro.html
- https://helpx.adobe.com/acrobat/using/create-verify-pdf-accessibility.html
- https://helpx.adobe.com/acrobat/using/compare-documents.html
- https://www.adobe.com/devnet-docs/acrobatetk/tools/AdminGuide/advancedconfig.html
- https://www.adobe.com/acrobat/online.html

Foxit, PDF-XChange, Nitro
- https://cdn01.foxitsoftware.com/pub/foxit/manual/phantom/en_us/foxit-pdf-editor-user-manual-13.pdf
- https://help.pdf-xchange.com/pdfxe10/tabs-guide_ed.html
- https://www.gonitro.com/user-guide/pro/article/explore-the-user-interface

PDFsam, Stirling-PDF
- https://pdfsam.org/pdfsam-basic/
- https://pdfsam.org/pdfsam-enhanced/
- https://github.com/Stirling-Tools/Stirling-PDF/blob/main/frontend/editor/src/core/data/useTranslatedToolRegistry.tsx
- https://github.com/Stirling-Tools/Stirling-PDF/blob/main/frontend/editor/src/core/data/toolsTaxonomy.ts

Webové katalogy
- https://smallpdf.com/pdf-tools
- https://www.ilovepdf.com/
- https://tools.pdf24.org/en/all-tools
- https://www.pdfgear.com/online-tools/
- https://www.sejda.com/

Ostatní desktopové aplikace
- https://code-industry.net/masterpdfeditor-help/
- https://support.bluebeam.com/user-manual/menus/document/document-menu.html
- https://support.bluebeam.com/user-manual/menus/batch/batch-menu.html
- https://support.bluebeam.com/user-manual/menus/tools/tools-menu.html
- https://docshield.tungstenautomation.com/PowerPDF/en_US/5.1.1-x2ki7a3ycc/help/PowerPDF_help/c_ribbonhandlingandoverview.html

Příkazová řádka
- https://qpdf.readthedocs.io/en/stable/cli.html
- https://github.com/pdfcpu/pdfcpu
- https://mupdf.readthedocs.io/en/latest/tools/mutool.html

Zásady UX
- https://www.nngroup.com/articles/ui-copy/
- https://www.nngroup.com/articles/menu-design/
- https://www.nngroup.com/articles/information-scent/
- https://www.nngroup.com/articles/polyhierarchy/
- https://www.nngroup.com/articles/card-sorting-definition/
- https://www.nngroup.com/articles/recognition-and-recall/
- https://www.nngroup.com/articles/top-10-ia-mistakes/
- https://baymard.com/blog/ecommerce-search-report-and-benchmark

---

## 8. Co chybí – návrhy nových toolů

Tato kapitola je **backlog mimo rozsah sjednocení aplikací**. První vydání nové aplikace znamená paritu s dnešními aplikacemi (kapitola 6) a rychlé výhry z části 8.1. Ostatní návrhy, včetně převodů z a do Office, sjednocení neblokují.

Seznam vychází z rešerše v kapitole 7. Zkratky produktů jsou v části 7.1.

- **Očekávání** říká, jak moc uživatelé tool čekají. **P1** má většina zkoumaných produktů včetně bezplatných. **P2** je běžný v profesionálních aplikacích, nebo je to levné rozšíření něčeho, co už umíme. **P3** je specializovaný.
- **Náročnost** je můj hrubý odhad bez rozboru kódu: nízká (GUI nad hotovou funkcí), střední, vysoká (nový velký celek nebo externí komponenta).

### 8.1 Rychlé výhry: knihovna to umí, chybí jen GUI

Těchto pět toolů už je v katalogu v kapitole 4 se stavem CLI nebo GUI·split.

| Tool | Kategorie | Co je potřeba | Kdo to má |
| --- | --- | --- | --- |
| **Compress** (`optimize.compress`) | Optimize | Předvolby nad existující optimalizací obrázků a struktury. | AA, FX, ST, SM, IL, PG, P24, SJ, BB, PP |
| **Extract Pages** (`pages.extract`) | Organize Pages | Přímá akce „uložit výběr jako nový PDF“. | AA, FX, PX, PS, ST, SM, IL, PG, P24, SJ, BB, PP |
| **Extract Text** (`convert.extract-text`) | Create & Convert | Dialog nad tím, co dělá CLI `fetch-text`. | AA, FX, P24, SJ |
| **Extract Images** (`convert.extract-images`) | Create & Convert | Dialog nad CLI `fetch-images`. | FX, ST, P24, SJ |
| **Document Report** (`review.report`) | Review & Inspect | Obrazovka nad CLI `info-*` a `xml`. | ST (Get ALL Info on PDF, Show JavaScript) |

### 8.2 Nové tooly

#### Organize Pages (Amber)

| Tool | Popis na kartě | Co dělá | Kdo to má | Očekávání | Náročnost | Klíčová slova |
| --- | --- | --- | --- | --- | --- | --- |
| **Multiple Pages per Sheet** | Put 2, 4 or more pages on one sheet | Vyřazení N stránek na arch s volbou mřížky, okrajů a pořadí. | ST, P24, SJ, pdfcpu | P2 | střední | `n-up, 2-up, 4-up, pages per sheet, handout, imposition, grid, tile` |
| **Make Booklet** | Reorder pages for booklet printing | Přeskládá stránky pro tisk brožury s vazbou na hřbet. | ST, pdfcpu, AA jen jako volba tisku | P2 | střední | `booklet, brochure, pamphlet, saddle stitch, imposition, fold, signature` |
| **Split Double Pages** | Cut scanned spreads into single pages | Rozřízne dvoustrany skenu na jednotlivé stránky. | FX, PX, ST, P24, SJ | P2 | střední | `split in half, halve, double page, spread, two-page scan, book scan, divide page` |
| **Remove Blank Pages** | Find and delete empty pages | Najde prázdné stránky a nabídne je k odstranění. Detekce už existuje v dialogu Bitonal a ve větvi OCR. | ST | P2 | nízká | `blank pages, empty pages, remove blank, white pages, scan cleanup, separator pages` |
| **Overlay Pages** | Put one PDF on top of another | Položí stránky jednoho PDF přes druhé nebo pod ně. Typicky hlavičkový papír. | PX, ST, P24, PP, qpdf | P2 | střední | `overlay, underlay, superimpose, letterhead, layer pages, background pdf, digital paper` |
| **Page Labels** | Set the page numbers shown by viewers | Úprava logického číslování stránek (i, ii, 1, 2, A-1). Čtení popisků knihovna umí. | AA, FX, PX, BB, qpdf | P3 | střední | `page labels, logical page numbers, renumber, roman numerals, front matter` |
| **Poster** | Print one page across several sheets | Rozdělí velkou stránku na dlaždice pro tisk na menší papír. | ST, pdfcpu, mutool | P3 | střední | `poster, tile, large format, split page, banner, plot` |

#### Create & Convert (Blue)

| Tool | Popis na kartě | Co dělá | Kdo to má | Očekávání | Náročnost | Klíčová slova |
| --- | --- | --- | --- | --- | --- | --- |
| **PDF to Word / Excel / PowerPoint** | Export to editable Office files | Export do formátů Office. Vyžaduje rekonstrukci rozvržení. | AA, FX, PX, ST, SM, IL, PG, P24, SJ, PP, PSE | P1 | vysoká | `pdf to word, docx, pdf to excel, xlsx, pptx, office, editable, convert to word` |
| **Office to PDF** | Convert Word, Excel, PowerPoint files | Převod z formátů Office. Stirling to řeší přes externí LibreOffice. | AA, FX, PX, ST, SM, IL, PG, P24, SJ, PP | P1 | vysoká | `word to pdf, docx to pdf, excel to pdf, xlsx, pptx to pdf, office, libreoffice` |
| **Convert to PDF/A** | Make an archival, self-contained PDF | Převod do PDF/A. Vyžadují ho úřady a archivy. | AA, FX, ST, SM, IL, P24, MP, BB | P1 | vysoká | `pdf/a, pdfa, archive, archival, long term, iso 19005, compliance, pdf/a-1b, pdf/a-2b` |
| **Text to PDF** | Turn text, Markdown or HTML into a PDF | Vytvoření PDF z textového souboru, Markdownu nebo HTML. | PX, SM, P24, SJ, IL | P2 | střední | `text to pdf, txt to pdf, markdown to pdf, md, html to pdf, web page to pdf` |
| **PDF to SVG** | Save pages as vector images | Export stránek do SVG. | P24, mutool, pdftocairo | P3 | střední | `svg, vector export, pdf to svg, scalable, inkscape, illustrator` |
| **Rasterize PDF** | Turn pages into images inside the PDF | Nahradí obsah každé stránky jedním obrázkem vykresleným z její podoby. Text, vektorová grafika a anotace přestanou být samostatné objekty a text nejde hledat ani vybrat, dokud se na výsledek nepustí OCR. Nejde o ochranu proti kopírování, obrázek stránky jde dál zkopírovat. Vykreslování stránek už máme. | PX, P24 | P3 | nízká | `rasterize, flatten to image, image pdf, non-editable, burn in` |
| **E-Invoice** | Read and create ZUGFeRD, Factur-X invoices | Vytvoření, převod a kontrola elektronických faktur vložených v PDF. | P24 | P3 | vysoká | `e-invoice, zugferd, factur-x, xrechnung, invoice xml, en 16931` |

#### Edit (Green)

| Tool | Popis na kartě | Co dělá | Kdo to má | Očekávání | Náročnost | Klíčová slova |
| --- | --- | --- | --- | --- | --- | --- |
| **Add Watermark** | Stamp text or an image across pages | Textový nebo obrazový vodoznak s průhledností, rotací, polohou a rozsahem stránek. | AA, FX, PX, ST, SM, IL, P24, MP, SJ, PSE | P1 | střední | `watermark, draft, confidential, logo, overlay, background text, branding, copyright` |
| **Header & Footer** | Add text, date and numbers to pages | Záhlaví a zápatí s textem, datem, názvem souboru a číslem stránky. Rozšíření dnešního Add Page Numbers. | AA, FX, PX, MP, SJ, BB, PP, PSE | P1 | střední | `header, footer, running head, date stamp, running title, top, bottom, file name on page` |
| **Flatten** | Make comments and form fields permanent | Vykreslí vzhled anotací a vyplněných polí do obsahu stránky a anotace i pole z dokumentu odstraní. Z komentářů a polí se stane běžný obsah stránky, který jde dál upravit jako každý jiný obsah (Edit Content). Nejde o ochranu proti změnám. | AA, FX, PX, ST, SM, PG, P24, SJ, BB, PP | P1 | střední | `flatten, freeze, make static, bake, burn in, make permanent, lock form, print annotations` |
| **Prepare Form** | Add text fields, checkboxes and buttons | Vytváření a úprava formulářových polí, případně rozpoznání polí. | AA, FX, PX, ST, IL, PG, P24, MP, SJ, BB, PP, PSE | P1 | vysoká | `create form, form fields, fillable, make fillable, text field, checkbox, radio button, dropdown, form builder` |
| **Edit Document Properties** | Change title, author and keywords | Úprava názvu, autora, předmětu a klíčových slov. Dnes jsou vlastnosti jen pro čtení. | AA, FX, ST, P24, SJ | P1 | nízká | `metadata, title, author, subject, keywords, document info, properties, edit metadata` |
| **Remove Comments** | Delete all comments at once | Odstraní všechny anotace, případně podle typu nebo autora. Základ už umí Remove Hidden Data. | ST, SJ, PX | P2 | nízká | `remove annotations, delete comments, strip comments, clear markup, remove highlights` |
| **Bates Numbering** | Number pages across a set of documents | Průběžné číslování s předponou a příponou přes víc dokumentů. | AA, FX, PX, MP, SJ, PP | P2 | střední | `bates, bates stamp, legal numbering, exhibit, prefix, sequential numbering, discovery` |
| **Import / Export Form Data** | Move form values in and out | Import a export hodnot formuláře (FDF, XFDF, CSV, XML). | AA, FX, PX, BB, PP, PSE, pdfcpu | P2 | střední | `fdf, xfdf, form data, export form, import form, csv, merge data` |
| **Import / Export Comments** | Share comments without the PDF | Export a import anotací a tisknutelný souhrn komentářů. | AA, FX, PX, BB, PP | P2 | střední | `export comments, import comments, xfdf, comment summary, summarize comments, review report` |
| **Generate Outline** | Build the table of contents automatically | Vytvoří osnovu z nadpisů nebo z textu stránek. Automatické generování osobních záložek z kapitol už existuje. | PX, FX, SJ | P2 | střední | `auto bookmarks, generate outline, table of contents, headings, toc from headings, create bookmarks` |
| **Set Initial View** | Choose how the PDF opens | Nastaví rozvržení stránek, panel a zvětšení při otevření. Čtení těchto voleb už je v Properties. | FX, P24, PP, pdfcpu | P3 | nízká | `initial view, viewer preferences, open options, page mode, page layout, open in full screen` |
| **Find & Replace Text** | Replace a word in the whole document | Nahrazení textu v obsahu stránek. | FX, PX | P3 | vysoká | `find and replace, replace text, search and replace, change word` |
| **Detect Links** | Turn web addresses into clickable links | Najde URL v textu a vytvoří z nich odkazy. | AA, FX, PX, BB | P3 | střední | `auto link, detect urls, create links from text, clickable urls, web links` |
| **Page Background** | Put a color or image behind the content | Barevné nebo obrazové pozadí stránek. | AA, FX, PX, MP | P3 | střední | `background, page color, paper color, letterhead, underlay image` |

Dvě chybějící funkce patří podle pravidla z kapitoly 1 mimo katalog. **Přidání přílohy** (AA, FX, PX, ST, MP, PP) patří do panelu Attachments. **Vlastní razítka** z obrázku nebo textu (AA, FX, PX, MP) patří do položky Annotate.

#### Protect & Sign (Red)

| Tool | Popis na kartě | Co dělá | Kdo to má | Očekávání | Náročnost | Klíčová slova |
| --- | --- | --- | --- | --- | --- | --- |
| **Find & Redact** | Redact e-mails, phone numbers, IDs | Hledání podle vzorů (e-mail, telefon, IBAN, rodné číslo, datum) a hromadná redakce. Regulární výrazy a redakce výběru už existují, chybí hotové vzory a jeden krok. | AA, FX, PX, ST, PP | P2 | nízká | `search and redact, find and redact, pattern, pii, personal data, email, phone number, iban, auto redact, gdpr` |
| **Remove Scripts & Actions** | Strip JavaScript and launch actions | Odstraní JavaScript a spouštěcí akce. Výpis skriptů umí CLI `info-javascript`. | AA (součást Sanitize document), ST (jen výpis, Show JavaScript) | P2 | střední | `remove javascript, scripts, actions, launch action, malware, safe pdf, disarm` |
| **Certify Document** | Sign and lock what may change later | Certifikační podpis s omezením dalších změn. | AA, FX, PX, BB | P3 | střední | `certify, docmdp, certification signature, lock document, author signature` |
| **Remove Signatures** | Clear signatures from a document | Odstraní podpisy a podpisová pole. | ST, PX | P3 | střední | `remove signature, unsign, clear signatures, delete signature field` |
| **Add Validation Data (LTV)** | Keep signatures verifiable for years | Vloží do dokumentu údaje pro dlouhodobé ověření podpisu. | v rešerši nepotvrzeno | P3 | vysoká | `ltv, long term validation, dss, ocsp, crl, pades-lt, archival signature` |

#### Optimize (Violet)

| Tool | Popis na kartě | Co dělá | Kdo to má | Očekávání | Náročnost | Klíčová slova |
| --- | --- | --- | --- | --- | --- | --- |
| **Repair PDF** | Recover a damaged document | Pokusí se opravit poškozený soubor a uložit ho znovu. S tímto toolem se kategorie přejmenuje na *Optimize & Repair*. | ST, IL, P24, SJ, BB, mutool | P1 | střední | `repair, fix, recover, corrupted, corrupt, damaged, broken, cannot open, rebuild` |
| **Convert Colors** | Convert to grayscale, sRGB or CMYK | Převod barev celého obsahu včetně textu a vektorů. Doplní dnešní Convert Images to Grayscale, který převádí jen obrázky. | AA, PX, BB, SJ, ST, mutool | P2 | vysoká | `convert colors, cmyk, srgb, color profile, icc, grayscale, recolor, invert colors, rgb to cmyk` |
| **Clean Up Scans** | Straighten and clean scanned pages | Narovnání, automatické otočení a odstranění šumu u skenů. Větev OCR to dělá jen na pracovním obrazu. | PX, AA, SJ, ST, FX | P2 | střední | `deskew, straighten, despeckle, clean scan, enhance scan, remove noise, auto rotate` |
| **Fast Web View** | Optimize for opening from the web | Linearizace souboru. | P24, qpdf | P3 | střední | `linearize, fast web view, web optimize, streaming, byte serving` |
| **Embed Fonts** | Embed or replace fonts | Vloží chybějící písma, vytvoří podmnožiny nebo písmo nahradí. | PX (Replace Fonts) | P3 | vysoká | `embed fonts, subset fonts, missing fonts, replace fonts, font embedding` |

#### Review & Inspect (Teal)

| Tool | Popis na kartě | Co dělá | Kdo to má | Očekávání | Náročnost | Klíčová slova |
| --- | --- | --- | --- | --- | --- | --- |
| **Check PDF Standards** | Verify PDF/A, PDF/X or PDF/UA compliance | Kontrola shody se standardem a zpráva o nálezech. | AA, FX (Preflight), P24 (Check PDF/A) | P2 | vysoká | `preflight, validate, pdf/a check, pdf/x, pdf/ua, compliance, conformance, standards` |
| **Search in Files** | Search many PDFs at once | Hledání textu ve složce PDF souborů. | AA, FX, PP, P24 | P2 | střední | `search folder, search multiple pdfs, find in files, full text search, index` |
| **Check Accessibility** | Find problems for screen readers | Kontrola tagů, alternativních textů a pořadí čtení v dokumentu. Výpis stromu struktury umí CLI. Tool kontroluje dokument, ne aplikaci. Přístupnost rozhraní aplikace je samostatný požadavek, viz 9.7. | AA, FX, PX, NI | P3 | vysoká | `accessibility, pdf/ua, wcag, tags, screen reader, alt text, reading order, section 508` |
| **Word Count** | Count words, characters and pages | Statistika textu dokumentu. | FX, PX | P3 | nízká | `word count, character count, count words, text statistics` |

### 8.3 Funkce napříč tooly

| Funkce | Popis | Kdo to má | Očekávání |
| --- | --- | --- | --- |
| **Dávkové zpracování** | Spustit jeden tool nad mnoha soubory nebo složkou. Kandidáti: Compress, OCR, Protect with Password, Remove Hidden Data, PDF to Images, Add Watermark, Split (stejné pravidlo na každý soubor zvlášť, token `%` pro index vstupu). Až se bude dělat: stav zpracování po souborech a opakování jen neúspěšných souborů (G10, odloženo). | BB (nabídka Batch), FX, AA, PP, ST | P1 |
| **Sledovaná složka** | Automatické zpracování souborů, které se objeví ve složce. | PP, ST, AA (Distiller) | P3 |
| **Často používané** | Třetí osobní sekce vedle Pinned a Recent. | P24 | P3 |

### 8.4 Co vědomě nenavrhuji

- **AI asistent, shrnutí, překlad a chat s dokumentem.** Mají je AA, FX, SM, IL a ST. Vyžadují cloud nebo velký model, což jde proti lokální práci bez účtu.
- **Žádosti o elektronický podpis a sdílená revize.** Mají je AA, FX, PX, SM, IL a PP. Vyžadují serverovou službu.
- **Multimédia, 3D, portfolia a správa práv (RMS).** Okrajové, 3D má vlastní větev.
- **Další měřicí nástroje Bluebeamu** (poloměr, objem, počítání prvků). Specializace na stavebnictví.
- **Řetězení toolů** (AA, FX, ST, IL, SJ, PP, BB). Collections ve Figmě zůstávají skupinami toolů a sekvence spuštěné jedním krokem z nich nebudou.

---

## 9. Doporučení pro UI katalogu

Stránka `pdf4qt-tools` ve Figmě už má hledání, filtr kategorií, připnuté tooly, kolekce a naposledy použité. To odpovídá tomu, co mají PDF24, Stirling-PDF a Acrobat, a zůstává. Níže je to, co je potřeba doplnit nebo rozhodnout, až se bude Figma upravovat. Souhrn úkolů pro Figmu je v části [Úkoly do Figmy](#úkoly-do-figmy).

### 9.1 Hledání

1. **Jedno globální hledání pro všechno.** Pole *Search tools, documents, or help…* (Ctrl+K) na stránkách Home, Open a Tools hledá tooly, příkazy, nastavení, dokumenty a nápovědu. Výsledky jsou ve skupinách *Tools*, *Commands*, *Settings*, *Documents*, *Help*. Tooly mají barvu kategorie, ostatní výsledky odstín Neutral.
2. **Katalogové hledání na stránce Tools.** Druhé pole *Search tools by name, action or keyword* zůstává a zobrazuje jen tooly, filtruje mřížku katalogu. Když je aktivní filtrační chip kategorie, ukáže i počet shod v ostatních kategoriích („3 more in Optimize“). Hledání v otevřeném dokumentu má vlastní pole *Search in document…* (Ctrl+F) a s těmito dvěma se nemíchá.
3. **Ukázat, proč výsledek odpovídá.** Když rozhodlo klíčové slovo, zobrazí se pod názvem: „Merge · matches *combine*“.
4. **Plánované tooly se nezobrazují.** Tooly z kapitoly 8 nejsou v katalogu ani ve výsledcích hledání, uživatel nesmí narazit na položku, kterou nejde spustit. Prázdný výsledek nabídne nejbližší dostupné tooly.
5. **Klávesová zkratka** Ctrl+K pro globální hledání odkudkoli. Enter na výsledku typu tool otevře **stránku toolu** (9.3), nikdy rovnou nezmění dokument.
6. **Staré názvy.** Klíčová slova toolů obsahují i názvy akcí z dnešních aplikací (*Create Bitonal Document*, *United Document*, *Render to Images*, *Page Geometry* …) a názvy karet z dnešní Figmy (*Merge PDF*, *Reorganize Pages*, *Delete Pages*, *Encrypt PDF*, *Bitonal Images* …), aby je uživatelé starých aplikací našli.

### 9.2 Karta toolu a spuštění

1. **Co tool potřebuje, je vidět předem.** Tři druhy vstupu: otevřený dokument, víc souborů, nic. Soubor jde na kartu i přetáhnout.
2. **Odkud se tool spouští, určuje, nad čím poběží (E5).**
   - Z panelu Tools v dokumentu: stránka toolu se otevře rovnou nad tímto dokumentem, bez výběru souboru.
   - Ze stránky Tools (a z Home): otevře se **úvodní stránka toolu** s výběrem souboru (seznam otevřených souborů, nebo otevření dalšího), s textovým panelem s popisem toolu a odkazem na tutoriál na YouTube. Popis je malá dokumentace toolu: co tool umí, jak pracuje, s čím pracuje a co vznikne. Nejde o jednořádkový popis z karty.
   - Tooly bez vstupu (Blank PDF, Manage Certificates) výběr souboru přeskočí. Tooly s více vstupy (Merge, Compare, Interleave Pages) vybírají víc souborů (9.3).
3. **Stavové štítky** na kartě: *New*, *Beta*, *Advanced*, a důvod nedostupnosti (*No scanner found*, *Not allowed by document security*, *Document is locked*). Nedostupný tool se neschovává, vysvětlí proč (4.7).
4. **Hustota.** Čtyřicet šest karet se na jednu obrazovku nevejde. Doporučení: výchozí pohled *All* ukazuje kategorie s nadpisem a podskupinami, tooly se štítkem *Advanced* jsou až na konci kategorie. Volba v nastavení je může skrýt z mřížky úplně, hledání je ale najde dál.
5. **Barva.** Odstín kategorie nese ikona toolu (komponenta *Tool icon*) a chip kategorie. Karta sama zůstává neutrální, jinak bude stránka pestrá a nečitelná. Kontrast ikony vůči pozadí musí splnit WCAG AA v obou tématech.

### 9.3 Stránka toolu a běh toolu

Tato část shrnuje model práce se stránkou toolu (spuštění, ukládání a chyby). Ve Figmě zatím není. Pokud vznikne samostatný dokument *model práce*, část 9.3 se do něj přesune a tady zůstane odkaz.

**Stránka toolu**

1. **Každý tool otevře vlastní stránku** s dostupnými volbami, náhledem toho, co se stane, a potvrzovacím tlačítkem (C5). Spuštění z hledání ani Enter tedy nikdy rovnou nemění dokument.
2. **Cíl je vidět.** Stránka ukazuje, nad čím tool poběží („Smlouva.pdf · 3 pages“).
3. **Tool nad otevřeným dokumentem běží v kartě dokumentu.** Lišta toolu nahoře nese název a barvu kategorie, panel voleb je vpravo. V režimu toolu má hlavní lišta vlastní akce, které patří výhradně toolu. Undo a Redo tam platí jen pro tool (například vnitřní kroky Edit Content nebo Sign by Hand).
4. **Vlastní kartu mají jen tooly s více vstupy a Compare (E1).** Jsou to Merge, Interleave Pages a Manage Pages. Sestava (*Assembly*) se skupinami, kontrolními body a ukládáním existuje jen v Manage Pages (5.1). Operace nad otevřeným dokumentem (Rotate, Remove, Insert, Extract, Crop, Resize, Page Boxes, Page Numbers) novou kartu neotevírají. Vlastní kartu má také Manage Certificates, protože nepracuje s žádným dokumentem. Karty otevřených dokumentů zůstávají v pruhu karet vedle ní.
5. **Vstupy vícesouborových toolů (E4).** Merge, Compare, Interleave Pages a Insert Pages nabídnou jako vstup i otevřené taby. Stránka toolu má pro přidání vstupů jednoduché UI: seznam otevřených tabů, ze kterého se dokument přidá jedním kliknutím, tlačítko pro výběr souboru z disku a přetažení souboru. U tabu s neuloženými změnami je jasně řečeno, že se použije rozpracovaná verze.
6. **Výběr stránek** má jedna společná komponenta (9.6).
7. **Potvrzovací tlačítko je pojmenované podle výsledku** (*Rotate Pages*, *Create PDF*, *Export Images*, *Save Signed Copy*), ne jednotné *Apply* (G1). Jeho umístění je u všech toolů stejné.

**Výsledek**

8. **Stránka toolu před potvrzením řekne, jaký výsledek vznikne, a po dokončení ukáže souhrn (G2).** Čtyři typy výsledku:

   | Typ výsledku | Chování |
   | --- | --- |
   | Změna otevřeného dokumentu | Dokument je neuložený (značka na tabu dokumentu), změnu jde vrátit Undo. |
   | Nový PDF | Název a umístění jsou vidět předem, souhrn nabídne *Open result*. |
   | Více souborů | Náhled názvů souborů předem, řešení kolizí s existujícími soubory, souhrn. |
   | Analýza | Nic se neukládá. |

9. **Náhled a souhrn.** U Compress naměřená velikost před a po, u Split počet vzniklých souborů, u Redact počet označených míst. Dnešní dialogy to z části umí, sjednotit.
10. **Otevření nového souboru (G3).** Po vytvoření nového souboru (Redact, Sign, Split, Save as copy) souhrn nabídne jeho otevření v nové kartě. Otevřený výsledek se stane aktivní kartou, takže další akce (Print, E-mail z panelu File, další tool) pracují s výsledkem, ne s originálem. Po redakci tak uživatel omylem nepošle originál s citlivými daty.
11. **Jednotný dialog výsledku.** Tool, který vytváří změněný dokument, před spuštěním neřeší, kam se zapisuje. Výsledek spočítá nanečisto a po dokončení ukáže u všech toolů stejný dialog se souhrnem a čtyřmi volbami: *Keep in this tab* (výsledek nahradí dokument v tabu, zatím neuložený, jde vrátit Undo), *Save as a copy* (nový soubor, originál beze změny, volitelně otevřít kopii v nové kartě), *Overwrite the original file* (použít a rovnou uložit) a *Discard result*. Liší se jen předvybraná volba: nevratné tooly (Redact Content, Remove Hidden Data, Convert to Black & White, Flatten) mají předvybranou kopii, ostatní *Keep in this tab*. Kopii mají předvybranou i čtyři šifrovací tooly (Protect with Password, Restrict Permissions, Encrypt with Certificate, Remove Password): ztracené heslo nejde obnovit a originál má zůstat v původním stavu. Předvybraná volba nese štítek *Recommended*. Volba *Save as a copy* ukazuje vždy název souboru, složku a *Browse…*. Tooly, které dokument v kartě nemění a zapisují jen nový soubor (Sign with Certificate, Add Timestamp, Redact Content), mají v dialogu místo čtyř voleb název souboru, složku a volbu otevřít kopii v nové kartě. Existující soubor stejného názvu je stav tohoto dialogu (upozornění pod názvem a tlačítko *Replace File*), ne zvláštní okno. Přepsání originálu je vždy vědomá volba. Toto pravidlo upřesňuje bod 8: název a umístění nového souboru se zadávají až v dialogu výsledku, ne na stránce toolu. Tool, který zakládá nový dokument (Images to PDF, Scan to PDF do nového dokumentu), má v dialogu volby *Open in this tab* a *Save as a file*. Výjimkou je Blank PDF: prázdný dokument se po *Create PDF* rovnou otevře jako neuložený, bez dialogu.
    Nabídky v toolbaru toolu mají u každé položky dlaždici s ikonou nebo vzorkem (barevné režimy, režimy položek), popis pod názvem a zaškrtnutí u vybrané položky.
12. **Další tool se volí v dialogu výsledku, ne odkazem.** Tooly na sebe neodkazují (5.1), jediná cesta z toolu do jiného toolu je volba *Then continue with* v dialogu výsledku. Je to plnohodnotná volba vedle voleb pro uložení, ne klikací odkaz. Ve Figmě ji nese komponenta *Continue with* (s vnořenou komponentou *Tool choice*).
    - **Podoba.** Řádek nejvýš tří navržených toolů (ikona v barvě kategorie a název) a odkaz *Another tool…*, který otevře hledání v katalogu. Kliknutí tool vybere, druhé kliknutí výběr zruší. Vybraný může být nejvýš jeden. Pod řádkem je věta, co se stane („Add Page Numbers starts on the new document after Merge finishes successfully.“, jinak „Nothing is chosen – the tool just finishes.“).
    - **Nic se nepředvybírá.** Dialog se vždy otevře bez vybraného toolu. Když uživatel žádný tool výslovně nevybere, nic dalšího se nespustí.
    - **Spuštění.** Vybraný tool se spustí až po úspěšném dokončení tohoto toolu, tedy po potvrzení dialogu a případném zápisu souboru, a poběží nad výsledkem. Když zápis selže nebo uživatel zvolí *Discard result*, další tool se nespustí. Potvrzovací tlačítko to říká názvem (*Open and Continue*, *Save Copy and Continue*).
    - **Kde volba není.** Souhrn výstupu do více souborů a zastavený nebo částečný výsledek volbu nemají, protože není jeden výsledek, nad kterým by další tool běžel. Volbu nemají ani šifrovací tooly, Sign with Certificate a Add Timestamp: další úprava by zašifrovaný výsledek znovu otevírala a podepsaný dokument by zneplatnila.
    - **Co se navrhuje.** Aplikace si pro každý tool A pamatuje, kolikrát po něm uživatel pustil tool B (počitadlo dvojice A → B). Navrhují se tři tooly s nejvyšším počitadlem. Při shodě má přednost dvojice použitá naposledy, pak pořadí v katalogu. Tool, který nad výsledkem nejde spustit (zamčený dokument, chybějící skener nebo Tesseract), se nenavrhuje a na jeho místo postoupí další v pořadí.
    - **Výchozí návrhy.** Každý tool má v registru nejvýš tři výchozí následníky (po OCR Compress, po Merge Add Page Numbers). Do počitadel vstupují s počáteční hodnotou 2, takže je uživatelská dvojice vytlačí, jakmile je použita třikrát. Uživatelská dvojice se začne navrhovat od druhého použití, aby se do návrhů nedostal jednorázový pokus.
    - **Co se počítá jako použití.** Tool B spuštěný volbou *Continue with* po úspěšném dokončení A, nebo tool B spuštěný ručně jako první další tool nad výsledkem A ve stejné relaci. Zahozený výsledek a nedokončený tool se nepočítají.
    - **Stárnutí.** Když součet počitadel všech dvojic jednoho toolu A překročí 50, všechna jeho počitadla (včetně výchozích) se vydělí dvěma a zaokrouhlí dolů. Dvojice, která klesne na nulu, se zahodí. Novější zvyky tak postupně převáží starší.
    - **Soukromí a nastavení.** Počitadla jsou jen lokální, ukládají se pouze identifikátory toolů a čísla, žádné názvy souborů. Stránka Settings má přepínač *Suggest tools I often use next* (při vypnutí se nabízejí jen výchozí návrhy a nic se nepočítá) a tlačítko *Clear history*.

    Volba je sekundární vstup do toolu (zásada 6).

**Zrušení, průběh a chyby**

13. **Cancel a Esc** na stránce toolu ji zavřou bez změny dokumentu. Během běhu zruší běžící úlohu. Dříve potvrzené změny neruší.
14. **Zavření stránky toolu** vrátí stránku, zoom a výběr do stavu před jeho spuštěním (E6).
15. **Dlouhé operace běží na pozadí** s průběhem a možností zrušit (OCR, Compress, Merge velkých souborů). Průběh je vidět na stránce toolu i na tabu dokumentu. Se zpracovávaným dokumentem se během běhu nesmí dělat nic (žádné úpravy ani další tooly), ostatní karty jsou volné. Zavření karty nebo ukončení aplikace během běhu se zeptá (G5).
16. **Chyba zápisu** (plný disk, zamčený soubor) se zobrazí přímo v dialogu výsledku u volby, která selhala (pole se jménem souboru, popis chyby, *Choose location…*). Výsledek ani nastavení toolu se neztrácí, takže jde uložit jinam nebo akci zopakovat (G7).
17. **Automatic refresh** v panelu File nesmí zahodit neuložené změny. Když se soubor na disku změní a dokument má neuložené změny, aplikace se zeptá (G6).
18. **Stop a Cancel mají různý význam.** *Stop* ukončí běžící operaci a nechá stránku toolu otevřenou. Nese průběh („Stop (412/1,592)“) a je vidět jen během běhu. *Cancel* zavře tool. Průběh je u všech toolů na stejném místě, v kartě nahoře v pravém panelu. Stavový řádek říká, co po zastavení zůstane (zapsané soubory, naskenované stránky, nebo nic).
19. **Export do souborů.** Tool, který zapisuje jeden soubor vedle dokumentu (Extract Text, Create Audio Book), se na jméno souboru ptá až v dialogu výsledku. Tool, který zapisuje víc souborů (PDF to Images, Extract Images), má cílovou složku, šablonu názvu s náhledem jmen a volbu pro existující soubory (*Ask*, *Overwrite*, *Skip*, *Rename*) v pravém panelu před spuštěním. Končí souhrnem s *Open Folder*. Zastavený nebo částečně neúspěšný export má vlastní stav téhož souhrnu.
20. **Nastavení je na stránce toolu jen jednou.** Co se vejde do toolbaru, je v toolbaru. Pravý panel nese jen to, co se do nabídky nevejde (číselné hodnoty, jména souborů, vlastní rozměr), a přehled. Různé úkony mají různé ikony, stejnou ikonu smí sdílet jen stejné nebo příbuzné úkony (například všechny obrazové formáty).

### 9.4 Místa, kde se dnes uživatel splete

1. **Otočení pohledu a otočení stránek.** *View → Rotate* se do souboru neuloží, *Rotate Pages* ano. Rozdíl nese název a popis karty toolu (*Turn pages in 90° steps*, klíčová slova *permanent*, *save rotation*), panel View se kvůli tomu nemění.
2. **Barevné režimy pohledu a převod barev.** *Grayscale* v panelu View dokument nemění, *Convert Images to Grayscale* ano. Stejně jako u otočení to řeší název a popis karty, ne odkaz v panelu View.
3. **Anotace a obsah stránky.** Tvar nakreslený přes Annotate je komentář, tvar z *Edit Content* je trvalý. Nástrojová lišta má ukazovat, kam se kreslí.
4. **Bookmarks a Outline.** Osobní záložky proti osnově dokumentu. Doporučení: v UI používat *Outline* pro osnovu a *My Bookmarks* pro osobní záložky.
5. **Tři druhy podpisu.** Digitální podpis certifikátem, časové razítko a nakreslený podpis mají různou právní váhu. Karta *Sign by Hand* má popis „Draw, type or insert a signature“ a stránka toolu větu „Not a digital ID signature – use Sign with Certificate.“
6. **Heslo pro otevření a heslo vlastníka.** Dva tooly místo jednoho dialogu to oddělují. Tool *Restrict Permissions* má upozornit, že omezení nejsou silná ochrana.

### 9.5 Rozšiřitelnost

Tool má být popsán daty. Stejně to má registr toolů ve Stirling-PDF. Pravidla z tohoto dokumentu se tak promítnou do registru toolů a nebudou se řešit případ od případu:

- ID, název, popis na kartě, klíčová slova, kategorie, podskupina, ikona, stav (například beta),
- počet a typy vstupních souborů (žádný, otevřený dokument, víc souborů),
- typ výsledku: změna otevřeného dokumentu, nový PDF, více souborů, analýza (9.3),
- typ pracovní karty: karta dokumentu, nebo vlastní karta (Merge, Interleave Pages, sestava Manage Pages, Compare) (9.3),
- nejvýš tři výchozí následníci pro volbu *Then continue with* po dokončení (9.3 bod 12), které postupně nahradí tooly podle skutečného používání,
- chování u zamčeného dokumentu: dostupný, nedostupný, dostupný s omezením (4.7),
- závislost na externí komponentě (Tesseract, skener), ze které plyne štítek nedostupnosti,
- zda jde spustit dávkově,
- krátká dokumentace a odkaz na tutoriál pro úvodní stránku toolu (9.2).

Pluginy v nové aplikaci nebudou. Všechny tooly jsou vestavěné a registr je jediný zdroj katalogu.

### 9.6 Výběr stránek

Všechny tooly, které pracují s částí dokumentu, používají **jednu společnou komponentu pro výběr stránek** (E3). Dnes má každý dialog vlastní výběr.

- Volby: aktuální stránka, vybrané stránky (z panelu Thumbnails), vše, rozsah (`1-3, 8, 10-12`). K tomu filtry sudé, liché, na výšku, na šířku.
- Komponenta vždy ukazuje počet vybraných stránek.
- Rozsahy se počítají ve fyzickém pořadí stránek. Popisky stránek se zobrazují vedle čísla („7 (iii)“).
- Výběr „viditelné stránky“ se při otevření toolu zafixuje, posun dokumentu ho už nemění.
- Výběr v panelu Thumbnails se při otevření toolu převezme jako předvolený rozsah (5.1).

Komponenta patří do knihovny komponent ve Figmě (*Components*).

### 9.7 Ovládání klávesnicí a přístupnost

Celý základní postup musí jít ovládat klávesnicí (I1). Přístupnost rozhraní aplikace je samostatný požadavek, nezávislý na budoucím toolu Check Accessibility (8.2), který kontroluje dokumenty. Přístupné názvy ikonových tlačítek řeší tooltip.

**Oblasti a pohyb mezi nimi.** Okno má pevné oblasti: lišta tabů, hlavní lišta, postranní panel, stránka dokumentu (nebo stránka toolu) a stavový řádek. F6 přesune fokus do další oblasti, Shift+F6 do předchozí. Je to zavedená konvence Windows (prohlížeče, Office). Uvnitř oblasti se Tab a Shift+Tab pohybují mezi skupinami ovládacích prvků. Uvnitř skupiny (tlačítka lišty, záložky postranního panelu, mřížka karet, náhledy stránek, výsledky hledání) se pohybuje šipkami a každá skupina má v pořadí Tab jen jednu zastávku. Pořadí Tab odpovídá vizuálnímu pořadí: zleva doprava, shora dolů.

**Taby.** Ctrl+Tab a Ctrl+Shift+Tab přepínají taby, Ctrl+W zavře aktivní tab.

**Rozbalovací panely hlavní lišty (File, View, Insert, Tools, režim výběru).** Enter, mezerník nebo šipka dolů panel otevře a fokus přejde na první položku. Šipky se pohybují mezi položkami, Enter položku spustí. Esc panel zavře a fokus vrátí na tlačítko, které ho otevřelo.

**Esc postupně ruší, co je navrchu:** otevřený panel nebo nabídku, potom aktivní režim myši (anotace, Redact, Measure), potom stránku toolu. Stránka toolu se zavře bez změny dokumentu a vrátí stránku, zoom a výběr (9.3).

**Návrat fokusu.** Po zavření panelu, nabídky, dialogu nebo stránky toolu se fokus vrátí na prvek, který je otevřel. Pokud už neexistuje, vrátí se na stránku dokumentu. Fokus nikdy nezůstane „nikde“.

**Viditelnost fokusu.** Fokus je vždy vidět (zřetelný rámeček v obou tématech) a nesmí ho zakrýt plovoucí lišta režimu myši ani rozbalovací panel. Prvek s fokusem se při přesunu posune do viditelné oblasti.

**Hledání.** Ctrl+K přesune fokus do globálního hledání, Ctrl+F do hledání v dokumentu. Šipky procházejí výsledky, Enter otevře zvýrazněný výsledek (u toolu jeho stránku), Esc hledání zavře a vrátí fokus.

**Stránka Tools.** Šipky se pohybují po mřížce karet, Enter otevře stránku toolu. Připnutí a odepnutí jde z kontextové nabídky (klávesa nabídky nebo Shift+F10).

**Náhledy stránek (Thumbnails, Assembly).** Šipky přesouvají fokus mezi náhledy, Shift+šipky rozšiřují výběr, Ctrl+mezerník přidá nebo odebere stránku z výběru, Ctrl+A vybere vše. Enter přejde na stránku v dokumentu. Del odstraní výběr (u zamčeného dokumentu ne, 4.7). Klávesa nabídky nebo Shift+F10 otevře kontextovou nabídku s operacemi nad výběrem (5.1).

**Přesun stránek bez tažení.** Alt+šipka posune vybrané stránky o jednu pozici, Alt+Home a Alt+End na začátek a na konec. Ctrl+X a Ctrl+V přesunou výběr za stránku s fokusem. Kontextová nabídka má *Move to…* s dialogem pro zadání cílové pozice („před stránku 12“), což funguje i pro myš bez tažení.

**Anotace a obsah stránky.** Vytvoření nového tvaru zůstává pro myš. Existující anotaci jde vybrat klávesnicí, posunout šipkami (se Shift o větší krok) a přesně nastavit v dialogu geometrie. To vše dnes už umí `PDFWidgetAnnotationManager` a `PDFAnnotationGeometryDialog`, jen se to musí zachovat. Esc režim ukončí (nápovědu ve stavovém řádku Figma už má).

**Stránka toolu.** Po otevření je fokus na prvním prvku, který vyžaduje rozhodnutí (výběr souboru, jinak první volba). Potvrzovací tlačítko je výchozí, takže ho spustí Ctrl+Enter odkudkoli ze stránky. Prostý Enter v textovém poli tool nespustí, aby nedošlo k nechtěné změně.

**Seznam zkratek.** Všechny zkratky jsou na stránce Settings → Shortcuts a jdou změnit. Tooltip tlačítka ukazuje i jeho zkratku.

---

## 10. Rozdíly proti současné Figmě a rozhodnutí

Stránka `pdf4qt-tools` má dnes 29 toolů v šesti kategoriích. Tento dokument jich navrhuje 46 ve stejném počtu kategorií. Odstíny zůstávají stejné až na prohození u Edit a Optimize. Úpravy Figmy jsou souhrnně v části [Úkoly do Figmy](#úkoly-do-figmy) a zatím nejsou provedené.

### 10.1 Kategorie

| Figma dnes | Návrh | Odstín |
| --- | --- | --- |
| Pages | Organize Pages (chip Pages) | Amber, beze změny |
| Convert | Create & Convert (chip Convert) | Blue, beze změny |
| Edit & Forms | Edit (chip Edit) | **Green**, ve Figmě je dnes Violet |
| Security | Protect & Sign (chip Protect & Sign) | Red, beze změny |
| Optimize | Optimize (chip Optimize), beze změny názvu | **Violet**, ve Figmě je dnes Green |
| Review | Review & Inspect (chip Review) | Teal, beze změny |

### 10.2 Tooly

| Figma dnes | Návrh |
| --- | --- |
| Merge PDF, Split PDF | **Merge**, **Split** |
| Insert Pages, Rotate Pages | beze změny, z Insert Pages je navíc vyčleněn **Add Image as Page** |
| Reorganize Pages | **Manage Pages** |
| Delete Pages | **Remove Pages** (*delete* zůstává v klíčových slovech) |
| Create PDF | rozděleno na **Images to PDF** a **Blank PDF** |
| PDF to Images, Extract Text, Extract Images | beze změny |
| OCR | **Recognize Text (OCR)** |
| Edit Content | beze změny, nově zahrnuje i přidání textu a obrázku |
| Annotate, Fill Forms, Attachments | **vyřazeno z katalogu**, zůstává v dokumentu: položka Annotate, pole formuláře, panel Attachments |
| Encrypt PDF | rozděleno na **Protect with Password**, **Restrict Permissions**, **Encrypt with Certificate** |
| Remove Password, Redact Content | beze změny |
| Verify Signatures | **vyřazeno z katalogu**, ověření při otevření a panel Signatures |
| Sign Document | rozděleno na **Sign with Certificate**, **Add Timestamp**, **Sign by Hand** |
| Sanitize | **Remove Hidden Data** (*sanitize* zůstává v klíčových slovech), i na dlaždici na Home |
| Compress PDF | **Compress**, nově zastřešuje Optimize Images a Optimize Structure |
| Bitonal Images | **Convert to Black & White** |
| Remove External Links | přesun z Optimize do **Protect & Sign** |
| Compare PDFs | **Compare** |
| Measure, Document Statistics | beze změny |
| Inspect Fonts, Read Aloud | **vyřazeno z katalogu**, zůstává v obrazovce Properties a v panelu Speech |

Nové karty, které Figma nemá: Extract Pages, Interleave Pages, Add Image as Page, Crop Pages, Resize Pages, Set Page Boxes, Scan to PDF, Create Audio Book, Add Page Numbers, Manage Certificates, Optimize Images, Optimize Structure, Convert Images to Grayscale, Output Preview, Ink Coverage, Soft Proofing, Object Inspector, Document Report.

### 10.3 Rozhodnuto

1. **Co je tool.** Tool má vlastní obrazovku nebo dialog s výsledkem, nebo jde spustit bez dokumentu. Anotace, vyplňování formulářů, osnova, přílohy, čtení nahlas, ověření podpisů, seznam písem a výběr tabulky v katalogu nejsou.
2. **Výjimky.** Measure, Redact Content, Sign by Hand a Soft Proofing zůstávají tooly.
3. **Názvy bez „PDF“.** Compress, Merge, Split a Compare. „PDF“ zůstává jen u převodů.
4. **Barvy.** Edit je Green, Optimize je Violet.
5. **Klíčová slova vyřazených položek** se nikam nepřenášejí.
6. **Kategorie Edit zůstává malá.** Má dva tooly a naplní ji až návrhy z kapitoly 8 (vodoznak, záhlaví a zápatí, Flatten, tvorba formulářů). S jinou kategorií se neslučuje.
7. **Šest kategorií.** Kategorie odpovídají úkolům uživatele a rešerši (zásada 4, část 7.2), ne počtu barev. Třídění karet s uživateli se nedělá. Podpisy, tisková příprava a vývojářské nástroje zůstávají podskupinami. Nové odstíny se nepřidávají.
8. **Remove Hidden Data.** Dnešní *Sanitize* se přejmenovává na *Remove Hidden Data*, protože název má říkat úkol. *Sanitize* zůstává v klíčových slovech.
9. **Sign by Hand.** Nahrazuje dnešní *Sign Electronically*, které se plete s digitálním podpisem. Popis karty „Draw, type or insert a signature“.
10. **Zámek místo Vieweru.** Dokument jde zamknout ikonkou zámku a pro zamčený dokument jsou dostupné pouze tooly, které ho nemění. Ikonky pro úpravy jsou zakázané, místo nedostupného toolu jde dokument odemknout nebo použít *Edit a Copy*. Oprávnění PDF se nově neřeší. OCR zůstává pro zamčený dokument nedostupné i v režimu „jen export textu“. Podpis zůstává nedostupný, protože je z pohledu uživatele úpravou dokumentu. Seznam je v části 4.7.

Rozhodnutí z triáže UX revize (3. 10. 2026, [tools_review_triage.md](tools_review_triage.md)):

11. **Kategorie *Optimize*** zůstává pod tímto názvem, dokud nebude existovat Repair PDF (D3). Chip kategorie 4 je *Protect & Sign* (D5).
12. **Add Image as Page** je samostatný vstup, předvolba Insert Pages. Katalog má 46 toolů (C8).
13. **Convert Images to Grayscale** místo Convert to Grayscale, dokud chybí Convert Colors (D7).
14. **Popis na kartě** má nejvýš dva řádky karty, ne 45 znaků (D9).
15. **Sekundární vstupy** do toolů mimo mřížku jsou povolené a nesou barvu primární kategorie (D2).
16. **Assembly.** Pracovní plocha PageMasteru se jmenuje *Assembly*, *Workspaces* na stránce Open jsou uložené sady otevřených dokumentů. Soubory `.pagemaster` bez zpětné kompatibility (A6).
17. **Pracovní karty.** Operace nad otevřeným dokumentem běží v jeho kartě, vlastní kartu má jen sestava z více zdrojů a Compare (E1). Panel Thumbnails má operace nad výběrem a *Extract to New Document* (E2).
18. **Stránka toolu.** Každý tool otevře stránku s volbami, náhledem a potvrzovacím tlačítkem pojmenovaným podle výsledku. Spuštění ze stránky Tools vede na úvodní stránku s výběrem souboru, dokumentací a odkazem na YouTube (C5, E5, G1). V režimu toolu platí Undo a Redo v hlavní liště jen pro tool (G4).
19. **Hledání.** Globální a katalogové hledání na stránce Tools zůstávají obě (C4). Plánované tooly se nezobrazují (C7).
20. **Výsledek.** Neuložené změny mají značku na tabu, nový soubor jde otevřít v nové kartě, která se stane aktivní (G2, G3). Se zpracovávaným dokumentem se během dlouhé operace nedá nic dělat (G5).
21. **Lišty a plovoucí panely** se nenahrazují. Plochu stránky ovládá skrytí postranního panelu, splitter, celá obrazovka a nastavení *Show sidebar when opening documents* (A1).
22. **Collections** zůstávají skupinami toolů, řetězení toolů se nenavrhuje (J2). Dávkové zpracování je odložené (G10).
23. **Pluginy** v nové aplikaci nebudou, stránka nastavení *Plugins* zaniká (K2).
24. **Ovládání klávesnicí** podle části 9.7 (I1). Přístupné názvy ikonových tlačítek řeší tooltip (I2).

Rozhodnutí z návrhu stránek toolů Organize Pages (4. 10. 2026):

25. **Sestava jen v Manage Pages.** Skupiny, kontrolní body a ukládání a načítání sestavy má jen Manage Pages. Merge a Interleave Pages mají vlastní kartu bez sestavy, Split pracuje s jedním dokumentem. Tím se upřesňují body 16 a 17.
26. **Tooly na sebe neodkazují.** Zmínka jiného toolu v textu je v pořádku, klikací odkaz ani položka nabídky ne. Další tool se volí volbou *Then continue with* v dialogu výsledku a spustí se až po úspěšném dokončení (9.3 bod 12). Nic se nepředvybírá. Nabízejí se nejvýš tři tooly: nejdřív výchozí z registru, postupně je vytlačí tooly, které uživatel po daném toolu skutečně pouští. Počitadla dvojic stárnou dělením dvěma po překročení 50 použití.
27. **Split více souborů najednou** se odkládá do dávkového zpracování (8.3). Dělení sestavy podle pravidla nahrazují skupiny a výstup *One PDF per group or item* v Manage Pages, dělení sestavy podle velikosti souboru se nenabízí.
28. **Tokeny názvů souborů** jsou ve všech toolech stejné: `#` číslo výstupu, `@` číslo stránky, `%` index vstupu.
29. **Každá ikona má jediný význam.** Pro nový význam se kreslí nová ikona.
30. **Šifrovací tooly.** Čtyři tooly sdílejí jednu obrazovku s jinou předvolbou. Protect with Password ponechá existující omezení dokumentu, pokud je uživatel nezmění v Restrict Permissions. Heslo se zadává dvakrát a slabé heslo jen varuje, potvrzení neblokuje. Sílu hesla ukazuje komponenta *Strength meter*: souvislý pruh, slovní úroveň (*Very weak* až *Very strong*), naměřená entropie („92 of 128 bits“) a štítky kritérií, která heslo splňuje (*12+ characters*, *Aa*, *123*, *#$&*). Stejná komponenta bez entropie a štítků ukazuje sílu algoritmu. Nahrazuje dnešních pět barevných značek. Když je dokument otevřený jen heslem pro otevření, tool si nejdřív vyžádá heslo vlastníka. Encrypt with Certificate šifruje pro vlastní certifikát uživatele se soukromým klíčem.
31. **Podpis.** Sign with Certificate a Add Timestamp zapisují nový soubor a otevřený dokument nemění. Když uživatel nemá certifikát, stránka toolu nabídne *Create Certificate…* a *Import .pfx…* přímo, správce certifikátů se neotevírá. Potvrzovací tlačítko Sign by Hand je *Apply to Pages*, dotaz „Document will be signed electronically“ nahrazuje dialog výsledku.
32. **Redact Content.** Vlastní barva výplně se vybírá z palety barev anotací, výchozí je černá. Při zavření toolu se značkami se aplikace zeptá, zda značky v dokumentu ponechat. Hledání pro redakci je součástí toolu (levý panel), ne samostatný panel pokročilého hledání.
33. **Remove Hidden Data** má předvolby *Standard* (vše kromě neviditelného textu, odpovídá dnešnímu výchozímu stavu), *Before sending*, *Before publishing* a *Everything*.
34. **Prohlížecí tooly bez potvrzovacího tlačítka.** Output Preview, Document Statistics a Object Inspector jen zobrazují, takže mají v liště toolu pouze *Close*. Výjimka z bodu 7 v §9.3. Compare (*Compare*), Ink Coverage (*Calculate Coverage*), Document Report (*Create Report*), Measure (*Save as Annotations*) a Soft Proofing (*Keep Proofing On*) potvrzovací tlačítko mají. Exporty analytických toolů (zpráva o porovnání, XML rozdílů, CSV měření, tabulka krytí, statistika, zpráva o dokumentu) se na soubor ptají ve stejném dialogu jako tooly zapisující nový soubor; existující soubor a chyba zápisu jsou stavy tohoto dialogu.
35. **Measure.** *Save as Annotations* přidá měření do dokumentu v kartě jako měřicí anotace bez dialogu výsledku (dokument je neuložený, Undo anotace vrátí). Je to stejná povaha změny jako anotování, proto výjimka z bodu 11 v §9.3. Režim *Annotations in the document* v nabídce zůstává (měření se ukládají hned). Kalibrace je jen v nabídce *Scale*. Jednotky, písmo a barvy popisků jsou v pravém panelu, protože se do nabídky toolbaru nevejdou (výjimka z bodu 20). Místo zprávy „The document does not contain any measurement.“ je tlačítko *Export CSV* zakázané. Jednotlivé dočasné měření jde nově odebrat v seznamu.
36. **Výběr barvy.** Barva se vybírá kulatými vzorky (komponenta *Color swatch*) a paleta je přímo v pravém panelu, pokud je tam místo. Rozklikávací okno se pro výběr barvy nepoužívá. Poslední vzorek *+* otevře vlastní barvu.
37. **Soft Proofing.** *Keep Proofing On* nechá náhled tiskových barev zapnutý i po zavření toolu, stavový řádek dokumentu to ukazuje položkou „Soft proof: profil“. *Close* náhled vypne. Nabídka profilů obsahuje i výstupní záměr dokumentu. Chybějící profil a barevný systém Generic mají vlastní stav s vysvětlením (dnes se přepínače tiše neprojeví).
38. **Compare.** Dokumenty se jmenují *Original* a *Revised*. Volby porovnání jsou v nabídce *Options* a jejich změna se projeví až po *Compare Again*. Druh rozdílu má vedle barvy i znak (−, +, ≠, ↔). Zkratky zůstávají: F5 porovnat, F6 další rozdíl, Shift+F6 předchozí. Chybový text rozsahu stránek je přepsaný srozumitelně („The first page (30) is after the last page (12).“) a zobrazuje se u pole.
39. **Document Report.** Sekce: základní informace, otisky souboru, metadata (XMP), JavaScript, pojmenované cíle, rámce stránek, strom struktury, barvy a písma (včetně náhrad a volitelných map znaků). Výběr stránek platí pro JavaScript, cíle a rámce stránek. Soubor se zapisuje vždy v UTF-8, volba `--text-codec` zůstává jen v příkazové řádce. *Structure as XML* má čtyři volby exportu proudů a řetězců.
40. **Nové proti dnešku v Review & Inspect:** výběr stránek, průběh se *Stop* a export tabulky v Ink Coverage, export statistik, předvolby barvy papíru v Output Preview, zobrazení chyb vykreslení (dnes se zahazují), upozornění na limit 26 přímých barev.

Otevřené otázky ke katalogu nezbývají.
