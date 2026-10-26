---
title: Color management
category: settings
order: 17
summary: How PDF4QT converts the colors of documents for your screen and the printer - ICC profiles, rendering intents and the settings of Settings › Color management.
keywords: color management, cms, icc, icc profile, color profile, little cms, lcms, generic, rendering intent, perceptual, relative colorimetric, absolute colorimetric, saturation, accuracy, chromatic adaptation, white point, bradford, cat02, cat97, xyz scaling, black point compensation, white paper, output intent, pdf/x, pdf/a, srgb, adobe rgb, cmyk, gray, profile folder, color accuracy, prepress
related: /settings/color-management, /help/settings, /help/color-modes, /help/settings-advanced
---
[Settings › Color management](/settings/color-management) decides how the colors of a document are converted to the colors of your screen and of your printer.

> [!TIP] Most people never change these settings
> The defaults show the colors of ordinary documents correctly. Change them only when you work with print production (prepress), proofs or calibrated color profiles, or when someone who knows your color workflow tells you to.

## What color management is

A document describes its colors by numbers, but the same numbers look different on different devices. An RGB color 255, 0, 0 is a different red on a cheap monitor, on a wide-gamut monitor and on paper. A CMYK color – the inks of a printing press – has no exact equal on a screen at all.

An **ICC profile** describes a device or a color space: which real color each of its numbers means. With the profile of the colors of the document and the profile of your screen, the color management converts every color so that it looks as close as possible to what the author meant. The settings say which engine converts the colors, which profiles it uses and how it handles colors, which the screen cannot show.

The conversion applies to the pages on the screen and to printing. It never changes the document.

## Color management

| Setting | What it does | Default |
| --- | --- | --- |
| [Color management system](/settings/color-management#color-management-system) | The engine, which converts the colors: **Generic** or **Little CMS** (its version is shown) | Little CMS |
| [Rendering intent](/settings/color-management#rendering-intent) | How colors outside of the range of the screen are handled | Automatic |
| [Accuracy](/settings/color-management#accuracy) | Precision of the conversion: Low, Medium or High | Medium |
| [Color adaptation method](/settings/color-management#color-adaptation-method) | How colors defined with another white point are adapted: None, XYZ scaling, CAT97 matrix, CAT02 matrix or Bradford method | Bradford method |
| [Black point compensation](/settings/color-management#black-point-compensation) | Maps the deepest black of the document to the deepest black of the screen | On |
| [White paper color transformed](/settings/color-management#white-paper-color-transformed) | Shows the paper in the white of the profile, not in pure white | Off |
| [Consider document output intents](/settings/color-management#consider-document-output-intents) | Uses the color profile, which the document brings for its output | On |

**Generic** converts the colors by simple formulas without ICC profiles. It is a little faster, but the colors are less exact, and the ICC profiles embedded in documents are not used. With Generic, all other settings of the section are disabled. **Little CMS** is the open-source color engine, which uses ICC profiles; keep it, unless you need the speed.

**Accuracy** – High gives the most exact colors and needs more memory and time when pages are drawn; Low is the fastest and least exact. Medium is a good balance.

**Color adaptation method** is used for colors, which the document defines with the white point of another light (for example the white of a lamp of 5000 K instead of daylight). The method shifts them so that the white looks white on your screen. Bradford is the common choice of color experts; the others give slightly different results.

**Black point compensation** keeps the details in dark areas: without it, the darkest shades of a document, which the screen cannot show darker, would merge into one black.

**White paper color transformed** shows the white of the page as the Device RGB color profile sees it, including the tint of its white point – for example a slightly yellowish white of a profile with the warmer white D50. Off, the paper is always pure white.

**Consider document output intents** – documents made for printing (PDF/X) or for archiving (PDF/A) often contain the ICC profile of the device, for which they were made (its *output intent*). With this setting on, PDF4QT uses that profile instead of the device profile of the same kind chosen below – most often a CMYK profile of a printing press replaces the Device CMYK color profile; the other kinds keep your profiles. Turn it off to see the document with your own profiles.

## Rendering intents

A screen cannot show every color of a document, and a printer cannot print every color of a screen. The rendering intent decides what happens with the colors, which are out of range:

| Intent | What it does | Good for |
| --- | --- | --- |
| Automatic | Uses the intent, which the document gives to each picture and graphic; perceptual, when it gives none | All documents (recommended) |
| Perceptual | Shifts all colors a little, so that their relations are kept and nothing is clipped | Photographs |
| Relative colorimetric | Keeps the colors, which can be shown, exactly and moves the others to the nearest possible color; white stays white | Logos, brand colors, proofs |
| Absolute colorimetric | Like relative colorimetric, but it also imitates the white of the source, for example the tint of a paper | Simulating paper in proofs |
| Saturation | Keeps colors vivid and saturated, accuracy comes second | Charts and business graphics |

Any other intent than Automatic overrides the intents of the document for everything.

The intents differ only where a profile describes them separately – typically CMYK profiles of printing presses. The simple built-in profiles (sRGB and the other gray and RGB profiles of the lists) treat perceptual, relative colorimetric and saturation alike, so with them you see a difference mostly with absolute colorimetric.

## Color profiles

| Setting | What it does | Default |
| --- | --- | --- |
| [Output color profile](/settings/color-management#output-color-profile) | The color space of your screen | sRGB |
| [Device gray color profile](/settings/color-management#device-gray-color-profile) | The meaning of the gray colors of documents, which have no profile | Gray D65, γ = 2.2 |
| [Device RGB color profile](/settings/color-management#device-rgb-color-profile) | The meaning of the RGB colors of documents, which have no profile | sRGB |
| [Device CMYK color profile](/settings/color-management#device-cmyk-color-profile) | The meaning of the CMYK colors (inks) of documents, which have no profile | The first CMYK profile of your system |
| [Additional profile directory](/settings/color-management#additional-profile-directory) | A folder with your own ICC profiles | None |

Most colors of ordinary documents have no profile of their own – they are plain gray, RGB or CMYK numbers. The three device profiles say what these numbers mean. Colors with an embedded ICC profile always use their own profile.

The lists offer the built-in profiles of PDF4QT, the profiles installed in your system (on Linux the folders inside `/usr/share/color/icc`) and the profiles of the additional folder:

- **Gray** – Gray D65, D50 or D93 (the white of daylight, of a lamp of 5000 K and of a cool blue white), each with the gamma 2.2 or linear.
- **RGB** – sRGB (the standard of screens and of the web), HDTV (ITU-R BT.709), Adobe RGB 1998, PAL / SECAM, NTSC, Adobe Wide Gamut RGB and ProPhoto RGB.
- **CMYK** – only the profiles of your system and of your folder. When there is none, the list offers **None (simple conversion)**: the inks are converted by a simple formula.
- **Output** – sRGB only, which suits almost every screen.

Choose the CMYK profile of your printing press (for example a FOGRA or GRACoL profile) to see how CMYK documents will look in print. Choose the RGB profile, which the author of a document used, when its RGB colors look too dull or too strong.

While the lists are read, a chosen profile is shown as *Loading profiles…*. A profile, which no longer exists – for example from a removed folder – is shown as *(not available)*. Choose another profile – until then the colors of its kind are converted by a simple formula.

## Your own profiles

**Additional profile directory** adds the ICC profiles (files `*.icc`) of a folder of your choice to the lists. Click **Browse…** to choose the folder. The folder is read again whenever you open Color management.

To keep PDF4QT fast and safe, a folder is not read, when:

- it is on another computer of the network,
- it has more than 1000 files or more than 64 profiles,
- a profile is larger than 8 MB, or all profiles together are larger than 64 MB,
- it does not exist or it is not a folder.

The row then says why the folder is not read, and only the built-in and the system profiles are offered.
