---
title: Viewing settings
category: settings
order: 13
summary: How pages are drawn, how the mouse wheel scrolls them, the magnifier and the text of printed pages.
keywords: viewing, display, rendering, antialiasing, smooth, text, pictures, images, annotations, crop box, layers, optional content, logical pixels, zoom, display scaling, dpi, scrolling, mouse wheel, speed, magnifier, loupe, print, real text
related: /settings/viewing, /help/settings, /help/read-documents, /help/color-modes
---
[Settings › Viewing](/settings/viewing) decides how the pages of documents are drawn on the screen. A change applies at once to every open document; the documents themselves are not changed.

## Page display

| Setting | What it does | Default |
| --- | --- | --- |
| [Antialiasing](/settings/viewing#antialiasing) | Smooths the edges of lines, shapes and other vector graphics | On |
| [Text antialiasing](/settings/viewing#text-antialiasing) | Smooths the edges of letters | On |
| [Smooth pictures](/settings/viewing#smooth-pictures) | Pictures are scaled by a high-quality method, so they are not jagged when the zoom differs from their resolution | On |
| [Display annotations](/settings/viewing#display-annotations) | Shows comments, highlights, stamps, links and form fields over the pages | On |
| [Adjust colors of annotations](/settings/viewing#adjust-colors-of-annotations) | A [color mode](/help/color-modes) recolors the annotations too, not only the page | Off |
| [Clip to crop box](/settings/viewing#clip-to-crop-box) | Hides the content outside of the crop box of a page, for example printer marks | On |
| [Ignore optional content](/settings/viewing#ignore-optional-content) | Shows the content of all layers, also of the layers, which are switched off | Off |
| [Use logical pixels when zooming](/settings/viewing#use-logical-pixels-when-zooming) | The zoom 100 % follows the display scaling of your system | On |

Turn the antialiasing off only when you prefer sharp, unsmoothed edges or when drawing is too slow on a weak computer – the pages then look rougher.

**Display annotations** is the same setting as **Show annotations** in the View panel of a document. Hidden annotations stay in the document; they are only not drawn.

With **Use logical pixels when zooming** turned off, the zoom 100 % shows a page at its physical size measured by your monitor (when the monitor reports its size), regardless of the display scaling.

**Ignore optional content** is useful for technical drawings and maps, whose layers are hidden by default. Without it, switch single layers on and off in the panel **Layers** of the sidebar, see [Bookmarks and the sidebar](/help/bookmarks).

## Scrolling

| Setting | What it does | Default |
| --- | --- | --- |
| [Smooth mouse wheel scrolling](/settings/viewing#smooth-mouse-wheel-scrolling) | The pages glide, when you turn the mouse wheel, instead of jumping | On |
| [Horizontal wheel speed](/settings/viewing#horizontal-wheel-speed) | How far one step of the wheel scrolls to the side (10–400 %) | 100 % |
| [Vertical wheel speed](/settings/viewing#vertical-wheel-speed) | How far one step of the wheel scrolls up and down (10–400 %) | 100 % |

A speed of 200 % scrolls twice as far as usual, 50 % half as far. The horizontal speed applies to a tilting wheel, to a touchpad and to the wheel with **Shift** held. The wheel with **Ctrl** held zooms the pages.

## Magnifier

The magnifier is a round glass, which enlarges the pages under the pointer. Turn it on by **Magnifier** in the View panel of a document.

| Setting | What it does | Default |
| --- | --- | --- |
| [Magnifier size](/settings/viewing#magnifier-size) | The diameter of the glass (50–400 px) | 100 px |
| [Magnifier zoom](/settings/viewing#magnifier-zoom) | How many times the glass enlarges (1.0–10.0×) | 2.0× |

## Printing and export

[Draw text as real text](/settings/viewing#draw-text-as-real-text) (off by default) prints the text of the pages as text, not as drawn shapes. The text then stays selectable in the files of printers, which keep text, for example a PDF printer. A font embedded in the document is used, when it can be; otherwise a similar font of your system replaces it, so some letters can look different from the screen. It does not change the pages on the screen.
