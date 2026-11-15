---
title: Protect with Password
category: tools
order: 51
summary: Require a password to open the document.
keywords: password, protect, encrypt, encryption, aes, rc4, open password, owner password, user password, attachments
related: /tools/protect.password, /tools/protect.permissions, /tools/protect.unlock, /help/tools
---
Protect with Password encrypts the document, so it opens only with the password you choose. The whole file is written again, encrypted with a new key.

### Passwords

- The **open password** is asked every time the document is opened. Type it twice: the second field checks that you did not mistype it.
- The **owner password** is needed to change or remove the protection later. By default the open password gives the owner access too.
- A document, which keeps its restrictions (for example *Printing only*), needs an owner password of its own. With the open password as the owner password every reader would get owner access and the restrictions would mean nothing, so the check box is off and disabled then.
- The owner password must differ from the open password, when it is its own one.

PDF4QT never keeps a password – not in the settings, not in the history of the tool, not on the clipboard. Write it down and keep it in a safe place: **a lost password cannot be recovered**, and PDF4QT never breaks a password.

### Password strength

The meter estimates how hard the password is to guess: from *Very weak* to *Very strong*, in bits of 128. Long passwords with lower and upper case letters, digits and symbols are the strongest. A weak password can still be used; the tool warns you.

Older algorithms use only a part of the password: AES 128-bit and RC4 128-bit only its first 32 characters, and only characters of the basic Latin alphabet and a few accented letters (PDFDocEncoding). The field says which characters cannot be used.

### Algorithm

| Algorithm | Use |
| --- | --- |
| AES 256-bit | The default. Very strong, opens in all current PDF readers |
| AES 128-bit | For readers older than 2010 |
| RC4 128-bit | Weak, can be broken. Only for very old readers |

PDF 2.0 marks AES 128-bit and RC4 as deprecated; PDF4QT still reads and writes them for older readers.

### What is encrypted

| Choice | Result |
| --- | --- |
| Everything | All contents including the document metadata |
| Everything except metadata | Only the XMP metadata stays readable, for example for search engines; the document information (its title and author in the document properties) is encrypted |
| File attachments only | The document opens freely, the attachments need the password |

A document, whose attachments only are encrypted, asks for the password when it is opened. *Open without attachments* opens it without the password: the attachments stay encrypted, the Attachments panel says *Locked – enter the password*, and the document cannot be changed or saved until you enter the password there. Until then the document stays locked: neither Unlock nor Edit a Copy is offered; the password of the attachments opens it again and unlocks it.

### Step by step

1. Type the open password and repeat it.
2. Keep *Use the open password for owner access too*, or uncheck it and type an owner password.
3. Choose the algorithm and what is encrypted, if you need another than the default.
4. Press Protect Document and save the result as a copy (recommended), keep it in this tab or overwrite the original file.

### Things to know

- A document, which is already encrypted, needs its owner password first: PDF4QT asks for it, when the tool opens. The new protection replaces the old one; the restrictions of the document are kept.
- The signatures of a signed document stop being valid, so PDF4QT writes the result only as a copy.
- A PDF/A document loses its PDF/A declaration – PDF/A does not allow encryption. A PDF/UA declaration stays.
