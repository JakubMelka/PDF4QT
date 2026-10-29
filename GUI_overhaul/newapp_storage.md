# Nová aplikace – úložiště (SQLite, WAL)

Zpět na [newapplication.md](newapplication.md).

## 1. Zásady

- **Jedna databáze na uživatele a verzi schématu**, sdílená všemi spuštěnými instancemi téže verze: `QStandardPaths::AppLocalDataLocation/pdf4qt-<schéma>.db` (lokální disk, ne roaming; WAL na síťovém disku nefunguje). Název nese verzi schématu, ne verzi aplikace (rozhodnutí vlastníka O19, podrobně v 5): více verzí PDF4QT může běžet současně, každá se svým souborem, starší soubor je zároveň zálohou. Vývojový soubor `pdf4qt.db` (vývojová schémata 1–4 před vydáním, sloučená do verze 1) se ignoruje a nikdy nemění.
- **Žádné QSettings** v nové aplikaci ani v knihovnách, které používá.
- **Přenosný režim** (argument `--portable`): databáze jen v paměti, na disk se nezapíše nic (nastavení, historie, náhledy). Všechna připojení procesu (každé vlákno má své) sdílí jednu databázi přes VFS `memdb` (URI `file:/pdf4qt?vfs=memdb`, SQLite ≥ 3.36; ne zastaralý shared cache); úložiště drží jedno připojení otevřené po celou dobu běhu, jinak by databáze zanikla. WAL v paměti není, transakce, triggery a oznámení změn fungují stejně (jen v rámci procesu); jiné instance ji nevidí a přenosná instance se neúčastní předání souborů ([newapplication.md](newapplication.md) rozhodnutí 11). Schéma vznikne migracemi od nuly, takže chování je stejné jako s prázdnou databází na disku. Každá stránka karty Home to ukáže pruhem nahoře a patička štítkem *Portable* u verze (`pdf4qt-settings-general-portable` 941:138200, Recent 955:150880). Token MCP je jen v paměti a při každém startu nový. Stejný mechanismus je náhradou, když databázi na disku nejde otevřít (5).
- **Žádná cache.** Stránka čte data z databáze, když se zobrazí a když přijde oznámení o změně. Nový nedávný soubor s náhledem z jedné instance se tak do zlomku sekundy objeví i v druhé. Nastavení, která používá engine (vykreslování, CMS, řeč), se při změně znovu přečtou a předají engine – to je nastavení enginu, ne kopie dat.
- Přímo C API `sqlite3` z vcpkg, ne Qt SQL: potřebujeme `BEGIN IMMEDIATE`, `busy_timeout`, `PRAGMA data_version` a řízení checkpointu.

## 2. Připojení a transakce

| Pravidlo | Proč |
| --- | --- |
| `journal_mode=WAL` (ověřit návratovou hodnotu), `synchronous=NORMAL`, `foreign_keys=ON`, `busy_timeout=5000`, `application_id`, `temp_store=MEMORY` | souběžné čtení s jedním zapisujícím, odolnost vůči pádu |
| Jedno připojení na vlákno (hlavní vlákno, vlákno náhledů, každé vlákno MCP, které čte) – nikdy sdílené | sqlite3 handle a příkazy nejsou určené pro více vláken |
| Zápis = krátká `BEGIN IMMEDIATE` transakce, nikdy přes čekání na uživatele, dialog ani přechod mezi vlákny | `IMMEDIATE` brání zablokování při povýšení čtení na zápis |
| Čtení = jeden příkaz, nebo krátká čtecí transakce pro konzistentní snímek; příkaz se po přečtení hned resetuje | otevřený čtecí příkaz brání checkpointu a WAL roste |
| `SQLITE_BUSY` po timeoutu: zápis se jednou zopakuje, pak chyba do UI (bez pádu, data nastavení zůstanou výchozí) | jiná instance drží zápis |
| `PRAGMA optimize` a `wal_checkpoint(PASSIVE)` při zavření | velikost WAL |

Třídy (`Pdf4QtLibApp/storage`, bez Widgets): `PDFDatabase` (připojení, otevření, migrace), `PDFDatabaseStatement` (RAII, vazba parametrů podle typu), `PDFDatabaseTransaction` (RAII, commit výslovně, jinak rollback), repozitáře po oblastech (`PDFSettingsRepository`, `PDFRecentRepository`, `PDFToolRepository`, `PDFMcpRepository` …) – vrací hodnoty, nic nedrží.

## 3. Oznámení změn mezi instancemi

1. Každá tabulka má triggery `AFTER INSERT/UPDATE/DELETE`, které zapíší řádek do `change_log(seq INTEGER PRIMARY KEY, topic TEXT, key TEXT, time INTEGER)` ve stejné transakci. Žádný zápis tak nezapomene oznámit změnu.
2. `PDFDatabaseNotifier` v hlavním vlákně každých 250 ms přečte `PRAGMA data_version` (levné, bez I/O při beze změny). Když se změnilo, načte řádky `change_log` se `seq` větším než poslední zpracované a vyšle `changed(topic, key)`.
3. Vlastní zápisy `data_version` nemění, proto repozitář po commitu oznámí změnu sám.
4. Mezera v `seq` (instance spala, log se mezitím pročistil) = oznámení „změnilo se vše“.
5. Pročištění logu (řádky starší 10 minut, nejvýš 10 000) při startu a jednou za hodinu.

## 4. Schéma fáze 1

| Tabulka | Obsah | Figma |
| --- | --- | --- |
| `setting(key PK, value, modified)` | hodnoty jen změněných nastavení; chybějící klíč = výchozí hodnota z kódu, *Restore defaults* sekce = smazání jejích klíčů | Settings, AI |
| `recent_file(id, path UNIQUE, name, folder, last_opened, open_count, pinned, pin_order, page_count, file_size, file_mtime, last_page, reached_end, view_state, locked, thumbnail BLOB, thumbnail_mtime)` | historie souborů; stav čtení „Page 12 of 24“ z `last_page`, *Finished* = poslední stránka byla zobrazená (`reached_end`), *Not started* = nikdy dál než stránka 1; `view_state` = stav panelu View pro tento dokument (rozvržení, barevný režim, zobrazení anotací, první stránka vpravo – výchozí hodnoty dává Settings › Viewing); `locked` = zámek dokumentu platí i po dalším otevření; náhled PNG do 320 px; počet nepřipnutých řádků podle *Recent files* (výchozí 10), připnuté se nemažou; chybějící soubor se zjišťuje při zobrazení | 28:104, 61:1201, 80:910 |
| `recent_folder(path PK, last_used, kind)`, `pinned_location(id, path, name, kind, position)` | složky (standardní složky systému první, pak nejvýš 20 naposledy použitých; počet PDF se počítá při zobrazení) a připnutá umístění | 67:544, 68:711 |
| `workspace(id, name, created, opened, saved, is_last_session)`, `workspace_document(workspace_id, position, path, page)` | pracovní prostory jen v databázi (žádné soubory na disku) a *Restore last session*; bez limitu (spravuje je uživatel); *Clear history* na Recent maže historii otevření, *Continue where you left off* a pracovní prostor posledního sezení (`is_last_session`), připnuté jen s volbou *Also remove the pinned items*; uložené pracovní prostory zůstávají | 68:634, 941:135742, 941:136915 |
| `tool_pin`, `tool_usage(tool_id, last_used, count)`, `tool_collection(id, builtin_id, name, icon, modified)`, `tool_collection_item`, `tool_pair(from_tool, to_tool, count, last_used)`, `tool_parameter(tool_id, name, value)` | katalog toolů a příprava na fázi 2 (počitadla podle [tools.md](tools.md) 9.3 bod 12; tajné parametry se neukládají). „runs recorded“ v Settings je samostatné počitadlo `tools.historyRuns`; *Clear history* smaže `tool_pair` a počitadlo, připnuté tooly, kolekce a naposledy použité zůstávají | 272:12821, 283:1888, 941:136741 |
| `shortcut(command_id PK, keys)` | změněné zkratky | 147:4184 |
| `annotation_style(style_id PK, value)` | styly nástrojů anotací (dnes QSettings v `pdfannotationstyle.cpp`) | 877:9598 |
| `bookmark(id, file_path, page, title, auto, position)` | *My Bookmarks* podle kanonické cesty souboru; *Generate automatically* je nastavení `bookmarks.autoGenerate` | 195:3445, 196:3437 |
| `trusted_certificate(id, data BLOB, added)` | certifikáty uživatele (dnes `TrustedCertStorage.bin`); certifikáty systému se čtou ze systému, nejdou odebrat; přidání je ve fázi 2 (*Manage Certificates*) | 147:3457 |
| `mcp_server(id=1, instance, pid, port, state, address, heartbeat)`, `mcp_client(session, name, address, connected)`, `mcp_activity(id, time, client, action, document, detail, status)`, `mcp_request(id, kind, session, created)` | stav serveru a asistentů pro všechny instance, *Recent activity* (500 záznamů / 30 dní), požadavky jiných instancí na vlastníka serveru (*Disconnect*, *Clear*, *Show Other Window*); heartbeat starší 10 s = server zanikl ([newapp_mcp.md](newapp_mcp.md) 4) | 653:5231, 940:135321 |

Hodnoty nastavení jsou typované v kódu: `PDFSettingDefinition` (klíč `section.name`, typ, výchozí hodnota, rozsah nebo volby, sekce a kotva pro hledání). Definice je jediný zdroj pro stránku Settings, globální hledání, validaci (hodnota mimo rozsah z jiné verze = výchozí) i *Restore defaults*.

- Volby, které závisí na systému (hlasy, jazyky řeči, ICC profily, jazyky rozhraní), dává poskytovatel voleb u definice. Uložená hodnota, která v systému chybí, se ukáže jako „(not available)“ a engine použije výchozí.
- Ovládací prvky panelů dokumentu, které mění stejnou hodnotu jako Settings, zapisují stejný klíč: přepínač tématu, panel Speech (= Settings › Speech). Panel View mění jen `view_state` dokumentu.
- *Restore defaults* sekce smaže klíče sekce; u Shortcuts řádky `shortcut`; Security certifikáty nemaže (dialog 941:135918 vypíše jen změněné hodnoty).
- Další klíče: `update.autoCheck`, `update.lastCheck`, `update.latestVersion`, `update.lastError` (kontrola aktualizací, [newapp_whatsnew.md](newapp_whatsnew.md) 5), `mcp.*` (výchozí v [newapp_mcp.md](newapp_mcp.md) 1), stavový klíč `window.geometry` (normální geometrie, maximalizace a obrazovka naposledy zavřeného hlavního okna, `PDFAppWindowGeometry`; první okno procesu ji obnoví, další okno se posune od aktivního; při ukončení aplikace s více okny vyhraje aktivní okno). Záloha nastavení (Settings › Storage › *Export…*) nevkládá token MCP ani `window.geometry` (monitory jednoho počítače, rozhodnutí vlastníka 29. 10. 2026) a import ponechá současné hodnoty (`PDFDatabaseBackup::getPrivateSettingKeys`). Logy jsou soubory v `AppLocalDataLocation/logs`, na Linuxu `StateLocation/logs` (About ukazuje cestu), katalog videí na Support je statická tabulka v kódu, zatím maketa (filtr *For you* = videa toolů, které uživatel spouštěl, podle `tool_usage`; [newapplication.md](newapplication.md), poznámka ke kroku 2).

## 5. Verze a chyby

- Verze schématu v `PRAGMA user_version` a zároveň v názvu souboru `pdf4qt-<schéma>.db` (O19). Soubory databáze jsou jen názvy `pdf4qt-<n>.db` s `n` od 1 bez úvodních nul (nejvýš 9 číslic); `pdf4qt.db`, `*.damaged-<datum>`, dočasné soubory upgradu a soubory SQLite (`-wal`, `-shm`, `-journal`) nejsou. Vývojové verze 1–4 jsou před vydáním sloučené do jediné verze 1 (stejné tabulky, indexy, triggery, `application_id`).
- **Otevření** (`PDFDatabaseOptions::directory`, `PDFDatabaseFiles`):
  1. Existuje soubor verze schématu aplikace → otevře se (WAL, kontroly níže).
  2. Neexistuje → vezme se nejnovější **starší** soubor. Pod zámkem upgradů (`pdf4qt-upgrade.lock`: malý soubor SQLite, jehož transakci `BEGIN EXCLUSIVE` drží jen jeden proces; zámek uvolní i operační systém, když proces spadne; soubor se nikdy nemaže) se nejdřív smažou dočasné soubory upgradů (`pdf4qt-<n>.db.upgrade-<pid>-<náhodné>`, ty existují jen u držitele zámku, takže nalezený zůstal po pádu) a znovu se ověří, že cílový soubor mezitím nevytvořila jiná instance (ta čekající ho pak jen otevře). Starší soubor se otevře jen pro čtení bez checkpointu při zavření a zkopíruje se zálohovacím API SQLite (jedna čtecí transakce = konzistentní snímek i za běhu starší verze, včetně řádků v jejím `-wal`) do dočasného souboru v téže složce, kopie se přepne na rollback journal, zmigruje, ověří (`quick_check`, `user_version`, `application_id`), zapíše se čas kontroly integrity a soubor se atomicky přejmenuje na `pdf4qt-<schéma>.db`. Cílový soubor tak vzniká vždy celý; starší soubor se nikdy nezapisuje. Bez staršího souboru vznikne stejnou cestou prázdná databáze.
  3. Jen **novější** soubory (starší verze spuštěná po novější) → obsah nejnovějšího z nich se zkopíruje do databáze v paměti (`sqlite3_backup`); změny platí do zavření aplikace, na disk se nic nezapíše (ani zámek), upozorní na to pruh *Read only*.
  4. Starší soubor nejde zkopírovat ani zmigrovat (poškozený, cizí `application_id`, verze v souboru neodpovídá názvu, migrace na jeho datech selže) → zůstane beze změny pod svým jménem, nový soubor vznikne prázdný a pruh *Replaced* ukáže jeho název (`PDFDatabaseState::damagedFilePath`). Starší soubory se dál nezkouší – data předposlední verze by byla nečekaná.
  5. Chyba prostředí (soubor nebo zámek nejde vytvořit, složka jen pro čtení, plný disk) → databáze v paměti s obsahem staršího souboru zmigrovaným v paměti, pruh s *Try Again* (zkusí upgrade znovu). `SQLITE_BUSY` se čeká jako u zamčené databáze.
- Staré soubory se automaticky nemažou. `PDFDatabaseFiles::getFiles` / `PDFDatabase::getDatabaseFiles` vrací seznam souborů (verze, velikost s `-wal`, čas změny, který je aktuální) pro přehled a mazání v *Settings › Storage* (krok 5).
- **Pravidla migrací** (O19, platí od vydání):
  - vydaná migrace se nikdy nemění; změna schématu = nová migrace = nová verze schématu, která tím dostane i vlastní soubor;
  - sloupce se nepřejmenovávají ani nemažou (starší verze, která čte kopii novějšího souboru, je musí najít); nový sloupec má výchozí hodnotu (`NOT NULL DEFAULT …` nebo povolené `NULL`), aby zápisy starší verze bez něj fungovaly;
  - neznámé hodnoty výčtů a nastavení se čtou jako výchozí (hodnota z novější verze, `PDFSettingDefinition`);
  - SQL vždy jmenuje sloupce: žádné `SELECT *`, žádné `INSERT` bez seznamu sloupců;
  - nekompatibilní změna (jiný význam sloupce, jiný formát hodnoty) = nová tabulka nebo nový sloupec v nové verzi schématu, nikdy změna starého.
- Migrace v jednom souboru (`PDFDatabaseOptions::filePath`, nástroje a testy) jsou jen dopředné a běží v `BEGIN EXCLUSIVE`, takže je spustí jen jedna instance; testy simulují novější aplikaci delším seznamem migrací (verze 2, 3, …). Novější schéma v souboru než zná aplikace se jako v bodě 3 zkopíruje do paměti.
- Kontrola při otevření: vždy levná kontrola hlavičky (`application_id`) a schématu; plný `quick_check` jen jednou za 7 dní nebo poté, co některá instance narazila na `SQLITE_CORRUPT` / `SQLITE_NOTADB` (záznam v tabulce `storage_maintenance` bez triggerů, mimo `setting`). Pročištění `change_log` čeká na zámek nejvýš 0 s a jinak se zkusí znovu za minutu, takže nikdy neblokuje GUI.
- Databáze zamčená jinou instancí při otevření (migrace): čeká se, dokud se neuvolní – bez přechodu do paměti (rozhodnutí vlastníka 19. 10. 2026, scénář reálně nenastane).
- Nečitelný nebo poškozený soubor aktuální verze (`quick_check` selže): soubor se přejmenuje na `pdf4qt-<schéma>.db.damaged-<datum>`, vznikne nový, uživatel dostane zprávu. Databázi nejde otevřít vůbec (práva, plný disk): aplikace běží s databází v paměti, upozorní na to a nabídne *Try Again* (znovu otevřít databázi na disku; změny z mezidobí se ztratí). Pruhy pro tyto případy a pro novější schéma jsou v `pdf4qt-settings-storage-banners` 958:28036 a ukazují se na každé stránce karty Home.
- Token MCP je v `setting` jako ostatní data uživatele; soubor databáze má práva jen pro vlastníka (Linux `0600`).

## 6. Testy (`UnitTestsAppStorage`)

Dva a čtyři procesy (test spouští sám sebe přes `QProcess`) současně zapisují tisíce transakcí a ověřují, že nic nechybí. Oznámení změny z cizího procesu přijde do 1 s. Dále se testuje: zámek zápisu jiným procesem (busy), migrace z každé starší verze, poškozený soubor, databáze jen pro čtení, novější schéma, checkpoint při otevřeném čtení, triggery všech tabulek a valgrind. Soubor podle verze schématu (O19): nový start vytvoří `pdf4qt-1.db`, upgrade z `pdf4qt-1.db` do `pdf4qt-2.db` se všemi daty (i z `-wal` běžící a spadlé starší verze), starší soubor bajtově beze změny a starší instance ho dál otevírá a zapisuje, výběr nejnovějšího staršího souboru, ignorované chybné názvy a `pdf4qt.db`, jen novější soubor (paměť, pruh), 2 a 4 procesy současně = jeden upgrade, pád po kopii (dočasný soubor smazán, upgrade znovu), nepoužitelný starší soubor, chybná migrace, složka / zámek nejde vytvořit, přenosný režim.
