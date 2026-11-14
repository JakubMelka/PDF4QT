---
title: Convert Images to Grayscale
category: tools
order: 32
summary: Convert the color images of a document to gray.
keywords: grayscale, gray, black and white, convert images, color images, remove color, monochrome, devicegray
related: /tools/optimize.grayscale, /help/tools
---
Convert Images to Grayscale converts the color images of a PDF to gray images (DeviceGray). The rest of the page – text, vector drawings, annotations – keeps its colors. *View › Page colors › Grayscale* changes only the display, this tool changes the file.

### Which images are converted

An image is a color image by its declared color space: DeviceRGB, DeviceCMYK, Lab, an ICC profile with three or four components, Indexed over a color space, Separation and DeviceN. Images that are already gray (DeviceGray, CalGray, an ICC profile with one component) or bitonal (1 bit, image masks) are skipped; the list shows them as *already gray* or *bitonal*. A color image, whose pixels happen to be gray, is converted too – it only gets smaller.

- **All · Color · Excluded** show all images, the color images or the excluded ones.
- **Exclude** (or *Convert this image* in Selected image) keeps the selected images in color.
- Inline images (*inline · kept*) are a part of the content of the page and are never changed.
- An image, which an annotation appearance uses too (*in an annotation · kept*), keeps its colors.

### Settings

- **Goal** and **Options** are the same as in Optimize Images.
- The profile *Grayscale images* says how the converted images are stored: the algorithm, the target resolution (0 keeps the resolution – the default), the resampling filter, the JPEG quality (85 by default, the number written into the file), the JPEG 2000 rate and the PNG predictor.
- *Keep original if larger* chooses only the smallest gray encoding: a color image is always converted, unless you exclude it.

### Step by step

1. The tool measures the result at once; the rows show the size of each converted image and the Summary the size of all color images before and after.
2. The canvas compares the selected image BEFORE and AFTER.
3. Press Convert to Grayscale. The result dialog says how many images were converted, excluded and skipped.

### What happens to the file

- The images of all layers are converted; the appearances of annotations and the glyphs of Type 3 fonts are not.
- Spot colors (Separation, DeviceN) of an image are converted to gray by the color management.
- A declaration of PDF/A or PDF/UA is removed and the result says so.
- A signed document can be saved only as a copy written whole; the signatures of the copy are no longer valid.
