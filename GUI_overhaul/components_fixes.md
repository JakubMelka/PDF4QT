# PDF4QT – opravy komponent ve Figmě (13. 10. 2026)

Seznam oprav, kterými se 13. a 14. 10. 2026 ve Figmě srovnaly nesrovnalosti stránky **Components** (body F-01 až F-85 z tehdejšího souboru nálezů). Co opravené není a co čeká na rozhodnutí, je v [components_review.md](components_review.md). Stav komponent po opravách popisuje [components_spec.md](components_spec.md).

## Obsah

1. [Souhrn](#1-souhrn)
2. [Co ve Figmě přibylo](#2-co-ve-figmě-přibylo)
3. [Pravidla, podle kterých se opravovalo](#3-pravidla-podle-kterých-se-opravovalo)
4. [Opravy podle bodů](#4-opravy-podle-bodů)
5. [Opravy nad rámec seznamu](#5-opravy-nad-rámec-seznamu)
6. [Co se změnilo na obrazovkách](#6-co-se-změnilo-na-obrazovkách)
7. [Ověření](#7-ověření)
8. [Doplněno 14. 10. 2026](#8-doplněno-14-10-2026)
9. [Fokus a zakázaný stav (14. 10. 2026)](#9-fokus-a-zakázaný-stav-14-10-2026)
10. [Rozhodnutí a nálezy N (14. 10. 2026)](#10-rozhodnutí-a-nálezy-n-14-10-2026)
11. [Barvy kategorií toolů a zakázané stavy (14. 10. 2026)](#11-barvy-kategorií-toolů-a-zakázané-stavy-14-10-2026)
12. [Zpětná vazba k obrazovkám (15. 10. 2026)](#12-zpětná-vazba-k-obrazovkám-15-10-2026)
13. [Rozhodnutí ke zpětné vazbě (15. 10. 2026)](#13-rozhodnutí-ke-zpětné-vazbě-15-10-2026)
14. [Body I v knihovně Pdf4QtLibFluent (15. 10. 2026)](#14-body-i-v-knihovně-pdf4qtlibfluent-15-10-2026)
15. [Výplně stavů, oko hesla, Checkbox a Radio (16. 10. 2026)](#15-výplně-stavů-oko-hesla-checkbox-a-radio-16-10-2026)
16. [Plná modrá, nabídka Annotate a dráha přepínače (16. 10. 2026)](#16-plná-modrá-nabídka-annotate-a-dráha-přepínače-16-10-2026)
17. [Tag, Star button, Color swatch, Collection row a Version row (17. 10. 2026)](#17-tag-star-button-color-swatch-collection-row-a-version-row-17-10-2026)
18. [Implementace komponent v knihovně (16.–17. 10. 2026)](#18-implementace-komponent-v-knihovně-1617-10-2026)

## 1. Souhrn

| Výsledek | Bodů | Body |
| --- | --- | --- |
| Opraveno | 64 | viz kapitoly 4, 8 a 9 (F-70 a F-71 v kapitole 9) |
| Opraveno zčásti | 5 | F-21, F-32, F-38, F-41, F-54 |
| Ponecháno, není to chyba | 15 | F-05, F-10, F-11, F-22, F-34, F-37, F-51, F-53, F-61, F-62, F-68, F-69, F-73, F-74, F-81 |
| Čeká na rozhodnutí | 1 | F-67 |

Stránka *Components* má po opravách **92 komponent**: 70 sad s 552 variantami a 22 samostatných (před opravami 89: 67 sad se 451 variantami a 22 samostatných). Ikon je stále 363.

Stav před opravami je ve Figmě v historii verzí (*File → Show version history*, poslední automatická verze před 13. 10. 2026 večer). Pojmenovanou verzi se skriptem vytvořit nepodařilo.

## 2. Co ve Figmě přibylo

**Proměnné kolekce *Color*** (135, dříve 133; obě jsou na konci kolekce):

| Proměnná | Light | Dark | K čemu |
| --- | --- | --- | --- |
| `bg/accent-hover` | `#1d4ed8` | `#2563eb` | výplň akcentového prvku pod myší (Button Primary, vybraný Filter chip) |
| `bg/accent-pressed` | `#0f4796` | `#1d4ed8` | výplň akcentového prvku při stisku; také obrys při hoveru a výplň při stisku zaškrtnutého Checkboxu a Radia |

Světlé hodnoty jsou stejné jako dosud (`accent/blue/icon`, `text/accent-pressed`), světlý režim se tedy nezměnil. V tmavém režimu má bílý text na obou výplních kontrast nejméně 5 : 1; dříve byly výplně světle modré.

**Komponenty:**

| Komponenta | Uzel | Popis |
| --- | --- | --- |
| Icon button | `706:147674` | malé tlačítko jen s ikonou: `Size` 24 / 28, `State` Default / Hover / Pressed / Focus / Disabled, vlastnost `Icon` |
| Menu separator | `706:150117` | oddělovač položek nabídky, 240×7 |
| Menu section | `706:150119` | nadpis skupiny položek nabídky, 240×24, vlastnost `Label` |

Scrollbar a Context menu / Recent file se staly sadami (`706:148919`, `706:150896`); původní komponenty `106:2120` a `64:592` jsou jejich výchozí varianty a instance na ně dál odkazují.

## 3. Pravidla, podle kterých se opravovalo

- **Jen tokeny.** Každá barva je proměnná kolekce *Color*.
- **Fokus je Default s kroužkem.** Varianta Focus se od Default liší jen kroužkem `border/focus` 2 px; nemění výplň ani barvu textu a ikony.
- **Stálý rozměr.** Varianty lišící se jen stavem mají stejný vnější rozměr a obsah na stejném místě.
- **Pole mají kroužek uvnitř** (místo obrysu), ostatní prvky ho mají tam, kde ho měly. Umístění kroužku se 13. 10. hromadně nesjednocovalo; sjednotilo se 14. 10. podle rozhodnutí Q-2 (kapitola 9).
- **Pole nemají stav Pressed.** Stisk pole mu dá fokus.
- **Co kreslí styl, řídí stránka *Widgets*.** Chybějící stav komponenty je nakreslen tak, jak ho kreslí *Widgets* a hotový kód. Kde si komponenta a *Widgets* odporují v něčem, co existuje na obou místech, nezměnilo se nic a rozpor je v souboru nálezů.
- **Instance se nerozbíjejí.** Žádná vlastnost ani hodnota varianty se nepřejmenovala. Kde se vrstva nahradila instancí, přepsání na obrazovkách se nejdřív zapsala a pak obnovila.

## 4. Opravy podle bodů

### 4.1 Chybějící stavy

| Bod | Komponenta | Oprava |
| --- | --- | --- |
| F-01, F-82 | Radio | `State` Default / Hover / Pressed / Focus / Disabled pro obě hodnoty `Checked` (2 → 10 variant), přepínač `Show label`, popis. |
| F-02 | Slider | `State` pro všech pět hodnot (5 → 25 variant): hover obrys úchytu, stisk výplň úchytu, fokus kroužek kolem úchytu, Disabled tlumená výplň a úchyt. |
| F-03 | Scrollbar | Sada `Orientation` Vertical / Horizontal × `State` Default / Hover / Pressed (1 → 6 variant). |
| F-04 | Toolbar button | Doplněna kombinace `Active=True, State=Disabled`. |
| F-06, F-23 | Toolbar field | `State=Disabled`; Focus má šířku Default (93 → 91). |
| F-07 | Search field | `State=Disabled` a osa `Filled` (zadaný text a tlačítko smazání ve stavech Default, Hover, Focus), vlastnost `Value`. |
| F-08 | Text field | `State=Error`: obrys `text/danger` 1 px uvnitř. |
| F-09 | Select, Password field, Color button | `State=Disabled`. |
| F-12 | Certificate row, Font row | `State` Pressed a Focus (každá sada 4 → 8 variant). |
| F-13 | Property row | Tlačítka akcí jsou instance Icon button, mají tedy všechny stavy. |
| F-14 | Tool choice | `State` Default / Hover / Pressed / Focus pro obě hodnoty `Selected` (2 → 8 variant). |
| F-15 | Signature card | Přepínač rozbalení je instance Icon button. |
| F-16 | Shortcut row | Akce bez zkratky kreslí čárkovanou klávesu „Add shortcut“; nový stav `State=Recording` („Press keys…“). |
| F-17, F-80 | Attachment row | Osa `Selected` jako Bookmark row (4 → 8 variant); `save` je instance Icon button, ikona při hoveru jako u ostatních řádků. |
| F-18 | Password field | Přepínač viditelnosti je instance Icon button 28. Chyba zůstává hodnotou `State`, zpráva o chybě patří formuláři. |
| F-19 | Context menu / Recent file, Context menu / Thumbnails | Řádky jsou instance Menu item (32 px) a Menu separator, všechny v Default. Recent file je sada `File` Default / Pinned / Missing podle popisu. *Remove Pages* je nebezpečná položka jako *Remove from list*. |
| F-20 | Menu item | Přepínače `Show icon` a `Submenu` (šipka vpravo), osa `Tone` Default / Danger (5 → 10 variant); nové komponenty Menu separator a Menu section. |
| F-78 | Property row | `Editable=True, State=Copied` má akci `edit`; popis opraven podle kresby (editovatelný řádek ukazuje `edit` stále). |

### 4.2 Fokus a rozměry

| Bod | Komponenta | Oprava |
| --- | --- | --- |
| F-24 | Select, Password field | Kroužek je uvnitř místo obrysu, jako u ostatních polí. |
| F-25 | Tag, Video card, Button Secondary, Filter chip, Select | Varianta Focus má rozměr Default. |
| F-26 | Tool row, Collection row, Issue row, Settings nav item, Shortcut row, Pinned card, Folder card, Video card, Color button, Text field, Spin box, Combo box, Tool shortcut | Kroužek už nezužuje ani neposouvá obsah. |
| F-23 | Stamp chip | Kroužek vně jako u Filter chipu; všech 16 variant má 85×26. |
| F-27 | Color swatch | Vybraný vzorek s fokusem má obrys výběru i kroužek fokusu kolem něj. |
| F-28, F-52 | Reading theme card | Výběr a fokus kreslí rámec `ring` jako u Color mode card; všech 8 variant má 159×86 (dříve 5 variant 159×90). |
| F-29 | Tool choice | Výběr je obrys `bg/accent` 2 px a výplň `bg/accent-subtle` jako u Color mode card; fokus je kroužek vně. |
| F-30 | Toolbar button | Focus už nemění barvu ikony. |

### 4.3 Obsah, který nešel nastavit

| Bod | Komponenta | Oprava |
| --- | --- | --- |
| F-31 | Tool row | Štítek je vystavená instance, text jde nastavit. |
| F-32 | Recent tool row, Workspace row | Kruh ikony je vystavená instance Tool icon; odstín a ikona 34 instancí na obrazovkách obnoveny. Nepoužitá vlastnost `Icon` u Recent tool row smazána. |
| F-33 | Pinned card, Recent item row | Náhled stránky je vystavená instance. |
| F-35 | Continue with | Vlastnosti `Title`, `Title note`, `Show first`; odkaz je vystavená instance Text link. |
| F-36 | Bookmark row, Font row, Issue row | Štítky jsou vystavené instance; Issue row má vlastnost `Status icon`. |
| F-38 | Password field | Vlastnost `Value` (ve variantách `Content=Visible`). |
| F-39 | Page selection | Vnořené chipy, zaškrtávátka a pole jsou vystavené. `Error` je osa variant (4 → 8 variant): při chybě je pole ve stavu Error. Šest instancí s chybou na obrazovkách převedeno. |
| F-40 | Version row | Přepínač `Show line`. |
| F-42 | Progress bar | Výplň se roztahuje se šířkou instance. |
| F-59 | Search result row | Hledaný výraz v úryvku je Semi Bold a `text/primary`. |

### 4.4 Rozdíly mezi sourozenci

| Bod | Komponenta | Oprava |
| --- | --- | --- |
| F-43, F-44, F-75 | Folder card, Resource card | Jeden recept barev jako Quick action tile: klid `accent/<hue>/bg` + `border`, hover `bg-hover` + `border-strong`, stisk `bg-pressed` + `border-strong`; platí i pro Neutral. |
| F-45 | Tool shortcut | Hover `accent/neutral/bg-hover`, stisk `bg-pressed`. |
| F-46 | Recent file row | Ikona `more` je v klidu `icon/muted`, při hoveru `icon/default`. |
| F-47 | Recent tool row | Texty se zkracují. |
| F-48 | Quick action tile, Tool icon | Popisy: doplněn odstín Green a význam Neutral. |
| F-49 | Toolbar menu button | Disabled má ikonu `icon/muted`, i ve všech instancích na obrazovkách (kapitola 8). |
| F-50 | Note row, Tab | Vybraný Note row má autora `text/accent`; tečka `unsaved` má barvu popisku tabu. |
| F-76 | Recent item row | `Type=Folder` používá `accent/neutral/*`. |
| F-77 | Tab | Zámek je jantarový ve všech stavech. |

### 4.5 Popisy, tokeny, drobnosti

| Bod | Komponenta | Oprava |
| --- | --- | --- |
| F-55 | Theme toggle segment | Popis podle skutečnosti: slunce segmentu Light se na instanci přepisuje na `icon/sun` (tak to má všech 20 instancí na *Main page*); stejný přepis dostala ukázka na stránce *Widgets*. |
| F-56 | Recent file row | Popis vysvětluje, že vrstva zkracuje na konci jen kvůli omezení Figmy. |
| F-57 | Strength meter | Popis: pruh z 20 buněk. |
| F-58 | Page thumbnail | Linky na celých pixelech; popis „asi 1 : 1,32“. |
| F-60 | Button, Filter chip | Hover a stisk akcentové výplně používají `bg/accent-hover` a `bg/accent-pressed`; opraveny i texty na stránce *Widgets*. |
| F-63 | Ikony `compress-flate`, `compress-rle`, `compress-fax` | Kresba vystředěna do rámu 24×24 (měla dvojnásobný posun). |
| F-64 | Mode preview/Bitonal | Oba bloky 44,5 px. |
| F-66 | Action tile, Video card | Obsah vyplňuje kořen 56 px; náhled videa vyplňuje vnitřní šířku. |
| F-83 | Tool icon | Osa `Size` 28 / 32 / 44 (7 → 21 variant), tah ikony 2 px ve všech velikostech; 508 zmenšených instancí na obrazovkách a 99 odznaků v Continue with nahrazeno variantou. |
| F-84 | Tab | Všechny varianty mají výšku 32; domovský tab je 85×32. |
| F-85 | 39 uzlů | Skryté výplně bez proměnné odstraněny. |

## 5. Opravy nad rámec seznamu

Na tyto věci se přišlo až při opravách.

| Komponenta | Oprava |
| --- | --- |
| Checkbox, Radio (i jejich kresby na stránce *Widgets*) | Obrys při hoveru a výplň při stisku zaškrtnutého stavu: `text/accent-pressed` → `bg/accent-pressed` (stejná světlá hodnota; v tmavém režimu už není světle modrá pod bílým zatržítkem). |
| Password field | Stav Error se kreslí jako u Text fieldu a na stránce *Widgets*: `text/danger` 1 px uvnitř (dříve `icon/danger` 2 px vně). |
| Filter chip | Vybraný chip má šířku nevybraného (61 → 63); výběr už nemění šířku. |
| Progress bar na obrazovkách | Šest instancí dostalo hodnotu odpovídající textu vedle (viz kapitola 6). |
| Status bar item (ukázka na stránce *Widgets*) | Varianta Warning má ikonu `alert-triangle`. |
| Stránka *Widgets*, texty | Opraveny názvy tokenů u zaškrtávátka, přepínače a posuvníku. |
| Sekce *Components* | Všech 97 uzlů přeskládáno do řad s odstupem 80 px, nové komponenty jsou u příbuzných; sekce má 2950×14549. Nic se nepřekrývá. |

## 6. Co se změnilo na obrazovkách

Opravy komponent se propsaly do instancí. Záměrné viditelné změny:

- **Domovský tab** je vysoký 32 místo 28 a sedí v pruhu jako ostatní taby (všechny obrazovky).
- **Karty složek a zdrojů** na Home mají barevný obrys podle odstínu; neutrální karty mají výplň `#f5f5f5` místo `#f7f7f7`.
- **Odznaky toolů** velikosti 28 a 44 mají ikonu s tahem 2 px. Deset odznaků ve výsledcích hledání na *Tools Overview* má ikonu 14 místo 16.
- **Pruhy průběhu** ukazují skutečný podíl. Dosud měl pruh široký 261 px při hodnotě 25 výplň 30 px (11 %). Šest pruhů, kde hodnota neodpovídala textu, je přepnuto: *Adding page numbers* 640 z 1 592 → 50, *file 3 of 4* → 75, *Scanning page 2 of 3* → 75, *Creating page 4 of 6* → 75, *Writing image 120 of 409* → 25, *page 31 of 48* → 75.
- **Kontextové nabídky** mají řádky 32 px (nabídka souboru 165 → 181, nabídka náhledů 196 → 220).
- **Vybraný chip** je o 2 px širší.
- **Vybraná karta motivu čtení** je o 4 px nižší.
- **Pole hesla** má kroužek fokusu uvnitř (10 instancí) a chybu jako tenký červený obrys (14 instancí).
- **Page selection s chybou** má červeně orámované pole (6 instancí).
- **Dva řádky zkratek** bez zkratky na *Main page* ukazují „Add shortcut“.
- **Tři ikony komprese** jsou vystředěné.
- **Jedna vybraná volba Tool choice** (*Tools - Organize Pages*) má nový obrys výběru.

## 7. Ověření

- **Kontrola komponent skriptem** (po všech opravách): žádná viditelná barva mimo proměnné; všechny varianty lišící se jen stavem mají stejný rozměr (před opravami 11 výjimek); žádná nová varianta nemá odpojenou vlastnost, kterou sourozenci mají navázanou; uvnitř sad se varianty nepřekrývají a v sekci se nepřekrývá žádná dvojice uzlů.
- **Otisk obrazovek.** Před opravami a po nich se pro všech deset stránek obrazovek a stránku *Widgets* spočítal otisk vzhledu instancí každé komponenty (rozměry, barvy, texty, polohy uvnitř instance). Změnily se jen komponenty, u kterých to opravy čekají; mezi nezměněnými jsou na obrazovkách mimo jiné Button, Checkbox, Combo box, Radio, Spin box, Status bar item, Toolbar button, Toolbar menu button, Tag, Key value row a Thumbnail item.
- **Mez otisku.** Domovský tab změnil výšku na všech obrazovkách a vybrané chipy šířku, takže se posunuly polohy instancí skoro ve všech rámcích (beze změny zůstalo 43 z 596 rámců obrazovek). Otisk vrstev, které nejsou instancemi komponent, proto nerozliší posun od jiné změny. Tyto vrstvy opravy neupravovaly, kromě záměn popsaných v kapitole 6.
- **Snímky.** Každá změněná sada byla po opravě vyfocena a prohlédnuta; u oprav s dopadem na obrazovky i dotčený rámec obrazovky před a po.
- **Neověřeno:** tmavý režim na snímcích (barvy jsou navázané na proměnné, nové hodnoty tmavého režimu jsou spočítané na kontrast, ne prohlédnuté), prototypové vazby mimo ty, které opravy výslovně obnovily, a instance Tab na osmi stránkách toolů jednotlivě (kontrolovány dvě stránky a otisk).

## 8. Doplněno 14. 10. 2026

Podle rozhodnutí z 14. 10. 2026 (otázky Q-1 a Q-3 souboru nálezů).

| Bod | Komponenta | Oprava |
| --- | --- | --- |
| F-72, F-65, F-37 | Color palette popover | Vzorky jsou instance kulatého Color swatch (dříve čtvercové rámce), vybraná barva je varianta `Selected=True`. Dvě řady po šesti vzorcích (dříve 5 / 5 / 2): obrys kořene se nepočítá do rozložení, vnitřní šířka je 208. Popover má 232×209 (dříve 232×247). Na obrazovkách použitý není. |
| F-79, F-49 | Toolbar button, Toolbar menu button, Menu item | Stav Disabled se kreslí jen tlumenými barvami jako na stránce *Widgets* a v hotovém stylu: krytí kořene 0,5 (tlačítka) a 0,6 (položka nabídky) je pryč, ikona `icon/muted`, text `text/muted`; zatržítko zakázané položky nabídky je `icon/muted` (dříve `icon/accent`). |
| F-79 | Obrazovky | Všech 1 627 zakázaných instancí těchto tří komponent na všech stránkách má teď krytí 1 a jen barvy `icon/muted` a `text/muted`. 664 z nich drželo ikonu `icon/default` jako místní přepis (záměna ikony si barvu pamatuje): 335 tlačítek na *Tools - Edit*, 296 tlačítek s nabídkou na sedmi stránkách, 33 dalších tlačítek; přepis je změněn na `icon/muted`. |

Ověření:

- **Kontrola všech zakázaných instancí** po změně: 1 627 instancí na 12 stránkách, žádná s krytím pod 1 a žádná s jinou barvou než `icon/muted` / `text/muted`. Čtyři tlačítka Undo / Redo na *Tools - OCR* leží v pruhu panelu, který má celý krytí 0,5 (N-19 v souboru nálezů); je to kompozice obrazovky, změna ji neovlivnila.
- **Otisk obrazovek** před změnou a po ní: na sedmi stránkách toolů se změnil jen vzhled instancí Toolbar button a Toolbar menu button a jejich ikon, na stránce *Widgets* Toolbar button a Menu item, na stránce *Components* paleta a zakázaná položka nabídky; vrstvy mimo komponenty se nezměnily nikde. Povolené instance se nezměnily. (Na *View Document* a v rámci `pdf4qt-recent` na *Main page* se mezitím změnilo víc, ale ne těmito opravami: jsou tam dva nové rámce `pdf4qt-document-search-mark*` a upravený pruh nástrojů, který tyto opravy nevytvořily.)
- **Výjimka:** Icon button si krytí 0,5 ponechal. Jeho ikona je tlumená už v klidu, takže tlumená barva by zakázané tlačítko od povoleného neodlišila. Výjimka odpadla s tokeny v kapitole 9.
- **Kontrast:** zakázaná ikona toolbaru měla po této změně kontrast 4,8 : 1 proti bílé, povolená 7,6 : 1 (dříve zakázaná asi 2 : 1). Rozdíl zvětšily tokeny v kapitole 9.

## 9. Fokus a zakázaný stav (14. 10. 2026)

Podle rozhodnutí z 14. 10. 2026 (otázky Q-2 a Q-8 souboru nálezů). Měřítkem bylo, aby se vzhled dal v Qt nakreslit přímo a bez obcházení.

### 9.1 Umístění kroužku fokusu (Q-2, F-70, F-71)

Pravidlo: **pole a řádky seznamů a nabídek mají kroužek uvnitř, volně stojící prvky vně.**

Proč právě takto (Qt):
- **Řádky** v seznamech na sebe doléhají a seznam je ořezává. Kreslí je delegát pohledu nebo `paintEvent` widgetu řádku a Qt ořezává kreslení na obdélník položky. Kroužek vně by byl uříznutý, uvnitř se nakreslí bez triků.
- **Položka nabídky** je jen obdélník uvnitř panelu `QMenu`. Klávesou vybraná položka je `State_Selected` a styl podle svého sledování vstupu rozliší klávesnici od myši.
- **Volně stojící prvky** (tlačítka, chipy, karty) dostanou kroužek vně od `QFocusFrame` stylu, jako dosud tlačítka.

| Komponenta | Změna |
| --- | --- |
| Nav item, Menu item, Link row, Recent file row, Recent tool row, Workspace row, Recent item row, Outline row, Bookmark row, Note row, Attachment row, Search result row, Certificate row, Font row, Thumbnail item | Varianty Focus: kroužek `border/focus` 2 px `INSIDE` (dříve `OUTSIDE`). Kde se obrys počítal do rozložení, má Focus nově `sil=0`, aby se obsah neposunul; rozměr i poloha všech potomků jsou stejné jako v Default (ověřeno skriptem u všech 28 variant). Popisy Certificate row a Font row upravené. |
| Video card | Kroužek `OUTSIDE` (dříve `INSIDE`); mezi kroužkem a obsahem zůstává 1 px výplně. |
| Reading theme card, Color mode card | Nová absolutní vrstva `focus-ring` (`border/focus` 2 px `OUTSIDE`, r12). U vybrané karty zůstává výběr (`ring` `bg/accent` 2 px `INSIDE`); dříve fokus výběr nahradil a ve světlém režimu nebyl vidět (N-09). |
| Stránka *Widgets* | Pravidlo v bloku *Focus ring* (sekce 00), tabulka pod ním, popisky ukázek Menu item a Nav item, text sekce 10 (položka vybraná klávesnicí: kroužek uvnitř, bez výplně Hover) a sekce 11 (svislý tab bar: kroužek uvnitř, r10). |

Proti návrhu z 13. 10. se liší jedna věc: **Tag zůstal s kroužkem uvnitř.** Z 935 instancí je skoro každá uvnitř karty, řádku nebo buňky tabulky (68 u ořezávajícího okraje, některé se překrývají se sousedy). Kroužek vně by se ořízl nebo vlezl do sousedů a v Qt štítek kreslí delegát řádku.

### 9.2 Tokeny zakázaného stavu (Q-8)

| Proměnná | Světlý | Tmavý | Kontrast proti `bg/surface` | Rozsah |
| --- | --- | --- | --- | --- |
| `text/disabled` | `#94a3b8` | `#525e72` | 2,56 : 1 / 2,52 : 1 | `TEXT_FILL` |
| `icon/disabled` | `#94a3b8` | `#525e72` | 2,56 : 1 / 2,52 : 1 | `SHAPE_FILL`, `STROKE_COLOR` |

Kolekce *Color* má 137 proměnných, obě nové jsou na konci. Hodnoty odpovídají zvyklosti Fluent (zakázaný text asi 2,5 : 1). Proti panelu nástrojů (`bg/subtle`) je kontrast 2,45 : 1 ve světlém a 2,38 : 1 v tmavém režimu. Povolená ikona `icon/default` má 7,6 : 1, tlumená `icon/muted` 4,8 : 1, takže zakázaná se od obou zřetelně liší.

| Kde | Změna |
| --- | --- |
| Komponenty | Všech 28 variant Disabled v 16 sadách (Button, Search field, Select, Checkbox, Radio, Combo box, Spin box, Text field, Slider, Color button, Toolbar button, Toolbar menu button, Toolbar field, Menu item, Password field, Icon button): text `text/disabled`, ikony `icon/disabled` (24 textů, 27 ikon). Zatržítko a tečka na akcentové výplni zůstávají `icon/on-accent`. Slider žádný text ani ikonu nemá. |
| Krytí | Icon button Disabled bez krytí (dříve 0,5) a přepínač hesla v Password field Disabled také (dříve navíc 0,5). Žádná varianta Disabled teď nemá krytí. |
| Obrazovky | 1 812 z 1 981 zakázaných instancí na 11 stránkách mělo ikonu jako místní přepis a ten je převeden na `icon/disabled`. Skoro všechny měly `icon/muted`, 9 tlačítek na *Tools - OCR* `icon/default` (vypadala jako povolená). Texty se dědí z komponent, přepis neměly. |
| Stránka *Widgets* | 29 zakázaných kreseb (19 textů, 10 ikon) v sekcích 01–06, 08, 10–12 a 14 a 23 popisných textů (`text/muted` / `icon/muted` u zakázaného stavu → `text/disabled` / `icon/disabled`), řádek *Disabled group* tabulky palety. Tlumené texty, které zakázané nejsou (zkratky, nadpisy skupin, šipky podnabídky, druhé řádky položek), zůstaly. |
| Popisy komponent | Select, Toolbar field, Toolbar button, Menu item, Icon button. |

### 9.3 Ověření

- **Kontrola komponent skriptem:** žádná barva mimo proměnné, stejné rozměry variant lišících se stavem, nic se nepřekrývá, ve variantách Disabled žádný `text/muted`, `icon/muted` ani krytí.
- **Všechny zakázané instance** (1 981 na 11 stránkách) kreslí jen `text/disabled`, `icon/disabled` a u zaškrtnutých polí `icon/on-accent`, žádná nemá krytí.
- **Otisk všech 12 stránek** před změnou a po ní: na stránkách toolů se změnily jen komponenty se zakázaným stavem (Button, Toolbar menu button, Text field, Combo box, Spin box, Checkbox a ikony). Vrstvy mimo komponenty se nezměnily nikde, *Main page* se nezměnila vůbec. Na stránce *Components* se změnilo právě 33 upravených sad.
- **Snímky:** Reading theme card, Menu item, Video card, Toolbar button, blok *Focus ring* a nabídka v sekci 10 stránky *Widgets*.
- **Nalezeno mimochodem:** dva problémy rozvržení, opravené v kapitole 9.4.

### 9.4 Rozvržení (N-20, N-21)

- **Color mode card na *Main page*:** řada `modes` (`160:4351`) měla mezi kartami 2 px, takže se kroužek fokusu vně dotkl sousední karty. Mezera je teď 8 px jako u řad Reading theme card na téže stránce. Karty se zúžily ze 135 na 130 px, popisky se nezalomily a výška obrazovky zůstala stejná.
- **Stránka *Widgets*:** rámec `widgets-11-tabs` (výška 2 349) zasahoval o 89 px do rámce sekce 12. Překryv vznikl dřív, ne úpravami z kapitoly 9. Sekce 12–17 jsou posunuté o 149 px dolů, takže mezi sekcemi 11 a 12 je 60 px jako mezi ostatními; sekce stránky je o 149 px vyšší (30 309).

## 10. Rozhodnutí a nálezy N (14. 10. 2026)

Podle odpovědí na otázky Q-4 až Q-6 a podle pokynu opravit nálezy N, které nepotřebují rozhodnutí. Měřítkem bylo, aby se vzhled dal v Qt nakreslit přímo.

### 10.1 Rozhodnutí Q-4 až Q-6

- **Q-4 Zkrácení názvu souboru:** aplikace zkracuje uprostřed a ponechá příponu. Figma zkrácení uprostřed neumí, kresba se nemění.
- **Q-5 Vybraná položka pod myší:** vybraná položka skupiny s jedním výběrem na myš nereaguje. Nav item má nově u `Selected=True` v Hover a Pressed výplň `bg/accent-subtle` jako v Default (varianty `47:108`, `47:113`; dříve `bg/accent-subtle-hover` a `-pressed`), stejně jako Settings nav item, Tab a Theme toggle segment. Text svislého tab baru na stránce *Widgets* (sekce 11, `620:1723`) to říká také. Certificate row je řádek tabulky a kreslí ho styl jako ostatní pohledy: vybraný řádek reaguje, Hover má nově `bg/accent-subtle-hover` (`145:2923`, dříve beze změny), Pressed zůstal `bg/accent-subtle-pressed`, jako u řádků panelů dokumentu a podle sekce 12 stránky *Widgets*. Popisy obou komponent jsou doplněné. Žádná instance na obrazovkách tyto varianty nepoužívá.
- **Q-6 Seznamy jako widgety:** jen návrh implementace, Figma se nemění.

### 10.2 Barvy (N-02, N-04, N-11, N-16)

- **N-02:** šipky Spin boxu, Combo boxu, Selectu a nově i Color buttonu (také pole s rozbalovací nabídkou) jsou v povolených stavech `icon/default` místo `icon/muted`, jak je kreslí stránka *Widgets* a styl. Instance na obrazovkách barvu převzaly, žádná ji neměla přepsanou ručně.
- **N-04:** zatržítko položky nabídky (`check`) je v osmi povolených variantách Menu item `icon/default` místo `icon/accent`.
- **N-11:** Resource card má nový odstín Violet (čtyři varianty, popis sady ho přiřazuje videotutoriálům) a karta „Video Tutorials“ (`51:414`) je jeho instancí místo ručně přebarvené `Hue=Neutral`. Dlaždice „Edit Content“ na Home (`50:407`) byla přebarvená na fialovou podle tehdejšího popisu Quick action tile; po rozhodnutí o barvách kategorií je znovu zelená (kapitola 11.1).
- **N-16:** Collection row `95:1801` na *Main page* má ikonu `icon/accent` jako ostatní řádky seznamu (dříve ruční přepis `icon/default`).

### 10.3 Stavba komponent (N-08, N-12, N-15, N-17, N-18)

- **N-08 Property row:** stav Copied ukazuje zelené zatržítko 14 px v místě tlačítka kopírování (rámec 24×24) bez popisku „Copied“, který text dává tooltip. Tlačítko úprav se už neposouvá o 38 px a hodnota se nezužuje. Popis komponenty je upravený.
- **N-12 Slider:** úchyt si při změně šířky instance drží 14×14. U hodnoty 0 a 100 je přichycený k okraji (`MIN`, `MAX`), u 25, 50 a 75 leží v rámci `thumb-slot` s omezením `SCALE` a sám je vystředěný. Ověřeno na dočasné instanci široké 360 px: úchyt má 14×14 a jeho střed leží na konci výplně. Na obrazovkách (17 zúžených sliderů na *View Document*, další na stránkách toolů) jsou úchyty znovu kulaté.
- **N-15 Tag:** popisek má pevnou výšku řádku 13 px místo `auto`. Všechny štítky mají teď 21 px, včetně 63 štítků „✂“ na *Tools - OCR*, které měly 19 px. Ostatní štítky se nezměnily.
- **N-17 Ikony:** 26 obrysových ikon se dvěma vektory má jediný vektor. Cesty druhého vektoru jsou připojené k prvnímu, takže identifikátor i přepsané barvy instancí zůstaly, zakončení a spoje čar jsou dál `ROUND` a vykreslené hranice se nezměnily. Barvu z odznaku nebo z přepsání tak přebírá celá ikona: 98 instancí, které kreslily druhou část v `icon/default` (68 odznaků OCR na *Tools - OCR*, 9 odznaků deskew, ikony v akcentu na tlačítkách), je teď jednobarevných. Ikony `tools` (čtyři kruhy) a `compress-bitmap` (vyplněné buňky a obrys) mají dva druhy kresby a zůstaly. Soubory `.svg` v repozitáři se nemění.
- **N-18 Tool icon:** kořen má velikost podle obsahu s okraji (32 → 8, 28 → 7, 44 → 11), vzhled se nezměnil. Vnořený odznak má ve 48 instancích uvnitř komponent (Tool row, Tool shortcut, Tool choice, Recent tool row, Workspace row, Note row, Attachment row, Font row) velikost podle obsahu, takže změna `Size` na řádku změní i kruh. Ověřeno na dočasné instanci Tool row (`Size=44` dá 44×44, `Size=28` 28×28). Žádná instance na obrazovkách neměla velikost jinou, než udává její varianta.

### 10.4 Chybějící kombinace stavů (N-14)

| Komponenta | Nové varianty | Kresba |
| --- | --- | --- |
| Text field | `State=Error focus` | obrys `text/danger` 2 px uvnitř, okraje jako Focus |
| Password field | `State=Error focus` pro tři hodnoty `Content` | totéž a zámek `icon/danger` |
| Search field | `State=Disabled, Filled=True` | výplň `bg/muted`, ikona a tlačítko smazání `icon/disabled`, text `text/disabled` |
| Color swatch | `State=Disabled` pro `Kind` × `Selected` | barva zůstává, obrys vzorku `border/default`, výběr `border/strong`, přeškrtnutí `icon/disabled` |
| Scrollbar | `State=Disabled` pro obě orientace | jezdec `border/default` (*Widgets*, sekce 09) |

Popisy sad jsou doplněné. Password field je o sloupec širší, proto se sada Locked document posunula doprava (mezera 80 px zůstala). Ve Scrollbaru se vodorovné varianty posunuly o 26 px doprava, aby se vešel svislý zakázaný pruh.

### 10.5 Stránka *Widgets* (N-03, N-05, N-06, N-07)

- **N-03:** ukázka fokusu tlačítka toolbaru v sekci 02 má kroužek vně (2 px, r 10) jako komponenta; popisek, název rámce i text sekce to říkají. Tlačítka, která vytváří `QToolBar`, fokus nedostanou (`Qt::NoFocus`).
- **N-05:** nadpis skupiny nabídky v sekci 10 má 24 px s textem uprostřed (dříve 21 px podle obsahu), jako komponenta Menu section.
- **N-06:** zaškrtnutý přepínač a částečně zaškrtnuté pole s fokusem mají v sekci 03 kroužek vně (na výplni `bg/accent` by vnitřní kroužek nebyl vidět); text sekce to popisuje. Styl to tak už kreslí.
- **N-07:** úvodní texty sekcí 03, 04, 07 a 09 popisují stavy, které komponenty mají dnes (Radio, Slider a Scrollbar se všemi stavy, chyba pole, tlačítko smazání).

### 10.6 Obrazovky (N-10)

- **N-10:** v deseti řádcích výsledků hledání na *Tools - Protect and Sign* je nalezený text (`jane.novak@…` podle výrazu `jane\.novak@\S+`) Semi Bold v `text/primary`. Dva úryvky s delší adresou se tím zalomily na druhý řádek; seznam se na obrazovku vejde.

### 10.7 Ověření

- **Otisk všech 12 stránek před změnami a po nich** (metoda z kapitoly 9.3). Změnily se jen skupiny, které vysvětlují úpravy:
  - Tag (výška řádku);
  - ikony (sloučené vektory; počet kreslených uzlů klesl přesně o počet instancí sloučených ikon, například o 600 na *Tools - OCR*);
  - Slider (úchyty zúžených instancí);
  - Search result row a prosté vrstvy pod ním na *Tools - Protect and Sign* (zalomené úryvky);
  - prosté vrstvy na *Tools - OCR* (štítky „✂“ o 2 px vyšší);
  - Quick action tile a Resource card na *Main page*;
  - na stránce *Components* upravené sady, na stránce *Widgets* upravené sekce.

  Ostatní komponenty a vrstvy se nezměnily.
- **Kontrola stránky *Components* skriptem:** žádná barva mimo proměnné, stavy mají rozměr varianty Default, varianty uvnitř sad se nepřekrývají a sady jsou od sebe aspoň 20 px.
- **Snímky:** seznam stránek s „✂“, zúžený slider, výsledky hledání na *Protect and Sign*, Password field, Color swatch, Property row, Search field, dlaždice OCR a ukázky v sekcích 02, 03 a 10 stránky *Widgets*. Žádný vnější kroužek fokusu na stránce *Widgets* není oříznutý rodičem.

## 11. Barvy kategorií toolů a zakázané stavy (14. 10. 2026)

Podle odpovědí na Q-7, N-11, N-19 a N-22. Q-7 Figmu nemění: Thumb preview a Sample page se neimplementují (review O-02).

### 11.1 Barvy kategorií toolů (N-11)

Platí karta `pdf4qt-tools` na *Tools Overview* (`272:12821`), část All tools:

| Kategorie | Odstín |
| --- | --- |
| Organize Pages | Amber |
| Create & Convert | Blue |
| Edit | Green |
| Protect & Sign | Red |
| Optimize | Violet |
| Review & Inspect | Teal |

Komprese patří do Optimize, je tedy fialová. Kontrola odznaků (Tool icon), dlaždic (Quick action tile) a hlaviček kategorií na všech stránkách ukázala, že stránky toolů, *Tools Overview* a *View Document* už toto schéma mají. Lišila se jen stránka *Main page*:

- dlaždice „Edit Content“ na Home (`50:407`) je zelená (v kapitole 10.2 byla přebarvená na fialovou podle starého popisu); ikona převzala `accent/green/icon`;
- kopie katalogu `pdf4qt-tools` na *Main page* (`91:1319`) měla schéma obráceně: hlavička „Edit & Forms“ (`94:1754`) a její čtyři tooly jsou zelené, hlavička „Optimize“ (`94:1925`) a její tři tooly fialové, dlaždice „Compress PDF“ (`94:1537`) je fialová včetně ikony (dříve zelená dlaždice s modrou ikonou);
- poslední tooly v téže kopii měly všechny Amber, teď mají barvu své kategorie: Compress PDF Violet, OCR Blue, Merge PDF Amber, Compare PDFs Teal, Redact Content Red (stejně jako na *Tools Overview*).

Rozdělení toolů do kategorií v kopii na *Main page* se nemění (je to starší podoba katalogu). Popisy Quick action tile, Tool icon a Tool category header uvádějí nové schéma. Karta „Video Tutorials“ má dál vlastní odstín Violet sady Resource card; Resource card není tool.

### 11.2 Zakázané chipy (N-19)

- **Filter chip** `78:836` má `State=Disabled` pro obě hodnoty `Selected` (`792:25870`, `792:25875`): text, počet a ikona `text/disabled` a `icon/disabled`; vybraný chip výplň `bg/muted` (jako Button Primary), nevybraný `bg/surface` s obrysem `border/default` (jako Button Secondary). Rozměr 63×32 a okraje jsou jako u Default, bez krytí. Sada se rozšířila o sloupec na 459 px, Star button, Icon button a Tag na stejném řádku se posunuly o 89 px doprava (mezery zůstaly).
- **Obrazovky:** řady chipů a výběr stránek, které během běhu toolu nejdou použít, měly krytí 0,5. Krytí je zrušené, chipy, zaškrtávací pole a pole rozsahu jsou ve stavu Disabled, popisky výběru stránek `text/disabled` a ikona stavu `icon/disabled`:

| Stránka | Rámce | Co |
| --- | --- | --- |
| *Tools - OCR* | `convert-ocr-preparing`, `convert-ocr-running`, `convert-ocr-applying` | `panel-tabs` (Settings / Review / Find, tlačítka Undo / Redo už zakázaná byla), `settings-tabs` (Recognition / Image / Output / Advanced), `page-selection` |
| *Tools - Edit* | `edit-page-numbers-running` | `page-selection` |
| *Tools - Create and Convert* | `convert-pdf-to-images-running`, `convert-pdf-to-images-conflict`, `convert-audiobook-running` | `page-selection` (2×), řada filtrů |
| *Tools - Optimize* | `optimize-compress-running`, `-done`, `-done-write-error` | řada chipů `tool-actions` |
| *Tools Overview* | `pdf4qt-tools-search-filtered` | chipy Protect & Sign a Review |

V Qt stačí na kontejneru zavolat `setEnabled(false)`. Další skupiny ovládacích prvků s krytím jsou převedené v kapitole 11.4.

### 11.3 Ověření

- Otisk stránek *Main page*, *Tools Overview*, *Tools - Edit*, *Tools - Create and Convert*, *Tools - Optimize*, *Tools - OCR* a *Components* před změnami a po nich. Změnily se jen:
  - na *Main page* rámce `pdf4qt-home` a `pdf4qt-tools` (skupiny Quick action tile, Tool icon a ikony);
  - na *Tools - OCR* tři upravené rámce;
  - na stránce *Components* sada Filter chip;
  - na ostatních stránkách skupiny Filter chip, Checkbox, Page selection, ikony a prosté vrstvy.

  Tool category header se v otisku nezměnil, protože si dvě hlavičky stejné velikosti barvy vyměnily.
- Po přebarvení nezůstal v žádné upravené instanci token jiného odstínu (ani přepsaná barva ikony).
- Snímky: katalog na *Main page*, Home, sada Filter chip, `convert-ocr-running`.

### 11.4 Ztlumené skupiny ovládacích prvků (N-22)

Ostatní skupiny ovládacích prvků s krytím 0,45–0,6 na obrazovkách toolů jsou převedené stejně jako chipy v 11.2: krytí je zrušené, komponenty se stavem Disabled jsou v něm a ostatní komponenty, popisky a ikony mají podle O-08 text `text/disabled` a ikonu `icon/disabled` (výplně beze změny). Barvy jsou zapsané i se správnou hodnotou proměnné, ne jen s vazbou.

| Stránka | Rámce | Skupiny | Přepnuto do Disabled | Podle O-08 |
| --- | --- | --- | --- | --- |
| *Tools - OCR* | `convert-ocr-preparing`, `-running`, `-applying`, `convert-prepare-scans-detecting` | 29: hlavička, výběr a seznam stránek, sekce Recognition, Saved profiles, This page, Straighten, Split, Crop, řádek Recognize Again | 84 (Button, Combo box, Checkbox, Spin box) | Tag, Text link, 160 textů |
| *Tools - Edit* | `edit-page-numbers-running`, deset rámců `edit-content…` | 15: sekce Numbering, Appearance, Preview, Summary, výběr barvy výplně | 80 (Color swatch, Spin box, Combo box) | Key value row, 30 textů, 11 ikon |
| *Tools - Protect and Sign* | `protect-sign-running`, `-timestamp-running`, `-redact-running`, `-sanitize-running` | 4 levé panely | 5 (Password field, Text field, Combo box, Icon button) | Tag, Signature card, Key value row, 53 textů, 20 ikon |
| *Tools - Organize Pages* | `pages-resize-custom`, `pages-crop-remove` | 3 | 3 (Checkbox, Spin box) | 3 texty |
| *Tools - Create and Convert* | `convert-scan-…` (4), `convert-audiobook-running` | 6: pole Pages v toolbaru, odkaz Reload devices, seznam štítků | 4 (Toolbar field) | Text link, Tag, 37 textů |
| *Tools - Optimize* | `optimize-compress-running`, `-images-done`, `-structure-done`, `-grayscale…` | 7: sekce What changes, nabídková tlačítka `tool-actions`, přeskočený obrázek | 13 (Toolbar menu button) | Key value row, Tag, 25 textů |

Zvláštní případy:

- **Hlavička tabulky certifikátů** (`protect-certificates-trusted`, `-remove`) byl řádek Certificate row s krytím 0,6. Není to zakázaný prvek, ale hlavička pohledu: krytí je zrušené a popisky mají `text/muted`, tak kreslí hlavičku `QHeaderView` i hotový styl.
- **Přeskočený obrázek** „Image 3 · already gray“ (`optimize-grayscale`, `-done`, `-menus`) má texty a štítek zakázané, krytí 0,6 zůstalo jen náhledu obrázku (obsah).
- **Sekce Preview** (`edit-page-numbers-running`): ukázka čísla stránky je obsah (`sample/*`) a nemění se, zakázané jsou popisky.
- Nezměněné zůstaly ztlumené náhledy stránek a obrázků (Sample page, Thumb preview, Mode preview, listy papíru při vkládání a mazání stránek) a skrytá ikona `icon/check` s krytím 0 v `optimize-structure-steps`.

### 11.5 Karta Video Tutorials

Karta „Video Tutorials“ na Home (`51:414`) měla po přepnutí na `Hue=Violet` (10.2) přepsanou výplň navázanou na `accent/violet/bg`, ale s uloženou bílou hodnotou, a Figma kreslila bílou. Výplň je zkopírovaná z varianty (`#faf5ff`). Kontrola všech 12 stránek (každá barva navázaná na proměnnou porovnaná s hodnotou proměnné, asi 183 000 barev) jinou takovou barvu nenašla. Otisk stránek tuto chybu nevidí, protože zapisuje název proměnné, ne uloženou hodnotu.

### 11.6 Ověření N-22

- Ve všech převedených skupinách zůstaly jen barvy `text/disabled`, `icon/disabled`, `text/muted` (hlavičky), `sample/*` (obsah) a `icon/on-accent` (zatržítko zaškrtnutého zakázaného pole, jak ho kreslí komponenta). Jeden popisek stavu podpisu (`accent/green/icon`) byl dodatečně opraven.
- Otisk stránek *Tools - OCR*, *Edit*, *Protect and Sign*, *Organize Pages*, *Create and Convert* a *Optimize*: na OCR se změnily jen čtyři upravené rámce, na ostatních stránkách jen prosté vrstvy a skupiny převedených komponent. Ve skupině Text link a Certificate row ubyly kreslené uzly – instance s krytím se počítala jako kreslená.
- Snímky: `convert-ocr-running`, `protect-sign-running`, `edit-page-numbers-running`, `protect-certificates-trusted`, `optimize-grayscale`, Home.

## 12. Zpětná vazba k obrazovkám (15. 10. 2026)

Opravené body zpětné vazby, které nepotřebovaly rozhodnutí. Ostatní body jsou v review jako Q-9 až Q-14.

- **Levý panel OCR** (*Tools - OCR*, 61 rámců): popisek „check marks“ visel za tlačítky All / None / Invert. Řádek `check-row` je teď svislý: nadpis „Check marks“ (Medium 11, `text/tertiary`, v zakázaných rámcích `text/disabled`) nad řadou tlačítek, stejně jako nadpis „Only“ výběru stránek. Výběr stránek měl pod „Only“ osiřelou ikonu stavu s prázdným textem; řádek stavu je v 61 instancích skrytý (počet zaškrtnutých stránek je v hlavičce panelu).
- **Přepínač motivu** (*Main page* 20×, *Tools Overview* 7×): třetí segment Auto (`icon/monitor`), vybraný, protože nastavení *Color scheme* je *Automatic*. Theme toggle segment má vodorovné okraje 6 místo 14 (tři segmenty po 61 px), popis komponenty je upravený. Přepnutím variant se ztratila barva slunce segmentu Light (`icon/sun`); je vrácená ve všech 27 přepínačích.
- **Úvodní stránka Compress** (*Tools Overview*, `pdf4qt-tool-start`): krok 4 popisuje volby dialogu výsledku („keep it in this tab, save it as a copy or overwrite the original file“) místo „written as a copy next to the original“. V části At a glance je výsledek „A smaller PDF“ a otevřený dokument „Your choice“ (dříve „A smaller copy of the PDF“ a „Stays unchanged“). Odkaz „Related tools“ je odstraněný ze všech tří úvodních stránek, protože tooly na sebe neodkazují ([tools.md](tools.md), 5.1). Úvodní stránky Merge popisují svůj dialog správně a zůstaly.
- **Dialog výsledku na *Tools - Optimize*** (`optimize-compress-done`, `-done-write-error`, `optimize-bw-done`): pole obsahovalo celou cestu a chybělo Browse…. Jako v ostatních dialozích výsledku je v poli jen název souboru a pod ním řádek se složkou a tlačítkem Browse… (kopie řádku z *Protect and Sign*). Dialog je o 44 px vyšší a posunutý o 22 px nahoru.
- **Náhled začernění v Redact** (14 rámců `protect-redact…`): oblast „Table column“ přesahovala 16 px pod tabulku do odstavce. Je vysoká 79 místo 95 px a končí na spodní hraně tabulky; popisek v seznamu je „52 × 80 pt“ (12 textů).
- **Compare** (26 rámců): legenda v levém panelu opakovala blok Colors pravého panelu (s jinými odstíny). Je odstraněná, barvy kategorií ukazuje jen blok Colors.
- **Ukázková data na Home:** data posledních souborů a toolů „Sep 18, 2025“ a „Sep 15, 2025“ jsou z roku 2026 jako na ostatních obrazovkách. Název souboru Invoice_2025.pdf zůstal (faktura za rok 2025). Roky 2025 jinde jsou záměrné (certifikát, který vypršel, text porovnávaných dokumentů, název zakázky).
- **Záložky pravého panelu OCR** (odpověď na Q-11): čtyři záložky Recognition / Image / Output / Advanced se v panelu 287 px lámaly na dva řádky. Záložka Advanced je zrušená (41 rámců), záložky se vejdou na jeden řádek. Její obsah se nemění a nic neubylo: sekce Engine (engine mode, review threshold, parallel workers, memory for images, page time limit), Restrictions (allowed / forbidden characters, review words not found in the dictionary, all 14 page layouts) a User words and patterns jsou v rozbalovací sekci *Advanced* na konci záložky Recognition, protože se všechny týkají rozpoznávání. Sbalená sekce je nadpis se šipkou `chevron-right` a nápovědou „Engine, restrictions, user words and patterns“ (29 rámců, v běžících stavech zakázaná); rámec `convert-ocr-section-advanced` ukazuje záložku Recognition posunutou na konec s rozbalenou sekcí (`chevron-down`). V Qt je to `QToolButton` se šipkou, který přepíná viditelnost kontejneru se sekcemi; samotné ikony místo názvů záložek se nepoužily (abstraktní kategorie bez srozumitelné ikony).
- **Neopraveno:** stránka B·9 v Manage Pages je nakreslená správně – stránka na výšku otočená o 90° má svislé řádky textu a nadpis u pravého okraje. „3 rendering issues“ ve stavovém řádku i ve Vlastnostech dokumentu je stejná informace na dvou místech, ne chyba.

Ověřeno snímky: `convert-ocr`, dialog `optimize-compress-done`, Home, `protect-redact-preview`, `review-compare`, `pdf4qt-tool-start`.

## 13. Rozhodnutí ke zpětné vazbě (15. 10. 2026)

Podle odpovědí na Q-10, Q-12, Q-13 a N-13 (rozhodnutí 17–20 v [components.md](components.md), kapitola 8).

- **Barvy výsledků hledání (Q-12).** Výsledek hledání a anotace Highlight v ukázkovém dokumentu měly stejnou barvu `sample/orange-soft`. Kolekce *Color* má tři nové proměnné (140 místo 137), v obou režimech stejné, s rozsahem výplň a obrys: `search/match` `#a5f3fc`, `search/match-current` `#22d3ee` a `search/match-border` `#0891b2`. Výsledky (`match`, 6×) a aktuální výsledek (`match-current`, 3×) na *View Document* je používají s násobením, aktuální výsledek má navíc obrys 2 px vně. Vrstva `highlight-annotation` komponenty Sample page má `annotation/yellow` s násobením, jako nově kreslené a označené zvýraznění; změna se propsala do 124 instancí na *View Document*, *Tools - Edit* a *Tools - Review and Inspect*. Rámce `pdf4qt-document-search` a `-search-mark` mají doplněný výsledek „Revenue“ uvnitř zvýrazněného řádku (v seznamu výsledků byl, na stránce chyběl); přes žluté zvýraznění vychází zeleně.
- **Often used next.** Úvodní stránky toolů (`pdf4qt-tool-start`, `-multi`, `-multi-one`) mají na konci karty *At a glance* položku `often-next`: popisek „Often used next“ a pod ním obyčejný zalomený text se třemi tooly (Compress: Protect with Password, Sign with Certificate, Merge; Merge: Add Page Numbers, Compress, Protect with Password). Písma a barvy jsou z řádku Key value row. Do řádku Key value row se tři názvy nevejdou (hodnota má 154 px a jeden řádek). V Qt je to `QLabel` se zalamováním. Pravidla zobrazení jsou v [tools.md](tools.md), 9.2.
- **Rozbalená sekce Advanced v OCR** (`convert-ocr-section-advanced`): popisek zaškrtávacího pole „Review words not found in the dictionary“ přetékal o 4 px přes okraj karty. Je zkrácený na „Review words not in the dictionary“.
- **Q-10** zůstává, jak je. **Q-13** je pravidlo chování palety Compare, Figmu nemění. **N-13** je jen ukázka v návrhu a z review je odstraněný.

Ověřeno snímky: `pdf4qt-document-search` (výsledek přes zvýraznění), boční sloupec `pdf4qt-tool-start`; kontrola barev vrstev `highlight-annotation`, `match` a `match-current` na stránkách *View Document*, *Tools - Edit* a *Tools - Review and Inspect*.

## 14. Body I v knihovně Pdf4QtLibFluent (15. 10. 2026)

Rozdíly hotové knihovny proti Figmě (review, část I) jsou opravené v kódu, chybějící widgety přibyly. Figma se tímto krokem nemění.

| Bod | Co se změnilo | Kde |
| --- | --- | --- |
| I-15, I-20, Q-12 | `PDFThemeColors` má 140 hodnot (nové `bg/accent-hover`, `bg/accent-pressed`, `text/disabled`, `icon/disabled`, `search/match`, `search/match-current`, `search/match-border`), kontrolní součet tabulky je spočítaný ve Figmě. Hover a stisk Primary, vybraného chipu a zaškrtnutého Checkboxu a Radia jsou z nových tokenů. Zakázaný stav všech prvků stylu, ikonový engine (`QIcon::Disabled`), palety textových widgetů a `PDFLabel` kreslí `text/disabled` a `icon/disabled`. | `pdfthemecolors`, `pdfwidgetstyle*.cpp`, `pdfthemeiconengine.cpp`, `pdffluentwidgets.cpp` |
| I-17 | Select: Hover `bg/hover`, otevřený `bg/pressed` s obrysem `border/strong`, šipka `icon/default`, předpona zakázaného `text/disabled`. | `drawSelectFrame` v `pdfwidgetstyleinputs.cpp` |
| I-18, I-21 | Položka nabídky vybraná klávesnicí má kroužek 2 px uvnitř bez výplně, položka pod myší `bg/hover`; zkratka a šipka podnabídky zakázané položky `text/disabled` / `icon/disabled`. | `pdfwidgetstylemenus.cpp` |
| I-21, I-22 | Svislý tab bar: vybraná položka na myš nereaguje, kroužek 2 px uvnitř. | `pdfwidgetstyletabs.cpp` |
| I-16 | Vlastní barva ikony segmentu (`PDFSegmentedControl::setSegmentIconColor`, vlastnost `iconColor`), slunce `icon/sun` ve všech stavech. Okraj segmentu 6 místo 14, tři segmenty Light / Dark / Auto se vejdou celé. | `pdffluentwidgets`, `pdfwidgetstyletabs.cpp` |
| I-04 | Počet u chipu, který je tlačítkem (vlastnost `chipCount`); vybraný chip má stejnou šířku jako nevybraný. | `pdfwidgetstyletabs.cpp` |
| I-12 | Role písma Semi Bold 10, Bold 13, Bold 14 a Bold 22. | `pdfthemefonts` |
| I-09 | Ikony `compress-flate`, `compress-rle`, `compress-fax` s `viewBox` 24×24 podle Figmy. | `resources/icons` |
| I-13, I-14 | Dvoubarevná ikona `PDFThemeIcons::paintFilled` (výplň a obrys), použitá pro jantarový zámek tabu. | `pdfthemeiconengine` |
| I-02 | `PDFIconButton` (Icon button 24 / 28, Tab close button 20). | `pdfcomponentbuttons` |
| I-19 | `PDFStampChip` (Stamp chip, odstíny přes `PDFAccentHue`). | `pdfcomponentbuttons`, `pdfaccenthue` |
| I-03 | `PDFNavItem` (Nav item, Settings nav item) a `PDFNavList` se skupinami oddělenými čarou `border/separator`; šipky, Home a End přesouvají výběr přes skupiny. | `pdfcomponentrows` |
| I-11 | `PDFToolBarField` (hodnota Semi Bold 13 a tlumená přípona, výška 32, nejméně 64). | `pdfcomponentpanels` |
| I-01, I-14 | `PDFDocumentTabBar`: zámek, tečka neuložených změn, pruh průběhu 2 px, tlačítko zavření na tab; název tabu pro čtečku obsahuje „locked“ a „unsaved changes“. | `pdfcomponentpanels` |
| I-07 | `PDFKeyValueList` (Key value row, jeden widget pro všechny řádky karty, O-01); předvolby řádků pohledů `PDFTreeView::setRowStyle` Certificate (34 px) a Library (30 px), text 12 px. | `pdfcomponentpanels`, `pdfwidgetstyleitemviews.cpp` |
| I-06 | Předvolba Outline: poloměr 8, okraje 6 / 8, šipka 14 v místě 16, mezera 4, odsazení úrovně 18, text `text/secondary` (pod myší `text/primary`), vybraný Medium `text/accent` se šipkou `icon/accent`. | `pdfwidgetstyleitemviews.cpp`, `pdfwidgetstylemetrics.cpp` |
| I-05 | Password field: výška 40, zámek 16 (chyba `icon/danger`, zakázané `icon/disabled`), text na x = 37, přepínač je `PDFIconButton` 28 se 7 px od pravého okraje (prázdné pole `icon/muted`, vyplněné `icon/default`), prostrkání 2 px jen u skrytého vyplněného hesla. Pole umí výšku a odsazení textu vlastnostmi `fieldHeight` a `fieldTextInsets`. | `pdffluentwidgets`, `pdfwidgetstyleinputs.cpp` |
| I-10 | Progress bar ověřen, beze změny (drážka 4 px `border/default`, výplň úměrná hodnotě, 100 % zeleně). | – |

Galerie má novou stránku *Components* (`18-components`) se vzorky ve stavech Default, Hover, Pressed, Selected a Disabled. `UnitTestsWidgetStyle` má 192 testů (nové `components`, `disabledAndSelect`, `rowStyles`), detektor úniku prochází i novou stránku.

Ověření: nezávislý agent porovnal kód a export galerie (oba režimy, 1× a 2×) s Figmou. Našel dvě chyby (zkrácené popisky třísegmentového přepínače, užší vybraný chip) a tři drobnosti (šipka podnabídky zakázané položky, písmo 12 px řádků Certificate a Library, šířka razítka o 1 px); všechny jsou opravené a druhý průchod je potvrdil. Co zůstává, je v review (I-13, I-21, I-23, I-24, Q-15 až Q-17).

## 15. Výplně stavů, oko hesla, Checkbox a Radio (16. 10. 2026)

Opravy Figmy podle rozhodnutí 21 až 23 ([components.md](components.md), 8.3) a odpovídající změny knihovny.

| Bod | Figma | Knihovna |
| --- | --- | --- |
| N-23 (Q-15) | Select (Hover, Pressed), Filter chip `Selected=False` (Hover, Pressed), Button `Variant=Secondary` (Hover, Pressed) a Pinned card (Hover, Pressed): výplň `bg/surface` + `bg/hover` / `bg/pressed` (8 variant, dříve jen poloprůhledná vrstva). | Styl to tak kreslil už dřív, beze změny. |
| N-23, rozšíření | Totéž pro vybrané karty, které měly jen poloprůhledné `bg/accent-subtle`, ačkoli nevybrané mají neprůhlednou plochu: Tool choice `Selected=True` (4 varianty), Action tile `Active=True` (4 varianty) a na obrazovkách vybrané volby dialogů výsledku (`option-keep`, `option-copy`, `option-tab`, `option-file`, `option-open`, 37 rámců na šesti stránkách toolů; 37. rámec v Audio Book doplnila nezávislá kontrola), vybraný asistent na *Main page* (3) a vybrané buňky kotvy v Organize Pages (1) a v Create and Convert (1, měla plnou výplň `bg/accent`). Na bílém pozadí se vzhled nemění. | Komponenty zatím nejsou widgety. |
| N-24 (Q-17) | Password field: oko v obsazích `Masked` a `Visible` (Default, Hover, Focus, Error, Error focus, 10 variant) má barvu Icon button `icon/muted` místo přepsané `icon/default`. | I-25: `PDFLineEdit::updatePasswordButton` barvu oka nepřepisuje. |
| N-25 (Q-18) | Checkbox a Radio: nezaškrtnuté Hover `bg/surface` + `bg/hover`, Pressed `bg/surface` + `bg/pressed`, tah `icon/muted` 1,5 px; zaškrtnuté Hover `bg/accent-hover` bez tahu (12 variant). Na stránce *Widgets*, sekce 03: ukázky stavů radia, částečně zaškrtnutého pole a zaškrtávátka pohledu, texty bloků a poznámka *Implementation*. | I-26: `PDFWidgetStyleButtons::drawIndicator` (i zaškrtávátko pohledů a částečně zaškrtnuté pole). |

Ověření Figmy:
- **Přepsané hodnoty:** 79 instancí upravených variant na všech stránkách nemá přepsanou výplň ani tah. Žádná instance sad nemá poloprůhlednou výplň bez `bg/surface`.
- **Snímky:** stavy Checkboxu a Radia na *Widgets*.
- **Poloprůhledné výplně:** prošel jsem všechny stránky. Mimo opravená místa zůstávají jen záměrné:
  - výběr a najetí u řádků, položek nabídek, záložek, segmentů a tlačítek lišty, které vlastní plochu nemají,
  - `bg/overlay`, `bg/scrim`,
  - značky na dokumentu (anotace, rozdíly, slova OCR, oblasti, kótování),
  - dlaždice ikon.
- **Karty:** karty v [components_spec.md](components_spec.md) jsou upravené.

**Editor buněk pohledů.** Byl to výchozí delegát Qt, který používá každý pohled bez vlastního delegáta (`QTableView`, `QTreeView`, `QTableWidget`, ...):
- **Chyba:** delegát vytvářel obyčejný `QLineEdit` s fialovým textem a výběrem z palety úniku. Editor ležel jen v obdélníku textu, takže vedle kroužku buňky byl vidět i jeho vlastní kroužek.
- **Továrna editorů:** styl při přípravě aplikace nastaví `PDFItemEditorFactory` jako výchozí továrnu editorů. Továrnu, kterou nastavila aplikace, nemění, a při uvolnění aplikace vrátí továrnu Qt.
- **Jeden kroužek:** prázdný editor buňky (bez rámečku) nekreslí nic, plochu `bg/surface` a kroužek editované buňky kreslí pohled pod editorem přes celou buňku. Kroužek je tak jen jeden, ať delegát dá editoru jakoukoli část buňky. Delegát pohledu styl nikdy nenahrazuje: první verze opravy nahrazovala výchozí delegát delegátem `PDFItemDelegate` a nezávislá kontrola kódu ukázala, že se tím ztrácejí úpravy v persistentních editorech, úpravy při změně motivu systému a spojení aplikace na původní delegát.
- **Test:** `itemEditing` v `UnitTestsWidgetStyle`, offscreen i nativně.

`UnitTestsWidgetStyle`: 193 testů, všechny prochází. Upravené jsou sondy indikátorů (oba režimy, poměry 1, 1,5 a 2), barva oka hesla a `itemEditorFactory`.

## 16. Plná modrá, nabídka Annotate a dráha přepínače (16. 10. 2026)

Provedení rozhodnutí 24 až 26 ([components.md](components.md), 8.3): Q-9 a Q-14 podle návrhu z review, Q-16 podle *Main page*.

### 16.1 Dráha segmentovaného přepínače (Q-16)

- **Figma:** dráha přepínače je `bg/track`, `rad=16`, bez vnitřního okraje, výška 32 jako na *Main page* (182×32). Ukázka na stránce *Widgets* (`620:1544`) měla okraj 2 a rozměr 186×36, teď je 182×32; text sekce 11 to říkal už dřív.
- **Knihovna:** `PDFSegmentedControl` nemá okraj dráhy.

### 16.2 Plná modrá jen pro primární tlačítko (Q-9)

**Komponenty:**
- **Filter chip `Selected=True`:**
  - výplň `bg/surface` + `bg/accent-subtle` (hover `-hover`, stisk `-pressed`),
  - text a počet `text/accent`, ikona `icon/accent`,
  - zakázaný beze změny `bg/muted`,
  - nový popis komponenty.
- **Segment `47:157`** (dříve Theme toggle segment):
  - nový název a popis,
  - vlastnost `Show icon`,
  - varianty `State=Disabled` pro obě hodnoty `Selected` podle O-08.

**Obrazovky:**
- **Výběr jedné z několika možností je segmentovaný přepínač** (Segment bez ikony na dráze `bg/track`). Všechny segmenty jednoho přepínače jsou stejně široké: nejširší obsah + 2 × 14. Převedeno:
  - `Page selection` v komponentě (8 variant, 151 instancí); dráha `modes` vyplňuje šířku komponenty a čtyři segmenty ji dělí rovným dílem (80 v komponentě, 70 v postranních panelech širokých 279). Obnoveny přepsané hodnoty instancí: `State=Disabled` u 6 instancí a prototypové odkazy u 11 instancí na *Tools - Edit*. Instance si nesly přepsanou mezeru mezi chipy (4 nebo 2 px), nezávislá kontrola zjistila, že tím dráha přetékala a ořezávala „Range“; mezera je teď 0,
  - zarovnání v Sign by Hand (12×),
  - Marks / Preview result (15×),
  - Fit / 100 % (75×),
  - Thumbnails / Details (22×),
  - předvolby Compress (6×, 3× zakázané),
  - profil OCR Fast / Standard / Quality (1×).

  Stroke / Fill v nástroji tvarů už segmenty byly, dostaly stejnou šířku 83.
- **Volby zapnuto / vypnuto** (Whole words, Regular expression, Ignore soft hyphens, Sync zoom): zapnutá volba má místo své ikony `icon/check` v `icon/accent` (14 chipů). Pro vypnutý stav má Sync zoom ikonu `icon/sync-view` (na obrazovkách je všude zapnutý, vidět je tedy zatržítko).
- **Zůstávají Filter chip s novou podobou:**
  - filtry seznamů,
  - záložky panelů OCR,
  - záložky Object Inspector,
  - Color / Grayscale / Bitonal,
  - Claude Code / Claude Desktop.

**Stránka *Widgets*:** texty sekce 11 (bloky „Existing“ a „horizontal tab bar“, poznámka *Implementation*) popisují nový vybraný chip, volby se zatržítkem a segmentový přepínač pro volby.

**Knihovna:**
- **Vybraný chip a chipový tab bar:** kreslí `bg/surface` + `bg/accent-subtle*`, text `text/accent`, ikonu `icon/accent`. Kroužek fokusu se kreslí vně tvaru bez spojení s výplní.
- **Volba zapnuto / vypnuto:** chip s dynamickou vlastností `option` (`PDFWidgetStyleProperties::OPTION`) má vždy místo pro ikonu a zapnutý kreslí `check` místo své ikony. Vlastnost je výslovná, odhad podle exkluzivní skupiny by zachytil i filtry s více výběry a nefungoval by s uloženou velikostí tlačítka.
- **`PDFSegmentedControl`:**
  - segmenty stejně široké (velikostní politika `Ignored`),
  - `sizeHint` je počet × nejširší segment,
  - přirozený okraj segmentu 14, v užším přepínači nejméně 6,
  - destička vybraného segmentu má poloměr 16 jako dráha (s poloměrem 14 by na dráze bez okraje vyčnívala z jejích rohů; opraveno i ve Figmě, všech 10 variant Segment), její stín kreslí přepínač, segment by ho ořízl,
  - skryté segmenty se do `sizeHint` nepočítají.
- **Galerie:** vzorky předvoleb a voleb se zatržítkem.
- **Test:** `chipsAndSegments`.

### 16.3 Nabídka Annotate (Q-14)

**Ikona.** Nová `icon/highlight-comment`: zvýrazňovač a bublina komentáře, jeden vektor `glyph`, tah 2 `icon/default`. Je i v repozitáři, `resources/icons/highlight-comment.svg`.

**Lišta dokumentu.** Tlačítko *Annotate* (`icon/highlighter`) je mezi *View* a *Insert* ve všech 24 dosavadních lištách na *View Document* (a ve třech nových rámcích níže):
- v zamčeném dokumentu ve stavu Disabled,
- prototypový odkaz vede na panel *Annotate*, v panelu *Annotate* vede dlaždice Highlight na režim zvýraznění; zakázaný *Insert* zamčeného dokumentu už nikam nevede,
- panely *Insert*, *Go to* a *Tools* jsou posunuté pod svá tlačítka.

**`pdf4qt-document-panel-annotate`** (nový rámec):
- dlaždice Highlight (aktivní: naposledy použitý typ), Underline, Strikeout, Squiggly a Highlight and comment,
- řada barev z palety anotací (vybraná `annotation/yellow`),
- položka „Show all comments (3)“.

**`pdf4qt-document-panel-insert`:** bez čtyř zvýraznění. Sekce Text a Sticky note jsou spojené do „Text and notes“ (Text box a sedm typů poznámky v jedné řadě) a panel je široký 778.

**`pdf4qt-document-tool-highlight`** (nový rámec):
- aktivní režim zvýraznění,
- plovoucí lišta s typem, barvami a zavřením,
- rozpracované žluté zvýraznění na stránce,
- ve stavovém řádku „Highlight: drag over text. Press Esc to finish.“

**`pdf4qt-document-selection-bar`** (nový rámec):
- vybraný text (`bg/accent-subtle`),
- nad ním lišta: Highlight, Underline, Strikeout, Squiggly, Highlight and comment, oddělovač, Copy (Icon button 28). Lišta má výplň, obrys a stín plovoucí lišty nástroje, je ale kompaktní (poloměr 10, okraj 4).

**Nastavení (*Main page*):**
- *Editing*: nová skupina „Annotations“ s volbou „Show markup bar when text is selected“ (zapnuto).
- *Shortcuts*: nové akce Highlight Ctrl+Shift+H, Underline Ctrl+Shift+L, Strikeout Ctrl+Shift+K, Squiggly Ctrl+Shift+Q, Highlight and Comment Ctrl+Shift+M. Tyto zkratky nejsou v aplikaci ani v návrhu obsazené (obsazené jsou Ctrl+Shift+E, F, R, S, I, A, G, O, + a −). Ctrl+Shift+U se nepoužívá, na Linuxu ji zachytává vstupní metoda IBus.

Popis chování je v [tools.md](tools.md) (Insert / Text Highlight a nabídka Annotate).

### 16.4 Nálezy ponechané beze změny

Dříve review, část F.2. Nejsou to chyby, nic se na nich nemění.

| Bod | Komponenta | Proč |
| --- | --- | --- |
| F-05 | Toolbar menu button | `Open=True` + Disabled neexistuje: otevřené tlačítko nemůže být zakázané. |
| F-07 | Pole | Stav Pressed pole nemají (stisk dá fokus). Šipky spin boxu mají stavy na stránce *Widgets*. |
| F-10 | Komponenty bez Disabled | Disabled mají ovládací prvky formulářů, toolbaru a nabídek. Ostatní se kreslí podle pravidla (O-08). |
| F-13 | Property row | Řádek sám nemá Pressed, Focus a Disabled: kliká se na tlačítka akcí, která stavy mají. |
| F-18 | Password field | Error a Focus zůstávají na jedné ose, stejně jako u Text fieldu; zpráva o chybě patří formuláři. |
| F-22 | Tab | Kroužek fokusu po celém obvodu je správně, i když výchozí obrys dolní hranu nemá. |
| F-34 | Tab | Hodnota průběhu je údaj za běhu. |
| F-37 | Color button, Color swatch | Figma neumí barvu jako vlastnost; barva je přepis výplně. |
| F-47 | Recent item row | Je to jiný seznam než Recent file row; obrys, hvězda a chybějící soubor do něj nepatří. |
| F-48 | Sady odstínů | Každá komponenta nabízí odstíny, které používá. |
| F-51 | Radio × Checkbox, Select × Combo box | Stránka *Widgets* rozdíly potvrzuje (Radio 1 px v klidu; Select je kompaktní varianta 32 px). |
| F-53 | Progress bar | Dráhu `border/default` kreslí tak i stránka *Widgets*; zelená při 100 je záměr. |
| F-61 | Tokeny mimo roli | Textové a ikonové tokeny jako výplně teček a obrysů zůstávají; hodnoty sedí v obou režimech. |
| F-64 | Layout preview, Mode preview/Custom colors | Stránky přesahující výřez ukazují posunutý souvislý pohled; `reading/amber/*` je náhled vlastních barev. |
| F-66 | Key value row, Library row | Poloměr 6 na kořeni bez výplně se nekreslí. |
| F-69 | Checkbox | Kroužek uvnitř u nezaškrtnutého a vně u zaškrtnutého je záměr: uvnitř by na akcentové výplni nebyl vidět. Radio to má stejně. |
| F-73, F-74 | Reading theme card, Strength meter | Barvy motivu a splnění kritérií jsou údaje za běhu. |
| F-81 | Search field | Ikona se při fokusu barví na `icon/accent`; stejně ji ukazuje stránka *Widgets*. |

## 17. Tag, Star button, Color swatch, Collection row a Version row (17. 10. 2026)

Provedení rozhodnutí 27 až 30 ([components.md](components.md), 8.5) k bodům review I-25 až I-28. Figma se měnila jen na stránce *Components* v těchto pěti sadách; barvy jsou navázané na proměnné kolekce *Color* (přenesené z existujících výplní, surová hodnota odpovídá vyřešené hodnotě `resolveForConsumer` světlého režimu).

| Bod | Figma | Knihovna |
| --- | --- | --- |
| I-25 (27) | Tag `116:2353`: Hover `116:2347` výplň `bg/subtle` + `bg/hover` (dříve `bg/muted`), Pressed `116:2349` `bg/subtle` + `bg/pressed` (dříve `bg/track`); obrys `border/default w1` beze změny (2 varianty). | `PDFComponentPainter::paintTag`: `bg/subtle` a nad ním `bg/hover` / `bg/pressed`. |
| I-26 (28) | Star button `64:569`: Hover `64:548`, `64:560` a Pressed `64:551`, `64:563` výplň `bg/surface` + `bg/hover` / `bg/pressed` (4 varianty). Color swatch `206:805`: Hover `206:767`, `206:775`, `206:784`, `206:796` a Pressed `206:769`, `206:777`, `206:787`, `206:799` totéž (8 variant, `Kind=Color` i `None`, vybrané i nevybrané). Dříve jen průsvitná vrstva. | `paintStar` a `paintColorSwatch`: pod překryvem kruh / čtverec `bg/surface`. |
| I-27 (29) | Collection row `89:1367`: tah vektoru `glyph` šipky v Hover (`I89:1347;46:95`) a Pressed (`I89:1356;46:95`) `icon/default` místo `icon/muted`, stejný přepis jako Link row `49:190` (2 varianty). | `paintCollectionRow` bere barvu šipky z `pointerIcon` jako Link row a Recent tool row. |
| I-28 (30) | Version row `103:1950`: výška řádku textu `summary` (`103:1940`, `103:1949`) pevných 14 px místo 130 % (2 varianty). Výška variant zůstala 60 px. | Beze změny (`VERSION_SUMMARY_LINE` = 14). |

Ověření Figmy:

- **Instance:** před úpravou i po ní jsem prošel instance upravených variant na všech dvanácti stránkách (jedna stránka na volání, i instance vnořené v instancích). Upravené varianty Tagu, Star buttonu a Color swatche nemá žádná instance; Collection row `State=Pressed` má jednu (`283:2369` v `collections-list` na *Tools Overview*), Version row čtyři (`104:2221`, `104:2230`, `104:2247`, `104:2264` na *Main page*). Žádná nemá přepsanou výplň, tah šipky ani výšku řádku, nic se proto neopravovalo; šipka instance `283:2369` je teď `icon/default` a řádky verzí mají stejné výšky (60 px se dvěma řádky shrnutí, 46 px s jedním).
- **Barvy:** všech 16 upravených výplní a tahů má surovou hodnotu rovnou vyřešené (`#f8fafc`, `#ffffff`, `#0f172a` s krytím 0,06 / 0,11, `#475569`).
- **Snímky:** `get_screenshot` sad `116:2353`, `64:569`, `206:805`, `89:1367` a `103:1950`, porovnané s galerií.

Kontrast Tagu (WCAG, proti klidovému stavu `bg/subtle`):

| Stav | Světlý | Tmavý |
| --- | --- | --- |
| Hover dříve (`bg/muted`) | 1,02 : 1 | 1,03 : 1 |
| Pressed dříve (`bg/track`) | 1,19 : 1 | 1,19 : 1 |
| Hover nově (`bg/subtle` + `bg/hover`) | 1,13 : 1 | 1,24 : 1 |
| Pressed nově (`bg/subtle` + `bg/pressed`) | 1,25 : 1 | 1,46 : 1 |
| Pro srovnání Filter chip a Button Secondary, Hover (`bg/surface` + `bg/hover` proti `bg/surface`) | 1,13 : 1 | 1,23 : 1 |

Hover Tagu je tedy stejně výrazný jako u srovnatelných prvků, obrys se nezesiluje (`border/strong` by proti `border/default` přidal jen 1,20 / 1,52 : 1 a Filter chip ho v Hover také nemá).

Knihovna (`Pdf4QtLibFluent/sources/pdfcomponentpainter.cpp`, popisy v `pdfcomponentpainter.h`): widgety `PDFTag`, `PDFStarButton`, `PDFColorSwatch` a `PDFCollectionRow` kreslí přes `PDFComponentPainter`, samy se neměnily. Test `UnitTestsFluentComponents`: sondy Tagu (Hover a Pressed nad `bg/subtle`, obrys zůstává), hvězdy a vzorku na cizím pozadí (`bg/track` a hover řádku: v klidu prosvítá pozadí, Hover a Pressed jsou vždy `bg/surface` + překryv, i vybraný vzorek a `Kind=None`, zakázaný bez hoveru) a barva šipky Collection row (klid a Focus `icon/muted`, Hover a Pressed `icon/default`), vše ve světlém i tmavém režimu při 1, 1,5 a 2. Build bez varování, `UnitTestsFluentComponents` 60/60, `UnitTestsWidgetStyle` 194/194, galerie vyexportovaná ve světlém a tmavém režimu při 1× a 2×.

## 18. Implementace komponent v knihovně (16.–17. 10. 2026)

Komponenty stránky *Components* jsou naprogramované v knihovně `Pdf4QtLibFluent` v pěti krocích plánu ([components.md](components.md), 6.3). Každý krok prošel dvěma nezávislými kontrolami (věrnost Figmě a review kódu, 6.5) a jejich nálezy jsou opravené; Figma se při implementaci jen četla. Kreslení a rozměry každé komponenty jsou na jednom místě, v `PDFComponentPainter` (`pdfcomponentpainter*.cpp`), které volá widget, delegát, galerie i test.

| Krok | Naprogramováno | Kontroly | Nejdůležitější opravy |
| --- | --- | --- | --- |
| 1 | Základ `PDFComponentPainter`, `PDFComponentButton` (`PDFRowButton`, `PDFCardButton`), `PDFToolBadge`, `PDFTag`, `PDFTextLink`, `PDFStarButton`, `PDFColorSwatch`, `PDFPageThumbnail`; stránky galerie 19–23, test `UnitTestsFluentComponents` | kód 27 (2 chyby), Figma 6 | odškrtnutí volby myší a mezerníkem, `&` v textech není mnemonika; I-25, I-26 |
| 2 | Řádky `PDFLinkRow`, `PDFRecentFileRow`, `PDFRecentToolRow`, `PDFWorkspaceRow`, `PDFToolRow`, `PDFCollectionRow`, `PDFToolShortcut`, `PDFIssueRow`, `PDFVersionRow`, `PDFPropertyRow`, tlačítko `PDFMoreButton` | kód 29 (2 chyby), Figma 6 | klávesnice a fokus editace Property row, umístění nabídky tlačítka bez fokusu (`popupAnchor`); I-27, I-28 |
| 3 | Dlaždice a karty (`PDFQuickActionTile`, `PDFFolderCard`, `PDFPinnedCard`, `PDFToolCategoryHeader`, `PDFToolChoice`, `PDFResourceCard`, `PDFHighlightCard`, `PDFVideoCard`, `PDFReadingThemeCard`, `PDFPreviewCard`), obrázky `PDFPreviewPainter`, `PDFLockedDocumentPicture`, logo; vzorek Home | kód 24 (1 chyba), Figma 14 + 6 nesrovnalostí | karta zrušená slotem `menuRequested`, výška podle šířky s mezipamětí, `drawScaledPixmap`, kontrola všech 364 ikon; I-29 až I-36, O-09 |
| 4 | `PDFPageSelection` s parserem `PDFPageRange`, `PDFContinueWith`, `PDFColorButton` a `PDFColorPalette`, `PDFSignatureCard`, `PDFStrengthMeter` | kód 29 (4 chyby), Figma 8 + 7 nesrovnalostí | jedno pravidlo směru textu (`DirectionGuard`, absolutní zarovnání), výsledek výběru jako intervaly, ohlašování chyb čtečce; I-37 až I-42, O-10 |
| 5 | Role `PDFComponentRoles`, `PDFRowDelegate` a delegáti `PDFThumbnailDelegate`, `PDFBookmarkDelegate`, `PDFNoteDelegate`, `PDFAttachmentDelegate`, `PDFSearchResultDelegate`, `PDFFontDelegate`, `PDFRecentItemDelegate`, `PDFShortcutDelegate` s `PDFShortcutRecorder`; stisk řádků předvoleb `PDFTreeView`; texty dat (I-43) | kód 30 (3 chyby, 6 rizik), Figma 9 + 4 nesrovnalosti | viz níže; I-44, I-45, rozšířená O-09 |

Rozhodnutí 27 až 30 vlastníka ke krokům 1 a 2 ([components.md](components.md), 8.5, provedení v kapitole 17): Tag má Hover a Pressed jako dvě výplně nad `bg/subtle`, Star button a Color swatch mají pod překryvem vlastní plochu `bg/surface`, šipka Collection row je pod ukazatelem `icon/default`, shrnutí Version row má řádky 14 px.

Opravy kontrol kroku 5 (17. 10. 2026):

- **Životnost pohledů (chyba).** Delegát, který je dítětem pohledu, sahal v destruktoru a ve filtru na už smazaný viewport (Qt drží viewport jako holý ukazatel, `QPointer` pohledu je v destruktoru `QWidget` ještě platný) – poškození haldy, které Release skryl. Pohled i viewport jsou v `QPointer`, pohled se zapomíná při jeho signálu `destroyed` (ten přichází před smazáním dětí), `viewport()` rušeného pohledu se nevolá. Pohled se přestane sledovat i po `setItemDelegate` jiného delegáta, viewport z `setViewport` se sleduje místo starého. Ověřeno testy pro každé pořadí rušení a samostatným sestavením Debug s ladicí haldou MSVC (zpožděné uvolnění, kontrola haldy) mimo repozitář: původní kód haldu poškodí, opravený ne.
- **Ztracené uvolnění (chyba).** Stisk delegáta i `PDFTreeView` končí i pohybem bez tlačítka, blokujícím modálním oknem (dialog ze slotu dvojkliku), ztrátou fokusu, tažením a opuštěním bez tlačítka; skrytí jiného pohledu sdíleného delegáta stisk neruší.
- **Sdílený delegát zkratek (chyba).** Každý záznam si pamatuje svůj pohled a položku, uložení a zavření editoru jdou jen do jeho pohledu (sloty pohledu, ne signály delegáta), pohled po zrušení záznamu znovu zaznamenává. Klávesy začátku záznamu (i Enter numerické klávesnice) mají přednost před zkratkami aplikace, Tab a Backtab posunou fokus dál, zaznamenaná kombinace je normalizovaná jako v `QKeySequenceEdit`.
- **Velikosti.** `sizeHint` nikdy nečte obrázek (model náhledů v `Qt::DecorationRole` vykresluje stránku), platí `Qt::SizeHintRole`, `QImage` se převádí jednou (zmenšená kopie se najde v `QPixmapCache`), výšky poznámek a úryvků jsou v mezipaměti a počítají se jedním rozvržením; šířka položky stromu se počítá od kořenového indexu a ve sloupci stromu, výšky se přepočítají po změně šířky sloupce, písma a jazyka. Řádky seznamu nejsou širší než viewport ani po objevení svislého posuvníku (dřív se objevil i vodorovný).
- **Figma.** Pruh průběhu plní od náběžného okraje pro každého volajícího (`paintProgressBar`), kroužek fokusu předvoleb Outline a Certificate obepíná celý řádek včetně odsazení a všech sloupců, jejich výběr zůstává bez fokusu `bg/accent-subtle` (I-44), galerie ukazuje jejich varianty Pressed a Focus s uzly Figmy a živé seznamy mají mezery obrazovek (panely 2, náhledy mřížka 8 / 6; dokumentováno u delegátů). Přepsaná barva ikony Icon button platí ve všech stavech tlačítka – uložení Attachment row i akce Property row (krok 2).
- **Drobnosti.** Index jiného modelu je neškodný, tooltip akce zmizí nad prázdnou plochou, akce nabídky zrušeného pohledu nic neudělá, slot, který zruší delegáta, nevede k pádu, generický editor Qt se nad řádky neotevře, vlastní kontextová nabídka třídy pohledu aplikace zůstává, oblast větví položky delegáta ve stromu styl nevyplňuje, zaškrtávací pole stisknutého řádku není stisknuté, `triggerAction` je nevirtuální (`performAction`), `getLastActionView`. Jednořádkové texty všech kreslicích funkcí kreslí jediná `PDFComponentPainter::drawElidedText` se směrem celého textu dat (dřív čtyři kopie pomocné funkce); názvy tabů dokumentů jsou texty dat.
- **Přenositelnost.** Výchozí argument `const State& state = State()` v `PDFComponentPainter::paintProgressBar` GCC odmítá (vnořená třída s inicializátory členů nemůže být výchozím argumentem uvnitř své třídy, CWG 1397); nahrazen přetížením.

K rozhodnutí vlastníka zůstávají body review I-29 až I-42, I-44 a I-45 (každý s doporučeným řešením) a odchylky O-09 a O-10 k potvrzení; I-24 (barvy sloupců určuje model) zůstává záměrně ([components_review.md](components_review.md)).

Konečný stav: build cílů `Pdf4QtLibFluent`, `Pdf4QtFluentGallery`, `UnitTestsWidgetStyle` a `UnitTestsFluentComponents` bez varování, `UnitTestsFluentComponents` 132/132, `UnitTestsWidgetStyle` 194/194, galerie (1 594 obrázků) vyexportovaná ve světlém a tmavém režimu při 1× a 2× bez fialového úniku.

### 18.1 Kontrola pod Linuxem a valgrindem (17. 10. 2026)

Všech 36 testů ctest proběhlo pod valgrind memcheck na Linuxu ve WSL (`Ubuntu_Default`, gcc-15, Qt 6.11.3; postup v CLAUDE.md, *Build and test*). Nálezy a opravy:

- **Use-after-free v `PDFColorButton` (chyba knihovny).** `QWidget::~QWidget` vysílá `destroyed` až po destruktoru `PDFColorPalette` (seznamy barev jsou už uvolněné), ale dřív, než `~QObject` vynuluje `QPointer` na paletu; slot tlačítka se smazané palety ptal na název barvy. Slot `onPaletteDestroyed` paletu nejdřív zahodí (ukáže se výchozí název), `~PDFColorPalette` zavře otevřený popover, dokud je paleta celá, a `~PDFColorButton` svou paletu smaže sám, dokud je tlačítko celé. Prověřena všechna spojení `destroyed` a filtry událostí knihovny (řádky, karty, panely, delegáti, tooltip, styl): stejný vzor měly ještě `PDFPropertyRow` (editor s fokusem smazaný aplikací – destruktor `QWidget` mu bere fokus a řádek potvrzoval editaci z rušeného `QLineEdit`; teď ji zruší událost `ChildRemoved`) a `PDFMoreButton` (nabídka smazaná během zobrazení nevyšle `aboutToHide`, tlačítko zůstalo stisknuté; uvolní ho i `destroyed`, destruktor tlačítka spojení odpojí). Delegáti, tooltip a styl pravidlo už dodržovali (jen porovnávají ukazatele, `qobject_cast` rušeného widgetu vrací `nullptr`). Testy `colorPaletteDestroyed`, rozšířené `moreButtonMenu` a `propertyRowFocusDeletion`.
- **Neplatné čtení ve `wrapText` (chyba testu).** Test `continueWithReview` držel referenci na prvek seznamu vráceného hodnotou (`PanelSamples::choices().at(i)`), seznam zanikl s koncem výrazu. Knihovna text kopíruje hned na začátku; test si seznam drží.
- **8 porovnání widgetu s kreslicí funkcí selhávalo nativně na Linuxu.** Rastrový engine Qt kreslí glyfy FreeType na zařízení `QWidget` ve výchozím formátu písma (subpixelové vyhlazení podle fontconfig, v Ubuntu `10-sub-pixel-rgb.conf`), do `QImage` vždy v odstínech šedi (`QRasterPaintEngine::drawCachedGlyphs`); na Windows jsou obě cesty stejné. Knihovna kreslí jako ostatní widgety systému, test porovnával dvě různá zařízení. Kreslicí funkce porovnávaná s widgetem se teď kreslí v `paintEvent` pomocného widgetu (`renderInWidget`), sondy barev zůstávají na `QImage` (nezávislé na vyhlazení systému).
- **Únik ve fontconfig (třetí strana).** 320 bajtů z parseru konfigurace fontconfig 2.17.1 při prvním `FcConfigGetFonts` (sem vede `QFontDatabase::addApplicationFont` z `PDFThemeFonts::loadFonts`); přesně stejný únik má program v C, který volá jen `FcConfigGetFonts`. Úzké potlačení s odůvodněním v `~/pdf4qt-ci/tools/pdf4qt.supp`.
- **Opraveno dříve při téže kontrole.** `PDFCertificateInfo::getCertificateInfo` (`Pdf4QtLibCore/sources/pdfcertificatestore.cpp`) bral klíč certifikátu přes `X509_PUBKEY_get`, který přidá referenci, a klíč neuvolnil – nahrazeno `X509_PUBKEY_get0` (bez reference, s kontrolou `nullptr`). Výchozí argument `const State& state = State()` v `paintProgressBar`, který GCC odmítá (CWG 1397), je nahrazen přetížením (viz *Přenositelnost* výše).

Výsledek: valgrind 36/36 testů bez chyb a bez definitivních úniků (potlačen jen únik fontconfig), nativně na Linuxu `UnitTestsFluentComponents` 133/133 a `UnitTestsWidgetStyle` 194/194, na Windows totéž, build čtyř cílů Fluent bez varování.

## 19. Generický test životnosti widgetů (17.–18. 10. 2026)

Tři chyby kapitoly 18 (viewport delegáta, kontextová nabídka delegáta, slot `destroyed` u `PDFColorButton`) prošly testy ve Windows Release a našla je až ladicí halda a valgrind. Aby se tato třída chyb hledala sama, i u widgetů napsaných později, je tu test `UnitTestsFluentLifetime` ([tst_fluentlifetimetest.cpp](../UnitTests/tst_fluentlifetimetest.cpp)). Bere každý widget každé stránky galerie (`GalleryPages::createPage`, stránky `00` až `23`) a ruší ho v každém pořadí a situaci; nový widget knihovny je pokrytý tím, že má živý vzorek na stránce galerie (pravidlo v [components.md](components.md) 6.4 a v CLAUDE.md). Pravidlo hlídá `librarySamples`: seznam tříd knihovny s metaobjektem generuje při buildu [fluentclasses.cmake](../UnitTests/fluentclasses.cmake) ze zdrojů, třída bez vzorku je chyba, výjimky mají odůvodnění (abstraktní báze, styl, správce témat, popupy a editory, jejichž vlastník vzorek má). Co aplikace s widgetem dělá a obecný vstup testu neumí, nese vzorek jako nápovědu `GalleryLifetimeHints` (`prepare`: nabídky tlačítek „více“ – výchozí u každého živého vzorku, každá druhá je dítětem tlačítka; `startTimers`: stav Copied a vložený editor Property row). `detectorSelfTest` dokazuje, že kontroly instance hlásí, k čemu jsou.

**Scénáře** (jeden řádek dat na stránku galerie):

- `pageDeletedWhileActive` – stránka nebo okno se smaže, když je widget stisknutý nebo pod ukazatelem, má fokus z klávesnice (test ověří, že styl ukazuje prstenec a u tlačítek přiložil rámeček fokusu), ukazuje tooltip, je držený Alt (mnemoniky), běží časovače vzorků; okno skryté a pak smazané, styl aplikace vyměněný (i za Fusion a za `QProxyStyle` a zpět), téma a směr změněné.
- `childDeletedFirst` – každý widget, který smí aplikace smazat (`WidgetCatalog`: ne části widgetů Qt ani tlačítka akcí `QToolBar`; widget přidaný `addWidget` ano), sám v dávkách nezávislých widgetů, střídavě pod ukazatelem, stisknutý a s fokusem; potom objekty aplikace, které nejsou widgety (skupiny tlačítek a akcí, akce, modely, delegáty, efekty, časovače, rozvržení kontejnerů aplikace); potom překreslení, hover, Tab, skrytí a zobrazení, změna velikosti, téma, směr rozvržení, `LanguageChange`, paleta a písmo aplikace, style sheet.
- `deletionOrders` – mazání v obráceném pořadí vytvoření a rodiče napřed s dítětem s fokusem a pod ukazatelem; widget každé třídy knihovny přesunutý do jiného okna (`setParent`) a smazaný v obou pořadích s oknem.
- `deletedFromOwnSignal` – záznamník je připojený ke všem objektům stránky (widgety, delegáti pohledů, sloupců a řádků) a k objektům zobrazených popupů knihovny; vstup (klik, dvojklik, pravé tlačítko, klávesy do vnořeného pole s fokusem, kolečko, tažení, odchod fokusu, F2 a zkratka do editoru, klik na každý druh widgetu popupu) jde postupně na každý widget, takže se najdou i signály složených widgetů, sourozenců, delegátů a popupů. Testují se signály tříd knihovny, `QAbstractButton` / `QAbstractItemView` a signály bází Qt, které knihovna emituje sama (názvy z `Q_EMIT` sbírá fluentclasses.cmake). Ze signálu se synchronně smaže emitent, jeho rodič (u popupu vlastník), okno, delegát nebo model. Každý signál knihovny na stránce musí být nalezen; signál, který se znovu neobjeví, se zkusí jako první na čerstvé stránce, pak po historii objevování, jinak je to chyba.
- `popupsOpen` – nabídky, kontextové nabídky delegátů, palety, rozbalení comboboxů, kalendář editoru data, message boxy `PDFMessageBox::information` a dalších, tooltipy a editory: smazaný vlastník, smazaný popup (popup knihovny i uvnitř vlastního `exec()`), smazané nebo skryté okno, přijatá akce, změna tématu / směru / stylu / jazyka a klik do stínového okraje okna při otevřeném popupu. Popup, který se na čerstvé stránce znovu neotevře, je chyba.
- `itemViews` – model, delegát (i sloupců a řádků, jeden delegát pro dva pohledy), pohled a viewport v každém pořadí ve stavech hover, stisk, editace (editor každého typu `PDFItemEditorFactory`, i s jeho vlastním popupem), kontextová nabídka a hover akce; reset modelu, `setModel(nullptr)`, vyměněný delegát, odebraný řádek, řazení, vyměněná hlavička, mazání ze signálů `PDFShortcutRecorder`. Pohled, který stav jednou dosáhl, ho musí dosáhnout v každém pokusu.

**Kontroly, které fungují i v Release:** háky QtCore (`qtHookData`, signal spy) sledují všechny objekty, takže dvojí zrušení, událost doručená zrušenému objektu a signál zrušeného objektu jsou chyby (slot se nekontroluje – Qt volá jeho zpětné volání jen u spojení řetězcem a spojení zrušeného příjemce ruší). Po každé instanci stránky nesmí zůstat widget, okno, popup ani objekt (kromě líně vytvářených globálních objektů Qt a knihovny, `InstanceCheck::getSingletons`), dlouho žijící objekt (aplikace, správce témat, styl, …) nesmí mít víc spojení svých signálů ani filtrů událostí než předtím (lambda připojená bez kontextového objektu, filtr na `qApp`), Qt nesmí vypsat varování (krátký seznam povolených s odůvodněním) – ani mimo instanci (vytvoření katalogu, rozdíl hlásí příští instance) – a `cleanupTestCase` ověří, že proces nic nezanechal. Aktivní je asistenční technologie (`AccessibilityClient`): rozhraní objektů každé aktualizace přístupnosti čte z cyklu událostí jako klient v jiném procesu. Čtení uvolněné paměti bez vedlejšího efektu najde valgrind – při kontrole mutacemi byl na Linuxu vždy první nativní pád, vlastní kontroly testu nikdy nevystřelily dřív; valgrind zůstává spolehlivým detektorem.

**Co Qt samo nepřežije, test nedělá** (místo seznamu případů obecná pravidla): mazač odmítne smazání, které by zrušilo objekt, jehož signál třídy Qt právě Qt emituje (test sleduje rozpracované emise; signály `QAbstractButton`, které Qt chrání, a skupiny `QButtonGroup` se zkouší, `toggled` u `QCheckBox` ne – `QCheckBox::nextCheckState` volá `checkStateSet` po něm), a šipku, která za filtry knihovny dojde do `QAbstractButtonPrivate::moveFocus` (filtr `ArrowProbe` instalovaný jako poslední; dřívější výjimka všech šipek u tlačítek tak už nezakrývá navigaci knihovny – `PDFNavList`, `PDFContinueWith`, `PDFColorPalette` – a dřívější heuristika „filtr knihovny“ byla chybná: na vzorku palety šipka do `moveFocus` došla). Pohled se ze svých signálů pressed, clicked, doubleClicked, activated a entered nemaže (maže se delegát a model). Další výjimky Qt s komentářem: delegát sloupce či řádku se před smazáním z pohledu vyjme (`delegateForIndex` vrací smazaného delegáta), přístupnost se neptá na nabídky Qt (`QAccessibleMenuItem` drží smazanou akci), nesmí se mazat tlačítka akcí `QToolBar` ani jeho `toggleViewAction`. Povolená varování: `QFontDatabase: Cannot find font directory` (offscreen ve Windows), `propagateSizeHints` / `raise` / klávesnice offscreen a `QItemSelectionModel: Selecting when no model` jen při smazaném modelu.

**Rozsah a čas.** Celý běh: asi 1 250 instancí stránek, 4 120 widgetů v pořadích rušení, 1 303 widgetů smazaných samostatně a 376 objektů, které nejsou widgety, 96 widgetů přesunutých do jiného okna; vstup na 171 widgetů našel 505 signálů a 791 smazání ze signálu (23 smazání, která Qt samo nepřežije, mazač odmítl; 29 signálů se zopakovalo až na čerstvé stránce); 144 popupů a 616 akcí s otevřeným popupem; 22 variant pohledů a 674 pokusů v požadovaném stavu; 101krát přiložený rámeček fokusu stylu. Test má 158 procesů (`function:tag`): Windows 54,2 s v jednom procesu, 9,3 s rozdělený na 30 jader (součet procesů 193 s; tři běhy najednou 26,6 s souběžně s valgrindem); Linux nativně 9 s na 28 jádrech; valgrind `--filter 'Fluent|WidgetStyle'` na 14 úlohách 307 s (5,1 min; součet procesů testu životnosti 2 994 s, nejdelší položkou je celý `UnitTestsFluentComponents` se 177 s, z testu životnosti `itemViews:22-document` se 122 s). `vg-run.py` dělí test sám (`--shard`, výchozí `FluentLifetime`).

**Nalezené chyby a opravy:**

| Místo | Příčina | Oprava | Regresní test |
| --- | --- | --- | --- |
| `PDFLineEdit` (`pdffluentwidgets.h`, `.cpp`) | tlačítko hesla holým ukazatelem: tlačítko smazané aplikací shodilo pole při změně směru, velikosti a dalším `setPasswordField` | `QPointer`, příznak `m_isPasswordField` | `regressionPasswordButtonDeleted` |
| `PDFMessageBox` (`pdfmessagebox.h`, `.cpp`) | části (titulek, text, detaily, zaškrtávátko, tlačítka, hlavní rozvržení) holými ukazateli: smazaná část shodila zobrazení, kopírování a tlačítka; bez tlačítek nešel zavřít | `QPointer` a kontroly; bez krabice tlačítek nebo bez tlačítek `reject()` zavře | `regressionMessageBoxParts`, `regressionMessageBoxWithoutButtons` |
| `PDFMessageBox::showMessage` | message box na zásobníku s rodičem volajícího: rodič smazaný během zobrazení ho smazal jako dítě a zásobník podruhé | `new` + `QPointer`, výsledek `NoButton` | `regressionMessageBoxParentDeleted`, `regressionMessageBoxDeletedInExec` (`delete` i `deleteLater` uvnitř `exec`) |
| `PDFDocumentTabBar` (`pdfcomponentpanels.cpp`) | `QTabBar` drží tlačítka tabů holými ukazateli: smazané tlačítko zavření shodilo tab bar při kreslení | slot `destroyed` tlačítko z tabu vyjme, destruktor tab baru spojení odpojí | `regressionTabCloseButtonDeleted` |
| `PDFKeyValueList::contextMenuEvent` | nabídka na zásobníku: seznam smazaný během nabídky ji smazal jako dítě a zásobník podruhé | `new` + `QPointer`, hodnota se zkopíruje předem | `regressionKeyValueListMenu` |
| `PDFColorPalette::updateSelection`, `rebuild`, `PDFColorButton::showPalette` (`pdfcomponentpanels.cpp`) | výběr vzorku barvy vyšle `toggled`; slot aplikace, který smazal paletu (nebo tlačítko), shodil `showPalette`, `setCurrentColor` a `setSuggestedColors` (poškození haldy, našlo `deletedFromOwnSignal` záznamníkem na všech objektech) | `QPointer` stráže, `updateSelection` a `rebuild` vracejí, zda paleta žije; `colorsChanged` jen u živé palety | `regressionPaletteDeletedBySwatch` (4 režimy); mutace ověřena: pád |
| `PDFWidgetStyleImpl::handlePopupMarginEvent` | po přeposlané události kliknutí do stínu popupu zapisoval do člena stylu, i když akce kliknutí styl vyměnila (styl smazán) | `QPointer` na sebe | `regressionStyleReplacedInForwardedClick` |
| `PDFWidgetStyleImpl` – rámeček fokusu | tlačítko s fokusem (nebo jeho kontejner) přesunuté do jiného okna: `QFocusFrame` zůstal v rodiči starého okna (`QWidget::mapTo` varovalo, rámeček v cizím okně) | při `ParentChange` widgetu s rámečkem nebo jeho předka se rámeček odpojí a připojí znovu z cyklu událostí | `regressionFocusFrameReparented`; mutace ověřena: varování |
| `PDFCalendarWidget` (`pdffluentwidgets.h`) | viewport vnitřního pohledu holým ukazatelem: vyměněný viewport se použil při kreslení buňky | `QPointer` | `regressionCalendarViewportReplaced` |
| galerie | vzorky kreslené funkcí držely styl a prototypy holými ukazateli, message boxy stránky 16 na zásobníku, živý měřič síly hesla volal měřič po `setLevel` (slot jiné části aplikace ho mohl smazat) | styl v době kreslení, `QPointer`, statické API `PDFMessageBox`, `setLevel` jako poslední volání | pokryto scénáři testu |
| test (nalezené mezery) | po zavřeném popupu nezůstalo aktivní okno a klávesové kroky se tiše neprovedly (fokus nešel nastavit); dedup variant spojoval různé kontexty (pole stránky výběru, pohledy s jinými delegáty a typy hodnot) | `Input::focus` okno aktivuje; varianta obsahuje předka z knihovny, delegáta, počet spojení signálů, pohledy typy hodnot | kontrola pokrytí signálů |

**Zkratky Ctrl+Alt.** Windows nerozliší AltGr od Ctrl+Alt a pro Ctrl+Alt+klávesa dávají znak úrovně AltGr („[“ u Ctrl+Alt+F na české klávesnici), takže editor zkratek (`PDFShortcutRecorder`) ukládal nesmysl. První oprava (virtuální kód kláves A–Z / 0–9) nefungovala na české, slovenské a francouzské klávesnici pro řadu číslic: Qt tam Ctrl+Alt+2 nikdy nenabídne (nález C1 revize). Platí proto: **ve Windows je zkratka s Ctrl+Alt vždy všechny modifikátory + základní klávesa fyzické klávesy** – její znak bez modifikátorů v aktuálním rozložení, velkým písmenem (`ToUnicode` bez stisknutých kláves, u mrtvé klávesy její samostatný znak), přesně kandidát `qtKey[0]`, kterého `QWindowsKeyMapper::possibleKeyCombinations` mapě zkratek Qt nabízí vždy se všemi modifikátory. Česky tedy Ctrl+Alt+F, Ctrl+Alt+V (ne „@“), Ctrl+Alt+Ě pro klávesu „ě/2“, Ctrl+Alt+Ú; francouzsky Ctrl+Alt+É; rusky Ctrl+Alt+Ф (latinské Ctrl+Alt+A je jiný kandidát téže klávesy a funguje také); Ctrl+Alt+´ u mrtvé klávesy. Zkratka „@“ (AltGr) i zkratka Ctrl+Alt+V se spustí stejným stiskem (rozhodnutí C2). Klávesy bez znaku (F1–F24, šipky, Enter, numerická klávesnice bez NumLocku) mají klávesu Qt, ostatní platformy se nemění. Čisté zobrazení `getWindowsKeyCombination` (virtuální klávesa → znak, modifikátory → kombinace) je oddělené od dotazu na rozložení (`getBaseCharacter`), test `shortcutControlAlt` ho volá se znaky české, německé, francouzské, ruské a americké klávesnice (písmena, číslice, OEM klávesy, Shift, mrtvé klávesy, numerická klávesnice, F-klávesy) a u každého řádku ověří proti modelu `possibleKeyCombinations` (opsanému ze zdrojů Qt 6.11.2), že zaznamenaná kombinace je kandidátem téhož stisku a že dřívější výsledek kandidátem není. Akce delegátů (`PDFRowDelegate`) a startovací klávesy `PDFShortcutDelegate` porovnávají stisk se stejnou sadou kombinací (`PDFShortcutRecorder::getMatchingKeyCombinations`: zaznamenaná, kombinace události a symbol AltGr samotný), takže zaznamenaná zkratka Ctrl+Alt+F na české klávesnici akci spustí (nález C3, test `shortcutControlAltActions`).

**Kontrola mutacemi** – historické chyby kapitoly 18, opravy této kapitoly a umělé chyby vrácené do knihovny po jedné (agent mutací ve vlastní kopii ve WSL; nativně = pád na Linuxu, valgrind = první rámec knihovny):

| Mutant | Co se vrátilo | Nativně | Valgrind | Odhaleno |
| --- | --- | --- | --- | --- |
| M1 | delegát / viewport (`pdfcomponentdelegates.cpp` ~452/500) | SIGSEGV | invalid read/write v `PDFRowDelegate::uninstall` | ano |
| M2 | kontextová nabídka delegáta, lambda s holým `this` | SIGSEGV | invalid read v `isActionAvailable` / `getViewOption` | ano |
| M3 | slot `destroyed` a destruktor `PDFColorButton` | SIGABRT | invalid read/write v `~PDFColorPalette` | ano |
| M3a | jen pořadí ve slotu (tlačítko se ptá palety na název barvy) | – | – | ne → vzorek `PDFColorButton` bez vlastního názvu („Live, the name from the palette“) |
| M4 | `showMessage` na zásobníku | – | – | jen regresní test → galerie volá `PDFMessageBox::question` / `critical` / … |
| M5 | části message boxu holými ukazateli | SIGSEGV | invalid read v `ensureDefaultButton` | ano |
| M6 | tlačítko hesla holým ukazatelem | SIGSEGV | `placePasswordButton` | ano |
| M7 | nabídka `PDFKeyValueList` na zásobníku | SIGABRT | invalid free | ano |
| M8 | `PDFDocumentTabBar` bez `destroyed` tlačítka zavření | SIGSEGV | `paintEvent` | ano |
| M9 | destruktor `PDFDocumentTabBar` bez odpojení | SIGSEGV | skok na neplatnou adresu | ano |
| M10 | `~PDFMoreButton` bez `disconnectMenu` (oprava 18.1) | – | – | ne → každá druhá nabídka je dítětem tlačítka (`GalleryLifetimeHints::giveMenus`) |
| A1 | bez `QPointer` po uvolnění v `PDFComponentButton` | SIGSEGV | `mouseReleaseEvent` | ano |
| A2 | synchronní `delete` tagu v `PDFVideoCard::rebuildTags` | – | – | ekvivalentní (klik Qt a stráže to přežijí) |
| A3 | lambda `themeChanged` bez kontextu | SIGSEGV | `invalidateHeight` | ano (zbylé spojení teď hlásí i počítadlo spojení dlouho žijících objektů ve Windows, mechanismus ověřuje `detectorSelfTest`) |
| A4 | delegát nezapomene pohled při `destroyed` | – | – | ekvivalentní (`QPointer` a `destroyed` viewportu) |

Doplňky M3a a M10 ověřil agent mutací ve své kopii (s nimi oba mutanty odhalí). Mutace oprav této kapitoly (palety a rámečku fokusu) ověřil fix agent ve Windows: test spadne, resp. selže na varování; zdroj byl obnoven a kontrolní součet SHA-256 sedí.

Výsledek: build cílů `Pdf4QtLibFluent`, `Pdf4QtFluentGallery`, `UnitTestsWidgetStyle`, `UnitTestsFluentComponents` a `UnitTestsFluentLifetime` bez varování ve Windows (MSVC) i v Linuxu (GCC 15); Windows `UnitTestsFluentLifetime` 158/158 (i třikrát najednou), `UnitTestsFluentComponents` 164/164, `UnitTestsWidgetStyle` 192/192 (bez `initTestCase` / `cleanupTestCase`); Linux nativně 158/158, 164/164, 192/192; valgrind bez chyb a bez definitivních úniků ve všech procesech. Cíl testu se sestaví jen se soukromými hlavičkami QtCore (`Qt6::CorePrivate`), jinak CMake vypíše zprávu a test vynechá. Otevřené: test neprověřuje vypnutí aplikace s žijícími widgety (`aboutToQuit`) ani vypršení časovače skrytí tooltipu (10 s); otvírače popupů jsou dosud pevný seznam kroků, nápovědy vzorků zatím pokrývají nabídky a časovače.
