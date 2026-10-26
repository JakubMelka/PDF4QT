---
title: Digital signatures
category: open-and-view
order: 55
summary: How PDF4QT verifies the digital signatures and timestamps of a document and which certificates it trusts.
keywords: signature, digital signature, signed, timestamp, certificate, trusted certificates, verify, valid, invalid, needs attention, AATL, certificate chain
related: /help/read-documents, /help/save-and-print, /help/settings-security
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
- the certificates of your operating system, when [Use system certificate store](/settings/security#use-system-certificate-store) is on (it is on by default),
- the certificates you have added yourself.

When the root of a chain is not trusted, its card offers **Add to trusted certificates**. Add a certificate only when you know where it comes from: every signature made with it will then be shown as valid, in all documents.

The table *Trusted certificates* in [Settings › Security](/settings/security) lists the certificates you have added (type *User*) and, with *Use system certificate store*, the certificates of your system (type *System*); the line below it counts the certificates of the AATL. Select your own certificates and click **Remove** (or press Delete) to stop trusting them; the confirmation shows the name and the SHA-256 fingerprint of each certificate. The certificates of the system are managed by your operating system and cannot be removed here. *Restore defaults* of the section keeps your certificates.

## Settings

Settings › Security has the options of the verification:

- [Signature verification](/settings/security#signature-verification) turns the verification off and on (on by default). Without it, the status bar says *not verified*.
- [Strict mode](/settings/security#strict-mode) treats every warning as an error, so a signature, which needs attention, is shown as invalid.
- [Ignore expired certificates](/settings/security#ignore-expired-certificates) does not check the validity period of the certificates. Use it with care: an expired certificate is no longer guaranteed by its issuer.
- [Use system certificate store](/settings/security#use-system-certificate-store) trusts the certificates of your operating system.

A change applies at once: the signatures of the open documents are verified again. See [Security settings](/help/settings-security).

> [!NOTE] After saving
> When you save a signed document, its signatures are verified again, because the file has changed.
