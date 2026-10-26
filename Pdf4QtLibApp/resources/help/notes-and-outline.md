---
title: Edit notes and the outline
category: edit-content
order: 20
summary: Select, reply to, hide and delete notes in the panel Notes, and add, rename, move and style the items of the outline.
keywords: notes, comments, replies, reply, hide, show, delete notes, outline, table of contents, chapters, bookmarks, new item, rename, indent, outdent, move, drag, target, destination, named destination, fit, zoom, inherit zoom, wildcard, bold, italic
related: /help/annotations, /help/bookmarks, /help/keyboard-shortcuts
---
The panels **Notes** and **Outline** of the sidebar show more than a list: you can edit the notes and the outline of the document there. Every change is one step, which Ctrl+Z undoes. A locked document cannot be changed - unlock it first, or use **Edit a Copy**.

## Notes

**Notes** lists the comments and markups of the document by their pages. The replies to a note follow it, shifted to the right.

- **Selection.** The notes selected in the panel are selected on the page too, and the other way round. Hold Ctrl or Shift to select more notes. A click on a note also shows it on its page.
- **Hidden notes.** A note hidden on the page has the crossed-out eye. A hidden note and a reply have no place on the page, so they are selected only in the panel.
- **Search.** *Search notes…* finds a note by its author or text. The selection stays while you search.

Right-click a note for its menu:

| Action | What it does |
| --- | --- |
| Open Comment | Opens the comment of the note on the page with its replies |
| Reply | Opens the comment with the field for a reply |
| Properties… | Opens the properties of the note (read only in a locked document) |
| Hide on Page, Show on Page | Hides the note on the page or shows it again; the note stays in the document |
| Delete | Deletes the selected notes with their replies. The Delete key does the same |

## Outline

**Outline** is the table of contents of the document.

| Action | How |
| --- | --- |
| Go to the page of an item | Click the item, or **Go to Page** in its menu |
| Add an item for the current view | The button **+** in the header of **Outline**, **New Item** in the menu of an item, the Insert key in the outline, or **Insert › Outline item** in the toolbar. Text selected on the page is offered as the title. Type the title and press Enter; Esc adds nothing |
| Rename an item | F2, or **Rename** in the menu of the item. Enter keeps the new title, Esc the old one |
| Move an item | Drag it above, below or onto another item, or use **Move Up**, **Move Down**, **Indent** and **Outdent** in its menu |
| Select more items | Hold Ctrl or Shift; such a click does not go to the page of the item. Drag the selected items together, or delete them all at once |
| Change where an item goes | **Set Target** in the menu of the item - see below |
| Bold or italic title | **Bold** and **Italic** in the menu of the item |
| Delete an item | The Delete key, or **Delete** in the menu. The item goes with the items inside of it |
| Find an item | Type a part of its title into *Search outline…*. Use `*` for any text and `?` for one character, for example `chapter ?` or `*appendix*` |

### Set Target

**Set Target** in the menu of an item decides, where the item goes and how the page is shown. Go to the page first - the targets without "…" take the page and the place you see now:

| Target | What the reader sees |
| --- | --- |
| Current View | The place you see in the document now |
| Whole Page | The whole page fits into the window |
| Page Width | The width of the page fits into the window, the page opens at the top of your view |
| Page Height | The height of the page fits into the window, the page opens at the left side of your view |
| Area… | The rectangle you draw fits into the window |
| Position… | The clicked place is at the top-left corner of the window |
| Visible Content | The content of the page without its margins fits into the window |
| Visible Content Width, Visible Content Height | The width or the height of the content fits into the window |
| Named Destination… | A place, which the document has named - choose it from the list of the document |

After **Area…** or **Position…**, a bar below the toolbar asks for the target: go to the page - scroll, use the page field or the thumbnails - and draw the area or click the place. Esc or the close button of the bar cancels it.

**Keep the Reader's Zoom** (for a **Position** target) opens the page at the zoom, which the reader uses, instead of the zoom stored with the item. **Keep the Reader's Zoom for All Items…** does this for every item of the outline at once, after it tells you, how many items change.

A new item goes to the place you see in the document and keeps the zoom of the reader. A document without an outline gets one with its first item. When you delete the last item, the **Outline** icon disappears from the sidebar.

Some damaged outlines cannot be edited; PDF4QT then shows the outline without the editing.
