# Triáž UX revize katalogu nástrojů

Podklad: [tools.md](tools.md) (2. 10. 2026), [tools_review.md](tools_review.md) (3. 10. 2026) a Figma `PDF4QT` (stránky *Main page*, *View Document*, *Components*). Datum: 3. 10. 2026. Soubor `tools.md` se tímto dokumentem nemění.

Každý řádek je jeden návrh úpravy. Sloupec **Hodnocení** je můj názor, sloupec **Rozhodnutí** je pro tebe. „Přijato podle doporučení“ znamená, že bod jsme výslovně neprobírali a rozhodnutí přebírá moje hodnocení. Takový bod jde kdykoli změnit.

- **R** – relevantní, doporučuji přijmout.
- **Č** – relevantní z části, doporučuji přijmout v upravené podobě (jak je popsáno).
- **N** – nedoporučuji, nebo to už řeší Figma či `tools.md`.

Sloupec **Kam** říká, kam úprava patří: `tools.md`, Figma, nebo samostatný dokument *model práce* (pravidla spuštění, ukládání a chyb, která se netýkají jen katalogu).

Body označené **(kód)** jsem ověřil ve zdrojácích. Body označené **(navíc)** v revizi nejsou. Body A2, A3, A4, A7, A8, B2, C1, C2, F5, F6, G9, H1, H2, I3, I4, I5, I6, I7, J4, J5 a K1 jsou po tvém rozhodnutí vypuštěné, ostatní čísla zůstávají kvůli odkazům.

---

## Co už Figma řeší

Revize Figmu neposuzovala, takže část jejích připomínek je už vyřešená. Pro posouzení bodů níže jsou podstatné tyto obrazovky:

| Oblast | Ve Figmě | Rámec |
| --- | --- | --- |
| Karty | Karta *Home* a karta pro každý dokument, tlačítko nové karty. Soubory se otevírají každý ve vlastní kartě. | všechny, `pdf4qt-open` |
| Úvodní stránka | *Quick Actions*: Open PDF, Create PDF, Convert PDF, OCR, Edit PDF, **Forms** (Fill in PDF forms), Manage, Compress, Protect, **Sign** (sign, timestamp and verify signatures), Sanitize, Measure, All Tools. K tomu Recent Files a Recent Tools. | `pdf4qt-home` |
| Globální hledání | Pole *Search tools, documents, or help…* na stránkách Home, Open a Tools. | `pdf4qt-home`, `pdf4qt-tools` |
| Stránka Tools | Zatím 29 toolů ve starých kategoriích (Pages, Convert, Edit & Forms, Security, Optimize, Review). Druhé pole *Search tools by name, action or keyword*, filtrační chipy, Pinned, Collections, Recent tools. | `pdf4qt-tools` |
| Lišta dokumentu | Jedna pevná řada: přepínač postranního panelu, Save, Undo, Redo, File, View, Insert, Go to, Tools, navigace stránek, zoom, režim výběru, *Search in document…* | `pdf4qt-document` |
| Panel Tools v dokumentu | Rozbalovací panel s hledáním *Search tools and actions…*, Pinned, Recent a odkazem *All tools*. | `pdf4qt-document-panel-tools` |
| Panel File | Save, Save as, Print, E-mail, Properties, Automatic refresh, **Settings… Ctrl+K**, Close Ctrl+W. | `pdf4qt-document-panel-file` |
| Panel View | Rozvržení stránek, barevné režimy, Fit, otočení pohledu, Magnifier, Full screen, Show annotations, Rendering settings. | `pdf4qt-document-panel-view` |
| Panel Insert | Všechny anotace (zvýraznění, Text box, poznámky, tvary, odkazy, razítka), **Page numbers**, Outline item, Delete annotations. | `pdf4qt-document-panel-insert` |
| Režim výběru | Select text, Select table, Screenshot, Extract image. | `pdf4qt-document-select-menu` |
| Postranní panel | Záložky Outline, Thumbnails, Layers, Attachments, Speech, Signatures, Bookmarks, Notes. Jde skrýt. | `pdf4qt-document-*`, `pdf4qt-document-sidebar-hidden` |
| Hledání v dokumentu | Výsledky v postranním panelu, volby Match case, Whole words, Regular expression, Wildcards. | `pdf4qt-document-search` |
| Aktivní režim myši | Plovoucí lišta s volbami a křížkem, ve stavovém řádku nápověda „Press Esc to finish“. | `pdf4qt-document-tool-active`, `-tool-shape` |
| Podpisy | Souhrn („One signature needs attention“), stavy *Valid* a *Needs attention*, řetěz certifikátů, *Add to trusted certificates*. | `pdf4qt-document-signatures` |
| Vlastnosti | Samostatná obrazovka s *Back to document*. Část Security ukazuje šifrování, oprávnění a upozornění na otevření heslem uživatele. | `pdf4qt-document-properties-security` |
| Open | Recent Folders, Pinned Files, Recent Files (stav *No longer exists*), **Workspaces** (uložené sady otevřených dokumentů, *Restore last session*, *Save open documents as workspace…*), Pinned Locations. | `pdf4qt-open` |
| Nastavení | General (mimo jiné *Show sidebar when opening documents*), Viewing, Color modes, Editing (mimo jiné *Highlight form fields*), Security, Speech, Shortcuts, Color management, Advanced. Stránka Plugins není, pluginy v nové aplikaci nebudou. | `pdf4qt-settings-*` |

Ve Figmě zatím **chybí**: stránka toolu s výběrem souboru, volbami, náhledem a potvrzením (C5, E5), pracovní plocha Manage Pages, Compare, operace se stránkami v panelu Thumbnails, označení neuloženého dokumentu na kartě a režim jen pro čtení.

---

## A. Zachování dnešních funkcí

| # | Návrh úpravy | Kam | Hodnocení | Proč | Rozhodnutí |
| --- | --- | --- | --- | --- | --- |
| A1 | V §6.2 nahradit „Zaniká, nové UI má pevné rozvržení“ textem: „Lišty v novém UI nejsou. Kontrolu nad plochou stránky zachovává tlačítko pro skrytí postranního panelu, splitter pro změnu jeho šířky, celá obrazovka a nastavení *Show sidebar when opening documents*. Přesouvání lišt a odpojování panelů do plovoucích oken se vědomě nenahrazuje.“ Stejně zmapovat přepínače z Diffu (§6.3) a Editoru (§6.1). | §6.1, §6.2, §6.3 | **Č** | Postranní panel jde ve Figmě skrýt tlačítkem v liště (`pdf4qt-document-sidebar-hidden`) a *Full screen* je v panelu View. Šířku panelu bude měnit splitter. Nová funkce tedy není potřeba, stačí opravit text v §6. (kód) Dnes jde lišty a panely přepínat v PageMasteru ([mainwindow.cpp:1033](../Pdf4QtPageMaster/mainwindow.cpp#L1033)), v Diffu ([mainwindow.cpp:97](../Pdf4QtDiff/mainwindow.cpp#L97), [:159](../Pdf4QtDiff/mainwindow.cpp#L159)) i v Editoru ([pdfeditormainwindow.cpp:331-342](../Pdf4QtLibGui/pdfeditormainwindow.cpp#L331-L342)). | Přijato: šířku panelu mění splitter, tlačítko pro skrytí už ve Figmě je. |
| A5 | Ctrl+K zůstává globálním hledáním podle §9.1/4. Ve Figmě v panelu File odebrat u *Settings…* zkratku Ctrl+K. Nastavení zkratku nepotřebuje, je snadno dostupné z panelu File a z levé navigace. | Figma | **R** **(navíc)** | Figma je sama se sebou v rozporu: panel File přiřazuje Ctrl+K nastavení. | Přijato: Ctrl+K zůstává globálním hledáním, *Settings…* bude bez zkratky. |
| A6 | Rozlišit dva pojmy „workspace“. *Workspace* ve Figmě (stránka Open) zůstává uloženou sadou otevřených dokumentů. Pracovní plocha PageMasteru (sestava stránek z více zdrojů) se přejmenuje na *Assembly*. U Assembly určit, co dělá Ctrl+S (uloží sestavu), a odlišit *Save Assembly* od *Create PDF*. Když při otevření uložené sestavy chybí nebo se změnil zdroj, nabídnout jeho nahrazení. Zpětná kompatibilita se soubory `.pagemaster` se neřeší. | §5.1 `pages.manage`, §6.2, Figma | **R** | Při stejném názvu uživatel nepozná, jestli ukládá rozpracovanou sestavu stránek, nebo seznam otevřených karet. *Assembly* nekoliduje s *Page layout* v panelu View ani s razítkem *DRAFT* a říká, že jde o skládání z více zdrojů. | Přijato: pracovní plocha PageMasteru → *Assembly*, `.pagemaster` bez zpětné kompatibility. |

## B. Faktické opravy v tools.md

| # | Návrh úpravy | Kam | Hodnocení | Proč | Rozhodnutí |
| --- | --- | --- | --- | --- | --- |
| B1 | Scan to PDF: bez otevřeného dokumentu vytvoří nový PDF, s dokumentem připojí stránky na konec. Opravit popis, vstup a výstup. V režimu jen pro čtení povolit sken do nového dokumentu. Nabídnout dvě volby: *New document* a *Add to open document*. | §4.2, §4.7, §5.2 | **R** | (kód) [scannerplugin.cpp:78-97](../Pdf4QtEditorPlugins/ScannerPlugin/scannerplugin.cpp#L78-L97) má větev bez `m_document`, která sestaví nový dokument. | Přijato podle doporučení. |
| B3 | Crop Pages: popsat, že se nastavuje CropBox a obsah mimo ořez v souboru zůstává. Pro skutečné odstranění odkázat na Redact. | §5.1 `pages.crop` | **R** | (kód) [pdfdocumentmanipulator.cpp:304](../Pdf4QtLibCore/sources/pdfdocumentmanipulator.cpp#L304) volá `setPageCropBox`. | Přijato podle doporučení. |
| B4 | Redact: popsat dva kroky. Značky jsou anotace Redact v otevřeném dokumentu, výsledek se zapíše do nového souboru a dnes se neotevře. | §5.4 `protect.redact` | **R** | (kód) [redactplugin.cpp:126-180](../Pdf4QtEditorPlugins/RedactPlugin/redactplugin.cpp#L126-L180) přidává anotace a [:203-205](../Pdf4QtEditorPlugins/RedactPlugin/redactplugin.cpp#L203-L205) zapisuje nový soubor. | Přijato podle doporučení. |
| B5 | Sign with Certificate bez certifikátu: zapsat dnešní chování. Nejdřív se otevře správce certifikátů a potom dialog podpisu. | §5.4 `protect.sign` | **Č** | (kód) [signatureplugin.cpp:334-337](../Pdf4QtEditorPlugins/SignaturePlugin/signatureplugin.cpp#L334-L337). Požadavek revize je tím z větší části splněný. | Přijato podle doporučení. |
| B6 | Flatten a Rasterize PDF: odstranit příslib, že obsah „nejde měnit“ nebo „kopírovat“. Popsat skutečnou transformaci. | §8.2 | **R** | Formulace slibuje ochranu, kterou nástroj nedává. | Přijato podle doporučení. |
| B7 | Sign with Certificate a Add Timestamp zůstávají pro dokument jen pro čtení nedostupné, ale v §4.7 opravit zdůvodnění. Místo „cestou dokument mění (podpisové pole)“ uvést: „Podpis je z pohledu uživatele úprava dokumentu, proto vyžaduje dokument otevřený pro úpravy.“ | §4.7 | **R** **(navíc)** | (kód) Dnešní tvrzení neplatí. `PDFDocumentSigner` sestaví podepsaná data z dokumentu a originálního souboru a uloží je dialogem do nového souboru, otevřený dokument se nemění ([signatureplugin.cpp:385-545](../Pdf4QtEditorPlugins/SignaturePlugin/signatureplugin.cpp#L385-L545)). Chybné technické zdůvodnění by mohlo později vést ke zpochybnění rozhodnutí. Redact zůstává nedostupný z technického důvodu, protože značky redakce jsou anotace v otevřeném dokumentu (B4). | Přijato: podpis zůstává pro dokument jen pro čtení nedostupný, opraví se jen zdůvodnění v §4.7. |

## C. Hledání a dohledatelnost

| # | Návrh úpravy | Kam | Hodnocení | Proč | Rozhodnutí |
| --- | --- | --- | --- | --- | --- |
| C3 | Ke každému toolu přidat do klíčových slov dnešní názvy akcí (*Create Bitonal Document, United Document, Separate to Multiple Documents, Optimize, Render to Images, Encryption, Edit page content, Sign Electronically, Page Geometry* …) a názvy karet z dnešní Figmy (*Merge PDF, Reorganize Pages, Delete Pages, Encrypt PDF, Bitonal Images* …). | §5 | **R** | Uživatelé starých aplikací hledají staré názvy. | Přijato podle doporučení. |
| C4 | Na stránce Tools zůstávají obě pole a v §9.1 se popíše jejich rozdíl. Globální hledání *Search tools, documents, or help…* (Ctrl+K) hledá tooly, příkazy, nastavení, dokumenty a nápovědu. Katalogové hledání *Search tools by name, action or keyword* zobrazuje jen tooly. Když je aktivní filtrační chip, katalogové hledání ukáže i počet shod v ostatních kategoriích. | §9.1, Figma | **Č** | Rozdíl mezi globálním hledáním a hledáním v dokumentu Figma už řeší: dokument má vlastní pole *Search in document…* s výsledky v panelu. Zbývalo vysvětlit dvě pole na stránce Tools. | Přijato: obě pole zůstávají, katalogové hledání zobrazuje jen tooly. |
| C5 | Do §9.3 zapsat obecné pravidlo: každý tool otevře vlastní stránku s dostupnými volbami, náhledem toho, co se stane, a potvrzovacím tlačítkem. Spuštění z hledání ani Enter tedy nikdy rovnou nemění dokument. Remove External Links zobrazí seznam odstraňovaných odkazů se stránkou, na které jsou, a s možností jednotlivé odkazy vynechat. Odstraní se až po potvrzení. Upravit §5.4 (dnes „jeden krok bez nastavení“) a §9.1/4. | §9.1/4, §9.3, §5.4 | **R** | Podle §9.1/4 by Enter spustil první výsledek a Remove External Links dnes nemá žádný dialog. Obecné pravidlo to řeší pro všechny tooly najednou. | Přijato: každý tool otevře stránku s volbami a potvrzením. Remove External Links ukáže seznam odkazů s možností je vynechat. |
| C6 | Tooly *Advanced* skryté v nastavení zmizí jen z mřížky, hledání je najde dál. | §9.2/3 | **R** | Jednovětné upřesnění. | Přijato podle doporučení. |
| C7 | Plánované tooly se uživateli nezobrazují vůbec, ani v katalogu, ani ve výsledcích hledání. Z §9.1/3 odstranit odkaz na seznam plánovaných funkcí a připravené odpovědi pro *word*, *excel* a *watermark*. Prázdný výsledek nabídne jen nejbližší dostupné tooly. | §9.1/3 | **R** | Uživatel nesmí narazit na položku, kterou nejde spustit. | Přijato: plánované tooly se nezobrazují. |
| C8 | Z Insert Pages vyčlenit samostatný vstup *Add Image as Page*: předvolba Insert Pages, která vloží obrázek jako novou stránku na zvolené místo. Klíčové slovo `add image` mají *Add Image as Page* i Edit Content a ve výsledcích hledání je rozliší název a popis („Insert a picture as a new page“ proti „Edit or add text, images and graphics“). Katalog tím naroste na 46 toolů. | §4.1, §5.1, §5.3 | **R** **(navíc)** | Odpovídá zásadě 2 z §2 (dialog s více účely se rozpadne na víc vstupů, jako u Encryption). Název sám říká, že vzniká nová stránka, a odliší se od vložení obrázku na stránku přes Edit Content. Pozor na podobnost s *Images to PDF*, které vytvoří nový dokument. Popis karty to musí říct. | Přijato: samostatný vstup *Add Image as Page*. |

## D. Kategorie a názvy

| # | Návrh úpravy | Kam | Hodnocení | Proč | Rozhodnutí |
| --- | --- | --- | --- | --- | --- |
| D1 | Šest kategorií zdůvodnit úkoly a rešerší (5 až 8 u spotřebitelských katalogů, nejblíž iLovePDF), ne počtem barev. Ověření tříděním karet se nedělá: z §7.6 vypustit doporučení „před implementací udělat uzavřené třídění karet“. | §2/4, §7.2/2, §7.6, §10.3/7 | **Č** | Rozhodnutí zůstává, mění se jen zdůvodnění. | Přijato: zdůvodnit úkoly a rešerší, třídění karet nedělat. |
| D2 | Zásadu „každý tool v jedné kategorii“ zachovat pro mřížku. Výslovně povolit sekundární vstupy: Quick Actions na Home, panel Insert (Page numbers), panel Tools v dokumentu, další krok po dokončení toolu. Vstup nese vždy barvu primární kategorie. | §2/6 | **Č** | Figma sekundární vstupy už má. `tools.md` by je měl popsat jako pravidlo, ne je vylučovat. | Přijato podle doporučení. |
| D3 | Kategorii ponechat jako *Optimize*, jak je ve Figmě, a nepřejmenovávat ji na *Optimize & Repair*, dokud nebude existovat Repair PDF. | §3, §4.5, §10.1 | **R** | Žádný ze 45 toolů nic neopravuje. | Přijato podle doporučení. |
| D4 | Popis kategorie Review & Inspect upravit: Measure umí uložit měření jako anotace. | §3 | **R** | Popis „dokument se nemění“ dnes neplatí. | Přijato podle doporučení. |
| D5 | Chip *Protect* změnit na *Protect & Sign*, nebo ponechat dnešní *Security* z Figmy, jejíž popis „Protect, sign and clean up documents“ podpis zmiňuje. | §3, §10.1 | **Č** | Drobnost. Samotné *Protect* nenapovídá, že tam je podpis. | Přijato: chip *Protect & Sign*. |
| D6 | *Sanitize* přejmenovat na *Remove Hidden Data* a *Sanitize* nechat v klíčových slovech. Mění se tím rozhodnutí §10.3/8 a dlaždice na Home i karta v katalogu. | §4.4, §5.4, §10.3/8, Figma | **R** | Odpovídá zásadě 1 z §2 (název říká úkol). Popis ve Figmě „Remove metadata and hidden content“ ukazuje, že úkol je odstranit skrytá data. | Přijato podle doporučení. |
| D7 | *Convert to Grayscale* přejmenovat na *Convert Images to Grayscale*, dokud chybí Convert Colors. | §4.5, §5.5 | **R** | Vedle barevného režimu *Grayscale* v panelu View, který dokument nemění, je rozlišení v názvu tím důležitější. | Přijato podle doporučení. |
| D8 | *Sign by Hand* ponechat. Popis na kartě: „Draw, type or insert a signature“. Na stránce toolu jedna věta: „Not a digital ID signature – use Sign with Certificate.“ | §4.4, §5.4 | **Č** | Navrhované *Add Signature Appearance* je odbornější než dnešní název. | Přijato podle doporučení. |
| D9 | Limit 45 znaků popisu nahradit pravidlem „nejvýš dva řádky karty“. | §2/8 | **R** | Karty ve Figmě (Pinned, Quick Actions) už popis na dva řádky zalamují. Pravidlo v `tools.md` je přísnější než návrh a v češtině by vedlo ke zkratkám. | Přijato podle doporučení. |

## E. Pracovní prostředí a práce se stránkami

| # | Návrh úpravy | Kam | Hodnocení | Proč | Rozhodnutí |
| --- | --- | --- | --- | --- | --- |
| E1 | Změnit §9.3/2. Operace nad otevřeným dokumentem (Rotate, Remove, Insert, Extract, Crop, Resize, Page Boxes, Page Numbers) běží v kartě dokumentu. Vlastní karta je jen pro sestavu z více zdrojů (Merge, Interleave, Manage Pages s více zdroji, Split více vstupů) a pro Compare. | §9.3/2, Figma | **R** | Figma má kartu pro každý dokument a podle dnešního §9.3/2 by otočení tří stránek otevřelo další kartu. Pracovní plocha Manage Pages ani Compare ve Figmě zatím nejsou, takže je správný čas to rozhodnout. | Přijato: operace nad otevřeným dokumentem běží v jeho kartě, vlastní kartu má jen sestava z více zdrojů a Compare. |
| E2 | V panelu Thumbnails umožnit výběr více stránek a nad výběrem Rotate, Remove, Extract, Insert a přesun (kontextová nabídka a klávesy). Navíc akce *Extract to New Document*, která rovnou otevře stránku toolu Extract Pages s vybranými stránkami a výsledek otevře jako nový dokument v nové kartě. Původní dokument zůstane beze změny. Stránka toolu spuštěného z katalogu (například Rotate Pages) převezme výběr z Thumbnails jako předvolený rozsah stránek. | §5.1, Figma | **R** | Thumbnails ve Figmě má jen velikost náhledů a synchronizaci s aktuální stránkou. Bez operací by uživatel kvůli otočení stránky musel přes Tools. | Přijato, navíc akce *Extract to New Document* (jen kopie, originál se nemění). |
| E3 | Jedna komponenta pro výběr stránek ve všech toolech: aktuální, vybrané, vše, rozsah, plus filtry sudé, liché, na výšku a na šířku. Vždy ukazuje počet stránek. Rozsahy se počítají ve fyzickém pořadí a popisky stránek se zobrazují („7 (iii)“). Výběr „viditelné“ se při otevření toolu zafixuje. | §9 (nová podkapitola), Figma Components | **R** | Komponenta v knihovně Figmy zatím není a dnes má každý dialog vlastní výběr. | Přijato: jedna společná komponenta pro výběr stránek ve všech toolech. |
| E4 | Vícesouborové tooly (Merge, Compare, Interleave, Insert) nabídnou jako vstup i otevřené karty. Stránka toolu má pro přidání vstupů jednoduché UI: seznam otevřených karet, ze kterého se dokument přidá jedním kliknutím, tlačítko pro výběr souboru z disku a přetažení souboru. U karty s neuloženými změnami je jasně řečeno, že se použije rozpracovaná verze. Dopsat do §9.2/1 a do popisu stránky toolu (E5). | §9.2/1, §9.3 | **R** | S kartami ve Figmě je to přirozený zdroj vstupů. | Přijato: dopsat do zadání, včetně jednoduchého UI pro přidání otevřených karet. |
| E5 | Tool spuštěný z panelu Tools v dokumentu otevře stránku toolu nad tímto dokumentem, bez výběru souboru. Tool spuštěný ze stránky Tools otevře **stránku toolu s výběrem souboru**: výběr souboru (seznam otevřených souborů, nebo otevření dalšího), textový panel s popisem toolu a odkaz na tutoriál na YouTube. Popis je malá dokumentace toolu: co tool umí, jak pracuje, s čím pracuje a co vznikne. Nejde o jednořádkový popis z karty. Doplnit do §9.2 a §9.3. Na stránce toolu dále rozlišit tooly bez vstupu (Blank PDF, Manage Certificates) a tooly s více vstupy (Merge, Compare, Interleave), kde se vybírá víc souborů. Cíl je pak na stránce toolu vidět („Smlouva.pdf · 3 pages“). | §9.2, §9.3, Figma | **R** | Úvodní stránka toolu řeší nejednoznačný cíl při spuštění z karty *Home* i spuštění bez otevřeného dokumentu. Ve Figmě zatím není. | Přijato: spuštění ze stránky Tools vede na úvodní stránku toolu s výběrem souboru, popisem a odkazem na YouTube. |
| E6 | Zavření stránky toolu vrátí stránku, zoom a výběr do stavu před jeho spuštěním. | §9.3 | **Č** | Detail implementace, stačí jedna věta. | Přijato. |

## F. Dokument jen pro čtení a oprávnění

| # | Návrh úpravy | Kam | Hodnocení | Proč | Rozhodnutí |
| --- | --- | --- | --- | --- | --- |
| F1 | Režim jen pro čtení je **ikonka zámku** u dokumentu. Zamčený dokument nejde upravit, aby si ho uživatel omylem nepřepsal. Zámek zapíná a vypíná uživatel. Oprávnění PDF (heslo vlastníka) se nově neřeší, zůstává dnešní chování. Dále: (c) když soubor nebo složka nejsou zapisovatelné, blokuje se jen Save a aplikace nabídne Save As. (d) U podepsaného dokumentu aplikace před uložením změny upozorní na dopad na podpisy a nabídne inkrementální uložení, které dosavadní podpisy zachová. §4.7 přepsat podle toho. | §4.7, Figma | **Č** | Zámek je jednoduchý a srozumitelný mechanismus. Inkrementální zápis už `PDFDocumentWriter` umí (testy `UnitTestsDocumentWriter`). | Přijato v upravené podobě: ikonka zámku u tabu (dokument nejde upravit) a přepínač zámku v panelu File. Oprávnění PDF se nově neřeší. (c) Save As, (d) upozornění a inkrementální uložení. |
| F2 | V režimu čtení místo „nedostupné“ nabídnout *Edit a Copy*: nová karta s neuloženou kopií, originál zůstane zamčený. | §4.7 | **R** | Uživatel může chtít menší kopii dokumentu, který chrání před změnou. S kartami ve Figmě je kopie v nové kartě přirozená. | Přijato. |
| F3 | Povolit OCR v režimu „jen export textu“ i pro dokument jen pro čtení. | §4.7 | **R** | `tools.md` to sám navrhuje, chybí jen rozhodnutí. | Nepřijato. |
| F4 | U zamčeného dokumentu jsou ikonky pro úpravy zakázané (Save, Undo, Redo, Insert a další). Nic dalšího se nemění. | §4.7, Figma | **Č** | Figma má dnes Save, Undo a Insert vždy aktivní. Zakázané ikonky stačí. | Přijato: ikonky pro úpravy jsou zakázané, nic víc. |

## G. Spuštění, potvrzení, ukládání a chyby

Každý tool otevře vlastní stránku s volbami, náhledem a potvrzovacím tlačítkem (C5, E5). Ve Figmě zatím není. Tyto body jsou podkladem pro její návrh a patří spíš do dokumentu *model práce*, v §9.3 by zůstal odkaz.

| # | Návrh úpravy | Kam | Hodnocení | Proč | Rozhodnutí |
| --- | --- | --- | --- | --- | --- |
| G1 | Potvrzovací tlačítko stránky toolu pojmenovat podle výsledku (*Rotate Pages, Create PDF, Export Images, Save Signed Copy*) místo jednotného *Apply*. Umístění na stránce zůstane u všech toolů stejné. | §9.3/1, Figma | **R** | Doporučení NN/g, na které se `tools.md` sám odvolává v §7.6. | Přijato. |
| G2 | Stránka toolu před potvrzením řekne, jaký výsledek vznikne, a po dokončení ukáže souhrn. Čtyři typy výsledku a jejich chování: změna otevřeného dokumentu (neuložený stav a Undo), nový PDF (název a umístění předem, potom *Open result*), více souborů (náhled názvů, řešení kolizí, souhrn), analýza (nic se neukládá). Na kartě (tabu) dokumentu ve Figmě doplnit značku neuložených změn, aby uživatel věděl, že je dokument upravený a je potřeba ho uložit. | model práce, §9.5, Figma | **R** | Karty ve Figmě neuložený stav nijak nerozlišují, přitom tooly upravují dokument v paměti. | Přijato: značka neuložených změn na tabu dokumentu. |
| G3 | Po vytvoření nového souboru (Redact, Sign, Split, Save as copy) nabídne souhrn na stránce toolu jeho otevření v nové kartě. Otevřený výsledek se stane aktivní kartou, takže další akce (Print, E-mail z panelu File, další tool) pracují s výsledkem, ne s originálem. | model práce | **R** | (kód) Redact ani Sign dnes výsledek neotevřou. Po redakci tak zůstane aktivní původní dokument s citlivými daty. Kdyby ho uživatel hned poslal přes E-mail z panelu File, odešle se originál bez redakce. | Přijato. |
| G4 | Cancel a Esc na stránce toolu ji zavřou bez změny dokumentu, během běhu zruší běžící úlohu. Dříve potvrzené změny neruší. Undo v liště pojmenovává krok a patří dokumentu nebo sestavě. Vnitřní Undo scén Edit Content a Sign by Hand musí být zřetelně oddělené od Undo v liště. | model práce | **R** | Figma má Undo a Redo v liště dokumentu. Edit Content má dnes vlastní Undo vedle Undo dokumentu ([editorplugin.cpp](../Pdf4QtEditorPlugins/EditorPlugin/editorplugin.cpp)). Esc pro ukončení režimu už Figma zavádí. | Nepřijato: v režimu toolu má hlavní lišta jiné akce, které patří výhradně toolu. Undo a Redo tam tedy platí jen pro tool. |
| G5 | Během dlouhé operace (OCR, Compress, Merge) se se zpracovávaným dokumentem nesmí dělat nic (žádné úpravy ani další tooly) a ostatní karty jsou volné. Zavření karty nebo ukončení aplikace během běhu se zeptá. Průběh je vidět na stránce toolu i na kartě dokumentu. | model práce, §9.3/5 | **R** | §9.3/5 slibuje práci v jiné kartě, ale neříká, co se smí dělat se zpracovávaným dokumentem. | Přijato: se zpracovávaným dokumentem se nesmí dělat nic. |
| G6 | *Automatic refresh* nesmí zahodit neuložené změny. Zeptá se. | model práce | **R** | Přepínač je v panelu File ve Figmě. Dnešní chování ověřit. | Přijato. |
| G7 | Chyba zápisu (plný disk, zamčený soubor) se zobrazí jako chyba, volby zůstanou na stránce toolu pro opakování. | model práce | **R** | Krátké pravidlo. | Přijato. |
| G8 | Stránka toolu Merge a Insert ukáže nepodporované, zamčené a poškozené vstupy před spuštěním. Částečný výsledek je výslovně označený. | §5.1 | **R** | Dnes se takové soubory „přeskočí s upozorněním“, což uživatel snadno přehlédne. | Přijato. |
| G10 | Dávkové zpracování: stav po souborech, opakování jen neúspěšných. | §8.3 | **Č** | Platí až s dávkami. Stačí doplnit do řádku v §8.3. | Odloženo: poznamenat do §8.3, zatím nedělat. |

## H. Konkrétní tooly

| # | Návrh úpravy | Kam | Hodnocení | Proč | Rozhodnutí |
| --- | --- | --- | --- | --- | --- |
| H3 | Stav podpisů ukázat i bez otevření panelu, například položkou ve stavovém řádku vedle *rendering issues* („2 signatures · 1 needs attention“). Kliknutí otevře panel Signatures. | Figma | **Č** | Panel Signatures ve Figmě už má stavy a souhrn, který revize požaduje. Chybí jen upozornění pro uživatele, který panel neotevře. Stavový řádek má pro *rendering issues* hotový vzor. | Přijato: informace o podpisech a jejich chybách ve stavovém řádku. |
| H4 | Redact: na stránce toolu „N areas marked – not removed yet“ a primární tlačítko *Create Redacted Copy*. Při hledání „black out“ nebo „redact“ se nesmí jako první ukázat obdélník z panelu Insert. | §5.4, Figma | **R** | Záměna značky za hotovou redakci je bezpečnostní riziko. Viz B4. | Přijato. |
| H5 | Compress: na stránce toolu předvolby, skutečně naměřená výsledná velikost, stav „File cannot be reduced with these settings“. Rozpad velikosti (obrázky, písma, obsah stránek, struktura) ukazuje přímo stránka toolu; odkaz „What takes up space?“ na Document Statistics se nedělá (upraveno 3. 10. 2026 – Compress má zůstat jednoduchý a neodkazovat na jiné tooly). | §5.5 | **R** | Levné. | Přijato, bez odkazu na Document Statistics. |
| H6 | Compare: vstupy pojmenovat *Original* a *Revised* místo Left a Right, umožnit jejich prohození a výběr z otevřených karet. Odlišit stavy *not compared yet*, *no differences* a *error*. | §5.6, Figma | **R** | Compare ve Figmě zatím není, takže jde o vstup pro jeho návrh. | Přijato. |
| H7 | Interleave: na stránce toolu náhled prvních stran výsledku a vysvětlení při různém počtu stran. | §5.1 | **Č** | Vítané, ale ne nutné. | Přijato. |
| H8 | Edit Content na skenu: na stránce toolu krátká nápověda, že OCR přidá neviditelný text, ale obraz stránky se tím upravit nedá. | §5.3 | **Č** | Jedna věta na stránce toolu. | Přijato. |
| H9 | Sign with Certificate: odborné parametry (adresa TSA, důvod, kontakt) dát na stránce toolu do části *Details*. Klíčové slovo `qualified signature` ponechat. | §5.4 | **Č** | Odebrat klíčové slovo nedoporučuji. Hledání „qualified signature“ má tool najít a nic tím neslibuje. | Přijato: klíčové slovo `qualified signature` neodebírat. |
| H10 | Na stránce toolu Sanitize (*Remove Hidden Data*, D6) rychlé akce jako tlačítka, například *Before sending* a *Before publishing*. Tlačítko jen nastaví zaškrtávací volby podle předvolby, uživatel je pak může dál upravit a potvrdit. | §5.4 | **R** | Levné: předvolby jen nastavují existující volby a nic nového nedělají. Běžný uživatel nemusí procházet seznam voleb, odborník je dál vidí. | Přijato: rychlé akce jako tlačítka, která nastaví zaškrtávací volby. |

## I. Přístupnost a výchozí rozhraní

| # | Návrh úpravy | Kam | Hodnocení | Proč | Rozhodnutí |
| --- | --- | --- | --- | --- | --- |
| I1 | Celý základní postup ovladatelný klávesnicí podle návrhu v části *Ovládání klávesnicí (I1)* pod touto tabulkou. Do `tools.md` jako nová podkapitola §9. | §9 | **R** | Figma zavádí Esc a nápovědu ve stavovém řádku, pořadí fokusu a alternativu tažení zatím ne. Řada věcí už v kódu je (výběr anotací klávesnicí, *Cut* a *Paste* stránek v PageMasteru), návrh je sjednocuje. | Přijato: ovládání podle části *Ovládání klávesnicí (I1)*. |
| I2 | Přístupné názvy ikonových tlačítek a karet, oznámení průběhu a výsledku. | specifikace UI | **R** | Lišta i záložky postranního panelu ve Figmě jsou jen ikony bez popisku. | Nepřijato: řeší se tooltipem. |
| I8 | Přístupnost rozhraní je samostatný požadavek, nezávislý na budoucím toolu Check Accessibility. | §8.2, §9 | **R** | Jedna věta, která předejde záměně. | Přijato. |

### Ovládání klávesnicí (I1)

**Oblasti a pohyb mezi nimi.** Okno má pevné oblasti: lišta tabů, hlavní lišta, postranní panel, stránka dokumentu (nebo stránka toolu) a stavový řádek. F6 přesune fokus do další oblasti, Shift+F6 do předchozí. Je to zavedená konvence Windows (prohlížeče, Office). Uvnitř oblasti se Tab a Shift+Tab pohybují mezi skupinami ovládacích prvků. Uvnitř skupiny (tlačítka lišty, záložky postranního panelu, mřížka karet, náhledy stránek, výsledky hledání) se pohybuje šipkami a každá skupina má v pořadí Tab jen jednu zastávku. Pořadí Tab odpovídá vizuálnímu pořadí: zleva doprava, shora dolů.

**Taby.** Ctrl+Tab a Ctrl+Shift+Tab přepínají taby, Ctrl+W zavře aktivní tab.

**Rozbalovací panely hlavní lišty (File, View, Insert, Tools, režim výběru).** Enter, mezerník nebo šipka dolů panel otevře a fokus přejde na první položku. Šipky se pohybují mezi položkami, Enter položku spustí. Esc panel zavře a fokus vrátí na tlačítko, které ho otevřelo.

**Esc postupně ruší, co je navrchu:** otevřený panel nebo nabídku, potom aktivní režim myši (anotace, Redact, Measure), potom stránku toolu. Stránka toolu se zavře bez změny dokumentu (G4) a vrátí stránku, zoom a výběr (E6).

**Návrat fokusu.** Po zavření panelu, nabídky, dialogu nebo stránky toolu se fokus vrátí na prvek, který je otevřel. Pokud už neexistuje, vrátí se na stránku dokumentu. Fokus nikdy nezůstane „nikde“.

**Viditelnost fokusu.** Fokus je vždy vidět (zřetelný rámeček v obou tématech) a nesmí ho zakrýt plovoucí lišta režimu myši ani rozbalovací panel. Prvek s fokusem se při přesunu posune do viditelné oblasti.

**Hledání.** Ctrl+K přesune fokus do globálního hledání, Ctrl+F do hledání v dokumentu. Šipky procházejí výsledky, Enter otevře zvýrazněný výsledek (u toolu jeho stránku, C5), Esc hledání zavře a vrátí fokus.

**Stránka Tools.** Šipky se pohybují po mřížce karet, Enter otevře stránku toolu. Připnutí a odepnutí jde z kontextové nabídky (klávesa nabídky nebo Shift+F10).

**Náhledy stránek (Thumbnails, Assembly).** Šipky přesouvají fokus mezi náhledy, Shift+šipky rozšiřují výběr, Ctrl+mezerník přidá nebo odebere stránku z výběru, Ctrl+A vybere vše. Enter přejde na stránku v dokumentu. Del odstraní výběr (u zamčeného dokumentu ne, F4). Klávesa nabídky nebo Shift+F10 otevře kontextovou nabídku s operacemi z E2.

**Přesun stránek bez tažení.** Alt+šipka posune vybrané stránky o jednu pozici, Alt+Home a Alt+End na začátek a na konec. Ctrl+X a Ctrl+V přesunou výběr za stránku s fokusem. Kontextová nabídka má *Move to…* s dialogem pro zadání cílové pozice („před stránku 12“), což funguje i pro myš bez tažení.

**Anotace a obsah stránky.** Vytvoření nového tvaru zůstává pro myš. Existující anotaci jde vybrat klávesnicí, posunout šipkami (se Shift o větší krok) a přesně nastavit v dialogu geometrie. To vše dnes už umí `PDFWidgetAnnotationManager` a `PDFAnnotationGeometryDialog`, jen se to musí zachovat. Esc režim ukončí (nápovědu ve stavovém řádku Figma už má).

**Stránka toolu.** Po otevření je fokus na prvním prvku, který vyžaduje rozhodnutí (výběr souboru, jinak první volba). Potvrzovací tlačítko je výchozí, takže ho spustí Ctrl+Enter odkudkoli ze stránky. Prostý Enter v textovém poli tool nespustí, aby nedošlo k nechtěné změně.

**Seznam zkratek.** Všechny zkratky jsou na stránce Settings → Shortcuts a jdou změnit. Tooltip tlačítka ukazuje i jeho zkratku.

## J. Rozsah dokumentu a ověřování

| # | Návrh úpravy | Kam | Hodnocení | Proč | Rozhodnutí |
| --- | --- | --- | --- | --- | --- |
| J1 | V úvodu kapitoly 8 napsat, že jde o backlog mimo rozsah sjednocení. První vydání znamená paritu s dnešními aplikacemi a rychlé výhry z §8.1. | §8 | **R** | Bez té věty to vypadá, že Office převody blokují sjednocení. | Přijato. |
| J2 | Collections ponechat jako skupiny toolů. Řetězení pojmenovat jinak (*Workflows*) a počítat s konflikty kroků, například Sanitize odstraní právě vytvořenou vrstvu OCR. | §8.3 | **R** | Collections ve Figmě („Prepare for sharing · Compress, Sanitize, Sign“) vypadají jako skupiny. §8.3 je chce změnit na sekvence. | Nepřijato: Collections zůstávají skupinami toolů. Řetězení toolů z §8.3 úplně vypadne. |
| J3 | Po této triáži nezůstává žádné otevřené rozhodnutí. Do §10.3 doplnit rozhodnutí z této triáže, větu „Otevřené otázky ke katalogu nezbývají“ ponechat. Úkoly pro Figmu do §10 nepatří, jsou v části *Úkoly do Figmy* níže. | §10 | **R** | Rozhodnutí z triáže mají být v `tools.md` na jednom místě, jako dosavadní rozhodnutí. | Přijato. |

## K. Další nálezy mimo revizi

| # | Návrh úpravy | Kam | Hodnocení | Proč | Rozhodnutí |
| --- | --- | --- | --- | --- | --- |
| K2 | Do §9.5 (tool popsaný daty) přidat typ výsledku (G2), typ pracovní karty (E1), chování u zamčeného dokumentu (F1, F2), závislost na externí komponentě (Tesseract, skener), krátkou dokumentaci a odkaz na tutoriál pro stránku toolu (E5). Pluginy v nové aplikaci nebudou, proto z §9.5 odstranit větu o registraci toolů pluginy a z výčtu nastavení v §6.1 stránku *Plugins*. | §9.5, §6.1 | **R** **(navíc)** | Pravidla se tak promítnou do registru toolů a nebudou se řešit případ od případu. | Přijato. |
| K3 | Do panelu View ve Figmě doplnit odkazy z §9.4/1–2: u *Rotate left/right* „Rotate pages permanently“ (Rotate Pages), u *Page colors → Grayscale* odkaz na převod barev. | Figma | **R** **(navíc)** | `tools.md` to doporučuje, ve Figmě to chybí. Otočení pohledu a barevné režimy jsou v panelu View bez rozlišení od trvalé změny. | Přijato. |

---

## Úkoly do Figmy

Úkoly vyplývající z přijatých bodů. Nejsou to otevřené otázky, ale práce na návrhu.

| Úkol | Body |
| --- | --- |
| Splitter pro změnu šířky postranního panelu. | A1 |
| *Settings…* v panelu File bez zkratky (Ctrl+K patří globálnímu hledání). | A5 |
| Ikonka zámku u tabu (dokument nejde upravit) a přepínač zámku v panelu File. U zamčeného dokumentu zakázat ikonky pro úpravy. | F1, F4 |
| Značka neuložených změn na tabu dokumentu. | G2 |
| Stránka toolu: výběr souboru (otevřené karty jedním kliknutím, soubor z disku, přetažení), krátká dokumentace a odkaz na YouTube, volby, náhled, potvrzovací tlačítko pojmenované podle výsledku, průběh, souhrn s otevřením výsledku. V režimu toolu vlastní akce v hlavní liště včetně Undo a Redo. | C5, E4, E5, G1–G8, G4 |
| Společná komponenta pro výběr stránek. | E3 |
| Operace nad výběrem v panelu Thumbnails a akce *Extract to New Document*. | E2 |
| Karta *Assembly* (sestava z více zdrojů) a karta Compare. | A6, E1, H6 |
| Katalog Tools podle `tools.md`: 46 toolů včetně *Add Image as Page*, kategorie *Optimize*, chip *Protect & Sign*, *Remove Hidden Data* místo Sanitize (i na Home). Plánované tooly nezobrazovat. | C7, C8, D3, D5, D6 |
| Stavový řádek: položka s počtem podpisů a jejich chybami, kliknutím otevře panel Signatures. | H3 |
| Panel View: odkazy na trvalé otočení stránek a převod barev. | K3 |

## Shrnutí

| Hodnocení | Počet |
| --- | --- |
| R | 44 |
| Č | 15 |
| N | 0 |

Figma už řeší viditelné vstupy do formulářů, podpisů a anotací, oddělené hledání v dokumentu (z větší části C4), stavy podpisů (z větší části H3) a skrývání postranního panelu (A1). Rozhodnuté jsou všechny body.
