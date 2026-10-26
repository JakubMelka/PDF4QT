---
title: Fill in forms
category: edit-content
order: 30
summary: Fill in the fields of a PDF form, undo the changes and save the filled document.
keywords: form, field, fill, check box, radio button, text field, choice, list, tab, undo, calculated, xfa, read only, save
related: /help/annotations, /help/signatures, /settings/editing#highlight-form-fields
---
A PDF form has fields, which you can fill in directly on the page: text fields, check boxes, radio buttons and lists of choices.

## Fill in a form

1. Open the document with the form.
2. Click a field and type its value, or choose a value of a list. Check boxes and radio buttons change by a click or by **Space**.
3. **Enter** confirms a text field, **Esc** returns its previous value. A field is also confirmed, when you click elsewhere.
4. **Tab** moves to the next field and **Shift+Tab** to the previous one, page by page in the order of the fields of each page; the page scrolls to the field. After the last field **Tab** goes on to the next part of the window.
5. Save the document by **Save**, or save a filled copy by **Save as**.

Every confirmed field is one step, which **Undo** takes back. **Undo** in a field, which you are just typing, confirms the field first and then takes the step back – **Redo** gives your text back. **Save** and closing the document confirm the field you are typing in, so nothing you have typed is lost.

A button of the form, which resets the form, returns the fields to their default values as one step *Reset form*; **Undo** gives your values back. A form, which has its default values already, is not changed.

## Fields, which cannot be filled

- A **locked** document cannot be changed – unlock it in its tab first.
- A document, which does not allow the filling of its form, shows the fields, but they do not react.
- **Read-only fields** keep their values. You can allow their editing in [Settings › Editing](/settings/editing#allow-editing-of-read-only-fields).
- **XFA forms** are shown, but PDF4QT does not fill them in; a bar above the page says so.

## Calculated fields

Some forms calculate fields by scripts, for example a total of other fields. PDF4QT does not run scripts, so these fields are unlocked and a bar above the page tells you to fill in their values yourself.

## Options

[Highlight form fields](/settings/editing#highlight-form-fields) tints all fields, which can be filled, and [Highlight required form fields](/settings/editing#highlight-required-form-fields) marks the fields, which must be filled; both are on by default. A change of the settings applies at once to the open documents. See [Editing settings](/help/settings-editing).

> [!WARNING] Signed documents
> Filling in a form changes the document. When the document is signed, PDF4QT asks, before it saves the changes, and saves them as an update after the signed part of the file, so the signature stays valid for its version. The change after the signature is shown in the signatures of the document.
