# Permissions and confirmations

The user decides what assistants may do. The permissions are set on the AI page of PDF4QT and apply to all assistants; `application.get_state` returns them in `permissions` (`read`, `edit`, `files`, each `allow`, `ask` or `deny`).

| Permission | Covers | Default |
| --- | --- | --- |
| Read documents | Text, properties, outline, annotations, form fields, attachments and page images of the open documents; showing them in the window (`view`) and closing tabs | Allow |
| Edit documents | Annotations, form fields, the outline and the tools of PDF4QT, which change a document | Ask |
| Open and save files | `files.open`, `files.save`, `files.save_copy` | Ask |

- **Allow** – the action runs without a question.
- **Ask** – PDF4QT shows a confirmation in its window and your call waits for the answer.
- **Deny** – the actions are not offered: they are missing in the action enum of `tools/list`, and a tool without any allowed action is not listed. A call of a denied action gives an error.

A change of a permission applies at once: clients, which listen for it, get `notifications/tools/list_changed`; others see it at their next `tools/list`. Every call is checked again when it runs.

## Read approval

With Read documents set to Ask, PDF4QT asks once per assistant: the first call, which reads a document while documents are open, shows "Claude Code wants to read your open documents".

- **Allow** applies to all open documents and to the documents opened later, until your client has not used PDF4QT for 30 minutes or the user disconnects it on the AI page.
- **Deny** refuses only that call; the next call that reads asks again. Do not ask again and again – after a denial, ask the user in your conversation first.
- `readApproved` of `application.get_state` says whether you have the approval.
- `files.open` of a file, which is open already, reads its document: it asks for the approval too.
- While Read documents is Ask, `resources/list` lists the open documents without their titles ("Open document <documentId>"); the titles are document data, which you get after the approval (`application.get_state`).

## Confirmations of changes

With Edit documents or Open and save files set to Ask, every call that changes a document, opens or saves a file shows one dialog in the tab of the document – one dialog per call, also when the call needs both permissions. The dialog shows what you want to do (the rows of the changes, the quoted texts, the file and its path), the reason ("Asked because Edit documents is set to Ask") and a countdown.

- The user has **2 minutes** from the moment the dialog appears. Without an answer, the request is denied automatically. A request, whose dialog has not appeared yet – it waits behind other requests of its tab, in a tab the user does not show, or behind another dialog of PDF4QT – is denied after 10 minutes of waiting.
- While another dialog of PDF4QT is open, the read approval and `files.open` wait until the user closes it; then the question appears.
- More requests of one tab wait in a queue and are shown one after another ("1 of 3"). At most 16 requests can wait in one tab.
- The dialog shows your texts exactly as plain text. Write them for the user.
- Some confirmations are shown even with Allow: overwriting a file (`files.save`, `files.save_copy` with `overwrite`), and opening a file or creating a document while another dialog of PDF4QT was open ("Asked because a dialog was open when the request came").
- After an allowed change, PDF4QT shows a bar with **Show Changes** and **Undo** – the user can undo your whole call in one step at any time.

While your call waits, keep it open. A client with the tasks extension gets a task in the state `working` with the status message "Waiting for the user to confirm in PDF4QT"; poll it. Set a tool timeout of at least 180 seconds when your client allows it – a timeout of your client cancels the request, and PDF4QT then withdraws the dialog.

## Denials

Every denial is an error result with a text written for you. The user sees the same outcome on the AI page (Recent activity).

| What happened | What to tell the user |
| --- | --- |
| The user denied the request | You did not do it because the user denied it. Do not repeat the call unless the user asks you to. |
| No answer within 2 minutes | The request expired. Ask the user whether to try again. |
| The document is locked in PDF4QT | The user locked the document against changes; the user can unlock it or work on a copy (Edit a Copy). |
| A tool or a save is running on the document | Try again when it has finished. |
| A dialog of PDF4QT is open | Changes are refused while the user works in a dialog; try again after the user closes it. |
| The tab of the document was closed | The document is no longer open; ask the user to open it again. |
| The assistant was disconnected | The user ended your session on the AI page. Do not reconnect on your own. |
| The server stopped | PDF4QT stopped its server or quit. |
| The permission is Deny | The user does not allow this kind of action; the user can change it on the AI page of PDF4QT. |

## Who you are

PDF4QT knows your client by the access token, the name your client reports (`clientInfo`) and the network address. Your annotations get your client's name as the author, and the dialogs name you ("Claude Code wants to …"). The user can disconnect you on the AI page: running calls end, the read approval is forgotten. A new access token disconnects every assistant for good.

See also pdf4qt://docs/errors.md and pdf4qt://help/ai-permissions.md (what the user reads).
