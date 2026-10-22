# PDF4QT – složené widgety (Components)

Návrh druhého kroku implementace nového uživatelského rozhraní: co se stane z každé komponenty stránky **Components** ve Figmě, v jakém pořadí a jak se to ověří. První krok (styl `PDFWidgetStyle`, témata a základní widgety v knihovně `Pdf4QtLibFluent`) je hotový.

Stav dokumentu: schválený návrh, implementace probíhá podle kapitoly 6 (aktualizována 16. 10. 2026). Hotové jsou úpravy stylu z kapitoly 5 a widgety k bodům I (6.1). Dokument popisuje Figmu **po opravách z 13. 10. 2026** ([components_fixes.md](components_fixes.md)): nesrovnalosti komponent se ten den ve Figmě srovnaly a vlastník rozhodl čtyři z pěti položených otázek (kapitola 8). Tímto dokumentem se Figma nemění.

Návrh vznikl ze stavu před opravami (89 komponent) a tehdy ho proti Figmě zpětně ověřilo pět nezávislých agentů; jejich nálezy jsou zapracované. Co opravy změnily, je v [components_fixes.md](components_fixes.md); co zůstává otevřené, v [components_review.md](components_review.md).

Související soubory:

- [components_spec.md](components_spec.md) – přesný opis všech 92 komponent (vlastnosti, stavba, varianty, poznámky), ukázkových rámců a ikon.
- [components_fixes.md](components_fixes.md) – opravy komponent ve Figmě z 13. a 14. 10. 2026: co se změnilo, nové komponenty a nové tokeny.
- [components_review.md](components_review.md) – co po opravách zůstává: otázky k rozhodnutí (Q), nesrovnalosti Figmy, které opravené nejsou (F), nové nálezy z oprav (N), rozdíly hotové knihovny proti Figmě (I) a vědomé odchylky tohoto návrhu (O).
- [components_dump.js](components_dump.js) – čtecí skript, kterým se karta komponenty přečte z Figmy znovu.

## Obsah

1. [Co ukázalo čtení Figmy](#1-co-ukázalo-čtení-figmy)
2. [Zásady](#2-zásady)
3. [Soupis komponent](#3-soupis-komponent)
4. [Návrh podle rodin](#4-návrh-podle-rodin)
5. [Co doplnit do hotového stylu](#5-co-doplnit-do-hotového-stylu)
6. [Plán implementace](#6-plán-implementace)
7. [Mimo rozsah](#7-mimo-rozsah)
8. [Rozhodnutí a otevřené otázky](#8-rozhodnutí-a-otevřené-otázky)

---

## 1. Co ukázalo čtení Figmy

Stránka *Components* (`158:4605`) má jednu sekci (`46:2`) s 97 přímými potomky: 70 sad variant, 22 samostatných komponent a 5 rámců (úložiště ikon a čtyři ukázky).

| | Počet |
| --- | --- |
| Komponenty celkem | 92 |
| z toho sady variant | 70 (dohromady 552 variant) |
| z toho samostatné komponenty | 22 |
| Ikony `icon/<name>` v rámci `Icons` | 363 |
| Ukázkové rámce | 4 |
| Proměnné kolekce *Color* | 137 |

Proti stavu před opravami (89 komponent, 67 sad se 451 variantami, 133 proměnných) přibyly tři komponenty – Icon button, Menu separator a Menu section – a dvě proměnné (`bg/accent-hover`, `bg/accent-pressed`). Scrollbar a Context menu / Recent file se staly sadami.

Použití komponent jsem spočítal na všech ostatních stránkách (instance, které nejsou vnořené v jiné instanci). Na deseti stránkách s obrazovkami je 24 252 takových instancí komponent (bez ikon), na stránce *Widgets* dalších 163. Počty jsou ze čtení před opravami; opravy přímé instance na obrazovkách nepřidaly ani neodebraly, přibyly jen instance vnořené (Icon button, řádky kontextových nabídek, odznak v Recent tool row a Workspace row).

| Stránka | Rámců | Instancí celkem (včetně ikon) |
| --- | --- | --- |
| Main page | 20 | 997 |
| View Document | 32 | 1 249 |
| Tools Overview | 7 | 520 |
| Tools - Edit | 37 | 2 247 |
| Tools - Protect and Sign | 110 | 4 435 |
| Tools - Review and Inspect | 121 | 5 307 |
| Tools - Organize Pages | 93 | 4 086 |
| Tools - Create and Convert | 51 | 2 341 |
| Tools - Optimize | 28 | 1 261 |
| Tools - OCR | 78 | 5 175 |
| Widgets | 18 | 360 |

Hlavní zjištění:

1. **Pětina komponent je hotová.** 19 komponent kreslí styl nad standardním widgetem Qt beze změny (pole, combo box, posuvníky, menu, toolbar, stavový řádek). Jsou mezi nimi 3 z 10 nejpoužívanějších komponent. Dalších 12 styl kreslí, ale něco k nim chybí nebo se po opravách Figmy liší (kapitola 5); patří k nim Button, Tab, Checkbox a Filter chip, které jsou v té desítce také. U čtyř z nich (Button, Checkbox, Radio, Theme toggle segment) jde jen o barvu.
2. **Nových tříd je kolem čtyřiceti.** 35 komponent potřebuje nový widget, 2 složený widget, 8 delegáta pohledu. 16 komponent jsou obrázky, z nich 4 zastupují skutečný obsah (náhled stránky) a neimplementují se.
3. **Ikony sedí.** 363 ikon ve Figmě, 363 souborů v [resources/icons](../Pdf4QtLibFluent/resources/icons/), stejná jména. Jediná ikona jiného rozměru než 24×24 je `file-tall` (18×22). Tři ikony komprese (`compress-flate`, `compress-rle`, `compress-fax`) se ve Figmě vystředily do rámu 24×24; soubory v repozitáři jsou starý export s větším `viewBox` a vymění se (review, bod I-09).
4. **Všechny barvy jsou tokeny.** Žádná výplň, obrys ani efekt v 92 komponentách není mimo proměnné kolekce *Color*; skryté nenavázané výplně jsou odstraněné. Kolekce má 140 proměnných (od 15. 10. 2026 i `search/*`, rozhodnutí 18), `PDFThemeColors` také 140.
5. **Stav `Disabled` má 16 komponent:** Button, Checkbox, Radio, Combo box, Select, Spin box, Text field, Search field, Password field, Slider, Color button, Toolbar button, Toolbar menu button, Toolbar field, Menu item a Icon button. Ostatní se kreslí podle pravidla (kapitola 8, rozhodnutí 1). Všech 16 kreslí zakázaný stav bez krytí, text `text/disabled` a ikony `icon/disabled` (rozhodnuto 14. 10. 2026, kapitola 8, rozhodnutí 6 a 8).
6. **Stránky toolů nejsou složené jen z komponent.** Dialog výsledku, karty pravého panelu, karta průběhu, panely nabídek toolbaru a dlaždice stránek jsou na stránkách *Tools - …* obyčejné rámce stavěné skripty. Na stránce *Components* nejsou, takže je tento krok nepokrývá (kapitola 7).
7. **Část obsahu se nastavuje na vnořené instanci.** Odstín a ikona odznaku, hvězda, štítek, náhled stránky, hodnota průběhu, štítky videa, odkaz a malá tlačítka akcí nejsou vlastnosti komponenty, ale vystavené vnořené instance (Tool row, Tool shortcut, Tool choice, Recent tool row, Workspace row, Note row, Attachment row, Bookmark row, Font row, Recent file row, Recent item row, Pinned card, Quick action tile, Video card, Continue with, Page selection, Property row, Signature card, Password field). Widget je má jako své vlastnosti. Obyčejným rámcem s pevnými barvami zůstal kruh ikony v Collection row (review, F-32).
8. **Stavy jsou úplnější a rozměr se se stavem nemění.** Radio a Slider mají všech pět stavů, Scrollbar hover, stisk a Disabled, Tool choice hover, stisk a fokus, Certificate row a Font row stisk a fokus, Text field a Password field chybu i chybu s fokusem, Search field vyplněné pole s tlačítkem smazání (i zakázané), Color swatch zakázaný stav, Shortcut row záznam zkratky. Varianta Focus má všude rozměr varianty Default a obsah na stejném místě.

---

## 2. Zásady

### 2.1 Čtyři druhy řešení

| Zkratka | Řešení | Kdy |
| --- | --- | --- |
| **S** | Kreslí styl nad standardním widgetem Qt | Komponenta je vzhled běžného ovládacího prvku. Hotovo ze stránky *Widgets*. **S+** znamená, že něco chybí. |
| **W** | Nový widget knihovny | Komponenta má vlastní obsah a stavy a na obrazovce jich je málo nebo obsahuje vlastní ovládací prvky. **K** je složený widget z jiných komponent. |
| **D** | Delegát pohledu | Data dává model a položek mohou být stovky. Dnešní aplikace už pro ně modely má. |
| **O** | Obrázek | Komponenta je ilustrace, ne ovládací prvek. |

Pravidlo mezi **W** a **D**: delegát jen tam, kde seznam plní model a jeho délku neurčujeme my (osnova, náhledy stránek, záložky, poznámky, přílohy, výsledky hledání, písma, historie, zkratky). Všude jinde widget. Widget má zadarmo fokus klávesnicí, tooltipy a přístupnost vnořených tlačítek, což se u delegáta musí dělat ručně. Katalog toolů má 46 řádků a seznamy na Home nejvýš desítky, to widgety zvládnou.

### 2.2 Kreslení na jednom místě

Každá komponenta druhu W a D má svůj vzhled v jedné kreslicí funkci, která dostane obsah (texty, ikonu, odstín) a stav (`QStyle::State`: hover, stisk, fokus, výběr, zakázáno). Tutéž funkci volá:

- widget ve svém `paintEvent`,
- delegát v `paint`,
- galerie, která potřebuje vykreslit každý stav bez myši (matice stavů pro ověření proti Figmě),
- test (sondy pixelů).

Bez toho by se stejný řádek kreslil dvakrát (widget a delegát) a galerie by stavy musela vynucovat obcházením. Je to stejný princip, jakým dnes galerie kreslí standardní widgety přes ručně složený `QStyleOption`.

Řádky a karty mají společný základ (`PDFRowButton`, `PDFCardButton`: stavy, fokus, klávesnice, přístupnost). Třídy jednotlivých komponent nad ním nesou jen typované API a konstanty geometrie z karty ve specifikaci.

### 2.3 Barvy, písma, ikony

- Barvy výhradně z `PDFThemeColors`, žádná paleta. Nové widgety spadají pod detektor úniku fialové stejně jako dosavadní. Výčet se nejdřív rozšíří o dva tokeny, které opravy přidaly (kapitola 5).
- Sedm odstínů (`Blue`, `Violet`, `Amber`, `Red`, `Teal`, `Green`, `Neutral`) dostane výčet `PDFAccentHue` a funkci, která k odstínu vrátí jeho šest tokenů (`accent/<hue>/bg`, `bg-hover`, `bg-pressed`, `border`, `border-strong`, `icon`). Komponenty s vlastností `Hue` berou barvy jen přes ni.
- Písma z `PDFThemeFonts`. Komponenty používají čtyři řezy, které dnešní role nemají: Bold 14 (Quick action tile, Highlight card), Bold 13 (Resource card), Bold 22 a Semi Bold 10 (vzorek v Reading theme card). Víceřádkové popisy mají výšku řádku 130 %, ve Video card 135 %.
- Ikony přes `PDFThemeIcons`, velikosti podle karty (12, 14, 16, 18, 20, 22, 24, 28), tah 2 px v každé velikosti (kapitola 8, rozhodnutí 4). Tři místa se vymykají jednobarevné obrysové ikoně: vyplněná záložka (`PDFThemeIcons::paintFilled`, kreslí ji `PDFBookmarkDelegate`), dvoubarevný zámek tabu (I-14) a slunce přepínače motivu v barvě `icon/sun` (I-16).

### 2.4 Stavy a fokus

- Stavy jsou přesně ty, které má komponenta jako varianty. Vybraná položka skupiny s jedním výběrem (záložka, segment, položka navigace, karta motivu a barevného režimu) na myš nereaguje a klik na ni nic nedělá. Vybraný řádek seznamu nebo tabulky naopak reaguje (`bg/accent-subtle-hover`, `bg/accent-subtle-pressed`) jako pohledy na stránce *Widgets*, sekce 12 (kapitola 8, rozhodnutí 10).
- Řádky seznamů (Link row, Recent file row, Recent tool row, Workspace row, Tool row, Collection row, Issue row, Property row) nemají vlastní plochu: Hover a Pressed jsou jen překryvy `bg/hover` / `bg/pressed` a řádek leží vždy na kartě `bg/surface` (tak jsou všechny instance na obrazovkách), takže rozhodnutí 21 platí přes plochu karty. Galerie je proto kreslí na `bg/surface`.
- Komponenta bez varianty Disabled se v zakázaném stavu kreslí podle pravidla: tlumený text a ikona jako Button Secondary, výplň beze změny, žádný hover (kapitola 8, rozhodnutí 1; review, O-08).
- Widget má v každém stavu rozměr varianty Default a jeho obsah se s fokusem neposouvá (kapitola 8, rozhodnutí 3). Figma je tak po opravách nakreslená.
- Fokusový kroužek `OUTSIDE` kreslí překryvný `QFocusFrame` stylu, widget si jen řekne o poloměr (vlastnost `radius`). Kroužek `INSIDE` kreslí komponenta sama a nahrazuje jím svůj obrys. Který z nich komponenta má, je v její kartě. Pole mají kroužek vždy uvnitř, i Password field a Select (kapitola 8, rozhodnutí 2). Řádky seznamů a nabídek mají kroužek uvnitř, volně stojící prvky vně (rozhodnutí 7): řádky na sebe doléhají a seznam je ořezává, Qt kreslení delegáta ořezává na obdélník položky. Štítek (Tag) má kroužek uvnitř, protože leží v řádcích a buňkách.
- Kde je výběr sám kroužkem (Tool choice, Color swatch, Reading theme card, Color mode card), kreslí se výběr uvnitř a fokus vně a jsou vidět oba. Ve světlém tématu mají oba kroužky stejnou barvu (review, N-09).
- Kroužek se ukazuje jen po klávesnici, stejně jako u hotových widgetů.

### 2.5 Texty a zkracování

- Všechny pevné texty komponent („Then continue with“, „optional“, „No longer exists“, „Suggested“, „Add shortcut“, „Press keys…“, „Copied“…) jdou přes `tr()`.
- Zkracování podle karty: `trunc1` je výpustka na konci jednoho řádku. Kde text zkrácení nemá, widget se roztáhne nebo text zalomí podle karty.
- Název souboru v Recent file row se zkracuje uprostřed a přípona zůstává; Figma umí jen zkrácení na konci (kapitola 8, rozhodnutí 9).

### 2.6 Přístupnost a klávesnice

- Řádky, dlaždice a karty jsou tlačítka (`QAbstractButton`): role Button, název z titulku, popis z podtitulku, aktivace mezerou a Enterem.
- Vnořené ovládací prvky (hvězda, nabídka „více“, odkaz, malá tlačítka akcí `PDFIconButton`) jsou samostatné widgety s vlastním názvem a tooltipem.
- Výběrové prvky (Tool choice, Color swatch, karty režimů, Nav item) jsou zaškrtávací tlačítka ve výlučné skupině, role RadioButton, šipky přesouvají výběr.
- U delegátů jdou názvy z rolí modelu; akce uvnitř řádku (hvězda, uložit, kopírovat) musí být dostupné i z kontextové nabídky řádku, protože čtečka obrazovky je v delegátu nevidí.
- Zrcadlení pro RTL a měřítka 1×, 1,5× a 2× se ověřují stejně jako u stylu.

---

## 3. Soupis komponent

Sloupec **Použití** je počet instancí na deseti stránkách s obrazovkami (bez stránky *Widgets*); „vnořená“ znamená, že komponenta nemá na obrazovkách žádnou přímou instanci: je uvnitř jiných komponent (Star button, Tool choice), nebo se do nich dosazuje záměnou instance (náhledy v Color mode card). „Nové“ je komponenta přidaná opravami 13. 10. 2026. Sloupec **Var.** je počet variant sady, „–“ je samostatná komponenta. Karta každé komponenty je ve [specifikaci](components_spec.md) pod stejným názvem.

Čísla 1–89 jsou původní a nemění se; tři nové komponenty mají čísla 90–92 a jsou zařazené v tabulce své rodiny. Tlačítka okna (93, 94) přibyla s plánem nové aplikace 18. 10. 2026 a součet 3.6 je nezahrnuje.

### 3.1 Kostra aplikace a Home

| # | Komponenta | Uzel | Var. | Použití | Řešení | Třída nebo místo |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Tab | `48:170` | 8 | 1 705, `app-top-bar` každé obrazovky | S+ | `QTabBar` s `documentTabs`; nově `PDFDocumentTabBar` (zámek, neuložené změny, průběh) |
| 2 | Tab close button | `48:110` | 4 | 4 samostatně, jinak v každém Tabu | S+ | v tabu hotovo (`PE_IndicatorTabClose`); samostatně předvolba `PDFIconButton` |
| 3 | New tab button | `48:184` | 4 | 553 | S | `QToolButton` s `newTab` |
| 4 | Nav item | `47:123` | 8 | 295, navigace Home a Tools, vlastnosti dokumentu | S+ | svislý `QTabBar` hotov; nově `PDFNavItem` a `PDFNavList` pro skupiny |
| 5 | Segment (do 16. 10. 2026 Theme toggle segment) | `47:157` | 10 | asi 400 | S+ | `PDFSegmentedControl`: barva ikony na segment, segmenty stejně široké, dráha bez okraje; volby jedné z několika možností v toolech (Q-9) |
| 6 | Search field | `49:143` | 7 | 80 | S | `PDFLineEdit::setSearchField` |
| 7 | Text link | `49:152` | 4 | 332 | W | `PDFTextLink` |
| 8 | Link row | `49:190` | 4 | 35 | W | `PDFLinkRow` |
| 9 | Button | `63:516` | 10 | 1 976 | S+ | `QPushButton`; hover a stisk varianty Primary z nových tokenů |
| 10 | Quick action tile | `46:431` | 28 | 41 | W | `PDFQuickActionTile` |
| 11 | Recent file row | `49:280` | 8 | 14 | W | `PDFRecentFileRow` |
| 12 | Recent tool row | `50:279` | 4 | 28 | W | `PDFRecentToolRow` |
| 13 | Workspace row | `64:544` | 4 | 6 | W | `PDFWorkspaceRow` |
| 14 | Star button | `64:569` | 8 | vnořená | W | `PDFStarButton` |
| 90 | Icon button | `706:147674` | 10 | nové, na obrazovkách zatím jen vnořeně (Property row, Signature card, Password field, Attachment row) | W | `PDFIconButton` |
| 15 | Context menu / Recent file | `706:150896` | 3 | 0 | S | `QMenu`; komponenta je vzor obsahu nabídky (běžný, připnutý a chybějící soubor) |
| 16 | Folder card | `63:597` | 16 | 9 | W | `PDFFolderCard` |
| 17 | Recent item row | `79:1026` | 12 | 9, historie | D | `PDFRecentItemDelegate` |
| 18 | Pinned card | `79:1115` | 4 | 3 | W | `PDFPinnedCard` |
| 19 | Page thumbnail | `78:892` | 3 | 1, jinak vnořená | W | `PDFPageThumbnail` |
| 93 | Window button | `947:1475` | 17 | vnořená (doplněno 18. 10. 2026) | W | `PDFWindowButton` (4.9) |
| 94 | Window controls | `947:1504` | 6 | 572, `app-top-bar` každé obrazovky | K | `PDFWindowControls` (4.9) |

### 3.2 Katalog toolů, What's New, About

| # | Komponenta | Uzel | Var. | Použití | Řešení | Třída nebo místo |
| --- | --- | --- | --- | --- | --- | --- |
| 20 | Tool icon | `89:1289` | 21 | 542 | W | `PDFToolBadge` |
| 21 | Tool row | `89:1330` | 4 | 178, katalog | W | `PDFToolRow` |
| 22 | Collection row | `89:1367` | 4 | 34 | W | `PDFCollectionRow` |
| 23 | Tool category header | `89:1424` | 7 | 24 | W | `PDFToolCategoryHeader` |
| 24 | Tool shortcut | `192:836` | 4 | 6 | W | `PDFToolShortcut` |
| 25 | Tool choice | `417:1006` | 8 | vnořená | W | `PDFToolChoice` |
| 26 | Continue with | `417:1007` | – | 33, dialog výsledku | K | `PDFContinueWith` |
| 27 | Filter chip | `78:836` | 10 | asi 370 | S+ | `QPushButton` / `QToolButton` s `chip`, vodorovný `QTabBar`; vybraný jemný akcent (Q-9), volba zapnuto / vypnuto se zatržítkem |
| 28 | Tag | `116:2353` | 4 | 907 | W | `PDFTag` |
| 29 | Resource card | `50:242` | 12 | 3 | W | `PDFResourceCard` |
| 30 | Highlight card | `103:1894` | 7 | 10 | W | `PDFHighlightCard` |
| 31 | Issue row | `103:1931` | 4 | 8 | W | `PDFIssueRow` |
| 32 | Version row | `103:1950` | 2 | 4 | W | `PDFVersionRow` |
| 33 | Video card | `116:2418` | 4 | 12 | W | `PDFVideoCard` |
| 34 | Logo / PDF4QT | `110:2154` | – | 1 | O | SVG v resources knihovny |
| 35 | Key value row | `110:2155` | – | 1 999 | W | `PDFKeyValueList` (jeden widget pro všechny řádky karty) |
| 36 | Library row | `110:2158` | – | 12 | S+ | řádek `PDFTreeView` se čtyřmi sloupci; geometrie k ověření |

### 3.3 Formulářové prvky a Settings

| # | Komponenta | Uzel | Var. | Použití | Řešení | Třída nebo místo |
| --- | --- | --- | --- | --- | --- | --- |
| 37 | Checkbox | `144:2878` | 10 | 1 648 | S+ | `QCheckBox`; hover a stisk zaškrtnutého z nového tokenu |
| 38 | Radio | `292:1001` | 10 | 153 | S+ | `QRadioButton`; hover a stisk zaškrtnutého z nového tokenu |
| 39 | Combo box | `144:2899` | 5 | 656 | S | `PDFComboBox` |
| 40 | Select | `78:857` | 5 | 10 | S | `PDFComboBox::setSelectPrefix` |
| 41 | Spin box | `144:2932` | 4 | 428 | S | `PDFSpinBox`, `PDFDoubleSpinBox` |
| 42 | Text field | `144:2941` | 5 | 306 | S | `PDFLineEdit` |
| 43 | Password field | `225:870` | 15 | 93 | S+ | `PDFLineEdit::setPasswordField`; výška, zámek, přepínač a prostrkání znaků se liší |
| 44 | Strength meter | `455:1270` | 6 | 45 | W | `PDFStrengthMeter` |
| 45 | Slider | `144:2961` | 25 | 52 | S | `QSlider` |
| 46 | Progress bar | `78:868` | 5 | 35 | S | `QProgressBar` |
| 47 | Scrollbar | `706:148919` | 6 | 62 | S | `QScrollBar` |
| 48 | Settings nav item | `145:2879` | 8 | 81 | W | `PDFNavItem` s podtitulkem |
| 49 | Shortcut row | `145:2904` | 5 | 16 | D | `PDFShortcutDelegate` |
| 50 | Certificate row | `145:2929` | 8 | 17 | S+ | řádek `PDFTreeView` s pěti sloupci; geometrie k ověření |
| 51 | Color button | `156:4239` | 5 | 21 | W | `PDFColorButton` |
| 52 | Color palette popover | `156:4240` | – | 0 | W | `PDFColorPalette`; vzorky jsou kulaté `PDFColorSwatch`, šest v řadě |

### 3.4 Okno dokumentu

| # | Komponenta | Uzel | Var. | Použití | Řešení | Třída nebo místo |
| --- | --- | --- | --- | --- | --- | --- |
| 53 | Toolbar button | `190:538` | 10 | 3 274 | S | `QToolButton` |
| 54 | Toolbar menu button | `190:577` | 9 | 2 266 | S | `QToolButton` s textem vedle ikony |
| 55 | Toolbar field | `190:593` | 4 | 67 | W | `PDFToolBarField` (potomek `PDFLineEdit`) |
| 56 | Sidebar tab | `190:619` | 8 | 338 | S | `QToolButton` s `sidebarTab` |
| 57 | Status bar item | `190:658` | 8 | 1 514 | S | `QToolButton` / `QLabel` s `statusItem` |
| 58 | Menu item | `190:704` | 10 | 13 | S | položka `QMenu` |
| 91 | Menu separator | `706:150117` | – | nové, na obrazovkách zatím jen vnořeně (obě kontextové nabídky) | S | oddělovač `QMenu` |
| 92 | Menu section | `706:150119` | – | nové, zatím nepoužité | S | nadpis skupiny `QMenu` (oddělovač s textem) |
| 59 | Context menu / Thumbnails | `266:12453` | – | 1 | S | `QMenu`; komponenta je vzor obsahu nabídky |
| 60 | Action tile | `190:740` | 8 | 42 | S | `QToolButton` s textem pod ikonou |
| 61 | Stamp chip | `190:779` | 16 | 14 | W | `PDFStampChip` |
| 62 | Thumbnail item | `191:777` | 8 | 570 | D | `PDFThumbnailDelegate` |
| 63 | Thumb preview/Text | `191:539` | – | 623 | O | zástupná kresba stránky, neimplementuje se |
| 64 | Thumb preview/Heading | `191:558` | – | 159 | O | zástupná kresba stránky, neimplementuje se |
| 65 | Thumb preview/Figure | `191:575` | – | 195 | O | zástupná kresba stránky, neimplementuje se |
| 66 | Outline row | `191:840` | 8 | 15 | S+ | řádek stromu `PDFTreeView`; geometrie k ověření |
| 67 | Bookmark row | `191:911` | 8 | 7 | D | `PDFBookmarkDelegate` |
| 68 | Note row | `191:998` | 8 | 5 | D | `PDFNoteDelegate` |
| 69 | Attachment row | `191:1045` | 8 | 3 | D | `PDFAttachmentDelegate` |
| 70 | Search result row | `191:1073` | 8 | 19 | D | `PDFSearchResultDelegate` |

### 3.5 Panely dokumentu, barvy a náhledy

| # | Komponenta | Uzel | Var. | Použití | Řešení | Třída nebo místo |
| --- | --- | --- | --- | --- | --- | --- |
| 71 | Signature card | `192:1032` | 6 | 10 | W | `PDFSignatureCard` |
| 72 | Property row | `206:873` | 7 | 35 | W | `PDFPropertyRow` |
| 73 | Font row | `206:944` | 8 | 8 | D | `PDFFontDelegate` |
| 74 | Locked document | `225:906` | 2 | 4 | O | kreslený widget `PDFLockedDocumentPicture` |
| 75 | Page selection | `265:985` | 8 | 151 | K | `PDFPageSelection` |
| 76 | Color swatch | `206:805` | 16 | 1 161 | W | `PDFColorSwatch` |
| 77 | Reading theme card | `156:4218` | 8 | 10 | W | `PDFReadingThemeCard` |
| 78 | Color mode card | `171:4545` | 8 | 16 | W | `PDFPreviewCard` |
| 79 | Mode preview/Normal | `171:4367` | – | 158 | O | `PDFPreviewPainter` |
| 80 | Mode preview/Inverted | `171:4376` | – | vnořená | O | `PDFPreviewPainter` |
| 81 | Mode preview/Grayscale | `171:4385` | – | 10 | O | `PDFPreviewPainter` |
| 82 | Mode preview/High contrast | `171:4394` | – | vnořená | O | `PDFPreviewPainter` |
| 83 | Mode preview/Bitonal | `171:4403` | – | 15 | O | `PDFPreviewPainter` |
| 84 | Mode preview/Custom colors | `171:4412` | – | vnořená | O | `PDFPreviewPainter` |
| 85 | Layout preview/Single page | `192:735` | – | vnořená | O | `PDFPreviewPainter` |
| 86 | Layout preview/Continuous | `192:743` | – | vnořená | O | `PDFPreviewPainter` |
| 87 | Layout preview/Two pages | `192:758` | – | vnořená | O | `PDFPreviewPainter` |
| 88 | Layout preview/Two columns | `192:773` | – | vnořená | O | `PDFPreviewPainter` |
| 89 | Sample page | `192:1039` | – | 306 | O | zástupná stránka dokumentu, neimplementuje se |

### 3.6 Součet

| Řešení | Počet | Komponenty |
| --- | --- | --- |
| S – hotovo | 19 | 3, 6, 15, 39–42, 45–47, 53, 54, 56–60, 91, 92 |
| S+ – styl, něco chybí | 12 | 1, 2, 4, 5, 9, 27, 36–38, 43, 50, 66 |
| W – nový widget | 35 | 7, 8, 10–14, 16, 18–25, 28–33, 35, 44, 48, 51, 52, 55, 61, 71, 72, 76–78, 90 |
| K – složený widget | 2 | 26, 75 |
| D – delegát | 8 | 17, 49, 62, 67–70, 73 |
| O – obrázek | 16 | 34, 63–65, 74, 79–89 |
| **Celkem** | **92** | |

Proti stavu před opravami přibyly komponenty 90 (W), 91 a 92 (S). Z S do S+ přešly Theme toggle segment, Button, Checkbox a Radio, protože opravy Figmy změnily barvy, které styl zatím kreslí postaru (kapitola 5).

---

## 4. Návrh podle rodin

Rozměry, okraje, barvy a písma jednotlivých tříd nejsou v této kapitole opsané podruhé. Platí karta ve [specifikaci](components_spec.md). Tady je jen to, co karta neříká: z čeho třída vychází, jaké má API a jak se chová.

### 4.1 Základ

| Třída | Co dělá |
| --- | --- |
| `PDFAccentHue` | Výčet sedmi odstínů a převod na tokeny (2.3). |
| `PDFComponentPainter` | Kreslicí funkce komponent a jejich popisy obsahu (2.2). Jediná místa, kde jsou konstanty geometrie z karet. |
| `PDFRowButton : QAbstractButton` | Společný základ řádků: stavy, fokus, klávesnice, přístupnost, rozmístění vnořených widgetů (hvězda, nabídka, tlačítka akcí). |
| `PDFCardButton : QAbstractButton` | Totéž pro dlaždice a karty. |
| `PDFRowDelegate : QStyledItemDelegate` | Společný základ delegátů: stavy z `QStyleOptionViewItem`, hover a stisk akcí uvnitř řádku, tooltipy akcí, velikost řádku. |

### 4.2 Drobné prvky

| Třída | Základ | API z vlastností Figmy | Chování |
| --- | --- | --- | --- |
| `PDFToolBadge` | `QWidget` | `setHue`, `setIconName`, `setBadgeSize(28 / 32 / 44)` | Bez interakce. Používají ho řádky, karty a titulek toolu. Velikost je vlastnost `Size` komponenty: průměr 32 v řádcích a kartách, 28 v Tool choice, v titulku toolu a ve výsledcích hledání, 44 v hlavičce pracovní plochy. Kruh je plný, ikona má polovinu průměru (16, 14, 22) a tah 2 px v každé velikosti; odznak se neškáluje. Odstín Neutral je pro vše bez kategorie toolu. Odstínem se barví celá ikona; obrysové ikony mají ve Figmě jediný vektor. |
| `PDFIconButton` | `QAbstractButton` | `setIconName`, `setButtonSize(24 / 28)`, `setIconColor` | Malé tlačítko jen s ikonou: 24×24 s ikonou 14, nebo 28×28 s ikonou 16, poloměr 6. V klidu bez výplně s ikonou `icon/muted`, hover `bg/hover` a `icon/default`, stisk `bg/pressed` a `icon/strong`, fokus kroužek vně, Disabled ikona `icon/disabled` bez krytí (rozhodnutí 8; dříve krytí 0,5, protože `icon/muted` je ikona už v klidu). Barvu ikony může nadřazený widget přepsat; přepsaná platí ve všech stavech a mění se jen pozadí. Název pro čtečku a tooltip jsou povinné. Nahrazuje obyčejné rámce akcí v Property row, Signature card, Password field a Attachment row. Vzor je Tab close button; jeho samostatné použití (review, I-02) je táž třída s předvolbou podle jeho karty: 20×20, ikona `x` 12, poloměr 4. |
| `PDFTag` | `QAbstractButton` | `setText` | Ve výchozím stavu jen štítek bez fokusu; klikací, když má akci. Řádek popisku je vysoký 13 px, štítek má tedy 21 px pro jakýkoli znak. |
| `PDFTextLink` | `QAbstractButton` | `setText` | Tlačítko s vzhledem odkazu. Řeší i samostatné odkazy, které by přes `QLabel` byly fialové. |
| `PDFStarButton` | `QAbstractButton` | `setPinned`, `setSubject(File / Tool)`, signál `pinnedChanged` | Zaškrtávací; tooltip a název „Pin file“ / „Unpin“ podle stavu, jak říká popis komponenty, u toolu (Tool row, Quick action tile) „Pin tool“ / „Unpin tool“. Popis pro čtečku je název řádku nebo karty, ve které hvězda leží. Velikost 28 (Tool row, Quick action tile) nebo 24 (Recent file row, Recent item row, Pinned card). |
| `PDFColorSwatch` | `QAbstractButton` | `setColor`, `setKind(Color / None)`, `setSelected` | Zaškrtávací ve výlučné skupině. Název barvy pro čtečku se zadává zvlášť. Vybraný vzorek s fokusem má obrys výběru uvnitř a kroužek fokusu vně. Zakázaný vzorek si nechá barvu, nereaguje na myš, výběr kreslí `border/strong` a přeškrtnutí `icon/disabled`. |
| `PDFStampChip` | `QAbstractButton` | `setText`, `setHue` (Green, Red, Blue, Amber) | Tlačítko. Kroužek fokusu vně jako u Filter chipu, rozměr se s fokusem nemění (review, I-19). |
| `PDFPageThumbnail` | `QWidget` | `setSize(Small / Medium / Large)`, `setPixmap` | Rám stránky se stínem; bez obrázku kreslí zástupné linky jako komponenta. |
| `PDFKeyValueList` | `QWidget` | `addRow(key, value)`, `clear` | Jeden widget kreslí všechny řádky karty (ve Figmě 1 999 instancí řádku, typicky 5–10 v kartě). Hodnota jde označit a zkopírovat. |

Tlačítko nabídky řádků a karet (`more`) instancí Icon button ve Figmě není: je to ikona, která je v klidu `icon/muted` a při hoveru řádku `icon/default`. Kreslí se podle karty svého řádku.

### 4.3 Řádky (nad `PDFRowButton`)

| Třída | API z vlastností Figmy | Vnořené prvky |
| --- | --- | --- |
| `PDFLinkRow` | `setTitle`, `setSubtitle`, `setIconName` | šipka vpravo |
| `PDFToolRow` | `setName`, `setDescription`, `setHue`, `setIconName`, `setTag`, `setPinned` | `PDFToolBadge`, `PDFTag`, `PDFStarButton` |
| `PDFToolShortcut` | `setName`, `setHue`, `setIconName` | `PDFToolBadge` |
| `PDFCollectionRow` | `setName`, `setMeta`, `setIconName` | šipka vpravo; kruh ikony má pevné barvy `bg/accent-subtle` a `icon/accent`, není to `PDFToolBadge` (review, F-32) |
| `PDFRecentToolRow` | `setName`, `setLastUsed`, `setIconName`, `setHue` | `PDFToolBadge`, šipka vpravo; oba texty se zkracují |
| `PDFRecentFileRow` | `setFileName`, `setPath`, `setTime`, `setStatus(Available / Missing)`, `setStarVisible`, `setPinned` | `PDFStarButton`, tlačítko nabídky |
| `PDFWorkspaceRow` | `setName`, `setMeta`, `setHue`, `setIconName` | `PDFToolBadge` (výchozí Violet s ikonou `layers`), tlačítko nabídky |
| `PDFIssueRow` | `setNumber`, `setTitle`, `setDescription`, `setDate`, `setStatusIconName` | zelený kruh s ikonou stavu (vlastnost `Status icon`, výchozí zatržítko); barva kruhu je pevná |
| `PDFVersionRow` | `setVersion`, `setDate`, `setSummary`, `setCurrent`, `setSelected`, `setClickable`, `setLineVisible` | odkaz na vydání (od 21. 10. 2026, vlastnosti `Selected` a `State` 103:1950): hover `bg/hover`, stisk `bg/pressed`, kroužek 2 px uvnitř, Enter a mezerník, signál `clicked`; `Selected` (verze zobrazená stránkou) má `bg/accent-subtle` (hover / stisk `bg/accent-subtle-hover` / `-pressed`) a verzi `text/accent`, `Current` je jen tečka `bg/accent` běžící verze; `setClickable(false)` = text bez fokusu a hoveru; `setLineVisible(false)` skrývá linku posledního řádku (vlastnost `Show line`) |
| `PDFPropertyRow` | `setKey`, `setValue`, `setEditable`, signály `copied`, `edited` | čtyři `PDFIconButton` 24: kopírovat, upravit, potvrdit (`icon/accent`), zrušit. Editovatelný řádek ukazuje upravit stále, hover přidává kopírovat. Stav `Copied` na čas: tlačítko kopírovat ukáže zelené zatržítko (`accent/green/icon`) a tooltip „Copied“; nic se neposouvá. Stav `Editing` s polem, Enter potvrdí a Esc zruší. |
| `PDFNavItem` | `setText`, `setSubtitle`, `setIconName`, zaškrtávací | Jedna třída se dvěma předvolbami, každá přesně podle své karty: Nav item (190×38, text Regular, kroužek uvnitř, vybraná na myš nereaguje) a Settings nav item (300×52 s podtitulkem, název Semi Bold, kroužek uvnitř). |

`PDFNavList` je sloupec `PDFNavItem` ve výlučné skupině se skupinami oddělenými mezerou; šipky nahoru a dolů přesouvají výběr přes hranice skupin.

Odstín a ikona odznaku, štítek a hvězda jsou ve Figmě vystavené vnořené instance; widget je má jako své vlastnosti (review, O-04). Odznak v řádku má výchozí `Size` 32; změna `Size` mění ve Figmě celý kruh i ikonu.

### 4.4 Dlaždice a karty (nad `PDFCardButton`)

| Třída | API z vlastností Figmy | Poznámka |
| --- | --- | --- |
| `PDFQuickActionTile` | `setTitle`, `setDescription`, `setIconName`, `setHue`, `setStarVisible`, `setPinned` | Výška pevná 88, šířku určuje mřížka. |
| `PDFFolderCard` | `setName`, `setPath`, `setMeta`, `setMetaVisible`, `setIconName`, `setHue` (Blue, Neutral, Green, Violet) | tlačítko nabídky |
| `PDFPinnedCard` | `setName`, `setPath`, `setProgressText`, `setProgress`, `setPixmap`, `setPinned`, `setStatus(Available / Missing)`, `setLocked` | `PDFPageThumbnail`, `QProgressBar`, `PDFStarButton`, tlačítko nabídky; chybějící soubor a dokument chráněný heslem jsou návrhy (4.10) |
| `PDFResourceCard` | `setTitle`, `setDescription`, `setIconName`, `setHue` (Neutral, Green, Amber) | |
| `PDFHighlightCard` | `setTitle`, `setDescription`, `setIconName`, `setHue` | Bez stavů, není tlačítko. |
| `PDFVideoCard` | `setTitle`, `setDescription`, `setDuration`, `setTags`, `setPixmap` | Náhled videa je obrázek od aplikace; vyplňuje vnitřní šířku karty. |
| `PDFToolCategoryHeader` | `setTitle`, `setDescription`, `setCount`, `setIconName`, `setHue` | Bez stavů, hlavička karty kategorie. |
| `PDFToolChoice` | `setName`, `setHue`, `setIconName`, zaškrtávací | Výlučná skupina, kterou lze odškrtnout (nic není předvolené). Stavy Default, Hover, Pressed a Focus pro vybranou i nevybranou volbu; hover a stisk kladou `bg/hover` a `bg/pressed` přes výplň. Výběr je obrys `bg/accent` 2 px uvnitř, výplň `bg/accent-subtle` a zatržítko, jako u Color mode card. Fokus je kroužek vně, vybraná volba s fokusem má oba. Odznak 28. Rozměr 154×48 a polohy obsahu se výběrem nemění. |
| `PDFReadingThemeCard` | `setName`, barvy vzorku (papír, text), zaškrtávací | Stálý rozměr 159×86 ve všech stavech. Výběr (`bg/accent` 2 px) kreslí rámec uvnitř karty přes obsah, fokus (`border/focus` 2 px) `QFocusFrame` vně; obojí stejně jako Color mode card, obsah se neposouvá. Řada karet potřebuje mezeru aspoň 4 px (*Main page* 8 px, *View Document* 6 px). |
| `PDFPreviewCard` | `setName`, `setPreview`, zaškrtávací | Color mode card. Slouží šesti barevným režimům i čtyřem rozložením stránek. |

Quick action tile, Folder card a Resource card mají jeden recept barev odstínu, i pro Neutral: klid `bg` a `border`, hover `bg-hover` a `border-strong`, stisk `bg-pressed` a `border-strong`. Tool shortcut má pod myší `accent/neutral/bg-hover` a při stisku `bg-pressed`.

### 4.5 Složené widgety

| Třída | Obsah | API |
| --- | --- | --- |
| `PDFContinueWith` | hlavička (titulek a poznámka), odkaz `PDFTextLink`, nula až tři `PDFToolChoice`, popisek | `setTitle`, `setTitleNote`, `setLinkText`, `setChoices` (nula až tři, o šířku se dělí rovným dílem), `getSelectedChoice`, `setCaption`, signál `anotherToolRequested`. Výchozí texty „Then continue with“, „optional“ a „Another tool…“. |
| `PDFPageSelection` | hlavička s počtem stran, čtyři chipy režimu, pole rozsahu, filtry (Even, Odd, Portrait, Landscape), stav, řádek chyby | `setPageCount`, `setMode`, `setRange`, `setFilters`, `setCurrentPage`, `setSelectedPages`, výsledek jako seznam stran, validace rozsahu s chybovou zprávou. Chyba je stav widgetu (varianta `Error`): řádek se zprávou je vidět a pole rozsahu je ve stavu chyby (`PDFLineEdit::setInvalid`, obrys `text/danger` 1 px uvnitř); výška roste o 24 px. |
| `PDFSignatureCard` | odznak stavu, podepisující, přepínač rozbalení (`PDFIconButton` 24, šipka dolů nebo nahoru), štítek stavu, shrnutí, rozbalovací podrobnosti a řetěz certifikátů | `setSigner`, `setMeta`, `setSummary`, `setStatus(Valid / Warning / Invalid)`, `setExpanded`, podrobnosti a řetěz jako data. Text štítku určuje stav; shrnutí a rozbalený obsah dodá aplikace (review, F-38, F-54). |
| `PDFStrengthMeter` | úroveň, podrobnost, 20 buněk, čtyři kritéria | `setLevel`, `setDetail`, `setDetailVisible`, `setCriteriaVisible`, splněná kritéria |
| `PDFColorButton` | vzorek, název, šipka | `setColor`, `setColorName`; otevírá `PDFColorPalette`. Má stav Disabled (výplň `bg/muted`, název `text/disabled`, šipka `icon/disabled`). |
| `PDFColorPalette` | Suggested, Recently used, odkaz „More colors…“ | `setSuggestedColors`, `setRecentColors`, signály `colorSelected`, `moreColorsRequested`; okno typu popover se stínem. Vzorky jsou `PDFColorSwatch` (kulaté, 28×28), dvě mřížky po šesti v řadě s mezerou 8, vnitřní šířka 208; vybraná barva je vybraný vzorek. Jen tam, kde panel nemá místo na řadu vzorků. |
| `PDFToolBarField` | hodnota a tlumená přípona („308 / 1592“) | potomek `PDFLineEdit`: `setSuffix`, `setSuffixVisible`; nejmenší šířka 64. Má stav Disabled bez krytí (výplň `bg/muted`, hodnota i přípona `text/disabled`). |
| `PDFDocumentTabBar` | taby dokumentů | potomek `QTabBar`: `setTabLocked`, `setTabUnsaved`, `setTabProgress`, `setTabClosable` na tab. Tab je vysoký 32 i bez křížku. Zámek je jantarový ve všech stavech, tečka neuložených změn má barvu popisku tabu. |

`PDFPageSelection` nahrazuje dnešní zadávání rozsahu stran textem. Syntaxe rozsahu zůstává ta, kterou knihovna už umí číst (`PDFClosedIntervalSet`); widget nad ní jen přidává režimy a filtry.

### 4.6 Delegáti (nad `PDFRowDelegate`)

| Delegát | Model dnes | Akce v řádku |
| --- | --- | --- |
| `PDFThumbnailDelegate` | `PDFThumbnailsItemModel` | – |
| `PDFBookmarkDelegate` | `PDFBookmarkItemModel` (má vlastního delegáta) | – |
| `PDFNoteDelegate` | strom poznámek v postranním panelu | – |
| `PDFAttachmentDelegate` | `PDFAttachmentsTreeItemModel` | uložit |
| `PDFSearchResultDelegate` | výsledky hledání | – |
| `PDFFontDelegate` | seznam písem dokumentu | kopírovat |
| `PDFRecentItemDelegate` | historie (nový model) | hvězda, nabídka; role `StarVisibleRole` (bez hvězdy a bez akce Pin), `MissingRole`, `LockedRole` (4.10) |
| `PDFShortcutDelegate` | akce aplikace (nový model) | záznam zkratky |

Delegát bere texty, ikonu a odstín z rolí modelu. Role, které dnešní modely nemají (podtitulek, odstín odznaku, autor a datum poznámky), se doplní při zapojení do aplikace; v tomto kroku je dodá model galerie.

Co opravy Figmy přidaly řádkům s delegátem:

- **Attachment row** má osu `Selected` (výběr jako u Bookmark row: výplň `bg/accent-subtle`, název `text/accent`). Akce uložit je Icon button 24 s ikonou `download`; delegát ji kreslí kreslicí funkcí `PDFIconButton` (2.2).
- **Shortcut row**: klik na klávesu spouští záznam zkratky. Stav `Recording` kreslí klávesu s kroužkem fokusu uvnitř a výzvou „Press keys…“ v `text/accent`. Akce bez zkratky má místo klávesy čárkovaný rámeček „Add shortcut“, který je stejným cílem kliknutí. Záznam je editor delegáta.
- **Font row** má stavy Pressed a Focus (kroužek uvnitř); štítek typu písma je text z modelu.
- **Note row**: vybraný řádek má autora v `text/accent`.
- **Search result row**: hledaný výraz v úryvku je Semi Bold a `text/primary`; rozsah výrazu dává model.
- **Recent item row**: složka má odstín Neutral, náhled první stránky souboru dodává aplikace.

Outline row, Certificate row a Library row delegáta nedostanou: jsou to řádky stromu a tabulky, které kreslí styl. Stačí srovnat geometrii (kapitola 5).

### 4.7 Obrázky

| Komponenta | Řešení |
| --- | --- |
| Mode preview (6), Layout preview (4) | `PDFPreviewPainter` je kreslí z tokenů `sample/*`, `reading/*`, `bg/track` a `border/strong` do zadaného obdélníku. Jsou to schémata, ne náhled skutečného dokumentu. |
| Locked document | Kreslený widget se stavy `Locked` a `Wrong`. |
| Logo / PDF4QT | SVG v resources knihovny. |
| Page thumbnail | Widget (4.2); zástupné linky jen bez obrázku. |
| Thumb preview (3), Sample page | Zastupují skutečně vykreslenou stránku PDF. Neimplementují se; galerie použije vlastní zástupný obrázek. |

### 4.8 Komponenty nad standardním widgetem

Co opravy Figmy přidaly komponentám, které kreslí styl, a čím se to v Qt nastavuje. Nové stavy jsou nakreslené podle stránky *Widgets* a hotového kódu; nový kód potřebují jen řádky s odkazem na kapitolu 5.

| Komponenta | Nové ve Figmě | V Qt |
| --- | --- | --- |
| Radio | `State` (Hover, Pressed, Focus, Disabled) pro obě hodnoty `Checked`, přepínač `Show label` | stavy `QRadioButton`; bez popisku je to tlačítko s prázdným textem. Zaškrtnutý přepínač s fokusem má kroužek vně, stejně jako zaškrtnuté a částečně zaškrtnuté pole. Barvy zaškrtnutého stavu v kapitole 5. |
| Slider | `State` pro všech pět hodnot | stavy úchytu `QSlider`: hover obrys, stisk výplň, fokus kroužek kolem úchytu, Disabled tlumená výplň a úchyt; úchyt má při každé šířce 14×14 |
| Scrollbar | `Orientation` (Vertical, Horizontal), `State` (Default, Hover, Pressed, Disabled) | `QScrollBar`; mění se jen barva jezdce, zakázaný jezdec je `border/default`. |
| Progress bar | výplň je úměrná šířce pruhu | `QProgressBar`; šířka výplně = hodnota × šířka (review, I-10) |
| Search field | osa `Filled` (zadaný text a tlačítko smazání `x-circle` 16), vlastnost `Value`, `State=Disabled` pro obě hodnoty `Filled` | `PDFLineEdit::setSearchField` a `setClearButtonEnabled` |
| Text field | `State=Error`: obrys `text/danger` 1 px uvnitř; `State=Error focus`: 2 px uvnitř | `PDFLineEdit::setInvalid` |
| Select | kroužek fokusu uvnitř, šířka se s fokusem nemění, `State=Disabled` | `PDFComboBox::setSelectPrefix` (kapitola 5, I-17) |
| Password field | kroužek fokusu uvnitř, chyba `text/danger` 1 px uvnitř, `State=Disabled`, vlastnost `Value`, přepínač je Icon button 28 | `PDFLineEdit::setPasswordField` (kapitola 5, I-05) |
| Toolbar button | kombinace `Active=True, State=Disabled`: výplň `bg/accent-subtle`, ikona `icon/disabled`, bez krytí | zaškrtnutý zakázaný `QToolButton` |
| Menu item | osa `Tone` (Default, Danger), přepínače `Show icon` a `Submenu` | vlastnost `destructive` akce; akce bez ikony drží sloupec ikony; akce s podnabídkou má šipku (kapitola 5, I-18) |
| Menu separator | nová komponenta: řádek 7 px, linka 1 px `border/divider`, okraje 3 a 4 | `QMenu::addSeparator` |
| Menu section | nová komponenta: řádek 24 px, text Medium 11 `text/muted`, vlastnost `Label` | `QMenu::addSection` |
| Context menu / Recent file | sada `File` (Default, Pinned, Missing); řádky jsou Menu item 32 px a Menu separator | obsah nabídky skládá aplikace: připnutý soubor má „Unpin“, u chybějícího je „Remove from list“ první a „Open“ zakázané |
| Context menu / Thumbnails | řádky jsou Menu item 32 px a Menu separator; „Remove Pages“ je nebezpečná položka | obsah nabídky skládá aplikace |
| Tab | výška 32 i bez křížku, zámek jantarový ve všech stavech, tečka neuložených změn v barvě popisku | `PDFDocumentTabBar` (4.5; kapitola 5, I-01) |
| Filter chip | výběr nemění šířku chipu; hover a stisk vybraného chipu z nových tokenů | kapitola 5, I-04 a I-15 |
| Button | hover a stisk varianty Primary z nových tokenů | kapitola 5, I-15 |
| Checkbox | obrys při hoveru a výplň při stisku zaškrtnutého stavu z nového tokenu | kapitola 5, I-15 |
| Theme toggle segment | popis: slunce segmentu Light má barvu `icon/sun` | kapitola 5, I-16 |
| Certificate row | `State` Pressed a Focus (kroužek uvnitř) | řádek `PDFTreeView` (kapitola 5, I-07) |

### 4.9 Okno aplikace (doplněno 19. 10. 2026)

Tlačítka okna bez systémového rámu ([newapplication.md](newapplication.md) 3); kreslí `PDFComponentPainter::paintWindowButton` a `paintWindowControls` (`pdfcomponentpainterwindow.cpp`), widgety jsou v `pdfcomponentwindow.h`.

| Třída | API | Chování |
| --- | --- | --- |
| `PDFWindowButton : QAbstractButton` | `setKind` (Minimize, Maximize, Restore, Close), `setPlatform` (Windows, Linux, MacOS), `setActivity` (FollowWindow, Active, Inactive), `setExternalHover`, `setExternalPressed`, `getActionName` | 46×38, glyf 16 (macOS kruh 12 v cíli 20×38). Stavy Default, Hover, Pressed z myši **nebo od okna** (nekliencké zprávy Windows), Inactive podle `isActiveWindow` (výchozí `FollowWindow`), Disabled = `setEnabled(false)` (Maximize okna pevné velikosti). Bez fokusu (okno má systémovou nabídku), název pro čtečku a tooltip „Minimize“ / „Maximize“ / „Restore“ / „Close“, na macOS „Close“ / „Minimize“ / „Zoom“ bez tooltipu jako nativní tlačítka; název a tooltip aplikace se při změně jazyka nepřepíší. |
| `PDFWindowControls : QWidget` | `setPlatform` (výchozí podle `Q_OS_*`), `setMaximized`, `setResizable`, `setActivity`, `setSystemButtons` (macOS), `getButton`, `getButtons`, `getButtonRect` / `getButtonAt` (logické souřadnice okna), `setHoveredButton` / `setPressedButton` (od okna), `activate`, signály `minimizeRequested`, `maximizeRestoreRequested`, `closeRequested` | Tři `PDFWindowButton` jako děti (každé je tlačítko pro čtečku). 138×38, macOS 80×38. Stav maximalizace a možnost změny velikosti říká aplikace (zná je z událostí okna), aktivitu okna sleduje skupina sama. `setSystemButtons(true)` na macOS: skupina nic nekreslí a jen drží místo nativních „semaforů“ (Qt je ponechá při obsahu zasahujícím do titulku). Signál vysílá klik, akce stisku čtečky i `activate`; slot smí skupinu smazat. |

Rozhodnuto při implementaci:

- **Zprava doleva** se pořadí tlačítek zrcadlí na všech platformách (Close je u RTL vlevo), glyfy se nezrcadlí. Tak to dělá Windows u okna s rozložením `WS_EX_LAYOUTRTL` (Chrome v hebrejštině a arabštině má tlačítka vlevo), GNOME a KDE u RTL jazyků a macOS u RTL jazyků přesouvá „semafory“ doprava. Shell proto skupinu klade na koncový okraj lišty (`QHBoxLayout` ji zrcadlí sám), na macOS na náběžný.
- **macOS** kreslí jen kruhy bez glyfů a bez vlastních stavů Hover a Pressed (Figma: glyfy kreslí systém); vlastní kreslení je náhradní řešení pro okno bez nativních tlačítek a pro galerii. Neaktivní okno i zakázané zoom mají `window/macos-inactive`.
- Maximize a Restore jsou jedno (prostřední) tlačítko: `setHoveredButton(Restore)` a `getButton(Maximize)` platí pro obě podoby.
- Pozadí stavu vyplňuje celé tlačítko bez zaoblení; výška tlačítek je výška skupiny (v liště 38).

Co musí udělat okno (shell), knihovna to nedělá:

- **Windows:** `WM_NCCALCSIZE` (bez rámu, stín systému), `WM_NCHITTEST`: `HTMAXBUTTON` nad `getButtonRect(Maximize)` (jinak se Snap Layouts neukážou), `HTCLIENT` nad ostatními tlačítky, taby a tlačítkem nového tabu (`PDFDocumentTabBar::tabAt` ≥ 0, `childAt`), `HTCAPTION` na volné ploše lišty, `HTTOP` / `HTLEFT` … na okraji (ne v maximalizovaném okně). Nekliencké zprávy nad Maximize předat skupině: `WM_NCMOUSEMOVE` / `WM_NCMOUSEHOVER` → `setHoveredButton(Maximize)`, `WM_NCMOUSELEAVE` → `setHoveredButton(std::nullopt)`, `WM_NCLBUTTONDOWN` → `setPressedButton(Maximize)` (zprávu spotřebovat, jinak Windows kreslí vlastní tlačítko), `WM_NCLBUTTONUP` nad tlačítkem → nejdřív `setPressedButton(std::nullopt)`, pak `activate(Maximize)` (slot smí skupinu smazat). Stisk shell zruší i při `WM_NCMOUSELEAVE`, `WM_LBUTTONUP` v klientské oblasti, `WM_CAPTURECHANGED` a deaktivaci okna. Souřadnice zpráv jsou fyzické pixely obrazovky, `getButtonRect` vrací logické souřadnice okna: `ScreenToClient` a pak dělení `QWindow::devicePixelRatio` (ne dělení a `QWindow::mapFromGlobal` – na monitorech s různým měřítkem je to špatně). `getButtonRect` / `getButtonAt` nevrací skryté tlačítko ani tlačítko skryté skupiny (celá obrazovka); nad zakázaným Maximize (okno pevné velikosti) shell `HTMAXBUTTON` nevrací.
- **Linux:** přesun a změna velikosti `QWindow::startSystemMove` / `startSystemResize` na volné ploše a na okraji (X11 i Wayland; na Waylandu je to dekorace klienta).
- **macOS:** okno s `Qt::ExpandedClientAreaHint` (a `Qt::NoTitleBarBackgroundHint`) a `setSystemButtons(true)`; skupina 80×38 na náběžném konci lišty, taby začínají za ní.
- **Všude:** dvojklik na volné ploše lišty = maximalizace / obnovení (`QTabBar::tabBarDoubleClicked(-1)` pro prázdnou plochu tab baru, jinak vlastní plocha lišty), `setMaximized` a `setResizable` podle událostí okna (`QEvent::WindowStateChange`), sloty signálů skupiny (`showMinimized`, `showMaximized` / `showNormal`, `close`). Lišta má odsazení 16 vlevo a 200 vpravo (16 + 46 volných + 138), taby zarovnané dolů; v maximalizovaném okně leží lišta u horního okraje obrazovky a tab je trefitelný na okraji, protože obdélník tabu `PDFDocumentTabBar` obsahuje i 6 px nad tvarem (v normálním okně tuto plochu dostane změna velikosti `HTTOP`).

`PDFDocumentTabBar` dostal pro lištu jen `setTabPasswordProtected` / `isTabPasswordProtected`: tab dokumentu chráněného heslem ukazuje ikonu `password` místo své ikony (Figma 228:8709; ikona tabu se zachová a vrátí, čtečka slyší „password protected“, tab bez ikony dostane její místo). Tab *New tab* (955:152339) je obyčejný tab s ikonou `folder` a křížkem, Home je nezavíratelný (`setTabClosable(false)`); obojí knihovna už uměla.

### 4.10 Prvky stránek kroku 2 (doplněno 21. 10. 2026)

Integrace kroku 2 aplikace (I2) přesunula do knihovny prvky, které stránky Home, Open, Recent, What's New, About, Support a Sponsor kreslily samy, a doplnila stavy, které stránky potřebovaly. Rámce *Main page*, které nejsou komponentami Figmy, jsou v knihovně komponentami s jednou kreslicí funkcí v `PDFComponentPainter` (`pdfcomponentpainterbadges.cpp`) a widgetem v `pdfcomponentbadges.h`; vzorky jsou na stránce galerie *19-shell-home* (blok „Elements of the pages“).

| Třída | API | Chování |
| --- | --- | --- |
| `PDFIconBadge : QWidget` | `setIconName`, `setExtent`, `setIconExtent`, `setRadius` (záporný = kruh), `setColors(výplň nebo nic, ikona, okraj)`, `setHue`, `setLogo` | icon-circle karet (What's New, About, Sponsor, prázdné stavy), logo-box About; bez výplně jen ikona (ikony dialogů 14 / 16). Pevná velikost i ve větším obdélníku, myš prochází, zakázaný má ikonu `icon/disabled`. Nahrazuje `PDFAppIconBadge` a `PDFAppIconCircle`. |
| `PDFStatusBadge : QWidget` | `setBadge(text, ikona, odstín)` | badge-up-to-date karty vydání: pilulka odstínu s okrajem, ikona 12 a text Semi Bold 11 barvou ikony odstínu, výška 21; jméno pro čtečku je text. |
| `PDFFactPill : QWidget` | `setText`, `setIconName` | fakta hero karet About a Sponsor: `bg/surface` s `border/default`, ikona 13 `icon/muted`, text Medium 12 `text/secondary`, výška 25; odsazení 9 / 11 leží uvnitř okraje 1 px (ikona na 10, text 12 px před koncem, pill-Open source 111:2227; revize R2-D2). |
| `PDFAvatar : QWidget` | `setInitials` (nejvýš 3 znaky), `setExtent`, `setFontPixelSize`, `setHue` | iniciály Bold v kruhu odstínu (Blue) s okrajem. |
| `PDFStatusRow : QWidget` | `setTitle`, `setSubtitle`, `setStatus`, `setActive` | priority Sponsor: tečka 8 (`bg/accent` aktivní, jinak `border/strong`), titulek a podtitulek (data, zkrácené), štítek Tag stavu vpravo (kreslený, ne vnořený widget), výška 46; jméno titulek, popis podtitulek a stav. |
| `PDFTintedCard : QFrame` | `setHue(odstín nebo nic)` | karta vydání, hero karty, Thank you, slib Sponsor: pozadí a okraj odstínu, radius 12; bez odstínu `bg/surface` s `border/default`. Rozložení dává aplikace. |

Stavy doplněné k existujícím komponentám (Figma je nemá, jsou to **návrhy** podle tokenů; k potvrzení vlastníkem):

- **Chybějící soubor** v Recent item row a Pinned card: název `text/muted` a odznak „No longer exists“ (`accent/red/bg`, `text/danger`, jako Recent file row `Status=Missing`) místo průběhu čtení (u Recent item row ve sloupci průběhu, u karty dole místo textu a pruhu). Odznak kreslí `PDFComponentPainter::paintMissingFileBadge` (i Recent file row).
- **Dokument chráněný heslem**: náhled stránky (`PageThumbnail::locked`) je `bg/subtle` s ikonou `lock` `icon/muted` (14 / 20 / 28 px podle velikosti) a okrajem `border/strong` místo obrázku.
- **Bez hvězdy** v Recent item row (`starVisible`, role `StarVisibleRole`): místo hvězdy zůstává prázdné, aby sloupce seznamu lícovaly; delegát pak nemá akci Pin (pracovní prostory se nepřipínají).
- **Síťová cesta** (UNC, URL, GVFS, namapovaná síťová jednotka, síťový mount – `PDFAppNetworkPaths`, rozhodnutí vlastníka 22. 10. 2026): Home, Open a Recent ji samy nekontaktují (žádná kontrola existence, počet PDF ani žádost o náhled). Řádek, karta i položka Recent zůstávají ve stavu Available bez odznaku: ukazují uložená data (čas, průběh čtení) a uložený náhled posledního otevření, jinak zástupný náhled. Nový vizuální stav není: „No longer exists“ by byl nepravdivý a neutrální odznak by zaplnil každý řádek sdílené složky. Kontrola a náhled přijdou s otevřením položky (`PDFAppMainWindow::openDocuments`).

Další doplňky: `PDFTreeView::RowStyle::Contents` (obsah příručky help-contents 955:152873: řádky 28, radius 6, šipka 14 na 8, mezera 8, odsazení 24 na úroveň; texty a stavy jako Outline). Položka bez potomků (téma) nemá místo šipky: její text začíná na 24 px za úroveň (toc-Protect with Password na 24, kapitola na 30), styl ho posune před obdélník položky (`getLeafLeft`, zrcadlí se pro RTL; revize R2-D2). Hlavička pohledu s předvolbou Library jsou sloupce tabulky About (columns 111:2291): bez pásu `bg/subtle` a bez oddělovačů sekcí, popisky Medium 11 `text/muted` 6 px pod horním okrajem s odsazením 10 jako buňky, `border/divider` na 27 a mezera 4 před prvním řádkem (výška hlavičky 32). `PDFMessageBox` má veřejné API dialogu návrhu: `setBadgeVisible`, `addContent`, `addActionButton(text, Leading / Trailing, primární)` (vlastní řada tlačítek v pořadí návrhu, tlačítko dialog samo nezavírá, standardní `QDialogButtonBox` se skryje, dokud nemá tlačítko), `getActionButtons`, `setCardWidth` / `getContentWidth` (texty se zalamují, výška podle obsahu); `PDFAppCardDialog` ho používá místo hledání odznaku v rozložení. Ikony `hourglass` a `timer-off` (karta Update status) vyexportované z *Components / Icons* beze změny.

Doplňky po kroku 2 (schváleno 22. 10. 2026): `PDFComponentPainter::TextLinkSize` – odkaz `Small` je Semi Bold 12 (MIT License v app-footer 28:217, stejná velikost jako okolní text patičky) se stejným řádkem 16 px, prstencem fokusu a podtržením jako výchozí odkaz, `PDFTextLink::setLinkSize` (neznámá hodnota = výchozí velikost). `PDFMessageBox::setScrimTarget` / `getScrimTarget` / `getScrim`: scrim zakryje zvolený widget a karta se centruje nad ním (aplikace zakryje jen tělo okna pod horní lištou s kartami a tlačítky okna – app-body ve snímku pdf4qt-recent-clear-history 941:135742); změna za zobrazení přesune scrim i kartu, smazaný cíl = rodič. Titulek a text zprávy jsou prostý text (`Qt::PlainText`) jako podrobnosti – název složky se značkami se nikdy nevykreslí jako HTML; žádný volající rich text nepotřeboval. Vlastnost `overlayScrollBars` (`PDFWidgetStyleProperties::OVERLAY_SCROLL_BARS`) na `QAbstractScrollArea`: `PM_ScrollView_ScrollBarOverlap` = 12 pro jeho posuvníky, viewport zabírá celou plochu a posuvník leží přes jeho okraj (kreslí jen drážku a jezdec, okraje ukazují obsah); oblast stránky karet Home a Help v aplikaci tak sahá o šířku posuvníku do okraje 32 px pracovní plochy a sloupce končí tam, kde rámce (1044 / 1408 při 1440 px).

---

## 5. Co doplnit do hotového stylu

Dvanáct komponent styl kreslí, ale podle karet něco chybí nebo se liší. Podrobně s čísly je to v [components_review.md](components_review.md), část I. Kapitola je od 15. 10. 2026 provedená ([components_fixes.md](components_fixes.md), kapitola 14), otevřený zůstává jen I-24 (I-13, I-21 a I-23 vyřešili delegáti kroku 5, 6.6); tabulky zůstávají jako záznam.

| Komponenta | Co chybí nebo se liší | Bod |
| --- | --- | --- |
| Tab | Zámek (dvě barvy v jedné ikoně, I-14), tečka neuložených změn, pruh průběhu. Výška 32 i bez křížku. | I-01 |
| Tab close button | Samostatné použití mimo tab (hlavička panelu, aktivní tool); řeší `PDFIconButton` (4.2). | I-02 |
| Nav item | Skupiny položek na jedné výlučné navigaci. | I-03 |
| Theme toggle segment | Slunce segmentu Light má barvu `icon/sun` ve všech stavech; `PDFSegmentedControl` kreslí ikony barvou stavu a potřebuje barvu ikony na segment. | I-16 |
| Button | Hover a stisk varianty Primary z `bg/accent-hover` a `bg/accent-pressed`. | I-15 |
| Filter chip | Počet a ikona u chipu, který je tlačítko (tab bar je má). Hover a stisk vybraného chipu z `bg/accent-hover` a `bg/accent-pressed`. Zakázaný chip: vybraný `bg/muted` (styl už tak kreslí), text a ikona `text/disabled` a `icon/disabled`. | I-04, I-15, I-20 |
| Checkbox, Radio | Obrys při hoveru a výplň při stisku zaškrtnutého stavu z `bg/accent-pressed`. | I-15 |
| Password field | Výška 40 místo 34, zámek 16 vlevo, přepínač 28×28 (`PDFIconButton`), prostrkání tajných znaků 2 px, stav Disabled. Kroužek fokusu uvnitř a chyba `text/danger` 1 px uvnitř jako u ostatních polí. | I-05 |
| Outline row | Poloměr 8 a okraje 6 / 8 proti řádku pohledu s poloměrem 6 a okraji 8. | I-06 |
| Certificate row, Library row | Výška 34 a 30, poloměr 6, druhý nebo první sloupec Semi Bold. | I-07 |

Čtyři body jsou jen k ověření, kód podle nich zatím měnit není třeba:

| Komponenta | Co ověřit | Bod |
| --- | --- | --- |
| Select | Kroužek fokusu uvnitř místo obrysu, šířka se s fokusem nemění; Disabled `bg/muted`. | I-17 |
| Menu item | Ikona v místě 16×16 a text na x = 36 i bez ikony, nebezpečná položka, šipka podnabídky 14 px `icon/muted`. Řádek má 32 px všude: obě kontextové nabídky jsou ve Figmě přestavěné z Menu item. | I-18 |
| Progress bar | Drážka 4 px ve widgetu 16 px, barvy dráhy a výplně, 100 % zeleně. | I-10 |
| Stamp chip | Kroužek vně, 85×26. Komponenta dostane vlastní třídu `PDFStampChip` (4.2); bod platí, pokud bude kreslit přes vlastnost `chip`. | I-19 |

Mimo jednotlivé komponenty:

| Místo | Co udělat | Bod |
| --- | --- | --- |
| `PDFThemeColors` | Čtyři nové hodnoty výčtu na konci, v pořadí Figmy: `bg/accent-hover` (`#1d4ed8` / `#2563eb`), `bg/accent-pressed` (`#0f4796` / `#1d4ed8`), `text/disabled` a `icon/disabled` (obě `#94a3b8` / `#525e72`). Výčet má pak 137 hodnot; v `UnitTestsWidgetStyle` se změní počet a kontrolní součet tabulky. Zakázaný stav všech prvků stylu a ikonový engine (`QIcon::Disabled`) přejdou z `text/muted` / `icon/muted` na nové tokeny. | I-15, I-20 |
| Styl (`pdfwidgetstylebuttons.cpp`, `pdfwidgetstyletabs.cpp`) | Nové tokeny místo `accent/blue/icon` a `text/accent-pressed` u čtyř prvků z tabulky výše. Světlé téma se nezmění (hodnoty jsou stejné), tmavé ano. Ostatní užití obou starých tokenů (odkazy, obrys úchytu posuvníku při hoveru) zůstávají. | I-15 |
| Ikony | Soubory `compress-flate.svg`, `compress-rle.svg` a `compress-fax.svg` vyměnit za nový export s `viewBox` 24×24. | I-09 |
| `PDFThemeFonts` | Čtyři nové role (2.3). | I-12 |
| Ikonový engine | Vyplněná záložka (I-13) a dvoubarevný zámek tabu (I-14). | I-13, I-14 |

Body jsou „k ověření při implementaci“: vycházejí z porovnání karet s konstantami ve zdrojácích, ne z měření v galerii.

---

## 6. Plán implementace

Všechno přibude do knihovny `Pdf4QtLibFluent`. Žádná aplikace se na ni v tomto kroku nepřipojuje a stávající zdrojový kód aplikací se nemění. Figma se při implementaci jen čte, needituje se.

Plán je aktualizovaný 16. 10. 2026 podle skutečného stavu kódu: krok 1 původního plánu je z velké části hotový (kapitola 5, [components_fixes.md](components_fixes.md), kapitoly 14 až 16) a opravy Q-9 přidaly Segment a volby se zatržítkem.

### 6.1 Co je hotové

| Oblast | Hotovo | Komponenty |
| --- | --- | --- |
| Tokeny, písma, ikony | `PDFThemeColors` se 140 hodnotami, role písem Semi Bold 10, Bold 13, 14 a 22 (I-12), ikony komprese (I-09), dvoubarevná ikona `PDFThemeIcons::paintFilled` (I-13, I-14), ikona `highlight-comment` | – |
| Kreslení | `PDFStylePainter`: tvary, obrysy uvnitř a vně, kroužek fokusu, stín, ikony, texty | – |
| Odstíny | `PDFAccentHue`, `PDFAccentHueColors::getColor` | – |
| Widgety | `PDFIconButton` (i Tab close button), `PDFStampChip`, `PDFNavItem` a `PDFNavList`, `PDFToolBarField`, `PDFKeyValueList`, `PDFDocumentTabBar` | 2, 90, 61, 4, 48, 55, 35, 1 |
| Styl | Segment a `PDFSegmentedControl`, Button, Filter chip včetně voleb `option`, Checkbox, Radio, Password field, Select, Menu item, předvolby řádků `PDFTreeView` Outline, Certificate a Library | 5, 9, 27, 37, 38, 43, 40, 58, 66, 50, 36 |
| Galerie a test | stránka `18-components`, v `UnitTestsWidgetStyle` testy `components`, `disabledAndSelect`, `rowStyles`, `itemEditing`, `chipsAndSegments` | – |

Zbývá 46 tříd v pěti krocích (6.3): tři základní, widgety, složené widgety, kreslené obrázky a delegáti.

### 6.2 Soubory

| Soubor (`Pdf4QtLibFluent/sources/`) | Obsah |
| --- | --- |
| `pdfcomponentpainter.h/.cpp` | `PDFComponentPainter`: kreslicí funkce komponent a jejich popisy obsahu (2.2) nad primitivy `PDFStylePainter`. Jediné místo s konstantami geometrie z karet; volá je widget, delegát, galerie i test. |
| `pdfcomponentbuttons.h/.cpp` | drobné prvky (4.2), k hotovým `PDFIconButton` a `PDFStampChip` |
| `pdfcomponentrows.h/.cpp` | `PDFRowButton` a řádky (4.3), k hotovým `PDFNavItem` a `PDFNavList` |
| `pdfcomponentcards.h/.cpp` | `PDFCardButton`, dlaždice a karty (4.4) |
| `pdfcomponentpanels.h/.cpp` | složené widgety (4.5), k hotovým `PDFToolBarField`, `PDFKeyValueList` a `PDFDocumentTabBar` |
| `pdfcomponentdelegates.h/.cpp` | `PDFRowDelegate` a delegáti (4.6) |
| `pdfpreviewpainter.h/.cpp` | obrázky (4.7): `PDFPreviewPainter`, `PDFLockedDocumentPicture` |
| `pdfcomponentwindow.h/.cpp`, `pdfcomponentpainterwindow.cpp` | tlačítka okna (4.9): `PDFWindowButton`, `PDFWindowControls` a jejich kreslení (doplněno 19. 10. 2026) |

Galerie dostane pět nových stránek podle kapitol specifikace: `19-shell-home` (Kostra a Home), `20-catalog` (Katalog toolů, What's New, About), `21-forms` (Formulářové prvky a Settings), `22-document` (Okno dokumentu), `23-panels` (Panely dokumentu, barvy a náhledy). Stránka `18-components` s hotovými widgety zůstává. Každý vzorek nese identifikátor uzlu Figmy a každá komponenta má matici svých variant (stavy kreslené přes `PDFComponentPainter` bez myši) a živý widget. Detektor úniku `UnitTestsWidgetStyle` vykresluje všechny stránky galerie, nové tedy také.

Test je nový cíl `UnitTestsFluentComponents` ([tst_fluentcomponentstest.cpp](../UnitTests/tst_fluentcomponentstest.cpp), `QT_QPA_PLATFORM=offscreen`, kompiluje stránky galerie jako `UnitTestsWidgetStyle`).

### 6.3 Kroky

| Krok | Obsah | Komponenty |
| --- | --- | --- |
| 1 | **Základ a drobné prvky.** `PDFComponentPainter`, `PDFRowButton`, `PDFCardButton` (stavy, fokus, klávesnice, přístupnost, rozmístění vnořených widgetů); `PDFToolBadge`, `PDFTag`, `PDFTextLink`, `PDFStarButton`, `PDFColorSwatch`, `PDFPageThumbnail`. Kostra testu `UnitTestsFluentComponents` a pět nových stránek galerie. | 20, 28, 7, 14, 76, 19 |
| 2 | **Řádky.** `PDFLinkRow`, `PDFRecentFileRow`, `PDFRecentToolRow`, `PDFWorkspaceRow`, `PDFToolRow`, `PDFCollectionRow`, `PDFToolShortcut`, `PDFIssueRow`, `PDFVersionRow`, `PDFPropertyRow`. | 8, 11, 12, 13, 21, 22, 24, 31, 32, 72 |
| 3 | **Dlaždice, karty a obrázky.** `PDFQuickActionTile`, `PDFFolderCard`, `PDFPinnedCard`, `PDFToolCategoryHeader`, `PDFToolChoice`, `PDFResourceCard`, `PDFHighlightCard`, `PDFVideoCard`, `PDFReadingThemeCard`, `PDFPreviewCard`; `PDFPreviewPainter`, `PDFLockedDocumentPicture`, logo. Na konci kroku vzorek Home podle *Main page* složený jen z widgetů knihovny, který ověří, že API widgetů do sebe zapadá. | 10, 16, 18, 23, 25, 29, 30, 33, 77, 78; 34, 74, 79–88 |
| 4 | **Složené widgety.** `PDFPageSelection`, `PDFContinueWith`, `PDFColorButton` a `PDFColorPalette`, `PDFSignatureCard`, `PDFStrengthMeter`. | 75, 26, 51, 52, 71, 44 |
| 5 | **Delegáti.** `PDFRowDelegate` (stavy z `QStyleOptionViewItem`, stisk sledovaný delegátem, hover a stisk akcí v řádku, kroužek fokusu uvnitř) a `PDFThumbnailDelegate`, `PDFBookmarkDelegate` (vyplněná záložka, I-13), `PDFNoteDelegate`, `PDFAttachmentDelegate`, `PDFSearchResultDelegate`, `PDFFontDelegate`, `PDFRecentItemDelegate`, `PDFShortcutDelegate`. Otevřené body I-21 a I-23. | 62, 67, 68, 69, 70, 73, 17, 49 |

Thumb preview (63–65) a Sample page (89) se neimplementují (O-02). Body I-24 (barvy sloupců Certificate row a Library row určuje model) a N-09 zůstávají, jak jsou v review.

### 6.4 Pravidla kódu

Kód je defenzivní: žádný vstup z API, z modelu ani z prostředí nesmí vést k pádu, nedefinovanému chování nebo kreslení mimo widget.

- **Vstupy API.** Neplatná hodnota výčtu, záporné číslo, hodnota mimo rozsah, prázdný text nebo neznámý název ikony se ošetří: hodnota se omezí (`qBound`), neplatná se nahradí výchozí, neznámá ikona se nekreslí. Index mimo rozsah je bez účinku a funkce vrací `nullptr` nebo `false`. `Q_ASSERT` jen pro vnitřní invarianty, nikdy místo ošetření vstupu.
- **Ukazatele a vlastnictví.** Vnořené widgety vlastní rodič Qt; nevlastněné objekty (model, skupina tlačítek, cizí widget) jsou v `QPointer` a před použitím se kontrolují. Žádné holé `delete` a `new` bez vlastníka. Každá funkce snese `nullptr` za painter, option, widget i model.
- **Životnost objektů.** Obsluha signálu `destroyed` nikdy nevolá metody odesílatele: destruktor odvozené třídy už proběhl a jeho členy neexistují, `QPointer` na objekt se ale vynuluje až v destruktoru `QObject`. Obsluha jen zapomene ukazatel. Objekt, který druzí sledují, oznámí svůj konec ze svého destruktoru, dokud je ještě celý. Po každém `Q_EMIT` a po každém volání, které může spustit kód aplikace (nabídka, dialog, zpětné volání), musí kód přežít smazání `this` i objektů, které používá: před voláním je drží v `QPointer` a po něm je kontroluje. Slot nikdy nemaže odesílatele hned (`deleteLater`). Každé spojení má kontextový objekt (příjemce, viz bod „Bez lambd“), takže zanikne se svým příjemcem. Widget s vnořenými objekty, vyskakovacími okny nebo sledovanými objekty má test, který je maže v každém pořadí: nejdřív rodiče, nejdřív dítě, z vlastního signálu, s otevřeným vyskakovacím oknem. **Každý widget knihovny má živý vzorek na stránce galerie:** test `UnitTestsFluentLifetime` vezme každý widget každé stránky a zruší ho v každém pořadí a situaci, nový widget je tak pokrytý už svým vzorkem. Funkce `librarySamples` to vynucuje: seznam tříd knihovny s metaobjektem se při buildu generuje ze zdrojů a třída bez vzorku je chyba (výjimky jen s odůvodněním – abstraktní báze, styl, správce témat, popupy a editory, jejichž vlastník vzorek má). Co aplikace s widgetem dělá a obecný vstup testu neumí (nabídka tlačítka „více“, běžící časovač, otevřený editor), patří do nápovědy vzorku `GalleryLifetimeHints`. Signál knihovny emitovaný kódem, po kterém pokračuje Qt bez ochrany, aplikace synchronně smazat nemůže; kód knihovny po každém vlastním `Q_EMIT` a po každém volání, které vyšle signál (`setChecked`, `setSelected` vnořeného tlačítka), ale smazání přežít musí. Vyskakovací okno spuštěné `exec()` není objekt na zásobníku, jehož rodiče může aplikace smazat (`QPointer` a `new`).
- **Bez lambd.** V knihovně se lambdy nepíšou, pokud nejsou opravdu nutné (rozhodnutí vlastníka 18. 10. 2026): hůř se ladí – nemají jméno v zásobníku volání ani pro breakpoint a zachycené proměnné skrývají životnost objektů. Spojení je `connect(odesílatel, &Odesílatel::signál, příjemce, &Příjemce::slot)` na členskou metodu, příjemce je vždy kontextový objekt. Slot zná odesílatele z argumentů nebo ze členu; `sender()` jen tehdy, když signál objekt neidentifikuje, v soukromém slotu, přes `qobject_cast` s kontrolou na `nullptr`. Povolenou výjimkou z tvaru s ukazatelem na člen jsou dvě volání `QMetaObject::invokeMethod` podle jména chráněných slotů `commitData` / `closeEditor` třídy `QAbstractItemView` (`PDFShortcutDelegate::closeEditorOfView`). Porovnávací a pomocné funkce jsou statické metody třídy nebo jejího `<Třída>Helper`. Lambda, která zůstat musí, má komentář proč, nic nezachycuje odkazem, co může zaniknout dřív, a ve spojení má kontextový objekt. Testy a galerie lambdy používat smí.
- **Aritmetika.** Žádné dělení nulou (počet položek, šířka, rozsah), velikosti a souřadnice bez přetečení `int`, prázdné a záporné obdélníky se nekreslí, porovnání `qreal` přes `qFuzzyCompare` / `qFuzzyIsNull`.
- **Kreslení.** Stav painteru se vrací (`save` / `restore` nebo `QPainterStateGuard`), kreslí se jen do obdélníku komponenty, dlouhé texty se zkracují podle karty (2.5), zrcadlí se pro RTL a kreslí v logických pixelech pro měřítka 1, 1,5 a 2.
- **Stav widgetu.** Setter nejdřív porovná novou hodnotu se starou a při shodě nic nedělá; signál se vysílá jen při skutečné změně a až po změně stavu (reentrance ze slotu nesmí rozbít stav). `updateGeometry` jen při změně velikosti, jinak `update`.
- **Téma.** Barvy a písma se berou při kreslení, nikdy se neukládají; po změně tématu se widget překreslí a přepočítá velikost. Nic se nečte z `QPalette`.
- **Delegáti.** Neplatný `QModelIndex` a chybějící role (`QVariant` bez hodnoty, jiný typ) dají výchozí obsah. Model se může během života pohledu změnit nebo zmizet.
- **Časovače a animace.** Časovač má vlastníka a zastaví se v destruktoru a při skrytí (stav `Copied` v Property row).
- **Přístupnost a texty.** Každý ovládací prvek má název pro čtečku; pevné texty přes `tr()`.
- **Konvence repozitáře.** MIT hlavička, CRLF, žádné anonymní namespace (pomocné funkce jako statické metody pomocné třídy), `Q_EMIT`, build bez varování `/W4`.
- **Testy** pokrývají i hraniční vstupy: prázdné texty, neplatné hodnoty, `nullptr`, index mimo rozsah, změnu tématu, měřítka a RTL.

### 6.5 Postup a ověření

Každý krok má tři fáze:

1. **Implementace.** Agent naprogramuje třídy kroku, vzorky v galerii a testy, sestaví cíle `Pdf4QtLibFluent`, `Pdf4QtFluentGallery`, `UnitTestsWidgetStyle` a `UnitTestsFluentComponents` bez varování, spustí oba testy a vyexportuje galerii ve světlém a tmavém tématu při 1× a 2×.
2. **Dvě nezávislé kontroly současně.**
   - **Věrnost Figmě:** agent si přečte Figmu (jen čtecími nástroji) a porovná export galerie a konstanty v kódu s kartami komponent: rozměry, okraje, poloměry, barvy v obou tématech, písma, stavy a fokus.
   - **Review kódu:** agent v roli seniorního programátora C++ a Qt posoudí správnost, defenzivnost podle 6.4, vlastnictví a životnost objektů, chování při změně tématu, klávesnici a přístupnost, RTL a měřítka, API a pokrytí testy.
3. **Opravy.** Všechny nálezy obou kontrol se opraví, znovu se sestaví, spustí testy a vyexportuje galerie. Nálezy, které vyžadují rozhodnutí nebo jsou rozporem ve Figmě, se zapíšou do [components_review.md](components_review.md), část I.
4. **Paměť a Linux.** Knihovna se sestaví i na Linuxu (GCC odmítá kód, který MSVC přeloží) a všechny testy se spustí pod valgrindem ve WSL ([CLAUDE.md](../CLAUDE.md), *Build and test*). Ve Windows Release zůstává uvolněná paměť čitelná, takže čtení po uvolnění tam testy neodhalí; tři takové chyby kroků 2–5 našel až ladicí build a valgrind. Krok je hotový, až všechny testy skončí i pod valgrindem s kódem 0. Mezi nimi je `UnitTestsFluentLifetime`, generický test pořadí rušení všech widgetů všech stránek galerie (kapitola 19 v [components_fixes.md](components_fixes.md)); nový widget proto musí mít živý vzorek v galerii (jinak selže `librarySamples`) a každý jeho signál musí být dosažitelný vstupem uživatele na stránce (jinak selže `deletedFromOwnSignal`), případně s nápovědou vzorku `GalleryLifetimeHints`.

Po posledním kroku se doplní [CLAUDE.md](../CLAUDE.md) (nové třídy, řádek testu v tabulce) a do [components_fixes.md](components_fixes.md) záznam o implementaci.

### 6.6 Průběh

Stav 17. 10. 2026: **všech pět kroků je hotových**. Každý krok prošel oběma kontrolami (6.5) a jejich nálezy jsou opravené; co vyžaduje rozhodnutí vlastníka, je v [components_review.md](components_review.md) (I-29 až I-42, I-44, I-45, O-09, O-10). Souhrn implementace je v [components_fixes.md](components_fixes.md), kapitola 18.

| Krok | Stav | Výsledek |
| --- | --- | --- |
| 1 | hotovo | `PDFComponentPainter`, `PDFComponentButton` (společný základ `PDFRowButton` a `PDFCardButton`), `PDFToolBadge`, `PDFTag`, `PDFTextLink`, `PDFStarButton`, `PDFColorSwatch`, `PDFPageThumbnail`; nové stránky galerie 19–23, test `UnitTestsFluentComponents`. Kontroly: review kódu 27 nálezů (2 chyby: odškrtnutí volby myší a mezerníkem, `&` v textech), Figma 6 drobných nálezů; vše opraveno, do review I-25 a I-26 (rozhodnuté 17. 10. 2026, 8.5). |
| 2 | hotovo | `PDFLinkRow`, `PDFRecentFileRow`, `PDFRecentToolRow`, `PDFWorkspaceRow`, `PDFToolRow`, `PDFCollectionRow`, `PDFToolShortcut` (karta, kroužek vně), `PDFIssueRow`, `PDFVersionRow`, `PDFPropertyRow`, tlačítko nabídky `PDFMoreButton`. Kontroly: review kódu 29 nálezů (2 chyby v klávesnici a fokusu editace Property row), Figma 6 drobných nálezů; vše opraveno, do review I-27 a I-28 (rozhodnuté 17. 10. 2026, 8.5). Mimo rodinu řádků přibyla vlastnost stylu `popupAnchor` (umístění nabídky tlačítka, které nebere fokus) a ikonový engine bere poměr pixelů ze skutečného zařízení. |
| 3 | hotovo | `PDFCardButton`, `PDFQuickActionTile`, `PDFFolderCard`, `PDFPinnedCard`, `PDFToolCategoryHeader`, `PDFToolChoice`, `PDFResourceCard`, `PDFHighlightCard`, `PDFVideoCard`, `PDFReadingThemeCard`, `PDFPreviewCard`; `PDFPreviewPainter` (Mode preview, Layout preview, logo), `PDFLockedDocumentPicture` (vlastní hlavička, stín odznaku kreslí do rodiče), logo v resources; vzorek Home podle *Main page* složený z widgetů knihovny. Kontroly: review kódu 24 nálezů (1 chyba: zrušená karta nebo řádek v `menuRequested` z kontextové nabídky, opravena i v řádcích kroku 2; dále pořadí Tab nových štítků, poloha štítků po změně titulku, nejmenší výška karet s výškou podle šířky), Figma 14 nálezů (zastaralá ikona `play-circle`, výška tabu ve vzorku Home a šest rozdílů vzorku) a 6 nesrovnalostí Figmy; vše opraveno, kromě I-29 až I-36 a O-09 (k rozhodnutí). Do základu přibyla výška podle šířky s mezipamětí (`computeHeightForWidth`, `invalidateHeight`) a nabídka tlačítka `more` (`showMoreButtonMenu`) společná řádkům a kartám, `PDFComponentPainter::drawScaledPixmap` (zmenšený obrázek v `QPixmapCache`) a předmět hvězdy (`PDFStarButton::setSubject`). Kontrola všech 364 ikon proti Figmě: znovu vyexportované `play-circle`, `anchor-point`, `margins` a `dot`. |
| 4 | hotovo | `PDFPageSelection` (režimy jako `PDFSegmentedControl` s rovnými segmenty, pole rozsahu se stavem chyby a řádkem chyby +24 px, filtry Even / Odd / Portrait / Landscape: filtry jednoho druhu se sčítají, oba druhy se zužují, orientace z `setPageSizes`, neznámý rozměr neprojde; chyba se oznámí čtečce `QAccessibleAnnouncementEvent`, chyba aplikace `setCustomError` platí v každém režimu), `PDFPageRange` (parser se syntaxí a hláškami `PDFClosedIntervalSet::parse` bez závislosti na Pdf4QtLibCore; navíc pomlčka – jako oddělovač, číslice libovolného písma, prázdné části se přeskakují, kontrola existence stránky, bez přetečení, nejvýš 10 000 000 stran), `PDFContinueWith` (nula až tři `PDFToolChoice` dělí šířku, výlučná skupina, která jde vyprázdnit, popisek podle výběru), `PDFColorButton` a `PDFColorPalette` (popover stylu, šipky v mřížce, Esc, klik mimo, zavření s oknem kotvy, poslední barvy), `PDFSignatureCard` (přepínač rozbalení, odkazy certifikátů, text štítku podle stavu), `PDFStrengthMeter`; kreslení v `pdfcomponentpainterpanels.cpp`, galerie v `gallerypanelpages.cpp`. Do základu přibylo `PDFComponentPainter::wrapText` (dřív soukromé zalamování karet), `PDFComponentButton::hasPopup` (stav pro čtečku, dřív jen u `PDFMoreButton`), vlastnost stylu `popupPlaced` (popover si panel umístí sám, styl ho při zobrazení neposouvá). Kontroly: review kódu 29 nálezů (4 chyby: zastaralý název barvy po výběru z palety, zrušení výběru stránek slotem `resultChanged` v `setPageCount` / `setLabel` / při změně jazyka, zdvojené zrcadlení zarovnání `PDFKeyValueList` zprava doleva, počet stránek v záhlaví o 1 px za okrajem; rizika: směr textu ve všech kreslicích funkcích, výsledek obřích dokumentů, zrušení palety ve slotu `closed`, množná čísla, ohlašování chyb), Figma 8 nálezů (výška Continue with a Tool choice 46 / 91 při jednořádkových názvech, ikona chyby na dvou řádcích, slova názvů se nelámou, název aktuálního toolu v popisku, čísla podle národního prostředí) a 7 nesrovnalostí Figmy; vše opraveno kromě I-37 až I-43 a O-10 (k rozhodnutí). Při opravách: jedno pravidlo směru textu pro všechny kreslicí funkce kroků 1 až 4 (`PDFComponentPainter::DirectionGuard`, absolutní zarovnání `State::leadingAlignment` / `trailingAlignment`, `Qt::LayoutDirectionAuto` odstraněno), `PDFComponentPainter::getLongestWordWidth`, `PDFToolChoice` s výškou podle šířky, výsledek `PDFPageSelection` jako intervaly se souhrnem (počet aritmeticky, intervaly až na požádání), oddělovače jiných písem v `PDFPageRange`, `PDFContinueWith::setCurrentToolName`, signál `PDFColorPalette::colorsChanged` a ve frontě odeslaný `closed`, stav rozbalení `PDFColorButton` pro čtečku (`PDFComponentButton::hasExpandablePopup`), ohlášení úrovně `PDFStrengthMeter`. |
| 5 | hotovo | `pdfcomponentdelegates.h/.cpp`: role modelu `PDFComponentRoles` (od `Qt::UserRole + 0x0C00`, tabulka rolí a výchozích hodnot v hlavičce), `PDFRowDelegate` (stavy z `QStyleOptionViewItem`, vybraný řádek reaguje na myš a výběr se kreslí i v pohledu bez fokusu – je to aktuální stránka, záložka, anotace; stisk sleduje delegát sám a drží ho, dokud je ukazatel nad položkou, konec uvolněním, opuštěním, skrytím a změnou modelu; akce v řádku s hoverem, stiskem a tooltipem `PDFToolTip`, klik na akci položku nevybere ani neaktivuje, akce jsou i v kontextové nabídce (myš, klávesa nabídky) a pod zkratkou aktuální položky; kroužek 2 px uvnitř po klávesnici; zakázaná položka podle O-08; jeden delegát pro více pohledů), `PDFThumbnailDelegate` (obrázek stránky z `PixmapRole` nebo `Qt::DecorationRole`, poměr stran zachován ve slotu 108 × 148), `PDFBookmarkDelegate` (vyplněná záložka), `PDFNoteDelegate`, `PDFAttachmentDelegate` (uložit = Icon button 24), `PDFSearchResultDelegate` (hledané výrazy z `MatchRangesRole`, druhý řádek zkrácený i s formáty), `PDFFontDelegate` (kopírovat jméno písma do schránky, i Ctrl+C), `PDFRecentItemDelegate` (hvězda zapisuje `PinnedRole`, nabídku `more` ukáže aplikace na signál `actionTriggered`), `PDFShortcutDelegate` s editorem `PDFShortcutRecorder` (klik na klávesu nebo „Add shortcut“, Enter, mezerník, F2; Esc, Tab a ztráta fokusu záznam ruší; zkratky aplikace klávesy během záznamu nekradou; konflikt v témže rodiči ohlásí `shortcutConflict` a nezapíše se, pokud není `setConflictsAccepted`; Backspace a Delete se zaznamenají jako klávesy – karta o mazání nic neříká – zkratku odebere položka „Remove shortcut“ kontextové nabídky); kreslení v `pdfcomponentpainterdelegates.cpp`, galerie v `gallerydelegatepages.cpp` (matice všech variant osmi sad a živé pohledy na stránkách 19, 21, 22 a 23). `PDFTreeView` sleduje stisk řádků předvoleb Outline a Certificate a dává jim `State_Sunken` (styl kreslí `bg/pressed`, vybraný řádek `bg/accent-subtle-pressed`), delegáta aplikace nemění. Texty dat (I-43): `PDFComponentPainter::getDataTextDirection` a `drawDataText` ve všech kreslicích funkcích kroků 1 až 5 a v `PDFKeyValueList`. Rozhodnuto při implementaci: Font row, Recent item row a Shortcut row výběr nekreslí (Figma osu Selected nemá), ikona kopírování Font row nemá vlastní stavy (prostý rámec podle karty), řádek během záznamu zkratky je kreslen jako Default, název akce Shortcut row končí 12 px před viditelnou klávesou (poznámka karty). Do základu přibylo `PDFToolTip::getTitle` a `PDFToolTip::showForWidget` nechává tooltipy položek s delegátem komponent delegátu. Uzavřeny I-13, I-21, I-23 a I-43. Kontroly: review kódu 30 nálezů (3 chyby: přístup k zrušenému viewportu v destruktoru a filtru delegáta, který je dítětem pohledu – poškození haldy skryté v Release; stisk, který zůstal po ztraceném uvolnění, v delegátu i v `PDFTreeView`; `PDFShortcutDelegate` sdílený dvěma pohledy; 6 rizik: `sizeHint` četl obrázek z `Qt::DecorationRole`, výšky bez mezipaměti, index jiného modelu, dvojí tvar výběru ve stromu, klávesy začátku záznamu kradené zkratkami aplikace, šířka položky stromu pod kořenovým indexem; 12 drobností, 9 drobností stylu), Figma 9 nálezů (2 chyby: pruh průběhu Recent item row zprava doleva, kroužek fokusu předvoleb Outline a Certificate jen kolem buňky; výběr předvoleb bez fokusu, chybějící vzorky Pressed a Focus předvoleb, mezery seznamů podle obrazovek, barva ikony tlačítka přepsaná rodičem ve všech stavech) a 4 nesrovnalosti Figmy; vše opraveno kromě I-44 a I-45 (k rozhodnutí), O-09 je rozšířená o Thumbnail item. Při opravách: sledované pohledy a viewporty v `QPointer`, odpojení při signálu `destroyed` pohledu (přichází před smazáním dětí) a při `setItemDelegate` jiného delegáta, viewport z `setViewport`; stisk končí i pohybem bez tlačítka, blokujícím modálním oknem, ztrátou fokusu, tažením, opuštěním; záznamy zkratek si pamatují svůj pohled a zavírají se jen v něm (sloty pohledu místo signálů delegáta); `triggerAction` je nevirtuální a volá `performAction`, klávesy delegáta říká `isKeyHandled` (i pro `ShortcutOverride`); velikost nikdy nečte obrázek, platí `Qt::SizeHintRole`, `QImage` se převádí jednou, výšky textů jsou v mezipaměti; řádky seznamu nejsou širší než viewport (žádný vodorovný posuvník po objevení svislého); `PDFComponentPainter::drawElidedText` a `TextKind` jako jediné místo zkracování a směru jednořádkových textů (směr textu dat podle celého textu), názvy tabů dokumentů jako texty dat, `paintProgressBar` plní od náběžného okraje pro každého volajícího; přepsaná barva ikony Icon button platí ve všech stavech tlačítka (i Property row). |

Výsledek po kroku 2: build bez varování, `UnitTestsFluentComponents` 60/60, `UnitTestsWidgetStyle` 194/194, galerie vyexportovaná ve světlém a tmavém tématu při 1× a 2× bez fialového úniku. Výsledek po kroku 3: build bez varování, `UnitTestsFluentComponents` 82/82, `UnitTestsWidgetStyle` 194/194, galerie (1 398 obrázků) ve světlém a tmavém tématu při 1× a 2× bez fialového úniku. Výsledek po kroku 4 (před kontrolami): build bez varování, `UnitTestsFluentComponents` 100/100, `UnitTestsWidgetStyle` 194/194, galerie (1 468 obrázků) ve světlém a tmavém tématu při 1× a 2× bez fialového úniku. Po opravách kontrol kroku 4: build bez varování, `UnitTestsFluentComponents` 108/108, `UnitTestsWidgetStyle` 194/194, galerie (1 470 obrázků) ve světlém a tmavém tématu při 1× a 2× bez fialového úniku. Výsledek po kroku 5 (před kontrolami): build bez varování, `UnitTestsFluentComponents` 125/125, `UnitTestsWidgetStyle` 194/194, galerie (1 585 obrázků) ve světlém a tmavém tématu při 1× a 2× bez fialového úniku. **Konečný výsledek po opravách kontrol kroku 5:** build bez varování, `UnitTestsFluentComponents` 132/132, `UnitTestsWidgetStyle` 194/194, galerie (1 594 obrázků) ve světlém a tmavém tématu při 1× a 2× bez fialového úniku; nepoškozená halda ověřená samostatným sestavením Debug (ladicí halda MSVC) mimo repozitář.

Doplněno 19. 10. 2026 (fáze 1 nové aplikace, krok 1, část F): `PDFWindowButton` a `PDFWindowControls` (4.9), ikony `window-minimize`, `window-maximize`, `window-restore`, `window-close`, `PDFDocumentTabBar::setTabPasswordProtected`; vzorky na stránce `19-shell-home` (všech 17 variant Window button a 6 variant Window controls s uzly Figmy, živé widgety) a tlačítka okna ve vzorku Home; testy `windowSizes`, `windowPixels` (obě témata, 1 / 1,5 / 2), `windowControlsBehaviour`, `windowActivity`, `windowDefensive`, `documentTabPassword`.

Smlouvy základu, které platí pro další kroky:

- Řádky dědí z `PDFRowButton`, dlaždice a karty z `PDFCardButton`; kreslí se v `paintContent` přes `PDFComponentPainter`, vnořené widgety se přidávají `addTrailingWidget` a drží se v `QPointer`.
- Kreslicí a rozměrová funkce každé komponenty je v `PDFComponentPainter`; widget, galerie, testy i delegáti kreslí jen přes ni.
- Texty komponent jsou data, `&` není mnemonika.
- Výlučný výběr potřebuje výlučnou `QButtonGroup` (automatická výlučnost Qt se nepoužívá, sdružila by tlačítka celé stránky); vybraná položka skupiny s jedním výběrem má `SelectionBehavior::ExclusiveStatic`.
- `PDFPageThumbnail` má rozměr rámu a je to dětský widget; řádky a karty kreslí náhled i jeho stín samy přes `PDFComponentPainter`.
- Komponenta s výškou podle šířky nastaví `QSizePolicy::setHeightForWidth` a přepíše `computeHeightForWidth`; `PDFComponentButton` výšku ukládá podle šířky a textu. Každý setter takové komponenty volá `invalidateHeight` (ne jen `update`); změnu tématu, písma, stylu a jazyka řeší základ sám. Nejmenší výška je výška pevných částí a jednoho řádku každého textu.
- Kontextová nabídka řádku nebo karty s tlačítkem `more` jde přes `PDFComponentButton::showMoreButtonMenu`: slot `menuRequested` smí řádek zrušit (událost je pak přijatá a dál nejde).
- Velké obrázky aplikace (náhled stránky, obrázek videa) kreslí `PDFComponentPainter::drawScaledPixmap`.
- Složený widget kreslí své části kreslicí funkcí s `paintNested = false` a vnořené widgety umisťuje podle rozvržení z `PDFComponentPainter` (`get…Layout`, logické souřadnice zleva doprava, zrcadlí se `QStyle::visualRect`). Skupina voleb je jedna zastávka Tab: šipky přesouvají fokus, zastávkou je poslední fokusovaná volba (to u zaškrtávacích tlačítek ve skupině dělá sám `QAbstractButton`), předtím vybraná nebo první.
- **Směr textu (jedno pravidlo pro kroky 1 až 5).** Každá kreslicí funkce `PDFComponentPainter` nastaví painteru po dobu kreslení směr ze `State::direction` (`DirectionGuard`, po návratu vrátí směr volajícího) a všechna zarovnání textů jsou absolutní (`Qt::AlignAbsolute`; náběžný a koncový okraj dávají `State::leadingAlignment` a `State::trailingAlignment`). Výsledek tedy nezávisí na směru painteru, který volající začal (painter delegáta na pohledu zprava doleva, painter widgetu), a základní směr textu je směr stavu, nikdy se neodhaduje z obsahu (`Qt::LayoutDirectionAuto` se nepoužívá, ani u zalamovaných textů `QTextLayout`). Widgety, které kreslí mimo `PDFComponentPainter` (`PDFKeyValueList`, přípona `PDFToolBarField`), mají painter ve směru widgetu a absolutní zarovnání. Test `textDirection` kreslí zástupce všech kroků painterem LTR, RTL i Auto pro oba směry stavu a porovnává je s widgety; delegáti kroku 5 pravidlo převzali (painter z `QStyledItemDelegate::paint` může mít libovolný směr, test `delegateDirectionAndTheme`).
- **Texty dat (od kroku 5, review I-43).** Jednořádkový text kreslí jediná funkce `PDFComponentPainter::drawElidedText` (druh `TextKind::Interface`, `Data`, `FileName`); text dat má směr celého textu, i když zkrácení odřízne jeho první silný znak. Text rozhraní (popisek, titulek, přeložená zpráva, čas a datum formátované aplikací, štítek Tag) má základní směr stavu. Text, který je daty – název souboru, cesta, jméno osoby, verze, číslo, hodnota vlastnosti, obsah dokumentu nebo uživatele – má vlastní směr podle prvního silného znaku (izolace FSI z UAX #9, bez silného znaku zleva doprava, `PDFComponentPainter::getDataTextDirection`). Kreslí ho `PDFComponentPainter::drawDataText`: painter dostane na dobu kreslení směr textu, zarovnání zůstává absolutní podle komponenty (relativní zarovnání se převede ve směru painteru volajícího). Zkrácení (`elidedText`, `elidedFileName`) se dělá dřív, výpustka je proto na konci textu v jeho směru: „Annual_Report_20…“ i v řádku zprava doleva, název souboru zkrácený uprostřed má příponu na svém konci. Řádky jednoho zalamovaného textu dat mají směr celého textu (`QTextLayout` shrnutí Version row a úryvku Search result row dostane `getDataTextDirection`). Daty jsou: název a cesta (Recent file row, Folder card, Pinned card, Attachment row, Recent item row), jméno Workspace row, číslo, titulek a popis Issue row, verze a shrnutí Version row, hodnota Property row a `PDFKeyValueList`, název barvy Color button, podepisující, hodnoty podrobností a názvy certifikátů Signature card, délka Video card, štítek stránky Thumbnail item, titulek a podtitulek Bookmark row, autor a text Note row, úryvek Search result row, název písma Font row, klávesa Shortcut row a název tabu dokumentu (`PDFDocumentTabBar`, zkrácený uprostřed). Test `dataTextDirection` (RTL rozhraní s latinkou, LTR rozhraní s hebrejštinou, zkrácení na konci i uprostřed).
- **Delegáti (krok 5).** Delegát dědí z `PDFRowDelegate`, kreslí v `paintItem` jen přes `PDFComponentPainter` a velikost dává `getItemSize` (šířka z možnosti, ze sloupce nebo viewportu pohledu – `QTreeView` se ptá bez šířky; -1 = výchozí šířka komponenty). Data bere z rolí `PDFComponentRoles` (chybějící role, jiný typ a neplatný index dají výchozí hodnotu). Akce uvnitř řádku vrací `getActions` (obdélníky v souřadnicích možnosti, z funkcí `PDFComponentPainter::get…Rect`); hover, stisk, tooltip, kontextovou nabídku a zkratku obstará základ. Delegát pohled sleduje filtrem událostí na pohledu a jeho viewportu (nainstaluje ho při prvním `paint` / `sizeHint`); pohled i viewport drží v `QPointer`, pohled zapomene při jeho signálu `destroyed` (přichází před smazáním dětí, viewport tehdy ještě existuje) a nikdy nevolá `viewport()` rušeného pohledu; při skrytí, opuštění a rušení pohledu nevolá žádnou funkci pohledu. Pohled, který delegáta přestal používat, se přestane sledovat. Velikost položky nikdy nečte obrázek (model může v `Qt::DecorationRole` vykreslit stránku), akce provádí `performAction` (nevirtuální `triggerAction` je ověří), klávesy delegáta říká `isKeyHandled`. Delegáti kreslí řádky plochých seznamů; mezery dává pohled podle obrazovek (u každého delegáta v hlavičce: panely dokumentu 2, náhledy mřížka 8 / 6, Recent, zkratky a písma 0). Styl delegáty nenahrazuje, delegát nemění styl; tooltipy položek s delegátem komponent nechává `PDFToolTip` delegátu.
- Okno typu popover knihovny má vlastnosti `popover` a `popupPlaced` a okraje stínu bere z `PDFWidgetStyleImpl::getPopupMargins` (jen pod `PDFWidgetStyle`).

### 6.7 Co bude po tomto kroku

Kostra aplikace podle *Main page* (okno s taby v titulku, navigace, stránky Home, Open, Tools, Settings), která už komponenty jen skládá.

---

## 7. Mimo rozsah

- **Vzory, které nejsou komponentami.** Dialog výsledku toolu, karty pravého panelu, karta průběhu, panely nabídek toolbaru, dlaždice stránek v sestavě, tabulky a stromy na stránkách toolů jsou ve Figmě obyčejné rámce. Na *Main page* také dlaždice loga v postranním panelu, čárkovaná plocha pro přetažení souboru (`drop-zone`) a patička; vzorek Home v galerii je kreslí vlastním widgetem galerie. Potřebují vlastní soupis, nejlépe spolu s rámcem toolu.
- **Titulkový pruh okna** a rozložení `app-top-bar`. Patří ke kostře aplikace.
- **Zapojení do aplikací.** Výměna dnešních pohledů za nové delegáty, nové role modelů.
- **Skutečný obsah.** Vykreslení náhledu stránky, načtení historie, registr toolů. Widgety dostanou data zvenku, galerie jim dá vzorová.

---

## 8. Rozhodnutí a otevřené otázky

### 8.1 Rozhodnuto 13. 10. 2026

| # | Otázka | Rozhodnutí |
| --- | --- | --- |
| 1 | Stav „zakázáno“ u komponent bez varianty Disabled | Kreslí se podle pravidla: text `text/disabled` a ikona `icon/disabled` jako Button Secondary, výplň beze změny, žádný hover (review, O-08; tokeny od 14. 10. 2026). |
| 2 | Fokus polí | Kroužek každého pole je uvnitř, i u Password field a Select. Figma je podle toho opravená. |
| 3 | Rozměr widgetu ve stavech | Widget má v každém stavu rozměr varianty Default. Ve Figmě mají varianty Focus rozměr Default a obsah se neposouvá. |
| 4 | Tah ikony v odznaku toolu | 2 px v každé velikosti. Figma je opravená: Tool icon má vlastnost `Size` 28 / 32 / 44. |

### 8.2 Rozhodnuto 14. 10. 2026

| # | Otázka | Rozhodnutí |
| --- | --- | --- |
| 5 | Paleta barev (Q-1) | Kulaté vzorky Color swatch, šest v řadě. Figma je opravená. |
| 6 | Zakázaný stav toolbaru a nabídky (Q-3) | Tlumené barvy bez krytí, jako stránka *Widgets* a hotový styl. Figma je opravená včetně 1 627 instancí na obrazovkách. |
| 7 | Umístění kroužku fokusu (Q-2) | Pole a řádky seznamů a nabídek uvnitř, volně stojící prvky vně. Uvnitř nově Nav item, Menu item, řádky panelů dokumentu, Home a nastavení, Certificate row, Font row a Thumbnail item; vně nově Video card, Reading theme card a Color mode card. Tag zůstal uvnitř (štítek v řádcích a buňkách). Figma i pravidlo na stránce *Widgets* jsou opravené. |
| 8 | Tokeny zakázaného stavu (Q-8) | Nové `text/disabled` a `icon/disabled` (`#94a3b8` / `#525e72`, asi 2,5 : 1) pro stav Disabled všech komponent, bez krytí, včetně Icon button. Figma je opravená včetně 1 812 přepisů na obrazovkách a stránky *Widgets*; v kódu review I-20. |
| 9 | Zkrácení názvu souboru (Q-4) | Aplikace zkracuje uprostřed a ponechá příponu (2.5). |
| 10 | Vybraná položka pod myší (Q-5) | Vybraná položka skupiny s jedním výběrem (záložky, segmenty, navigace, karty volby) na myš nereaguje a klik na ni nic nedělá; Nav item je ve Figmě sjednocený se Settings nav item (vybraná varianta Hover a Pressed vypadá jako Default) a pravidlo svislého tab baru na stránce *Widgets* je opravené, v kódu review I-22. Vybraný řádek seznamu reaguje jako u ostatních pohledů; Certificate row je ve Figmě sjednocený s řádky panelů dokumentu (vybraný řádek má Hover `bg/accent-subtle-hover`). |
| 11 | Seznamy jako widgety (Q-6) | Katalog toolů, seznamy na Home a vlastnosti dokumentu jsou sloupce widgetů; historie, zkratky a panely dokumentu jsou pohledy s delegáty (2.1). |
| 12 | Šipky polí a značka nabídky (N-02, N-04) | Povolené šipky Spin boxu, Combo boxu, Selectu a Color buttonu a zatržítko položky nabídky jsou `icon/default`, jako na stránce *Widgets* a ve stylu; tlumené barvy patří zakázanému stavu. |
| 13 | Barvy kategorií toolů (N-11) | Platí karta `pdf4qt-tools` na *Tools Overview*, část All tools: Organize Pages Amber, Create & Convert Blue, Edit Green, Protect & Sign Red, Optimize Violet, Review & Inspect Teal. Komprese patří do Optimize, je tedy fialová. Dlaždice Home, odznaky toolů a hlavičky kategorií mají barvu kategorie toolu; popisy Quick action tile, Tool icon a Tool category header a kopie katalogu na *Main page* jsou opravené. Resource card má nový odstín Violet pro videotutoriály. |
| 14 | Zástupné náhledy (Q-7) | Thumb preview a Sample page se neimplementují, v aplikaci je vždy vykreslená stránka; galerie má vlastní zástupný obrázek (4.7, review O-02). |
| 15 | Zakázané chipy (N-19) | Filter chip má stav Disabled bez krytí: text, počet a ikona `text/disabled` a `icon/disabled`, vybraný chip výplň `bg/muted`, nevybraný `bg/surface` s obrysem `border/default`. Řady chipů a výběr stránek, které během běhu toolu nejdou použít, jsou ve Figmě v tomto stavu; v Qt `setEnabled(false)` na kontejneru. |
| 16 | Ztlumené skupiny na obrazovkách (N-22) | Ovládací prvky, které teď nejdou použít, se nikdy neztlumují krytím: komponenty se stavem Disabled jsou v něm, ostatní se kreslí podle O-08 (text `text/disabled`, ikona `icon/disabled`, výplň beze změny), popisky a ikony mimo komponenty také. Krytí zůstává jen u obsahu (náhledy stránek a obrázků). Hlavička tabulky je hlavička pohledu, ne zakázaný řádek: popisky `text/muted` jako u `QHeaderView`. Figma je opravená na šesti stránkách toolů. |
| 17 | Dvě tlačítka v OCR (Q-10) | Zůstává, jak je: *Recognize N Checked Pages* v pravém panelu a *Apply to PDF* v liště toolu. |
| 18 | Barvy výsledků hledání (Q-12) | Výsledek hledání má vlastní tokeny `search/match` (`#a5f3fc`), `search/match-current` (`#22d3ee`) a obrys aktuálního výsledku `search/match-border` (`#0891b2`, 2 px vně), kreslené násobením přes stránku. Tyrkysová není výchozí barvou žádné anotace; přes žluté zvýraznění vychází zeleně. Tokeny jsou v obou režimech stejné, kreslí se přes dokument. Anotace Highlight v ukázkovém dokumentu má skutečnou barvu Highlight `annotation/yellow`. Dnes `PDFFindTextTool` kreslí výsledky `Qt::blue` a aktuální výsledek `Qt::yellow`. |
| 19 | Stejná barva dvou kategorií v Compare (Q-13) | Barva, kterou už má jiná kategorie, se v paletě nabízí dál, ale její výběr barvy prohodí: kategorie, která ji měla, dostane předchozí barvu měněné kategorie. Dvě kategorie tak nikdy nemají stejnou barvu. |
| 20 | Pruhy v tabulce jazyků OCR (N-13) | Jsou jen ukázka v návrhu, neřeší se. |

Opravou Figmy se uzavřela i výška řádku nabídky: řádek má 32 px všude, obě kontextové nabídky jsou přestavěné z komponenty Menu item.

### 8.3 Rozhodnuto 16. 10. 2026

| # | Otázka | Rozhodnutí |
| --- | --- | --- |
| 21 | Výplň stavů na jiném pozadí než bílém (Q-15) | Ovládací prvek má vlastní plochu i na barevném pozadí: Hover a Pressed kladou průsvitné `bg/hover` / `bg/pressed` jako druhou výplň nad `bg/surface`, ne místo ní. Styl to už tak kreslí. Figma je opravená 16. 10. 2026 (Select, nevybraný Filter chip, Button Secondary, Pinned card; vzor Tool choice), totéž dostaly vybrané karty, které měly jen poloprůhledné `bg/accent-subtle` (Tool choice, Action tile, volby dialogů výsledku na obrazovkách). |
| 22 | Přepínač hesla ve vyplněném poli (Q-17) | Oko v Password field je ve všech obsazích obyčejné Icon button bez přepsané barvy: klid `icon/muted`, hover `icon/default`, stisk `icon/strong`, Disabled `icon/disabled`. Ztmavení ve vyplněném poli nic nesdělovalo (oko má smysl i v prázdném poli) a `icon/muted` je čitelné (asi 4,8 : 1 na bílé). Přepsání barvy Icon button zůstává jen pro sémantické barvy (accent, danger), které se se stavem nemění. Figma i knihovna jsou opravené 16. 10. 2026. |
| 23 | Najetí myší na Checkbox a Radio (Q-18) | Změna musí být vidět v obou režimech. Nezaškrtnuté pole: Hover výplň `bg/surface+bg/hover`, Pressed `bg/surface+bg/pressed` (dvě výplně jako u rozhodnutí 21), tah v Hover a Pressed `icon/muted` místo `border/strong`; Radio dál zesílí tah z 1 na 1,5 px. Zaškrtnuté pole: Hover výplň `bg/accent-hover` bez tahu, jako Button Primary; Pressed `bg/accent-pressed`. Default, Focus a Disabled beze změny. Dnešní změna výplně měla kontrast 1,05 : 1 (světlý) a 1,06 : 1 (tmavý), nová 1,13 / 1,23 : 1 a obrys 3,2 / 2,8 : 1; zaškrtnuté pole 1,49 / 1,41 : 1. Figma (komponenty i stránka *Widgets*, sekce 03) i knihovna jsou opravené 16. 10. 2026. |
| 24 | Plná modrá jen pro primární tlačítko (Q-9) | Vybraný Filter chip má `bg/surface` + `bg/accent-subtle` s textem `text/accent` a ikonou `icon/accent`, jako Toolbar button `Active=True`; plná výplň `bg/accent` patří jen potvrzovacímu tlačítku. Volba jedné z několika možností (předvolby Compress, výběr stránek, zarovnání, Fit / 100 %, Thumbnails / Details, Marks / Preview result, profil OCR) je segmentovaný přepínač – komponenta Segment (dříve Theme toggle segment) bez ikony na dráze `bg/track`, všechny segmenty stejně široké. Volba zapnuto / vypnuto (Whole words, Regular expression, Sync zoom) je chip, zapnutý s ikonou zatržítka místo své ikony. Provedeno ve Figmě i v knihovně ([components_fixes.md](components_fixes.md), 16.2). |
| 25 | Anotace v liště dokumentu (Q-14) | Nabídka *Annotate* mezi *View* a *Insert*: zvýraznění textu, Highlight and comment, barva, odkaz na komentáře; lišta nad vybraným textem; zkratky Ctrl+Shift+H, L, K, Q, M. Popis v [tools.md](tools.md), 9.8, Figma v [components_fixes.md](components_fixes.md), 16.3. |
| 26 | Dráha přepínače motivu (Q-16) | Platí *Main page*: dráha `bg/track` `rad=16` bez okraje, výška 32 (182×32). Ukázka na *Widgets* a `PDFSegmentedControl` jsou upravené. |

### 8.4 Otevřené

Q-9, Q-14 a Q-16 jsou rozhodnuté 16. 10. 2026 (24 až 26) a provedené, body review I-25 až I-28 z kroků 1 a 2 rozhodl vlastník 17. 10. 2026 (27 až 30, 8.5) a Figma i knihovna jsou podle nich opravené. Z kontroly kroku 3 čekají na rozhodnutí body review I-29 až I-36 a odchylka O-09 (rozšířená kontrolou kroku 5 o Thumbnail item), z kontroly kroku 4 body I-37 až I-42 a odchylka O-10 (I-43 vyřešil krok 5), z kontroly kroku 5 body I-44 (výběr v pohledu bez fokusu: *Widgets* 12 proti obrazovkám) a I-45 (šířka názvu Shortcut row ve Figmě) ([components_review.md](components_review.md)); každý má doporučené řešení. Figma se do rozhodnutí nemění, knihovna kreslí podle Figmy (I-29, I-30), celými pixely (I-31, rozhodnutí 30) a hodnotu 0 bez výplně (I-32).

Body I-01 až I-22 a chybějící widgety k nim (`PDFIconButton`, `PDFStampChip`, `PDFNavItem`, `PDFNavList`, `PDFToolBarField`, `PDFDocumentTabBar`, `PDFKeyValueList`, `PDFAccentHue`, předvolby řádků pohledů) jsou naprogramované 15. 10. 2026 ([components_fixes.md](components_fixes.md), kapitola 14). Všech pět kroků 6.3 je naprogramovaných (6.6). Z rozdílů knihovny proti Figmě zůstává jen I-24 (barvy sloupců určuje model); I-13, I-21 a I-23 vyřešili delegáti kroku 5.

Nesrovnalosti Figmy, které opravené nejsou (review, část F), mají u sebe napsáno, jak je implementace vezme. Z nových nálezů (část N) zůstává jen zmírněný N-09 (2.4).

### 8.5 Rozhodnuto 17. 10. 2026

| # | Otázka | Rozhodnutí |
| --- | --- | --- |
| 27 | Najetí myší na Tag (I-25) | Hover musí být jasně vidět. Hover má výplň `bg/subtle` + `bg/hover`, Pressed `bg/subtle` + `bg/pressed` (dvě výplně jako u rozhodnutí 21), obrys `border/default` zůstává. Dřívější `bg/muted` a `bg/track` měly proti klidu kontrast 1,02 a 1,19 : 1 (světlý režim), 1,03 a 1,19 : 1 (tmavý; stisk tam stínoval do tmavší barvy). Nový Hover má 1,13 / 1,24 : 1, Pressed 1,25 / 1,46 : 1 (světlý / tmavý), tedy totéž co nevybraný Filter chip a Button Secondary (`bg/surface+bg/hover` proti `bg/surface` 1,13 / 1,23 : 1), proto se obrys při Hover a Pressed nezesiluje (také Filter chip má v Hover obrys `border/default`). Figma i knihovna jsou opravené 17. 10. 2026. |
| 28 | Plocha Star button a Color swatch (I-26) | Hover a Pressed nejsou jen průsvitná vrstva: `bg/hover` / `bg/pressed` leží nad vlastní plochou `bg/surface` (jako rozhodnutí 21), ve všech variantách (nepřipnutá i připnutá hvězda, vzorek vybraný i nevybraný, `Kind=Color` i `None`). Hover hvězdy v řádku pod myší (Recent file row) se tak nesčítá s hoverem řádku a vypadá stejně na každém pozadí. Ostatní beze změny (Default a Focus bez výplně, Disabled bez hoveru). Figma i knihovna jsou opravené 17. 10. 2026. |
| 29 | Šipka Collection row (I-27) | Sjednoceno s Link row a Recent tool row: šipka je v klidu a ve Focus `icon/muted`, v Hover a Pressed `icon/default`. Figma i knihovna jsou opravené 17. 10. 2026. |
| 30 | Řádkování shrnutí Version row (I-28) | Řádky shrnutí mají 14 px. Výška řádku textu `summary` je ve Figmě pevných 14 px (dříve 130 % z 11 px = 14,3 px, přitom textové pole dvou řádků mělo 28 px). Rozměry řádků se nezměnily (60 px se dvěma řádky, 46 px s jedním); knihovna 14 px kreslila už dříve. |
