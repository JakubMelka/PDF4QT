---
title: Output Preview
category: tools
order: 86
summary: Separations, overprint and color warnings of a page.
keywords: output preview, separations, overprint, spot colors, cmyk, prepress, rich black, coverage warning, plates, shape channel, opacity channel
related: /tools/review.output-preview, /tools/review.ink-coverage, /tools/review.soft-proof, /help/color-management, /help/tools
---
Output Preview shows the current page as its separations: each process ink and spot color can be shown or hidden, the colors are mixed as on the press and overprint is simulated. Nothing in the document changes.

### The modes

- Separations shows the page as the checked inks print it. Uncheck an ink in Inks to leave its plate out.
- Coverage Warning paints the places, where the inks together exceed the coverage limit (300 % by default), in the alarm color.
- Rich Black Warning paints black of at least the rich black limit (95 % by default), which is mixed with cyan, magenta or yellow.
- Ink Coverage is a heat map of the sum of the inks, from no ink to the highest coverage of the page.
- Shape Channel and Opacity Channel show where the page is covered by objects and how opaque the content is.

### The values

At cursor shows the tint of every ink under the mouse pointer, Page mm² the area of the ink on the page weighted by its tint. The values are read from the picture of the page, nothing is rendered again for them.

### The switches

- Simulate Separations shows every spot color as a plate of its own. When it is off, the spot colors are converted to process colors.
- Paper Color simulates the color of the paper: white, cream, gray or a custom color.
- Content chooses, which kinds of objects are drawn: images, text, vector graphics, shading patterns and tiling patterns.

At most 26 spot colors can be shown as plates; the others are converted to process colors and the tool says so.

The page is rendered as it is printed: the annotations, which are printed, and the layers by their print state. Page Up and Page Down show the previous and the next page.
