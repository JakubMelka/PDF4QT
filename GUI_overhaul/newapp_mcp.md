# Nová aplikace – MCP server

Zpět na [newapplication.md](newapplication.md). Figma (*Main page*): `pdf4qt-ai` 653:5231 (běží), `pdf4qt-ai-stopped` 653:5814, `pdf4qt-ai-remote` 653:6384, `pdf4qt-ai-other-instance` 940:135321, `pdf4qt-ai-port-in-use` 955:148587, `pdf4qt-ai-setup-other-client` 955:149138, `pdf4qt-ai-new-token` 940:135813, `pdf4qt-ai-confirm-open` 940:136305, poznámky a přepínače stavů 955:150808, banner čekajícího dotazu na Home `pdf4qt-home-ai-waiting` 967:12672; dotazy v dokumentu (*View Document*): změna 940:133693, čtení `pdf4qt-document-ai-confirm-read` 966:150610, uložení 940:134248, uložení kopie 958:27539, podepsaný dokument 958:27048, fronta 955:153841, jiný tab 955:154475, náhled 955:151877, po změně 940:33875, poznámka k pravidlům 958:28030.

## 1. Co je dané Figmou

| Položka | Hodnota |
| --- | --- |
| Transport | Model Context Protocol, **Streamable HTTP**, jeden endpoint `/mcp`, jméno serveru `pdf4qt` |
| Adresa | port 8765 (1024–65535); *This computer only* = `127.0.0.1`, *Allow remote connections* = všechna rozhraní. Ve vzdáleném režimu stránka ukáže (a *Copy address* i příkazy použijí) první IPv4 adresu aktivního rozhraní s výchozí bránou; seznam adres rozhraní pro kontrolu `Host` se obnoví při změně sítě (`QNetworkInformation`). Obsazený port: server zkusí na portu požadavek `GET /mcp` – odpověď s hlavičkou `Server: PDF4QT/<verze>` znamená jiné okno PDF4QT (to platí i pro přenosnou instanci, která databázi jiných instancí nevidí; *Show Other Window* tam chybí), jinak jde o jiný program (`pdf4qt-ai-port-in-use` 955:148587). *Copy Address* funguje i u zastaveného serveru (adresa je známá). |
| Přístup | `Authorization: Bearer <token>`. *Require access token* jde vypnout jen v místním režimu (pak požadavek bez hlavičky projde, špatný token je stále `401`, příkaz nastavení je bez `--header`); ve vzdáleném režimu je vždy zapnutý a zamčený. Nový token se potvrzuje dialogem a odpojí všechny asistenty. Výchozí: token vyžadován, jen tento počítač, port 8765. V přenosném režimu je token jen v paměti a při každém startu nový. |
| Oprávnění (platí pro všechny asistenty) | Tři stavy **Allow / Ask / Deny** (rozhodnutí vlastníka 18. 10. 2026) pro *Read documents*, *Edit documents*, *Open and save files*; výchozí Allow, Ask, Ask. *Deny* = akce se asistentovi nenabídne; *Ask* = každý takový požadavek se potvrzuje dialogem; *Allow* = bez dotazu. *Ask* u *Read documents* = jeden dotaz za relaci asistenta (první čtení), platí pro všechny otevřené dokumenty do odpojení (`pdf4qt-document-ai-confirm-read` 966:150610). Přepsání původního souboru se potvrzuje vždy. |
| Ovládání | *Start / Stop server*, *Start with PDF4QT* (výchozí vypnuto). Změna skupiny *Connection* (port, síť, token) restartuje běžící server, u zastaveného platí při dalším startu; změna oprávnění platí hned bez restartu (`list_changed`). |
| Přehled | připojení asistenti (jméno, adresa, čas; *Disconnect* u každého a *Disconnect all* ukončí relace a běžící volání klienta – se stejným tokenem se může připojit znovu, trvale zablokovat = nový token), *Recent activity* (klient, akce, dokument, stav; *Clear*) |
| Nastavení klienta | dlaždice asistentů; Claude Code: `claude mcp add --transport http pdf4qt http://127.0.0.1:8765/mcp --header "Authorization: Bearer …"`; *Other client* (955:149138): adresa, hlavička a JSON pro klienty nastavované souborem. Codex CLI, Gemini CLI, GitHub Copilot (VS Code `mcp.json`) a Cursor (`mcp.json`) mají vlastní text podle stejného vzoru (HTTP + hlavička); Claude Desktop přes most `mcp-remote` (Node.js), protože jeho konfigurační soubor podporuje jen stdio servery. Texty jsou v resources aplikace a test kontroluje, že obsahují aktuální adresu a token. |

## 2. Protokol

- Revize MCP **2025-11-25** a **2025-06-18**. Klient v `initialize` navrhne verzi, server vrátí nejvyšší podporovanou. Před implementací se ověří, zda mezitím nevyšla novější revize. Starší revize s dávkami JSON-RPC se nepodporuje.
- **POST `/mcp`** = jedna zpráva JSON-RPC.
  - Notifikace a odpověď klienta → `202 Accepted`.
  - Požadavek → `application/json`. Pokud požadavek nese `_meta.progressToken`, odpovídá se `text/event-stream`: průběh, pak odpověď, pak konec proudu.
- **GET `/mcp`** = SSE proud pro zprávy serveru (`notifications/resources/updated`, `…/list_changed`, `notifications/message`). Jeden na relaci, každých 15 s komentář proti uzavření.
- **DELETE `/mcp`** ukončí relaci.
- **Hlavičky:**
  - `Mcp-Session-Id` – 128 bitů z `QRandomGenerator::system()`; vzniká v odpovědi na `initialize`.
  - Neznámá nebo prošlá relace → `404`.
  - `MCP-Protocol-Version` musí odpovídat dohodnuté verzi, jinak `400`.
  - Kontroluje se i `Accept`.
- **Schopnosti serveru:** `tools{listChanged}`, `resources{subscribe, listChanged}`, `prompts{listChanged}`, `logging`, `completions`. Pole `instructions` v odpovědi na `initialize`: krátký návod – id dokumentů, stránky od 1, souřadnice, kde je dokumentace.
- **Další mechanismy:**
  - `ping`, `notifications/cancelled` (zruší běh), `notifications/progress`;
  - stránkování seznamů kurzorem;
  - `completion/complete` doplňuje `documentId` a `page`.
- **Chyby:**
  - Chyba protokolu (neznámá metoda, neplatné parametry) = chyba JSON-RPC.
  - Chyba provedení toolu (dokument zamčený, oprávnění, nenalezeno) = výsledek s `isError: true` a srozumitelným textem pro model.

## 3. Vlákna

Vlákno na spojení, požadavek se předá hlavnímu vláknu, výsledek jde zpět. „Klient“ je TCP spojení: jedna relace MCP může mít několik spojení najednou (proud GET a souběžné POSTy).

| Vlákno | Objekt | Úloha |
| --- | --- | --- |
| naslouchací | `PDFMcpListener : QTcpServer` | `incomingConnection(qintptr)` předá deskriptor novému vláknu spojení; nad limitem (32 spojení) odpoví `503` a zavře |
| spojení (1 na TCP spojení) | `PDFMcpConnection` ve vlastním `QThread` s event loopem | HTTP/1.1 s keep-alive, jen `Content-Length` (bez chunked požadavků → `411`), limity: hlavička 16 KiB, tělo 8 MiB, hlavička do 10 s, nečinnost 60 s; ověření hostitele, Origin a tokenu; JSON-RPC; odpověď nebo SSE |
| hlavní (GUI) | `PDFAppMcpHost` (implementace `PDFMcpHost`) | ověří oprávnění a stav (zamčený, zaneprázdněný dokument, modální dialog), najde dokument; **rychlé operace provede sám**, delší předá workeru se separátní kopií dokumentu; u změn provede **commit** |
| worker (`QThreadPool` MCP, nejvýš polovina jader) | `PDFMcpWork` | delší práce nad **separátní kopií původního dokumentu**, kterou smí upravovat: hledání v celém dokumentu (`PDFDocumentTextIndex`), text více stránek, vykreslení více stránek nebo nad 300 DPI (`PDFRasterizerPool`), stavba změněného dokumentu (`PDFDocumentModifier`) |

### Rychlé operace v hlavním vlákně

Bez kopie dokumentu, přímo v hlavním vlákně: stav aplikace a dokumentu, informace o dokumentu, osnova, seznam anotací, **text jedné stránky** a **vykreslení jedné stránky** do 300 DPI. Kritérium je jedna stránka a krátký čas. Operace, která by vlákno GUI zdržela znatelně (víc stránek, celý dokument, vysoké DPI), jde do workeru. Aplikace měří dobu operací v hlavním vlákně a v testech hlídá, že jedna nepřesáhne 100 ms na referenčním dokumentu.

### Separátní kopie dokumentu

Worker dostane vlastní kopii původního dokumentu (dokument tabu v okamžiku požadavku, sdílená neměnná data + vlastní `PDFDocumentBuilder` pro úpravy). Kopii smí upravovat; dokument v tabu se nemění, dokud hlavní vlákno nepřijme výsledek. Během výpočtu uživatel pracuje dál; během dotazu na potvrzení je zakrytý jen tab dotčeného dokumentu, ostatní taby jsou volné.

### Průběh volání

1. Spojení vytvoří `PDFMcpCall` (`std::shared_ptr`, stav chráněný mutexem, příznak zrušení = `PDFOperationControl`) a předá ho přes `QMetaObject::invokeMethod(host, &PDFAppMcpHost::handleCall, Qt::QueuedConnection, call)`.
2. Hlavní vlákno buď operaci provede hned (rychlé operace), nebo vytvoří separátní kopii dokumentu a předá ji s úlohou workeru.
3. Worker spočítá výsledek. Změněná kopie jde zpět do hlavního vlákna. Pořadí: **výsledek workeru → dotaz se shrnutím změn (je-li oprávnění *Ask*) → kontrola, že se dokument v tabu nezměnil → commit**:
   - když se dokument mezitím změnil (uživatel ho upravil), commit se odmítne s chybou „document changed, retry“ (aktivita *failed*);
   - jinak jde změna přes undo/redo jako jeden krok s popisem podle klienta a obsahu („Claude Code added 3 highlights and 1 note“); anotace vytvořené asistentem mají autora = jméno klienta.
4. Hotové volání se předá zpět vláknu spojení frontou na objekt spojení. Spojení, které mezitím zaniklo, výsledek zahodí; odpojení klienta volání zruší.
5. **Nikdo neblokuje:** hlavní vlákno nikdy nečeká na síť ani na worker, vlákno spojení nečeká blokujícím voláním (čeká ve svém event loopu, takže vidí odpojení a `notifications/cancelled`), žádný zámek nepřetrvá přechod mezi vlákny.
6. **Modální dialog v aplikaci:** vnořený event loop by zpracoval i požadavky MCP. Čtení pracuje dál (první čtení s *Ask* počká na zavření dialogu), **zápisové změny se odmítnou** („application is busy, a dialog is open“). Požadavek, který **vytváří dokument** (`open_document`, ve fázi 2 tool s novým dokumentem), počká, až se dialog zavře, a pak se zobrazí dotaz uživateli.
7. **Potvrzení** (oprávnění *Ask*; přepsání původního souboru vždy; nový dokument po zavření modálního dialogu vždy): hlavní vlákno ukáže neblokující dotaz (žádné `exec()`) nad tabem dotčeného dokumentu, u otevření souboru nad aktivním tabem. Volání čeká ve stavu *waiting*. Dotaz ukazuje klienta (identita, místní nebo vzdálený s adresou, čas), dokument, co se změní (*Show on Page* ukáže navržené anotace na stránce) a proč se ptá: jeden dotaz na požadavek, řádek důvodu vyjmenuje všechna oprávnění nastavená na *Ask* („Asked because Edit documents and Open and save files are set to Ask“), jinak „Overwriting the original file is always confirmed“ nebo „Asked because a dialog was open when the request came“. U podepsaného dokumentu varuje před dopadem na podpisy; uložení podepsaného dokumentu asistentem ukáže jen tento dotaz s řádkem o podpisech (ne dialog 955:146677). Fronta je na tab („1 of 3“), každý tab ukáže svůj nejstarší požadavek.
   - **Bez výchozího tlačítka:** Enter nic neudělá, Esc = *Deny*, tlačítka ignorují vstup 1 s po zobrazení (požadavek přijde asynchronně, uživatel může zrovna psát).
   - Požadavek pro dokument v neaktivním tabu ohlásí položka stavového řádku ostatních tabů dokumentů („Claude Code is waiting for your confirmation · Invoice.pdf“, klik = přepne tab); na tabu Home a *New tab* je to banner nahoře na stránce s *Show* (`pdf4qt-home-ai-waiting` 967:12672).
   - Bez odpovědi do 2 minut, po odpojení klienta nebo po zavření tabu se požadavek zamítne (aktivita `denied_timeout` / `denied_closed`); zamčený nebo zaneprázdněný dokument (běží tool) se zamítne hned bez dotazu (`denied_locked`). Texty aktivity: „Denied automatically · no answer in 2 min“, „Denied · the tab was closed“, „Denied · the assistant disconnected“, „Denied · the document is locked“, „Denied · a tool is running on the document“, „Denied by you“, „Allowed by you“, „Failed · <důvod>“.
   - Po povolené změně ukáže tab lištu oznámení s *Undo* a *Show Changes* (Figma 940:33875).
8. **Zastavení a restart serveru, ukončení aplikace:**
   1. zavřít naslouchání;
   2. zrušit volání a dotazy;
   3. ukončit SSE proudy a spojení;
   4. `quit` a `wait` všech vláken.

   Teprve pak se smí zničit hostitel a dokumenty. Hostitel je v cizích vláknech jen za `QPointer` a frontou.

Třídy `PDFMcpServer`, `PDFMcpListener`, `PDFMcpConnection`, `PDFMcpHttpParser`, `PDFMcpSessionRegistry` (mutex), `PDFMcpCall`, `PDFMcpHost` a `PDFMcpToolDefinition` / `PDFMcpResourceDefinition` jsou v `Pdf4QtLibMcp` bez Widgets. Vlastní parser HTTP je malý (jen `/mcp`, tři metody) a testuje se fuzzingem.

## 4. Bezpečnost

| Hrozba | Opatření |
| --- | --- |
| Webová stránka v prohlížeči (DNS rebinding, CSRF) | Hlavička `Host` musí být `127.0.0.1:port`, `localhost:port` nebo `[::1]:port` (ve vzdáleném režimu i adresy rozhraní). `Origin`, pokud je, musí být loopback, jinak `403`. Bez CORS. |
| Cizí klient | token 256 bitů (base64url), porovnání v konstantním čase; bez tokenu (je-li vyžadován) nebo se špatným `401`. Ve vzdáleném režimu je token povinný (přepínač zamčený). |
| Odposlech ve vzdáleném režimu | Fáze 1 jen HTTP; varování na stránce AI říká, že provoz není šifrovaný, a karta *Your documents stay with you* se ve vzdáleném režimu skryje. TLS (`QSslServer`) je možné rozšíření. |
| Oprávnění | Každá akce toolu deklaruje potřebné oprávnění (čtení, úpravy, soubory). *Deny* odebere akci z výčtu ve schématu (tool bez povolené akce zmizí z `tools/list`, + `list_changed`); *Ask* akci nechá a vyžádá potvrzení; kontroluje se i při volání. |
| Soubory | *Open and save files*: jen absolutní cesty; přepsání originálu jen výslovným parametrem a vždy s potvrzením; zápis přes stejné ukládání jako v GUI (bezpečný zápis, inkrementální u podepsaných dokumentů). |
| Zámek dokumentu | Zamčený dokument MCP nezmění, stejně jako uživatel ([tools.md](tools.md) 4.7). |
| Zahlcení | Limity spojení, velikosti těla a odpovědi (16 MiB). Vykreslení do 600 DPI a 50 MPx. Text po stránkách, nejvýš 50 stránek na volání. Hledání vrací nejvýš 500 výsledků. Relace vyprší po 30 minutách nečinnosti. |
| Audit | Každé volání, které čte nebo mění dokument, se zapíše do `mcp_activity`: čas, klient (`clientInfo` + adresa), akce, dokument a stav `ok`, `waiting` (řádek se po rozhodnutí přepíše; klik znovu otevře dotaz), `confirmed`, `denied_user`, `denied_timeout`, `denied_closed`, `denied_locked`, `failed` (s důvodem). Záznam vidí stránka AI ve všech instancích; uchovává se posledních 500 záznamů, nejvýš 30 dní. |

Více instancí: server běží v instanci, která obsadí port (*Start with PDF4QT* v další instanci port neobsadí). Stav serveru a asistentů je v databázi. Stránka AI ostatních instancí ukáže, že server je **blokovaný jiným oknem PDF4QT** (`pdf4qt-ai-other-instance` 940:135321; asistenti vidí jen dokumenty toho okna), *Start server* je tam zakázané, *Show Other Window* požádá vlastníka serveru o zobrazení jeho okna a stav se mění online. Aktivace okna jiného procesu má stejná omezení jako předání souborů ([newapplication.md](newapplication.md) 3): žádající instance na Windows nejdřív povolí popředí `AllowSetForegroundWindow(mcp_server.pid)`, na Waylandu okno vlastníka bez aktivačního tokenu jen požádá o pozornost.

- Nastavení a oprávnění jsou sdílená: vlastník serveru reaguje na oznámení změn `mcp.settings` (restart), `mcp.permissions` (`list_changed`) a `mcp.token` (odpojení všech).
- Akce z jiné instance (*Disconnect*, *Disconnect all*, *Clear*, *Show Other Window*) jdou přes tabulku `mcp_request`, kterou vlastník zpracuje a smaže ([newapp_storage.md](newapp_storage.md) 4). Dotazy na potvrzení se ukazují jen v okně vlastníka.
- Vlastník zapisuje každé 2 s `mcp_server.heartbeat`; starší než 10 s = server zanikl (pád) a ostatní instance smažou jeho klienty a povolí *Start server*.

## 5. Fáze 1 – tooly (hierarchicky)

Jeden MCP tool na oblast, podpříkaz určuje povinný parametr `action`. `tools/list` tak zůstane krátký (fáze 1: 6 toolů, fáze 2: +6) a model vidí příbuzné operace pohromadě. Popisy oprávnění na stránce AI odpovídají akcím: *Read documents* = čtení obsahu, přepínání mezi dokumenty a stránkami a zavření tabu, *Edit documents* = anotace a formuláře (text ve Figmě popisuje i fázi 2: operace se stránkami a tooly), *Open and save files* = otevření a uložení.

**Tvar schématu.** `inputSchema` je objekt bez `oneOf` / `anyOf` / `allOf` na nejvyšší úrovni (některé klienty a API je odmítají):
- `action` – výčet akcí;
- společné parametry (`documentId`);
- pro každou akci objekt parametrů pojmenovaný jako akce.

Příklad:

```json
{ "action": "add", "documentId": "k3f9…", "add": { "type": "highlight", "page": 4, "quads": [[72, 700, 210, 700, 210, 712, 72, 712]], "contents": "Check this number" } }
```

Server ověří, že je vyplněný právě objekt zvolené akce. Odpověď má `structuredContent` (`outputSchema`: `action` + objekt výsledku akce) a krátký text. Popis toolu vyjmenuje akce po jednom řádku; podrobnosti jsou v `pdf4qt://docs/tools/{tool}.md`.

**Oprávnění po akcích:**
- `tools/list` obsahuje tool, jen když je povolená aspoň jedna jeho akce;
- výčet `action` ve schématu nabízí jen povolené akce;
- po změně oprávnění přijde `notifications/tools/list_changed`;
- při volání se oprávnění kontroluje znovu.

Anotace MCP: `readOnlyHint` mají jen tooly, které čtou ve všech akcích; tool, který umí mazat, má `destructiveHint`.

Konvence: stránky se číslují od 1 (fyzické pořadí); souřadnice jsou v bodech PDF s počátkem vlevo dole, bez otočení stránky.

| Tool | Akce | Oprávnění | Co dělá |
| --- | --- | --- | --- |
| `application` (readOnly) | `get_state` | čtení | verze, otevřené dokumenty (id, název, cesta, stránky, aktivní, zamčený, neuložený, verze), aktuální cesta |
| | `navigate` | čtení | otevře cestu aplikace ([newapp_navigation.md](newapp_navigation.md)) |
| `documents` | `open` | soubory | otevře soubor do nového tabu (heslo, stránka), vrátí `documentId`; soubor už otevřený v této instanci (kanonická cesta) se neotevře znovu – vrátí se jeho `documentId` a tab se aktivuje; při *Ask* nebo po modálním dialogu se ptá |
| | `activate` | čtení | přepne tab |
| | `close` | čtení | zavře tab; dokument s neuloženými změnami nezavře |
| | `get_info`, `get_outline` | čtení | vlastnosti, oprávnění, podpisy, popisky stránek; osnova (titulek, stránka, úroveň) |
| | `save` | úpravy + soubory | uložení do vlastního souboru nebo do nové cesty |
| `view` | `go_to_page` | čtení | stránka podle čísla nebo popisku |
| | `get_selection` | čtení | vybraný text a stránka v aktivním dokumentu |
| | `render_page` | čtení | obrázek stránky PNG (výchozí 150 DPI) jako obsah `image` |
| `text` (readOnly) | `get_page_text` | čtení | text stránek, volitelně s obdélníky řádků |
| | `search` | čtení | dotaz, velikost písmen, celá slova, regulární výraz, stránky; výsledky: stránka, popisek, kontext, čtyřúhelníky |
| `annotations` (destructive) | `list` | čtení | id (reference objektu), typ, stránka, obdélník, text, autor, barva, změna, odpovědi |
| | `add` | úpravy | text note, free text, highlight / underline / strikeout / squiggly (čtyřúhelníky, typicky ze `search`), square, circle, line, ink, stamp; barva z palety anotací nebo `#rrggbb`; autor = jméno klienta (`clientInfo.name`) |
| | `update`, `delete` | úpravy | změna textu, barvy a geometrie; smazání |
| `forms` | `list` | čtení | pole formuláře: jméno, typ, hodnota, volby, povinné, jen pro čtení, stránka |
| | `set_values`, `reset` | úpravy | vyplnění polí (jako uživatel, včetně formátování a výpočtů) a vrácení výchozích hodnot |

Rychlé akce (`get_state`, `get_info`, `get_page_text` jedné stránky, `render_page` do 300 DPI) běží v hlavním vlákně, ostatní podle kapitoly 3.

## 6. Fáze 2 – tooly dokumentu

Tooly katalogu přibudou jako šest hierarchických toolů podle kategorií: `pages`, `convert`, `edit`, `protect`, `optimize`, `review`.

- Akce je druhá část id toolu (`pages.merge` → tool `pages`, `action: "merge"`). Objekt parametrů akce vznikne z definic parametrů `PDFDocumentTool` ([newapp_tools.md](newapp_tools.md) 4).
- Společné parametry: `documentId` / `documentIds` a `result` podle druhu výsledku (`keep`, `save_copy` + `path`, `overwrite`; u nového dokumentu `open_new_tab`, `save_file` + `path`); výchozí je předvybraná volba dialogu výsledku.
- V nabídce akcí jsou jen implementované tooly. Akce, kterou zakazují oprávnění nebo zámek dokumentu, chybí ve výčtu, nebo vrátí `isError` s důvodem.
- Průběh přes `progressToken`, zrušení, potvrzení podle oprávnění druhu výsledku (*Ask*); přepsání originálu vždy.
- Oprávnění podle druhu výsledku: analýza = čtení, změna = úpravy, zápis souboru = soubory.
- Výsledek `keep` jde přes undo/redo, `save_copy` a `overwrite` přes stejný zápis jako dialog výsledku.

## 7. Resources

| URI | Typ | Obsah |
| --- | --- | --- |
| `pdf4qt://docs/index.md`, `pdf4qt://docs/{topic}.md`, `pdf4qt://docs/tools/{tool}.md` | Markdown | uživatelská dokumentace pro asistenty: začátek, dokumenty a stránky (id, číslování, souřadnice), hledání, anotace, vykreslení, oprávnění; jedna stránka na MCP tool s jeho akcemi a příklady; ve fázi 2 i dokumentace toolů katalogu (stejné soubory jako úvodní stránka toolu) |
| `pdf4qt://skills/{skill}/SKILL.md` | Markdown (Agent Skills) | `pdf4qt-documents` (práce s otevřenými dokumenty), `pdf4qt-review` (kontrola a komentáře), `pdf4qt-research` (hledání a citace stránek) |
| `pdf4qt://workflows/{name}.md` | Markdown | postupy: `summarize-document`, `review-and-comment`, `find-and-highlight`; stejné postupy jsou i **prompty** MCP (`prompts/list`, `prompts/get`) s argumenty `documentId`, `pages`, `query` |
| `pdf4qt://documents/{documentId}/info` | JSON | jako `get_document_info` |
| `pdf4qt://documents/{documentId}/outline` | JSON | osnova |
| `pdf4qt://documents/{documentId}/text` | text | text celého dokumentu (nejvýš 4 MiB, jinak chyba s odkazem na stránky) |
| `pdf4qt://documents/{documentId}/pages/{page}/text` | text | text stránky |
| `pdf4qt://documents/{documentId}/pages/{page}/image/{dpi}` | `image/png` (blob) | vykreslená stránka, např. 300 DPI (do 600 DPI a 50 MPx) |
| `pdf4qt://documents/{documentId}/pages/{page}/annotations`, `…/annotations` | JSON | anotace stránky / dokumentu |

- `resources/list` vrací dokumentaci, skilly, postupy a pro každý otevřený dokument `info` a `outline`. Stránky jsou jen šablony (`resources/templates/list`), aby seznam neměl tisíce položek.
- Odběr (`resources/subscribe`) dokumentu pošle `updated` po každé změně jeho verze; otevření a zavření dokumentu pošle `list_changed`.
- Dokumentace, skilly a postupy jsou anglicky, v resources aplikace (`Pdf4QtLibApp/resources/mcp`), a jejich správnost ověřuje test (každý odkaz a každé jméno toolu existuje).

## 8. Testy

- **`UnitTestsMcp`** (bez GUI, falešný hostitel):
  - parser HTTP: fuzzing, useknuté a obří požadavky, limity a časové limity;
  - hostitel, Origin, token, relace a vyjednání verze;
  - každá metoda protokolu, chyby JSON-RPC proti `isError`;
  - hierarchické tooly: každá akce, chybějící nebo cizí objekt parametrů, akce zakázaná oprávněním (není ve výčtu, volání odmítnuto), schéma bez `oneOf` na nejvyšší úrovni;
  - SSE, průběh, zrušení, odpojení klienta během volání;
  - 16 klientů × 200 souběžných volání, zastavení serveru uprostřed volání;
  - valgrind, s krátkými limity přes proměnné prostředí.
- **`UnitTestsAppMcp`** (aplikace offscreen):
  - otevření dokumentu, hledání, vykreslení, přidání anotace;
  - undo změny z MCP, souběh úpravy uživatele a commitu (odmítnutí);
  - zamčený dokument, vypnutá oprávnění, potvrzení a jeho zamítnutí, modální dialog;
  - zavření tabu během volání, dvě instance a jeden port.
- **Ručně:** MCP Inspector a Claude Code proti běžící aplikaci podle návodu ze stránky AI.
