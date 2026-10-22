---
title: Digital signatures
category: open-and-view
order: 55
summary: How PDF4QT verifies the digital signatures and timestamps of a document and which certificates it trusts.
keywords: signature, digital signature, signed, timestamp, certificate, trusted certificates, verify, valid, invalid, needs attention, AATL, certificate chain
related: /help/read-documents, /help/save-and-print, /help/settings
---
When a signed document is shown, PDF4QT verifies its signatures and timestamps in the background. The verification is offline: nothing is sent over the network. The status bar shows a summary, for example *2 signatures · 1 needs attention*; click it to open the panel **Signatures** in the sidebar.

## What the result means

| Result | Meaning |
| --- | --- |
| Valid | The document has not been changed since it was signed, and the certificate of the signer is trusted |
| Needs attention | The signature is intact, but something is not certain – usually the certificate of the signer is not trusted, or it has expired |
| Invalid | The document has been changed after it was signed, the signature is damaged, or the certificate has been revoked |

Expand a card of a signature to see when it was signed, its hash algorithm, which part of the document it covers, its timestamp and the chain of the certificates from the signer to the root.

## Trusted certificates

A signature is valid only when its chain ends at a certificate you trust. PDF4QT trusts:

- the certificates of the Adobe Approved Trust List (AATL), which come with PDF4QT,
- the certificates of your operating system, when *Use system certificate store* is turned on in Settings › Security,
- the certificates you have added yourself.

When the root of a chain is not trusted, its card offers **Add to trusted certificates**. Add a certificate only when you know where it comes from: every signature made with it will then be shown as valid, in all documents.

## Settings

Settings › Security has the options of the verification: turn the verification off, treat every warning as an error (*Strict mode*), ignore the expiration of certificates, and use the certificate store of the system. A change applies at once to the open documents.

> [!NOTE] After saving
> When you save a signed document, its signatures are verified again, because the file has changed.
