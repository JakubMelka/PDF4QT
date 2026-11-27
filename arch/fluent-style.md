# Fluent style (Pdf4QtLibFluent)

The design is the Figma file of the GUI overhaul. Page *Widgets* (one section per family of controls): the drawings and the *Geometry* lines are binding, the *Implementation* notes are guidance - several of them do not work against the Qt sources. Page *Components*: specification, decisions and open findings in [GUI_overhaul/components.md](../GUI_overhaul/components.md) and [components_review.md](../GUI_overhaul/components_review.md).

## Colors and the leak palette

- `PDFThemeColors` has one enumeration value per variable of the Figma collection *Color*, in the order of Figma; a theme is a table of their values (built-in `light` and `dark`, more by `PDFThemeManager::registerTheme`). Widgets take colors from `PDFThemeManager`, never by a cast of `QWidget::style()`.
- **The style never reads a QPalette.** The palette of the application is filled with the leak color violet `#A000FF`, so a system color leaking into the interface is seen at once: a violet pixel is a bug. What Qt draws by itself from the palette (text of a line edit, rich text, calendar, status bar message, tooltip, message box) has a subclass in the library, which writes the theme colors into its own palette (`PDFThemeWidgetHelper::bindTextPalette`). Known gap: the HTML parser of Qt takes the color of a link from the palette of the application, so links of a rich text are violet.
- Icons of the system icon theme are drawn by the platform from the palette; the style replaces them by an icon of the design or draws nothing.

## Threads

The library works only in the GUI thread: the painters and the icon engine read the active theme, keep pixmaps in `QPixmapCache` and share parsed SVG renderers without a lock. A picture made by a worker (a rendered page) is passed as a pixmap / image.

## The style

- `PDFWidgetStyle : QCommonStyle` dispatches each virtual function to one area per family of controls; nothing is drawn by `QCommonStyle`. Variants of a control are dynamic properties set by `PDFWidgetStyleProperties::set`.
- The focus ring is shown only after the keyboard: fields draw it inside, buttons get it from one `QFocusFrame` owned by the style, so a widget keeps the size of its shape in the design.
- Popups (menu, combo box list, calendar, tooltip) are translucent windows with margins for the shadow; their position is corrected at the show event so the panel keeps the edge by which Qt anchored it; mouse events in the margins are passed to what lies below. The Qt tooltip (`QTipLabel`) is an opaque panel.
- An area with the property `overlayScrollBars` has its scroll bars over the edge of the viewport: the page area reaches into the padding of the workspace by the width of the bar, so the page keeps the width of the frames.
- `PDFMessageBox` paints its own frameless card and covers its parent (or its scrim target) by a scrim; a dialog of the design is a message box without the badge, with own content and own action buttons.
- Icons are the SVG files of Figma, unchanged, never scaled as a picture: their strokes keep 2 px at each size. The font Inter (SIL OFL) and JetBrains Mono are bundled; sizes are in pixels.

## Where Qt works against the design

Each place is handled in the style and covered by `UnitTestsWidgetStyle` - check them after an upgrade of Qt:

- `QToolBarLayout` takes the positions of its items from transposed margins.
- `QTreeView` animates its branches from pictures filled by `palette().base()` - the animation is off; `QTreeView::updateGeometries` resets the margins of the viewport.
- `QHeaderView` derives the minimal section from `PM_HeaderMargin`.
- Cell editors come from the default item editor factory of Qt (stock widgets drawn from the palette): the style installs its own factory as the default (unless the application has one; `unpolish` restores Qt's). The default delegate places the editor into the rectangle of the text, so the editor is frameless and the view paints the cell below it. The style never replaces the delegate of a view (it would cut the editors and connections of the application off).
- `QProgressBar` has a minimal size hint higher than the bar - fixed thickness set in `polish`.
- `QPlainTextEdit` has no line height - an own document layout.
- `QGraphicsView` fills its viewport from the palette (`setupViewport` turns `autoFillBackground` on, `CacheBackground` fills the cached pixmap by the brush of the background role): the viewport is drawn bg/track by `PE_Widget`, its palette gets bg/track as Base / Window (polish, unpolish, theme change).
- `QAbstractSpinBox` writes the text of a value with the signals of its editor blocked: the mixed state of a spin box follows the text of the editor.
- A class font (`QApplication::setFont(font, className)`) is inherited by the children, so containers have none.
- The painter of a widget has the direction of the application, not of the widget: a text aligned to the leading side of a widget is aligned absolutely.

## Components

- Each component has one drawing function and its size computation in `PDFComponentPainter` - the only place of their geometry constants - called by the widget, the gallery, the delegates and the tests. Nested elements (star, tag, menu) are real child widgets; the painter draws them only for the gallery and the delegates.
- The text of a button is data: `&` is no mnemonic. The exclusivity of a choice comes only from an exclusive `QButtonGroup`, never from Qt's auto-exclusivity (it groups all buttons below the parent). A choice group is one stop of Tab; the arrows move the focus.
- Each drawing function sets the direction of its state on the painter while it draws and uses absolute alignments only, so the result does not depend on the caller.
- A text which is data (file name, path, name, value, content of the document) keeps the direction of the whole text and is elided at one place (`drawElidedText`); texts of the interface have the direction of the component. A folder (`TextKind::Path`) is elided in the middle by its folders, the root and the last folder stay; a detail after it ("C:\Tender · 17 pages") is a text of the interface, always whole, at the trailing side of the folder (`drawElidedTextWithDetail`). Progress fills from the leading edge.
- Disabled controls use `text/disabled` / `icon/disabled`, never opacity. The full `bg/accent` fill belongs only to the primary button; a selected chip is `bg/surface` + `bg/accent-subtle` with `text/accent`. A single choice of a few options is a segmented control.
- The page range parser of the library reimplements `PDFClosedIntervalSet::parse`, because the library does not depend on Pdf4QtLibCore; pages are numbered from 1.
- Sections of a panel are `PDFExpanderCard` (heading, Tag, link Expand / Collapse, summary of a collapsed card); breadcrumbs without routes are `PDFPathBar` (shortened in the middle, the first and the last segment stay; a hostile path keeps widgets for the first and the last 255 segments only, every index is one of the list given). A field of the values of several objects has the mixed state (`setMixed`): "Mixed" in the color of a value (also with Qt's `setPrefix` / `setSuffix`), announced as the value / description "Mixed" (a spin box presents no held number), no signal of the library on entering or leaving it; the library never emits a signal of a Qt base class, which Qt emits itself (Qt uses the object after it; the lifetime test deletes inside such a signal).
- Numeric fields of a panel (`PDFSpinBox`, `PDFDoubleSpinBox`): connect `valueEntered` and write its value - never `valueChanged` (Qt emits it while typing with keyboard tracking and not for an unchanged value) nor `editingFinished` (it comes also without an entry). `valueEntered` comes for every commit of the user - Enter, leaving the field after typing, a step (keys, wheel, buttons, menu) - also for an unchanged value and from the mixed state; never for `setValue` / `setMixed` / `stepBy` of the program or a field left untouched. It is emitted last, after Qt's handling of the whole event (a slot may delete the field); `commitEntry()` commits a typed text before a save.
- Window buttons draw every platform at run time and take hover and press also from the window, so the application can answer `WM_NCHITTEST` with `HTMAXBUTTON` for the Snap Layouts of Windows ([components.md](../GUI_overhaul/components.md) 4.9).

## Delegates

Items of views with data of a model (thumbnails, bookmarks, notes, search results, Recent, shortcuts) are drawn by the delegates of the library through `PDFComponentPainter`. A delegate never replaces anything of the style and the style never replaces it (in a tree the style leaves the area of the branches unfilled). A delegate watches its views by event filters and `QPointer`, drops a view at its `destroyed` (sent before the children are deleted) and never calls `viewport()` of a dying view; it gives its size without reading picture roles (a model may render a page there). Delegates draw the rows of flat lists; the view gives the spacing. Row presets of `PDFTreeView` track the pressed row also against a lost release and draw the focus ring around the whole row. A toggle action of a row (`Action::checkable`: eye, lock) is drawn by `paintToggleActions` permanently while on, otherwise only under the pointer or with the focus ring; with a role it writes its state into the model.

## Workflow

After a change of the style run `UnitTestsWidgetStyle` and export the gallery (`Pdf4QtFluentGallery --export`) in both themes. A new control gets its samples in the gallery - the gallery is also what the leak detector renders and what `UnitTestsFluentLifetime` destroys in every order. Samples drawn by a function hold no plain pointer to a widget (`QPointer`) and take the style at paint time.
