---
title: Advanced settings
category: settings
order: 18
summary: The rendering engine, threads, memory caches, the quality of shadings and the developer options.
keywords: advanced, performance, speed, slow, memory, ram, rendering engine, renderer, blend2d, qpainter, threads, multithreading, cpu, prefetch, cache, compiled pages, thumbnails, fonts, shading, gradient, mesh, color tolerance, developer, debug, text blocks, text lines, render time
related: /settings/advanced, /help/settings, /help/settings-viewing, /help/document-properties
---
[Settings › Advanced](/settings/advanced) tunes the engine, which draws the pages. The defaults suit almost every computer and document. Change them when pages are drawn too slowly, when PDF4QT takes too much memory, or when a page does not look right and you want to find out why.

## Rendering engine

| Setting | What it does | Default |
| --- | --- | --- |
| [Rendering engine](/settings/advanced#rendering-engine) | The drawing engine of the pages: QPainter, Blend2D – multithreaded or Blend2D – single thread | Blend2D – multithreaded |
| [Multithreading strategy](/settings/advanced#multithreading-strategy) | How many processor cores prepare the pages: Single thread, Multithreading (load balanced) or Multithreading (maximum threads) | Multithreading (load balanced) |
| [Prefetch pages](/settings/advanced#prefetch-pages) | Prepares the next pages in advance, so scrolling does not wait for them | On |

**Blend2D** is a fast, high-quality drawing engine; *multithreaded* draws a page with more processor cores at once. **QPainter** is the drawing engine of the Qt framework. Try QPainter or Blend2D – single thread, when a page looks wrong with the default engine; if the page then looks right, please report the document.

**Multithreading strategy** applies to the whole application:

- **Single thread** prepares one page after another and does the work of a page in one thread. Pages appear more slowly, but more of the computer stays free for other work.
- **Multithreading (load balanced)** prepares several pages at once on more processor cores.
- **Multithreading (maximum threads)** also splits the work inside a page into threads. It is the fastest for complex pages, but it keeps all cores busy.

**Prefetch pages** costs a little processor time and memory; turn it off on a very weak computer.

## Cache

Pages are first *compiled* – translated into a list of drawing steps – and then drawn. The compiled pages, the pictures of the thumbnails and the fonts are kept in memory, so the pages appear at once when you return to them.

| Setting | What it keeps | Default | Range |
| --- | --- | --- | --- |
| [Compiled page cache size](/settings/advanced#compiled-page-cache-size) | Memory for the compiled pages | 524,288 kB (512 MB) | 65,536 kB – 16,777,216 kB |
| [Thumbnail image cache size](/settings/advanced#thumbnail-image-cache-size) | Memory for the pictures of the thumbnails | 65,536 kB (64 MB) | 1,024 kB – 1,048,576 kB |
| [Cached font limit](/settings/advanced#cached-font-limit) | The number of fonts kept ready | 32 | 8 – 1024 |
| [Cached instanced font limit](/settings/advanced#cached-instanced-font-limit) | The number of fonts kept ready in a particular size | 128 | 16 – 4096 |

Larger caches make scrolling back and forth faster, but they take more memory. Lower them, when your computer has little memory or when you keep many large documents open. A compiled page, which does not fit into the whole compiled page cache, is reported as a rendering issue, which asks you to increase the cache. A thumbnail cache below 16 MB acts as 16 MB.

## Shading

Shadings are smooth color gradients of a page. Some of them are drawn as a mesh of small triangles; these settings set the balance between their quality and the speed of drawing.

| Setting | What it does | Default |
| --- | --- | --- |
| [Preferred mesh resolution ratio](/settings/advanced#preferred-mesh-resolution-ratio) | The usual size of a triangle as a part of the page: 0.02 of an A4 page (297 mm) is about 6 mm | 0.020 |
| [Minimal mesh resolution ratio](/settings/advanced#minimal-mesh-resolution-ratio) | The smallest triangle; smaller triangles are never made | 0.005 |
| [Color tolerance](/settings/advanced#color-tolerance) | How different the colors of the corners of a triangle may be before it is divided | 0.010 |

Smaller values give smoother gradients and slower drawing. When a gradient shows visible steps or stripes, lower the color tolerance or the preferred ratio; when pages with gradients are drawn too slowly, raise them.

## Developer

These options help to find out, why a page is drawn slowly, or why its text is selected, searched or read aloud in an unexpected order.

| Setting | What it does | Default |
| --- | --- | --- |
| [Developer mode](/settings/advanced#developer-mode) | Makes the three options below available; it shows nothing by itself | Off |
| [Display page compile/draw time](/settings/advanced#display-page-compile-draw-time) | Writes on each page, how long it took to compile and to draw it | Off |
| [Display text blocks](/settings/advanced#display-text-blocks) | Frames the blocks of text, which PDF4QT has found on the pages | Off |
| [Display text lines](/settings/advanced#display-text-lines) | Frames the lines of text, which PDF4QT has found on the pages | Off |

The three options can be changed only when **Developer mode** is on, and they work only while it is on. The frames and times are drawn only on the screen, never into the document or in print. See also *Rendering issues* in [Document properties, links and rendering issues](/help/document-properties).
