---
title: Sign with Certificate
category: tools
order: 41
summary: Sign a document digitally with your certificate.
keywords: sign, digital signature, certificate, pades, pkcs7, signature with timestamp, visible signature, signature field, pfx
related: /tools/protect.sign, /tools/protect.timestamp, /tools/protect.sign-by-hand, /tools/protect.certificates, /help/signatures, /help/tools
---
Sign with Certificate adds a digital signature made with the private key of your certificate. The open document stays as it is: the signed document is written to a new file (its name ends with *_SIGNED*).

### Three kinds of signature

| Kind | What it proves |
| --- | --- |
| Digital signature (this tool) | Who signed, and that the document has not been changed since |
| Document timestamp (Add Timestamp) | That the document existed in this form at a time confirmed by a timestamp authority – not who signed it |
| Drawing (Sign by Hand) | Nothing – the drawing becomes part of the page, like ink on paper |

### Step by step

1. Choose the certificate in the left panel. Without a certificate, press *Create Certificate…* or *Import .pfx…*.
2. Type the password of the .pfx file. The field says at once, when the password does not open the file. A certificate of the system store needs no password here – the operating system asks for its PIN, when you sign.
3. Optionally fill in the *Reason* and *Contact info*.
4. Choose the type in the *Signature* menu: *Signature*, or *Signature with timestamp* to let a timestamp authority confirm the time of signing. The same menu chooses the format.
5. Choose *Visible* or *Invisible*. For a visible signature draw its appearance with *Draw*: a text label, a freehand curve, marks, shapes or an SVG image. The drawing on the first page with graphics becomes the appearance; its outline is the outline of the signature field. Closing the tool with a drawing asks first, also when you close the tab or the window or open another tool.
6. Press *Save Signed Copy* and choose the name and the folder of the signed copy.

A document with an empty signature field prepared by its form offers *In field …* in the *Visible* menu: the signature goes into that field. A field locked by the form cannot be signed here.

### Format

| Format | Use |
| --- | --- |
| PAdES | The default. The format of PDF 2.0 and of the European eIDAS regulation |
| Compatible (PKCS#7) | For older software, which cannot read PAdES |

### Timestamp authority

A timestamp authority confirms the time of the signature. PDF4QT sends it only a hash of the document, never its content; the authority sees your IP address and the time of the request. Nothing is sent until you press *Save Signed Copy*. When the authority cannot be reached, you can try again, choose another authority or sign without a timestamp.

### Things to know

- The signature is added as an incremental update of the file: the bytes of the original file stay untouched, so earlier signatures stay valid.
- Unsaved changes of the tab are included in the signed copy.
- A document certified with *no changes allowed* cannot be signed: a signature would break its certification.
- A signature made with a certificate you created yourself is valid, but the people who receive the document trust it only after they add your certificate to their trusted certificates.
- Your password is used only for this signature. PDF4QT never stores it – not in the settings, not in the history of the tool, not on the clipboard.
