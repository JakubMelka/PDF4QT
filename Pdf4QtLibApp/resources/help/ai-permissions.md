---
title: Permissions of assistants
category: ai-assistants
order: 20
summary: Allow, Ask or Deny reading, editing and opening of files for all assistants.
keywords: permission, allow, ask, deny, confirm, confirmation, privacy, read, edit, save, files, activity, denied
related: /ai, /help/ai-assistant, /help/ai-tools
---
The permissions on the page [AI](/ai) apply to all assistants. Each of them has three states:

| State | Meaning |
| --- | --- |
| Allow | The assistant may do it without asking |
| Ask | PDF4QT asks you every time |
| Deny | The action is not offered to assistants at all |

## The permissions

| Permission | What it covers | Default |
| --- | --- | --- |
| Read documents | Text, properties, outline, annotations, form fields, attachments and page images of open documents; switching between them and closing them | Allow |
| Edit documents | Annotations, form fields, the outline and the tools of PDF4QT, which change a document | Ask |
| Open and save files | Opening documents from the disk and saving results | Ask |

A change of a permission applies at once, the server does not restart.

## Questions in the document

When a permission is **Ask**, the tab of the document shows the question of the assistant: what it wants to do, with which document, why you are asked, and the buttons to allow or deny it. Other tabs stay free while the question waits; the status bar of every tab says that an assistant waits for your answer, and the Home tab shows a banner with **Show**.

- **Read documents** set to Ask asks once for each assistant: the first time it wants to read, PDF4QT shows your open documents and asks whether it may read them. **Allow** applies to all open documents, also to the ones you open later, until it has not used PDF4QT for 30 minutes or you disconnect it. **Deny** refuses only that request; the assistant is asked again next time.
- A change of a document shows the changes, the pages and the quoted texts. **Show on Page** draws the proposed annotations on the page, so you can see them before you decide.
- One request asks only once, also when it needs more permissions.
- Overwriting the original file is always confirmed, also when **Open and save files** is Allow. A request, which came while a dialog of PDF4QT was open, is confirmed too.
- No button is preselected: **Enter** does nothing, **Esc** denies, and the buttons ignore clicks for a second after the question appears – a question, which comes while you type, is never answered by accident.
- Without an answer within 2 minutes, the request is denied automatically.
- At most 16 requests of assistants can wait in one tab; more are refused until you answer.

After an assistant changed a document – also with **Allow** – a bar over the page says what it did, for example *Claude Code added 3 highlights and 1 note*. **Show Changes** selects the new annotations in the Notes panel, **Undo** removes the whole change in one step. Nothing is saved to the file until you or the assistant save it.

## When a request is refused without a question

- A locked document is never changed by an assistant. Unlock it, or let the assistant work on a copy (Edit a Copy).
- While a save, a print or a tool runs on the document, changes are refused.
- While a dialog of PDF4QT is open, changes are refused; requests to read wait until you close the dialog.

## Recent activity

The page AI lists what the assistants did. The status of a request is one of:

| Status | Meaning |
| --- | --- |
| Waiting for your confirmation | The question waits in a tab; **Show** brings it up |
| Waiting for confirmation in the other PDF4QT window | The question waits in the window of PDF4QT, which runs the server |
| Waiting for a choice in <assistant> | The assistant asks you a question in its own window (for example, which document it should use) |
| Allowed by you | You allowed the request |
| Denied by you | You denied the request |
| Denied automatically · no answer in 2 min | Nobody answered the question in time |
| Denied · the tab was closed | The tab of the document was closed while the question waited |
| Denied · the document is locked | The document is locked in PDF4QT |
| Denied · a tool is running on the document | A save, a print or a tool ran on the document |
| Denied · a dialog was open in PDF4QT | A dialog of PDF4QT was open when the request came |
| Denied · the assistant disconnected | The assistant was disconnected while its request waited |
| Denied · the server stopped | The server stopped while the request waited |
| Denied · the permission is set to Deny | The permission of the request is set to Deny; repeated requests within a minute are one row |
| Canceled by the assistant | The assistant gave up the request itself |
| Failed | The request could not be done; the reason follows the status |

A finished request without a status line was done without a question.

> [!NOTE] Your documents stay with you
> PDF4QT sends nothing on its own. An assistant gets only what it asks for, within the permissions you allow. The texts in your documents are given to the assistant as data – an assistant should never follow instructions written in a document.
