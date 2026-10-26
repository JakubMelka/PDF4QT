---
title: Editing settings
category: settings
order: 15
summary: The undo history, your name in new annotations, the highlight of form fields and the markup bar.
keywords: editing, undo, redo, history, steps, memory, author, name, anonymous, privacy, user name, annotations, forms, form fields, highlight, required fields, read only, markup bar, selection
related: /settings/editing, /help/settings, /help/annotations, /help/forms
---
[Settings › Editing](/settings/editing) has the settings of changing documents: annotations, forms and the undo history.

## Undo history

| Setting | What it does | Default |
| --- | --- | --- |
| [Maximum undo steps](/settings/editing#maximum-undo-steps) | How many changes of a document **Undo** can take back (0–1000) | 100 |
| [Maximum redo steps](/settings/editing#maximum-redo-steps) | How many undone changes **Redo** can bring back (0–1000) | 100 |

Each open document has its own history. **0** turns Undo or Redo off. When the limit is reached, the oldest step is forgotten. A new limit applies to a document at its next change or Undo.

The steps share everything, which a change did not touch, so a step usually takes little memory – about as much as the parts of the document, which it changed. The steps of one document never hold more than 512 MB; above that, the oldest steps are forgotten too.

> [!NOTE] A forgotten saved version
> When the version you have saved is no longer in the history, Undo cannot return to it. The document is then shown as changed, also after Undo, until you save it again.

## Author identity

Every new comment, highlight or other annotation carries the name of its author. The name is stored in the document, so everyone, who opens the document, sees it.

| Setting | Choices | Default |
| --- | --- | --- |
| [Author of new annotations](/settings/editing#author-of-new-annotations) | Anonymous (do not disclose the user name), System user name, Custom name | Anonymous |
| [Custom author name](/settings/editing#custom-author-name) | Your name, used when Custom name is chosen | Empty |

- **Anonymous** writes *Unknown* as the author.
- **System user name** writes the name, under which you are signed in to your computer.
- **Custom name** writes the name you type into **Custom author name**. When it is empty, *Unknown* is written. Invisible characters, which could disguise a name, are removed.

The setting concerns only new annotations; the authors of existing annotations do not change.

## Forms

| Setting | What it does | Default |
| --- | --- | --- |
| [Highlight form fields](/settings/editing#highlight-form-fields) | Tints all fields, which can be filled, so you find them at a glance | On |
| [Highlight required form fields](/settings/editing#highlight-required-form-fields) | Marks the fields, which must be filled, in a color of their own | On |
| [Allow editing of read-only fields](/settings/editing#allow-editing-of-read-only-fields) | Lets you change fields, which the form marks as read only | Off |

Turn on **Allow editing of read-only fields** only when a form locks a field, which you need to fill – for example a form, whose script would unlock the field, because PDF4QT does not run the scripts of forms. A locked document, a document, which does not allow filling its form, and an XFA form stay read only also with this setting. See [Fill in forms](/help/forms).

## Annotations

[Show markup bar when text is selected](/settings/editing#show-markup-bar-when-text-is-selected) (on by default) shows a small bar above selected text with **Highlight**, **Underline**, **Strikeout**, **Squiggly**, **Highlight and comment** and **Copy**. Turn it off, if the bar gets in your way; the marks stay in the panel Annotate and on their shortcuts. See [Annotate a document](/help/annotations).
