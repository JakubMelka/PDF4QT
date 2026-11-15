---
title: Encrypt with Certificate
category: tools
order: 53
summary: Encrypt the document, so only the holder of the certificate can open it.
keywords: certificate encryption, encryption, public key, pfx, p12, private key, digital id, encrypt for, certificate security, self-signed certificate, import certificate
related: /tools/protect.certificate-encrypt, /tools/protect.unlock, /tools/protect.password, /help/tools
---
Encrypt with Certificate encrypts the document for your digital ID – a certificate file (.pfx) with its private key. Only someone who has the certificate file and its password can open the result; no password travels with the document. The encryption is always AES 256-bit.

### Choose a certificate

The list shows the certificates of your certificate folder. A row shows the name of the holder, the organization, the e-mail and the date until the certificate is valid.

| Row | What it means |
| --- | --- |
| Details are shown after the password | A .pfx file, which was never opened in PDF4QT. Its name and details are inside the file and are read with its password; PDF4QT then remembers the public part of the certificate (never the password or the private key) |
| Expired – cannot be used | The certificate is out of its validity. Create a new one |
| Not meant for encryption – cannot be used | The certificate does not allow encryption (an older self-signed certificate of PDF4QT Editor, for example). Create a new one |
| System store | A certificate of the operating system. It will be supported with Manage Certificates; for now use a .pfx file |
| from an older PDF4QT application | A .pfx file of PDF4QT Editor or PDF4QT Viewer. It is only read; Copy to My Certificates copies it into your certificate folder, so it stays available when the older applications are removed |

When you have no certificate yet, create one or import a .pfx file – both buttons are in the toolbar and in the empty list.

### The certificate password

Type the password of the chosen .pfx file. It is used once – to read the certificate and to check, that you can open the result – and it is not stored. A wrong password shows *Invalid certificate or password.*; PDF4QT never tells, which of the two was wrong. A .pfx file without a password needs none.

### What is encrypted

| Encrypt | What happens |
| --- | --- |
| Everything | All contents including the document metadata |
| Everything except metadata | Only the XMP metadata stays readable, for example for search engines; the document information (its title and author in the document properties) is encrypted |
| File attachments only | The document opens freely, the attached files need the certificate |

### Permissions

The card Permissions of whoever opens the document is open to limit printing, editing or copying. Everything is allowed by default, and then the holder of the certificate keeps full access, including the right to change or remove the encryption later.

> [!WARNING] Restrictions apply to you as well
> The document is encrypted for your own certificate. When you restrict a permission, the restriction applies to you too: you will not be able to change or remove this protection yourself. Keep everything allowed, when you want to remove the encryption later with Remove Password.

Permissions are respected by PDF4QT and by most readers, but they are not a hard protection: a reader, which ignores them, can still print or copy. The encryption itself – who can open the document – is the real protection.

### Create a self-signed certificate

Create Certificate… makes a new digital ID: the name, the e-mail and the organization of the holder (the organization unit is optional), the country, the key length (2048 bits is recommended; 1024 bits is weak and only for old systems; 4096 bits is the strongest and makes signing slower) and the date, until when the certificate is valid (five years by default). The file is saved as the next free cert_N.pfx in your certificate folder and comes back chosen. The password protects the private key in the file; it can be left empty, but then anyone with the file can open your documents and sign as you. A self-signed certificate is not trusted by others until they add it to their trusted certificates.

### Import a .pfx file

Import… copies a .pfx or .p12 file into your certificate folder (a .p12 file gets the extension .pfx). A file larger than 1 MB, or a file, which is no certificate file with a private key, is refused. When your folder already has a different file of the same name, rename the file you want to import or choose another one – the existing file is never replaced.

> [!NOTE] Portable mode
> In the [portable mode](/help/portable-mode) (`--portable`) Create Certificate… and Import… are disabled: the private key of a new certificate would stay on a computer, which need not be safe. A .pfx file, which is already in your certificate folder, can be used as usual.

### Step by step

1. Choose a certificate, or create or import one.
2. Type the password of the certificate file.
3. Choose what to encrypt in the toolbar and, if you need, limit the permissions.
4. Press Encrypt Document.
5. Save the result as a copy (recommended), keep it in this tab or overwrite the original file.

### What happens to the file

- The file is written whole, with the AES 256-bit encryption of PDF 2.0 for one recipient – your certificate.
- A signed document can be saved only as a copy: its signatures are no longer valid in the encrypted file.
- PDF/A does not allow encryption: the result loses its PDF/A declaration. A PDF/UA declaration stays – assistive technologies can always read an encrypted document.
- An encrypted document is opened with the certificate on the password screen: type the password of your .pfx file.

> [!WARNING] Keep a backup of the certificate
> Without the private key the document is lost. Keep a backup of the certificate file and its password: PDF4QT cannot open the document without them, and nobody can recover them for you. PDF4QT never breaks or bypasses an encryption.
