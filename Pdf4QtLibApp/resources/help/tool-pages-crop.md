---
title: Crop Pages
category: tools
order: 22
summary: margins, crop box and removing a crop explained
keywords: crop, trim, margins, crop box, remove crop, visible area, remove white margins
related: /tools/pages.crop, /tools/pages.resize, /help/tools
---
Crop Pages sets the visible area of pages: equal margins or a custom box. Cropping hides, it does not delete: the content outside of the crop stays in the file and **Remove Crop** brings it back.

### Step by step

1. Choose the pages in **Pages to crop**: all, the current page, the pages selected in Thumbnails or a range such as `1-3, 8`, narrowed by Even, Odd, Portrait or Landscape.
2. Click a thumbnail in the left panel to see that page in the preview. The values of the right panel are shown for this page.
3. Choose **Crop: Equal margins** and type the **Margin**: the same margin is cut from all four sides of every page. Or choose **Custom box** and type **Left**, **Top**, **Width** and **Height** of the box on the page of the preview - or drag the box and its handles in the preview. The arrows move the box, Shift and the arrows change its size, Ctrl makes the step larger.
4. **Result** shows the size of the page now and after the crop. When the pages have more sizes, a note says so: the margin is cut from every page, a custom box keeps its margins of the page of the preview on the other pages.
5. Press **Crop Pages**. The result can stay in this tab, be saved as a copy or overwrite the original file.

**Remove Crop** in the toolbar shows the whole page again: the crop box of the selected pages becomes their whole page (the media box). The dashed line of the preview is the crop, which will be removed.

### What happens to the file

- Only the crop box of each selected page changes. The content, the annotations, the form fields and the bookmarks stay as they are.
- The margins are measured from the visible area of the page as it is shown: a page turned by 90° gets its left margin at its left side on the screen. A page with a different unit length (UserUnit) gets the same margin in millimeters.
- A page without its own bleed, trim or art box uses the crop box for them: they follow the new crop box. A page, which has them, keeps them.
- The thumbnail stored in the file for a cropped page is removed, it would not match the page.
- A crop, which would leave less than 3 points of a page, or a custom box outside of the page is refused: the right panel says the largest value, which fits.
- A signed document stays signed: the change is saved after the signed part of the file. A certified document can be changed only as a copy.

> [!NOTE] Cropping hides, it does not delete
> The content outside of the crop box stays in the file. To remove content for good, use Redact Content.

### Assistants

The tool is also available to AI assistants through the action `pages.crop` (see [AI tools](/help/ai-tools)).
