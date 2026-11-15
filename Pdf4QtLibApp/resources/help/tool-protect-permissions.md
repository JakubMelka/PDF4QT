---
title: Restrict Permissions
category: tools
order: 52
summary: Limit printing, copying and editing of the document.
keywords: permissions, restrict, owner password, prevent printing, prevent copying, no editing, rights, encryption, drm
related: /tools/protect.permissions, /tools/protect.password, /tools/protect.unlock, /help/tools
---
Restrict Permissions encrypts the document with an owner password and limits what readers may do with it. Checked actions stay allowed; everything else needs the owner password.

### The permissions

| Permission | What it allows |
| --- | --- |
| Print in low resolution | Printing in draft quality |
| Print in high resolution | Printing in full quality; it includes printing in low resolution |
| Modify document contents | Editing the text, the images and other content of the pages |
| Assemble the document | Inserting, rotating and deleting pages, editing the bookmarks – also without the permission to modify the contents |
| Edit comments and form fields | Adding and changing annotations and interactive items; it includes filling in forms |
| Fill in forms | Filling in the existing form fields and signing them |
| Copy and extract content | Copying text and images to other applications |
| Access for screen readers | Always allowed in PDF 2.0: assistive technologies may always read the document |

Some permissions depend on others: checking *Print in high resolution* checks *Print in low resolution*, unchecking low resolution unchecks high resolution, and *Edit comments and form fields* keeps *Fill in forms* checked.

### Presets

A preset only checks the list – adjust it freely afterwards. The label of the preset then says *Custom*.

| Preset | Allowed |
| --- | --- |
| View only | Nothing but reading |
| View and print | Printing in low and high resolution |
| Print and fill forms | Printing and filling in form fields |
| Allow everything | Everything – only the security settings are protected |

### Restrictions rely on the reader

The restrictions are a request of the author. PDF readers, which respect the standard, enforce them – PDF4QT does, and never offers a way to ignore them – but they do not stop someone determined. Use an open password for confidential content.

### Passwords

- The **owner password** lifts the restrictions and changes the security later. It is required. Keep it safe: a lost password cannot be recovered, and PDF4QT never breaks a password.
- *Also require a password to open* adds an open password. It must differ from the owner password – otherwise every reader gets owner access.

PDF4QT never keeps a password.

### Step by step

1. Pick a preset or check the actions, which readers may do.
2. Type the owner password and repeat it.
3. Optionally check *Also require a password to open* and type the open password.
4. Choose the algorithm and what is encrypted, if needed (see Protect with Password).
5. Press Apply Restrictions and save the result.

### Things to know

- A document, which is already encrypted, needs its owner password first.
- The signatures of a signed document stop being valid, so the result is written only as a copy.
- A PDF/A document loses its PDF/A declaration; a PDF/UA declaration stays.
- When a forbidden action asks for the owner password in PDF4QT, the dialog offers *Open Restrict Permissions* to change the restrictions.
