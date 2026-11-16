---
title: Add Timestamp
category: tools
order: 42
summary: Prove the time a document existed, using a timestamp authority.
keywords: timestamp, time stamp, rfc 3161, timestamp authority, tsa, document timestamp
related: /tools/protect.timestamp, /tools/protect.sign, /help/signatures, /help/tools
---
Add Timestamp asks a timestamp authority to sign the time at which the document existed. No certificate of your own is needed. The open document stays as it is: the timestamped document is written to a new file (its name ends with *_TIMESTAMPED*).

A document timestamp proves that the document existed in this form at that time – it does not say who signed it. It is invisible: it shows in the Signatures panel.

### Step by step

1. Pick an authority from the list in the right panel, or type the address of your own (http or https). The last used address is remembered.
2. Press *Save Timestamped Copy* and choose the name and the folder of the copy.

### What is sent

Only a hash of the document is sent to the authority, never its content. The authority sees your IP address and the time of the request. Nothing is sent until you press *Save Timestamped Copy*.

When the authority does not answer or refuses the request, nothing is written: try again or change the authority.

### Things to know

- The timestamp covers the whole document, also its earlier signatures. It is added as an incremental update of the file, so earlier signatures stay valid.
- Unsaved changes of the tab are included in the copy – except in a document certified with *no changes allowed*: undo the changes first, a timestamp may only be added without other changes.
