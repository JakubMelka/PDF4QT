# PDF4QT – specifikace komponent z Figmy (stránka Components)

Přesný opis stránky **Components** návrhu sjednocené aplikace: všech 92 komponent (70 sad s 552 variantami a 22 samostatných komponent), ukázkové rámce a ikony. Je to podklad pro implementaci složených widgetů knihovny `Pdf4QtLibFluent`. Co se z komponent stane v kódu (widget, delegát, varianta stylu) a v jakém pořadí, říká [components.md](components.md).

Stav: přečteno z Figmy 13. 10. 2026 po opravách komponent z téhož dne (soubor `PDF4QT`, klíč `QVGmx58DYmMq41kwfpMjtl`, stránka *Components* `158:4605`, sekce `46:2`). Seznam oprav je v [components_fixes.md](components_fixes.md), otevřené body v [components_review.md](components_review.md). Po opravách byly všechny karty porovnány s výstupem čtecího skriptu řádek po řádku (počet variant, rozměr, popis, vlastnosti, stavba výchozí varianty a rozdíly variant); poznámky karet strojově porovnat nejde. **Závazná je Figma, ne tento dokument.** Po změně komponenty ve Figmě se její karta musí přečíst znovu. Figma se čtením nezměnila.

## Obsah

1. [Jak číst karty](#1-jak-číst-karty)
2. [Kostra aplikace a Home](#2-kostra-aplikace-a-home) – 22 komponent (Window button a Window controls doplněné 18. 10. 2026)
3. [Katalog toolů, What's New, About](#3-katalog-toolů-whats-new-about) – 17 komponent
4. [Formulářové prvky a Settings](#4-formulářové-prvky-a-settings) – 16 komponent
5. [Okno dokumentu](#5-okno-dokumentu) – 20 komponent
6. [Panely dokumentu, barvy a náhledy](#6-panely-dokumentu-barvy-a-náhledy) – 19 komponent
7. [Ukázkové rámce sekce](#7-ukázkové-rámce-sekce)
8. [Ikony](#8-ikony)

## 1. Jak číst karty

Každá karta má čtyři části: vlastnosti komponenty, stavbu výchozí varianty, rozdíly ostatních variant a poznámky. Stavba a rozdíly jsou doslovný výstup čtecího skriptu, názvy vrstev, vlastností, variant a tokenů jsou tedy přesně jako ve Figmě.

Skript, který karty přečetl, je uložený vedle tohoto dokumentu: [components_dump.js](components_dump.js). Pouští se nástrojem `use_figma` (jen čte), na prvním řádku se vyplní identifikátory komponent.

### Zápis stavby

Jeden uzel je jeden řádek, odsazení o jednu mezeru je jedna úroveň zanoření:

```text
<typ> '<název vrstvy>' <šířka>x<výška> klíč=hodnota; klíč=hodnota; …
```

| Zápis | Význam |
| --- | --- |
| typ `C` `F` `T` `I` `R` `E` `V` `G` `L` `B` | komponenta, rámec, text, instance, obdélník, elipsa, vektor, skupina, čára, logická operace |
| `hid=1` | vrstva je ve výchozím stavu skrytá (obvykle ji zapíná přepínač, viz `ref`) |
| `at=x,y` | poloha v rodiči (jen když rodič nemá auto layout); ` abs` = absolutně umístěná vrstva uvnitř auto layoutu |
| `con=` | vazby (vodorovná/svislá) u vrstvy bez auto layoutu |
| `rot=` | otočení ve stupních |
| `lay=H\|V gap<n> pad<nahoře>,<vpravo>,<dole>,<vlevo> <hlavní osa>/<příčná osa>` | auto layout: směr, mezera, vnitřní okraje, zarovnání; `wrap<n>` = zalamování s mezerou řádků |
| `z=<vodorovně>/<svisle>` | velikost: `fix` pevná, `hug` podle obsahu, `fill` vyplní rodiče |
| `mm=minW,maxW,minH,maxH` | nejmenší a největší rozměry |
| `fill=<token>` | výplň proměnnou kolekce *Color*; více výplní je spojeno `+` |
| `#rrggbb!` | barva, která **není** navázaná na proměnnou |
| `@0.5` | krytí výplně, pokud se liší od alfy proměnné |
| `st=<token> w<tloušťka> <INSIDE\|CENTER\|OUTSIDE>` | obrys; `w1,1,0,1` = tloušťky nahoře, vpravo, dole, vlevo; `dash` = čárkování |
| `rad=<n>` nebo `rad=lh,ph,pd,ld` | poloměr rohů (levý horní, pravý horní, pravý dolní, levý dolní) |
| `fx=DROP_SHADOW(x,y,rozostření,rozšíření <token>)` | efekt |
| `op=` | krytí vrstvy |
| `clip=1` | vrstva ořezává obsah |
| `f=<rodina> <řez> <velikost>/<výška řádku> <zarovnání> <automatická velikost>` | písmo; zarovnání `L` `C` `R` `J` vodorovně a `T` `C` `B` svisle; `ls` = prostrkání v pixelech; `trunc<n>` = zkrácení výpustkou na n řádků |
| `tx='<text>'` | text vrstvy (delší než 42 znaků je zkrácen znakem `…`) |
| `ref=<co>:<vlastnost>` | vrstvu řídí vlastnost komponenty: `visible` viditelnost, `characters` text, `mainComponent` záměna instance |
| `ic=<ikona> stroke=<token> w<tloušťka> fill=<token>` | instance ikony a barva jejího vektoru: obrys, u vyplněné ikony i výplň |
| `inst=<komponenta>{<varianta>} [<vlastnost>=<hodnota>] ov:<pole>` | instance jiné komponenty (nerozbaluje se, má vlastní kartu); `ov:` = pole přepsaná proti hlavní komponentě. Zda je instance vystavená (její vlastnosti jdou nastavit na nadřazené instanci), říkají poznámky karty. |
| `… +N nodes` | strom byl zkrácen (jen u ilustrací) |

### Zápis rozdílů variant

Rozdíl se uvádí proti výchozí variantě. Varianty s jinou hodnotou vlastnosti `State` než `Default` (`Hover`, `Pressed`, `Focus`, `Disabled`, `Error`, …) se uvádějí proti variantě `State=Default` téže skupiny (je to napsáno v závorce `proti …`). Sady, jejichž varianty se liší jen názvem odstínu, mají jeden řádek se zástupným znakem (`Hue=<hue>`).

| Zápis | Význam |
| --- | --- |
| `kořen: klíč=hodnota` | změna na kořenové vrstvě varianty |
| `a>b: klíč=hodnota` | změna na vrstvě `b` uvnitř vrstvy `a`; `x#2` je druhá vrstva jménem `x` |
| `klíč=-` | hodnota ve variantě není nastavená |
| `+ cesta` | vrstva, kterou základní varianta nemá (některé karty píšou slovem „přidáno“ nebo „přibývá“) |
| `- cesta` | vrstva, kterou tato varianta nemá (některé karty píšou „chybí“) |
| `~ cesta` | změna na vrstvě; znak `~` karty uvádějí jen někde, význam je stejný jako bez něj |

### Co je dobré vědět o čtení

- **Kořen komponenty.** Způsob určení velikosti kořene, ořez obsahu a to, zda se obrys počítá do rozložení, uvádí první poznámka každé karty. Stavba je na kořeni nemá.
- **Skryté vrstvy.** Vrstva skrytá ve výchozím stavu (`hid=1`) hlásí pevnou velikost (`z=fix/…`), i když po zobrazení vyplní šířku. Skutečné chování je v poznámkách.
- **Barvy.** Karty uvádějí názvy tokenů; hodnoty obou barevných režimů jsou v `PDFThemeColors`. U ikon je uvedena barva vektoru po přepsání v komponentě, ne barva hlavní komponenty ikony.
- **Popisy.** Figma vrací v popisech entity HTML (`&quot;`, `&amp;`, `&#39;`); znamenají uvozovku, ampersand a apostrof.
- **Skript po ověření.** [components_dump.js](components_dump.js) byl po ověření rozšířen: čte i vrstvy skryté ve výchozím stavu, na kořeni vypisuje velikost (`z=`) a ořez (`clip=1`), `sil=0` znamená, že se obrys nepočítá do rozložení (vypisuje se jen u vrstvy s auto layoutem, která obrys má – ve variantě bez obrysu chybí), `exp=1` označuje vystavenou vnořenou instanci, u ikon vypisuje obrys i výplň a prostrkání uvádí s jednotkou (`ls2px`). Karty přečtené rozšířeným skriptem mají tyto údaje přímo ve stavbě; karty komponent, které se od prvního čtení nezměnily, mají na kořeni jen údaje původního skriptu a zbytek v první poznámce.
- **Poloha v sekci.** Kde komponenta nebo sada na stránce leží, není součástí specifikace; sekce byla po opravách znovu srovnána. Rozměr sady uvádějí jen některé karty, hlavně tam, kde se opravou změnil.
- **Co v kartách není.** Prototypové vazby (přechody mezi variantami) a vnitřek vnořených instancí jiných komponent, které mají vlastní kartu.

## 2. Kostra aplikace a Home

### Tab · `48:170`

Sada 8 variant · výchozí varianta `Selected=True, State=Default` · rozměr 195×32

Popis ve Figmě: „Top-level application tab (Home or an open document). Home tab hides the close button (Closable = false). Every variant is at least 32 px high, so a tab without the close button has the same height as a closable one.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **Project_Report.pdf** |
| Icon | záměna instance | **icon/file** |
| Closable | přepínač | **true** |
| Locked | přepínač | **false** |
| Unsaved | přepínač | **false** |
| Progress | přepínač | **false** |
| Selected | varianta | **True**, False |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Selected=True, State=Default' 195x32 lay=H gap8 pad6,10,6,16 MIN/CENTER; z=hug/hug; sil=0; mm=-,-,32,-; fill=bg/subtle; st=border/default w1,1,0,1 INSIDE; rad=8,8,0,0
 I 'icon' 14x14 z=fix/fix; ref=mainComponent:Icon; ic=icon/file stroke=icon/accent w2
 T 'label' 119x16 z=hug/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT WIDTH_AND_HEIGHT; tx='Project_Report.pdf'; ref=characters:Label
 I 'lock' 12x12 hid=1; z=fix/fix; ref=visible:Locked; ic=icon/lock stroke=icon/default w2 fill=accent/amber/icon
 E 'unsaved' 8x8 hid=1; z=fix/fix; fill=text/primary; ref=visible:Unsaved
 I 'close' 20x20 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=4; ref=visible:Closable; inst=Tab close button{State=Default} ov:name
 F 'progress' 195x2 hid=1; at=0,30 abs; con=STRETCH/MAX; z=fix/fix; fill=bg/track; clip=1; ref=visible:Progress
  R 'value' 107x2 at=0,0; con=SCALE/STRETCH; z=fix/fix; fill=bg/accent
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=True, State=Hover` | beze změny |
| `Selected=True, State=Pressed` | beze změny |
| `Selected=True, State=Focus` | `~ .: st=border/focus w2 INSIDE` |
| `Selected=False, State=Default` | `~ .: s=193x32; sil=-; fill=-; st=-`<br>`~ icon: ic=icon/file stroke=icon/muted w2`<br>`~ label: s=117x16; fill=text/tertiary; f=Inter Medium 13/auto LT WIDTH_AND_HEIGHT`<br>`~ unsaved: fill=text/tertiary`<br>`~ progress: s=193x2`<br>`~ progress>value: s=106x2` |
| `Selected=False, State=Hover` (proti `Selected=False, State=Default`) | `~ .: fill=bg/hover` |
| `Selected=False, State=Pressed` (proti `Selected=False, State=Default`) | `~ .: fill=bg/pressed` |
| `Selected=False, State=Focus` (proti `Selected=False, State=Default`) | `~ .: sil=0; st=border/focus w2 INSIDE` |

**Poznámky**

- Kořen: velikost `hug/hug` s nejmenší výškou 32 (`mm=-,-,32,-`), ořez obsahu ne, obrys se počítá do rozložení ne (`sil=0` u všech variant, které obrys mají). Platí pro všech 8 variant.
- **Nové – výška 32 i bez křížku.** Každá varianta má nejmenší výšku 32 (`minHeight`). Záložka bez tlačítka zavření (`Closable=false`, tedy i Home) je proto vysoká 32, dříve byla 28 (16 text + 2 × 6 odsazení). Obsah je svisle uprostřed (`MIN/CENTER`): `icon` na y = 9, `label` na y = 8, `lock` na y = 10, `unsaved` na y = 12 – stejně jako v záložce s křížkem. Šířka se dál řídí obsahem. Záložka s křížkem se nezměnila (6 + 20 + 6 = 32).
- **Nové – zámek je jantarový ve všech stavech.** Skrytá ikona `lock` (`icon/lock`, 12×12): jediný vektor `glyph` má tah `icon/default` w2 a výplň `accent/amber/icon` ve všech osmi variantách (dříve jen ve dvou `State=Default`, v Hover, Pressed a Focus byla výplň `icon/muted`). Výplň instance přepisuje (hlavní komponenta `icon/lock` má vektor jen s tahem). Zápis `fill=` za `ic=` znamená výplň vektoru ikony. Skrytá instance vrací své potomky jen při běhu skriptu s `figma.skipInvisibleInstanceChildren = false`.
- **Nové – tečka neuložených změn má barvu popisku.** `unsaved` (elipsa 8×8) je `text/primary` ve vybrané záložce a `text/tertiary` v nevybrané (ve všech čtyřech stavech), tedy vždy stejná proměnná jako `label` téže varianty. Tah ikony `lock` zůstává `icon/default` ve všech variantách.
- `Selected=True, State=Hover` a `Selected=True, State=Pressed` jsou teď zcela shodné s výchozí variantou – vybraná záložka nemá kreslený stav Hover ani Pressed. Nevybraná záložka je má (`fill=bg/hover`, `fill=bg/pressed`).
- Focus: `border/focus`, 2 px, `INSIDE`. Váha je jednotná `w2`, výchozí varianta má `w1,1,0,1` (bez spodní hrany) – ve stavu Focus je tah i na spodní hraně. Výplň se nemění: `Selected=True` drží `bg/subtle`, `Selected=False` zůstává bez výplně. U `Selected=True` nahrazuje `border/default`.
- `Selected=False, State=Default` nemá výplň ani tah.
- Velikost: `icon`, `lock`, `unsaved`, `close` jsou `fix/fix`, `label` je `hug/hug` bez zkracování (`WIDTH_AND_HEIGHT`). Součet 16 + 14 + 8 + 119 + 8 + 20 + 10 = 195 (šířka kořene). Šířka `Selected=False` je 193 (text 117 místo 119, jiný řez písma: `Inter Medium` místo `Inter Semi Bold`). Se zobrazeným zámkem a tečkou je vybraná záložka široká 231 (s křížkem) a 203 (bez křížku), nevybraná 229 a 201.
- Zaoblení jen nahoře: `rad=8,8,0,0`.
- Vrstva `progress` je umístěna absolutně (`at=0,30 abs`, `con=STRETCH/MAX`, výška 2) u spodní hrany; díky nejmenší výšce 32 leží na y = 30 i v záložce bez křížku. `value` má šířku 107 ze 195 (106 ze 193), `con=SCALE/STRETCH`. Hodnota průběhu nemá vlastnost, vlastnost `Progress` řídí jen viditelnost.
- Skryté vrstvy `lock`, `unsaved`, `progress` jsou všechny svázány s vlastností (`Locked`, `Unsaved`, `Progress`) a jsou skryté ve všech 8 variantách. Vrstva skrytá ve výchozím stavu se čte s velikostí `fix`.
- Vnořená komponenta: `close` = `Tab close button{State=Default}` (`ov:name`); komponenta z ní vystavuje jen viditelnost (`Closable`). Ikona `lock` nemá vlastnost záměny instance.
- V pruhu aplikace (`app-top-bar`, 1440×38, odsazení nahoře 6, zarovnání dolů) teď Home sedí na y = 6 s výškou 32 jako ostatní záložky; dříve byl 85×28 na y = 10.
- Žádná varianta Disabled. Žádná barva nevázaná na proměnnou.

### Tab close button · `48:110`

Sada 4 variant · výchozí varianta `State=Default` · rozměr 20×20

Popis ve Figmě: „Closes a document tab. 20x20 hit area, 12 px icon.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'State=Default' 20x20 lay=H gap0 pad0,0,0,0 CENTER/CENTER; rad=4
 I 'icon' 12x12 z=fix/fix; ic=icon/x stroke=icon/muted w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `fill=bg/hover`; `icon: ic=icon/x stroke=icon/default w2` |
| `State=Pressed` | kořen: `fill=bg/pressed`; `icon: ic=icon/x stroke=icon/strong w2` |
| `State=Focus` | kořen: `st=border/focus w2 OUTSIDE` |

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ano (ve všech variantách).
- Výchozí varianta nemá výplň ani tah.
- Focus: `border/focus`, 2 px, `OUTSIDE`; výplň se nepřidává, ikona zůstává `icon/muted`.
- Velikost: kořen 20×20 je větší než obsah (ikona 12×12 `fix/fix`, výplně 0, zarovnání `CENTER/CENTER`) - pevný rozměr.
- Ikona `icon/x` není svázána s vlastností (nelze zaměnit).
- Žádná varianta Disabled.

### Icon button · `706:147674`

Sada 10 variant · výchozí varianta `Size=24, State=Default` · rozměr 24×24

Popis ve Figmě: „Small icon-only button for secondary actions inside rows, cards and fields (copy, edit, confirm, cancel, expand, show password, save). Size=24 has a 14 px icon, Size=28 a 16 px icon; the icon is swapped by the Icon property. Default has no background and a muted icon; Hover and Pressed add a background and darken the icon; Focus is the default look with a 2 px ring outside; Disabled draws the icon in icon/disabled, without opacity.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Icon | záměna instance | **icon/more-horizontal** |
| Size | varianta | **24**, 28 |
| State | varianta | **Default**, Hover, Pressed, Focus, Disabled |

**Stavba výchozí varianty**

```text
C 'Size=24, State=Default' 24x24 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=6
 I 'icon' 14x14 z=fix/fix; ref=mainComponent:Icon; ic=icon/more-horizontal stroke=icon/muted w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Size=24, State=Hover` | kořen: `fill=bg/hover`; `icon: ic=icon/more-horizontal stroke=icon/default w2` |
| `Size=24, State=Pressed` | kořen: `fill=bg/pressed`; `icon: ic=icon/more-horizontal stroke=icon/strong w2` |
| `Size=24, State=Focus` | kořen: `st=border/focus w2 OUTSIDE` |
| `Size=24, State=Disabled` | `icon`: `stroke=icon/disabled w2` |
| `Size=28, State=Default` | kořen: `s=28x28`; `icon: s=16x16` |
| `Size=28, State=Hover` (proti `Size=28, State=Default`) | kořen: `fill=bg/hover`; `icon: ic=icon/more-horizontal stroke=icon/default w2` |
| `Size=28, State=Pressed` (proti `Size=28, State=Default`) | kořen: `fill=bg/pressed`; `icon: ic=icon/more-horizontal stroke=icon/strong w2` |
| `Size=28, State=Focus` (proti `Size=28, State=Default`) | kořen: `st=border/focus w2 OUTSIDE` |
| `Size=28, State=Disabled` (proti `Size=28, State=Default`) | `icon`: `stroke=icon/disabled w2` |

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ano (obrys má jen Focus a je `OUTSIDE`, rozložení tedy neovlivní). Platí pro všechny varianty.
- **Nová komponenta** (13. 10. 2026). Nahrazuje malá tlačítka s ikonou, která byla ve čtyřech komponentách nakreslená jako obyčejné rámce nebo holé ikony bez stavů: akce v Property row (`206:873`), přepínač rozbalení v Signature card (`192:1032`), přepínač viditelnosti v Password field (`225:870`) a šipka `save` v Attachment row (`191:1045`). Sada má 252×116.
- Vzor je Tab close button (`48:110`): Default bez výplně a bez tahu, ikona `icon/muted`; Hover `bg/hover` + `icon/default`; Pressed `bg/pressed` + `icon/strong`; Focus = Default + kroužek `border/focus` 2 px `OUTSIDE` (výplň se nepřidává, ikona zůstává `icon/muted`). Geometrie `Size=28` (28×28, `rad=6`, ikona 16) je shodná se Star button (`64:569`).
- Disabled: ikona `icon/disabled`, bez krytí (**změna 14. 10. 2026**, rozhodnutí Q-8). Dříve krytí kořene `op=0.5` při ikoně `icon/muted`: Default má ikonu `icon/muted` už v klidu, takže tlumená barva by zakázané tlačítko neodlišila; s tokenem `icon/disabled` výjimka odpadla.
- Rozměry: `Size=24` = 24×24 s ikonou 14×14, `Size=28` = 28×28 s ikonou 16×16; `rad=6` u obou. Kořen je pevný a ikona je vystředěná (`CENTER/CENTER`, okraje 0). Ikony komponent `icon/*` se zmenšují, tloušťka tahu zůstává 2 px.
- Ikona `icon` je svázaná s vlastností `Icon` (záměna instance), výchozí je `icon/more-horizontal`. Barvu ikony určuje přepsání tahu vektoru `glyph` uvnitř instance; při záměně ikony se přepsání přenáší (všechny ikony mají vektor `glyph`).
- Nadřazená komponenta může barvu ikony přepsat (např. `confirm` v Property row má `icon/accent`, tlačítka ve stavu Hover řádku `icon/default`). Takové přepsání platí ve všech stavech tlačítka – při přepnutí vnořené instance na Hover / Pressed se změní jen pozadí.
- Prototyp: varianty `State=Default` obou velikostí mají interakce `ON_HOVER` → `State=Hover` a `ON_PRESS` → `State=Pressed` (`CHANGE_TO`), stejně jako Tab close button.
- Sada sama nemá výplň (Tab close button má pod variantami `bg/titlebar`, protože leží v pruhu záložek).
- Žádná barva nevázaná na proměnnou. Bez textu.

### New tab button · `48:184`

Sada 4 variant · výchozí varianta `State=Default` · rozměr 28×28

Popis ve Figmě: „Opens a new tab with the Home screen.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'State=Default' 28x28 lay=H gap0 pad8,8,8,8 MIN/MIN; fill=bg/overlay; rad=4
 I 'icon' 12x12 z=fix/fix; ic=icon/plus stroke=icon/default w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `fill=bg/hover` |
| `State=Pressed` | kořen: `fill=bg/pressed`; `icon: ic=icon/plus stroke=icon/strong w2` |
| `State=Focus` | kořen: `sil=0; st=border/focus w2 OUTSIDE` |

**Poznámky**

- Kořen: velikost `hug/hug`, ořez obsahu ne, obrys se počítá do rozložení ne (ve všech variantách).
- Výchozí varianta má výplň `bg/overlay` (na rozdíl od `Tab close button`, který je ve výchozím stavu bez výplně).
- Focus: `border/focus`, 2 px, `OUTSIDE`; výplň `bg/overlay` zůstává.
- Stav Hover nemění barvu ikony (zůstává `icon/default`), Pressed ji mění na `icon/strong`.
- Velikost: 8 + 12 + 8 = 28 v obou osách, ikona `fix/fix`; zarovnání `MIN/MIN`.
- Ikona `icon/plus` není svázána s vlastností.
- Žádná varianta Disabled.

### Window button · `947:1475`

Sada 17 variant · výchozí varianta `Kind=Minimize, State=Default` · rozměr 46×38 · doplněno do Figmy 18. 10. 2026 (komponenty 93 a 94, [newapplication.md](newapplication.md) 6)

Popis ve Figmě: „Window button of the frameless application window, at the right end of app-top-bar (the tab strip is the title bar). 46×38, glyph 16 px centered, no radius. Default has no background (icon/default glyph); Hover bg/hover, Pressed bg/pressed with an icon/strong glyph. Close: Hover bg/danger, Pressed bg/danger-pressed, glyph icon/on-accent (the Windows caption colors #C42B1C / #B22A1E in both modes). Inactive: the window is not active, no background, glyph icon/disabled (hover and press work as in Default). Kind=Maximize, State=Disabled: the window cannot be resized (fixed size), glyph icon/disabled, no hover, no press. Kind Maximize is shown for a normal window, Restore for a maximized one. Windows 11: the Maximize/Restore button must be the HTMAXBUTTON hit-test target (WM_NCHITTEST of the frameless window returns HTMAXBUTTON over it), otherwise the Snap Layouts flyout does not appear on hover. Use Window controls for the group.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Kind | varianta | **Minimize**, Maximize, Restore, Close |
| State | varianta | **Default**, Hover, Pressed, Inactive, Disabled (jen `Kind=Maximize`) |

**Stavba výchozí varianty**

```text
C 'Kind=Minimize, State=Default' 46x38 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix
 I 'icon' 16x16 z=fix/fix; ic=icon/window-minimize stroke=icon/default w2
```

**Varianty a stavy**

| Varianta | Uzel | Změna |
| --- | --- | --- |
| `Kind=Minimize, State=Hover` | `947:1442` | kořen: `fill=bg/hover`; `icon: stroke=icon/strong` |
| `Kind=Minimize, State=Pressed` | `947:1445` | kořen: `fill=bg/pressed`; `icon: stroke=icon/strong` |
| `Kind=Maximize, State=Default` | `947:1448` | `icon: ic=icon/window-maximize stroke=icon/default w2` |
| `Kind=Maximize, State=Hover` / `Pressed` | `947:1451` / `947:1454` | jako Minimize |
| `Kind=Restore, State=Default` | `947:1457` | `icon: ic=icon/window-restore stroke=icon/default w2` |
| `Kind=Restore, State=Hover` / `Pressed` | `947:1460` / `947:1463` | jako Minimize |
| `Kind=Close, State=Default` | `947:1466` | `icon: ic=icon/window-close stroke=icon/default w2` |
| `Kind=Close, State=Hover` | `947:1469` | kořen: `fill=bg/danger`; `icon: stroke=icon/on-accent` |
| `Kind=Close, State=Pressed` | `947:1472` | kořen: `fill=bg/danger-pressed`; `icon: stroke=icon/on-accent` |
| `Kind=Minimize / Maximize / Restore / Close, State=Inactive` | `955:142595` / `955:142598` / `955:142601` / `955:142604` | `icon: stroke=icon/disabled` |
| `Kind=Maximize, State=Disabled` | `955:142607` | `icon: stroke=icon/disabled` |

**Poznámky**

- Kořen: velikost `fix/fix`, bez ořezu, bez obrysu, bez zaoblení; výplň stavu vyplňuje celé tlačítko (dotýká se okraje okna).
- Ikony `icon/window-minimize` (`947:1383`), `icon/window-maximize` (`947:1388`), `icon/window-restore` (`947:1394`), `icon/window-close` (`947:1400`) jsou v rámci `Icons` a v [resources/icons](../Pdf4QtLibFluent/resources/icons/) (stejně jako ostatní ikony 24×24, tah 2 px v každé velikosti).
- Tokeny `bg/danger` (`#c42b1c`) a `bg/danger-pressed` (`#b22a1e`) jsou v obou režimech stejné (barvy titulku Windows).
- Stav Focus komponenta nemá: tlačítka okna nejsou v řetězci fokusu (okno se ovládá klávesnicí přes systémovou nabídku).
- V knihovně: `PDFWindowButton`, kreslí `PDFComponentPainter::paintWindowButton`.

### Window controls · `947:1504`

Sada 6 variant · výchozí varianta `Platform=Windows, Maximized=False` (`947:1484`) · rozměr 138×38 (macOS 80×38)

Popis ve Figmě (zkráceně): Windows – Minimize, Maximize (Restore při `Maximized=True`) a Close, 138×38, absolutně u pravého okraje `app-top-bar` (x = šířka − 138, y = 0, vazby Right/Top); lišta drží vpravo odsazení 200 (16 + 138 + 46 px volných pro přesun okna), aby taby nikdy nevjely pod tlačítka. Linux – stejná tlačítka, glyfy i umístění jako Windows (kreslí je aplikace, téma prostředí se nesleduje). macOS – nativní „semafory“ close / minimize / zoom (kruhy 12 px, mezery 8 px, 80×38 včetně okrajů 14 px) na LEVÉM konci `app-top-bar`, lišta tabů začíná za nimi (levé odsazení asi 80), vpravo žádná tlačítka okna; kruhy zastupují nativní tlačítka `NSWindow` (Qt je ponechá, když obsah zasahuje do titulku), jejich glyfy při najetí kreslí systém; barvy `window/macos-close` (`#ff5f57`), `window/macos-minimize` (`#febc2e`), `window/macos-zoom` (`#28c840`) v obou tématech, `window/macos-inactive` pro všechny tři kruhy neaktivního okna. `Maximized=True` na macOS vypadá stejně, v celé obrazovce je systém skryje. `Maximized=True` na všech platformách: taby se dotýkají horního okraje obrazovky (žádná mezera 6 px nahoře), tab jde trefit hozením ukazatele k okraji.

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Platform | varianta | **Windows**, Linux, macOS |
| Maximized | varianta | **False**, True |

**Stavba výchozí varianty**

```text
C 'Platform=Windows, Maximized=False' 138x38 lay=H gap0 pad0,0,0,0 MIN/MIN; z=hug/hug
 I 'minimize' 46x38 inst=Window button{Kind=Minimize, State=Default} ov:name
 I 'maximize' 46x38 inst=Window button{Kind=Maximize, State=Default} ov:name
 I 'close' 46x38 inst=Window button{Kind=Close, State=Default} ov:name
```

**Varianty**

| Varianta | Uzel | Změna |
| --- | --- | --- |
| `Platform=Windows, Maximized=True` | `947:1494` | `+ restore` (Window button `Kind=Restore`), `- maximize` |
| `Platform=Linux, Maximized=False` | `955:142610` | beze změny |
| `Platform=Linux, Maximized=True` | `955:142614` | jako Windows `Maximized=True` |
| `Platform=macOS, Maximized=False` / `True` | `955:142618` / `955:142631` | kořen: `s=80x38; lay=H gap8 pad0,14,0,14 MIN/CENTER; z=fix/fix`; `close`, `minimize`, `zoom` jsou elipsy 12×12 s výplní `window/macos-close`, `window/macos-minimize`, `window/macos-zoom` |

**Poznámky**

- Na obrazovkách je ve všech 572 lištách instance `window-controls` (`Platform=Windows, Maximized=False`), absolutně u pravého okraje lišty `app-top-bar` (1440×38, `lay=H gap8 pad6,200,0,16 MIN/MAX`, výplň `bg/titlebar`), např. `950:9055` v `pdf4qt-home` 28:4 a `950:19729` v `pdf4qt-document` 193:756.
- Stav Inactive a zakázané Maximize jsou varianty Window button; neaktivní okno na macOS má všechny tři kruhy `window/macos-inactive`.
- V knihovně: `PDFWindowControls`, kreslí `PDFComponentPainter::paintWindowControls`.

### Nav item · `47:123`

Sada 8 variant · výchozí varianta `Selected=False, State=Default` · rozměr 190×38

Popis ve Figmě: „Sidebar navigation item of the Home screen. Selected = current page. The selected item does not react to the pointer: Selected=True with Hover or Pressed looks like Selected=True, State=Default, and a click on it does nothing.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **Open** |
| Icon | záměna instance | **icon/folder** |
| Selected | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Selected=False, State=Default' 190x38 lay=H gap10 pad9,12,9,12 MIN/CENTER; rad=10
 F 'icon-wrap' 20x20 lay=V gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix
  I 'folder' 20x20 z=fix/fix; ref=mainComponent:Icon; ic=icon/folder stroke=icon/default w2
 T 'label' 33x16 z=hug/hug; fill=text/secondary; f=Inter Regular 13/auto LT WIDTH_AND_HEIGHT; tx='Open'; ref=characters:Label
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=False, State=Hover` | kořen: `fill=bg/hover` |
| `Selected=False, State=Pressed` | kořen: `fill=bg/pressed` |
| `Selected=False, State=Focus` | kořen: `sil=0; st=border/focus w2 INSIDE` |
| `Selected=True, State=Default` | kořen: `fill=bg/accent-subtle`; `icon-wrap>folder: ic=icon/folder stroke=icon/accent w2`; `label: s=34x16; fill=text/accent; f=Inter Semi Bold 13/auto LT WIDTH_AND_HEIGHT` |
| `Selected=True, State=Hover` | (proti `Selected=True, State=Default`) beze změny |
| `Selected=True, State=Pressed` | (proti `Selected=True, State=Default`) beze změny |
| `Selected=True, State=Focus` | (proti `Selected=True, State=Default`) kořen: `sil=0; st=border/focus w2 INSIDE` |

**Poznámky**

- Kořen: velikost `fix/hug`, ořez obsahu ne, obrys se počítá do rozložení ne (ve všech variantách).
- Výchozí varianta nemá výplň ani tah.
- Focus: `border/focus`, 2 px, `INSIDE` (**změna 14. 10. 2026**, rozhodnutí Q-2: řádky seznamů a nabídek mají kroužek uvnitř, dříve `OUTSIDE`); výplň se nemění (`Selected=False` bez výplně, `Selected=True` drží `bg/accent-subtle`).
- Velikost: kořen 190×38 je širší než obsah (12 + 20 + 10 + 33 + 12 = 87), zarovnání `MIN/CENTER` - šířka není dána obsahem; výška 9 + 20 + 9 = 38. `label` je `hug/hug` bez zkracování (`WIDTH_AND_HEIGHT`). Kořen je `fix/hug`: šířka pevná, výška podle obsahu.
- **Změna 14. 10. 2026 (rozhodnutí Q-5):** vybraná položka na myš nereaguje – `Selected=True` v Hover a Pressed má výplň `bg/accent-subtle` jako v Default (dříve `bg/accent-subtle-hover` a `-pressed`), stejně jako Settings nav item, Tab a Theme toggle segment. Varianty zůstaly kvůli úplné mřížce; žádná instance je nepoužívá.
- Vrstva ikony se jmenuje `folder` (jméno výchozí ikony), ne `icon`; je uvnitř rámu `icon-wrap` 20×20.
- `Selected=True` mění řez písma (`Inter Regular` → `Inter Semi Bold`), šířka textu 33 → 34, šířka kořene se nemění.
- Žádná varianta Disabled.

### Segment · `47:157`

Sada 10 variant (do 16. 10. 2026 Theme toggle segment) · výchozí varianta `Selected=True, State=Default` · rozměr 91×32

Popis ve Figmě: „One segment of a segmented control: a single choice of a few options on the bg/track container (r16, no padding). Used by the Light / Dark / Auto switch in the sidebar (three segments of equal width, icon/monitor for Auto; Auto follows the color scheme of the system and is the same setting as Settings › General › Color scheme: Automatic) and, without the icon (Show icon off), by the single-choice options of the tools: Compress presets, page range All / Current / Selected / Range, alignment, Fit / 100 %, Thumbnails / Details, Marks / Preview result, OCR profile. All segments of one control have the same width: the widest content + 2 × 14, a control of a given width (the sidebar switch, 182) divides it equally and keeps at least 6 on each side. The selected segment is a raised bg/surface plate (radius 16, as the track) and does not react to the pointer. The component draws the icon in icon/default (selected) and icon/muted (not selected); the sun of the Light segment is overridden to the icon/sun color on the instance, in every state.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **Light** |
| Icon | záměna instance | **icon/sun** |
| Show icon | přepínač | **true** |
| Selected | varianta | **True**, False |
| State | varianta | **Default**, Hover, Pressed, Focus, Disabled |

**Stavba výchozí varianty**

```text
C 'Selected=True, State=Default' 91x32 lay=H gap6 pad6,6,6,6 CENTER/CENTER; fill=bg/surface; rad=16; fx=DROP_SHADOW(0,1,3,0 effect/shadow)
 I 'icon' 14x14 z=fix/fix; ref=visible:Show icon,mainComponent:Icon; ic=icon/sun stroke=icon/default w2
 T 'label' 27x13 z=hug/hug; fill=text/secondary; f=Inter Semi Bold 11/auto LT WIDTH_AND_HEIGHT; tx='Light'; ref=characters:Label
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=True, State=Hover` | beze změny (`same`) |
| `Selected=True, State=Pressed` | beze změny (`same`) |
| `Selected=True, State=Focus` | kořen: `sil=0; st=border/focus w2 OUTSIDE` |
| `Selected=False, State=Default` | kořen: `fill=-; fx=-`; `icon: ic=icon/sun stroke=icon/muted w2`; `label: fill=text/muted; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT` |
| `Selected=False, State=Hover` | (proti `Selected=False, State=Default`) kořen: `fill=bg/hover` |
| `Selected=False, State=Pressed` | (proti `Selected=False, State=Default`) kořen: `fill=bg/pressed` |
| `Selected=False, State=Focus` | (proti `Selected=False, State=Default`) kořen: `sil=0; st=border/focus w2 OUTSIDE` |
| `Selected=True, State=Disabled` | `icon: ic=icon/sun stroke=icon/disabled w2`; `label: fill=text/disabled` |
| `Selected=False, State=Disabled` | (proti `Selected=False, State=Default`) `icon: ic=icon/sun stroke=icon/disabled w2`; `label: fill=text/disabled` |

- **Změna 15. 10. 2026 (zpětná vazba):** přepínač v postranním panelu má tři segmenty Light / Dark / Auto (Auto s `icon/monitor`), protože nastavení *Color scheme* má i hodnotu *Automatic*. Vodorovné okraje všech variant jsou 6 místo 14, aby se do pruhu 182 px vešly tři segmenty po 61 px (ikona 14 + mezera 6 + popisek nejvýš 27). Na obrazovkách je vybraný Auto.

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ne (ve všech variantách).
- `Selected=True, State=Hover` a `Selected=True, State=Pressed` jsou shodné s výchozí variantou - vybraný segment nemá kreslený stav Hover ani Pressed.
- Ikona má v komponentě `stroke=icon/default` (`Selected=True`) a `stroke=icon/muted` (`Selected=False`); token `icon/sun` komponenta nepoužívá, protože jedna sada slouží oběma segmentům (ikona je záměna instance). **Nový popis (13. 10. 2026)** to říká výslovně: slunce segmentu Light se přebarvuje na `icon/sun` přepsáním tahu na instanci, ve všech stavech; dřívější popis („The Light icon keeps the icon/sun color“) kresbě komponenty odporoval. Ikona `icon/sun` má jediný vektor.
- Focus: `border/focus`, 2 px, `OUTSIDE`; výplň se nemění (`Selected=True` drží `bg/surface` i stín, `Selected=False` zůstává bez výplně).
- Stín `DROP_SHADOW(0,1,3,0 effect/shadow)` má jen `Selected=True`.
- Velikost: kořen 91×32 je větší než obsah (14 + 14 + 6 + 27 + 14 = 75 na šířku, 6 + 14 + 6 = 26 na výšku), zarovnání `CENTER/CENTER` - rozměr není dán obsahem. `label` je `hug/hug` bez zkracování. Kořen je `fix/fix`: šířka i výška jsou pevné.
- `rad=16` = polovina výšky (do 16. 10. 2026 `rad=14`, s dráhou bez okraje vyčnívaly rohy vybrané destičky z rohů dráhy).
- `Selected=False` mění řez písma (`Inter Semi Bold` → `Inter Medium`), rozměr textu se nemění (27×13).
- **Změna 16. 10. 2026 (Q-9, Q-16):** komponenta se jmenuje Segment a slouží každé volbě jedné z několika možností, ne jen přepínači motivu. Přibyla vlastnost `Show icon` (vázaná na viditelnost vrstvy `icon`, výchozí `true`, segmenty voleb toolů ji mají vypnutou) a `State=Disabled` pro obě hodnoty `Selected` podle pravidla O-08: výplň a stín beze změny, text `text/disabled`, ikona `icon/disabled`, bez krytí (výběr stránek a předvolby Compress během běhu toolu).
- **Šířka segmentů (16. 10. 2026):** všechny segmenty jednoho přepínače jsou stejně široké – nejširší obsah (popisek Semi Bold 11, s ikonou + 20) + 2 × 14; přepínač s danou šířkou (motiv v postranním panelu, 182) ji dělí rovným dílem a obsah centruje, na každé straně zůstává nejméně 6. Na obrazovkách mají segmenty pevnou šířku podle tohoto pravidla (instance `fix/fix`).
- **Dráha (Q-16):** kontejner `bg/track` s `rad=16` bez vnitřního okraje, výška 32 = výška segmentu (jako přepínač motivu na *Main page*, 182×32). Ukázka na stránce *Widgets* (`620:1544`) měla do 16. 10. 2026 okraj 2 a rozměr 186×36.
- Použití od 16. 10. 2026 (Q-9): přepínač motivu (20× *Main page*, 7× *Tools Overview*), Stroke / Fill nástroje tvarů, výběr stránek `Page selection` (režimy All / Current / Selected / Range, 151 instancí, segmenty dělí šířku komponenty rovným dílem), zarovnání v Sign by Hand, Marks / Preview result v Redact, Fit / 100 % a Thumbnails / Details v lištách náhledu, předvolby Compress, profil OCR Fast / Standard / Quality.

### Search field · `49:143`

Sada 8 variant · výchozí varianta `State=Default, Filled=False` · rozměr 320×36

Popis ve Figmě: „Global search of tools, documents and help. Text input: no pressed state; Focus = keyboard or mouse focus. State=Disabled is the unavailable field. Filled=True shows the typed text (property Value, text/primary) and the clear button (icon/x-circle 16 px, icon/muted) at the right.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Placeholder | text | **Search tools, documents, or help...** |
| Value | text | **merge pdf** |
| State | varianta | **Default**, Hover, Focus, Disabled |
| Filled | varianta | **False**, True |

**Stavba výchozí varianty**

```text
C 'State=Default, Filled=False' 320x36 lay=H gap10 pad10,16,10,16 MIN/CENTER; z=fix/hug; sil=0; fill=bg/surface; st=border/default w1 INSIDE; rad=100
 I 'icon' 16x16 z=fix/fix; ic=icon/search stroke=icon/muted w2
 T 'placeholder' 262x16 z=fill/hug; fill=text/muted; f=Inter Regular 13/auto LT HEIGHT; tx='Search tools, documents, or help...'; ref=characters:Placeholder
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover, Filled=False` | kořen: `st=border/strong w1 INSIDE` |
| `State=Focus, Filled=False` | kořen: `st=border/focus w2 INSIDE`; `icon: ic=icon/search stroke=icon/accent w2` |
| `State=Disabled, Filled=False` | kořen: `fill=bg/muted`; `icon: ic=icon/search stroke=icon/disabled w2`; `placeholder: fill=text/disabled` |
| `State=Default, Filled=True` | `+ value: T 236x16 z=fill/hug; fill=text/primary; f=Inter Regular 13/auto LT HEIGHT; tx='merge pdf'; ref=characters:Value`<br>`+ clear: I 16x16 z=fix/fix; ic=icon/x-circle stroke=icon/muted w2`<br>`- placeholder` |
| `State=Hover, Filled=True` (proti `State=Default, Filled=True`) | kořen: `st=border/strong w1 INSIDE` |
| `State=Focus, Filled=True` (proti `State=Default, Filled=True`) | kořen: `st=border/focus w2 INSIDE`; `icon: ic=icon/search stroke=icon/accent w2` |
| `State=Disabled, Filled=True` (proti `State=Default, Filled=True`) | kořen: `fill=bg/muted`; `icon: ic=icon/search stroke=icon/disabled w2`; `value: fill=text/disabled`; `clear: ic=icon/x-circle stroke=icon/disabled w2` |

**Poznámky**

- Kořen: velikost `fix/hug`, ořez obsahu ne, obrys se počítá do rozložení ne (ve všech variantách). Všech osm variant má 320×36, ikona je na 16,10 a text na 42,10.
- **Nově vlastnost `Filled`** (`False` = prázdné pole se zástupným textem, `True` = pole s napsaným textem). Dosavadní tři varianty dostaly hodnotu `Filled=False`; identifikátory `49:133`, `49:135`, `49:139` se nezměnily.
- **Nově `Filled=True`** (`706:146720` Default, `706:146726` Hover, `706:146732` Focus): místo vrstvy `placeholder` je vrstva `value` (`text/primary`, vázaná na novou textovou vlastnost `Value`) a za ní tlačítko smazání `clear` – instance `icon/x-circle` 16×16 s obrysem `icon/muted w2`. Ikona je `icon/x-circle`, jak ji kreslí *Widgets* v sekci 04 (blok „New – placeholder, cursor, selection, clear button“, `SP_LineEditClearButton`), ne `icon/x`. Tlačítko smazání nemá v komponentě vlastní stav; *Widgets* uvádí pro najetí `icon/default`.
- Šířka: 16 + ikona 16 + mezera 10 + `value` 236 + mezera 10 + `clear` 16 + 16 = 320. Mezera mezi textem a tlačítkem smazání je mezera rozložení sady (10); *Widgets* uvádí u obecného řádkového pole 8.
- **Nově `State=Disabled`** (`706:146716`, jen pro `Filled=False`): výplň `bg/muted`; obrys `border/default w1 INSIDE` se nemění; ikona je `icon/disabled` a zástupný text `text/disabled` (**změna 14. 10. 2026**, rozhodnutí Q-8; dříve zůstávaly `icon/muted` a `text/muted` jako v Default). Od 14. 10. 2026 (N-14) má variantu i kombinace `Filled=True` + `Disabled` (`773:147222`): výplň `bg/muted`, ikona hledání i tlačítko smazání `icon/disabled`, text `text/disabled`; tlačítko smazání zůstává na místě, aby se text neposunul.
- Focus: `border/focus`, 2 px, `INSIDE` (nahrazuje `border/default w1 INSIDE`); výplň `bg/surface` zůstává; ikona hledání přechází na `icon/accent` (v obou hodnotách `Filled`). Ostatní pole ikonu při fokusu nepřebarvují; *Widgets* (sekce 04, „Search field / Focus (pill, icon 16)“) kreslí pole s ikonou `icon/accent`, proto zůstalo.
- Sada nemá stav Pressed (popis: „no pressed state“).
- Velikost: `placeholder` i `value` jsou `fill/hug` – pole se roztahuje na šířku; 16 + 16 + 10 + 262 + 16 = 320, 10 + 16 + 10 = 36.
- Texty `placeholder` a `value` mají `HEIGHT` bez zkracování (žádné `trunc`).
- `rad=100` (plně zaoblené konce při výšce 36).
- Ikony `icon/search` a `icon/x-circle` nejsou svázány s vlastností.
- Sada není auto layout: varianty `Filled=False` jsou v řádku y = 24 (x = 24, 368, 712, 1056), varianty `Filled=True` v řádku y = 84 (x = 24, 368, 712, 1056); sada vyrostla z 1056×84 na 1400×144.

### Text link · `49:152`

Sada 4 variant · výchozí varianta `State=Default` · rozměr 53×16

Popis ve Figmě: „Inline link ("View all", "MIT License").“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **View all** |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'State=Default' 53x16 lay=H gap0 pad0,2,0,2 MIN/MIN; z=hug/hug; rad=4
 T 'label' 49x16 z=hug/hug; fill=text/accent; f=Inter Semi Bold 13/auto LT WIDTH_AND_HEIGHT; tx='View all'; ref=characters:Label
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | `label: f=Inter Semi Bold 13/auto LT WIDTH_AND_HEIGHT UNDERLINE` |
| `State=Pressed` | `label: fill=text/accent-pressed; f=Inter Semi Bold 13/auto LT WIDTH_AND_HEIGHT UNDERLINE` |
| `State=Focus` | kořen: `st=border/focus w2 OUTSIDE` |

**Poznámky**

- Kořen: velikost `hug/hug`, ořez obsahu ne, obrys se počítá do rozložení ano (ve všech variantách).
- Popis je uveden tak, jak ho vrací `get_design_context` - se skutečnými uvozovkami. Vlastnost `description` plugin API vrací tentýž text s entitami `&quot;` místo uvozovek; entity jsou podoba tohoto čtení, ne znění popisu.
- Kořen nemá výplň v žádné variantě.
- Focus: `border/focus`, 2 px, `OUTSIDE`, `rad=4`; text není podtržen a barva se nemění (`text/accent`).
- Velikost: 2 + 49 + 2 = 53, `label` je `hug/hug` bez zkracování - komponenta je dána textem.
- Žádná varianta Disabled ani navštívený odkaz.

### Link row · `49:190`

Sada 4 variant · výchozí varianta `State=Default` · rozměr 328×50

Popis ve Figmě: „Row inside a list card (Support section). Icon color is overridden per item.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Title | text | **Report Issue** |
| Subtitle | text | **Tell us about a problem** |
| Icon | záměna instance | **icon/alert-circle** |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'State=Default' 328x50 lay=H gap0 pad10,10,10,10 SPACE_BETWEEN/CENTER; rad=8
 F 'content' 150x30 lay=H gap12 pad0,0,0,0 MIN/CENTER; z=hug/hug
  I 'icon' 16x16 z=fix/fix; ref=mainComponent:Icon; ic=icon/alert-circle stroke=icon/default w2
  F 'text' 122x30 lay=V gap1 pad0,0,0,0 MIN/MIN; z=hug/hug
   T 'title' 80x16 z=hug/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT WIDTH_AND_HEIGHT; tx='Report Issue'; ref=characters:Title
   T 'subtitle' 122x13 z=hug/hug; fill=text/tertiary; f=Inter Regular 11/auto LT WIDTH_AND_HEIGHT; tx='Tell us about a problem'; ref=characters:Subtitle
 I 'chevron' 14x14 z=fix/fix; ic=icon/chevron-right stroke=icon/muted w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `fill=bg/hover`; `chevron: ic=icon/chevron-right stroke=icon/default w2` |
| `State=Pressed` | kořen: `fill=bg/pressed`; `chevron: ic=icon/chevron-right stroke=icon/default w2` |
| `State=Focus` | kořen: `sil=0; st=border/focus w2 INSIDE` |

**Poznámky**

- Kořen: velikost `fix/hug`, ořez obsahu ne, obrys se počítá do rozložení ne (ve všech variantách).
- Výchozí varianta nemá výplň ani tah.
- Focus: `border/focus`, 2 px, `INSIDE` (**změna 14. 10. 2026**, rozhodnutí Q-2: řádky seznamů a nabídek mají kroužek uvnitř, dříve `OUTSIDE`); výplň se nepřidává, `chevron` zůstává `icon/muted`.
- Velikost: kořen 328×50 je širší než obsah, rozložení `SPACE_BETWEEN` (obsah vlevo, `chevron` vpravo); výška 10 + 30 + 10 = 50. `content`, `text`, `title`, `subtitle` jsou `hug/hug` s `WIDTH_AND_HEIGHT` - texty se nezkracují ani nezalamují. Kořen je `fix/hug`: šířka pevná, výška podle obsahu.
- Ikona má ve výchozí variantě `stroke=icon/default`; popis říká, že se barva ikony přepisuje u jednotlivých položek (v komponentě na to není vlastnost).
- `chevron` (`icon/chevron-right`) není svázán s vlastností.
- Žádná varianta Disabled.

### Button · `63:516`

Sada 10 variant · výchozí varianta `Variant=Primary, State=Default` · rozměr 148×36

Popis ve Figmě: „Push button. Primary = the one main action of a view (blue); Secondary = everything else. Icon is optional (Show icon).“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **Browse Files…** |
| Icon | záměna instance | **icon/folder-open** |
| Show icon | přepínač | **true** |
| Variant | varianta | **Primary**, Secondary |
| State | varianta | **Default**, Hover, Pressed, Focus, Disabled |

**Stavba výchozí varianty**

```text
C 'Variant=Primary, State=Default' 148x36 lay=H gap8 pad0,18,0,16 CENTER/CENTER; z=hug/fix; fill=bg/accent; rad=8
 I 'icon' 16x16 z=fix/fix; ref=visible:Show icon,mainComponent:Icon; ic=icon/folder-open stroke=icon/on-accent w2
 T 'label' 90x16 z=hug/hug; fill=icon/on-accent; f=Inter Semi Bold 13/auto LT WIDTH_AND_HEIGHT; tx='Browse Files…'; ref=characters:Label
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Variant=Primary, State=Hover` | kořen: `fill=bg/accent-hover` |
| `Variant=Primary, State=Pressed` | kořen: `fill=bg/accent-pressed` |
| `Variant=Primary, State=Focus` | kořen: `st=border/focus w2 OUTSIDE` |
| `Variant=Primary, State=Disabled` | kořen: `fill=bg/muted`; `icon: ic=icon/folder-open stroke=icon/disabled w2`; `label: fill=text/disabled` |
| `Variant=Secondary, State=Default` | kořen: `s=150x36; fill=bg/surface; st=border/default w1 INSIDE`; `icon: ic=icon/folder-open stroke=icon/default w2`; `label: fill=text/secondary` |
| `Variant=Secondary, State=Hover` | (proti `Variant=Secondary, State=Default`) kořen: `fill=bg/surface+bg/hover` |
| `Variant=Secondary, State=Pressed` | (proti `Variant=Secondary, State=Default`) kořen: `fill=bg/surface+bg/pressed; st=border/strong w1 INSIDE` |
| `Variant=Secondary, State=Focus` | (proti `Variant=Secondary, State=Default`) kořen: `lay=H gap8 pad0,19,0,17 CENTER/CENTER; st=border/focus w2 OUTSIDE` |
| `Variant=Secondary, State=Disabled` | (proti `Variant=Secondary, State=Default`) `icon: ic=icon/folder-open stroke=icon/disabled w2`; `label: fill=text/disabled` |

**Poznámky**

- Kořen: velikost `hug/fix`, ořez obsahu ne, obrys se počítá do rozložení ano (ve všech variantách).
- Text `label` výchozí varianty má výplň `icon/on-accent` (token skupiny `icon/`, ne `text/`).
- Nové (oprava F-60): `Variant=Primary` má ve stavu Hover výplň `bg/accent-hover` a ve stavu Pressed `bg/accent-pressed` (dříve `accent/blue/icon` a `text/accent-pressed`, tokeny jiné role, v tmavém režimu světle modré). Hodnoty ve světlém režimu jsou stejné jako dřív (`#1d4ed8`, `#0f4796`), v tmavém režimu `#2563eb` a `#1d4ed8`. Popisek zůstává `icon/on-accent`.
- Focus: `border/focus`, 2 px, `OUTSIDE`. Primary drží výplň `bg/accent`. Secondary drží výplň `bg/surface`, tah `border/default w1 INSIDE` je nahrazen tahem fokusu.
- Nové (oprava F-25): všechny stavy téže hodnoty `Variant` mají stejný rozměr i polohu potomků – Primary 148×36, Secondary 150×36 (dříve měl `Variant=Secondary, State=Focus` šířku 148). Tah 1 px `INSIDE` se počítá do rozložení (`strokesIncludedInLayout` je zapnuto), tah `OUTSIDE` stavu Focus ne; varianta `Variant=Secondary, State=Focus` to vyrovnává vodorovnými výplněmi o 1 px většími (vpravo 19, vlevo 17 místo 18 a 16). Ověřeno i se skrytou ikonou (126×36 v obou stavech).
- Rozdíl šířky mezi Primary (148) a Secondary (150) při stejném obsahu zůstává: je to tah 1 px po obou stranách (16 + 16 + 8 + 90 + 18 = 148, s tahem 150).
- `Variant=Secondary, State=Disabled` nemění výplň ani tah kořene (zůstává `bg/surface` a `border/default`), mění jen ikonu a text na `icon/disabled` a `text/disabled` (do 14. 10. 2026 `icon/muted` a `text/muted`). `Variant=Primary, State=Disabled` mění i výplň (`bg/muted`).
- `Variant=Secondary, State=Hover` nemění tah, `State=Pressed` ho mění na `border/strong`. **Změna 16. 10. 2026 (N-23, rozhodnutí 21):** Hover a Pressed kladou poloprůhledné `bg/hover` / `bg/pressed` jako druhou výplň nad `bg/surface`, ne místo ní, takže prvek má vlastní plochu i na `bg/sidebar` a `bg/subtle` (vzor Tool choice).
- Velikost: `label` je `hug/hug` bez zkracování - šířka je dána obsahem. Výška 36 při svislých výplních 0 a obsahu vysokém 16 - výška je pevná (kořen `hug/fix`). Vodorovné výplně jsou nesouměrné: vpravo 18, vlevo 16.
- Vrstva `icon` je svázána s dvěma vlastnostmi: `visible:Show icon` a `mainComponent:Icon`.
- Stav Disabled existuje pro obě hodnoty `Variant`; mřížka 2 × 5 je úplná.

### Quick action tile · `46:431`

Sada 28 variant · výchozí varianta `Hue=Blue, State=Default` · rozměr 224×88

Popis ve Figmě: „Home screen tool tile. Hue = category of the tool, the same color as its Tool icon badge and Tool category header in the Tools catalog: Amber = Organize Pages (merge, manage pages), Blue = Create &amp; Convert (open, images, OCR), Green = Edit (edit content, page numbers), Red = Protect &amp; Sign (password, signature, hidden data), Violet = Optimize (compress), Teal = Review &amp; Inspect (measure, compare), Neutral = catalog. Fixed height 88 px; description may wrap to two lines.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Title | text | **Open PDF** |
| Description | text | **Browse and open a document** |
| Icon | záměna instance | **icon/folder-open** |
| Show star | přepínač | **false** |
| Hue | varianta | **Blue**, Violet, Amber, Red, Teal, Neutral, Green |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Hue=Blue, State=Default' 224x88 lay=H gap12 pad16,16,16,16 MIN/CENTER; z=fix/fix; sil=0; fill=accent/blue/bg; st=accent/blue/border w1 INSIDE; rad=12
 F 'icon-circle' 40x40 lay=V gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=bg/surface; rad=20
  I 'folder-open' 20x20 z=fix/fix; ref=mainComponent:Icon; ic=icon/folder-open stroke=accent/blue/icon w2
 F 'text' 140x51 lay=V gap2 pad0,0,0,0 MIN/MIN; z=fill/hug
  T 'title' 140x17 z=fill/hug; fill=text/primary; f=Inter Bold 14/auto LT HEIGHT; tx='Open PDF'; ref=characters:Title
  T 'description' 140x32 z=fill/hug; fill=text/tertiary; f=Inter Regular 12/130% LT HEIGHT; tx='Browse and open a document'; ref=characters:Description
 I 'star' 28x28 hid=1; lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=6; ref=visible:Show star; inst=Star button{Pinned=False, State=Default} ov:name; exp=1
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Hue=<Hue>, State=Default` (Hue = Violet, Amber, Red, Teal, Neutral, Green; hue = violet, amber, red, teal, neutral, green) | kořen: `fill=accent/<hue>/bg; st=accent/<hue>/border w1 INSIDE`; `icon-circle>folder-open: ic=icon/folder-open stroke=accent/<hue>/icon w2` |
| `Hue=<Hue>, State=Hover` (všech 7 odstínů včetně Blue) | (proti `Hue=<Hue>, State=Default`) kořen: `fill=accent/<hue>/bg-hover; st=accent/<hue>/border-strong w1 INSIDE` |
| `Hue=<Hue>, State=Pressed` (všech 7 odstínů včetně Blue) | (proti `Hue=<Hue>, State=Default`) kořen: `fill=accent/<hue>/bg-pressed; st=accent/<hue>/border-strong w1 INSIDE` |
| `Hue=<Hue>, State=Focus` (všech 7 odstínů včetně Blue) | (proti `Hue=<Hue>, State=Default`) kořen: `st=border/focus w2 OUTSIDE` |

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ne (ve všech variantách; `sil=0`).
- Vzor byl ověřen u všech 28 variant (7 odstínů × 4 stavy); žádná výjimka. Dlaždice je vzorem receptu barevných karet: stejně se nyní kreslí `Folder card` a `Resource card`.
- **Změna 14. 10. 2026 (rozhodnutí N-11):** odstín je barva kategorie toolu podle karty `pdf4qt-tools` na *Tools Overview* (část All tools): Organize Pages Amber, Create & Convert Blue, Edit Green, Protect & Sign Red, Optimize Violet, Review & Inspect Teal. Popis to tak říká (dříve Violet = content, Green = optimize a komprese u Amber i Green). Dlaždice „Edit Content“ (Home) je zelená, dlaždice komprese („Compress“ na Home, „Compress PDF“ v kopii katalogu na *Main page*) fialové. Kresba variant se nezměnila.
- Popis je uveden tak, jak ho vrací vlastnost `description` plugin API, včetně entity `&amp;` (znak & v názvech kategorií).
- Focus: `border/focus`, 2 px, `OUTSIDE`; výplň `accent/<hue>/bg` zůstává, tah `accent/<hue>/border w1 INSIDE` je nahrazen tahem fokusu.
- Stavy Hover i Pressed používají stejný tah `accent/<hue>/border-strong`.
- `icon-circle` má `fill=bg/surface` ve všech odstínech a stavech; mění se jen barva ikony.
- `star` je vystavená vnořená instance `Star button` (`exp=1`), ve výchozím stavu skrytá (`Show star`).
- Velikost: `text`, `title`, `description` jsou `fill/hug` - dlaždice se roztahuje na šířku (16 + 40 + 12 + 140 + 16 = 224 při skryté hvězdě). Výška kořene 88 je pevná (kořen `fix/fix`) a větší než obsah (16 + 51 + 16 = 83), zarovnání `MIN/CENTER`; popis: „Fixed height 88 px“.
- Texty `title` a `description` mají `HEIGHT` bez zkracování (žádné `trunc`) - zalamují se; `description` má ve výchozí variantě výšku 32 (dva řádky, 12/130%).

### Recent file row · `49:280`

Sada 8 variant · výchozí varianta `Status=Available, State=Default` · rozměr 381×51

Popis ve Figmě: „Row of the Recent Files list. Missing = the file was moved or deleted: the name is dimmed and a &quot;No longer exists&quot; badge replaces the time; the ⋮ menu offers &quot;Remove from list&quot;. Long file names are truncated in the middle so the extension stays visible (e.g. &quot;Technical_Doc…ation_v2.pdf&quot;). The star before ⋮ pins/unpins the file (exposed nested instance &quot;star&quot;: set Pinned=True for pinned files). Show star=false hides it. Figma cannot truncate a text in the middle, so the layer &quot;name&quot; shows a truncation at the end only because of this limit; the application elides the name in the middle.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| File name | text | **Project_Report.pdf** |
| Path | text | **C:\Users\Jakub\Documents** |
| Time | text | **Today 10:24** |
| Show star | přepínač | **true** |
| Status | varianta | **Available**, Missing |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Status=Available, State=Default' 381x51 lay=H gap12 pad10,12,10,12 MIN/CENTER; z=fix/hug; sil=0; st=border/default w1 INSIDE; rad=8
 I 'icon' 18x22 z=fix/fix; ic=icon/file-tall stroke=icon/pdf w2
 F 'text' 181x31 lay=V gap2 pad0,0,0,0 MIN/MIN; z=fill/hug
  T 'name' 181x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='Project_Report.pdf'; ref=characters:File name
  T 'path' 181x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='C:\Users\Jakub\Documents'; ref=characters:Path
 F 'missing-badge' 101x19 hid=1; lay=H gap0 pad3,8,3,8 MIN/MIN; z=hug/hug; fill=accent/red/bg; rad=10; clip=1
  T 'label' 85x13 z=hug/hug; fill=text/danger; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='No longer exists'
 T 'time' 70x15 z=hug/hug; fill=text/tertiary; f=Inter Regular 12/auto RT WIDTH_AND_HEIGHT; tx='Today 10:24'; ref=characters:Time
 I 'star' 24x24 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=6; ref=visible:Show star; inst=Star button{Pinned=False, State=Default} ov:height,name,width; exp=1
 I 'more' 16x16 z=fix/fix; ic=icon/more-vertical stroke=icon/muted w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Status=Available, State=Hover` | kořen: `st=border/strong w1 INSIDE; fill=bg/hover`; `more: ic=icon/more-vertical stroke=icon/default w2` |
| `Status=Available, State=Pressed` | kořen: `st=border/strong w1 INSIDE; fill=bg/pressed`; `more: ic=icon/more-vertical stroke=icon/default w2` |
| `Status=Available, State=Focus` | kořen: `st=border/focus w2 INSIDE` |
| `Status=Missing, State=Default` | `icon: ic=icon/file-tall stroke=icon/muted w2`; `text: s=150x31`; `text>name: s=150x16; fill=text/muted`; `text>path: s=150x13`; `missing-badge: hid=-`; `time: z=fix/fix; hid=1` |
| `Status=Missing, State=Hover` | (proti `Status=Missing, State=Default`) kořen: `st=border/strong w1 INSIDE; fill=bg/hover`; `more: ic=icon/more-vertical stroke=icon/default w2` |
| `Status=Missing, State=Pressed` | (proti `Status=Missing, State=Default`) kořen: `st=border/strong w1 INSIDE; fill=bg/pressed`; `more: ic=icon/more-vertical stroke=icon/default w2` |
| `Status=Missing, State=Focus` | (proti `Status=Missing, State=Default`) kořen: `st=border/focus w2 INSIDE` |

**Poznámky**

- Kořen: velikost `fix/hug`, ořez obsahu ne, obrys se počítá do rozložení ne (ve všech variantách, `sil=0`).
- **Změna:** ikona `more` (`icon/more-vertical`) je ve stavech Default a Focus `icon/muted` a ve stavech Hover a Pressed `icon/default` (dříve `icon/default` ve všech stavech) – stejně jako u Workspace row, Folder card, Recent item row a Pinned card. Platí pro obě hodnoty `Status`.
- **Změna popisu:** popis nově říká, že vrstva `name` ukazuje zkrácení na konci jen proto, že Figma neumí zkracovat uprostřed; aplikace jméno zkracuje uprostřed (přípona zůstává vidět). Vrstva `name` má dál `trunc1` (zkrácení na konci, jeden řádek), totéž `path`.
- Popis je uveden tak, jak ho vrací vlastnost `description` plugin API, s entitami `&quot;`; ve Figmě jsou v popisu skutečné uvozovky. Dřívější dvojité kódování (`&amp;quot;`, tedy doslovný text `&quot;` v popisu ve Figmě) bylo při úpravě popisu opraveno.
- Výchozí varianta nemá výplň, má tah `border/default w1 INSIDE`.
- Focus: `border/focus`, 2 px, `INSIDE` (**změna 14. 10. 2026**, rozhodnutí Q-2: řádky seznamů a nabídek mají kroužek uvnitř, dříve `OUTSIDE`); nahrazuje tah `border/default`; výplň se nepřidává, `more` zůstává `icon/muted`.
- `missing-badge` (101×19, `fill=accent/red/bg`) je skrytá ve čtyřech variantách `Status=Available` a není svázána s žádnou vlastností; zobrazují ji varianty `Status=Missing` (`hid=-`). Její text `No longer exists` není svázán s vlastností.
- `Status=Missing`: `time` (70×15, `text/tertiary`) je skrytý (`hid=1`) ve všech čtyřech variantách a zůstává svázán s vlastností `Time`. Údaj `z=fix/fix` u něj neznamená změnu režimu: vrstva skrytá ve výchozím stavu se čte s velikostí `fix`, skutečný režim je vidět až po zobrazení vrstvy (ve variantách `Status=Available`, kde je `time` vidět, je `hug/hug`). Jméno je `text/muted`, ikona `icon/muted`; `path` barvu nemění (`text/muted`).
- Velikost: `text`, `name`, `path` jsou `fill/hug` - řádek se roztahuje na šířku. 12 + 18 + 12 + 181 + 12 + 70 + 12 + 24 + 12 + 16 + 12 = 381; v `Status=Missing` 12 + 18 + 12 + 150 + 12 + 101 + 12 + 24 + 12 + 16 + 12 = 381. Výška 10 + 31 + 10 = 51 (kořen `fix/hug`: šířka pevná, výška podle obsahu).
- `time` je zarovnán vpravo (`RT`), `hug/hug` bez zkracování.
- Ikona souboru `icon/file-tall` (18×22, `stroke=icon/pdf`) není svázána s vlastností. Řádek nemá náhled stránky – `icon` je instance ikony bez vlastností, takže ji nelze vystavit (na rozdíl od `thumbnail` v Recent item row).
- Vnořená komponenta: `star` = `Star button{Pinned=False, State=Default}`, přepsaná velikost 24×24 (`ov:height,name,width`; samotná komponenta je 28×28), svázaná s `visible:Show star`. `star` je vystavená vnořená instance (`isExposedInstance = true`, všech 8 variant): `Pinned` a `State` hvězdy se nastavují na instanci řádku, jak uvádí i popis („exposed nested instance“), proto je komponenta mezi vlastními vlastnostmi nemá. Jiná vnořená instance komponenty v řádku není.
- Žádná varianta Disabled. Ikona `more` není instancí tlačítka (jen ikona 16×16), nemá vlastní stavy.

### Recent tool row · `50:279`

Sada 4 variant · výchozí varianta `State=Default` · rozměr 342×48

Popis ve Figmě: „Recently used tool on the Home screen. The icon circle is an exposed nested Tool icon (layer &quot;icon-circle&quot;): set its Hue to the category of the tool (same as its Quick action tile) and its Icon to the icon of the tool, keep Size=32. The name and the time are truncated at the end to one line.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Tool | text | **Compress** |
| Last used | text | **Today 11:02** |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'State=Default' 342x48 lay=H gap12 pad8,12,8,10 MIN/CENTER; z=fix/hug; rad=8
 I 'icon-circle' 32x32 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=accent/amber/bg; rad=16; inst=Tool icon{Hue=Amber, Size=32} [Icon=icon/minimize-2] ov:name,height,width,strokes; exp=1
 F 'text' 250x30 lay=V gap1 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  T 'name' 250x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='Compress'; ref=characters:Tool
  T 'time' 250x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='Today 11:02'; ref=characters:Last used
 I 'chevron' 14x14 z=fix/fix; ic=icon/chevron-right stroke=icon/muted w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `fill=bg/hover`; `chevron: ic=icon/chevron-right stroke=icon/default w2` |
| `State=Pressed` | kořen: `fill=bg/pressed`; `chevron: ic=icon/chevron-right stroke=icon/default w2` |
| `State=Focus` | kořen: `st=border/focus w2 INSIDE; sil=0` |

**Poznámky**

- Kořen: velikost `fix/hug`, ořez obsahu ne, obrys se počítá do rozložení ano, ve variantách Focus ne (`sil=0`, kroužek uvnitř obsah neposouvá).
- Výchozí varianta nemá výplň ani tah.
- Focus: `border/focus`, 2 px, `INSIDE` (**změna 14. 10. 2026**, rozhodnutí Q-2: řádky seznamů a nabídek mají kroužek uvnitř, dříve `OUTSIDE`); výplň se nepřidává, `chevron` zůstává `icon/muted`.
- **Změna:** `icon-circle` už není rámec s pevným jantarovým odstínem, ale vystavená vnořená instance Tool icon{Hue=Amber, Size=32} [Icon=icon/minimize-2] (`isExposedInstance = true`, všechny 4 varianty; rozměr 32×32, `rad=16`, ikona 16×16 s tahem 2 px – totéž co dřív). Odstín (`Hue`) a ikona (`Icon`) se nastavují na instanci řádku ve skupině `icon-circle`; `Size` nechat 32 (kruh má v řádku pevný rozměr, změna `Size` by změnila jen ikonu a poloměr). Přepsaná pole `height,width,strokes` patří vnořené ikoně (rozměr 16×16 a barva tahu), ne kruhu.
- **Změna (13. 10. 2026):** vlastní vlastnost `Icon` (záměna instance, `Icon#50:49`), která po náhradě vrstvy instancí Tool icon nebyla navázána na žádnou vrstvu, byla ze sady odstraněna; sada má jen `Tool`, `Last used` a `State`. Ikona se nastavuje vlastností `Icon` vystavené instance `icon-circle`.- **Nové:** `name` a `time` mají `trunc1` (jeden řádek, zkrácení na konci) a vyplňují šířku (`fill/hug`), stejně jako sourozenecké řádky (Workspace row, Recent item row).
- Velikost: `text`, `name`, `time` jsou `fill/hug` - řádek se roztahuje na šířku. 10 + 32 + 12 + 250 + 12 + 14 + 12 = 342, 8 + 32 + 8 = 48 (kořen `fix/hug`: šířka pevná, výška podle obsahu).
- `chevron` (`icon/chevron-right`) není svázán s vlastností.
- Žádná varianta Disabled.
- Popis je uveden tak, jak ho vrací vlastnost `description` plugin API, včetně entit `&quot;`; ve Figmě jsou v popisu skutečné uvozovky.

### Workspace row · `64:544`

Sada 4 variant · výchozí varianta `State=Default` · rozměr 344×52

Popis ve Figmě: „Saved workspace = a named set of open documents, their tabs, the active tools and the view state. Click opens all documents of the workspace in new tabs. ⋮ menu: Open, Open and close current documents, Rename, Update with open documents, Delete. The icon circle is an exposed nested Tool icon (layer &quot;icon-circle&quot;, Hue=Violet and icon/layers by default, Size=32).“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Name | text | **Thesis review** |
| Meta | text | **4 documents · Opened Yesterday 16:15** |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'State=Default' 344x52 lay=H gap12 pad0,8,0,10 MIN/CENTER; z=fix/fix; rad=8
 I 'icon-circle' 32x32 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=accent/violet/bg; rad=16; inst=Tool icon{Hue=Violet, Size=32} [Icon=icon/layers] ov:name,height,width,strokes; exp=1
 F 'text' 254x30 lay=V gap1 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  T 'name' 254x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='Thesis review'; ref=characters:Name
  T 'meta' 254x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='4 documents · Opened Yesterday 16:15'; ref=characters:Meta
 I 'more' 16x16 z=fix/fix; ic=icon/more-vertical stroke=icon/muted w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `fill=bg/hover`; `more: ic=icon/more-vertical stroke=icon/default w2` |
| `State=Pressed` | kořen: `fill=bg/pressed`; `more: ic=icon/more-vertical stroke=icon/default w2` |
| `State=Focus` | kořen: `st=border/focus w2 INSIDE; sil=0` |

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ano, ve variantách Focus ne (`sil=0`, kroužek uvnitř obsah neposouvá).
- Výchozí varianta nemá výplň ani tah.
- Focus: `border/focus`, 2 px, `INSIDE` (**změna 14. 10. 2026**, rozhodnutí Q-2: řádky seznamů a nabídek mají kroužek uvnitř, dříve `OUTSIDE`); výplň se nepřidává, `more` zůstává `icon/muted`.
- **Změna:** `icon-circle` už není rámec s pevnou ikonou a odstínem, ale vystavená vnořená instance Tool icon{Hue=Violet, Size=32} [Icon=icon/layers] (`isExposedInstance = true`, všechny 4 varianty; rozměr 32×32, `rad=16`, ikona 16×16 s tahem 2 px – vzhled se nezměnil). `Hue` a `Icon` lze nastavit na instanci řádku ve skupině `icon-circle`; výchozí fialová s `icon/layers` je vzhled uloženého pracovního prostoru. `Size` nechat 32 (kruh má v řádku pevný rozměr). Vlastní vlastnost `Icon` ani `Hue` komponenta nemá. Přepsaná pole `height,width,strokes` patří vnořené ikoně.
- Velikost: `text`, `name`, `meta` jsou `fill/hug` s `trunc1` - řádek se roztahuje na šířku, texty se zkracují na jeden řádek. 10 + 32 + 12 + 254 + 12 + 16 + 8 = 344. Výška kořene 52 při svislých výplních 0 a obsahu vysokém 32 - výška je pevná (kořen `fix/fix`).
- Vodorovné výplně jsou nesouměrné: vpravo 8, vlevo 10.
- Ikona `more` není instancí tlačítka (jen ikona 16×16), nemá vlastní stavy; položky nabídky vyjmenovává jen popis.
- Žádná varianta Disabled.
- Popis je uveden tak, jak ho vrací vlastnost `description` plugin API, včetně entit `&quot;`; ve Figmě jsou v popisu skutečné uvozovky.

### Star button · `64:569`

Sada 8 variant · výchozí varianta `Pinned=False, State=Default` · rozměr 28×28

Popis ve Figmě: „Toggle that pins (stars) a file. Pinned=False: outline star, muted; pins the file on click. Pinned=True: filled amber star; unpins on click. Tooltip: &quot;Pin file&quot; / &quot;Unpin&quot;. Used in file rows before the ⋮ menu.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Pinned | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Pinned=False, State=Default' 28x28 lay=H gap0 pad0,0,0,0 CENTER/CENTER; rad=6
 I 'icon' 16x16 z=fix/fix; ic=icon/star stroke=icon/muted w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Pinned=False, State=Hover` | kořen: `fill=bg/surface+bg/hover`; `icon: ic=icon/star stroke=icon/default w2` |
| `Pinned=False, State=Pressed` | kořen: `fill=bg/surface+bg/pressed`; `icon: ic=icon/star stroke=icon/default w2` |
| `Pinned=False, State=Focus` | kořen: `st=border/focus w2 OUTSIDE` |
| `Pinned=True, State=Default` | `icon: ic=icon/star-filled stroke=icon/sun w2 fill=icon/sun` |
| `Pinned=True, State=Hover` | (proti `Pinned=True, State=Default`) kořen: `fill=bg/surface+bg/hover` |
| `Pinned=True, State=Pressed` | (proti `Pinned=True, State=Default`) kořen: `fill=bg/surface+bg/pressed` |
| `Pinned=True, State=Focus` | (proti `Pinned=True, State=Default`) kořen: `st=border/focus w2 OUTSIDE` |

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ano (ve všech variantách).
- Popis je uveden tak, jak ho vrací vlastnost `description` plugin API, včetně entit `&quot;`. U komponenty `Text link` vrací `get_design_context` tentýž popis se skutečnými uvozovkami místo `&quot;`, entity jsou tedy podoba čtení přes `description`, ne znění popisu ve Figmě; u této komponenty popis jinou cestou čten nebyl.
- Výchozí varianta nemá výplň ani tah. **Změna 17. 10. 2026 (I-26, rozhodnutí 28):** Hover a Pressed (obě hodnoty `Pinned`) kladou `bg/hover` / `bg/pressed` jako druhou výplň nad vlastní `bg/surface` (dříve jen poloprůhledná vrstva, která se v řádku pod myší sčítala s hoverem řádku). Default a Focus výplň nemají.
- `Pinned=True` používá jinou komponentu ikony (`icon/star-filled` místo `icon/star`); její jediný vektor má tah `icon/sun` w2 i výplň `icon/sun` (všechny 4 varianty `Pinned=True`). Instance přepisuje obojí - hlavní komponenta `icon/star-filled` má tah i výplň `icon/default`. Ikona `icon/star` (`Pinned=False`) má jen tah, bez výplně. Zápis `fill=` za `ic=` znamená výplň vektoru ikony.
- `Pinned=False`: Hover a Pressed mění ikonu z `icon/muted` na `icon/default`. `Pinned=True`: Hover a Pressed barvu ikony nemění.
- Focus: `border/focus`, 2 px, `OUTSIDE`; výplň se nepřidává; u `Pinned=False` ikona zůstává `icon/muted`.
- Velikost: kořen 28×28 je větší než obsah (ikona 16×16 `fix/fix`, výplně 0, `CENTER/CENTER`) - pevný rozměr. V řádcích (`Recent file row`, `Recent item row`, `Pinned card`) je instance přepsána na 24×24, v `Quick action tile` má 28×28.
- Popis uvádí texty nápovědy „Pin file“ / „Unpin“; v komponentě nejsou nakresleny.
- Žádná varianta Disabled.

### Context menu / Recent file · `706:150896`

Sada 3 variant · výchozí varianta `File=Default` · rozměr 220×181

Popis ve Figmě: „⋮ menu of a recent file row. The rows are instances of Menu item and Menu separator, all in State=Default; Remove from list is a destructive action (Tone=Danger). File=Pinned: a pinned file shows &quot;Unpin&quot; instead of &quot;Pin to Pinned Files&quot;. File=Missing: the file does not exist, &quot;Remove from list&quot; is the first item and &quot;Open&quot; is disabled.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| File | varianta | **Default**, Pinned, Missing |

**Stavba výchozí varianty**

```text
C 'File=Default' 220x181 lay=V gap0 pad6,6,6,6 MIN/MIN; z=fix/hug; fill=bg/surface; st=border/default w1 INSIDE; rad=10; fx=DROP_SHADOW(0,4,16,0 effect/shadow); clip=1
 I 'menu-item' 206x32 lay=H gap10 pad0,10,0,10 MIN/CENTER; z=fill/fix; rad=6; inst=Menu item{State=Default, Tone=Default} [Submenu=false, Show shortcut=false, Show icon=true, Shortcut='Ctrl+D', Icon=icon/folder-open, Checked=false, Label='Open'] ov:height,name,width,strokes
 I 'menu-item' 206x32 lay=H gap10 pad0,10,0,10 MIN/CENTER; z=fill/fix; rad=6; inst=Menu item{State=Default, Tone=Default} [Submenu=false, Show shortcut=false, Show icon=true, Shortcut='Ctrl+D', Icon=icon/plus, Checked=false, Label='Open in new tab'] ov:height,name,width,strokes
 I 'menu-item' 206x32 lay=H gap10 pad0,10,0,10 MIN/CENTER; z=fill/fix; rad=6; inst=Menu item{State=Default, Tone=Default} [Submenu=false, Show shortcut=false, Show icon=true, Shortcut='Ctrl+D', Icon=icon/pin, Checked=false, Label='Pin to Pinned Files'] ov:height,name,width,strokes
 I 'menu-item' 206x32 lay=H gap10 pad0,10,0,10 MIN/CENTER; z=fill/fix; rad=6; inst=Menu item{State=Default, Tone=Default} [Submenu=false, Show shortcut=false, Show icon=true, Shortcut='Ctrl+D', Icon=icon/external-link, Checked=false, Label='Show in Explorer'] ov:height,name,width,strokes
 I 'separator' 206x7 lay=H gap0 pad3,4,3,4 MIN/CENTER; z=fill/hug; inst=Menu separator ov:height,name,width
 I 'menu-item' 206x32 lay=H gap10 pad0,10,0,10 MIN/CENTER; z=fill/fix; rad=6; inst=Menu item{State=Default, Tone=Danger} [Submenu=false, Show icon=true, Checked=false, Show shortcut=false, Shortcut='Ctrl+D', Icon=icon/x, Label='Remove from list'] ov:height,name,width,strokes
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `File=Pinned` | `menu-item#3`: `inst=Menu item{State=Default, Tone=Default} [Submenu=false, Show shortcut=false, Show icon=true, Shortcut='Ctrl+D', Icon=icon/pin-off, Checked=false, Label='Unpin'] ov:height,name,width,strokes` |
| `File=Missing` | `menu-item#1`: `inst=Menu item{State=Default, Tone=Danger} [Submenu=false, Show icon=true, Checked=false, Show shortcut=false, Shortcut='Ctrl+D', Icon=icon/x, Label='Remove from list'] ov:height,name,width,strokes`; `menu-item#2`: `inst=Menu item{State=Disabled, Tone=Default} [Submenu=false, Show icon=true, Show shortcut=false, Shortcut='Ctrl+D', Checked=false, Icon=icon/folder-open, Label='Open'] ov:height,name,width,strokes`; `menu-item#3`: `inst=Menu item{State=Default, Tone=Default} [Submenu=false, Show shortcut=false, Show icon=true, Shortcut='Ctrl+D', Icon=icon/plus, Checked=false, Label='Open in new tab'] ov:height,name,width,strokes`; `menu-item#4`: `inst=Menu item{State=Default, Tone=Default} [Submenu=false, Show shortcut=false, Show icon=true, Shortcut='Ctrl+D', Icon=icon/pin, Checked=false, Label='Pin to Pinned Files'] ov:height,name,width,strokes`; `menu-item#5`: `inst=Menu item{State=Default, Tone=Default} [Submenu=false, Show shortcut=false, Show icon=true, Shortcut='Ctrl+D', Icon=icon/external-link, Checked=false, Label='Show in Explorer'] ov:height,name,width,strokes` |

**Poznámky**

- Kořen: velikost `fix/hug` (vodorovně/svisle), ořez obsahu ano, obrys se počítá do rozložení ano. Platí pro všechny tři varianty.
- Komponenta je nově **sada** `706:150896` (dříve samostatná komponenta `64:592`). Původní komponenta `64:592` je výchozí varianta `File=Default` a má stejný identifikátor – existující instance na ni ukazují dál. Nové varianty: `File=Pinned` (`706:150753`), `File=Missing` (`706:150754`).
- Řádky jsou nově instance komponenty Menu item (`190:704`, 32 px, `Show shortcut=false`, šířka `fill` = 206) a oddělovač je instance Menu separator (`706:150117`, 7 px); dříve to byly obyčejné rámce `menu-item` 206×30 a `divider` 206×1. Výška nabídky je proto 181 (dříve 165): 6 + 4 × 32 + 7 + 32 + 6 = 179, + 2 px obrys `INSIDE` započtený do rozložení.
- Všechny řádky jsou ve stavu `State=Default`; výplň `bg/hover` prvního řádku (`Open`) z původní kresby je odstraněna. Stav řádku (Hover, Pressed, Disabled) se na obrazovce nastavuje na vnořené instanci Menu item; barva ikony a textu se při přepnutí varianty řádku mění s ní.
- `File=Default`: `Open`, `Open in new tab`, `Pin to Pinned Files`, `Show in Explorer` | `Remove from list`.
- `File=Pinned` (připnutý soubor): třetí položka je `Unpin` s ikonou `icon/pin-off`; jinak shodná s `File=Default`.
- `File=Missing` (soubor neexistuje): `Remove from list` je první položka, za ní oddělovač, `Open` je `State=Disabled` (tlumené tokeny); ostatní položky jsou nakreslené povolené, protože popis jmenuje jako zakázanou jen `Open`. Pořadí: `Remove from list` | `Open`, `Open in new tab`, `Pin to Pinned Files`, `Show in Explorer`. Rozdíly proti výchozí variantě uvádí tabulka po pozicích řádků (`menu-item#1` až `#5`), vrstva `separator` je druhá místo páté.
- `Remove from list` je `Tone=Danger` (ikona `icon/x` v `icon/danger`, text `text/danger`) ve všech třech variantách.
- Pět vrstev řádků má stejné jméno `menu-item`, oddělovač `separator`. Vnořené instance nejsou vystavené (`exp` není nastaveno) – jejich vlastnosti se nenastavují na nadřazené instanci, ale přímo na řádku. `ov:…strokes` u řádků je barva tahu zaměněné ikony, kterou Figma ukládá při záměně; `Shortcut='Ctrl+D'` je výchozí text skryté zkratky.
- Panel: výplň `bg/surface`, obrys `border/default` 1 px `INSIDE`, `rad=10`, stín `DROP_SHADOW(0,4,16,0 effect/shadow)`, odsazení 6 ze všech stran, `gap0`; šířka 220 je pevná, výška podle řádků.
- Popis je uveden tak, jak ho vrací vlastnost `description` plugin API, včetně entit `&quot;`.
- Žádná barva nevázaná na proměnnou.

### Folder card · `63:597`

Sada 16 variant · výchozí varianta `Hue=Neutral, State=Default` · rozměr 252×84

Popis ve Figmě: „Folder in Recent Folders and Pinned Locations. Hue=Green/Blue/Violet: the fixed standard folders (Documents = green, Downloads = blue, Desktop = violet) – always first, cannot be removed. Hue=Neutral: folders the user opened from recently, or pinned locations. Click opens the file dialog in that folder; ⋮ menu: Pin location / Unpin, Show in Explorer, Remove from list (Neutral only). Meta line: &quot;Last used …&quot; or the pinned location kind (This PC, Personal cloud, Network).“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Name | text | **Documents** |
| Path | text | **C:\Users\Jakub\Documents** |
| Meta | text | **Last used Today 10:24** |
| Icon | záměna instance | **icon/folder** |
| Show meta | přepínač | **true** |
| Hue | varianta | Blue, **Neutral**, Green, Violet |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Hue=Neutral, State=Default' 252x84 lay=H gap12 pad12,10,12,14 MIN/CENTER; z=fix/fix; fill=accent/neutral/bg; st=accent/neutral/border w1 INSIDE; rad=12
 F 'icon-circle' 36x36 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=bg/surface; rad=18; clip=1
  I 'icon' 18x18 z=fix/fix; ref=mainComponent:Icon; ic=icon/folder stroke=accent/neutral/icon w2
 F 'text' 150x46 lay=V gap2 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  T 'name' 150x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='Documents'; ref=characters:Name
  T 'path' 150x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='C:\Users\Jakub\Documents'; ref=characters:Path
  T 'meta' 150x13 z=fill/hug; fill=text/tertiary; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='Last used Today 10:24'; ref=visible:Show meta,characters:Meta
 I 'more' 16x16 z=fix/fix; ic=icon/more-vertical stroke=icon/muted w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Hue=<Hue>, State=Default` (Hue = Blue, Green, Violet; hue = blue, green, violet) | kořen: `fill=accent/<hue>/bg; st=accent/<hue>/border w1 INSIDE`; `icon-circle>icon: ic=icon/folder stroke=accent/<hue>/icon w2` |
| `Hue=<Hue>, State=Hover` (všechny 4 odstíny včetně Neutral) | (proti `Hue=<Hue>, State=Default`) kořen: `fill=accent/<hue>/bg-hover; st=accent/<hue>/border-strong w1 INSIDE`; `more: ic=icon/more-vertical stroke=icon/default w2` |
| `Hue=<Hue>, State=Pressed` (všechny 4 odstíny včetně Neutral) | (proti `Hue=<Hue>, State=Default`) kořen: `fill=accent/<hue>/bg-pressed; st=accent/<hue>/border-strong w1 INSIDE`; `more: ic=icon/more-vertical stroke=icon/default w2` |
| `Hue=<Hue>, State=Focus` (všechny 4 odstíny včetně Neutral) | (proti `Hue=<Hue>, State=Default`) kořen: `lay=H gap12 pad13,11,13,15 MIN/CENTER; st=border/focus w2 OUTSIDE` |

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ano (ve všech variantách).
- Nové (oprava F-43, F-44, F-75): všechny čtyři odstíny včetně `Neutral` se kreslí jedním receptem, stejným jako `Quick action tile` a `Resource card`: Default výplň `accent/<hue>/bg`, tah `accent/<hue>/border`, ikona `accent/<hue>/icon`; Hover `accent/<hue>/bg-hover` a tah `accent/<hue>/border-strong`; Pressed `accent/<hue>/bg-pressed` a tah `accent/<hue>/border-strong`; ikona odstínu se ve stavech nemění. Vzor byl ověřen u všech 16 variant; žádná výjimka.
- Co se proti dřívějšku změnilo: `Hue=Neutral` měl v Default a Focus výplň `bg/muted` (nyní `accent/neutral/bg`), tah `border/default` a `border/strong` (nyní `accent/neutral/border` a `accent/neutral/border-strong`, hodnoty obou režimů jsou stejné) a ikonu `icon/default` (nyní `accent/neutral/icon`; ve světlém režimu stejná hodnota, v tmavém světlejší). Blue, Green a Violet měly už ve stavu Default tah `accent/<hue>/border-strong`; nyní mají v Default světlejší `accent/<hue>/border` a `border-strong` až při Hover a Pressed.
- Texty (`text/primary`, `text/muted`, `text/tertiary`), kruh ikony (`bg/surface`) a ikona `more` se s odstínem nemění; `more` je `icon/muted` v Default a Focus a `icon/default` v Hover a Pressed.
- Fokus: `border/focus`, 2 px, `OUTSIDE`; nahrazuje tah odstínu, výplň `accent/<hue>/bg` zůstává.
- Nové (oprava F-26): varianta Focus má potomky na stejných místech a ve stejných rozměrech jako Default (`icon-circle` na 15, 24; `text` 150×46; `more` na 225, 34). Dříve byl `text` ve stavu Focus o 2 px širší (152), protože tah `OUTSIDE` se do rozložení nepočítá; Focus to vyrovnává výplněmi o 1 px většími (13, 11, 13, 15 místo 12, 10, 12, 14).
- Barva ikony je přepis tahu vektoru uvnitř instance `icon`. Po záměně ikony vlastností `Icon` se přepis na jinou ikonu nepřenáší (instance se zaměněnou ikonou na stránce *Main page* mají tah ikony `icon/default`, pokud ho návrhář nepřepsal ručně); widget barví ikonu vždy tokenem `accent/<hue>/icon`.
- Popis je uveden tak, jak ho vrací vlastnost `description` plugin API, včetně entit `&quot;`.

### Recent item row · `79:1026`

Sada 12 variant · výchozí varianta `Type=File, State=Default` · rozměr 802×56

Popis ve Figmě: „Row of the Recent list. Type=File shows the rendered first page (Page thumbnail) and the reading progress (last viewed page / page count; bar = nested Progress bar, override Value). Type=Folder / Workspace show an icon instead and no bar. Click opens the item; star pins it (exposed nested instance &quot;star&quot;); ⋮ menu: Open, Open in new tab, Pin/Unpin, Show in Explorer, Remove from list. The thumbnail of Type=File is an exposed nested instance (Page thumbnail). Type=Folder uses the hue accent/neutral, Type=Workspace accent/violet.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Name | text | **Project_Report.pdf** |
| Details | text | **C:\Users\Jakub\Documents · 24 pages · 2.4 MB** |
| Progress text | text | **Page 12 of 24** |
| Opened | text | **Today 10:24** |
| Type | varianta | **File**, Folder, Workspace |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Type=File, State=Default' 802x56 lay=H gap14 pad0,8,0,10 MIN/CENTER; z=fix/fix; rad=8
 F 'leading' 36x42 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; clip=1
  I 'thumbnail' 32x42 lay=V gap2 pad5,4,0,4 MIN/MIN; z=fix/fix; fill=bg/surface; st=border/strong w1 INSIDE; rad=2; fx=DROP_SHADOW(0,1,2,0 effect/shadow); clip=1; inst=Page thumbnail{Size=Small} ov:name; exp=1
 F 'text' 410x31 lay=V gap2 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  T 'name' 410x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='Project_Report.pdf'; ref=characters:Name
  T 'details' 410x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='C:\Users\Jakub\Documents · 24 pages · 2.4 MB'; ref=characters:Details
 F 'progress' 132x22 lay=V gap5 pad0,0,0,0 MIN/MIN; z=fix/hug; clip=1
  T 'progress-text' 132x13 z=fill/hug; fill=text/tertiary; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='Page 12 of 24'; ref=characters:Progress text
  I 'bar' 120x4 z=fix/fix; fill=border/default; rad=2; clip=1; inst=Progress bar{Value=50} ov:name; exp=1
 T 'opened' 96x15 z=fix/hug; fill=text/tertiary; f=Inter Regular 12/auto RT HEIGHT trunc1; tx='Today 10:24'; ref=characters:Opened
 I 'star' 24x24 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=6; inst=Star button{Pinned=False, State=Default} ov:height,name,width; exp=1
 I 'more' 16x16 z=fix/fix; ic=icon/more-vertical stroke=icon/muted w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Type=File, State=Hover` | kořen: `fill=bg/hover`; `more: ic=icon/more-vertical stroke=icon/default w2` |
| `Type=File, State=Pressed` | kořen: `fill=bg/pressed`; `more: ic=icon/more-vertical stroke=icon/default w2` |
| `Type=File, State=Focus` | kořen: `st=border/focus w2 INSIDE; sil=0` |
| `Type=Folder, State=Default` | přibývá `leading>icon-circle: F 32x32 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=accent/neutral/bg; rad=16; clip=1`; přibývá `leading>icon-circle>icon: I 16x16 z=fix/fix; ic=icon/folder stroke=accent/neutral/icon w2`; `progress: s=132x13`; `progress>bar: hid=1`; chybí `leading>thumbnail` |
| `Type=Workspace, State=Default` | přibývá `leading>icon-circle: F 32x32 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=accent/violet/bg; rad=16; clip=1`; přibývá `leading>icon-circle>icon: I 16x16 z=fix/fix; ic=icon/layers stroke=accent/violet/icon w2`; `progress: s=132x13`; `progress>bar: hid=1`; chybí `leading>thumbnail` |
| `Type=<Type>, State=Hover` (Folder, Workspace) | (proti `Type=<Type>, State=Default`) kořen: `fill=bg/hover`; `more: ic=icon/more-vertical stroke=icon/default w2` |
| `Type=<Type>, State=Pressed` (Folder, Workspace) | (proti `Type=<Type>, State=Default`) kořen: `fill=bg/pressed`; `more: ic=icon/more-vertical stroke=icon/default w2` |
| `Type=<Type>, State=Focus` (Folder, Workspace) | (proti `Type=<Type>, State=Default`) kořen: `st=border/focus w2 INSIDE; sil=0` |

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ano, ve variantách Focus ne (`sil=0`, kroužek uvnitř obsah neposouvá).
- Stavy Hover, Pressed a Focus jsou u všech tří hodnot `Type` shodné (ověřeno u všech 9 variant).
- **Změna:** `Type=Folder` má kruh `accent/neutral/bg` a ikonu `accent/neutral/icon` (dříve `bg/muted` a `icon/default`), ve všech čtyřech stavech – odstín Neutral stejným způsobem, jakým `Type=Workspace` používá `accent/violet/*`.
- **Nové:** `thumbnail` = `Page thumbnail{Size=Small}` je vystavená vnořená instance (`exp=1`, čtyři varianty `Type=File`): její vlastnost `Size` je vidět na instanci řádku. Náhled první stránky dodává aplikace jako obrázek.
- Popis je uveden tak, jak ho vrací vlastnost `description` plugin API, včetně entit `&quot;`; ve Figmě jsou v popisu skutečné uvozovky.
- Výchozí varianta nemá výplň ani tah (na rozdíl od `Recent file row`, který má tah `border/default`).
- Focus: `border/focus`, 2 px, `INSIDE` (**změna 14. 10. 2026**, rozhodnutí Q-2: řádky seznamů a nabídek mají kroužek uvnitř, dříve `OUTSIDE`); výplň se nepřidává, `more` zůstává `icon/muted`.
- `Type=Folder` a `Type=Workspace` mají jinou stavbu rámu `leading`: místo instance `thumbnail` je v něm `icon-circle` 32×32 s ikonou 16×16. Je to obyčejný rámec, ne instance Tool icon; ikona a barvy jsou pevné (`icon/folder` s `accent/neutral/bg` a `accent/neutral/icon`; `icon/layers` s `accent/violet/bg` a `accent/violet/icon`) a nejsou svázány s vlastností.
- `Type=Folder` a `Type=Workspace`: `bar` je skrytý (`hid=1`) ve všech 8 variantách těchto dvou typů a není svázán s vlastností (skrývá ho varianta); `progress-text` zůstává viditelný a svázaný s vlastností `Progress text`, rám `progress` má výšku 13 místo 22.
- Sloupce pevné šířky: `leading` 36 (`fix/fix`), `progress` 132 (`fix/hug`), `opened` 96 (`fix/hug`, zarovnání vpravo `RT`), `star` 24, `more` 16. Roztahuje se jen `text` (`fill/hug`). 10 + 36 + 14 + 410 + 14 + 132 + 14 + 96 + 14 + 24 + 14 + 16 + 8 = 802.
- `bar` má šířku 120 (`fix/fix`) ve sloupci `progress` širokém 132.
- Výška kořene 56 při svislých výplních 0 a obsahu vysokém 42 - výška je pevná (kořen `fix/fix`). Vodorovné výplně: vpravo 8, vlevo 10.
- Všechny texty mají `trunc1` (jeden řádek, zkrácení na konci).
- Vnořené komponenty: `thumbnail` = `Page thumbnail{Size=Small}` (`ov:name`); `bar` = `Progress bar{Value=50}` (`ov:name`); `star` = `Star button{Pinned=False, State=Default}` přepsaná na 24×24 (`ov:height,name,width`). `thumbnail`, `bar` a `star` jsou vystavené vnořené instance (`isExposedInstance = true`; `bar` a `star` ve všech 12 variantách, `thumbnail` ve čtyřech variantách `Type=File`): `Size` náhledu, `Value` pruhu a `Pinned` a `State` hvězdy se nastavují na instanci řádku (popis: „override Value“, „exposed nested instance "star"“), proto je komponenta mezi vlastními vlastnostmi nemá.
- Žádná varianta Disabled.

### Pinned card · `79:1115`

Sada 4 variant · výchozí varianta `State=Default` · rozměr 259×100

Popis ve Figmě: „Pinned document on the Recent page: first-page thumbnail, name, path, reading progress. Star (filled) unpins; ⋮ menu as in Recent item row. Progress bar: nested Progress bar, override Value. The thumbnail is an exposed Page thumbnail instance: its properties are set on the card instance.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Name | text | **Project_Report.pdf** |
| Path | text | **C:\Users\Jakub\Documents** |
| Progress text | text | **Page 12 of 24 · Today 10:24** |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'State=Default' 259x100 lay=H gap12 pad12,8,12,12 MIN/MIN; z=fix/fix; fill=bg/surface; st=border/default w1 INSIDE; rad=12
 I 'thumbnail' 56x74 lay=V gap4 pad9,8,0,8 MIN/MIN; z=fix/fix; fill=bg/surface; st=border/strong w1 INSIDE; rad=2; fx=DROP_SHADOW(0,1,2,0 effect/shadow); clip=1; inst=Page thumbnail{Size=Medium} ov:name; exp=1
 F 'text' 133x74 lay=V gap3 pad0,0,0,0 MIN/MIN; z=fill/fill; clip=1
  T 'name' 133x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='Project_Report.pdf'; ref=characters:Name
  T 'path' 133x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='C:\Users\Jakub\Documents'; ref=characters:Path
  F 'spacer' 133x16 z=fill/fill; clip=1
  T 'progress-text' 133x13 z=fill/hug; fill=text/tertiary; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='Page 12 of 24 · Today 10:24'; ref=characters:Progress text
  I 'bar' 133x4 z=fill/fix; fill=border/default; rad=2; clip=1; inst=Progress bar{Value=50} ov:height,name,width; exp=1
 F 'actions' 24x44 lay=V gap4 pad0,0,0,0 MIN/CENTER; z=hug/hug; clip=1
  I 'star' 24x24 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=6; inst=Star button{Pinned=True, State=Default} ov:height,name,width; exp=1
  I 'more' 16x16 z=fix/fix; ic=icon/more-vertical stroke=icon/muted w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `fill=bg/surface+bg/hover; st=border/strong w1 INSIDE`; `actions>more: ic=icon/more-vertical stroke=icon/default w2` |
| `State=Pressed` | kořen: `fill=bg/surface+bg/pressed; st=border/strong w1 INSIDE`; `actions>more: ic=icon/more-vertical stroke=icon/default w2` |
| `State=Focus` | kořen: `lay=H gap12 pad13,9,13,13 MIN/MIN; st=border/focus w2 OUTSIDE` |

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ano (ve všech variantách).
- Focus: `border/focus`, 2 px, `OUTSIDE`; výplň `bg/surface` zůstává, tah `border/default w1 INSIDE` je nahrazen tahem fokusu.
- Nové (oprava F-26): varianta Focus má potomky na stejných místech a ve stejných rozměrech jako Default (`thumbnail` na 13, 13; `text` 133×74; `spacer` 16). Dříve byl `text` ve stavu Focus o 2 px širší i vyšší (135×76), protože tah `OUTSIDE` se do rozložení nepočítá; Focus to vyrovnává výplněmi o 1 px většími (13, 9, 13, 13 místo 12, 8, 12, 12).
- Rozměr: 1 + 12 + 56 + 12 + 133 + 12 + 24 + 8 + 1 = 259 a 1 + 12 + 74 + 12 + 1 = 100 (1 px je tah `INSIDE` započtený do rozložení); ve stavu Focus 13 + 56 + 12 + 133 + 12 + 24 + 9 = 259 a 13 + 74 + 13 = 100. Kořen je `fix/fix`.
- Velikost: `text` je `fill/fill`, jeho texty `fill/hug` s `trunc1`, `spacer` je `fill/fill` (vyplňuje zbylou výšku mezi `path` a `progress-text`), `bar` je `fill/fix` - karta se roztahuje na šířku. `thumbnail` je `fix/fix`, `actions` `hug/hug`. Zarovnání kořene `MIN/MIN` (obsah nahoře).
- Vodorovné výplně jsou nesouměrné: vpravo 8, vlevo 12.
- Vnořené komponenty: `thumbnail` = `Page thumbnail{Size=Medium}` (`ov:name`); `bar` = `Progress bar{Value=50}` přepsaný na 133×4 (`ov:height,name,width`); `star` = `Star button{Pinned=True, State=Default}` přepsaná na 24×24 (`ov:height,name,width`). Všechny tři jsou vystavené vnořené instance (`exp=1`, všechny 4 varianty): `Value` pruhu (popis: „override Value“), `Pinned` a `State` hvězdy a vlastnosti náhledu se nastavují na instanci karty, proto je komponenta mezi vlastními vlastnostmi nemá. Progress bar nemá od 13. 10. 2026 auto layout a jeho vrstva `fill` sleduje šířku instance (vazby `SCALE/STRETCH`): v pruhu širokém 133 má `Value=50` šířku 66.5 (dříve pevných 60); v `Recent item row` je 60×4 v pruhu 120×4.
- Nové (oprava F-33): `thumbnail` je vystavená instance. `Page thumbnail` má v době opravy jedinou vlastnost `Size` (místo pro náhled v kartě je 56×74 = `Size=Medium`; jiná velikost by rozložení karty změnila). Obrázek první stránky dodává aplikace.
- Hvězda je ve výchozím stavu `Pinned=True` (v `Recent file row` a `Recent item row` je `Pinned=False`).
- `actions`: svislý sloupec `star` nad `more`, mezera 4, zarovnání `MIN/CENTER`.
- Stavy Hover a Pressed mění výplň i tah (`border/strong`) a ikonu `more` na `icon/default`. **Změna 16. 10. 2026 (N-23, rozhodnutí 21):** Hover a Pressed kladou poloprůhledné `bg/hover` / `bg/pressed` jako druhou výplň nad `bg/surface`, ne místo ní, takže prvek má vlastní plochu i na `bg/sidebar` a `bg/subtle` (vzor Tool choice).

### Page thumbnail · `78:892`

Sada 3 variant · výchozí varianta `Size=Small` · rozměr 32×42

Popis ve Figmě: „Placeholder for the rendered first page of a document (the app renders the real page into this frame). The aspect ratio is about 1:1.32 (between A4 and Letter), rounded to whole pixels in each size.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Size | varianta | **Small**, Medium, Large |

**Stavba výchozí varianty**

```text
C 'Size=Small' 32x42 lay=V gap2 pad5,4,0,4 MIN/MIN; fill=bg/surface; st=border/strong w1 INSIDE; rad=2; fx=DROP_SHADOW(0,1,2,0 effect/shadow); clip=1
 F 'title-line' 22x2 z=fill/fix; fill=text/muted; clip=1
 F 'line' 22x1 z=fill/fix; fill=border/strong; clip=1
 F 'line' 22x1 z=fill/fix; fill=border/strong; clip=1
 F 'line' 14x1 z=fix/fix; fill=border/strong; clip=1
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Size=Medium` | kořen: `s=56x74; lay=V gap4 pad9,8,0,8 MIN/MIN`; `title-line: s=38x4`; `line#1: s=38x2`; `line#2: s=38x2`; `line#3: s=38x2; z=fill/fix`; přibývá `line#4: F 38x2 z=fill/fix; fill=border/strong; clip=1`; přibývá `line#5: F 38x2 z=fill/fix; fill=border/strong; clip=1`; přibývá `line#6: F 25x2 z=fix/fix; fill=border/strong; clip=1` |
| `Size=Large` | kořen: `s=80x106; lay=V gap5 pad13,11,0,11 MIN/MIN`; `title-line: s=56x5`; `line#1: s=56x3`; `line#2: s=56x3`; `line#3: s=56x3; z=fill/fix`; přibývá `line#4: F 56x3 z=fill/fix; fill=border/strong; clip=1`; přibývá `line#5: F 56x3 z=fill/fix; fill=border/strong; clip=1`; přibývá `line#6: F 56x3 z=fill/fix; fill=border/strong; clip=1`; přibývá `line#7: F 56x3 z=fill/fix; fill=border/strong; clip=1`; přibývá `line#8: F 36x3 z=fix/fix; fill=border/strong; clip=1` |

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ano, obrys se počítá do rozložení ano (ve všech variantách).
- Komponenta nemá vlastnost `State` ani jiné vlastnosti než `Size`; je to zástupná kresba stránky (popis: aplikace do rámu kreslí skutečnou stránku).
- Rozměry variant: 32×42, 56×74, 80×106; poměry stran 1:1,3125, 1:1,3214 a 1:1,325 (popis: „about 1:1.32 …, rounded to whole pixels in each size“).
- Počet řádků se liší: Small `title-line` + 3 × `line`, Medium `title-line` + 6 × `line`, Large `title-line` + 8 × `line`. Poslední `line` je vždy kratší a `fix/fix` (14, 25, 36), ostatní jsou `fill/fix`. Ve výchozí variantě mají tři vrstvy stejné jméno `line`.
- **Změna (13. 10. 2026):** čáry leží na celých pixelech – `line` 22×1 a 14×1 ve Small (dříve 22×1.5 a 14.4×1.5), poslední čára Medium 25×2 (dříve 25.2×2). Neceločíselný rozměr v komponentě nezůstal žádný; rozměry variant, mezery a okraje se nezměnily.
- Šířka: 4 + 22 + 4 = 30 (Small), 8 + 38 + 8 = 54 (Medium), 11 + 56 + 11 = 78 (Large), kořen má 32, 56 a 80 - rozdíl 2 px je tah 1 px `INSIDE` započtený do rozložení (`strokesIncludedInLayout` je zapnuto).
- Spodní výplň kořene je 0 ve všech velikostech; zarovnání `MIN/MIN` (řádky od horního okraje).
- Kořen: `fill=bg/surface`, `st=border/strong w1 INSIDE`, `rad=2`, stín `DROP_SHADOW(0,1,2,0 effect/shadow)` - stejné ve všech velikostech.
- Použití ve skupině: `Recent item row` (`Size=Small`), `Pinned card` (`Size=Medium`); `Size=Large` v této skupině použita není.

## 3. Katalog toolů, What's New, About

### Tool icon · `89:1289`

Sada 21 variant · výchozí varianta `Hue=Blue, Size=32` · rozměr 32×32

Popis ve Figmě: „Circular category-coloured badge for a tool. Hue = tool category (Organize Pages amber, Create &amp; Convert blue, Edit green, Protect &amp; Sign red, Optimize violet, Review &amp; Inspect teal); Neutral is the fallback for everything without a tool category (collections, documents, settings and help in the search results, attachments, fonts, annotation types). Size = diameter of the badge: 32 in rows and cards, 28 in Tool choice, in the title of a tool and in the search results, 44 in the header of a workspace. The icon is half of the diameter (16, 14, 22) and its stroke is 2 px at every size, so pick the Size variant and never scale an instance. Inside rows the badge hugs its Size variant, so changing Size on the row resizes the circle too.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Icon | záměna instance | **icon/grid** |
| Hue | varianta | **Blue**, Violet, Amber, Red, Teal, Green, Neutral |
| Size | varianta | **32**, 28, 44 |

**Stavba výchozí varianty**

```text
C 'Hue=Blue, Size=32' 32x32 lay=H gap0 pad8,8,8,8 CENTER/CENTER; z=hug/hug; fill=accent/blue/bg; rad=16
 I 'icon' 16x16 z=fix/fix; ref=mainComponent:Icon; ic=icon/grid stroke=accent/blue/icon w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Hue=<hue>, Size=32` (hue = Violet, Amber, Red, Teal, Green, Neutral; ověřeno u všech šesti) | kořen: `fill=accent/<hue>/bg`; `icon`: `ic=icon/grid stroke=accent/<hue>/icon w2` (název tokenu malými písmeny: violet, amber, red, teal, green, neutral) |
| `Hue=Blue, Size=28` | kořen: `s=28x28; rad=14`; `icon`: `s=14x14` |
| `Hue=<hue>, Size=28` (šest ostatních odstínů) | kořen: `s=28x28; fill=accent/<hue>/bg; rad=14`; `icon`: `s=14x14; ic=icon/grid stroke=accent/<hue>/icon w2` |
| `Hue=Blue, Size=44` | kořen: `s=44x44; rad=22`; `icon`: `s=22x22` |
| `Hue=<hue>, Size=44` (šest ostatních odstínů) | kořen: `s=44x44; fill=accent/<hue>/bg; rad=22`; `icon`: `s=22x22; ic=icon/grid stroke=accent/<hue>/icon w2` |

**Poznámky**

- Kořen: velikost `hug/hug` s okraji (průměr − ikona) / 2 – 8 u `Size=32`, 7 u 28, 11 u 44; ořez obsahu ne, obrys se počítá do rozložení ano (ve všech variantách; kořen sám obrys nemá). **Změna 14. 10. 2026 (N-18):** dříve byl kořen `fix/fix` bez okrajů; rozměr se nezměnil.
- **Nové:** osa `Size` se třemi hodnotami. Odznak je vždy plný kruh (`rad` = polovina průměru), ikona má polovinu průměru (32 → 16, 28 → 14, 44 → 22), zarovnání `CENTER/CENTER`. Tah ikony je ve všech velikostech 2 px (`w2`) – ikona se mění změnou rozměru instance ikony, ne měřítkem, takže tah zůstává. Kořen nemá obrys ani efekt.
- Dříve měla sada jen 32×32 a menší a větší odznaky vznikaly měřítkem instance (`scaleFactor` 0,875 a 1,375, tah 1,75 a 2,75 px). Všechny takové instance na všech stránkách byly nahrazeny variantou své velikosti s `scaleFactor = 1`; instanci Tool icon už nikde neškálovat.
- `Hue=Neutral` popis nově přiřazuje: je to výchozí odstín pro vše, co nemá kategorii toolu (kolekce, dokumenty, nastavení a nápověda ve výsledcích hledání, přílohy, písma, typy anotací).
- Žádné stavy (Hover, Pressed, Focus, Disabled) – sada má jen osy `Hue` a `Size`.
- Barva tahu ikony se mění s odstínem (`accent/<hue>/icon`), ikona se zaměňuje vlastností `Icon`. **Změna 14. 10. 2026 (N-17):** obrysové ikony mají jediný vektor `glyph`, takže barvu odstínu po záměně převezme celá ikona. Dříve měly ikony složené ze dvou vektorů barvu jen na prvním a 98 instancí ikon na obrazovkách kreslilo druhou část `icon/default` (například 68 odznaků OCR na stránce *Tools - OCR*; kapitola 8).
- Pořadí hodnot `Size` ve Figmě je 32, 28, 44 (výchozí první). Sada není auto layout: řádky mřížky jsou velikosti (32, 28, 44), sloupce odstíny.
- Komponenta je vnořena v Tool row, Tool shortcut, Recent tool row a Workspace row (`Size=32`) a v Tool choice (`Size=28`). V těchto komponentách (a v Note row, Attachment row a Font row, celkem 48 vnořených instancí) je instance vystavená a od 14. 10. 2026 (N-18) má v auto layoutu velikost `hug/hug`: změna `Size` na nadřazené instanci změní kruh, ikonu i poloměr (ověřeno: odznak v řádku Tool row s `Size=44` má 44×44, s `Size=28` 28×28). Dříve měla instance pevný rozměr a měnila se jen ikona a poloměr.

### Tool row · `89:1330`

Sada 4 variant · výchozí varianta `State=Default` · rozměr 380×48

Popis ve Figmě: „One tool inside a category card on the Tools page. Whole row launches the tool; the star pins/unpins it. Badge (Hue, Icon) and star (Pinned) are exposed nested instances. The tag is an exposed nested instance too: Show tag shows it and its Label sets the text (default &quot;Advanced&quot;).“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Name | text | **Merge PDF** |
| Description | text | **Combine multiple PDFs into one file** |
| Show tag | přepínač | **false** |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'State=Default' 380x48 lay=H gap10 pad0,6,0,8 MIN/CENTER; z=fix/fix; rad=8
 I 'badge' 32x32 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=accent/blue/bg; rad=16; inst=Tool icon{Hue=Blue, Size=32} [Icon=icon/grid] ov:name; exp=1
 F 'text' 286x30 lay=V gap1 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  T 'name' 286x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='Merge PDF'; ref=characters:Name
  T 'description' 286x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='Combine multiple PDFs into one file'; ref=characters:Description
 I 'tag' 73x21 hid=1; lay=H gap0 pad3,9,3,9 MIN/CENTER; z=hug/hug; fill=bg/subtle; st=border/default w1 INSIDE; rad=100; ref=visible:Show tag; inst=Tag{State=Default} [Label='Advanced'] ov:height,name,width; exp=1
 I 'star' 28x28 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=6; inst=Star button{Pinned=False, State=Default} ov:name; exp=1
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `fill=bg/hover` |
| `State=Pressed` | kořen: `fill=bg/pressed` |
| `State=Focus` | kořen: `sil=0; st=border/focus w2 INSIDE` |

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ne ve variantě Focus (`sil=0`); ostatní varianty obrys nemají.
- Výchozí varianta nemá výplň ani obrys kořene (jen `rad=8`); výplň se objeví až v Hover (`bg/hover`) a Pressed (`bg/pressed`).
- **Změna:** Fokus: `border/focus`, 2 px, `INSIDE`, a obrys se už nepočítá do rozložení – obsah má ve Focus stejné polohy a šířky jako ve Default (`badge` 8,8, `text` 286 px, `star` 346,10; dříve se `text` zúžil na 282 px a obsah se posunul o 2 px). Ověřeno i se zobrazeným štítkem (`text` 203 px v obou stavech). Skrytá nenavázaná výplň `#ffffff` varianty Focus byla odstraněna; Focus je Default + kroužek.
- Není varianta Disabled ani kombinace stavu s připnutím (stav hvězdy nese vnořená instance).
- Vnořené komponenty: `badge` = Tool icon{Hue=Blue, Size=32} [Icon=icon/grid]; `tag` = Tag{State=Default} [Label='Advanced'], skrytý ve všech čtyřech variantách (73×21), viditelnost vázána na `Show tag`; `star` = Star button{Pinned=False, State=Default}. Všechny tři jsou vystavené vnořené instance (`isExposedInstance = true`, všechny 4 varianty): `Hue` a `Icon` odznaku, `Label` (a `State`) štítku a `Pinned` a `State` hvězdy se nastavují na instanci řádku, proto je Tool row mezi vlastními vlastnostmi nemá.
- **Nové:** `tag` je vystavená instance – text štítku („Advanced“) se nastavuje jeho vlastností `Label` na instanci řádku; zobrazuje ho dál `Show tag`.
- `badge` má po přidání osy `Size` do Tool icon i vlastnost `Size`; v řádku je výchozí 32, změna `Size` změní i velikost kruhu (`hug/hug` od 14. 10. 2026, N-18).
- Rozměry: `badge` 32×32 a `star` 28×28 jsou pevné (`z=fix/fix`), `tag` `hug/hug`, `text` vyplňuje zbytek šířky (`z=fill/hug`); `name` i `description` jsou jednořádkové se zkrácením (`trunc1`).
- Vodorovné odsazení kořene je nesouměrné: vpravo 6, vlevo 8, svislé 0; výška kořene 48 při nejvyšším potomku 32 px (kořen je `fix/fix`, výška je pevná).
- Popis je uveden tak, jak ho vrací vlastnost `description` plugin API, včetně entit `&quot;`; ve Figmě jsou v popisu skutečné uvozovky.

### Collection row · `89:1367`

Sada 4 variant · výchozí varianta `State=Default` · rozměr 324×52

Popis ve Figmě: „A tool collection (task-oriented bundle of tools) in the Collections card. Meta lists the tools it contains.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Name | text | **Prepare for sharing** |
| Meta | text | **3 tools · Compress, Sanitize, Sign** |
| Icon | záměna instance | **icon/layers** |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'State=Default' 324x52 lay=H gap12 pad0,8,0,10 MIN/CENTER; z=fix/fix; rad=8
 F 'icon-circle' 32x32 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=bg/accent-subtle; rad=16; clip=1
  I 'icon' 16x16 z=fix/fix; ref=mainComponent:Icon; ic=icon/layers stroke=icon/accent w2
 F 'text' 236x30 lay=V gap1 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  T 'name' 236x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='Prepare for sharing'; ref=characters:Name
  T 'meta' 236x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='3 tools · Compress, Sanitize, Sign'; ref=characters:Meta
 I 'chevron' 14x14 z=fix/fix; ic=icon/chevron-right stroke=icon/muted w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `fill=bg/hover`; `chevron: ic=icon/chevron-right stroke=icon/default w2` |
| `State=Pressed` | kořen: `fill=bg/pressed`; `chevron: ic=icon/chevron-right stroke=icon/default w2` |
| `State=Focus` | kořen: `sil=0; st=border/focus w2 INSIDE` |

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ne ve variantě Focus (`sil=0`); ostatní varianty obrys nemají.
- Výchozí varianta nemá výplň ani obrys kořene (jen `rad=8`).
- **Změna:** Fokus: `border/focus`, 2 px, `INSIDE`, a obrys se už nepočítá do rozložení – obsah má ve Focus stejné polohy a šířky jako ve Default (`icon-circle` 10,10, `text` 236 px; dříve se `text` zúžil na 232 px a obsah se posunul o 2 px). Skrytá nenavázaná výplň `#ffffff` varianty Focus byla odstraněna; Focus je Default + kroužek.
- Není varianta Disabled.
- Kruh ikony (`icon-circle`, 32×32, `rad=16`) má ve všech stavech `fill=bg/accent-subtle` a ikonu s tahem `icon/accent`; na rozdíl od Tool row, Recent tool row a Workspace row to není instance Tool icon a nemá odstín (`Hue`). Rozměry odpovídají variantě Tool icon `Size=32` (kruh 32, poloměr 16, ikona 16, tah 2 px), ale dvojice barev `bg/accent-subtle` + `icon/accent` není žádný z odstínů Tool icon, proto rámec nahrazen nebyl (nahrazení by přebarvilo všechny instance na obrazovkách).
- `chevron` (icon/chevron-right, 14×14, tah `icon/muted`) není vázán na žádnou vlastnost – je vždy zobrazen a nelze jej zaměnit vlastností. **Změna 17. 10. 2026 (I-27, rozhodnutí 29):** ve stavech Hover a Pressed má tah `icon/default`, stejně jako Link row a Recent tool row (dříve zůstával `icon/muted`); Default a Focus `icon/muted`.
- Rozměry: `icon-circle` a `chevron` pevné, `text` vyplňuje zbytek šířky; `name` i `meta` jednořádkové se zkrácením (`trunc1`).
- Vodorovné odsazení kořene je nesouměrné: vpravo 8, vlevo 10.

### Tool category header · `89:1424`

Sada 7 variant · výchozí varianta `Hue=Blue` · rozměr 393×61

Popis ve Figmě: „Tinted header of a tool category card on the Tools page. Hue = color of the category, the same as the badges of its tools and the Quick action tiles on Home: Organize Pages Amber, Create &amp; Convert Blue, Edit Green, Protect &amp; Sign Red, Optimize Violet, Review &amp; Inspect Teal.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Title | text | **Pages** |
| Description | text | **Merge, split, reorder and rotate pages** |
| Count | text | **6 tools** |
| Icon | záměna instance | **icon/grid** |
| Hue | varianta | **Blue**, Violet, Amber, Red, Teal, Green, Neutral |

**Stavba výchozí varianty**

```text
C 'Hue=Blue' 393x61 lay=H gap12 pad12,16,12,16 MIN/CENTER; fill=accent/blue/bg; st=accent/blue/border w0,0,1,0 INSIDE
 F 'icon-circle' 36x36 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=bg/surface; rad=18; clip=1
  I 'icon' 18x18 z=fix/fix; ref=mainComponent:Icon; ic=icon/grid stroke=accent/blue/icon w2
 F 'text' 261x35 lay=V gap2 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  T 'title' 261x18 z=fill/hug; fill=text/primary; f=Inter Bold 15/auto LT HEIGHT trunc1; tx='Pages'; ref=characters:Title
  T 'description' 261x15 z=fill/hug; fill=text/tertiary; f=Inter Regular 12/auto LT HEIGHT trunc1; tx='Merge, split, reorder and rotate pages'; ref=characters:Description
 T 'count' 40x15 z=hug/hug; fill=text/tertiary; f=Inter Medium 12/auto LT WIDTH_AND_HEIGHT; tx='6 tools'; ref=characters:Count
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Hue=<hue>` (hue = Violet, Amber, Red, Teal, Green, Neutral; ověřeno u všech šesti) | kořen: `fill=accent/<hue>/bg; st=accent/<hue>/border w0,0,1,0 INSIDE`; `icon-circle>icon`: `ic=icon/grid stroke=accent/<hue>/icon w2` (název tokenu malými písmeny) |

**Poznámky**

- Kořen: velikost `fix/hug`, ořez obsahu ne, obrys se počítá do rozložení ano (ve všech variantách).
- Žádné stavy – záhlaví není interaktivní (sada má jen osu `Hue`).
- Obrys kořene je jen dole: `w0,0,1,0` (horní, pravý, dolní, levý), `INSIDE`, token `accent/<hue>/border`.
- Kořen nemá zaoblení rohů.
- `icon-circle` (36×36, `rad=18`) má ve všech odstínech `fill=bg/surface`; s odstínem se mění jen tah ikony.
- Rozměry: `icon-circle` pevný, `count` podle textu (`hug/hug`, bez zkrácení), `text` vyplňuje zbytek; `title` i `description` jednořádkové se zkrácením (`trunc1`).
- Výška 61 = 12 + 36 + 12 + 1: dolní obrys je započten do rozložení (`strokesIncludedInLayout` je zapnuto) a výška kořene je podle obsahu (`fix/hug`).

### Tool shortcut · `192:836`

Sada 4 variant · výchozí varianta `State=Default` · rozměr 180×48

Popis ve Figmě: „Pinned tool in the Tools panel of the document toolbar. The badge (icon and hue) is the same as in the Tools catalog.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Name | text | **Merge PDF** |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'State=Default' 180x48 lay=H gap10 pad8,12,8,8 MIN/CENTER; z=fix/fix; fill=bg/surface; st=border/default w1 INSIDE; rad=10
 I 'badge' 32x32 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=accent/blue/bg; rad=16; inst=Tool icon{Hue=Blue, Size=32} [Icon=icon/grid] ov:name; exp=1
 T 'label' 116x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='Merge PDF'; ref=characters:Name
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `fill=accent/neutral/bg-hover; st=border/strong w1 INSIDE` |
| `State=Pressed` | kořen: `fill=accent/neutral/bg-pressed; st=border/strong w1 INSIDE` |
| `State=Focus` | kořen: `lay=H gap10 pad9,13,9,9 MIN/CENTER; st=border/focus w2 OUTSIDE` |

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ano (ve všech variantách).
- Nové (oprava F-45): Hover má výplň `accent/neutral/bg-hover` a Pressed `accent/neutral/bg-pressed` (dříve `accent/neutral/bg` a `accent/neutral/bg-hover`) – stejné kroky jako Resource card, Folder card, Quick action tile a Action tile. Výplň `bg/surface` stavu Default zůstala. Oba stavy jsou tím o jeden krok tmavší než dřív.
- Fokus: `border/focus`, 2 px, `OUTSIDE`; nahrazuje obrys `border/default w1 INSIDE`, výplň `bg/surface` zůstává.
- Nové (pravidlo stejného rozměru stavů): varianta Focus má potomky na stejných místech jako Default (`badge` 9, 8; `label` 116×16 na 51, 16). Dříve byl `badge` o 1 px vlevo a `label` o 2 px širší (118), protože obrys `OUTSIDE` se do rozložení nepočítá; Focus to vyrovnává výplněmi o 1 px většími (9, 13, 9, 9 místo 8, 12, 8, 8).
- `badge` je vnořená instance `Tool icon{Hue=Blue, Size=32}` s ikonou `icon/grid`; je vystavená (`exp=1`), `Hue`, `Size` a `Icon` odznaku se nastavují na instanci Tool shortcut (popis: odznak je stejný jako v katalogu toolů).
- Není varianta Disabled.

### Tool choice · `417:1006`

Sada 8 variant · výchozí varianta `Selected=False, State=Default` · rozměr 154×48

Popis ve Figmě: „One suggested tool in the &quot;Then continue with&quot; choice of a result dialog. Selected = the tool starts on the result after the current tool finishes successfully. Click again to unselect. The selected card is marked by a 2 px ring of bg/accent inside and by the fill bg/accent-subtle over bg/surface (as Color mode card), not by the focus color. State: Hover and Pressed lay bg/hover and bg/pressed over the fill, Focus adds the layer &quot;focus-ring&quot; (2 px border/focus outside), so a selected and focused card shows both rings.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Name | text | **Add Page Numbers** |
| Selected | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Selected=False, State=Default' 154x48 lay=H gap8 pad8,10,8,10 MIN/CENTER; z=fix/hug; fill=bg/surface; st=border/default w1 INSIDE; rad=10
 I 'tool-icon' 28x28 lay=H gap0 pad7,7,7,7 CENTER/CENTER; z=hug/hug; fill=accent/blue/bg; rad=14; inst=Tool icon{Hue=Blue, Size=28} [Icon=icon/grid] ov:name; exp=1
 T 'name' 96x30 z=fill/hug; fill=text/primary; f=Inter Medium 12/auto LT HEIGHT; tx='Add Page Numbers'; ref=characters:Name
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=True, State=Default` | kořen: `lay=H gap8 pad9,11,9,11 MIN/CENTER; fill=bg/surface+bg/accent-subtle; st=bg/accent w2 INSIDE; sil=0`; `name`: `s=72x30`; `+ check: I 16x16 z=fix/fix; ic=icon/check stroke=icon/accent w2` |
| `Selected=False, State=Hover` | kořen: `fill=bg/surface+bg/hover` |
| `Selected=True, State=Hover` (proti `Selected=True, State=Default`) | kořen: `fill=bg/surface+bg/accent-subtle+bg/hover` |
| `Selected=False, State=Pressed` | kořen: `fill=bg/surface+bg/pressed` |
| `Selected=True, State=Pressed` (proti `Selected=True, State=Default`) | kořen: `fill=bg/surface+bg/accent-subtle+bg/pressed` |
| `Selected=False, State=Focus` | `+ focus-ring: F 154x48 at=0,0 abs; con=STRETCH/STRETCH; z=fix/fix; st=border/focus w2 OUTSIDE; rad=10` |
| `Selected=True, State=Focus` (proti `Selected=True, State=Default`) | `+ focus-ring: F 154x48 at=0,0 abs; con=STRETCH/STRETCH; z=fix/fix; st=border/focus w2 OUTSIDE; rad=10` |

**Poznámky**

- Kořen: velikost `fix/hug`, ořez obsahu ne, obrys se počítá do rozložení ano u `Selected=False` (1 px `INSIDE`) a ne u `Selected=True` (`sil=0`, 2 px `INSIDE`; okraje 9,11,9,11 nahrazují 8,10,8,10 + 1 px obrysu). Obsah má proto ve všech 8 variantách stejné polohy: `tool-icon` 11,10, `name` 47,9, `check` 127,16; velikost 154×48.
- **Nové:** osa `State` (Default, Hover, Pressed, Focus) pro obě hodnoty `Selected`. Hover a Pressed kladou `bg/hover` a `bg/pressed` (poloprůhledné tokeny) jako druhou výplň nad dosavadní výplň (`bg/surface`, resp. `bg/accent-subtle`); obrys se nemění. **Změna 16. 10. 2026:** vybraná karta má pod poloprůhledným `bg/accent-subtle` také `bg/surface` (dříve jen `bg/accent-subtle`), aby měla vlastní plochu i na barevném pozadí, jako nevybraná (rozhodnutí 21). Na bílém pozadí se vzhled nemění. Totéž platí pro vybrané karty voleb v dialozích výsledku na obrazovkách toolů (`option-*`, 36 rámců), vybraného asistenta na *Main page* a vybranou buňku kotvy v Organize Pages.
- **Nové:** Focus = Default + vrstva `focus-ring`: absolutně umístěný rámec přes celou kartu (`STRETCH/STRETCH`), bez výplně, obrys `border/focus` 2 px `OUTSIDE`, `rad=10` (vnější poloměr 12). Nic jiného se nemění – odpovídá překryvnému `QFocusFrame`. Kroužek přesahuje kartu o 2 px; rodič ho nesmí ořezávat (v Continue with bylo proto vypnuto ořezávání rámce `choices`).
- **Změna:** výběr se už nekreslí barvou fokusu (`border/focus` 1 px), ale jako u Color mode card: obrys `bg/accent` 2 px `INSIDE` a výplň `bg/accent-subtle`. Vybraná karta s fokusem má oba kroužky: `bg/accent` uvnitř a `border/focus` vně (ve světlém režimu mají oba tokeny stejnou hodnotu `#1a73e8`, v tmavém se liší).
- Ve variantách `Selected=True` přibývá ikona `check` (icon/check, 16×16, tah `icon/accent`) jako třetí potomek; `name` se zúží z 96 na 72 px (16 + mezera 8).
- `name` nemá zkrácení (`HEIGHT` bez `trunc`) – text se zalamuje; ve výchozí variantě má výšku 30 px (dva řádky) při šířce 96 i 72 px.
- Výška karty obepíná obsah (`z=fix/hug`): 2 × 9 + větší z odznaku 28 a výšky názvu, tedy **46 px pro název na jeden řádek** (např. „Compress“, „Protect with Password“ při šířce 233 na obrazovce `418:32221`) a 48 px pro dva řádky. Knihovna počítá řádky při šířce názvu vybrané karty (72 při 154), aby se výška výběrem neměnila; nejmenší šířka karty nechá nejdelší slovo názvu celé (doplněno 17. 10. 2026 po kontrole kroku 4, F1 a F3).
- Vnořená komponenta: `tool-icon` = Tool icon{Hue=Blue, Size=28} [Icon=icon/grid]. **Změna:** instance už není zmenšena měřítkem (`scaleFactor = 1`), používá variantu `Size=28` (ikona 14×14, tah 2 px, `rad=14`). `tool-icon` je vystavená vnořená instance (`isExposedInstance = true`, všech 8 variant): `Hue` a `Icon` se nastavují na instanci Tool choice, proto pro ně komponenta vlastní vlastnost nemá; `Size` nechat 28.
- Není varianta Disabled.
- Rozměry: `tool-icon` má od 14. 10. 2026 (N-18) velikost `hug/hug` (varianta `Size=28`: okraje 7 a ikona 14, tedy stále 28×28; ověřeno ve Figmě 17. 10. 2026 při kontrole kroku 3), `name` vyplňuje zbytek šířky; v komponentě Continue with jsou instance roztaženy (`z=fill/fill`, 154.67×48).
- Sada je auto layout se zalamováním (šířka 356, mezery 16): sloupce `Selected`, řádky `State`.
- Popis je uveden tak, jak ho vrací vlastnost `description` plugin API, včetně entit `&quot;` (uvozovky); ve Figmě jsou v popisu skutečné uvozovky.

### Continue with · `417:1007`

Samostatná komponenta · výchozí varianta — (bez variant) · rozměr 480×93

Popis ve Figmě: „Choice of the next tool in a result dialog (tools.md §9.3 item 12). Up to three suggested tools; nothing is ever preselected. The chosen tool starts on the result only after the current tool finished successfully. Title and Title note are the texts of the header; the link &quot;link-another-tool&quot; is an exposed nested instance (set its Label); Show first, Show second and Show third hide the choices, so zero to three tools can be offered.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Show second | přepínač | **true** |
| Show third | přepínač | **true** |
| Caption | text | **Nothing is chosen – the tool just finishes.** |
| Title | text | **Then continue with** |
| Title note | text | **optional** |
| Show first | přepínač | **true** |

**Stavba výchozí varianty**

```text
C 'Continue with' 480x93 lay=V gap8 pad0,0,0,0 MIN/MIN; z=fix/hug
 F 'header' 480x16 lay=H gap6 pad0,0,0,0 MIN/CENTER; z=fill/hug; clip=1
  T 'Then continue with' 112x15 z=hug/hug; fill=text/primary; f=Inter Semi Bold 12/auto LT WIDTH_AND_HEIGHT; tx='Then continue with'; ref=characters:Title
  T 'optional' 42x13 z=hug/hug; fill=text/muted; f=Inter Regular 11/auto LT WIDTH_AND_HEIGHT; tx='optional'; ref=characters:Title note
  F 'spacer' 215x1 z=fill/fix; clip=1
  I 'link-another-tool' 93x16 lay=H gap0 pad0,2,0,2 MIN/MIN; z=hug/hug; rad=4; inst=Text link{State=Default} [Label='Another tool…'] ov:height,name,width; exp=1
 F 'choices' 480x48 lay=H gap8 pad0,0,0,0 MIN/MIN; z=fill/hug
  I 'choice-1' 154.67x48 lay=H gap8 pad8,10,8,10 MIN/CENTER; z=fill/fill; fill=bg/surface; st=border/default w1 INSIDE; rad=10; ref=visible:Show first; inst=Tool choice{Selected=False, State=Default} [Name='Add Page Numbers'] ov:counterAxisSizingMode,height,name,width,componentProperties,strokes; exp=1
  I 'choice-2' 154.67x48 lay=H gap8 pad8,10,8,10 MIN/CENTER; z=fill/fill; fill=bg/surface; st=border/default w1 INSIDE; rad=10; ref=visible:Show second; inst=Tool choice{Selected=False, State=Default} [Name='Compress'] ov:counterAxisSizingMode,height,name,width,componentProperties,strokes; exp=1
  I 'choice-3' 154.67x48 lay=H gap8 pad8,10,8,10 MIN/CENTER; z=fill/fill; fill=bg/surface; st=border/default w1 INSIDE; rad=10; ref=visible:Show third; inst=Tool choice{Selected=False, State=Default} [Name='Protect with Password'] ov:counterAxisSizingMode,height,name,width,componentProperties,strokes; exp=1
 T 'caption' 480x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT; tx='Nothing is chosen – the tool just finishes.'; ref=characters:Caption
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost `fix/hug`, ořez obsahu ne, obrys se počítá do rozložení ano (kořen sám obrys nemá).
- **Nové:** texty záhlaví jsou vlastnosti – `Title` (vrstva `Then continue with`) a `Title note` (vrstva `optional`).
- **Nové:** `link-another-tool` = Text link{State=Default} [Label='Another tool…'] je vystavená vnořená instance (`exp=1`): text odkazu se nastavuje jejím `Label`, stav jejím `State` na instanci Continue with. Vlastní textovou vlastnost pro odkaz komponenta nemá (vlastnost rodiče nelze ve Figmě navázat na vrstvu uvnitř vnořené instance).
- **Nové:** `choice-1` má přepínač viditelnosti `Show first` (výchozí true) jako `choice-2` (`Show second`) a `choice-3` (`Show third`). Lze tedy zobrazit 0 až 3 volby; zbývající volby se o šířku dělí rovným dílem (při dvou 236 px). Vlastnost pro skrytí celého bloku není.
- **Změna:** rámec `choices` už neořezává obsah (dříve `clip=1`), aby fokusový kroužek volby (2 px vně, viz Tool choice) nebyl oříznut. Vzhled se tím nemění.
- Vnořené komponenty: `choice-1`, `choice-2`, `choice-3` = Tool choice{Selected=False, State=Default} se jmény 'Add Page Numbers', 'Compress', 'Protect with Password'. Každá volba má ve vnořené instanci `tool-icon` přepsaný odstín a ikonu: `choice-1` = Tool icon{Hue=Green, Size=28} s ikonou icon/hash (tah `accent/green/icon`), `choice-2` = Tool icon{Hue=Violet, Size=28} s icon/minimize-2 (`accent/violet/icon`), `choice-3` = Tool icon{Hue=Red, Size=28} s icon/lock (`accent/red/icon`). **Změna:** odznaky jsou varianta `Size=28` bez měřítka – 28×28, `rad=14`, ikona 14×14 s tahem 2 px (dříve zmenšená instance s tahem 1,75 px a `rad=16`). Text `name` má v `choice-1` a `choice-3` 96.67×30 (dva řádky), v `choice-2` 96.67×15 (jeden řádek).
- `choice-1`, `choice-2` a `choice-3` jsou vystavené vnořené instance (`isExposedInstance = true`): jména nástrojů (`Name`), výběr (`Selected`) a nově stav (`State`) se nastavují na instanci Continue with, stejně jako `Hue` a `Icon` jejich odznaků `tool-icon`. Ve výchozím stavu jsou všechny tři volby `Selected=False` (popis: nic není předvybráno).
- Na instancích `choice-*` jsou přepsána pole `counterAxisSizingMode,height,name,width`. Další přepisy patří jejich vnořeným uzlům: `componentProperties` a `name` instanci `tool-icon` (odstín, velikost a ikona), `name` vnořené ikoně a `strokes` jejímu vektoru (barva tahu podle odstínu). Obrys instancí `choice-*` přepsán není (`border/default w1 INSIDE` jako v hlavní komponentě Tool choice).
- Rozložení: svislé, mezera 8, bez odsazení; `header`, `choices` i `caption` vyplňují šířku (`z=fill/hug`). Volby se dělí o šířku rovným dílem (`z=fill/fill`, 3 × 154.67 + 2 × 8 = 480); `spacer` (`z=fill/fix`, 215×1) odtlačuje odkaz k pravému okraji.
- `caption` nemá zkrácení (`HEIGHT` bez `trunc`) – delší text se zalamuje a výška komponenty roste.
- Výška: řádek `choices` obepíná volby (`z=fill/hug`) a volby ho vyplňují (`z=fill/fill`), je tedy vysoký jako nejvyšší volba při své šířce. Výchozí stav 480×93 má volby s dvouřádkovými názvy (48); když se všechny názvy vejdou na jeden řádek, má řádek 46 a komponenta **91 px** (10 z 12 instancí na *Tools - Organize Pages*, např. `418:32221` 474×91 se dvěma volbami 233×46). Bez voleb knihovna vynechá řádek i jeho mezeru (37 px, review O-10 a I-42). Doplněno 17. 10. 2026 po kontrole kroku 4 (I6).
- Text popisku vybrané volby podle [tools.md](tools.md) 9.3 bod 12 a instance `418:32494`: „Add Page Numbers starts on the new document after Merge finishes successfully.“ (název toolu, který výsledek vytvořil, dodá aplikace).
- Popis je uveden tak, jak ho vrací vlastnost `description` plugin API, včetně entit `&quot;`; ve Figmě jsou v popisu skutečné uvozovky.

### Filter chip · `78:836`

Sada 10 variant · výchozí varianta `Selected=False, State=Default` · rozměr 63×32

Popis ve Figmě: „Filter of a list (Recent list: All, Files, Folders, Workspaces, Pinned), a tab of a panel (Recognition / Image / Output) or an on / off option (Whole words, Regular expression). One chip of a filter row or of a tab row is selected; options are independent. The selected chip is bg/surface + bg/accent-subtle with text/accent and icon/accent, like an active Toolbar button (hover bg/accent-subtle-hover, pressed bg/accent-subtle-pressed); the full bg/accent fill belongs only to the primary button of the screen. An option that is on shows icon/check instead of its icon (the chip keeps its width). A single choice of a few options (presets, page range, alignment, Fit / 100 %) is not a row of chips but a segmented control (Segment). The count shows how many items the filter contains; Show count hides it. State=Disabled (a row of chips that cannot be used now, e.g. while OCR runs): label, count and icon text/disabled and icon/disabled, the selected chip is filled bg/muted, the unselected one keeps bg/surface and border/default; no opacity, the chips do not react to the pointer.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **All** |
| Count | text | **12** |
| Show count | přepínač | **true** |
| Show icon | přepínač | **false** |
| Icon | záměna instance | **icon/match-case** |
| Selected | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus, Disabled |

**Stavba výchozí varianty**

```text
C 'Selected=False, State=Default' 63x32 lay=H gap6 pad0,14,0,14 CENTER/CENTER; z=hug/fix; fill=bg/surface; st=border/default w1 INSIDE; rad=16
 I 'icon' 14x14 hid=1; z=fix/fix; ref=visible:Show icon,mainComponent:Icon; ic=icon/match-case fill=icon/default
 T 'label' 15x15 z=hug/hug; fill=text/secondary; f=Inter Semi Bold 12/auto LT WIDTH_AND_HEIGHT; tx='All'; ref=characters:Label
 T 'count' 12x13 z=hug/hug; fill=text/muted; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='12'; ref=visible:Show count,characters:Count
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=False, State=Hover` | kořen: `fill=bg/surface+bg/hover` |
| `Selected=False, State=Pressed` | kořen: `fill=bg/surface+bg/pressed` |
| `Selected=False, State=Focus` | kořen: `lay=H gap6 pad0,15,0,15 CENTER/CENTER; st=border/focus w2 OUTSIDE` |
| `Selected=False, State=Disabled` | `icon`: `fill=icon/disabled`; `label`: `fill=text/disabled`; `count`: `fill=text/disabled` |
| `Selected=True, State=Default` | kořen: `lay=H gap6 pad0,15,0,15 CENTER/CENTER; fill=bg/surface+bg/accent-subtle; st=-`; `icon`: `ic=icon/match-case fill=icon/accent`; `label`: `fill=text/accent`; `count`: `fill=text/accent` |
| `Selected=True, State=Hover` (proti `Selected=True, State=Default`) | kořen: `fill=bg/surface+bg/accent-subtle-hover` |
| `Selected=True, State=Pressed` (proti `Selected=True, State=Default`) | kořen: `fill=bg/surface+bg/accent-subtle-pressed` |
| `Selected=True, State=Focus` (proti `Selected=True, State=Default`) | kořen: `st=border/focus w2 OUTSIDE` |
| `Selected=True, State=Disabled` (proti `Selected=True, State=Default`) | kořen: `fill=bg/muted`; `icon`: `fill=icon/disabled`; `label`: `fill=text/disabled`; `count`: `fill=text/disabled` |

**Poznámky**

- Kořen: velikost `hug/fix`, ořez obsahu ne, obrys se počítá do rozložení ano (ve všech variantách).
- `icon` (icon/match-case, 14×14, skrytý ve všech 10 variantách, vázaný na `Show icon` a `Icon`): jeho jediný vektor nemá tah, jen výplň – je to plněná ikona; zápis `fill=` za `ic=` znamená výplň vektoru ikony. Výplň je `icon/default` ve čtyřech variantách `Selected=False` a `icon/accent` (do 16. 10. 2026 `icon/on-accent`) ve čtyřech variantách `Selected=True`; stavy Hover, Pressed a Focus ji nemění, obě varianty Disabled mají `icon/disabled`. Instance výplň vektoru přepisuje (hlavní komponenta icon/match-case má výplň `icon/default`). Skrytá instance vrací své potomky jen při běhu skriptu s `figma.skipInvisibleInstanceChildren = false`; hodnoty jsou přečteny takto.
- Nové (oprava F-60): vybraný čip má ve stavu Hover výplň `bg/accent-hover` a ve stavu Pressed `bg/accent-pressed` (dříve `accent/blue/icon` a `text/accent-pressed`). Hodnoty ve světlém režimu se nezměnily, v tmavém režimu jsou to tmavší modré (`#2563eb`, `#1d4ed8`), na kterých je text `icon/on-accent` čitelný.
- Nové (oprava F-25 a následná úprava 13. 10. 2026): všech osm variant (od 14. 10. 2026 deset i s Disabled) má stejný rozměr 63×32 i polohu potomků (dříve měly `Selected=False, State=Focus` a všechny čtyři `Selected=True` šířku 61). Obrys 1 px `INSIDE` se počítá do rozložení (`strokesIncludedInLayout` je zapnuto), obrys `OUTSIDE` stavu Focus ne; varianty bez obrysu `INSIDE` (`Selected=False, State=Focus` a čtyři `Selected=True`) to vyrovnávají vodorovnými okraji 15 místo 14. Ověřeno i se zobrazenou ikonou (83×32) a se skrytým počtem (65×32 se zobrazenou ikonou).
- Vybraný stav nemá obrys (`st=-`); má vodorovné okraje 15 místo 14, takže je stejně široký jako nevybraný (63×32) a výběrem se šířka čipu nemění. Šířka je dána obsahem (`label` a `count` jsou `hug/hug`, kořen `hug/fix`), výška 32 je pevná, `rad=16`.
- Fokus: `border/focus`, 2 px, `OUTSIDE`; ve variantě `Selected=False` nahrazuje obrys `border/default` a výplň `bg/surface` zůstává, ve variantě `Selected=True` zůstává výplň `bg/surface+bg/accent-subtle`.
- Hover a Pressed nevybraného čipu mění jen výplň, obrys `border/default w1 INSIDE` zůstává. **Změna 16. 10. 2026 (N-23, rozhodnutí 21):** Hover a Pressed kladou poloprůhledné `bg/hover` / `bg/pressed` jako druhou výplň nad `bg/surface`, ne místo ní, takže prvek má vlastní plochu i na `bg/sidebar` a `bg/subtle` (vzor Tool choice).
- Mřížka 2 × 5 je úplná. **Změna 14. 10. 2026 (N-19):** přibyl `State=Disabled` pro obě hodnoty `Selected`, bez krytí a se stejným rozměrem a okraji jako Default. Vybraný zakázaný chip má výplň `bg/muted` jako Button Primary, nevybraný si nechá `bg/surface` a obrys `border/default` jako Button Secondary. Na obrazovkách tak vypadají řady chipů a výběr stránek, které během běhu toolu nejdou použít (dříve krytí 0,5 celé řady).
- Texty nemají zkrácení (`WIDTH_AND_HEIGHT`) – čip roste s popiskem.
- **Změna 16. 10. 2026 (Q-9):** plná výplň `bg/accent` patří jen primárnímu tlačítku obrazovky. Vybraný chip má `bg/surface` + `bg/accent-subtle` (hover `-hover`, stisk `-pressed`), text a počet `text/accent`, ikonu `icon/accent` – jako Toolbar button `Active=True`; poznámka o opravě F-60 výše popisuje dřívější plnou výplň. Kroužek fokusu `border/focus` je na jemné výplni vidět. Chip slouží filtrům seznamů, záložkám panelů (Recognition / Image / Output, Settings / Review / Find, záložky Object Inspector, Color / Grayscale / Bitonal, Claude Code / Claude Desktop) a volbám zapnuto / vypnuto. Volba jedné z několika možností je segmentovaný přepínač (Segment), na obrazovkách převedeno asi 330 chipů.
- **Volby zapnuto / vypnuto (16. 10. 2026):** zapnutá volba (Whole words, Regular expression, Ignore soft hyphens, Sync zoom) má místo své ikony `icon/check` (tah `icon/accent`), šířka se nemění. Volba má vždy ikonu: Sync zoom dostal `icon/sync-view` pro vypnutý stav. V Qt: chip s dynamickou vlastností `option`.

### Tag · `116:2353`

Sada 4 variant · výchozí varianta `State=Default` · rozměr 44×21

Popis ve Figmě: „Topic tag on a tutorial card. Clicking filters the tutorials by that tag. The label has a fixed 13 px line height, so the tag is 21 px high for any character.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **OCR** |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'State=Default' 44x21 lay=H gap0 pad3,9,3,9 MIN/CENTER; z=hug/hug; fill=bg/subtle; st=border/default w1 INSIDE; rad=100
 T 'label' 24x13 z=hug/hug; fill=text/secondary; f=Inter Medium 11/13px LT WIDTH_AND_HEIGHT; tx='OCR'; ref=characters:Label
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `fill=bg/subtle+bg/hover` |
| `State=Pressed` | kořen: `fill=bg/subtle+bg/pressed` |
| `State=Focus` | kořen: `lay=H gap0 pad2,8,2,8 MIN/CENTER; st=border/focus w2 INSIDE` |

**Poznámky**

- Kořen: velikost `hug/hug`, ořez obsahu ne, obrys se počítá do rozložení ano (ve všech variantách).
- Fokus: `border/focus`, 2 px, `INSIDE`; nahrazuje obrys `border/default`, výplň `bg/subtle` zůstává.
- Nové (oprava F-25): varianta Focus má stejný rozměr jako Default (44×21, dříve 46×23) a text na stejném místě (10, 4). Obrys se počítá do rozložení; Focus má obrys o 1 px silnější a vyrovnává to výplněmi o 1 px menšími (2, 8, 2, 8 místo 3, 9, 3, 9): 44 = 9 + 24 + 9 + 2 × 1 = 8 + 24 + 8 + 2 × 2.
- Hover a Pressed mění jen výplň, obrys `border/default w1 INSIDE` zůstává. **Změna 17. 10. 2026 (I-25, rozhodnutí 27):** Hover a Pressed kladou poloprůhledné `bg/hover` / `bg/pressed` jako druhou výplň nad vlastní `bg/subtle` (dříve plné `bg/muted` / `bg/track`, hover měl proti klidu kontrast jen 1,02 / 1,03 : 1). Nově Hover 1,13 / 1,24 : 1, Pressed 1,25 / 1,46 : 1 (světlý / tmavý), stejně jako hover nevybraného Filter chipu; obrys se proto nezesiluje.
- Velikost podle textu (`label` `hug/hug`, bez zkrácení), `rad=100` (pilulka). **Změna 14. 10. 2026 (N-15):** výška řádku textu je pevných 13 px (dříve `auto`), štítek má proto výšku 21 bez ohledu na písmo znaku. Dříve měl štítek s jediným znakem „✂“, který Inter nemá a Figma ho kreslí náhradním písmem s řádkem 11 px, rozměr 31×19 (63 instancí na stránce *Tools - OCR*, F-67); teď má 31×21 jako ostatní. Ostatní štítky se nezměnily (Inter Medium 11 má automatický řádek 13 px). Widget nastaví výšku řádku 13 px.
- Není varianta Disabled ani vybraný stav (popis: kliknutí filtruje podle štítku).
- Komponenta je vnořena v Tool row (`tag`, Label 'Advanced'; není vystavená instance) a ve Video card (`tag-1`, `tag-2`; obě vystavené).
- Sada má auto layout (vodorovný) a po zmenšení varianty Focus se sama zmenšila z 274×71 na 272×69.

### Resource card · `50:242`

Sada 16 variant · výchozí varianta `Hue=Neutral, State=Default` · rozměr 116×124

Popis ve Figmě: „Learn &amp; Resources card (tutorials, manual, getting started). Hue marks the kind of resource: Violet = video tutorials, Green = documentation (user manual), Amber = getting started, Neutral = anything else. Cards in a row share width and height.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Title | text | **User Manual** |
| Description | text | **Detailed documentation** |
| Icon | záměna instance | **icon/book-open** |
| Hue | varianta | **Neutral**, Green, Amber, Violet |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Hue=Neutral, State=Default' 116x124 lay=V gap12 pad16,14,16,14 MIN/MIN; z=fix/hug; sil=0; fill=accent/neutral/bg; st=accent/neutral/border w1 INSIDE; rad=12
 F 'icon-wrap' 36x32 lay=V gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix
  I 'icon' 24x24 z=fix/fix; ref=mainComponent:Icon; ic=icon/book-open stroke=accent/neutral/icon w2
 F 'text' 88x48 lay=V gap4 pad0,0,0,0 MIN/MIN; z=fill/hug
  T 'title' 88x16 z=fill/hug; fill=text/primary; f=Inter Bold 13/auto LT HEIGHT; tx='User Manual'; ref=characters:Title
  T 'description' 88x28 z=fill/hug; fill=text/tertiary; f=Inter Regular 11/130% LT HEIGHT; tx='Detailed documentation'; ref=characters:Description
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Hue=<Hue>, State=Default` (Hue = Green, Amber, Violet; hue = green, amber, violet) | kořen: `fill=accent/<hue>/bg; st=accent/<hue>/border w1 INSIDE`; `icon-wrap>icon`: `ic=icon/book-open stroke=accent/<hue>/icon w2` |
| `Hue=<Hue>, State=Hover` (všechny 4 odstíny včetně Neutral; proti `Hue=<Hue>, State=Default`) | kořen: `fill=accent/<hue>/bg-hover; st=accent/<hue>/border-strong w1 INSIDE` |
| `Hue=<Hue>, State=Pressed` (všechny 4 odstíny včetně Neutral; proti `Hue=<Hue>, State=Default`) | kořen: `fill=accent/<hue>/bg-pressed; st=accent/<hue>/border-strong w1 INSIDE` |
| `Hue=<Hue>, State=Focus` (všechny 4 odstíny včetně Neutral; proti `Hue=<Hue>, State=Default`) | kořen: `st=border/focus w2 OUTSIDE` |

**Poznámky**

- Kořen: velikost `fix/hug`, ořez obsahu ne, obrys se počítá do rozložení ne (ve všech variantách; `sil=0`).
- Nové (oprava F-43, F-75): všechny tři odstíny včetně `Neutral` se kreslí jedním receptem, stejným jako `Quick action tile` a `Folder card`: Default výplň `accent/<hue>/bg`, obrys `accent/<hue>/border`, ikona `accent/<hue>/icon`; Hover `accent/<hue>/bg-hover` a obrys `accent/<hue>/border-strong`; Pressed `accent/<hue>/bg-pressed` a obrys `accent/<hue>/border-strong`; ikona se ve stavech nemění. Vzor byl ověřen u všech 12 variant; žádná výjimka. Stejně jsou nakreslené čtyři varianty odstínu Violet přidané 14. 10. 2026.
- Co se proti dřívějšku změnilo: obrys byl u všech odstínů obecný (`border/default`, při Hover a Pressed `border/strong`), nyní je z řady odstínu – Green a Amber tím dostaly barevný obrys. `Hue=Neutral` měl v Default a Focus výplň `bg/muted` (nyní `accent/neutral/bg`) a ikonu `icon/default` (nyní `accent/neutral/icon`; ve světlém režimu stejná hodnota, v tmavém světlejší); `accent/neutral/border` a `accent/neutral/border-strong` mají v obou režimech stejné hodnoty jako `border/default` a `border/strong`.
- **Změna 14. 10. 2026 (N-11):** osa `Hue` má čtyři hodnoty (Neutral, Green, Amber a nově Violet – varianty `773:147242`, `773:147249`, `773:147256`, `773:147263` v řádku y = 468; sada má 584×616). Violet je odstín videotutoriálů: karta „Video Tutorials“ na *Main page* (Home) je instancí `Hue=Violet`; dříve to byla `Hue=Neutral` s ručně přepsanou výplní a ikonou. Odstíny Blue, Red a Teal tu nejsou.
- Fokus: `border/focus`, 2 px, `OUTSIDE`; nahrazuje obrys odstínu, výplň varianty Default daného odstínu zůstává, rozměr se nemění.
- Texty (`text/primary`, `text/tertiary`) se s odstínem ani stavem nemění.
- Mřížka 4 × 4 je úplná; není varianta Disabled.
- Obrys není započten do rozložení (`strokesIncludedInLayout` je vypnuto): `text` má šířku 88 = 116 − 14 − 14 a výška 124 = 16 + 32 + 12 + 48 + 16.
- Popis je uveden tak, jak ho vrací vlastnost `description` plugin API, včetně entity `&amp;`.

### Highlight card · `103:1894`

Sada 7 variant · výchozí varianta `Hue=Blue` · rozměr 259×137

Popis ve Figmě: „Release highlight on the What&#39;s New page. Not interactive; Hue groups highlights by area.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Title | text | **One app for everything** |
| Description | text | **Editor, Viewer, PageMaster and Diff are now a single application.** |
| Icon | záměna instance | **icon/grid** |
| Hue | varianta | **Blue**, Violet, Amber, Red, Teal, Green, Neutral |

**Stavba výchozí varianty**

```text
C 'Hue=Blue' 259x137 lay=V gap10 pad16,16,16,16 MIN/MIN; fill=bg/surface; st=border/default w1 INSIDE; rad=12
 F 'icon-circle' 40x40 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=accent/blue/bg; rad=20; clip=1
  I 'icon' 20x20 z=fix/fix; ref=mainComponent:Icon; ic=icon/grid stroke=accent/blue/icon w2
 F 'text' 225x53 lay=V gap4 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  T 'title' 225x17 z=fill/hug; fill=text/primary; f=Inter Bold 14/auto LT HEIGHT; tx='One app for everything'; ref=characters:Title
  T 'description' 225x32 z=fill/hug; fill=text/tertiary; f=Inter Regular 12/130% LT HEIGHT; tx='Editor, Viewer, PageMaster and Diff are no…'; ref=characters:Description
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Hue=<hue>` (hue = Violet, Amber, Red, Teal, Green, Neutral; ověřeno u všech šesti) | `icon-circle`: `fill=accent/<hue>/bg`; `icon-circle>icon`: `ic=icon/grid stroke=accent/<hue>/icon w2` (název tokenu malými písmeny) |

**Poznámky**

- Kořen: velikost `fix/hug`, ořez obsahu ne, obrys se počítá do rozložení ano (ve všech variantách).
- Není interaktivní (uvádí to i popis) – žádné stavy; sada má jen osu `Hue`.
- S odstínem se mění jen kruh ikony a tah ikony; kořen má ve všech variantách `fill=bg/surface` a `st=border/default w1 INSIDE`.
- Obrys je započten do rozložení (`strokesIncludedInLayout` je zapnuto): `text` má šířku 225 = 259 − 16 − 16 − 2 a výška 137 = 16 + 40 + 10 + 53 + 16 + 2.
- Texty nemají zkrácení (`HEIGHT` bez `trunc`) – zalamují se; `description` má řádkování 130 % a ve výchozí variantě výšku 32 px (dva řádky); `text` je `z=fill/hug`.
- `icon-circle` 40×40 pevný (`rad=20`), ikona 20×20.
- Popis je uveden tak, jak ho vrací vlastnost `description` plugin API, včetně entity `&#39;` (apostrof ve výrazu „What's New“). U komponenty `Text link` vrací `get_design_context` tentýž popis se skutečnými uvozovkami místo `&quot;`, entity jsou tedy podoba čtení přes `description`, ne znění popisu ve Figmě; u této komponenty popis jinou cestou čten nebyl.

### Issue row · `103:1931`

Sada 4 variant · výchozí varianta `State=Default` · rozměr 786×52

Popis ve Figmě: „A fixed GitHub issue in the release. Clicking opens the issue on GitHub. Status icon swaps the icon of the status circle (icon/check = fixed).“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Number | text | **#612** |
| Title | text | **Crash when opening some encrypted documents** |
| Description | text | **Documents protected with AES-256 and an empty user password now open correctly.** |
| Date | text | **Sep 12, 2026** |
| Status icon | záměna instance | **icon/check** |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'State=Default' 786x52 lay=H gap12 pad0,10,0,10 MIN/CENTER; z=fix/fix; rad=8
 F 'status' 24x24 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=accent/green/bg; rad=12; clip=1
  I 'icon' 14x14 z=fix/fix; ref=mainComponent:Status icon; ic=icon/check stroke=accent/green/icon w2
 T 'number' 44x16 z=fix/hug; fill=text/accent; f=Inter Semi Bold 13/auto LT HEIGHT; tx='#612'; ref=characters:Number
 F 'text' 593x30 lay=V gap1 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  T 'title' 593x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='Crash when opening some encrypted documents'; ref=characters:Title
  T 'description' 593x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='Documents protected with AES-256 and an em…'; ref=characters:Description
 T 'date' 69x13 z=hug/hug; fill=text/muted; f=Inter Regular 11/auto LT WIDTH_AND_HEIGHT; tx='Sep 12, 2026'; ref=characters:Date
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `fill=bg/hover` |
| `State=Pressed` | kořen: `fill=bg/pressed` |
| `State=Focus` | kořen: `sil=0; st=border/focus w2 INSIDE` |

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ne ve variantě Focus (`sil=0`); ostatní varianty obrys nemají.
- Výchozí varianta nemá výplň ani obrys kořene (jen `rad=8`).
- **Změna:** Fokus: `border/focus`, 2 px, `INSIDE`, a obrys se už nepočítá do rozložení – obsah má ve Focus stejné polohy a šířky jako ve Default (`status` 10,14, `number` 46,18, `text` 593 px, `date` 707; dříve se `text` zúžil na 589 px a obsah se posunul o 2 px). Skrytá nenavázaná výplň `#ffffff` varianty Focus byla odstraněna; Focus je Default + kroužek.
- Není varianta Disabled.
- **Nové:** ikona stavového kruhu je vlastnost `Status icon` (záměna instance, výchozí `icon/check`), navázaná na vrstvu `status>icon` ve všech 4 variantách. Kruh `status` (24×24, `fill=accent/green/bg`) a barva tahu ikony (`accent/green/icon`) zůstávají pevné – sada nemá osu ani odstín pro jiný stav problému; u ikony z více vektorů přebírá zelený tah po záměně jen první vektor.
- Popisek stavu (text) v komponentě není – stav vyjadřuje jen ikona v kruhu; žádná textová vrstva k navázání neexistuje.
- `number` má pevnou šířku 44 px (`z=fix/hug`) a nemá zkrácení (`HEIGHT` bez `trunc`) – delší číslo by se zalomilo; barva `text/accent`.
- `date` má velikost podle textu (`hug/hug`), `text` vyplňuje zbytek; `title` i `description` jednořádkové se zkrácením (`trunc1`).

### Version row · `103:1950`

Sada 2 variant · výchozí varianta `Current=True` · rozměr 324×60

Popis ve Figmě: „One release in the Version history timeline. Current=True marks the installed version; the page hides the rail line on the last row with Show line = false.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Version | text | **v1.5.3** |
| Date | text | **Sep 14, 2026** |
| Summary | text | **One unified app, faster loading, timestamped signatures.** |
| Show line | přepínač | **true** |
| Current | varianta | **True**, False |

**Stavba výchozí varianty**

```text
C 'Current=True' 324x60 lay=H gap12 pad0,8,0,8 MIN/MIN; z=fix/hug
 F 'rail' 12x60 lay=V gap4 pad4,0,0,0 MIN/CENTER; z=fix/fill; clip=1
  E 'dot' 10x10 z=fix/fix; fill=bg/accent
  F 'line' 2x42 z=fix/fill; fill=bg/track; clip=1; ref=visible:Show line
 F 'text' 284x60 lay=V gap2 pad0,0,14,0 MIN/MIN; z=fill/hug; clip=1
  F 'top' 284x16 lay=H gap8 pad0,0,0,0 SPACE_BETWEEN/CENTER; z=fill/hug; clip=1
   T 'version' 37x16 z=hug/hug; fill=text/accent; f=Inter Semi Bold 13/auto LT WIDTH_AND_HEIGHT; tx='v1.5.3'; ref=characters:Version
   T 'date' 69x13 z=hug/hug; fill=text/muted; f=Inter Regular 11/auto LT WIDTH_AND_HEIGHT; tx='Sep 14, 2026'; ref=characters:Date
  T 'summary' 284x28 z=fill/hug; fill=text/tertiary; f=Inter Regular 11/14px LT HEIGHT; tx='One unified app, faster loading, timestamp…'; ref=characters:Summary
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Current=False` | `~ rail>dot: fill=border/strong`<br>`~ text>top>version: fill=text/primary` |

**Poznámky**

- Kořen: velikost `fix/hug`, ořez obsahu ne, obrys se počítá do rozložení ano (ve všech variantách).
- **Nové – přepínač `Show line`** (výchozí `true`) řídí v obou variantách viditelnost vrstvy `line` (čára kolejnice pod tečkou). Poslední řádek časové osy má `Show line = false`: čára zmizí, tečka zůstává na svém místě (y = 4) a rozměr řádku se nemění (výšku určuje text). Dříve se čára skrývala přepsáním viditelnosti v instanci; takové přepsání Figma po navázání vrstvy na vlastnost ukazuje jako `Show line = false` (instance `version-v1.5.0` `104:2264` v seznamu `versions-list` na stránce Main page).
- Žádné stavy (Hover, Pressed, Focus, Disabled) – sada má jen osu `Current`; kořen nemá výplň (skrytá výplň `#ffffff` bez proměnné byla 13. 10. 2026 odstraněna), obrys ani zaoblení.
- Výchozí varianta je `Current=True` (nainstalovaná verze): tečka `bg/accent`, verze `text/accent`; v `Current=False` má tečka `fill=border/strong` (token obrysu použitý jako výplň) a verze `text/primary`.
- `rail` (12 px široký, `z=fix/fill`) sahá přes celou výšku řádku: odsazení nahoře 4, tečka 10×10, mezera 4, čára 2 px široká vyplňuje zbytek (`z=fix/fill`, 42 px při výšce 60).
- Výška je dána textem (kořen `fix/hug`): `text` je `z=fill/hug` s dolním odsazením 14 (16 + 2 + 28 + 14 = 60); `summary` nemá zkrácení (`HEIGHT` bez `trunc`, ve výchozí variantě dva řádky) – delší text řádek zvýší. **Změna 17. 10. 2026 (I-28, rozhodnutí 30):** výška řádku shrnutí je pevných 14 px (dříve 130 % z 11 px = 14,3 px, přitom pole dvou řádků mělo 28 px); rozměry se nezměnily, řádek s jedním řádkem shrnutí má 46 px.
- `top`: `version` vlevo a `date` vpravo (`SPACE_BETWEEN`), oba podle textu.

### Video card · `116:2418`

Sada 4 variant · výchozí varianta `State=Default` · rozměr 272×258

Popis ve Figmě: „A video tutorial on the Support page. The thumbnail area is a placeholder for the cached YouTube thumbnail; the whole card opens the video. Tags are exposed instances and filter the list when clicked.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Title | text | **Getting started with PDF4QT** |
| Description | text | **A quick tour of the interface and the main features.** |
| Duration | text | **2:45** |
| Show tag 2 | přepínač | **true** |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'State=Default' 272x258 lay=V gap0 pad0,0,0,0 MIN/MIN; z=fix/hug; fill=bg/surface; st=border/default w1 INSIDE; rad=12; clip=1
 F 'thumbnail' 270x150 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fill/fix; fill=bg/muted; st=border/divider w0,0,1,0 INSIDE; clip=1
  F 'play' 44x44 lay=H gap0 pad0,0,0,4 CENTER/CENTER; z=fix/fix; fill=bg/scrim; rad=22; clip=1
   F 'triangle' 16x16 z=fix/fix; clip=1
    V 'Vector' 11.5x13 at=3,1.5; con=SCALE/SCALE; z=fix/fix; fill=icon/on-accent
  F 'duration' 38x17 at=224,125 abs; con=MAX/MAX; lay=H gap0 pad2,7,2,7 MIN/MIN; z=hug/hug; fill=bg/scrim; rad=6; clip=1
   T 'label' 24x13 z=hug/hug; fill=icon/on-accent; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='2:45'; ref=characters:Duration
 F 'body' 270x106 lay=V gap10 pad12,12,12,12 MIN/MIN; z=fill/hug; clip=1
  F 'text' 246x51 lay=V gap3 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
   T 'title' 246x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='Getting started with PDF4QT'; ref=characters:Title
   T 'description' 246x32 z=fill/hug; fill=text/tertiary; f=Inter Regular 12/135% LT HEIGHT trunc2; tx='A quick tour of the interface and the main…'; ref=characters:Description
  F 'tags' 246x21 lay=H gap6 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
   I 'tag-1' 44x21 lay=H gap0 pad3,9,3,9 MIN/CENTER; z=hug/hug; fill=bg/subtle; st=border/default w1 INSIDE; rad=100; inst=Tag{State=Default} [Label='OCR'] ov:name; exp=1
   I 'tag-2' 55x21 lay=H gap0 pad3,9,3,9 MIN/CENTER; z=hug/hug; fill=bg/subtle; st=border/default w1 INSIDE; rad=100; ref=visible:Show tag 2; inst=Tag{State=Default} [Label='Basics'] ov:height,name,width; exp=1
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `fill=bg/subtle; st=border/strong w1 INSIDE` |
| `State=Pressed` | kořen: `fill=bg/muted; st=border/strong w1 INSIDE` |
| `State=Focus` | kořen: `lay=V gap0 pad1,1,1,1 MIN/MIN; st=border/focus w2 OUTSIDE; sil=0` |

**Poznámky**

- Kořen: velikost `fix/hug`, ořez obsahu ano, obrys se počítá do rozložení ano ve variantách Default, Hover a Pressed a ne ve variantě Focus (`sil=0`, místo toho výplně 1 px ze všech stran).
- Fokus: `border/focus`, 2 px, `OUTSIDE` (**změna 14. 10. 2026**, rozhodnutí Q-2: volně stojící karta má kroužek vně jako tlačítka, dříve `INSIDE`); nahrazuje obrys `border/default`, výplň `bg/surface` zůstává.
- Nové (oprava F-25): varianta Focus má stejný rozměr jako Default (272×258, dříve 272×260) a všechny potomky na stejných místech a ve stejných rozměrech (`thumbnail` 270×150 na 1, 1; `body` 270×106 na 1, 151; `title` 246 px – dříve 244). Obrys 2 px se ve variantě Focus do rozložení nepočítá a místo obrysu 1 px drží obsah výplně 1 px; obsah se neposouvá ani nezužuje. Od 14. 10. 2026 je kroužek vně, mezi ním a obsahem zůstává 1 px výplně `bg/surface` (dříve kroužek uvnitř překrýval krajní 1 px náhledu).
- Nové (oprava F-66): `thumbnail` vyplňuje vnitřní šířku (`z=fill/fix`, 270×150; dříve pevných 272 px, o 2 px víc než vnitřní šířka, přesah ořezával kořen). `play` je tím přesně uprostřed náhledu (dříve o 1 px vpravo) a `duration` je 8 px od skutečného pravého okraje náhledu (dříve 6 px od viditelného okraje). Výška náhledu 150 je pevná. V instanci jiné šířky se náhled přizpůsobí (na stránce Support 266,5 → náhled 264,5).
- Náhled nemá vlastní zaoblení; kořen ořezává obsah (`clipsContent = true` ve všech 4 variantách), rohy náhledu jsou tedy oříznuty zaoblením kořene `rad=12`.
- Náhled je zástupná plocha (`fill=bg/muted`, dolní obrys `border/divider w0,0,1,0 INSIDE`); pro obrázek náhledu není žádná vlastnost. Ve stavu Pressed má kořen stejnou výplň `bg/muted` jako náhled.
- `play`: kruh 44×44 (`rad=22`, `fill=bg/scrim`) uprostřed náhledu s levým odsazením 4; trojúhelník je vektor 11.5×13 s `fill=icon/on-accent`, není to instance ikony.
- `duration`: absolutně umístěný štítek (`at=224,125 abs`, `con=MAX/MAX` – ukotven vpravo dole, 8 px od pravého a 8 px od dolního okraje náhledu), `fill=bg/scrim`, `rad=6`; text má `fill=icon/on-accent` (token ikon použitý pro text).
- Vnořené komponenty: `tag-1` = Tag{State=Default} [Label='OCR'] (vždy zobrazen, bez vazby), `tag-2` = Tag{State=Default} [Label='Basics'] s viditelností vázanou na `Show tag 2`. Oba štítky jsou vystavené vnořené instance (`exp=1`, všechny 4 varianty): jejich `Label` a `State` se nastavují na instanci Video card, jak uvádí i popis, proto pro texty štítků komponenta vlastní vlastnost nemá; pro skrytí prvního štítku ani pro třetí štítek vlastnost není.
- `title` je jednořádkový (`trunc1`), `description` nejvýše dvouřádkový (`trunc2`, řádkování 135 %); `body` a `text` jsou `z=fill/hug`.
- Hover a Pressed mění výplň i obrys (`border/strong w1 INSIDE`). Není varianta Disabled.
- Výška: 258 = 1 + 150 + 106 + 1. Sada má auto layout (vodorovný) a po zmenšení varianty Focus se sama zmenšila z 1208×308 na 1208×306.

### Logo / PDF4QT · `110:2154`

Samostatná komponenta · výchozí varianta — (bez variant) · rozměr 130×130

Popis ve Figmě: „Application icon from PDF4QT_COLOR.svg. Fills bound to brand/logo-red and brand/logo-green. Scale the instance freely.“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Logo / PDF4QT' 130x130 
 V 'body' 111.27x106.97 at=18.23,11.03; con=SCALE/SCALE; z=fix/fix; fill=brand/logo-red
 V 'lines' 41.42x18.61 at=2,67.38; con=SCALE/SCALE; z=fix/fix; fill=brand/logo-red
 V 'corner' 24.74x30.74 at=104.76,11.04; con=SCALE/SCALE; z=fix/fix; fill=brand/logo-green
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost `fix/fix` (kořen nemá automatické rozložení, pevných 130×130), ořez obsahu ano, obrys se počítá do rozložení – nečteno (bez automatického rozložení se příznak neuplatní).
- Kořen nemá automatické rozložení, viditelnou výplň ani obrys; tři vektory jsou umístěny souřadnicemi a mají omezení `SCALE/SCALE` – instance se má volně škálovat (uvádí to i popis).
- Barvy jsou vázány na proměnné: `body` a `lines` mají `fill=brand/logo-red`, `corner` má `fill=brand/logo-green`. Žádná barva mimo proměnné; kořen nemá výplň (skrytá nevázaná výplň `#ffffff` byla 13. 10. 2026 odstraněna).
- Strom má čtyři uzly, nebyl zkrácen.
- Žádné vlastnosti, varianty ani stavy.

### Key value row · `110:2155`

Samostatná komponenta · výchozí varianta — (bez variant) · rozměr 360×28

Popis ve Figmě: „Label/value line for build information and project facts.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Key | text | **Build date** |
| Value | text | **Sep 14, 2026 14:32** |

**Stavba výchozí varianty**

```text
C 'Key value row' 360x28 lay=H gap12 pad0,10,0,10 MIN/CENTER; rad=6
 T 'key' 120x15 z=fix/hug; fill=text/tertiary; f=Inter Regular 12/auto LT HEIGHT; tx='Build date'; ref=characters:Key
 T 'value' 208x15 z=fill/hug; fill=text/primary; f=Inter Medium 12/auto RT HEIGHT trunc1; tx='Sep 14, 2026 14:32'; ref=characters:Value
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ano.
- Kořen má `rad=6`, ale žádnou výplň (skrytá nevázaná výplň `#ffffff` byla 13. 10. 2026 odstraněna) ani obrys – zaoblení se ve výchozí podobě neprojeví. Žádné stavy (Hover, Focus, Disabled).
- `key` má pevnou šířku 120 px (`z=fix/hug`) a nemá zkrácení (`HEIGHT` bez `trunc`) – delší klíč se zalomí.
- `value` vyplňuje zbytek šířky (`z=fill/hug`, 208 = 360 − 10 − 10 − 120 − 12), je zarovnán vpravo (`RT`) a jednořádkový se zkrácením (`trunc1`).
- Výška kořene 28 při výšce textu 15 px a svislém odsazení 0 (kořen je `fix/fix`, výška je pevná); potomci jsou svisle vystředěni (`MIN/CENTER`).

### Library row · `110:2158`

Samostatná komponenta · výchozí varianta — (bez variant) · rozměr 760×30

Popis ve Figmě: „One third-party library in the About page table: name, version, purpose, license.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Library | text | **Qt** |
| Version | text | **6.9.0** |
| Purpose | text | **Application framework** |
| License | text | **LGPL v3** |

**Stavba výchozí varianty**

```text
C 'Library row' 760x30 lay=H gap12 pad0,10,0,10 MIN/CENTER; rad=6
 T 'library' 150x15 z=fix/hug; fill=text/primary; f=Inter Semi Bold 12/auto LT HEIGHT; tx='Qt'; ref=characters:Library
 T 'version' 90x15 z=fix/hug; fill=text/secondary; f=Inter Regular 12/auto LT HEIGHT; tx='6.9.0'; ref=characters:Version
 T 'purpose' 354x15 z=fill/hug; fill=text/tertiary; f=Inter Regular 12/auto LT HEIGHT trunc1; tx='Application framework'; ref=characters:Purpose
 T 'license' 110x15 z=fix/hug; fill=text/secondary; f=Inter Regular 12/auto RT HEIGHT; tx='LGPL v3'; ref=characters:License
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ano.
- Kořen má `rad=6`, ale žádnou výplň (skrytá nevázaná výplň `#ffffff` byla 13. 10. 2026 odstraněna) ani obrys – zaoblení se ve výchozí podobě neprojeví. Žádné stavy (Hover, Focus, Disabled); není varianta pro záhlaví tabulky.
- Sloupce: `library` 150 px, `version` 90 px a `license` 110 px mají pevnou šířku (`z=fix/hug`), `purpose` vyplňuje zbytek (`z=fill/hug`, 354 = 760 − 10 − 10 − 150 − 90 − 110 − 3 × 12); mezera mezi sloupci 12.
- `license` je zarovnán vpravo (`RT`), ostatní sloupce vlevo.
- Zkrácení má jen `purpose` (`trunc1`); `library`, `version` a `license` jsou `HEIGHT` bez `trunc` – delší text se v pevné šířce zalomí.
- Výška kořene 30 při výšce textu 15 px a svislém odsazení 0 (kořen je `fix/fix`, výška je pevná); o 2 px vyšší než Key value row (28).

## 4. Formulářové prvky a Settings

### Checkbox · `144:2878`

Sada 10 variant · výchozí varianta `Checked=False, State=Default` · rozměr 68×18

Popis ve Figmě: „Checkbox with an optional label. Checked × State; Disabled is used when the option depends on another one.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **Enable** |
| Show label | přepínač | **true** |
| Checked | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus, Disabled |

**Stavba výchozí varianty**

```text
C 'Checked=False, State=Default' 68x18 lay=H gap8 pad0,0,0,0 MIN/CENTER
 F 'box' 18x18 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=bg/surface; st=border/strong w1.5 INSIDE; rad=5; clip=1
 T 'label' 42x16 z=hug/hug; fill=text/primary; f=Inter Regular 13/auto LT WIDTH_AND_HEIGHT; tx='Enable'; ref=visible:Show label,characters:Label
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Checked=False, State=Hover` | `box: fill=bg/surface+bg/hover; st=icon/muted w1.5 INSIDE` |
| `Checked=False, State=Pressed` | `box: fill=bg/surface+bg/pressed; st=icon/muted w1.5 INSIDE` |
| `Checked=False, State=Focus` | `box: st=border/focus w2 INSIDE` |
| `Checked=False, State=Disabled` | `box: fill=bg/muted; st=border/default w1.5 INSIDE`; `label: fill=text/disabled` |
| `Checked=True, State=Default` | `box: fill=bg/accent; st=-`; přidáno `box>check: I 12x12 z=fix/fix; ic=icon/check stroke=icon/on-accent w2` |
| `Checked=True, State=Hover` (proti `Checked=True, State=Default`) | `box: fill=bg/accent-hover` |
| `Checked=True, State=Pressed` (proti `Checked=True, State=Default`) | `box: fill=bg/accent-pressed` |
| `Checked=True, State=Focus` (proti `Checked=True, State=Default`) | `box: st=border/focus w2 OUTSIDE` |
| `Checked=True, State=Disabled` (proti `Checked=True, State=Default`) | `box: fill=border/strong`; `label: fill=text/disabled` |

**Poznámky**

- Kořen: velikost `hug/hug` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- Mřížka Checked × State je úplná (2 × 5 = 10 variant), včetně `Checked=True, State=Disabled`. Neexistuje třetí (neurčitý / tristate) stav.
- Focus ring: u `Checked=False` je `border/focus w2 INSIDE` (nahrazuje tah `border/strong w1.5`, výplň `bg/surface` zůstává), u `Checked=True` je `border/focus w2 OUTSIDE` (výplň `bg/accent` zůstává). Umístění tahu se mezi oběma hodnotami `Checked` liší (INSIDE × OUTSIDE).
- Tah pole má neceločíselnou tloušťku `w1.5` (Radio `292:1001` má `w1`).
- Hover u `False` klade `bg/hover` nad `bg/surface` a mění tah na `icon/muted`, u `True` mění výplň na `bg/accent-hover`, jako Button Primary. **Změna 16. 10. 2026 (N-25, rozhodnutí 23):** nezaškrtnuté pole má v Hover `bg/surface+bg/hover` a v Pressed `bg/surface+bg/pressed` s tahem `icon/muted w1.5 INSIDE` (dříve `bg/subtle` / `bg/muted` s `border/strong`); zaškrtnuté v Hover výplň `bg/accent-hover` bez tahu (dříve `bg/accent` s tahem `bg/accent-pressed w1.5`). Změna je vidět v obou režimech. Totéž kreslí stránka *Widgets*, sekce 03.
- **Změna (13. 10. 2026):** výplň `True, Pressed` pole je navázaná na `bg/accent-pressed` (dříve `text/accent-pressed`, token textu); tah `True, Hover` zmizel 16. 10. 2026 (rozhodnutí 23), Hover má výplň `bg/accent-hover`. Token použitý mimo svou skupinu zůstává `border/strong` jako výplň pole (`True, Disabled`).
- V `Checked=True, State=Disabled` se ikona `check` nemění (`stroke=icon/on-accent w2`), mění se jen výplň pole a barva textu.
- Velikost: `box` je pevný 18×18, `label` je `hug/hug` bez zkracování – šířka komponenty roste s textem (kořen je `hug/hug`).
- `Show label=false` skrývá vrstvu `label` (`ref=visible:Show label`); mezera `gap8` je vlastností kořene.
- Vnořené komponenty: jen ikona `icon/check` 12×12 (vrstva `check`, bez vazby na vlastnost).

### Radio · `292:1001`

Sada 10 variant · výchozí varianta `Checked=False, State=Default` · rozměr 67×18

Popis ve Figmě: „Radio button with an optional label. Checked × State; Disabled is used when the option depends on another one.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **Option** |
| Show label | přepínač | **true** |
| Checked | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus, Disabled |

**Stavba výchozí varianty**

```text
C 'Checked=False, State=Default' 67x18 lay=H gap8 pad0,0,0,0 MIN/CENTER; z=hug/hug
 F 'box' 18x18 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=bg/surface; st=border/strong w1 INSIDE; rad=9; clip=1
 T 'label' 41x16 z=hug/hug; fill=text/primary; f=Inter Regular 13/auto LT WIDTH_AND_HEIGHT; tx='Option'; ref=visible:Show label,characters:Label
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Checked=False, State=Hover` | `box: fill=bg/surface+bg/hover; st=icon/muted w1.5 INSIDE` |
| `Checked=False, State=Pressed` | `box: fill=bg/surface+bg/pressed; st=icon/muted w1.5 INSIDE` |
| `Checked=False, State=Focus` | `box: st=border/focus w2 INSIDE` |
| `Checked=False, State=Disabled` | `box: fill=bg/muted; st=border/default w1.5 INSIDE`; `label: fill=text/disabled` |
| `Checked=True, State=Default` | `box: fill=bg/accent; st=-`; přidáno `box>dot: E 8x8 z=fix/fix; fill=icon/on-accent` |
| `Checked=True, State=Hover` (proti `Checked=True, State=Default`) | `box: fill=bg/accent-hover` |
| `Checked=True, State=Pressed` (proti `Checked=True, State=Default`) | `box: fill=bg/accent-pressed` |
| `Checked=True, State=Focus` (proti `Checked=True, State=Default`) | `box: st=border/focus w2 OUTSIDE` |
| `Checked=True, State=Disabled` (proti `Checked=True, State=Default`) | `box: fill=border/strong`; `label: fill=text/disabled` |

**Poznámky**

- Kořen: velikost `hug/hug` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- Nové (oprava 13. 10. 2026, F-01 a F-82): vlastnost `State` s pěti hodnotami pro obě hodnoty `Checked` (dřívější varianty `Checked=False` a `Checked=True` jsou nyní `…, State=Default`, jejich kresba se nezměnila a zachovaly si identifikátory `292:994` a `292:997`), přepínač `Show label` a popis komponenty. Mřížka Checked × State je úplná (2 × 5 = 10 variant). Neexistuje třetí (neurčitý) stav.
- Stavy jsou nakreslené podle stránky *Widgets*, sekce 03, blok „New – radio button states“, a shodují se s kódem (`PDFWidgetStyleButtons::drawIndicator`): nezaškrtnuté pole má v klidu tah `border/strong w1`, v Hover a Pressed `icon/muted w1.5` (mění se tedy výplň, barva i tloušťka tahu), v Disabled `border/default w1.5`.
- Tah pole v klidu zůstal `w1` (Checkbox `144:2878` má `w1.5`) – stránka *Widgets* to výslovně potvrzuje („the Components radio uses 1 px – keep it“), F-51 proto u Radia beze změny.
- Focus ring: u `Checked=False` je `border/focus w2 INSIDE` (nahrazuje tah `border/strong w1`, výplň `bg/surface` zůstává), u `Checked=True` je `border/focus w2 OUTSIDE` (výplň `bg/accent` zůstává, tečka se nemění). Je to stejně jako u Checkboxu a jako v kódu (`PDFWidgetStyleImpl::getFocusRingShape`: zaškrtnutý indikátor má prstenec kolem pole). Kresba „On / Focus“ na stránce *Widgets* má prstenec `INSIDE`; ten by ve světlém režimu nebyl vidět (`border/focus` = `bg/accent` = `#1a73e8`), proto komponenta sleduje Checkbox a kód.
- Hover u `False` klade `bg/hover` nad `bg/surface` a tah zesílí na `icon/muted w1.5`, u `True` mění výplň na `bg/accent-hover`. **Změna 16. 10. 2026 (N-25, rozhodnutí 23):** nezaškrtnuté pole má v Hover `bg/surface+bg/hover` a v Pressed `bg/surface+bg/pressed` s tahem `icon/muted w1.5 INSIDE` (dříve `bg/subtle` / `bg/muted` s `border/strong`); zaškrtnuté v Hover výplň `bg/accent-hover` bez tahu (dříve `bg/accent` s tahem `bg/accent-pressed w1.5`). Změna je vidět v obou režimech. Totéž kreslí stránka *Widgets*, sekce 03.
- Ve všech stavech má `box` 18×18 a tečka `dot` leží na stejném místě (5,5 v poli) – tah `INSIDE` i `OUTSIDE` nemění rozměr ani polohu obsahu.
- **Změna (13. 10. 2026):** výplň `True, Pressed` pole je navázaná na `bg/accent-pressed` (dříve `text/accent-pressed`, token textu), stejně jako u Checkboxu; tah `True, Hover` zmizel 16. 10. 2026 (rozhodnutí 23), Hover má výplň `bg/accent-hover`. Token použitý mimo svou skupinu zůstává `border/strong` jako výplň pole (`True, Disabled`); tečka `dot` je elipsa 8×8 s výplní `icon/on-accent`.
- V `Checked=True, State=Disabled` se tečka nemění (`icon/on-accent`), mění se jen výplň pole a barva textu.
- Velikost: `box` pevný 18×18 (`rad=9` = kruh), `label` `hug/hug` bez zkracování – šířka komponenty roste s textem (kořen je `hug/hug`).
- `Show label=false` skrývá vrstvu `label` (`ref=visible:Show label`); mezera `gap8` je vlastností kořene. Vazby `visible:Show label` a `characters:Label` má vrstva `label` ve všech deseti variantách.
- Sada: rozměr 511×104 (dříve 214×66), auto layout se zalamováním – řádek `Checked=False` a řádek `Checked=True`, v každém pět stavů v pořadí Default, Hover, Pressed, Focus, Disabled.

### Combo box · `144:2899`

Sada 5 variant · výchozí varianta `State=Default` · rozměr 260×34

Popis ve Figmě: „Drop-down for a setting value. Fixed width in forms, value truncates.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Value | text | **Automatic detection** |
| State | varianta | **Default**, Hover, Pressed, Focus, Disabled |

**Stavba výchozí varianty**

```text
C 'State=Default' 260x34 lay=H gap8 pad0,10,0,12 MIN/CENTER; fill=bg/surface; st=border/default w1 INSIDE; rad=8
 T 'value' 214x16 z=fill/hug; fill=text/primary; f=Inter Regular 13/auto LT HEIGHT trunc1; tx='Automatic detection'; ref=characters:Value
 I 'chevron' 14x14 z=fix/fix; ic=icon/chevron-down stroke=icon/default w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `st=border/strong w1 INSIDE` |
| `State=Pressed` | kořen: `fill=bg/subtle; st=border/strong w1 INSIDE` |
| `State=Focus` | kořen: `lay=H gap8 pad0,9,0,11 MIN/CENTER; st=border/focus w2 INSIDE` |
| `State=Disabled` | kořen: `fill=bg/muted`; `value: fill=text/disabled`; `chevron: ic=icon/chevron-down stroke=icon/disabled w2` |

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- **Změna 14. 10. 2026 (N-02):** šipka `chevron` je v povolených stavech `icon/default` (dříve `icon/muted`), jak ji kreslí stránka *Widgets* (sekce 06) i styl; tlumená barva patří jen zakázanému stavu.
- Má stav Disabled. V něm se nemění tah (`border/default w1 INSIDE`); výplň kořene je `bg/muted`, text `text/disabled` a ikona `chevron` `icon/disabled` (**změna 14. 10. 2026**, rozhodnutí Q-8; dříve text `text/muted` a ikona beze změny `icon/muted`).
- Focus ring: `border/focus w2 INSIDE` na místě okraje, výplň `bg/surface` zůstává. **Změna (13. 10. 2026):** okraje kořene jsou ve Focus `pad0,9,0,11` místo `pad0,10,0,12` a vyrovnávají tah 2 px – `value` i `chevron` mají stejnou šířku a polohu jako v Default (`value` 214×16). Dříve byl `value` ve Focus o 2 px užší (212) a obsah o 1 px posunutý.
- Tah INSIDE se počítá do rozvržení: 260 − odsazení 22 − chevron 14 − mezera 8 − tah 2 × 1 = 214 = šířka textu `value`; ve Focus je odsazení 20 a tah 2 × 2, text má opět 214.
- Velikost: text `value` je `fill/hug` s `trunc1` (zkracuje se na jeden řádek), `chevron` pevný 14×14 – šířku určuje kořen (pevná šířka i výška), popis uvádí pevnou šířku ve formulářích.
- Hover mění jen tah (ne výplň) – Select `78:857` mění v Hover výplň; fokus kreslí oba stejně (`border/focus w2 INSIDE` na místě okraje, Select tak od 13. 10. 2026). Výška Combo boxu je 34, Selectu 32.
- Komponenta nemá variantu otevřeného seznamu ani vlastnost pro zástupný text (placeholder).
- Vnořené komponenty: jen ikona `icon/chevron-down` (bez vazby na vlastnost).

### Select · `78:857`

Sada 5 variant · výchozí varianta `State=Default` · rozměr 154×32

Popis ve Figmě: „Drop-down select (e.g. sort order of the Recent list: Last opened, Name, Path, Size, Reading progress). Focus draws the 2 px border/focus ring inside, in place of the border, and keeps the size of Default. State=Disabled: bg/muted fill and text/disabled value, as Combo box.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Prefix | text | **Sort:** |
| Value | text | **Last opened** |
| State | varianta | **Default**, Hover, Pressed, Focus, Disabled |

**Stavba výchozí varianty**

```text
C 'State=Default' 154x32 lay=H gap8 pad0,10,0,12 MIN/CENTER; z=hug/fix; fill=bg/surface; st=border/default w1 INSIDE; rad=8
 T 'prefix' 28x15 z=hug/hug; fill=text/muted; f=Inter Regular 12/auto LT WIDTH_AND_HEIGHT; tx='Sort:'; ref=characters:Prefix
 T 'value' 72x15 z=hug/hug; fill=text/secondary; f=Inter Semi Bold 12/auto LT WIDTH_AND_HEIGHT; tx='Last opened'; ref=characters:Value
 I 'icon' 14x14 z=fix/fix; ic=icon/chevron-down stroke=icon/default w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `fill=bg/surface+bg/hover` |
| `State=Pressed` | kořen: `fill=bg/surface+bg/pressed; st=border/strong w1 INSIDE` |
| `State=Focus` | kořen: `lay=H gap8 pad0,9,0,11 MIN/CENTER; st=border/focus w2 INSIDE` |
| `State=Disabled` | kořen: `fill=bg/muted`; `prefix: fill=text/disabled`; `value: fill=text/disabled`; `icon: ic=icon/chevron-down stroke=icon/disabled w2` |

**Poznámky**

- Kořen: velikost `hug/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- **Změna 14. 10. 2026 (N-02):** šipka `icon` je v povolených stavech `icon/default` (dříve `icon/muted`), stejně jako u Combo boxu.
- **Změna Focus:** kroužek je nově `border/focus w2 INSIDE` a nahrazuje obrys (dříve `OUTSIDE`), stejně jako u Combo boxu a Text fieldu; výplň `bg/surface` zůstává. Varianta má nově rozměr Default 154×32 (dříve 152×32) a děti jsou na stejných místech jako v Default (`prefix` 13,8.5, `value` 49,8.5, `icon` 129,9): tah 2 px se počítá do rozložení, proto má varianta vnitřní okraje o 1 px menší (vlevo 11, vpravo 9 místo 12 a 10). Widget tedy kreslí kroužek dovnitř obrysu pole a obsah neposouvá.
- **Nově stav Disabled** (`706:146738`): výplň `bg/muted`, `value` a `prefix` `text/disabled`, ikona `icon/disabled`; tah `border/default w1 INSIDE` se nemění – stejně jako Combo box `144:2899` (**změna 14. 10. 2026**, rozhodnutí Q-8; dříve `value` `text/muted`, `prefix` a ikona beze změny).
- Šířka: obsah 28 + 72 + 14, mezery 2 × 8, odsazení 12 + 10 a tah 2 × 1 = 154; ve Focus odsazení 11 + 9 a tah 2 × 2 = 154.
- Hover mění výplň (`bg/hover` nad `bg/surface`, N-23 od 16. 10. 2026; dříve `bg/hover` místo `bg/surface`), tah zůstává `border/default`; Combo box v Hover mění jen tah na `border/strong`. Pressed: `bg/pressed` (Combo box `bg/subtle`). Tento rozdíl i výška 32 zůstávají záměrně – je to kompaktní select stylu (vlastnost `select`, *Widgets* sekce 06).
- Velikost: oba texty jsou `hug/hug` bez zkracování, ikona pevná 14×14 – šířka komponenty roste s texty (kořen je na šířku `hug`), výška je pevná 32.
- Písmo 12 (prefix Regular, value Semi Bold), výška 32; Combo box má písmo 13 a výšku 34.
- Vrstva ikony se jmenuje `icon` (v Combo boxu `chevron`), bez vazby na vlastnost.
- Sada není auto layout: varianty jsou v řádku y = 24 s krokem 178 (x = 24, 202, 380, 558, 736); sada vyrostla ze 734×80 na 914×80.

### Spin box · `144:2932`

Sada 4 variant · výchozí varianta `State=Default` · rozměr 140×34

Popis ve Figmě: „Numeric field with stepper and optional unit suffix.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Value | text | **50** |
| Suffix | text | **px** |
| Show suffix | přepínač | **true** |
| State | varianta | **Default**, Hover, Focus, Disabled |

**Stavba výchozí varianty**

```text
C 'State=Default' 140x34 lay=H gap8 pad0,6,0,12 MIN/CENTER; fill=bg/surface; st=border/default w1 INSIDE; rad=8
 T 'value' 78x16 z=fill/hug; fill=text/primary; f=Inter Regular 13/auto LT HEIGHT trunc1; tx='50'; ref=characters:Value
 T 'suffix' 14x15 z=hug/hug; fill=text/muted; f=Inter Regular 12/auto LT WIDTH_AND_HEIGHT; tx='px'; ref=visible:Show suffix,characters:Suffix
 F 'stepper' 12x24 lay=V gap0 pad0,0,0,0 MIN/MIN; z=hug/hug; clip=1
  I 'up' 12x12 z=fix/fix; clip=1; ic=icon/chevron-up stroke=icon/default w2
  I 'down' 12x12 z=fix/fix; ic=icon/chevron-down stroke=icon/default w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `st=border/strong w1 INSIDE` |
| `State=Focus` | kořen: `lay=H gap8 pad0,5,0,11 MIN/CENTER; st=border/focus w2 INSIDE` |
| `State=Disabled` | kořen: `fill=bg/muted`; `value: fill=text/disabled`; `suffix: fill=text/disabled`; `up`, `down`: `stroke=icon/disabled w2` |

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- Má stav Disabled; nemá stav Pressed (Combo box `144:2899` jej má).
- **Změna 14. 10. 2026 (N-02):** šipky `up` a `down` jsou v povolených stavech `icon/default` (dříve `icon/muted`), stejně jako u Combo boxu.
- V Disabled se nemění tah; přípona `suffix` je `text/disabled` a ikony stepperu `icon/disabled` (**změna 14. 10. 2026**, rozhodnutí Q-8; dříve zůstávaly `text/muted` a `icon/muted`).
- Focus ring: `border/focus w2 INSIDE` na místě okraje, výplň `bg/surface` zůstává. **Změna (13. 10. 2026):** okraje kořene jsou ve Focus `pad0,5,0,11` místo `pad0,6,0,12` a vyrovnávají tah 2 px – `value`, `suffix` i `stepper` mají stejnou šířku a polohu jako v Default (`value` 78×16). Dříve byl `value` ve Focus o 2 px užší (76) a obsah o 1 px posunutý.
- Stepper nemá vlastní stavy: šipky `up` / `down` jsou holé instance ikon 12×12 bez pozadí, bez Hover / Pressed / Disabled jednotlivé šipky.
- Drobná nejednotnost: vrstva `up` má `clip=1`, vrstva `down` ne. Příčina je v hlavních komponentách ikon, ne v přepsání ve Spin boxu: `icon/chevron-up` má zapnutý ořez obsahu, `icon/chevron-down` ne (skrytá výplň `#ffffff` kořene `icon/chevron-up` byla 13. 10. 2026 odstraněna). Na kresbu to nemá vliv.
- Velikost: `value` je `fill/hug` s `trunc1`, `suffix` `hug/hug`, `stepper` `hug/hug` (12×24) – šířku určuje kořen (pevná šířka i výška).
- Šířka `value`: 140 − odsazení 18 − suffix 14 − stepper 12 − mezery 16 − tah 2 × 1 = 78 (tah INSIDE se počítá do rozvržení); ve Focus odsazení 16 a tah 2 × 2, tedy opět 78.
- Pravé odsazení je 6 (levé 12); ve Focus 5 a 11.
- `Show suffix=false` skrývá vrstvu `suffix`.

### Text field · `144:2941`

Sada 6 variant · výchozí varianta `State=Default` · rozměr 260×34

Popis ve Figmě: „Single-line text input for settings. Disabled shows the value that will be used. State=Error marks an invalid value with a 1 px text/danger border; the error message is drawn by the parent. State=Error focus is the invalid field with the focus: a 2 px text/danger ring inside.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Value | text | **Text** |
| State | varianta | **Default**, Hover, Focus, Disabled, Error, Error focus |

**Stavba výchozí varianty**

```text
C 'State=Default' 260x34 lay=H gap8 pad0,12,0,12 MIN/CENTER; z=fix/fix; fill=bg/surface; st=border/default w1 INSIDE; rad=8
 T 'value' 234x16 z=fill/hug; fill=text/primary; f=Inter Regular 13/auto LT HEIGHT trunc1; tx='Text'; ref=characters:Value
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `st=border/strong w1 INSIDE` |
| `State=Focus` | kořen: `lay=H gap8 pad0,11,0,11 MIN/CENTER; st=border/focus w2 INSIDE` |
| `State=Disabled` | kořen: `fill=bg/muted`; `value: fill=text/disabled` |
| `State=Error` | kořen: `st=text/danger w1 INSIDE` |
| `State=Error focus` | kořen: `lay=H gap8 pad0,11,0,11 MIN/CENTER; st=text/danger w2 INSIDE` |

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- **Nově stav Error** (`706:146714`): obrys `text/danger`, 1 px, `INSIDE`; výplň `bg/surface` a text `text/primary` zůstávají jako v Default, rozměr i poloha textu jsou stejné jako v Default (`value` 234×16 na 13,9). Kresba odpovídá stránce *Widgets*, sekce 04, blok „New – read-only, error, frameless“ (dynamická vlastnost `error`). Password field `225:870` kreslí chybu od 13. 10. 2026 stejným obrysem (`text/danger`, 1 px, `INSIDE`) a navíc přebarvuje ikonu zámku na `icon/danger`.
- Text chybové zprávy komponenta neobsahuje – kreslí ho rodič (Page selection `265:985` řádkem `error`; *Widgets* uvádí pomocný text Regular 12 `text/danger` 4 px pod polem).
- **Nově stav `Error focus`** (`773:147199`, 14. 10. 2026, N-14): chyba s fokusem – obrys `text/danger`, 2 px, `INSIDE`, s geometrií varianty Focus (okraje 11), jak ji kreslí *Widgets* (sekce 04). `Error` a `Focus` zůstávají hodnotami jedné vlastnosti `State` (F-18), kombinace je proto samostatnou hodnotou.
- Nemá stav Pressed (pole stiskem získá fokus).
- Má stav Disabled: mění se výplň kořene a barva textu, tah zůstává `border/default w1 INSIDE`.
- Focus ring: `border/focus w2 INSIDE` na místě okraje, výplň `bg/surface` zůstává. **Změna (13. 10. 2026):** vodorovné okraje kořene jsou ve Focus 11 místo 12 a vyrovnávají tah 2 px, který se počítá do rozložení – `value` má stejnou šířku i polohu jako v Default (234×16, x = 13). Dříve byl ve Focus o 2 px užší (232) a o 1 px posunutý vpravo.
- Komponenta nemá vlastnost ani variantu pro zástupný text (placeholder), pro kurzor, výběr textu ani pro ikonu / tlačítko uvnitř pole.
- Velikost: `value` je `fill/hug` s `trunc1` – šířku určuje kořen (pevná šířka i výška).
- Šířka `value`: 260 − odsazení 24 − tah 2 × 1 = 234 (tah INSIDE se počítá do rozvržení); ve Focus 260 − odsazení 22 − tah 2 × 2 = 234.
- Sada je auto layout (vodorovně, mezera 20, okraje 24); přidáním varianty Error vyrostla z 1148×82 na 1428×82 a variantou `Error focus` (14. 10. 2026) na 1708×82.

### Password field · `225:870`

Sada 18 variant · výchozí varianta `Content=Empty, State=Default` · rozměr 320×40

Popis ve Figmě: „Password input of the locked document screen. Content: Empty (placeholder), Masked, Visible (eye toggle). State=Error is shown after a wrong password. The focus ring is drawn inside, in place of the border. Disabled is a field that cannot be edited. Value is the text shown by Content=Visible. The eye toggle is an exposed instance of Icon button with its own states. State=Error focus is the wrong password with the focus: a 2 px text/danger ring inside.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Value | text | **Report-2025!** |
| Content | varianta | **Empty**, Masked, Visible |
| State | varianta | **Default**, Hover, Focus, Error, Disabled, Error focus |

**Stavba výchozí varianty**

```text
C 'Content=Empty, State=Default' 320x40 lay=H gap8 pad0,6,0,12 MIN/CENTER; z=fix/fix; fill=bg/surface; st=border/default w1 INSIDE; rad=8
 I 'lock' 16x16 z=fix/fix; ic=icon/lock stroke=icon/muted w2
 T 'value' 240x16 z=fill/hug; fill=text/muted; f=Inter Regular 13/auto LT HEIGHT trunc1; tx='Password'
 I 'toggle' 28x28 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=6; inst=Icon button{Size=28, State=Default} [Icon=icon/eye] ov:name,height,width,strokes; exp=1
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Content=Empty, State=Hover` | kořen: `st=border/strong w1 INSIDE` |
| `Content=Empty, State=Focus` | kořen: `lay=H gap8 pad0,5,0,11 MIN/CENTER; st=border/focus w2 INSIDE` |
| `Content=Empty, State=Disabled` | kořen: `fill=bg/muted`; `lock: ic=icon/lock stroke=icon/disabled w2`; `value: fill=text/disabled`; `toggle: inst=Icon button{Size=28, State=Disabled} [Icon=icon/eye] ov:name,height,width,strokes` |
| `Content=Empty, State=Error` | kořen: `st=text/danger w1 INSIDE`; `lock: ic=icon/lock stroke=icon/danger w2` |
| `Content=Masked, State=Default` | `value: fill=text/primary; f=Inter Regular 13/auto ls2px LT HEIGHT trunc1; tx='••••••••••••'`; `toggle: inst=Icon button{Size=28, State=Default} [Icon=icon/eye] ov:name,strokes` |
| `Content=Masked, State=Hover` (proti `Content=Masked, State=Default`) | kořen: `st=border/strong w1 INSIDE` |
| `Content=Masked, State=Focus` (proti `Content=Masked, State=Default`) | kořen: `lay=H gap8 pad0,5,0,11 MIN/CENTER; st=border/focus w2 INSIDE` |
| `Content=Masked, State=Disabled` (proti `Content=Masked, State=Default`) | kořen: `fill=bg/muted`; `lock: ic=icon/lock stroke=icon/disabled w2`; `value: fill=text/disabled`; `toggle: inst=Icon button{Size=28, State=Disabled} [Icon=icon/eye] ov:name,height,width,strokes` |
| `Content=Masked, State=Error` (proti `Content=Masked, State=Default`) | kořen: `st=text/danger w1 INSIDE`; `lock: ic=icon/lock stroke=icon/danger w2` |
| `Content=Visible, State=Default` | `value: fill=text/primary; tx='Report-2025!'; ref=characters:Value`; `toggle: inst=Icon button{Size=28, State=Default} [Icon=icon/eye-off] ov:name,strokes` |
| `Content=Visible, State=Hover` (proti `Content=Visible, State=Default`) | kořen: `st=border/strong w1 INSIDE` |
| `Content=Visible, State=Focus` (proti `Content=Visible, State=Default`) | kořen: `lay=H gap8 pad0,5,0,11 MIN/CENTER; st=border/focus w2 INSIDE` |
| `Content=Visible, State=Disabled` (proti `Content=Visible, State=Default`) | kořen: `fill=bg/muted`; `lock: ic=icon/lock stroke=icon/disabled w2`; `value: fill=text/disabled`; `toggle: inst=Icon button{Size=28, State=Disabled} [Icon=icon/eye-off] ov:name,height,width,strokes` |
| `Content=Visible, State=Error` (proti `Content=Visible, State=Default`) | kořen: `st=text/danger w1 INSIDE`; `lock: ic=icon/lock stroke=icon/danger w2` |
| `Content=<Content>, State=Error focus` (všechny tři hodnoty `Content`; proti `Content=<Content>, State=Default`) | kořen: `lay=H gap8 pad0,5,0,11 MIN/CENTER; st=text/danger w2 INSIDE`; `lock: ic=icon/lock stroke=icon/danger w2` |

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- **Změna (13. 10. 2026, rozhodnutí vlastníka, F-24):** kroužek fokusu je `INSIDE` a nahrazuje okraj (`border/focus w2 INSIDE`), stejně jako u Text field (`144:2941`); dřív byl `OUTSIDE`. Pole má ve Focus stále 320×40 a nepřesahuje ven.
- **Změna (stálá geometrie):** ve všech 15 variantách leží `lock` na x = 13, `value` na x = 37 se šířkou 240 a `toggle` na x = 285 (y = 6). Dřív byly děti ve Focus a Error o 1 px posunuté a `value` o 2 px širší. Dosaženo okraji kořene: Default / Hover / Disabled / Error `pad0,6,0,12` s tahem 1 px `INSIDE`; Focus `pad0,5,0,11` s tahem 2 px `INSIDE`. Vnitřní okraj obsahu od hrany pole je tedy vždy 13 vlevo a 7 vpravo.
- **Nové:** `State=Disabled` (tři varianty, podle Text field): výplň kořene `bg/muted`, text `value` v `text/disabled`, tah zůstává `border/default w1 INSIDE`, `lock` je `icon/disabled`, `toggle` je Icon button ve stavu `State=Disabled` (ikona `icon/disabled`, bez krytí). **Změna 14. 10. 2026** (rozhodnutí Q-8): dříve `value` `text/muted`, `lock` beze změny `icon/muted` a přepínač navíc s krytím `op=0.5`.
- Mřížka Content × State je úplná (3 × 6 = 18 variant). V sadě jsou sloupce v pořadí Default, Hover, Focus, Disabled, Error (stejně pořadí vrstev); seznam hodnot vlastnosti `State` je Default, Hover, Focus, Error, Disabled – pořadí hodnot nelze přes plugin API změnit. Nemá stav Pressed. `Error` a `Focus` jsou hodnoty téže vlastnosti; kombinace „chyba + fokus“ je od 14. 10. 2026 (N-14) samostatná hodnota `Error focus` (varianty `773:147201`, `773:147208`, `773:147215` ve sloupci x = 1744): obrys `text/danger` 2 px `INSIDE` s geometrií Focus (`pad0,5,0,11`) a zámek `icon/danger`. Sada má 2088×216 (dříve 1744×216).
- **Změna (13. 10. 2026):** stav Error je obrys `text/danger`, 1 px, `INSIDE` na místě okraje, s geometrií varianty Default (okraje `pad0,6,0,12`, pole má 320×40 a nepřesahuje ven) – stejně jako `State=Error` Text field (`144:2941`) a jako chyba pole na stránce *Widgets* (`widgets-04-text-input`: 1 px `text/danger` uvnitř). Dříve to byl tah `icon/danger w2 OUTSIDE`, který přesahoval pole o 2 px. Ikona `lock` má dál `stroke=icon/danger`; výplň, barva textu a ikona přepínače se nemění. Komponenta neobsahuje text chybové zprávy (patří formuláři, ne poli).
- **Nové:** `toggle` je vystavená instance Icon button (`706:147674`, `Size=28`, `exp=1`): 28×28, `rad=6`, ikona 16×16 – stejná geometrie jako dřívější rámec, navíc vlastní stavy Hover, Pressed, Focus, Disabled, které se nastavují na vnořené instanci. Vnitřní vrstva ikony se jmenuje `icon` (dřív `toggle-icon`).
- Ikona přepínače (výpis barvu u instance Icon button neukazuje, je to přepsání tahu vektoru `glyph`): `Empty` = `icon/eye` v `icon/muted` (bez přepsání), `Masked` = `icon/eye` v `icon/muted`, `Visible` = `icon/eye-off` v `icon/muted`; ve variantách Disabled vždy `icon/disabled`. **Změna 16. 10. 2026 (N-24, rozhodnutí 22):** `Masked` a `Visible` měly přepsanou barvu `icon/default`, která platila ve všech stavech tlačítka; teď mají barvu Icon button `icon/muted` jako `Empty`, oko je tedy obyčejné Icon button (hover `icon/default`, stisk `icon/strong`). Přepsání tahu zůstává jen kvůli záměně ikony (hodnota je stejná jako v Icon button).
- **Nové (F-38):** textová vlastnost `Value` (výchozí `Report-2025!`) je svázaná s vrstvou `value` jen ve variantách `Content=Visible`. V `Content=Empty` je `value` zástupný text `'Password'` v `text/muted` a v `Content=Masked` `'••••••••••••'` (12 znaků •, `text/primary`, proklad `ls2px` – 2 px) – obojí bez vazby, protože textová vlastnost má pro celou sadu jedinou výchozí hodnotu a všechny svázané vrstvy by ukazovaly totéž.
- Instance v prototypu: přepínače dvou instancí na stránce View Document (`228:8949`, `228:9209`) mají přepsané interakce (klik přepne obrazovku se skrytým a viditelným heslem); po výměně vrstvy byly obnoveny.
- Velikost: `lock` pevný 16×16, `toggle` pevný 28×28, `value` `fill/hug` s `trunc1` – šířku určuje kořen (pevná šířka i výška). Výška 40 (Text field 34).
- Šířka `value`: 320 − 13 − 16 − 8 − 8 − 28 − 7 = 240 ve všech variantách.
- Vnořené komponenty: Icon button (`toggle`) a ikony `icon/lock`, `icon/eye`, `icon/eye-off`.

### Strength meter · `455:1270`

Sada 6 variant · výchozí varianta `Level=Empty` · rozměr 300×50

Popis ve Figmě: „Strength of a password or of an encryption algorithm. A bar of 20 cells, filled in steps of 5 %, with the level in words, the measured entropy on the right and, for passwords, the criteria that make the password stronger.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Detail | text | **118 of 128 bits** |
| Show detail | přepínač | **true** |
| Show criteria | přepínač | **true** |
| Level | varianta | **Empty**, Very weak, Weak, Moderate, Strong, Very strong |

**Stavba výchozí varianty**

```text
C 'Level=Empty' 300x50 lay=V gap6 pad0,0,0,0 MIN/MIN; z=fix/hug
 F 'top' 300x15 lay=H gap8 pad0,0,0,0 MIN/CENTER; z=fill/hug; clip=1
  T 'level' 110x15 z=hug/hug; fill=text/tertiary; f=Inter Semi Bold 12/auto LT WIDTH_AND_HEIGHT; tx='Password strength'
  F 'spacer' 99x1 z=fill/fix; clip=1
  T 'detail' 75x13 z=hug/hug; fill=text/tertiary; f=Inter Regular 11/auto LT WIDTH_AND_HEIGHT; tx='118 of 128 bits'; ref=visible:Show detail,characters:Detail
 F 'track' 300x6 lay=H gap0 pad0,0,0,0 MIN/MIN; z=fill/fix; fill=border/divider; rad=3; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
  F 'cell' 15x6 z=fill/fill; clip=1
 F 'criteria' 300x17 lay=H gap4 pad0,0,0,0 MIN/MIN wrap4; z=fill/hug; clip=1; ref=visible:Show criteria
  F 'criterion' 109x17 lay=H gap4 pad2,8,2,6 MIN/CENTER; z=hug/hug; fill=bg/muted; rad=10; clip=1
   F 'dot-wrap' 12x12 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; clip=1
    E 'dot' 4x4 z=fix/fix; fill=text/tertiary
   T '12+ characters' 79x13 z=hug/hug; fill=text/tertiary; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='12+ characters'
  F 'criterion' 44x17 lay=H gap4 pad2,8,2,6 MIN/CENTER; z=hug/hug; fill=bg/muted; rad=10; clip=1
   F 'dot-wrap' 12x12 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; clip=1
    E 'dot' 4x4 z=fix/fix; fill=text/tertiary
   T 'Aa' 14x13 z=hug/hug; fill=text/tertiary; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='Aa'
  F 'criterion' 50x17 lay=H gap4 pad2,8,2,6 MIN/CENTER; z=hug/hug; fill=bg/muted; rad=10; clip=1
   F 'dot-wrap' 12x12 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; clip=1
    E 'dot' 4x4 z=fix/fix; fill=text/tertiary
   T '123' 20x13 z=hug/hug; fill=text/tertiary; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='123'
  F 'criterion' 52x17 lay=H gap4 pad2,8,2,6 MIN/CENTER; z=hug/hug; fill=bg/muted; rad=10; clip=1
   F 'dot-wrap' 12x12 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; clip=1
    E 'dot' 4x4 z=fix/fix; fill=text/tertiary
   T '#$&' 22x13 z=hug/hug; fill=text/tertiary; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='#$&'
```

**Varianty a stavy**

Zápis „splněno `criterion#N`“ v tabulce znamená přesně tyto tři změny daného kritéria (ověřeno u každé varianty a každého kritéria zvlášť): `criteria>criterion#N: fill=accent/green/bg`; přidáno `criteria>criterion#N>icon/check: I 12x12 z=fix/fix; ic=icon/check stroke=accent/green/icon w2`; text kritéria (`criteria>criterion#N>12+ characters` / `>Aa` / `>123` / `>#$&`): `fill=accent/green/icon`; odebráno `criteria>criterion#N>dot-wrap` a `criteria>criterion#N>dot-wrap>dot`.

| Varianta | Změna |
| --- | --- |
| `Level=Very weak` | `top>level: s=61x15; fill=accent/red/icon; tx='Very weak'`; `top>spacer: s=148x1`; `track>cell#1` až `track>cell#3` (3 buňky): `fill=accent/red/icon`; žádné kritérium splněno |
| `Level=Weak` | `top>level: s=33x15; fill=accent/red/icon; tx='Weak'`; `top>spacer: s=176x1`; `track>cell#1` až `track>cell#7` (7 buněk): `fill=accent/red/icon`; splněno `criterion#1` (`12+ characters`) |
| `Level=Moderate` | `top>level: s=56x15; fill=accent/amber/icon; tx='Moderate'`; `top>spacer: s=153x1`; `track>cell#1` až `track>cell#11` (11 buněk): `fill=accent/amber/icon`; splněno `criterion#1`, `criterion#2` (`12+ characters`, `Aa`) |
| `Level=Strong` | `top>level: s=39x15; fill=accent/green/icon; tx='Strong'`; `top>spacer: s=170x1`; `track>cell#1` až `track>cell#16` (16 buněk): `fill=accent/green/icon`; splněno `criterion#1`, `criterion#2`, `criterion#3` (`12+ characters`, `Aa`, `123`) |
| `Level=Very strong` | `top>level: s=68x15; fill=accent/green/icon; tx='Very strong'`; `top>spacer: s=141x1`; `track>cell#1` až `track>cell#20` (všech 20 buněk): `fill=accent/green/icon`; splněno `criterion#1`, `criterion#2`, `criterion#3`, `criterion#4` (`12+ characters`, `Aa`, `123`, `#$&`) |

**Poznámky**

- Kořen: velikost `fix/hug` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- **Změna (13. 10. 2026, F-57):** opraven jen popis ve Figmě – místo „A continuous bar“ je „A bar of 20 cells, filled in steps of 5 %“, jak je pruh nakreslen. Kresba, vlastnosti ani vrstvy se nezměnily (znovu přečteno).
- Stavba podle `Level` (počet vybarvených buněk z 20 / barva / splněná kritéria): Empty 0 / – / 0; Very weak 3 / `accent/red/icon` / 0; Weak 7 / `accent/red/icon` / 1; Moderate 11 / `accent/amber/icon` / 2; Strong 16 / `accent/green/icon` / 3; Very strong 20 / `accent/green/icon` / 4. Very weak a Weak mají stejnou barvu (červená), Strong a Very strong také (zelená); liší se jen délkou a textem.
- Řádek `top`: vlevo `level` (Inter Semi Bold 12), pružný `spacer`, vpravo `detail` (Inter Regular 11, `text/tertiary`). Text `level` nemá vazbu na vlastnost – je dán variantou: `'Password strength'` v `text/tertiary` (Empty), jinak název úrovně v barvě úrovně (`accent/<hue>/icon`, token ikon použitý pro text i pro buňky pruhu).
- Řádek `detail` se mezi variantami nemění: ve všech šesti (i v `Level=Empty`) je text `'118 of 128 bits'` (výchozí hodnota vlastnosti `Detail`) v `text/tertiary`; viditelnost řídí `Show detail`.
- Pruh `track`: 300×6, `fill=border/divider` (token rámečků použitý jako výplň), `rad=3`, `clip=1`; skládá se z 20 buněk `cell` (`fill/fill`, 15×6 při šířce 300, `gap0`), nevybarvená buňka nemá žádnou výplň. Stavba je po 5 % (20 buněk), popis ve Figmě to teď říká také. Buňky nemají vlastní zaoblení: levý konec vybarvené části je zaoblen ořezem pruhu, pravý konec je rovný.
- Kritéria `criteria`: čtyři štítky `criterion` v pevném pořadí `12+ characters`, `Aa`, `123`, `#$&`; rozvržení vodorovné se zalamováním (`gap4`, `wrap4`), při šířce 300 jsou v jednom řádku. Texty kritérií nejsou vlastnosti (název vrstvy = text). Viditelnost celého řádku řídí `Show criteria`.
- Štítek nesplněného kritéria: `pad2,8,2,6`, `gap4`, `fill=bg/muted`, `rad=10`, vlevo `dot-wrap` 12×12 s tečkou `dot` 4×4 (`fill=text/tertiary`), text Inter Medium 11 v `text/tertiary`. Splněné kritérium: `fill=accent/green/bg`, místo `dot-wrap` instance `icon/check` 12×12 (`stroke=accent/green/icon w2`), text v `accent/green/icon`. Rozměr štítku se nemění (značka i ikona mají 12×12).
- Jiná stavba variant: splněné kritérium nemá vrstvu `dot-wrap` a má navíc vrstvu `icon/check` (výměna vrstvy, ne přepnutí viditelnosti). Vrstva ikony má název `icon/check` (nepřejmenovaná instance).
- Kritéria se plní vždy zleva a jejich počet je svázán s úrovní; varianta s jinou kombinací (např. splněno jen `Aa`) neexistuje. Barva splněného kritéria je vždy zelená bez ohledu na barvu úrovně.
- Velikost: `top`, `track`, `criteria` jsou na šířku `fill`, `track` má pevnou výšku 6; `level` a `detail` jsou `hug/hug` bez zkracování. Kořen má pevnou šířku (300) a výšku podle obsahu.
- Žádné stavy (Hover / Focus / Disabled) – komponenta je jen ukazatel.

### Slider · `144:2961`

Sada 25 variant · výchozí varianta `Value=0, State=Default` · rozměr 180×20

Popis ve Figmě: „Slider for continuous values (speech rate, pitch, volume). Value variants are design stops only; the control is continuous in the application. State changes only the thumb (Hover, Pressed; Focus adds a ring around it), Disabled also grays the filled part. The thumb keeps 14 × 14 when the slider is resized; only the track and the filled part stretch.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Value | varianta | **0**, 25, 50, 75, 100 |
| State | varianta | **Default**, Hover, Pressed, Focus, Disabled |

**Stavba výchozí varianty**

```text
C 'Value=0, State=Default' 180x20 z=fix/fix
 F 'track' 180x4 at=0,8; con=STRETCH/CENTER; z=fix/fix; fill=bg/track; rad=2; clip=1
 E 'thumb' 14x14 at=0,3; con=MIN/CENTER; z=fix/fix; fill=bg/surface; st=bg/accent w2 INSIDE
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Value=25, State=Default` | přidáno `fill: F 49x4 at=0,8; con=SCALE/CENTER; z=fix/fix; fill=bg/accent; rad=2; clip=1`; `thumb: at=42,3` |
| `Value=50, State=Default` | přidáno `fill: F 90x4 at=0,8; con=SCALE/CENTER; z=fix/fix; fill=bg/accent; rad=2; clip=1`; `thumb: at=83,3` |
| `Value=75, State=Default` | přidáno `fill: F 132x4 at=0,8; con=SCALE/CENTER; z=fix/fix; fill=bg/accent; rad=2; clip=1`; `thumb: at=125,3` |
| `Value=100, State=Default` | přidáno `fill: F 173x4 at=0,8; con=SCALE/CENTER; z=fix/fix; fill=bg/accent; rad=2; clip=1`; `thumb: at=166,3` |
| `Value=0, State=Hover` | `thumb: st=accent/blue/icon w2 INSIDE` |
| `Value=25, State=Hover` (proti `Value=25, State=Default`) | `thumb: st=accent/blue/icon w2 INSIDE` |
| `Value=50, State=Hover` (proti `Value=50, State=Default`) | `thumb: st=accent/blue/icon w2 INSIDE` |
| `Value=75, State=Hover` (proti `Value=75, State=Default`) | `thumb: st=accent/blue/icon w2 INSIDE` |
| `Value=100, State=Hover` (proti `Value=100, State=Default`) | `thumb: st=accent/blue/icon w2 INSIDE` |
| `Value=0, State=Pressed` | `thumb: fill=bg/accent` |
| `Value=25, State=Pressed` (proti `Value=25, State=Default`) | `thumb: fill=bg/accent` |
| `Value=50, State=Pressed` (proti `Value=50, State=Default`) | `thumb: fill=bg/accent` |
| `Value=75, State=Pressed` (proti `Value=75, State=Default`) | `thumb: fill=bg/accent` |
| `Value=100, State=Pressed` (proti `Value=100, State=Default`) | `thumb: fill=bg/accent` |
| `Value=0, State=Focus` | přidáno `focus-ring: E 18x18 at=-2,1; con=SCALE/CENTER; z=fix/fix; st=border/focus w2 INSIDE` |
| `Value=25, State=Focus` (proti `Value=25, State=Default`) | přidáno `focus-ring: E 18x18 at=40,1; con=SCALE/CENTER; z=fix/fix; st=border/focus w2 INSIDE` |
| `Value=50, State=Focus` (proti `Value=50, State=Default`) | přidáno `focus-ring: E 18x18 at=81,1; con=SCALE/CENTER; z=fix/fix; st=border/focus w2 INSIDE` |
| `Value=75, State=Focus` (proti `Value=75, State=Default`) | přidáno `focus-ring: E 18x18 at=123,1; con=SCALE/CENTER; z=fix/fix; st=border/focus w2 INSIDE` |
| `Value=100, State=Focus` (proti `Value=100, State=Default`) | přidáno `focus-ring: E 18x18 at=164,1; con=SCALE/CENTER; z=fix/fix; st=border/focus w2 INSIDE` |
| `Value=0, State=Disabled` | `thumb: fill=bg/muted; st=border/strong w2 INSIDE` |
| `Value=25, State=Disabled` (proti `Value=25, State=Default`) | `fill: fill=border/strong`; `thumb: fill=bg/muted; st=border/strong w2 INSIDE` |
| `Value=50, State=Disabled` (proti `Value=50, State=Default`) | `fill: fill=border/strong`; `thumb: fill=bg/muted; st=border/strong w2 INSIDE` |
| `Value=75, State=Disabled` (proti `Value=75, State=Default`) | `fill: fill=border/strong`; `thumb: fill=bg/muted; st=border/strong w2 INSIDE` |
| `Value=100, State=Disabled` (proti `Value=100, State=Default`) | `fill: fill=border/strong`; `thumb: fill=bg/muted; st=border/strong w2 INSIDE` |

**Poznámky**

- Kořen: bez auto layoutu, velikost `fix/fix` (vodorovně/svisle), ořez obsahu ne; započítání obrysu do rozložení se u kořene bez auto layoutu neuplatní. Platí pro všechny varianty.
- Nové (oprava 13. 10. 2026, F-02): vlastnost `State` s pěti hodnotami pro každou z pěti hodnot `Value` (5 × 5 = 25 variant, mřížka úplná). Dřívějších pět variant `Value=…` je nyní `Value=…, State=Default`; jejich kresba ani identifikátory se nezměnily (`144:2942`, `144:2945`, `144:2949`, `144:2953`, `144:2957`).
- Stavy jsou nakreslené podle stránky *Widgets*, sekce 07, blok „New – handle states, disabled, tick marks“, a shodují se s kódem (`pdfwidgetstyleranges.cpp`): Hover mění jen tah jezdce (`accent/blue/icon`), Pressed jen výplň jezdce (`bg/accent`, tah `bg/accent` zůstává – jezdec je plný kruh), Disabled mění jezdce (`bg/muted` + tah `border/strong`) a vrstvu `fill` (`border/strong`). Dráha `track` zůstává ve všech stavech `bg/track`, tedy i v Disabled.
- Focus = Default + prstenec: nová vrstva `focus-ring` (elipsa 18×18, bez výplně, tah `border/focus w2 INSIDE`) leží pod jezdcem a obepíná ho těsně zvenku (jezdec 14×14, prstenec o 2 px větší na každou stranu, střed společný). Jezdec, dráha ani výplň se ve Focus nemění. Vrstva `focus-ring` existuje jen ve variantách `State=Focus` a má stejná omezení jako jezdec (viz poznámku o omezeních níže).
- Prstenec přesahuje kořen: u `Value=0` začíná na `x=-2`, u `Value=100` končí na `x=182` (kořen neořezává); svisle se do výšky 20 vejde (`y=1` až `19`). Rozměr komponenty zůstává 180×20 ve všech stavech.
- `Value` je varianta s pěti hodnotami 0, 25, 50, 75, 100 (ne číslo ani text); podle popisu jsou to jen zarážky návrhu, ovládací prvek je v aplikaci spojitý.
- Jiná stavba variant: `Value=0` nemá vrstvu `fill` vůbec (ve všech pěti stavech), ostatní varianty ji mají (v pořadí vrstev mezi `track` a `thumb`; ve Focus je `focus-ring` mezi `fill` a `thumb`).
- Geometrie: dráha levého okraje jezdce je 0 až 166 (= 180 − 14); polohy 42, 83, 125, 166 odpovídají 25 / 50 / 75 / 100 % ze 166 po zaokrouhlení. Šířka `fill` = poloha jezdce + 7 (49, 90, 132, 173), tj. výplň končí ve středu jezdce; u `Value=100` má výplň 173, ne 180.
- Kořen nemá auto layout (vrstvy mají absolutní polohu). Omezení: `track` `STRETCH/CENTER`, `fill` `SCALE/CENTER`. **Změna 14. 10. 2026 (N-12):** jezdec si při změně šířky instance drží 14×14 (dříve měl vodorovně `SCALE` a v širší instanci se roztáhl do elipsy). U `Value=0` mají `thumb` a `focus-ring` omezení `MIN/CENTER`, u `Value=100` `MAX/CENTER`; u 25, 50 a 75 leží v novém rámci `thumb-slot` (14×14 na místě jezdce, bez výplně, `SCALE/CENTER`) s omezením `CENTER/CENTER`, takže střed jezdce sleduje konec výplně a jezdec zůstává kruhem (ověřeno na instanci široké 360 px: `Value=25` má jezdec 14×14 se středem na x = 98, výplň končí na 98). Polohy vrstev v tabulce platí pro výchozí šířku 180; `thumb` a `focus-ring` jsou u 25, 50 a 75 potomky `thumb-slot`.
- Jezdec: `fill=bg/surface`, `st=bg/accent w2 INSIDE` (token pozadí použitý jako tah), bez stínu. Dráha: 4 px vysoká, `bg/track`, `rad=2`, svisle uprostřed výšky 20. Tokeny mimo svou skupinu ve stavech: `accent/blue/icon` jako tah (Hover), `border/strong` jako výplň i tah (Disabled).
- Jen vodorovná podoba; žádné značky, popisek ani zobrazení hodnoty (svislý slider, značky a QDial jsou jen na stránce *Widgets*).
- Sada: rozměr 1028×228 (dříve 1028×68), auto layout se zalamováním – řádky po stavech (Default, Hover, Pressed, Focus, Disabled), sloupce po hodnotách 0 až 100.

### Progress bar · `78:868`

Sada 5 variant · výchozí varianta `Value=0` · rozměr 120×4

Popis ve Figmě: „Reading progress of a document (last viewed page / page count). Value is rounded to the nearest variant in the design; in the app it is continuous. 100 = finished (green).“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Value | varianta | **0**, 25, 50, 75, 100 |

**Stavba výchozí varianty**

```text
C 'Value=0' 120x4 z=fix/fix; fill=border/default; rad=2; clip=1
 F 'fill' 1x4 hid=1; at=0,0; con=SCALE/STRETCH; z=fix/fix; fill=bg/accent; rad=2; clip=1
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Value=25` | `fill: s=30x4; hid=-` |
| `Value=50` | `fill: s=60x4; hid=-` |
| `Value=75` | `fill: s=90x4; hid=-` |
| `Value=100` | `fill: s=120x4; hid=-; fill=accent/green/icon` |

**Poznámky**

- Kořen: bez auto layoutu, velikost `fix/fix` (vodorovně/svisle), ořez obsahu ano; započítání obrysu do rozložení se u kořene bez auto layoutu neuplatní. Platí pro všechny varianty.
- Nové (oprava 13. 10. 2026, F-42): výplň sleduje šířku instance. Kořen už nemá auto layout a vrstva `fill` má omezení `SCALE/STRETCH` (vodorovně poměrně, svisle přes celou výšku), takže její šířka je vždy 25 / 50 / 75 / 100 % šířky pruhu: v instanci široké 133 px (Pinned card) má `Value=50` výplň 66.5 px (dříve pevných 60), v instanci 261 px (karty průběhu na stránkách Tools) má `Value=25` 65.25 px (dříve 30). Při výchozí šířce 120 se kresba nezměnila (30 / 60 / 90 / 120).
- Šířka výplně v instanci nemusí být celé číslo (66.5, 65.25, 195.75 …); v aplikaci je šířka výplně = hodnota × šířka pruhu.
- `Value` je varianta s pěti hodnotami 0, 25, 50, 75, 100; podle popisu je hodnota v aplikaci spojitá a v návrhu se zaokrouhluje na nejbližší variantu.
- `Value=100` má jinou barvu výplně: `accent/green/icon` (dokončeno, zelená; token ikon použitý jako výplň; záměr), ostatní `bg/accent`.
- Ve `Value=0` je vrstva `fill` skrytá (`hid=1`, 1×4, výplň `bg/accent`) a není vázána na žádnou vlastnost – viditelnost se liší jen variantou.
- Pozadí pruhu je `border/default` (token rámečků použitý jako výplň); Slider `144:2961` a Scrollbar `706:148919` používají pro dráhu `bg/track`. Ponecháno záměrně (F-53): stránka *Widgets*, sekce 08, i kód (`CE_ProgressBarGroove`) kreslí žlábek výslovně `border/default`; `bg/track` má jen tenká 2px varianta v záložce dokumentu.
- Výška 4, `rad=2` u kořene i u výplně – vrstva `fill` má vlastní zaoblení všech rohů, tedy i pravého konce uprostřed pruhu. Kořen obsah ořezává (ve všech pěti variantách).
- Žádné stavy, žádný neurčitý (indeterminate) průběh, žádný text s procenty (na stránce *Widgets* jsou jako „New“).
- Sada: rozměr 744×52, beze změny.

### Scrollbar · `706:148919`

Sada 8 variant · výchozí varianta `Orientation=Vertical, State=Default` · rozměr 6×200

Popis ve Figmě: „Scrollbar of a scroll area, vertical or horizontal (Orientation). Resize the instance to the track length and the nested thumb to (visible / total) × track; move the thumb for the scroll position. State recolours the thumb only: Hover when the pointer is over the scroll bar, Pressed while the thumb is dragged. Disabled: the thumb is border/default.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Orientation | varianta | **Vertical**, Horizontal |
| State | varianta | **Default**, Hover, Pressed, Disabled |

**Stavba výchozí varianty**

```text
C 'Orientation=Vertical, State=Default' 6x200 z=fix/fix; fill=bg/track; rad=3
 F 'thumb' 6x80 at=0,0; z=fix/fix; fill=border/strong; rad=3; clip=1
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Orientation=Vertical, State=Hover` | `thumb: fill=icon/muted` |
| `Orientation=Vertical, State=Pressed` | `thumb: fill=text/tertiary` |
| `Orientation=Horizontal, State=Default` | kořen: `s=200x6`; `thumb: s=80x6` |
| `Orientation=Horizontal, State=Hover` (proti `Orientation=Horizontal, State=Default`) | `thumb: fill=icon/muted` |
| `Orientation=Horizontal, State=Pressed` (proti `Orientation=Horizontal, State=Default`) | `thumb: fill=text/tertiary` |
| `Orientation=Vertical, State=Disabled` | `thumb: fill=border/default` |
| `Orientation=Horizontal, State=Disabled` (proti `Orientation=Horizontal, State=Default`) | `thumb: fill=border/default` |

**Poznámky**

- Kořen: bez auto layoutu, velikost `fix/fix` (vodorovně/svisle), ořez obsahu ne; započítání obrysu do rozložení se u kořene bez auto layoutu neuplatní. Platí pro všechny varianty.
- Nové (oprava 13. 10. 2026, F-03): dřívější samostatná komponenta `Scrollbar` `106:2120` je nyní sadou `706:148919` s vlastnostmi `Orientation` (Vertical, Horizontal) a `State` (Default, Hover, Pressed). Původní komponenta je varianta `Orientation=Vertical, State=Default`: zachovala si identifikátor `106:2120` i klíč, její kresba se nezměnila a všechny instance na ni dál ukazují. Nové varianty: `706:148920` (Vertical, Hover), `706:148922` (Vertical, Pressed), `706:148924` (Horizontal, Default), `706:148926` (Horizontal, Hover), `706:148928` (Horizontal, Pressed).
- Stavy podle stránky *Widgets*, sekce 09, blok „New – states, horizontal bar, reserved extent“, shodně s kódem (`pdfwidgetstyleranges.cpp`): mění se jen výplň jezdce – `border/strong` v klidu, `icon/muted` s ukazatelem nad posuvníkem (kdekoli, nejen nad jezdcem), `text/tertiary` při tažení. Dráha `bg/track` se nemění. Stav Disabled (jezdec `border/default`, *Widgets* sekce 09) má komponenta od 14. 10. 2026 (N-14; `773:147238` svislý, `773:147240` vodorovný); prázdná dráha bez jezdce v komponentě není.
- Komponenta je jen samotný pruh široký 6 px. Widget je podle stránky *Widgets* široký 12 px (`PM_ScrollBarExtent`) a pruh je v něm uprostřed s okraji 3 px – tyto okraje (žlábek 12 px) součástí komponenty nejsou.
- Svislá podoba 6×200, vodorovná 200×6; dráha `bg/track` s `rad=3`, jezdec `thumb` přes celou tloušťku (6×80, resp. 80×6, `rad=3`) s výplní z tokenu rámečků / ikon / textu použitého jako výplň. Žádné šipky / tlačítka na koncích, žádné odsazení jezdce od okraje dráhy.
- Kořen nemá auto layout; `thumb` je na `at=0,0` s omezeními `MIN/MIN` (vlevo / nahoře) – při změně délky instance se jezdec sám nemění. Podle popisu se délka a poloha jezdce nastavují ručně: délka = (viditelná část / celek) × délka dráhy. Vrstva `thumb` má ve všech variantách stejný název, přepsaná velikost a poloha jezdce proto přepnutí stavu přežije.
- Výchozí rozměr 6×200 (200×6) a jezdec 80 jsou jen ukázkové hodnoty; nejmenší délka jezdce je podle *Widgets* 32 (`PM_ScrollBarSliderMin`).
- Popis komponenty je nyní na sadě (varianty vlastní popis nemají).
- Sada: poloha 1110,114 v sekci (místo původní komponenty), rozměr 352×248, bez auto layoutu: čtyři svislé varianty vedle sebe (x = 24, 50, 76, 102 – Default, Hover, Pressed, Disabled), čtyři vodorovné pod sebou vpravo od nich (x = 128; y = 24, 50, 76, 102). Vodorovné varianty se 14. 10. 2026 posunuly z x = 102 na 128.

### Settings nav item · `145:2879`

Sada 8 variant · výchozí varianta `Selected=False, State=Default` · rozměr 300×52

Popis ve Figmě: „Section entry in the Settings menu: icon, name and a one-line summary of what the section contains.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Name | text | **General** |
| Subtitle | text | **Language, theme, documents** |
| Icon | záměna instance | **icon/settings** |
| Selected | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Selected=False, State=Default' 300x52 lay=H gap12 pad0,12,0,12 MIN/CENTER; z=fix/fix; rad=10
 I 'icon' 20x20 z=fix/fix; ref=mainComponent:Icon; ic=icon/settings stroke=icon/default w2
 F 'text' 244x30 lay=V gap1 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  T 'name' 244x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='General'; ref=characters:Name
  T 'subtitle' 244x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='Language, theme, documents'; ref=characters:Subtitle
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=False, State=Hover` | kořen: `fill=bg/hover` |
| `Selected=False, State=Pressed` | kořen: `fill=bg/pressed` |
| `Selected=False, State=Focus` | kořen: `sil=0; st=border/focus w2 INSIDE` |
| `Selected=True, State=Default` | kořen: `fill=bg/accent-subtle`; `icon: ic=icon/settings stroke=icon/accent w2`; `text>name: fill=text/accent` |
| `Selected=True, State=Hover` (proti `Selected=True, State=Default`) | beze změny |
| `Selected=True, State=Pressed` (proti `Selected=True, State=Default`) | beze změny |
| `Selected=True, State=Focus` (proti `Selected=True, State=Default`) | kořen: `sil=0; st=border/focus w2 INSIDE` |

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ne v obou variantách Focus (`sil=0`); ostatní varianty obrys nemají.
- Mřížka Selected × State je úplná (2 × 4 = 8 variant). Nemá stav Disabled.
- `Selected=True, State=Hover` a `Selected=True, State=Pressed` jsou totožné se `Selected=True, State=Default` – vybraná položka na najetí a stisk vizuálně nereaguje.
- **Změna:** Focus ring: `border/focus w2 INSIDE`, obrys se už nepočítá do rozložení – obsah má ve Focus stejné polohy a šířky jako ve Default (`icon` 12,16, `text` 244 px; dříve 240 px a posun o 2 px). U `Selected=False` kořen žádnou výplň nemá (ani ve Focus), u `Selected=True` zůstává `bg/accent-subtle`. Focus je Default + kroužek.
- Výchozí varianta nemá výplň ani tah (průhledné pozadí), `rad=10`.
- U `Selected=True` se mění ikona (`stroke=icon/accent`) a `name` (`text/accent`); `subtitle` zůstává `text/muted`.
- Velikost: ikona pevná 20×20, blok `text` je `fill/hug`, oba texty `fill/hug` s `trunc1` – šířku určuje kořen (pevná šířka i výška), výška 52.
- Vnořené komponenty: ikona 20×20 vystavená vlastností `Icon` (výchozí `icon/settings`).

### Shortcut row · `145:2904`

Sada 5 variant · výchozí varianta `State=Default` · rozměr 780×40

Popis ve Figmě: „One action in the Shortcuts table. Clicking the key cap starts recording a new shortcut: State=Recording shows the key cap with the focus ring and the prompt &quot;Press keys…&quot; (the cap is shown also when Has shortcut is off). Has shortcut=false shows the dashed placeholder &quot;Add shortcut&quot; in the place of the key cap.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Action | text | **Open…** |
| Shortcut | text | **Ctrl+O** |
| Icon | záměna instance | **icon/folder-open** |
| Has shortcut | přepínač | **true** |
| State | varianta | **Default**, Hover, Pressed, Focus, Recording |

**Stavba výchozí varianty**

```text
C 'State=Default' 780x40 lay=H gap12 pad0,10,0,10 MIN/CENTER; z=fix/fix; rad=8
 I 'icon' 16x16 z=fix/fix; ref=mainComponent:Icon; ic=icon/folder-open stroke=icon/muted w2
 T 'action' 624x16 z=fill/hug; fill=text/primary; f=Inter Regular 13/auto LT HEIGHT trunc1; tx='Open…'; ref=characters:Action
 F 'key-slot' 96x23 lay=V gap0 pad0,0,0,0 MIN/MAX; z=hug/fix; clip=1
  F 'shortcut' 59x23 lay=H gap0 pad3,9,3,9 MIN/MIN; z=hug/hug; fill=bg/subtle; st=border/default w1 INSIDE; rad=6; clip=1; ref=visible:Has shortcut
   T 'keys' 39x15 z=hug/hug; fill=text/secondary; f=Inter Medium 12/auto LT WIDTH_AND_HEIGHT; tx='Ctrl+O'; ref=characters:Shortcut
  F 'add-shortcut' 96x23 lay=H gap0 pad3,9,3,9 MIN/MIN; z=hug/hug; st=border/strong w1 INSIDE dash3,3; rad=6; clip=1
   T 'label' 76x15 z=hug/hug; fill=text/muted; f=Inter Medium 12/auto LT WIDTH_AND_HEIGHT; tx='Add shortcut'
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `fill=bg/hover` |
| `State=Pressed` | kořen: `fill=bg/pressed` |
| `State=Focus` | kořen: `sil=0; st=border/focus w2 INSIDE` |
| `State=Recording` | `key-slot>shortcut`: `s=92x23; lay=H gap0 pad2,8,2,8 MIN/MIN; st=border/focus w2 INSIDE; ref=-`; `key-slot>shortcut>keys`: `s=72x15; fill=text/accent; tx='Press keys…'; ref=-` |

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano – kromě varianty `State=Focus`, kde se nepočítá (`sil=0`), aby kroužek fokusu nezužoval obsah.
- Nemá stav Disabled ani vlastnost Selected.
- Focus ring: `border/focus w2 INSIDE` na celém řádku, kořen ve Focus nemá výplň. Opraveno: obrys se ve Focus nepočítá do rozložení, všechny potomky jsou na stejných místech jako v Default a `action` má stejnou šířku 624 (dříve se zužoval o 4 px).
- Výchozí varianta nemá výplň ani tah, `rad=8`. Hover (`bg/hover`) a Pressed (`bg/pressed`) mění jen výplň řádku; čepička má vlastní výplň `bg/subtle` a obrys `border/default`, na obou pozadích je čitelná.
- Nové – `key-slot`: rám na pravém konci řádku se svislým rozložením, pevnou výškou 23, zarovnáním vpravo (`MIN/MAX`) a ořezem obsahu. Obsahuje pod sebou čepičku `shortcut` a zástupný prvek `add-shortcut`. Při `Has shortcut=true` je vidět čepička a `add-shortcut` leží pod ní mimo ořez (y = 23); při `Has shortcut=false` je čepička skrytá, `add-shortcut` se posune na její místo a je vidět. Je to obvyklý způsob, jak ve Figmě přepínačem zaměnit dvě vrstvy; v aplikaci jde o prostou podmínku „akce má / nemá zkratku“.
- Nové – `add-shortcut` (akce bez zkratky, `Has shortcut=false`): prázdná čepička s čárkovaným obrysem `border/strong` 1 px `INSIDE` (čárky 3 / 3), bez výplně, `rad=6`, odsazení jako čepička (`pad3,9,3,9`), text „Add shortcut“ Inter Medium 12 `text/muted`; 96×23. Text není vlastnost komponenty. Je to cíl kliknutí, kterým se spouští záznam, stejně jako čepička.
- Nové – `State=Recording` (probíhá záznam zkratky): čepička má místo obrysu kroužek `border/focus` 2 px `INSIDE` a text „Press keys…“ v `text/accent`; výplň `bg/subtle` zůstává. Odsazení čepičky je `pad2,8,2,8` místo `pad3,9,3,9`, aby silnější obrys započtený do rozložení nezměnil výšku 23 ani polohu textu. Řádek sám je nakreslen jako Default (bez výplně).
- V `State=Recording` nejsou `shortcut` a `keys` svázané s vlastnostmi (`ref=-`): čepička je vidět i při `Has shortcut=false` (záznam první zkratky) a text „Press keys…“ nenahradí hodnota vlastnosti `Shortcut`. Vrstvy `icon` a `action` jsou svázané jako v ostatních variantách.
- Šířka textu `action` je 624 (dříve 661): `key-slot` má šířku širšího ze svých dvou potomků (96 = `add-shortcut`), i když je vidět užší čepička; ta je zarovnaná k pravému okraji, její poloha se proti dřívějšku nezměnila (pravý okraj 10 px od okraje řádku). Dlouhý název akce se tedy zkrátí o 37 px dříve; v aplikaci stačí, aby text končil 12 px před čepičkou.
- **Implementace (kontrola kroku 5, 17. 10. 2026):** čepička je svisle vystředěná na celé pixely (Figma `y=8,5`, knihovna 8); šířka čepičky je zaokrouhlená šířka textu Qt + 20 (Figma „Ctrl+O“ 59, knihovna 58; „Press keys…“ 92 proti 93), poloha textu v čepičce je stejná. Testy porovnávají s tolerancí 1 px.
- Klávesová čepička `shortcut`: `pad3,9,3,9`, `fill=bg/subtle`, `st=border/default w1 INSIDE`, `rad=6`, text Inter Medium 12 `text/secondary`; 59×23 = text 39×15 + odsazení 18 / 6 + tah INSIDE 1 px na každé straně (tah se počítá do rozvržení i u vnořeného rámce). Vlastní Hover ani Pressed čepička nemá. Zkratka je jeden text (`'Ctrl+O'`), ne samostatné klávesy.
- Velikost: ikona pevná 16×16, `action` `fill/hug` s `trunc1`, `key-slot` `hug/fix` – šířku určuje kořen (pevná šířka i výška; 780 je šířka tabulky v návrhu), výška 40. Všech 5 variant má 780×40.
- Vnořené komponenty: ikona 16×16 vystavená vlastností `Icon` (výchozí `icon/folder-open`, `stroke=icon/muted`).
- Žádná barva nevázaná na proměnnou.

### Certificate row · `145:2929`

Sada 8 variant · výchozí varianta `Selected=False, State=Default` · rozměr 780×34

Popis ve Figmě: „Row of the trusted certificates table in Settings / Security. State has Default, Hover, Pressed and Focus; Focus is the Default row with a 2 px border/focus ring inside (rows of a list touch each other and are clipped by the list). A selected row reacts to the pointer like the rows of the other lists: Hover bg/accent-subtle-hover, Pressed bg/accent-subtle-pressed.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Type | text | **System** |
| Certificate | text | **ISRG Root X1** |
| Organization | text | **Internet Security Research Group** |
| Valid from | text | **2015-06-04** |
| Valid to | text | **2035-06-04** |
| Selected | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Selected=False, State=Default' 780x34 lay=H gap12 pad0,10,0,10 MIN/CENTER; z=fix/fix; rad=6
 T 'type' 64x15 z=fix/hug; fill=text/tertiary; f=Inter Regular 12/auto LT HEIGHT trunc1; tx='System'; ref=characters:Type
 T 'certificate' 260x15 z=fill/hug; fill=text/primary; f=Inter Semi Bold 12/auto LT HEIGHT trunc1; tx='ISRG Root X1'; ref=characters:Certificate
 T 'organization' 220x15 z=fix/hug; fill=text/secondary; f=Inter Regular 12/auto LT HEIGHT trunc1; tx='Internet Security Research Group'; ref=characters:Organization
 T 'valid-from' 84x15 z=fix/hug; fill=text/tertiary; f=Inter Regular 12/auto LT HEIGHT trunc1; tx='2015-06-04'; ref=characters:Valid from
 T 'valid-to' 84x15 z=fix/hug; fill=text/tertiary; f=Inter Regular 12/auto LT HEIGHT trunc1; tx='2035-06-04'; ref=characters:Valid to
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=False, State=Hover` | `~ .: fill=bg/hover` |
| `Selected=False, State=Pressed` | `~ .: fill=bg/pressed` |
| `Selected=False, State=Focus` | `~ .: st=border/focus w2 INSIDE; sil=0` |
| `Selected=True, State=Default` | `~ .: fill=bg/accent-subtle` |
| `Selected=True, State=Hover` (proti `Selected=True, State=Default`) | `~ .: fill=bg/accent-subtle-hover` |
| `Selected=True, State=Pressed` (proti `Selected=True, State=Default`) | `~ .: fill=bg/accent-subtle-pressed` |
| `Selected=True, State=Focus` (proti `Selected=True, State=Default`) | `~ .: st=border/focus w2 INSIDE; sil=0` |

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano, ve variantách Focus ne (`sil=0`). Platí pro všechny varianty; všech 8 variant má 780×34 a texty na stejných místech (x = 10, 86, 358, 590, 686; y = 9,5).
- **Nové – stavy Pressed a Focus** (4 nové varianty: `706:147107`, `706:147113`, `706:147119`, `706:147125`). Tokeny a umístění obrysu jsou převzaty z Bookmark row (`191:911`): Pressed `bg/pressed`, ve vybraném řádku `bg/accent-subtle-pressed`; Focus je varianta Default s obrysem `border/focus` 2 px (nevybraný řádek bez výplně, vybraný si ponechává `bg/accent-subtle`), velikost řádku ani poloha textů se nemění. Obrys vznikl 13. 10. 2026 jako `OUTSIDE`, od 14. 10. 2026 je `INSIDE` se `sil=0` (rozhodnutí Q-2: řádky seznamů mají kroužek uvnitř). Stav Disabled neexistuje.
- **Změna 14. 10. 2026 (rozhodnutí Q-5):** `Selected=True, State=Hover` má výplň `bg/accent-subtle-hover` (dříve byla totožná s Default, nález F-11). Vybraný řádek tak reaguje na myš i na stisk jako řádky ostatních seznamů (Bookmark row, Outline row, Note row …) a jako pohledy na stránce *Widgets* (sekce 12); tabulku kreslí styl bez výjimky.
- Výběr mění jen výplň kořene (`bg/accent-subtle`); barvy textů se u `Selected=True` nemění (Bookmark row a Settings nav item při výběru přebarvují hlavní text).
- Sloupce: `type` pevný 64, `certificate` pružný (`fill`, 260 při šířce 780), `organization` pevný 220, `valid-from` pevný 84, `valid-to` pevný 84; mezera 12, odsazení 10 vlevo i vpravo. Všechny texty `trunc1`, zarovnané vlevo (i data).
- Výchozí varianta nemá výplň ani tah, `rad=6`; mezi řádky není v komponentě oddělovací čára.
- Sada je svislý auto layout (mezera 20, okraje 24), varianty jsou pod sebou v pořadí Default, Hover, Pressed, Focus pro `Selected=False` a totéž pro `Selected=True`; sada má 828×460 (dříve 828×244).
- Žádná barva nevázaná na proměnnou.

### Color button · `156:4239`

Sada 5 variant · výchozí varianta `State=Default` · rozměr 260×34

Popis ve Figmě: „Shows the current color as a swatch with its name. Opens the Color palette popover; hex and RGB are only behind “More colors…”. Disabled grays the field and the name as Combo box does; the swatch keeps its color.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Name | text | **Black** |
| State | varianta | **Default**, Hover, Pressed, Focus, Disabled |

**Stavba výchozí varianty**

```text
C 'State=Default' 260x34 lay=H gap10 pad0,10,0,8 MIN/CENTER; z=fix/fix; fill=bg/surface; st=border/default w1 INSIDE; rad=8
 F 'swatch' 20x20 z=fix/fix; fill=reading/paper/text; st=border/strong w1 INSIDE; rad=5; clip=1
 T 'name' 186x16 z=fill/hug; fill=text/primary; f=Inter Regular 13/auto LT HEIGHT trunc1; tx='Black'; ref=characters:Name
 I 'chevron' 14x14 z=fix/fix; ic=icon/chevron-down stroke=icon/default w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `st=border/strong w1 INSIDE` |
| `State=Pressed` | kořen: `fill=bg/subtle; st=border/strong w1 INSIDE` |
| `State=Focus` | kořen: `lay=H gap10 pad0,9,0,7 MIN/CENTER; st=border/focus w2 INSIDE` |
| `State=Disabled` | kořen: `fill=bg/muted`; `name: fill=text/disabled`; `chevron: ic=icon/chevron-down stroke=icon/disabled w2` |

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- Nové (oprava 13. 10. 2026, F-09): stav `State=Disabled` (varianta `706:149577`), nakreslený jako Disabled Combo boxu `144:2899`: výplň kořene `bg/muted`, text `name` `text/muted`; tah (`border/default w1 INSIDE`) ani ikona `chevron` (`stroke=icon/muted w2`) se nemění. **Změna 14. 10. 2026** (rozhodnutí Q-8): text `name` je `text/disabled` a ikona `chevron` `icon/disabled`. V povolených stavech je `chevron` od 14. 10. 2026 `icon/default` (dříve `icon/muted`), stejně jako u Combo boxu (N-02).
- V Disabled si vzorek `swatch` ponechává barvu i tah (`border/strong w1 INSIDE`) bez snížené průhlednosti – Combo box ani stránka *Widgets* (sekce 06: „Disabled: bg/muted, text/disabled, chevron icon/disabled“) nic takového nedělají.
- Focus ring: `border/focus w2 INSIDE`, výplň `bg/surface` zůstává. Nové: varianta Focus má vnitřní okraje o 1 px menší (vlevo 7, vpravo 9 místo 8 a 10), čímž se vyrovná silnější tah počítaný do rozložení – `swatch`, `name` (186×16) i `chevron` leží ve Focus na stejném místě jako v ostatních stavech (dříve byl obsah posunut o 1 px a `name` o 2 px užší).
- Barva vzorku `swatch` není vlastnost komponenty: výplň je vázána na proměnnou `reading/paper/text`, mění se jen přepsáním výplně v instanci. Vzorek 20×20, `rad=5`, tah `border/strong w1 INSIDE`.
- Odsazení je nesouměrné: vlevo 8, vpravo 10; mezera 10 (Combo box: vlevo 12, vpravo 10, mezera 8).
- Velikost: `swatch` pevný 20×20, `chevron` pevný 14×14, `name` `fill/hug` s `trunc1` – šířku určuje kořen (pevná šířka i výška).
- Podle popisu se zobrazuje jen název barvy; hex / RGB v komponentě není.
- Vnořené komponenty: jen ikona `icon/chevron-down` (bez vazby na vlastnost). Otevíraná nabídka je samostatná komponenta Color palette popover `156:4240`.
- Sada: rozměr 1428×82 (dříve 1148×82), pět variant v řadě.

### Color palette popover · `156:4240`

Samostatná komponenta · výchozí varianta `Color palette popover` · rozměr 232×209

Popis ve Figmě: „Popover opened by the Color button: curated swatches, recently used colors, and “More colors…” which opens the full system picker with hex/RGB and eyedropper. The samples are instances of the round Color swatch, six in a row; the current color is the selected swatch. Use it only where the panel has no room for the colors inline.“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Color palette popover' 232x209 lay=V gap10 pad12,12,12,12 MIN/MIN; z=fix/hug; sil=0; fill=bg/surface; st=border/default w1 INSIDE; rad=12; fx=DROP_SHADOW(0,8,24,0 effect/shadow)
 T 'label-suggested' 57x13 z=hug/hug; fill=text/muted; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='Suggested'
 F 'swatches' 208x64 lay=H gap8 pad0,0,0,0 MIN/MIN wrap8; z=fill/hug; clip=1
  I 'swatch-reading/paper/text' 28x28 z=fix/fix; inst=Color swatch{Kind=Color, Selected=False, State=Default} ov:name,fills
  I 'swatch-reading/soft-gray/text' 28x28 z=fix/fix; inst=Color swatch{Kind=Color, Selected=False, State=Default} ov:name,fills
  I 'swatch-reading/sepia/text' 28x28 z=fix/fix; inst=Color swatch{Kind=Color, Selected=False, State=Default} ov:name,fills
  I 'swatch-reading/mint/text' 28x28 z=fix/fix; inst=Color swatch{Kind=Color, Selected=False, State=Default} ov:name,fills
  I 'swatch-reading/night/text' 28x28 z=fix/fix; inst=Color swatch{Kind=Color, Selected=False, State=Default} ov:name,fills
  I 'swatch-reading/black/text' 28x28 z=fix/fix; inst=Color swatch{Kind=Color, Selected=False, State=Default} ov:name,fills
  I 'swatch-reading/terminal/text' 28x28 z=fix/fix; inst=Color swatch{Kind=Color, Selected=True, State=Default} ov:name,fills
  I 'swatch-reading/amber/text' 28x28 z=fix/fix; inst=Color swatch{Kind=Color, Selected=False, State=Default} ov:name,fills
  I 'swatch-reading/high-contrast/text' 28x28 z=fix/fix; inst=Color swatch{Kind=Color, Selected=False, State=Default} ov:name,fills
  I 'swatch-reading/custom/text' 28x28 z=fix/fix; inst=Color swatch{Kind=Color, Selected=False, State=Default} ov:name,fills
  I 'swatch-sample/gray-dark' 28x28 z=fix/fix; inst=Color swatch{Kind=Color, Selected=False, State=Default} ov:name,fills
  I 'swatch-sample/blue' 28x28 z=fix/fix; inst=Color swatch{Kind=Color, Selected=False, State=Default} ov:name,fills
 T 'label-recent' 75x13 z=hug/hug; fill=text/muted; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='Recently used'
 F 'recent' 100x28 lay=H gap8 pad0,0,0,0 MIN/MIN; z=hug/hug; clip=1
  I 'swatch-reading/terminal/text' 28x28 z=fix/fix; inst=Color swatch{Kind=Color, Selected=False, State=Default} ov:name,fills
  I 'swatch-sample/orange' 28x28 z=fix/fix; inst=Color swatch{Kind=Color, Selected=False, State=Default} ov:name,fills
  I 'swatch-reading/custom/text' 28x28 z=fix/fix; inst=Color swatch{Kind=Color, Selected=False, State=Default} ov:name,fills
 F 'divider' 208x1 z=fill/fix; fill=border/divider; clip=1
 I 'more-colors' 89x16 lay=H gap0 pad0,2,0,2 MIN/MIN; z=hug/hug; rad=4; inst=Text link{State=Default} [Label='More colors…'] ov:height,name,width
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost `fix/hug` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ne.
- **Změna (14. 10. 2026, rozhodnutí Q-1):** vzorky jsou instance kulatého Color swatch (`206:805`), ne čtvercové rámce. Přepsaná je jen výplň vnitřního kruhu `color` (přepis `fills`); barvy: `reading/paper/text`, `reading/soft-gray/text`, `reading/sepia/text`, `reading/mint/text`, `reading/night/text`, `reading/black/text`, `reading/terminal/text`, `reading/amber/text`, `reading/high-contrast/text`, `reading/custom/text`, `sample/gray-dark`, `sample/blue`; v `recent` `reading/terminal/text`, `sample/orange`, `reading/custom/text`. Názvy vrstev jsou `swatch-<token>`.
- Vybraná barva je sedmý vzorek (`swatch-reading/terminal/text`), varianta `Selected=True` (obrys `bg/accent` 2 px uvnitř kořene vzorku). Táž barva je první ve skupině `recent`, tam nevybraná. Stavy Hover, Pressed a Focus mají vzorky ze své komponenty.
- Mřížka `swatches`: vodorovné rozvržení se zalamováním, `gap8`, `wrap8`, šířka 208 = 6 × 28 + 5 × 8, dva řádky po šesti vzorcích. Obrys kořene se do rozložení nepočítá (`sil=0`), proto je vnitřní šířka 232 − 24 = 208.
- Skupina `recent` je `hug/hug` (100×28 pro tři vzorky), bez zalamování.
- Popisky `label-suggested` / `label-recent`: Inter Medium 11, `text/muted`, texty `'Suggested'` a `'Recently used'` nejsou vlastnosti.
- Rám: `fill=bg/surface`, `st=border/default w1 INSIDE`, `rad=12`, stín `DROP_SHADOW(0,8,24,0 effect/shadow)`; odsazení 12 ze všech stran, mezera 10. Šipka / zobáček k tlačítku není nakreslen.
- Vnořené komponenty: vzorky (Color swatch) a `more-colors` = instance `Text link{State=Default}` s `Label='More colors…'`; komponenta žádnou vnořenou instanci nevystavuje.
- Na obrazovkách není použitá; barvy se na stránkách toolů vybírají řadou vzorků přímo v panelu. Popover je jen pro místa, kde panel nemá místo.
- Velikost: `swatches` a `divider` jsou na šířku `fill`, ostatní `hug`; šířka 232 je dána kořenem (pevná), výška kořene je podle obsahu.

## 5. Okno dokumentu

### Toolbar button · `190:538`

Sada 10 variant · výchozí varianta `Active=False, State=Default` · rozměr 36×36

Popis ve Figmě: „Icon-only button of the document toolbar. Active = checked tool or toggled state; Menu shows a chevron for buttons that open a list. Active=True with State=Disabled is a checked tool that cannot be used now: it keeps the bg/accent-subtle fill and the icon turns icon/disabled.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Icon | záměna instance | **icon/save** |
| Menu | přepínač | **false** |
| Active | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus, Disabled |

**Stavba výchozí varianty**

```text
C 'Active=False, State=Default' 36x36 lay=H gap2 pad0,8,0,8 CENTER/CENTER; z=hug/fix; mm=36,-,-,-; rad=10
 I 'icon' 20x20 z=fix/fix; ref=mainComponent:Icon; ic=icon/save stroke=icon/default w2
 I 'chevron' 12x12 hid=1; z=fix/fix; ref=visible:Menu; ic=icon/chevron-down stroke=icon/muted w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Active=False, State=Hover` | kořen: `fill=bg/hover`; `icon`: `ic=icon/save stroke=icon/strong w2` |
| `Active=False, State=Pressed` | kořen: `fill=bg/pressed`; `icon`: `ic=icon/save stroke=icon/strong w2` |
| `Active=False, State=Focus` | kořen: `st=border/focus w2 OUTSIDE` |
| `Active=False, State=Disabled` | `icon`: `ic=icon/save stroke=icon/disabled w2` |
| `Active=True, State=Default` | kořen: `fill=bg/accent-subtle`; `icon`: `ic=icon/save stroke=icon/accent w2`; `chevron`: `ic=icon/chevron-down stroke=icon/accent w2` |
| `Active=True, State=Hover` (proti `Active=True, State=Default`) | kořen: `fill=bg/accent-subtle-hover` |
| `Active=True, State=Pressed` (proti `Active=True, State=Default`) | kořen: `fill=bg/accent-subtle-pressed` |
| `Active=True, State=Focus` (proti `Active=True, State=Default`) | kořen: `st=border/focus w2 OUTSIDE` |
| `Active=True, State=Disabled` (proti `Active=True, State=Default`) | `icon`: `ic=icon/save stroke=icon/disabled w2`; `chevron`: `ic=icon/chevron-down stroke=icon/disabled w2` |

**Poznámky**

- Kořen: velikost `hug/fix` (vodorovně/svisle), nejmenší šířka 36, ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- Mřížka 2 × 5 je úplná. Nová je varianta `Active=True, State=Disabled` (`706:147787`): zaškrtnutý nástroj, který teď nejde použít. Je nakreslená podle stránky *Widgets*, sekce 02, blok „New – checked + disabled“: výplň `bg/accent-subtle` zůstává, ikona i šipka jsou `icon/disabled` (do 14. 10. 2026 `icon/muted`), **bez krytí** (`op` není nastaveno).
- **Změna (14. 10. 2026, rozhodnutí Q-3):** Disabled se kreslí jen tlumenými tokeny, bez krytí: `Active=False, State=Disabled` má ikonu `icon/muted` (dříve navíc `op=0.5` na kořeni), `Active=True, State=Disabled` výplň `bg/accent-subtle` a ikonu `icon/muted`. Stejně kreslí zakázané tlačítko stránka *Widgets* (sekce 02) i hotový styl. **Změna (14. 10. 2026, rozhodnutí Q-8):** tlumené tokeny jsou nově `icon/disabled` (světlejší než `icon/muted`, kontrast asi 2,5 : 1 proti `bg/surface`), ve stylu je to k implementaci (review, I-20).
- Focus: `border/focus` 2 px `OUTSIDE`, jinak stejné jako Default téže skupiny: `Active=False, State=Focus` nemá výplň a ikona je `icon/default` (dříve `icon/strong`, opraveno – shodně s Toolbar menu button `190:577`, Sidebar tab `190:619` a se stránkou *Widgets*); `Active=True, State=Focus` si ponechává výplň `bg/accent-subtle` a ikonu `icon/accent`.
- Nesoulad se stránkou *Widgets*, který zůstává: blok „New – … Focus (2 px inside, r 10)“ a poznámka *Implementation* kreslí kroužek fokusu **uvnitř** (`INSIDE`), komponenta ho má `OUTSIDE`.
- Výchozí varianta nemá žádnou výplň ani obrys (průhledné pozadí), `rad=10`.
- Vrstva `chevron` (12×12, `icon/chevron-down`) je skrytá a svázaná s přepínačem `Menu`. Barva šipky (čteno se zpřístupněnými skrytými vrstvami): tah `icon/muted` w2 ve čtyřech variantách `Active=False` (Default až Focus), `icon/disabled` w2 v obou variantách Disabled, tah `icon/accent` w2 ve čtyřech variantách `Active=True` × Default, Hover, Pressed, Focus. V Hover, Pressed a Focus varianty `Active=False` zůstává `icon/muted`, i když ikona přechází na `icon/strong`. Údaj `z=fix/fix` je čten ve skrytém stavu (skrytá vrstva se čte jako `fix`).
- Rozměr: kořen má minimální šířku 36 (`mm=36,-,-,-`), ikona 20×20 pevná, odsazení 8 vlevo i vpravo, mezera 2. Kořen je na šířku `hug`, se zobrazenou šipkou má tedy 8 + 20 + 2 + 12 + 8 = 50 px; výška je pevná 36. Všech 10 variant má 36×36.
- Bez textu; komponenta vystavuje záměnu ikony `Icon`.
- Instance se zaměněnou ikonou mají barvu ikony uloženou jako vlastní přepsání tahu (vzniká při záměně); změna barvy v hlavní komponentě se do nich proto nepropíše. Proto byla přepsání zakázaných instancí na obrazovkách sjednocena ručně: 14. 10. 2026 mají všech 1 327 zakázaných instancí ikonu `icon/disabled` (dříve část `icon/default`, část `icon/muted`).
- Žádná barva nevázaná na proměnnou.

### Toolbar menu button · `190:577`

Sada 9 variant · výchozí varianta `Open=False, State=Default` · rozměr 70×36

Popis ve Figmě: „Toolbar button with icon and text that opens a panel (File, View, Insert, Go to, Tools). Open = its panel is shown.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **File** |
| Icon | záměna instance | **icon/file** |
| Open | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus, Disabled |

**Stavba výchozí varianty**

```text
C 'Open=False, State=Default' 70x36 lay=H gap8 pad0,12,0,10 CENTER/CENTER; z=hug/fix; rad=10
 I 'icon' 18x18 z=fix/fix; ref=mainComponent:Icon; ic=icon/file stroke=icon/default w2
 T 'label' 22x16 z=hug/hug; fill=text/secondary; f=Inter Medium 13/auto LT WIDTH_AND_HEIGHT; tx='File'; ref=characters:Label
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Open=False, State=Hover` | kořen: `fill=bg/hover`; `icon`: `ic=icon/file stroke=icon/strong w2`; `label`: `fill=text/primary` |
| `Open=False, State=Pressed` | kořen: `fill=bg/pressed`; `icon`: `ic=icon/file stroke=icon/strong w2`; `label`: `fill=text/primary` |
| `Open=False, State=Focus` | kořen: `st=border/focus w2 OUTSIDE` |
| `Open=True, State=Default` | kořen: `fill=bg/accent-subtle`; `icon`: `ic=icon/file stroke=icon/accent w2`; `label`: `fill=text/accent` |
| `Open=True, State=Hover` (proti `Open=True, State=Default`) | kořen: `fill=bg/accent-subtle-hover` |
| `Open=True, State=Pressed` (proti `Open=True, State=Default`) | kořen: `fill=bg/accent-subtle-pressed` |
| `Open=True, State=Focus` (proti `Open=True, State=Default`) | kořen: `st=border/focus w2 OUTSIDE` |
| `Open=False, State=Disabled` | `icon`: `ic=icon/file stroke=icon/disabled w2`; `label`: `fill=text/disabled` |

**Poznámky**

- Kořen: velikost `hug/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- Existující kombinace: `Open=False` × Default, Hover, Pressed, Focus, Disabled (5) a `Open=True` × Default, Hover, Pressed, Focus (4). Kombinace `Open=True, State=Disabled` záměrně neexistuje – tlačítko s otevřeným panelem nemůže být zakázané.
- Výchozí varianta nemá výplň ani obrys, `rad=10`.
- Focus: `border/focus` 2 px `OUTSIDE`. `Open=False, State=Focus` nemá výplň a ikona i text zůstávají jako v Default (`icon/default`, `text/secondary`); `Open=True, State=Focus` si ponechává `bg/accent-subtle`, `icon/accent` a `text/accent`. Stejně se teď chová i Toolbar button (`190:538`).
- Disabled: ikona `icon/disabled`, text `text/disabled`, bez krytí (**změna 14. 10. 2026**, rozhodnutí Q-3 a Q-8; dříve `op=0.5` na kořeni a tokeny `icon/muted` / `text/muted`) – shodně s Toolbar button.
- Instance se zaměněnou ikonou mají barvu ikony uloženou jako vlastní přepsání tahu (vzniká při záměně); oprava barvy v hlavní komponentě se do nich nepropíše. Přepsání byla proto sjednocena ručně: 14. 10. 2026 kreslí všech 298 instancí `Open=False, State=Disabled` na obrazovkách ikonu `icon/disabled` a text `text/disabled`.
- Komponenta nemá žádnou šipku (chevron), ačkoli otevírá panel.
- Odsazení je nesouměrné: vpravo 12, vlevo 10; mezera ikona-text 8. Ikona 18×18 (Toolbar button má 20×20).
- Rozměr: text je `hug` bez zkracování (`WIDTH_AND_HEIGHT`), šířka tedy roste s textem (10 + 18 + 8 + 22 + 12 = 70), výška 36. Všech 9 variant má 70×36.
- Žádná barva nevázaná na proměnnou.

### Toolbar field · `190:593`

Sada 4 variant · výchozí varianta `State=Default` · rozměr 91×32

Popis ve Figmě: „Editable value in the toolbar: current page with page count, or zoom. Focus keeps the width of Default (2 px border/focus ring inside). State=Disabled: bg/muted fill and text/disabled value.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Value | text | **308** |
| Suffix | text | **/ 1592** |
| Show suffix | přepínač | **true** |
| State | varianta | **Default**, Hover, Focus, Disabled |

**Stavba výchozí varianty**

```text
C 'State=Default' 91x32 lay=H gap4 pad0,10,0,10 CENTER/CENTER; z=hug/fix; mm=64,-,-,-; fill=bg/surface; st=border/default w1 INSIDE; rad=8
 T 'value' 26x16 z=hug/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT WIDTH_AND_HEIGHT; tx='308'; ref=characters:Value
 T 'suffix' 39x16 z=hug/hug; fill=text/muted; f=Inter Regular 13/auto LT WIDTH_AND_HEIGHT; tx='/ 1592'; ref=visible:Show suffix,characters:Suffix
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover` | kořen: `st=border/strong w1 INSIDE` |
| `State=Focus` | kořen: `lay=H gap4 pad0,9,0,9 CENTER/CENTER; st=border/focus w2 INSIDE` |
| `State=Disabled` | kořen: `fill=bg/muted`; `value: fill=text/disabled`; `suffix: fill=text/disabled` |

**Poznámky**

- Kořen: velikost `hug/fix` (vodorovně/svisle), nejmenší šířka 64, ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- Stavy jsou Default, Hover, Focus, Disabled; Pressed není (pole stiskem získá fokus).
- **Změna Focus:** varianta má nově šířku Default 91×32 (dříve 93×32). Kroužek zůstal `border/focus` 2 px `INSIDE` (je to pole; tlačítka lišty mají `OUTSIDE`), výplň `bg/surface` zůstává. Tah 2 px se počítá do rozložení, proto má varianta vnitřní okraje o 1 px menší (9 místo 10); děti jsou na stejných místech jako v Default (`value` 11,8, `suffix` 41,8).
- **Nově stav Disabled** (`706:146743`): výplň `bg/muted`, `value` a `suffix` `text/disabled` (**změna 14. 10. 2026**, rozhodnutí Q-8; dříve `value` `text/muted` a `suffix` beze změny); tah `border/default w1 INSIDE` se nemění. Bez krytí – kreslí se tlumenými tokeny jako Text field a Combo box, ne krytím 0,5 jako tlačítka lišty.
- Šířka: 10 + 26 + 4 + 39 + 10 = 89 a tah 2 × 1 = 91; ve Focus 9 + 26 + 4 + 39 + 9 = 87 a tah 2 × 2 = 91. Výška je pevná 32.
- Rozměr: oba texty jsou `hug` bez zkracování, minimální šířka kořene 64 (`mm=64,-,-,-`), výška 32 (tlačítka lišty mají 36).
- Vrstva `suffix` je svázaná s přepínačem `Show suffix` (viditelnost) i s textem `Suffix`.
- Strom obsahuje jen dva texty; žádná ikona, šipka rozbalení ani krokovací tlačítka, a není nakreslen kurzor ani výběr textu.
- Žádná barva nevázaná na proměnnou.
- Sada není auto layout: varianty jsou v řádku y = 24 s krokem 115 (x = 24, 139, 254, 369); sada vyrostla z 371×80 na 484×80.

### Sidebar tab · `190:619`

Sada 8 variant · výchozí varianta `Selected=False, State=Default` · rozměr 32×32

Popis ve Figmě: „Icon tab at the top of the document sidebar (Outline, Thumbnails, Layers, Attachments, Speech, Signatures, Bookmarks, Notes). Tabs without content are hidden.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Icon | záměna instance | **icon/pages** |
| Selected | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Selected=False, State=Default' 32x32 lay=H gap0 pad0,0,0,0 CENTER/CENTER; rad=8
 I 'icon' 18x18 z=fix/fix; ref=mainComponent:Icon; ic=icon/pages stroke=icon/default w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=False, State=Hover` | kořen: `fill=bg/hover`; `icon`: `ic=icon/pages stroke=icon/strong w2` |
| `Selected=False, State=Pressed` | kořen: `fill=bg/pressed`; `icon`: `ic=icon/pages stroke=icon/strong w2` |
| `Selected=False, State=Focus` | kořen: `st=border/focus w2 OUTSIDE` |
| `Selected=True, State=Default` | kořen: `fill=bg/accent-subtle`; `icon`: `ic=icon/pages stroke=icon/accent w2` |
| `Selected=True, State=Hover` (proti `Selected=True, State=Default`) | kořen: `fill=bg/accent-subtle-hover` |
| `Selected=True, State=Pressed` (proti `Selected=True, State=Default`) | kořen: `fill=bg/accent-subtle-pressed` |
| `Selected=True, State=Focus` (proti `Selected=True, State=Default`) | kořen: `st=border/focus w2 OUTSIDE` |

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- Mřížka 2 × 4 je úplná; stav Disabled neexistuje (popis říká, že karty bez obsahu jsou skryté).
- Focus: `border/focus` 2 px `OUTSIDE`; `Selected=False, State=Focus` nemá výplň a ikona zůstává `icon/default`, `Selected=True, State=Focus` si ponechává `bg/accent-subtle`.
- Výchozí varianta nemá výplň ani obrys, `rad=8`.
- Rozměr 32×32 bez odsazení, ikona 18×18 pevná uprostřed; kořen má pevnou šířku i výšku a nemá nastavený minimální rozměr.
- Jen ikona, žádný text; vystavuje záměnu ikony `Icon`.
- Žádná barva nevázaná na proměnnou.

### Status bar item · `190:658`

Sada 8 variant · výchozí varianta `Tone=Neutral, State=Default` · rozměr 69×22

Popis ve Figmě: „Clickable item of the status bar, e.g. the number of rendering issues.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **Ready** |
| Icon | záměna instance | **icon/info** |
| Show icon | přepínač | **true** |
| Tone | varianta | **Neutral**, Warning |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Tone=Neutral, State=Default' 69x22 lay=H gap6 pad0,8,0,8 CENTER/CENTER; rad=6
 I 'icon' 14x14 z=fix/fix; ref=visible:Show icon,mainComponent:Icon; ic=icon/info stroke=icon/muted w2
 T 'label' 33x13 z=hug/hug; fill=text/tertiary; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='Ready'; ref=characters:Label
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Tone=Neutral, State=Hover` | kořen: `fill=bg/hover` |
| `Tone=Neutral, State=Pressed` | kořen: `fill=bg/pressed` |
| `Tone=Neutral, State=Focus` | kořen: `st=border/focus w2 OUTSIDE` |
| `Tone=Warning, State=Default` | `icon`: `ic=icon/info stroke=accent/amber/icon w2`; `label`: `fill=text/secondary` |
| `Tone=Warning, State=Hover` (proti `Tone=Warning, State=Default`) | kořen: `fill=bg/hover` |
| `Tone=Warning, State=Pressed` (proti `Tone=Warning, State=Default`) | kořen: `fill=bg/pressed` |
| `Tone=Warning, State=Focus` (proti `Tone=Warning, State=Default`) | kořen: `st=border/focus w2 OUTSIDE` |

**Poznámky**

- Kořen: velikost `hug/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- Tóny jsou jen dva: `Neutral` (ikona `icon/muted`, text `text/tertiary`) a `Warning` (ikona `accent/amber/icon`, text `text/secondary`). Jiný tón (chyba, úspěch) neexistuje.
- Tón mění jen barvu ikony a textu; pozadí je v obou tónech stejné (Default bez výplně, Hover `bg/hover`, Pressed `bg/pressed`).
- Tón `Warning` nemění ikonu samotnou - výchozí instance zůstává `icon/info`; jinou ikonu je třeba nastavit vlastností `Icon`.
- Mřížka 2 × 4 je úplná; stav Disabled neexistuje.
- Focus: `border/focus` 2 px `OUTSIDE`, bez výplně; barvy ikony a textu se nemění.
- Vrstva `icon` je svázaná s přepínačem `Show icon` (viditelnost) i se záměnou `Icon`.
- Rozměr: text `hug` bez zkracování, šířka roste s textem (8 + 14 + 6 + 33 + 8 = 69), výška 22, `rad=6`.
- Žádná barva nevázaná na proměnnou.

### Menu item · `190:704`

Sada 10 variant · výchozí varianta `State=Default, Tone=Default` · rozměr 240×32

Popis ve Figmě: „Row of a panel or a drop-down list: icon, label, optional keyboard shortcut and check mark. Show icon hides the icon and keeps its 16 px column, so the labels stay aligned. Submenu shows a chevron at the right edge (use it with Show shortcut off). Tone=Danger is a destructive action: text/danger and icon/danger in Default, Hover, Pressed and Focus; a disabled item uses text/disabled and icon/disabled in both tones.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **Properties…** |
| Icon | záměna instance | **icon/info** |
| Shortcut | text | **Ctrl+D** |
| Show shortcut | přepínač | **true** |
| Checked | přepínač | **false** |
| Show icon | přepínač | **true** |
| Submenu | přepínač | **false** |
| State | varianta | **Default**, Hover, Pressed, Focus, Disabled |
| Tone | varianta | **Default**, Danger |

**Stavba výchozí varianty**

```text
C 'State=Default, Tone=Default' 240x32 lay=H gap10 pad0,10,0,10 MIN/CENTER; z=fix/fix; rad=6
 F 'icon-slot' 16x16 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix
  I 'icon' 16x16 z=fix/fix; ref=visible:Show icon,mainComponent:Icon; ic=icon/info stroke=icon/default w2
 T 'label' 147x16 z=fill/hug; fill=text/secondary; f=Inter Regular 13/auto LT HEIGHT trunc1; tx='Properties…'; ref=characters:Label
 T 'shortcut' 37x15 z=hug/hug; fill=text/muted; f=Inter Regular 12/auto LT WIDTH_AND_HEIGHT; tx='Ctrl+D'; ref=visible:Show shortcut,characters:Shortcut
 I 'check' 16x16 hid=1; z=fix/fix; ref=visible:Checked; ic=icon/check stroke=icon/default w2
 I 'chevron' 14x14 hid=1; z=fix/fix; ref=visible:Submenu; ic=icon/chevron-right stroke=icon/muted w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Hover, Tone=Default` | kořen: `fill=bg/hover` |
| `State=Pressed, Tone=Default` | kořen: `fill=bg/pressed` |
| `State=Focus, Tone=Default` | kořen: `st=border/focus w2 INSIDE; sil=0` |
| `State=Disabled, Tone=Default` | `icon-slot>icon`: `ic=icon/info stroke=icon/disabled w2`; `label`: `fill=text/disabled`; `shortcut`: `fill=text/disabled` |
| `State=Default, Tone=Danger` | `icon-slot>icon`: `ic=icon/info stroke=icon/danger w2`; `label`: `fill=text/danger` |
| `State=Hover, Tone=Danger` (proti `State=Default, Tone=Danger`) | kořen: `fill=bg/hover` |
| `State=Pressed, Tone=Danger` (proti `State=Default, Tone=Danger`) | kořen: `fill=bg/pressed` |
| `State=Focus, Tone=Danger` (proti `State=Default, Tone=Danger`) | kořen: `st=border/focus w2 INSIDE; sil=0` |
| `State=Disabled, Tone=Danger` (proti `State=Default, Tone=Danger`) | `icon-slot>icon`: `ic=icon/info stroke=icon/disabled w2`; `label`: `fill=text/disabled`; `shortcut`: `fill=text/disabled` |

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano, ve variantách Focus ne (`sil=0`, kroužek uvnitř obsah neposouvá).
- Nové (podle stránky *Widgets*, sekce 10, blok „New – complete menu“): sloupec ikony `icon-slot`, přepínače `Show icon` a `Submenu`, vlastnost `Tone`. Názvy variant mají nově dvě vlastnosti (`State=…, Tone=…`); pět původních variant má stejné identifikátory jako dříve a hodnotu `Tone=Default`.
- `icon-slot` je rám 16×16 (bez výplně, bez ořezu), ve kterém leží instance `icon`. `Show icon=false` skrývá jen `icon`; rám zůstává, sloupec ikony je tedy vyhrazen vždy a text začíná na x = 36 s ikonou i bez ní (10 + 16 + 10) – popisky položek v nabídce jsou zarovnané.
- `Submenu=true` zobrazí `chevron` (`icon/chevron-right`, 14×14, tah `icon/muted` ve všech 10 variantách) na pravém okraji řádku: pravý okraj šipky je 10 px od okraje, jako u značky `check`. Položka s podnabídkou nemá zkratku – nastavuje se spolu se `Show shortcut=false` (jsou to dva nezávislé přepínače, komponenta to nevynucuje).
- `Tone=Danger` je destruktivní položka: text `text/danger`, ikona `icon/danger` ve stavech Default, Hover, Pressed a Focus; pozadí stavů je stejné jako u `Tone=Default`. Zkratka zůstává `text/muted`, značka `check` `icon/default`, šipka `icon/muted`.
- `State=Disabled, Tone=Danger` je nakreslená stejně jako `State=Disabled, Tone=Default` (text `text/disabled`, ikona `icon/disabled`, bez krytí) – zakázaná položka nemá tón; tak ji kreslí i styl (`PDFWidgetStyleMenus::drawMenuItem`: zakázaný stav má přednost před destruktivním).
- Pořadí vrstev: `icon-slot`, `label`, `shortcut`, `check`, `chevron`. Značka `check` (16×16, `icon/check`) je skrytá, svázaná s `Checked`; `chevron` je skrytý, svázaný se `Submenu`. Údaj `z=fix/fix` skrytých vrstev je čten ve skrytém stavu.
- `Checked` je přepínač, ne varianta: zaškrtnutí nemění pozadí ani barvu textu a lze ho kombinovat s každým stavem včetně Disabled. Výlučná volba (tečka 6 px ve sloupci ikony, *Widgets* sekce 10) v komponentě není.
- Hover a Pressed mění jen výplň kořene; ikona a text zůstávají.
- Focus: `border/focus` 2 px `INSIDE`, bez výplně (**změna 14. 10. 2026**, rozhodnutí Q-2, dříve `OUTSIDE`). Tak se kreslí položka nabídky vybraná klávesnicí (`State_Selected` po navigaci klávesami); položka pod myší má vzhled Hover.
- Disabled: ikona `icon/disabled`, text i zkratka `text/disabled`, značka `check` `icon/disabled`, bez krytí (**změna 14. 10. 2026**, rozhodnutí Q-3 a Q-8; dříve `op=0.6`, tokeny `muted` a značka `icon/accent`). Tak kreslí zakázanou položku i stránka *Widgets* a styl.
- **Změna 14. 10. 2026 (N-04):** značka `check` je v povolených stavech `icon/default` (dříve `icon/accent`), shodně se stránkou *Widgets* a stylem; zakázaná `icon/disabled`.
- Rozměr: `label` je `fill` se zkrácením na 1 řádek (`trunc1`), řádek se tedy roztahuje na šířku (nakresleno 240); zkratka je `hug`; výška 32, `rad=6`. Všech 10 variant má 240×32.
- Výchozí varianta nemá výplň ani obrys.
- Oddělovač a nadpis skupiny jsou samostatné komponenty Menu separator (`706:150117`) a Menu section (`706:150119`).
- Žádná barva nevázaná na proměnnou.

### Menu separator · `706:150117`

Samostatná komponenta · výchozí varianta — (bez variant) · rozměr 240×7

Popis ve Figmě: „Separator between groups of items of a menu: a 1 px line (border/divider) with margins of 3 px above and below and 4 px at the sides, 7 px high. It fills the width of the menu.“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Menu separator' 240x7 lay=H gap0 pad3,4,3,4 MIN/CENTER; z=fix/hug
 F 'line' 232x1 z=fill/fix; fill=border/divider; clip=1
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost `fix/hug` (vodorovně/svisle), ořez obsahu ne, obrys nemá (počítání obrysu do rozložení se neuplatní).
- Nová komponenta (F-20). Nakreslená podle stránky *Widgets*, sekce 10 (blok „New – complete menu“, rám `separator` 246×7, a řádek *Geometry* „separator 1 px, margins 3 / 4“): čára 1 px `border/divider`, okraje 3 px nahoře a dole, 4 px po stranách, celková výška 7. Stejně kreslí oddělovač styl (`SEPARATOR_HEIGHT = 7`, `SEPARATOR_MARGIN = 4`).
- `line` je rám 1 px vysoký, na šířku `fill` – čára se roztahuje s komponentou; šířka 240 je nakreslená (jako Menu item), v nabídce se instance nastavuje na `fill`.
- Kořen nemá výplň; oddělovač nemá stavy (není interaktivní).
- Použití: Context menu / Recent file (`706:150896`) a Context menu / Thumbnails (`266:12453`), kde nahradil rám `divider` 206×1 bez okrajů.
- Žádná barva nevázaná na proměnnou.

### Menu section · `706:150119`

Samostatná komponenta · výchozí varianta — (bez variant) · rozměr 240×24

Popis ve Figmě: „Caption of a group of items of a menu (a separator with a text): Medium 11 in text/muted, a row of 24 px with paddings 6 / 10 / 2, no line. It fills the width of the menu.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **Document** |

**Stavba výchozí varianty**

```text
C 'Menu section' 240x24 lay=H gap0 pad6,10,2,10 MIN/CENTER; z=fix/fix
 T 'label' 220x13 z=fill/hug; fill=text/muted; f=Inter Medium 11/auto LT HEIGHT trunc1; tx='Document'; ref=characters:Label
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ne, obrys nemá (počítání obrysu do rozložení se neuplatní).
- Nová komponenta (F-20): nadpis skupiny položek nabídky (`QAction::setSeparator` s textem). Nakreslená podle stránky *Widgets*, sekce 10: text Inter Medium 11 `text/muted`, odsazení 6 nahoře, 10 po stranách, 2 dole, žádná čára.
- Výška řádku je 24 px podle textu bloku („24 px row“), řádku *Geometry* („section title 24“) a stylu (`TITLE_HEIGHT = 24`). Kresba na stránce *Widgets* (rám `section-title`) má výšku jen 21 px, protože je na výšku `hug` (6 + 13 + 2) – nesoulad uvnitř stránky *Widgets*; komponenta se drží hodnoty 24.
- Text je svisle vystředěn ve zbylých 16 px (y = 7,5), stejně jako ve stylu (`Qt::AlignVCenter` v obdélníku zmenšeném o odsazení).
- `label` je `fill` se zkrácením na 1 řádek (`trunc1`); šířka 240 je nakreslená (jako Menu item), v nabídce se instance nastavuje na `fill`.
- Kořen nemá výplň; nadpis nemá stavy (není interaktivní) ani ikonu – text začíná na x = 10, ne ve sloupci textu položek (x = 36), stejně jako na stránce *Widgets*.
- V žádné komponentě ani obrazovce zatím není použita.
- Žádná barva nevázaná na proměnnou.

### Context menu / Thumbnails · `266:12453`

Samostatná komponenta · výchozí varianta `Context menu / Thumbnails` (jediná) · rozměr 220×220

Popis ve Figmě: „Context menu of the Thumbnails panel over the selected pages (tools.md §5.1, §9.7). Rotate, insert, move, extract to a new document, remove. The rows are instances of Menu item and Menu separator, all in State=Default; Remove Pages is a destructive action (Tone=Danger).“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Context menu / Thumbnails' 220x220 lay=V gap0 pad6,6,6,6 MIN/MIN; z=fix/hug; fill=bg/surface; st=border/default w1 INSIDE; rad=10; fx=DROP_SHADOW(0,4,16,0 effect/shadow); clip=1
 I 'menu-item' 206x32 lay=H gap10 pad0,10,0,10 MIN/CENTER; z=fill/fix; rad=6; inst=Menu item{State=Default, Tone=Default} [Submenu=false, Show shortcut=false, Show icon=true, Shortcut='Ctrl+D', Icon=icon/rotate-ccw, Checked=false, Label='Rotate Left'] ov:height,name,width,strokes
 I 'menu-item' 206x32 lay=H gap10 pad0,10,0,10 MIN/CENTER; z=fill/fix; rad=6; inst=Menu item{State=Default, Tone=Default} [Submenu=false, Show shortcut=false, Show icon=true, Shortcut='Ctrl+D', Icon=icon/rotate-cw, Checked=false, Label='Rotate Right'] ov:height,name,width,strokes
 I 'separator' 206x7 lay=H gap0 pad3,4,3,4 MIN/CENTER; z=fill/hug; inst=Menu separator ov:height,name,width
 I 'menu-item' 206x32 lay=H gap10 pad0,10,0,10 MIN/CENTER; z=fill/fix; rad=6; inst=Menu item{State=Default, Tone=Default} [Submenu=false, Show shortcut=false, Show icon=true, Shortcut='Ctrl+D', Icon=icon/file-plus, Checked=false, Label='Insert Pages…'] ov:height,name,width,strokes
 I 'menu-item' 206x32 lay=H gap10 pad0,10,0,10 MIN/CENTER; z=fill/fix; rad=6; inst=Menu item{State=Default, Tone=Default} [Submenu=false, Show shortcut=false, Show icon=true, Shortcut='Ctrl+D', Icon=icon/arrow-up-down, Checked=false, Label='Move to…'] ov:height,name,width,strokes
 I 'separator' 206x7 lay=H gap0 pad3,4,3,4 MIN/CENTER; z=fill/hug; inst=Menu separator ov:height,name,width
 I 'menu-item' 206x32 lay=H gap10 pad0,10,0,10 MIN/CENTER; z=fill/fix; rad=6; inst=Menu item{State=Default, Tone=Default} [Submenu=false, Show shortcut=false, Show icon=true, Shortcut='Ctrl+D', Icon=icon/copy, Checked=false, Label='Extract to New Document'] ov:height,name,width,strokes
 I 'menu-item' 206x32 lay=H gap10 pad0,10,0,10 MIN/CENTER; z=fill/fix; rad=6; inst=Menu item{State=Default, Tone=Danger} [Submenu=false, Show icon=true, Checked=false, Show shortcut=false, Shortcut='Ctrl+D', Icon=icon/trash, Label='Remove Pages'] ov:height,name,width,strokes
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost `fix/hug` (vodorovně/svisle), ořez obsahu ano, obrys se počítá do rozložení ano.
- Řádky jsou nově instance komponenty Menu item (`190:704`, 32 px, `Show shortcut=false`, šířka `fill` = 206) a oddělovače instance Menu separator (`706:150117`, 7 px); dříve obyčejné rámce `menu-item` 206×30 a `divider` 206×1. Výška nabídky je proto 220 (dříve 196): 2 × 6 + 6 × 32 + 2 × 7 = 218, + 2 px obrys `INSIDE` započtený do rozložení.
- Šest položek ve třech skupinách oddělených dvěma oddělovači: `Rotate Left`, `Rotate Right` | `Insert Pages…`, `Move to…` | `Extract to New Document`, `Remove Pages`.
- `Remove Pages` je nově destruktivní položka `Tone=Danger` (ikona `icon/trash` v `icon/danger`, text `text/danger`) – maže obsah dokumentu.
- Všechny řádky jsou ve stavu `State=Default`. Stav řádku (Hover, Pressed, Disabled) se na obrazovce nastavuje na vnořené instanci Menu item; jediná instance nabídky (stránka *View Document*, `266:12848`) má takto řádek `Extract to New Document` v `State=Hover` (obnovené přepsání původní výplně `bg/hover`).
- Šest vrstev řádků má stejné jméno `menu-item`, oddělovače `separator`. Vnořené instance nejsou vystavené (`exp` není nastaveno). `ov:…strokes` u řádků je barva tahu zaměněné ikony, kterou Figma ukládá při záměně; `Shortcut='Ctrl+D'` je výchozí text skryté zkratky.
- Text řádku je `fill` se zkrácením (`trunc1`, z Menu item): nejdelší popisek `Extract to New Document` potřebuje 159 px z dostupných 160 (206 − 10 − 16 − 10 − 10).
- Panel: výplň `bg/surface`, obrys `border/default` 1 px `INSIDE`, `rad=10`, stín `DROP_SHADOW(0,4,16,0 effect/shadow)`, odsazení 6 ze všech stran, `gap0`; šířka 220 je pevná, výška podle řádků; kořen ořezává obsah.
- Hlavní komponenta `icon/copy` má zapnutý ořez obsahu (ostatní ikony ne) – drobný nesoulad ikon, v této komponentě se nepřepisuje.
- Žádná barva nevázaná na proměnnou.

### Action tile · `190:740`

Sada 8 variant · výchozí varianta `Active=False, State=Default` · rozměr 88×56

Popis ve Figmě: „Graphic tile of a toolbar panel: one click runs the action or activates the tool. Active = the tool is currently in use.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **Highlight** |
| Icon | záměna instance | **icon/highlighter** |
| Active | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Active=False, State=Default' 88x56 lay=V gap5 pad11,2,9,2 CENTER/CENTER; z=fix/fix; fill=accent/neutral/bg; rad=10
 I 'icon' 18x18 z=fix/fix; ref=mainComponent:Icon; ic=icon/highlighter stroke=icon/default w2
 T 'label' 84x13 z=fill/hug; fill=text/secondary; f=Inter Medium 11/auto CT HEIGHT trunc1; tx='Highlight'; ref=characters:Label
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Active=False, State=Hover` | kořen: `fill=accent/neutral/bg-hover` |
| `Active=False, State=Pressed` | kořen: `fill=accent/neutral/bg-pressed` |
| `Active=False, State=Focus` | kořen: `st=border/focus w2 OUTSIDE` |
| `Active=True, State=Default` | kořen: `fill=bg/surface+bg/accent-subtle`; `icon`: `ic=icon/highlighter stroke=icon/accent w2`; `label`: `fill=text/accent` |
| `Active=True, State=Hover` (proti `Active=True, State=Default`) | kořen: `fill=bg/surface+bg/accent-subtle-hover` |
| `Active=True, State=Pressed` (proti `Active=True, State=Default`) | kořen: `fill=bg/surface+bg/accent-subtle-pressed` |
| `Active=True, State=Focus` (proti `Active=True, State=Default`) | kořen: `st=border/focus w2 OUTSIDE` |

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- Mřížka 2 × 4 je úplná; stav Disabled neexistuje.
- Dlaždice má výplň už ve výchozím stavu (`accent/neutral/bg`), na rozdíl od tlačítek lišty, která jsou v Default průhledná.
- Hover a Pressed (`Active=False`) mění jen výplň; ikona (`icon/default`) a text (`text/secondary`) zůstávají.
- Focus: `border/focus` 2 px `OUTSIDE`, výplň zůstává (`accent/neutral/bg`, resp. `bg/surface+bg/accent-subtle`).
- **Změna 16. 10. 2026:** `Active=True` má ve všech stavech pod poloprůhledným `bg/accent-subtle*` výplň `bg/surface` (dříve jen poloprůhledná výplň), stejně jako vybraná Tool choice (rozhodnutí 21). Na bílém pozadí se vzhled nemění.
- Nové (oprava F-66): svislé výplně jsou 11 nahoře a 9 dole (dříve 10 a 8) ve všech 8 variantách, obsah tím vyplňuje celý kořen: 11 + 18 + 5 + 13 + 9 = 56. Poloha ikony (y = 11) a textu (y = 34) se nezměnila – dříve chybějící 2 px rozdělovalo zarovnání na střed po 1 px. Ikona s textem je o 1 px pod středem dlaždice (výplň nahoře je o 2 px větší), tak jako dřív.
- Rozměr: text je `fill` na šířku, zarovnaný na střed, zkrácený na 1 řádek (`trunc1`) - šířka dlaždice (88) tedy text neroztahuje; vodorovné odsazení jen 2 px. Kořen má pevnou šířku i výšku.
- Mezera ikona-text 5.
- Žádná barva nevázaná na proměnnou.

### Stamp chip · `190:779`

Sada 16 variant · výchozí varianta `Hue=Green, State=Default` · rozměr 85×26

Popis ve Figmě: „Stamp annotation in the Insert panel; the hue follows the meaning of the stamp.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **Approved** |
| Hue | varianta | **Green**, Red, Blue, Amber |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Hue=Green, State=Default' 85x26 lay=H gap0 pad0,10,0,10 CENTER/CENTER; z=hug/fix; fill=accent/green/bg; st=accent/green/border-strong w1 INSIDE; rad=6
 T 'label' 63x13 z=hug/hug; fill=accent/green/icon; f=Inter Semi Bold 11/auto ls0.4px LT WIDTH_AND_HEIGHT UPPER; tx='Approved'; ref=characters:Label
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Hue=Green, State=Hover` | kořen: `fill=accent/green/bg-hover` |
| `Hue=Green, State=Pressed` | kořen: `fill=accent/green/bg-pressed` |
| `Hue=Green, State=Focus` | kořen: `lay=H gap0 pad0,11,0,11 CENTER/CENTER; st=border/focus w2 OUTSIDE` |
| `Hue=Red, State=Default` | kořen: `fill=accent/red/bg; st=accent/red/border-strong w1 INSIDE`; `label`: `fill=accent/red/icon` |
| `Hue=Red, State=Hover` (proti `Hue=Red, State=Default`) | kořen: `fill=accent/red/bg-hover` |
| `Hue=Red, State=Pressed` (proti `Hue=Red, State=Default`) | kořen: `fill=accent/red/bg-pressed` |
| `Hue=Red, State=Focus` (proti `Hue=Red, State=Default`) | kořen: `lay=H gap0 pad0,11,0,11 CENTER/CENTER; st=border/focus w2 OUTSIDE` |
| `Hue=Blue, State=Default` | kořen: `fill=accent/blue/bg; st=accent/blue/border-strong w1 INSIDE`; `label`: `fill=accent/blue/icon` |
| `Hue=Blue, State=Hover` (proti `Hue=Blue, State=Default`) | kořen: `fill=accent/blue/bg-hover` |
| `Hue=Blue, State=Pressed` (proti `Hue=Blue, State=Default`) | kořen: `fill=accent/blue/bg-pressed` |
| `Hue=Blue, State=Focus` (proti `Hue=Blue, State=Default`) | kořen: `lay=H gap0 pad0,11,0,11 CENTER/CENTER; st=border/focus w2 OUTSIDE` |
| `Hue=Amber, State=Default` | kořen: `fill=accent/amber/bg; st=accent/amber/border-strong w1 INSIDE`; `label`: `fill=accent/amber/icon` |
| `Hue=Amber, State=Hover` (proti `Hue=Amber, State=Default`) | kořen: `fill=accent/amber/bg-hover` |
| `Hue=Amber, State=Pressed` (proti `Hue=Amber, State=Default`) | kořen: `fill=accent/amber/bg-pressed` |
| `Hue=Amber, State=Focus` (proti `Hue=Amber, State=Default`) | kořen: `lay=H gap0 pad0,11,0,11 CENTER/CENTER; st=border/focus w2 OUTSIDE` |

**Poznámky**

- Kořen: velikost `hug/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano (obrys `OUTSIDE` variant Focus se do rozložení nepočítá nikdy). Platí pro všechny varianty.
- Mřížka 4 × 4 je úplná a pravidelná: `fill=accent/<hue>/bg`, `st=accent/<hue>/border-strong w1 INSIDE`, text `accent/<hue>/icon`; Hover `accent/<hue>/bg-hover`, Pressed `accent/<hue>/bg-pressed` (hue = green, red, blue, amber). Stav Disabled neexistuje.
- Focus (opraveno): `border/focus` 2 px `OUTSIDE` jako u Filter chip (`78:836`); kroužek nahrazuje obrys v barvě odstínu, výplň `accent/<hue>/bg` zůstává. Dříve byl kroužek `INSIDE` a varianta byla o 2 px širší (87×26).
- Všech 16 variant má stejný rozměr 85×26 a text na stejném místě (x = 11). Obrys `OUTSIDE` se do rozložení nepočítá, proto mají varianty Focus vodorovné odsazení 11 místo 10 (10 + 1 px obrysu výchozí varianty): 11 + 63 + 11 = 85. Pro implementaci je to jeden rozměr: odsazení textu od okraje tvaru je 11 px ve všech stavech, kroužek fokusu leží vně tvaru.
- Text používá pro barvu token ikony (`accent/<hue>/icon`).
- Text je uložen jako `Approved` a zobrazen verzálkami (`UPPER`), prostrkání `ls0.4px` (0,4 px - jednotkou prostrkání jsou pixely, ne procenta), Inter Semi Bold 11.
- Rozměr: text `hug` bez zkracování, šířka roste s textem; výška 26, `rad=6`.
- Bez ikony; komponenta vystavuje jen text `Label`.
- Žádná barva nevázaná na proměnnou.

### Thumbnail item · `191:777`

Sada 8 variant · výchozí varianta `Selected=False, State=Default` · rozměr 124×183

Popis ve Figmě: „Page thumbnail of the document sidebar. Selected = the page currently shown.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Page | text | **308** |
| Preview | záměna instance | **Thumb preview/Text** |
| Selected | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Selected=False, State=Default' 124x183 lay=V gap6 pad8,8,6,8 MIN/CENTER; rad=10
 F 'page' 108x148 lay=H gap0 pad0,0,0,0 MIN/MIN; z=hug/hug; st=border/strong w1 OUTSIDE; rad=2; fx=DROP_SHADOW(0,1,3,0 effect/shadow); clip=1
  I 'preview' 108x148 lay=V gap4 pad12,10,10,10 MIN/MIN; z=fix/fix; fill=reading/paper/page; clip=1; ref=mainComponent:Preview; inst=Thumb preview/Text ov:name
 F 'number' 38x15 lay=H gap0 pad1,8,1,8 CENTER/CENTER; z=hug/hug; rad=100; clip=1
  T 'label' 22x13 z=hug/hug; fill=text/tertiary; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='308'; ref=characters:Page
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=False, State=Hover` | kořen: `fill=bg/hover` |
| `Selected=False, State=Pressed` | kořen: `fill=bg/pressed` |
| `Selected=False, State=Focus` | kořen: `st=border/focus w2 INSIDE; sil=0` |
| `Selected=True, State=Default` | kořen: `fill=bg/accent-subtle`; `page`: `st=bg/accent w2 OUTSIDE`; `number`: `fill=bg/accent`; `number>label`: `fill=icon/on-accent; f=Inter Semi Bold 11/auto LT WIDTH_AND_HEIGHT` |
| `Selected=True, State=Hover` (proti `Selected=True, State=Default`) | kořen: `fill=bg/accent-subtle-hover` |
| `Selected=True, State=Pressed` (proti `Selected=True, State=Default`) | kořen: `fill=bg/accent-subtle-pressed` |
| `Selected=True, State=Focus` (proti `Selected=True, State=Default`) | kořen: `st=border/focus w2 INSIDE; sil=0` |

**Poznámky**

- Kořen: velikost `hug/hug` (vodorovně/svisle), ořez obsahu ne. Obrys má kořen jen ve variantách Focus a do rozložení se nepočítá (`sil=0`, kroužek uvnitř obsah neposouvá); údaj výpisu „obrys se počítá do rozložení“ u ostatních variant se týká kořene bez obrysu a rozměr nemění (opraveno 17. 10. 2026, kontrola kroku 5, FI-4).
- Mřížka 2 × 4 je úplná; stav Disabled neexistuje.
- Vnořená komponenta: `preview` je instance `Thumb preview/Text` (přepsané pole jen `name`), zaměnitelná vlastností `Preview` (další náhledy: `Thumb preview/Heading` `191:558`, `Thumb preview/Figure` `191:575`). Náhledy samy nemají žádné vlastnosti.
- Rámec `page` je `hug` a obepíná instanci náhledu, která má pevný rozměr 108×148 - velikost i poměr stran stránky jsou v nákresu pevné. `page` má obrys `border/strong` 1 px `OUTSIDE`, `rad=2`, stín `DROP_SHADOW(0,1,3,0 effect/shadow)` a `clip=1`.
- Selected: kromě výplně kořene se mění obrys stránky na `bg/accent` 2 px `OUTSIDE` (token pozadí použitý jako obrys), štítek čísla dostane výplň `bg/accent` a text `icon/on-accent` (token ikony použitý jako barva textu) v řezu Semi Bold místo Medium.
- Štítek `number` nemá v nevybraném stavu žádnou výplň; `rad=100` (pilulka), odsazení 1,8,1,8.
- Focus: `border/focus` 2 px `INSIDE` na kořeni (ne na stránce; **změna 14. 10. 2026**, rozhodnutí Q-2: položka seznamu má kroužek uvnitř, dříve `OUTSIDE`); od obrysu vybrané stránky `bg/accent` ho dělí 6 px; `Selected=False` bez výplně, `Selected=True` si ponechává `bg/accent-subtle` i obrys stránky `bg/accent`.
- Rozměr je dán obsahem: šířka 8 + 108 + 8 = 124, výška 8 + 148 + 6 + 15 + 6 = 183; `rad=10`.
- Žádná barva nevázaná na proměnnou.

### Thumb preview/Text · `191:539`

Samostatná komponenta · výchozí varianta `Thumb preview/Text` (jediná) · rozměr 108×148

Popis ve Figmě: „bez popisu“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Thumb preview/Text' 108x148 lay=V gap4 pad12,10,10,10 MIN/MIN; fill=reading/paper/page
 F 'heading' 48x3 z=fix/fix; fill=sample/gray-mid; rad=1; clip=1
 F 'space' 88x3 z=fix/fix; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 81x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 53x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'space' 88x3 z=fix/fix; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 84x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 77x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 35x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'space' 88x3 z=fix/fix; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 62x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ano, obrys se počítá do rozložení ano.
- Ilustrace obsahu stránky (zástupné čáry místo textu): 18 potomků - 1 `heading`, 3 `space`, 14 `line`. Strom je úplný.
- Používá se jako hodnota záměny `Preview` komponenty Thumbnail item (`191:777`), kde je výchozí.
- Všechny uzly mají pevný rozměr (`z=fix/fix`), komponenta 108×148 se neroztahuje.
- Barvy jsou tokeny `reading/paper/page`, `sample/gray-mid`, `sample/gray-light`; rámce `space` nemají výplň.
- Žádná barva nevázaná na proměnnou.

### Thumb preview/Heading · `191:558`

Samostatná komponenta · výchozí varianta `Thumb preview/Heading` (jediná) · rozměr 108×148

Popis ve Figmě: „bez popisu“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Thumb preview/Heading' 108x148 lay=V gap4 pad12,10,10,10 MIN/MIN; fill=reading/paper/page
 F 'heading' 62x5 z=fix/fix; fill=sample/gray-dark; rad=1; clip=1
 F 'space' 88x2 z=fix/fix; clip=1
 F 'heading' 35x3 z=fix/fix; fill=sample/gray-mid; rad=1; clip=1
 F 'space' 88x6 z=fix/fix; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 83x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 44x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'space' 88x3 z=fix/fix; clip=1
 F 'heading' 40x3 z=fix/fix; fill=sample/gray-mid; rad=1; clip=1
 F 'space' 88x1 z=fix/fix; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 79x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 66x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ano, obrys se počítá do rozložení ano.
- Ilustrace stránky s nadpisy: 16 potomků - 3 `heading`, 4 `space`, 9 `line`. Strom je úplný.
- Používá se jako hodnota záměny `Preview` komponenty Thumbnail item (`191:777`).
- Všechny uzly mají pevný rozměr (`z=fix/fix`), komponenta 108×148 se neroztahuje.
- Barvy jsou tokeny `reading/paper/page`, `sample/gray-dark`, `sample/gray-mid`, `sample/gray-light`; rámce `space` nemají výplň.
- Žádná barva nevázaná na proměnnou.

### Thumb preview/Figure · `191:575`

Samostatná komponenta · výchozí varianta `Thumb preview/Figure` (jediná) · rozměr 108×148

Popis ve Figmě: „bez popisu“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Thumb preview/Figure' 108x148 lay=V gap4 pad12,10,10,10 MIN/MIN; fill=reading/paper/page
 F 'heading' 44x3 z=fix/fix; fill=sample/gray-mid; rad=1; clip=1
 F 'space' 88x3 z=fix/fix; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 84x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 62x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'space' 88x3 z=fix/fix; clip=1
 F 'figure' 88x46 lay=H gap4 pad0,0,0,0 MIN/MAX; z=fix/fix; clip=1
  F 'bar' 14x23 z=fix/fix; fill=sample/blue-soft; rad=1; clip=1
  F 'bar' 14x37 z=fix/fix; fill=sample/orange-soft; rad=1; clip=1
  F 'bar' 14x30 z=fix/fix; fill=sample/blue-soft; rad=1; clip=1
  F 'bar' 14x46 z=fix/fix; fill=sample/orange-soft; rad=1; clip=1
  F 'bar' 14x18 z=fix/fix; fill=sample/blue-soft; rad=1; clip=1
 F 'space' 88x3 z=fix/fix; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 88x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 76x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'line' 40x2 z=fix/fix; fill=sample/gray-light; rad=1; clip=1
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ano, obrys se počítá do rozložení ano.
- Ilustrace stránky se sloupcovým grafem: 12 potomků - 1 `heading`, 3 `space`, 7 `line` a rámec `figure` s pěti sloupci `bar`. Strom je úplný.
- Používá se jako hodnota záměny `Preview` komponenty Thumbnail item (`191:777`).
- Rámec `figure` je vodorovné rozložení se zarovnáním sloupců ke spodní hraně (`MIN/MAX`), pět sloupců šířky 14 s mezerou 4.
- Všechny uzly mají pevný rozměr (`z=fix/fix`), komponenta 108×148 se neroztahuje.
- Barvy jsou tokeny `reading/paper/page`, `sample/gray-mid`, `sample/gray-light`, `sample/blue-soft`, `sample/orange-soft`; rámce `space` a `figure` nemají výplň.
- Žádná barva nevázaná na proměnnou.

### Outline row · `191:840`

Sada 8 variant · výchozí varianta `Selected=False, State=Default` · rozměr 276×30

Popis ve Figmě: „Item of the document outline tree. Level 2/Level 3 indent the row; Level 3 requires Level 2 (switch both on for the third level). Chevron is chevron-right (collapsed) or chevron-down (expanded). Has children = false hides the chevron of a leaf row, its 16 px slot stays, so the titles of leaf and expandable rows align.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Title | text | **Market Analysis** |
| Level 2 | přepínač | **false** |
| Level 3 | přepínač | **false** |
| Has children | přepínač | **true** |
| Chevron | záměna instance | **icon/chevron-right** |
| Selected | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Selected=False, State=Default' 276x30 lay=H gap4 pad0,8,0,6 MIN/CENTER; z=fix/fix; rad=8
 F 'indent-1' 14x1 hid=1; z=fix/fix; clip=1; ref=visible:Level 2
 F 'indent-2' 14x1 hid=1; z=fix/fix; clip=1; ref=visible:Level 3
 F 'chevron-wrap' 16x16 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; clip=1
  I 'chevron' 14x14 z=fix/fix; ref=visible:Has children,mainComponent:Chevron; ic=icon/chevron-right stroke=icon/muted w2
 T 'label' 242x16 z=fill/hug; fill=text/secondary; f=Inter Regular 13/auto LT HEIGHT trunc1; tx='Market Analysis'; ref=characters:Title
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=False, State=Hover` | `~ .: fill=bg/hover`<br>`~ label: fill=text/primary` |
| `Selected=False, State=Pressed` | `~ .: fill=bg/pressed`<br>`~ label: fill=text/primary` |
| `Selected=False, State=Focus` | `~ .: st=border/focus w2 INSIDE; sil=0` |
| `Selected=True, State=Default` | `~ .: fill=bg/accent-subtle`<br>`~ chevron-wrap>chevron: ic=icon/chevron-right stroke=icon/accent w2`<br>`~ label: fill=text/accent; f=Inter Medium 13/auto LT HEIGHT trunc1` |
| `Selected=True, State=Hover` (proti `Selected=True, State=Default`) | `~ .: fill=bg/accent-subtle-hover` |
| `Selected=True, State=Pressed` (proti `Selected=True, State=Default`) | `~ .: fill=bg/accent-subtle-pressed` |
| `Selected=True, State=Focus` (proti `Selected=True, State=Default`) | `~ .: st=border/focus w2 INSIDE; sil=0` |

**Poznámky**

- Kořen: velikost `fix/fix` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano, ve variantách Focus ne (`sil=0`, kroužek uvnitř obsah neposouvá).
- **Změna je jen v popisu komponenty**, kresba, vlastnosti ani varianty se nezměnily. Popis nově říká, že `Level 3` vyžaduje `Level 2` a že `Has children = false` skrývá šipku listové položky, zatímco její místo 16 px zůstává.
- Šipka rozbalení: instance `chevron` 14×14 v pevném rámci `chevron-wrap` 16×16. Viditelnost šipky řídí přepínač `Has children` (vazba `visible` na instanci `chevron`); rámec `chevron-wrap` záměrně není svázán s žádnou vlastností, takže místo 16 px zůstává i u položky bez potomků a titulky listových a rozbalitelných řádků jsou zarovnané. Zvláštní přepínač `Show chevron` proto komponenta nemá – byl by to druhý přepínač téže vrstvy.
- Úrovně odsazení: nakresleny jsou tři. Odsazení tvoří dva skryté prázdné rámce `indent-1` a `indent-2`, každý 14×1 bez výplně ve všech 8 variantách, svázané s přepínači `Level 2` a `Level 3`. Při mezeře rozložení 4 přidá každý zobrazený rámec 14 + 4 = 18 px před šipku. Údaj `z=fix/fix` je u nich čten ve skrytém stavu (skrytá vrstva se čte jako `fix`). Úroveň 1 = oba přepínače vypnuté, úroveň 2 = jen `Level 2`, úroveň 3 = oba zapnuté.
- `Level 2` a `Level 3` jsou dva nezávislé přepínače, ne jedna vlastnost s číslem úrovně: technicky lze zapnout `Level 3` bez `Level 2` (pak jedno odsazení), popis to ale zakazuje – `Level 3` vyžaduje `Level 2`. Hlubší úroveň než třetí komponenta nemá.
- Rozbalený stav není varianta: podle popisu se mění záměnou `Chevron` (`icon/chevron-right` = sbaleno, `icon/chevron-down` = rozbaleno). Výchozí je `icon/chevron-right`.
- Barva šipky je `icon/muted`, ve vybraném řádku `icon/accent`; v Hover a Pressed se nemění.
- Selected: text `text/accent` a řez Medium místo Regular. Vrstva `label` má ve všech 8 variantách stejný rozměr 242×16 (`fill/hug`, `trunc1`) - změna řezu rozměr vrstvy nemění.
- Hover a Pressed (`Selected=False`) mění text na `text/primary`; Focus text nemění (`text/secondary`).
- Focus: `border/focus` 2 px `INSIDE` (**změna 14. 10. 2026**, rozhodnutí Q-2: řádky seznamů a nabídek mají kroužek uvnitř, dříve `OUTSIDE`); `Selected=False` bez výplně, `Selected=True` si ponechává `bg/accent-subtle`.
- Mřížka 2 × 4 je úplná; stav Disabled neexistuje.
- Rozměr: `label` je `fill` se zkrácením na 1 řádek (`trunc1`), řádek se roztahuje na šířku (nakresleno 276 = 6 + 16 + 4 + 242 + 8); výška 30, `rad=8`. Odsazení je nesouměrné: vlevo 6, vpravo 8.
- Žádná barva nevázaná na proměnnou.

### Bookmark row · `191:911`

Sada 8 variant · výchozí varianta `Selected=False, State=Default` · rozměr 276×46

Popis ve Figmě: „Bookmarked page in the document sidebar. The Auto tag marks bookmarks generated automatically from the main chapters. The tag is an exposed Tag instance, its Label can be set on the row.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Title | text | **Page 17** |
| Subtitle | text | **Executive Summary** |
| Auto generated | přepínač | **true** |
| Selected | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus |
| tag → Label | text vystavené instance `Tag` | **Auto** |
| tag → State | varianta vystavené instance `Tag` | **Default** (hodnoty viz karta Tag) |

**Stavba výchozí varianty**

```text
C 'Selected=False, State=Default' 276x46 lay=H gap10 pad8,10,8,10 MIN/CENTER; z=fix/hug; rad=8
 I 'icon' 16x16 z=fix/fix; clip=1; ic=icon/bookmark stroke=icon/accent w2 fill=icon/accent
 F 'text' 175x30 lay=V gap1 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  T 'title' 175x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='Page 17'; ref=characters:Title
  T 'subtitle' 175x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='Executive Summary'; ref=characters:Subtitle
 I 'tag' 45x21 lay=H gap0 pad3,9,3,9 MIN/CENTER; z=hug/hug; fill=bg/subtle; st=border/default w1 INSIDE; rad=100; ref=visible:Auto generated; inst=Tag{State=Default} [Label='Auto'] ov:height,name,width; exp=1
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=False, State=Hover` | `~ .: fill=bg/hover` |
| `Selected=False, State=Pressed` | `~ .: fill=bg/pressed` |
| `Selected=False, State=Focus` | `~ .: st=border/focus w2 INSIDE; sil=0` |
| `Selected=True, State=Default` | `~ .: fill=bg/accent-subtle`<br>`~ text>title: fill=text/accent` |
| `Selected=True, State=Hover` (proti `Selected=True, State=Default`) | `~ .: fill=bg/accent-subtle-hover` |
| `Selected=True, State=Pressed` (proti `Selected=True, State=Default`) | `~ .: fill=bg/accent-subtle-pressed` |
| `Selected=True, State=Focus` (proti `Selected=True, State=Default`) | `~ .: st=border/focus w2 INSIDE; sil=0` |

**Poznámky**

- Kořen: velikost `fix/hug` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano, ve variantách Focus ne (`sil=0`, kroužek uvnitř obsah neposouvá).
- Mřížka 2 × 4 je úplná; stav Disabled neexistuje.
- **Nové – štítek je vystavená instance.** `tag` je instance `Tag{State=Default}` s textem `Label='Auto'` (přepsaná pole `height`, `name`, `width`) a má ve všech 8 variantách `exp=1`: text štítku (`Label`) se nastavuje přímo na instanci řádku, dříve to nešlo. Vystavena je celá instance `Tag`, tedy i její `State`. Viditelnost štítku dál řídí přepínač `Auto generated`. Štítek se ve stavech řádku ani ve výběru nemění.
- Ikona `icon/bookmark` není zaměnitelná (není svázaná s žádnou vlastností), má `clip=1` a je vyplněná: vektor `glyph` má ve všech 8 variantách výplň `icon/accent` (ve stromu `fill=` za `ic=`) i tah `icon/accent` w2. Přepsaná pole vektoru jsou `fills` a `strokes`; hlavní komponenta `icon/bookmark` výplň nemá, jen tah `icon/default` w2. Barva je stejná i v nevybraném řádku.
- Selected: mění se jen výplň kořene a barva titulku na `text/accent`; podtitul zůstává `text/muted`.
- Focus: `border/focus` 2 px `INSIDE` (**změna 14. 10. 2026**, rozhodnutí Q-2: řádky seznamů a nabídek mají kroužek uvnitř, dříve `OUTSIDE`); `Selected=False` bez výplně, `Selected=True` si ponechává `bg/accent-subtle`.
- Rozměr: blok `text` je `fill`, oba texty `fill` se zkrácením na 1 řádek (`trunc1`), štítek `hug` - řádek se roztahuje na šířku (nakresleno 276 = 10 + 16 + 10 + 175 + 10 + 45 + 10); výška 46 = 8 + 30 + 8, `rad=8`. Delší text štítku blok `text` zúží.
- Žádná barva nevázaná na proměnnou (skrytá výplň `#ffffff`, kterou instance `icon` dědila z hlavní komponenty `icon/bookmark`, byla 13. 10. 2026 odstraněna).

### Note row · `191:998`

Sada 8 variant · výchozí varianta `Selected=False, State=Default` · rozměr 276×63

Popis ve Figmě: „Annotation in the Notes tab of the document sidebar. The badge shows the annotation type; Selected is synchronized with the selection on the page. The selected row draws the author in text/accent.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Author | text | **Jakub Melka** |
| Date | text | **Sep 28** |
| Text | text | **Check these numbers against the Q3 report before publishing.** |
| Selected | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Selected=False, State=Default' 276x63 lay=H gap10 pad8,10,8,8 MIN/MIN; z=fix/hug; rad=8
 I 'badge' 32x32 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=accent/amber/bg; rad=16; inst=Tool icon{Hue=Amber, Size=32} [Icon=icon/highlighter] ov:name,height,width,strokes; exp=1
 F 'text' 216x47 lay=V gap2 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  F 'head' 216x15 lay=H gap8 pad0,0,0,0 MIN/CENTER; z=fill/hug; clip=1
   T 'author' 171x15 z=fill/hug; fill=text/primary; f=Inter Semi Bold 12/auto LT HEIGHT trunc1; tx='Jakub Melka'; ref=characters:Author
   T 'date' 37x13 z=hug/hug; fill=text/muted; f=Inter Regular 11/auto LT WIDTH_AND_HEIGHT; tx='Sep 28'; ref=characters:Date
  T 'body' 216x30 z=fill/hug; fill=text/secondary; f=Inter Regular 12/auto LT HEIGHT trunc2; tx='Check these numbers against the Q3 report …'; ref=characters:Text
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=False, State=Hover` | `~ .: fill=bg/hover` |
| `Selected=False, State=Pressed` | `~ .: fill=bg/pressed` |
| `Selected=False, State=Focus` | `~ .: st=border/focus w2 INSIDE; sil=0` |
| `Selected=True, State=Default` | `~ .: fill=bg/accent-subtle`<br>`~ text>head>author: fill=text/accent` |
| `Selected=True, State=Hover` (proti `Selected=True, State=Default`) | `~ .: fill=bg/accent-subtle-hover` |
| `Selected=True, State=Pressed` (proti `Selected=True, State=Default`) | `~ .: fill=bg/accent-subtle-pressed` |
| `Selected=True, State=Focus` (proti `Selected=True, State=Default`) | `~ .: st=border/focus w2 INSIDE; sil=0` |

**Poznámky**

- Kořen: velikost `fix/hug` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano, ve variantách Focus ne (`sil=0`, kroužek uvnitř obsah neposouvá).
- Mřížka 2 × 4 je úplná; stav Disabled neexistuje.
- **Nové – vybraný řádek obarvuje autora.** Ve všech čtyřech variantách `Selected=True` má hlavní řádek `author` barvu `text/accent` (dříve `text/primary`), stejně jako titulek vybraného Bookmark row (`191:911`). Řez (Semi Bold 12) ani rozměr se nemění; `date` zůstává `text/muted`, `body` `text/secondary`.
- Vnořená komponenta: `badge` je instance `Tool icon{Hue=Amber, Size=32}` s `Icon=icon/highlighter`, 32×32, `rad=16`, a je vystavená (`exp=1`) – její `Hue` a `Icon` (typ anotace) jdou nastavit na instanci řádku. Rozměr, výplň `accent/amber/bg` a zaoblení jsou z hlavní komponenty a `badge` nemá žádný tah. Přepsání `height`, `width` patří vnořené instanci `icon` (16×16) a `strokes` jejímu vektoru `glyph` (tah `accent/amber/icon` w2).
- Focus: `border/focus` 2 px `INSIDE` (**změna 14. 10. 2026**, rozhodnutí Q-2: řádky seznamů a nabídek mají kroužek uvnitř, dříve `OUTSIDE`); `Selected=False` bez výplně, `Selected=True` si ponechává `bg/accent-subtle`.
- Obsah je zarovnán nahoru (`MIN/MIN`), odznak tedy sedí u horního okraje řádku; odsazení je nesouměrné: vpravo 10, vlevo 8.
- Rozměr: blok `text` je `fill`; `author` `fill` se zkrácením na 1 řádek, `date` `hug` bez zkracování, `body` `fill` se zkrácením na 2 řádky (`trunc2`). Výška je dána obsahem (8 + 15 + 2 + 30 + 8 = 63 při dvou řádcích textu), `rad=8`.
- Výpis zkracuje text vrstvy `body` na 42 znaků; plné znění je ve vlastnosti `Text`.
- Žádná barva nevázaná na proměnnou.

### Attachment row · `191:1045`

Sada 8 variant · výchozí varianta `Selected=False, State=Default` · rozměr 276×48

Popis ve Figmě: „File embedded in the document. A click opens it, the arrow saves it to a file. Selected marks the row chosen in the panel. The arrow is an exposed instance of Icon button with its own states.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Name | text | **budget-2026.xlsx** |
| Meta | text | **Spreadsheet · 84 kB** |
| Selected | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Selected=False, State=Default' 276x48 lay=H gap10 pad8,6,8,8 MIN/CENTER; z=fix/hug; rad=8
 I 'badge' 32x32 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=accent/neutral/bg; rad=16; inst=Tool icon{Hue=Neutral, Size=32} [Icon=icon/file-text] ov:name,height,width,strokes; exp=1
 F 'text' 186x30 lay=V gap1 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  T 'name' 186x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='budget-2026.xlsx'; ref=characters:Name
  T 'meta' 186x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='Spreadsheet · 84 kB'; ref=characters:Meta
 I 'save' 24x24 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=6; inst=Icon button{Size=24, State=Default} [Icon=icon/download] ov:name,height,width,strokes; exp=1
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=False, State=Hover` | kořen: `fill=bg/hover`; `save`: `inst=Icon button{Size=24, State=Default} [Icon=icon/download] ov:name,strokes` |
| `Selected=False, State=Pressed` | kořen: `fill=bg/pressed`; `save`: `inst=Icon button{Size=24, State=Default} [Icon=icon/download] ov:name,strokes` |
| `Selected=False, State=Focus` | kořen: `st=border/focus w2 INSIDE; sil=0` |
| `Selected=True, State=Default` | kořen: `fill=bg/accent-subtle`; `text>name`: `fill=text/accent` |
| `Selected=True, State=Hover` (proti `Selected=True, State=Default`) | kořen: `fill=bg/accent-subtle-hover`; `save`: `inst=Icon button{Size=24, State=Default} [Icon=icon/download] ov:name,strokes` |
| `Selected=True, State=Pressed` (proti `Selected=True, State=Default`) | kořen: `fill=bg/accent-subtle-pressed`; `save`: `inst=Icon button{Size=24, State=Default} [Icon=icon/download] ov:name,strokes` |
| `Selected=True, State=Focus` (proti `Selected=True, State=Default`) | kořen: `st=border/focus w2 INSIDE; sil=0` |

**Poznámky**

- Kořen: velikost `fix/hug` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano, ve variantách Focus ne (`sil=0`, kroužek uvnitř obsah neposouvá).
- **Nové (13. 10. 2026):** vlastnost `Selected` (False, True) – čtyři nové varianty `Selected=True`, původní čtyři jsou přejmenované na `Selected=False, State=…` (instance na ně ukazují dál). Výběr je nakreslen jako u Bookmark row (`191:911`): výplň kořene `bg/accent-subtle` (Hover `bg/accent-subtle-hover`, Pressed `bg/accent-subtle-pressed`), název `name` v `text/accent`; `meta` zůstává `text/muted`. Focus: `border/focus` 2 px `INSIDE`, `sil=0` (opraveno 17. 10. 2026 podle Figmy `191:1035`, `706:149964` a tabulky karty; text tu dříve uváděl zastaralé `OUTSIDE`); `Selected=False` bez výplně, `Selected=True` si ponechává `bg/accent-subtle`. Mřížka 2 × 4 je úplná; stav Disabled neexistuje.
- **Nové:** šipka `save` je vystavená instance Icon button (`706:147674`, `Size=24`, `exp=1`) s `Icon=icon/download`; dřív to byla holá ikona 16×16 bez stavů. Tlačítko má vlastní stavy (Hover, Pressed, Focus, Disabled), nastavují se na vnořené instanci. Ikona má teď 14×14 (dřív 16×16) – velikost ikony tlačítka `Size=24`.
- Barva ikony `save` (výpis ji u instance Icon button neukazuje, je to přepsání tahu vektoru `glyph`): Default a Focus řádku `icon/muted` (bez přepsání), Hover a Pressed řádku `icon/default` – v obou hodnotách `Selected`. **Změna (F-80):** dřív byla v Hover a Pressed `icon/strong`; teď se chová jako ostatní vedlejší ikony řádků (`more`, `chevron`: `icon/muted` → `icon/default`). Rozdíl `ov:` ve výpisu variant Hover a Pressed je právě toto přepsání.
- Pravé odsazení kořene je 6 (dřív 10), aby střed ikony zůstal na stejném místě (x = 258 při šířce 276): tlačítko 24×24 leží na x 246–270, y 12–36. Blok `text` je o 4 px užší (186 místo 190).
- Vnořená komponenta: `badge` je instance `Tool icon{Hue=Neutral, Size=32}` s `Icon=icon/file-text`, 32×32, `rad=16`, výplň `accent/neutral/bg`. Výpis ji teď uvádí jako vystavenou (`exp=1`, ve všech 8 variantách) a s hodnotou `Size=32`; původní karta uváděla, že `Hue` ani `Icon` vystavené nejsou. Tato oprava se vrstvy `badge` nedotkla – změna přišla souběžně odjinud (úpravy komponenty Tool icon), je potřeba ji ověřit v kartě Tool icon.
- Rozměr: blok `text` je `fill`, oba texty `fill` se zkrácením na 1 řádek (`trunc1`) - řádek se roztahuje na šířku (nakresleno 276 = 8 + 32 + 10 + 186 + 10 + 24 + 6); výška 48 = 8 + 32 + 8, `rad=8`.
- Odsazení je nesouměrné: vpravo 6, vlevo 8.
- Žádná barva nevázaná na proměnnou.

### Search result row · `191:1073`

Sada 8 variant · výchozí varianta `Selected=False, State=Default` · rozměr 276×62

Popis ve Figmě: „One occurrence found in the document, shown in the Search tab of the sidebar. The found term inside the snippet is Semi Bold in text/primary (the application styles the match this way); the rest of the snippet stays Regular in text/secondary.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Page | text | **Page 308** |
| Snippet | text | **…regional revenue grew by 18 % compared with the previous year…** |
| Selected | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Selected=False, State=Default' 276x62 lay=V gap3 pad8,10,8,10 MIN/MIN; z=fix/hug; rad=8
 T 'page' 51x13 z=hug/hug; fill=text/muted; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='Page 308'; ref=characters:Page
 T 'snippet' 256x30 z=fill/hug; fill=mixed; f=mixed 12/auto LT HEIGHT trunc2; tx='…regional revenue grew by 18 % compared wi…'; ref=characters:Snippet
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=False, State=Hover` | `~ .: fill=bg/hover` |
| `Selected=False, State=Pressed` | `~ .: fill=bg/pressed` |
| `Selected=False, State=Focus` | `~ .: st=border/focus w2 INSIDE; sil=0` |
| `Selected=True, State=Default` | `~ .: fill=bg/accent-subtle`<br>`~ page: fill=text/accent` |
| `Selected=True, State=Hover` (proti `Selected=True, State=Default`) | `~ .: fill=bg/accent-subtle-hover` |
| `Selected=True, State=Pressed` (proti `Selected=True, State=Default`) | `~ .: fill=bg/accent-subtle-pressed` |
| `Selected=True, State=Focus` (proti `Selected=True, State=Default`) | `~ .: st=border/focus w2 INSIDE; sil=0` |

**Poznámky**

- Kořen: velikost `fix/hug` (vodorovně/svisle), ořez obsahu ne, obrys se počítá do rozložení ano, ve variantách Focus ne (`sil=0`, kroužek uvnitř obsah neposouvá).
- Mřížka 2 × 4 je úplná; stav Disabled neexistuje.
- **Nové – zvýraznění nalezeného výrazu.** Úryvek `snippet` má ve všech 8 variantách dva styly v jedné textové vrstvě (ve výpisu `fill=mixed; f=mixed`): znaky 0–10 `'…regional '` Inter Regular 12, `text/secondary`; znaky 10–17 `'revenue'` (nalezený výraz) Inter **Semi Bold** 12, `text/primary`; znaky 17–63 `' grew by 18 % … year…'` Inter Regular 12, `text/secondary`. Velikost písma, řádkování a rozměr vrstvy (256×30) se nezměnily. Stejně styl výrazu kreslí instance v rámci `pdf4qt-document-search`.
- Zvýraznění je styl rozsahu znaků, ne vlastnost: vlastnost `Snippet` nese jen text. Když se text nastaví vlastností na instanci, dostane celý úryvek styl prvního znaku (Regular, `text/secondary`) a výraz si instance musí označit sama přepsáním stylu rozsahu. Aplikace označí výraz stejně: Semi Bold a `text/primary`, zbytek beze změny.
- Selected: mění se výplň kořene a barva textu `page` na `text/accent`; úryvek zůstává `text/secondary` a výraz `text/primary` i ve vybraném řádku.
- Focus: `border/focus` 2 px `INSIDE` (**změna 14. 10. 2026**, rozhodnutí Q-2: řádky seznamů a nabídek mají kroužek uvnitř, dříve `OUTSIDE`); `Selected=False` bez výplně, `Selected=True` si ponechává `bg/accent-subtle`.
- Rozměr: `page` je `hug` bez zkracování, `snippet` `fill` se zkrácením na 2 řádky (`trunc2`) - řádek se roztahuje na šířku (nakresleno 276 = 10 + 256 + 10), výška je dána obsahem (8 + 13 + 3 + 30 + 8 = 62 při dvou řádcích úryvku), `rad=8`.
- Bez ikony a bez vnořených komponent.
- Výpis zkracuje text vrstvy `snippet` na 42 znaků; plné znění je ve vlastnosti `Snippet`.
- Žádná barva nevázaná na proměnnou (ověřeno i po jednotlivých úsecích textu).

## 6. Panely dokumentu, barvy a náhledy

V popisech tří komponent této skupiny (`192:1032`, `206:805`, `171:4545`) vrátila Figma entitu `&quot;`; v kartách je přepsána jako uvozovka `"`.

### Signature card · `192:1032`

Sada 6 variant · výchozí varianta `Status=Valid, Expanded=False` · rozměr 276×127

Popis ve Figmě: „Signature or timestamp in the Signatures tab. The collapsed card answers "is it valid?"; the expanded card shows details and the certificate chain from the signer to the root. The expanded part of each Status is an example of that status: Warning has an untrusted root certificate, Invalid a revoked certificate of the signer. The expand toggle is an exposed instance of Icon button with its own states.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Signer | text | **Jane Novak** |
| Meta | text | **Digital signature · Sep 28, 2026** |
| Summary | text | **The document has not been changed since it was signed.** |
| Status | varianta | **Valid**, Warning, Invalid |
| Expanded | varianta | **False**, True |

**Stavba výchozí varianty**

```text
C 'Status=Valid, Expanded=False' 276x127 lay=V gap10 pad12,12,12,12 MIN/MIN; z=fix/hug; fill=bg/surface; st=border/default w1 INSIDE; rad=12
 F 'header' 250x32 lay=H gap10 pad0,24,0,0 MIN/CENTER; z=fill/hug
  F 'status-badge' 32x32 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=accent/green/bg; rad=16; clip=1
   I 'icon' 16x16 z=fix/fix; ic=icon/shield-check stroke=accent/green/icon w2
  F 'text' 184x30 lay=V gap1 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
   T 'signer' 184x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='Jane Novak'; ref=characters:Signer
   T 'meta' 184x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='Digital signature · Sep 28, 2026'; ref=characters:Meta
  I 'toggle' 24x24 at=231,4 abs; con=MAX/CENTER; lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=6; inst=Icon button{Size=24, State=Default} [Icon=icon/chevron-down] ov:layoutPositioning,name,strokes; exp=1
 F 'status-pill' 45x19 lay=H gap0 pad2,8,2,8 MIN/MIN; z=hug/hug; fill=accent/green/bg; st=accent/green/border w1 INSIDE; rad=100; clip=1
  T 'label' 27x13 z=hug/hug; fill=accent/green/icon; f=Inter Semi Bold 11/auto LT WIDTH_AND_HEIGHT; tx='Valid'
 T 'summary' 250x30 z=fill/hug; fill=text/secondary; f=Inter Regular 12/auto LT HEIGHT; tx='The document has not been changed since it was signed.'; ref=characters:Summary
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Status=Valid, Expanded=True` | kořen: `s=276x382`<br>`header>toggle: inst=Icon button{Size=24, State=Default} [Icon=icon/chevron-up] ov:layoutPositioning,name,strokes`<br>přidané uzly: blok A pod tabulkou |
| `Status=Warning, Expanded=False` | `header>status-badge: fill=accent/amber/bg`<br>`header>status-badge>icon: ic=icon/alert-triangle stroke=accent/amber/icon w2`<br>`status-pill: s=104x19; fill=accent/amber/bg; st=accent/amber/border w1 INSIDE`<br>`status-pill>label: s=86x13; fill=accent/amber/icon; tx='Needs attention'` |
| `Status=Warning, Expanded=True` | kořen: `s=276x403`<br>`header>status-badge: fill=accent/amber/bg`<br>`header>status-badge>icon: ic=icon/alert-triangle stroke=accent/amber/icon w2`<br>`header>toggle: inst=Icon button{Size=24, State=Default} [Icon=icon/chevron-up] ov:layoutPositioning,name,strokes`<br>`status-pill: s=104x19; fill=accent/amber/bg; st=accent/amber/border w1 INSIDE`<br>`status-pill>label: s=86x13; fill=accent/amber/icon; tx='Needs attention'`<br>přidané uzly: blok B pod tabulkou |
| `Status=Invalid, Expanded=False` | `header>status-badge: fill=accent/red/bg`<br>`header>status-badge>icon: ic=icon/x-circle stroke=accent/red/icon w2`<br>`status-pill: s=53x19; fill=accent/red/bg; st=accent/red/border w1 INSIDE`<br>`status-pill>label: s=35x13; fill=accent/red/icon; tx='Invalid'` |
| `Status=Invalid, Expanded=True` | kořen: `s=276x382`<br>`header>status-badge: fill=accent/red/bg`<br>`header>status-badge>icon: ic=icon/x-circle stroke=accent/red/icon w2`<br>`header>toggle: inst=Icon button{Size=24, State=Default} [Icon=icon/chevron-up] ov:layoutPositioning,name,strokes`<br>`status-pill: s=53x19; fill=accent/red/bg; st=accent/red/border w1 INSIDE`<br>`status-pill>label: s=35x13; fill=accent/red/icon; tx='Invalid'`<br>přidané uzly: blok A pod tabulkou se třemi řádky jinak (blok C) |

Blok A – uzly přidané ve variantě `Status=Valid, Expanded=True` (proti výchozí variantě):

```text
+ divider#1: F 250x1 z=fill/fix; fill=border/divider; clip=1
+ details: F 250x70 lay=V gap6 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ details>detail#1: F 250x13 lay=H gap8 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ details>detail#1>key: T 92x13 z=fix/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT; tx='Signed on'
+ details>detail#1>value: T 150x13 z=fill/hug; fill=text/primary; f=Inter Medium 11/auto LT HEIGHT; tx='Sep 28, 2026, 14:02'
+ details>detail#2: F 250x13 lay=H gap8 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ details>detail#2>key: T 92x13 z=fix/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT; tx='Hash algorithm'
+ details>detail#2>value: T 150x13 z=fill/hug; fill=text/primary; f=Inter Medium 11/auto LT HEIGHT; tx='SHA-256'
+ details>detail#3: F 250x13 lay=H gap8 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ details>detail#3>key: T 92x13 z=fix/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT; tx='Covers'
+ details>detail#3>value: T 150x13 z=fill/hug; fill=text/primary; f=Inter Medium 11/auto LT HEIGHT; tx='Entire document'
+ details>detail#4: F 250x13 lay=H gap8 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ details>detail#4>key: T 92x13 z=fix/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT; tx='Timestamp'
+ details>detail#4>value: T 150x13 z=fill/hug; fill=text/primary; f=Inter Medium 11/auto LT HEIGHT; tx='Embedded, valid'
+ divider#2: F 250x1 z=fill/fix; fill=border/divider; clip=1
+ caption: T 88x13 z=hug/hug; fill=text/muted; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='Certificate chain'
+ chain: F 250x120 lay=V gap0 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ chain>certificate#1: F 250x39 lay=H gap10 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ chain>certificate#1>rail: F 8x39 lay=V gap3 pad4,0,0,0 MIN/CENTER; z=fix/fill; clip=1
+ chain>certificate#1>rail>dot: E 8x8 z=fix/fix; fill=accent/green/icon
+ chain>certificate#1>rail>line: F 1x24 z=fix/fill; fill=border/strong; clip=1
+ chain>certificate#1>text: F 232x39 lay=V gap1 pad0,0,10,0 MIN/MIN; z=fill/hug; clip=1
+ chain>certificate#1>text>name: T 232x15 z=fill/hug; fill=text/primary; f=Inter Semi Bold 12/auto LT HEIGHT trunc1; tx='Jane Novak'
+ chain>certificate#1>text>info: T 232x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT; tx='Signer · RSA 2048 · valid until Mar 14, 2028'
+ chain>certificate#2: F 250x52 lay=H gap10 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ chain>certificate#2>rail: F 8x52 lay=V gap3 pad4,0,0,0 MIN/CENTER; z=fix/fill; clip=1
+ chain>certificate#2>rail>dot: E 8x8 z=fix/fix; fill=accent/green/icon
+ chain>certificate#2>rail>line: F 1x37 z=fix/fill; fill=border/strong; clip=1
+ chain>certificate#2>text: F 232x52 lay=V gap1 pad0,0,10,0 MIN/MIN; z=fill/hug; clip=1
+ chain>certificate#2>text>name: T 232x15 z=fill/hug; fill=text/primary; f=Inter Semi Bold 12/auto LT HEIGHT trunc1; tx='Example Issuing CA 2'
+ chain>certificate#2>text>info: T 232x26 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT; tx='Intermediate · RSA 4096 · valid until Jan 5, 2031'
+ chain>certificate#3: F 250x29 lay=H gap10 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ chain>certificate#3>rail: F 8x29 lay=V gap3 pad4,0,0,0 MIN/CENTER; z=fix/fill; clip=1
+ chain>certificate#3>rail>dot: E 8x8 z=fix/fix; fill=accent/green/icon
+ chain>certificate#3>text: F 232x29 lay=V gap1 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ chain>certificate#3>text>name: T 232x15 z=fill/hug; fill=text/primary; f=Inter Semi Bold 12/auto LT HEIGHT trunc1; tx='Example Root CA'
+ chain>certificate#3>text>info: T 232x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT; tx='Trusted root certificate'
```

Blok B – uzly přidané ve variantě `Status=Warning, Expanded=True` (proti výchozí variantě):

```text
+ divider#1: F 250x1 z=fill/fix; fill=border/divider; clip=1
+ details: F 250x70 lay=V gap6 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ details>detail#1: F 250x13 lay=H gap8 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ details>detail#1>key: T 92x13 z=fix/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT; tx='Signed on'
+ details>detail#1>value: T 150x13 z=fill/hug; fill=text/primary; f=Inter Medium 11/auto LT HEIGHT; tx='Sep 28, 2026, 14:02'
+ details>detail#2: F 250x13 lay=H gap8 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ details>detail#2>key: T 92x13 z=fix/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT; tx='Hash algorithm'
+ details>detail#2>value: T 150x13 z=fill/hug; fill=text/primary; f=Inter Medium 11/auto LT HEIGHT; tx='SHA-256'
+ details>detail#3: F 250x13 lay=H gap8 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ details>detail#3>key: T 92x13 z=fix/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT; tx='Covers'
+ details>detail#3>value: T 150x13 z=fill/hug; fill=text/primary; f=Inter Medium 11/auto LT HEIGHT; tx='Entire document'
+ details>detail#4: F 250x13 lay=H gap8 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ details>detail#4>key: T 92x13 z=fix/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT; tx='Timestamp'
+ details>detail#4>value: T 150x13 z=fill/hug; fill=text/primary; f=Inter Medium 11/auto LT HEIGHT; tx='Embedded, valid'
+ divider#2: F 250x1 z=fill/fix; fill=border/divider; clip=1
+ caption: T 88x13 z=hug/hug; fill=text/muted; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='Certificate chain'
+ chain: F 250x141 lay=V gap0 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ chain>certificate#1: F 250x39 lay=H gap10 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ chain>certificate#1>rail: F 8x39 lay=V gap3 pad4,0,0,0 MIN/CENTER; z=fix/fill; clip=1
+ chain>certificate#1>rail>dot: E 8x8 z=fix/fix; fill=accent/green/icon
+ chain>certificate#1>rail>line: F 1x24 z=fix/fill; fill=border/strong; clip=1
+ chain>certificate#1>text: F 232x39 lay=V gap1 pad0,0,10,0 MIN/MIN; z=fill/hug; clip=1
+ chain>certificate#1>text>name: T 232x15 z=fill/hug; fill=text/primary; f=Inter Semi Bold 12/auto LT HEIGHT trunc1; tx='Jane Novak'
+ chain>certificate#1>text>info: T 232x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT; tx='Signer · RSA 2048 · valid until Mar 14, 2028'
+ chain>certificate#2: F 250x52 lay=H gap10 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ chain>certificate#2>rail: F 8x52 lay=V gap3 pad4,0,0,0 MIN/CENTER; z=fix/fill; clip=1
+ chain>certificate#2>rail>dot: E 8x8 z=fix/fix; fill=accent/green/icon
+ chain>certificate#2>rail>line: F 1x37 z=fix/fill; fill=border/strong; clip=1
+ chain>certificate#2>text: F 232x52 lay=V gap1 pad0,0,10,0 MIN/MIN; z=fill/hug; clip=1
+ chain>certificate#2>text>name: T 232x15 z=fill/hug; fill=text/primary; f=Inter Semi Bold 12/auto LT HEIGHT trunc1; tx='Example Issuing CA 2'
+ chain>certificate#2>text>info: T 232x26 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT; tx='Intermediate · RSA 4096 · valid until Jan 5, 2031'
+ chain>certificate#3: F 250x50 lay=H gap10 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ chain>certificate#3>rail: F 8x50 lay=V gap3 pad4,0,0,0 MIN/CENTER; z=fix/fill; clip=1
+ chain>certificate#3>rail>dot: E 8x8 z=fix/fix; fill=accent/amber/icon
+ chain>certificate#3>text: F 232x50 lay=V gap1 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
+ chain>certificate#3>text>name: T 232x15 z=fill/hug; fill=text/primary; f=Inter Semi Bold 12/auto LT HEIGHT trunc1; tx='Example Root CA'
+ chain>certificate#3>text>info: T 232x13 z=fill/hug; fill=accent/amber/icon; f=Inter Regular 11/auto LT HEIGHT; tx='Not among trusted certificates'
+ chain>certificate#3>text>link-wrap: F 170x20 lay=H gap0 pad4,0,0,0 MIN/MIN; z=hug/hug; clip=1
+ chain>certificate#3>text>link-wrap>trust-link: I 170x16 lay=H gap0 pad0,2,0,2 MIN/MIN; z=hug/hug; rad=4; inst=Text link{State=Default} [Label='Add to trusted certificates'] ov:height,name,width
```

Blok C – ve variantě `Status=Invalid, Expanded=True` jsou přidané uzly řádek po řádku shodné s blokem A kromě těchto tří (ostatních 34 řádků je stejných):

```text
+ details>detail#4>value: T 150x13 z=fill/hug; fill=text/primary; f=Inter Medium 11/auto LT HEIGHT; tx='Not embedded'
+ chain>certificate#1>rail>dot: E 8x8 z=fix/fix; fill=accent/red/icon
+ chain>certificate#1>text>info: T 232x13 z=fill/hug; fill=accent/red/icon; f=Inter Regular 11/auto LT HEIGHT; tx='Signer · RSA 2048 · revoked on Sep 2, 2026'
```

**Poznámky**

- Kořen: velikost `fix/hug`, ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- **Nové (13. 10. 2026, F-15):** přepínač `toggle` je vystavená instance Icon button (`706:147674`, `Size=24`, `exp=1`) s ikonou `icon/chevron-down` (sbalená karta) nebo `icon/chevron-up` (rozbalená), ikona 14×14 v `icon/muted` (barva stavu Default tlačítka, bez přepsání). Stavy Hover, Pressed, Focus, Disabled se nastavují na vnořené instanci. Dřív to byla holá ikona 14×14 bez stavů.
- Umístění přepínače: tlačítko 24×24 je v hlavičce umístěné absolutně (`at=231,4 abs`, vazby `MAX/CENTER`), aby šipka zůstala přesně tam, kde byla (pravý okraj ikony na hraně obsahu karty) a text se nezúžil. Tlačítko proto přesahuje hlavičku o 5 px doprava do okraje karty (od rámečku karty je 8 px). Místo pro ikonu a mezeru drží pravý okraj hlavičky `pad0,24,0,0` (14 ikona + 10 mezera); blok `text` má dál 184. Hlavička `header` už neořezává obsah, aby se neořízlo pozadí a kroužek fokusu tlačítka. Kresba sbalených variant a variant Valid a Warning se nezměnila ani o pixel (ověřeno porovnáním snímků sady i instancí na obrazovkách).
- **Nové (F-54):** rozbalená část `Status=Invalid` má vlastní příklad – odvolaný certifikát podepisujícího: tečka prvního certifikátu a jeho text `info` jsou `accent/red/icon` (token odznaku a štítku této varianty), `info` zní `'Signer · RSA 2048 · revoked on Sep 2, 2026'`, řádek `Timestamp` má hodnotu `'Not embedded'`. Mezilehlý a kořenový certifikát zůstávají zelené. Rozměr se nezměnil (276×382). Znění je navržené při opravě – na obrazovkách není žádná instance `Status=Invalid`, ze které by šlo převzít.
- `Status=Warning, Expanded=True` (beze změny) je příklad nedůvěryhodného kořene: třetí certifikát má tečku a `info` v `accent/amber/icon`, text `'Not among trusted certificates'`, přidaný `link-wrap` s odkazem; řetěz je vyšší o 21 px (141 místo 120, kořen 403 místo 382).
- Shrnutí `summary` je svázané s vlastností `Summary`, která má pro celou sadu jedinou výchozí hodnotu („The document has not been changed since it was signed.“). Ve variantách Warning a Invalid je proto na stránce Components stejný text; vlastní text musí nastavit instance (obrazovka View Document používá pro Warning „The signature is intact, but the certificate of the signer is not trusted.“). Oba příklady rozbalené části jsou zvolené tak, aby výchozímu shrnutí neodporovaly (dokument se nezměnil, problém je v certifikátu).
- `status-pill` je obyčejný rámec, není to instance jiné komponenty. Text `status-pill>label` není vázán na žádnou vlastnost; mění ho jen varianta Status: `'Valid'`, `'Needs attention'`, `'Invalid'`. Vazbu na textovou vlastnost nedostal ze stejného důvodu jako shrnutí: jedna výchozí hodnota by ve všech třech stavech ukazovala stejný text.
- Na vlastnosti jsou vázány jen `signer`, `meta` a `summary`. Celá rozbalená část (`details`, `caption`, `chain`) má pevné vzorové texty bez vazby na vlastnost; `chain>certificate#1>text>name` má text `'Jane Novak'` bez vazby na vlastnost Signer.
- Mřížka Status × Expanded (3 × 2) je úplná. Karta sama nemá žádnou variantu stavu ukazatele ani fokusu; stavy má jen vnořený přepínač.
- Žádná viditelná barva mimo proměnné.
- Poslední certifikát řetězu (`certificate#3`) nemá v `rail` uzel `line` a jeho `text` má spodní odsazení 0 místo 10.
- Vnořené komponenty: Icon button (`toggle`, vystavený) a `Text link{State=Default}` s `Label='Add to trusted certificates'` (jen ve `Status=Warning, Expanded=True`; žádná jeho vlastnost vystavena není).
- Ikona odznaku podle Status: `icon/shield-check`, `icon/alert-triangle`, `icon/x-circle` (16×16).
- Velikost: děti kořene mají `z=fill/hug` (šířka se roztahuje s kartou, výška podle obsahu), `status-pill` a `caption` `z=hug/hug`. `signer`, `meta` a jména certifikátů jsou `trunc1`; `summary`, hodnoty detailů a `info` se nezkracují (`HEIGHT` – zalamují se). Sloupec `key` detailů má pevnou šířku 92. Kořen má pevnou šířku 276 a výšku podle obsahu; vnitřní šířka 250 = 276 − 2 × 12 − 2 × 1, protože obrys kořene se počítá do rozložení.
- Výpis zkracuje texty delší než 42 znaků znakem `…`: `summary` (plné znění je výchozí hodnota vlastnosti Summary) a `chain>certificate#2>text>info` (`'Intermediate · RSA 4096 · valid until Jan 5, 2031'`, zalamuje se na dva řádky 232x26 ve všech třech rozbalených variantách).

### Property row · `206:873`

Sada 7 variant · výchozí varianta `Editable=False, State=Default` · rozměr 326×30

Popis ve Figmě: „Row of the Document properties dialog. Editable items always show Edit; Hover adds Copy; Copied confirms the copy: a green check (accent/green/icon, 14 px) replaces the Copy button in its 24 px slot for a moment and the tooltip says &quot;Copied&quot;, so nothing moves. Editing shows the inline editor, Enter confirms and Esc cancels. The actions (copy, edit, confirm, cancel) are exposed instances of Icon button with its own states.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Key | text | **Author** |
| Value | text | **Jane Novak** |
| Editable | varianta | **False**, True |
| State | varianta | **Default**, Hover, Copied, Editing |

**Stavba výchozí varianty**

```text
C 'Editable=False, State=Default' 326x30 lay=H gap8 pad0,4,0,10 MIN/CENTER; z=fix/fix; rad=6
 T 'key' 132x15 z=fix/hug; fill=text/tertiary; f=Inter Regular 12/auto LT HEIGHT trunc1; tx='Author'; ref=characters:Key
 T 'value' 140x15 z=fill/hug; fill=text/primary; f=Inter Medium 12/auto LT HEIGHT trunc1; tx='Jane Novak'; ref=characters:Value
 F 'actions' 24x24 lay=H gap0 pad0,0,0,0 MIN/CENTER; z=hug/hug
  F 'reserved' 24x24 z=fix/fix; clip=1
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Editable=False, State=Hover` | kořen: `fill=bg/hover`<br>`+ actions>copy: I 24x24 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=6; inst=Icon button{Size=24, State=Default} [Icon=icon/copy] ov:name,strokes; exp=1`<br>`- actions>reserved` |
| `Editable=False, State=Copied` | kořen: `fill=bg/hover`<br>`+ actions>copied: F 24x24 lay=H gap4 pad0,0,0,0 CENTER/CENTER; z=fix/fix; clip=1`<br>`+ actions>copied>icon: I 14x14 z=fix/fix; ic=icon/check stroke=accent/green/icon w2`<br>`- actions>reserved` |
| `Editable=True, State=Default` | `~ value: s=116x15`<br>`~ actions: s=48x24`<br>`+ actions>edit: I 24x24 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=6; inst=Icon button{Size=24, State=Default} [Icon=icon/edit] ov:name,height,width,strokes; exp=1` |
| `Editable=True, State=Hover` (proti `Editable=True, State=Default`) | kořen: `fill=bg/hover`<br>`~ actions>edit: inst=Icon button{Size=24, State=Default} [Icon=icon/edit] ov:name,strokes`<br>`+ actions>copy: I 24x24 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=6; inst=Icon button{Size=24, State=Default} [Icon=icon/copy] ov:name,strokes; exp=1`<br>`- actions>reserved` |
| `Editable=True, State=Copied` (proti `Editable=True, State=Default`) | kořen: `fill=bg/hover`<br>`~ actions>edit: inst=Icon button{Size=24, State=Default} [Icon=icon/edit] ov:name,strokes`<br>`+ actions>copied: F 24x24 lay=H gap4 pad0,0,0,0 CENTER/CENTER; z=fix/fix; clip=1`<br>`+ actions>copied>icon: I 14x14 z=fix/fix; ic=icon/check stroke=accent/green/icon w2`<br>`- actions>reserved` |
| `Editable=True, State=Editing` (proti `Editable=True, State=Default`) | kořen: `fill=bg/hover`<br>`+ field: F 116x24 lay=H gap0 pad0,8,0,8 MIN/CENTER; z=fill/fix; fill=bg/surface; st=border/focus w2 INSIDE; rad=6; clip=1`<br>`+ field>value: T 96x15 z=fill/hug; fill=text/primary; f=Inter Medium 12/auto LT HEIGHT trunc1; tx='Jane Novak'; ref=characters:Value`<br>`+ actions>confirm: I 24x24 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=6; inst=Icon button{Size=24, State=Default} [Icon=icon/check] ov:name,strokes; exp=1`<br>`+ actions>cancel: I 24x24 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=6; inst=Icon button{Size=24, State=Default} [Icon=icon/x] ov:name,height,width,strokes; exp=1`<br>`- value`<br>`- actions>edit`<br>`- actions>reserved` |

**Poznámky**

- Kořen: velikost `fix/fix`, ořez obsahu ne, obrys se počítá do rozložení ano (kořen sám obrys nemá). Platí pro všechny varianty.
- **Nové (13. 10. 2026):** tlačítka akcí `copy`, `edit`, `confirm`, `cancel` už nejsou obyčejné rámce, ale vystavené instance komponenty Icon button (`706:147674`, `Size=24`, `exp=1`); názvy vrstev zůstaly. Stav tlačítka (Hover, Pressed, Focus, Disabled) se nastavuje na vnořené instanci, i z instance řádku. Rámec `actions` proto už neořezává obsah (kroužek fokusu tlačítka je `OUTSIDE`). Kresba variant, které existovaly, se nezměnila ani o pixel (ověřeno porovnáním snímků).
- **Nové (F-78):** `Editable=True, State=Copied` má tlačítko `edit` jako ostatní varianty `Editable=True` (první dítě rámce `actions`, ikona `icon/default`, protože řádek má výplň Hover). **Změna 14. 10. 2026 (N-08):** potvrzení kopie je zelená značka `icon/check` 14×14 v místě tlačítka `copy` (rámec `copied` 24×24, obsah vystředěný) bez popisku, takže `actions` má 48×24 jako v Hover, `edit` zůstává na x = 274 a hodnota má 116 (v `Editable=False, State=Copied` 140). Dříve popisek „Copied“ (62 px) posunul `edit` o 38 px vlevo a zúžil hodnotu.
- Barva ikon vnořených tlačítek (výpis ji u instance Icon button neukazuje, je to přepsání tahu vektoru `glyph` uvnitř instance): `edit` v `Editable=True, State=Default` `icon/muted` (bez přepsání, barva stavu Default tlačítka); `edit` v Hover a Copied `icon/default`; `copy` v obou variantách Hover `icon/default`; `confirm` `icon/accent`; `cancel` `icon/muted` (bez přepsání). Přepsaná barva platí ve všech stavech tlačítka, při Hover / Pressed tlačítka se mění jen jeho pozadí (`bg/hover`, `bg/pressed`).
- Popis ve Figmě je opravený podle kresby: `edit` je u editovatelné položky vidět vždy (i v Default), Hover přidává `copy`.
- Mřížka Editable × State (2 × 4) má 7 variant: chybí `Editable=False, State=Editing`.
- Žádná varianta Pressed, Focus (řádku) ani Disabled. Fokusový obrys má editační pole (`field`: `st=border/focus w2 INSIDE`, výplň `bg/surface`) a vnořená tlačítka ve svém stavu Focus.
- Žádná barva mimo proměnné.
- Kořen nemá ve variantách `State=Default` žádnou výplň; Hover, Copied i Editing mají `fill=bg/hover`.
- Rámec `actions`: v `Editable=False, State=Default` obsahuje jen prázdný rámec `reserved` 24×24 (rezervované místo), který v Hover nahradí tlačítko `copy` stejné velikosti – šířka hodnoty se nemění (140). V `Editable=True, State=Default` je `edit` + `reserved` (48×24, hodnota 116), v Hover `edit` + `copy`. Pořadí dětí rámce `actions` ve Figmě: Default `edit`, `reserved`; Hover `edit`, `copy`; Copied `edit`, `copied` (u `Editable=False` jen `copied`); Editing `confirm`, `cancel`. Kořen má ve stavu Editing děti v pořadí `key`, `field`, `actions`.
- Stav Copied: `copied` je rámec 24×24 s ikonou `icon/check` 14×14 (`accent/green/icon`) uprostřed, na místě tlačítka `copy`. Text „Copied“ ukáže aplikace v tooltipu (podle popisu ve Figmě).
- Stav Editing: text `value` je nahrazen rámcem `field` 116×24 (stejná šířka jako `value` v `Editable=True, State=Default`), jehož vnitřní text je vázán na vlastnost Value; `actions` zůstává 48×24. Obrys `field` (`border/focus` 2 px `INSIDE`) se počítá do rozložení (`strokesIncludedInLayout = true`), text tedy začíná 10 px od okraje pole (2 obrys + 8 odsazení, x = 160 v řádku) a má šířku 96.
- Výpis u instancí Icon button uvádí `ov:name,height,width,strokes` nebo `ov:name,strokes`; pole `height`, `width`, `strokes` patří vnořené ikoně (velikost 14×14 a barva tahu), rozdíl mezi instancemi nemá na kresbu vliv.
- Velikost: `key` má pevnou šířku 132 (`z=fix/hug`, `trunc1`), `value` vyplňuje zbytek (`z=fill/hug`, `trunc1`), `actions` `z=hug/hug`; výška řádku 30 se mezi variantami nemění. Vnořená komponenta: Icon button (a ikony).

### Font row · `206:944`

Sada 8 variant · výchozí varianta `Embedded=True, State=Default` · rozměr 692×48

Popis ve Figmě: „Font used by the document, in the Fonts tab of Document properties. Hover offers Copy. Pressed keeps the Copy icon, Focus is the Default row with a 2 px border/focus ring inside (rows of a list touch each other and are clipped by the list). The type tag is an exposed Tag instance: set its Label (TrueType, Type 1, …) on the row.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Name | text | **Inter-SemiBold** |
| Family | text | **Font family: Inter** |
| Embedded | varianta | **True**, False |
| State | varianta | **Default**, Hover, Pressed, Focus |
| type-tag → Label | text vystavené instance `Tag` | **TrueType** |
| type-tag → State | varianta vystavené instance `Tag` | **Default** (hodnoty viz karta Tag) |

**Stavba výchozí varianty**

```text
C 'Embedded=True, State=Default' 692x48 lay=H gap12 pad8,8,8,8 MIN/CENTER; z=fix/hug; rad=8
 I 'badge' 32x32 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=accent/neutral/bg; rad=16; inst=Tool icon{Hue=Neutral, Size=32} [Icon=icon/type] ov:name,height,width,strokes
 F 'text' 314x30 lay=V gap1 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  T 'name' 314x16 z=fill/hug; fill=text/primary; f=Inter Semi Bold 13/auto LT HEIGHT trunc1; tx='Inter-SemiBold'; ref=characters:Name
  T 'family' 314x13 z=fill/hug; fill=text/muted; f=Inter Regular 11/auto LT HEIGHT trunc1; tx='Font family: Inter'; ref=characters:Family
 F 'type' 130x21 lay=H gap0 pad0,0,0,0 MIN/MIN; z=fix/hug; clip=1
  I 'type-tag' 70x21 lay=H gap0 pad3,9,3,9 MIN/CENTER; z=hug/hug; fill=bg/subtle; st=border/default w1 INSIDE; rad=100; inst=Tag{State=Default} [Label='TrueType'] ov:height,name,width; exp=1
 F 'status' 128x15 lay=H gap6 pad0,0,0,0 MIN/CENTER; z=fix/hug; clip=1
  E 'dot' 8x8 z=fix/fix; fill=accent/green/icon
  T 'status-label' 105x15 z=hug/hug; fill=text/secondary; f=Inter Medium 12/auto LT WIDTH_AND_HEIGHT; tx='Embedded subset'
 F 'copy' 24x24 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=6; clip=1
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Embedded=True, State=Hover` | `~ .: fill=bg/hover`<br>`+ copy>icon: I 14x14 z=fix/fix; clip=1; ic=icon/copy stroke=icon/default w2` |
| `Embedded=True, State=Pressed` | `~ .: fill=bg/pressed`<br>`+ copy>icon: I 14x14 z=fix/fix; clip=1; ic=icon/copy stroke=icon/default w2` |
| `Embedded=True, State=Focus` | `~ .: st=border/focus w2 INSIDE; sil=0` |
| `Embedded=False, State=Default` | `~ status>dot: fill=accent/amber/icon`<br>`~ status>status-label: s=86x15; tx='Not embedded'` |
| `Embedded=False, State=Hover` (proti `Embedded=False, State=Default`) | `~ .: fill=bg/hover`<br>`+ copy>icon: I 14x14 z=fix/fix; clip=1; ic=icon/copy stroke=icon/default w2` |
| `Embedded=False, State=Pressed` (proti `Embedded=False, State=Default`) | `~ .: fill=bg/pressed`<br>`+ copy>icon: I 14x14 z=fix/fix; clip=1; ic=icon/copy stroke=icon/default w2` |
| `Embedded=False, State=Focus` (proti `Embedded=False, State=Default`) | `~ .: st=border/focus w2 INSIDE; sil=0` |

**Poznámky**

- Kořen: velikost `fix/hug`, ořez obsahu ne, obrys se počítá do rozložení ano, ve variantách Focus ne (`sil=0`). Platí pro všechny varianty; všech 8 variant má 692×48 a potomky na stejných místech (`badge` 8,8; `text` 52,9; `type` 378,13,5; `status` 520,16,5; `copy` 660,12).
- **Nové – stavy Pressed a Focus** (4 nové varianty: `706:147131`, `706:147147`, `706:147161`, `706:147177`). Tokeny a umístění obrysu jsou převzaty z Bookmark row (`191:911`): Pressed má `fill=bg/pressed` a stejně jako Hover ukazuje ikonu `icon/copy`; Focus je varianta Default (bez ikony `icon/copy`, bez výplně) s obrysem `border/focus` 2 px, velikost řádku se nemění. Obrys vznikl 13. 10. 2026 jako `OUTSIDE`, od 14. 10. 2026 je `INSIDE` se `sil=0` (rozhodnutí Q-2). Mřížka Embedded × State (2 × 4) je úplná; stav Disabled neexistuje.
- **Nové – štítek typu je vystavená instance.** `type-tag` = `Tag{State=Default}` s `Label='TrueType'` má ve všech 8 variantách `exp=1`: text štítku (`Label`) se nastavuje přímo na instanci řádku. Vystavena je celá instance `Tag`, tedy i její `State`.
- Žádná barva mimo proměnné.
- Kořen nemá v Default žádnou výplň, v Hover `fill=bg/hover`, v Pressed `fill=bg/pressed`.
- Rámec `copy` 24×24 je v Default a ve Focus prázdný (místo je rezervované), v Hover a Pressed je v něm ikona `icon/copy` 14×14; rozměry řádku se nemění. `copy` je obyčejný rámec, ne instance tlačítka, a nemá vlastní stavy.
- Vnořené komponenty: `badge` = `Tool icon{Hue=Neutral, Size=32}` s `Icon=icon/type`; není vystavená (ikonu odznaku nelze vlastností řádku změnit). Přepsání `height,width` patří vnořené instanci ikony `icon` (16×16) a `strokes` jejímu vektoru `glyph` (obrys `accent/neutral/icon w2`).
- Text `status-label` není vázán na vlastnost; určuje ho varianta Embedded: `'Embedded subset'` s tečkou `accent/green/icon`, `'Not embedded'` s tečkou `accent/amber/icon`.
- Velikost: sloupce `type` (130) a `status` (128) mají pevnou šířku (`z=fix/hug`), `badge` 32×32 a `copy` 24×24 jsou pevné, `text` vyplňuje zbytek (`z=fill/hug`); `name` a `family` jsou `trunc1`.
- **Implementace (kontrola kroku 5, 17. 10. 2026):** vystředěné prvky leží na celých pixelech: štítek `type` na y = 13 a stav `status` na y = 16 (Figma 13,5 a 16,5), kresba je ostřejší; testy proti geometrii Figmy počítají s půl pixelem.
- Rozložení sady: sloupce jsou hodnoty `Embedded` (True x = 24, False x = 740), řádky stavy (Default y = 24, Hover 96, Pressed 168, Focus 240); sada má 1456×312 (dříve 1456×168, sloupce byly stavy a řádky hodnoty `Embedded`).

### Locked document · `225:906`

Sada 2 variant · výchozí varianta `State=Locked` · rozměr 160×160

Popis ve Figmě: „Illustration of the locked document screen. State=Wrong is shown after a wrong password.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| State | varianta | **Locked**, Wrong |

**Stavba výchozí varianty**

```text
C 'State=Locked' 160x160 
 E 'backdrop' 160x160 at=0,0; z=fix/fix; fill=accent/blue/bg
 E 'spark' 8x8 at=10,44; z=fix/fix; fill=accent/blue/border-strong
 E 'spark' 6x6 at=140,30; z=fix/fix; fill=accent/blue/border-strong
 E 'spark' 5x5 at=20,122; z=fix/fix; fill=accent/blue/border-strong
 R 'sheet-back' 78x100 at=28,40; rot=8; z=fix/fix; fill=bg/surface; st=accent/blue/border w1 INSIDE; rad=8
 F 'sheet' 82x104 at=40,26; z=fix/fix; fill=bg/surface; st=accent/blue/border w1 INSIDE; rad=8; fx=DROP_SHADOW(0,4,12,0 effect/shadow); clip=1
  R 'heading' 34x6 at=12,14; z=fix/fix; fill=accent/blue/border-strong; rad=3
  R 'line' 58x4 at=12,30; z=fix/fix; fill=bg/muted; rad=2
  R 'line' 58x4 at=12,40; z=fix/fix; fill=bg/muted; rad=2
  R 'line' 44x4 at=12,50; z=fix/fix; fill=bg/muted; rad=2
  R 'line' 58x4 at=12,64; z=fix/fix; fill=bg/muted; rad=2
  R 'line' 36x4 at=12,74; z=fix/fix; fill=bg/muted; rad=2
 F 'badge' 60x60 at=90,90; lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; fill=bg/accent; st=bg/surface w4 OUTSIDE; rad=30; fx=DROP_SHADOW(0,4,12,0 effect/shadow); clip=1
  I 'lock' 28x28 z=fix/fix; ic=icon/lock stroke=icon/on-accent w2
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `State=Wrong` | `~ backdrop: fill=accent/red/bg`<br>`~ spark#1: fill=accent/red/border-strong`<br>`~ spark#2: fill=accent/red/border-strong`<br>`~ spark#3: fill=accent/red/border-strong`<br>`~ sheet-back: st=accent/red/border w1 INSIDE`<br>`~ sheet: st=accent/red/border w1 INSIDE`<br>`~ sheet>heading: fill=accent/red/border-strong`<br>`~ badge: fill=icon/danger` |

**Poznámky**

- Kořen: bez automatického rozvržení, velikost fix/fix, ořez obsahu ne, obrys se počítá do rozložení ne (kořen sám obrys nemá). Platí pro všechny varianty.
- Komponenta je ilustrace (obrázek) pevné velikosti 160×160: kořen nemá automatické rozvržení ani výplň, všechny vrstvy mají pevnou polohu `at=` a `z=fix/fix`.
- Žádná barva mimo proměnné.
- `State=Wrong` mění jen barvy: tokeny `accent/blue/*` se nahrazují `accent/red/*` (`bg`, `border`, `border-strong`), výplň odznaku `bg/accent` se mění na `icon/danger` (token skupiny ikon použitý jako výplň). Ikona `icon/lock` (`stroke=icon/on-accent w2`, 28×28), řádky `line` (`bg/muted`) a výplně `bg/surface` zůstávají beze změny.
- `sheet-back` je obdélník otočený o 8 (`rot=8`); `sheet` a `badge` mají stín `DROP_SHADOW(0,4,12,0 effect/shadow)`; `badge` má obrys `bg/surface w4 OUTSIDE` (zasahuje 4 px vně kruhu 60×60).
- Opakovaná jména vrstev: tři `spark`, pět `line` (ve výpisu rozdílů značeno `#1`…).
- Žádný stav ukazatele ani fokusu, žádná textová vlastnost, žádná vnořená komponenta kromě ikony.

### Page selection · `265:985`

Sada 8 variant · výchozí varianta `Mode=All, Error=False` · rozměr 320×156

Popis ve Figmě: „Common page selection used by every tool that works with a part of the document (tools.md §9.6). Modes: All, Current, Selected (taken from the Thumbnails panel), Range (physical page order, labels shown next to the number). Filters narrow the result. Always shows the number of selected pages; Error=True shows an invalid range: the error row is visible and the range field is in State=Error. The mode segments (a segmented control), the filter check boxes and the range field are exposed nested instances - their properties are set on an instance of Page selection.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Label | text | **Pages** |
| Total | text | **48 pages** |
| Count | text | **7 pages selected · 3 (iii) – 12 (xii)** |
| Error text | text | **Page 60 does not exist – the document has 48 pages** |
| Mode | varianta | Range, **All**, Current, Selected |
| Error | varianta | **False**, True |

**Stavba výchozí varianty**

```text
C 'Mode=All, Error=False' 320x156 lay=V gap10 pad0,0,0,0 MIN/MIN; z=fix/hug
 F 'header' 320x15 lay=H gap8 pad0,0,0,0 MIN/CENTER; z=fill/hug; clip=1
  T 'label' 36x15 z=hug/hug; fill=text/primary; f=Inter Semi Bold 12/auto LT WIDTH_AND_HEIGHT; tx='Pages'; ref=characters:Label
  F 'spacer' 218x1 z=fill/fix; clip=1
  T 'total' 50x13 z=hug/hug; fill=text/tertiary; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='48 pages'; ref=characters:Total
 F 'modes' 320x32 lay=H gap0 pad0,0,0,0 MIN/CENTER; z=fill/hug; fill=bg/track; rad=16
  I 'mode-all' 80x32 z=fill/fix; inst=Segment{Selected=True, State=Default} [Show icon=false, Label='All']; exp=1
  I 'mode-current' 80x32 z=fill/fix; inst=Segment{Selected=False, State=Default} [Show icon=false, Label='Current']; exp=1
  I 'mode-selected' 80x32 z=fill/fix; inst=Segment{Selected=False, State=Default} [Show icon=false, Label='Selected']; exp=1
  I 'mode-range' 80x32 z=fill/fix; inst=Segment{Selected=False, State=Default} [Show icon=false, Label='Range']; exp=1
 F 'range' 320x34 hid=1; lay=H gap8 pad0,0,0,0 MIN/MIN; z=fix/hug; clip=1
  I 'range-field' 320x34 lay=H gap8 pad0,12,0,12 MIN/CENTER; z=fill/fix; fill=bg/surface; st=border/default w1 INSIDE; rad=8; inst=Text field{State=Default} [Value='1-3, 8, 10-12'] ov:height,name,width; exp=1
 F 'filters' 320x65 lay=V gap8 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  T 'filters-label' 24x13 z=hug/hug; fill=text/tertiary; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='Only'
  F 'filter-row' 320x18 lay=H gap8 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
   I 'filter-even-pages' 156x18 lay=H gap8 pad0,0,0,0 MIN/CENTER; z=fill/hug; inst=Checkbox{Checked=False, State=Default} [Show label=true, Label='Even pages'] ov:height,name,primaryAxisSizingMode,width; exp=1
   I 'filter-odd-pages' 156x18 lay=H gap8 pad0,0,0,0 MIN/CENTER; z=fill/hug; inst=Checkbox{Checked=False, State=Default} [Show label=true, Label='Odd pages'] ov:height,name,primaryAxisSizingMode,width; exp=1
  F 'filter-row' 320x18 lay=H gap8 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
   I 'filter-portrait' 156x18 lay=H gap8 pad0,0,0,0 MIN/CENTER; z=fill/hug; inst=Checkbox{Checked=False, State=Default} [Show label=true, Label='Portrait'] ov:height,name,primaryAxisSizingMode,width; exp=1
   I 'filter-landscape' 156x18 lay=H gap8 pad0,0,0,0 MIN/CENTER; z=fill/hug; inst=Checkbox{Checked=False, State=Default} [Show label=true, Label='Landscape'] ov:height,name,primaryAxisSizingMode,width; exp=1
 F 'status' 320x14 lay=H gap6 pad0,0,0,0 MIN/CENTER; z=fill/hug; clip=1
  I 'icon' 14x14 z=fix/fix; ic=icon/check-circle stroke=icon/muted w2
  T 'count' 172x13 z=hug/hug; fill=text/secondary; f=Inter Medium 11/auto LT WIDTH_AND_HEIGHT; tx='7 pages selected · 3 (iii) – 12 (xii)'; ref=characters:Count
 F 'error' 320x14 hid=1; lay=H gap6 pad0,0,0,0 MIN/CENTER; z=fix/hug; clip=1
  I 'icon' 14x14 z=fix/fix; ic=icon/alert-circle stroke=icon/danger w2
  T 'message' 300x13 z=fill/hug; fill=text/danger; f=Inter Regular 11/auto LT HEIGHT; tx='Page 60 does not exist – the document has 48 pages'; ref=characters:Error text
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Mode=Current, Error=False` | `modes`: vybraný segment `mode-current` (`Segment{Selected=True}`), ostatní `Selected=False` |
| `Mode=Selected, Error=False` | `modes`: vybraný segment `mode-selected` (`Segment{Selected=True}`), ostatní `Selected=False` |
| `Mode=Range, Error=False` | `modes`: vybraný segment `mode-range` (`Segment{Selected=True}`), ostatní `Selected=False`<br>kořen: `s=320x200`<br>`~ range: hid=-; z=fill/hug` |
| `Mode=All, Error=True` | kořen: `s=320x180`<br>`~ range>range-field: st=text/danger w1 INSIDE; inst=Text field{State=Error} [Value='1-3, 8, 10-12'] ov:height,name,width`<br>`~ error: hid=-; z=fill/hug` |
| `Mode=Current, Error=True` | `modes`: vybraný segment `mode-current` (`Segment{Selected=True}`), ostatní `Selected=False`<br>kořen: `s=320x180`<br>`~ range>range-field: st=text/danger w1 INSIDE; inst=Text field{State=Error} [Value='1-3, 8, 10-12'] ov:height,name,width`<br>`~ error: hid=-; z=fill/hug` |
| `Mode=Selected, Error=True` | `modes`: vybraný segment `mode-selected` (`Segment{Selected=True}`), ostatní `Selected=False`<br>kořen: `s=320x180`<br>`~ range>range-field: st=text/danger w1 INSIDE; inst=Text field{State=Error} [Value='1-3, 8, 10-12'] ov:height,name,width`<br>`~ error: hid=-; z=fill/hug` |
| `Mode=Range, Error=True` | `modes`: vybraný segment `mode-range` (`Segment{Selected=True}`), ostatní `Selected=False`<br>kořen: `s=320x224`<br>`~ range: hid=-; z=fill/hug`<br>`~ range>range-field: st=text/danger w1 INSIDE; inst=Text field{State=Error} [Value='1-3, 8, 10-12'] ov:height,name,width`<br>`~ error: hid=-; z=fill/hug` |

**Poznámky**

- **Změna 16. 10. 2026 (Q-9):** režimy All / Current / Selected / Range jsou segmentovaný přepínač: rámec `modes` je dráha `bg/track` (`rad=16`, bez okraje a mezer) přes celou šířku, režimy jsou vystavené instance `Segment` bez ikony, které ji dělí rovným dílem. Dříve to byla řada Filter chip s plnou výplní `bg/accent` u vybraného. Instance na obrazovkách převzaly zakázaný stav (`State=Disabled`, 6 instancí) i prototypové odkazy (11 instancí na *Tools - Edit*).
- Kořen: velikost fix/hug, ořez obsahu ne, obrys se počítá do rozložení ano (kořen sám obrys nemá). Platí pro všechny varianty.
- **Nově je `Error` vlastnost varianty** (`Error=False|True`), ne přepínač. Přepínač `Error` byl odstraněn (jediná povolená výjimka z pravidla neměnit vlastnosti), textová vlastnost `Error text` zůstala. Dosavadní čtyři varianty dostaly hodnotu `Error=False` a zachovaly identifikátory (`265:871`, `265:909`, `265:947`, `264:853`); nové jsou `Mode=All, Error=True` `706:147295`, `Mode=Current, Error=True` `706:147349`, `Mode=Selected, Error=True` `706:147403`, `Mode=Range, Error=True` `706:147457`.
- **`Error=True`:** řádek `error` je viditelný (kořen vyšší o 24 px = 14 + mezera 10) **a pole `range-field` je `Text field{State=Error}`** (obrys `text/danger w1 INSIDE`). Chybu rozsahu tedy kreslí řádek zprávy i pole. Ve variantách `Mode=All|Current|Selected` je vrstva `range` skrytá, chyba se v nich projeví jen řádkem `error`; pole je i tam přepnuté na `State=Error`.
- **Nově jsou vnořené instance vystavené** (`exp=1`, ve všech osmi variantách): čtyři segmenty režimu `mode-*` (Segment, do 16. 10. 2026 Filter chip), čtyři zaškrtávátka `filter-*` (Checkbox) a pole `range-field` (Text field). Jejich vlastnosti (`Label`, `Selected`, `State`, `Checked`, `Value`, …) jdou nastavit přímo na instanci Page selection. Řídit režim je přesto třeba vlastností `Mode` (ta přepíná vybraný segment i viditelnost pole).
- Výchozí varianta je `Mode=All, Error=False`, přestože první hodnota vlastnosti Mode ve Figmě je `Range` (pořadí hodnot: Range, All, Current, Selected).
- Žádná barva mimo proměnné.
- Ikona `error>icon` (`icon/alert-circle` 14×14) má ve všech variantách stejnou kresbu: vektor `glyph` s obrysem `icon/danger w2`, bez výplně. Text `error>message` má ve všech `fill=text/danger`. Potomky instance ve skryté vrstvě vrací Plugin API jen s `figma.skipInvisibleInstanceChildren = false`; bez toho se instance jeví jako prázdná.
- Skrytá vrstva `range` (`hid=1`) není vázána na žádnou vlastnost; zobrazují ji jen varianty `Mode=Range` (`hid=-`, kořen vyšší o 44 px = 34 + mezera 10). Vrstva `error` (`hid=1`) už není vázána na vlastnost; zobrazují ji varianty `Error=True`. Text `message` je vázán na `Error text`.
- Vrstva skrytá ve výchozím stavu se přes Plugin API čte s velikostí `fix`, dokud je skrytá: `range` a `error` takto vracejí `z=fix/hug`. Skutečný způsob určení velikosti je `z=fill/hug` (viz rozdíly variant, kde jsou vrstvy zobrazené) – obě vyplňují šířku kořene jako ostatní děti.
- Segmenty režimu jsou od 16. 10. 2026 (Q-9) stejně široké: dráha `modes` vyplňuje šířku komponenty a segmenty ji dělí rovným dílem (80×32 v komponentě, 70×32 v postranních panelech; přirozená šířka by byla 75 = nejširší popisek „Selected“ + 2 × 14) na dráze `bg/track` bez okraje, přepnutím režimu se nic neposouvá. Vybraný je deska `bg/surface` se stínem a popiskem `text/secondary`, nevybrané bez výplně s popiskem `text/muted`. (Dříve řada Filter chip 45 / 75 / 82 / 67 s vybraným `fill=bg/accent`.)
- Vnořené komponenty: čtyři `Segment` (`Selected=True|False, State=Default`, `Show icon=false`, popisky All / Current / Selected / Range), `Text field{State=Default|Error}` s `Value='1-3, 8, 10-12'`, čtyři `Checkbox{Checked=False, State=Default}` s `Show label=true` a popisky Even pages / Odd pages / Portrait / Landscape (přepsané `primaryAxisSizingMode`, `z=fill/hug`, každý 156 široký).
- Texty `filters-label` (`'Only'`) nejsou vázány na vlastnost. Ikona stavového řádku je `icon/check-circle` se `stroke=icon/muted w2` i při chybě.
- Žádná varianta stavu (Hover, Focus, Disabled) celé komponenty; stavy mají jen vnořené komponenty. Kombinace „režim + vybrané filtry“ nemá vlastní variantu (filtry se nastavují na vystavených zaškrtávátkách).
- Velikost: kořen 320 široký, děti `z=fill/hug` (roztahují se na šířku), výška podle obsahu (156; 200 v `Mode=Range`; `Error=True` přidá 24: 180 a 224). `count` je `z=hug/hug` bez zkrácení, `message` `z=fill/hug` `HEIGHT` (zalamuje se – delší zpráva přidá řádek 12 px). `header` má mezi `label` a `total` pružný `spacer` (`z=fill/fix`, výška 1).
- Sada je auto layout (vodorovně, mezera 40, okraje 24), nově se zalamováním (mezera řádků 40): první řádek `Error=False`, druhý `Error=True`; sada vyrostla z 1448×248 na 1448×512.
- Ukázkový rámec `265:1057`: instance `265:1059` je nově `Mode=Range, Error=True`, pole `range-field` je `Text field{State=Error}` s `Value="1-3, 60"` (dříve přepsané na `State=Focus`); rozměr instance 320×224 a rámce 368×291 se nezměnil.

### Color swatch · `206:805`

Sada 20 variant · výchozí varianta `Kind=Color, Selected=False, State=Default` · rozměr 28×28

Popis ve Figmě: „Predefined color in the bar of the active annotation tool. Override the fill of the "color" layer with an annotation/* variable. Kind=None means no fill. A selected swatch with the focus keeps its selection ring and gets the focus ring outside of it. State=Disabled keeps the color and has no hover; the selection ring is border/strong and the slash of Kind=None icon/disabled.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Kind | varianta | **Color**, None |
| Selected | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus, Disabled |

**Stavba výchozí varianty**

```text
C 'Kind=Color, Selected=False, State=Default' 28x28 lay=H gap0 pad0,0,0,0 CENTER/CENTER; z=fix/fix; rad=14
 F 'color' 20x20 z=fix/fix; fill=annotation/yellow; st=border/strong w1 INSIDE; rad=10; clip=1
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Kind=Color, Selected=False, State=Hover` | kořen: `fill=bg/surface+bg/hover` |
| `Kind=Color, Selected=False, State=Pressed` | kořen: `fill=bg/surface+bg/pressed` |
| `Kind=Color, Selected=False, State=Focus` | kořen: `st=border/focus w2 OUTSIDE` |
| `Kind=Color, Selected=True, State=Default` | kořen: `st=bg/accent w2 INSIDE` |
| `Kind=Color, Selected=True, State=Hover` (proti `Kind=Color, Selected=True, State=Default`) | kořen: `fill=bg/surface+bg/hover` |
| `Kind=Color, Selected=True, State=Pressed` (proti `Kind=Color, Selected=True, State=Default`) | kořen: `fill=bg/surface+bg/pressed` |
| `Kind=Color, Selected=True, State=Focus` (proti `Kind=Color, Selected=True, State=Default`) | `+ focus-ring: E 28x28 at=0,0 abs; con=STRETCH/STRETCH; z=fix/fix; st=border/focus w2 OUTSIDE` |
| `Kind=None, Selected=False, State=Default` | `~ color: fill=bg/surface`<br>`+ color>slash: L 20x0 at=3,17; rot=45; z=fix/fix; st=icon/danger w1.5 CENTER` |
| `Kind=None, Selected=False, State=Hover` (proti `Kind=None, Selected=False, State=Default`) | kořen: `fill=bg/surface+bg/hover` |
| `Kind=None, Selected=False, State=Pressed` (proti `Kind=None, Selected=False, State=Default`) | kořen: `fill=bg/surface+bg/pressed` |
| `Kind=None, Selected=False, State=Focus` (proti `Kind=None, Selected=False, State=Default`) | kořen: `st=border/focus w2 OUTSIDE` |
| `Kind=None, Selected=True, State=Default` | kořen: `st=bg/accent w2 INSIDE`<br>`~ color: fill=bg/surface`<br>`+ color>slash: L 20x0 at=3,17; rot=45; z=fix/fix; st=icon/danger w1.5 CENTER` |
| `Kind=None, Selected=True, State=Hover` (proti `Kind=None, Selected=True, State=Default`) | kořen: `fill=bg/surface+bg/hover` |
| `Kind=None, Selected=True, State=Pressed` (proti `Kind=None, Selected=True, State=Default`) | kořen: `fill=bg/surface+bg/pressed` |
| `Kind=None, Selected=True, State=Focus` (proti `Kind=None, Selected=True, State=Default`) | `+ focus-ring: E 28x28 at=0,0 abs; con=STRETCH/STRETCH; z=fix/fix; st=border/focus w2 OUTSIDE` |
| `Kind=Color, Selected=False, State=Disabled` | `color: st=border/default w1 INSIDE` |
| `Kind=Color, Selected=True, State=Disabled` (proti `Kind=Color, Selected=True, State=Default`) | kořen: `st=border/strong w2 INSIDE`; `color: st=border/default w1 INSIDE` |
| `Kind=None, Selected=False, State=Disabled` (proti `Kind=None, Selected=False, State=Default`) | `color: st=border/default w1 INSIDE`; `color>slash: st=icon/disabled w1.5 CENTER` |
| `Kind=None, Selected=True, State=Disabled` (proti `Kind=None, Selected=True, State=Default`) | kořen: `st=border/strong w2 INSIDE`; `color: st=border/default w1 INSIDE`; `color>slash: st=icon/disabled w1.5 CENTER` |

**Poznámky**

- Kořen: velikost fix/fix, ořez obsahu ne, obrys se počítá do rozložení ano. Platí pro všechny varianty.
- Mřížka Kind × Selected × State (2 × 2 × 5) je úplná, 20 variant. **Nový stav Disabled** (14. 10. 2026, N-14; `773:147228`, `773:147230`, `773:147232`, `773:147235`): bez výplně kořene a bez hoveru; barva vzorku zůstává (je to údaj), obrys vrstvy `color` je `border/default`, výběr `border/strong w2 INSIDE` místo `bg/accent` a přeškrtnutí `Kind=None` `icon/disabled`. Bez krytí, jako ostatní zakázané stavy.
- Žádná barva mimo proměnné.
- **Změna 17. 10. 2026 (I-26, rozhodnutí 28):** Hover a Pressed všech čtyř kombinací `Kind` × `Selected` kladou `bg/hover` / `bg/pressed` jako druhou výplň nad vlastní `bg/surface` (dříve jen poloprůhledná vrstva). Default, Focus a Disabled výplň kořene nemají.
- Barva vzorku není vlastnost komponenty: podle popisu se v instanci přepisuje výplň vrstvy `color` proměnnou `annotation/*` (výchozí `annotation/yellow`). Vrstva `color` má vždy obrys `border/strong w1 INSIDE`.
- Fokusový obrys nevybraného vzorku je `border/focus w2 OUTSIDE` na kořeni (kruh 28×28, `rad=14`), tedy 2 px vně. Varianta Focus nemá výplň kořene (vychází ze stavu Default, ne z Hover).
- Výběr je obrys kořene `bg/accent w2 INSIDE`.
- Přeškrtnutí `Kind=None`: vrstva `slash` je čára 20×0 s počátkem `at=3,17` a `rot=45`, tah 1,5 `CENTER`. Figma kreslí tah čáry posunutý o polovinu tloušťky napříč čárou (export SVG `206:781`), vykreslená středová čára tedy vede z (2,47; 16,47) do (16,61; 2,33) v souřadnicích vrstvy `color`; ve 1× leží jádro čáry v řádku 14 kořene na x = 12. Knihovna kreslí tuto vykreslenou čáru.
- Nové (oprava 13. 10. 2026, F-27): vybraný vzorek s fokusem (`Selected=True, State=Focus`, varianty `206:779` a `206:802`) si ponechává prstenec výběru na kořeni (`bg/accent w2 INSIDE`, přesně jako `Selected=True, State=Default`) a fokus kreslí navíc nová vrstva `focus-ring`: elipsa 28×28 na `at=0,0`, absolutně umístěná (do rozložení se nepočítá), bez výplně, tah `border/focus w2 OUTSIDE`. Prstenec fokusu je tedy na stejném místě jako u nevybraného vzorku (2 px vně kruhu 28×28) a prstenec výběru leží těsně uvnitř něj – vybraný a nevybraný vzorek se ve Focus liší. Dříve obrys fokusu obrys výběru nahrazoval.
- Ve světlém režimu mají `border/focus` a `bg/accent` stejnou hodnotu (`#1a73e8`), oba prstence proto splývají v jeden o tloušťce 4 px; v tmavém režimu se liší (`#5b9dff` a `#3b82f6`).
- Vrstva `focus-ring` existuje jen ve dvou variantách `Selected=True, State=Focus`; je první v pořadí vrstev (pod vrstvou `color`), která zůstává 20×20 na `4,4` jako ve všech ostatních variantách. Nevybrané varianty Focus se nezměnily (fokus je dál obrys kořene).
- `Kind=None`: vrstva `color` má `fill=bg/surface` a přibývá čára `slash` (`L 20x0`, `at=3,17`, `rot=45`, `st=icon/danger w1.5 CENTER`) – tloušťka 1.5, ne 2.
- Velikost je pevná: kořen 28×28, `color` 20×20 (`z=fix/fix`) uprostřed (`CENTER/CENTER`). Stavy Hover/Pressed vyplňují celý kruh 28×28 (`bg/hover`, `bg/pressed`), obrysy nemění rozměr.
- Žádná vnořená komponenta, žádný text.
- Sada: rozměr 544×128 (dříve 440×128); varianty Disabled jsou ve sloupcích x = 440 (nevybrané) a x = 492 (vybrané).

### Reading theme card · `156:4218`

Sada 8 variant · výchozí varianta `Selected=False, State=Default` · rozměr 159×86

Popis ve Figmě: „Preset pair of page and text color for the Custom colors mode. The preview fills are overridden per instance with reading/* variables.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Name | text | **Paper** |
| Selected | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Selected=False, State=Default' 159x86 lay=V gap6 pad5,5,7,5 MIN/MIN; z=fix/hug; rad=12
 F 'preview' 149x53 lay=H gap8 pad12,12,12,12 MIN/CENTER; z=fill/hug; fill=reading/paper/page; st=border/default w1 INSIDE; rad=8; clip=1
  T 'aa' 30x27 z=hug/hug; fill=reading/paper/text; f=Inter Bold 22/auto LT WIDTH_AND_HEIGHT; tx='Aa'
  F 'lines' 85x26 lay=V gap2 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
   T 'sample-1' 85x12 z=fill/hug; fill=reading/paper/text; f=Inter Semi Bold 10/auto LT HEIGHT trunc1; tx='Quick brown fox'
   T 'sample-2' 85x12 z=fill/hug; fill=reading/paper/text; f=Inter Regular 10/auto LT HEIGHT trunc1; tx='jumps over the dog'
 F 'label-row' 149x15 lay=H gap6 pad0,5,0,5 MIN/CENTER; z=fill/hug; clip=1
  T 'name' 139x15 z=fill/hug; fill=text/primary; f=Inter Semi Bold 12/auto LT HEIGHT trunc1; tx='Paper'; ref=characters:Name
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=False, State=Hover` | kořen: `fill=bg/hover` |
| `Selected=False, State=Pressed` | kořen: `fill=bg/pressed` |
| `Selected=False, State=Focus` | `+ focus-ring: F 159x86 at=0,0 abs; con=STRETCH/STRETCH; z=fix/fix; st=border/focus w2 OUTSIDE; rad=12` |
| `Selected=True, State=Default` | kořen: `fill=bg/accent-subtle`<br>`~ label-row>name: s=119x15; fill=text/accent`<br>`+ label-row>check: I 14x14 z=fix/fix; ic=icon/check stroke=icon/accent w2`<br>`+ ring: F 159x86 at=0,0 abs; con=STRETCH/STRETCH; z=fix/fix; st=bg/accent w2 INSIDE; rad=12` |
| `Selected=True, State=Hover` (proti `Selected=True, State=Default`) | stejné |
| `Selected=True, State=Pressed` (proti `Selected=True, State=Default`) | stejné |
| `Selected=True, State=Focus` (proti `Selected=True, State=Default`) | `+ focus-ring: F 159x86 at=0,0 abs; con=STRETCH/STRETCH; z=fix/fix; st=border/focus w2 OUTSIDE; rad=12` |

**Poznámky**

- Kořen: velikost `fix/hug`, ořez obsahu ne; kořen nemá v žádné variantě obrys (obrys výběru nese vrstva `ring`, kroužek fokusu vrstva `focus-ring`). Platí pro všechny varianty.
- Mřížka Selected × State (2 × 4) je úplná. Chybí stav Disabled.
- Žádná barva mimo proměnné. Barvy náhledu jsou tokeny `reading/paper/page` (výplň `preview`) a `reading/paper/text` (texty `aa`, `sample-1`, `sample-2`); podle popisu se v instanci přepisují proměnnými `reading/*` – barvy motivu nejsou vlastností komponenty.
- **Změna (13. 10. 2026):** výběr a fokus kreslí absolutně umístěný rámec `ring` přes celou kartu (`at=0,0 abs`, `con=STRETCH/STRETCH`, 159×86, `rad=12`, bez výplně, obrys `w2 INSIDE`), ne obrys kořene – stejně jako sourozenecká Color mode card (`171:4545`). Vrstvu mají varianta `Selected=False, State=Focus` a všechny čtyři varianty `Selected=True`; je poslední vrstvou kořene a do auto layoutu se nepočítá. Všech 8 variant má 159×86 a obsah má stejnou polohu i šířku (`preview` 149×53, `label-row` 149×15). Dříve nesl obrys kořen, počítal se do rozložení a varianty s obrysem měly 159×90 a obsah užší o 4 px.
- Fokus (**změna 14. 10. 2026**, rozhodnutí Q-2: volně stojící karta má kroužek vně): absolutní vrstva `focus-ring` (159×86, `con=STRETCH/STRETCH`, `rad=12`, bez výplně, `border/focus w2 OUTSIDE`) jako poslední vrstva kořene. `Selected=False, State=Focus` je Default s touto vrstvou; v `Selected=True, State=Focus` zůstává celý výběr (výplň `bg/accent-subtle`, `ring` `bg/accent w2 INSIDE`) a kroužek fokusu je vně, takže vybraná karta s fokusem se liší od vybrané bez fokusu. Dříve `ring` ve Focus změnil barvu na `border/focus` a ve světlém režimu (stejná hodnota jako `bg/accent`) nebyl fokus vybrané karty vidět (N-09). Ve světlém režimu oba obrysy splývají v pruh 4 px.
- `Selected=True, State=Hover` a `Selected=True, State=Pressed` jsou shodné s `Selected=True, State=Default` – vybraná karta nemá nakreslenou odezvu na ukazatel.
- Výběr: výplň kořene `bg/accent-subtle`, vrstva `ring` s obrysem `bg/accent w2 INSIDE`, jméno v `text/accent` a přidaná ikona `icon/check` 14×14 (`stroke=icon/accent w2`) vpravo v `label-row`; jméno se zúží ze 139 na 119 (ikona 14 + mezera 6).
- Texty `aa` (`'Aa'`), `sample-1` (`'Quick brown fox'`) a `sample-2` (`'jumps over the dog'`) nejsou vázány na vlastnost; na vlastnost je vázáno jen `name`.
- Velikost: `preview` a `label-row` `z=fill/hug` (roztahují se s šířkou karty), `lines` a `name` `z=fill/hug`, texty `sample-1`, `sample-2`, `name` jsou `trunc1`; `aa` `z=hug/hug`.
- Žádná vnořená komponenta kromě ikony `icon/check`.

### Color mode card · `171:4545`

Sada 8 variant · výchozí varianta `Selected=False, State=Default` · rozměr 125×122

Popis ve Figmě: „Selectable card of a page color mode (radio behavior: exactly one card of a group is selected). Swap "Preview" for the miniature of the mode.“

**Vlastnosti**

| Vlastnost | Typ | Hodnoty (výchozí tučně) |
| --- | --- | --- |
| Name | text | **Normal** |
| Preview | záměna instance | **Mode preview/Normal** |
| Selected | varianta | **False**, True |
| State | varianta | **Default**, Hover, Pressed, Focus |

**Stavba výchozí varianty**

```text
C 'Selected=False, State=Default' 125x122 lay=V gap6 pad5,5,7,5 MIN/MIN; rad=12
 I 'preview' 115x89 lay=V gap5 pad10,10,10,10 MIN/MIN; z=fill/hug; fill=sample/white; st=border/strong w1 INSIDE; rad=8; clip=1; ref=mainComponent:Preview; inst=Mode preview/Normal ov:height,name,width
 F 'label-row' 115x15 lay=H gap6 pad0,5,0,5 MIN/CENTER; z=fill/hug; clip=1
  T 'name' 105x15 z=fill/hug; fill=text/primary; f=Inter Semi Bold 12/auto LT HEIGHT trunc1; tx='Normal'; ref=characters:Name
```

**Varianty a stavy**

| Varianta | Změna |
| --- | --- |
| `Selected=False, State=Hover` | kořen: `fill=bg/hover` |
| `Selected=False, State=Pressed` | kořen: `fill=bg/pressed` |
| `Selected=False, State=Focus` | `+ focus-ring: F 125x122 at=0,0 abs; con=STRETCH/STRETCH; z=fix/fix; st=border/focus w2 OUTSIDE; rad=12` |
| `Selected=True, State=Default` | kořen: `fill=bg/accent-subtle`<br>`~ label-row>name: s=85x15; fill=text/accent`<br>`+ label-row>check: I 14x14 z=fix/fix; ic=icon/check stroke=icon/accent w2`<br>`+ ring: F 125x122 at=0,0 abs; con=STRETCH/STRETCH; z=fix/fix; st=bg/accent w2 INSIDE; rad=12` |
| `Selected=True, State=Hover` (proti `Selected=True, State=Default`) | stejné |
| `Selected=True, State=Pressed` (proti `Selected=True, State=Default`) | stejné |
| `Selected=True, State=Focus` (proti `Selected=True, State=Default`) | `+ focus-ring: F 125x122 at=0,0 abs; con=STRETCH/STRETCH; z=fix/fix; st=border/focus w2 OUTSIDE; rad=12` |

**Poznámky**

- Kořen: velikost fix/hug, ořez obsahu ne, obrys se počítá do rozložení ne (kořen sám obrys nemá). Platí pro všechny varianty.
- Mřížka Selected × State (2 × 4) je úplná. Chybí stav Disabled.
- Žádná barva mimo proměnné.
- Výběr kreslí absolutně umístěný rámec `ring` přes celou kartu (`at=0,0 abs`, `con=STRETCH/STRETCH`, `rad=12`, obrys `w2 INSIDE`), ne obrys kořene; velikost karty je ve všech variantách 125×122. Stejně je od 13. 10. 2026 nakreslena sourozenecká Reading theme card (`156:4218`), kde dříve obrys na kořeni měnil rozměr karty.
- Fokus (**změna 14. 10. 2026**, rozhodnutí Q-2: volně stojící karta má kroužek vně): absolutní vrstva `focus-ring` (125×122, `con=STRETCH/STRETCH`, `rad=12`, `border/focus w2 OUTSIDE`) jako poslední vrstva kořene. V `Selected=False, State=Focus` kořen nemá výplň; v `Selected=True, State=Focus` zůstává celý výběr (výplň, `ring` `bg/accent w2 INSIDE`, barva jména a ikona) a kroužek fokusu je vně. Dříve prstenec `ring` ve Focus změnil barvu na `border/focus` a výběr ukazovala jen výplň (N-09). Řada karet potřebuje mezeru aspoň 4 px, aby se kroužek vně nedotkl sousední karty (*Main page* 8 px, *View Document* 6 px).
- `Selected=True, State=Hover` a `Selected=True, State=Pressed` jsou shodné s `Selected=True, State=Default`.
- Výběr: `fill=bg/accent-subtle`, prstenec `bg/accent w2 INSIDE`, jméno v `text/accent`, přidaná ikona `icon/check` 14×14 (`stroke=icon/accent w2`); jméno se zúží ze 105 na 85.
- Vnořená komponenta: `preview` je instance vázaná na vlastnost Preview (`ref=mainComponent:Preview`), výchozí `Mode preview/Normal` (další miniatury: Inverted, Grayscale, High contrast, Bitonal, Custom colors – vlastní karty níže), přepsané `height,name,width`, `z=fill/hug`. Miniatury samy nemají žádné vlastnosti.
- Podle popisu se karty chovají jako přepínače (právě jedna karta skupiny je vybraná).
- Velikost: `preview` a `label-row` `z=fill/hug`, `name` `z=fill/hug` `trunc1`.

### Mode preview/Normal · `171:4367`

Samostatná komponenta · rozměr 115×89

Popis ve Figmě: „bez popisu“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Mode preview/Normal' 115x89 lay=V gap5 pad10,10,10,10 MIN/MIN; fill=sample/white; st=border/strong w1 INSIDE; rad=8
 F 'heading' 56x6 z=fix/fix; fill=sample/gray-dark; rad=2; clip=1
 F 'line-1' 93x4 z=fill/fix; fill=sample/gray-mid; rad=2; clip=1
 F 'line-2' 90x4 z=fix/fix; fill=sample/gray-mid; rad=2; clip=1
 F 'line-3' 69x4 z=fix/fix; fill=sample/gray-mid; rad=2; clip=1
 F 'picture' 93x29 lay=H gap4 pad3,0,0,0 MIN/MIN; z=fill/hug; clip=1
  F 'block-1' 44.5x26 z=fill/fix; fill=sample/blue-soft; rad=2; clip=1
  F 'block-2' 44.5x26 z=fill/fix; fill=sample/orange-soft; rad=2; clip=1
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost fix/hug, ořez obsahu ano, obrys se počítá do rozložení ano.
- Komponenta je ilustrace (obrázek) – miniatura stránky pro vlastnost Preview komponenty Color mode card (`171:4545`). Rozměr 115×89, strom má jen 8 uzlů a je ve výpisu celý (neořezaný).
- Barvy vrstev: podklad `sample/white` s obrysem `border/strong w1 INSIDE` a `rad=8`; `heading` `sample/gray-dark`; řádky `sample/gray-mid`; bloky `sample/blue-soft` a `sample/orange-soft`. Žádná barva mimo proměnné.
- Vnitřní šířka obsahu je 93 = 115 − 2 × 10 − 2 × 1, protože obrys 1 px se počítá do rozložení (`strokesIncludedInLayout = true`). `line-1` a `picture` vyplňují šířku (`z=fill`), `heading` (56), `line-2` (90) a `line-3` (69) mají pevnou šířku; bloky se dělí o šířku (`z=fill/fix`, 44.5 + 4 + 44.5).
- Žádné vlastnosti, žádný text, žádná vnořená komponenta.

### Mode preview/Inverted · `171:4376`

Samostatná komponenta · rozměr 115×89

Popis ve Figmě: „bez popisu“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Mode preview/Inverted' 115x89 lay=V gap5 pad10,10,10,10 MIN/MIN; fill=sample/black; st=border/strong w1 INSIDE; rad=8
 F 'heading' 56x6 z=fix/fix; fill=sample/gray-light; rad=2; clip=1
 F 'line-1' 93x4 z=fill/fix; fill=sample/gray-dark; rad=2; clip=1
 F 'line-2' 90x4 z=fix/fix; fill=sample/gray-dark; rad=2; clip=1
 F 'line-3' 69x4 z=fix/fix; fill=sample/gray-dark; rad=2; clip=1
 F 'picture' 93x29 lay=H gap4 pad3,0,0,0 MIN/MIN; z=fill/hug; clip=1
  F 'block-1' 44.5x26 z=fill/fix; fill=sample/orange; rad=2; clip=1
  F 'block-2' 44.5x26 z=fill/fix; fill=sample/blue; rad=2; clip=1
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost fix/hug, ořez obsahu ano, obrys se počítá do rozložení ano.
- Komponenta je ilustrace (obrázek) – miniatura stránky pro Color mode card (`171:4545`). Rozměr 115×89, strom má 8 uzlů a je ve výpisu celý.
- Barvy vrstev: podklad `sample/black` s obrysem `border/strong w1 INSIDE`; `heading` `sample/gray-light`; řádky `sample/gray-dark`; bloky `sample/orange` a `sample/blue`. Žádná barva mimo proměnné.
- Stavba a rozměry jsou shodné s Mode preview/Normal (`171:4367`), liší se jen barvy.

### Mode preview/Grayscale · `171:4385`

Samostatná komponenta · rozměr 115×89

Popis ve Figmě: „bez popisu“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Mode preview/Grayscale' 115x89 lay=V gap5 pad10,10,10,10 MIN/MIN; fill=sample/white; st=border/strong w1 INSIDE; rad=8
 F 'heading' 56x6 z=fix/fix; fill=sample/gray-dark; rad=2; clip=1
 F 'line-1' 93x4 z=fill/fix; fill=sample/gray-mid; rad=2; clip=1
 F 'line-2' 90x4 z=fix/fix; fill=sample/gray-mid; rad=2; clip=1
 F 'line-3' 69x4 z=fix/fix; fill=sample/gray-mid; rad=2; clip=1
 F 'picture' 93x29 lay=H gap4 pad3,0,0,0 MIN/MIN; z=fill/hug; clip=1
  F 'block-1' 44.5x26 z=fill/fix; fill=sample/gray-mid; rad=2; clip=1
  F 'block-2' 44.5x26 z=fill/fix; fill=sample/gray-light; rad=2; clip=1
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost fix/hug, ořez obsahu ano, obrys se počítá do rozložení ano.
- Komponenta je ilustrace (obrázek) – miniatura stránky pro Color mode card (`171:4545`). Rozměr 115×89, strom má 8 uzlů a je ve výpisu celý.
- Barvy vrstev: podklad `sample/white` s obrysem `border/strong w1 INSIDE`; `heading` `sample/gray-dark`; řádky `sample/gray-mid`; bloky `sample/gray-mid` a `sample/gray-light`. Žádná barva mimo proměnné.
- Stavba a rozměry jsou shodné s Mode preview/Normal (`171:4367`), liší se jen barvy bloků.

### Mode preview/High contrast · `171:4394`

Samostatná komponenta · rozměr 115×89

Popis ve Figmě: „bez popisu“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Mode preview/High contrast' 115x89 lay=V gap5 pad10,10,10,10 MIN/MIN; fill=sample/white; st=border/strong w1 INSIDE; rad=8
 F 'heading' 56x6 z=fix/fix; fill=sample/black; rad=2; clip=1
 F 'line-1' 93x4 z=fill/fix; fill=sample/black; rad=2; clip=1
 F 'line-2' 90x4 z=fix/fix; fill=sample/black; rad=2; clip=1
 F 'line-3' 69x4 z=fix/fix; fill=sample/black; rad=2; clip=1
 F 'picture' 93x29 lay=H gap4 pad3,0,0,0 MIN/MIN; z=fill/hug; clip=1
  F 'block-1' 44.5x26 z=fill/fix; fill=sample/blue; rad=2; clip=1
  F 'block-2' 44.5x26 z=fill/fix; fill=sample/orange; rad=2; clip=1
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost fix/hug, ořez obsahu ano, obrys se počítá do rozložení ano.
- Komponenta je ilustrace (obrázek) – miniatura stránky pro Color mode card (`171:4545`). Rozměr 115×89, strom má 8 uzlů a je ve výpisu celý.
- Barvy vrstev: podklad `sample/white` s obrysem `border/strong w1 INSIDE`; `heading` a řádky `sample/black`; bloky `sample/blue` a `sample/orange`. Žádná barva mimo proměnné.
- Stavba a rozměry jsou shodné s Mode preview/Normal (`171:4367`), liší se jen barvy.

### Mode preview/Bitonal · `171:4403`

Samostatná komponenta · rozměr 115×89

Popis ve Figmě: „bez popisu“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Mode preview/Bitonal' 115x89 lay=V gap5 pad10,10,10,10 MIN/MIN; fill=sample/white; st=border/strong w1 INSIDE; rad=8
 F 'heading' 56x6 z=fix/fix; fill=sample/black; rad=2; clip=1
 F 'line-1' 93x4 z=fill/fix; fill=sample/black; rad=2; clip=1
 F 'line-2' 90x4 z=fix/fix; fill=sample/black; rad=2; clip=1
 F 'line-3' 69x4 z=fix/fix; fill=sample/black; rad=2; clip=1
 F 'picture' 93x29 lay=H gap4 pad3,0,0,0 MIN/MIN; z=fill/hug; clip=1
  F 'block-1' 44.5x26 z=fix/fix; fill=sample/black; rad=2; clip=1
  F 'block-2' 44.5x26 z=fill/fix; fill=sample/white; st=sample/black w1 INSIDE; rad=2; clip=1
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost fix/hug, ořez obsahu ano, obrys se počítá do rozložení ano.
- Komponenta je ilustrace (obrázek) – miniatura stránky pro Color mode card (`171:4545`). Rozměr 115×89, strom má 8 uzlů a je ve výpisu celý.
- Barvy vrstev: podklad `sample/white` s obrysem `border/strong w1 INSIDE`; `heading`, řádky a `block-1` `sample/black`; `block-2` `sample/white` s obrysem `sample/black w1 INSIDE`. Žádná barva mimo proměnné.
- **Změna (13. 10. 2026):** oba bloky jsou široké 44.5 jako v ostatních miniaturách (dříve `block-1` 43.5 a `block-2` 45.5 – rozdíl 2 px byl dvojnásobek tloušťky obrysu `block-2`). `block-1` má nyní pevnou šířku (`z=fix/fix`), `block-2` zůstává `z=fill/fix` a jako jediný blok má obrys (`sample/black w1 INSIDE`).
- Zbytek stavby a rozměrů je shodný s Mode preview/Normal (`171:4367`).

### Mode preview/Custom colors · `171:4412`

Samostatná komponenta · rozměr 115×89

Popis ve Figmě: „bez popisu“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Mode preview/Custom colors' 115x89 lay=V gap5 pad10,10,10,10 MIN/MIN; fill=reading/amber/page; st=border/strong w1 INSIDE; rad=8
 F 'heading' 56x6 z=fix/fix; fill=reading/amber/text; rad=2; clip=1
 F 'line-1' 93x4 z=fill/fix; fill=reading/amber/text; rad=2; clip=1
 F 'line-2' 90x4 z=fix/fix; fill=reading/amber/text; rad=2; clip=1
 F 'line-3' 69x4 z=fix/fix; fill=reading/amber/text; rad=2; clip=1
 F 'picture' 93x29 lay=H gap4 pad3,0,0,0 MIN/MIN; z=fill/hug; clip=1
  F 'block-1' 44.5x26 z=fill/fix; fill=reading/amber/text; rad=2; clip=1
  F 'block-2' 44.5x26 z=fill/fix; fill=reading/amber/text; rad=2; clip=1
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost fix/hug, ořez obsahu ano, obrys se počítá do rozložení ano.
- Komponenta je ilustrace (obrázek) – miniatura stránky pro Color mode card (`171:4545`). Rozměr 115×89, strom má 8 uzlů a je ve výpisu celý.
- Barvy vrstev: podklad `reading/amber/page` s obrysem `border/strong w1 INSIDE`; `heading`, řádky i oba bloky `reading/amber/text` (jediná barva popředí). Na rozdíl od ostatních miniatur používá tokeny `reading/*`, ne `sample/*`. Žádná barva mimo proměnné.
- Stavba a rozměry jsou shodné s Mode preview/Normal (`171:4367`), liší se jen barvy.

### Layout preview/Single page · `192:735`

Samostatná komponenta · rozměr 115×89

Popis ve Figmě: „bez popisu“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Layout preview/Single page' 115x89 fill=bg/track; st=border/strong w1 INSIDE; rad=8
 F 'page' 40x56 at=37,16; z=fix/fix; fill=reading/paper/page; st=sample/gray-light w1 INSIDE; rad=1; clip=1
  F 'line' 17x2 at=6,7; z=fix/fix; fill=sample/gray-mid; rad=1; clip=1
  F 'line' 28x2 at=6,13; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 28x2 at=6,18; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 28x2 at=6,23; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 28x2 at=6,28; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 20x2 at=6,33; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: bez automatického rozvržení, velikost fix/fix, ořez obsahu ano, obrys se počítá do rozložení ne.
- Komponenta je ilustrace (obrázek) – miniatura rozložení stránek. Rozměr 115×89, kořen nemá automatické rozvržení, vrstvy mají pevnou polohu. Strom má 8 uzlů a je ve výpisu celý.
- Barvy prvních vrstev: podklad `bg/track` s obrysem `border/strong w1 INSIDE` a `rad=8`; stránka `reading/paper/page` s obrysem `sample/gray-light w1 INSIDE` a `rad=1`; první řádek `sample/gray-mid`, další řádky `sample/gray-light`. Žádná barva mimo proměnné.
- Jedna stránka 40×56 na `at=37,16` (vodorovně uprostřed: 37 + 40 + 38 = 115).
- Podklad je token motivu aplikace (`bg/track`), stránka a řádky jsou tokeny `reading/*` a `sample/*`.
- Žádné vlastnosti, žádný text, žádná vnořená komponenta.

### Layout preview/Continuous · `192:743`

Samostatná komponenta · rozměr 115×89

Popis ve Figmě: „bez popisu“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Layout preview/Continuous' 115x89 fill=bg/track; st=border/strong w1 INSIDE; rad=8
 F 'page' 40x56 at=37,-32; z=fix/fix; fill=reading/paper/page; st=sample/gray-light w1 INSIDE; rad=1; clip=1
  F 'line' 17x2 at=6,7; z=fix/fix; fill=sample/gray-mid; rad=1; clip=1
  F 'line' 28x2 at=6,13; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 28x2 at=6,18; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 28x2 at=6,23; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 28x2 at=6,28; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 20x2 at=6,33; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'page' 40x56 at=37,30; z=fix/fix; fill=reading/paper/page; st=sample/gray-light w1 INSIDE; rad=1; clip=1
  F 'line' 17x2 at=6,7; z=fix/fix; fill=sample/gray-mid; rad=1; clip=1
  F 'line' 28x2 at=6,13; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 28x2 at=6,18; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 28x2 at=6,23; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 28x2 at=6,28; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 20x2 at=6,33; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: bez automatického rozvržení, velikost fix/fix, ořez obsahu ano, obrys se počítá do rozložení ne.
- Komponenta je ilustrace (obrázek) – miniatura rozložení stránek. Rozměr 115×89, kořen nemá automatické rozvržení. Strom má 15 uzlů a je ve výpisu celý.
- Barvy vrstev: podklad `bg/track` s obrysem `border/strong w1 INSIDE` a `rad=8`; stránky `reading/paper/page` s obrysem `sample/gray-light w1 INSIDE`; první řádek stránky `sample/gray-mid`, další `sample/gray-light`. Žádná barva mimo proměnné.
- Dvě stránky 40×56 pod sebou na `at=37,-32` a `at=37,30` (mezera 6). První stránka přesahuje horní okraj (y = −32 až 24, vidět je jen její spodních 24 px), druhá leží celá uvnitř (y = 30 až 86 z 89); přesahující část první stránky ořezává kořen.
- Žádné vlastnosti, žádný text, žádná vnořená komponenta.

### Layout preview/Two pages · `192:758`

Samostatná komponenta · rozměr 115×89

Popis ve Figmě: „bez popisu“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Layout preview/Two pages' 115x89 fill=bg/track; st=border/strong w1 INSIDE; rad=8
 F 'page' 36x50 at=20,19; z=fix/fix; fill=reading/paper/page; st=sample/gray-light w1 INSIDE; rad=1; clip=1
  F 'line' 14x2 at=6,7; z=fix/fix; fill=sample/gray-mid; rad=1; clip=1
  F 'line' 24x2 at=6,13; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,18; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,23; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,28; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 17x2 at=6,33; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'page' 36x50 at=59,19; z=fix/fix; fill=reading/paper/page; st=sample/gray-light w1 INSIDE; rad=1; clip=1
  F 'line' 14x2 at=6,7; z=fix/fix; fill=sample/gray-mid; rad=1; clip=1
  F 'line' 24x2 at=6,13; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,18; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,23; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,28; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 17x2 at=6,33; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: bez automatického rozvržení, velikost fix/fix, ořez obsahu ano, obrys se počítá do rozložení ne.
- Komponenta je ilustrace (obrázek) – miniatura rozložení stránek. Rozměr 115×89, kořen nemá automatické rozvržení. Strom má 15 uzlů a je ve výpisu celý.
- Barvy vrstev: podklad `bg/track` s obrysem `border/strong w1 INSIDE` a `rad=8`; stránky `reading/paper/page` s obrysem `sample/gray-light w1 INSIDE`; první řádek stránky `sample/gray-mid`, další `sample/gray-light`. Žádná barva mimo proměnné.
- Dvě stránky 36×50 vedle sebe na `at=20,19` a `at=59,19` (mezera 3; menší než stránka 40×56 miniatury Single page, řádky 14 / 24 / 17 místo 17 / 28 / 20).
- Žádné vlastnosti, žádný text, žádná vnořená komponenta.

### Layout preview/Two columns · `192:773`

Samostatná komponenta · rozměr 115×89

Popis ve Figmě: „bez popisu“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Layout preview/Two columns' 115x89 fill=bg/track; st=border/strong w1 INSIDE; rad=8
 F 'page' 36x50 at=20,-26; z=fix/fix; fill=reading/paper/page; st=sample/gray-light w1 INSIDE; rad=1; clip=1
  F 'line' 14x2 at=6,7; z=fix/fix; fill=sample/gray-mid; rad=1; clip=1
  F 'line' 24x2 at=6,13; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,18; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,23; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,28; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 17x2 at=6,33; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'page' 36x50 at=59,-26; z=fix/fix; fill=reading/paper/page; st=sample/gray-light w1 INSIDE; rad=1; clip=1
  F 'line' 14x2 at=6,7; z=fix/fix; fill=sample/gray-mid; rad=1; clip=1
  F 'line' 24x2 at=6,13; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,18; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,23; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,28; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 17x2 at=6,33; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'page' 36x50 at=20,30; z=fix/fix; fill=reading/paper/page; st=sample/gray-light w1 INSIDE; rad=1; clip=1
  F 'line' 14x2 at=6,7; z=fix/fix; fill=sample/gray-mid; rad=1; clip=1
  F 'line' 24x2 at=6,13; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,18; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,23; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,28; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 17x2 at=6,33; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'page' 36x50 at=59,30; z=fix/fix; fill=reading/paper/page; st=sample/gray-light w1 INSIDE; rad=1; clip=1
  F 'line' 14x2 at=6,7; z=fix/fix; fill=sample/gray-mid; rad=1; clip=1
  F 'line' 24x2 at=6,13; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,18; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,23; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,28; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 17x2 at=6,33; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'page' 36x50 at=20,86; z=fix/fix; fill=reading/paper/page; st=sample/gray-light w1 INSIDE; rad=1; clip=1
  F 'line' 14x2 at=6,7; z=fix/fix; fill=sample/gray-mid; rad=1; clip=1
  F 'line' 24x2 at=6,13; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,18; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,23; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,28; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 17x2 at=6,33; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
 F 'page' 36x50 at=59,86; z=fix/fix; fill=reading/paper/page; st=sample/gray-light w1 INSIDE; rad=1; clip=1
  F 'line' 14x2 at=6,7; z=fix/fix; fill=sample/gray-mid; rad=1; clip=1
  F 'line' 24x2 at=6,13; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,18; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,23; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 24x2 at=6,28; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
  F 'line' 17x2 at=6,33; z=fix/fix; fill=sample/gray-light; rad=1; clip=1
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: bez automatického rozvržení, velikost fix/fix, ořez obsahu ano, obrys se počítá do rozložení ne.
- Komponenta je ilustrace (obrázek) – miniatura rozložení stránek. Rozměr 115×89, kořen nemá automatické rozvržení. Strom má 43 uzlů a je ve výpisu celý.
- Barvy vrstev: podklad `bg/track` s obrysem `border/strong w1 INSIDE` a `rad=8`; stránky `reading/paper/page` s obrysem `sample/gray-light w1 INSIDE`; první řádek stránky `sample/gray-mid`, další `sample/gray-light`. Žádná barva mimo proměnné.
- Šest stránek 36×50 ve třech řadách po dvou: `at=20,-26` a `at=59,-26`, `at=20,30` a `at=59,30`, `at=20,86` a `at=59,86` (vodorovná mezera 3, svislá 6). Horní řada přesahuje horní okraj (y = −26 až 24), prostřední leží celá uvnitř (y = 30 až 80), ze spodní řady (y = 86 až 136) jsou uvnitř kořene jen horní 3 px; přesahující části ořezává kořen. Všech šest stránek má stejnou stavbu (stránka a šest řádků).
- Žádné vlastnosti, žádný text, žádná vnořená komponenta.

### Sample page · `192:1039`

Samostatná komponenta · rozměr 680×962

Popis ve Figmě: „Placeholder content of a document page for the View Document screens. Colors are constant in both themes because a PDF page does not follow the application theme.“

**Vlastnosti**

Bez vlastností.

**Stavba výchozí varianty**

```text
C 'Sample page' 680x962 lay=V gap14 pad56,72,56,72 MIN/MIN; fill=reading/paper/page; fx=DROP_SHADOW(0,2,10,0 effect/shadow)
 F 'running-head' 536x19 lay=H gap0 pad0,0,8,0 SPACE_BETWEEN/CENTER; z=fill/hug; clip=1
  T 'title' 116x11 z=hug/hug; fill=sample/gray-dark; f=Inter Medium 9/auto ls0.6px LT WIDTH_AND_HEIGHT UPPER; tx='Project Report 2026'
  T 'page-number' 18x11 z=hug/hug; fill=sample/gray-dark; f=Inter Medium 9/auto LT WIDTH_AND_HEIGHT; tx='308'
 F 'title' 536x45 lay=V gap4 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  T 'chapter' 64x12 z=hug/hug; fill=sample/blue; f=Inter Semi Bold 10/auto ls0.8px LT WIDTH_AND_HEIGHT UPPER; tx='Chapter 4'
  T 'heading' 190x29 z=hug/hug; fill=reading/paper/text; f=Inter Bold 24/auto LT WIDTH_AND_HEIGHT; tx='Market Analysis'
 T 'paragraph' 536x51 z=fill/hug; fill=reading/paper/text; f=Inter Regular 11/17 LT HEIGHT; tx='This chapter summarizes how the market developed during the reporting period and how the individual regions and customer segments contributed to the overall result. All figures are stated in millions of euros and compared with the same period of the previous year unless noted otherwise.'
 T 'heading-2' 139x17 z=hug/hug; fill=reading/paper/text; f=Inter Semi Bold 14/auto LT WIDTH_AND_HEIGHT; tx='4.1 Regional revenue'
 F 'paragraph-with-highlight' 536x51 lay=V gap0 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  F 'line' 484x17 lay=H gap0 pad0,0,0,0 MIN/CENTER; z=hug/hug; clip=1
   F 'highlight-annotation' 194x17 lay=H gap0 pad0,0,0,0 MIN/MIN; z=hug/hug; fill=annotation/yellow; rad=1; clip=1; blend=MULTIPLY
    T 'text' 194x17 z=hug/hug; fill=reading/paper/text; f=Inter Regular 11/17 LT WIDTH_AND_HEIGHT; tx='Revenue grew by 18 % year over year'
   T 'text' 290x17 z=hug/hug; fill=reading/paper/text; f=Inter Regular 11/17 LT WIDTH_AND_HEIGHT; tx=', driven mostly by new customers in Central Europe and'
  T 'paragraph' 536x34 z=fill/hug; fill=reading/paper/text; f=Inter Regular 11/17 LT HEIGHT; tx='by the renewal of long-term contracts in the Nordic countries. The western region remained stable, while the southern region recorded a slight decline caused by postponed public tenders.'
 F 'figure' 536x177 lay=V gap8 pad6,0,2,0 MIN/CENTER; z=fill/hug; clip=1
  F 'chart' 536x150 lay=H gap26 pad16,24,0,24 CENTER/MAX; z=fill/fix; st=sample/gray-light w0,0,1,0 INSIDE; clip=1
   F 'bars' 44x78 lay=H gap4 pad0,0,0,0 MIN/MAX; z=hug/hug; clip=1
    F 'previous' 20x62 z=fix/fix; fill=sample/blue-soft; rad=2; clip=1
    F 'current' 20x78 z=fix/fix; fill=sample/blue; rad=2; clip=1
   F 'bars' 44x96 lay=H gap4 pad0,0,0,0 MIN/MAX; z=hug/hug; clip=1
    F 'previous' 20x84 z=fix/fix; fill=sample/blue-soft; rad=2; clip=1
    F 'current' 20x96 z=fix/fix; fill=sample/blue; rad=2; clip=1
   F 'bars' 44x74 lay=H gap4 pad0,0,0,0 MIN/MAX; z=hug/hug; clip=1
    F 'previous' 20x70 z=fix/fix; fill=sample/blue-soft; rad=2; clip=1
    F 'current' 20x74 z=fix/fix; fill=sample/blue; rad=2; clip=1
   F 'bars' 44x122 lay=H gap4 pad0,0,0,0 MIN/MAX; z=hug/hug; clip=1
    F 'previous' 20x98 z=fix/fix; fill=sample/blue-soft; rad=2; clip=1
    F 'current' 20x122 z=fix/fix; fill=sample/blue; rad=2; clip=1
   F 'bars' 44x56 lay=H gap4 pad0,0,0,0 MIN/MAX; z=hug/hug; clip=1
    F 'previous' 20x56 z=fix/fix; fill=sample/blue-soft; rad=2; clip=1
    F 'current' 20x50 z=fix/fix; fill=sample/blue; rad=2; clip=1
   F 'bars' 44x88 lay=H gap4 pad0,0,0,0 MIN/MAX; z=hug/hug; clip=1
    F 'previous' 20x76 z=fix/fix; fill=sample/blue-soft; rad=2; clip=1
    F 'current' 20x88 z=fix/fix; fill=sample/blue; rad=2; clip=1
  T 'caption' 241x11 z=hug/hug; fill=sample/gray-dark; f=Inter Regular 9/auto LT WIDTH_AND_HEIGHT; tx='Figure 12 – Revenue by region, previous and current year'
 T 'heading-2' 163x17 z=hug/hug; fill=reading/paper/text; f=Inter Semi Bold 14/auto LT WIDTH_AND_HEIGHT; tx='4.2 Customer segments'
 T 'paragraph' 536x51 z=fill/hug; fill=reading/paper/text; f=Inter Regular 11/17 LT HEIGHT; tx='Enterprise customers account for more than a half of the revenue. The share of small and medium businesses keeps growing thanks to the simplified licensing introduced last year, which lowered the entry barrier for teams of up to fifty people.'
 F 'table' 536x125 lay=V gap0 pad0,0,0,0 MIN/MIN; z=fill/hug; clip=1
  F 'header-row' 536x25 lay=H gap12 pad6,4,6,4 MIN/CENTER; z=fill/hug; st=sample/gray-mid w0,0,1,0 INSIDE; clip=1
   T 'cell' 344x12 z=fill/hug; fill=reading/paper/text; f=Inter Semi Bold 10/auto LT HEIGHT; tx='Segment'
   T 'cell' 80x12 z=fix/hug; fill=reading/paper/text; f=Inter Semi Bold 10/auto RT HEIGHT; tx='Revenue'
   T 'cell' 80x12 z=fix/hug; fill=reading/paper/text; f=Inter Semi Bold 10/auto RT HEIGHT; tx='Change'
  F 'row' 536x25 lay=H gap12 pad6,4,6,4 MIN/CENTER; z=fill/hug; st=sample/gray-light w0,0,1,0 INSIDE; clip=1
   T 'cell' 344x12 z=fill/hug; fill=reading/paper/text; f=Inter Regular 10/auto LT HEIGHT; tx='Enterprise'
   T 'cell' 80x12 z=fix/hug; fill=reading/paper/text; f=Inter Regular 10/auto RT HEIGHT; tx='41.2'
   T 'cell' 80x12 z=fix/hug; fill=reading/paper/text; f=Inter Regular 10/auto RT HEIGHT; tx='+12 %'
  F 'row' 536x25 lay=H gap12 pad6,4,6,4 MIN/CENTER; z=fill/hug; st=sample/gray-light w0,0,1,0 INSIDE; clip=1
   T 'cell' 344x12 z=fill/hug; fill=reading/paper/text; f=Inter Regular 10/auto LT HEIGHT; tx='Small and medium business'
   T 'cell' 80x12 z=fix/hug; fill=reading/paper/text; f=Inter Regular 10/auto RT HEIGHT; tx='23.8'
   T 'cell' 80x12 z=fix/hug; fill=reading/paper/text; f=Inter Regular 10/auto RT HEIGHT; tx='+27 %'
  F 'row' 536x25 lay=H gap12 pad6,4,6,4 MIN/CENTER; z=fill/hug; st=sample/gray-light w0,0,1,0 INSIDE; clip=1
   T 'cell' 344x12 z=fill/hug; fill=reading/paper/text; f=Inter Regular 10/auto LT HEIGHT; tx='Public sector'
   T 'cell' 80x12 z=fix/hug; fill=reading/paper/text; f=Inter Regular 10/auto RT HEIGHT; tx='9.4'
   T 'cell' 80x12 z=fix/hug; fill=reading/paper/text; f=Inter Regular 10/auto RT HEIGHT; tx='−3 %'
  F 'row' 536x25 lay=H gap12 pad6,4,6,4 MIN/CENTER; z=fill/hug; st=sample/gray-light w0,0,1,0 INSIDE; clip=1
   T 'cell' 344x12 z=fill/hug; fill=reading/paper/text; f=Inter Regular 10/auto LT HEIGHT; tx='Education'
   T 'cell' 80x12 z=fix/hug; fill=reading/paper/text; f=Inter Regular 10/auto RT HEIGHT; tx='4.1'
   T 'cell' 80x12 z=fix/hug; fill=reading/paper/text; f=Inter Regular 10/auto RT HEIGHT; tx='+8 %'
 T 'paragraph' 536x34 z=fill/hug; fill=reading/paper/text; f=Inter Regular 11/17 LT HEIGHT; tx='The public sector remains the most volatile segment. Its results depend on the timing of tenders, and a single postponed contract can shift the revenue between two quarters.'
```

**Varianty a stavy**

Bez variant.

**Poznámky**

- Kořen: velikost fix/fix, ořez obsahu ano, obrys se počítá do rozložení ano (kořen sám obrys nemá).
- Komponenta je ilustrace (obrázek) – zástupný obsah stránky dokumentu pro obrazovky View Document. Rozměr 680×962, svislé rozvržení `gap14`, odsazení `56,72,56,72` (šířka obsahu 536), stín `DROP_SHADOW(0,2,10,0 effect/shadow)`. Strom má 60 uzlů a je ve výpisu celý.
- Barvy vrstev: papír `reading/paper/page`; záhlaví (`title`, `page-number`) a popisek grafu `caption` `sample/gray-dark`; `chapter` `sample/blue`; nadpisy, odstavce a buňky tabulky `reading/paper/text`; zvýraznění `highlight-annotation` `annotation/yellow` s násobením (`rad=1`; do 15. 10. 2026 `sample/orange-soft`, stejná barva jako výsledek hledání, Q-12); sloupce grafu `previous` `sample/blue-soft` a `current` `sample/blue`; dolní linka grafu a řádků tabulky `sample/gray-light`, dolní linka záhlaví tabulky `sample/gray-mid`. Žádná barva mimo proměnné.
- Podle popisu jsou barvy v obou motivech stejné (tokeny `reading/paper/*` a `sample/*`), protože stránka PDF se neřídí motivem aplikace; stín je token `effect/shadow`.
- Písma: Inter Medium 9 (záhlaví; `title` navíc `UPPER` a prostrkání 0.6 px), Inter Semi Bold 10 `UPPER` s prostrkáním 0.8 px (`chapter`), Inter Bold 24 (`heading`), Inter Semi Bold 14 (`heading-2`), Inter Regular 11 s výškou řádku 17 (odstavce), Inter Regular 9 (`caption`), Inter Semi Bold 10 (záhlaví tabulky) a Inter Regular 10 (řádky tabulky). Prostrkání `ls` je v pixelech.
- Stavba pod nadpisem `4.1 Regional revenue`: `paragraph-with-highlight` (řádek `line` se zvýrazněním a zbytek odstavce jako samostatný text), `figure` (graf `chart` a popisek `caption`), nadpis `4.2 Customer segments`, odstavec, `table` a závěrečný odstavec. Graf `chart` má pevnou výšku 150 a jen dolní linku (`w0,0,1,0`); šest skupin `bars` s mezerou 26, v každé dva sloupce šířky 20 (`previous`, `current`) s mezerou 4, zarovnané ke spodní hraně (`CENTER/MAX`); výšky `previous`/`current` jsou 62/78, 84/96, 70/74, 98/122, 56/50 a 76/88. Tabulka má záhlaví `header-row` a čtyři řádky `row` po 536x25, každý jen s dolní linkou; první buňka vyplňuje šířku (344), druhá a třetí mají pevnou šířku 80 a text zarovnaný vpravo (`RT`).
- Opakovaná jména vrstev: dva různé uzly `title` (text v `running-head` a rámec s `chapter` + `heading`), dva uzly `text` v řádku `line`, čtyři `paragraph` (jeden z nich uvnitř `paragraph-with-highlight`), dva `heading-2`, šest `bars` (v každém `previous` a `current`), čtyři `row` a patnáct `cell`.
- Žádné vlastnosti; žádný text není vázán na vlastnost; žádná vnořená komponenta. Text `'−3 %'` začíná znakem minus U+2212; mezery před `%` jsou obyčejné mezery.

## 7. Ukázkové rámce sekce

Stránka Components (`158:4605`), sekce `Components` (`46:2`). Přečteno čtecím skriptem (jen `children`, `name`, `id`, `type`, `width`, `height`, `characters`, `componentProperties` a `getMainComponentAsync()`, u dvou změněných příkladů i viditelné vrstvy uvnitř instancí s jejich výplní a obrysem). Stav po opravách z 13. 10. 2026; snímky (`get_screenshot`) pocházejí z prvního čtení a po opravách znovu pořízeny nebyly.

Sekce má 97 přímých potomků: 70 × COMPONENT_SET, 22 × COMPONENT a 5 × FRAME; je to jediný potomek stránky. Žádný přímý potomek sekce není TEXT ani jiný typ – nadpisy a popisky jsou až uvnitř rámců. Sekce má 2950×14549; po opravách byly komponenty v sekci znovu srovnány do řad a žádné dva přímé potomky se nepřekrývají. Poloha komponenty nebo rámce v sekci nemá význam a není součástí specifikace, tabulka proto uvádí jen rozměr (šířka×výška).

| Uzel | Typ | Název | Rozměr | Obsah |
| --- | --- | --- | --- | --- |
| `46:3` | FRAME | Icons | 720×912 | Přímí potomci jsou komponenty ikon (COMPONENT), žádná instance: icon/home, icon/file, icon/x-circle, icon/plus, icon/file-text, icon/folder, icon/clock, icon/gift, icon/info, icon/headphones, icon/heart, icon/settings, icon/sun, icon/moon, icon/search, icon/folder-open, icon/file-plus, icon/edit, icon/message-square, icon/layout, icon/repeat, icon/maximize, icon/lock, icon/grid, icon/file-tall, icon/more-vertical, icon/download, icon/play-circle, icon/book-open, icon/help-circle, icon/chevron-right, icon/alert-circle, icon/github, icon/star, icon/shield, icon/x, icon/minimize-2, icon/eraser, icon/ruler, icon/pen-tool, icon/tools, icon/monitor, icon/briefcase, icon/pin, icon/pin-off, icon/save, icon/cloud, icon/server, icon/folder-plus, icon/layers, … (prvních 50 názvů v pořadí rámce; rámec má celkem 364 potomků, všechny jsou komponenty `icon/<name>`, úplný seznam je v kapitole Ikony). Rámec je auto layout `H` se zalamováním, mezery 16/16, okraje 24, bez výplně |
| `53:396` | FRAME | Example – Recent Files, empty state | 445×407 | TEXT 'Recent Files – first start (empty state)'; FRAME 'recent-files' – uvnitř instance Text link{State=Default} a icon/download |
| `53:458` | FRAME | Example – Recent file rows | 480×220 | TEXT 'Long file name – truncated in the middle, extension stays visible'; INSTANCE 'Recent file row' → Recent file row{Status=Available, State=Default}; TEXT 'File no longer exists'; INSTANCE 'Recent file row' → Recent file row{Status=Missing, State=Default} |
| `262:806` | FRAME | Example – Tab states | 441×275 | TEXT 'Tab – Locked, Unsaved, Progress'; 3 × FRAME 'row' – v každém instance Tab{Selected=True, State=Default}; 2 × FRAME 'row' – v každém instance Tab{Selected=False, State=Default} |
| `265:1057` | FRAME | Example – Page selection, invalid range | 368×291 | TEXT 'Range with an error'; INSTANCE 'Page selection' → Page selection{Mode=Range, Error=True} |

Poznámky k tabulce:

- Rámec `46:3` Icons není příklad – je to úložiště komponent ikon; jeho ikony nejsou přímými potomky sekce, a proto nejsou v počtu 22 komponent sekce.
- U vnořených rámců (`recent-files`, `row`) jsou v tabulce uvedeny jen instance v nich obsažené; texty a obyčejné vrstvy jsou popsány u příkladů níže.
- Hodnoty vlastností instancí (přepínače, texty, varianty) jsou přečteny z `componentProperties` a uvedeny u jednotlivých příkladů níže. Vrstvy vypnuté přepínačem se uvnitř instance čtou jen s `figma.skipInvisibleInstanceChildren = false`.
- Čtyři příkladové rámce jsou svislý auto layout s mezerou 12 (nadpis, pod ním obsah).

### Příklady

- **`53:396` Example – Recent Files, empty state** – prázdný stav seznamu Recent Files při prvním spuštění. Není složen z instance řádku: rámec `recent-files` (397×330) je obyčejný rámec s nadpisem „Recent Files“, odkazem „View all“ (Text link, `State=Default`), textem „No recent files“ uprostřed a čárkovaně orámovanou plochou „Drag and drop a PDF file here to open it“ s ikonou icon/download. Z komponent předvádí jen Text link a ikonu icon/download; komponenta Recent file row zde použita není.
- **`53:458` Example – Recent file rows** – komponenta Recent file row ve dvou hodnotách vlastnosti `Status`, obě `State=Default`; obě instance mají 381×51 a `Show star=false`:
  - `Status=Available` – `File name="Technical_Docu…_final_v2.pdf"`, `Path="D:\Projects\PDF4QT\Docs\Archive"`, `Time="Yesterday 18:02"`. Příklad předvádí dlouhý název souboru zkrácený uprostřed tak, že přípona zůstává vidět; výpustka `…` je ale přímo součástí hodnoty vlastnosti `File name`, Figma zde nic nezkracuje. Pod názvem cesta, vpravo čas a tlačítko nabídky (tři tečky), ikona souboru červená;
  - `Status=Missing` – `File name="Building_Plan.pdf"`, `Path="C:\Users\Jakub\Desktop"`, `Time="Sep 18, 2025"` (čas se v této variantě nezobrazuje): soubor, který už neexistuje – šedá ikona, tlumený název, místo času červený štítek „No longer exists“, tlačítko nabídky zůstává.
- **`262:806` Example – Tab states** – komponenta Tab: nadpis `'Tab – Locked, Unsaved, Progress'` a pět řádků (každý `row` = jedna instance a popisek vpravo); varianty jen `Selected=True` (3 ×) a `Selected=False` (2 ×), vždy `State=Default`. Rozdíly mezi řádky jsou dány vlastnostmi instance `Label`, `Locked`, `Unsaved`, `Progress` a `Closable`; všechny instance mají `Icon=icon/file`. **Změna (13. 10. 2026):** všech pět instancí je vysokých 32 – i instance bez tlačítka zavření (`Closable=false`), které dříve měly 28; Tab má nově nejmenší výšku 32. Rámec je proto o 8 px vyšší (441×275, dříve 441×267) a všech pět řádků `row` má výšku 32. Řádky (popisek, instance, vlastnosti, vzhled):
  - „Locked“ – instance `262:809`, `Selected=True, State=Default`, 177×32: `Label="Contract.pdf"`, `Locked=true`, `Unsaved=false`, `Progress=false`, `Closable=true` – vybraná karta s ikonou zámku a křížkem (`close` = Tab close button{State=Default}, 20×20);
  - „Unsaved changes (dot replaces close)“ – instance `262:824`, `Selected=True, State=Default`, 153×32: `Label="Report_v3.pdf"`, `Locked=false`, `Unsaved=true`, `Progress=false`, `Closable=false` – vybraná karta, místo křížku tečka (`unsaved` 8×8, `text/primary`);
  - „Locked + unsaved (Edit a Copy)“ – instance `262:838`, `Selected=True, State=Default`, 209×32: `Label="Contract (copy).pdf"`, `Locked=true`, `Unsaved=true`, `Progress=false`, `Closable=false` – vybraná karta se zámkem i tečkou;
  - „Long operation in progress“ – instance `262:852`, `Selected=False, State=Default`, 170×32: `Label="Scan_2026.pdf"`, `Locked=false`, `Unsaved=false`, `Progress=true`, `Closable=true` – nevybraná karta s křížkem a s pruhem průběhu na dolním okraji (`progress` 170×2 s výplní `bg/track`, `progress>value` 93.37×2 s výplní `bg/accent`);
  - „Locked, inactive tab“ – instance `262:876`, `Selected=False, State=Default`, 165×32: `Label="Invoice.pdf"`, `Locked=true`, `Unsaved=false`, `Progress=false`, `Closable=true` – nevybraná karta se zámkem a křížkem.
  - Barvy v instancích: ikona souboru `icon/accent` ve vybrané a `icon/muted` v nevybrané kartě, popisek `text/primary` a `text/tertiary`, zámek s výplní `accent/amber/icon` a tahem `icon/default`, křížek `icon/muted`. Vybraná karta má výplň `bg/subtle` a obrys `border/default`, nevybraná nemá výplň ani obrys.
- **`265:1057` Example – Page selection, invalid range** – komponenta Page selection s chybným rozsahem: nadpis `'Range with an error'` a instance `265:1059`, 320×224. **Změna (13. 10. 2026):** instance je varianta `Mode=Range, Error=True` (chyba je nově hodnota vlastnosti varianty `Error`, dříve přepínač zapnutý na variantě `Mode=Range`) a pole rozsahu je ve stavu `State=Error` (dříve bylo přepsané na `State=Focus`). Rozměr instance ani rámce se nezměnil.
  - Vlastnosti instance: `Label="Pages"`, `Total="48 pages"`, `Count="No pages selected"`, `Error text="Page 60 does not exist – the document has 48 pages"`.
  - Přepsání uvnitř instance: vnořené pole `range>range-field` (320×34) je `Text field{State=Error}` s `Value="1-3, 60"` (v komponentě `Value='1-3, 8, 10-12'`); obrys `text/danger w1 INSIDE`, výplň `bg/surface`, text `text/primary` – stav Error je součástí varianty, hodnota je jediné přepsání pole.
  - Režimy: `mode-all`, `mode-current`, `mode-selected` a `mode-range` jsou od 16. 10. 2026 (Q-9) segmenty `Segment{State=Default}` bez ikony, stejně široké, na dráze `bg/track`, vybraný `mode-range` (`Selected=True`, deska `bg/surface` se stínem, popisek `text/secondary`); dříve řada Filter chip.
  - Chybu ukazují tři věci: pole v `State=Error`, řádek `error` (320×14; ikona `icon/alert-circle` s obrysem `icon/danger`, zpráva 300×13 v `text/danger`) a souhrn `Count`; ikona stavového řádku zůstává `icon/check-circle` s `icon/muted`, text souhrnu `text/secondary`.
  - Skladba instance shora: záhlaví „Pages“ a „48 pages“ (15), čtyři segmenty All / Current / Selected / Range (32, vybraný Range), textové pole „1-3, 60“ s červeným obrysem 1 px (34), skupina „Only“ se čtyřmi nezaškrtnutými políčky Even pages, Odd pages, Portrait, Landscape (65; každé políčko `Checkbox{Checked=False, State=Default}` 156×18), řádek „No pages selected“ (14) a červená chybová zpráva s ikonou (14); mezi bloky mezera 10.

## 8. Ikony

Zdroj ve Figmě: soubor `QVGmx58DYmMq41kwfpMjtl`, stránka Components (`158:4605`), rámec `Icons` v sekci `Components`,
komponenty pojmenované `icon/<name>`. Stav po opravách z 13. 10. 2026 (kořeny 17 ikon bez skryté výplně, tři ikony `compress-*`
s vystředěnou kresbou); rozměry, výplně a ořez kořenů a geometrie tří opravených ikon byly po opravách přečteny znovu. Čteno jedním voláním `use_figma` (skript pouze četl: `setCurrentPageAsync`,
`findAllWithCriteria({types:['COMPONENT']})`, `name`, `width`, `height`, `parent.name`, `fills`, `strokes`,
`strokeWeight`, `visible`; nic nenastavoval, nevytvářel ani nemazal).

Zdroj v repozitáři: adresář `E:\Programming\PDF\PDF_For_Qt\PdfForQt_bugfixing\Pdf4QtLibFluent\resources\icons`
(soubory `<name>.svg`; adresář byl jen vypsán a soubory čteny, nic nebylo změněno).

### Počty

| | Počet |
| --- | --- |
| Ikony ve Figmě (`icon/<name>`) | 364 |
| Soubory `.svg` v repozitáři | 364 |

- Všech 364 komponent `icon/…` leží přímo v rámci `Icons` (rodič `FRAME:Icons`), žádná mimo něj.
- V rámci `Icons` není žádná komponenta, jejíž jméno nezačíná `icon/`.
- Žádné jméno ikony se ve Figmě neopakuje.
- Adresář repozitáře obsahuje jen soubory `.svg` (364), žádné jiné soubory; jména se neliší ani velikostí písmen.
- Stránka Components obsahuje celkem 940 uzlů typu COMPONENT: 364 ikon, 554 variant v 70 sadách a 22 samostatných komponent (před opravami 836; 16. 10. 2026 přibyla ikona `icon/highlight-comment` a dvě varianty Disabled sady Segment).

### Ikony, které jsou ve Figmě a chybí v repozitáři

Žádná. Každá z 364 ikon Figmy má soubor `<name>.svg`.

### Soubory repozitáře bez ikony ve Figmě

Žádný. Každý z 364 souborů má komponentu `icon/<name>`.

### Ikony jiného rozměru než 24×24

Ve Figmě (rozměr komponenty):

| Ikona | Rozměr ve Figmě | Rozměr souboru v repozitáři |
| --- | --- | --- |
| `file-tall` | 18×22 | 18×22 (`width="18" height="22" viewBox="0 0 18 22"`) |

Všech ostatních 362 komponent má ve Figmě 24×24.

Soubory repozitáře, jejichž kořenový `<svg>` nemá 24×24, ačkoli komponenta ve Figmě má 24×24. Jsou to staré exporty
z doby, kdy kresba těchto tří ikon přesahovala rám komponenty (vektor `glyph` ležel na dvojnásobku správné polohy):

| Soubor | Rozměr v souboru (starý export) | Komponenta ve Figmě po opravě |
| --- | --- | --- |
| `compress-fax.svg` | 25×25 (`viewBox="0 0 25 25"`) | 24×24, `glyph` 18×16 na 3,4, vykreslené hranice 2,3 až 22,21 |
| `compress-flate.svg` | 26×25 (`viewBox="0 0 26 25"`) | 24×24, `glyph` 14×18 na 5,3, vykreslené hranice 4,2 až 20,22 |
| `compress-rle.svg` | 24×25 (`viewBox="0 0 24 25"`) | 24×24, `glyph` 18×12 na 3,6, vykreslené hranice 3,5 až 21,19 |

Ostatních 359 souborů má `width="24" height="24"`. Ve Figmě je kresba těchto tří ikon od 13. 10. 2026 vystředěná a celá, včetně tahu, leží uvnitř 24×24 (dříve sahaly vykreslené hranice `glyph` u `compress-fax` do x = 25 a y = 25, u `compress-flate` do x = 26 a y = 25, u `compress-rle` do y = 25); kořeny obsah neořezávají. **Tři soubory `.svg` v repozitáři jsou stále staré exporty a je třeba je vyexportovat z Figmy znovu** – nový export má `width="24" height="24" viewBox="0 0 24 24"` jako ostatní ikony. Do té doby se soubor a komponenta u těchto tří ikon liší rozměrem i polohou kresby.

### Ikony s jiným obrysem než 2 px nebo s výplní

Jiná tloušťka obrysu než 2 px: žádná. Každý uzel uvnitř ikon, který má viditelný obrys, má `strokeWeight` 2
(žádná smíšená tloušťka). Shodně v repozitáři: jediná hodnota atributu `stroke-width` ve všech souborech je `2`.

Ikony s vyplněným uzlem (8 ikon):

| Ikona | Co se liší ve Figmě | Soubor v repozitáři |
| --- | --- | --- |
| `tools` | 4 uzly ELLIPSE s výplní, bez obrysu | 4 `<circle>` s `fill`, žádný `stroke` |
| `star-filled` | 1 uzel VECTOR s výplní i obrysem (obrys 2 px) | 1 `<path>` s `fill` i `stroke`, `stroke-width="2"` |
| `compress-bitmap` | 1 uzel VECTOR s výplní bez obrysu (vedle obrysového tvaru 2 px) | 2 `<path>` s `fill` + 1 `<path>` se `stroke`, `stroke-width="2"` |
| `linux` | 1 uzel VECTOR s výplní bez obrysu | 1 `<path>` s `fill`, žádný `stroke` |
| `match-case` | 1 uzel VECTOR s výplní bez obrysu | 2 `<path>` s `fill`, žádný `stroke` |
| `regex` | 1 uzel VECTOR s výplní bez obrysu | 2 `<path>` s `fill`, žádný `stroke` |
| `whole-word` | 1 uzel VECTOR s výplní bez obrysu | 3 `<path>` s `fill`, žádný `stroke` |
| `wildcard` | 1 uzel VECTOR s výplní bez obrysu | 2 `<path>` s `fill`, žádný `stroke` |

- Stejných 8 souborů je jedinými soubory repozitáře s barevným atributem `fill="#…"` – repozitář a Figma se shodují. Devátý soubor s atributem `fill` jiným než `none` na tvaru je `eye-off.svg`: `<rect width="24" height="24" fill="white"/>` uvnitř `<clipPath>`, tedy ořez, ne kresba.
- Ikony `linux`, `match-case`, `regex`, `whole-word`, `wildcard` a `tools` nemají v souboru žádný obrys, jsou čistě
  vyplněné (u písmenových ikon jde o text převedený na křivky).
- Žádná komponenta ikony nemá na svém kořeni výplň ani obrys, viditelné ani skryté (skrytá výplň `#ffffff` na kořeni 17 komponent byla 13. 10. 2026 odstraněna, viz oddíl o kořeni komponenty níže). Žádná neobsahuje skrytý uzel ani tvar bez barvy.

### Kořen komponenty ikony: výplň, ořez, popis

- Kořen žádné z 364 komponent ikon nemá výplň. 17 komponent mělo na kořeni vypnutou (`visible=false`) výplň `#ffffff` bez vazby na proměnnou, kterou dědily i jejich instance; 13. 10. 2026 byla odstraněna: `scissors`, `image`, `align-left`, `key`, `eye-off`, `columns`, `bar-chart-2`, `volume-2`, `paperclip`, `type`, `bookmark`, `zap`, `chevron-up`, `eye`, `droplet`, `sliders`, `presentation`. Čtení po opravách nenašlo v žádné komponentě sekce (včetně vnořených instancí ikon) skrytou výplň ani skrytý obrys a žádnou plnou barvu výplně nebo obrysu mimo proměnné (barvy efektů se při tom nečetly).
- 18 komponent ikon má kořen, který ořezává obsah (`clipsContent=true`): uvedených 17 a `copy` (ořez oprava nezměnila). Kořen ostatních 345 obsah neořezává. Instance těchto 18 ikon mají proto ve stromech karet `clip=1` (například `icon/chevron-up` v Signature card, `icon/copy` v Property row a Font row).
- Dvě komponenty ikon mají ve Figmě popis: `optimize-images` – „Optimize Images tool: downsample and recompress images.“ a `optimize-structure` – „Optimize Structure tool: merge duplicate objects and compact the file structure.“ Ostatních 361 je bez popisu.
- Všechny viditelné obrysy (384 uzlů) i výplně (11 uzlů) ikon jsou navázány na proměnnou `icon/default`. Obrys je vždy zarovnán na střed (`CENTER`), zakončení čar je `ROUND` u 378 uzlů a `NONE` u 5. Ikony mají 1 uzel (362 ikon), 2 uzly (`compress-bitmap`: vyplněné buňky a obrys) nebo 4 uzly (`tools`); uzly jsou `VECTOR` (364) a `ELLIPSE` (4). **Změna 14. 10. 2026 (N-17):** 26 obrysových ikon, které měly dva vektory `glyph`, má jediný vektor – cesty druhého vektoru jsou přidány do prvního (stejný identifikátor, takže přepsané barvy instancí platí dál), zakončení i spoje čar zůstaly `ROUND` a vykreslené hranice se nezměnily: `box-edit`, `confidence`, `crop-handles`, `deskew`, `detect`, `export`, `files`, `fill`, `grid-mm`, `image-replace`, `line-diag`, `loupe`, `mark-accept`, `mark-reject`, `multi-create`, `not-text`, `ocr`, `ocr-project`, `page-position`, `region-add`, `region-exclude`, `text-label`, `text-line-add`, `transform`, `view-split`, `word-overlay`. Soubory `.svg` v repozitáři se tím nemění (kresba je stejná). Údaje o počtech uzlů obrysu (383) a zakončení (378 `ROUND`) výše jsou z doby před sloučením; po něm je obrysových uzlů 357 a `ROUND` 352. Kořen žádné ikony nemá auto layout.
- V repozitáři se ořez kořene projevil jen u `eye-off.svg`: je to jediný soubor s atributem `clip-path` (skupina `icon/eye-off` má `clip-path="url(#clip0_0_380)"`, v `<defs>` je `<clipPath>` s obdélníkem 24×24).

### Poznámky k metodě

- Skript hlásil u každé ikony: rozměr jiný než 24×24; každý vnořený uzel s viditelným obrysem jiné tloušťky než 2;
  každý vnořený uzel s viditelnou výplní; uzel bez barvy; skrytý uzel; výplň nebo obrys kořene komponenty.
  Vektory s obrysem 2 px bez výplně se nehlásí (jsou výchozí případ).
- Počet vyplněných uzlů ve Figmě (1 VECTOR) a počet `<path>` v souboru se u písmenových ikon liší, protože export
  rozdělil jeden vektor na více cest; nejde o rozdíl obsahu.
- Porovnání jmen bylo provedeno přesně (řazení po bajtech, rozdíl množin v obou směrech je prázdný).
- Při ověření dokumentu byly navíc čteny `clipsContent`, `description`, vypnuté výplně kořene, proměnné barev obrysů a výplní, `strokeAlign`, `strokeCap` a vykreslené hranice (`absoluteRenderBounds`).
- Po opravách z 13. 10. 2026 byly znovu přečteny: počet a typ potomků rámce `Icons`, rozměr každé ikony, výplně kořene (i vypnuté), `clipsContent`, poloha, rozměr a vykreslené hranice vektoru `glyph` tří ikon `compress-*` a skryté nebo nevázané barvy ve všech komponentách sekce. Ostatní údaje kapitoly (tloušťky a zakončení obrysů, vyplněné ikony, popisy, porovnání jmen se soubory) pocházejí z prvního čtení; opravy se jich netýkaly.

### Seznam jmen ikon ve Figmě (seřazeno, 364)

accessibility, ai, alert-circle, alert-triangle, align-left, alt-text, anchor-point, angle, area, array, arrow-down, arrow-up, arrow-up-down, award, ban, bar-chart-2, book-open, bookmark, bookmark-plus, box-edit, boxes-show, boxes-write, braces, briefcase, calendar, calibrate, camera, caret, categories, center-both, center-h, center-v, certificate, certificate-add, certificate-lock, chain, chars, check, check-circle, chevron-down, chevron-first, chevron-last, chevron-left, chevron-right, chevron-up, circle, clipboard, clipboard-cut, clock, cloud, code, colorspace, columns, compare, compass, compress-auto, compress-bitmap, compress-fax, compress-flate, compress-rle, confidence, copy, corner-down-left, corner-down-right, crop, crop-handles, crop-off, crosshair, csv, cursor, cuts-clear, densitometer, deskew, destination, detect, dictionary, diff-markers, diff-next, diff-original, diff-overlay, diff-pages, diff-prev, diff-revised, diff-side, dot, download, dpi, drive, droplet, duplicate-page, edit, encoding, eraser, expand, export, external-link, extract, eye, eye-off, file, file-plus, file-tall, file-text, file-wide, files, fill, filter, fit-height, fit-image, fit-width, flag, folder, folder-open, folder-plus, font, form-fill, freehand-curve, gamut, gift, github, globe, gradient, grid, grid-mm, group, gstate, hash, headphones, heart, help-circle, hidden-data, highlight-comment, highlighter, histogram, home, html, icc-profile, image, image-replace, images, import, info, ink-coverage, ink-warning, insert-after, insert-before, insert-first, insert-last, intent, interleave, interleave-reversed, key, keyboard, label, languages, layers, layout, layout-columns, layout-form, layout-grid, layout-rows, line, line-diag, line-h, line-v, link, link-off, linux, list, list-ordered, locate, lock, loupe, mail, margins, mark-accept, mark-reject, match-case, maximize, mcp, measure-h, measure-line, measure-v, message-square, metadata, mic, minimize-2, minus, monitor, moon, more-horizontal, more-vertical, move-to, multi-create, network, not-text, obj-align-bottom, obj-align-hcenter, obj-align-left, obj-align-right, obj-align-top, obj-align-vcenter, object, object-type, ocr, ocr-project, offset, opacity-channel, optimize-images, optimize-structure, order-forward, orientation-auto, overwrite, page-current, page-image, page-insert, page-moved, page-pair, page-position, page-same, pages, panel-left, paper-white, paperclip, password, pattern, pause, pen-tool, perimeter, perspective, pfx-file, pie-chart, pilcrow, pin, pin-compare, pin-off, pixels, play, play-circle, plus, plus-circle, polygon, polyline, power, presentation, print-draft, printer, projection, range, rect-area, rect-perimeter, redact, redact-area, redact-page, redact-search, redact-text, redo, reference, refresh-cw, regex, region-add, region-exclude, repeat, replace, report, reverse, rich-black, rotate-180, rotate-ccw, rotate-cw, rotation-reset, ruler, same-height, same-size, same-width, save, save-as, scale, scanner, scissors, scribble, script, search, search-index, select-all, select-invert, select-none, select-rect, separations, server, settings, shape-channel, shape-rect, shield, shield-check, shield-lock, shield-plus, sign-certificate, signature, signature-field, skip, sliders, snap, soft-proof, sort-az, sort-source, split-pages, split-spread, square, square-round, squiggly, stamp, star, star-filled, sticky-note, stop, stream, strength, strikethrough, sun, swap, sync-view, tab, table, terminal, text-align-center, text-align-right, text-as-vector, text-box, text-cursor, text-hidden, text-label, text-line-add, text-lines, timestamp, to-annotation, tools, tooltip, transform, trash, trash-restore, tree, txt, type, underline, undo, ungroup, units, unlock, user, vector-shape, view-split, volume-2, volume-off, waveform, whole-word, wildcard, word-overlay, x, x-circle, xml, xobject-form, zap, zoom-in
