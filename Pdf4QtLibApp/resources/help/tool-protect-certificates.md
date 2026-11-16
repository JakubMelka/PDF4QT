---
title: Manage Certificates
category: tools
order: 44
summary: Create, import and remove your certificates, and choose whom you trust.
keywords: certificates, certificate manager, digital id, pfx, p12, trusted certificates, create certificate, import certificate, delete certificate, self-signed, system store, trust
related: /tools/protect.certificates, /tools/protect.sign, /tools/protect.certificate-encrypt, /help/settings-security, /help/portable-mode, /help/tools
---
Manage Certificates opens in its own tab **Certificates**. The left panel switches between **My certificates** – your digital IDs with a private key, for signing and for certificate encryption – and **Trusted certificates** – the certificates, on which a valid signature must end. The right panel shows the details of the chosen certificate.

### My certificates

**Certificate files** are the .pfx files of your PDF4QT certificate folder; **Open Folder** shows the folder. On Windows the card **From the system store** lists the personal certificates of the operating system: they are read-only here and the system keeps their private keys (*Private key: In the system*). Files of the older PDF4QT Editor and Viewer are read-only too; **Copy to My Certificates** copies such a file into your folder.

- **Create…** makes a self-signed certificate: name, e-mail, organization, country, a key of 2048 (recommended), 3072 or 4096 bits and the date, until when it is valid. A longer key takes longer to create; **Cancel** stops it and nothing is written. The password protects the private key in the file.
- **Import…** copies a .pfx or .p12 file into your folder. An existing file of the same name is never replaced.
- **Delete** removes a file of your certificate folder after a question. Certificates of the system store and files of the older applications are never deleted here.

A self-signed certificate is valid, but nobody trusts it until the recipient adds it to their trusted certificates – a signature made with it shows as *not trusted* elsewhere.

> [!WARNING] Keep a backup of your .pfx files
> A deleted certificate cannot be recovered. Without its private key you cannot sign as before or open documents encrypted for it. Keep a copy of the file and its password in a safe place.

### Trusted certificates

The table lists the certificates you trust (**User**) and, when *Use system certificate store* is on, those of your operating system (**System**). It is the same list as in [Settings › Security](/help/settings-security): a change in one place shows in the other.

- **Add Trusted…** adds a certificate file (.cer, .crt, .der or .pem). A file, which is no certificate, or a certificate you already trust is refused.
- **Remove** stops trusting a certificate of yours after a question. System certificates are managed by the operating system.

The trusted certificates of the older applications (TrustedCertStorage.bin) are not imported: add the certificates you need again.

> [!NOTE] Portable mode
> In the [portable mode](/help/portable-mode) **Create…**, **Import…** and **Delete** are disabled: a private key would stay on a computer, which need not be safe. **Open Folder** and the trusted certificates work as usual.
