---
title: Sign by Hand
category: tools
order: 43
summary: Draw your signature, text and marks on the pages.
keywords: sign by hand, handwritten signature, draw signature, initials, freehand, check mark, svg signature
related: /tools/protect.sign-by-hand, /tools/protect.sign, /help/tools
---
Sign by Hand draws a signature, text labels, marks and shapes on the pages. The drawing becomes part of the content of the pages.

### Step by step

1. Pick a tool in the toolbar – *Text label*, *Freehand curve*, *SVG image…*, *Accept mark*, *Reject mark*, *Dot*, or a shape of the *Shapes* menu – and click or drag on the page.
2. Drag elements to move them and use the handles to resize them. Double-click a text label to edit its text.
3. Change the pen color, the pen width, the fill or the font in the *Style* panel. Without a selection the style applies to new elements.
4. Use *Arrange* to align elements, give them the same size, center them on the page or lay them out.
5. Press *Apply to Pages*. Keep the result in this tab, save it as a copy or overwrite the original file.

The list of the elements in the left panel shows them by pages; a click selects an element, Delete removes it, Undo brings it back. Undo and Redo take back every change of the drawing until you apply it.

Esc ends a drag or the picked tool; without them it clears the selection; when none of them is active, Esc closes the tool. Closing the tool with a drawing asks first, also when you close the tab or the window or open another tool; an empty drawing closes without a question.

### Things to know

- A drawn signature is not a digital ID signature: it does not prove who signed the document. Use Sign with Certificate for that.
- An SVG image may be at most 4 MB and may not refer to other files.
- In a signed document the drawing is added after the signatures. They stay valid for the version they signed, but the Signatures panel reports a change made after signing.
- A document with a certification, which allows no change of the pages, can only be saved as a copy without the certification.
- A PDF/A or PDF/UA document keeps its declaration. The drawing is not tagged for assistive technologies.
