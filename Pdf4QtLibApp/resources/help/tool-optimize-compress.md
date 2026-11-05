---
title: Compress
category: tools
order: 5
summary: options and presets explained
keywords: compress, shrink, smaller, reduce size, optimize, file size, jpeg, dpi, downsample
related: /tools/optimize.compress, /help/tools
---
Compress makes a PDF smaller in one step. You do not have to understand image compression or the inner structure of a PDF: pick a preset, look at the measured result and press Compress. Under the hood the tool runs Optimize Images and Optimize Structure together with settings that match the preset.

### Presets

Every preset decides how far images are reduced and how thoroughly the file structure is cleaned. The measured size after compression is shown before you confirm, so you can switch presets until the result fits.

| Preset | Images | Structure | Typical use |
| --- | --- | --- | --- |
| Smaller file | Downsampled to screen resolution, stronger JPEG | Full clean-up | E-mail attachments, upload limits |
| Balanced | Reduced where no difference is visible | Full clean-up | Default for most documents |
| Best quality | Kept as they are | Full clean-up (lossless) | Print, archiving |
| Custom | Your settings in the Custom panel | Your settings in the Custom panel | When a preset is not enough |

### Step by step

1. Choose the document above. The tool opens in the document’s tab with the Balanced preset selected.
2. Pick a preset. The page shows the size before and the measured size after, with the saving in percent.
3. The right panel shows which parts of the file take up the most space.
4. Press Compress. The summary shows the measured result and lets you keep it in this tab, save it as a copy or overwrite the original file.

### What happens to the file

- Images are downsampled to the preset resolution and recompressed; images that would get larger are kept.
- Unused objects are removed, identical objects are merged and all streams are recompressed.
- The file is written whole, with every setting: its old object streams, cross-reference streams and linearization data (Fast Web View) are not carried over.
- Text, forms, annotations, outline and metadata stay exactly as they are, except for a declaration of PDF/A or PDF/UA: the result is not checked against the standard, so the declaration is removed from its metadata and the result says so.
- A signed document can be saved only as a copy. The signatures of the copy are no longer valid.
- CMYK and Lab images are converted to RGB when they are recompressed.

> [!WARNING] When the file cannot be made smaller
> If the chosen preset does not reduce the size, Compress says “File cannot be reduced with these settings” and writes nothing. Try Smaller file, or look at the size breakdown in the right panel.

> [!NOTE] Memory
> The tool works on several images at once. With [Multithreading strategy](/settings/advanced#multithreading-strategy) set to Single thread, it holds one image at a time: it is slower, but it needs less memory. The setting also slows down the drawing of pages until you change it back.

> [!TIP] Fine control
> Optimize Images and Optimize Structure expose every setting behind the presets: color mode, DPI, JPEG quality, resampling filter, object merging and more.

### Command line

The same optimization is available in PdfTool for scripts and batch jobs. The command overwrites its input file:

```
pdftool optimize --opt-images input.pdf
pdftool optimize --opt-images --opt-images-color-dpi 150 --opt-images-gray-dpi 150 input.pdf
```
