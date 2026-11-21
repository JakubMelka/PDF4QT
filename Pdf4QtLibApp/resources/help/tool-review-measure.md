---
title: Measure
category: tools
order: 84
summary: Measure distances, perimeters, areas and angles on the pages.
keywords: measure, dimensions, distance, length, area, perimeter, angle, scale, ruler, calibrate, floor plan, drawing, takeoff
related: /tools/review.measure, /help/tools
---
Measure measures distances, perimeters, areas and angles on the pages of a drawing in the scale you choose. The measurements may lie on different pages; each one lies on the page of its first point.

### Step by step

1. Choose the kind in the toolbar: Horizontal, Vertical or Linear length, Perimeter or Area of a shape (click its corners and finish on the first point), Rectangle Perimeter or Rectangle Area (two opposite corners), or Angle (three points, the second one is the vertex).
2. Choose the scale of the drawing in Scale: a preset, Calibrate Scale… (pick two points of a known distance and enter the real distance), or Custom Scale…. Manage Presets… adds, edits and removes the presets.
3. Click the points on the page. The points snap to the corners and edges of the drawing. The right button starts the measurement over, C shows the large cross.
4. The measurements are listed in the left panel. Click a row to show its measurement on the page, its × removes a temporary one, Clear removes all temporary ones.
5. Export CSV saves the list. Save as Annotations adds the temporary measurements to the document as measurement annotations in one step of Undo.

Esc ends the measurement you are drawing, then the drawing mode or the calibration, then the selection of a row; when none of them is active, Esc closes the tool. Closing the tool with **Close** or Esc with temporary measurements or a measurement being drawn asks first, also when you close the tab or the window or open another tool: **Keep Measuring**, **Save and Close** (saves them as annotations, when the document allows annotations) or **Discard Measurements**. When you close the tab or the window, the measurements are saved only when the close goes on: a Keep answer in another tool cancels it and they stay temporary; **Cancel** at the following question about unsaved changes keeps the window open, the annotations stay in the document and Undo removes them. When some measurements cannot be stored as annotations, the stored ones stay in the document and the tool stays open with the others – the tab, the window or the other page is not closed.

### Temporary measurements and annotations

Temporary measurements live only while the tool is open; they always use the scale of now. Annotations in the document adds each new measurement to the document at once, one step of Undo each. Measurement annotations of the document, also those of other programs, are listed with the tag Annotation and other PDF readers show them too. In a PDF/A document the annotations are written without transparency, as PDF/A requires: the label colors are mixed with white and an area is drawn by its outline only; the document stays PDF/A.

A locked document, a document whose permissions do not allow new annotations and a certified document that does not allow them keep the measurements temporary; Export CSV works. Enter Owner Password… unlocks the permissions, when the document has an owner password.

### Scale

The scale is remembered by PDF4QT for each document that has a file and an identifier; it is never written into the document. A document without them uses its scale until it is closed. A document that defines the scale of its drawing (a viewport with a measure) uses that scale; a measurement inside such a viewport always uses the scale of the viewport. Otherwise the default scale is used, Real size (1:1) until you change it.

A preset and the default scale have a name and an optional description; the Scale menu and Manage Presets… show the description of a preset beside its ratio. Calibrate Scale… and Custom Scale… save a scale with a name as a new preset.

### CSV

The CSV file has the columns Page; Type; Value; Unit; Text; Scale, separated by semicolons, in UTF-8, with a dot as the decimal separator. It contains the measurement annotations of the document and the temporary measurements, ordered by pages.

### Assistant

The assistant (MCP) uses the tool as `review.measure`: it lists the measurement annotations of the pages and the scales of their viewports, exports them as CSV and adds new measurement annotations.
