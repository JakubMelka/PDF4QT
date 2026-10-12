# UX revize návrhu sjednocené aplikace PDF4QT

Datum: 3. 10. 2026. Revidovaný podklad: [tools.md](tools.md), návrh z 2. 10. 2026.

**Sjednocení čtyř aplikací doporučuji. Katalog je dobrý základ a má rozsáhlé funkční pokrytí. Před implementací bych ale doplnil model práce s dokumenty, ukládání a dohledatelnost běžných činností. Z nynějšího návrhu ještě neplyne, že aplikace bude snadno použitelná.** Největší přínos sjednocení nastane tehdy, když uživatel plynule přejde od čtení k úpravám, skládání stránek a porovnání, se zachovanou rozdělanou prací.

**Závazná podmínka zadání: žádná současná funkcionalita nesmí být odstraněna bez plnohodnotné náhrady v nové aplikaci.** Přesun do jiného panelu nebo sloučení dialogů je možné, pokud zůstanou dostupné dosavadní možnosti, výsledky a uložená práce. Tento požadavek má přednost před zjednodušením katalogu, pevným rozvržením i termínem nahrazení starých aplikací. Konkrétní kontrola je v kapitole 11.

Revize hodnotí především uživatelské postupy a vnitřní konzistenci plánu. Vychází z celého dokumentu a cílené kontroly lokálního kódu Vieweru, společného controlleru, PageMasteru, skenování a ořezu. Nejde o úplný audit všech akcí, ověření větve OCR ani test hotového GUI. Živou Figmu jsem neposuzoval. Doporučení k chování uživatelů jsou hypotézy pro prototyp; nejsou výsledkem uživatelského výzkumu. Konkurenční matici z kapitoly 7 jsem znovu celou neověřoval.

## 1. Co je na návrhu dobré

- **Jeden vstup do aplikace.** Uživatel nebude muset předem vědět, kterou ze čtyř aplikací otevřít, aby dokončil úkol.
- **Pojmenování podle výsledku.** Compress, Extract Pages nebo Recognize Text (OCR) lépe vysvětlují účel než názvy interních dialogů.
- **Mapa současných funkcí v kapitole 6.** Je vhodným základem pro kontrolu migrace. Zahrnuje i méně viditelné funkce, jako jsou osobní záložky, měření nebo pracovní plochy.
- **Společná pracovní plocha pro stránky.** Jedenáct vstupních bodů může zjednodušit hledání, pokud vedou do stejného prostředí a zachovávají výběr i historii.
- **Hledání se synonymy, připnutí a dostupnost z dokumentu.** Tyto cesty umožňují obsloužit příležitostného i zkušeného uživatele.
- **Náhled, vysvětlení nedostupnosti, průběh a výchozí ukládání kopie u nevratných zásahů.** To jsou správné základy důvěry v aplikaci.
- **Rozlišení změny pohledu a změny souboru.** Plán správně upozorňuje na otočení, barvy a rozdíl mezi anotací a obsahem stránky.

Tyto principy bych zachoval. Počet 45 nástrojů sám o sobě nepovažuji za problém; rozhodující je, kolik voleb uživatel vidí v konkrétní situaci a zda rozumí jejich výsledku.

## 2. Nejdůležitější nálezy

Priority níže hodnotí dopad na sjednocení GUI. Nejsou totožné s prioritami nových funkcí P1–P3 v původním dokumentu.

| Priorita | Nález | Důsledek pro uživatele | Doporučená změna |
| --- | --- | --- | --- |
| Nutné před implementací | Plán výslovně ruší zobrazení lišt a Prepare Icon Theme bez popsané náhrady (§6.2) | Uživatel přijde o dosavadní možnosti přizpůsobení. | Doplnit funkčně rovnocennou náhradu; označení „zaniká“ není přijatelný výsledek migrace. |
| Nutné před implementací | Nejasný vztah dokumentu, pracovní plochy a nástroje (§5.1, §9.3) | Neví, zda upravuje originál, sestavuje nový soubor nebo jen mění náhled. | Definovat typy pracovních karet a společný postup spuštění, potvrzení a uložení. |
| Nutné před implementací | Vyřazené položky ztrácejí synonyma (§10.3/5), přestože hledání má najít vše (§9.1) | Hledání „vyplnit formulář“ či „číst nahlas“ může skončit bez správného výsledku. | Převést synonyma na odpovídající příkazy a panely; nabídnout i viditelné vstupy. |
| Nutné před implementací | Read-only je odvozené od technického průběhu operace (§4.7) | Uživatel nemůže vytvořit upravenou kopii, přestože originál chce pouze chránit. | Rozlišit režim čtení, zákaz úprav, oprávnění PDF a nemožnost zapsat soubor. |
| Nutné před implementací | Apply, Save a vytvoření výsledku nemají společná pravidla (§9.3) | Hrozí neuložený výsledek, přepsání zdroje nebo odeslání původního souboru. | Popsat výstup, označení neuložených změn, Undo a pokračování na výsledku. |
| Nutné před implementací | Chybí pravidla souběhu a selhání (§9.3/5) | OCR může doběhnout nad starou verzí; chyba zápisu může působit jako úspěch. | Definovat stav dokumentu během běhu, zrušení a částečné dokončení. |
| Důležité pro první vydání | Formuláře a anotace jsou funkčně zachované, ale jejich nalezení není dotažené (§1, §5.3) | Začátečník neví, kam kliknout, zvlášť u naskenovaného formuláře. | Viditelný vstup „Vyplnit a podepsat“, zvýraznění polí a cesta k doplnění textu. |
| Důležité pro první vydání | Přístupnost se omezuje hlavně na barvu a kontrast (§9.2) | Operace závislé na tažení nebo myši budou pro část lidí obtížné. | Navrhnout klávesnici, čtečku obrazovky, zvětšení UI a alternativy tažení. |
| Důležité pro první vydání | Některé názvy slibují víc, než funkce dělá (§3, §5.5, §8.2) | Uživatel zamění šedé obrázky za šedý dokument nebo Flatten za ochranu. | Upravit názvy a popisy podle skutečného výsledku. |
| Ověřit prototypem | Kategorie a jediná příslušnost se odvozují od barev (§2/4, §2/6) | Funkce se hůř hledá podle uživatelova očekávání. | Zachovat primární kategorii, dovolit další vstupy a ověřit třídění. |

## 3. Katalog musí podporovat hledání úkolů napříč aplikací

### 3.1 Interní rozdělení nesmí rozhodovat o dohledatelnosti

Rozdělení na tool, příkaz, panel a režim myši je užitečné pro návrh systému. Uživatel ale přichází s úkolem „doplnit jméno“, „zvýraznit text“ nebo „ověřit podpis“. Nepotřebuje vědět, do které vrstvy implementace úkol patří.

**Bod §10.3/5 bych změnil.** Původní synonyma vyřazených položek mají přejít na jejich nové cíle. Například `read aloud` a „číst nahlas“ otevřou panel Speech, `verify signature` panel Signatures a `fill form` aktivuje pomoc s vyplněním. Bez dokumentu se nejprve nabídne jeho otevření a potom se pokračuje v původně zvoleném úkolu.

V katalogu nemusí přibýt sedm plnohodnotných nástrojů. Doporučuji však viditelné odkazy na nejčastější činnosti: **Komentovat a zvýraznit, Vyplnit a podepsat, Otevřít přílohy, Ověřit podpisy, Číst nahlas.** Odkazy mohou otevírat existující režim nebo panel. Zejména v téměř prázdné kategorii Edit bude jinak snadné přehlédnout značnou část schopností aplikace.

Viditelné volby snižují nároky na vybavování příkazů z paměti. To je princip popsaný v [NN/g: Recognition and Recall](https://www.nngroup.com/articles/recognition-and-recall/); konkrétní výběr uvedených odkazů je doporučení této revize.

### 3.2 Globální hledání a hledání v dokumentu musí být rozlišitelné

Zachoval bych Ctrl+K pro „Najít nástroj nebo příkaz“ a Ctrl+F pro text dokumentu. Popisek musí vysvětlit rozsah. Pokud hledání v Tools omezuje kategorie, má být filtr vidět a musí existovat snadná volba „Hledat všude“.

Doplnit pravidla pro tyto případy:

- Český název, česká synonyma bez diakritiky, původní anglický název i názvy ze starého UI vedou ke stejnému výsledku. Anglické a přeložené výrazy se mají spojovat, ne navzájem nahrazovat.
- Dotazy „přidat obrázek“, „vložit stránku“, „podpis“ a „otočit“ ukážou rozdíly účelu. Uživatel nesmí po spuštění teprve zjistit, že vložil celou novou stránku místo obrázku na stránku.
- Enter spustí zvýrazněný výsledek. U operací s nevratným dopadem otevře přípravu, nikoli okamžité provedení; týká se to i Remove External Links bez vlastního nastavení.
- Skryté odborné nástroje zůstanou dohledatelné. Označení Advanced samo o sobě neznamená nedostupnost.
- Chybějící funkce musí být výslovně označená jako nepodporovaná. Plánovaná funkce nesmí vypadat jako spustitelná karta ani být prvním výsledkem před dostupnou akcí.

### 3.3 Šest kategorií je přijatelná hypotéza

Šest kategorií bych pro první prototyp zachoval. Počet dostupných barev ale není uživatelský důvod pro jejich počet. Stejně tak z druhého odkazu na stejný nástroj neplyne nutnost změnit jeho barvu.

Každý nástroj může mít jednu hlavní kategorii, jednu identitu a jednu barvu, ale být dostupný i přes související úkol. Set Page Boxes lze nabídnout u tiskové přípravy, OCR u úprav skenů a podpis u vyplnění formuláře. Podobné omezené vícečetné zařazení doporučuje [NN/g: Polyhierarchies](https://www.nngroup.com/articles/polyhierarchy/); konkrétní přesahy je třeba ověřit s uživateli.

Zkrácené štítky nesmí skrýt podstatnou část názvu: samotné **Protect** málo napovídá, že zde najdu podpis. Pro první vydání bych změnil **Optimize & Repair** na **Optimize**, dokud nebude oprava skutečně dostupná. Popis Review jako práce, která dokument nemění, zase potřebuje připustit ukládání měření do anotací.

## 4. Sjednotit pracovní prostředí, ne pouze spouštěče

### 4.1 Doporučené typy pracovních karet

| Typ | Příklad | Co je pro uživatele podstatné |
| --- | --- | --- |
| Dokument | Čtení, anotace, úprava textu, otočení stránek | Jedna aktuální verze, jeden stav uložení a srozumitelná historie změn. |
| Sestava stránek | Sloučení více zdrojů, pokročilé skupiny PageMasteru | Viditelné zdroje a výstup; uložení pracovní plochy je odlišené od vytvoření PDF. |
| Porovnání | Dvě verze smlouvy | Pojmenované vstupy, jejich verze, seznam rozdílů a vlastní výstupní zpráva. |

V §9.3/2 nestačí přiřadit vlastní kartu celé kategorii Organize Pages. Otočení tří stránek otevřeného PDF patří přirozeně do jeho karty. Sestavení nového PDF z více zdrojů má jiný pracovní kontext. Technologie náhledové plochy může být společná.

**Manage Pages bych zpřístupnil přímo z náhledů dokumentu.** Rotate, Remove, Extract a Insert mají být dostupné nad výběrem bez návratu do Tools. Katalogové karty mohou otevřít tentýž pohled s předvolenou akcí. Při přechodu Remove → Rotate → Extract se zachová výběr, pořadí i historie a nevzniknou tři kopie rozpracovaného dokumentu.

### 4.2 Společná pravidla spuštění

- Z dokumentu se předvolí právě tento dokument a případný výběr stránek. Uživatel vidí například „Smlouva.pdf · vybrány 3 stránky“.
- Z globálního katalogu je cíl výslovně vidět. Při více otevřených PDF se nesmí tiše použít náhodně poslední karta.
- Bez dokumentu se vybere soubor pouze tam, kde je potřeba. Scan to PDF a Blank PDF musí umožnit začít nový dokument.
- U více vstupů se vedle souborů z disku nabídnou i otevřené dokumenty. U změněného dokumentu musí být jasné, že se použije aktuální rozpracovaná verze, případně uživatel vědomě zvolí uloženou verzi.
- Zrušení výběru souboru vrátí uživatele do výchozího kontextu. Návrat z nástroje obnoví stránku, zoom i výběr, pokud to výsledek dovoluje.

### 4.3 Výběr stránek je společná funkce s velkým dopadem

Zavést stejné ovládání rozsahu v OCR, exportu, otáčení, ořezu i číslování: aktuální stránka, vybrané stránky, vše, vlastní rozsah. Vždy zobrazit počet a náhled výsledného výběru.

Vyjasnit fyzický index versus popisek stránky: „Stránka 7 z 24, označení iii“. Rozsahy nesmějí být nejednoznačné po vložení nebo přesunu stránek. Definovat, zda sudé/liché znamená pořadí v souboru. Výběr „viditelné“ nesmí při posunu náhledu nenápadně změnit už připravenou operaci. Pokročilou syntaxi zachovat jako zkratku, ale běžný uživatel ji nemá muset znát.

### 4.4 PageMaster nesmí přijít o projektovou práci

Uložení pracovní plochy, skupiny a kontrolní body jsou v plánu správně zachované. Ještě je potřeba určit, co dělá Ctrl+S v sestavě a jak se liší **Uložit pracovní plochu** od **Vytvořit PDF**. Při opětovném otevření sestavy řešit chybějící nebo změněné zdroje a nabídnout jejich nahrazení. Zachovat kompatibilitu s existujícími `.pagemaster` soubory nebo navrhnout jejich import.

Existenci těchto funkcí potvrzuje [PageMaster MainWindow](../Pdf4QtPageMaster/mainwindow.cpp), zejména `createProjectJson`, `saveWorkspace`, `saveCheckpoint` a `loadCheckpoint`. Jejich pouhé přiřazení ke kartě Manage Pages ještě neurčuje nový pracovní postup.

## 5. Čtení, oprávnění a ukládání potřebují přesnější pravidla

### 5.1 Náhrada Vieweru

Zjednodušený režim čtení dává smysl. Při otevření PDF ze správce souborů bych přednostně ukázal dokument, hledání, navigaci, kopírování a tisk, s viditelnou možností pokračovat k úpravám. Automatické zobrazení katalogu by běžné čtení zdržovalo.

Rozlišit následující situace:

| Situace | Očekávaná reakce |
| --- | --- |
| Uživatel zvolil režim čtení | Jednoduché rozhraní a vědomý přechod k úpravám; žádný zápis bez jeho akce. |
| Uživatel výslovně zamkl dokument proti změnám | Zámek se sám nezruší. Nabídnout odemčení nebo práci s kopií. |
| Soubor nebo složka nejsou zapisovatelné | Vysvětlit nemožnost přepsat zdroj a nabídnout uložení jinam. |
| PDF omezuje danou operaci | Popsat konkrétní omezení; kopie není univerzálním řešením oprávnění. |
| Dokument má podpisy | Ukázat podpisový stav a před změnou její dopad podle konkrétní operace. |

Pro uživatele není podstatné, že Redact nebo Sign uvnitř programu dočasně mění dokument. Pokud chce pracovat s kopií a oprávnění to dovolují, nástroj může založit samostatnou pracovní kopii a zdroj ponechat zamčený. §4.7 by měl popsat tuto cestu místo plošného zákazu podle vnitřní implementace.

Pravidla navíc musí platit pro celou aplikaci, tedy i formuláře, anotace, osnovu, klávesové zkratky a přetažení. Tabulka dostupnosti samotných 45 nástrojů nestačí.

### 5.2 Apply není Save

Jednotné umístění tlačítek je dobré. Jednotné slovo Apply pro všechny operace už méně: **Otočit stránky**, **Vytvořit PDF**, **Exportovat obrázky** nebo **Uložit podepsanou kopii** přesněji předpovídají výsledek.

| Výsledek nástroje | Doporučené chování |
| --- | --- |
| Úprava otevřeného PDF | Potvrzení změní pracovní dokument, zobrazí neuložený stav a nabídne Undo. Uložení souboru zůstává výslovnou akcí. |
| Nový PDF soubor | Před dokončením je jasný název a umístění. Po úspěchu lze pokračovat přímo na výsledku. |
| Více souborů | Náhled názvů a počtu, pravidla kolizí, souhrn úspěšných a neúspěšných výstupů, otevření cílové složky. |
| Pouhý náhled nebo analýza | Zavření nástroje neznačí neuloženou změnu PDF. Export zprávy je samostatná akce. |

Zvlášť definovat Cancel a Esc: ruší dosud nepotvrzené nastavení či běžící úlohu, nikoli dříve potvrzené změny. Undo má pojmenovat krok a patřit aktivnímu dokumentu či sestavě; pokud editor uvnitř nástroje drží vlastní nepotvrzené změny, musí být rozsah Undo zřejmý.

Po Save as copy má být patrné, který soubor je aktivní. Doporučený další krok i odeslání e-mailem musí pracovat s výsledkem. Po redakci nestačí otevřít složku a ponechat nenápadně aktivní původní citlivý dokument.

### 5.3 Dlouhá operace a chyba jsou součástí návrhu

Pro první verzi doporučuji během zpracování zablokovat konfliktní úpravy příslušného dokumentu a nechat ostatní karty použitelné. Alternativou je práce se zachycenou verzí a kontrolou konfliktu při dokončení; výsledek nesmí přepsat novější změny.

Definovat také zavření karty během běhu, ukončení aplikace, nedostatek místa, změnu souboru jinou aplikací a zotavení rozpracované práce po pádu. Automatické obnovení dokumentu z disku nesmí zahodit neuložené změny. Chyba má ponechat nastavení a vstupy pro opakování. U dávky ukázat stav po jednotlivých souborech a umožnit opakovat jen neúspěšné položky.

Při Merge je „přeskočili jsme dva soubory“ zásadní informace. Nepodporované, zamčené a poškozené vstupy mají být vidět před spuštěním; částečný výsledek musí být výslovně označený. Pouhé krátké upozornění nestačí.

## 6. Konkrétní úpravy nástrojů a textů

| Místo v plánu | Doporučení a důvod |
| --- | --- |
| Scan to PDF (§4.7, §5.2) | Nabídnout „Nový dokument“ a „Přidat do otevřeného“. Lokální `ScannerPlugin::onScanTriggered` už obsahuje větev bez `m_document`, která vytvoří nový PDF. Popis pouze připojení na konec proto zužuje doložené chování; nově vytvořený sken nesmí blokovat jiný dokument otevřený pro čtení. Viz [scannerplugin.cpp](../Pdf4QtEditorPlugins/ScannerPlugin/scannerplugin.cpp). |
| Crop Pages (§5.1) | Vysvětlit, že ořez PDF může jen skrýt okraje a není odstraněním citlivých dat. Lokální [pdfdocumentmanipulator.cpp](../Pdf4QtLibCore/sources/pdfdocumentmanipulator.cpp) volá `setPageCropBox`; [pdfdocumentbuilder.cpp](../Pdf4QtLibCore/sources/pdfdocumentbuilder.cpp) zde mění položku CropBox. Pro trvalé odstranění nabídnout redakci. |
| Sanitize (§10.3/8) | Doporučuji název **Remove Hidden Data**, Sanitize ponechat jako synonymum. Kratší odborný název není automaticky srozumitelnější. Předem ukázat, zda se odstraní komentáře, přílohy či OCR vrstva, a nabídnout profily podle účelu. |
| Convert to Grayscale (§5.5) | Při dnešním rozsahu přejmenovat na **Convert Images to Grayscale**. Název má sám sdělit, že barevný text a vektory zůstanou. |
| Sign by Hand (§5.4, §9.4) | Podporuje i text a obrázek; zvážit **Add Signature Appearance** nebo srozumitelněji **Draw or Insert Signature**. U vstupu „Podepsat“ krátce vysvětlit rozdíl vůči podpisu certifikátem. Finální název ověřit s laiky. |
| Sign with Certificate | Bez certifikátu nabídnout jeho výběr/import a návrat do rozpracovaného podpisu. Odborné parametry skrýt do podrobností. Slova jako `qualified signature` v hledání nesmějí působit jako garance výsledku. |
| Verify Signatures mimo katalog | Panel potřebuje viditelný vstup a rozlišitelné stavy: ověřuje se, ověřeno, problém, nelze dokončit ověření. Nestačí automatická kontrola bez srozumitelného výsledku. |
| Redact Content | Oddělit označení míst od dokončení odstranění. Zřetelně zobrazit „Zatím pouze označeno“ a poté úspěšné vytvoření výstupu. Černý obdélník z Annotate nesmí vypadat jako rovnocenný způsob redakce. |
| Edit Content | Běžné vstupy „Upravit text“, „Přidat text“ a „Přidat obrázek“ mohou vést do stejného editoru. U skenu vysvětlit, že OCR přidá rozpoznanou vrstvu, ale samo nezaručuje pohodlnou úpravu původního rozvržení. |
| Compress | Předvolby, náhled čitelnosti a naměřená velikost výsledku. Uvést i případ „Soubor už nelze zmenšit zvoleným nastavením“. Cílovou velikost prezentovat jako cíl, jehož dosažitelnost závisí na obsahu a kvalitě. |
| Flatten a Rasterize PDF (§8.2) | Odstranit příslib, že obsah pak „nejde měnit“ či kopírovat. Popisovat konkrétní transformaci: pole a anotace se převedou do obsahu, případně stránky do obrázků. Uživatel to nesmí chápat jako záruku ochrany. |
| Compare | Vstupy nazvat „Původní“ a „Nová verze“, umožnit prohození, výběr otevřených karet a uchování nastavení. Stav „Bez rozdílů“ odlišit od „Porovnání ještě neproběhlo“ a od chyby; uvést rozsah a aktivní filtry. |
| Interleave Pages | Nabídnout náhled několika prvních výsledných stran, obrácení druhého vstupu a vysvětlení při různém počtu stran. Samotný název je pro laiky málo známý. |
| Document Statistics | Přístup také z Compress jako „Co zabírá místo?“. Úkol může být užitečný běžnému uživateli, i když dnešní přehled je odborný. |

Limit 45 znaků pro popis karty bych změnil na doporučenou stručnost a prostor pro dvě krátké řádky. Překlady a vysvětlení rozdílu mezi podobnými funkcemi nesmějí kvůli limitu ztratit význam. Vývojové stavy GUI, CLI a Branch ponechat v návrhové dokumentaci; uživatel potřebuje znát skutečnou dostupnost a případná omezení konkrétního sestavení.

## 7. Bude mít uživatel vše potřebné?

**Pro běžné čtení, komentování, skládání stránek, základní úpravy, podpis a porovnání je funkční základ široký. Úplnost však závisí na dokončení celých úkolů a zachování dat při přechodu mezi funkcemi.** Mapa akcí v kapitole 6 je nutná, ale neprokazuje například, že po přeskupení stránek zůstanou správné odkazy, pole a osnova.

| Běžný úkol | Co plán nabízí | Co doplnit do návrhu postupu |
| --- | --- | --- |
| Otevřít PDF, najít údaj, zkopírovat ho a vytisknout | Viewerové příkazy, Select, Print | Přímé otevření dokumentu, rychlost čtení, klávesnice a přehledný náhled rozsahu tisku. |
| Vyplnit formulář a poslat jej zpět | Formulářová pole, Edit Content, podpis, e-mail | Rozpoznat rozdíl mezi interaktivním formulářem a skenem, zvýraznit pole, nabídnout text a zaškrtnutí, uložit a odeslat správný výsledek. |
| Sloučit přílohy, otočit stránku a přidat číslování | Merge, Manage Pages, Rotate, Add Page Numbers | Jeden souvislý postup bez exportu a ručního znovuotevírání mezi kroky. |
| Poslat menší čitelný sken | Scan, OCR, Compress | Založení nového PDF, dostupnost jazyků, jednoduchá předvolba, kontrola čitelnosti a velikosti. |
| Odstranit osobní údaje před sdílením | Redact, Sanitize | Rozlišit značky od hotové redakce, vysvětlit rozsah odstranění skrytých dat a pokračovat na vyčištěné kopii. |
| Porovnat dvě smlouvy a předat rozdíly | Compare, zpráva PDF/XML | Výběr správných verzí, smysluplné výchozí filtry a snadný export zprávy. |
| Pokračovat ve složité sestavě z PageMasteru | Manage Pages, pracovní plocha, checkpoint | Kompatibilita projektu, obnova zdrojů, jasný rozdíl mezi uložením sestavy a výsledného PDF. |

### Doporučené pořadí práce

1. **První vydání sjednoceného GUI:** zachovat dnešní funkce a data, dokončit výše uvedené postupy, dohledatelnost, ukládání, chyby a přístupnost. Compress, Extract Pages a jednoduché exporty mají přímý přínos. OCR je hodnotné, ale jeho připravenost je nutné ověřit samostatně.
2. **Další rozšíření podle cílových uživatelů:** vodoznak, záhlaví/zápatí, úprava metadat, přidávání příloh, Flatten a dávky pro často opakované operace. Využít společné ovládání a předvolby, aby nevznikla další sada odlišných dialogů.
3. **Samostatné produktové projekty:** tvorba formulářů, Office převody, PDF/A a rozsáhlé automatizace. Jejich potřeba je reálná pro některé skupiny, ale četnost u konkurence sama neznamená, že mají blokovat sjednocení GUI.

Collections bych zatím ponechal jako uložené skupiny vstupů. Pokud mají později spouštět operace, pojmenovat je odlišně, například Workflows. Automatizace potřebuje vlastní pravidla pořadí, vstupů, chyb a výstupů. Například výsledek Sanitize může podle zvolených voleb odstranit právě vytvořenou OCR vrstvu; řetězec musí takový konflikt ukázat.

Pro uživatele přecházející ze čtyř aplikací doplnit migraci nastavení, zkratek, historie a záložek. Zachovat očekávané otevírání PDF ze systému. U více dokumentů umožnit práci ve více oknech nebo její náhradu ověřit s lidmi, kteří dnes používají dva monitory.

## 8. Přístupnost a klidné výchozí rozhraní

Katalog bych doplnil krátkou úvodní sadou běžných úkolů a připnutými položkami. Kompletní přehled zůstane jedním krokem dostupný. Pořadí hlavních položek se nemá samo přeskupovat podle používání; historii lze ukázat odděleně. Prázdné Pinned a Collections nemají při prvním spuštění zabírat většinu obrazovky. „Často používané“ bych nyní nepřidával jako třetí podobnou sekci.

Rozvržení ověřit na menším notebooku a při 200% měřítku. Postranní panely mají jít skrýt, případně změnit jejich šířku. Jednotné rozvržení nesmí zmenšit stránku natolik, že se obtížně čte nebo upravuje. Globální navigace, náhledy, panel nástroje a podpisy nemohou všechny trvale vyžadovat plnou šířku.

Požadavky pro první návrh:

- Celý základní postup ovladatelný klávesnicí, předvídatelné pořadí Tab a návrat fokusu po zavření panelu. Viditelný fokus se nesmí schovat za panel či oznámení; související princip popisuje [W3C: Focus Not Obscured](https://www.w3.org/WAI/WCAG22/Understanding/focus-not-obscured-minimum.html).
- K tažení stránek alternativy „Přesunout před/za stránku“ a tlačítka posunu, vedle klávesových zkratek. [W3C: Dragging Movements](https://www.w3.org/WAI/WCAG22/Understanding/dragging-movements.html) upozorňuje i na potřebu alternativy ovladatelné ukazatelem bez tažení.
- Popisky pro čtečku obrazovky, oznámení počtu vybraných stránek, postupu a výsledku. Ikony mají mít textový nebo přístupný název.
- Vysvětlení nedostupnosti přístupné i klávesnicí; šedá karta s tooltipem pouze při najetí nestačí. Nabídnout konkrétní další akci, například vybrat soubor nebo znovu vyhledat skener.
- U změn, chyb a rozdílů v Compare používat vedle barvy i text, symbol nebo vzor. Červená barva celé kategorie Protect nesmí být jediným signálem destruktivní akce.
- Kontrast měřit na skutečných komponentách a jejich stavech. Pro významové netextové prvky je relevantní poměr 3:1 vůči sousedním barvám, s výjimkami popsanými v [W3C: Non-text Contrast](https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast.html). Samotný výběr odstínu nic nezaručuje.

WCAG zde používám jako vodítko pro návrh desktopového GUI, nikoli jako tvrzení o certifikaci aplikace. Přístupnost samotného rozhraní je samostatný požadavek; nelze ji odložit spolu s budoucím nástrojem Check Accessibility pro PDF.

## 9. Jak ověřit návrh před implementací

Nestačí třídit názvy nástrojů do kategorií. Doporučuji malý interaktivní prototyp hlavních přechodů a první kolo s 5–8 účastníky z více skupin: příležitostní čtenáři, kancelářští uživatelé a současní uživatelé PageMasteru/Diffu. Jde o hledání problémů, nikoli statistické potvrzení použitelnosti. Samostatně ověřit klávesnici a čtečku obrazovky.

Zadání formulovat jako cíle bez názvu tlačítka. Část úloh začít z prázdné aplikace, část s více otevřenými a jedním neuloženým dokumentem.

| Scénář | Co sledovat |
| --- | --- |
| „Otevřete smlouvu, najděte cenu a vytiskněte příslušnou stránku.“ | Přímý přístup ke čtení, hledání a tisku bez katalogu. |
| „Do tohoto formuláře napište jméno a doplňte podpis.“ Jednou interaktivní PDF, jednou sken. | Zda uživatel najde správný způsob vyplnění a rozumí druhu podpisu. |
| „Z těchto souborů vytvořte jeden dokument, otočte stránku a doplňte čísla.“ | Plynulost přechodů, uchování výběru a jasná identita výsledku. |
| „Z dokumentu otevřeného pro čtení vytvořte menší kopii.“ | Zda rozumí zámku, oprávněním a uložení kopie. |
| „Odstraňte z kopie osobní údaje a připravte ji k odeslání.“ | Záměna ořezu, obdélníku a redakce; dokončení odstranění a výběr správného souboru. |
| „Porovnejte uloženou smlouvu s rozpracovanou novou verzí.“ | Výběr vstupů, orientace původní/nová, interpretace výsledku a filtrů. |
| „Rozdělte soubor do složky, kde už některé výsledné názvy existují.“ | Pochopení kolizí, přepsání a částečného dokončení. |
| „Spusťte OCR, přejděte do jiného PDF a poté úlohu zrušte.“ | Viditelnost průběhu, rozsah zrušení a zachování práce. |
| „Otevřete uloženou sestavu a dokončete ji.“ | Rozdíl pracovní plocha/PDF, chybějící zdroje, obnovení a export. |

Zaznamenat úspěch bez pomoci, první zvolenou cestu, návraty a omyly, čas k nalezení funkce a pochopení výsledku. Na konci se ptát, který soubor se změnil, kde je uložený a co lze vrátit. Zvlášť zaznamenat chyby, které uživatel nepovažuje za chybu, například pouhé překrytí textu místo odstranění.

Navržená podmínka pro pokračování: žádná pozorovaná záměna zdroje a výsledku ani nepochopená nevratná operace; opakované selhání stejného základního úkolu znamená úpravu prototypu a nové ověření. Z malého počtu účastníků nevyvozovat obecnou procentní úspěšnost produktu.

## 10. Co upravit přímo v tools.md

- **§1 a §10.3/1,5:** oddělit definici technického nástroje od uživatelských vstupů; přenést synonyma na příkazy a panely.
- **§2 a §3:** počet kategorií označit za návrh k ověření; dovolit související vstupy při jedné hlavní kategorii; neodvozovat architekturu od počtu barev.
- **§4.7:** nahradit samotnou tabulku mění/nemění dokument modelem dostupnosti podle úkolu, cíle, oprávnění a zámku. Doplnit příkazy, formuláře a práci s kopií.
- **§5:** opravit Scan to PDF a zpřesnit Crop, Grayscale, Sanitize, podpisy a jejich výsledky. U každého nástroje uvést chování bez dokumentu, s výběrem a s neuloženým vstupem.
- **§6.2:** odstranit návrhy zániku funkcí bez náhrady. U zobrazení lišt a Prepare Icon Theme uvést konkrétní nový přístup se zachováním schopností.
- **§6:** k mapě akcí přidat migrační scénáře s ověřením zachování osnovy, odkazů, anotací, formulářů, popisků stránek a relevantních podpisových stavů. Tyto vlastnosti nelze předpokládat jen podle přítomnosti tlačítka.
- **§8:** oddělit rozsah sjednocení od dalšího produktového rozvoje; opravit přísliby neměnitelnosti u Flatten a Rasterize.
- **§9:** doplnit společná pravidla cíle, výběru, potvrzení, ukládání, Undo, chyb, souběhu, přístupnosti a návratu do práce.
- **§10:** větu „Otevřené otázky ke katalogu nezbývají“ nahradit seznamem rozhodnutí k ověření, zejména pro režim čtení, vyplnění formulářů, pracovní karty a dokončení operací.

## 11. Podmínka migrace: žádná ztráta funkcionality

### 11.1 Dvě explicitní ztráty v současném plánu

V §6.2 jsou dvě položky označené jako zanikající. Obě je nutné změnit, aby plán splňoval zadání:

| Současná funkce | Co říká plán | Požadovaná náhrada |
| --- | --- | --- |
| Zobrazení a skrytí nástrojových lišt | „Zaniká, nové UI má pevné rozvržení.“ | Zachovat možnost upravit viditelnost ovládacích skupin/panelů a zapamatovat nastavení. Výchozí rozvržení může být jednoduché, ale nesmí uživateli odebrat odpovídající kontrolu nad pracovní plochou. |
| Prepare Icon Theme | „Vývojářská akce, zaniká.“ | Zachovat přípravu vlastní sady ikon a její použití, například v pokročilém nastavení vzhledu. Označení funkce za vývojářskou neopravňuje k jejímu odstranění. |

Obě funkce jsou doložené v [PageMaster MainWindow](../Pdf4QtPageMaster/mainwindow.cpp): nabídka Toolbars používá `toggleViewAction()`, stav okna se ukládá a obnovuje; operace `PrepareIconTheme` volá `m_iconTheme.prepareTheme()` a aplikace vlastní téma načítá přes `m_iconTheme.loadTheme()`.

Další rizika jsou méně přímá: ztracená synonyma, skenování omezené na otevřený dokument, nedostupné dílčí režimy OCR při exportu nebo funkce ukryté do panelu bez jasného vstupu. U těchto bodů se musí ověřit reálná dosažitelnost úkolu, nikoli jen existence kódu.

### 11.2 Co znamená plnohodnotná náhrada

Není nutné zachovat každou původní nabídku či dialog. Je však nutné zachovat každou jejich schopnost. Náhrada splňuje požadavek, když uživatel:

- dokončí stejný úkol ve sjednoceném GUI bez spouštění staré aplikace nebo příkazové řádky;
- má dostupné dosavadní vstupy, rozsahy, volby, režimy a výstupní formáty;
- dosáhne srovnatelného výsledku bez neohlášené ztráty obsahu, kvality nebo editovatelnosti;
- může pokračovat s dříve uloženými dokumenty, sestavami, profily a osobními daty tam, kde je dnešní aplikace uchovává;
- funkci najde přes srozumitelný vstup a vyhledávání, i když se změnil název nebo umístění.

Například „Compare existuje“ nestačí, pokud chybí překryv, filtry, nastavení rozsahů, PDF zpráva nebo XML export. „Manage Pages existuje“ nestačí bez skupin, checkpointů, obnovy odebraných položek a všech původních způsobů dělení. „Measure existuje“ nestačí bez kalibrace, jednotek, CSV exportu a převodu do anotací.

### 11.3 Doplnit registr funkční shody

Kapitolu 6 rozšířit na ověřitelný seznam. Jeden řádek má odpovídat konkrétní schopnosti, případně samostatnému režimu nebo volbě, ne celému pluginu. Jako výchozí stav uvést konkrétní revizi zdrojů a konfiguraci sestavení; současná tabulka odkazující obecně na master může časem zastarat.

| Pole každého záznamu | Obsah |
| --- | --- |
| Identita a původ | Stabilní identifikátor schopnosti, původní aplikace/plugin a místo v kódu či původním UI. |
| Původní chování | Vstupy, možnosti nastavení, předpoklady, výstupy a uložená data. |
| Nový přístup | Konkrétní karta, panel, kontextová nabídka nebo příkaz; současně způsob dohledání. |
| Ověřovací scénář | Vstupní soubor či projekt a očekávaný výsledek porovnatelný se starou aplikací. |
| Stav | Navrženo → implementováno → ověřeno. „Zaniká“ ani „bude později“ neznamená splnění. |

Při inventuře projít nabídky, kontextové nabídky, lišty, klávesové akce, panely, nastavení, pluginy, chování při otevření souboru a načítání uložených projektů. Zkontrolovat obě podporované platformy a závislosti pluginů. CLI-only funkce a návrhy z kapitoly 8 evidovat zvlášť od existujících schopností čtyř GUI aplikací; tato evidence má odlišit nové rozšíření od povinné náhrady, nikoli ospravedlnit odebrání stávající funkce.

### 11.4 Podmínka nahrazení původních aplikací

**Původní čtyři aplikace lze plně nahradit až tehdy, když má každá jejich současná schopnost v registru stav „ověřeno“.** Do té doby lze vydat náhled sjednoceného GUI souběžně se starými aplikacemi. Zachování staré aplikace je přechodové opatření, nikoli konečná náhrada chybějící funkce v nové aplikaci.

Ověření musí zahrnovat dostupnost v GUI i správnost výsledku. U transformací PDF přidat smysluplné regresní kontroly dat; pro dohledatelnost a práci v panelech použít průchod scénářem. Build a úspěšné jednotkové testy samy neprokazují funkční shodu rozhraní.

Tato revize identifikuje konkrétní rizika a stanovuje způsob kontroly. Netvrdí, že kompletní inventura a funkční shoda již byly ověřeny.

Za největší přínos této revize považuji doplnění uvedených pravidel před rozšířením katalogu. Teprve propojené, srozumitelné a bezpečně dokončitelné postupy naplní cíl jedné uživatelsky přívětivé aplikace.

Ověření této změny: jde pouze o nový dokument revize; zdrojový kód ani původní plán se nemění. Build a jednotkové testy se pro tuto dokumentační změnu nespouštějí. Soubor má být uložen jako UTF-8 s konci řádků CRLF.
