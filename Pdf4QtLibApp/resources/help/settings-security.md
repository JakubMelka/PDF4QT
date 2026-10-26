---
title: Security settings
category: settings
order: 16
summary: What documents may open or start on your computer, how signatures are verified and which certificates you trust.
keywords: security, links, web links, url, uri, launch, program, application, run, file, signatures, verification, strict mode, expired certificates, system certificate store, trusted certificates, remove certificate, fingerprint, sha-256, aatl, malware, phishing
related: /settings/security, /help/settings, /help/signatures, /help/document-properties
---
[Settings › Security](/settings/security) protects your computer from documents, which you do not know, and decides, which digital signatures PDF4QT treats as valid.

## Links and actions

A document can contain links to web pages and actions, which want to start a program or open a file. PDF4QT never opens any of them without asking you.

| Setting | What it does | Default |
| --- | --- | --- |
| [Allow URI link launch](/settings/security#allow-uri-link-launch) | Lets documents open web links (http and https) and e-mail links in your browser or your e-mail program | On |
| [Allow application launch](/settings/security#allow-application-launch) | Lets documents start programs and open files in the programs of your system | Off |

With **Allow URI link launch** on, a click on a web link asks *Open the web link?* (an e-mail link *Open the e-mail link?*) every time and shows the address: **Open Link** opens it, **Copy Link** copies the address, **Cancel** does nothing. Check the address – a link in a document can lead to a fake site. Addresses of other kinds (for example `file:` or `javascript:`) are never opened. With the setting off, the link is not opened and a bar above the page says so.

With **Allow application launch** on, a document can ask *Start a program from …?* (**Start Program**, **Don’t Start**) or *Open a file from …?* (**Open File**, **Don’t Open**). The question shows the program, its parameters and its folder, or the file and the program, which opens it. Start a program only when you trust the author of the document: a program or a file from an unknown document can contain malware. With the setting off (the default), nothing is started and a bar above the page says so; its button **Security Settings** opens this setting.

A link to another PDF document is not affected by these settings: the document opens in a new tab of PDF4QT. A link to a file on another computer of the network is never followed by itself – the bar above the page offers **Open Anyway**.

## Signature verification

| Setting | What it does | Default |
| --- | --- | --- |
| [Signature verification](/settings/security#signature-verification) | Verifies the digital signatures of a document, when it opens | On |
| [Strict mode](/settings/security#strict-mode) | Treats every warning as an error: a signature, which would need attention, is shown as invalid | Off |
| [Ignore expired certificates](/settings/security#ignore-expired-certificates) | Does not check the validity period of the certificates | Off |
| [Use system certificate store](/settings/security#use-system-certificate-store) | Trusts the certificates, which your operating system trusts | On |

A change applies at once: the signatures of the open documents are verified again. With **Signature verification** off, the status bar of a signed document says *not verified*.

Turn on **Ignore expired certificates** only when you understand the risk: a signature made with a certificate, which was no longer valid when it signed, would then look valid. **Strict mode** suits checks of documents, where nothing uncertain may pass.

## Trusted certificates

The table **Trusted certificates** lists the certificates, which make a signature valid, with their **Type**, **Certificate**, **Organization**, **Valid from** and **Valid to**:

- **User** – certificates you have trusted yourself by **Add to trusted certificates** in the panel Signatures of a signed document;
- **System** – the certificates of your operating system, when **Use system certificate store** is on. They are read in the background (*Reading the certificates of the system…*).

The line below the table also counts the certificates of the Adobe Approved Trust List (AATL), which come with PDF4QT and are always trusted.

Hold the pointer over a row to see its issuer and its SHA-256 fingerprint. To stop trusting your own certificates, select them and click **Remove** (or press **Delete**). The confirmation lists the name and the fingerprint of each certificate; **Remove** cannot be undone. Signatures made by these certificates are then no longer valid, unless the system or the AATL trusts them. The certificates of the system are managed by your operating system and cannot be removed here.

**Restore defaults** of the section keeps your trusted certificates. A [backup](/help/data-storage) contains them; when you import a backup, its certificates are trusted only when you check that in the confirmation.

See [Digital signatures](/help/signatures) for what the results of a verification mean.
