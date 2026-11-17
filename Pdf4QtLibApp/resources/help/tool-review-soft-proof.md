---
title: Soft Proofing
category: tools
order: 87
summary: Simulate the colors of a print and check the gamut.
keywords: soft proof, soft proofing, gamut, gamut check, icc profile, color profile, cmyk simulation, out of gamut, rendering intent
related: /tools/review.soft-proof, /tools/review.output-preview, /help/color-management, /help/tools
---
Soft Proofing shows the document in the tab as it would look printed with a CMYK profile. Gamut Check paints the colors, which the profile cannot print, in the alarm color. Nothing in the document changes.

### Step by step

1. Choose the profile of the press or the printer. The menu lists the output intents of the document and the CMYK profiles of the system and of the profile folder of Settings › Color management. An output intent, which cannot be used (an RGB or gray profile, a missing or damaged profile), is shown with its reason.
2. Choose the rendering intent. Relative colorimetric keeps the colors, which can be printed, exact; Absolute colorimetric also simulates the white of the paper.
3. Turn Soft Proof and Gamut Check on or off.
4. Close ends the proof. Keep Proofing On leaves it on after the tool closes.

### Only on the screen

The proof belongs to the tab: printing, exports, the thumbnails and the other tools never use it, and it is not saved. With Keep Proofing On the status bar shows the profile; a click on it opens Soft Proofing again, where you can turn the proof off.

### When it is not available

PDF4QT has no built-in CMYK profiles. Without a CMYK profile, add a profile folder in Settings › Color management. The Generic color engine cannot simulate another device: choose Little CMS 2 in Settings › Color management.
