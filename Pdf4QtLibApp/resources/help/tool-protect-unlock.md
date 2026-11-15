---
title: Remove Password
category: tools
order: 54
summary: Remove the encryption of a document you have access to.
keywords: remove password, unlock, decrypt, remove security, remove restrictions, remove encryption, unprotect
related: /tools/protect.unlock, /tools/protect.password, /tools/protect.permissions, /help/tools
---
Remove Password writes the document without encryption: it opens without a password and has no restrictions. The panel *What changes* shows the encryption, the open password and the permissions before and after.

### Who can remove it

Only someone, who has owner access to the document:

- A document protected by a password needs its **owner password**. PDF4QT asks for it, when the tool opens, unless you have already unlocked the document with it.
- A document encrypted with a certificate needs a certificate, whose permissions allow changing the encryption. Encrypt with Certificate gives that permission to its own certificate only when everything is allowed. The panel *Now* says in *Opens for* whose certificate opened the document.

**PDF4QT does not break passwords.** Without owner access the protection cannot be removed.

### Things to know

- Anyone will be able to open, copy and edit the result. Remove the protection only when the content no longer needs it. The protected original stays untouched, when you save the result as a copy (recommended).
- The whole file is written again, without the old encryption dictionary.
- The signatures of a signed document stop being valid, so PDF4QT writes the result only as a copy.
- A document, which is not encrypted, has nothing to remove: the button stays disabled.
