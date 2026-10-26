---
title: Color modes
category: settings
order: 14
summary: Read pages in inverted, gray, high-contrast, black and white or your own colors, the reading themes and the contrast of the colors.
keywords: color mode, page colors, dark mode, night mode, inverted, invert, grayscale, high contrast, bitonal, black and white, custom colors, reading theme, sepia, paper, mint, amber, terminal, contrast, wcag, eye strain, accessibility
related: /settings/color-modes, /help/settings, /help/read-documents, /help/settings-viewing
---
A color mode redraws the pages of documents in other colors, for example light text on a dark page for reading at night. It changes only how the pages look on your screen: the document, its file and its printing stay as they are.

Choose a mode in [Settings › Color modes](/settings/color-modes#color-mode), or in **Page colors** of the View panel of a document. Both are the same setting: the mode applies to every open document at once and it is kept for the next start.

## The modes

| Mode | What it does |
| --- | --- |
| Normal | The colors of the document |
| Inverted | Every color is reversed: white pages become black, black text becomes white |
| Grayscale | Every color becomes a shade of gray |
| High contrast | Light parts become lighter and dark parts darker; the colors stay |
| Bitonal | Every part becomes pure black or pure white, like a fax |
| Custom colors | The pages are redrawn in two colors of your choice – the page color and the text color |

In the View panel the modes are called **Normal colors**, **Inverted colors**, **Grayscale**, **High contrast**, **Bitonal** and **Custom colors**.

Annotations (highlights, comments, stamps) keep their own colors over the recolored page. To recolor them too, turn on [Adjust colors of annotations](/settings/viewing#adjust-colors-of-annotations) in Settings › Viewing.

## Custom colors and the reading themes

In the mode **Custom colors** the dark parts of a page take the text color, the light parts the page color, and the shades in between are mixed from the two. Pictures keep their shapes; only their colors are mapped between the two colors.

The cards of [Preset](/settings/color-modes#preset) are ready-made pairs of colors. A click on a card switches the mode to Custom colors and uses its colors:

| Reading theme | Page | Text |
| --- | --- | --- |
| Paper | White | Ink black |
| Sepia | Sepia | Sepia brown |
| Soft gray | Soft gray | Charcoal |
| Mint | Mint | Forest green |
| Night | Night blue | Silver |
| Black | Black | Off-white |
| Terminal | Near black | Terminal green |
| Amber | Near black | Amber |
| High contrast | Black | Yellow |
| Custom… | Your own colors | Your own colors |

Amber is the default. To make your own pair, choose a color by [Text color](/settings/color-modes#text-color) or [Page color](/settings/color-modes#page-color) – the preset switches to **Custom…**. A click on **Custom…** keeps the two colors, which are set now, so you can adjust a reading theme further. Choosing another reading theme replaces both colors by its pair – your own pair is not kept.

**Preview** shows a sample page in the chosen colors. **Contrast** says how well the text can be read against the page, measured by the rules of the Web Content Accessibility Guidelines (WCAG):

| Contrast | Verdict | Meaning |
| --- | --- | --- |
| 7 : 1 and more | Excellent | Easy to read also for people with low vision |
| 4.5 : 1 to 7 : 1 | Good | Fine for normal text |
| 3 : 1 to 4.5 : 1 | Low | Only for large headings; small print is tiring |
| Less than 3 : 1 | Poor | Hard to read – choose other colors |

## Mode adjustments

| Setting | Used by | What it does | Default |
| --- | --- | --- | --- |
| [High contrast strength](/settings/color-modes#high-contrast-strength) | High contrast | How strongly the contrast is raised (1–100). At 1 the page barely changes; the higher the value, the closer the light and dark parts get to white and black | 10 |
| [Black and white threshold](/settings/color-modes#black-and-white-threshold) | Bitonal | Parts lighter than the threshold (0–255) become white, darker parts black. A higher value makes more of the page black | 128 |

The threshold of the pictures in the Bitonal mode is found automatically for each picture.

> [!TIP] Dark pages in a dark window
> The [color scheme](/settings/general#color-scheme) makes the window of PDF4QT dark, but not the pages. For dark pages, choose Inverted or a dark reading theme such as Night or Black.
