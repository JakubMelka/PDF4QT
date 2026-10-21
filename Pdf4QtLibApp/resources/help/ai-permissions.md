---
title: Permissions of assistants
category: ai-assistants
order: 20
summary: Allow, Ask or Deny reading, editing and opening of files for all assistants.
keywords: permission, allow, ask, deny, confirm, privacy, read, edit, save, files
related: /ai, /help/ai-assistant
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
| Read documents | Text, properties, outline, annotations and page images of open documents; switching between them and closing them | Allow |
| Edit documents | Annotations, form fields, page operations and the tools of PDF4QT | Ask |
| Open and save files | Opening documents from the disk and saving results | Ask |

A change of a permission applies at once, the server does not restart.

## Questions in the document

When a permission is **Ask**, the tab of the document shows the question of the assistant: what it wants to do, with which document, and the buttons to allow or deny it. Other tabs stay free while the question waits.

- **Read documents** set to Ask asks once for each session of an assistant; the answer applies to all open documents until the assistant disconnects.
- Overwriting the original file is always confirmed, also when **Open and save files** is Allow.

> [!NOTE] Your documents stay with you
> PDF4QT sends nothing on its own. An assistant gets only what it asks for, within the permissions you allow.
