---
title: Document properties, links and rendering issues
category: open-and-view
order: 60
summary: The properties of a document, how links and actions of a document are opened, rendering issues and the developer view.
keywords: properties, information, title, author, subject, keywords, custom properties, metadata, XMP, fonts, embedded, security, encryption, permissions, password, viewer preferences, print settings, link, web link, URL, program, launch, JavaScript, script, form, rendering issues, errors, developer, text blocks, render times
related: /help/read-documents, /help/signatures, /settings/security
---
The properties of a document describe the document itself and the file on the disk. Open them by **File › Properties…** or by **Ctrl+D**. They replace the view of the document in its tab; **Back to document** returns to the pages.

## Sections

| Section | What it shows |
| --- | --- |
| Properties | The title, the subject, the author and the keywords of the document, the program, which has made it, the PDF version, the dates, the number of the pages and their paper, the file on the disk and the custom properties of the author |
| Security | The encryption of the document and what you are allowed to do with it (print, copy, change pages, fill forms) |
| Fonts | The fonts used by the pages and whether they are embedded in the document |
| Display and print | How the author asks a viewer to show and to print the document |
| XMP metadata | The metadata packet of the document as XML |

**Copy all** copies the properties of every section as text.

## Change the properties

The title, the subject, the author, the keywords and the custom properties have the **Edit** button. Type the new value and press **Enter**, **Esc** cancels the change. In the section **XMP metadata** edit the XML and press **Apply changes**; **Create default metadata** makes a packet from the properties of a document, which has none. Each change is one step of **Undo** and the document has unsaved changes until you save it.

> [!NOTE] Locked documents
> A locked document cannot be changed. Unlock it first, or use **Edit a Copy**.

## Links and actions

A link of a document shows where it leads, when the pointer is over it: the address in a tooltip and in the status bar.

- **A link in the document** goes to its page at once.
- **A web link** (http, https or an e-mail address) asks every time, before it opens in your web browser or your e-mail program. Check the address – a link in a document can lead to a fake site. **Copy Link** copies the address instead. Other kinds of addresses are never opened.
- **A link to another PDF document** opens it in a new tab.
- **A program or a file**, which a document wants to start, is never started without your confirmation, and only when [Settings › Security](/settings/security) allows it (it is turned off by default).

When a link is not opened, a bar above the page says why. **Security Settings** opens the setting, which turns web links or programs on and off.

> [!WARNING] Scripts and forms
> PDF4QT does not run JavaScript of documents. A form, which uses scripts to calculate or to check its fields, works without them, and a bar says so. A form is never sent to the internet.

## Rendering issues

When a part of a page cannot be drawn exactly (a font, which is not embedded, an unsupported shading, a damaged content), the status bar shows the number of the issues. Click it, or press **Ctrl+E**, to see the list; a click on an issue shows its page. Only the pages, which have been shown, are checked.

## Developer view

With **Developer mode** in [Settings › Advanced](/settings/advanced) the pages can show the blocks and the lines of the recognized text and the time of drawing of each page. It helps to find, why a text is selected or read aloud in a wrong order.
