---
title: Edit annotations
category: edit-content
order: 15
summary: Select, move, recolor and comment annotations - the annotation bar, the menus, Properties, Geometry and Copy to Pages.
keywords: annotation, select, move, resize, rotate, color, width, properties, geometry, comment, reply, copy, paste, hide, delete, locked, align, distribute
related: /help/annotations, /help/notes-and-outline, /help/keyboard-shortcuts
---
Annotations on a page can be selected and changed directly. Every change is one step, which **Undo** removes.

## Select annotations

- Click an annotation to select it. **Shift+click** adds another annotation or removes it from the selection.
- **Ctrl** + drag on an empty place of the page draws a rectangle: the annotations, which lie completely inside, are selected.
- Where annotations overlap, a click selects the topmost one and **Alt+click** selects the next one below it. A right click on overlapping annotations lists them, each with its own menu.
- **Select All on Displayed Pages** in the menu of an annotation selects every annotation of the pages you see.
- **Esc** or a click on an empty place clears the selection. The panel **Notes** shows the same selection.

The status bar says what you can do with the selection, for example which handles it has.

## The annotation bar

When annotations are selected, a bar appears above the page. For one annotation it shows its type and author, the color and the line width, **Comment**, **Properties** and **More** (the menu of the selection). A rectangle, an ellipse and a polygon have a fill too: choose **Stroke** or **Fill** in the bar.

Trying several colors or widths one after another is one step - one **Undo** returns the original look. For more annotations the bar shows the common color and width (*Mixed* for different widths) and the buttons, which align the annotations or distribute them evenly.

## Move, resize and rotate

Drag an annotation to move it, the square handles change its size (**Shift** keeps the proportions) and the round handle above it rotates it; some annotations turn only in steps of 90 degrees. The arrow keys move the selection. The menu **Rotate and Flip** turns the selection by 90 degrees or by 180 degrees, or mirrors it horizontally or vertically.

The points of a line, a polyline, a polygon and a freehand drawing have their own handles: drag a point to move it. A right click on a point or on a part of an annotation offers what is possible there: **Delete Point**, **Insert Point**, **Remove This Part**, **Add Stroke** and **Erase Here** (freehand drawings), **Mark More Text** (highlights and other text marks), **Add Callout Line** and **Remove Callout Line** (text boxes).

A click always follows a link, also a selected one. To edit a link, select it by **Ctrl+click** (**Shift+click** adds it to the selection), **Alt+click** or a right click - then drag it to move it, and its handles change the size of its clickable area. A link, which is not selected, is not moved by a drag. A link cannot be rotated.

**Geometry…** of the menu sets the exact position, size and rotation in points, millimeters or inches. The reference point chooses the corner (or the center), which stays in place. The points of a line, a polyline or a polygon can be typed too.

## Properties

**Properties…** (**F2**) opens all properties of the annotation: the author, the subject and the text of the comment, the color, the fill, the opacity, the border and the parts of its type - the line endings, the icon of a sticky note, the font of a text box, the leader lines of a measurement. **Apply** changes everything as one step, **Cancel** keeps the annotation as it was.

The section **Behavior** of **Properties** has the flags of the annotation: **Locked**, **Hidden on the page**, **Contents locked**, **Printed with the page**, **Keeps its size when zoomed** and **Stays upright when the page is rotated**.

The properties of a link have its **Destination**: the page (a page label such as *iv* works too) and the way the page is shown - the whole page, its width or height, an area or a position. **Go to target** shows the target, **Set to current view** takes the page and the place, which you see now. The **Text** of a link is its description for screen readers.

## Comments and replies

**Comment** of the bar, **Open Comment** of the menu or a double click opens the comment of an annotation with all its replies. Write a reply into the field at the bottom and press **Reply** (or **Ctrl+Enter**). **Edit Text** of the menu of the comment changes the text of the annotation. **Esc** closes the comment.

## Copy, paste and copy to pages

**Cut**, **Copy** and **Paste** work with annotations as with text, also between two documents. **Paste at Original Position** (**Ctrl+Shift+V**) puts the copy at the same place of the page. Annotations can also be dragged onto the page of another document; hold **Ctrl** to copy them.

**Copy to Pages…** copies the selected annotations - with their comments and replies - to other pages at the same position and size: all pages, the current page, the pages selected in the thumbnails or a range such as *2-10*, and only the even, odd, portrait or landscape pages of them. One copy makes at most 100,000 annotations.

## Hide and delete

**Hide on Page** hides an annotation; it stays in the document and in the panel **Notes**, where **Show on Page** shows it again. **Delete** (**Del**) removes the selected annotations together with their replies.

## Locked annotations and documents

An annotation marked **Locked** cannot be moved, resized, restyled or deleted, but its comment can still be changed. **Contents locked** protects only its comment text. Both are in the section **Behavior** of **Properties**.

> [!NOTE] Documents, which do not allow changes
> In a locked document, or a document, whose author did not allow changes, annotations can still be selected, copied and read: the bar shows only **Comment**, **Properties** show the values without changing them and the comment has no reply field.
