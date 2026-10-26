// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pdfaccenthue.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpainter.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfcomponentwindow.h"
#include "pdffluentwidgets.h"
#include "pdflockeddocumentpicture.h"
#include "pdfmessagebox.h"
#include "pdfpreviewpainter.h"
#include "pdfreferencepointpicker.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdftooltip.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include "gallerypages.h"

#include <QtTest>

#include <QAccessible>
#include <QButtonGroup>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QClipboard>
#include <QTextDocument>
#include <QCommonStyle>
#include <QContextMenuEvent>
#include <QDialog>
#include <QFile>
#include <QFocusFrame>
#include <QGraphicsEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMetaEnum>
#include <QPainter>
#include <QPixmapCache>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QMimeData>
#include <QScrollArea>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QRegularExpression>
#include <QTimer>
#include <QSet>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QStyleOptionTab>
#include <QToolTip>
#include <QTreeView>
#include <QTranslator>
#include <QVBoxLayout>

#include <functional>
#include <limits>
#include <thread>
#include <type_traits>

using Color = pdf::PDFThemeColors::Color;
using Painter = pdf::PDFComponentPainter;

// A property row cannot be constructed with a flag before its parent: a pointer
// to the parent in the place of the flag was taken as true (review C5)
static_assert(!std::is_constructible_v<pdf::PDFPropertyRow, QString, QString, bool, QWidget*>);
static_assert(!std::is_constructible_v<pdf::PDFPropertyRow, QString, QString, bool>);
static_assert(std::is_constructible_v<pdf::PDFPropertyRow, QString, QString, QWidget*>);
static_assert(std::is_constructible_v<pdf::PDFPropertyRow, QString, QString, pdf::PDFPropertyRow::Editability, QWidget*>);

/// Translator of the test, which translates a few texts of the rows
class TestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(context);
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);

        const QByteArrayView source(sourceText);
        if (source == "No longer exists")
        {
            return QStringLiteral("Gone");
        }
        if (source == "Current version")
        {
            return QStringLiteral("Installed");
        }
        return QString();
    }

    virtual bool isEmpty() const override { return false; }
};

/// Translator of the test of the cards: the pattern of the composed
/// descriptions and the name of a picture
class CardTestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(context);
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);

        const QByteArrayView source(sourceText);
        if (source == "%1, %2")
        {
            return QStringLiteral("%1; %2");
        }
        if (source == "High contrast")
        {
            return QStringLiteral("Strong contrast");
        }
        return QString();
    }

    virtual bool isEmpty() const override { return false; }
};

/// Translator of the test of the composite widgets: a few default texts
class PanelTestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(context);
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);

        const QByteArrayView source(sourceText);
        if (source == "Pages")
        {
            return QStringLiteral("Seiten");
        }
        if (source == "Then continue with")
        {
            return QStringLiteral("Dann weiter mit");
        }
        if (source == "Moderate")
        {
            return QStringLiteral("Mittel");
        }
        if (source == "Certificate chain")
        {
            return QStringLiteral("Zertifikatskette");
        }
        return QString();
    }

    virtual bool isEmpty() const override { return false; }
};

/// Row of the test: it draws its background by the state and has a fixed size
class TestRow : public pdf::PDFRowButton
{
public:
    explicit TestRow(QWidget* parent = nullptr) :
        pdf::PDFRowButton(parent)
    {

    }

    void setSubtitle(const QString& subtitle)
    {
        replaceAccessibleDescription(m_subtitle, subtitle);
        m_subtitle = subtitle;
    }

    mutable int paintCount = 0;
    mutable Painter::State lastState;

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const Painter::State& state) const override
    {
        ++paintCount;
        lastState = state;

        if (state.isPressed())
        {
            pdf::PDFStylePainter::fillRect(painter, QRectF(rect), Color::BgPressed);
        }
        else if (state.isHovered())
        {
            pdf::PDFStylePainter::fillRect(painter, QRectF(rect), Color::BgHover);
        }

        if (state.hasFocusRing())
        {
            Painter::paintFocusRingInside(painter, QRectF(rect), pdf::PDFStylePainter::Radii(8.0));
        }
    }

    virtual QSize getContentSizeHint() const override { return QSize(300, 48); }

private:
    QString m_subtitle;
};

/// Row of the test, whose size includes its nested widgets (as the rows of the steps 2 and 3)
class TrailingRow : public TestRow
{
public:
    explicit TrailingRow(QWidget* parent = nullptr) :
        TestRow(parent)
    {

    }

protected:
    virtual QSize getContentSizeHint() const override { return QSize(200 + getTrailingRect().width(), 48); }
};

/// Row of the test, which is not interactive (a text)
class LabelRow : public TestRow
{
public:
    explicit LabelRow(QWidget* parent = nullptr) :
        TestRow(parent)
    {

    }

    virtual bool isInteractive() const override { return false; }
};

/// Free-standing button of the test with the ring outside, which does not set its radius
class BareOutsideButton : public pdf::PDFComponentButton
{
public:
    explicit BareOutsideButton(QWidget* parent = nullptr) :
        pdf::PDFComponentButton(FocusRing::Outside, parent)
    {

    }

protected:
    virtual void paintContent(QPainter*, const QRect&, const Painter::State&) const override { }
    virtual QSize getContentSizeHint() const override { return QSize(40, 24); }
};

/// Card of the test
class TestCard : public pdf::PDFCardButton
{
public:
    explicit TestCard(QWidget* parent = nullptr) :
        pdf::PDFCardButton(parent)
    {

    }

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const Painter::State& state) const override
    {
        pdf::PDFStylePainter::fill(painter, QRectF(rect), pdf::PDFStylePainter::Radii(12.0), state.isOn() ? Color::BgAccentSubtle : Color::BgSurface);
    }

    virtual QSize getContentSizeHint() const override { return QSize(154, 48); }
};

/// Widget, which draws a painter function in its paint event. A function of
/// PDFComponentPainter compared with its widget is rendered through it, so both are
/// drawn on the same kind of paint device: the raster engine of Qt draws the glyphs
/// of a FreeType font in the default format of the font engine (the subpixel
/// antialiasing of fontconfig on Linux) on a widget, but in grayscale on a QImage
/// (QRasterPaintEngine::drawCachedGlyphs). On Windows both are the same.
class PainterCanvas : public QWidget
{
public:
    explicit PainterCanvas(const std::function<void(QPainter&)>& paint, QSize size) :
        m_paint(paint)
    {
        // Only the drawing over the fill of renderWidget (no background of a window)
        setAttribute(Qt::WA_DontShowOnScreen, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        resize(size);
    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        m_paint(painter);
    }

private:
    std::function<void(QPainter&)> m_paint;
};

/// Model of the shortcut map of Qt on Windows for one key press: the key of a
/// layout as QWindowsKeyMapper::updatePossibleKeyCodes stores it and the
/// candidates of QWindowsKeyMapper::possibleKeyCombinations, which
/// QShortcutMap::createNewSequences matches against the shortcuts, with the
/// retry of QShortcutMap::nextState without the keypad modifier
/// (qtbase/src/plugins/platforms/windows/qwindowskeymapper.cpp of Qt 6.11.2:
/// toKeyOrUnicode 555-581, updatePossibleKeyCodes 656-717,
/// possibleKeyCombinations 1347-1397; qtbase/src/gui/kernel/qshortcutmap.cpp
/// 339-341). The layouts are not installed, the test gives the characters of
/// their levels.
class WindowsShortcutCandidates
{
public:
    WindowsShortcutCandidates() = delete;

    /// A key of a layout: characters of its levels (0 - none), the Qt key of
    /// the table KeyTbl of Qt for the virtual key (winceKeyBend), extended key
    struct LayoutKey
    {
        quint32 virtualKey = 0;
        char16_t base = 0;          ///< without the modifiers, qtKey[0]
        char16_t shift = 0;         ///< Shift, qtKey[1]
        char16_t altGr = 0;         ///< Ctrl+Alt, qtKey[6]
        char16_t altGrShift = 0;    ///< Ctrl+Alt+Shift, qtKey[7]
        int keyTable = 0;           ///< KeyTbl[virtualKey]
        bool extended = false;      ///< extended key (the Enter of the keypad)
    };

    /// toKeyOrUnicode: the character uppercased, a control character or none
    /// is the key of the table
    static quint32 toKey(char16_t character, int keyTable)
    {
        quint32 code = character ? quint32(QChar(character).toUpper().unicode()) : 0;
        if (code < 0x20 || code == 0x7F)
        {
            code = quint32(keyTable);
        }
        return code == quint32(Qt::Key_unknown) ? 0 : code;
    }

    /// The candidates of possibleKeyCombinations. The levels Ctrl, Ctrl+Shift,
    /// Alt and Alt+Shift (qtKey[2..5]) are left empty: each of them only adds a
    /// candidate of its own, the base candidate is always there.
    static QList<QKeyCombination> getCandidates(const LayoutKey& key, Qt::KeyboardModifiers keyMods)
    {
        static const Qt::KeyboardModifiers modsTable[9] = {
            Qt::NoModifier, Qt::ShiftModifier, Qt::ControlModifier, Qt::ControlModifier | Qt::ShiftModifier,
            Qt::AltModifier, Qt::AltModifier | Qt::ShiftModifier, Qt::AltModifier | Qt::ControlModifier,
            Qt::AltModifier | Qt::ShiftModifier | Qt::ControlModifier, Qt::NoModifier
        };

        quint32 qtKey[9] = { };
        qtKey[0] = toKey(key.base, key.keyTable);
        qtKey[1] = toKey(key.shift, key.keyTable);
        qtKey[6] = toKey(key.altGr, key.keyTable);
        qtKey[7] = toKey(key.altGrShift, key.keyTable);

        // The fall back key of the layouts without the Latin characters
        quint32 fallbackKey = quint32(key.keyTable);
        if (!fallbackKey || fallbackKey == quint32(Qt::Key_unknown))
        {
            fallbackKey = 0;
            if (key.virtualKey != qtKey[0] && key.virtualKey != qtKey[1] && key.virtualKey < 0x5B && key.virtualKey > 0x2F)
            {
                fallbackKey = key.virtualKey;
            }
        }
        qtKey[8] = fallbackKey;

        QList<QKeyCombination> result;
        const quint32 baseKey = qtKey[0];
        if (baseKey == quint32(Qt::Key_Return) && key.extended)
        {
            result << QKeyCombination(keyMods, Qt::Key_Enter);
            return result;
        }

        if (baseKey == quint32(Qt::Key_Tab) && keyMods.testFlag(Qt::ShiftModifier))
        {
            result << QKeyCombination(keyMods & ~Qt::ShiftModifier, Qt::Key_Backtab);
        }

        // The base key is always valid
        result << QKeyCombination::fromCombined(int(baseKey) + int(keyMods));

        for (int i = 1; i < 9; ++i)
        {
            const Qt::KeyboardModifiers neededMods = modsTable[i];
            const quint32 levelKey = qtKey[i];
            if (levelKey && levelKey != baseKey && (keyMods & neededMods) == neededMods)
            {
                const Qt::KeyboardModifiers missingMods = keyMods & ~neededMods;
                const QKeyCombination matchedKey = QKeyCombination::fromCombined(int(levelKey) + int(missingMods));
                auto it = std::find_if(result.begin(), result.end(), [levelKey](const QKeyCombination& combination) { return quint32(combination.key()) == levelKey; });
                if (it == result.end())
                {
                    result << matchedKey;
                }
                else if (missingMods > it->keyboardModifiers())
                {
                    *it = matchedKey;
                }
            }
        }

        return result;
    }

    /// Returns true, if the shortcut map matches the combination for the key press
    static bool isMatched(const QKeyCombination& combination, const LayoutKey& key, Qt::KeyboardModifiers keyMods)
    {
        for (const QKeyCombination& candidate : getCandidates(key, keyMods))
        {
            if (candidate == combination)
            {
                return true;
            }

            // The retry without the keypad modifier
            if (keyMods.testFlag(Qt::KeypadModifier) && QKeyCombination::fromCombined(candidate.toCombined() & ~int(Qt::KeypadModifier)) == combination)
            {
                return true;
            }
        }

        return false;
    }

    /// Returns the candidates as a text (the message of a failure)
    static QString describe(const LayoutKey& key, Qt::KeyboardModifiers keyMods)
    {
        QStringList texts;
        for (const QKeyCombination& candidate : getCandidates(key, keyMods))
        {
            texts << QKeySequence(candidate).toString(QKeySequence::PortableText);
        }
        return texts.join(QStringLiteral(" | "));
    }
};

Q_DECLARE_METATYPE(WindowsShortcutCandidates::LayoutKey)

/// Delegate of the fonts, whose action has the shortcut Ctrl+Alt+F (the
/// matching of the action keys, review C3 of the lifetime test)
class ControlAltFontDelegate : public pdf::PDFFontDelegate
{
public:
    explicit ControlAltFontDelegate(const QKeySequence& shortcut) :
        m_shortcut(shortcut)
    {
    }

    virtual QList<Action> getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QList<Action> actions = pdf::PDFFontDelegate::getActions(option, index);
        for (Action& action : actions)
        {
            action.shortcut = m_shortcut;
        }
        return actions;
    }

    using pdf::PDFFontDelegate::isKeyHandled;
    using pdf::PDFFontDelegate::handleKey;

private:
    QKeySequence m_shortcut;
};

class FluentComponentsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void sizes();
    void sizeStability();
    void pixelProbes_data();
    void pixelProbes();
    void widgetPixels();
    void themeChange();
    void rightToLeft();
    void starButton();
    void colorSwatchGroup();
    void tag();
    void baseKeyboard();
    void baseSelection();
    void baseFocusRing();
    void baseAccessibility();
    void baseTrailingWidgets();
    void defensive();
    void mnemonics();
    void selectionByMouseAndKeys();
    void independentGroups();
    void selectionReentrancy();
    void selectionToNone();
    void trailingLifetime();
    void accessibilityEvents();
    void focusFrame();
    void apiDetails();
    void navItem();
    void linkUnderline();
    void linkSmall();
    void swatchGeometry();
    void thumbnailShadow();
    void galleryPages();
    void rowSizes();
    void rowSizeStability();
    void rowPixels_data();
    void rowPixels();
    void rowWidgetsMatchPainter();
    void fileNameElision();
    void rowNestedWidgets();
    void rowKeyboard();
    void rowAccessibility();
    void propertyRowEditing();
    void propertyRowCopy();
    void propertyRowLifetime();
    void rowThemeAndDirection();
    void rowDefensive();
    void propertyRowKeyboard();
    void propertyRowFocusDeletion();
    void moreButtonMenu();
    void rowNestedDeletion();
    void rowRetranslation();
    void rowDetails();
    void cardSizes();
    void cardSizeStability();
    void cardPixels_data();
    void cardPixels();
    void cardWidgetsMatchPainter();
    void cardBehaviour();
    void cardChoices();
    void cardAccessibility();
    void cardThemeAndDirection();
    void cardDefensive();
    void previewPainter();
    void cardMenuDeletion();
    void cardTags();
    void cardHeightForWidth();
    void cardStates();
    void cardDetails();
    void scaledPictures();
    void foreignStyle();
    void pageRangeParser();
    void pageRangeFuzz();
    void pageSelectionResult();
    void pageSelectionWidget();
    void continueWith();
    void colorButtonAndPalette();
    void colorPaletteDestroyed();
    void colorPaletteDetails();
    void colorSwatchRow();
    void colorSwatchRowPalette();
    void referencePointPicker();
    void signatureCard();
    void strengthMeter();
    void panelPixels_data();
    void panelPixels();
    void panelWidgetsMatchPainter();
    void panelThemeAndDefensive();
    void panelGallery();
    void textDirection();
    void keyValueListDirection();
    void keyValueListElision();
    void pageRangeReview();
    void pageSelectionReview();
    void pageSelectionHugeDocument();
    void continueWithReview();
    void colorButtonReview();
    void signatureAndMeterReview();
    void dataTextDirection();
    void delegateSizes();
    void delegatePixels_data();
    void delegatePixels();
    void delegatePressTracking();
    void delegateActions();
    void delegateKeyboardAndMenu();
    void shortcutRecording();
    void treeViewPressed();
    void delegateLifetime();
    void delegateDirectionAndTheme();
    void delegateDefensive();
    void delegateGallery();
    void delegateViewLifetime();
    void delegateLostRelease();
    void treeViewLostRelease();
    void treeViewRowFocusAndSelection();
    void shortcutSharedDelegate();
    void shortcutControlAlt_data();
    void shortcutControlAlt();
    void shortcutControlAltActions();
    void delegateReviewDetails();
    void delegateListWidth();
    void delegateNoteReplies();

    // R2: lambdas of the components replaced by member functions, safe constructs
    void r2AccentHueDefensive();
    void r2DelegateMenuActionLifetime();
    void r2VideoCardTagText();
    void r2PerformActionInvalidIndex();

    // R1: lambdas of the panels converted to member functions
    void panelsMemberSlots();

    // Fixes of the review of the lambdas (S3, S9, S10, S11)
    void lambdasReviewFixes();

    // Fixes of the code review of the library (Pdf4QtLibFluent/CODE_REVIEW.md: F01 - F04, F06, F09, F14)
    void reviewNavigationRemoval();
    void reviewClipboardLifetime();
    void reviewShortcutSelectionLifetime();
    void reviewPropertyEditorLifetime();
    void reviewDelegateFilterLifetime();
    void reviewSignatureDefaults();
    void reviewDisplayLong();

    // Window button and Window controls of the frameless window, the password of a document tab
    void windowSizes();
    void windowPixels_data();
    void windowPixels();
    void windowControlsBehaviour();
    void windowActivity();
    void windowDefensive();
    void windowControlsApplication();
    void documentTabPassword();
    void documentTabLockClicked();

    // Integration of step 2: elements of the pages, the states of the recent items, a dialog of the design
    void pageElementPixels_data();
    void pageElementPixels();
    void pageElementWidgets();
    void recentStates();
    void messageBoxCustomContent();
    void messageBoxInitialFocus();

    // Step 3: the rendering issue row (198:5950) and the icon of the badge of a message box (1028:22475)
    void renderingIssueRowPixels_data();
    void renderingIssueRowPixels();
    void renderingIssueRowWidget();
    void messageBoxBadgeIcon();
    void messageBoxTextLines();

    // Step 6: the icon chooser and the Tool row without the star of the collections editor (283:1888)
    void iconChoice();
    void toolRowWithoutStar();

private:
    /// Returns the pixel of the area, which differs most from the background
    static QColor strongestInk(const QImage& image, const QRect& area, const QColor& background);

    /// Returns the distance of two colors (sum of the differences of the channels)
    static int colorDistance(const QColor& first, const QColor& second);

    /// Renders the drawing over the background bg/surface (into a QImage: the glyphs
    /// are antialiased in grayscale on each platform, the probes of the colors of the
    /// texts do not depend on the subpixel antialiasing of the system)
    static QImage render(const std::function<void(QPainter&)>& paint, QSize size, qreal scale = 1.0);

    /// Renders the drawing over the background bg/surface in the paint event of a
    /// widget, as \p renderWidget renders a widget. A widget and the painter function
    /// of its component are compared by these two functions, on the same kind of
    /// paint device (glyphs of a widget use the subpixel antialiasing of fontconfig
    /// on Linux, glyphs drawn into a QImage are always grayscale).
    static QImage renderInWidget(const std::function<void(QPainter&)>& paint, QSize size, qreal scale = 1.0);
    static QColor pixel(const QImage& image, qreal x, qreal y);
    static bool isNear(const QColor& first, const QColor& second, int tolerance = 3);
    static QColor blend(const QColor& top, const QColor& bottom);
    static int countLeakPixels(const QImage& image);
    static QColor token(Color color) { return pdf::PDFStylePainter::color(color); }
    static Painter::State state(QStyle::State flags, Qt::LayoutDirection direction = Qt::LeftToRight) { return Painter::State(flags | QStyle::State_Enabled, direction); }
    static int countPixels(const QImage& image, const QRect& area, const QColor& color, int tolerance);

    /// Returns a description of the largest difference of two images of the same size (empty, if they are equal)
    static QString describeDifference(const QImage& first, const QImage& second, int tolerance);

    /// Renders the widget with its children over bg/surface at the scale
    static QImage renderWidget(QWidget* widget, qreal scale);

    /// Paints the item by the delegate with a composed option (without a view) over bg/surface
    static QImage renderDelegate(QAbstractItemDelegate* delegate, const QModelIndex& index, QStyle::State flags, QSize size, qreal scale = 1.0, Qt::LayoutDirection direction = Qt::LeftToRight);

    /// Returns the heights of the ink of the leftmost and of the rightmost glyph in the area (scale 1)
    static QPair<int, int> edgeInkHeights(const QImage& image, const QRect& area);

    /// Collects the events of accessibility
    static void collectAccessibleEvent(QAccessibleEvent* event);
    static int countAccessibleEvents(const QObject* object, QAccessible::Event type);
    static QList<QPair<const QObject*, QAccessible::Event>> s_accessibleEvents;
    static QList<int> s_politeness;     ///< Politeness of the collected announcements

    pdf::PDFWidgetStyle* m_style = nullptr;
};

QList<QPair<const QObject*, QAccessible::Event>> FluentComponentsTest::s_accessibleEvents;
QList<int> FluentComponentsTest::s_politeness;

QString FluentComponentsTest::describeDifference(const QImage& first, const QImage& second, int tolerance)
{
    if (first.size() != second.size())
    {
        return QStringLiteral("size %1x%2 / %3x%4").arg(first.width()).arg(first.height()).arg(second.width()).arg(second.height());
    }

    int count = 0;
    int largest = 0;
    QString description;
    for (int y = 0; y < first.height(); ++y)
    {
        for (int x = 0; x < first.width(); ++x)
        {
            if (!isNear(first.pixelColor(x, y), second.pixelColor(x, y), tolerance))
            {
                if (count == 0)
                {
                    description = QStringLiteral("(%1, %2) %3 / %4").arg(x).arg(y).arg(first.pixelColor(x, y).name(QColor::HexArgb), second.pixelColor(x, y).name(QColor::HexArgb));
                }
                largest = qMax(largest, colorDistance(first.pixelColor(x, y), second.pixelColor(x, y)));
                ++count;
            }
        }
    }

    return count > 0 ? QStringLiteral("%1 pixels, largest distance %2, first %3").arg(count).arg(largest).arg(description) : QString();
}

QImage FluentComponentsTest::renderWidget(QWidget* widget, qreal scale)
{
    QImage image(widget->size() * scale, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(token(Color::BgSurface));

    QPainter painter(&image);
    widget->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();
    return image;
}

void FluentComponentsTest::collectAccessibleEvent(QAccessibleEvent* event)
{
    s_accessibleEvents.append(qMakePair(static_cast<const QObject*>(event->object()), event->type()));
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (event->type() == QAccessible::Announcement)
    {
        s_politeness.append(int(static_cast<QAccessibleAnnouncementEvent*>(event)->politeness()));
    }
#endif
}

int FluentComponentsTest::countAccessibleEvents(const QObject* object, QAccessible::Event type)
{
    int count = 0;
    for (const auto& event : std::as_const(s_accessibleEvents))
    {
        if (event.first == object && event.second == type)
        {
            ++count;
        }
    }
    return count;
}

QImage FluentComponentsTest::render(const std::function<void(QPainter&)>& paint, QSize size, qreal scale)
{
    QImage image(size * scale, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(token(Color::BgSurface));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paint(painter);
    painter.end();
    return image;
}

QImage FluentComponentsTest::renderInWidget(const std::function<void(QPainter&)>& paint, QSize size, qreal scale)
{
    // Drawn in a widget, as renderWidget draws the widgets (the same glyph format)
    PainterCanvas canvas(paint, size);
    return renderWidget(&canvas, scale);
}

QColor FluentComponentsTest::pixel(const QImage& image, qreal x, qreal y)
{
    // Coordinates are logical, the center of the logical pixel is read
    const qreal scale = image.devicePixelRatio();
    return image.pixelColor(qFloor((x + 0.5) * scale), qFloor((y + 0.5) * scale));
}

bool FluentComponentsTest::isNear(const QColor& first, const QColor& second, int tolerance)
{
    return qAbs(first.red() - second.red()) <= tolerance && qAbs(first.green() - second.green()) <= tolerance &&
           qAbs(first.blue() - second.blue()) <= tolerance && qAbs(first.alpha() - second.alpha()) <= tolerance;
}

QColor FluentComponentsTest::blend(const QColor& top, const QColor& bottom)
{
    const qreal alpha = top.alphaF();
    return QColor::fromRgbF(top.redF() * alpha + bottom.redF() * (1.0 - alpha),
                            top.greenF() * alpha + bottom.greenF() * (1.0 - alpha),
                            top.blueF() * alpha + bottom.blueF() * (1.0 - alpha));
}

int FluentComponentsTest::countLeakPixels(const QImage& image)
{
    const QColor leak = pdf::PDFThemeColors::getLeakColor();
    int count = 0;

    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            const QRgb value = line[x];
            if (qAlpha(value) == 255 && qAbs(qRed(value) - leak.red()) <= 6 && qAbs(qGreen(value) - leak.green()) <= 6 && qAbs(qBlue(value) - leak.blue()) <= 6)
            {
                ++count;
            }
        }
    }

    return count;
}

int FluentComponentsTest::countPixels(const QImage& image, const QRect& area, const QColor& color, int tolerance)
{
    // The area is logical
    int count = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
    {
        for (int x = area.left(); x <= area.right(); ++x)
        {
            if (isNear(pixel(image, x, y), color, tolerance))
            {
                ++count;
            }
        }
    }
    return count;
}

void FluentComponentsTest::initTestCase()
{
    m_style = new pdf::PDFWidgetStyle();
    QApplication::setStyle(m_style);
    QVERIFY(pdf::PDFWidgetStyle::isActive());
}

void FluentComponentsTest::cleanup()
{
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void FluentComponentsTest::sizes()
{
    // Tool icon: diameter 28 / 32 / 44, icon of half of it
    QCOMPARE(Painter::getBadgeDiameter(Painter::BadgeSize::Small), 28);
    QCOMPARE(Painter::getBadgeDiameter(Painter::BadgeSize::Medium), 32);
    QCOMPARE(Painter::getBadgeDiameter(Painter::BadgeSize::Large), 44);
    QCOMPARE(Painter::getBadgeIconExtent(Painter::BadgeSize::Small), 14);
    QCOMPARE(Painter::getBadgeIconExtent(Painter::BadgeSize::Medium), 16);
    QCOMPARE(Painter::getBadgeIconExtent(Painter::BadgeSize::Large), 22);
    pdf::PDFToolBadge badge(QStringLiteral("grid"));
    QCOMPARE(badge.sizeHint(), QSize(32, 32));
    badge.setBadgeSize(Painter::BadgeSize::Large);
    QCOMPARE(badge.sizeHint(), QSize(44, 44));
    badge.setBadgeSize(Painter::BadgeSize::Small);
    QCOMPARE(badge.sizeHint(), QSize(28, 28));

    // Tag: 21 px high for any character, 10 px from the edges (border 1 + padding 9), "OCR" is 44 x 21
    const QSize ocrTag = Painter::getTagSize(Painter::Tag{ QStringLiteral("OCR") });
    QCOMPARE(ocrTag.height(), 21);
    QVERIFY2(qAbs(ocrTag.width() - 44) <= 1, qPrintable(QString::number(ocrTag.width())));
    QCOMPARE(Painter::getTagSize(Painter::Tag{ QStringLiteral("A") }).height(), 21);
    QCOMPARE(Painter::getTagSize(Painter::Tag{ QStringLiteral("✂") }).height(), 21);
    QCOMPARE(Painter::getTagSize(Painter::Tag{ QStringLiteral("Advanced") }).width(), qCeil(pdf::PDFStylePainter::textWidth(QStringLiteral("Advanced"), Painter::getTagFont())) + 20);
    QCOMPARE(pdf::PDFTag(QStringLiteral("OCR")).sizeHint(), ocrTag);

    // Text link: 2 px at the sides, 16 px high, "View all" is 53 x 16
    const QSize linkSize = Painter::getTextLinkSize(Painter::TextLink{ QStringLiteral("View all") });
    QCOMPARE(linkSize.height(), 16);
    QVERIFY2(qAbs(linkSize.width() - 53) <= 1, qPrintable(QString::number(linkSize.width())));
    pdf::PDFTextLink link(QStringLiteral("View all"));
    QCOMPARE(link.sizeHint(), linkSize);
    QCOMPARE(link.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 4.0);
    QCOMPARE(link.focusPolicy(), Qt::TabFocus);

    // Star button: 28 and 24, radius 6
    pdf::PDFStarButton star;
    QCOMPARE(star.sizeHint(), QSize(28, 28));
    star.setStarSize(pdf::PDFStarButton::StarSize::Small);
    QCOMPARE(star.sizeHint(), QSize(24, 24));
    QCOMPARE(star.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 6.0);

    // Color swatch: 28 x 28, round
    pdf::PDFColorSwatch swatch(Qt::yellow, QStringLiteral("Yellow"));
    QCOMPARE(swatch.sizeHint(), QSize(28, 28));
    QCOMPARE(swatch.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 14.0);

    // Page thumbnail: 32 x 42, 56 x 74, 80 x 106 and the margins of the shadow
    QCOMPARE(Painter::getPageThumbnailSize(Painter::ThumbnailSize::Small), QSize(32, 42));
    QCOMPARE(Painter::getPageThumbnailSize(Painter::ThumbnailSize::Medium), QSize(56, 74));
    QCOMPARE(Painter::getPageThumbnailSize(Painter::ThumbnailSize::Large), QSize(80, 106));
    QCOMPARE(Painter::getPageThumbnailShadowMargins(), QMargins(2, 1, 2, 3));
    // The widget has the size of the frame, the shadow takes no space in a layout (F1)
    pdf::PDFPageThumbnail thumbnail(Painter::ThumbnailSize::Medium);
    QCOMPARE(thumbnail.sizeHint(), QSize(56, 74));
    QCOMPARE(thumbnail.minimumSizeHint(), QSize(56, 74));
    thumbnail.setThumbnailSize(Painter::ThumbnailSize::Large);
    QCOMPARE(thumbnail.sizeHint(), QSize(80, 106));
    thumbnail.setThumbnailSize(Painter::ThumbnailSize::Small);
    QCOMPARE(thumbnail.sizeHint(), QSize(32, 42));

    // Bases: the size of the content
    QCOMPARE(TestRow().sizeHint(), QSize(300, 48));
    QCOMPARE(TestCard().sizeHint(), QSize(154, 48));
    QCOMPARE(TestCard().property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 12.0);
    QCOMPARE(Painter::getCardRadius(), 12.0);
    QCOMPARE(TestCard().getTrailingMargins(), Painter::getCardTrailingMargins());
    QCOMPARE(TestCard().getTrailingSpacing(), 8);
    QCOMPARE(TestRow().getTrailingMargins(), QMargins(0, 0, 12, 0));
    QCOMPARE(TestRow().getTrailingSpacing(), Painter::getRowTrailingSpacing());
    QVERIFY(!TestRow().property(pdf::PDFWidgetStyleProperties::RADIUS).isValid());

    // A button with the ring outside has the ring also without its own radius (C8)
    BareOutsideButton bare;
    QVERIFY(bare.property(pdf::PDFWidgetStyleProperties::RADIUS).isValid());
    QCOMPARE(bare.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 0.0);
}

void FluentComponentsTest::sizeStability()
{
    // A widget has the size of the variant Default in each state
    pdf::PDFStarButton star;
    pdf::PDFColorSwatch swatch(Qt::red);
    pdf::PDFTag tag(QStringLiteral("OCR"));
    tag.setClickable(true);
    pdf::PDFTextLink link(QStringLiteral("View all"));
    TestRow row;

    for (QAbstractButton* button : std::initializer_list<QAbstractButton*>{ &star, &swatch, &tag, &link, &row })
    {
        const QSize size = button->sizeHint();
        button->setAttribute(Qt::WA_UnderMouse, true);
        QCOMPARE(button->sizeHint(), size);
        button->setDown(true);
        QCOMPARE(button->sizeHint(), size);
        button->setDown(false);
        button->setCheckable(true);
        button->setChecked(true);
        QCOMPARE(button->sizeHint(), size);
        button->setEnabled(false);
        QCOMPARE(button->sizeHint(), size);
        button->setLayoutDirection(Qt::RightToLeft);
        QCOMPARE(button->sizeHint(), size);
    }

    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(star.sizeHint(), QSize(28, 28));
    QCOMPARE(tag.sizeHint(), Painter::getTagSize(Painter::Tag{ QStringLiteral("OCR") }));
}

void FluentComponentsTest::pixelProbes_data()
{
    QTest::addColumn<QString>("theme");
    QTest::addColumn<qreal>("scale");

    for (const char* theme : { "light", "dark" })
    {
        for (const qreal scale : { 1.0, 1.5, 2.0 })
        {
            QTest::newRow(qPrintable(QStringLiteral("%1@%2").arg(QString::fromLatin1(theme)).arg(scale))) << QString::fromLatin1(theme) << scale;
        }
    }
}

void FluentComponentsTest::pixelProbes()
{
    QFETCH(QString, theme);
    QFETCH(qreal, scale);
    QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));

    const QColor surface = token(Color::BgSurface);
    const QStyle::State none = QStyle::State_None;

    // Tool icon: the circle of the hue, outside of the icon
    for (const pdf::PDFAccentHue hue : { pdf::PDFAccentHue::Blue, pdf::PDFAccentHue::Amber, pdf::PDFAccentHue::Neutral })
    {
        const Painter::ToolBadge badge{ QStringLiteral("grid"), hue, Painter::BadgeSize::Medium };
        const QImage image = render([&](QPainter& painter) { Painter::paintToolBadge(&painter, QRectF(0, 0, 32, 32), badge, state(none)); }, QSize(32, 32), scale);
        QVERIFY(isNear(pixel(image, 4, 16), token(pdf::PDFAccentHueColors::getColor(hue, pdf::PDFAccentHueColors::Role::Background))));
        QVERIFY(isNear(pixel(image, 0, 0), surface));
        QVERIFY(countPixels(image, QRect(8, 8, 16, 16), token(pdf::PDFAccentHueColors::getColor(hue, pdf::PDFAccentHueColors::Role::Icon)), 40) > 0);
    }

    // Tag: fill bg/subtle, hover and press bg/hover / bg/pressed over it (decision 27), border, focus ring inside
    const Painter::Tag tag{ QStringLiteral("OCR") };
    const QSize tagSize = Painter::getTagSize(tag);
    const qreal tagMiddle = tagSize.width() / 2.0;
    auto renderTag = [&](QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintTag(&painter, QRectF(QPointF(0, 0), tagSize), tag, state(flags)); }, tagSize, scale);
    };
    QImage image = renderTag(none);
    QVERIFY(isNear(pixel(image, 5, 10), token(Color::BgSubtle)));
    QVERIFY(isNear(pixel(image, tagMiddle, 0), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, tagMiddle, 1), token(Color::BgSubtle)));
    image = renderTag(QStyle::State_MouseOver);
    QVERIFY(isNear(pixel(image, 5, 10), blend(token(Color::BgHover), token(Color::BgSubtle))));
    QVERIFY(!isNear(pixel(image, 5, 10), token(Color::BgSubtle), 8));
    QVERIFY(isNear(pixel(image, tagMiddle, 0), token(Color::BorderDefault)));
    image = renderTag(QStyle::State_MouseOver | QStyle::State_Sunken);
    QVERIFY(isNear(pixel(image, 5, 10), blend(token(Color::BgPressed), token(Color::BgSubtle))));
    QVERIFY(isNear(pixel(image, tagMiddle, 0), token(Color::BorderDefault)));
    image = renderTag(QStyle::State_HasFocus);
    QVERIFY(isNear(pixel(image, tagMiddle, 0), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, tagMiddle, 1), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, tagMiddle, 2), token(Color::BgSubtle)));

    // Text link: text/accent, pressed text/accent-pressed, focus ring outside
    const Painter::TextLink link{ QStringLiteral("View all") };
    const QSize linkSize = Painter::getTextLinkSize(link);
    auto renderLink = [&](QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(QPointF(4, 4), linkSize), link, state(flags)); }, linkSize + QSize(8, 8), scale);
    };
    image = renderLink(none);
    QVERIFY(countPixels(image, QRect(4, 4, linkSize.width(), linkSize.height()), token(Color::TextAccent), 8) > 5);
    image = renderLink(QStyle::State_MouseOver | QStyle::State_Sunken);
    QVERIFY(countPixels(image, QRect(4, 4, linkSize.width(), linkSize.height()), token(Color::TextAccentPressed), 8) > 5);
    image = renderLink(QStyle::State_HasFocus);
    QVERIFY(isNear(pixel(image, 4 + linkSize.width() / 2.0, 2), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 4 + linkSize.width() / 2.0, 3), token(Color::BorderFocus)));
    image = render([&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(QPointF(4, 4), linkSize), link, Painter::State(QStyle::State_None)); }, linkSize + QSize(8, 8), scale);
    QCOMPARE(countPixels(image, QRect(4, 4, linkSize.width(), linkSize.height()), token(Color::TextAccent), 8), 0);
    QVERIFY(countPixels(image, QRect(4, 4, linkSize.width(), linkSize.height()), token(Color::TextDisabled), 8) > 5);

    // Star button: hover and press backgrounds, the pinned star is filled amber
    auto renderStar = [&](QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintStar(&painter, QRectF(0, 0, 28, 28), Painter::StarSize::Medium, Painter::State(flags)); }, QSize(28, 28), scale);
    };
    const QStyle::State enabled = QStyle::State_Enabled;
    image = renderStar(enabled);
    QVERIFY(isNear(pixel(image, 2, 14), surface));
    QVERIFY(isNear(pixel(image, 13, 14), surface));
    image = renderStar(enabled | QStyle::State_MouseOver);
    QVERIFY(isNear(pixel(image, 2, 14), blend(token(Color::BgHover), surface)));
    image = renderStar(enabled | QStyle::State_MouseOver | QStyle::State_Sunken);
    QVERIFY(isNear(pixel(image, 2, 14), blend(token(Color::BgPressed), surface)));

    // Hover and press lie over the own surface (decision 28): the same on another
    // background and no second overlay over the hover of a row; at rest the background shows
    const QColor rowHover = blend(token(Color::BgHover), surface);
    for (const QColor& background : { token(Color::BgTrack), rowHover })
    {
        auto renderStarOn = [&](QStyle::State flags, bool isOn)
        {
            return render([&](QPainter& painter)
            {
                painter.fillRect(QRectF(0, 0, 28, 28), background);
                Painter::paintStar(&painter, QRectF(0, 0, 28, 28), Painter::StarSize::Medium, Painter::State(flags | (isOn ? QStyle::State_On : QStyle::State_None)));
            }, QSize(28, 28), scale);
        };
        for (const bool isOn : { false, true })
        {
            QVERIFY(isNear(pixel(renderStarOn(enabled, isOn), 2, 14), background));
            QVERIFY(isNear(pixel(renderStarOn(enabled | QStyle::State_MouseOver, isOn), 2, 14), blend(token(Color::BgHover), surface)));
            QVERIFY(isNear(pixel(renderStarOn(enabled | QStyle::State_MouseOver | QStyle::State_Sunken, isOn), 2, 14), blend(token(Color::BgPressed), surface)));
            QVERIFY(isNear(pixel(renderStarOn(QStyle::State_MouseOver, isOn), 2, 14), background));
        }
    }
    image = renderStar(enabled | QStyle::State_On);
    QVERIFY(isNear(pixel(image, 13, 14), token(Color::IconSun), 12));
    image = renderStar(QStyle::State_On);
    QVERIFY(isNear(pixel(image, 13, 14), token(Color::IconDisabled), 12));
    image = renderStar(QStyle::State_MouseOver);
    QVERIFY(isNear(pixel(image, 2, 14), surface));

    // Color swatch: the color, the selection ring, hover, disabled selection, no color
    const Painter::ColorSwatch swatch{ token(Color::AnnotationYellow), Painter::SwatchKind::Color };
    auto renderSwatch = [&](const Painter::ColorSwatch& content, QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintColorSwatch(&painter, QRectF(4, 4, 28, 28), content, Painter::State(flags)); }, QSize(36, 36), scale);
    };
    image = renderSwatch(swatch, enabled);
    QVERIFY(isNear(pixel(image, 18, 18), token(Color::AnnotationYellow)));
    QVERIFY(isNear(pixel(image, 18, 5), surface));
    image = renderSwatch(swatch, enabled | QStyle::State_On);
    QVERIFY(isNear(pixel(image, 18, 5), token(Color::BgAccent), 12));
    QVERIFY(isNear(pixel(image, 18, 18), token(Color::AnnotationYellow)));
    image = renderSwatch(swatch, enabled | QStyle::State_MouseOver);
    QVERIFY(isNear(pixel(image, 18, 5), blend(token(Color::BgHover), surface), 4));

    // Hover and press lie over the own surface (decision 28), also on another background
    for (const QColor& background : { token(Color::BgTrack), rowHover })
    {
        auto renderSwatchOn = [&](const Painter::ColorSwatch& content, QStyle::State flags)
        {
            return render([&](QPainter& painter)
            {
                painter.fillRect(QRectF(0, 0, 36, 36), background);
                Painter::paintColorSwatch(&painter, QRectF(4, 4, 28, 28), content, Painter::State(flags));
            }, QSize(36, 36), scale);
        };
        for (const Painter::ColorSwatch& content : { swatch, Painter::ColorSwatch{ QColor(), Painter::SwatchKind::None } })
        {
            QVERIFY(isNear(pixel(renderSwatchOn(content, enabled), 18, 7), background));
            QVERIFY(isNear(pixel(renderSwatchOn(content, enabled | QStyle::State_MouseOver), 18, 7), blend(token(Color::BgHover), surface), 4));
            QVERIFY(isNear(pixel(renderSwatchOn(content, enabled | QStyle::State_MouseOver | QStyle::State_Sunken), 18, 7), blend(token(Color::BgPressed), surface), 4));
            QVERIFY(isNear(pixel(renderSwatchOn(content, enabled | QStyle::State_On | QStyle::State_MouseOver), 18, 7), blend(token(Color::BgHover), surface), 4));
            QVERIFY(isNear(pixel(renderSwatchOn(content, QStyle::State_MouseOver), 18, 7), background));
        }
    }
    image = renderSwatch(swatch, QStyle::State_On | QStyle::State_MouseOver);
    QVERIFY(isNear(pixel(image, 18, 5), token(Color::BorderStrong), 12));
    QVERIFY(isNear(pixel(image, 18, 18), token(Color::AnnotationYellow)));
    image = renderSwatch(swatch, enabled | QStyle::State_On | QStyle::State_HasFocus);
    QVERIFY(isNear(pixel(image, 18, 5), token(Color::BgAccent), 12));
    QVERIFY(isNear(pixel(image, 18, 3), token(Color::BorderFocus), 12));
    const Painter::ColorSwatch noColor{ QColor(), Painter::SwatchKind::None };
    image = renderSwatch(noColor, enabled);
    QVERIFY(isNear(pixel(image, 16, 18), token(Color::IconDanger), 40));
    QVERIFY(isNear(pixel(image, 13, 13), surface));
    image = renderSwatch(noColor, none);
    QVERIFY(isNear(pixel(image, 16, 18), token(Color::IconDisabled), 40));

    // Page thumbnail: border, title line, lines, the paper
    const QRectF frame(4, 4, 32, 42);
    image = render([&](QPainter& painter) { Painter::paintPageThumbnail(&painter, frame, Painter::PageThumbnail(), state(none)); }, QSize(40, 50), scale);
    QVERIFY(isNear(pixel(image, 4 + 16, 4), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(image, 4 + 16, 4 + 6), token(Color::TextMuted)));
    QVERIFY(isNear(pixel(image, 4 + 16, 4 + 10), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(image, 4 + 16, 4 + 30), surface));
    QVERIFY(isNear(pixel(image, 4 + 25, 4 + 16), surface));     // the last line is 14 px long
    QCOMPARE(countLeakPixels(image), 0);

    QPixmap picture(QSize(64, 84));
    picture.fill(token(Color::SampleBlue));
    Painter::PageThumbnail withPicture;
    withPicture.size = Painter::ThumbnailSize::Medium;
    withPicture.pixmap = picture;
    image = render([&](QPainter& painter) { Painter::paintPageThumbnail(&painter, QRectF(4, 4, 56, 74), withPicture, state(none)); }, QSize(64, 82), scale);
    QVERIFY(isNear(pixel(image, 4 + 28, 4 + 37), token(Color::SampleBlue)));
    QVERIFY(isNear(pixel(image, 4 + 28, 4), token(Color::BorderStrong), 6));
}

void FluentComponentsTest::widgetPixels()
{
    const QColor surface = token(Color::BgSurface);

    // The widgets draw the same as the painter
    pdf::PDFStarButton star;
    star.resize(star.sizeHint());
    QImage image = star.grab().toImage();
    const QColor base = image.pixelColor(2, 14);
    star.setAttribute(Qt::WA_UnderMouse, true);
    image = star.grab().toImage();
    QVERIFY(isNear(image.pixelColor(2, 14), blend(token(Color::BgHover), base), 4));
    star.setEnabled(false);
    image = star.grab().toImage();
    QCOMPARE(image.pixelColor(2, 14), base);

    // A disabled swatch keeps its color and has no hover
    pdf::PDFColorSwatch swatch(token(Color::AnnotationRed), QStringLiteral("Red"));
    swatch.resize(swatch.sizeHint());
    swatch.setAttribute(Qt::WA_UnderMouse, true);
    image = swatch.grab().toImage();
    QVERIFY(isNear(image.pixelColor(14, 1), blend(token(Color::BgHover), base), 4));
    swatch.setEnabled(false);
    image = swatch.grab().toImage();
    QCOMPARE(image.pixelColor(14, 1), base);
    QVERIFY(isNear(image.pixelColor(14, 14), token(Color::AnnotationRed)));

    // A tag, which is only a label, does not react to the pointer
    pdf::PDFTag tag(QStringLiteral("OCR"));
    tag.resize(tag.sizeHint());
    tag.setAttribute(Qt::WA_UnderMouse, true);
    image = tag.grab().toImage();
    QVERIFY(isNear(image.pixelColor(5, 10), token(Color::BgSubtle)));
    tag.setClickable(true);
    image = tag.grab().toImage();
    QVERIFY(isNear(image.pixelColor(5, 10), blend(token(Color::BgHover), token(Color::BgSubtle))));

    // Badge and thumbnail
    pdf::PDFToolBadge badge(QStringLiteral("lock"), pdf::PDFAccentHue::Red);
    badge.resize(badge.sizeHint());
    image = badge.grab().toImage();
    QVERIFY(isNear(image.pixelColor(4, 16), token(Color::AccentRedBg)));
    pdf::PDFPageThumbnail thumbnail;
    thumbnail.resize(thumbnail.sizeHint());
    image = thumbnail.grab().toImage();
    QVERIFY(isNear(image.pixelColor(16, 0), token(Color::BorderStrong)));
    QVERIFY(isNear(image.pixelColor(16, 30), surface));

    for (QWidget* widget : std::initializer_list<QWidget*>{ &star, &swatch, &tag, &badge, &thumbnail })
    {
        QCOMPARE(countLeakPixels(widget->grab().toImage()), 0);
    }

    // The widgets at the scales 1.5 and 2 draw the same as the painter (C18)
    QWidget thumbnailHolder;
    thumbnailHolder.resize(40, 52);
    pdf::PDFPageThumbnail* childThumbnail = new pdf::PDFPageThumbnail(&thumbnailHolder);
    childThumbnail->setGeometry(QRect(QPoint(4, 4), childThumbnail->sizeHint()));
    swatch.setEnabled(true);
    swatch.setSelected(true);
    swatch.setAttribute(Qt::WA_UnderMouse, false);
    star.setEnabled(true);
    for (const qreal scale : { 1.0, 1.5, 2.0 })
    {
        const QImage starImage = renderWidget(&star, scale);
        const QImage starPainted = renderInWidget([&](QPainter& painter) { Painter::paintStar(&painter, QRectF(0, 0, 28, 28), Painter::StarSize::Medium, star.getComponentState()); }, QSize(28, 28), scale);
        QVERIFY(isNear(pixel(starImage, 2, 14), blend(token(Color::BgHover), surface), 4));
        if (qFuzzyCompare(scale, 1.0))
        {
            // At other scales the widget, which is not on a screen of the scale, draws its icon from a pixmap of its own scale
            const QString starDifference = describeDifference(starImage, starPainted, 3);
            QVERIFY2(starDifference.isEmpty(), qPrintable(starDifference));
        }

        const QImage swatchImage = renderWidget(&swatch, scale);
        QVERIFY(isNear(pixel(swatchImage, 14, 1), token(Color::BgAccent), 12));
        QVERIFY(isNear(pixel(swatchImage, 14, 14), token(Color::AnnotationRed)));

        // The thumbnail is a child widget (its shadow lies in the parent)
        const QImage thumbnailImage = renderWidget(&thumbnailHolder, scale);
        QVERIFY(isNear(pixel(thumbnailImage, 4 + 16, 4), token(Color::BorderStrong), 6));
        QVERIFY(isNear(pixel(thumbnailImage, 4 + 16, 4 + 6), token(Color::TextMuted), 6));
        QCOMPARE(countLeakPixels(thumbnailImage), 0);
    }
}

void FluentComponentsTest::themeChange()
{
    pdf::PDFToolBadge badge(QStringLiteral("grid"), pdf::PDFAccentHue::Teal);
    badge.resize(badge.sizeHint());
    pdf::PDFTag tag(QStringLiteral("OCR"));
    tag.resize(tag.sizeHint());
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    TestRow* row = new TestRow(&window);
    layout->addWidget(row);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QColor lightBadge = badge.grab().toImage().pixelColor(4, 16);
    const QColor lightTag = tag.grab().toImage().pixelColor(5, 10);
    QVERIFY(isNear(lightBadge, token(Color::AccentTealBg)));

    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    const QColor darkBadge = badge.grab().toImage().pixelColor(4, 16);
    const QColor darkTag = tag.grab().toImage().pixelColor(5, 10);
    QVERIFY(isNear(darkBadge, token(Color::AccentTealBg)));
    QVERIFY(isNear(darkTag, token(Color::BgSubtle)));
    QVERIFY(lightBadge != darkBadge);
    QVERIFY(lightTag != darkTag);

    // The row repaints itself with the colors of the new theme and asks its layout for a new size
    row->paintCount = 0;
    int layoutRequests = 0;
    struct Filter : public QObject
    {
        int* count = nullptr;
        virtual bool eventFilter(QObject*, QEvent* event) override
        {
            if (event->type() == QEvent::LayoutRequest)
            {
                ++*count;
            }
            return false;
        }
    } filter;
    filter.count = &layoutRequests;
    window.installEventFilter(&filter);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QTRY_VERIFY(row->paintCount > 0);
    QTRY_VERIFY(layoutRequests > 0);
    window.removeEventFilter(&filter);
}

void FluentComponentsTest::rightToLeft()
{
    // The label of a link starts at the trailing edge in a wider rectangle
    const Painter::TextLink link{ QStringLiteral("View all") };
    const QSize linkSize = Painter::getTextLinkSize(link);
    const QRect area(0, 0, linkSize.width() + 80, linkSize.height());
    const QColor accent = token(Color::TextAccent);

    QImage image = render([&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(area), link, state(QStyle::State_None)); }, area.size());
    QVERIFY(countPixels(image, QRect(0, 0, 40, area.height()), accent, 8) > 0);
    QCOMPARE(countPixels(image, QRect(area.width() - 40, 0, 40, area.height()), accent, 8), 0);
    image = render([&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(area), link, state(QStyle::State_None, Qt::RightToLeft)); }, area.size());
    QCOMPARE(countPixels(image, QRect(0, 0, 40, area.height()), accent, 8), 0);
    QVERIFY(countPixels(image, QRect(area.width() - 40, 0, 40, area.height()), accent, 8) > 0);

    // Mirroring of the logical rectangles
    const Painter::State rtl(QStyle::State_Enabled, Qt::RightToLeft);
    QCOMPARE(rtl.visualRect(QRectF(10, 0, 100, 20), QRectF(10, 0, 30, 20)), QRectF(80, 0, 30, 20));
    QCOMPARE(Painter::State().visualRect(QRectF(10, 0, 100, 20), QRectF(10, 0, 30, 20)), QRectF(10, 0, 30, 20));
    QCOMPARE(rtl.leadingAlignment(), Qt::AlignRight | Qt::AlignAbsolute);
    QCOMPARE(rtl.trailingAlignment(), Qt::AlignLeft | Qt::AlignAbsolute);
    QCOMPARE(Painter::State().leadingAlignment(), Qt::AlignLeft | Qt::AlignAbsolute);
    QCOMPARE(Painter::State().trailingAlignment(), Qt::AlignRight | Qt::AlignAbsolute);
    QCOMPARE(Painter::State(QStyle::State_Enabled, Qt::LayoutDirectionAuto).isRightToLeft(), false);

    // The state of a widget carries its direction
    pdf::PDFTextLink linkWidget(QStringLiteral("View all"));
    linkWidget.setLayoutDirection(Qt::RightToLeft);
    QVERIFY(linkWidget.getComponentState().isRightToLeft());

    // Trailing widgets of a row are mirrored
    TestRow row;
    row.resize(300, 48);
    pdf::PDFStarButton* star = new pdf::PDFStarButton(pdf::PDFStarButton::StarSize::Small);
    row.addTrailingWidget(star);
    QCOMPARE(star->geometry(), QRect(300 - 12 - 24, 12, 24, 24));
    row.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(star->geometry(), QRect(12, 12, 24, 24));
}

void FluentComponentsTest::starButton()
{
    pdf::PDFStarButton star;
    QSignalSpy spy(&star, &pdf::PDFStarButton::pinnedChanged);
    QVERIFY(!star.isPinned());
    QCOMPARE(star.toolTip(), QStringLiteral("Pin file"));
    QCOMPARE(star.getSelectionBehavior(), pdf::PDFComponentButton::SelectionBehavior::Toggle);

    // The signal is emitted only on a real change, after the change of the state
    bool pinnedInSlot = false;
    connect(&star, &pdf::PDFStarButton::pinnedChanged, &star, [&](bool) { pinnedInSlot = star.isPinned(); });
    star.setPinned(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.back().front().toBool(), true);
    QVERIFY(pinnedInSlot);
    star.setPinned(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(star.toolTip(), QStringLiteral("Unpin"));
    QVERIFY(star.getComponentState().isOn());

    // A click toggles it
    star.click();
    QVERIFY(!star.isPinned());
    QCOMPARE(spy.count(), 2);
    QCOMPARE(star.toolTip(), QStringLiteral("Pin file"));

    // A tooltip of the application is kept
    star.setToolTip(QStringLiteral("Pin the tool"));
    star.setPinned(true);
    QCOMPARE(star.toolTip(), QStringLiteral("Pin the tool"));

    // Accessibility: a checkable button named by its state
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(&star);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::Button);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Unpin"));
    QVERIFY(accessible->state().checkable);
    QVERIFY(accessible->state().checked);

    // Invalid size
    star.setStarSize(static_cast<pdf::PDFStarButton::StarSize>(5));
    QCOMPARE(star.getStarSize(), pdf::PDFStarButton::StarSize::Medium);
}

void FluentComponentsTest::colorSwatchGroup()
{
    // Swatches of an exclusive button group
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    QButtonGroup palette;
    pdf::PDFColorSwatch* yellow = new pdf::PDFColorSwatch(token(Color::AnnotationYellow), QStringLiteral("Yellow"), &window);
    pdf::PDFColorSwatch* red = new pdf::PDFColorSwatch(token(Color::AnnotationRed), QStringLiteral("Red"), &window);
    pdf::PDFColorSwatch* none = new pdf::PDFColorSwatch(&window);
    none->setKind(pdf::PDFColorSwatch::Kind::None);
    palette.addButton(yellow);
    palette.addButton(red);
    palette.addButton(none);
    layout->addWidget(yellow);
    layout->addWidget(red);
    layout->addWidget(none);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    yellow->setSelected(true);
    QVERIFY(yellow->isSelected());
    red->click();
    QVERIFY(red->isSelected());
    QVERIFY(!yellow->isSelected());

    // A click on the selected swatch keeps it
    red->click();
    QVERIFY(red->isSelected());

    // The selected swatch reacts to the pointer (the design has its Hover variant)
    red->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY(red->getComponentState().isHovered());
    red->setAttribute(Qt::WA_UnderMouse, false);

    // The arrows move the selection
    red->setFocus(Qt::TabFocusReason);
    QTest::keyClick(red, Qt::Key_Right);
    QVERIFY(none->isSelected());
    QVERIFY(none->hasFocus());

    // The selected swatch can be unselected by the program: the current color is none of them
    none->setSelected(false);
    QVERIFY(!none->isSelected());
    QVERIFY(!palette.checkedButton());
    QVERIFY(palette.exclusive());
    QCOMPARE(none->property("selected").toBool(), false);
    red->setProperty("selected", true);
    QVERIFY(red->isSelected());

    // Accessibility: radio buttons named by their colors
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(red);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::RadioButton);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Red"));
    QCOMPARE(red->toolTip(), QStringLiteral("Red"));
    QCOMPARE(QAccessible::queryAccessibleInterface(none)->text(QAccessible::Name), QStringLiteral("No color"));
    pdf::PDFColorSwatch unnamed(QColor(0x12, 0x34, 0x56));
    QCOMPARE(QAccessible::queryAccessibleInterface(&unnamed)->text(QAccessible::Name), QStringLiteral("#123456"));
    pdf::PDFColorSwatch invalid((QColor()));
    QCOMPARE(QAccessible::queryAccessibleInterface(&invalid)->text(QAccessible::Name), QStringLiteral("Color"));
    unnamed.setAccessibleName(QStringLiteral("Navy"));
    QCOMPARE(QAccessible::queryAccessibleInterface(&unnamed)->text(QAccessible::Name), QStringLiteral("Navy"));

    // A button group of the application takes the place of the auto exclusivity
    QWidget other;
    QButtonGroup group;
    pdf::PDFColorSwatch first(Qt::green, QString(), &other);
    pdf::PDFColorSwatch second(Qt::blue, QString(), &other);
    group.addButton(&first);
    group.addButton(&second);
    first.click();
    second.click();
    QVERIFY(second.isSelected());
    QVERIFY(!first.isSelected());
    second.click();
    QVERIFY(second.isSelected());
    QCOMPARE(group.checkedButton(), &second);

    // A disabled swatch does not react
    second.setEnabled(false);
    second.setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY(!second.getComponentState().isHovered());
    QVERIFY(second.getComponentState().isOn());

    // Setters
    first.setColor(Qt::cyan);
    QCOMPARE(first.getColor(), QColor(Qt::cyan));
    first.setKind(static_cast<pdf::PDFColorSwatch::Kind>(42));
    QCOMPARE(first.getKind(), pdf::PDFColorSwatch::Kind::Color);
}

void FluentComponentsTest::tag()
{
    pdf::PDFTag tag(QStringLiteral("Advanced"));
    QVERIFY(!tag.isClickable());
    QCOMPARE(tag.focusPolicy(), Qt::NoFocus);
    QVERIFY(tag.testAttribute(Qt::WA_TransparentForMouseEvents));
    QCOMPARE(QAccessible::queryAccessibleInterface(&tag)->role(), QAccessible::StaticText);
    QCOMPARE(QAccessible::queryAccessibleInterface(&tag)->text(QAccessible::Name), QStringLiteral("Advanced"));

    // A label does not activate
    QSignalSpy spy(&tag, &QAbstractButton::clicked);
    tag.resize(tag.sizeHint());
    QTest::keyClick(&tag, Qt::Key_Return);
    QCOMPARE(spy.count(), 0);

    tag.setClickable(true);
    QCOMPARE(tag.focusPolicy(), Qt::StrongFocus);
    QVERIFY(!tag.testAttribute(Qt::WA_TransparentForMouseEvents));
    QCOMPARE(QAccessible::queryAccessibleInterface(&tag)->role(), QAccessible::Button);
    QTest::keyClick(&tag, Qt::Key_Return);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(tag.getFocusRing(), pdf::PDFComponentButton::FocusRing::Inside);

    // The mouse of a label passes to the row below it
    TestRow row;
    row.resize(300, 48);
    pdf::PDFTag* rowTag = new pdf::PDFTag(QStringLiteral("Advanced"));
    row.addTrailingWidget(rowTag);
    QSignalSpy rowSpy(&row, &QAbstractButton::clicked);
    row.show();
    QVERIFY(QTest::qWaitForWindowExposed(&row));
    QVERIFY(!row.childAt(rowTag->geometry().center()));
    QTest::mouseClick(&row, Qt::LeftButton, Qt::NoModifier, rowTag->geometry().center());
    QCOMPARE(rowSpy.count(), 1);
}

void FluentComponentsTest::baseKeyboard()
{
    TestRow row;
    row.setText(QStringLiteral("Merge PDF"));
    row.resize(row.sizeHint());
    row.show();
    row.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&row));
    row.setFocus(Qt::TabFocusReason);

    QSignalSpy spy(&row, &QAbstractButton::clicked);
    QTest::keyClick(&row, Qt::Key_Space);
    QCOMPARE(spy.count(), 1);
    QTest::keyClick(&row, Qt::Key_Return);
    QCOMPARE(spy.count(), 2);
    QTest::keyClick(&row, Qt::Key_Enter, Qt::KeypadModifier);
    QCOMPARE(spy.count(), 3);

    // Enter with a modifier and keys of other meaning do nothing
    QTest::keyClick(&row, Qt::Key_Return, Qt::ControlModifier);
    QTest::keyClick(&row, Qt::Key_A);
    QCOMPARE(spy.count(), 3);

    // A disabled row does not activate
    row.setEnabled(false);
    QTest::keyClick(&row, Qt::Key_Return);
    QCOMPARE(spy.count(), 3);

    // A card as well
    TestCard card;
    card.resize(card.sizeHint());
    QSignalSpy cardSpy(&card, &QAbstractButton::clicked);
    QTest::keyClick(&card, Qt::Key_Return);
    QCOMPARE(cardSpy.count(), 1);
}

void FluentComponentsTest::baseSelection()
{
    using Behavior = pdf::PDFComponentButton::SelectionBehavior;

    // Static exclusive group: the selected card does not react to the pointer and a click keeps it
    QWidget parent;
    QButtonGroup staticGroup;
    TestCard* first = new TestCard(&parent);
    TestCard* second = new TestCard(&parent);
    first->setSelectionBehavior(Behavior::ExclusiveStatic);
    second->setSelectionBehavior(Behavior::ExclusiveStatic);
    staticGroup.addButton(first);
    staticGroup.addButton(second);
    QVERIFY(first->isCheckable());
    QVERIFY(!first->autoExclusive());
    first->click();
    QVERIFY(first->isChecked());
    first->click();
    QVERIFY(first->isChecked());
    second->click();
    QVERIFY(second->isChecked());
    QVERIFY(!first->isChecked());
    second->setAttribute(Qt::WA_UnderMouse, true);
    second->setDown(true);
    QVERIFY(!second->getComponentState().isHovered());
    QVERIFY(!second->getComponentState().isPressed());
    first->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY(first->getComponentState().isHovered());
    second->setDown(false);

    // Deselectable group: a click on the selected card unselects it, also in a button group
    QWidget other;
    QButtonGroup group;
    TestCard* third = new TestCard(&other);
    TestCard* fourth = new TestCard(&other);
    third->setSelectionBehavior(Behavior::Deselectable);
    fourth->setSelectionBehavior(Behavior::Deselectable);
    group.addButton(third);
    group.addButton(fourth);
    third->click();
    QVERIFY(third->isChecked());
    fourth->click();
    QVERIFY(fourth->isChecked());
    QVERIFY(!third->isChecked());
    fourth->click();
    QVERIFY(!fourth->isChecked());
    QVERIFY(!third->isChecked());
    QVERIFY(group.exclusive());
    QVERIFY(!group.checkedButton());
    third->click();
    QVERIFY(third->isChecked());

    // Deselectable without a group
    TestCard single;
    single.setSelectionBehavior(Behavior::Deselectable);
    single.click();
    QVERIFY(single.isChecked());
    single.click();
    QVERIFY(!single.isChecked());

    // Back to a plain button, an invalid value is a plain button
    single.click();
    single.setSelectionBehavior(Behavior::None);
    QVERIFY(!single.isCheckable());
    QVERIFY(!single.isChecked());
    single.setSelectionBehavior(static_cast<Behavior>(77));
    QCOMPARE(single.getSelectionBehavior(), Behavior::None);
    QCOMPARE(single.getComponentState().flags & (QStyle::State_On | QStyle::State_Off), QStyle::State(QStyle::State_None));
}

void FluentComponentsTest::baseFocusRing()
{
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    TestRow* row = new TestRow(&window);
    TestCard* card = new TestCard(&window);
    layout->addWidget(row);
    layout->addWidget(card);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    auto findFrame = [&window]() -> QFocusFrame*
    {
        const QList<QFocusFrame*> frames = window.findChildren<QFocusFrame*>();
        for (QFocusFrame* frame : frames)
        {
            if (frame->widget())
            {
                return frame;
            }
        }
        return nullptr;
    };

    // A click shows no ring
    QTest::mouseClick(row, Qt::LeftButton);
    QVERIFY(row->hasFocus());
    QVERIFY(!row->getComponentState().hasFocusRing());

    // The keyboard: the row draws the ring inside, the focus frame is not used
    QTest::keyClick(row, Qt::Key_Tab);
    QTest::keyClick(card, Qt::Key_Tab, Qt::ShiftModifier);
    QTRY_VERIFY(row->hasFocus());
    QVERIFY(row->getComponentState().hasFocusRing());
    QVERIFY(!findFrame());
    row->paintCount = 0;
    row->repaint();
    QVERIFY(row->paintCount > 0);
    QVERIFY(row->lastState.hasFocusRing());

    // The card leaves its ring to the focus frame of the style, it has the size of the card
    QTest::keyClick(row, Qt::Key_Tab);
    QTRY_VERIFY(card->hasFocus());
    QVERIFY(!card->getComponentState().hasFocusRing());
    QTRY_VERIFY(findFrame());
    QCOMPARE(findFrame()->widget(), card);
    QCOMPARE(findFrame()->geometry(), card->geometry().adjusted(-2, -2, 2, 2));

    // A disabled row has no ring
    QTest::keyClick(card, Qt::Key_Tab, Qt::ShiftModifier);
    QTRY_VERIFY(row->hasFocus());
    row->setEnabled(false);
    QVERIFY(!row->getComponentState().hasFocusRing());
}

void FluentComponentsTest::baseAccessibility()
{
    TestRow row;
    row.setText(QStringLiteral("Merge PDF"));
    row.setSubtitle(QStringLiteral("Combine multiple PDFs into one file"));

    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(&row);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::Button);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Merge PDF"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Combine multiple PDFs into one file"));
    QVERIFY(accessible->actionInterface());
    QVERIFY(accessible->actionInterface()->actionNames().contains(QAccessibleActionInterface::pressAction()));

    // The press action clicks the row
    QSignalSpy spy(&row, &QAbstractButton::clicked);
    accessible->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    QCOMPARE(spy.count(), 1);

    // A description of the application is kept, a new subtitle replaces the previous one
    row.setSubtitle(QStringLiteral("Other"));
    QCOMPARE(row.accessibleDescription(), QStringLiteral("Other"));
    row.setAccessibleDescription(QStringLiteral("Own"));
    row.setSubtitle(QStringLiteral("Third"));
    QCOMPARE(row.accessibleDescription(), QStringLiteral("Own"));

    // The name with an ampersand is the text as it is drawn
    row.setText(QStringLiteral("Save & close"));
    QCOMPARE(row.getAccessibleText(), QStringLiteral("Save & close"));
    row.setAccessibleName(QStringLiteral("Own name"));
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Own name"));

    // A choice is a radio button with the checked state
    TestCard card;
    card.setText(QStringLiteral("Add Page Numbers"));
    card.setSelectionBehavior(pdf::PDFComponentButton::SelectionBehavior::Deselectable);
    QAccessibleInterface* cardAccessible = QAccessible::queryAccessibleInterface(&card);
    QCOMPARE(cardAccessible->role(), QAccessible::RadioButton);
    QVERIFY(!cardAccessible->state().checked);
    cardAccessible->actionInterface()->doAction(QAccessibleActionInterface::toggleAction());
    QVERIFY(card.isChecked());
    QVERIFY(cardAccessible->state().checked);
    QCOMPARE(cardAccessible->text(QAccessible::Name), QStringLiteral("Add Page Numbers"));
}

void FluentComponentsTest::baseTrailingWidgets()
{
    TestRow row;
    row.resize(380, 48);
    row.show();
    QVERIFY(QTest::qWaitForWindowExposed(&row));
    QCOMPARE(row.getTrailingRect(), QRect());
    QCOMPARE(row.getContentRect(QMargins(8, 0, 6, 0), 10), QRect(8, 0, 366, 48));

    // A row of widgets 12 px from the trailing edge with gaps of 12 px
    pdf::PDFStarButton* star = new pdf::PDFStarButton();
    pdf::PDFIconButton* more = new pdf::PDFIconButton(QStringLiteral("more-vertical"), pdf::PDFIconButton::Size::Small);
    row.addTrailingWidget(star);
    row.addTrailingWidget(more);
    row.addTrailingWidget(more);
    row.addTrailingWidget(nullptr);
    row.addTrailingWidget(&row);
    QCOMPARE(row.getTrailingWidgets().size(), 2);
    QCOMPARE(star->parentWidget(), &row);
    QCOMPARE(star->geometry(), QRect(380 - 12 - 24 - 12 - 28, 10, 28, 28));
    QCOMPARE(more->geometry(), QRect(380 - 12 - 24, 12, 24, 24));
    QCOMPARE(row.getTrailingRect(), QRect(380 - 12 - 64, 10, 64, 28));
    QCOMPARE(row.getContentRect(QMargins(8, 0, 6, 0), 10).right(), 380 - 12 - 64 - 1 - 10);

    // A hidden widget leaves its place, a resize moves the widgets
    star->hide();
    QApplication::processEvents();
    QCOMPARE(row.getTrailingRect(), QRect(380 - 12 - 24, 12, 24, 24));
    star->show();
    row.resize(400, 48);
    QCOMPARE(more->geometry(), QRect(400 - 12 - 24, 12, 24, 24));

    // A change of the size of a nested widget
    star->setStarSize(pdf::PDFStarButton::StarSize::Small);
    QApplication::sendPostedEvents();
    QApplication::processEvents();
    QCOMPARE(star->geometry(), QRect(400 - 12 - 24 - 12 - 24, 12, 24, 24));

    // A column at the top (actions of Pinned card)
    row.setTrailingOrientation(Qt::Vertical);
    row.setTrailingSpacing(4);
    row.setTrailingMargins(QMargins(0, 12, 8, 0));
    QCOMPARE(star->geometry(), QRect(400 - 8 - 24, 12, 24, 24));
    QCOMPARE(more->geometry(), QRect(400 - 8 - 24, 40, 24, 24));
    row.setTrailingSpacing(-5);
    QCOMPARE(row.getTrailingSpacing(), 0);

    // A removed widget is not placed any more, a deleted one is forgotten
    row.removeTrailingWidget(more);
    row.removeTrailingWidget(more);
    QCOMPARE(row.getTrailingWidgets().size(), 1);
    QVERIFY(!more->testAttribute(Qt::WA_NoMousePropagation));
    delete star;
    QCOMPARE(row.getTrailingWidgets().size(), 0);
    QCOMPARE(row.getTrailingRect(), QRect());
    row.resize(200, 48);
}

void FluentComponentsTest::defensive()
{
    // Null painter
    const Painter::State normal;
    Painter::paintToolBadge(nullptr, QRectF(0, 0, 32, 32), Painter::ToolBadge(), normal);
    Painter::paintTag(nullptr, QRectF(0, 0, 40, 21), Painter::Tag{ QStringLiteral("OCR") }, normal);
    Painter::paintTextLink(nullptr, QRectF(0, 0, 40, 16), Painter::TextLink{ QStringLiteral("Link") }, normal);
    Painter::paintStar(nullptr, QRectF(0, 0, 28, 28), Painter::StarSize::Medium, normal);
    Painter::paintColorSwatch(nullptr, QRectF(0, 0, 28, 28), Painter::ColorSwatch(), normal);
    Painter::paintPageThumbnail(nullptr, QRectF(0, 0, 32, 42), Painter::PageThumbnail(), normal);
    Painter::paintPageThumbnailShadow(nullptr, QRectF(0, 0, 32, 42));
    Painter::paintPageThumbnailFrame(nullptr, QRectF(0, 0, 32, 42), Painter::PageThumbnail(), normal);
    Painter::paintFocusRingOutside(nullptr, QRectF(0, 0, 10, 10), pdf::PDFStylePainter::Radii(2.0));
    Painter::paintFocusRingInside(nullptr, QRectF(0, 0, 10, 10), pdf::PDFStylePainter::Radii(2.0));

    // Empty and negative rectangles draw nothing, the state of the painter is restored
    const QImage empty = render([](QPainter&) { }, QSize(40, 40));
    for (const QRectF& rect : { QRectF(), QRectF(10, 10, 0, 20), QRectF(10, 10, -20, -20), QRectF(10, 10, 20, -5) })
    {
        const QImage image = render([&](QPainter& painter)
        {
            const QTransform transform = painter.transform();
            Painter::paintToolBadge(&painter, rect, Painter::ToolBadge{ QStringLiteral("grid"), pdf::PDFAccentHue::Red, Painter::BadgeSize::Large }, normal);
            Painter::paintTag(&painter, rect, Painter::Tag{ QStringLiteral("OCR") }, normal);
            Painter::paintTextLink(&painter, rect, Painter::TextLink{ QStringLiteral("Link") }, Painter::State(QStyle::State_Enabled | QStyle::State_HasFocus));
            Painter::paintStar(&painter, rect, Painter::StarSize::Medium, Painter::State(QStyle::State_Enabled | QStyle::State_On | QStyle::State_MouseOver));
            Painter::paintColorSwatch(&painter, rect, Painter::ColorSwatch{ Qt::red, Painter::SwatchKind::None }, Painter::State(QStyle::State_Enabled | QStyle::State_On | QStyle::State_HasFocus));
            Painter::paintPageThumbnail(&painter, rect, Painter::PageThumbnail(), normal);
            Painter::paintPageThumbnailShadow(&painter, rect);
            Painter::paintPageThumbnailFrame(&painter, rect, Painter::PageThumbnail(), normal);
            QCOMPARE(painter.transform(), transform);
            QCOMPARE(painter.hasClipping(), false);
        }, QSize(40, 40));
        QCOMPARE(image, empty);
    }

    // Small rectangles: the components shrink, nothing is drawn outside
    for (const QSizeF& size : { QSizeF(3, 3), QSizeF(1, 40), QSizeF(12, 5) })
    {
        const QRectF rect(QPointF(10, 10), size);
        const QImage image = render([&](QPainter& painter)
        {
            Painter::paintToolBadge(&painter, rect, Painter::ToolBadge{ QStringLiteral("grid"), pdf::PDFAccentHue::Red, Painter::BadgeSize::Large }, normal);
            Painter::paintTag(&painter, rect, Painter::Tag{ QStringLiteral("A long label") }, normal);
            Painter::paintStar(&painter, rect, Painter::StarSize::Medium, Painter::State(QStyle::State_Enabled | QStyle::State_On | QStyle::State_MouseOver));
            Painter::paintColorSwatch(&painter, rect, Painter::ColorSwatch{ Qt::red, Painter::SwatchKind::None }, Painter::State(QStyle::State_Enabled | QStyle::State_On));
            Painter::paintPageThumbnail(&painter, rect, Painter::PageThumbnail{ Painter::ThumbnailSize::Large, QPixmap() }, normal);
        }, QSize(60, 60));
        const QRect outside = rect.toAlignedRect().adjusted(-4, -4, 4, 4);
        for (int y = 0; y < 60; ++y)
        {
            for (int x = 0; x < 60; ++x)
            {
                if (!outside.contains(x, y))
                {
                    QVERIFY2(image.pixelColor(x, y) == empty.pixelColor(0, 0), qPrintable(QStringLiteral("%1 %2 (%3)").arg(x).arg(y).arg(size.width())));
                }
            }
        }
    }

    // Empty texts, unknown and empty icons, invalid colors
    const QImage image = render([&](QPainter& painter)
    {
        Painter::paintToolBadge(&painter, QRectF(0, 0, 32, 32), Painter::ToolBadge{ QStringLiteral("no-such-icon"), pdf::PDFAccentHue::Blue, Painter::BadgeSize::Medium }, normal);
        Painter::paintToolBadge(&painter, QRectF(0, 0, 32, 32), Painter::ToolBadge{ QString(), pdf::PDFAccentHue::Blue, Painter::BadgeSize::Medium }, normal);
        Painter::paintTag(&painter, QRectF(0, 0, 20, 21), Painter::Tag(), normal);
        Painter::paintTextLink(&painter, QRectF(0, 0, 4, 16), Painter::TextLink(), normal);
        Painter::paintColorSwatch(&painter, QRectF(0, 0, 28, 28), Painter::ColorSwatch{ QColor(), Painter::SwatchKind::Color }, normal);
    }, QSize(40, 40));
    QCOMPARE(countLeakPixels(image), 0);
    QCOMPARE(Painter::getTagSize(Painter::Tag()), QSize(20, 21));
    QCOMPARE(Painter::getTextLinkSize(Painter::TextLink()), QSize(4, 16));
    QCOMPARE(Painter::elidedText(QStringLiteral("Text"), Painter::getTagFont(), 0.0), QString());
    QCOMPARE(Painter::elidedText(QStringLiteral("Text"), Painter::getTagFont(), -5.0), QString());
    QCOMPARE(Painter::elidedText(QString(), Painter::getTagFont(), 50.0), QString());

    // Invalid values of the enumerations are replaced by the defaults
    QCOMPARE(Painter::normalized(static_cast<Painter::BadgeSize>(31)), Painter::BadgeSize::Medium);
    QCOMPARE(Painter::normalized(static_cast<pdf::PDFAccentHue>(-1)), pdf::PDFAccentHue::Neutral);
    QCOMPARE(Painter::normalized(static_cast<Painter::StarSize>(0)), Painter::StarSize::Medium);
    QCOMPARE(Painter::normalized(static_cast<Painter::SwatchKind>(9)), Painter::SwatchKind::Color);
    QCOMPARE(Painter::normalized(static_cast<Painter::ThumbnailSize>(9)), Painter::ThumbnailSize::Small);
    QCOMPARE(Painter::getBadgeDiameter(static_cast<Painter::BadgeSize>(1000)), 32);
    QCOMPARE(Painter::getPageThumbnailSize(static_cast<Painter::ThumbnailSize>(-3)), QSize(32, 42));
    QCOMPARE(Painter::getStarSize(static_cast<Painter::StarSize>(99)), QSize(28, 28));

    pdf::PDFToolBadge badge;
    badge.setBadgeSize(static_cast<Painter::BadgeSize>(7));
    QCOMPARE(badge.getBadgeSize(), Painter::BadgeSize::Medium);
    badge.setHue(static_cast<pdf::PDFAccentHue>(100));
    QCOMPARE(badge.getHue(), pdf::PDFAccentHue::Neutral);
    badge.setIconName(QStringLiteral("no-such-icon"));
    badge.resize(badge.sizeHint());
    QCOMPARE(countLeakPixels(badge.grab().toImage()), 0);
    pdf::PDFPageThumbnail thumbnail;
    thumbnail.setThumbnailSize(static_cast<Painter::ThumbnailSize>(12));
    QCOMPARE(thumbnail.getThumbnailSize(), Painter::ThumbnailSize::Small);
    thumbnail.setPixmap(QPixmap());
    thumbnail.resize(1, 1);
    thumbnail.grab();
    thumbnail.resize(0, 0);
    thumbnail.grab();

    // Widgets with zero size
    pdf::PDFTag tag;
    tag.resize(0, 0);
    tag.grab();
    TestCard card;
    card.setFocusRingRadius(-3.0);
    QCOMPARE(card.getFocusRingRadius(), 0.0);
    QCOMPARE(card.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 0.0);
    card.setFocusRingRadius(qQNaN());
    QCOMPARE(card.getFocusRingRadius(), 0.0);
    card.setTrailingOrientation(static_cast<Qt::Orientation>(17));
    QCOMPARE(card.getTrailingOrientation(), Qt::Horizontal);
    card.resize(0, 0);
    card.addTrailingWidget(new pdf::PDFStarButton());
    card.grab();
    QVERIFY(card.getContentRect(QMargins(20, 20, 20, 20), 8).width() >= 0);
    QVERIFY(card.getContentRect(QMargins(20, 20, 20, 20), 8).height() >= 0);
}

void FluentComponentsTest::mnemonics()
{
    // The text is data: an ampersand is drawn, measured and announced, it creates no shortcut (C2)
    TestRow row;
    row.setText(QStringLiteral("Protect & Sign"));
    QCOMPARE(row.shortcut(), QKeySequence());
    QCOMPARE(row.getAccessibleText(), QStringLiteral("Protect & Sign"));
    QCOMPARE(QAccessible::queryAccessibleInterface(&row)->text(QAccessible::Name), QStringLiteral("Protect & Sign"));

    // A shortcut of the application is kept
    const QKeySequence own(Qt::CTRL | Qt::Key_P);
    row.setShortcut(own);
    row.setText(QStringLiteral("Review & Inspect"));
    QCOMPARE(row.shortcut(), own);

    pdf::PDFTag tag(QStringLiteral("R&D"));
    QCOMPARE(tag.shortcut(), QKeySequence());
    QCOMPARE(tag.sizeHint().width(), qCeil(pdf::PDFStylePainter::textWidth(QStringLiteral("R&D"), Painter::getTagFont())) + 20);
    QCOMPARE(QAccessible::queryAccessibleInterface(&tag)->text(QAccessible::Name), QStringLiteral("R&D"));
    pdf::PDFTextLink link(QStringLiteral("Terms & Conditions"));
    QCOMPARE(link.shortcut(), QKeySequence());
    QCOMPARE(link.sizeHint(), Painter::getTextLinkSize(Painter::TextLink{ QStringLiteral("Terms & Conditions") }));

    // The text set through QAbstractButton loses its mnemonic, when the button is polished
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    TestRow* polished = new TestRow(&window);
    static_cast<QAbstractButton*>(polished)->setText(QStringLiteral("Merge &PDF"));
    QVERIFY(!polished->shortcut().isEmpty());
    TestRow* pressed = new TestRow(&window);
    layout->addWidget(polished);
    layout->addWidget(pressed);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QCOMPARE(polished->shortcut(), QKeySequence());

    // ... or when its mnemonic is pressed; the button is not activated by it
    static_cast<QAbstractButton*>(pressed)->setText(QStringLiteral("&Open"));
    QCOMPARE(pressed->shortcut(), QKeySequence(Qt::ALT | Qt::Key_O));
    QSignalSpy spy(pressed, &QAbstractButton::clicked);
    QTest::keyClick(&window, Qt::Key_O, Qt::AltModifier);
    QTest::qWait(250);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(pressed->shortcut(), QKeySequence());
}

void FluentComponentsTest::selectionByMouseAndKeys()
{
    using Behavior = pdf::PDFComponentButton::SelectionBehavior;

    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    QButtonGroup choices;
    QButtonGroup pages;
    QButtonGroup swatches;
    swatches.setExclusive(false);

    TestCard* first = new TestCard(&window);
    TestCard* second = new TestCard(&window);
    TestCard* current = new TestCard(&window);
    TestCard* other = new TestCard(&window);
    TestCard* kept = new TestCard(&window);
    for (TestCard* card : { first, second })
    {
        card->setSelectionBehavior(Behavior::Deselectable);
        choices.addButton(card);
    }
    for (TestCard* card : { current, other })
    {
        card->setSelectionBehavior(Behavior::ExclusiveStatic);
        pages.addButton(card);
    }
    kept->setSelectionBehavior(Behavior::Exclusive);
    swatches.addButton(kept);
    for (TestCard* card : { first, second, current, other, kept })
    {
        layout->addWidget(card);
    }
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    // Deselectable: the mouse, Space and Enter unselect the selected item, also
    // in an exclusive group, which stays exclusive (C1)
    QSignalSpy firstClicked(first, &QAbstractButton::clicked);
    bool isExclusiveInSlot = false;
    connect(first, &QAbstractButton::clicked, first, [&]() { isExclusiveInSlot = choices.exclusive(); });
    QTest::mouseClick(first, Qt::LeftButton);
    QVERIFY(first->isChecked());
    QTest::mouseClick(first, Qt::LeftButton);
    QVERIFY(!first->isChecked());
    QVERIFY(!choices.checkedButton());
    QVERIFY(choices.exclusive());
    QVERIFY(isExclusiveInSlot);
    QCOMPARE(firstClicked.count(), 2);
    QTest::mouseClick(second, Qt::LeftButton);
    QTest::mouseClick(first, Qt::LeftButton);
    QVERIFY(first->isChecked());
    QVERIFY(!second->isChecked());

    first->setFocus(Qt::TabFocusReason);
    QTest::keyClick(first, Qt::Key_Space);
    QVERIFY(!first->isChecked());
    QTest::keyClick(first, Qt::Key_Space);
    QVERIFY(first->isChecked());
    QTest::keyClick(first, Qt::Key_Return);
    QVERIFY(!first->isChecked());
    QVERIFY(choices.exclusive());

    // A press released outside of the button does not unselect it, the group stays exclusive
    QTest::mouseClick(first, Qt::LeftButton);
    QVERIFY(first->isChecked());
    QTest::mousePress(first, Qt::LeftButton);
    QTest::mouseRelease(first, Qt::LeftButton, Qt::NoModifier, QPoint(-20, -20));
    QVERIFY(first->isChecked());
    QVERIFY(choices.exclusive());
    QCOMPARE(choices.checkedButton(), first);

    // ExclusiveStatic: nothing reacts on the selected item, no clicked signal (C5)
    current->click();
    QVERIFY(current->isChecked());
    QVERIFY(!current->isActivatable());
    QSignalSpy currentClicked(current, &QAbstractButton::clicked);
    QSignalSpy currentPressed(current, &QAbstractButton::pressed);
    QTest::mouseClick(current, Qt::LeftButton);
    current->setFocus(Qt::TabFocusReason);
    QTest::keyClick(current, Qt::Key_Space);
    QTest::keyClick(current, Qt::Key_Return);
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(current);
    QVERIFY(accessible && accessible->actionInterface());
    QVERIFY(!accessible->actionInterface()->actionNames().contains(QAccessibleActionInterface::toggleAction()));
    QVERIFY(!accessible->actionInterface()->actionNames().contains(QAccessibleActionInterface::pressAction()));
    accessible->actionInterface()->doAction(QAccessibleActionInterface::toggleAction());
    QCOMPARE(currentClicked.count(), 0);
    QCOMPARE(currentPressed.count(), 0);
    QVERIFY(current->isChecked());
    QVERIFY(!current->isDown());

    // The other item is clicked as a button
    QSignalSpy otherClicked(other, &QAbstractButton::clicked);
    QTest::mouseClick(other, Qt::LeftButton);
    QCOMPARE(otherClicked.count(), 1);
    QVERIFY(other->isChecked());
    QVERIFY(!current->isChecked());
    QVERIFY(current->isActivatable());
    QVERIFY(accessible->actionInterface()->actionNames().contains(QAccessibleActionInterface::toggleAction()));

    // Exclusive in a group, which is not exclusive: a click keeps the selected item (C10)
    QTest::mouseClick(kept, Qt::LeftButton);
    QVERIFY(kept->isChecked());
    QTest::mouseClick(kept, Qt::LeftButton);
    QVERIFY(kept->isChecked());
    kept->click();
    QVERIFY(kept->isChecked());
}

void FluentComponentsTest::independentGroups()
{
    using Behavior = pdf::PDFComponentButton::SelectionBehavior;

    // Two choices and a radio button laid out on one page do not exclude each other (C3)
    QWidget page;
    QVBoxLayout* layout = new QVBoxLayout(&page);
    QButtonGroup themes;
    QButtonGroup modes;
    TestCard* light = new TestCard(&page);
    TestCard* dark = new TestCard(&page);
    TestCard* normal = new TestCard(&page);
    TestCard* sepia = new TestCard(&page);
    QRadioButton* radio = new QRadioButton(QStringLiteral("Option"), &page);
    for (TestCard* card : { light, dark })
    {
        card->setSelectionBehavior(Behavior::ExclusiveStatic);
        themes.addButton(card);
        layout->addWidget(card);
    }
    for (TestCard* card : { normal, sepia })
    {
        card->setSelectionBehavior(Behavior::ExclusiveStatic);
        modes.addButton(card);
        layout->addWidget(card);
    }
    layout->addWidget(radio);

    radio->setChecked(true);
    light->click();
    normal->click();
    QVERIFY(light->isChecked());
    QVERIFY(normal->isChecked());
    QVERIFY(radio->isChecked());
    dark->click();
    QVERIFY(dark->isChecked());
    QVERIFY(!light->isChecked());
    QVERIFY(normal->isChecked());
    QVERIFY(radio->isChecked());

    // Without a group each exclusive button is a choice of its own
    TestCard* single = new TestCard(&page);
    TestCard* another = new TestCard(&page);
    single->setSelectionBehavior(Behavior::Exclusive);
    another->setSelectionBehavior(Behavior::Deselectable);
    QVERIFY(!single->autoExclusive());
    QVERIFY(!another->autoExclusive());
    single->click();
    another->click();
    QVERIFY(single->isChecked());
    QVERIFY(another->isChecked());
    QVERIFY(radio->isChecked());
    QVERIFY(dark->isChecked());
    another->click();
    QVERIFY(!another->isChecked());
}

void FluentComponentsTest::selectionReentrancy()
{
    using Behavior = pdf::PDFComponentButton::SelectionBehavior;

    // A slot, which deletes the group while the item is unselected, breaks nothing (C4)
    QWidget parent;
    QPointer<QButtonGroup> group = new QButtonGroup(&parent);
    TestCard* card = new TestCard(&parent);
    card->setSelectionBehavior(Behavior::Deselectable);
    group->addButton(card);
    card->click();
    QVERIFY(card->isChecked());
    connect(card, &QAbstractButton::toggled, &parent, [&](bool isChecked) { if (!isChecked) { delete group.data(); } });
    card->click();
    QVERIFY(!group);
    QVERIFY(!card->isChecked());
    QVERIFY(!card->group());

    // ... or the button itself
    QButtonGroup other;
    QPointer<TestCard> doomed = new TestCard(&parent);
    doomed->setSelectionBehavior(Behavior::Deselectable);
    other.addButton(doomed);
    doomed->click();
    connect(doomed.data(), &QAbstractButton::toggled, &parent, [&](bool isChecked) { if (!isChecked) { delete doomed.data(); } });
    doomed->click();
    QVERIFY(!doomed);
    QVERIFY(other.exclusive());
    QVERIFY(other.buttons().isEmpty());

    // ... also during a click by the mouse, whose group was made non-exclusive for the click
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    QPointer<QButtonGroup> mouseGroup = new QButtonGroup(&window);
    TestCard* mouseCard = new TestCard(&window);
    TestCard* mouseOther = new TestCard(&window);
    for (TestCard* item : { mouseCard, mouseOther })
    {
        item->setSelectionBehavior(Behavior::Deselectable);
        mouseGroup->addButton(item);
        layout->addWidget(item);
    }
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::mouseClick(mouseCard, Qt::LeftButton);
    QVERIFY(mouseCard->isChecked());
    connect(mouseCard, &QAbstractButton::toggled, &window, [&](bool isChecked) { if (!isChecked) { delete mouseGroup.data(); } });
    QTest::mouseClick(mouseCard, Qt::LeftButton);
    QVERIFY(!mouseGroup);
    QVERIFY(!mouseCard->isChecked());
    QTest::mouseClick(mouseOther, Qt::LeftButton);
    QTest::mouseClick(mouseCard, Qt::LeftButton);
    QVERIFY(mouseOther->isChecked());
    QVERIFY(mouseCard->isChecked());
}

void FluentComponentsTest::selectionToNone()
{
    using Behavior = pdf::PDFComponentButton::SelectionBehavior;

    // The checked button of an exclusive group becomes a plain button: unchecked with the signal (C9)
    QWidget parent;
    QButtonGroup group;
    TestCard* first = new TestCard(&parent);
    TestCard* second = new TestCard(&parent);
    for (TestCard* card : { first, second })
    {
        card->setSelectionBehavior(Behavior::Exclusive);
        group.addButton(card);
    }
    first->click();
    QCOMPARE(group.checkedButton(), first);

    QSignalSpy toggled(first, &QAbstractButton::toggled);
    first->setSelectionBehavior(Behavior::None);
    QCOMPARE(toggled.count(), 1);
    QCOMPARE(toggled.front().front().toBool(), false);
    QVERIFY(!first->isChecked());
    QVERIFY(!first->isCheckable());
    QVERIFY(!group.checkedButton());
    QVERIFY(group.exclusive());
    second->click();
    QCOMPARE(group.checkedButton(), second);
    first->click();
    QCOMPARE(group.checkedButton(), second);
    QVERIFY(!first->isChecked());
}

void FluentComponentsTest::trailingLifetime()
{
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);
    TrailingRow* row = new TrailingRow(&window);
    layout->addWidget(row);
    pdf::PDFStarButton* star = new pdf::PDFStarButton();
    pdf::PDFIconButton* more = new pdf::PDFIconButton(QStringLiteral("more-vertical"), pdf::PDFIconButton::Size::Small);
    row->addTrailingWidget(star);
    row->addTrailingWidget(more);
    window.resize(400, 48);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(row->width(), 400);
    QCOMPARE(star->geometry(), QRect(400 - 12 - 24 - 12 - 28, 10, 28, 28));
    QCOMPARE(layout->sizeHint().width(), 200 + 28 + 12 + 24);

    // The size hint follows the nested widgets, the layout gets the new size (C6)
    star->hide();
    QApplication::sendPostedEvents();
    QApplication::processEvents();
    QCOMPARE(layout->sizeHint().width(), 200 + 24);
    star->show();
    QApplication::sendPostedEvents();
    QApplication::processEvents();
    QCOMPARE(layout->sizeHint().width(), 200 + 28 + 12 + 24);

    // A deleted widget leaves no gap (C7)
    delete more;
    QApplication::processEvents();
    QCOMPARE(row->getTrailingWidgets().size(), 1);
    QCOMPARE(star->geometry(), QRect(400 - 12 - 28, 10, 28, 28));
    QApplication::sendPostedEvents();
    QApplication::processEvents();
    QCOMPARE(layout->sizeHint().width(), 200 + 28);

    // A widget moved to another parent is forgotten and not placed any more (C7)
    QWidget other;
    star->setParent(&other);
    QCOMPARE(row->getTrailingWidgets().size(), 0);
    QCOMPARE(row->getTrailingRect(), QRect());
    QVERIFY(!star->testAttribute(Qt::WA_NoMousePropagation));
    const QRect starGeometry = star->geometry();
    window.resize(300, 48);
    QApplication::sendPostedEvents();
    QApplication::processEvents();
    QCOMPARE(star->geometry(), starGeometry);
    QCOMPARE(row->getContentRect(QMargins(), 0), QRect(0, 0, row->width(), 48));

    // A widget hidden by the application stays hidden, the others are shown (C7)
    pdf::PDFIconButton* hidden = new pdf::PDFIconButton(QStringLiteral("x"), pdf::PDFIconButton::Size::Small);
    hidden->hide();
    row->addTrailingWidget(hidden);
    QVERIFY(hidden->isHidden());
    pdf::PDFIconButton* child = new pdf::PDFIconButton(QStringLiteral("copy"), pdf::PDFIconButton::Size::Small, row);
    row->addTrailingWidget(child);
    QVERIFY(child->isVisible());
    QVERIFY(child->testAttribute(Qt::WA_NoMousePropagation));
    row->addTrailingWidget(&window);
    QCOMPARE(row->getTrailingWidgets().size(), 2);

    // A click on a disabled nested widget does not activate the row (C27)
    QSignalSpy rowClicked(row, &QAbstractButton::clicked);
    child->setEnabled(false);
    QTest::mouseClick(child, Qt::LeftButton);
    QCOMPARE(rowClicked.count(), 0);
    QTest::mouseClick(row, Qt::LeftButton, Qt::NoModifier, QPoint(20, 24));
    QCOMPARE(rowClicked.count(), 1);
}

void FluentComponentsTest::accessibilityEvents()
{
    const QAccessible::UpdateHandler previousHandler = QAccessible::installUpdateHandler(&FluentComponentsTest::collectAccessibleEvent);
    s_accessibleEvents.clear();

    // The name of the star is its state: a change is announced (C12)
    pdf::PDFStarButton star;
    star.setPinned(true);
    QCOMPARE(countAccessibleEvents(&star, QAccessible::NameChanged), 1);
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(&star);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Unpin"));
    QCOMPARE(accessible->text(QAccessible::Description), QString());
    star.setToolTip(QStringLiteral("Pin the tool to Home"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Pin the tool to Home"));
    star.setToolTip(QString());
    star.setAccessibleName(QStringLiteral("Pinned tool"));
    s_accessibleEvents.clear();
    star.setPinned(false);
    QCOMPARE(countAccessibleEvents(&star, QAccessible::NameChanged), 0);

    // A new language takes the texts of the state again
    star.setToolTip(QString());
    QEvent languageChange(QEvent::LanguageChange);
    QApplication::sendEvent(&star, &languageChange);
    QCOMPARE(star.toolTip(), QStringLiteral("Pin file"));

    // The name of a swatch follows its color, its kind and its name
    pdf::PDFColorSwatch swatch(QColor(0x12, 0x34, 0x56));
    s_accessibleEvents.clear();
    swatch.setColor(QColor(0x65, 0x43, 0x21));
    QCOMPARE(countAccessibleEvents(&swatch, QAccessible::NameChanged), 1);
    swatch.setColorName(QStringLiteral("Brown"));
    QCOMPARE(countAccessibleEvents(&swatch, QAccessible::NameChanged), 2);
    swatch.setColor(Qt::red);
    QCOMPARE(countAccessibleEvents(&swatch, QAccessible::NameChanged), 2);
    swatch.setColorName(QString());
    QCOMPARE(countAccessibleEvents(&swatch, QAccessible::NameChanged), 3);
    swatch.setKind(pdf::PDFColorSwatch::Kind::None);
    QCOMPARE(countAccessibleEvents(&swatch, QAccessible::NameChanged), 4);
    QCOMPARE(QAccessible::queryAccessibleInterface(&swatch)->text(QAccessible::Name), QStringLiteral("No color"));

    // The role of a tag changes with its clickability
    pdf::PDFTag tag(QStringLiteral("OCR"));
    s_accessibleEvents.clear();
    tag.setClickable(true);
    QCOMPARE(countAccessibleEvents(&tag, QAccessible::RoleChanged), 1);
    QCOMPARE(QAccessible::queryAccessibleInterface(&tag)->role(), QAccessible::Button);

    QAccessible::installUpdateHandler(previousHandler);
}

void FluentComponentsTest::focusFrame()
{
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    TestRow* row = new TestRow(&window);
    BareOutsideButton* bare = new BareOutsideButton(&window);
    TestCard* card = new TestCard(&window);
    layout->addWidget(row);
    layout->addWidget(bare);
    layout->addWidget(card);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    auto findFrame = [&window]() -> QFocusFrame*
    {
        const QList<QFocusFrame*> frames = window.findChildren<QFocusFrame*>();
        for (QFocusFrame* frame : frames)
        {
            if (frame->widget())
            {
                return frame;
            }
        }
        return nullptr;
    };

    // A button with the ring outside, which has no radius of its own, has the ring (C8)
    QTest::mouseClick(row, Qt::LeftButton);
    QTest::keyClick(row, Qt::Key_Tab);
    QTRY_VERIFY(bare->hasFocus());
    QTRY_VERIFY(findFrame());
    QCOMPARE(findFrame()->widget(), bare);

    // A new radius repaints the whole ring, which is shown (C20)
    QTest::keyClick(bare, Qt::Key_Tab);
    QTRY_VERIFY(card->hasFocus());
    QTRY_VERIFY(findFrame() && findFrame()->widget() == card);
    QFocusFrame* frame = findFrame();
    QApplication::processEvents();

    struct PaintFilter : public QObject
    {
        bool isRingRepainted = false;
        virtual bool eventFilter(QObject*, QEvent* event) override
        {
            // The corner of the frame lies outside of the card
            if (event->type() == QEvent::Paint && static_cast<QPaintEvent*>(event)->region().contains(QPoint(0, 0)))
            {
                isRingRepainted = true;
            }
            return false;
        }
    } filter;
    frame->installEventFilter(&filter);
    card->setFocusRingRadius(4.0);
    QCOMPARE(card->property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 4.0);
    QTRY_VERIFY(filter.isRingRepainted);
    frame->removeEventFilter(&filter);
}

void FluentComponentsTest::apiDetails()
{
    using Behavior = pdf::PDFComponentButton::SelectionBehavior;
    const QColor surface = token(Color::BgSurface);

    // The state is not converted from the flags implicitly; flags without State_Enabled are disabled (C21)
    static_assert(!std::is_convertible_v<QStyle::State, Painter::State>);
    static_assert(std::is_constructible_v<Painter::State, QStyle::State>);
    QVERIFY(!Painter::State(QStyle::State_None).isEnabled());
    QVERIFY(Painter::State().isEnabled());

    // Checked and selected are told apart (C22)
    const Painter::State checked(QStyle::State_Enabled | QStyle::State_On);
    QVERIFY(checked.isChecked());
    QVERIFY(!checked.isSelected());
    QVERIFY(checked.isOn());
    const Painter::State selected(QStyle::State_Enabled | QStyle::State_Selected);
    QVERIFY(!selected.isChecked());
    QVERIFY(selected.isSelected());
    QVERIFY(selected.isOn());

    // Enumerations and properties for the meta-object system (C24)
    QCOMPARE(QByteArray(QMetaEnum::fromType<Behavior>().valueToKey(int(Behavior::Deselectable))), QByteArray("Deselectable"));
    QCOMPARE(QByteArray(QMetaEnum::fromType<pdf::PDFComponentButton::FocusRing>().valueToKey(int(pdf::PDFComponentButton::FocusRing::Outside))), QByteArray("Outside"));
    pdf::PDFStarButton star;
    QSignalSpy pinnedSpy(&star, &pdf::PDFStarButton::pinnedChanged);
    QVERIFY(star.setProperty("pinned", true));
    QVERIFY(star.isPinned());
    QCOMPARE(pinnedSpy.count(), 1);
    QCOMPARE(star.property("pinned").toBool(), true);
    QVERIFY(star.metaObject()->property(star.metaObject()->indexOfProperty("pinned")).hasNotifySignal());
    pdf::PDFColorSwatch swatch;
    QVERIFY(swatch.setProperty("color", QColor(Qt::green)));
    QCOMPARE(swatch.getColor(), QColor(Qt::green));

    // A tag, which stops being clickable, gives its focus away and a text is not pressed by Space (C11, C19)
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    pdf::PDFTag* tag = new pdf::PDFTag(QStringLiteral("Forms"), &window);
    tag->setClickable(true);
    LabelRow* label = new LabelRow(&window);
    layout->addWidget(tag);
    layout->addWidget(label);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    tag->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(tag->hasFocus());
    QVERIFY(tag->testAttribute(Qt::WA_SetCursor));
    tag->setClickable(false);
    QVERIFY(!tag->hasFocus());
    QVERIFY(!tag->testAttribute(Qt::WA_SetCursor));
    label->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(label->hasFocus());
    QSignalSpy labelPressed(label, &QAbstractButton::pressed);
    QSignalSpy labelClicked(label, &QAbstractButton::clicked);
    QTest::keyClick(label, Qt::Key_Space);
    QTest::keyClick(label, Qt::Key_Return);
    QTest::mouseClick(label, Qt::LeftButton);
    QCOMPARE(labelPressed.count(), 0);
    QCOMPARE(labelClicked.count(), 0);
    QVERIFY(!label->isActivatable());

    // The placeholder of a thumbnail is mirrored for right-to-left layouts (C16): the shorter
    // last line lies at the leading edge
    const QRectF frame(4, 4, 32, 42);
    const QImage ltr = render([&](QPainter& painter) { Painter::paintPageThumbnailFrame(&painter, frame, Painter::PageThumbnail(), state(QStyle::State_None)); }, QSize(40, 50));
    const QImage rtl = render([&](QPainter& painter) { Painter::paintPageThumbnailFrame(&painter, frame, Painter::PageThumbnail(), state(QStyle::State_None, Qt::RightToLeft)); }, QSize(40, 50));
    QVERIFY(isNear(pixel(ltr, 10, 20), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(ltr, 29, 20), surface));
    QVERIFY(isNear(pixel(rtl, 10, 20), surface));
    QVERIFY(isNear(pixel(rtl, 29, 20), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(rtl, 10, 14), token(Color::BorderStrong)));
}

void FluentComponentsTest::navItem()
{
    using Behavior = pdf::PDFComponentButton::SelectionBehavior;

    // The item of a navigation is a row with the static exclusive behavior (C17)
    pdf::PDFNavList list;
    list.addItem(QStringLiteral("Home"), QStringLiteral("home"));
    list.addItem(QStringLiteral("Open"), QStringLiteral("folder"));
    list.show();
    list.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&list));
    pdf::PDFNavItem* home = list.getItem(0);
    pdf::PDFNavItem* open = list.getItem(1);
    QVERIFY(qobject_cast<pdf::PDFRowButton*>(home));
    QCOMPARE(home->getSelectionBehavior(), Behavior::ExclusiveStatic);
    QCOMPARE(home->getFocusRing(), pdf::PDFComponentButton::FocusRing::Inside);
    QCOMPARE(QAccessible::queryAccessibleInterface(home)->role(), QAccessible::RadioButton);
    QCOMPARE(home->sizeHint(), QSize(190, 38));

    // A click on the current item does nothing
    QSignalSpy homeClicked(home, &QAbstractButton::clicked);
    QSignalSpy changed(&list, &pdf::PDFNavList::currentIndexChanged);
    QTest::mouseClick(home, Qt::LeftButton);
    home->setFocus(Qt::TabFocusReason);
    QTest::keyClick(home, Qt::Key_Return);
    QTest::keyClick(home, Qt::Key_Space);
    QCOMPARE(homeClicked.count(), 0);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(list.getCurrentIndex(), 0);
    home->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY(!home->getComponentState().isHovered());

    // Another item becomes the current one by the mouse and by Enter
    QTest::mouseClick(open, Qt::LeftButton);
    QCOMPARE(list.getCurrentIndex(), 1);
    QCOMPARE(changed.count(), 1);
    home->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY(home->getComponentState().isHovered());
    home->setFocus(Qt::TabFocusReason);
    QTest::keyClick(home, Qt::Key_Return);
    QCOMPARE(list.getCurrentIndex(), 0);
    QCOMPARE(changed.count(), 2);

    // No current item (a page outside of the navigation), then an item again;
    // an index beyond the items does nothing
    list.setCurrentIndex(-1);
    QCOMPARE(list.getCurrentIndex(), -1);
    QVERIFY(!home->isChecked());
    QVERIFY(!open->isChecked());
    QCOMPARE(changed.count(), 3);
    QCOMPARE(changed.last().at(0).toInt(), -1);
    list.setCurrentIndex(-1);
    list.setCurrentIndex(5);
    QCOMPARE(changed.count(), 3);
    QCOMPARE(list.getCurrentIndex(), -1);
    QTest::mouseClick(open, Qt::LeftButton);
    QCOMPARE(list.getCurrentIndex(), 1);
    QCOMPARE(changed.count(), 4);
    list.setCurrentIndex(0);
    QVERIFY(!open->isChecked());
    QCOMPARE(changed.count(), 5);

    // A slot of the item deletes the list, while the current item is cleared
    {
        pdf::PDFNavList* deletedList = new pdf::PDFNavList();
        deletedList->addItem(QStringLiteral("A"), QString());
        QPointer<pdf::PDFNavList> deletedGuard(deletedList);
        connect(deletedList->getItem(0), &QAbstractButton::toggled, this, [deletedList]() { delete deletedList; });
        deletedList->setCurrentIndex(-1);
        QVERIFY(deletedGuard.isNull());
    }

    // Invalid kind
    home->setKind(static_cast<pdf::PDFNavItem::Kind>(7));
    QCOMPARE(home->getKind(), pdf::PDFNavItem::Kind::Home);
    home->setKind(pdf::PDFNavItem::Kind::Settings);
    QCOMPARE(home->sizeHint(), QSize(300, 52));
}

void FluentComponentsTest::linkUnderline()
{
    // The underline is a whole pixel at the bottom of the line box of 16 px (F3); the
    // drawing does not depend on the antialiasing of the caller (samples of the gallery)
    const Painter::TextLink link{ QStringLiteral("View all") };
    const QSize size = Painter::getTextLinkSize(link);
    const int textWidth = qFloor(pdf::PDFStylePainter::textWidth(link.text, Painter::getTextLinkFont()));
    const QColor accent = token(Color::TextAccent);

    for (const qreal scale : { 1.0, 1.5, 2.0 })
    {
        auto paintHovered = [&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(QPointF(4, 4), size), link, state(QStyle::State_MouseOver)); };
        const QImage image = render(paintHovered, size + QSize(8, 8), scale);

        QImage plain(image.size(), QImage::Format_ARGB32_Premultiplied);
        plain.setDevicePixelRatio(scale);
        plain.fill(token(Color::BgSurface));
        {
            QPainter painter(&plain);
            paintHovered(painter);
        }
        const QString difference = describeDifference(image, plain, 0);
        QVERIFY2(difference.isEmpty(), qPrintable(difference));

        for (int x = 4 + 2; x < 4 + 2 + textWidth; ++x)
        {
            QVERIFY2(isNear(pixel(image, x, 4 + 15), accent, 3), qPrintable(QStringLiteral("%1 at %2").arg(pixel(image, x, 4 + 15).name()).arg(x)));
        }
        QCOMPARE(countPixels(image, QRect(4, 4 + 14, size.width(), 1), accent, 40), 0);
        QCOMPARE(countPixels(image, QRect(4, 4 + 16, size.width(), 1), accent, 40), 0);

        // Without the pointer there is no underline
        const QImage normal = render([&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(QPointF(4, 4), size), link, state(QStyle::State_None)); }, size + QSize(8, 8), scale);
        QCOMPARE(countPixels(normal, QRect(4, 4 + 15, size.width(), 1), accent, 40), 0);
    }

    // The widget under the pointer draws the same underline
    pdf::PDFTextLink widget(link.text);
    widget.resize(widget.sizeHint());
    widget.setAttribute(Qt::WA_UnderMouse, true);
    const QImage image = renderWidget(&widget, 1.0);
    QVERIFY(countPixels(image, QRect(2, 15, textWidth, 1), accent, 3) >= textWidth - 1);
}

void FluentComponentsTest::linkSmall()
{
    // The small link (MIT License of app-footer 28:217): Semi Bold 12 as the texts
    // around it, the line box of 16 px of the component (the same height, the same
    // focus ring and the underline at the bottom row)
    const Painter::TextLink small{ QStringLiteral("MIT License"), Painter::TextLinkSize::Small };
    const Painter::TextLink normal{ QStringLiteral("MIT License") };
    const QFont smallFont = Painter::getTextLinkFont(Painter::TextLinkSize::Small, false);
    QCOMPARE(smallFont.pixelSize(), 12);
    QCOMPARE(smallFont.weight(), QFont::DemiBold);
    QVERIFY(!smallFont.underline());
    QVERIFY(Painter::getTextLinkFont(Painter::TextLinkSize::Small, true).underline());
    QCOMPARE(Painter::getTextLinkFont(Painter::TextLinkSize::Default, false), Painter::getTextLinkFont());
    QCOMPARE(Painter::getTextLinkFont().pixelSize(), 13);
    QCOMPARE(Painter::getTextLinkFont(static_cast<Painter::TextLinkSize>(42), false).pixelSize(), 13);

    const QSize smallSize = Painter::getTextLinkSize(small);
    QCOMPARE(smallSize, QSize(qCeil(pdf::PDFStylePainter::textWidth(small.text, smallFont)) + 4, 16));
    QVERIFY(smallSize.width() < Painter::getTextLinkSize(normal).width());
    QCOMPARE(Painter::getTextLinkSize(Painter::TextLink{ QString(), Painter::TextLinkSize::Small }), QSize(4, 16));

    // Pixels in both themes at 1 / 1.5 / 2: the ink of the small link is lower and
    // narrower than the ink of the default one, both in text/accent, centered in
    // the box; the underline under the pointer is the bottom row of the box
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        const QColor accent = token(Color::TextAccent);
        for (const qreal scale : { 1.0, 1.5, 2.0 })
        {
            auto inkRows = [&](const Painter::TextLink& link, QStyle::State flags, int* first, int* last, int* right)
            {
                const QSize size = Painter::getTextLinkSize(link);
                const QImage image = render([&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(QPointF(0, 0), size), link, state(flags)); }, size, scale);
                *first = -1;
                *last = -1;
                *right = -1;
                for (int y = 0; y < image.height(); ++y)
                {
                    for (int x = 0; x < image.width(); ++x)
                    {
                        if (colorDistance(image.pixelColor(x, y), token(Color::BgSurface)) > 120)
                        {
                            *first = *first < 0 ? y : *first;
                            *last = y;
                            *right = qMax(*right, x);
                        }
                    }
                }
                return image;
            };

            int smallFirst = 0;
            int smallLast = 0;
            int smallRight = 0;
            int normalFirst = 0;
            int normalLast = 0;
            int normalRight = 0;
            const QImage smallImage = inkRows(small, QStyle::State_None, &smallFirst, &smallLast, &smallRight);
            inkRows(normal, QStyle::State_None, &normalFirst, &normalLast, &normalRight);
            QVERIFY(smallFirst >= 0 && normalFirst >= 0);
            QCOMPARE(countLeakPixels(smallImage), 0);
            QVERIFY2(smallLast - smallFirst < normalLast - normalFirst, qPrintable(QStringLiteral("%1-%2 / %3-%4").arg(smallFirst).arg(smallLast).arg(normalFirst).arg(normalLast)));
            QVERIFY(smallRight < normalRight);
            QVERIFY2(qAbs((smallFirst + smallLast + 1) - 16 * scale) <= 3 * scale, qPrintable(QStringLiteral("%1-%2 at %3").arg(smallFirst).arg(smallLast).arg(scale)));
            QVERIFY(isNear(strongestInk(smallImage, QRect(0, 0, smallSize.width(), 16), token(Color::BgSurface)), accent, 40));

            // The underline of the pointer: the last row of the box
            const QImage hovered = render([&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(QPointF(0, 0), smallSize), small, state(QStyle::State_MouseOver)); }, smallSize, scale);
            const int textWidth = qFloor(pdf::PDFStylePainter::textWidth(small.text, smallFont));
            QVERIFY(countPixels(hovered, QRect(2, 15, textWidth, 1), accent, 3) >= textWidth - 1);
        }
    }

    // The widget: the size follows the size of the text, an unknown size is the
    // default size, the widget draws as the painter
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    pdf::PDFTextLink link(small.text);
    QCOMPARE(link.getLinkSize(), Painter::TextLinkSize::Default);
    QCOMPARE(link.sizeHint(), Painter::getTextLinkSize(normal));
    link.setLinkSize(Painter::TextLinkSize::Small);
    QCOMPARE(link.getLinkSize(), Painter::TextLinkSize::Small);
    QCOMPARE(link.sizeHint(), smallSize);
    link.resize(link.sizeHint());
    for (const qreal scale : { 1.0, 2.0 })
    {
        const QImage widgetImage = renderWidget(&link, scale);
        const QImage painterImage = renderInWidget([&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(QPointF(0, 0), smallSize), small, state(QStyle::State_None)); }, smallSize, scale);
        const QString difference = describeDifference(widgetImage, painterImage, 2);
        QVERIFY2(difference.isEmpty(), qPrintable(difference));
    }
    link.setLinkSize(static_cast<Painter::TextLinkSize>(42));
    QCOMPARE(link.getLinkSize(), Painter::TextLinkSize::Default);
    QCOMPARE(link.sizeHint(), Painter::getTextLinkSize(normal));
}

void FluentComponentsTest::swatchGeometry()
{
    // The fill of the color does not bleed into its border of 1 px at 1x (F4)
    const Painter::ColorSwatch yellow{ token(Color::AnnotationYellow), Painter::SwatchKind::Color };
    QImage image = render([&](QPainter& painter) { Painter::paintColorSwatch(&painter, QRectF(0, 0, 28, 28), yellow, state(QStyle::State_None)); }, QSize(28, 28));
    QVERIFY2(isNear(pixel(image, 4, 14), token(Color::BorderStrong), 4), qPrintable(pixel(image, 4, 14).name()));
    QVERIFY2(isNear(pixel(image, 23, 14), token(Color::BorderStrong), 4), qPrintable(pixel(image, 23, 14).name()));
    QVERIFY2(isNear(pixel(image, 14, 4), token(Color::BorderStrong), 4), qPrintable(pixel(image, 14, 4).name()));
    QVERIFY(isNear(pixel(image, 14, 14), token(Color::AnnotationYellow)));
    QVERIFY(isNear(pixel(image, 6, 14), token(Color::AnnotationYellow)));

    // A translucent color is not tinted by the border
    QColor translucent = token(Color::AnnotationBlue);
    translucent.setAlpha(128);
    image = render([&](QPainter& painter) { Painter::paintColorSwatch(&painter, QRectF(0, 0, 28, 28), Painter::ColorSwatch{ translucent, Painter::SwatchKind::Color }, state(QStyle::State_None)); }, QSize(28, 28));
    QVERIFY(isNear(pixel(image, 14, 14), blend(translucent, token(Color::BgSurface)), 3));

    // The slash of no color lies on the center line drawn by Figma (F2): in the row 14 its
    // strongest pixel is x = 12 (the origin of the card without the shift would give 13)
    const Painter::ColorSwatch none{ QColor(), Painter::SwatchKind::None };
    image = render([&](QPainter& painter) { Painter::paintColorSwatch(&painter, QRectF(0, 0, 28, 28), none, state(QStyle::State_None)); }, QSize(28, 28));
    const QColor danger = token(Color::IconDanger);
    int bestX = -1;
    int bestDistance = std::numeric_limits<int>::max();
    for (int x = 8; x <= 18; ++x)
    {
        const QColor color = pixel(image, x, 14);
        const int distance = qAbs(color.red() - danger.red()) + qAbs(color.green() - danger.green()) + qAbs(color.blue() - danger.blue());
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestX = x;
        }
    }
    QCOMPARE(bestX, 12);
    QVERIFY2(isNear(pixel(image, 4, 14), token(Color::BorderStrong), 4), qPrintable(pixel(image, 4, 14).name()));
}

void FluentComponentsTest::thumbnailShadow()
{
    // The widget is the frame, its shadow is drawn into the parent around it (F1)
    QWidget parent;
    parent.resize(100, 140);
    pdf::PDFPageThumbnail* thumbnail = new pdf::PDFPageThumbnail(Painter::ThumbnailSize::Medium, &parent);
    thumbnail->move(10, 10);
    thumbnail->resize(thumbnail->sizeHint());
    QCOMPARE(thumbnail->geometry(), QRect(10, 10, 56, 74));
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));

    for (const QString& theme : { QStringLiteral("light"), QStringLiteral("dark") })
    {
        QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));
        const QColor surface = token(Color::BgSurface);
        for (const qreal scale : { 1.0, 2.0 })
        {
            const QImage image = renderWidget(&parent, scale);
            QVERIFY2(isNear(pixel(image, 10 + 28, 10), token(Color::BorderStrong), 6), qPrintable(pixel(image, 10 + 28, 10).name()));
            QVERIFY(isNear(pixel(image, 10 + 28, 10 + 65), surface, 3));

            // Below the frame lies the shadow, it is darker than the background of the
            // parent and it ends within the margins of the shadow
            const QColor background = pixel(image, 90, 120);
            const QColor below = pixel(image, 10 + 28, 10 + 74);
            QVERIFY2(below.lightness() < background.lightness(), qPrintable(QStringLiteral("%1 / %2").arg(below.name(), background.name())));
            QVERIFY(isNear(pixel(image, 10 + 28, 10 + 74 + 4), background, 1));
            QVERIFY(isNear(pixel(image, 10 + 56 + 3, 10 + 37), background, 1));
            QCOMPARE(countLeakPixels(image), 0);
        }

        // Rendered into a device (QWidget::grab, as on the screen): the frame lies at its place and the shadow below it
        const QImage grabbed = parent.grab().toImage();
        QVERIFY2(isNear(grabbed.pixelColor(10 + 28, 10), token(Color::BorderStrong), 6), qPrintable(grabbed.pixelColor(10 + 28, 10).name()));
        QVERIFY(isNear(grabbed.pixelColor(10 + 28, 10 + 65), surface, 3));
        QVERIFY(grabbed.pixelColor(10 + 28, 10 + 74) != grabbed.pixelColor(10 + 28, 10 + 74 + 8));
    }

    // The application may remove the shadow; the widget still draws the frame
    thumbnail->setGraphicsEffect(nullptr);
    const QImage image = renderWidget(&parent, 1.0);
    QVERIFY(isNear(pixel(image, 10 + 28, 10 + 74), pixel(image, 90, 120), 1));
    QVERIFY(isNear(pixel(image, 10 + 28, 10), token(Color::BorderStrong), 6));
}

void FluentComponentsTest::galleryPages()
{
    // The pages of the components exist, each component has its samples with
    // the nodes of the design, no page shows the leak color
    const QStringList expected = { QStringLiteral("19-shell-home"), QStringLiteral("20-catalog"), QStringLiteral("21-forms"), QStringLiteral("22-document"), QStringLiteral("23-panels") };

    for (const QString& theme : { QStringLiteral("light"), QStringLiteral("dark") })
    {
        QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));

        QScrollArea scrollArea;
        scrollArea.resize(1400, 900);
        scrollArea.show();
        QVERIFY(QTest::qWaitForWindowExposed(&scrollArea));

        const std::vector<pdffluentgallery::GalleryPages::Page> pages = pdffluentgallery::GalleryPages::createPages(nullptr);
        QStringList ids;
        QSet<QString> nodes;
        for (const pdffluentgallery::GalleryPages::Page& page : pages)
        {
            ids << page.id;
            if (!expected.contains(page.id))
            {
                continue;
            }

            scrollArea.takeWidget();
            scrollArea.setWidget(page.widget);
            scrollArea.setWidgetResizable(true);
            QApplication::processEvents();

            for (const pdffluentgallery::GallerySpecimen* specimen : page.widget->findChildren<pdffluentgallery::GallerySpecimen*>())
            {
                nodes.insert(specimen->getFigmaNode());
            }

            for (const qreal scale : { 1.0, 2.0 })
            {
                const QImage image = pdffluentgallery::GalleryPages::renderWidget(page.widget, scale);
                const int leakPixels = countLeakPixels(image);
                QVERIFY2(leakPixels == 0, qPrintable(QStringLiteral("%1 pixels of the leak color on the page %2 (%3, %4)").arg(leakPixels).arg(page.id, theme).arg(scale)));
            }
        }

        // The pages follow the page of the finished components
        const qsizetype componentsIndex = ids.indexOf(QStringLiteral("18-components"));
        QVERIFY(componentsIndex >= 0);
        QCOMPARE(ids.mid(componentsIndex + 1, expected.size()), expected);

        // Variants of the design: Text link, Star button, Page thumbnail, Tool icon, Tag, Color swatch
        for (const char* node : { "49:144", "49:150", "64:545", "64:566", "78:869", "78:882", "89:1268", "706:146711", "116:2351", "206:779", "773:147235",
                                  "49:160", "49:181", "49:198", "49:225", "49:236", "49:269", "50:243", "50:270", "64:508", "64:535",
                                  "89:1290", "89:1320", "89:1331", "89:1358", "192:816", "192:831", "103:1895", "103:1922", "103:1932", "103:1941",
                                  "206:818", "206:822", "206:829", "206:837", "206:844", "206:854", "206:862",
                                  "46:268", "46:270", "46:277", "46:284", "46:291", "46:298", "46:305", "46:312", "46:319", "46:326", "46:333", "46:340",
                                  "46:347", "46:354", "46:361", "46:368", "46:375", "46:382", "46:389", "46:396", "46:403", "46:410", "46:417", "46:424",
                                  "93:1387", "93:1395", "93:1403", "93:1411", "63:517", "63:527", "63:537", "63:547", "63:557", "63:567", "63:577", "63:587",
                                  "72:878", "72:888", "72:898", "72:908", "72:918", "72:928", "72:938", "72:948", "79:1027", "79:1049", "79:1071", "79:1093",
                                  "89:1368", "89:1376", "89:1384", "89:1392", "89:1400", "89:1408", "89:1416", "417:994", "417:999", "706:146750", "706:146766",
                                  "706:146755", "706:146773", "706:146760", "706:146780", "50:163", "50:165", "50:172", "50:179", "50:186", "50:193", "50:200",
                                  "50:207", "50:214", "50:221", "50:228", "50:235", "773:147242", "773:147249", "773:147256", "773:147263", "103:1845", "103:1852",
                                  "103:1859", "103:1866", "103:1873", "103:1880", "103:1887", "116:2354", "116:2370", "116:2386", "116:2402", "110:2154",
                                  "156:4146", "156:4154", "156:4162", "156:4170", "156:4178", "156:4188", "156:4198", "156:4208", "171:4413", "171:4429",
                                  "171:4445", "171:4461", "171:4477", "171:4494", "171:4511", "171:4528", "171:4367", "171:4376", "171:4385", "171:4394",
                                  "171:4403", "171:4412", "192:735", "192:743", "192:758", "192:773", "225:874", "225:890" })
        {
            QVERIFY2(nodes.contains(QString::fromLatin1(node)), node);
        }

        scrollArea.takeWidget();
        for (const pdffluentgallery::GalleryPages::Page& page : pages)
        {
            delete page.widget;
        }
    }
}

QColor FluentComponentsTest::strongestInk(const QImage& image, const QRect& area, const QColor& background)
{
    QColor best = background;
    int bestDistance = -1;
    for (int y = area.top(); y <= area.bottom(); ++y)
    {
        for (int x = area.left(); x <= area.right(); ++x)
        {
            const QColor color = pixel(image, x, y);
            const int distance = colorDistance(color, background);
            if (distance > bestDistance)
            {
                bestDistance = distance;
                best = color;
            }
        }
    }
    return best;
}

int FluentComponentsTest::colorDistance(const QColor& first, const QColor& second)
{
    return qAbs(first.red() - second.red()) + qAbs(first.green() - second.green()) + qAbs(first.blue() - second.blue());
}

/// Contents of the rows of the step 2 as the default variants of the design
class RowSamples
{
public:
    RowSamples() = delete;

    static Painter::LinkRow linkRow() { return Painter::LinkRow{ QStringLiteral("Report Issue"), QStringLiteral("Tell us about a problem"), QStringLiteral("alert-circle"), std::nullopt }; }

    static Painter::RecentFileRow recentFileRow()
    {
        Painter::RecentFileRow row;
        row.fileName = QStringLiteral("Project_Report.pdf");
        row.path = QStringLiteral("C:\\Users\\Jakub\\Documents");
        row.time = QStringLiteral("Today 10:24");
        return row;
    }

    static Painter::RecentToolRow recentToolRow() { return Painter::RecentToolRow{ QStringLiteral("Compress"), QStringLiteral("Today 11:02"), QStringLiteral("minimize-2"), pdf::PDFAccentHue::Amber }; }

    static Painter::WorkspaceRow workspaceRow()
    {
        Painter::WorkspaceRow row;
        row.name = QStringLiteral("Thesis review");
        row.meta = QStringLiteral("4 documents · Opened Yesterday 16:15");
        return row;
    }

    static Painter::ToolRow toolRow()
    {
        Painter::ToolRow row;
        row.name = QStringLiteral("Merge PDF");
        row.description = QStringLiteral("Combine multiple PDFs into one file");
        row.iconName = QStringLiteral("grid");
        return row;
    }

    static Painter::CollectionRow collectionRow()
    {
        Painter::CollectionRow row;
        row.name = QStringLiteral("Prepare for sharing");
        row.meta = QStringLiteral("3 tools · Compress, Sanitize, Sign");
        return row;
    }

    static Painter::ToolShortcut toolShortcut() { return Painter::ToolShortcut{ QStringLiteral("Merge PDF"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue }; }

    static Painter::IssueRow issueRow()
    {
        Painter::IssueRow row;
        row.number = QStringLiteral("#612");
        row.title = QStringLiteral("Crash when opening some encrypted documents");
        row.description = QStringLiteral("Documents protected with AES-256 and an empty user password now open correctly.");
        row.date = QStringLiteral("Sep 12, 2026");
        return row;
    }

    static Painter::VersionRow versionRow()
    {
        Painter::VersionRow row;
        row.version = QStringLiteral("v1.5.3");
        row.date = QStringLiteral("Sep 14, 2026");
        row.summary = QStringLiteral("One unified app, faster loading, timestamped signatures.");
        return row;
    }

    static Painter::PropertyRow propertyRow(bool editable, Painter::PropertyState state)
    {
        Painter::PropertyRow row;
        row.key = QStringLiteral("Author");
        row.value = QStringLiteral("Jane Novak");
        row.editable = editable;
        row.state = state;
        return row;
    }
};

void FluentComponentsTest::rowSizes()
{
    // Sizes of the default variants of the cards
    QCOMPARE(Painter::getLinkRowSize(RowSamples::linkRow()), QSize(328, 50));
    QCOMPARE(Painter::getRecentFileRowSize(RowSamples::recentFileRow()), QSize(381, 51));
    QCOMPARE(Painter::getRecentToolRowSize(RowSamples::recentToolRow()), QSize(342, 48));
    QCOMPARE(Painter::getWorkspaceRowSize(RowSamples::workspaceRow()), QSize(344, 52));
    QCOMPARE(Painter::getToolRowSize(RowSamples::toolRow()), QSize(380, 48));
    QCOMPARE(Painter::getCollectionRowSize(RowSamples::collectionRow()), QSize(324, 52));
    Painter::CollectionRow createRow = RowSamples::collectionRow();
    createRow.isCreate = true;
    QCOMPARE(Painter::getCollectionRowSize(createRow), QSize(324, 54));
    QVERIFY(Painter::getCollectionRowMinimumSize(createRow).width() < Painter::getCollectionRowSize(createRow).width());
    pdf::PDFCollectionRow createWidget(QStringLiteral("New collection…"), QStringLiteral("Group the tools you use for one task"));
    QVERIFY(!createWidget.isCreateVariant());
    createWidget.setCreateVariant(true);
    QVERIFY(createWidget.isCreateVariant());
    QCOMPARE(createWidget.sizeHint(), QSize(324, 54));
    QVERIFY(createWidget.getContent().isCreate);
    QCOMPARE(Painter::getToolShortcutSize(RowSamples::toolShortcut()), QSize(180, 48));
    QCOMPARE(Painter::getIssueRowSize(RowSamples::issueRow()), QSize(786, 52));
    QCOMPARE(Painter::getVersionRowSize(RowSamples::versionRow()), QSize(324, 60));
    QCOMPARE(Painter::getPropertyRowSize(RowSamples::propertyRow(false, Painter::PropertyState::Default)), QSize(326, 30));
    QCOMPARE(Painter::getToolShortcutRadius(), 10.0);
    QCOMPARE(Painter::getMoreButtonSize(), QSize(16, 16));

    // The texts of a link row are not elided: a long title widens the row
    Painter::LinkRow longLink = RowSamples::linkRow();
    longLink.title = QStringLiteral("Report an issue of the application on the page of the project at GitHub");
    const int titleWidth = qCeil(pdf::PDFStylePainter::textWidth(longLink.title, pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::BodySemiBold)));
    QCOMPARE(Painter::getLinkRowMinimumSize(longLink).width(), 10 + 16 + 12 + titleWidth + 12 + 14 + 10);
    QCOMPARE(Painter::getLinkRowSize(longLink).width(), Painter::getLinkRowMinimumSize(longLink).width());

    // The summary of a version row wraps, the row grows; without a summary it has the top line only
    Painter::VersionRow version = RowSamples::versionRow();
    QCOMPARE(Painter::getVersionRowHeight(version, 324), 60);
    QVERIFY(Painter::getVersionRowHeight(version, 200) > 60);
    QCOMPARE(Painter::getVersionRowHeight(version, 2000), 46);
    version.summary.clear();
    QCOMPARE(Painter::getVersionRowHeight(version, 324), 30);

    // Widgets have the sizes of the painter and place the nested widgets as the cards
    pdf::PDFLinkRow linkRow(QStringLiteral("Report Issue"), QStringLiteral("Tell us about a problem"), QStringLiteral("alert-circle"));
    QCOMPARE(linkRow.sizeHint(), QSize(328, 50));
    linkRow.setTitle(longLink.title);
    QCOMPARE(linkRow.sizeHint(), Painter::getLinkRowSize(longLink));
    QCOMPARE(linkRow.minimumSizeHint(), Painter::getLinkRowMinimumSize(longLink));

    pdf::PDFRecentFileRow fileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users\\Jakub\\Documents"), QStringLiteral("Today 10:24"));
    QCOMPARE(fileRow.sizeHint(), QSize(381, 51));
    fileRow.resize(fileRow.sizeHint());
    fileRow.show();
    QCOMPARE(fileRow.getStarButton()->geometry(), QRect(317, 13, 24, 24));
    QCOMPARE(fileRow.getMenuButton()->geometry(), QRect(353, 17, 16, 16));
    QCOMPARE(fileRow.getMenuButton()->property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 4.0);

    pdf::PDFRecentToolRow recentToolRow(QStringLiteral("Compress"), QStringLiteral("Today 11:02"), QStringLiteral("minimize-2"), pdf::PDFAccentHue::Amber);
    QCOMPARE(recentToolRow.sizeHint(), QSize(342, 48));

    pdf::PDFWorkspaceRow workspaceRow(QStringLiteral("Thesis review"), QStringLiteral("4 documents"));
    QCOMPARE(workspaceRow.sizeHint(), QSize(344, 52));
    workspaceRow.resize(workspaceRow.sizeHint());
    workspaceRow.show();
    QCOMPARE(workspaceRow.getMenuButton()->geometry(), QRect(320, 18, 16, 16));
    QCOMPARE(workspaceRow.getHue(), pdf::PDFAccentHue::Violet);
    QCOMPARE(workspaceRow.getIconName(), QStringLiteral("layers"));

    pdf::PDFToolRow toolRow(QStringLiteral("Merge PDF"), QStringLiteral("Combine multiple PDFs into one file"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    QCOMPARE(toolRow.sizeHint(), QSize(380, 48));
    toolRow.resize(toolRow.sizeHint());
    toolRow.show();
    QCOMPARE(toolRow.getStarButton()->geometry(), QRect(346, 10, 28, 28));
    QVERIFY(toolRow.getTagWidget()->isHidden());
    toolRow.setTag(QStringLiteral("Advanced"));
    QVERIFY(!toolRow.getTagWidget()->isHidden());
    const int tagWidth = Painter::getTagSize(Painter::Tag{ QStringLiteral("Advanced") }).width();
    QCOMPARE(toolRow.getTagWidget()->geometry(), QRect(346 - 10 - tagWidth, 13, tagWidth, 21));
    QCOMPARE(toolRow.getStarButton()->geometry(), QRect(346, 10, 28, 28));
    toolRow.setTag(QString());
    QVERIFY(toolRow.getTagWidget()->isHidden());

    pdf::PDFCollectionRow collectionRow(QStringLiteral("Prepare for sharing"), QStringLiteral("3 tools"));
    QCOMPARE(collectionRow.sizeHint(), QSize(324, 52));

    pdf::PDFToolShortcut shortcut(QStringLiteral("Merge PDF"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    QCOMPARE(shortcut.sizeHint(), QSize(180, 48));
    QCOMPARE(shortcut.getFocusRing(), pdf::PDFComponentButton::FocusRing::Outside);
    QCOMPARE(shortcut.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 10.0);

    pdf::PDFIssueRow issueRow(QStringLiteral("#612"), QStringLiteral("Crash"), QStringLiteral("Fixed"), QStringLiteral("Sep 12, 2026"));
    QCOMPARE(issueRow.sizeHint(), QSize(786, 52));
    QCOMPARE(issueRow.getStatusIconName(), QStringLiteral("check"));

    pdf::PDFVersionRow versionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14, 2026"), RowSamples::versionRow().summary);
    QCOMPARE(versionRow.sizeHint(), QSize(324, 60));
    QVERIFY(versionRow.hasHeightForWidth());
    QVERIFY(versionRow.sizePolicy().hasHeightForWidth());
    QCOMPARE(versionRow.heightForWidth(324), 60);
    QVERIFY(versionRow.heightForWidth(150) > 60);

    // Property row: copy in the reserved place, editable edit + copy, editing confirm + cancel
    pdf::PDFPropertyRow propertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"));
    QCOMPARE(propertyRow.sizeHint(), QSize(326, 30));
    propertyRow.resize(propertyRow.sizeHint());
    propertyRow.show();
    QCOMPARE(propertyRow.getCopyButton()->geometry(), QRect(298, 3, 24, 24));
    propertyRow.setEditable(true);
    QCOMPARE(propertyRow.getEditButton()->geometry(), QRect(274, 3, 24, 24));
    QCOMPARE(propertyRow.getCopyButton()->geometry(), QRect(298, 3, 24, 24));
    QVERIFY(propertyRow.startEditing());
    QCOMPARE(propertyRow.getConfirmButton()->geometry(), QRect(274, 3, 24, 24));
    QCOMPARE(propertyRow.getCancelButton()->geometry(), QRect(298, 3, 24, 24));
    QCOMPARE(propertyRow.getEditor()->geometry(), QRect(150, 3, 116, 24));
    QCOMPARE(propertyRow.getConfirmButton()->getIconColor(), std::optional<Color>(Color::IconAccent));
}

void FluentComponentsTest::rowSizeStability()
{
    // Each row has the size of its variant Default in each state
    pdf::PDFLinkRow linkRow(QStringLiteral("Report Issue"), QStringLiteral("Tell us about a problem"), QStringLiteral("alert-circle"));
    pdf::PDFRecentFileRow fileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today 10:24"));
    pdf::PDFRecentToolRow recentToolRow(QStringLiteral("Compress"), QStringLiteral("Today"), QStringLiteral("minimize-2"), pdf::PDFAccentHue::Amber);
    pdf::PDFWorkspaceRow workspaceRow(QStringLiteral("Thesis review"), QStringLiteral("4 documents"));
    pdf::PDFToolRow toolRow(QStringLiteral("Merge PDF"), QStringLiteral("Combine"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    pdf::PDFCollectionRow collectionRow(QStringLiteral("Prepare"), QStringLiteral("3 tools"));
    pdf::PDFToolShortcut shortcut(QStringLiteral("Merge PDF"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    pdf::PDFIssueRow issueRow(QStringLiteral("#612"), QStringLiteral("Crash"), QStringLiteral("Fixed"), QStringLiteral("Sep 12"));
    pdf::PDFVersionRow versionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14"), QStringLiteral("Summary"));
    pdf::PDFPropertyRow propertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), pdf::PDFPropertyRow::Editability::Editable);

    for (QAbstractButton* button : std::initializer_list<QAbstractButton*>{ &linkRow, &fileRow, &recentToolRow, &workspaceRow, &toolRow, &collectionRow, &shortcut, &issueRow, &versionRow, &propertyRow })
    {
        const QSize size = button->sizeHint();
        button->setAttribute(Qt::WA_UnderMouse, true);
        QCOMPARE(button->sizeHint(), size);
        button->setDown(true);
        QCOMPARE(button->sizeHint(), size);
        button->setDown(false);
        button->setLayoutDirection(Qt::RightToLeft);
        QCOMPARE(button->sizeHint(), size);
        button->setEnabled(false);
        QCOMPARE(button->sizeHint(), size);
        button->setEnabled(true);
    }

    // Star, status and the states of the property row do not change the size
    const QSize fileSize = fileRow.sizeHint();
    fileRow.setPinned(true);
    fileRow.setStatus(pdf::PDFRecentFileRow::Status::Missing);
    QCOMPARE(fileRow.sizeHint(), fileSize);
    fileRow.setStarVisible(false);
    QCOMPARE(fileRow.sizeHint(), fileSize);

    const QSize propertySize = propertyRow.sizeHint();
    propertyRow.copy();
    QCOMPARE(propertyRow.sizeHint(), propertySize);
    propertyRow.startEditing();
    QCOMPARE(propertyRow.sizeHint(), propertySize);
    propertyRow.setEditable(false);
    QCOMPARE(propertyRow.sizeHint(), propertySize);

    // A change of the theme keeps the sizes
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(fileRow.sizeHint(), fileSize);
    QCOMPARE(shortcut.sizeHint(), QSize(180, 48));
}

void FluentComponentsTest::rowPixels_data()
{
    pixelProbes_data();
}

void FluentComponentsTest::rowPixels()
{
    QFETCH(QString, theme);
    QFETCH(qreal, scale);
    QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));

    const QColor surface = token(Color::BgSurface);
    const QColor hover = blend(token(Color::BgHover), surface);
    const QColor pressed = blend(token(Color::BgPressed), surface);
    const QStyle::State none = QStyle::State_None;
    const QStyle::State over = QStyle::State_MouseOver;
    const QStyle::State down = QStyle::State_MouseOver | QStyle::State_Sunken;
    const QStyle::State focus = QStyle::State_HasFocus;

    // Nearer to the first token than to the second one
    auto isCloser = [](const QColor& color, const QColor& expected, const QColor& other)
    {
        return colorDistance(color, expected) < colorDistance(color, other);
    };

    // Link row: hover and press backgrounds, the focus ring inside, the chevron darker under the pointer
    const Painter::LinkRow link = RowSamples::linkRow();
    auto renderLink = [&](QStyle::State flags) { return render([&](QPainter& painter) { Painter::paintLinkRow(&painter, QRectF(0, 0, 328, 50), link, state(flags)); }, QSize(328, 50), scale); };
    QImage image = renderLink(none);
    QVERIFY(isNear(pixel(image, 20, 4), surface));
    QVERIFY(isCloser(strongestInk(image, QRect(304, 18, 14, 14), surface), token(Color::IconMuted), token(Color::IconDefault)));
    QVERIFY(isCloser(strongestInk(image, QRect(10, 17, 16, 16), surface), token(Color::IconDefault), token(Color::IconMuted)));
    image = renderLink(over);
    QVERIFY(isNear(pixel(image, 20, 4), hover));
    QVERIFY(isCloser(strongestInk(image, QRect(304, 18, 14, 14), hover), token(Color::IconDefault), token(Color::IconMuted)));
    image = renderLink(down);
    QVERIFY(isNear(pixel(image, 20, 4), pressed));
    image = renderLink(focus);
    QVERIFY(isNear(pixel(image, 160, 0), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 160, 1), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 160, 2), surface));
    image = render([&](QPainter& painter) { Painter::paintLinkRow(&painter, QRectF(0, 0, 328, 50), link, Painter::State(QStyle::State_MouseOver)); }, QSize(328, 50), scale);
    QVERIFY(isNear(pixel(image, 20, 4), surface));
    QCOMPARE(countPixels(image, QRect(38, 10, 120, 30), token(Color::TextPrimary), 8), 0);
    QVERIFY(countPixels(image, QRect(38, 10, 120, 30), token(Color::TextDisabled), 8) > 0);

    // Recent file row: border, hover with the strong border, focus ring, the badge of a missing file
    const Painter::RecentFileRow file = RowSamples::recentFileRow();
    auto renderFile = [&](const Painter::RecentFileRow& content, QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintRecentFileRow(&painter, QRectF(0, 0, 381, 51), content, state(flags)); }, QSize(381, 51), scale);
    };
    image = renderFile(file, none);
    QVERIFY(isNear(pixel(image, 190, 0), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, 190, 1), surface));
    QVERIFY(isCloser(strongestInk(image, QRect(12, 14, 18, 22), surface), token(Color::IconPdf), token(Color::IconMuted)));
    QVERIFY(countPixels(image, QRect(235, 18, 70, 15), token(Color::TextTertiary), 12) > 0);
    image = renderFile(file, over);
    QVERIFY(isNear(pixel(image, 190, 0), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(image, 190, 4), hover));
    QVERIFY(isCloser(strongestInk(image, QRect(353, 17, 16, 16), hover), token(Color::IconDefault), token(Color::IconMuted)));
    image = renderFile(file, down);
    QVERIFY(isNear(pixel(image, 190, 4), pressed));
    image = renderFile(file, focus);
    QVERIFY(isNear(pixel(image, 190, 0), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 190, 1), token(Color::BorderFocus)));
    QVERIFY(isCloser(strongestInk(image, QRect(353, 17, 16, 16), surface), token(Color::IconMuted), token(Color::IconDefault)));

    Painter::RecentFileRow missing = file;
    missing.status = Painter::FileStatus::Missing;
    image = renderFile(missing, none);
    const int badgeWidth = qCeil(pdf::PDFStylePainter::textWidth(Painter::getMissingFileText(), Painter::getTagFont())) + 16;
    QVERIFY(isNear(pixel(image, 305 - badgeWidth / 2.0, 17), token(Color::AccentRedBg)));
    QVERIFY(countPixels(image, QRect(305 - badgeWidth, 16, badgeWidth, 19), token(Color::TextDanger), 12) > 0);
    QVERIFY(isCloser(strongestInk(image, QRect(12, 14, 18, 22), surface), token(Color::IconMuted), token(Color::IconPdf)));
    QCOMPARE(countPixels(image, QRect(42, 10, 100, 16), token(Color::TextPrimary), 6), 0);

    Painter::RecentFileRow pinned = file;
    pinned.pinned = true;
    image = renderFile(pinned, none);
    QVERIFY(isNear(pixel(image, 317 + 12, 13 + 12), token(Color::IconSun), 12));
    pinned.paintNested = false;
    image = renderFile(pinned, none);
    QVERIFY(isNear(pixel(image, 317 + 12, 13 + 12), surface));

    // Recent tool row and Workspace row: badge, chevron / menu under the pointer
    image = render([&](QPainter& painter) { Painter::paintRecentToolRow(&painter, QRectF(0, 0, 342, 48), RowSamples::recentToolRow(), state(over)); }, QSize(342, 48), scale);
    QVERIFY(isNear(pixel(image, 13, 24), token(Color::AccentAmberBg)));
    QVERIFY(isNear(pixel(image, 100, 3), hover));
    QVERIFY(isCloser(strongestInk(image, QRect(316, 17, 14, 14), hover), token(Color::IconDefault), token(Color::IconMuted)));
    image = render([&](QPainter& painter) { Painter::paintWorkspaceRow(&painter, QRectF(0, 0, 344, 52), RowSamples::workspaceRow(), state(none)); }, QSize(344, 52), scale);
    QVERIFY(isNear(pixel(image, 13, 26), token(Color::AccentVioletBg)));
    QVERIFY(isCloser(strongestInk(image, QRect(320, 18, 16, 16), surface), token(Color::IconMuted), token(Color::IconDefault)));

    // Tool row: the star 28 at the trailing edge, the tag before it
    Painter::ToolRow tool = RowSamples::toolRow();
    tool.tag = QStringLiteral("Advanced");
    image = render([&](QPainter& painter) { Painter::paintToolRow(&painter, QRectF(0, 0, 380, 48), tool, state(down)); }, QSize(380, 48), scale);
    QVERIFY(isNear(pixel(image, 11, 24), token(Color::AccentBlueBg)));
    QVERIFY(isNear(pixel(image, 200, 3), pressed));
    const int toolTagWidth = Painter::getTagSize(Painter::Tag{ tool.tag }).width();
    QVERIFY(isNear(pixel(image, 336 - toolTagWidth / 2.0, 13), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, 336 - toolTagWidth / 2.0, 15), token(Color::BgSubtle)));

    // Collection row: the circle of the fixed colors, the chevron icon/muted at
    // rest and icon/default under the pointer as in its sibling rows (decision 29)
    image = render([&](QPainter& painter) { Painter::paintCollectionRow(&painter, QRectF(0, 0, 324, 52), RowSamples::collectionRow(), state(over)); }, QSize(324, 52), scale);
    const QColor circle = blend(token(Color::BgAccentSubtle), hover);
    QVERIFY(isNear(pixel(image, 13, 26), circle));
    QVERIFY(isCloser(strongestInk(image, QRect(18, 18, 16, 16), circle), token(Color::IconAccent), token(Color::IconDefault)));
    QVERIFY(isCloser(strongestInk(image, QRect(302, 19, 14, 14), hover), token(Color::IconDefault), token(Color::IconMuted)));
    image = render([&](QPainter& painter) { Painter::paintCollectionRow(&painter, QRectF(0, 0, 324, 52), RowSamples::collectionRow(), state(down)); }, QSize(324, 52), scale);
    QVERIFY(isNear(pixel(image, 100, 3), pressed));
    QVERIFY(isCloser(strongestInk(image, QRect(302, 19, 14, 14), pressed), token(Color::IconDefault), token(Color::IconMuted)));
    image = render([&](QPainter& painter) { Painter::paintCollectionRow(&painter, QRectF(0, 0, 324, 52), RowSamples::collectionRow(), state(none)); }, QSize(324, 52), scale);
    QVERIFY(isCloser(strongestInk(image, QRect(302, 19, 14, 14), surface), token(Color::IconMuted), token(Color::IconDefault)));
    image = render([&](QPainter& painter) { Painter::paintCollectionRow(&painter, QRectF(0, 0, 324, 52), RowSamples::collectionRow(), state(focus)); }, QSize(324, 52), scale);
    QVERIFY(isCloser(strongestInk(image, QRect(302, 19, 14, 14), surface), token(Color::IconMuted), token(Color::IconDefault)));

    // The create row (new-collection 272:12952): bg/subtle inside the border
    // border/default, the circle bg/surface with its border and the icon
    // icon/accent, no chevron; under the pointer bg/hover over bg/subtle
    Painter::CollectionRow createRow = RowSamples::collectionRow();
    createRow.isCreate = true;
    createRow.iconName = QStringLiteral("plus-circle");
    image = render([&](QPainter& painter) { Painter::paintCollectionRow(&painter, QRectF(0, 0, 324, 54), createRow, state(none)); }, QSize(324, 54), scale);
    QVERIFY(isNear(pixel(image, 200, 4), token(Color::BgSubtle)));
    QVERIFY(isNear(pixel(image, 200, 0), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, 14, 27), token(Color::BgSurface)));
    QVERIFY(isNear(pixel(image, 310, 27), token(Color::BgSubtle)));
    QVERIFY(isCloser(strongestInk(image, QRect(19, 19, 16, 16), token(Color::BgSurface)), token(Color::IconAccent), token(Color::IconMuted)));
    image = render([&](QPainter& painter) { Painter::paintCollectionRow(&painter, QRectF(0, 0, 324, 54), createRow, state(over)); }, QSize(324, 54), scale);
    QVERIFY(isNear(pixel(image, 200, 4), blend(token(Color::BgHover), token(Color::BgSubtle))));

    // Tool shortcut: surface and border, the neutral steps under the pointer, the focus ring outside without the border
    const Painter::ToolShortcut shortcut = RowSamples::toolShortcut();
    auto renderShortcut = [&](QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintToolShortcut(&painter, QRectF(4, 4, 180, 48), shortcut, state(flags)); }, QSize(188, 56), scale);
    };
    image = renderShortcut(none);
    QVERIFY(isNear(pixel(image, 94, 4), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, 94, 6), surface));
    image = renderShortcut(over);
    QVERIFY(isNear(pixel(image, 94, 4), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(image, 94, 7), token(Color::AccentNeutralBgHover)));
    image = renderShortcut(down);
    QVERIFY(isNear(pixel(image, 94, 7), token(Color::AccentNeutralBgPressed)));
    image = renderShortcut(focus);
    QVERIFY(isNear(pixel(image, 94, 2), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 94, 3), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 94, 4), surface));
    image = render([&](QPainter& painter) { Painter::paintToolShortcut(&painter, QRectF(4, 4, 180, 48), shortcut, state(focus), false); }, QSize(188, 56), scale);
    QVERIFY(isNear(pixel(image, 94, 3), surface));
    QVERIFY(isNear(pixel(image, 94, 4), surface));

    // Issue row: the green status circle, the number text/accent
    image = render([&](QPainter& painter) { Painter::paintIssueRow(&painter, QRectF(0, 0, 786, 52), RowSamples::issueRow(), state(none)); }, QSize(786, 52), scale);
    QVERIFY(isNear(pixel(image, 12, 26), token(Color::AccentGreenBg)));
    QVERIFY(countPixels(image, QRect(46, 18, 44, 16), token(Color::TextAccent), 12) > 0);
    QVERIFY(countPixels(image, QRect(690, 19, 86, 13), token(Color::TextMuted), 12) > 0);

    // Version row: the dot of the current version (only the dot, Figma 103:1950
    // of 19. 10. 2026), the line, the hidden line
    Painter::VersionRow version = RowSamples::versionRow();
    auto renderVersion = [&](const Painter::VersionRow& content, QStyle::State flags = QStyle::State_None)
    {
        return render([&](QPainter& painter) { Painter::paintVersionRow(&painter, QRectF(0, 0, 324, 60), content, state(flags)); }, QSize(324, 60), scale);
    };
    image = renderVersion(version);
    QVERIFY(isNear(pixel(image, 13, 9), token(Color::BgAccent)));
    QVERIFY(isNear(pixel(image, 13, 40), token(Color::BgTrack)));
    QCOMPARE(countPixels(image, QRect(32, 0, 60, 16), token(Color::TextAccent), 12), 0);
    QVERIFY(countPixels(image, QRect(32, 0, 60, 16), token(Color::TextPrimary), 12) > 0);
    QVERIFY(countPixels(image, QRect(32, 18, 284, 28), token(Color::TextTertiary), 12) > 0);
    QVERIFY(isNear(pixel(image, 200, 52), surface));
    version.current = false;
    version.lineVisible = false;
    image = renderVersion(version);
    QVERIFY(isNear(pixel(image, 13, 9), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(image, 13, 40), surface));
    QCOMPARE(countPixels(image, QRect(32, 0, 60, 16), token(Color::TextAccent), 12), 0);

    // Version row: the states of the link (bg/hover, bg/pressed, the ring inside)
    image = renderVersion(version, over);
    QVERIFY(isNear(pixel(image, 200, 52), hover));
    image = renderVersion(version, down);
    QVERIFY(isNear(pixel(image, 200, 52), pressed));
    image = renderVersion(version, focus);
    QVERIFY(isNear(pixel(image, 160, 1), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 200, 52), surface));

    // Selected (the shown version): bg/accent-subtle and the version text/accent,
    // the pointer and the press in the tone of the selection; also State_Selected
    version.selected = true;
    const QColor selectedFill = blend(token(Color::BgAccentSubtle), surface);
    image = renderVersion(version);
    QVERIFY(isNear(pixel(image, 200, 52), selectedFill));
    QVERIFY(countPixels(image, QRect(32, 0, 60, 16), token(Color::TextAccent), 12) > 0);
    image = renderVersion(version, over);
    QVERIFY(isNear(pixel(image, 200, 52), blend(token(Color::BgAccentSubtleHover), surface)));
    image = renderVersion(version, down);
    QVERIFY(isNear(pixel(image, 200, 52), blend(token(Color::BgAccentSubtlePressed), surface)));
    image = renderVersion(version, focus);
    QVERIFY(isNear(pixel(image, 160, 1), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 200, 52), selectedFill));
    version.selected = false;
    image = renderVersion(version, QStyle::State_Selected);
    QVERIFY(isNear(pixel(image, 200, 52), selectedFill));

    // Property row: the states of the design
    auto renderProperty = [&](const Painter::PropertyRow& content, QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), content, state(flags)); }, QSize(326, 30), scale);
    };
    image = renderProperty(RowSamples::propertyRow(false, Painter::PropertyState::Default), none);
    QVERIFY(isNear(pixel(image, 5, 15), surface));
    QVERIFY(isNear(strongestInk(image, QRect(298, 3, 24, 24), surface), surface));
    image = renderProperty(RowSamples::propertyRow(false, Painter::PropertyState::Default), over);
    QVERIFY(isNear(pixel(image, 5, 15), hover));
    QVERIFY(isCloser(strongestInk(image, QRect(303, 8, 14, 14), hover), token(Color::IconDefault), token(Color::IconMuted)));
    image = renderProperty(RowSamples::propertyRow(true, Painter::PropertyState::Default), none);
    QVERIFY(isCloser(strongestInk(image, QRect(279, 8, 14, 14), surface), token(Color::IconMuted), token(Color::IconDefault)));
    image = renderProperty(RowSamples::propertyRow(true, Painter::PropertyState::Copied), none);
    QVERIFY(isNear(pixel(image, 5, 15), hover));
    QVERIFY(isCloser(strongestInk(image, QRect(303, 8, 14, 14), hover), token(Color::AccentGreenIcon), token(Color::IconDefault)));
    image = renderProperty(RowSamples::propertyRow(true, Painter::PropertyState::Editing), none);
    QVERIFY(isNear(pixel(image, 200, 3), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 200, 4), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 262, 15), surface));
    QVERIFY(isNear(pixel(image, 146, 15), hover));
    QVERIFY(isCloser(strongestInk(image, QRect(279, 8, 14, 14), hover), token(Color::IconAccent), token(Color::IconMuted)));

    // No leak color anywhere
    QCOMPARE(countLeakPixels(image), 0);
}

void FluentComponentsTest::rowWidgetsMatchPainter()
{
    // The widgets with real nested widgets draw the same as the painter with the nested elements
    QWidget window;
    window.resize(820, 300);
    pdf::PDFRecentFileRow* fileRow = new pdf::PDFRecentFileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users\\Jakub\\Documents"), QStringLiteral("Today 10:24"), &window);
    fileRow->setGeometry(0, 0, 381, 51);
    fileRow->setPinned(true);
    pdf::PDFToolRow* toolRow = new pdf::PDFToolRow(QStringLiteral("Merge PDF"), QStringLiteral("Combine multiple PDFs into one file"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue, &window);
    toolRow->setGeometry(0, 60, 380, 48);
    toolRow->setTag(QStringLiteral("Advanced"));
    pdf::PDFWorkspaceRow* workspaceRow = new pdf::PDFWorkspaceRow(QStringLiteral("Thesis review"), QStringLiteral("4 documents · Opened Yesterday 16:15"), &window);
    workspaceRow->setGeometry(0, 120, 344, 52);
    pdf::PDFPropertyRow* propertyRow = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), pdf::PDFPropertyRow::Editability::Editable, &window);
    propertyRow->setGeometry(0, 180, 326, 30);
    window.setFocusPolicy(Qt::StrongFocus);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.setFocus(Qt::OtherFocusReason);
    QVERIFY(!fileRow->hasFocus());

    for (const qreal scale : { 1.0, 1.5, 2.0 })
    {
        // At the scale 1.5 a nested widget at an odd logical position starts in
        // the middle of a device pixel, Qt clips it to whole pixels: the
        // antialiased edges of the nested widgets differ slightly
        const int tolerance = qFuzzyCompare(scale, 1.5) ? 24 : 3;

        for (const bool hovered : { false, true })
        {
            for (QWidget* row : std::initializer_list<QWidget*>{ fileRow, toolRow, workspaceRow, propertyRow })
            {
                row->setAttribute(Qt::WA_UnderMouse, hovered);
            }

            const QStyle::State flags = hovered ? QStyle::State_MouseOver : QStyle::State_None;
            const QString prefix = QStringLiteral("scale %1, hovered %2").arg(scale).arg(hovered);

            Painter::RecentFileRow file = fileRow->getContent();
            file.paintNested = true;
            QString difference = describeDifference(renderWidget(fileRow, scale), renderInWidget([&](QPainter& painter) { Painter::paintRecentFileRow(&painter, QRectF(0, 0, 381, 51), file, state(flags)); }, QSize(381, 51), scale), tolerance);
            QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Recent file row (%1): %2").arg(prefix, difference)));

            Painter::ToolRow tool = toolRow->getContent();
            tool.paintNested = true;
            difference = describeDifference(renderWidget(toolRow, scale), renderInWidget([&](QPainter& painter) { Painter::paintToolRow(&painter, QRectF(0, 0, 380, 48), tool, state(flags)); }, QSize(380, 48), scale), tolerance);
            QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Tool row (%1): %2").arg(prefix, difference)));

            Painter::WorkspaceRow workspace = workspaceRow->getContent();
            workspace.paintNested = true;
            difference = describeDifference(renderWidget(workspaceRow, scale), renderInWidget([&](QPainter& painter) { Painter::paintWorkspaceRow(&painter, QRectF(0, 0, 344, 52), workspace, state(flags)); }, QSize(344, 52), scale), tolerance);
            QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Workspace row (%1): %2").arg(prefix, difference)));

            Painter::PropertyRow property = propertyRow->getContent();
            QCOMPARE(property.state, hovered ? Painter::PropertyState::Hover : Painter::PropertyState::Default);
            property.paintNested = true;
            difference = describeDifference(renderWidget(propertyRow, scale), renderInWidget([&](QPainter& painter) { Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), property, state(QStyle::State_None)); }, QSize(326, 30), scale), tolerance);
            QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Property row (%1): %2").arg(prefix, difference)));
        }

        // Copied: the check in the place of the copy button
        propertyRow->setAttribute(Qt::WA_UnderMouse, false);
        propertyRow->setCopiedDuration(60000);
        propertyRow->copy();
        Painter::PropertyRow copied = propertyRow->getContent();
        QCOMPARE(copied.state, Painter::PropertyState::Copied);
        copied.paintNested = true;
        QString difference = describeDifference(renderWidget(propertyRow, scale), renderInWidget([&](QPainter& painter) { Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), copied, state(QStyle::State_None)); }, QSize(326, 30), scale), tolerance);
        QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Property row copied (scale %1): %2").arg(scale).arg(difference)));
        propertyRow->hide();
        propertyRow->show();
        QVERIFY(!propertyRow->isCopied());

        // Editing: the frame and the actions confirm and cancel (the text is the field)
        QVERIFY(propertyRow->startEditing());
        Painter::PropertyRow editing = propertyRow->getContent();
        QCOMPARE(editing.state, Painter::PropertyState::Editing);
        editing.paintNested = true;
        editing.value.clear();
        QImage widgetImage = renderWidget(propertyRow, scale);
        const QImage painterImage = renderInWidget([&](QPainter& painter) { Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), editing, state(QStyle::State_None)); }, QSize(326, 30), scale);
        difference = describeDifference(widgetImage.copy(QRect(0, 0, qRound(140 * scale), qRound(30 * scale))), painterImage.copy(QRect(0, 0, qRound(140 * scale), qRound(30 * scale))), tolerance);
        QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Property row editing, key (scale %1): %2").arg(scale).arg(difference)));
        difference = describeDifference(widgetImage.copy(QRect(qRound(270 * scale), 0, qRound(56 * scale), qRound(30 * scale))), painterImage.copy(QRect(qRound(270 * scale), 0, qRound(56 * scale), qRound(30 * scale))), tolerance);
        QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Property row editing, actions (scale %1): %2").arg(scale).arg(difference)));
        propertyRow->cancelEditing();

        // No leak color anywhere
        QCOMPARE(countLeakPixels(renderWidget(&window, scale)), 0);
    }
}

void FluentComponentsTest::fileNameElision()
{
    const QFont font = pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::BodySemiBold);
    const QString name = QStringLiteral("Technical_Documentation_v2.pdf");
    const qreal fullWidth = pdf::PDFStylePainter::textWidth(name, font);
    const QChar ellipsis(0x2026);

    // A name, which fits, is kept
    QCOMPARE(Painter::elidedFileName(name, font, fullWidth + 1.0), name);

    // A longer name is elided in the middle and keeps its extension
    for (const qreal width : { fullWidth - 10.0, fullWidth / 2.0, fullWidth / 3.0 })
    {
        const QString elided = Painter::elidedFileName(name, font, width);
        QVERIFY2(elided.endsWith(QStringLiteral(".pdf")), qPrintable(elided));
        QVERIFY2(elided.contains(ellipsis), qPrintable(elided));
        QVERIFY2(elided.startsWith(QStringLiteral("Te")), qPrintable(elided));
        QVERIFY2(pdf::PDFStylePainter::textWidth(elided, font) <= width + 1.0, qPrintable(elided));
    }
    QVERIFY(Painter::elidedFileName(name, font, fullWidth / 2.0).contains(QStringLiteral("v2.pdf")));

    // Not even the extension fits: the name is elided at its end, it never overflows
    const qreal tinyWidth = pdf::PDFStylePainter::textWidth(QStringLiteral(".pdf"), font);
    QVERIFY(pdf::PDFStylePainter::textWidth(Painter::elidedFileName(name, font, tinyWidth), font) <= tinyWidth + 1.0);

    // Names without an extension are elided at their end
    const QFontMetricsF metrics(font);
    for (const QString& plain : { QStringLiteral("README_OF_THE_PROJECT_WITHOUT_EXTENSION"), QStringLiteral(".profile_of_the_user_of_the_application"), QStringLiteral("folder.v2\\file_of_the_application_long"), QStringLiteral("name_ending_with_a_dot_of_the_file.") })
    {
        QCOMPARE(Painter::elidedFileName(plain, font, 80.0), metrics.elidedText(plain, Qt::ElideRight, 80.0));
    }

    // Defensive inputs
    QCOMPARE(Painter::elidedFileName(QString(), font, 100.0), QString());
    QCOMPARE(Painter::elidedFileName(name, font, 0.0), QString());
    QCOMPARE(Painter::elidedFileName(name, font, -5.0), QString());
    QCOMPARE(Painter::elidedFileName(name, font, qQNaN()), QString());

    // The widget keeps the full name (accessible name), only the drawing is elided
    pdf::PDFRecentFileRow row(name, QStringLiteral("C:\\Users"), QStringLiteral("Today"));
    row.resize(200, 51);
    QCOMPARE(row.getFileName(), name);
    QCOMPARE(QAccessible::queryAccessibleInterface(&row)->text(QAccessible::Name), name);
    QCOMPARE(countLeakPixels(row.grab().toImage()), 0);
}

void FluentComponentsTest::rowNestedWidgets()
{
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    pdf::PDFRecentFileRow* fileRow = new pdf::PDFRecentFileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today"), &window);
    pdf::PDFToolRow* toolRow = new pdf::PDFToolRow(QStringLiteral("Merge PDF"), QStringLiteral("Combine"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue, &window);
    pdf::PDFWorkspaceRow* workspaceRow = new pdf::PDFWorkspaceRow(QStringLiteral("Thesis review"), QStringLiteral("4 documents"), &window);
    layout->addWidget(fileRow);
    layout->addWidget(toolRow);
    layout->addWidget(workspaceRow);
    QMenu menu;
    menu.addAction(QStringLiteral("Open"));
    fileRow->setMenu(&menu);
    QCOMPARE(fileRow->getMenu(), &menu);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QSignalSpy fileClicked(fileRow, &QAbstractButton::clicked);
    QSignalSpy filePinned(fileRow, &pdf::PDFRecentFileRow::pinnedChanged);
    QSignalSpy fileMenu(fileRow, &pdf::PDFRecentFileRow::menuRequested);

    // The star pins the file, the row is not activated
    QTest::mouseClick(fileRow->getStarButton(), Qt::LeftButton);
    QCOMPARE(filePinned.count(), 1);
    QVERIFY(fileRow->isPinned());
    QCOMPARE(fileClicked.count(), 0);

    // The signal only on a real change
    fileRow->setPinned(true);
    QCOMPARE(filePinned.count(), 1);
    fileRow->setPinned(false);
    QCOMPARE(filePinned.count(), 2);
    QCOMPARE(filePinned.back().front().toBool(), false);

    // The menu button shows the menu (after the signal, whose slot may fill it), the row is not activated
    bool isMenuVisibleInSlot = true;
    QMetaObject::Connection connection = connect(fileRow, &pdf::PDFRecentFileRow::menuRequested, fileRow, [&]() { isMenuVisibleInSlot = menu.isVisible(); });
    QTest::mouseClick(fileRow->getMenuButton(), Qt::LeftButton);
    QCOMPARE(fileMenu.count(), 1);
    QVERIFY(!isMenuVisibleInSlot);
    disconnect(connection);
    QCOMPARE(fileClicked.count(), 0);
    QTRY_VERIFY(menu.isVisible());
    menu.hide();

    // A right click shows the same menu
    const QPoint inside(100, 25);
    QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, inside, fileRow->mapToGlobal(inside));
    QApplication::sendEvent(fileRow, &contextEvent);
    QCOMPARE(fileMenu.count(), 2);
    QTRY_VERIFY(menu.isVisible());
    menu.hide();

    // A deleted menu is forgotten
    {
        QMenu* temporaryMenu = new QMenu();
        workspaceRow->setMenu(temporaryMenu);
        delete temporaryMenu;
        QCOMPARE(workspaceRow->getMenu(), nullptr);
        QVERIFY(!workspaceRow->getMenuButton()->showMenu());
    }

    // A disabled nested button does not activate the row
    fileRow->getStarButton()->setEnabled(false);
    QTest::mouseClick(fileRow->getStarButton(), Qt::LeftButton);
    QCOMPARE(fileClicked.count(), 0);
    QCOMPARE(filePinned.count(), 2);
    fileRow->getStarButton()->setEnabled(true);

    // The row itself is activated elsewhere
    QTest::mouseClick(fileRow, Qt::LeftButton, Qt::NoModifier, QPoint(100, 25));
    QCOMPARE(fileClicked.count(), 1);

    // Hidden star: the row does not place it
    fileRow->setStarVisible(false);
    QVERIFY(fileRow->getStarButton()->isHidden());
    fileRow->setStarVisible(true);
    QVERIFY(!fileRow->getStarButton()->isHidden());

    // Tool row: the star, the tag is a label (the mouse passes to the row)
    QSignalSpy toolClicked(toolRow, &QAbstractButton::clicked);
    QSignalSpy toolPinned(toolRow, &pdf::PDFToolRow::pinnedChanged);
    QTest::mouseClick(toolRow->getStarButton(), Qt::LeftButton);
    QCOMPARE(toolPinned.count(), 1);
    QCOMPARE(toolClicked.count(), 0);
    toolRow->setTag(QStringLiteral("Advanced"));
    QApplication::processEvents();
    QTest::mouseClick(toolRow, Qt::LeftButton, Qt::NoModifier, toolRow->getTagWidget()->geometry().center());
    QCOMPARE(toolClicked.count(), 1);

    // Workspace row: the menu button
    QSignalSpy workspaceClicked(workspaceRow, &QAbstractButton::clicked);
    QSignalSpy workspaceMenu(workspaceRow, &pdf::PDFWorkspaceRow::menuRequested);
    QTest::mouseClick(workspaceRow->getMenuButton(), Qt::LeftButton);
    QCOMPARE(workspaceMenu.count(), 1);
    QCOMPARE(workspaceClicked.count(), 0);

    // The menu button is darker under the pointer of its row
    fileRow->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY(fileRow->getComponentState().isHovered());
    const QImage hovered = fileRow->getMenuButton()->grab().toImage();
    fileRow->setAttribute(Qt::WA_UnderMouse, false);
    const QImage normal = fileRow->getMenuButton()->grab().toImage();
    QVERIFY(hovered != normal);
}

void FluentComponentsTest::rowKeyboard()
{
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    pdf::PDFRecentFileRow* fileRow = new pdf::PDFRecentFileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today"), &window);
    pdf::PDFToolShortcut* shortcut = new pdf::PDFToolShortcut(QStringLiteral("Merge PDF"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue, &window);
    pdf::PDFVersionRow* versionRow = new pdf::PDFVersionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14"), QStringLiteral("Summary"), &window);
    pdf::PDFPropertyRow* propertyRow = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), pdf::PDFPropertyRow::Editability::Editable, &window);
    layout->addWidget(fileRow);
    layout->addWidget(shortcut);
    layout->addWidget(versionRow);
    layout->addWidget(propertyRow);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QSignalSpy fileClicked(fileRow, &QAbstractButton::clicked);
    QSignalSpy filePinned(fileRow, &pdf::PDFRecentFileRow::pinnedChanged);
    QSignalSpy fileMenu(fileRow, &pdf::PDFRecentFileRow::menuRequested);

    // The row is activated by Enter and Space
    fileRow->setFocus(Qt::TabFocusReason);
    QTest::keyClick(fileRow, Qt::Key_Return);
    QTest::keyClick(fileRow, Qt::Key_Space);
    QCOMPARE(fileClicked.count(), 2);

    // Tab reaches the star and the menu button, which do not activate the row
    QTest::keyClick(fileRow, Qt::Key_Tab);
    QTRY_VERIFY(fileRow->getStarButton()->hasFocus());
    QTest::keyClick(fileRow->getStarButton(), Qt::Key_Space);
    QCOMPARE(filePinned.count(), 1);
    QTest::keyClick(fileRow->getStarButton(), Qt::Key_Tab);
    QTRY_VERIFY(fileRow->getMenuButton()->hasFocus());
    QTest::keyClick(fileRow->getMenuButton(), Qt::Key_Return);
    QCOMPARE(fileMenu.count(), 1);
    QCOMPARE(fileClicked.count(), 2);

    // Back to the row by Shift+Tab: its focus ring is inside
    QTest::keyClick(fileRow->getMenuButton(), Qt::Key_Tab, Qt::ShiftModifier);
    QTRY_VERIFY(fileRow->getStarButton()->hasFocus());
    QTest::keyClick(fileRow->getStarButton(), Qt::Key_Tab, Qt::ShiftModifier);
    QTRY_VERIFY(fileRow->hasFocus());
    QVERIFY(fileRow->getComponentState().hasFocusRing());

    // The shortcut is activated by Enter, its ring lies outside (drawn by the style)
    QSignalSpy shortcutClicked(shortcut, &QAbstractButton::clicked);
    shortcut->setFocus(Qt::TabFocusReason);
    QTest::keyClick(shortcut, Qt::Key_Enter, Qt::KeypadModifier);
    QCOMPARE(shortcutClicked.count(), 1);
    QVERIFY(!shortcut->getComponentState().hasFocusRing());

    // The version row is a link (Enter, Space, click, the ring inside); the
    // property row is a text without the focus, the actions of the property row take it
    QCOMPARE(versionRow->focusPolicy(), Qt::StrongFocus);
    QCOMPARE(propertyRow->focusPolicy(), Qt::NoFocus);
    QCOMPARE(propertyRow->getCopyButton()->focusPolicy(), Qt::TabFocus);
    QCOMPARE(propertyRow->getEditButton()->focusPolicy(), Qt::TabFocus);
    QSignalSpy versionClicked(versionRow, &QAbstractButton::clicked);
    QTest::mouseClick(versionRow, Qt::LeftButton);
    QCOMPARE(versionClicked.count(), 1);
    versionRow->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(versionRow->hasFocus());
    QTest::keyClick(versionRow, Qt::Key_Return);
    QCOMPARE(versionClicked.count(), 2);
    QVERIFY(versionRow->getComponentState().hasFocusRing());
    QTest::keyClick(versionRow, Qt::Key_Space);
    QCOMPARE(versionClicked.count(), 3);

    // A version row, which is not clickable, is a text: it gives its focus away and ignores the mouse
    versionRow->setClickable(false);
    QVERIFY(!versionRow->hasFocus());
    QCOMPARE(versionRow->focusPolicy(), Qt::NoFocus);
    QVERIFY(!versionRow->testAttribute(Qt::WA_Hover));
    QTest::mouseClick(versionRow, Qt::LeftButton);
    QTest::keyClick(versionRow, Qt::Key_Return);
    QCOMPARE(versionClicked.count(), 3);
    versionRow->setClickable(false);
    versionRow->setClickable(true);
    QCOMPARE(versionRow->focusPolicy(), Qt::StrongFocus);
    QVERIFY(versionRow->testAttribute(Qt::WA_Hover));

    // The copy button is shown, when it has the keyboard focus
    const QColor surface = token(Color::BgSurface);
    auto renderCopy = [&]()
    {
        QImage image(QSize(24, 24), QImage::Format_ARGB32_Premultiplied);
        image.fill(surface);
        QPainter painter(&image);
        propertyRow->getCopyButton()->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
        painter.end();
        return image;
    };
    QCOMPARE(colorDistance(strongestInk(renderCopy(), QRect(0, 0, 24, 24), surface), surface), 0);
    propertyRow->getCopyButton()->setFocus(Qt::TabFocusReason);
    QVERIFY(propertyRow->getCopyButton()->hasFocus());
    QVERIFY(colorDistance(strongestInk(renderCopy(), QRect(5, 5, 14, 14), surface), surface) > 100);
}

void FluentComponentsTest::rowAccessibility()
{
    // Rows are buttons named by their titles and described by their subtitles
    pdf::PDFLinkRow linkRow(QStringLiteral("Report Issue"), QStringLiteral("Tell us about a problem"), QStringLiteral("alert-circle"));
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(&linkRow);
    QCOMPARE(accessible->role(), QAccessible::Button);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Report Issue"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Tell us about a problem"));
    linkRow.setSubtitle(QStringLiteral("Other"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Other"));

    pdf::PDFRecentFileRow fileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Docs"), QStringLiteral("Today"));
    accessible = QAccessible::queryAccessibleInterface(&fileRow);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Project_Report.pdf"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("C:\\Docs, Today"));
    fileRow.setStatus(pdf::PDFRecentFileRow::Status::Missing);
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("No longer exists, C:\\Docs"));
    fileRow.setStatus(pdf::PDFRecentFileRow::Status::Available);
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("C:\\Docs, Today"));
    fileRow.setTime(QString());
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("C:\\Docs"));
    fileRow.setTime(QStringLiteral("Yesterday"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("C:\\Docs, Yesterday"));
    fileRow.setAccessibleDescription(QStringLiteral("Application description"));
    fileRow.setPath(QStringLiteral("D:\\"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Application description"));
    QCOMPARE(QAccessible::queryAccessibleInterface(fileRow.getStarButton())->text(QAccessible::Name), QStringLiteral("Pin file"));
    QCOMPARE(QAccessible::queryAccessibleInterface(fileRow.getMenuButton())->text(QAccessible::Name), QStringLiteral("More actions"));
    QCOMPARE(QAccessible::queryAccessibleInterface(fileRow.getMenuButton())->role(), QAccessible::Button);
    QCOMPARE(fileRow.getMenuButton()->toolTip(), QStringLiteral("More actions"));

    // The menu button opens a popup, when it has a menu
    QVERIFY(!QAccessible::queryAccessibleInterface(fileRow.getMenuButton())->state().hasPopup);
    QMenu menu;
    fileRow.setMenu(&menu);
    QVERIFY(QAccessible::queryAccessibleInterface(fileRow.getMenuButton())->state().hasPopup);
    fileRow.setMenu(nullptr);

    // The ampersand of a name is data
    pdf::PDFToolRow toolRow(QStringLiteral("Protect & Sign"), QStringLiteral("Passwords"), QStringLiteral("lock"), pdf::PDFAccentHue::Red);
    QCOMPARE(toolRow.shortcut(), QKeySequence());
    QCOMPARE(QAccessible::queryAccessibleInterface(&toolRow)->text(QAccessible::Name), QStringLiteral("Protect & Sign"));
    QCOMPARE(QAccessible::queryAccessibleInterface(&toolRow)->text(QAccessible::Description), QStringLiteral("Passwords"));
    toolRow.setTag(QStringLiteral("Advanced"));
    QCOMPARE(QAccessible::queryAccessibleInterface(toolRow.getTagWidget())->role(), QAccessible::StaticText);

    pdf::PDFRecentToolRow recentToolRow(QStringLiteral("Compress"), QStringLiteral("Today 11:02"), QStringLiteral("minimize-2"), pdf::PDFAccentHue::Violet);
    QCOMPARE(QAccessible::queryAccessibleInterface(&recentToolRow)->text(QAccessible::Description), QStringLiteral("Today 11:02"));
    pdf::PDFWorkspaceRow workspaceRow(QStringLiteral("Thesis"), QStringLiteral("4 documents"));
    QCOMPARE(QAccessible::queryAccessibleInterface(&workspaceRow)->text(QAccessible::Description), QStringLiteral("4 documents"));
    pdf::PDFCollectionRow collectionRow(QStringLiteral("Prepare"), QStringLiteral("3 tools"));
    QCOMPARE(QAccessible::queryAccessibleInterface(&collectionRow)->text(QAccessible::Description), QStringLiteral("3 tools"));
    pdf::PDFToolShortcut shortcut(QStringLiteral("Merge PDF"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    QCOMPARE(QAccessible::queryAccessibleInterface(&shortcut)->text(QAccessible::Name), QStringLiteral("Merge PDF"));

    // Issue row: the number and the title
    pdf::PDFIssueRow issueRow(QStringLiteral("#612"), QStringLiteral("Crash when opening"), QStringLiteral("Fixed"), QStringLiteral("Sep 12"));
    QCOMPARE(QAccessible::queryAccessibleInterface(&issueRow)->text(QAccessible::Name), QStringLiteral("#612 Crash when opening"));
    QCOMPARE(QAccessible::queryAccessibleInterface(&issueRow)->text(QAccessible::Description), QStringLiteral("Fixed, Sep 12"));
    issueRow.setDate(QString());
    QCOMPARE(QAccessible::queryAccessibleInterface(&issueRow)->text(QAccessible::Description), QStringLiteral("Fixed"));
    issueRow.setNumber(QString());
    QCOMPARE(QAccessible::queryAccessibleInterface(&issueRow)->text(QAccessible::Name), QStringLiteral("Crash when opening"));

    // Version row: a link with the date and the summary as its description; the
    // selected row says "Shown"; a row, which is not clickable, is a text without actions
    pdf::PDFVersionRow versionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14, 2026"), QStringLiteral("One unified app."));
    accessible = QAccessible::queryAccessibleInterface(&versionRow);
    QCOMPARE(accessible->role(), QAccessible::Button);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("v1.5.3"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Current version. Sep 14, 2026. One unified app."));
    QVERIFY(accessible->actionInterface() && accessible->actionInterface()->actionNames().contains(QAccessibleActionInterface::pressAction()));
    versionRow.setSelected(true);
    QVERIFY(versionRow.isSelected());
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Current version. Shown. Sep 14, 2026. One unified app."));
    versionRow.setSelected(true);
    versionRow.setCurrent(false);
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Shown. Sep 14, 2026. One unified app."));
    versionRow.setSelected(false);
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Sep 14, 2026. One unified app."));
    QVERIFY(versionRow.setProperty("selected", true));
    QVERIFY(versionRow.getContent().selected);
    versionRow.setClickable(false);
    QVERIFY(!versionRow.isClickable());
    QCOMPARE(accessible->role(), QAccessible::StaticText);
    if (QAccessibleActionInterface* actions = accessible->actionInterface())
    {
        QVERIFY(!actions->actionNames().contains(QAccessibleActionInterface::pressAction()));
    }

    // Property row: a text, the buttons named by their actions
    pdf::PDFPropertyRow propertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), pdf::PDFPropertyRow::Editability::Editable);
    accessible = QAccessible::queryAccessibleInterface(&propertyRow);
    QCOMPARE(accessible->role(), QAccessible::StaticText);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Author"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Jane Novak"));
    QCOMPARE(QAccessible::queryAccessibleInterface(propertyRow.getCopyButton())->text(QAccessible::Name), QStringLiteral("Copy"));
    QCOMPARE(QAccessible::queryAccessibleInterface(propertyRow.getEditButton())->text(QAccessible::Name), QStringLiteral("Edit"));
    QCOMPARE(QAccessible::queryAccessibleInterface(propertyRow.getConfirmButton())->text(QAccessible::Name), QStringLiteral("Confirm"));
    QCOMPARE(QAccessible::queryAccessibleInterface(propertyRow.getCancelButton())->text(QAccessible::Name), QStringLiteral("Cancel"));
    QCOMPARE(propertyRow.getEditButton()->toolTip(), QStringLiteral("Edit"));
    propertyRow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&propertyRow));
    propertyRow.setCopiedDuration(60000);
    propertyRow.copy();
    QCOMPARE(propertyRow.getCopyButton()->toolTip(), QStringLiteral("Copied"));
    QCOMPARE(QAccessible::queryAccessibleInterface(propertyRow.getCopyButton())->text(QAccessible::Name), QStringLiteral("Copied"));
    QCOMPARE(propertyRow.getEditor()->accessibleName(), QStringLiteral("Author"));
}

void FluentComponentsTest::propertyRowEditing()
{
    // In a dialog: Enter and Esc of the field do not go to the dialog
    QDialog dialog;
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    pdf::PDFPropertyRow* row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), pdf::PDFPropertyRow::Editability::Editable, &dialog);
    QPushButton* okButton = new QPushButton(QStringLiteral("OK"), &dialog);
    okButton->setDefault(true);
    QObject::connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(row);
    layout->addWidget(okButton);
    dialog.show();
    dialog.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&dialog));

    QSignalSpy edited(row, &pdf::PDFPropertyRow::edited);
    QSignalSpy editingChanged(row, &pdf::PDFPropertyRow::editingChanged);
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    QSignalSpy rejected(&dialog, &QDialog::rejected);

    QVERIFY(row->getEditButton()->isVisibleTo(row));
    QVERIFY(row->getCopyButton()->isVisibleTo(row));
    QVERIFY(!row->getConfirmButton()->isVisibleTo(row));
    QVERIFY(!row->getEditor()->isVisibleTo(row));
    QCOMPARE(row->getPropertyState(), Painter::PropertyState::Default);

    // The edit button starts the editing: the field has the value and the focus
    QTest::mouseClick(row->getEditButton(), Qt::LeftButton);
    QVERIFY(row->isEditing());
    QCOMPARE(row->getPropertyState(), Painter::PropertyState::Editing);
    QCOMPARE(editingChanged.count(), 1);
    QCOMPARE(editingChanged.back().front().toBool(), true);
    QVERIFY(row->getEditor()->isVisibleTo(row));
    QCOMPARE(row->getEditor()->text(), QStringLiteral("Jane Novak"));
    QTRY_VERIFY(row->getEditor()->hasFocus());
    QVERIFY(row->getConfirmButton()->isVisibleTo(row));
    QVERIFY(row->getCancelButton()->isVisibleTo(row));
    QVERIFY(!row->getEditButton()->isVisibleTo(row));
    QVERIFY(!row->getCopyButton()->isVisibleTo(row));
    QVERIFY(row->startEditing());
    QCOMPARE(editingChanged.count(), 1);

    // Enter with an unchanged value: no signal edited, the dialog is not accepted
    QTest::keyClick(row->getEditor(), Qt::Key_Return);
    QVERIFY(!row->isEditing());
    QCOMPARE(edited.count(), 0);
    QCOMPARE(editingChanged.count(), 2);
    QCOMPARE(accepted.count(), 0);
    QVERIFY(dialog.isVisible());
    QTRY_VERIFY(row->getEditButton()->hasFocus());

    // Enter of the keypad confirms a changed value, the signal follows the change
    QString valueInSlot;
    QMetaObject::Connection connection = connect(row, &pdf::PDFPropertyRow::edited, row, [&]() { valueInSlot = row->getValue(); });
    QVERIFY(row->startEditing());
    row->getEditor()->setText(QStringLiteral("John Doe"));
    QTest::keyClick(row->getEditor(), Qt::Key_Enter, Qt::KeypadModifier);
    QCOMPARE(edited.count(), 1);
    QCOMPARE(edited.back().front().toString(), QStringLiteral("John Doe"));
    QCOMPARE(row->getValue(), QStringLiteral("John Doe"));
    QCOMPARE(valueInSlot, QStringLiteral("John Doe"));
    QCOMPARE(accepted.count(), 0);
    QCOMPARE(QAccessible::queryAccessibleInterface(row)->text(QAccessible::Description), QStringLiteral("John Doe"));
    disconnect(connection);

    // Esc cancels, the dialog is not rejected
    QVERIFY(row->startEditing());
    row->getEditor()->setText(QStringLiteral("Discarded"));
    QTest::keyClick(row->getEditor(), Qt::Key_Escape);
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("John Doe"));
    QCOMPARE(edited.count(), 1);
    QCOMPARE(rejected.count(), 0);
    QVERIFY(dialog.isVisible());

    // The confirm and cancel buttons
    QVERIFY(row->startEditing());
    row->getEditor()->setText(QStringLiteral("By button"));
    QTest::mouseClick(row->getConfirmButton(), Qt::LeftButton);
    QCOMPARE(row->getValue(), QStringLiteral("By button"));
    QCOMPARE(edited.count(), 2);
    QVERIFY(row->startEditing());
    row->getEditor()->setText(QStringLiteral("Canceled"));
    QTest::mouseClick(row->getCancelButton(), Qt::LeftButton);
    QCOMPARE(row->getValue(), QStringLiteral("By button"));
    QCOMPARE(edited.count(), 2);

    // A value set while editing does not change the field; the confirmation takes the field
    QVERIFY(row->startEditing());
    row->setValue(QStringLiteral("From the application"));
    QCOMPARE(row->getEditor()->text(), QStringLiteral("By button"));
    row->confirmEditing();
    QCOMPARE(row->getValue(), QStringLiteral("By button"));
    QCOMPARE(edited.count(), 3);

    // A row, which stops being editable, cancels the editing without the signal edited
    QVERIFY(row->startEditing());
    row->getEditor()->setText(QStringLiteral("Lost"));
    const qsizetype changes = editingChanged.count();
    row->setEditable(false);
    QVERIFY(!row->isEditing());
    QCOMPARE(editingChanged.count(), changes + 1);
    QCOMPARE(edited.count(), 3);
    QCOMPARE(row->getValue(), QStringLiteral("By button"));
    QVERIFY(!row->getEditButton()->isVisibleTo(row));
    QVERIFY(!row->startEditing());
    row->confirmEditing();
    row->cancelEditing();
    QCOMPARE(edited.count(), 3);

    // Re-entrant: the slot of the signal edited makes the row not editable
    row->setEditable(true);
    connection = connect(row, &pdf::PDFPropertyRow::edited, row, [row]() { row->setEditable(false); });
    QVERIFY(row->startEditing());
    row->getEditor()->setText(QStringLiteral("Reentrant"));
    row->confirmEditing();
    QCOMPARE(row->getValue(), QStringLiteral("Reentrant"));
    QVERIFY(!row->isEditable());
    QVERIFY(!row->isEditing());
    disconnect(connection);

    // Re-entrant: the slot of editingChanged cancels the editing again
    row->setEditable(true);
    connection = connect(row, &pdf::PDFPropertyRow::editingChanged, row, [row](bool isEditing) { if (isEditing) { row->setEditable(false); } });
    QVERIFY(!row->startEditing());
    QVERIFY(!row->isEditing());
    QVERIFY(!row->getEditor()->isVisibleTo(row));
    disconnect(connection);

    // A disabled row is not edited
    row->setEditable(true);
    row->setEnabled(false);
    QVERIFY(!row->startEditing());
    row->setEnabled(true);
    QVERIFY(row->startEditing());
    row->setEnabled(false);
    QVERIFY(!row->isEditing());
    row->setEnabled(true);

    // The field lies in the place of the value, mirrored for right-to-left
    layout->removeWidget(row);
    row->resize(326, 30);
    QVERIFY(row->startEditing());
    QCOMPARE(row->getEditor()->geometry(), QRect(150, 3, 116, 24));
    row->setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(row->getEditor()->geometry(), QRect(326 - 150 - 116, 3, 116, 24));
    QCOMPARE(row->getConfirmButton()->geometry(), QRect(28, 3, 24, 24));
    row->cancelEditing();
}

void FluentComponentsTest::propertyRowCopy()
{
    pdf::PDFPropertyRow row(QStringLiteral("Author"), QStringLiteral("Jane Novak"), pdf::PDFPropertyRow::Editability::Editable);
    row.resize(row.sizeHint());
    row.show();
    QVERIFY(QTest::qWaitForWindowExposed(&row));
    QSignalSpy copied(&row, &pdf::PDFPropertyRow::copied);

    // The copy button is visible only under the pointer (the pointer of the platform is not used)
    row.setAttribute(Qt::WA_UnderMouse, false);
    row.getCopyButton()->setAttribute(Qt::WA_UnderMouse, false);
    row.getEditButton()->clearFocus();
    QVERIFY(!row.getCopyButton()->hasFocus());
    const QColor surface = token(Color::BgSurface);
    QCOMPARE(colorDistance(strongestInk(renderWidget(&row, 1.0), QRect(298, 3, 24, 24), surface), surface), 0);
    row.setAttribute(Qt::WA_UnderMouse, true);
    QCOMPARE(row.getPropertyState(), Painter::PropertyState::Hover);
    QVERIFY(colorDistance(strongestInk(renderWidget(&row, 1.0), QRect(303, 8, 14, 14), blend(token(Color::BgHover), surface)), surface) > 100);
    row.setAttribute(Qt::WA_UnderMouse, false);

    // The copy: clipboard, signal, the state Copied for the time
    row.setCopiedDuration(40);
    QCOMPARE(row.getCopiedDuration(), 40);
    QTest::mouseClick(row.getCopyButton(), Qt::LeftButton);
    QCOMPARE(copied.count(), 1);
    QCOMPARE(copied.back().front().toString(), QStringLiteral("Jane Novak"));
    QVERIFY(row.isCopied());
    QCOMPARE(row.getPropertyState(), Painter::PropertyState::Copied);
    if (QClipboard* clipboard = QGuiApplication::clipboard())
    {
        QCOMPARE(clipboard->text(), QStringLiteral("Jane Novak"));
    }
    row.setAttribute(Qt::WA_UnderMouse, false);
    const QImage copiedImage = renderWidget(&row, 1.0);
    const QColor hover = blend(token(Color::BgHover), surface);
    QVERIFY(isNear(pixel(copiedImage, 5, 15), hover));
    QVERIFY(colorDistance(strongestInk(copiedImage, QRect(303, 8, 14, 14), hover), token(Color::AccentGreenIcon)) < colorDistance(strongestInk(copiedImage, QRect(303, 8, 14, 14), hover), token(Color::IconDefault)));
    QTRY_VERIFY(!row.isCopied());
    QCOMPARE(row.getCopyButton()->toolTip(), QStringLiteral("Copy"));

    // Nothing is copied by a disabled row or during the editing
    row.setEnabled(false);
    row.copy();
    QCOMPARE(copied.count(), 1);
    row.setEnabled(true);
    QVERIFY(row.startEditing());
    row.copy();
    QCOMPARE(copied.count(), 1);
    QVERIFY(!row.isCopied());

    // The editing ends the state Copied
    row.cancelEditing();
    row.setCopiedDuration(60000);
    row.copy();
    QVERIFY(row.isCopied());
    QVERIFY(row.startEditing());
    QVERIFY(!row.isCopied());
    row.cancelEditing();

    // A hidden row stops the timer and leaves the state
    row.copy();
    QVERIFY(row.isCopied());
    row.hide();
    QVERIFY(!row.isCopied());

    // A hidden row copies (clipboard and signal) without the state Copied
    const qsizetype copiedCount = copied.count();
    row.setValue(QStringLiteral("Hidden value"));
    row.copy();
    QCOMPARE(copied.count(), copiedCount + 1);
    QVERIFY(!row.isCopied());
    if (QClipboard* clipboard = QGuiApplication::clipboard())
    {
        QCOMPARE(clipboard->text(), QStringLiteral("Hidden value"));
    }

    // Defensive durations
    row.setCopiedDuration(-5);
    QCOMPARE(row.getCopiedDuration(), 0);
    row.setCopiedDuration(std::numeric_limits<int>::min());
    QCOMPARE(row.getCopiedDuration(), 0);
    row.show();
    row.copy();
    QTRY_VERIFY(!row.isCopied());
}

void FluentComponentsTest::propertyRowLifetime()
{
    // Deleted while the timer of the state Copied runs
    {
        pdf::PDFPropertyRow* row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"));
        row->setCopiedDuration(20);
        row->show();
        row->copy();
        delete row;
        QTest::qWait(60);
    }

    // Deleted while editing (with the focus in the field)
    {
        QWidget window;
        QVBoxLayout* layout = new QVBoxLayout(&window);
        pdf::PDFPropertyRow* row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"), pdf::PDFPropertyRow::Editability::Editable, &window);
        layout->addWidget(row);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(row->startEditing());
        delete row;
        QApplication::processEvents();
    }

    // Deleted by the slots of its signals
    {
        QPointer<pdf::PDFPropertyRow> row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"), pdf::PDFPropertyRow::Editability::Editable);
        connect(row.data(), &pdf::PDFPropertyRow::edited, row.data(), [&row]() { delete row.data(); });
        QVERIFY(row->startEditing());
        row->getEditor()->setText(QStringLiteral("Other"));
        row->confirmEditing();
        QVERIFY(!row);
    }
    {
        QPointer<pdf::PDFPropertyRow> row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"), pdf::PDFPropertyRow::Editability::Editable);
        connect(row.data(), &pdf::PDFPropertyRow::editingChanged, row.data(), [&row](bool isEditing) { if (!isEditing) { delete row.data(); } });
        QVERIFY(row->startEditing());
        row->setEditable(false);
        QVERIFY(!row);
    }
    {
        QPointer<pdf::PDFPropertyRow> row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"), pdf::PDFPropertyRow::Editability::Editable);
        row->show();
        QVERIFY(QTest::qWaitForWindowExposed(row.data()));
        connect(row.data(), &pdf::PDFPropertyRow::editingChanged, row.data(), [&row](bool isEditing) { if (isEditing) { delete row.data(); } });
        QTest::mouseClick(row->getEditButton(), Qt::LeftButton);
        QVERIFY(!row);
    }
    {
        QPointer<pdf::PDFPropertyRow> row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"));
        row->show();
        QVERIFY(QTest::qWaitForWindowExposed(row.data()));
        connect(row.data(), &pdf::PDFPropertyRow::copied, row.data(), [&row]() { delete row.data(); });
        QTest::mouseClick(row->getCopyButton(), Qt::LeftButton);
        QVERIFY(!row);
        QTest::qWait(10);
    }

    // A row deleted by the slot of its star
    {
        QPointer<pdf::PDFRecentFileRow> row = new pdf::PDFRecentFileRow(QStringLiteral("a.pdf"), QString(), QString());
        row->show();
        QVERIFY(QTest::qWaitForWindowExposed(row.data()));
        connect(row.data(), &pdf::PDFRecentFileRow::pinnedChanged, row.data(), [&row]() { delete row.data(); });
        QTest::mouseClick(row->getStarButton(), Qt::LeftButton);
        QVERIFY(!row);
    }
}

void FluentComponentsTest::rowThemeAndDirection()
{
    // A change of the theme: the colors are taken at the time of drawing
    QWidget holder;
    pdf::PDFCollectionRow& collectionRow = *new pdf::PDFCollectionRow(QStringLiteral("Prepare for sharing"), QStringLiteral("3 tools"), &holder);
    collectionRow.resize(collectionRow.sizeHint());
    const QColor lightCircle = pixel(renderWidget(&collectionRow, 1.0), 13, 26);
    QVERIFY(isNear(lightCircle, blend(token(Color::BgAccentSubtle), token(Color::BgSurface))));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    const QColor darkCircle = pixel(renderWidget(&collectionRow, 1.0), 13, 26);
    QVERIFY2(isNear(darkCircle, blend(token(Color::BgAccentSubtle), token(Color::BgSurface))), qPrintable(QStringLiteral("%1 / %2").arg(darkCircle.name(), blend(token(Color::BgAccentSubtle), token(Color::BgSurface)).name())));
    QVERIFY(lightCircle != darkCircle);
    pdf::PDFVersionRow versionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14"), QStringLiteral("Summary"));
    versionRow.resize(versionRow.sizeHint());
    QVERIFY(isNear(pixel(renderWidget(&versionRow, 1.0), 13, 9), token(Color::BgAccent)));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    // Right-to-left: the icon of a link row at the right edge, the chevron at the left one
    const QColor surface = token(Color::BgSurface);
    const QImage ltr = render([&](QPainter& painter) { Painter::paintLinkRow(&painter, QRectF(0, 0, 328, 50), RowSamples::linkRow(), state(QStyle::State_None)); }, QSize(328, 50));
    const QImage rtl = render([&](QPainter& painter) { Painter::paintLinkRow(&painter, QRectF(0, 0, 328, 50), RowSamples::linkRow(), state(QStyle::State_None, Qt::RightToLeft)); }, QSize(328, 50));
    QVERIFY(colorDistance(strongestInk(ltr, QRect(10, 17, 16, 16), surface), surface) > 100);
    QVERIFY(colorDistance(strongestInk(rtl, QRect(328 - 26, 17, 16, 16), surface), surface) > 100);
    QVERIFY(colorDistance(strongestInk(rtl, QRect(10, 18, 14, 14), surface), surface) > 50);
    QCOMPARE(colorDistance(strongestInk(rtl, QRect(40, 0, 100, 50), surface), surface), 0);

    // The version row has the rail at the right edge
    const QImage rtlVersion = render([&](QPainter& painter) { Painter::paintVersionRow(&painter, QRectF(0, 0, 324, 60), RowSamples::versionRow(), state(QStyle::State_None, Qt::RightToLeft)); }, QSize(324, 60));
    QVERIFY(isNear(pixel(rtlVersion, 324 - 14, 9), token(Color::BgAccent)));
    QVERIFY(isNear(pixel(rtlVersion, 310, 40), token(Color::BgTrack)));
    QVERIFY(isNear(pixel(rtlVersion, 13, 40), surface));

    // Widgets mirror their nested widgets
    pdf::PDFRecentFileRow fileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today"));
    fileRow.resize(381, 51);
    fileRow.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(fileRow.getMenuButton()->geometry(), QRect(12, 17, 16, 16));
    QCOMPARE(fileRow.getStarButton()->geometry(), QRect(40, 13, 24, 24));
    QVERIFY(fileRow.getComponentState().isRightToLeft());

    pdf::PDFToolRow toolRow(QStringLiteral("Merge PDF"), QStringLiteral("Combine"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    toolRow.resize(380, 48);
    toolRow.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(toolRow.getStarButton()->geometry(), QRect(6, 10, 28, 28));
}

void FluentComponentsTest::rowDefensive()
{
    // Null painter
    const Painter::State normal;
    Painter::paintIconButton(nullptr, QRectF(0, 0, 24, 24), Painter::IconButton(), normal);
    Painter::paintMoreButton(nullptr, QRectF(0, 0, 16, 16), normal);
    Painter::paintLinkRow(nullptr, QRectF(0, 0, 328, 50), RowSamples::linkRow(), normal);
    Painter::paintRecentFileRow(nullptr, QRectF(0, 0, 381, 51), RowSamples::recentFileRow(), normal);
    Painter::paintRecentToolRow(nullptr, QRectF(0, 0, 342, 48), RowSamples::recentToolRow(), normal);
    Painter::paintWorkspaceRow(nullptr, QRectF(0, 0, 344, 52), RowSamples::workspaceRow(), normal);
    Painter::paintToolRow(nullptr, QRectF(0, 0, 380, 48), RowSamples::toolRow(), normal);
    Painter::paintCollectionRow(nullptr, QRectF(0, 0, 324, 52), RowSamples::collectionRow(), normal);
    Painter::paintToolShortcut(nullptr, QRectF(0, 0, 180, 48), RowSamples::toolShortcut(), normal);
    Painter::paintIssueRow(nullptr, QRectF(0, 0, 786, 52), RowSamples::issueRow(), normal);
    Painter::paintVersionRow(nullptr, QRectF(0, 0, 324, 60), RowSamples::versionRow(), normal);
    Painter::paintPropertyRow(nullptr, QRectF(0, 0, 326, 30), RowSamples::propertyRow(true, Painter::PropertyState::Editing), normal);

    const QStyle::State all = QStyle::State_Enabled | QStyle::State_MouseOver | QStyle::State_Sunken | QStyle::State_HasFocus;
    auto paintAll = [&](QPainter& painter, const QRectF& rect)
    {
        const Painter::State full(all);
        Painter::IconButton button;
        button.iconName = QStringLiteral("copy");
        Painter::paintIconButton(&painter, rect, button, full);
        Painter::paintMoreButton(&painter, rect, full);
        Painter::paintLinkRow(&painter, rect, RowSamples::linkRow(), full);
        Painter::RecentFileRow missing = RowSamples::recentFileRow();
        missing.status = Painter::FileStatus::Missing;
        Painter::paintRecentFileRow(&painter, rect, missing, full);
        Painter::paintRecentToolRow(&painter, rect, RowSamples::recentToolRow(), full);
        Painter::paintWorkspaceRow(&painter, rect, RowSamples::workspaceRow(), full);
        Painter::ToolRow tool = RowSamples::toolRow();
        tool.tag = QStringLiteral("Advanced");
        Painter::paintToolRow(&painter, rect, tool, full);
        Painter::paintCollectionRow(&painter, rect, RowSamples::collectionRow(), full);
        Painter::paintToolShortcut(&painter, rect, RowSamples::toolShortcut(), full);
        Painter::paintIssueRow(&painter, rect, RowSamples::issueRow(), full);
        Painter::paintVersionRow(&painter, rect, RowSamples::versionRow(), full);
        Painter::paintPropertyRow(&painter, rect, RowSamples::propertyRow(true, Painter::PropertyState::Editing), full);
        Painter::paintPropertyRow(&painter, rect, RowSamples::propertyRow(false, Painter::PropertyState::Copied), full);
    };

    // Empty and negative rectangles draw nothing, the state of the painter is restored
    const QImage empty = render([](QPainter&) { }, QSize(40, 40));
    for (const QRectF& rect : { QRectF(), QRectF(10, 10, 0, 20), QRectF(10, 10, -20, -20), QRectF(10, 10, 20, -5) })
    {
        const QImage image = render([&](QPainter& painter)
        {
            const QTransform transform = painter.transform();
            const QPen pen = painter.pen();
            paintAll(painter, rect);
            QCOMPARE(painter.transform(), transform);
            QCOMPARE(painter.hasClipping(), false);
            QCOMPARE(painter.pen(), pen);
        }, QSize(40, 40));
        QCOMPARE(image, empty);
    }

    // Small rectangles: nothing is drawn outside (the focus rings outside reach 2 px)
    for (const QSizeF& size : { QSizeF(3, 3), QSizeF(1, 40), QSizeF(12, 5), QSizeF(60, 8) })
    {
        const QRectF rect(QPointF(10, 10), size);
        const QImage image = render([&](QPainter& painter)
        {
            const QPen pen = painter.pen();
            paintAll(painter, rect);
            QCOMPARE(painter.pen(), pen);
            QCOMPARE(painter.hasClipping(), false);
        }, QSize(90, 70));
        const QRect outside = rect.toAlignedRect().adjusted(-3, -3, 3, 3);
        for (int y = 0; y < 70; ++y)
        {
            for (int x = 0; x < 90; ++x)
            {
                if (!outside.contains(x, y))
                {
                    QVERIFY2(image.pixelColor(x, y) == empty.pixelColor(0, 0), qPrintable(QStringLiteral("%1 %2 (%3 x %4)").arg(x).arg(y).arg(size.width()).arg(size.height())));
                }
            }
        }
    }

    // Empty texts, unknown icons
    const QImage image = render([&](QPainter& painter)
    {
        Painter::paintLinkRow(&painter, QRectF(0, 0, 328, 50), Painter::LinkRow{ QString(), QString(), QStringLiteral("no-such-icon"), std::nullopt }, normal);
        Painter::paintRecentFileRow(&painter, QRectF(0, 0, 381, 51), Painter::RecentFileRow(), normal);
        Painter::paintRecentToolRow(&painter, QRectF(0, 0, 342, 48), Painter::RecentToolRow(), normal);
        Painter::paintWorkspaceRow(&painter, QRectF(0, 0, 344, 52), Painter::WorkspaceRow{ QString(), QString(), QStringLiteral("no-such-icon"), pdf::PDFAccentHue::Teal, true }, normal);
        Painter::paintToolRow(&painter, QRectF(0, 0, 380, 48), Painter::ToolRow(), normal);
        Painter::paintCollectionRow(&painter, QRectF(0, 0, 324, 52), Painter::CollectionRow{ QString(), QString(), QString() }, normal);
        Painter::paintToolShortcut(&painter, QRectF(0, 0, 180, 48), Painter::ToolShortcut(), normal);
        Painter::paintIssueRow(&painter, QRectF(0, 0, 786, 52), Painter::IssueRow{ QString(), QString(), QString(), QString(), QStringLiteral("no-such-icon") }, normal);
        Painter::paintVersionRow(&painter, QRectF(0, 0, 324, 60), Painter::VersionRow(), normal);
        Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), Painter::PropertyRow(), normal);
        Painter::paintIconButton(&painter, QRectF(0, 0, 24, 24), Painter::IconButton{ QString(), -5, -3, -1.0, std::nullopt }, normal);
    }, QSize(800, 70));
    QCOMPARE(countLeakPixels(image), 0);
    QCOMPARE(Painter::getVersionRowHeight(Painter::VersionRow(), -10), 30);
    QCOMPARE(Painter::getVersionRowHeight(RowSamples::versionRow(), 0), 30);
    QCOMPARE(Painter::getVersionRowHeight(RowSamples::versionRow(), std::numeric_limits<int>::min()), 30);
    QCOMPARE(Painter::getPropertyRowEditorRect(QRectF(), RowSamples::propertyRow(true, Painter::PropertyState::Editing), normal), QRectF(18, 0, 0, 0));
    QCOMPARE(Painter::getPropertyRowEditorRect(QRectF(0, 0, 326, 30), RowSamples::propertyRow(true, Painter::PropertyState::Editing), Painter::State(QStyle::State_Enabled, Qt::RightToLeft)), QRectF(60, 3, 116, 24));

    // Invalid values of the enumerations
    QCOMPARE(Painter::normalized(static_cast<Painter::FileStatus>(7)), Painter::FileStatus::Available);
    QCOMPARE(Painter::normalized(static_cast<Painter::PropertyState>(9), true), Painter::PropertyState::Default);
    QCOMPARE(Painter::normalized(Painter::PropertyState::Editing, false), Painter::PropertyState::Default);
    QCOMPARE(Painter::normalized(Painter::PropertyState::Editing, true), Painter::PropertyState::Editing);
    const Painter::PropertyRow invalidProperty = RowSamples::propertyRow(false, static_cast<Painter::PropertyState>(42));
    QCOMPARE(countLeakPixels(render([&](QPainter& painter) { Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), invalidProperty, normal); }, QSize(326, 30))), 0);

    pdf::PDFRecentFileRow fileRow;
    fileRow.setStatus(static_cast<pdf::PDFRecentFileRow::Status>(5));
    QCOMPARE(fileRow.getStatus(), pdf::PDFRecentFileRow::Status::Available);
    pdf::PDFRecentToolRow recentToolRow;
    recentToolRow.setHue(static_cast<pdf::PDFAccentHue>(77));
    QCOMPARE(recentToolRow.getHue(), pdf::PDFAccentHue::Neutral);
    pdf::PDFWorkspaceRow workspaceRow;
    workspaceRow.setHue(static_cast<pdf::PDFAccentHue>(-4));
    QCOMPARE(workspaceRow.getHue(), pdf::PDFAccentHue::Neutral);
    pdf::PDFToolRow toolRow;
    toolRow.setHue(static_cast<pdf::PDFAccentHue>(12));
    QCOMPARE(toolRow.getHue(), pdf::PDFAccentHue::Neutral);
    pdf::PDFToolShortcut shortcut;
    shortcut.setHue(static_cast<pdf::PDFAccentHue>(12));
    QCOMPARE(shortcut.getHue(), pdf::PDFAccentHue::Neutral);
    pdf::PDFLinkRow linkRow;
    linkRow.setIconColor(static_cast<Color>(-1));
    QVERIFY(!linkRow.getIconColor().has_value());
    linkRow.setIconColor(static_cast<Color>(100000));
    QVERIFY(!linkRow.getIconColor().has_value());
    linkRow.setIconColor(Color::IconAccent);
    QCOMPARE(linkRow.getIconColor(), std::optional<Color>(Color::IconAccent));
    linkRow.resetIconColor();
    QVERIFY(!linkRow.getIconColor().has_value());
    pdf::PDFIssueRow issueRow;
    issueRow.setStatusIconName(QStringLiteral("no-such-icon"));
    pdf::PDFVersionRow versionRow;
    QCOMPARE(versionRow.heightForWidth(-50), 30);
    pdf::PDFPropertyRow propertyRow(QStringLiteral("Key"), QString(), pdf::PDFPropertyRow::Editability::Editable);
    pdf::PDFCollectionRow collectionRow;

    // Widgets of zero and of tiny sizes, empty contents
    for (QWidget* widget : std::initializer_list<QWidget*>{ &fileRow, &recentToolRow, &workspaceRow, &toolRow, &shortcut, &linkRow, &issueRow, &versionRow, &propertyRow, &collectionRow })
    {
        for (const QSize& size : { QSize(0, 0), QSize(1, 1), QSize(30, 10) })
        {
            widget->resize(size);
            QCOMPARE(countLeakPixels(widget->grab().toImage()), 0);
        }
    }
    QVERIFY(propertyRow.startEditing());
    QCOMPARE(propertyRow.getEditor()->geometry(), QRect(18, 0, 0, 10));
    propertyRow.confirmEditing();
    QCOMPARE(propertyRow.getValue(), QString());
}

void FluentComponentsTest::propertyRowKeyboard()
{
    using Editability = pdf::PDFPropertyRow::Editability;

    // The rows are created without a parent and added to the dialog later (the
    // focus chain of their children is moved with them)
    QDialog dialog;
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLineEdit* before = new QLineEdit(&dialog);
    pdf::PDFPropertyRow* row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), Editability::Editable);
    QLineEdit* after = new QLineEdit(&dialog);
    QPushButton* okButton = new QPushButton(QStringLiteral("OK"), &dialog);
    okButton->setDefault(true);
    QObject::connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(before);
    layout->addWidget(row);
    layout->addWidget(after);
    layout->addWidget(okButton);
    dialog.show();
    dialog.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&dialog));

    QSignalSpy edited(row, &pdf::PDFPropertyRow::edited);
    QSignalSpy copied(row, &pdf::PDFPropertyRow::copied);
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    QSignalSpy rejected(&dialog, &QDialog::rejected);
    QLineEdit* editor = row->getEditor();
    QVERIFY(editor);

    // Tab goes from the field to the confirm and to the cancel button (review C1)
    QVERIFY(row->startEditing());
    QTRY_VERIFY(editor->hasFocus());
    editor->setText(QStringLiteral("Keyboard"));
    QTest::keyClick(editor, Qt::Key_Tab);
    QTRY_VERIFY(row->getConfirmButton()->hasFocus());
    QVERIFY(row->isEditing());
    QTest::keyClick(row->getConfirmButton(), Qt::Key_Tab);
    QTRY_VERIFY(row->getCancelButton()->hasFocus());
    QVERIFY(row->isEditing());
    QTest::keyClick(row->getCancelButton(), Qt::Key_Backtab, Qt::ShiftModifier);
    QTRY_VERIFY(row->getConfirmButton()->hasFocus());

    // Space on the confirm button confirms, the focus stays in the row (review C2)
    QTest::keyClick(row->getConfirmButton(), Qt::Key_Space);
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Keyboard"));
    QCOMPARE(edited.count(), 1);
    QTRY_VERIFY(row->getEditButton()->hasFocus());

    // Space on the cancel button cancels, the focus stays in the row
    QVERIFY(row->startEditing());
    editor->setText(QStringLiteral("Canceled"));
    row->getCancelButton()->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(row->getCancelButton()->hasFocus());
    QTest::keyClick(row->getCancelButton(), Qt::Key_Space);
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Keyboard"));
    QTRY_VERIFY(row->getEditButton()->hasFocus());

    // Enter on the confirm button confirms, not the default button of the dialog (review C3)
    QVERIFY(row->startEditing());
    editor->setText(QStringLiteral("Enter"));
    row->getConfirmButton()->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(row->getConfirmButton()->hasFocus());
    QTest::keyClick(row->getConfirmButton(), Qt::Key_Return);
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Enter"));
    QCOMPARE(accepted.count(), 0);
    QTRY_VERIFY(row->getEditButton()->hasFocus());

    // Enter on the cancel button cancels, Esc on the confirm button cancels, the dialog stays
    QVERIFY(row->startEditing());
    editor->setText(QStringLiteral("Lost"));
    row->getCancelButton()->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(row->getCancelButton()->hasFocus());
    QTest::keyClick(row->getCancelButton(), Qt::Key_Enter, Qt::KeypadModifier);
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Enter"));
    QVERIFY(row->startEditing());
    row->getConfirmButton()->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(row->getConfirmButton()->hasFocus());
    QTest::keyClick(row->getConfirmButton(), Qt::Key_Escape);
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Enter"));
    QCOMPARE(accepted.count(), 0);
    QCOMPARE(rejected.count(), 0);
    QVERIFY(dialog.isVisible());

    // Enter on the edit and the copy button presses them (as Space)
    row->getEditButton()->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(row->getEditButton()->hasFocus());
    QTest::keyClick(row->getEditButton(), Qt::Key_Return);
    QVERIFY(row->isEditing());
    QCOMPARE(accepted.count(), 0);
    row->cancelEditing();
    row->getCopyButton()->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(row->getCopyButton()->hasFocus());
    QTest::keyClick(row->getCopyButton(), Qt::Key_Return);
    QCOMPARE(copied.count(), 1);
    QCOMPARE(accepted.count(), 0);

    // The arrows and the page keys stay in the field (review C4)
    QVERIFY(row->startEditing());
    QTRY_VERIFY(editor->hasFocus());
    for (const Qt::Key key : { Qt::Key_Down, Qt::Key_Up, Qt::Key_PageDown, Qt::Key_PageUp, Qt::Key_Down })
    {
        QTest::keyClick(editor, key);
        QApplication::processEvents();
        QVERIFY(editor->hasFocus());
        QVERIFY(row->isEditing());
    }

    // A row, which stops being editable, keeps the focus in its copy button
    row->setEditable(false);
    QVERIFY(!row->isEditing());
    QTRY_VERIFY(row->getCopyButton()->hasFocus());
    row->setEditable(true);

    // The focus leaving the row confirms the editing (review C11)
    QVERIFY(row->startEditing());
    editor->setText(QStringLiteral("Outside"));
    after->setFocus(Qt::MouseFocusReason);
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Outside"));
    QCOMPARE(edited.count(), 3);
    QVERIFY(after->hasFocus());

    // ... also by Tab from the cancel button
    QVERIFY(row->startEditing());
    editor->setText(QStringLiteral("Tabbed"));
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClick(editor, Qt::Key_Tab);
    QTest::keyClick(row->getConfirmButton(), Qt::Key_Tab);
    QTest::keyClick(row->getCancelButton(), Qt::Key_Tab);
    QTRY_VERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Tabbed"));

    // A popup of the field and another window keep the editing
    QVERIFY(row->startEditing());
    QTRY_VERIFY(editor->hasFocus());
    before->setFocus(Qt::PopupFocusReason);
    QVERIFY(row->isEditing());
    editor->setFocus(Qt::OtherFocusReason);
    before->setFocus(Qt::ActiveWindowFocusReason);
    QVERIFY(row->isEditing());

    // A hidden row cancels the editing and is not edited, when it is shown again
    editor->setText(QStringLiteral("Hidden"));
    row->hide();
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Tabbed"));
    row->show();
    QVERIFY(!row->isEditing());
    QVERIFY(!editor->isVisibleTo(row));

    // A click of the OK button of the dialog confirms the editing before the dialog closes
    QVERIFY(row->startEditing());
    QTRY_VERIFY(editor->hasFocus());
    editor->setText(QStringLiteral("By OK"));
    QTest::mouseClick(okButton, Qt::LeftButton);
    QCOMPARE(accepted.count(), 1);
    QCOMPARE(row->getValue(), QStringLiteral("By OK"));
    QVERIFY(!row->isEditing());
}

void FluentComponentsTest::propertyRowFocusDeletion()
{
    // A row deleted by the slot of the signal edited, which the focus leaving the row emits
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    QPointer<pdf::PDFPropertyRow> row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"), pdf::PDFPropertyRow::Editability::Editable, &window);
    QLineEdit* other = new QLineEdit(&window);
    layout->addWidget(row.data());
    layout->addWidget(other);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    connect(row.data(), &pdf::PDFPropertyRow::edited, &window, [&row]() { delete row.data(); });
    QVERIFY(row->startEditing());
    QTRY_VERIFY(row->getEditor()->hasFocus());
    row->getEditor()->setText(QStringLiteral("Other"));
    other->setFocus(Qt::MouseFocusReason);
    QVERIFY(!row);
    QApplication::processEvents();
    QVERIFY(other->hasFocus());

    // The focused field deleted by the application while editing: the destructor
    // of QWidget takes the focus away from the field, which is then only a
    // QWidget; the row does not confirm from it, the editing is canceled
    pdf::PDFPropertyRow* focusedRow = new pdf::PDFPropertyRow(QStringLiteral("Title"), QStringLiteral("Report"), pdf::PDFPropertyRow::Editability::Editable, &window);
    layout->addWidget(focusedRow);
    focusedRow->show();
    QSignalSpy editedSpy(focusedRow, &pdf::PDFPropertyRow::edited);
    QSignalSpy editingSpy(focusedRow, &pdf::PDFPropertyRow::editingChanged);
    QVERIFY(focusedRow->startEditing());
    QTRY_VERIFY(focusedRow->getEditor()->hasFocus());
    focusedRow->getEditor()->setText(QStringLiteral("Changed"));
    delete focusedRow->getEditor();
    QCOMPARE(focusedRow->getEditor(), nullptr);
    QVERIFY(!focusedRow->isEditing());
    QCOMPARE(focusedRow->getValue(), QStringLiteral("Report"));
    QCOMPARE(editedSpy.count(), 0);
    QCOMPARE(editingSpy.count(), 2);
    QApplication::processEvents();
}

void FluentComponentsTest::moreButtonMenu()
{
    QWidget window;
    window.move(100, 100);
    window.resize(420, 140);
    pdf::PDFRecentFileRow* row = new pdf::PDFRecentFileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today"), &window);
    row->setGeometry(0, 0, 381, 51);

    // The clicks go through the window system, as the clicks of the user (a
    // popup receives the press outside of it and closes, Qt may replay it)
    auto clickButton = [&window](QWidget* widget) { QTest::mouseClick(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, widget->mapTo(&window, widget->rect().center())); };
    pdf::PDFRecentFileRow* rightToLeftRow = new pdf::PDFRecentFileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today"), &window);
    rightToLeftRow->setLayoutDirection(Qt::RightToLeft);
    rightToLeftRow->setGeometry(0, 60, 381, 51);
    QMenu menu;
    menu.addAction(QStringLiteral("Open"));
    menu.addAction(QStringLiteral("Remove from the list"));
    row->setMenu(&menu);
    rightToLeftRow->setMenu(&menu);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    auto globalRect = [](const QWidget* widget) { return QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size()); };

    // Below the button, at its leading edge; the button stays pressed, while its menu is shown
    QSignalSpy requested(row, &pdf::PDFRecentFileRow::menuRequested);
    pdf::PDFMoreButton* button = row->getMenuButton();
    clickButton(button);
    QTRY_VERIFY(menu.isVisible());
    QVERIFY(button->isDown());
    QRect buttonRect = globalRect(button);
    QVERIFY2(qAbs(menu.geometry().left() - buttonRect.left()) < 40, qPrintable(QStringLiteral("menu %1, button %2").arg(menu.geometry().left()).arg(buttonRect.left())));
    QVERIFY(menu.geometry().top() >= buttonRect.top());

    // A second click of the button closes the menu and the button is released
    // (review C8; the replay of the press, which QMenu::setNoReplayFor prevents,
    // needs the mouse grab of a real platform, the offscreen platform has none)
    clickButton(button);
    QTRY_VERIFY(!menu.isVisible());
    QCOMPARE(requested.count(), 1);
    QTRY_VERIFY(!button->isDown());

    // Right-to-left: the menu is aligned with the right (leading) edge of the button (review C7)
    pdf::PDFMoreButton* rightToLeftButton = rightToLeftRow->getMenuButton();
    QVERIFY(rightToLeftButton->showMenu());
    QTRY_VERIFY(menu.isVisible());
    buttonRect = globalRect(rightToLeftButton);
    const QRect menuRect = menu.geometry();
    QVERIFY2(menuRect.left() < buttonRect.left() && menuRect.right() - buttonRect.right() < menuRect.width() / 2,
             qPrintable(QStringLiteral("menu %1..%2, button %3..%4").arg(menuRect.left()).arg(menuRect.right()).arg(buttonRect.left()).arg(buttonRect.right())));
    menu.hide();
    QTRY_VERIFY(!rightToLeftButton->isDown());

    // The context menu key shows the menu below the menu button, not in the middle of the row (review C17)
    row->setFocus(Qt::TabFocusReason);
    QContextMenuEvent keyboardEvent(QContextMenuEvent::Keyboard, row->rect().center(), row->mapToGlobal(row->rect().center()));
    QApplication::sendEvent(row, &keyboardEvent);
    QVERIFY(keyboardEvent.isAccepted());
    QTRY_VERIFY(menu.isVisible());
    buttonRect = globalRect(button);
    QVERIFY2(qAbs(menu.geometry().left() - buttonRect.left()) < 40, qPrintable(QStringLiteral("menu %1, button %2").arg(menu.geometry().left()).arg(buttonRect.left())));
    QCOMPARE(requested.count(), 2);
    menu.hide();
    QTRY_VERIFY(!button->isDown());

    // A disabled button shows nothing
    button->setEnabled(false);
    QVERIFY(!button->showMenu());
    QVERIFY(!button->showMenu(QPoint(10, 10)));
    QVERIFY(!button->isDown());
    button->setEnabled(true);

    // A menu deleted while it is shown sends no aboutToHide: the button is released
    // by its signal destroyed (and nothing of the deleted menu is called)
    QPointer<QMenu> deletedMenu = new QMenu();
    deletedMenu->addAction(QStringLiteral("Open"));
    row->setMenu(deletedMenu.data());
    QVERIFY(button->showMenu());
    QTRY_VERIFY(deletedMenu->isVisible());
    QVERIFY(button->isDown());
    delete deletedMenu.data();
    QVERIFY(!button->isDown());
    QCOMPARE(button->getMenu(), nullptr);
    QVERIFY(!button->showMenu());

    // A menu closed normally and deleted later does not touch the button again
    QPointer<QMenu> laterMenu = new QMenu();
    laterMenu->addAction(QStringLiteral("Open"));
    row->setMenu(laterMenu.data());
    QVERIFY(button->showMenu());
    QTRY_VERIFY(laterMenu->isVisible());
    laterMenu->hide();
    QTRY_VERIFY(!button->isDown());
    button->setDown(true);
    delete laterMenu.data();
    QVERIFY(button->isDown());
    button->setDown(false);

    // The button deleted with its menu (a child of the button), which is shown
    pdf::PDFMoreButton* ownerButton = new pdf::PDFMoreButton(&window);
    ownerButton->setGeometry(390, 0, 24, 24);
    ownerButton->show();
    QPointer<QMenu> childMenu = new QMenu(ownerButton);
    childMenu->addAction(QStringLiteral("Open"));
    ownerButton->setMenu(childMenu.data());
    QVERIFY(ownerButton->showMenu());
    QTRY_VERIFY(childMenu->isVisible());
    delete ownerButton;
    QVERIFY(!childMenu);
    QApplication::processEvents();
}

void FluentComponentsTest::rowNestedDeletion()
{
    // The nested widgets are owned by the row; a nested widget deleted by the
    // application is forgotten, the row draws and works without it (review C6)
    QWidget window;
    window.resize(820, 300);
    pdf::PDFRecentFileRow* fileRow = new pdf::PDFRecentFileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today"), &window);
    fileRow->setGeometry(0, 0, 381, 51);
    pdf::PDFWorkspaceRow* workspaceRow = new pdf::PDFWorkspaceRow(QStringLiteral("Thesis review"), QStringLiteral("4 documents"), &window);
    workspaceRow->setGeometry(0, 60, 344, 52);
    pdf::PDFToolRow* toolRow = new pdf::PDFToolRow(QStringLiteral("Merge PDF"), QStringLiteral("Combine"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue, &window);
    toolRow->setGeometry(0, 120, 380, 48);
    toolRow->setTag(QStringLiteral("Advanced"));
    pdf::PDFPropertyRow* propertyRow = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"), pdf::PDFPropertyRow::Editability::Editable, &window);
    propertyRow->setGeometry(0, 180, 326, 30);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QMenu menu;
    menu.addAction(QStringLiteral("Open"));

    // Recent file row without the star and the menu button
    fileRow->setPinned(true);
    delete fileRow->getStarButton();
    QCOMPARE(fileRow->getStarButton(), nullptr);
    QVERIFY(!fileRow->isPinned());
    QVERIFY(!fileRow->isStarVisible());
    fileRow->setPinned(true);
    fileRow->setStarVisible(true);
    QVERIFY(!fileRow->getContent().starVisible);
    QCOMPARE(countLeakPixels(renderWidget(fileRow, 1.0)), 0);
    delete fileRow->getMenuButton();
    QCOMPARE(fileRow->getMenuButton(), nullptr);
    fileRow->setMenu(&menu);
    QCOMPARE(fileRow->getMenu(), nullptr);
    for (const QContextMenuEvent::Reason reason : { QContextMenuEvent::Mouse, QContextMenuEvent::Keyboard })
    {
        QContextMenuEvent event(reason, QPoint(100, 25), fileRow->mapToGlobal(QPoint(100, 25)));
        QApplication::sendEvent(fileRow, &event);
        QVERIFY(!event.isAccepted());
    }
    QVERIFY(!menu.isVisible());
    QCOMPARE(countLeakPixels(renderWidget(fileRow, 1.0)), 0);

    // The slot of menuRequested deletes the menu button
    workspaceRow->setMenu(&menu);
    connect(workspaceRow, &pdf::PDFWorkspaceRow::menuRequested, workspaceRow, [workspaceRow]() { delete workspaceRow->getMenuButton(); });
    QContextMenuEvent workspaceEvent(QContextMenuEvent::Mouse, QPoint(100, 25), workspaceRow->mapToGlobal(QPoint(100, 25)));
    QApplication::sendEvent(workspaceRow, &workspaceEvent);
    QVERIFY(!workspaceEvent.isAccepted());
    QCOMPARE(workspaceRow->getMenuButton(), nullptr);
    QCOMPARE(workspaceRow->getMenu(), nullptr);
    QVERIFY(!menu.isVisible());
    QCOMPARE(countLeakPixels(renderWidget(workspaceRow, 1.0)), 0);

    // Tool row without the tag and the star: the texts take their place
    const int textWidthWithTag = toolRow->minimumSizeHint().width();
    delete toolRow->getTagWidget();
    QCOMPARE(toolRow->getTagWidget(), nullptr);
    QVERIFY(toolRow->getContent().tag.isEmpty());
    toolRow->setTag(QStringLiteral("Other"));
    QCOMPARE(toolRow->getTag(), QStringLiteral("Other"));
    QVERIFY(toolRow->minimumSizeHint().width() < textWidthWithTag);
    delete toolRow->getStarButton();
    toolRow->setPinned(true);
    QVERIFY(!toolRow->isPinned());
    QCOMPARE(countLeakPixels(renderWidget(toolRow, 1.0)), 0);

    // Property row: deleted buttons, the field deleted while editing ends the editing
    delete propertyRow->getCopyButton();
    delete propertyRow->getEditButton();
    for (const QEvent::Type type : { QEvent::Enter, QEvent::Leave, QEvent::HoverEnter, QEvent::HoverLeave })
    {
        QEvent event(type);
        QApplication::sendEvent(propertyRow, &event);
    }
    propertyRow->copy();
    QVERIFY(propertyRow->startEditing());
    delete propertyRow->getConfirmButton();
    delete propertyRow->getCancelButton();
    QSignalSpy editingChanged(propertyRow, &pdf::PDFPropertyRow::editingChanged);
    delete propertyRow->getEditor();
    QCOMPARE(propertyRow->getEditor(), nullptr);
    QVERIFY(!propertyRow->isEditing());
    QCOMPARE(editingChanged.count(), 1);
    QVERIFY(!propertyRow->startEditing());
    propertyRow->setEditable(false);
    propertyRow->setKey(QStringLiteral("Key"));
    propertyRow->setLayoutDirection(Qt::RightToLeft);
    propertyRow->resize(200, 30);
    QCOMPARE(countLeakPixels(renderWidget(propertyRow, 1.0)), 0);
    QCOMPARE(countLeakPixels(renderWidget(&window, 2.0)), 0);
}

void FluentComponentsTest::rowRetranslation()
{
    // Texts of the descriptions are translated again after a change of the language (review C15)
    pdf::PDFRecentFileRow fileRow(QStringLiteral("a.pdf"), QStringLiteral("C:\\Docs"), QStringLiteral("Today"));
    fileRow.setStatus(pdf::PDFRecentFileRow::Status::Missing);
    pdf::PDFVersionRow versionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14"), QStringLiteral("Summary"));
    QAccessibleInterface* fileAccessible = QAccessible::queryAccessibleInterface(&fileRow);
    QAccessibleInterface* versionAccessible = QAccessible::queryAccessibleInterface(&versionRow);
    QCOMPARE(fileAccessible->text(QAccessible::Description), QStringLiteral("No longer exists, C:\\Docs"));
    QCOMPARE(versionAccessible->text(QAccessible::Description), QStringLiteral("Current version. Sep 14. Summary"));

    TestTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    QTRY_COMPARE(fileAccessible->text(QAccessible::Description), QStringLiteral("Gone, C:\\Docs"));
    QTRY_COMPARE(versionAccessible->text(QAccessible::Description), QStringLiteral("Installed. Sep 14. Summary"));
    QCOMPARE(Painter::getMissingFileText(), QStringLiteral("Gone"));

    QVERIFY(QCoreApplication::removeTranslator(&translator));
    QTRY_COMPARE(fileAccessible->text(QAccessible::Description), QStringLiteral("No longer exists, C:\\Docs"));
    QTRY_COMPARE(versionAccessible->text(QAccessible::Description), QStringLiteral("Current version. Sep 14. Summary"));
}

void FluentComponentsTest::rowDetails()
{
    const QColor surface = token(Color::BgSurface);
    auto isCloser = [](const QColor& color, const QColor& expected, const QColor& other) { return colorDistance(color, expected) < colorDistance(color, other); };

    // The color of the icon overridden by the hovered property row (icon/default)
    // holds in every state of the button: its press changes only the background
    // (card Icon button 706:147674, review F6 of step 5; formerly icon/strong, C12 of step 2)
    const QStyle::State pressed = QStyle::State_MouseOver | QStyle::State_Sunken;
    const QColor pressedBackground = blend(token(Color::BgPressed), surface);
    for (const Painter::PropertyAction action : { Painter::PropertyAction::Edit, Painter::PropertyAction::Copy })
    {
        for (const QStyle::State buttonFlags : { QStyle::State(QStyle::State_None), QStyle::State(QStyle::State_MouseOver), pressed })
        {
            const QImage image = render([&](QPainter& painter) { Painter::paintPropertyRowAction(&painter, QRectF(0, 0, 24, 24), action, Painter::PropertyState::Hover, state(buttonFlags)); }, QSize(24, 24));
            const QColor background = pixel(image, 2, 12);
            if (buttonFlags.testFlag(QStyle::State_Sunken))
            {
                QVERIFY(isNear(background, pressedBackground));
            }
            QVERIFY2(isCloser(strongestInk(image, QRect(5, 5, 14, 14), background), token(Color::IconDefault), token(Color::IconStrong)), qPrintable(QString::number(int(buttonFlags))));
            QVERIFY(isCloser(strongestInk(image, QRect(5, 5, 14, 14), background), token(Color::IconDefault), token(Color::IconMuted)));
        }
    }

    // The edit button of a row in the state Default has no override: the colors of an Icon button
    const QImage restingPressed = render([&](QPainter& painter) { Painter::paintPropertyRowAction(&painter, QRectF(0, 0, 24, 24), Painter::PropertyAction::Edit, Painter::PropertyState::Default, state(pressed)); }, QSize(24, 24));
    QVERIFY(isCloser(strongestInk(restingPressed, QRect(5, 5, 14, 14), pressedBackground), token(Color::IconStrong), token(Color::IconDefault)));

    QWidget window;
    pdf::PDFPropertyRow* propertyRow = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), pdf::PDFPropertyRow::Editability::Editable, &window);
    propertyRow->setGeometry(0, 0, 326, 30);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    propertyRow->setAttribute(Qt::WA_UnderMouse, true);
    pdf::PDFIconButton* editButton = propertyRow->getEditButton();
    editButton->setAttribute(Qt::WA_UnderMouse, true);
    editButton->setDown(true);
    QImage editImage = renderWidget(editButton, 1.0);
    QVERIFY(isCloser(strongestInk(editImage, QRect(5, 5, 14, 14), pressedBackground), token(Color::IconDefault), token(Color::IconStrong)));
    editButton->setDown(false);
    editButton->setAttribute(Qt::WA_UnderMouse, false);
    editImage = renderWidget(editButton, 1.0);
    QVERIFY(isCloser(strongestInk(editImage, QRect(5, 5, 14, 14), surface), token(Color::IconDefault), token(Color::IconMuted)));
    propertyRow->setAttribute(Qt::WA_UnderMouse, false);

    // The text of the field starts 10 px from its edge: ring 2 + padding 8 (review F1)
    QCOMPARE(Painter::getPropertyRowEditorTextInset(), 10);
    QCOMPARE(propertyRow->getEditor()->property(pdf::PDFWidgetStyleProperties::FIELD_TEXT_INSETS).value<QMargins>(), QMargins(10, 0, 10, 0));
    const QImage editing = render([&](QPainter& painter) { Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), RowSamples::propertyRow(true, Painter::PropertyState::Editing), state(QStyle::State_None)); }, QSize(326, 30));
    QCOMPARE(countPixels(editing, QRect(152, 6, 8, 18), token(Color::TextPrimary), 40), 0);
    QVERIFY(countPixels(editing, QRect(160, 6, 60, 18), token(Color::TextPrimary), 40) > 0);

    // Icon colors: the widget and the painter have one rule, an invalid color is not drawn (review C13, C14)
    pdf::PDFIconButton iconButton(QStringLiteral("copy"));
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconMuted);
    iconButton.setAttribute(Qt::WA_UnderMouse, true);
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconDefault);
    iconButton.setDown(true);
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconStrong);
    iconButton.setIconColor(Color::IconAccent);
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconAccent);
    iconButton.setIconColor(static_cast<Color>(99999));
    QVERIFY(!iconButton.getIconColor().has_value());
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconStrong);
    iconButton.setEnabled(false);
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconDisabled);
    Painter::IconButton invalidButton;
    invalidButton.iconName = QStringLiteral("copy");
    invalidButton.iconColor = static_cast<Color>(-3);
    QCOMPARE(Painter::getIconButtonColor(invalidButton, Painter::State()), Color::IconMuted);
    Painter::LinkRow invalidLink = RowSamples::linkRow();
    invalidLink.iconColor = static_cast<Color>(100000);
    const QImage linkImage = render([&](QPainter& painter)
    {
        Painter::paintIconButton(&painter, QRectF(0, 0, 24, 24), invalidButton, Painter::State());
        Painter::paintLinkRow(&painter, QRectF(0, 30, 328, 50), invalidLink, Painter::State());
    }, QSize(328, 80));
    QCOMPARE(countLeakPixels(linkImage), 0);
    QVERIFY(isCloser(strongestInk(linkImage, QRect(10, 47, 16, 16), surface), token(Color::IconDefault), token(Color::IconMuted)));

    // A star hidden by the application gives its place to the texts (review C16)
    pdf::PDFRecentFileRow fileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today"));
    const int minimumWithStar = fileRow.minimumSizeHint().width();
    fileRow.getStarButton()->hide();
    QVERIFY(!fileRow.isStarVisible());
    QVERIFY(!fileRow.getContent().starVisible);
    QCOMPARE(fileRow.minimumSizeHint().width(), minimumWithStar - 24 - 12);
    fileRow.setStarVisible(true);
    QVERIFY(fileRow.isStarVisible());
    QCOMPARE(fileRow.minimumSizeHint().width(), minimumWithStar);

    // A narrow tool row drops its tag as the painter (review N1)
    pdf::PDFToolRow toolRow(QStringLiteral("Merge PDF"), QStringLiteral("Combine"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    toolRow.setTag(QStringLiteral("Advanced"));
    const int tagWidth = Painter::getTagSize(Painter::Tag{ QStringLiteral("Advanced") }).width();
    toolRow.resize(380, 48);
    toolRow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&toolRow));
    QCOMPARE(toolRow.getTagWidget()->geometry().width(), tagWidth);
    QVERIFY(Painter::isToolRowTagShown(QRectF(0, 0, 380, 48), toolRow.getContent()));
    toolRow.resize(120, 48);
    QVERIFY(!Painter::isToolRowTagShown(QRectF(0, 0, 120, 48), toolRow.getContent()));
    QVERIFY(toolRow.getTagWidget()->geometry().isEmpty());
    QVERIFY(!toolRow.getTagWidget()->isHidden());
    toolRow.resize(380, 48);
    QCOMPARE(toolRow.getTagWidget()->geometry().width(), tagWidth);
    QVERIFY(!Painter::isToolRowTagShown(QRectF(0, 0, 380, 48), Painter::ToolRow()));

    // Right-to-left chevrons point to the left (review F2)
    const QImage rightToLeftLink = render([&](QPainter& painter) { Painter::paintLinkRow(&painter, QRectF(0, 0, 328, 50), RowSamples::linkRow(), state(QStyle::State_None, Qt::RightToLeft)); }, QSize(328, 50));
    const QImage chevronLeft = render([&](QPainter& painter) { pdf::PDFStylePainter::icon(&painter, QRectF(10, 18, 14, 14), QStringLiteral("chevron-left"), Color::IconMuted); }, QSize(328, 50));
    const QString chevronDifference = describeDifference(rightToLeftLink.copy(QRect(8, 16, 18, 18)), chevronLeft.copy(QRect(8, 16, 18, 18)), 3);
    QVERIFY2(chevronDifference.isEmpty(), qPrintable(chevronDifference));
    struct ChevronRow
    {
        std::function<void(QPainter&)> paint;
        QRectF chevron;
    };
    const ChevronRow chevronRows[] =
    {
        { [](QPainter& painter) { Painter::paintRecentToolRow(&painter, QRectF(0, 0, 342, 48), RowSamples::recentToolRow(), state(QStyle::State_None, Qt::RightToLeft)); }, QRectF(12, 17, 14, 14) },
        { [](QPainter& painter) { Painter::paintCollectionRow(&painter, QRectF(0, 0, 342, 48), RowSamples::collectionRow(), state(QStyle::State_None, Qt::RightToLeft)); }, QRectF(8, 17, 14, 14) }
    };
    for (const ChevronRow& chevronRow : chevronRows)
    {
        const QImage rowImage = render(chevronRow.paint, QSize(342, 48));
        const QRectF chevron = chevronRow.chevron;
        const QImage expected = render([&](QPainter& painter) { pdf::PDFStylePainter::icon(&painter, chevron, QStringLiteral("chevron-left"), Color::IconMuted); }, QSize(342, 48));
        const QRect area = chevron.toRect().adjusted(-2, -2, 2, 2);
        const QString difference = describeDifference(rowImage.copy(area), expected.copy(area), 3);
        QVERIFY2(difference.isEmpty(), qPrintable(difference));
    }

    // A long number of an issue wraps in its column of 44 px (review F5)
    Painter::IssueRow longIssue = RowSamples::issueRow();
    longIssue.number = QStringLiteral("#1234567");
    const QImage longIssueImage = render([&](QPainter& painter) { Painter::paintIssueRow(&painter, QRectF(0, 0, 786, 52), longIssue, state(QStyle::State_None)); }, QSize(786, 52));
    const QImage shortIssueImage = render([&](QPainter& painter) { Painter::paintIssueRow(&painter, QRectF(0, 0, 786, 52), RowSamples::issueRow(), state(QStyle::State_None)); }, QSize(786, 52));
    QVERIFY(countPixels(longIssueImage, QRect(46, 8, 44, 9), token(Color::TextAccent), 40) > 0);
    QCOMPARE(countPixels(shortIssueImage, QRect(46, 8, 44, 9), token(Color::TextAccent), 40), 0);
    QCOMPARE(countPixels(longIssueImage, QRect(91, 0, 10, 52), token(Color::TextAccent), 40), 0);

    // An issue without its description (the issues of RELEASES.txt) has the title
    // centered in the row as the number, not at the place of the two lines (review R2-D2)
    for (const bool hasDescription : { false, true })
    {
        Painter::IssueRow titleOnly = RowSamples::issueRow();
        titleOnly.title = QStringLiteral("Crash");
        titleOnly.description = hasDescription ? QStringLiteral("Fixed") : QString();
        const QImage titleImage = render([&](QPainter& painter) { Painter::paintIssueRow(&painter, QRectF(0, 0, 786, 52), titleOnly, state(QStyle::State_None)); }, QSize(786, 52));
        int inkTop = -1;
        int inkBottom = -1;
        for (int y = 0; y < 52; ++y)
        {
            if (countPixels(titleImage, QRect(101, y, 200, 1), token(Color::TextPrimary), 60) > 0)
            {
                inkTop = inkTop < 0 ? y : inkTop;
                inkBottom = y;
            }
        }
        QVERIFY(inkTop >= 0);
        const qreal center = (inkTop + inkBottom) / 2.0;
        QVERIFY2(hasDescription ? center < 22.0 : qAbs(center - 26.0) <= 2.0, qPrintable(QStringLiteral("%1 %2").arg(inkTop).arg(inkBottom)));
    }

    // The height of a version row follows the summary also with the cached height (review N8)
    pdf::PDFVersionRow versionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14"), QStringLiteral("Short"));
    const int shortHeight = versionRow.heightForWidth(200);
    QCOMPARE(versionRow.heightForWidth(200), shortHeight);
    versionRow.setSummary(RowSamples::versionRow().summary + QStringLiteral(" ") + RowSamples::versionRow().summary);
    QVERIFY(versionRow.heightForWidth(200) > shortHeight);
    QCOMPARE(versionRow.heightForWidth(200), Painter::getVersionRowHeight(versionRow.getContent(), 200));
}

/// Contents of the cards of the step 3 as the default variants of the design
class CardSamples
{
public:
    CardSamples() = delete;

    static Painter::QuickActionTile tile()
    {
        Painter::QuickActionTile tile;
        tile.title = QStringLiteral("Open PDF");
        tile.description = QStringLiteral("Browse and open a document");
        return tile;
    }

    static Painter::FolderCard folder()
    {
        Painter::FolderCard card;
        card.name = QStringLiteral("Documents");
        card.path = QStringLiteral("C:\\Users\\Jakub\\Documents");
        card.meta = QStringLiteral("Last used Today 10:24");
        return card;
    }

    static Painter::PinnedCard pinned()
    {
        Painter::PinnedCard card;
        card.name = QStringLiteral("Project_Report.pdf");
        card.path = QStringLiteral("C:\\Users\\Jakub\\Documents");
        card.progressText = QStringLiteral("Page 12 of 24 · Today 10:24");
        card.progress = 0.5;
        return card;
    }

    static Painter::ToolCategoryHeader header()
    {
        return Painter::ToolCategoryHeader{ QStringLiteral("Pages"), QStringLiteral("Merge, split, reorder and rotate pages"), QStringLiteral("6 tools"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue, 0.0 };
    }

    static Painter::ToolChoice choice() { return Painter::ToolChoice{ QStringLiteral("Add Page Numbers"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue }; }
    static Painter::ResourceCard resource() { return Painter::ResourceCard{ QStringLiteral("User Manual"), QStringLiteral("Detailed documentation"), QStringLiteral("book-open"), pdf::PDFAccentHue::Neutral }; }

    static Painter::HighlightCard highlight()
    {
        return Painter::HighlightCard{ QStringLiteral("One app for everything"), QStringLiteral("Editor, Viewer, PageMaster and Diff are now a single application."), QStringLiteral("grid"), pdf::PDFAccentHue::Blue };
    }

    static Painter::VideoCard video()
    {
        Painter::VideoCard card;
        card.title = QStringLiteral("Getting started with PDF4QT");
        card.description = QStringLiteral("A quick tour of the interface and the main features.");
        card.duration = QStringLiteral("2:45");
        card.tags = QStringList{ QStringLiteral("OCR"), QStringLiteral("Basics") };
        return card;
    }

    static Painter::ReadingThemeCard reading()
    {
        Painter::ReadingThemeCard card;
        card.name = QStringLiteral("Paper");
        return card;
    }

    static Painter::PreviewCard preview() { return Painter::PreviewCard{ QStringLiteral("Normal"), pdf::PDFPreviewPainter::Preview::ModeNormal }; }
};

void FluentComponentsTest::cardSizes()
{
    // Sizes of the default variants of the cards
    QCOMPARE(Painter::getQuickActionTileSize(CardSamples::tile()), QSize(224, 88));
    QCOMPARE(Painter::getFolderCardSize(CardSamples::folder()), QSize(252, 84));
    QCOMPARE(Painter::getPinnedCardSize(CardSamples::pinned()), QSize(259, 100));
    QCOMPARE(Painter::getToolCategoryHeaderSize(CardSamples::header()), QSize(393, 61));
    QCOMPARE(Painter::getToolChoiceSize(CardSamples::choice()), QSize(154, 48));
    QCOMPARE(Painter::getResourceCardSize(CardSamples::resource()), QSize(116, 124));
    QCOMPARE(Painter::getHighlightCardSize(CardSamples::highlight()), QSize(259, 137));
    QCOMPARE(Painter::getVideoCardSize(CardSamples::video()), QSize(272, 258));
    QCOMPARE(Painter::getReadingThemeCardSize(CardSamples::reading()), QSize(159, 86));
    QCOMPARE(Painter::getPreviewCardSize(CardSamples::preview()), QSize(125, 122));
    QCOMPARE(pdf::PDFPreviewPainter::getPreviewSize(), QSize(115, 89));
    QCOMPARE(pdf::PDFPreviewPainter::getLockedDocumentSize(), QSize(160, 160));
    QCOMPARE(pdf::PDFPreviewPainter::getLogoSize(), QSize(130, 130));
    QCOMPARE(Painter::getToolChoiceRadius(), 10.0);

    // Heights follow the widths: wrapped texts of the resource, highlight and video cards
    Painter::ResourceCard resource = CardSamples::resource();
    QCOMPARE(Painter::getResourceCardHeight(resource, 116), 124);
    QVERIFY(Painter::getResourceCardHeight(resource, 60) > 124);
    resource.description.clear();
    QCOMPARE(Painter::getResourceCardHeight(resource, 116), 16 + 32 + 12 + 16 + 16);
    resource.title.clear();
    QCOMPARE(Painter::getResourceCardHeight(resource, 116), 16 + 32 + 12 + 16);
    Painter::HighlightCard highlight = CardSamples::highlight();
    QCOMPARE(Painter::getHighlightCardHeight(highlight, 259), 137);
    QCOMPARE(Painter::getHighlightCardHeight(highlight, 2000), 137 - 16);
    Painter::VideoCard video = CardSamples::video();
    QCOMPARE(Painter::getVideoCardHeight(video, 272), 258);
    QCOMPARE(Painter::getVideoCardHeight(video, 2000), 258 - 16);
    QCOMPARE(Painter::getVideoCardHeight(video, 120), 258);
    video.tags = QStringList{ QString(), QStringLiteral("  ") };
    QCOMPARE(Painter::getVideoCardHeight(video, 272), 258 - 31);

    // Widgets have the sizes of the painter and place the nested widgets as the cards
    pdf::PDFQuickActionTile tile(QStringLiteral("Open PDF"), QStringLiteral("Browse and open a document"), QStringLiteral("folder-open"), pdf::PDFAccentHue::Blue);
    QCOMPARE(tile.sizeHint(), QSize(224, 88));
    QVERIFY(!tile.isStarVisible());
    tile.resize(tile.sizeHint());
    tile.show();
    tile.setStarVisible(true);
    QVERIFY(tile.isStarVisible());
    QCOMPARE(tile.getStarButton()->geometry(), QRect(180, 30, 28, 28));
    QCOMPARE(tile.minimumSizeHint().width(), Painter::getQuickActionTileMinimumSize(tile.getContent()).width());
    QCOMPARE(tile.getFocusRing(), pdf::PDFComponentButton::FocusRing::Outside);
    QCOMPARE(tile.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 12.0);

    pdf::PDFFolderCard folder(QStringLiteral("Documents"), QStringLiteral("C:\\Users"), QStringLiteral("Last used"));
    QCOMPARE(folder.sizeHint(), QSize(252, 84));
    folder.resize(folder.sizeHint());
    folder.show();
    QCOMPARE(folder.getMenuButton()->geometry(), QRect(225, 34, 16, 16));
    QCOMPARE(folder.getHue(), pdf::PDFAccentHue::Neutral);
    QCOMPARE(folder.getIconName(), QStringLiteral("folder"));

    pdf::PDFPinnedCard pinned(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Page 12 of 24"), 0.5);
    QCOMPARE(pinned.sizeHint(), QSize(259, 100));
    QVERIFY(pinned.isPinned());
    pinned.resize(pinned.sizeHint());
    pinned.show();
    QCOMPARE(pinned.getStarButton()->geometry(), QRect(226, 13, 24, 24));
    QCOMPARE(pinned.getMenuButton()->geometry(), QRect(230, 41, 16, 16));

    pdf::PDFToolCategoryHeader header(QStringLiteral("Pages"), QStringLiteral("Merge"), QStringLiteral("6 tools"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    QCOMPARE(header.sizeHint(), QSize(393, 61));
    QCOMPARE(header.focusPolicy(), Qt::NoFocus);

    pdf::PDFToolChoice choice(QStringLiteral("Add Page Numbers"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    QCOMPARE(choice.sizeHint(), QSize(154, 48));
    QCOMPARE(choice.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 10.0);
    QCOMPARE(choice.getSelectionBehavior(), pdf::PDFComponentButton::SelectionBehavior::Deselectable);

    pdf::PDFResourceCard resourceCard(QStringLiteral("User Manual"), QStringLiteral("Detailed documentation"), QStringLiteral("book-open"), pdf::PDFAccentHue::Neutral);
    QCOMPARE(resourceCard.sizeHint(), QSize(116, 124));
    QVERIFY(resourceCard.hasHeightForWidth());
    QVERIFY(resourceCard.sizePolicy().hasHeightForWidth());
    QCOMPARE(resourceCard.heightForWidth(116), 124);
    QVERIFY(resourceCard.heightForWidth(60) > 124);
    static_cast<QAbstractButton&>(resourceCard).setText(QStringLiteral("A much longer title of the resource card, which wraps"));
    QVERIFY(resourceCard.heightForWidth(116) > 124);
    resourceCard.setTitle(QStringLiteral("User Manual"));
    QCOMPARE(resourceCard.heightForWidth(116), 124);

    pdf::PDFHighlightCard highlightCard(QStringLiteral("One app for everything"), CardSamples::highlight().description, QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    QCOMPARE(highlightCard.sizeHint(), QSize(259, 137));
    QCOMPARE(highlightCard.heightForWidth(259), 137);
    QCOMPARE(highlightCard.focusPolicy(), Qt::NoFocus);

    pdf::PDFVideoCard videoCard(QStringLiteral("Getting started with PDF4QT"), CardSamples::video().description, QStringLiteral("2:45"), CardSamples::video().tags);
    QCOMPARE(videoCard.sizeHint(), QSize(272, 258));
    QCOMPARE(videoCard.heightForWidth(272), 258);
    videoCard.resize(272, 258);
    videoCard.show();
    const QList<pdf::PDFTag*> tags = videoCard.getTagWidgets();
    QCOMPARE(tags.size(), 2);
    const int ocrWidth = Painter::getTagSize(Painter::Tag{ QStringLiteral("OCR") }).width();
    QCOMPARE(tags.at(0)->geometry(), QRect(13, 224, ocrWidth, 21));
    QCOMPARE(tags.at(1)->geometry().topLeft(), QPoint(13 + ocrWidth + 6, 224));

    pdf::PDFReadingThemeCard reading(QStringLiteral("Paper"), Color::ReadingPaperPage, Color::ReadingPaperText);
    QCOMPARE(reading.sizeHint(), QSize(159, 86));
    QCOMPARE(reading.getSelectionBehavior(), pdf::PDFComponentButton::SelectionBehavior::ExclusiveStatic);
    pdf::PDFPreviewCard preview(pdf::PDFPreviewPainter::Preview::ModeHighContrast);
    QCOMPARE(preview.sizeHint(), QSize(125, 122));
    QCOMPARE(preview.getSelectionBehavior(), pdf::PDFComponentButton::SelectionBehavior::ExclusiveStatic);

    pdf::PDFLockedDocumentPicture picture;
    QCOMPARE(picture.sizeHint(), QSize(160, 160));
    QCOMPARE(picture.getState(), pdf::PDFLockedDocumentPicture::State::Locked);

    // Resource cards of a row share the height of the tallest one (as on the
    // Home screen); the window is higher, a stretch below the row takes the rest
    QWidget window;
    QVBoxLayout* windowLayout = new QVBoxLayout(&window);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    QWidget* row = new QWidget(&window);
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(12);
    pdf::PDFResourceCard* shortCard = new pdf::PDFResourceCard(QStringLiteral("Manual"), QStringLiteral("Docs"), QStringLiteral("book-open"), pdf::PDFAccentHue::Green, row);
    pdf::PDFResourceCard* tallCard = new pdf::PDFResourceCard(QStringLiteral("Getting Started"), QStringLiteral("New to PDF4QT? Start here and learn the basics"), QStringLiteral("help-circle"), pdf::PDFAccentHue::Amber, row);
    rowLayout->addWidget(shortCard);
    rowLayout->addWidget(tallCard);
    windowLayout->addWidget(row);
    windowLayout->addStretch(1);
    window.resize(236, 400);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(shortCard->width(), tallCard->width());
    QVERIFY(tallCard->heightForWidth(tallCard->width()) > shortCard->heightForWidth(shortCard->width()));
    QCOMPARE(tallCard->height(), tallCard->heightForWidth(tallCard->width()));
    QCOMPARE(shortCard->height(), tallCard->height());
    QVERIFY(row->height() < 300);
}

void FluentComponentsTest::cardSizeStability()
{
    // Each card has the size of its variant Default in each state
    pdf::PDFQuickActionTile tile(QStringLiteral("Open PDF"), QStringLiteral("Browse"), QStringLiteral("folder-open"), pdf::PDFAccentHue::Blue);
    pdf::PDFFolderCard folder(QStringLiteral("Documents"), QStringLiteral("C:\\Users"), QStringLiteral("Last used"));
    pdf::PDFPinnedCard pinned(QStringLiteral("Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Page 1"), 0.1);
    pdf::PDFToolCategoryHeader header(QStringLiteral("Pages"), QStringLiteral("Merge"), QStringLiteral("6 tools"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    pdf::PDFToolChoice choice(QStringLiteral("Add Page Numbers"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    pdf::PDFResourceCard resource(QStringLiteral("User Manual"), QStringLiteral("Detailed documentation"), QStringLiteral("book-open"), pdf::PDFAccentHue::Neutral);
    pdf::PDFHighlightCard highlight(QStringLiteral("One app"), QStringLiteral("Editor and Viewer are one."), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    pdf::PDFVideoCard video(QStringLiteral("Getting started"), QStringLiteral("A quick tour"), QStringLiteral("2:45"), { QStringLiteral("OCR") });
    pdf::PDFReadingThemeCard reading(QStringLiteral("Paper"), Color::ReadingPaperPage, Color::ReadingPaperText);
    pdf::PDFPreviewCard preview(pdf::PDFPreviewPainter::Preview::ModeNormal);

    for (QAbstractButton* button : std::initializer_list<QAbstractButton*>{ &tile, &folder, &pinned, &header, &choice, &resource, &highlight, &video, &reading, &preview })
    {
        const QSize size = button->sizeHint();
        button->setAttribute(Qt::WA_UnderMouse, true);
        QCOMPARE(button->sizeHint(), size);
        button->setDown(true);
        QCOMPARE(button->sizeHint(), size);
        button->setDown(false);
        if (button->isCheckable())
        {
            button->setChecked(true);
            QCOMPARE(button->sizeHint(), size);
        }
        button->setLayoutDirection(Qt::RightToLeft);
        QCOMPARE(button->sizeHint(), size);
        button->setEnabled(false);
        QCOMPARE(button->sizeHint(), size);
        button->setEnabled(true);
    }

    // The star, the meta line, the pin and the progress do not change the size
    tile.setStarVisible(true);
    tile.setPinned(true);
    QCOMPARE(tile.sizeHint(), QSize(224, 88));
    folder.setMetaVisible(false);
    QCOMPARE(folder.sizeHint(), QSize(252, 84));
    pinned.setPinned(false);
    pinned.setProgress(1.0);
    QCOMPARE(pinned.sizeHint(), QSize(259, 100));

    // A change of the theme keeps the sizes
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(tile.sizeHint(), QSize(224, 88));
    QCOMPARE(choice.sizeHint(), QSize(154, 48));
    QCOMPARE(reading.sizeHint(), QSize(159, 86));
    QCOMPARE(resource.heightForWidth(116), 124);
}

void FluentComponentsTest::cardPixels_data()
{
    pixelProbes_data();
}

void FluentComponentsTest::cardPixels()
{
    QFETCH(QString, theme);
    QFETCH(qreal, scale);
    QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));

    using Role = pdf::PDFAccentHueColors::Role;
    const QColor surface = token(Color::BgSurface);
    const QColor hover = blend(token(Color::BgHover), surface);
    const QColor pressed = blend(token(Color::BgPressed), surface);
    const QStyle::State none = QStyle::State_None;
    const QStyle::State over = QStyle::State_MouseOver;
    const QStyle::State down = QStyle::State_MouseOver | QStyle::State_Sunken;
    const QStyle::State focus = QStyle::State_HasFocus;
    const QStyle::State on = QStyle::State_On;

    auto isCloser = [](const QColor& color, const QColor& expected, const QColor& other)
    {
        return colorDistance(color, expected) < colorDistance(color, other);
    };
    auto hueColor = [](pdf::PDFAccentHue hue, Role role) { return token(pdf::PDFAccentHueColors::getColor(hue, role)); };

    // Quick action tile: the recipe of the hue in all hues, the focus ring replaces the border
    for (const pdf::PDFAccentHue hue : { pdf::PDFAccentHue::Blue, pdf::PDFAccentHue::Violet, pdf::PDFAccentHue::Amber, pdf::PDFAccentHue::Red, pdf::PDFAccentHue::Teal, pdf::PDFAccentHue::Green, pdf::PDFAccentHue::Neutral })
    {
        Painter::QuickActionTile tile = CardSamples::tile();
        tile.hue = hue;
        auto renderTile = [&](QStyle::State flags) { return render([&](QPainter& painter) { Painter::paintQuickActionTile(&painter, QRectF(4, 4, 224, 88), tile, state(flags)); }, QSize(232, 96), scale); };
        QImage image = renderTile(none);
        QVERIFY(isNear(pixel(image, 104, 10), hueColor(hue, Role::Background)));
        QVERIFY(isNear(pixel(image, 104, 4), hueColor(hue, Role::Border)));
        QVERIFY(isNear(pixel(image, 24, 48), surface));
        QVERIFY(isCloser(strongestInk(image, QRect(30, 38, 20, 20), surface), hueColor(hue, Role::Icon), token(Color::IconDefault)) || hue == pdf::PDFAccentHue::Neutral);
        image = renderTile(over);
        QVERIFY(isNear(pixel(image, 104, 10), hueColor(hue, Role::BackgroundHover)));
        QVERIFY(isNear(pixel(image, 104, 4), hueColor(hue, Role::BorderStrong)));
        image = renderTile(down);
        QVERIFY(isNear(pixel(image, 104, 10), hueColor(hue, Role::BackgroundPressed)));
        QVERIFY(isNear(pixel(image, 104, 4), hueColor(hue, Role::BorderStrong)));
        image = renderTile(focus);
        QVERIFY(isNear(pixel(image, 104, 2), token(Color::BorderFocus)));
        QVERIFY(isNear(pixel(image, 104, 3), token(Color::BorderFocus)));
        QVERIFY(isNear(pixel(image, 104, 4), hueColor(hue, Role::Background)));
        image = render([&](QPainter& painter) { Painter::paintQuickActionTile(&painter, QRectF(4, 4, 224, 88), tile, state(focus), false); }, QSize(232, 96), scale);
        QVERIFY(isNear(pixel(image, 104, 3), surface));
        QVERIFY(isNear(pixel(image, 104, 4), hueColor(hue, Role::Background)));

        // Disabled: the fill of the rest, no hover, texts text/disabled (O-08)
        image = render([&](QPainter& painter) { Painter::paintQuickActionTile(&painter, QRectF(4, 4, 224, 88), tile, Painter::State(QStyle::State_MouseOver)); }, QSize(232, 96), scale);
        QVERIFY(isNear(pixel(image, 104, 10), hueColor(hue, Role::Background)));
        QCOMPARE(countPixels(image, QRect(72, 20, 140, 60), token(Color::TextPrimary), 8), 0);
        QVERIFY(countPixels(image, QRect(72, 20, 140, 60), token(Color::TextDisabled), 12) > 0);
    }

    // Folder card: the hues of the set, the menu darker under the pointer
    for (const pdf::PDFAccentHue hue : { pdf::PDFAccentHue::Blue, pdf::PDFAccentHue::Neutral, pdf::PDFAccentHue::Green, pdf::PDFAccentHue::Violet })
    {
        Painter::FolderCard card = CardSamples::folder();
        card.hue = hue;
        auto renderFolder = [&](QStyle::State flags) { return render([&](QPainter& painter) { Painter::paintFolderCard(&painter, QRectF(4, 4, 252, 84), card, state(flags)); }, QSize(260, 92), scale); };
        QImage image = renderFolder(none);
        const QColor fill = hueColor(hue, Role::Background);
        QVERIFY(isNear(pixel(image, 104, 8), fill));
        QVERIFY(isNear(pixel(image, 104, 4), hueColor(hue, Role::Border)));
        QVERIFY(isCloser(strongestInk(image, QRect(229, 38, 16, 16), fill), token(Color::IconMuted), token(Color::IconDefault)));
        image = renderFolder(over);
        QVERIFY(isNear(pixel(image, 104, 8), hueColor(hue, Role::BackgroundHover)));
        QVERIFY(isNear(pixel(image, 104, 4), hueColor(hue, Role::BorderStrong)));
        QVERIFY(isCloser(strongestInk(image, QRect(229, 38, 16, 16), hueColor(hue, Role::BackgroundHover)), token(Color::IconDefault), token(Color::IconMuted)));
        image = renderFolder(down);
        QVERIFY(isNear(pixel(image, 104, 8), hueColor(hue, Role::BackgroundPressed)));
        image = renderFolder(focus);
        QVERIFY(isNear(pixel(image, 104, 3), token(Color::BorderFocus)));
        QVERIFY(isNear(pixel(image, 104, 4), fill));
    }

    // Pinned card: own surface under the overlays, the progress, the thumbnail
    Painter::PinnedCard pinned = CardSamples::pinned();
    auto renderPinned = [&](const Painter::PinnedCard& content, QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintPinnedCard(&painter, QRectF(4, 4, 259, 100), content, state(flags)); }, QSize(267, 108), scale);
    };
    QImage image = renderPinned(pinned, none);
    QVERIFY(isNear(pixel(image, 154, 59), surface));
    QVERIFY(isNear(pixel(image, 154, 4), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, 95, 89), token(Color::BgAccent)));
    QVERIFY(isNear(pixel(image, 205, 89), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, 17, 50), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(image, 4 + 226 + 12, 4 + 13 + 12), token(Color::IconSun), 12));
    image = renderPinned(pinned, over);
    QVERIFY(isNear(pixel(image, 154, 59), hover));
    QVERIFY(isNear(pixel(image, 154, 4), token(Color::BorderStrong)));
    image = renderPinned(pinned, down);
    QVERIFY(isNear(pixel(image, 154, 59), pressed));
    image = renderPinned(pinned, focus);
    QVERIFY(isNear(pixel(image, 154, 3), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 154, 4), surface));
    pinned.progress = 1.0;
    image = renderPinned(pinned, none);
    QVERIFY(isNear(pixel(image, 205, 89), token(Color::AccentGreenIcon)));
    pinned.progress = 0.0;
    image = renderPinned(pinned, none);
    QVERIFY(isNear(pixel(image, 86, 89), token(Color::BorderDefault)));
    pinned.paintNested = false;
    image = renderPinned(pinned, none);
    QVERIFY(isNear(pixel(image, 4 + 226 + 12, 4 + 13 + 12), surface));

    // Tool category header: the tint, the border at the bottom, the white circle
    const Painter::ToolCategoryHeader header = CardSamples::header();
    image = render([&](QPainter& painter) { Painter::paintToolCategoryHeader(&painter, QRectF(0, 0, 393, 61), header, state(none)); }, QSize(393, 61), scale);
    QVERIFY(isNear(pixel(image, 200, 5), token(Color::AccentBlueBg)));
    QVERIFY(isNear(pixel(image, 200, 60), token(Color::AccentBlueBorder)));
    QVERIFY(isNear(pixel(image, 20, 30), surface));
    QVERIFY(countPixels(image, QRect(320, 22, 60, 16), token(Color::TextTertiary), 12) > 0);

    // Tool choice: the selection ring inside, bg/surface under the overlays, the focus ring outside
    const Painter::ToolChoice choice = CardSamples::choice();
    auto renderChoice = [&](QStyle::State flags) { return render([&](QPainter& painter) { Painter::paintToolChoice(&painter, QRectF(4, 4, 154, 48), choice, state(flags)); }, QSize(162, 56), scale); };
    const QColor selectedFill = blend(token(Color::BgAccentSubtle), surface);
    image = renderChoice(none);
    QVERIFY(isNear(pixel(image, 81, 4), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, 151, 28), surface));
    QCOMPARE(colorDistance(strongestInk(image, QRect(131, 20, 16, 16), surface), surface), 0);
    image = renderChoice(over);
    QVERIFY(isNear(pixel(image, 151, 28), hover));
    image = renderChoice(down);
    QVERIFY(isNear(pixel(image, 151, 28), pressed));
    image = renderChoice(focus);
    QVERIFY(isNear(pixel(image, 81, 2), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 81, 4), token(Color::BorderDefault)));
    image = renderChoice(on);
    QVERIFY(isNear(pixel(image, 81, 4), token(Color::BgAccent)));
    QVERIFY(isNear(pixel(image, 81, 5), token(Color::BgAccent)));
    QVERIFY(isNear(pixel(image, 151, 28), selectedFill));
    QVERIFY(isCloser(strongestInk(image, QRect(131, 20, 16, 16), selectedFill), token(Color::IconAccent), token(Color::IconDefault)));
    image = renderChoice(on | over);
    QVERIFY(isNear(pixel(image, 151, 28), blend(token(Color::BgHover), selectedFill)));
    image = renderChoice(on | down);
    QVERIFY(isNear(pixel(image, 151, 28), blend(token(Color::BgPressed), selectedFill)));
    image = renderChoice(on | focus);
    QVERIFY(isNear(pixel(image, 81, 2), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 81, 4), token(Color::BgAccent)));

    // Resource card and Highlight card
    auto renderResource = [&](pdf::PDFAccentHue hue, QStyle::State flags)
    {
        Painter::ResourceCard card = CardSamples::resource();
        card.hue = hue;
        return render([&](QPainter& painter) { Painter::paintResourceCard(&painter, QRectF(4, 4, 116, 124), card, state(flags)); }, QSize(124, 132), scale);
    };
    for (const pdf::PDFAccentHue hue : { pdf::PDFAccentHue::Neutral, pdf::PDFAccentHue::Green, pdf::PDFAccentHue::Amber, pdf::PDFAccentHue::Violet })
    {
        image = renderResource(hue, none);
        QVERIFY(isNear(pixel(image, 104, 10), hueColor(hue, Role::Background)));
        QVERIFY(isNear(pixel(image, 60, 4), hueColor(hue, Role::Border)));
        QVERIFY(colorDistance(strongestInk(image, QRect(24, 24, 24, 24), hueColor(hue, Role::Background)), hueColor(hue, Role::Background)) > 60);
        image = renderResource(hue, over);
        QVERIFY(isNear(pixel(image, 104, 10), hueColor(hue, Role::BackgroundHover)));
        image = renderResource(hue, down);
        QVERIFY(isNear(pixel(image, 104, 10), hueColor(hue, Role::BackgroundPressed)));
    }
    Painter::HighlightCard highlight = CardSamples::highlight();
    highlight.hue = pdf::PDFAccentHue::Teal;
    image = render([&](QPainter& painter) { Painter::paintHighlightCard(&painter, QRectF(4, 4, 259, 137), highlight, state(over)); }, QSize(267, 145), scale);
    QVERIFY(isNear(pixel(image, 154, 24), surface));
    QVERIFY(isNear(pixel(image, 154, 4), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, 25, 41), token(Color::AccentTealBg)));

    // Video card: the preview, the play button, the duration, the states of the surface
    const Painter::VideoCard video = CardSamples::video();
    auto renderVideo = [&](QStyle::State flags) { return render([&](QPainter& painter) { Painter::paintVideoCard(&painter, QRectF(4, 4, 272, 258), video, state(flags)); }, QSize(280, 266), scale); };
    image = renderVideo(none);
    QVERIFY(isNear(pixel(image, 20, 20), token(Color::BgMuted)));
    QVERIFY(isNear(pixel(image, 140, 154), token(Color::BorderDivider)));
    QVERIFY(isNear(pixel(image, 125, 80), blend(token(Color::BgScrim), token(Color::BgMuted))));
    QVERIFY(isNear(pixel(image, 140, 80), token(Color::IconOnAccent), 24));
    QVERIFY(isNear(pixel(image, 266, 245), surface));
    QVERIFY(isNear(pixel(image, 140, 261), token(Color::BorderDefault)));
    QVERIFY(countPixels(image, QRect(17, 228, 30, 21), token(Color::BgSubtle), 2) > 0);
    image = renderVideo(over);
    QVERIFY(isNear(pixel(image, 266, 245), token(Color::BgSubtle)));
    QVERIFY(isNear(pixel(image, 140, 261), token(Color::BorderStrong)));
    image = renderVideo(down);
    QVERIFY(isNear(pixel(image, 266, 245), token(Color::BgMuted)));
    image = renderVideo(focus);
    QVERIFY(isNear(pixel(image, 140, 2), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 140, 261), surface));

    // Reading theme card: the sample in the colors of the theme, the selection and the focus rings
    Painter::ReadingThemeCard reading = CardSamples::reading();
    reading.pageColor = Color::ReadingNightPage;
    reading.textColor = Color::ReadingNightText;
    auto renderReading = [&](const Painter::ReadingThemeCard& content, QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintReadingThemeCard(&painter, QRectF(4, 4, 159, 86), content, state(flags)); }, QSize(167, 94), scale);
    };
    image = renderReading(reading, none);
    QVERIFY(isNear(pixel(image, 12, 34), token(Color::ReadingNightPage)));
    QVERIFY(isNear(pixel(image, 154, 76), surface));
    QVERIFY(colorDistance(strongestInk(image, QRect(18, 22, 30, 26), token(Color::ReadingNightPage)), token(Color::ReadingNightPage)) > 100);
    image = renderReading(reading, over);
    QVERIFY(isNear(pixel(image, 154, 76), hover));
    image = renderReading(reading, down);
    QVERIFY(isNear(pixel(image, 154, 76), pressed));
    image = renderReading(reading, focus);
    QVERIFY(isNear(pixel(image, 84, 2), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 84, 4), surface));
    for (const QStyle::State flags : { on, on | over, on | down })
    {
        // The selected card does not react to the pointer
        image = renderReading(reading, flags);
        QVERIFY(isNear(pixel(image, 84, 4), token(Color::BgAccent)));
        QVERIFY(isNear(pixel(image, 84, 5), token(Color::BgAccent)));
        QVERIFY(isNear(pixel(image, 154, 64), blend(token(Color::BgAccentSubtle), surface)));
        QVERIFY(countPixels(image, QRect(12, 68, 40, 15), token(Color::TextAccent), 16) > 0);
    }
    image = renderReading(reading, on | focus);
    QVERIFY(isNear(pixel(image, 84, 2), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 84, 4), token(Color::BgAccent)));
    Painter::ReadingThemeCard custom = reading;
    custom.customPageColor = QColor(QStringLiteral("#123456"));
    image = renderReading(custom, none);
    QVERIFY(isNear(pixel(image, 12, 34), QColor(QStringLiteral("#123456"))));

    // Color mode card: the picture, the selection as Reading theme card
    const Painter::PreviewCard preview = CardSamples::preview();
    auto renderPreviewCard = [&](QStyle::State flags) { return render([&](QPainter& painter) { Painter::paintPreviewCard(&painter, QRectF(4, 4, 125, 122), preview, state(flags)); }, QSize(133, 130), scale); };
    image = renderPreviewCard(none);
    QVERIFY(isNear(pixel(image, 12, 50), token(Color::SampleWhite)));
    QVERIFY(isNear(pixel(image, 9, 50), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(image, 120, 112), surface));
    image = renderPreviewCard(over);
    QVERIFY(isNear(pixel(image, 120, 112), hover));
    image = renderPreviewCard(on | over);
    QVERIFY(isNear(pixel(image, 120, 100), blend(token(Color::BgAccentSubtle), surface)));
    QVERIFY(isNear(pixel(image, 60, 4), token(Color::BgAccent)));

    QCOMPARE(countLeakPixels(image), 0);
}

void FluentComponentsTest::cardWidgetsMatchPainter()
{
    // The widgets draw the same as the painter with the nested elements
    QWidget window;
    window.resize(900, 700);
    pdf::PDFQuickActionTile* tile = new pdf::PDFQuickActionTile(QStringLiteral("Compress"), QStringLiteral("Reduce the file size in one step"), QStringLiteral("minimize-2"), pdf::PDFAccentHue::Violet, &window);
    tile->setGeometry(0, 0, 224, 88);
    tile->setStarVisible(true);
    tile->setPinned(true);
    pdf::PDFFolderCard* folder = new pdf::PDFFolderCard(QStringLiteral("Documents"), QStringLiteral("C:\\Users\\Jakub\\Documents"), QStringLiteral("Last used Today 10:24"), pdf::PDFAccentHue::Green, &window);
    folder->setGeometry(240, 0, 252, 84);
    pdf::PDFPinnedCard* pinned = new pdf::PDFPinnedCard(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users\\Jakub\\Documents"), QStringLiteral("Page 12 of 24"), 0.5, &window);
    pinned->setGeometry(500, 0, 259, 100);
    QPixmap page(QSize(60, 80));
    page.fill(QColor(QStringLiteral("#4080c0")));
    pinned->setPixmap(page);
    pdf::PDFVideoCard* video = new pdf::PDFVideoCard(QStringLiteral("Getting started with PDF4QT"), CardSamples::video().description, QStringLiteral("2:45"), CardSamples::video().tags, &window);
    video->setGeometry(0, 120, 272, 258);
    pdf::PDFToolChoice* choice = new pdf::PDFToolChoice(QStringLiteral("Add Page Numbers"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue, &window);
    choice->setGeometry(300, 120, 154, 48);
    choice->setSelected(true);
    pdf::PDFReadingThemeCard* reading = new pdf::PDFReadingThemeCard(QStringLiteral("Sepia"), Color::ReadingSepiaPage, Color::ReadingSepiaText, &window);
    reading->setGeometry(300, 200, 159, 86);
    pdf::PDFPreviewCard* preview = new pdf::PDFPreviewCard(pdf::PDFPreviewPainter::Preview::LayoutTwoColumns, &window);
    preview->setGeometry(500, 200, 125, 122);
    pdf::PDFResourceCard* resource = new pdf::PDFResourceCard(QStringLiteral("User Manual"), QStringLiteral("Detailed documentation"), QStringLiteral("book-open"), pdf::PDFAccentHue::Amber, &window);
    resource->setGeometry(650, 200, 116, 124);
    pdf::PDFHighlightCard* highlight = new pdf::PDFHighlightCard(QStringLiteral("One app"), QStringLiteral("All in one."), QStringLiteral("grid"), pdf::PDFAccentHue::Red, &window);
    highlight->setGeometry(0, 400, 259, 137);
    pdf::PDFToolCategoryHeader* header = new pdf::PDFToolCategoryHeader(QStringLiteral("Pages"), QStringLiteral("Merge"), QStringLiteral("6 tools"), QStringLiteral("grid"), pdf::PDFAccentHue::Amber, &window);
    header->setGeometry(300, 400, 393, 61);
    window.setFocusPolicy(Qt::StrongFocus);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.setFocus(Qt::OtherFocusReason);

    for (const qreal scale : { 1.0, 1.5, 2.0 })
    {
        const int tolerance = qFuzzyCompare(scale, 1.5) ? 24 : 3;

        for (const bool hovered : { false, true })
        {
            for (QWidget* card : std::initializer_list<QWidget*>{ tile, folder, pinned, video, choice, reading, preview, resource })
            {
                card->setAttribute(Qt::WA_UnderMouse, hovered);
            }

            const QStyle::State flags = hovered ? QStyle::State_MouseOver : QStyle::State_None;
            const QString prefix = QStringLiteral("scale %1, hovered %2").arg(scale).arg(hovered);
            auto compare = [&](QWidget* widget, const std::function<void(QPainter&)>& paint, const char* name)
            {
                const QString difference = describeDifference(renderWidget(widget, scale), renderInWidget(paint, widget->size(), scale), tolerance);
                return difference.isEmpty() ? QString() : QStringLiteral("%1 (%2): %3").arg(QString::fromLatin1(name), prefix, difference);
            };

            Painter::QuickActionTile tileContent = tile->getContent();
            tileContent.paintNested = true;
            QString difference = compare(tile, [&](QPainter& painter) { Painter::paintQuickActionTile(&painter, QRectF(0, 0, 224, 88), tileContent, state(flags)); }, "Quick action tile");
            QVERIFY2(difference.isEmpty(), qPrintable(difference));

            Painter::FolderCard folderContent = folder->getContent();
            folderContent.paintNested = true;
            difference = compare(folder, [&](QPainter& painter) { Painter::paintFolderCard(&painter, QRectF(0, 0, 252, 84), folderContent, state(flags)); }, "Folder card");
            QVERIFY2(difference.isEmpty(), qPrintable(difference));

            Painter::PinnedCard pinnedContent = pinned->getContent();
            pinnedContent.paintNested = true;
            difference = compare(pinned, [&](QPainter& painter) { Painter::paintPinnedCard(&painter, QRectF(0, 0, 259, 100), pinnedContent, state(flags)); }, "Pinned card");
            QVERIFY2(difference.isEmpty(), qPrintable(difference));

            Painter::VideoCard videoContent = video->getContent();
            videoContent.paintNested = true;
            difference = compare(video, [&](QPainter& painter) { Painter::paintVideoCard(&painter, QRectF(0, 0, 272, 258), videoContent, state(flags)); }, "Video card");
            QVERIFY2(difference.isEmpty(), qPrintable(difference));

            // The selected tool choice reacts to the pointer (it can be unselected)
            difference = compare(choice, [&](QPainter& painter) { Painter::paintToolChoice(&painter, QRectF(0, 0, 154, 48), choice->getContent(), state(flags | QStyle::State_On)); }, "Tool choice");
            QVERIFY2(difference.isEmpty(), qPrintable(difference));

            difference = compare(reading, [&](QPainter& painter) { Painter::paintReadingThemeCard(&painter, QRectF(0, 0, 159, 86), reading->getContent(), state(flags)); }, "Reading theme card");
            QVERIFY2(difference.isEmpty(), qPrintable(difference));

            difference = compare(preview, [&](QPainter& painter) { Painter::paintPreviewCard(&painter, QRectF(0, 0, 125, 122), preview->getContent(), state(flags)); }, "Color mode card");
            QVERIFY2(difference.isEmpty(), qPrintable(difference));

            difference = compare(resource, [&](QPainter& painter) { Painter::paintResourceCard(&painter, QRectF(0, 0, 116, 124), resource->getContent(), state(flags)); }, "Resource card");
            QVERIFY2(difference.isEmpty(), qPrintable(difference));

            // Not interactive: no hover
            difference = compare(highlight, [&](QPainter& painter) { Painter::paintHighlightCard(&painter, QRectF(0, 0, 259, 137), highlight->getContent(), state(QStyle::State_None)); }, "Highlight card");
            QVERIFY2(difference.isEmpty(), qPrintable(difference));
            difference = compare(header, [&](QPainter& painter) { Painter::paintToolCategoryHeader(&painter, QRectF(0, 0, 393, 61), header->getContent(), state(QStyle::State_None)); }, "Tool category header");
            QVERIFY2(difference.isEmpty(), qPrintable(difference));
        }

        // The selected reading theme card does not react to the pointer
        reading->setSelected(true);
        reading->setAttribute(Qt::WA_UnderMouse, true);
        QVERIFY(!reading->getComponentState().isHovered());
        const QString difference = describeDifference(renderWidget(reading, scale), renderInWidget([&](QPainter& painter) { Painter::paintReadingThemeCard(&painter, QRectF(0, 0, 159, 86), reading->getContent(), state(QStyle::State_On)); }, QSize(159, 86), scale), tolerance);
        QVERIFY2(difference.isEmpty(), qPrintable(difference));
        reading->setSelected(false);

        QCOMPARE(countLeakPixels(renderWidget(&window, scale)), 0);
    }
}

void FluentComponentsTest::cardBehaviour()
{
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    pdf::PDFQuickActionTile* tile = new pdf::PDFQuickActionTile(QStringLiteral("Merge"), QStringLiteral("Combine"), QStringLiteral("layers"), pdf::PDFAccentHue::Amber, &window);
    tile->setStarVisible(true);
    pdf::PDFFolderCard* folder = new pdf::PDFFolderCard(QStringLiteral("Documents"), QStringLiteral("C:\\Users"), QStringLiteral("Last used"), pdf::PDFAccentHue::Green, &window);
    pdf::PDFPinnedCard* pinned = new pdf::PDFPinnedCard(QStringLiteral("Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Page 1"), 0.2, &window);
    pdf::PDFVideoCard* video = new pdf::PDFVideoCard(QStringLiteral("Getting started"), QStringLiteral("A tour"), QStringLiteral("2:45"), { QStringLiteral("OCR"), QStringLiteral("Basics") }, &window);
    pdf::PDFHighlightCard* highlight = new pdf::PDFHighlightCard(QStringLiteral("One app"), QStringLiteral("All in one"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue, &window);
    for (QWidget* widget : std::initializer_list<QWidget*>{ tile, folder, pinned, video, highlight })
    {
        layout->addWidget(widget);
    }
    QMenu menu;
    menu.addAction(QStringLiteral("Open"));
    folder->setMenu(&menu);
    pinned->setMenu(&menu);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    // Quick action tile: the star pins the tool, the tile is not activated
    QSignalSpy tileClicked(tile, &QAbstractButton::clicked);
    QSignalSpy tilePinned(tile, &pdf::PDFQuickActionTile::pinnedChanged);
    QTest::mouseClick(tile->getStarButton(), Qt::LeftButton);
    QCOMPARE(tilePinned.count(), 1);
    QVERIFY(tile->isPinned());
    QCOMPARE(tileClicked.count(), 0);
    tile->setPinned(true);
    QCOMPARE(tilePinned.count(), 1);
    QTest::mouseClick(tile, Qt::LeftButton, Qt::NoModifier, QPoint(80, 40));
    QCOMPARE(tileClicked.count(), 1);
    tile->setFocus(Qt::TabFocusReason);
    QTest::keyClick(tile, Qt::Key_Return);
    QTest::keyClick(tile, Qt::Key_Space);
    QCOMPARE(tileClicked.count(), 3);
    tile->setStarVisible(false);
    QVERIFY(tile->getStarButton()->isHidden());

    // Folder card: the menu button and a right click show the menu, the card is not activated
    QSignalSpy folderClicked(folder, &QAbstractButton::clicked);
    QSignalSpy folderMenu(folder, &pdf::PDFFolderCard::menuRequested);
    QTest::mouseClick(folder->getMenuButton(), Qt::LeftButton);
    QCOMPARE(folderMenu.count(), 1);
    QCOMPARE(folderClicked.count(), 0);
    QTRY_VERIFY(menu.isVisible());
    menu.hide();
    const QPoint inside(60, 40);
    QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, inside, folder->mapToGlobal(inside));
    QApplication::sendEvent(folder, &contextEvent);
    QCOMPARE(folderMenu.count(), 2);
    QTRY_VERIFY(menu.isVisible());
    menu.hide();
    QTest::mouseClick(folder, Qt::LeftButton, Qt::NoModifier, inside);
    QCOMPARE(folderClicked.count(), 1);

    // Tab reaches the menu button, Enter on it shows the menu, the card is not activated
    folder->setFocus(Qt::TabFocusReason);
    QTest::keyClick(folder, Qt::Key_Tab);
    QTRY_VERIFY(folder->getMenuButton()->hasFocus());
    QTest::keyClick(folder->getMenuButton(), Qt::Key_Return);
    QCOMPARE(folderMenu.count(), 3);
    QTRY_VERIFY(menu.isVisible());
    menu.hide();
    QCOMPARE(folderClicked.count(), 1);

    // Pinned card: the star unpins the document, the menu, the card itself
    QSignalSpy pinnedClicked(pinned, &QAbstractButton::clicked);
    QSignalSpy pinnedChanged(pinned, &pdf::PDFPinnedCard::pinnedChanged);
    QSignalSpy pinnedMenu(pinned, &pdf::PDFPinnedCard::menuRequested);
    QTest::mouseClick(pinned->getStarButton(), Qt::LeftButton);
    QCOMPARE(pinnedChanged.count(), 1);
    QCOMPARE(pinnedChanged.back().front().toBool(), false);
    QVERIFY(!pinned->isPinned());
    QTest::mouseClick(pinned->getMenuButton(), Qt::LeftButton);
    QCOMPARE(pinnedMenu.count(), 1);
    QTRY_VERIFY(menu.isVisible());
    menu.hide();
    QCOMPARE(pinnedClicked.count(), 0);
    QTest::mouseClick(pinned, Qt::LeftButton, Qt::NoModifier, QPoint(100, 50));
    QCOMPARE(pinnedClicked.count(), 1);

    // Video card: a tag filters the list (signal tagClicked), the card is not activated
    QSignalSpy videoClicked(video, &QAbstractButton::clicked);
    QSignalSpy tagClicked(video, &pdf::PDFVideoCard::tagClicked);
    QApplication::processEvents();
    const QList<pdf::PDFTag*> tags = video->getTagWidgets();
    QCOMPARE(tags.size(), 2);
    QVERIFY(tags.at(0)->isClickable());
    QTest::mouseClick(tags.at(1), Qt::LeftButton);
    QCOMPARE(tagClicked.count(), 1);
    QCOMPARE(tagClicked.back().front().toString(), QStringLiteral("Basics"));
    QCOMPARE(videoClicked.count(), 0);
    tags.at(0)->setEnabled(false);
    QTest::mouseClick(tags.at(0), Qt::LeftButton);
    QCOMPARE(videoClicked.count(), 0);
    QCOMPARE(tagClicked.count(), 1);
    QTest::mouseClick(video, Qt::LeftButton, Qt::NoModifier, QPoint(100, 60));
    QCOMPARE(videoClicked.count(), 1);

    // New tags replace the old ones (also from the slot of a tag); empty tags are left out
    connect(video, &pdf::PDFVideoCard::tagClicked, video, [video]() { video->setTags({ QStringLiteral("Forms"), QString(), QStringLiteral(" "), QStringLiteral("Review") }); });
    QTest::mouseClick(video->getTagWidgets().at(1), Qt::LeftButton);
    QApplication::processEvents();
    QTRY_COMPARE(video->getTagWidgets().size(), 2);
    QCOMPARE(video->getTagWidgets().at(0)->text(), QStringLiteral("Forms"));
    QCOMPARE(video->getTagWidgets().at(1)->text(), QStringLiteral("Review"));

    // A tag, which does not fit the width, is hidden
    video->setTags({ QStringLiteral("Signatures"), QStringLiteral("Security"), QStringLiteral("Certificates"), QStringLiteral("Advanced"), QStringLiteral("Timestamps") });
    layout->removeWidget(video);
    video->resize(272, video->heightForWidth(272));
    const QList<pdf::PDFTag*> manyTags = video->getTagWidgets();
    QCOMPARE(manyTags.size(), 5);
    QVERIFY(!manyTags.front()->isHidden());
    QVERIFY(manyTags.back()->isHidden());
    for (const pdf::PDFTag* tag : manyTags)
    {
        QVERIFY(tag->isHidden() || video->rect().contains(tag->geometry()));
    }

    // The highlight card is a text: no focus, no activation, no hover
    QSignalSpy highlightClicked(highlight, &QAbstractButton::clicked);
    QTest::mouseClick(highlight, Qt::LeftButton);
    highlight->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY(!highlight->getComponentState().isHovered());
    QCOMPARE(highlightClicked.count(), 0);
    QCOMPARE(highlight->focusPolicy(), Qt::NoFocus);
}

void FluentComponentsTest::cardChoices()
{
    // Tool choice: an exclusive group, which can be left empty
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    QButtonGroup group;
    QList<pdf::PDFToolChoice*> choices;
    for (const char* name : { "Add Page Numbers", "Compress", "Sign" })
    {
        pdf::PDFToolChoice* choice = new pdf::PDFToolChoice(QString::fromLatin1(name), QStringLiteral("grid"), pdf::PDFAccentHue::Blue, &window);
        group.addButton(choice);
        layout->addWidget(choice);
        choices << choice;
    }
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QCOMPARE(group.checkedButton(), nullptr);
    QTest::mouseClick(choices.at(1), Qt::LeftButton);
    QVERIFY(choices.at(1)->isSelected());
    QTest::mouseClick(choices.at(0), Qt::LeftButton);
    QVERIFY(choices.at(0)->isSelected());
    QVERIFY(!choices.at(1)->isSelected());

    // A click on the selected tool unselects it, the group is empty and stays exclusive
    QTest::mouseClick(choices.at(0), Qt::LeftButton);
    QVERIFY(!choices.at(0)->isSelected());
    QCOMPARE(group.checkedButton(), nullptr);
    QVERIFY(group.exclusive());

    // The selected tool reacts to the pointer; Space unselects it as well
    choices.at(2)->setSelected(true);
    choices.at(2)->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY(choices.at(2)->getComponentState().isHovered());
    choices.at(2)->setFocus(Qt::TabFocusReason);
    QTest::keyClick(choices.at(2), Qt::Key_Space);
    QVERIFY(!choices.at(2)->isSelected());
    choices.at(2)->setSelected(true);
    choices.at(2)->setSelected(false);
    QCOMPARE(group.checkedButton(), nullptr);

    // The arrows from the selected tool move the selection, from an unselected
    // tool (the group can be empty) only the focus
    choices.at(0)->setSelected(true);
    choices.at(0)->setFocus(Qt::TabFocusReason);
    QTest::keyClick(choices.at(0), Qt::Key_Right);
    QTRY_VERIFY(choices.at(1)->hasFocus());
    QVERIFY(choices.at(1)->isSelected());
    QVERIFY(!choices.at(0)->isSelected());
    choices.at(1)->setSelected(false);
    QTest::keyClick(choices.at(1), Qt::Key_Right);
    QTRY_VERIFY(choices.at(2)->hasFocus());
    QCOMPARE(group.checkedButton(), nullptr);

    // Reading theme cards and Color mode cards: the selected card is static
    QWidget themes;
    QHBoxLayout* themesLayout = new QHBoxLayout(&themes);
    QButtonGroup themeGroup;
    pdf::PDFReadingThemeCard* paper = new pdf::PDFReadingThemeCard(QStringLiteral("Paper"), Color::ReadingPaperPage, Color::ReadingPaperText, &themes);
    pdf::PDFReadingThemeCard* sepia = new pdf::PDFReadingThemeCard(QStringLiteral("Sepia"), Color::ReadingSepiaPage, Color::ReadingSepiaText, &themes);
    pdf::PDFPreviewCard* inverted = new pdf::PDFPreviewCard(pdf::PDFPreviewPainter::Preview::ModeInverted, &themes);
    for (QAbstractButton* button : std::initializer_list<QAbstractButton*>{ paper, sepia, inverted })
    {
        themeGroup.addButton(button);
        themesLayout->addWidget(button);
    }
    themes.show();
    themes.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&themes));

    QSignalSpy paperClicked(paper, &QAbstractButton::clicked);
    paper->setSelected(true);
    QTest::mouseClick(paper, Qt::LeftButton);
    QCOMPARE(paperClicked.count(), 0);
    QVERIFY(paper->isSelected());
    paper->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY(!paper->getComponentState().isHovered());
    QTest::mouseClick(sepia, Qt::LeftButton);
    QVERIFY(sepia->isSelected());
    QVERIFY(!paper->isSelected());
    QTest::mouseClick(inverted, Qt::LeftButton);
    QVERIFY(inverted->isSelected());

    // The arrows move the selection in the group
    sepia->setFocus(Qt::TabFocusReason);
    sepia->setSelected(true);
    QTest::keyClick(sepia, Qt::Key_Right);
    QTRY_VERIFY(inverted->hasFocus());
    QVERIFY(inverted->isSelected());
    QVERIFY(!sepia->isSelected());
    QTest::keyClick(inverted, Qt::Key_Left);
    QTRY_VERIFY(sepia->hasFocus());
    QVERIFY(sepia->isSelected());
    QVERIFY(!inverted->isSelected());

    // Unselecting by the program leaves the group empty
    inverted->setSelected(true);
    inverted->setSelected(false);
    QCOMPARE(themeGroup.checkedButton(), nullptr);

    // Disabled: no hover, no activation
    sepia->setEnabled(false);
    QSignalSpy sepiaClicked(sepia, &QAbstractButton::clicked);
    QTest::mouseClick(sepia, Qt::LeftButton);
    QCOMPARE(sepiaClicked.count(), 0);
}

void FluentComponentsTest::cardAccessibility()
{
    pdf::PDFQuickActionTile tile(QStringLiteral("Protect & Sign"), QStringLiteral("Passwords and signatures"), QStringLiteral("lock"), pdf::PDFAccentHue::Red);
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(&tile);
    QCOMPARE(accessible->role(), QAccessible::Button);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Protect & Sign"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Passwords and signatures"));
    QCOMPARE(tile.shortcut(), QKeySequence());
    tile.setStarVisible(true);
    QAccessibleInterface* star = QAccessible::queryAccessibleInterface(tile.getStarButton());
    QCOMPARE(star->text(QAccessible::Name), QStringLiteral("Pin tool"));
    QCOMPARE(star->text(QAccessible::Description), QStringLiteral("Protect & Sign"));
    QCOMPARE(tile.getStarButton()->toolTip(), QStringLiteral("Pin tool"));
    tile.setPinned(true);
    QCOMPARE(star->text(QAccessible::Name), QStringLiteral("Unpin tool"));
    tile.setTitle(QStringLiteral("Compress"));
    QCOMPARE(star->text(QAccessible::Description), QStringLiteral("Compress"));
    tile.getStarButton()->setAccessibleDescription(QStringLiteral("Application description"));
    QCOMPARE(star->text(QAccessible::Description), QStringLiteral("Application description"));

    pdf::PDFFolderCard folder(QStringLiteral("Documents"), QStringLiteral("C:\\Docs"), QStringLiteral("Last used Today"));
    accessible = QAccessible::queryAccessibleInterface(&folder);
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("C:\\Docs, Last used Today"));
    folder.setMetaVisible(false);
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("C:\\Docs"));
    QCOMPARE(QAccessible::queryAccessibleInterface(folder.getMenuButton())->text(QAccessible::Name), QStringLiteral("More actions"));

    pdf::PDFPinnedCard pinned(QStringLiteral("Report.pdf"), QStringLiteral("C:\\Docs"), QStringLiteral("Page 12 of 24"), 0.5);
    QCOMPARE(QAccessible::queryAccessibleInterface(&pinned)->text(QAccessible::Description), QStringLiteral("C:\\Docs, Page 12 of 24"));
    QCOMPARE(QAccessible::queryAccessibleInterface(pinned.getStarButton())->text(QAccessible::Name), QStringLiteral("Unpin"));
    QCOMPARE(QAccessible::queryAccessibleInterface(pinned.getStarButton())->text(QAccessible::Description), QStringLiteral("Report.pdf"));

    // The stars of the rows of step 2: a tool row pins a tool, a file row a file
    pdf::PDFToolRow toolRow(QStringLiteral("Merge"), QStringLiteral("Combine files"), QStringLiteral("layers"), pdf::PDFAccentHue::Amber);
    QCOMPARE(QAccessible::queryAccessibleInterface(toolRow.findChild<pdf::PDFStarButton*>())->text(QAccessible::Name), QStringLiteral("Pin tool"));
    QCOMPARE(QAccessible::queryAccessibleInterface(toolRow.findChild<pdf::PDFStarButton*>())->text(QAccessible::Description), QStringLiteral("Merge"));
    pdf::PDFRecentFileRow fileRow(QStringLiteral("a.pdf"), QStringLiteral("C:\\Docs"), QStringLiteral("Today"));
    QCOMPARE(QAccessible::queryAccessibleInterface(fileRow.findChild<pdf::PDFStarButton*>())->text(QAccessible::Name), QStringLiteral("Pin file"));
    QCOMPARE(QAccessible::queryAccessibleInterface(fileRow.findChild<pdf::PDFStarButton*>())->text(QAccessible::Description), QStringLiteral("a.pdf"));

    pdf::PDFToolCategoryHeader header(QStringLiteral("Pages"), QStringLiteral("Merge, split"), QStringLiteral("6 tools"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    accessible = QAccessible::queryAccessibleInterface(&header);
    QCOMPARE(accessible->role(), QAccessible::StaticText);
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Merge, split, 6 tools"));

    pdf::PDFToolChoice choice(QStringLiteral("Compress"), QStringLiteral("grid"), pdf::PDFAccentHue::Violet);
    accessible = QAccessible::queryAccessibleInterface(&choice);
    QCOMPARE(accessible->role(), QAccessible::RadioButton);
    QVERIFY(accessible->state().checkable);
    QVERIFY(!accessible->state().checked);
    choice.setSelected(true);
    QVERIFY(accessible->state().checked);

    pdf::PDFResourceCard resource(QStringLiteral("User Manual"), QStringLiteral("Detailed documentation"), QStringLiteral("book-open"), pdf::PDFAccentHue::Green);
    QCOMPARE(QAccessible::queryAccessibleInterface(&resource)->text(QAccessible::Description), QStringLiteral("Detailed documentation"));
    pdf::PDFHighlightCard highlight(QStringLiteral("One app"), QStringLiteral("All in one"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    accessible = QAccessible::queryAccessibleInterface(&highlight);
    QCOMPARE(accessible->role(), QAccessible::StaticText);
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("All in one"));
    if (QAccessibleActionInterface* actions = accessible->actionInterface())
    {
        QVERIFY(!actions->actionNames().contains(QAccessibleActionInterface::pressAction()));
    }

    pdf::PDFVideoCard video(QStringLiteral("Getting started"), QStringLiteral("A quick tour"), QStringLiteral("2:45"), { QStringLiteral("OCR") });
    QCOMPARE(QAccessible::queryAccessibleInterface(&video)->text(QAccessible::Description), QStringLiteral("2:45, A quick tour"));
    QCOMPARE(QAccessible::queryAccessibleInterface(video.getTagWidgets().front())->role(), QAccessible::Button);

    pdf::PDFReadingThemeCard reading(QStringLiteral("Night"), Color::ReadingNightPage, Color::ReadingNightText);
    QCOMPARE(QAccessible::queryAccessibleInterface(&reading)->role(), QAccessible::RadioButton);
    QCOMPARE(QAccessible::queryAccessibleInterface(&reading)->text(QAccessible::Name), QStringLiteral("Night"));

    // The card of a picture without a name is named by its picture
    pdf::PDFPreviewCard preview(pdf::PDFPreviewPainter::Preview::ModeHighContrast);
    accessible = QAccessible::queryAccessibleInterface(&preview);
    QCOMPARE(accessible->role(), QAccessible::RadioButton);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("High contrast"));
    QCOMPARE(preview.getContent().name, QStringLiteral("High contrast"));
    preview.setPreview(pdf::PDFPreviewPainter::Preview::LayoutTwoColumns);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Two columns"));
    preview.setName(QStringLiteral("Book"));
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Book"));

    // The picture of a locked document is a graphic named by its state
    pdf::PDFLockedDocumentPicture picture;
    accessible = QAccessible::queryAccessibleInterface(&picture);
    QCOMPARE(accessible->role(), QAccessible::Graphic);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Locked document"));
    picture.setState(pdf::PDFLockedDocumentPicture::State::Wrong);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Wrong password"));
    picture.setAccessibleName(QStringLiteral("Application name"));
    picture.setState(pdf::PDFLockedDocumentPicture::State::Locked);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Application name"));
}

void FluentComponentsTest::cardThemeAndDirection()
{
    // A change of the theme: the colors are taken at the time of drawing
    pdf::PDFQuickActionTile tile(QStringLiteral("Open PDF"), QStringLiteral("Browse"), QStringLiteral("folder-open"), pdf::PDFAccentHue::Blue);
    tile.resize(224, 88);
    const QColor light = pixel(renderWidget(&tile, 1.0), 100, 6);
    QVERIFY(isNear(light, token(Color::AccentBlueBg)));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    const QColor dark = pixel(renderWidget(&tile, 1.0), 100, 6);
    QVERIFY(isNear(dark, token(Color::AccentBlueBg)));
    QVERIFY(light != dark);
    pdf::PDFLockedDocumentPicture picture;
    picture.resize(160, 160);
    QVERIFY(isNear(pixel(renderWidget(&picture, 1.0), 80, 5), token(Color::AccentBlueBg)));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    // Right-to-left: the nested widgets at the leading edge
    tile.setStarVisible(true);
    tile.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(tile.getStarButton()->geometry(), QRect(16, 30, 28, 28));
    QVERIFY(tile.getComponentState().isRightToLeft());
    pdf::PDFFolderCard folder(QStringLiteral("Documents"), QStringLiteral("C:\\Users"), QStringLiteral("Last"));
    folder.resize(252, 84);
    folder.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(folder.getMenuButton()->geometry(), QRect(11, 34, 16, 16));
    pdf::PDFPinnedCard pinned(QStringLiteral("Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Page 1"), 0.5);
    pinned.resize(259, 100);
    pinned.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(pinned.getStarButton()->geometry(), QRect(9, 13, 24, 24));
    QCOMPARE(pinned.getMenuButton()->geometry(), QRect(13, 41, 16, 16));
    pdf::PDFVideoCard video(QStringLiteral("Getting started"), QStringLiteral("A tour"), QStringLiteral("2:45"), { QStringLiteral("OCR") });
    video.resize(272, video.heightForWidth(272));
    video.setLayoutDirection(Qt::RightToLeft);
    const QRect tagRect = video.getTagWidgets().front()->geometry();
    QCOMPARE(tagRect.right(), 272 - 13 - 1);

    // Painted right-to-left: the circle of the tile at the right edge, the progress from the right
    const QColor surface = token(Color::BgSurface);
    const QImage rtlTile = render([&](QPainter& painter) { Painter::paintQuickActionTile(&painter, QRectF(0, 0, 224, 88), CardSamples::tile(), state(QStyle::State_None, Qt::RightToLeft)); }, QSize(224, 88));
    QVERIFY(isNear(pixel(rtlTile, 224 - 20, 44), surface));
    QVERIFY(isNear(pixel(rtlTile, 20, 8), token(Color::AccentBlueBg)));
    Painter::PinnedCard pinnedCard = CardSamples::pinned();
    pinnedCard.progress = 0.25;
    const QImage rtlPinned = render([&](QPainter& painter) { Painter::paintPinnedCard(&painter, QRectF(0, 0, 259, 100), pinnedCard, state(QStyle::State_None, Qt::RightToLeft)); }, QSize(259, 100));
    QVERIFY(isNear(pixel(rtlPinned, 259 - 81 - 5, 85), token(Color::BgAccent)));
    QVERIFY(isNear(pixel(rtlPinned, 259 - 81 - 120, 85), token(Color::BorderDefault)));
}

void FluentComponentsTest::cardDefensive()
{
    // Null painter
    const Painter::State normal;
    Painter::paintProgressBar(nullptr, QRectF(0, 0, 100, 4), 0.5);
    Painter::paintQuickActionTile(nullptr, QRectF(0, 0, 224, 88), CardSamples::tile(), normal);
    Painter::paintFolderCard(nullptr, QRectF(0, 0, 252, 84), CardSamples::folder(), normal);
    Painter::paintPinnedCard(nullptr, QRectF(0, 0, 259, 100), CardSamples::pinned(), normal);
    Painter::paintToolCategoryHeader(nullptr, QRectF(0, 0, 393, 61), CardSamples::header(), normal);
    Painter::paintToolChoice(nullptr, QRectF(0, 0, 154, 48), CardSamples::choice(), normal);
    Painter::paintResourceCard(nullptr, QRectF(0, 0, 116, 124), CardSamples::resource(), normal);
    Painter::paintHighlightCard(nullptr, QRectF(0, 0, 259, 137), CardSamples::highlight(), normal);
    Painter::paintVideoCard(nullptr, QRectF(0, 0, 272, 258), CardSamples::video(), normal);
    Painter::paintReadingThemeCard(nullptr, QRectF(0, 0, 159, 86), CardSamples::reading(), normal);
    Painter::paintPreviewCard(nullptr, QRectF(0, 0, 125, 122), CardSamples::preview(), normal);
    pdf::PDFPreviewPainter::paintPreview(nullptr, QRectF(0, 0, 115, 89), pdf::PDFPreviewPainter::Preview::ModeNormal);
    pdf::PDFPreviewPainter::paintLockedDocument(nullptr, QRectF(0, 0, 160, 160), pdf::PDFPreviewPainter::LockedState::Wrong);
    pdf::PDFPreviewPainter::paintLogo(nullptr, QRectF(0, 0, 130, 130));

    const QStyle::State all = QStyle::State_Enabled | QStyle::State_MouseOver | QStyle::State_Sunken | QStyle::State_HasFocus | QStyle::State_On;
    auto paintAll = [&](QPainter& painter, const QRectF& rect)
    {
        const Painter::State full(all);
        Painter::paintProgressBar(&painter, rect, 0.5);
        Painter::QuickActionTile tile = CardSamples::tile();
        tile.starVisible = true;
        Painter::paintQuickActionTile(&painter, rect, tile, full);
        Painter::paintFolderCard(&painter, rect, CardSamples::folder(), full);
        Painter::paintPinnedCard(&painter, rect, CardSamples::pinned(), full);
        Painter::paintToolCategoryHeader(&painter, rect, CardSamples::header(), full);
        Painter::paintToolChoice(&painter, rect, CardSamples::choice(), full);
        Painter::paintResourceCard(&painter, rect, CardSamples::resource(), full);
        Painter::paintHighlightCard(&painter, rect, CardSamples::highlight(), full);
        Painter::paintVideoCard(&painter, rect, CardSamples::video(), full);
        Painter::paintReadingThemeCard(&painter, rect, CardSamples::reading(), full);
        Painter::paintPreviewCard(&painter, rect, CardSamples::preview(), full);
        for (int i = 0; i < 10; ++i)
        {
            pdf::PDFPreviewPainter::paintPreview(&painter, rect, static_cast<pdf::PDFPreviewPainter::Preview>(i));
        }
        pdf::PDFPreviewPainter::paintLockedDocument(&painter, rect, pdf::PDFPreviewPainter::LockedState::Wrong);
        pdf::PDFPreviewPainter::paintLogo(&painter, rect);
    };

    // Empty, negative and invalid rectangles draw nothing, the state of the painter is restored
    const QImage empty = render([](QPainter&) { }, QSize(40, 40));
    for (const QRectF& rect : { QRectF(), QRectF(10, 10, 0, 20), QRectF(10, 10, -20, -20), QRectF(10, 10, 20, -5), QRectF(10, 10, qQNaN(), 20), QRectF(qQNaN(), 10, 20, 20) })
    {
        const QImage image = render([&](QPainter& painter)
        {
            const QTransform transform = painter.transform();
            const QPen pen = painter.pen();
            paintAll(painter, rect);
            QCOMPARE(painter.transform(), transform);
            QCOMPARE(painter.hasClipping(), false);
            QCOMPARE(painter.pen(), pen);
        }, QSize(40, 40));
        QCOMPARE(image, empty);
    }

    // Small rectangles: nothing is drawn outside (the focus rings outside reach 2 px)
    for (const QSizeF& size : { QSizeF(3, 3), QSizeF(1, 40), QSizeF(12, 5), QSizeF(60, 8), QSizeF(40, 30) })
    {
        const QRectF rect(QPointF(10, 10), size);
        const QImage image = render([&](QPainter& painter)
        {
            paintAll(painter, rect);
            QCOMPARE(painter.hasClipping(), false);
        }, QSize(90, 70));
        const QRect outside = rect.toAlignedRect().adjusted(-3, -3, 3, 3);
        for (int y = 0; y < 70; ++y)
        {
            for (int x = 0; x < 90; ++x)
            {
                if (!outside.contains(x, y))
                {
                    QVERIFY2(image.pixelColor(x, y) == empty.pixelColor(0, 0), qPrintable(QStringLiteral("%1 %2 (%3 x %4)").arg(x).arg(y).arg(size.width()).arg(size.height())));
                }
            }
        }
    }

    // Empty texts, unknown icons, invalid values of the enumerations, null pixmaps
    const QImage image = render([&](QPainter& painter)
    {
        Painter::paintQuickActionTile(&painter, QRectF(0, 0, 224, 88), Painter::QuickActionTile{ QString(), QString(), QStringLiteral("no-such-icon"), static_cast<pdf::PDFAccentHue>(42), true, true, true }, normal);
        Painter::paintFolderCard(&painter, QRectF(0, 0, 252, 84), Painter::FolderCard{ QString(), QString(), QString(), true, QString(), static_cast<pdf::PDFAccentHue>(-3), true }, normal);
        Painter::paintPinnedCard(&painter, QRectF(0, 0, 259, 100), Painter::PinnedCard{ QString(), QString(), QString(), qQNaN(), QPixmap(), true, true }, normal);
        Painter::paintToolCategoryHeader(&painter, QRectF(0, 0, 393, 61), Painter::ToolCategoryHeader{ QString(), QString(), QString(), QString(), static_cast<pdf::PDFAccentHue>(99), qQNaN() }, normal);
        Painter::paintToolChoice(&painter, QRectF(0, 0, 154, 48), Painter::ToolChoice{ QString(), QStringLiteral("no-such-icon"), static_cast<pdf::PDFAccentHue>(8) }, normal);
        Painter::paintResourceCard(&painter, QRectF(0, 0, 116, 124), Painter::ResourceCard{ QString(), QString(), QString(), static_cast<pdf::PDFAccentHue>(8) }, normal);
        Painter::paintHighlightCard(&painter, QRectF(0, 0, 259, 137), Painter::HighlightCard{ QString(), QString(), QString(), static_cast<pdf::PDFAccentHue>(8) }, normal);
        Painter::paintVideoCard(&painter, QRectF(0, 0, 272, 258), Painter::VideoCard{ QString(), QString(), QString(), { QString(), QString() }, QPixmap(), true }, normal);
        Painter::ReadingThemeCard invalid;
        invalid.pageColor = static_cast<Color>(-7);
        invalid.textColor = static_cast<Color>(100000);
        Painter::paintReadingThemeCard(&painter, QRectF(0, 0, 159, 86), invalid, normal);
        Painter::paintPreviewCard(&painter, QRectF(0, 0, 125, 122), Painter::PreviewCard{ QString(), static_cast<pdf::PDFPreviewPainter::Preview>(77) }, normal);
        pdf::PDFPreviewPainter::paintLockedDocument(&painter, QRectF(0, 0, 160, 160), static_cast<pdf::PDFPreviewPainter::LockedState>(5));
    }, QSize(400, 260));
    QCOMPARE(countLeakPixels(image), 0);

    // Progress out of the range and NaN
    QCOMPARE(Painter::normalizedProgress(qQNaN()), 0.0);
    QCOMPARE(Painter::normalizedProgress(-2.0), 0.0);
    QCOMPARE(Painter::normalizedProgress(7.0), 1.0);
    QCOMPARE(Painter::normalizedProgress(std::numeric_limits<qreal>::infinity()), 1.0);
    QCOMPARE(Painter::normalizedProgress(-std::numeric_limits<qreal>::infinity()), 0.0);
    QCOMPARE(Painter::normalizedProgress(0.25), 0.25);
    QCOMPARE(Painter::normalizedFolderHue(pdf::PDFAccentHue::Red), pdf::PDFAccentHue::Neutral);
    QCOMPARE(Painter::normalizedFolderHue(pdf::PDFAccentHue::Violet), pdf::PDFAccentHue::Violet);
    QCOMPARE(Painter::normalizedResourceHue(pdf::PDFAccentHue::Blue), pdf::PDFAccentHue::Neutral);
    QCOMPARE(Painter::normalizedResourceHue(pdf::PDFAccentHue::Amber), pdf::PDFAccentHue::Amber);
    QCOMPARE(pdf::PDFPreviewPainter::normalized(static_cast<pdf::PDFPreviewPainter::Preview>(-1)), pdf::PDFPreviewPainter::Preview::ModeNormal);
    QCOMPARE(pdf::PDFPreviewPainter::normalized(static_cast<pdf::PDFPreviewPainter::LockedState>(3)), pdf::PDFPreviewPainter::LockedState::Locked);
    QCOMPARE(Painter::getVideoCardTags(Painter::VideoCard{ QString(), QString(), QString(), { QString(), QStringLiteral("A"), QStringLiteral("  ") }, QPixmap(), true }), QStringList{ QStringLiteral("A") });
    QCOMPARE(Painter::getResourceCardHeight(CardSamples::resource(), -100), Painter::getResourceCardHeight(CardSamples::resource(), 0));
    QCOMPARE(Painter::getVideoCardHeight(CardSamples::video(), std::numeric_limits<int>::min()), Painter::getVideoCardHeight(CardSamples::video(), 0));
    QVERIFY(Painter::getVideoCardTagRects(QRectF(), CardSamples::video(), normal).at(0).isEmpty());

    // Widgets: invalid values, NaN, empty contents, tiny sizes
    pdf::PDFQuickActionTile tile;
    tile.setHue(static_cast<pdf::PDFAccentHue>(77));
    QCOMPARE(tile.getHue(), pdf::PDFAccentHue::Neutral);
    pdf::PDFFolderCard folder;
    folder.setHue(pdf::PDFAccentHue::Amber);
    QCOMPARE(folder.getHue(), pdf::PDFAccentHue::Neutral);
    pdf::PDFPinnedCard pinned;
    pinned.setProgress(qQNaN());
    QCOMPARE(pinned.getProgress(), 0.0);
    pinned.setProgress(3.0);
    QCOMPARE(pinned.getProgress(), 1.0);
    pinned.setPixmap(QPixmap());
    pdf::PDFToolCategoryHeader header;
    header.setTopRadius(qQNaN());
    QCOMPARE(header.getTopRadius(), 0.0);
    header.setTopRadius(-5.0);
    QCOMPARE(header.getTopRadius(), 0.0);
    pdf::PDFToolChoice choice;
    choice.setHue(static_cast<pdf::PDFAccentHue>(-2));
    QCOMPARE(choice.getHue(), pdf::PDFAccentHue::Neutral);
    pdf::PDFResourceCard resource;
    resource.setHue(pdf::PDFAccentHue::Red);
    QCOMPARE(resource.getHue(), pdf::PDFAccentHue::Neutral);
    QCOMPARE(resource.heightForWidth(-10), resource.heightForWidth(0));
    pdf::PDFHighlightCard highlight;
    pdf::PDFVideoCard video;
    video.setTags({ QString(), QStringLiteral(" "), QString() });
    QCOMPARE(video.getTagWidgets().size(), 0);
    pdf::PDFReadingThemeCard reading;
    reading.setColors(static_cast<Color>(-1), static_cast<Color>(999999));
    QCOMPARE(reading.getPageColor(), Color::ReadingPaperPage);
    QCOMPARE(reading.getTextColor(), Color::ReadingPaperText);
    pdf::PDFPreviewCard preview;
    preview.setPreview(static_cast<pdf::PDFPreviewPainter::Preview>(55));
    QCOMPARE(preview.getPreview(), pdf::PDFPreviewPainter::Preview::ModeNormal);
    pdf::PDFLockedDocumentPicture picture;
    picture.setState(static_cast<pdf::PDFLockedDocumentPicture::State>(9));
    QCOMPARE(picture.getState(), pdf::PDFLockedDocumentPicture::State::Locked);

    for (QWidget* widget : std::initializer_list<QWidget*>{ &tile, &folder, &pinned, &header, &choice, &resource, &highlight, &video, &reading, &preview, &picture })
    {
        for (const QSize& size : { QSize(0, 0), QSize(1, 1), QSize(30, 10), QSize(500, 40) })
        {
            widget->resize(size);
            QCOMPARE(countLeakPixels(widget->grab().toImage()), 0);
        }
    }

    // Nested widgets deleted by the application are forgotten
    pdf::PDFQuickActionTile starTile;
    starTile.setStarVisible(true);
    delete starTile.getStarButton();
    QCOMPARE(starTile.getStarButton(), nullptr);
    QVERIFY(!starTile.isPinned());
    starTile.setPinned(true);
    starTile.setStarVisible(true);
    QCOMPARE(countLeakPixels(starTile.grab().toImage()), 0);
    pdf::PDFPinnedCard menuCard;
    delete menuCard.getMenuButton();
    QCOMPARE(menuCard.getMenu(), nullptr);
    menuCard.setMenu(nullptr);
    QContextMenuEvent contextEvent(QContextMenuEvent::Keyboard, QPoint(5, 5), QPoint(5, 5));
    QApplication::sendEvent(&menuCard, &contextEvent);
    pdf::PDFVideoCard tagCard(QStringLiteral("Video"), QString(), QString(), { QStringLiteral("OCR"), QStringLiteral("Basics") });
    delete tagCard.getTagWidgets().front();
    QCOMPARE(tagCard.getTagWidgets().size(), 1);
    tagCard.resize(272, 258);
    QCOMPARE(countLeakPixels(tagCard.grab().toImage()), 0);

    // A card deleted by the slot of its star or of its tag
    {
        QPointer<pdf::PDFPinnedCard> card = new pdf::PDFPinnedCard(QStringLiteral("a.pdf"), QString(), QString(), 0.0);
        card->show();
        QVERIFY(QTest::qWaitForWindowExposed(card.data()));
        connect(card.data(), &pdf::PDFPinnedCard::pinnedChanged, card.data(), [&card]() { delete card.data(); });
        QTest::mouseClick(card->getStarButton(), Qt::LeftButton);
        QVERIFY(!card);
    }
    {
        QPointer<pdf::PDFVideoCard> card = new pdf::PDFVideoCard(QStringLiteral("Video"), QString(), QString(), { QStringLiteral("OCR") });
        card->resize(272, 258);
        card->show();
        QVERIFY(QTest::qWaitForWindowExposed(card.data()));
        connect(card.data(), &pdf::PDFVideoCard::tagClicked, card.data(), [&card]() { card->deleteLater(); });
        QTest::mouseClick(card->getTagWidgets().front(), Qt::LeftButton);
        QTRY_VERIFY(!card);
    }
}

void FluentComponentsTest::previewPainter()
{
    using Preview = pdf::PDFPreviewPainter::Preview;

    for (const QString& theme : { QStringLiteral("light"), QStringLiteral("dark") })
    {
        QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));
        const QColor surface = token(Color::BgSurface);

        // Each picture draws inside of its rectangle, the pictures differ
        QList<QImage> images;
        for (int i = 0; i < 10; ++i)
        {
            const Preview preview = static_cast<Preview>(i);
            const QImage image = render([&](QPainter& painter) { pdf::PDFPreviewPainter::paintPreview(&painter, QRectF(10, 10, 115, 89), preview); }, QSize(135, 109));
            for (int y = 0; y < 109; ++y)
            {
                for (int x = 0; x < 135; ++x)
                {
                    if (!QRect(10, 10, 115, 89).contains(x, y))
                    {
                        QVERIFY2(isNear(image.pixelColor(x, y), surface, 0), qPrintable(QStringLiteral("%1: %2 %3").arg(i).arg(x).arg(y)));
                    }
                }
            }
            QVERIFY(isNear(pixel(image, 10 + 57, 10), token(Color::BorderStrong)));
            for (const QImage& other : images)
            {
                QVERIFY2(other != image, qPrintable(QString::number(i)));
            }
            images << image;
            QCOMPARE(countLeakPixels(image), 0);
        }

        // The colors of the modes and of the layouts
        auto renderPreview = [&](Preview preview, const QSize& size) { return render([&](QPainter& painter) { pdf::PDFPreviewPainter::paintPreview(&painter, QRectF(QPointF(0, 0), size), preview); }, size); };
        QImage image = renderPreview(Preview::ModeNormal, QSize(115, 89));
        QVERIFY(isNear(pixel(image, 5, 50), token(Color::SampleWhite)));
        QVERIFY(isNear(pixel(image, 30, 13), token(Color::SampleGrayDark)));
        QVERIFY(isNear(pixel(image, 30, 23), token(Color::SampleGrayMid)));
        QVERIFY(isNear(pixel(image, 30, 64), token(Color::SampleBlueSoft)));
        QVERIFY(isNear(pixel(image, 84, 64), token(Color::SampleOrangeSoft)));
        image = renderPreview(Preview::ModeInverted, QSize(115, 89));
        QVERIFY(isNear(pixel(image, 5, 50), token(Color::SampleBlack)));
        QVERIFY(isNear(pixel(image, 30, 64), token(Color::SampleOrange)));
        image = renderPreview(Preview::ModeBitonal, QSize(115, 89));
        QVERIFY(isNear(pixel(image, 30, 64), token(Color::SampleBlack)));
        QVERIFY(isNear(pixel(image, 84, 64), token(Color::SampleWhite)));
        QVERIFY(isNear(pixel(image, 84, 52), token(Color::SampleBlack)));
        image = renderPreview(Preview::ModeCustomColors, QSize(115, 89));
        QVERIFY(isNear(pixel(image, 5, 50), token(Color::ReadingAmberPage)));
        QVERIFY(isNear(pixel(image, 30, 64), token(Color::ReadingAmberText)));
        image = renderPreview(Preview::LayoutSinglePage, QSize(115, 89));
        QVERIFY(isNear(pixel(image, 10, 44), token(Color::BgTrack)));
        QVERIFY(isNear(pixel(image, 57, 60), token(Color::ReadingPaperPage)));
        QVERIFY(isNear(pixel(image, 37, 40), token(Color::SampleGrayLight)));
        image = renderPreview(Preview::LayoutTwoPages, QSize(115, 89));
        QVERIFY(isNear(pixel(image, 57, 44), token(Color::BgTrack)));
        QVERIFY(isNear(pixel(image, 40, 60), token(Color::ReadingPaperPage)));
        QVERIFY(isNear(pixel(image, 80, 60), token(Color::ReadingPaperPage)));

        // A wider picture: the mode fills the width, the layout is centered
        image = renderPreview(Preview::ModeNormal, QSize(150, 89));
        QVERIFY(isNear(pixel(image, 135, 23), token(Color::SampleGrayMid)));
        image = renderPreview(Preview::LayoutSinglePage, QSize(155, 89));
        QVERIFY(isNear(pixel(image, 77, 60), token(Color::ReadingPaperPage)));
        QVERIFY(isNear(pixel(image, 40, 60), token(Color::BgTrack)));

        // A smaller rectangle scales the picture down
        image = render([&](QPainter& painter) { pdf::PDFPreviewPainter::paintPreview(&painter, QRectF(0, 0, 58, 45), Preview::ModeNormal); }, QSize(70, 60));
        QVERIFY(isNear(pixel(image, 64, 30), surface));
        QVERIFY(isNear(pixel(image, 30, 52), surface));
        QVERIFY(isNear(pixel(image, 15, 32), token(Color::SampleBlueSoft), 24));

        // Locked document: blue and red
        const QImage locked = render([&](QPainter& painter) { pdf::PDFPreviewPainter::paintLockedDocument(&painter, QRectF(0, 0, 160, 160), pdf::PDFPreviewPainter::LockedState::Locked); }, QSize(160, 160));
        const QImage wrong = render([&](QPainter& painter) { pdf::PDFPreviewPainter::paintLockedDocument(&painter, QRectF(0, 0, 160, 160), pdf::PDFPreviewPainter::LockedState::Wrong); }, QSize(160, 160));
        QVERIFY(isNear(pixel(locked, 80, 5), token(Color::AccentBlueBg)));
        QVERIFY(isNear(pixel(wrong, 80, 5), token(Color::AccentRedBg)));
        QVERIFY(isNear(pixel(locked, 104, 120), token(Color::BgAccent)));
        QVERIFY(isNear(pixel(wrong, 104, 120), token(Color::IconDanger)));
        QVERIFY(isNear(pixel(locked, 14, 48), token(Color::AccentBlueBorderStrong)));
        QVERIFY(isNear(pixel(locked, 88, 116), surface, 12));
        QVERIFY(isNear(pixel(locked, 60, 42), token(Color::AccentBlueBorderStrong)));
        QVERIFY(locked != wrong);
        QCOMPARE(countLeakPixels(locked), 0);

        // A larger rectangle keeps the illustration centered at its size
        const QImage centered = render([&](QPainter& painter) { pdf::PDFPreviewPainter::paintLockedDocument(&painter, QRectF(0, 0, 200, 180), pdf::PDFPreviewPainter::LockedState::Locked); }, QSize(200, 180));
        QVERIFY(isNear(pixel(centered, 100, 15), token(Color::AccentBlueBg)));
        QVERIFY(isNear(pixel(centered, 10, 90), surface));
    }

    // The logo: the SVG of the resources, red and green of the brand
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QVERIFY(QFile::exists(pdf::PDFPreviewPainter::getLogoResourcePath()));
    const QImage logo = render([](QPainter& painter) { pdf::PDFPreviewPainter::paintLogo(&painter, QRectF(0, 0, 130, 130)); }, QSize(130, 130));
    QVERIFY(isNear(pixel(logo, 115, 90), token(Color::BrandLogoRed), 8));
    QVERIFY(isNear(pixel(logo, 120, 35), token(Color::BrandLogoGreen), 8));
    QVERIFY(isNear(pixel(logo, 30, 30), token(Color::BgSurface)));
    const QImage wideLogo = render([](QPainter& painter) { pdf::PDFPreviewPainter::paintLogo(&painter, QRectF(0, 0, 260, 130)); }, QSize(260, 130));
    QVERIFY(isNear(pixel(wideLogo, 20, 65), token(Color::BgSurface)));
    QVERIFY(isNear(pixel(wideLogo, 65 + 115, 90), token(Color::BrandLogoRed), 8));
}

void FluentComponentsTest::cardMenuDeletion()
{
    // A slot of menuRequested deletes the card (or the row) in the handler of a
    // context menu event: the event is accepted, it does not go on to the
    // parent through the deleted widget (QApplication::notify, review C1)
    QMenu menu;
    menu.addAction(QStringLiteral("Open"));

    using Factory = std::function<QAbstractButton*(QWidget*)>;
    const QList<QPair<QString, Factory>> factories =
    {
        { QStringLiteral("Folder card"), [&menu](QWidget* parent) { pdf::PDFFolderCard* card = new pdf::PDFFolderCard(QStringLiteral("Docs"), QStringLiteral("C:\\Docs"), QString(), pdf::PDFAccentHue::Blue, parent);
                                                                    card->setMenu(&menu);
                                                                    connect(card, &pdf::PDFFolderCard::menuRequested, card, [card]() { delete card; });
                                                                    return static_cast<QAbstractButton*>(card); } },
        { QStringLiteral("Pinned card"), [&menu](QWidget* parent) { pdf::PDFPinnedCard* card = new pdf::PDFPinnedCard(QStringLiteral("a.pdf"), QString(), QString(), 0.5, parent);
                                                                    card->setMenu(&menu);
                                                                    connect(card, &pdf::PDFPinnedCard::menuRequested, card, [card]() { delete card; });
                                                                    return static_cast<QAbstractButton*>(card); } },
        { QStringLiteral("Recent file row"), [&menu](QWidget* parent) { pdf::PDFRecentFileRow* row = new pdf::PDFRecentFileRow(QStringLiteral("a.pdf"), QString(), QString(), parent);
                                                                        row->setMenu(&menu);
                                                                        connect(row, &pdf::PDFRecentFileRow::menuRequested, row, [row]() { delete row; });
                                                                        return static_cast<QAbstractButton*>(row); } },
        { QStringLiteral("Workspace row"), [&menu](QWidget* parent) { pdf::PDFWorkspaceRow* row = new pdf::PDFWorkspaceRow(QStringLiteral("Work"), QString(), parent);
                                                                      row->setMenu(&menu);
                                                                      connect(row, &pdf::PDFWorkspaceRow::menuRequested, row, [row]() { delete row; });
                                                                      return static_cast<QAbstractButton*>(row); } }
    };

    for (const auto& factory : factories)
    {
        for (const QContextMenuEvent::Reason reason : { QContextMenuEvent::Mouse, QContextMenuEvent::Keyboard })
        {
            QWidget window;
            QVBoxLayout* layout = new QVBoxLayout(&window);
            QPointer<QAbstractButton> button = factory.second(&window);
            layout->addWidget(button.data());
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));

            QContextMenuEvent event(reason, QPoint(20, 20), button->mapToGlobal(QPoint(20, 20)));
            QApplication::sendEvent(button.data(), &event);
            QVERIFY2(!button, qPrintable(factory.first));
            QVERIFY2(event.isAccepted(), qPrintable(factory.first));
            QVERIFY(!menu.isVisible());
        }
    }

    // Without the menu button the event goes to the parent (ignored)
    pdf::PDFFolderCard card(QStringLiteral("Docs"), QString(), QString());
    delete card.getMenuButton();
    QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(5, 5), QPoint(5, 5));
    QApplication::sendEvent(&card, &event);
    QVERIFY(!event.isAccepted());
}

void FluentComponentsTest::cardTags()
{
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    QPushButton* before = new QPushButton(QStringLiteral("Before"), &window);
    pdf::PDFVideoCard* card = new pdf::PDFVideoCard(QStringLiteral("Getting started"), QStringLiteral("A tour"), QStringLiteral("2:45"), { QStringLiteral("OCR") }, &window);
    QPushButton* after = new QPushButton(QStringLiteral("After"), &window);
    layout->addWidget(before);
    layout->addWidget(card);
    layout->addWidget(after);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    // Tags of a later setTags follow the card in the order of the focus (review C2)
    card->setTags({ QStringLiteral("Forms"), QStringLiteral("Review") });
    QApplication::processEvents();
    const QList<pdf::PDFTag*> tags = card->getTagWidgets();
    QCOMPARE(tags.size(), 2);
    card->setFocus(Qt::TabFocusReason);
    QVERIFY(card->hasFocus());
    QTest::keyClick(card, Qt::Key_Tab);
    QTRY_VERIFY(tags.at(0)->hasFocus());
    QTest::keyClick(tags.at(0), Qt::Key_Tab);
    QTRY_VERIFY(tags.at(1)->hasFocus());
    QTest::keyClick(tags.at(1), Qt::Key_Tab);
    QTRY_VERIFY(after->hasFocus());

    // Empty and blank tags are not stored (the indices of getTags are the indices of the widgets, review C12)
    card->setTags({ QString(), QStringLiteral("Forms"), QStringLiteral("  "), QStringLiteral("Review"), QStringLiteral("Signatures") });
    QCOMPARE(card->getTags(), (QStringList{ QStringLiteral("Forms"), QStringLiteral("Review"), QStringLiteral("Signatures") }));
    QCOMPARE(card->getTagWidgets().size(), 3);
    for (int i = 0; i < 3; ++i)
    {
        QCOMPARE(card->getTagWidgets().at(i)->text(), card->getTags().at(i));
    }

    // A tag hidden by the application stays hidden and takes no place (review C13)
    layout->removeWidget(card);
    card->setGeometry(0, 0, 272, card->heightForWidth(272));
    QApplication::processEvents();
    QList<pdf::PDFTag*> widgets = card->getTagWidgets();
    const QPoint secondPosition = widgets.at(1)->pos();
    widgets.at(0)->hide();
    QApplication::processEvents();
    card->resize(271, card->height());
    card->resize(272, card->height());
    QApplication::processEvents();
    QVERIFY(widgets.at(0)->isHidden());
    QCOMPARE(widgets.at(1)->pos().x(), 13);
    QCOMPARE(card->getContent().tags, (QStringList{ QStringLiteral("Review"), QStringLiteral("Signatures") }));
    widgets.at(0)->show();
    QApplication::processEvents();
    QTRY_COMPARE(widgets.at(1)->pos(), secondPosition);
    QVERIFY(!widgets.at(0)->isHidden());

    // A deleted tag leaves its place to the others, the same tags recreate it
    delete widgets.at(0);
    QApplication::processEvents();
    QCOMPARE(card->getTagWidgets().size(), 2);
    QCOMPARE(card->getTagWidgets().at(0)->pos().x(), 13);
    card->setTags({ QStringLiteral("Forms"), QStringLiteral("Review"), QStringLiteral("Signatures") });
    QApplication::processEvents();
    QCOMPARE(card->getTagWidgets().size(), 3);
    QCOMPARE(card->getTagWidgets().at(0)->text(), QStringLiteral("Forms"));

    // Tags, which do not fit, are hidden by the card and shown again, when they fit
    card->setTags({ QStringLiteral("Signatures"), QStringLiteral("Security"), QStringLiteral("Certificates"), QStringLiteral("Advanced") });
    QApplication::processEvents();
    widgets = card->getTagWidgets();
    QVERIFY(widgets.back()->isHidden());
    card->resize(600, card->heightForWidth(600));
    QApplication::processEvents();
    QVERIFY(!widgets.back()->isHidden());
    card->resize(272, card->heightForWidth(272));
    QApplication::processEvents();
    QVERIFY(widgets.back()->isHidden());

    // The title moves the tags also on a card of a fixed size (review C3)
    pdf::PDFVideoCard fixed(QString(), QStringLiteral("A quick tour of the interface and the main features."), QStringLiteral("2:45"), { QStringLiteral("OCR") });
    fixed.setFixedSize(272, 258);
    fixed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fixed));
    const int topWithoutTitle = fixed.getTagWidgets().front()->y();
    fixed.setTitle(QStringLiteral("Getting started"));
    QCOMPARE(fixed.getTagWidgets().front()->y(), topWithoutTitle + 16 + 3);
    QCOMPARE(fixed.getTagWidgets().front()->geometry(), Painter::getVideoCardTagRects(QRectF(fixed.rect()), fixed.getContent(), fixed.getComponentState()).front().toAlignedRect());
    static_cast<QAbstractButton&>(fixed).setText(QString());
    fixed.repaint();
    QCOMPARE(fixed.getTagWidgets().front()->y(), topWithoutTitle);
}

void FluentComponentsTest::cardHeightForWidth()
{
    // Each change of the content of a card, whose height follows its width,
    // gives the height of the painter (the cached height is invalidated, review C10 / C11 / C24)
    pdf::PDFResourceCard resource(QStringLiteral("User Manual"), QStringLiteral("Docs"), QStringLiteral("book-open"), pdf::PDFAccentHue::Neutral);
    pdf::PDFHighlightCard highlight(QStringLiteral("One app"), QStringLiteral("All in one"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    pdf::PDFVideoCard video(QStringLiteral("Getting started"), QStringLiteral("A tour"), QStringLiteral("2:45"), {});
    pdf::PDFVersionRow version(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14"), QStringLiteral("Short"));

    auto check = [&](int width)
    {
        QCOMPARE(resource.heightForWidth(width), Painter::getResourceCardHeight(resource.getContent(), width));
        QCOMPARE(highlight.heightForWidth(width), Painter::getHighlightCardHeight(highlight.getContent(), width));
        QCOMPARE(video.heightForWidth(width), Painter::getVideoCardHeight(video.getContent(), width));
        QCOMPARE(version.heightForWidth(width), Painter::getVersionRowHeight(version.getContent(), width));
    };

    for (const int width : { 116, 259, 272, 324 })
    {
        check(width);
    }

    const QString longText = QStringLiteral("A much longer description of the card, which wraps into several lines of the text");
    const int resourceHeight = resource.heightForWidth(116);
    resource.setDescription(longText);
    QVERIFY(resource.heightForWidth(116) > resourceHeight);
    const int highlightHeight = highlight.heightForWidth(259);
    highlight.setDescription(longText);
    QVERIFY(highlight.heightForWidth(259) > highlightHeight);
    const int videoHeight = video.heightForWidth(272);
    video.setDescription(longText);
    QVERIFY(video.heightForWidth(272) > videoHeight);
    const int videoTagsHeight = video.heightForWidth(272);
    video.setTags({ QStringLiteral("OCR") });
    QCOMPARE(video.heightForWidth(272), videoTagsHeight + 21 + 10);
    const int versionHeight = version.heightForWidth(324);
    version.setSummary(longText + longText);
    QVERIFY(version.heightForWidth(324) > versionHeight);
    for (const int width : { 116, 259, 272, 324 })
    {
        check(width);
    }

    // A title set through QAbstractButton and a change of the theme
    static_cast<QAbstractButton&>(highlight).setText(longText);
    check(259);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    check(272);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    check(272);

    // The minimal height is the height of the fixed parts and of one line of each
    // text, not the height of texts wrapped at the minimal width (review C4)
    pdf::PDFResourceCard wrapping(QStringLiteral("Video Tutorials"), QStringLiteral("Watch step-by-step guides"), QStringLiteral("play-circle"), pdf::PDFAccentHue::Violet);
    QCOMPARE(wrapping.minimumSizeHint().height(), 16 + 32 + 12 + 16 + 4 + 14 + 16);
    QVERIFY(wrapping.heightForWidth(wrapping.minimumSizeHint().width()) > wrapping.minimumSizeHint().height());
    pdf::PDFHighlightCard wrappingHighlight(QStringLiteral("Faster loading"), longText, QStringLiteral("zap"), pdf::PDFAccentHue::Amber);
    QCOMPARE(wrappingHighlight.minimumSizeHint().height(), 2 * 17 + 40 + 10 + 17 + 4 + 16);
    QCOMPARE(Painter::getResourceCardMinimumSize(Painter::ResourceCard{ QString(), QString(), QString(), pdf::PDFAccentHue::Neutral }).height(), 16 + 32 + 12 + 16);
    QCOMPARE(Painter::getHighlightCardMinimumSize(Painter::HighlightCard{ QString(), longText, QString(), pdf::PDFAccentHue::Blue }).height(), 2 * 17 + 40 + 10 + 16);

    // A row of resource cards in a layout without the height for width (a box
    // layout makes its hint from the minimum of its items)
    QWidget row;
    QHBoxLayout* layout = new QHBoxLayout(&row);
    for (int i = 0; i < 3; ++i)
    {
        layout->addWidget(new pdf::PDFResourceCard(QStringLiteral("Video Tutorials"), QStringLiteral("Watch step-by-step guides"), QStringLiteral("play-circle"), pdf::PDFAccentHue::Violet, &row));
    }
    QVERIFY(row.minimumSizeHint().height() < 150);
}

void FluentComponentsTest::cardStates()
{
    const QColor surface = token(Color::BgSurface);
    const QStyle::State disabledOn = QStyle::State_On;

    // A disabled pinned card: the value of its bar border/strong (review C5)
    Painter::PinnedCard pinned = CardSamples::pinned();
    const QImage disabledPinned = render([&](QPainter& painter) { Painter::paintPinnedCard(&painter, QRectF(4, 4, 259, 100), pinned, Painter::State(QStyle::State_None)); }, QSize(267, 108));
    QVERIFY(isNear(pixel(disabledPinned, 95, 89), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(disabledPinned, 205, 89), token(Color::BorderDefault)));
    pinned.progress = 1.0;
    QVERIFY(isNear(pixel(render([&](QPainter& painter) { Painter::paintPinnedCard(&painter, QRectF(4, 4, 259, 100), pinned, Painter::State(QStyle::State_None)); }, QSize(267, 108)), 205, 89), token(Color::BorderStrong)));
    QImage bar = render([](QPainter& painter) { Painter::paintProgressBar(&painter, QRectF(0, 0, 100, 4), 0.5, Painter::State(QStyle::State_None)); }, QSize(100, 4));
    QVERIFY(isNear(pixel(bar, 20, 2), token(Color::BorderStrong)));
    bar = render([](QPainter& painter) { Painter::paintProgressBar(&painter, QRectF(0, 0, 100, 4), 0.0); }, QSize(100, 4));
    QCOMPARE(countPixels(bar, QRect(0, 0, 100, 4), token(Color::BgAccent), 8), 0);
    pdf::PDFPinnedCard pinnedCard(QStringLiteral("Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Page 1"), 0.5);
    pinnedCard.resize(259, 100);
    pinnedCard.setEnabled(false);
    QVERIFY(isNear(pixel(renderWidget(&pinnedCard, 1.0), 91, 85), token(Color::BorderStrong)));

    // Disabled choice cards: the texts text/disabled, the selection ring border/strong (review O-09)
    const QImage choice = render([&](QPainter& painter) { Painter::paintToolChoice(&painter, QRectF(4, 4, 154, 48), CardSamples::choice(), Painter::State(disabledOn)); }, QSize(162, 56));
    QVERIFY(isNear(pixel(choice, 81, 4), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(choice, 81, 5), token(Color::BorderStrong)));
    QCOMPARE(countPixels(choice, QRect(52, 14, 70, 30), token(Color::TextPrimary), 8), 0);
    QVERIFY(countPixels(choice, QRect(52, 14, 70, 30), token(Color::TextDisabled), 16) > 0);
    const QImage reading = render([&](QPainter& painter) { Painter::paintReadingThemeCard(&painter, QRectF(4, 4, 159, 86), CardSamples::reading(), Painter::State(disabledOn)); }, QSize(167, 94));
    QVERIFY(isNear(pixel(reading, 84, 4), token(Color::BorderStrong)));
    QCOMPARE(countPixels(reading, QRect(12, 68, 120, 15), token(Color::TextAccent), 16), 0);
    QVERIFY(countPixels(reading, QRect(12, 68, 120, 15), token(Color::TextDisabled), 16) > 0);
    const QImage previewCard = render([&](QPainter& painter) { Painter::paintPreviewCard(&painter, QRectF(4, 4, 125, 122), CardSamples::preview(), Painter::State(disabledOn)); }, QSize(133, 130));
    QVERIFY(isNear(pixel(previewCard, 60, 4), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(previewCard, 12, 50), token(Color::SampleWhite)));

    // Right-to-left: the badge of the tool choice and the letters of the reading
    // theme at the right edge, the check of the color mode card at the left edge
    const QImage rtlChoice = render([&](QPainter& painter) { Painter::paintToolChoice(&painter, QRectF(0, 0, 154, 48), CardSamples::choice(), state(QStyle::State_On, Qt::RightToLeft)); }, QSize(154, 48));
    QVERIFY(isNear(pixel(rtlChoice, 154 - 11 - 14, 24 - 10), token(Color::AccentBlueBg), 8));
    QVERIFY(isNear(pixel(rtlChoice, 11 + 14, 24 - 10), blend(token(Color::BgAccentSubtle), surface), 8));
    QVERIFY(countPixels(rtlChoice, QRect(11, 16, 16, 16), token(Color::IconAccent), 40) > 0);
    Painter::ReadingThemeCard night = CardSamples::reading();
    night.pageColor = Color::ReadingNightPage;
    night.textColor = Color::ReadingNightText;
    const QImage rtlReading = render([&](QPainter& painter) { Painter::paintReadingThemeCard(&painter, QRectF(0, 0, 159, 86), night, state(QStyle::State_On, Qt::RightToLeft)); }, QSize(159, 86));
    QVERIFY(colorDistance(strongestInk(rtlReading, QRect(159 - 18 - 30, 18, 30, 26), token(Color::ReadingNightPage)), token(Color::ReadingNightPage)) > 100);
    QVERIFY(countPixels(rtlReading, QRect(10, 64, 16, 15), token(Color::IconAccent), 40) > 0);
    const QImage rtlPreview = render([&](QPainter& painter) { Painter::paintPreviewCard(&painter, QRectF(0, 0, 125, 122), CardSamples::preview(), state(QStyle::State_On, Qt::RightToLeft)); }, QSize(125, 122));
    QVERIFY(countPixels(rtlPreview, QRect(10, 100, 16, 15), token(Color::IconAccent), 40) > 0);
    QVERIFY(countPixels(rtlPreview, QRect(75, 100, 40, 15), token(Color::TextAccent), 16) > 0);
    QCOMPARE(countLeakPixels(rtlPreview), 0);
}

void FluentComponentsTest::cardDetails()
{
    // The ellipsis follows the last word without a space (review F10)
    const QFont font = pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Tiny);
    const QString full = QStringLiteral("jumps over the dog");
    bool hasElided = false;
    for (qreal width = 20.0; width < pdf::PDFStylePainter::textWidth(full, font); width += 1.0)
    {
        const QString elided = Painter::elidedText(full, font, width);
        QVERIFY2(!elided.contains(QStringLiteral(" \u2026")), qPrintable(elided));
        QVERIFY(elided.isEmpty() || QFontMetricsF(font).horizontalAdvance(elided) <= width + 0.5);
        hasElided = hasElided || elided == QStringLiteral("jumps over the\u2026");
    }
    QVERIFY(hasElided);
    QCOMPARE(Painter::elidedText(full, font, 10000.0), full);
    QVERIFY(!Painter::elidedText(QStringLiteral("Annual Report 2026.pdf"), font, 60.0, Qt::ElideMiddle).contains(QStringLiteral(" \u2026")));

    // The selected property notifies its change (review C17)
    for (const QMetaObject* metaObject : { &pdf::PDFToolChoice::staticMetaObject, &pdf::PDFReadingThemeCard::staticMetaObject, &pdf::PDFPreviewCard::staticMetaObject, &pdf::PDFColorSwatch::staticMetaObject })
    {
        const QMetaProperty property = metaObject->property(metaObject->indexOfProperty("selected"));
        QVERIFY(property.hasNotifySignal());
        QCOMPARE(property.notifySignal().name(), QByteArray("toggled"));
    }
    pdf::PDFToolChoice choice(QStringLiteral("Compress"), QStringLiteral("grid"), pdf::PDFAccentHue::Violet);
    QSignalSpy toggled(&choice, &QAbstractButton::toggled);
    choice.setProperty("selected", true);
    QCOMPARE(toggled.count(), 1);

    // Labels are not checkable and do not react to the pointer (review C18)
    pdf::PDFHighlightCard highlight(QStringLiteral("One app"), QStringLiteral("All in one"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    pdf::PDFToolCategoryHeader header(QStringLiteral("Pages"), QStringLiteral("Merge"), QStringLiteral("6 tools"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    pdf::PDFVersionRow version(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14"), QStringLiteral("Summary"));
    version.setClickable(false);
    for (pdf::PDFComponentButton* label : std::initializer_list<pdf::PDFComponentButton*>{ &highlight, &header, &version })
    {
        QVERIFY(!label->testAttribute(Qt::WA_Hover));
        label->setSelectionBehavior(pdf::PDFComponentButton::SelectionBehavior::Exclusive);
        QCOMPARE(label->getSelectionBehavior(), pdf::PDFComponentButton::SelectionBehavior::None);
        QVERIFY(!label->isCheckable());
        QCOMPARE(QAccessible::queryAccessibleInterface(label)->role(), QAccessible::StaticText);
    }

    // The star: its subject chooses the texts, an invalid value is a file
    pdf::PDFStarButton star;
    QCOMPARE(star.getSubject(), pdf::PDFStarButton::Subject::File);
    QCOMPARE(star.getStateText(), QStringLiteral("Pin file"));
    star.setSubject(pdf::PDFStarButton::Subject::Tool);
    QCOMPARE(star.toolTip(), QStringLiteral("Pin tool"));
    star.setSubject(static_cast<pdf::PDFStarButton::Subject>(42));
    QCOMPARE(star.getSubject(), pdf::PDFStarButton::Subject::File);
    QCOMPARE(star.toolTip(), QStringLiteral("Pin file"));
    QVERIFY(QAccessible::queryAccessibleInterface(&star)->text(QAccessible::Description).isEmpty());

    // A change of the language translates the composed descriptions and the
    // name of a picture (review C15)
    pdf::PDFFolderCard folder(QStringLiteral("Docs"), QStringLiteral("C:\\Docs"), QStringLiteral("Last used"));
    pdf::PDFVideoCard video(QStringLiteral("Video"), QStringLiteral("A tour"), QStringLiteral("2:45"), {});
    pdf::PDFPreviewCard preview(pdf::PDFPreviewPainter::Preview::ModeHighContrast);
    QAccessibleInterface* folderAccessible = QAccessible::queryAccessibleInterface(&folder);
    QAccessibleInterface* videoAccessible = QAccessible::queryAccessibleInterface(&video);
    QAccessibleInterface* previewAccessible = QAccessible::queryAccessibleInterface(&preview);
    QCOMPARE(folderAccessible->text(QAccessible::Description), QStringLiteral("C:\\Docs, Last used"));
    s_accessibleEvents.clear();
    QAccessible::installUpdateHandler(&FluentComponentsTest::collectAccessibleEvent);
    CardTestTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    QTRY_COMPARE(folderAccessible->text(QAccessible::Description), QStringLiteral("C:\\Docs; Last used"));
    QTRY_COMPARE(videoAccessible->text(QAccessible::Description), QStringLiteral("2:45; A tour"));
    QCOMPARE(previewAccessible->text(QAccessible::Name), QStringLiteral("Strong contrast"));
    QTRY_VERIFY(countAccessibleEvents(&preview, QAccessible::NameChanged) > 0);
    QVERIFY(QCoreApplication::removeTranslator(&translator));
    QTRY_COMPARE(folderAccessible->text(QAccessible::Description), QStringLiteral("C:\\Docs, Last used"));
    QAccessible::installUpdateHandler(nullptr);
}

void FluentComponentsTest::scaledPictures()
{
    // A large picture is scaled smoothly to the pixels of the device: a pattern
    // of single pixels becomes its average, not an aliased pattern (review C8)
    QImage pattern(800, 800, QImage::Format_RGB32);
    for (int y = 0; y < pattern.height(); ++y)
    {
        for (int x = 0; x < pattern.width(); ++x)
        {
            pattern.setPixel(x, y, ((x + y) % 2) ? qRgb(0, 0, 0) : qRgb(255, 255, 255));
        }
    }
    const QPixmap pixmap = QPixmap::fromImage(pattern);
    for (const qreal scale : { 1.0, 2.0 })
    {
        const QImage first = render([&](QPainter& painter) { Painter::drawScaledPixmap(&painter, QRectF(0, 0, 100, 100), pixmap); }, QSize(100, 100), scale);
        const QImage second = render([&](QPainter& painter) { Painter::drawScaledPixmap(&painter, QRectF(0, 0, 100, 100), pixmap); }, QSize(100, 100), scale);
        QCOMPARE(first, second);
        for (const QPoint& point : { QPoint(10, 10), QPoint(50, 37), QPoint(91, 80) })
        {
            const QColor color = pixel(first, point.x(), point.y());
            QVERIFY2(qAbs(color.red() - 128) < 24, qPrintable(color.name()));
        }
    }

    // Defensive cases: nothing is drawn
    const QImage empty = render([](QPainter&) { }, QSize(20, 20));
    QCOMPARE(render([&](QPainter& painter)
    {
        Painter::drawScaledPixmap(nullptr, QRectF(0, 0, 10, 10), pixmap);
        Painter::drawScaledPixmap(&painter, QRectF(0, 0, 10, 10), QPixmap());
        Painter::drawScaledPixmap(&painter, QRectF(0, 0, 0, 10), pixmap);
        Painter::drawScaledPixmap(&painter, QRectF(0, 0, qQNaN(), 10), pixmap);
        Painter::drawScaledPixmap(&painter, QRectF(0, 0, std::numeric_limits<qreal>::infinity(), 10), pixmap);
    }, QSize(20, 20)), empty);

    // The video card and the pinned card draw the large picture through it
    QPixmapCache::clear();
    Painter::VideoCard video = CardSamples::video();
    video.pixmap = pixmap;
    const QImage card = render([&](QPainter& painter) { Painter::paintVideoCard(&painter, QRectF(0, 0, 272, 258), video, state(QStyle::State_None)); }, QSize(272, 258));
    QVERIFY(qAbs(pixel(card, 30, 30).red() - 128) < 24);

    // The logo: the same picture from the cache, from a scaled painter and from
    // another thread (review C7)
    const QImage logo = render([](QPainter& painter) { pdf::PDFPreviewPainter::paintLogo(&painter, QRectF(0, 0, 130, 130)); }, QSize(130, 130));
    QCOMPARE(render([](QPainter& painter) { pdf::PDFPreviewPainter::paintLogo(&painter, QRectF(0, 0, 130, 130)); }, QSize(130, 130)), logo);
    const QImage scaledLogo = render([](QPainter& painter) { painter.scale(2.0, 2.0); pdf::PDFPreviewPainter::paintLogo(&painter, QRectF(0, 0, 65, 65)); }, QSize(130, 130));
    QVERIFY(describeDifference(scaledLogo, logo, 24).isEmpty() || colorDistance(pixel(scaledLogo, 115, 90), token(Color::BrandLogoRed)) < 24);
    QVERIFY(isNear(pixel(scaledLogo, 115, 90), token(Color::BrandLogoRed), 8));
    QImage threadLogo(130, 130, QImage::Format_ARGB32_Premultiplied);
    const QColor surface = token(Color::BgSurface);
    std::thread worker([&threadLogo, surface]()
    {
        threadLogo.fill(surface);
        QPainter painter(&threadLogo);
        painter.setRenderHint(QPainter::Antialiasing, true);
        pdf::PDFPreviewPainter::paintLogo(&painter, QRectF(0, 0, 130, 130));
    });
    worker.join();
    QVERIFY(isNear(pixel(threadLogo, 115, 90), token(Color::BrandLogoRed), 8));
    const QImage rotatedLogo = render([](QPainter& painter) { painter.translate(65, 65); painter.rotate(90.0); painter.translate(-65, -65); pdf::PDFPreviewPainter::paintLogo(&painter, QRectF(0, 0, 130, 130)); }, QSize(130, 130));
    QCOMPARE(countLeakPixels(rotatedLogo), 0);
    QVERIFY(rotatedLogo != logo);

    // Infinite and huge rectangles: nothing is drawn or the drawing stays clipped (review C24)
    const QImage background = render([](QPainter&) { }, QSize(40, 40));
    const qreal infinity = std::numeric_limits<qreal>::infinity();
    QCOMPARE(render([&](QPainter& painter)
    {
        for (const QRectF& rect : { QRectF(0, 0, infinity, 40), QRectF(-infinity, 0, 40, 40), QRectF(0, 0, 40, -infinity) })
        {
            pdf::PDFPreviewPainter::paintPreview(&painter, rect, pdf::PDFPreviewPainter::Preview::ModeNormal);
            pdf::PDFPreviewPainter::paintLockedDocument(&painter, rect, pdf::PDFPreviewPainter::LockedState::Locked, true);
            pdf::PDFPreviewPainter::paintLogo(&painter, rect);
        }
    }, QSize(40, 40)), background);
    const QImage huge = render([&](QPainter& painter)
    {
        const QRectF rect(-1.0e7, -1.0e7, 2.0e7, 2.0e7);
        pdf::PDFPreviewPainter::paintPreview(&painter, rect, pdf::PDFPreviewPainter::Preview::LayoutTwoColumns);
        pdf::PDFPreviewPainter::paintLockedDocument(&painter, rect, pdf::PDFPreviewPainter::LockedState::Wrong);
        pdf::PDFPreviewPainter::paintLogo(&painter, rect);
        QCOMPARE(painter.hasClipping(), false);
    }, QSize(40, 40));
    QCOMPARE(countLeakPixels(huge), 0);

    // The shadow of the badge of the locked document reaches beyond its 160 x
    // 160, as in the design; the widget draws it into its parent (review C19)
    QCOMPARE(pdf::PDFPreviewPainter::getLockedDocumentOverflowMargins(), QMargins(0, 0, 6, 10));
    const QImage clipped = render([](QPainter& painter) { pdf::PDFPreviewPainter::paintLockedDocument(&painter, QRectF(10, 10, 160, 160), pdf::PDFPreviewPainter::LockedState::Locked); }, QSize(190, 190));
    const QImage overflow = render([](QPainter& painter) { pdf::PDFPreviewPainter::paintLockedDocument(&painter, QRectF(10, 10, 160, 160), pdf::PDFPreviewPainter::LockedState::Locked, true); }, QSize(190, 190));
    QVERIFY(isNear(pixel(clipped, 10 + 161, 10 + 125), surface, 0));
    QVERIFY(colorDistance(pixel(overflow, 10 + 161, 10 + 125), surface) > 3);
    QVERIFY(colorDistance(pixel(overflow, 10 + 125, 10 + 162), surface) > 3);
    QVERIFY(isNear(pixel(overflow, 10 + 168, 10 + 125), surface, 0));
    QVERIFY(isNear(pixel(overflow, 5, 80), surface, 0));
    QCOMPARE(describeDifference(clipped.copy(10, 10, 160, 160), overflow.copy(10, 10, 160, 160), 0), QString());

    QWidget parent;
    parent.resize(200, 200);
    pdf::PDFLockedDocumentPicture* picture = new pdf::PDFLockedDocumentPicture(&parent);
    picture->setGeometry(10, 10, 160, 160);
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));
    const QImage rendered = renderWidget(&parent, 1.0);
    QVERIFY(colorDistance(pixel(rendered, 10 + 161, 10 + 125), surface) > 3);
    QVERIFY(isNear(pixel(rendered, 10 + 80, 10 + 5), token(Color::AccentBlueBg)));
    QCOMPARE(countLeakPixels(rendered), 0);
}

void FluentComponentsTest::foreignStyle()
{
    // Under another style of the application nothing draws the outside ring:
    // the card keeps its border and draws the ring inside itself (review C14)
    QApplication::setStyle(new QCommonStyle());
    QVERIFY(!pdf::PDFWidgetStyle::isActive());

    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    pdf::PDFQuickActionTile* tile = new pdf::PDFQuickActionTile(QStringLiteral("Open PDF"), QStringLiteral("Browse"), QStringLiteral("folder-open"), pdf::PDFAccentHue::Blue, &window);
    tile->setFixedSize(224, 88);
    layout->addWidget(tile);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    window.setAttribute(Qt::WA_KeyboardFocusChange, true);
    tile->setFocus(Qt::TabFocusReason);
    QVERIFY(tile->hasFocus());
    QVERIFY(pdf::PDFWidgetStyle::isFocusRingVisible(tile));

    const QImage image = renderWidget(tile, 1.0);
    QVERIFY(isNear(pixel(image, 104, 0), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 104, 1), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 104, 3), token(Color::AccentBlueBg)));

    // The style of the components is restored for the other tests
    m_style = new pdf::PDFWidgetStyle();
    QApplication::setStyle(m_style);
    QVERIFY(pdf::PDFWidgetStyle::isActive());
}

/// Contents of the composite widgets of the step 4 as the variants of the design
class PanelSamples
{
public:
    PanelSamples() = delete;

    static QList<Painter::ToolChoice> choices()
    {
        return { Painter::ToolChoice{ QStringLiteral("Add Page Numbers"), QStringLiteral("hash"), pdf::PDFAccentHue::Green },
                 Painter::ToolChoice{ QStringLiteral("Compress"), QStringLiteral("minimize-2"), pdf::PDFAccentHue::Violet },
                 Painter::ToolChoice{ QStringLiteral("Protect with Password"), QStringLiteral("lock"), pdf::PDFAccentHue::Red } };
    }

    static Painter::ContinueWith continueWith(int count = 3)
    {
        Painter::ContinueWith content;
        content.title = QStringLiteral("Then continue with");
        content.note = QStringLiteral("optional");
        content.linkText = QStringLiteral("Another tool…");
        content.caption = QStringLiteral("Nothing is chosen – the tool just finishes.");
        content.choices = choices().mid(0, count);
        return content;
    }

    static Painter::SignatureCard signature(Painter::SignatureStatus status, bool expanded)
    {
        using Status = Painter::SignatureStatus;
        Painter::SignatureCard card;
        card.signer = QStringLiteral("Jane Novak");
        card.meta = QStringLiteral("Digital signature · Sep 28, 2026");
        card.summary = QStringLiteral("The document has not been changed since it was signed.");
        card.status = status;
        card.expanded = expanded;
        card.chainCaption = QStringLiteral("Certificate chain");
        card.details = { { QStringLiteral("Signed on"), QStringLiteral("Sep 28, 2026, 14:02") }, { QStringLiteral("Hash algorithm"), QStringLiteral("SHA-256") },
                         { QStringLiteral("Covers"), QStringLiteral("Entire document") },
                         { QStringLiteral("Timestamp"), status == Status::Invalid ? QStringLiteral("Not embedded") : QStringLiteral("Embedded, valid") } };
        Painter::SignatureCertificate signer{ QStringLiteral("Jane Novak"), QStringLiteral("Signer · RSA 2048 · valid until Mar 14, 2028"), Status::Valid, QString() };
        if (status == Status::Invalid)
        {
            signer = { QStringLiteral("Jane Novak"), QStringLiteral("Signer · RSA 2048 · revoked on Sep 2, 2026"), Status::Invalid, QString() };
        }
        Painter::SignatureCertificate root{ QStringLiteral("Example Root CA"), QStringLiteral("Trusted root certificate"), Status::Valid, QString() };
        if (status == Status::Warning)
        {
            root = { QStringLiteral("Example Root CA"), QStringLiteral("Not among trusted certificates"), Status::Warning, QStringLiteral("Add to trusted certificates") };
        }
        card.certificates = { signer, { QStringLiteral("Example Issuing CA 2"), QStringLiteral("Intermediate · RSA 4096 · valid until Jan 5, 2031"), Status::Valid, QString() }, root };
        return card;
    }

    static void fill(pdf::PDFSignatureCard* widget, const Painter::SignatureCard& card)
    {
        widget->setSigner(card.signer);
        widget->setMeta(card.meta);
        widget->setSummary(card.summary);
        widget->setStatus(card.status);
        widget->setDetails(card.details);
        widget->setCertificates(card.certificates);
        widget->setExpanded(card.expanded);
    }

    static Painter::StrengthMeter meter(Painter::StrengthLevel level, Painter::StrengthCriteria criteria = Painter::StrengthCriterion::None)
    {
        Painter::StrengthMeter meter;
        meter.level = level;
        meter.title = QStringLiteral("Password strength");
        meter.detail = QStringLiteral("118 of 128 bits");
        meter.satisfied = criteria;
        return meter;
    }

    static QList<pdf::PDFColorPalette::Entry> suggested()
    {
        QList<pdf::PDFColorPalette::Entry> entries;
        const QStringList colors = { QStringLiteral("#111111"), QStringLiteral("#2a2a2a"), QStringLiteral("#5b4636"), QStringLiteral("#1a2b22"), QStringLiteral("#cfd4da"), QStringLiteral("#eeeeee"),
                                     QStringLiteral("#00ff7f"), QStringLiteral("#f59e0b"), QStringLiteral("#ffff00"), QStringLiteral("#e9eef5"), QStringLiteral("#475569"), QStringLiteral("#2563eb") };
        for (int i = 0; i < colors.size(); ++i)
        {
            entries << pdf::PDFColorPalette::Entry{ QColor(colors.at(i)), QStringLiteral("Color %1").arg(i + 1) };
        }
        entries[0].name = QStringLiteral("Black");
        entries[6].name = QStringLiteral("Terminal green");
        return entries;
    }

    static QList<pdf::PDFColorPalette::Entry> recent()
    {
        return { { QColor(QStringLiteral("#00ff7f")), QStringLiteral("Terminal green") }, { QColor(QStringLiteral("#fb923c")), QStringLiteral("Orange") },
                 { QColor(QStringLiteral("#e9eef5")), QString() } };
    }

    /// Returns an empty text, if the images are the same: at the scale 1.5 the
    /// nested widgets lie on half pixels, the antialiased pixels at their edges
    /// are clipped by the widget, so a few pixels at the edges may differ
    static QString compareScaled(const QImage& first, const QImage& second, qreal scale, const QString& name)
    {
        if (first.size() != second.size())
        {
            return QStringLiteral("%1: size %2x%3 / %4x%5").arg(name).arg(first.width()).arg(first.height()).arg(second.width()).arg(second.height());
        }

        const bool isFractional = qFuzzyCompare(scale, 1.5);
        const int tolerance = isFractional ? 24 : 3;
        int count = 0;
        QString description;
        for (int y = 0; y < first.height(); ++y)
        {
            for (int x = 0; x < first.width(); ++x)
            {
                const QColor a = first.pixelColor(x, y);
                const QColor b = second.pixelColor(x, y);
                if (qAbs(a.red() - b.red()) > tolerance || qAbs(a.green() - b.green()) > tolerance || qAbs(a.blue() - b.blue()) > tolerance)
                {
                    if (count == 0)
                    {
                        description = QStringLiteral("(%1, %2) %3 / %4").arg(x).arg(y).arg(a.name(), b.name());
                    }
                    ++count;
                }
            }
        }

        const int allowed = isFractional ? 160 : 0;
        return count > allowed ? QStringLiteral("%1 (scale %2): %3 pixels, first %4").arg(name).arg(scale).arg(count).arg(description) : QString();
    }

    static QStringList labels()
    {
        static const char* romans[] = { "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x", "xi", "xii" };
        QStringList result;
        for (int page = 1; page <= 48; ++page)
        {
            result << (page <= 12 ? QString::fromLatin1(romans[page - 1]) : QString::number(page - 12));
        }
        return result;
    }
};

void FluentComponentsTest::pageRangeParser()
{
    using Range = pdf::PDFPageRange;
    using Interval = Range::Interval;

    // Valid texts (the syntax of PDFClosedIntervalSet::parse)
    struct ValidCase
    {
        QString text;
        int pageCount;
        QList<Interval> intervals;
    };
    const QList<ValidCase> validCases =
    {
        { QString(), 10, {} },
        { QStringLiteral("   "), 10, {} },
        { QStringLiteral("1-3, 8, 10-12"), 48, { { 1, 3 }, { 8, 8 }, { 10, 12 } } },
        { QStringLiteral("  5 "), 10, { { 5, 5 } } },
        { QStringLiteral("3,1,2"), 10, { { 1, 3 } } },
        { QStringLiteral("1-5,3-8"), 10, { { 1, 8 } } },
        { QStringLiteral("1-2,3-4"), 10, { { 1, 4 } } },
        { QStringLiteral("7,7,7"), 10, { { 7, 7 } } },
        { QStringLiteral("-3"), 10, { { 1, 3 } } },
        { QStringLiteral("8-"), 10, { { 8, 10 } } },
        { QStringLiteral("-"), 10, { { 1, 10 } } },
        { QStringLiteral("-"), 0, {} },
        { QStringLiteral("1 - 3"), 10, { { 1, 3 } } },
        { QStringLiteral("1–3"), 10, { { 1, 3 } } },
        { QStringLiteral("+4"), 10, { { 4, 4 } } },
        { QStringLiteral("0004"), 10, { { 4, 4 } } },
        { QStringLiteral("٣-٥"), 10, { { 3, 5 } } },
        { QStringLiteral(",,1,,"), 10, { { 1, 1 } } },
        { QStringLiteral("1, ,2"), 10, { { 1, 2 } } },
        { QStringLiteral("10-10"), 10, { { 10, 10 } } },
        { QStringLiteral("\t2\t,\t4\t"), 10, { { 2, 2 }, { 4, 4 } } }
    };
    for (const ValidCase& validCase : validCases)
    {
        const Range::Result result = Range::parse(validCase.text, validCase.pageCount);
        QVERIFY2(result.isValid(), qPrintable(validCase.text + QStringLiteral(": ") + result.errorMessage));
        QVERIFY2(result.intervals == validCase.intervals, qPrintable(validCase.text + QStringLiteral(": ") + Range::format(result.intervals)));
    }

    // Errors: the first one is reported, the result has no interval
    struct ErrorCase
    {
        QString text;
        int pageCount;
        QString message;
    };
    const QList<ErrorCase> errorCases =
    {
        { QStringLiteral("a"), 10, QStringLiteral("Can't convert '\u2068a\u2069' to a number.") },
        { QStringLiteral("1-2-3"), 10, QStringLiteral("Can't convert '\u20682-3\u2069' to a number.") },
        { QStringLiteral("1.5"), 10, QStringLiteral("Can't convert '\u20681.5\u2069' to a number.") },
        { QStringLiteral("1 2"), 10, QStringLiteral("Can't convert '\u20681 2\u2069' to a number.") },
        { QStringLiteral("++1"), 10, QStringLiteral("Can't convert '\u2068++1\u2069' to a number.") },
        { QStringLiteral("+"), 10, QStringLiteral("Can't convert '\u2068+\u2069' to a number.") },
        { QStringLiteral("x-3"), 10, QStringLiteral("Can't convert '\u2068x\u2069' to a number.") },
        { QStringLiteral("2-y"), 10, QStringLiteral("Can't convert '\u2068y\u2069' to a number.") },
        { QStringLiteral("²"), 10, QStringLiteral("Can't convert '\u2068²\u2069' to a number.") },
        { QStringLiteral("0"), 10, QStringLiteral("Page 0 does not exist – the document has 10 pages") },
        { QStringLiteral("11"), 10, QStringLiteral("Page 11 does not exist – the document has 10 pages") },
        { QStringLiteral("1-3, 60"), 48, QStringLiteral("Page 60 does not exist – the document has 48 pages") },
        { QStringLiteral("2"), 1, QStringLiteral("Page 2 does not exist – the document has 1 page") },
        { QStringLiteral("1"), 0, QStringLiteral("Page 1 does not exist – the document has 0 pages") },
        { QStringLiteral("1-"), 0, QStringLiteral("Page 1 does not exist – the document has 0 pages") },
        { QStringLiteral("5-3"), 10, QStringLiteral("Closed interval [5, 3] is invalid.") },
        { QStringLiteral("a, 99"), 10, QStringLiteral("Can't convert '\u2068a\u2069' to a number.") },
        { QStringLiteral("99, a"), 10, QStringLiteral("Page 99 does not exist – the document has 10 pages") },
        { QStringLiteral("3000000000"), 10, QStringLiteral("Page 3000000000 does not exist – the document has 10 pages") },
        { QStringLiteral("2147483648-2147483649"), 10, QStringLiteral("Page 2147483648 does not exist – the document has 10 pages") },
        { QStringLiteral("9223372036854775807"), 10, QStringLiteral("Page 9223372036854775807 does not exist – the document has 10 pages") },
        { QStringLiteral("9223372036854775808"), 10, QStringLiteral("Can't convert '\u20689223372036854775808\u2069' to a number.") },
        { QStringLiteral("99999999999999999999999"), 10, QStringLiteral("Can't convert '\u206899999999999999999999999\u2069' to a number.") }
    };
    for (const ErrorCase& errorCase : errorCases)
    {
        const Range::Result result = Range::parse(errorCase.text, errorCase.pageCount);
        QVERIFY2(!result.isValid(), qPrintable(errorCase.text));
        QCOMPARE(result.errorMessage, errorCase.message);
        QVERIFY(result.intervals.isEmpty());
        QCOMPARE(result.getCount(), 0);
    }

    // A long part is shortened in the message
    const QString garbage(1000, QLatin1Char('x'));
    const Range::Result garbageResult = Range::parse(garbage, 10);
    QCOMPARE(garbageResult.errorMessage, QStringLiteral("Can't convert '\u2068%1…\u2069' to a number.").arg(QString(32, QLatin1Char('x'))));

    // The page count is bounded, extreme values do not overflow
    QCOMPARE(Range::parse(QStringLiteral("-"), std::numeric_limits<int>::max()).intervals, (QList<Interval>{ { 1, Range::MAX_PAGE_COUNT } }));
    QCOMPARE(Range::parse(QStringLiteral("-"), -5).intervals, QList<Interval>());
    QVERIFY(!Range::parse(QStringLiteral("2147483647"), std::numeric_limits<int>::max()).isValid());
    QCOMPARE(Range::parse(QStringLiteral("1, 10000000"), std::numeric_limits<int>::max()).getCount(), 2);

    // Result: count, membership, pages
    const Range::Result result = Range::parse(QStringLiteral("10-12, 1-3, 8"), 48);
    QCOMPARE(result.getCount(), 7);
    QCOMPARE(result.getPages(), (QList<int>{ 1, 2, 3, 8, 10, 11, 12 }));
    for (int page = -2; page <= 50; ++page)
    {
        QCOMPARE(result.contains(page), result.getPages().contains(page));
    }
    QCOMPARE(Range::format(result.intervals), QStringLiteral("1-3, 8, 10-12"));
    QCOMPARE(Range::format({}), QString());

    // Normalization: invalid intervals are dropped, the last page INT_MAX does not overflow
    const int maxInt = std::numeric_limits<int>::max();
    QCOMPARE(Range::normalized({ { 5, 3 }, { 0, 2 }, { -4, -1 }, { maxInt - 1, maxInt }, { 1, 1 }, { maxInt, maxInt } }),
             (QList<Interval>{ { 1, 1 }, { maxInt - 1, maxInt } }));
    QCOMPARE(Range::normalized({ { 4, 6 }, { 1, 3 }, { 7, 9 } }), (QList<Interval>{ { 1, 9 } }));

    // A huge text: a hundred thousand parts
    QStringList parts;
    for (int page = 100000; page >= 1; --page)
    {
        parts << QString::number(page);
    }
    QElapsedTimer timer;
    timer.start();
    const Range::Result huge = Range::parse(parts.join(QLatin1Char(',')), 100000);
    QVERIFY(huge.isValid());
    QCOMPARE(huge.intervals, (QList<Interval>{ { 1, 100000 } }));
    QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));
}

void FluentComponentsTest::pageRangeFuzz()
{
    using Range = pdf::PDFPageRange;

    // Random texts of the characters of the syntax and of others never crash,
    // a valid result is sorted, disjoint and within the document, its text
    // gives the same result again
    QRandomGenerator generator(20261017);
    const QString alphabet = QStringLiteral("0123456789--,, +–٣a.\t9");
    for (int iteration = 0; iteration < 20000; ++iteration)
    {
        const int pageCount = generator.bounded(-3, 120);
        const int length = generator.bounded(0, 40);
        QString text;
        for (int i = 0; i < length; ++i)
        {
            text += alphabet.at(generator.bounded(static_cast<int>(alphabet.size())));
        }

        const Range::Result result = Range::parse(text, pageCount);
        if (!result.isValid())
        {
            QVERIFY(result.intervals.isEmpty());
            continue;
        }

        int previousLast = -1;
        for (const Range::Interval& interval : result.intervals)
        {
            QVERIFY2(interval.first >= 1 && interval.first <= interval.last && interval.last <= qMax(0, pageCount), qPrintable(text));
            QVERIFY2(interval.first > previousLast + 1, qPrintable(text));
            previousLast = interval.last;
        }

        const Range::Result again = Range::parse(Range::format(result.intervals), pageCount);
        QVERIFY2(again.isValid() && again.intervals == result.intervals, qPrintable(text));
    }

    // Random bytes as UTF-16 (surrogates, controls, any script)
    for (int iteration = 0; iteration < 2000; ++iteration)
    {
        QString text;
        const int length = generator.bounded(0, 64);
        for (int i = 0; i < length; ++i)
        {
            text += QChar(static_cast<char16_t>(generator.bounded(0, 0x10000)));
        }
        const Range::Result result = Range::parse(text, 50);
        QVERIFY(result.isValid() || result.intervals.isEmpty());
        QVERIFY(result.errorMessage.size() < 200);
    }
}

void FluentComponentsTest::pageSelectionResult()
{
    using Selection = pdf::PDFPageSelection;
    using Mode = Selection::Mode;
    using Filter = Selection::Filter;

    Selection selection;
    QCOMPARE(selection.getMode(), Mode::All);
    QCOMPARE(selection.getPageCount(), 0);
    QCOMPARE(selection.getResultCount(), 0);
    QCOMPARE(selection.getStatusText(), QStringLiteral("No pages selected"));
    QCOMPARE(selection.getContent().total, QStringLiteral("0 pages"));
    QCOMPARE(selection.getLabel(), QStringLiteral("Pages"));

    QSignalSpy resultSpy(&selection, &Selection::resultChanged);
    QSignalSpy modeSpy(&selection, &Selection::modeChanged);
    QSignalSpy filtersSpy(&selection, &Selection::filtersChanged);
    QSignalSpy rangeSpy(&selection, &Selection::rangeChanged);

    selection.setPageCount(48);
    QCOMPARE(selection.getResultCount(), 48);
    QCOMPARE(selection.getStatusText(), QStringLiteral("48 pages selected · 1 – 48"));
    QCOMPARE(selection.getContent().total, QStringLiteral("48 pages"));
    QCOMPARE(resultSpy.count(), 1);
    selection.setPageCount(48);
    QCOMPARE(resultSpy.count(), 1);

    // Labels are shown next to the numbers, a label equal to the number is not
    selection.setPageLabels(PanelSamples::labels());
    QCOMPARE(selection.getStatusText(), QStringLiteral("48 pages selected · 1 (i) – 48 (36)"));
    QStringList numbers;
    for (int page = 1; page <= 48; ++page)
    {
        numbers << QString::number(page);
    }
    selection.setPageLabels(numbers);
    QCOMPARE(selection.getStatusText(), QStringLiteral("48 pages selected · 1 – 48"));
    selection.setPageLabels(PanelSamples::labels());

    // Current page
    selection.setCurrentPage(3);
    selection.setMode(Mode::Current);
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(modeSpy.at(0).at(0).value<Mode>(), Mode::Current);
    QCOMPARE(selection.getResultPages(), QList<int>{ 3 });
    QCOMPARE(selection.getStatusText(), QStringLiteral("1 page selected · 3 (iii)"));
    selection.setCurrentPage(100);
    QCOMPARE(selection.getResultCount(), 0);
    selection.setCurrentPage(-7);
    QCOMPARE(selection.getCurrentPage(), 0);
    QCOMPARE(selection.getResultCount(), 0);

    // Selected pages: sorted without duplicates, pages out of the document are left out of the result
    selection.setSelectedPages({ 5, 3, 3, 100, 0, -1, 12 });
    QCOMPARE(selection.getSelectedPages(), (QList<int>{ 3, 5, 12, 100 }));
    selection.setMode(Mode::Selected);
    QCOMPARE(selection.getResultPages(), (QList<int>{ 3, 5, 12 }));
    QCOMPARE(selection.getStatusText(), QStringLiteral("3 pages selected · 3 (iii) – 12 (xii)"));

    // Range: the text of the design
    selection.setRange(QStringLiteral("3-5, 8, 10-12"));
    QCOMPARE(rangeSpy.count(), 1);
    QCOMPARE(rangeSpy.at(0).at(0).toString(), QStringLiteral("3-5, 8, 10-12"));
    QCOMPARE(selection.getResultCount(), 3);
    selection.setMode(Mode::Range);
    QCOMPARE(selection.getResultCount(), 7);
    QCOMPARE(selection.getStatusText(), QStringLiteral("7 pages selected · 3 (iii) – 12 (xii)"));
    QCOMPARE(selection.getRange(), QStringLiteral("3-5, 8, 10-12"));
    QCOMPARE(selection.getRangeField()->text(), QStringLiteral("3-5, 8, 10-12"));

    // Filters: Even and Odd together keep all pages, the kinds narrow each other
    selection.setFilters(Filter::Even);
    QCOMPARE(filtersSpy.count(), 1);
    QCOMPARE(selection.getResultPages(), (QList<int>{ 4, 8, 10, 12 }));
    QVERIFY(selection.getFilterCheckBox(Filter::Even)->isChecked());
    selection.setFilters(Filter::Even | Filter::Odd);
    QCOMPARE(selection.getResultCount(), 7);
    selection.getFilterCheckBox(Filter::Even)->setChecked(false);
    QCOMPARE(selection.getFilters(), Selection::Filters(Filter::Odd));
    QCOMPARE(selection.getResultPages(), (QList<int>{ 3, 5, 11 }));
    QCOMPARE(filtersSpy.count(), 3);

    // Orientation needs the sizes: an unknown size passes no orientation filter
    selection.setFilters(Filter::Landscape);
    QCOMPARE(selection.getResultCount(), 0);
    QList<QSizeF> sizes;
    for (int page = 1; page <= 48; ++page)
    {
        sizes << (page == 8 || page == 11 ? QSizeF(842, 595) : QSizeF(595, 842));
    }
    sizes[4] = QSizeF();
    sizes[9] = QSizeF(qQNaN(), 10);
    selection.setPageSizes(sizes);
    QCOMPARE(selection.getResultPages(), (QList<int>{ 8, 11 }));
    selection.setFilters(Filter::Portrait);
    QCOMPARE(selection.getResultPages(), (QList<int>{ 3, 4, 12 }));
    selection.setFilters(Filter::Portrait | Filter::Landscape | Filter::Odd);
    QCOMPARE(selection.getResultPages(), (QList<int>{ 3, 5, 11 }));
    selection.setFilters(Filter::Portrait | Filter::Odd);
    QCOMPARE(selection.getResultPages(), (QList<int>{ 3 }));
    QCOMPARE(selection.getResultIntervals(), (QList<pdf::PDFPageRange::Interval>{ { 3, 3 } }));
    selection.setFilters(Selection::Filters(0xFF));
    QCOMPARE(selection.getFilters(), Filter::Even | Filter::Odd | Filter::Portrait | Filter::Landscape);
    selection.setFilters(Filter::None);

    // Errors: the invalid range is the state Error, the height grows by 24
    QCOMPARE(selection.heightForWidth(320), 200);
    resultSpy.clear();
    selection.setRange(QStringLiteral("1-3, 60"));
    QVERIFY(!selection.isValid());
    QCOMPARE(selection.getError(), QStringLiteral("Page 60 does not exist – the document has 48 pages"));
    QCOMPARE(selection.getResultCount(), 0);
    QVERIFY(selection.getRangeField()->isInvalid());
    QCOMPARE(selection.getRangeField()->accessibleDescription(), selection.getError());
    QCOMPARE(selection.heightForWidth(320), 224);
    QCOMPARE(resultSpy.count(), 1);

    // In another mode the error of the range is not shown, the field keeps it
    selection.setMode(Mode::All);
    QVERIFY(selection.isValid());
    QCOMPARE(selection.getRangeError(), QStringLiteral("Page 60 does not exist – the document has 48 pages"));
    QCOMPARE(selection.heightForWidth(320), 156);
    QVERIFY(selection.getRangeField()->isInvalid());

    // An error of the application in any mode
    selection.setCustomError(QStringLiteral("The pages are locked"));
    QCOMPARE(selection.getError(), QStringLiteral("The pages are locked"));
    QCOMPARE(selection.heightForWidth(320), 180);
    selection.setMode(Mode::Range);
    QCOMPARE(selection.getError(), QStringLiteral("Page 60 does not exist – the document has 48 pages"));
    selection.setRange(QStringLiteral("2"));
    QCOMPARE(selection.getError(), QStringLiteral("The pages are locked"));
    QVERIFY(!selection.getRangeField()->isInvalid());
    selection.setCustomError(QString());
    QVERIFY(selection.isValid());

    // A long message wraps, the widget grows by its lines
    selection.setCustomError(QString(40, QLatin1Char(' ')).replace(QLatin1Char(' '), QStringLiteral("word ")));
    QVERIFY(selection.heightForWidth(320) > 224 + 12);
    QVERIFY(selection.heightForWidth(200) > selection.heightForWidth(320));
    selection.setCustomError(QString());

    // Defensive cases
    selection.setMode(static_cast<Mode>(17));
    QCOMPARE(selection.getMode(), Mode::Range);
    selection.setPageCount(-5);
    QCOMPARE(selection.getPageCount(), 0);
    QCOMPARE(selection.getContent().total, QStringLiteral("0 pages"));
    selection.setPageCount(std::numeric_limits<int>::max());
    QCOMPARE(selection.getPageCount(), pdf::PDFPageRange::MAX_PAGE_COUNT);
    selection.setPageCount(1);
    QCOMPARE(selection.getContent().total, QStringLiteral("1 page"));
    QCOMPARE(selection.getFilterCheckBox(Filter::None), nullptr);
    QCOMPARE(selection.getFilterCheckBox(static_cast<Filter>(0x40)), nullptr);

    // Nested widgets deleted by the application are forgotten
    delete selection.getRangeField();
    delete selection.getFilterCheckBox(Filter::Odd);
    selection.setPageCount(10);
    selection.setRange(QStringLiteral("1-4"));
    QCOMPARE(selection.getResultCount(), 4);
    selection.setFilters(Filter::Odd);
    QCOMPARE(selection.getResultPages(), (QList<int>{ 1, 3 }));
    QCOMPARE(selection.getRangeField(), nullptr);
    selection.resize(320, 300);
    QCOMPARE(countLeakPixels(renderWidget(&selection, 1.0)), 0);

    // A slot, which deletes the widget, does not break it
    QPointer<Selection> deleted = new Selection();
    deleted->setPageCount(5);
    connect(deleted.data(), &Selection::resultChanged, deleted.data(), [&deleted]() { delete deleted.data(); });
    deleted->setMode(Mode::Current);
    QVERIFY(!deleted);
}

void FluentComponentsTest::pageSelectionWidget()
{
    using Selection = pdf::PDFPageSelection;
    using Mode = Selection::Mode;

    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(10, 10, 10, 10);
    Selection* selection = new Selection(&window);
    selection->setPageCount(48);
    selection->setPageLabels(PanelSamples::labels());
    selection->setRange(QStringLiteral("1-3"));
    layout->addWidget(selection);
    layout->addStretch(1);
    window.resize(340, 400);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    // Default size of the design, the height follows the mode and the error
    QCOMPARE(selection->sizeHint(), QSize(320, 156));
    QCOMPARE(selection->width(), 320);
    QTRY_COMPARE(selection->height(), 156);
    selection->setMode(Mode::Range);
    QTRY_COMPARE(selection->height(), 200);
    QVERIFY(selection->getRangeField()->isVisible());
    QCOMPARE(selection->getRangeField()->geometry(), QRect(0, 67, 320, 34));
    selection->setRange(QStringLiteral("60"));
    QTRY_COMPARE(selection->height(), 224);
    selection->setMode(Mode::All);
    QTRY_COMPARE(selection->height(), 156);
    QVERIFY(!selection->getRangeField()->isVisible());

    // The segments divide the width equally (Q-9)
    QList<QAbstractButton*> segments = selection->getModeControl()->findChildren<QAbstractButton*>(Qt::FindDirectChildrenOnly);
    QCOMPARE(segments.size(), 4);
    QCOMPARE(selection->getModeControl()->geometry(), QRect(0, 25, 320, 32));
    for (QAbstractButton* segment : segments)
    {
        QCOMPARE(segment->width(), 80);
        QCOMPARE(segment->height(), 32);
    }
    QCOMPARE(segments.at(3)->text(), QStringLiteral("Range"));

    // Check boxes in two columns of 156 with the gap of 8
    QCOMPARE(selection->getFilterCheckBox(Selection::Filter::Even)->geometry(), QRect(0, 88, 156, 18));
    QCOMPARE(selection->getFilterCheckBox(Selection::Filter::Odd)->geometry(), QRect(164, 88, 156, 18));
    QCOMPARE(selection->getFilterCheckBox(Selection::Filter::Landscape)->geometry(), QRect(164, 114, 156, 18));

    // Minimal width: the segments with the padding of 6, the check boxes
    const QSize minimum = selection->minimumSizeHint();
    QVERIFY2(minimum.width() < 279 && minimum.width() > 200, qPrintable(QString::number(minimum.width())));
    selection->setFixedWidth(279);
    QTRY_COMPARE(selection->width(), 279);
    for (QAbstractButton* segment : segments)
    {
        QVERIFY(qAbs(segment->width() - 70) <= 1);
    }

    // Keyboard: the arrows move the mode, Tab goes from the segments to the field and the filters
    window.setAttribute(Qt::WA_KeyboardFocusChange, true);
    segments.at(0)->setFocus(Qt::TabFocusReason);
    QVERIFY(segments.at(0)->hasFocus());
    QTest::keyClick(segments.at(0), Qt::Key_Right);
    QCOMPARE(selection->getMode(), Mode::Current);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Right);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Right);
    QCOMPARE(selection->getMode(), Mode::Range);
    QVERIFY(selection->getRangeField()->isVisible());
    QWidget* next = segments.at(3)->nextInFocusChain();
    while (next && !(next->focusPolicy() & Qt::TabFocus))
    {
        next = next->nextInFocusChain();
    }
    QCOMPARE(next, selection->getRangeField());
    next = selection->getRangeField()->nextInFocusChain();
    QCOMPARE(next, selection->getFilterCheckBox(Selection::Filter::Even));

    // A typed range updates the result at once
    selection->getRangeField()->setFocus(Qt::TabFocusReason);
    selection->getRangeField()->selectAll();
    QTest::keyClicks(selection->getRangeField(), QStringLiteral("2-4,9"));
    QCOMPARE(selection->getRange(), QStringLiteral("2-4,9"));
    QCOMPARE(selection->getResultCount(), 4);

    // Accessibility: the label is the name, the status (and the error) the description; a new error is announced
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(selection);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Pages"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("4 pages selected · 2 (ii) – 9 (ix)"));
    QCOMPARE(QAccessible::queryAccessibleInterface(selection->getRangeField())->text(QAccessible::Name), QStringLiteral("Page range"));
    QVERIFY(!selection->getModeControl()->accessibleName().isEmpty());
    s_accessibleEvents.clear();
    const QAccessible::UpdateHandler previousHandler = QAccessible::installUpdateHandler(&FluentComponentsTest::collectAccessibleEvent);
    selection->setRange(QStringLiteral("70"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QCOMPARE(countAccessibleEvents(selection, QAccessible::Announcement), 1);
    selection->setRange(QStringLiteral("80"));
    QCOMPARE(countAccessibleEvents(selection, QAccessible::Announcement), 2);
    selection->setRange(QStringLiteral("80"));
    QCOMPARE(countAccessibleEvents(selection, QAccessible::Announcement), 2);
#endif
    QVERIFY(accessible->text(QAccessible::Description).contains(QStringLiteral("Page 80 does not exist")));
    QAccessible::installUpdateHandler(previousHandler);
    selection->setAccessibleName(QStringLiteral("Pages to print"));
    selection->setRange(QStringLiteral("1"));
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Pages to print"));

    // Painted parts: the label text/primary, the count text/tertiary, disabled text/disabled
    selection->setFixedWidth(320);
    QTRY_COMPARE(selection->width(), 320);
    const QColor surface = token(Color::BgSurface);
    QImage image = renderWidget(selection, 1.0);
    QVERIFY(colorDistance(strongestInk(image, QRect(0, 0, 40, 15), surface), token(Color::TextPrimary)) < 40);
    QVERIFY(colorDistance(strongestInk(image, QRect(270, 0, 50, 15), surface), token(Color::TextTertiary)) < 60);
    selection->setEnabled(false);
    image = renderWidget(selection, 1.0);
    QVERIFY(colorDistance(strongestInk(image, QRect(0, 0, 40, 15), surface), token(Color::TextDisabled)) < 40);
    selection->setEnabled(true);

    // Right-to-left: the first column of the filters and the label at the right
    selection->setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(selection->getFilterCheckBox(Selection::Filter::Even)->geometry(), QRect(164, 132, 156, 18));
    image = renderWidget(selection, 1.0);
    QVERIFY(colorDistance(strongestInk(image, QRect(280, 0, 40, 15), surface), token(Color::TextPrimary)) < 40);
    selection->setLayoutDirection(Qt::LeftToRight);

    // Theme change and the scales: no leak, the painted parts are the same as the painter draws
    for (const char* theme : { "light", "dark" })
    {
        QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(QString::fromLatin1(theme)));
        for (const qreal scale : { 1.0, 1.5, 2.0 })
        {
            const QImage widgetImage = renderWidget(selection, scale);
            QCOMPARE(countLeakPixels(widgetImage), 0);
            const QImage painterImage = renderInWidget([&](QPainter& painter) { Painter::paintPageSelection(&painter, QRectF(selection->rect()), selection->getContent(), state(QStyle::State_None)); }, selection->size(), scale);
            const QRect header(0, 0, 320, 15);
            QCOMPARE(describeDifference(widgetImage.copy(QRect(header.topLeft() * scale, header.size() * scale)), painterImage.copy(QRect(header.topLeft() * scale, header.size() * scale)), 3), QString());
        }
    }
}

void FluentComponentsTest::continueWith()
{
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(10, 10, 10, 10);
    pdf::PDFContinueWith* widget = new pdf::PDFContinueWith(&window);
    layout->addWidget(widget);
    layout->addStretch(1);
    window.resize(500, 300);

    // Default texts, no choice
    QCOMPARE(widget->getTitle(), QStringLiteral("Then continue with"));
    QCOMPARE(widget->getTitleNote(), QStringLiteral("optional"));
    QCOMPARE(widget->getLinkText(), QStringLiteral("Another tool…"));
    QCOMPARE(widget->getCurrentCaption(), QStringLiteral("Nothing is chosen – the tool just finishes."));
    QCOMPARE(widget->getSelectedChoice(), -1);
    QCOMPARE(widget->heightForWidth(480), 37);
    QCOMPARE(widget->getChoiceWidget(0), nullptr);

    // Three choices: 480 x 93, equal widths (154.67 in the design)
    widget->setChoices(PanelSamples::choices());
    QCOMPARE(widget->sizeHint(), QSize(480, 93));
    QCOMPARE(Painter::getContinueWithSize(PanelSamples::continueWith()), QSize(480, 93));
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QTRY_COMPARE(widget->size(), QSize(480, 93));
    for (int i = 0; i < 3; ++i)
    {
        QVERIFY(widget->getChoiceWidget(i)->isVisible());
        QCOMPARE(widget->getChoiceWidget(i)->height(), 48);
        QVERIFY(qAbs(widget->getChoiceWidget(i)->width() - 155) <= 1);
        QCOMPARE(widget->getChoiceWidget(i)->y(), 24);
    }
    QCOMPARE(widget->getChoiceWidget(0)->x(), 0);
    QCOMPARE(widget->getChoiceWidget(2)->geometry().right(), 479);
    QCOMPARE(widget->getLinkWidget()->geometry().right(), 479);
    QCOMPARE(widget->getLinkWidget()->y(), 0);

    // Two choices of 236, one, more than three (only three are kept)
    widget->setChoices(PanelSamples::choices().mid(0, 2));
    QTRY_COMPARE(widget->getChoiceWidget(1)->width(), 236);
    QVERIFY(!widget->findChildren<pdf::PDFToolChoice*>().at(2)->isVisible());
    QList<Painter::ToolChoice> four = PanelSamples::choices();
    four << Painter::ToolChoice{ QStringLiteral("Merge"), QStringLiteral("merge"), pdf::PDFAccentHue::Amber };
    widget->setChoices(four);
    QCOMPARE(widget->getChoices().size(), 3);
    QCOMPARE(widget->getChoiceWidget(3), nullptr);

    // Selection: a click selects, a second click unselects, another click switches
    QSignalSpy selectionSpy(widget, &pdf::PDFContinueWith::selectedChoiceChanged);
    QTest::mouseClick(widget->getChoiceWidget(1), Qt::LeftButton);
    QCOMPARE(widget->getSelectedChoice(), 1);
    QCOMPARE(selectionSpy.count(), 1);
    QCOMPARE(widget->getCurrentCaption(), QStringLiteral("Compress starts on the new document after this tool finishes successfully."));
    widget->setCurrentToolName(QStringLiteral("Merge"));
    QCOMPARE(widget->getCurrentCaption(), QStringLiteral("Compress starts on the new document after Merge finishes successfully."));
    widget->setCurrentToolName(QString());
    QTest::mouseClick(widget->getChoiceWidget(2), Qt::LeftButton);
    QCOMPARE(widget->getSelectedChoice(), 2);
    QCOMPARE(selectionSpy.count(), 2);
    QVERIFY(!widget->getChoiceWidget(1)->isSelected());
    QTest::mouseClick(widget->getChoiceWidget(2), Qt::LeftButton);
    QCOMPARE(widget->getSelectedChoice(), -1);
    QCOMPARE(selectionSpy.count(), 3);
    QCOMPARE(selectionSpy.last().at(0).toInt(), -1);

    // Programmatic selection, out of range indices are ignored, new choices clear the selection
    widget->setSelectedChoice(0);
    QCOMPARE(widget->getSelectedChoice(), 0);
    widget->setSelectedChoice(5);
    widget->setSelectedChoice(-2);
    QCOMPARE(widget->getSelectedChoice(), 0);
    widget->setChoices(PanelSamples::choices());
    QCOMPARE(widget->getSelectedChoice(), 0);
    widget->setChoices(PanelSamples::choices().mid(1));
    QCOMPARE(widget->getSelectedChoice(), -1);
    widget->setChoices(PanelSamples::choices());
    widget->setSelectedChoice(-1);
    QCOMPARE(widget->getSelectedChoice(), -1);

    // The link asks for another tool
    QSignalSpy linkSpy(widget, &pdf::PDFContinueWith::anotherToolRequested);
    QTest::mouseClick(widget->getLinkWidget(), Qt::LeftButton);
    QCOMPARE(linkSpy.count(), 1);

    // Keyboard: the choices are one stop of Tab, the arrows move the focus without a selection
    QVERIFY(widget->getChoiceWidget(0)->focusPolicy() & Qt::TabFocus);
    QVERIFY(!(widget->getChoiceWidget(1)->focusPolicy() & Qt::TabFocus));
    QVERIFY(!(widget->getChoiceWidget(2)->focusPolicy() & Qt::TabFocus));
    window.setAttribute(Qt::WA_KeyboardFocusChange, true);
    widget->getLinkWidget()->setFocus(Qt::TabFocusReason);
    QVERIFY(widget->getLinkWidget()->hasFocus());
    QTest::keyClick(widget->getLinkWidget(), Qt::Key_Tab);
    QVERIFY(widget->getChoiceWidget(0)->hasFocus());
    QTest::keyClick(widget->getChoiceWidget(0), Qt::Key_Right);
    QVERIFY(widget->getChoiceWidget(1)->hasFocus());
    QCOMPARE(widget->getSelectedChoice(), -1);
    QVERIFY(widget->getChoiceWidget(1)->focusPolicy() & Qt::TabFocus);
    QVERIFY(!(widget->getChoiceWidget(0)->focusPolicy() & Qt::TabFocus));
    QTest::keyClick(widget->getChoiceWidget(1), Qt::Key_End);
    QVERIFY(widget->getChoiceWidget(2)->hasFocus());
    QTest::keyClick(widget->getChoiceWidget(2), Qt::Key_Right);
    QVERIFY(widget->getChoiceWidget(2)->hasFocus());
    QTest::keyClick(widget->getChoiceWidget(2), Qt::Key_Space);
    QCOMPARE(widget->getSelectedChoice(), 2);
    QTest::keyClick(widget->getChoiceWidget(2), Qt::Key_Home);
    QVERIFY(widget->getChoiceWidget(0)->hasFocus());
    QVERIFY(widget->getChoiceWidget(0)->focusPolicy() & Qt::TabFocus);
    QVERIFY(!(widget->getChoiceWidget(2)->focusPolicy() & Qt::TabFocus));
    QTest::keyClick(widget->getChoiceWidget(0), Qt::Key_Return);
    QCOMPARE(widget->getSelectedChoice(), 0);
    QTest::keyClick(widget->getChoiceWidget(0), Qt::Key_Space);
    QCOMPARE(widget->getSelectedChoice(), -1);

    // Right-to-left: the first choice at the right, the left arrow goes forward
    widget->setLayoutDirection(Qt::RightToLeft);
    QTRY_COMPARE(widget->getChoiceWidget(0)->geometry().right(), 479);
    QCOMPARE(widget->getLinkWidget()->x(), 0);
    widget->getChoiceWidget(0)->setFocus(Qt::TabFocusReason);
    QTest::keyClick(widget->getChoiceWidget(0), Qt::Key_Left);
    QVERIFY(widget->getChoiceWidget(1)->hasFocus());
    widget->setLayoutDirection(Qt::LeftToRight);

    // Accessibility: the title is the name, the caption the description, the choices radio buttons
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(widget);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Then continue with"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Nothing is chosen – the tool just finishes."));
    QCOMPARE(QAccessible::queryAccessibleInterface(widget->getChoiceWidget(0))->role(), QAccessible::RadioButton);
    QCOMPARE(QAccessible::queryAccessibleInterface(widget->getChoiceWidget(0))->text(QAccessible::Name), QStringLiteral("Add Page Numbers"));

    // Own texts, a pattern without %1, an empty link
    widget->setCaption(QStringLiteral("Nothing"));
    widget->setSelectionCaption(QStringLiteral("Something"));
    QCOMPARE(widget->getCurrentCaption(), QStringLiteral("Nothing"));
    widget->setSelectedChoice(1);
    QCOMPARE(widget->getCurrentCaption(), QStringLiteral("Something"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Something"));
    widget->setLinkText(QString());
    QVERIFY(widget->getLinkWidget()->isHidden());
    QVERIFY(widget->getContent().linkText.isEmpty());

    // A long caption wraps, the widget grows
    widget->setCaption(QString(40, QLatin1Char(' ')).replace(QLatin1Char(' '), QStringLiteral("long ")));
    widget->setSelectedChoice(-1);
    QVERIFY(widget->heightForWidth(480) > 93);
    QVERIFY(widget->heightForWidth(300) > widget->heightForWidth(480));

    // Defensive: a deleted choice widget, a deleted link, a slot deleting the widget
    delete widget->getChoiceWidget(1);
    delete widget->getLinkWidget();
    widget->setChoices(PanelSamples::choices().mid(0, 2));
    widget->setLinkText(QStringLiteral("Other"));
    QCOMPARE(widget->getLinkText(), QString());
    QCOMPARE(widget->getChoiceWidget(1), nullptr);
    widget->setSelectedChoice(0);
    QCOMPARE(widget->getSelectedChoice(), 0);
    QCOMPARE(countLeakPixels(renderWidget(widget, 1.0)), 0);

    QPointer<pdf::PDFContinueWith> deleted = new pdf::PDFContinueWith();
    deleted->setChoices(PanelSamples::choices());
    connect(deleted.data(), &pdf::PDFContinueWith::selectedChoiceChanged, deleted.data(), [&deleted]() { delete deleted.data(); });
    deleted->getChoiceWidget(0)->click();
    QVERIFY(!deleted);
}

void FluentComponentsTest::colorButtonAndPalette()
{
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(20, 20, 20, 20);
    pdf::PDFColorButton* button = new pdf::PDFColorButton(QColor(QStringLiteral("#111111")), QStringLiteral("Black"), &window);
    QLineEdit* other = new QLineEdit(&window);
    layout->addWidget(button);
    layout->addWidget(other);
    layout->addStretch(1);
    window.resize(400, 500);

    // Size, texts, accessibility
    QCOMPARE(button->sizeHint(), QSize(260, 34));
    QCOMPARE(button->minimumSizeHint().height(), 34);
    QCOMPARE(button->getShownName(), QStringLiteral("Black"));
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(button);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Black"));
    QVERIFY(accessible->state().hasPopup);
    button->setAccessibleName(QStringLiteral("Text color"));
    QVERIFY(accessible->text(QAccessible::Description).startsWith(QStringLiteral("Black")));
    button->setAccessibleName(QString());

    pdf::PDFColorPalette* palette = button->getPalette();
    QVERIFY(palette->isPopup());
    QCOMPARE(palette->parentWidget(), button);
    palette->setSuggestedColors(PanelSamples::suggested());
    palette->setRecentColors(PanelSamples::recent());
    QCOMPARE(Painter::getColorPaletteSize(palette->getContent()), QSize(232, 209));

    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    // A click opens the popover below the button; the focus goes to the selected color
    QSignalSpy selectedSpy(button, &pdf::PDFColorButton::colorSelected);
    QSignalSpy changedSpy(button, &pdf::PDFColorButton::colorChanged);
    QTest::mouseClick(button, Qt::LeftButton);
    QVERIFY(button->isPaletteVisible());
    QCOMPARE(QApplication::activePopupWidget(), palette);
    QVERIFY(button->getContent().open);
    // The margins of the shadow of a popover (0 / 8 / 24) of a translucent window, none of an opaque one (review C18)
    const QMargins margins = palette->testAttribute(Qt::WA_TranslucentBackground) ? QMargins(24, 16, 24, 32) : QMargins();
    QCOMPARE(palette->size(), QSize(232, 209).grownBy(margins));
    const QPoint panelTopLeft = palette->mapToGlobal(QPoint(margins.left(), margins.top()));
    QCOMPARE(panelTopLeft, button->mapToGlobal(QPoint(0, button->height() + 4)));
    QCOMPARE(palette->layoutDirection(), Qt::LeftToRight);
    QCOMPARE(QApplication::focusWidget(), palette->getSuggestedSwatch(0));

    // The panel: its border, the selected swatch, the shadow in the margins
    const QImage panelImage = renderWidget(palette, 1.0);
    QCOMPARE(countLeakPixels(panelImage), 0);

    // Keyboard in the grid: the arrows, rows of six, into the recent colors, Home and End
    window.setAttribute(Qt::WA_KeyboardFocusChange, true);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Right);
    QCOMPARE(QApplication::focusWidget(), palette->getSuggestedSwatch(1));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
    QCOMPARE(QApplication::focusWidget(), palette->getSuggestedSwatch(7));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
    QCOMPARE(QApplication::focusWidget(), palette->getRecentSwatch(1));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
    QCOMPARE(QApplication::focusWidget(), palette->getRecentSwatch(1));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Up);
    QCOMPARE(QApplication::focusWidget(), palette->getSuggestedSwatch(7));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_End);
    QCOMPARE(QApplication::focusWidget(), palette->getSuggestedSwatch(11));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Right);
    QCOMPARE(QApplication::focusWidget(), palette->getSuggestedSwatch(11));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Home);
    QCOMPARE(QApplication::focusWidget(), palette->getSuggestedSwatch(0));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Up);
    QCOMPARE(QApplication::focusWidget(), palette->getSuggestedSwatch(0));
    QVERIFY(palette->getSuggestedSwatch(0)->isSelected());

    // One stop of Tab for each grid
    QVERIFY(palette->getSuggestedSwatch(0)->focusPolicy() & Qt::TabFocus);
    QVERIFY(!(palette->getSuggestedSwatch(1)->focusPolicy() & Qt::TabFocus));
    QVERIFY(palette->getRecentSwatch(0)->focusPolicy() & Qt::TabFocus);
    QVERIFY(!(palette->getRecentSwatch(1)->focusPolicy() & Qt::TabFocus));

    // Enter picks the focused color: the popover closes, the focus returns, the color becomes the first recent one
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Right);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    QVERIFY(!button->isPaletteVisible());
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(button->getColor(), QColor(QStringLiteral("#2a2a2a")));
    QCOMPARE(button->getShownName(), QStringLiteral("Color 2"));
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Color 2"));
    QCOMPARE(palette->getRecentColors().front().color, QColor(QStringLiteral("#2a2a2a")));
    QCOMPARE(palette->getRecentColors().size(), 4);
    QTRY_VERIFY(button->hasFocus());

    // Opened again: the new color is selected; Esc closes, the color stays
    QVERIFY(button->showPalette());
    QVERIFY(palette->getSuggestedSwatch(1)->isSelected());
    QCOMPARE(QApplication::focusWidget(), palette->getSuggestedSwatch(1));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QVERIFY(!button->isPaletteVisible());
    QCOMPARE(selectedSpy.count(), 1);
    QTRY_VERIFY(button->hasFocus());

    // A click on a recent color, a color of the same value is not a change
    QVERIFY(button->showPalette());
    QTest::mouseClick(palette->getRecentSwatch(0), Qt::LeftButton);
    QCOMPARE(selectedSpy.count(), 2);
    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(!button->isPaletteVisible());

    // More colors…
    QSignalSpy moreSpy(button, &pdf::PDFColorButton::moreColorsRequested);
    QVERIFY(button->showPalette());
    QTest::mouseClick(palette->getMoreColorsLink(), Qt::LeftButton);
    QCOMPARE(moreSpy.count(), 1);
    QVERIFY(!button->isPaletteVisible());

    // A click on the button closes the open palette, it does not open it again
    // (also a click into the shadow of the popover, which covers the button)
    for (const QPoint& point : { QPoint(130, 5), QPoint(130, 30) })
    {
        QVERIFY(button->showPalette());
        // Through the window: the system delivers the events to the popup
        QTest::mouseClick(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, button->mapTo(&window, point));
        QVERIFY2(!button->isPaletteVisible(), qPrintable(QStringLiteral("%1, %2").arg(point.x()).arg(point.y())));
        QApplication::processEvents();
        QVERIFY(!button->isPaletteVisible());
    }

    // A click elsewhere closes the palette
    QVERIFY(button->showPalette());
    QTest::mouseClick(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, QPoint(window.width() - 5, window.height() - 5));
    QVERIFY(!button->isPaletteVisible());

    // Keys of a combo box open the palette
    button->setFocus(Qt::TabFocusReason);
    QTest::keyClick(button, Qt::Key_Down, Qt::AltModifier);
    QVERIFY(button->isPaletteVisible());
    palette->close();
    QTest::keyClick(button, Qt::Key_F4);
    QVERIFY(button->isPaletteVisible());
    palette->close();
    QTest::keyClick(button, Qt::Key_Space);
    QVERIFY(button->isPaletteVisible());
    palette->close();

    // The owner window hides: the popover closes; a disabled button does not open it
    QVERIFY(button->showPalette());
    window.hide();
    QVERIFY(!button->isPaletteVisible());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(button->showPalette());
    button->setEnabled(false);
    QVERIFY(!button->isPaletteVisible());
    QVERIFY(!button->showPalette());
    button->setEnabled(true);

    // The palette deleted while it is open: the button creates a new one
    QVERIFY(button->showPalette());
    delete palette;
    QVERIFY(!button->isPaletteVisible());
    QVERIFY(!button->getContent().open);
    pdf::PDFColorPalette* newPalette = button->getPalette();
    QVERIFY(newPalette);
    QVERIFY(button->showPalette());
    QVERIFY(button->isPaletteVisible());

    // The button deleted while its palette is open
    QPointer<pdf::PDFColorPalette> guard(newPalette);
    delete button;
    QVERIFY(!guard);
    QCOMPARE(QApplication::activePopupWidget(), nullptr);

    // A slot, which deletes the button, does not break it
    QPointer<pdf::PDFColorButton> deleted = new pdf::PDFColorButton(QColor(Qt::red), QString(), &window);
    deleted->show();
    deleted->getPalette()->setSuggestedColors(PanelSamples::suggested());
    connect(deleted.data(), &pdf::PDFColorButton::colorSelected, &window, [&deleted]() { delete deleted.data(); });
    QVERIFY(deleted->showPalette());
    deleted->getPalette()->getSuggestedSwatch(3)->click();
    QVERIFY(!deleted);

    // Names: a name of the palette, a hexadecimal value, no color
    pdf::PDFColorButton plain(QColor(QStringLiteral("#123456")));
    QCOMPARE(plain.getShownName(), QStringLiteral("#123456"));
    plain.setColor(QColor());
    QCOMPARE(plain.getShownName(), QStringLiteral("No color"));
    plain.setColor(QColor(0x10, 0x20, 0x30, 0x80));
    QCOMPARE(plain.getShownName(), QStringLiteral("#80102030"));
    QVERIFY(!plain.showPalette());
}

void FluentComponentsTest::colorPaletteDestroyed()
{
    // Regression (valgrind memcheck on Linux): QWidget emits destroyed after the
    // destructor of the palette has deleted its colors, but before QObject clears
    // the pointers to it; the button asked the deleted lists for the name of its
    // color. The button must drop the palette and show the default name.
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(20, 20, 20, 20);
    pdf::PDFColorButton* button = new pdf::PDFColorButton(QColor(QStringLiteral("#00ff7f")), QString(), &window);
    QWidget* container = new QWidget(&window);
    QVBoxLayout* containerLayout = new QVBoxLayout(container);
    pdf::PDFColorButton* nested = new pdf::PDFColorButton(QColor(QStringLiteral("#111111")), QString(), container);
    containerLayout->addWidget(nested);
    layout->addWidget(button);
    layout->addWidget(container);
    layout->addStretch(1);
    window.resize(400, 500);
    QSignalSpy changedSpy(button, &pdf::PDFColorButton::colorChanged);

    // The name comes from the palette; the palette deleted while it is closed
    QPointer<pdf::PDFColorPalette> palette = button->getPalette();
    palette->setSuggestedColors(PanelSamples::suggested());
    QCOMPARE(button->text(), QStringLiteral("Terminal green"));
    delete palette.data();
    QVERIFY(!palette);
    QCOMPARE(button->text(), QStringLiteral("#00FF7F"));
    QCOMPARE(button->getShownName(), QStringLiteral("#00FF7F"));
    QVERIFY(!button->isPaletteVisible());
    QVERIFY(!button->getContent().open);
    QCOMPARE(QAccessible::queryAccessibleInterface(button)->text(QAccessible::Name), QStringLiteral("#00FF7F"));

    // A new palette gives the name again; deleted later (deleteLater) while it is open
    palette = button->getPalette();
    QVERIFY(palette);
    palette->setSuggestedColors(PanelSamples::suggested());
    QCOMPARE(button->text(), QStringLiteral("Terminal green"));
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QVERIFY(button->showPalette());
    QCOMPARE(QApplication::activePopupWidget(), palette.data());
    palette->deleteLater();
    QTRY_VERIFY(!palette);
    QCOMPARE(QApplication::activePopupWidget(), nullptr);
    QCOMPARE(button->text(), QStringLiteral("#00FF7F"));
    QVERIFY(!button->isPaletteVisible());
    QVERIFY(!button->getContent().open);
    QApplication::processEvents();

    // Deleted directly while it is open (the queued signal closed of the hide is dropped)
    palette = button->getPalette();
    palette->setSuggestedColors(PanelSamples::suggested());
    QVERIFY(button->showPalette());
    delete palette.data();
    QVERIFY(!palette);
    QCOMPARE(QApplication::activePopupWidget(), nullptr);
    QCOMPARE(button->text(), QStringLiteral("#00FF7F"));
    QApplication::processEvents();
    QCOMPARE(button->text(), QStringLiteral("#00FF7F"));
    QCOMPARE(changedSpy.count(), 0);

    // The button works with a new palette
    palette = button->getPalette();
    QVERIFY(button->showPalette());
    QVERIFY(button->isPaletteVisible());
    button->hidePalette();
    QVERIFY(!button->isPaletteVisible());

    // A button deleted with its parent while its palette is open: the palette
    // is deleted by the button, while the button is whole
    QPointer<pdf::PDFColorPalette> nestedPalette = nested->getPalette();
    nestedPalette->setSuggestedColors(PanelSamples::suggested());
    QVERIFY(nested->showPalette());
    QCOMPARE(QApplication::activePopupWidget(), nestedPalette.data());
    QPointer<pdf::PDFColorButton> nestedGuard(nested);
    delete container;
    QVERIFY(!nestedGuard);
    QVERIFY(!nestedPalette);
    QCOMPARE(QApplication::activePopupWidget(), nullptr);
    QApplication::processEvents();

    // The window deleted while the palette of its button is open
    QWidget* other = new QWidget();
    QVBoxLayout* otherLayout = new QVBoxLayout(other);
    pdf::PDFColorButton* otherButton = new pdf::PDFColorButton(QColor(QStringLiteral("#2563eb")), QString(), other);
    otherLayout->addWidget(otherButton);
    other->resize(300, 300);
    other->show();
    other->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(other));
    QPointer<pdf::PDFColorPalette> otherPalette = otherButton->getPalette();
    QVERIFY(otherButton->showPalette());
    delete other;
    QVERIFY(!otherPalette);
    QCOMPARE(QApplication::activePopupWidget(), nullptr);
    QApplication::processEvents();
}

void FluentComponentsTest::colorPaletteDetails()
{
    // An inline palette (not a popover): no margins, popup does nothing
    pdf::PDFColorPalette palette;
    QVERIFY(!palette.isPopup());
    QCOMPARE(palette.sizeHint(), QSize(232, 2 * 12 + 16));
    palette.setSuggestedColors(PanelSamples::suggested());
    palette.setRecentColors(PanelSamples::recent());
    QCOMPARE(palette.sizeHint(), QSize(232, 209));
    QWidget anchor;
    anchor.show();
    palette.popup(&anchor);
    QVERIFY(!palette.isVisible());
    palette.popup(nullptr);

    // A popover anchored to a foreign widget closes, when the widget is deleted
    QWidget host;
    host.resize(300, 300);
    QWidget* foreignAnchor = new QWidget(&host);
    foreignAnchor->setGeometry(10, 10, 100, 30);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    pdf::PDFColorPalette* popover = new pdf::PDFColorPalette(&host, Qt::Popup);
    popover->setSuggestedColors(PanelSamples::suggested());
    popover->popup(foreignAnchor);
    QVERIFY(popover->isVisible());
    QSignalSpy closedSpy(popover, &pdf::PDFColorPalette::closed);
    delete foreignAnchor;
    QVERIFY(!popover->isVisible());
    QCOMPARE(closedSpy.count(), 0);
    QTRY_COMPARE(closedSpy.count(), 1);
    QWidget* hiddenAnchor = new QWidget(&host);
    hiddenAnchor->hide();
    popover->popup(hiddenAnchor);
    QVERIFY(!popover->isVisible());
    popover->popup(popover);
    QVERIFY(!popover->isVisible());
    delete popover;

    // Limits and duplicates
    QList<pdf::PDFColorPalette::Entry> many;
    for (int i = 0; i < 100; ++i)
    {
        many << pdf::PDFColorPalette::Entry{ QColor::fromHsv(i * 3, 200, 200), QString() };
    }
    palette.setSuggestedColors(many);
    QCOMPARE(palette.getSuggestedColors().size(), pdf::PDFColorPalette::MAX_SUGGESTED_COLORS);
    QCOMPARE(palette.sizeHint().height(), 12 + 13 + 10 + 8 * 36 - 8 + 10 + 13 + 10 + 28 + 10 + 1 + 10 + 16 + 12);
    palette.setRecentColors(many);
    QCOMPARE(palette.getRecentColors().size(), pdf::PDFColorPalette::MAX_RECENT_COLORS);
    palette.setRecentColors({ { Qt::red, QStringLiteral("Red") }, { Qt::red, QStringLiteral("Other red") }, { QColor(), QString() }, { QColor(), QString() } });
    QCOMPARE(palette.getRecentColors().size(), 2);
    palette.addRecentColor({ QColor(), QStringLiteral("None") });
    QCOMPARE(palette.getRecentColors().size(), 2);
    QCOMPARE(palette.getRecentColors().front().name, QStringLiteral("None"));
    QCOMPARE(palette.getRecentSwatch(0)->getKind(), pdf::PDFColorSwatch::Kind::None);
    QCOMPARE(palette.getRecentSwatch(2), nullptr);
    QCOMPARE(palette.getSuggestedSwatch(-1), nullptr);
    QCOMPARE(palette.getSuggestedSwatch(48), nullptr);

    // Names
    palette.setSuggestedColors(PanelSamples::suggested());
    QCOMPARE(palette.getColorName(QColor(QStringLiteral("#00ff7f"))), QStringLiteral("Terminal green"));
    QCOMPARE(palette.getColorName(Qt::red), QStringLiteral("Red"));
    QCOMPARE(palette.getColorName(QColor(1, 2, 3)), QStringLiteral("#010203"));
    QCOMPARE(palette.getColorName(QColor()), QStringLiteral("None"));
    QCOMPARE(QAccessible::queryAccessibleInterface(palette.getSuggestedSwatch(0))->text(QAccessible::Name), QStringLiteral("Black"));

    // The current color selects the first suggested swatch of the same value
    palette.setCurrentColor(QColor(QStringLiteral("#00ff7f")));
    QVERIFY(palette.getSuggestedSwatch(6)->isSelected());
    QCOMPARE(palette.getContent().selected, 6);
    palette.setCurrentColor(QColor(1, 2, 3));
    QCOMPARE(palette.getContent().selected, -1);
    for (int i = 0; i < 12; ++i)
    {
        QVERIFY(!palette.getSuggestedSwatch(i)->isSelected());
    }

    // An inline palette selects a color without closing
    QSignalSpy spy(&palette, &pdf::PDFColorPalette::colorSelected);
    palette.resize(palette.sizeHint());
    palette.getSuggestedSwatch(4)->click();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(palette.getCurrentColor(), QColor(QStringLiteral("#cfd4da")));
    QVERIFY(palette.getSuggestedSwatch(4)->isSelected());
    QCOMPARE(palette.getRecentColors().front().name, QStringLiteral("Color 5"));

    // The widget draws as the painter with the nested swatches and link, also right-to-left and at the scales
    palette.setRecentColors(PanelSamples::recent());
    palette.setCurrentColor(QColor(QStringLiteral("#00ff7f")));
    palette.resize(palette.sizeHint());
    palette.show();
    QVERIFY(QTest::qWaitForWindowExposed(&palette));
    if (QWidget* focused = QApplication::focusWidget())
    {
        // No focus ring of the focus frame in the comparison
        focused->clearFocus();
    }
    QApplication::processEvents();
    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        palette.setLayoutDirection(direction);
        for (const qreal scale : { 1.0, 1.5, 2.0 })
        {
            Painter::ColorPalette content = palette.getContent();
            content.paintNested = true;
            const QString difference = PanelSamples::compareScaled(renderWidget(&palette, scale),
                                                                   renderInWidget([&](QPainter& painter) { Painter::paintColorPalette(&painter, QRectF(0, 0, 232, 209), content, state(QStyle::State_None, direction)); }, QSize(232, 209), scale),
                                                                   scale, QStringLiteral("Palette"));
            QVERIFY2(difference.isEmpty(), qPrintable(difference));
        }
    }

    // Layout of the design: labels, two rows of six, the recent row, the divider, the link
    const Painter::ColorPaletteLayout layout = Painter::getColorPaletteLayout(palette.getContent());
    QCOMPARE(layout.suggestedLabel, QRect(12, 12, 208, 13));
    QCOMPARE(layout.suggested.size(), 12);
    QCOMPARE(layout.suggested.at(0), QRect(12, 35, 28, 28));
    QCOMPARE(layout.suggested.at(5), QRect(192, 35, 28, 28));
    QCOMPARE(layout.suggested.at(6), QRect(12, 71, 28, 28));
    QCOMPARE(layout.recentLabel, QRect(12, 109, 208, 13));
    QCOMPARE(layout.recent.at(2), QRect(84, 132, 28, 28));
    QCOMPARE(layout.divider, QRect(12, 170, 208, 1));
    QCOMPARE(layout.more.topLeft(), QPoint(12, 181));
    QCOMPARE(layout.size, QSize(232, 209));

    // Empty palette: only the link
    const Painter::ColorPalette empty{ {}, {}, -1, QString(), QString(), QStringLiteral("More colors…"), true };
    QCOMPARE(Painter::getColorPaletteSize(empty), QSize(232, 40));
    Painter::ColorPalette nothing;
    QCOMPARE(Painter::getColorPaletteSize(nothing), QSize(232, 24));
}

void FluentComponentsTest::colorSwatchRow()
{
    using Row = pdf::PDFColorSwatchRow;
    using Option = Row::Option;
    using Scheme = pdf::PDFThemeColors::Scheme;

    // The colors of the design in their order, the canonical values of the light theme
    static constexpr Color order[] = { Color::AnnotationYellow, Color::AnnotationOrange, Color::AnnotationRed, Color::AnnotationPink,
                                       Color::AnnotationPurple, Color::AnnotationBlue, Color::AnnotationGreen, Color::AnnotationBlack };
    const QStringList names = { QStringLiteral("Yellow"), QStringLiteral("Orange"), QStringLiteral("Red"), QStringLiteral("Pink"),
                                QStringLiteral("Purple"), QStringLiteral("Blue"), QStringLiteral("Green"), QStringLiteral("Black") };
    auto canonical = [](Color color) { return QColor(QString::fromLatin1(pdf::PDFThemeColors::getBuiltinTokenValue(color, Scheme::Light))); };
    const std::vector<Row::Entry> defaults = Row::getAnnotationColors(Option::More);
    QCOMPARE(defaults.size(), size_t(8));
    for (int i = 0; i < 8; ++i)
    {
        QCOMPARE(defaults[i].token, order[i]);
        QCOMPARE(defaults[i].canonicalColor, canonical(order[i]));
        QCOMPARE(defaults[i].name, names[i]);
    }
    const std::vector<Row::Entry> fill = Row::getAnnotationColors(Option::NoColor | Option::White | Option::More);
    QCOMPARE(fill.size(), size_t(10));
    QCOMPARE(fill.front().token, Color::ColorCount);
    QVERIFY(!fill.front().canonicalColor.isValid());
    QCOMPARE(fill.front().name, QStringLiteral("No color"));
    QCOMPARE(fill.back().token, Color::AnnotationWhite);
    QCOMPARE(fill.back().canonicalColor, QColor(QStringLiteral("#ffffff")));
    QCOMPARE(fill.back().name, QStringLiteral("White"));
    QCOMPARE(fill[1].token, Color::AnnotationYellow);

    // Sizes of a row
    QCOMPARE(Row::getRowSize(0, false), QSize(0, 0));
    QCOMPARE(Row::getRowSize(3, false), QSize(88, 28));
    QCOMPARE(Row::getRowSize(0, true), QSize(32, 32));
    QCOMPARE(Row::getRowSize(-4, true), QSize(32, 32));

    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(20, 20, 20, 20);
    QLineEdit* before = new QLineEdit(&window);
    Row* row = new Row(&window);
    Row* fillRow = new Row(&window);
    QLineEdit* after = new QLineEdit(&window);
    layout->addWidget(before);
    layout->addWidget(row);
    layout->addWidget(fillRow);
    layout->addWidget(after);
    layout->addStretch(1);
    window.resize(500, 400);

    // Geometry against Figma 877:10188: 8 swatches 28 with gaps 2, More 32 x 32, 272 x 32
    QCOMPARE(row->getOptions(), Row::Options(Option::More));
    QCOMPARE(row->getSwatchCount(), 8);
    QCOMPARE(row->sizeHint(), QSize(272, 32));
    QCOMPARE(row->minimumSizeHint(), QSize(272, 32));
    QVERIFY(row->getMoreButton());
    QVERIFY(!row->getSwatch(-1));
    QVERIFY(!row->getSwatch(8));
    QVERIFY(!row->getEntry(8).canonicalColor.isValid());
    QCOMPARE(row->getEntry(2).name, QStringLiteral("Red"));

    // Fill of a shape (207:7199): none first, white after black, 332 x 32
    fillRow->setOptions(Option::NoColor | Option::White | Option::More);
    QCOMPARE(fillRow->getSwatchCount(), 10);
    QCOMPARE(fillRow->sizeHint(), QSize(332, 32));
    QCOMPARE(fillRow->getSwatch(0)->getKind(), pdf::PDFColorSwatch::Kind::None);
    QCOMPARE(fillRow->getSwatch(9)->getKind(), pdf::PDFColorSwatch::Kind::Color);

    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QCOMPARE(row->size(), QSize(272, 32));
    for (int i = 0; i < 8; ++i)
    {
        QCOMPARE(row->getSwatch(i)->geometry(), QRect(i * 30, 2, 28, 28));
        QCOMPARE(row->getSwatch(i)->getColor(), token(order[i]));
    }
    QCOMPARE(row->getMoreButton()->geometry(), QRect(240, 0, 32, 32));
    QCOMPARE(fillRow->size(), QSize(332, 32));
    QCOMPARE(fillRow->getSwatch(9)->geometry(), QRect(270, 2, 28, 28));
    QCOMPARE(fillRow->getMoreButton()->geometry(), QRect(300, 0, 32, 32));

    // setColor never emits; the canonical value, rgb compared; custom colors; none
    QSignalSpy selectedSpy(row, &Row::colorSelected);
    QSignalSpy fillSpy(fillRow, &Row::colorSelected);
    QVERIFY(!row->getColor().isValid());
    QVERIFY(!row->isNoColorSelected());
    for (int i = 0; i < 8; ++i)
    {
        QVERIFY(!row->getSwatch(i)->isSelected());
    }
    row->setColor(QColor(QStringLiteral("#ff3b30")));
    QCOMPARE(row->getColor(), canonical(Color::AnnotationRed));
    QVERIFY(row->getSwatch(2)->isSelected());
    row->setColor(QColor(0xff, 0x3b, 0x30, 0x80));
    QVERIFY(row->getSwatch(2)->isSelected());
    QCOMPARE(row->getColor(), canonical(Color::AnnotationRed));
    QCOMPARE(row->getColor().alpha(), 255);
    row->setColor(QColor(QStringLiteral("#123456")));
    QCOMPARE(row->getColor(), QColor(QStringLiteral("#123456")));
    for (int i = 0; i < 8; ++i)
    {
        QVERIFY(!row->getSwatch(i)->isSelected());
    }
    row->setColor(QColor());
    QVERIFY(!row->isNoColorSelected());
    QVERIFY(!row->getColor().isValid());
    fillRow->setColor(QColor());
    QVERIFY(fillRow->isNoColorSelected());
    QVERIFY(!fillRow->getColor().isValid());
    QVERIFY(fillRow->getSwatch(0)->isSelected());
    fillRow->setColor(QColor(Qt::white));
    QVERIFY(fillRow->getSwatch(9)->isSelected());
    QVERIFY(!fillRow->isNoColorSelected());
    QCOMPARE(fillRow->getColor(), QColor(QStringLiteral("#ffffff")));
    QCOMPARE(selectedSpy.count(), 0);
    QCOMPARE(fillSpy.count(), 0);

    // A theme with other annotation colors: the swatches show it, the row keeps the canonical values
    pdf::PDFThemeManager* themeManager = pdf::PDFThemeManager::getInstance();
    pdf::PDFThemeColors custom(QStringLiteral("swatch-row-test"), QStringLiteral("Swatch row test"), Scheme::Dark);
    custom.setColor(Color::AnnotationYellow, QColor(QStringLiteral("#806000")));
    themeManager->registerTheme(custom);
    QVERIFY(themeManager->setTheme(QStringLiteral("swatch-row-test")));
    QCOMPARE(row->getSwatch(0)->getColor(), QColor(QStringLiteral("#806000")));
    QVERIFY(isNear(pixel(renderWidget(row, 1.0), 14, 16), QColor(QStringLiteral("#806000"))));
    row->setColor(QColor(QStringLiteral("#ffd60a")));
    QVERIFY(row->getSwatch(0)->isSelected());
    QCOMPARE(row->getColor(), QColor(QStringLiteral("#ffd60a")));
    row->setColor(QColor(QStringLiteral("#806000")));
    QVERIFY(!row->getSwatch(0)->isSelected());
    QTest::mouseClick(row->getSwatch(0), Qt::LeftButton);
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(selectedSpy.takeFirst().at(0).value<QColor>(), QColor(QStringLiteral("#ffd60a")));
    QCOMPARE(row->getColor(), QColor(QStringLiteral("#ffd60a")));
    themeManager->setScheme(Scheme::Dark);
    QCOMPARE(row->getSwatch(0)->getColor(), token(Color::AnnotationYellow));
    QCOMPARE(row->getColor(), canonical(Color::AnnotationYellow));
    themeManager->setScheme(Scheme::Light);

    // A click selects (the selected swatch again too: the user has chosen it), only the user emits
    QTest::mouseClick(row->getSwatch(5), Qt::LeftButton);
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(selectedSpy.takeFirst().at(0).value<QColor>(), canonical(Color::AnnotationBlue));
    QVERIFY(row->getSwatch(5)->isSelected());
    QVERIFY(!row->getSwatch(0)->isSelected());
    QTest::mouseClick(row->getSwatch(5), Qt::LeftButton);
    QCOMPARE(selectedSpy.count(), 1);
    QVERIFY(row->getSwatch(5)->isSelected());
    selectedSpy.clear();
    QTest::mouseClick(fillRow->getSwatch(0), Qt::LeftButton);
    QCOMPARE(fillSpy.count(), 1);
    QVERIFY(!fillSpy.takeFirst().at(0).value<QColor>().isValid());
    QVERIFY(fillRow->isNoColorSelected());

    // Keyboard: the swatches are one stop of Tab (the selected one), More the next stop
    auto tabStops = [](Row* tested)
    {
        int count = 0;
        for (int i = 0; i < tested->getSwatchCount(); ++i)
        {
            if (tested->getSwatch(i) && (tested->getSwatch(i)->focusPolicy() & Qt::TabFocus))
            {
                ++count;
            }
        }
        return count;
    };
    QCOMPARE(tabStops(row), 1);
    QVERIFY(row->getSwatch(5)->focusPolicy() & Qt::TabFocus);
    window.setAttribute(Qt::WA_KeyboardFocusChange, true);
    before->setFocus(Qt::TabFocusReason);
    QTest::keyClick(before, Qt::Key_Tab);
    QCOMPARE(QApplication::focusWidget(), row->getSwatch(5));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
    QCOMPARE(QApplication::focusWidget(), row->getMoreButton());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
    QCOMPARE(QApplication::focusWidget(), fillRow->getSwatch(0));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Backtab);
    QCOMPARE(QApplication::focusWidget(), row->getMoreButton());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Backtab);
    QCOMPARE(QApplication::focusWidget(), row->getSwatch(5));

    // The arrows, Home and End move the focus, they do not select
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Right);
    QCOMPARE(QApplication::focusWidget(), row->getSwatch(6));
    QVERIFY(row->getSwatch(5)->isSelected());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_End);
    QCOMPARE(QApplication::focusWidget(), row->getSwatch(7));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Right);
    QCOMPARE(QApplication::focusWidget(), row->getSwatch(7));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Home);
    QCOMPARE(QApplication::focusWidget(), row->getSwatch(0));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Left);
    QCOMPARE(QApplication::focusWidget(), row->getSwatch(0));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
    QCOMPARE(QApplication::focusWidget(), row->getSwatch(1));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Up);
    QCOMPARE(QApplication::focusWidget(), row->getSwatch(0));
    QCOMPARE(tabStops(row), 1);
    QVERIFY(row->getSwatch(0)->focusPolicy() & Qt::TabFocus);
    QCOMPARE(selectedSpy.count(), 0);
    QVERIFY(row->getSwatch(5)->isSelected());

    // Space and Enter select the focused swatch
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space);
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(selectedSpy.takeFirst().at(0).value<QColor>(), canonical(Color::AnnotationYellow));
    QVERIFY(row->getSwatch(0)->isSelected());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Right);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(selectedSpy.takeFirst().at(0).value<QColor>(), canonical(Color::AnnotationOrange));
    QVERIFY(row->getSwatch(1)->isSelected());

    // Accessibility: radio buttons named by the colors, the row "Colors", More "More colors"
    for (int i = 0; i < 8; ++i)
    {
        QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(row->getSwatch(i));
        QVERIFY(accessible);
        QCOMPARE(accessible->role(), QAccessible::RadioButton);
        QCOMPARE(accessible->text(QAccessible::Name), names[i]);
    }
    QCOMPARE(QAccessible::queryAccessibleInterface(fillRow->getSwatch(0))->text(QAccessible::Name), QStringLiteral("No color"));
    QCOMPARE(QAccessible::queryAccessibleInterface(fillRow->getSwatch(9))->text(QAccessible::Name), QStringLiteral("White"));
    QCOMPARE(QAccessible::queryAccessibleInterface(row->getMoreButton())->text(QAccessible::Name), QStringLiteral("More colors"));
    QCOMPARE(row->accessibleName(), QStringLiteral("Colors"));
    row->setAccessibleName(QStringLiteral("Highlight color"));
    QEvent languageChange(QEvent::LanguageChange);
    QApplication::sendEvent(row, &languageChange);
    QCOMPARE(row->accessibleName(), QStringLiteral("Highlight color"));
    QVERIFY(row->getSwatch(1)->isSelected());
    QCOMPARE(QAccessible::queryAccessibleInterface(row->getSwatch(3))->text(QAccessible::Name), QStringLiteral("Pink"));

    // Right-to-left: the first color at the right, More at the left, the arrows by the picture
    row->setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(row->getSwatch(0)->geometry(), QRect(244, 2, 28, 28));
    QCOMPARE(row->getSwatch(7)->geometry(), QRect(34, 2, 28, 28));
    QCOMPARE(row->getMoreButton()->geometry(), QRect(0, 0, 32, 32));
    row->getSwatch(1)->setFocus(Qt::TabFocusReason);
    QTest::keyClick(row->getSwatch(1), Qt::Key_Right);
    QCOMPARE(QApplication::focusWidget(), row->getSwatch(0));
    QTest::keyClick(row->getSwatch(0), Qt::Key_Left);
    QCOMPARE(QApplication::focusWidget(), row->getSwatch(1));
    row->setLayoutDirection(Qt::LeftToRight);
    QCOMPARE(row->getSwatch(0)->geometry(), QRect(0, 2, 28, 28));

    // Visible limit: the swatches over it are hidden, More stays; the hidden color stays selectable by setColor
    row->setVisibleSwatchLimit(4);
    QCOMPARE(row->getVisibleSwatchLimit(), 4);
    QCOMPARE(row->getVisibleSwatchCount(), 4);
    QCOMPARE(row->sizeHint(), QSize(152, 32));
    QTRY_COMPARE(row->size(), QSize(152, 32));
    QCOMPARE(row->getSwatchCount(), 8);
    for (int i = 0; i < 8; ++i)
    {
        QCOMPARE(row->getSwatch(i)->isHidden(), i >= 4);
    }
    QCOMPARE(row->getMoreButton()->geometry(), QRect(120, 0, 32, 32));
    row->getSwatch(0)->setFocus(Qt::TabFocusReason);
    QTest::keyClick(row->getSwatch(0), Qt::Key_End);
    QCOMPARE(QApplication::focusWidget(), row->getSwatch(3));
    row->setColor(canonical(Color::AnnotationGreen));
    QVERIFY(row->getSwatch(6)->isSelected());
    QCOMPARE(row->getColor(), canonical(Color::AnnotationGreen));
    QCOMPARE(tabStops(row), 1);
    row->setVisibleSwatchLimit(-5);
    QCOMPARE(row->getVisibleSwatchLimit(), -1);
    QCOMPARE(row->getVisibleSwatchCount(), 8);
    QCOMPARE(row->sizeHint(), QSize(272, 32));
    row->setVisibleSwatchLimit(0);
    QCOMPARE(row->sizeHint(), QSize(32, 32));
    row->setVisibleSwatchLimit(-1);

    // Without More: the swatches alone, 28 high
    row->setOptions(Row::Options());
    QVERIFY(!row->getMoreButton());
    QCOMPARE(row->sizeHint(), QSize(238, 28));
    QVERIFY(!row->showPalette());
    QVERIFY(row->getSwatch(6)->isSelected());
    row->setOptions(Option::More);
    QVERIFY(row->getMoreButton());
    QCOMPARE(row->sizeHint(), QSize(272, 32));

    // Options dropping "No color" drop its selection
    fillRow->setColor(QColor());
    QVERIFY(fillRow->isNoColorSelected());
    fillRow->setOptions(Option::White | Option::More);
    QVERIFY(!fillRow->isNoColorSelected());
    QCOMPARE(fillRow->getSwatchCount(), 9);
    for (int i = 0; i < 9; ++i)
    {
        QVERIFY(!fillRow->getSwatch(i)->isSelected());
    }
    QCOMPARE(fillSpy.count(), 0);

    // Pixel probes in both themes at the device pixel ratios 1, 1.5 and 2: the
    // colors, the selection ring of the selected swatch, the gaps, More, no leak color
    fillRow->setOptions(Option::NoColor | Option::White | Option::More);
    fillRow->setColor(canonical(Color::AnnotationRed));
    QTRY_COMPARE(fillRow->size(), QSize(332, 32));
    for (const char* theme : { "light", "dark" })
    {
        QVERIFY(themeManager->setTheme(QString::fromLatin1(theme)));
        const QColor surface = token(Color::BgSurface);
        for (const qreal scale : { 1.0, 1.5, 2.0 })
        {
            const QString where = QStringLiteral("%1 %2").arg(QString::fromLatin1(theme)).arg(scale);
            const QImage image = renderWidget(fillRow, scale);
            QVERIFY2(countLeakPixels(image) == 0, qPrintable(where));
            for (int i = 1; i < 10; ++i)
            {
                const Color color = i < 9 ? order[i - 1] : Color::AnnotationWhite;
                QVERIFY2(isNear(pixel(image, i * 30 + 14, 16), token(color)), qPrintable(where));
            }
            // "No color": the surface crossed by the slash icon/danger
            QVERIFY2(colorDistance(strongestInk(image, QRect(6, 8, 16, 16), surface), token(Color::IconDanger)) < 80, qPrintable(where));
            // The selected swatch (red, index 3) has the ring bg/accent inside, the others none
            QVERIFY2(isNear(pixel(image, 90 + 14, 3), token(Color::BgAccent), 40), qPrintable(where));
            QVERIFY2(isNear(pixel(image, 60 + 14, 3), surface, 3), qPrintable(where));
            // The gaps are the background, More has its icon
            QVERIFY2(isNear(pixel(image, 29, 16), surface, 3), qPrintable(where));
            QVERIFY2(isNear(pixel(image, 299, 16), surface, 3), qPrintable(where));
            QVERIFY2(colorDistance(strongestInk(image, QRect(300, 0, 32, 32), surface), token(Color::IconDefault)) < 80, qPrintable(where));
        }
    }
    themeManager->setScheme(Scheme::Light);

    // Defensive: hostile limits and colors
    Row hostile;
    hostile.setVisibleSwatchLimit(std::numeric_limits<int>::max());
    QCOMPARE(hostile.getVisibleSwatchCount(), 8);
    hostile.setVisibleSwatchLimit(std::numeric_limits<int>::min());
    QCOMPARE(hostile.getVisibleSwatchLimit(), -1);
    hostile.setColor(QColor(0x12, 0x34, 0x56, 0));
    QVERIFY(hostile.getColor().isValid());
    QVERIFY(!hostile.getSwatch(7)->isSelected());
    // The rgb values are compared: a transparent black is black, reported opaque
    hostile.setColor(QColor::fromRgba(0));
    QVERIFY(hostile.getSwatch(7)->isSelected());
    QCOMPARE(hostile.getColor(), QColor(Qt::black));
}

void FluentComponentsTest::colorSwatchRowPalette()
{
    using Row = pdf::PDFColorSwatchRow;
    using Option = Row::Option;
    using Scheme = pdf::PDFThemeColors::Scheme;
    auto canonical = [](Color color) { return QColor(QString::fromLatin1(pdf::PDFThemeColors::getBuiltinTokenValue(color, Scheme::Light))); };

    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(20, 20, 20, 20);
    Row* row = new Row(&window);
    QLineEdit* other = new QLineEdit(&window);
    layout->addWidget(row);
    layout->addWidget(other);
    layout->addStretch(1);
    window.resize(500, 500);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QSignalSpy selectedSpy(row, &Row::colorSelected);
    row->setColor(canonical(Color::AnnotationRed));

    // More opens the palette 4 px below it, with the colors of the row as the suggested ones
    QVERIFY(!row->isPaletteVisible());
    QTest::mouseClick(row->getMoreButton(), Qt::LeftButton);
    QVERIFY(row->isPaletteVisible());
    pdf::PDFColorPalette* palette = row->getPalette();
    QVERIFY(palette->isPopup());
    QCOMPARE(palette->parentWidget(), row);
    QCOMPARE(QApplication::activePopupWidget(), palette);
    QCOMPARE(palette->getSuggestedColors().size(), 8);
    for (int i = 0; i < 8; ++i)
    {
        QCOMPARE(palette->getSuggestedColors().at(i).color, row->getEntry(i).canonicalColor);
        QCOMPARE(palette->getSuggestedColors().at(i).name, row->getEntry(i).name);
    }
    const QMargins margins = palette->testAttribute(Qt::WA_TranslucentBackground) ? QMargins(24, 16, 24, 32) : QMargins();
    QAbstractButton* more = row->getMoreButton();
    QCOMPARE(palette->mapToGlobal(QPoint(margins.left(), margins.top())), more->mapToGlobal(QPoint(0, more->height() + 4)));
    QVERIFY(palette->getSuggestedSwatch(2)->isSelected());
    QCOMPARE(QApplication::focusWidget(), palette->getSuggestedSwatch(2));
    QCOMPARE(countLeakPixels(renderWidget(palette, 1.0)), 0);

    // A suggested color picked by the keyboard: emitted once, the swatch of the row selected, the focus back on More
    window.setAttribute(Qt::WA_KeyboardFocusChange, true);
    for (int i = 0; i < 3; ++i)
    {
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Right);
    }
    QCOMPARE(QApplication::focusWidget(), palette->getSuggestedSwatch(5));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    QVERIFY(!row->isPaletteVisible());
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(selectedSpy.takeFirst().at(0).value<QColor>(), canonical(Color::AnnotationBlue));
    QVERIFY(row->getSwatch(5)->isSelected());
    QCOMPARE(row->getColor(), canonical(Color::AnnotationBlue));
    QTRY_VERIFY(more->hasFocus());

    // A custom color (a recent color of the palette): emitted as chosen, no swatch selected
    palette->setRecentColors({ pdf::PDFColorPalette::Entry{ QColor(QStringLiteral("#123456")), QStringLiteral("Navy") } });
    QVERIFY(row->showPalette());
    QTest::mouseClick(palette->getRecentSwatch(0), Qt::LeftButton);
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(selectedSpy.takeFirst().at(0).value<QColor>(), QColor(QStringLiteral("#123456")));
    QCOMPARE(row->getColor(), QColor(QStringLiteral("#123456")));
    for (int i = 0; i < 8; ++i)
    {
        QVERIFY(!row->getSwatch(i)->isSelected());
    }

    // Opened again: no suggested color is selected; Esc closes, nothing is emitted
    QVERIFY(row->showPalette());
    for (int i = 0; i < 8; ++i)
    {
        QVERIFY(!palette->getSuggestedSwatch(i)->isSelected());
    }
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QVERIFY(!row->isPaletteVisible());
    QCOMPARE(selectedSpy.count(), 0);
    QCOMPARE(row->getColor(), QColor(QStringLiteral("#123456")));
    QTRY_VERIFY(more->hasFocus());

    // Nothing selected: the palette selects nothing either (also not "No color")
    Row* fillRow = new Row(&window);
    fillRow->setOptions(Option::NoColor | Option::White | Option::More);
    layout->insertWidget(1, fillRow);
    QTRY_VERIFY(fillRow->isVisible() && fillRow->size() == QSize(332, 32));
    QSignalSpy fillSpy(fillRow, &Row::colorSelected);
    QVERIFY(fillRow->showPalette());
    pdf::PDFColorPalette* fillPalette = fillRow->getPalette();
    QCOMPARE(fillPalette->getSuggestedColors().size(), 10);
    QVERIFY(!fillPalette->getSuggestedColors().front().color.isValid());
    for (int i = 0; i < 10; ++i)
    {
        QVERIFY(!fillPalette->getSuggestedSwatch(i)->isSelected());
    }

    // "No color" picked in the palette
    QTest::mouseClick(fillPalette->getSuggestedSwatch(0), Qt::LeftButton);
    QCOMPARE(fillSpy.count(), 1);
    QVERIFY(!fillSpy.takeFirst().at(0).value<QColor>().isValid());
    QVERIFY(fillRow->isNoColorSelected());
    QVERIFY(fillRow->getSwatch(0)->isSelected());

    // More colors… of the palette
    QSignalSpy moreSpy(row, &Row::moreColorsRequested);
    QVERIFY(row->showPalette());
    QTest::mouseClick(palette->getMoreColorsLink(), Qt::LeftButton);
    QCOMPARE(moreSpy.count(), 1);
    QVERIFY(!row->isPaletteVisible());

    // Keyboard: Space on More opens the palette
    more->setFocus(Qt::TabFocusReason);
    QTest::keyClick(more, Qt::Key_Space);
    QVERIFY(row->isPaletteVisible());
    palette->close();
    QVERIFY(!row->isPaletteVisible());

    // A disabled row does not open it and closes it; a hidden window closes it
    QVERIFY(row->showPalette());
    row->setEnabled(false);
    QVERIFY(!row->isPaletteVisible());
    QVERIFY(!row->showPalette());
    row->setEnabled(true);
    QVERIFY(row->showPalette());
    window.hide();
    QVERIFY(!row->isPaletteVisible());
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    // The palette deleted while it is open: a new one is created
    QVERIFY(row->showPalette());
    QPointer<pdf::PDFColorPalette> deletedPalette = palette;
    delete palette;
    QVERIFY(!deletedPalette);
    QVERIFY(!row->isPaletteVisible());
    QCOMPARE(QApplication::activePopupWidget(), nullptr);
    palette = row->getPalette();
    QVERIFY(palette);
    QVERIFY(row->showPalette());
    palette->close();

    // A theme change and a language change with the palette open
    QVERIFY(row->showPalette());
    pdf::PDFThemeManager::getInstance()->setScheme(Scheme::Dark);
    QEvent languageChange(QEvent::LanguageChange);
    QApplication::sendEvent(row, &languageChange);
    QVERIFY(row->isPaletteVisible());
    QCOMPARE(palette->getSuggestedColors().size(), 8);
    pdf::PDFThemeManager::getInstance()->setScheme(Scheme::Light);
    palette->close();

    // Nested widgets deleted by the application: a swatch, the More button
    delete row->getSwatch(3);
    QVERIFY(!row->getSwatch(3));
    QCOMPARE(row->getSwatchCount(), 8);
    QCOMPARE(row->getVisibleSwatchCount(), 7);
    QCOMPARE(row->sizeHint(), QSize(242, 32));
    QTRY_COMPARE(row->getSwatch(4)->geometry(), QRect(90, 2, 28, 28));
    row->setColor(canonical(Color::AnnotationPink));
    QCOMPARE(row->getColor(), canonical(Color::AnnotationPink));
    delete row->getMoreButton();
    QVERIFY(!row->getMoreButton());
    QVERIFY(!row->showPalette());
    QCOMPARE(row->sizeHint(), QSize(208, 28));
    // Options set again create the missing widgets
    row->setOptions(Option::White | Option::More);
    QCOMPARE(row->getSwatchCount(), 9);
    for (int i = 0; i < 9; ++i)
    {
        QVERIFY(row->getSwatch(i));
    }
    QVERIFY(row->getMoreButton());
    QVERIFY(row->getSwatch(3)->isSelected());
    QTRY_VERIFY(row->getMoreButton()->isVisible());
    QVERIFY(row->showPalette());
    palette->close();

    // A swatch moved to another parent is forgotten
    QWidget foreign;
    pdf::PDFColorSwatch* moved = row->getSwatch(1);
    moved->setParent(&foreign);
    QVERIFY(!row->getSwatch(1));
    moved->click();
    QCOMPARE(selectedSpy.count(), 0);

    // The row deleted from its own signal: a swatch, the palette
    QPointer<Row> deleted = new Row(&window);
    layout->addWidget(deleted);
    QTRY_VERIFY(deleted->isVisible());
    connect(deleted.data(), &Row::colorSelected, &window, [&deleted]() { delete deleted.data(); });
    deleted->getSwatch(1)->click();
    QVERIFY(!deleted);

    deleted = new Row(&window);
    layout->addWidget(deleted);
    QTRY_VERIFY(deleted->isVisible() && deleted->getMoreButton()->isVisible());
    connect(deleted.data(), &Row::colorSelected, &window, [&deleted]() { delete deleted.data(); });
    QVERIFY(deleted->showPalette());
    QPointer<pdf::PDFColorPalette> deletedRowPalette = deleted->getPalette();
    deletedRowPalette->getSuggestedSwatch(2)->click();
    QVERIFY(!deleted);
    QVERIFY(!deletedRowPalette);
    QCOMPARE(QApplication::activePopupWidget(), nullptr);
    QApplication::processEvents();

    // The row deleted from moreColorsRequested and from toggled of its swatch
    deleted = new Row(&window);
    layout->addWidget(deleted);
    QTRY_VERIFY(deleted->isVisible() && deleted->getMoreButton()->isVisible());
    connect(deleted.data(), &Row::moreColorsRequested, &window, [&deleted]() { delete deleted.data(); });
    QVERIFY(deleted->showPalette());
    deleted->getPalette()->getMoreColorsLink()->click();
    QVERIFY(!deleted);
    QApplication::processEvents();

    deleted = new Row(&window);
    connect(deleted->getSwatch(4), &QAbstractButton::toggled, &window, [&deleted]() { delete deleted.data(); });
    deleted->setColor(canonical(Color::AnnotationPurple));
    QVERIFY(!deleted);

    // The row deleted while its palette is open (directly and with its window)
    Row* open = new Row(&window);
    layout->addWidget(open);
    QTRY_VERIFY(open->isVisible() && open->getMoreButton()->isVisible());
    QVERIFY(open->showPalette());
    QPointer<pdf::PDFColorPalette> openPalette = open->getPalette();
    delete open;
    QVERIFY(!openPalette);
    QCOMPARE(QApplication::activePopupWidget(), nullptr);
    QApplication::processEvents();

    QWidget* otherWindow = new QWidget();
    QVBoxLayout* otherLayout = new QVBoxLayout(otherWindow);
    Row* otherRow = new Row(otherWindow);
    otherLayout->addWidget(otherRow);
    otherWindow->resize(400, 300);
    otherWindow->show();
    otherWindow->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(otherWindow));
    QVERIFY(otherRow->showPalette());
    QPointer<pdf::PDFColorPalette> otherPalette = otherRow->getPalette();
    delete otherWindow;
    QVERIFY(!otherPalette);
    QCOMPARE(QApplication::activePopupWidget(), nullptr);
    QApplication::processEvents();
}

void FluentComponentsTest::referencePointPicker()
{
    using Picker = pdf::PDFReferencePointPicker;
    using Scheme = pdf::PDFThemeColors::Scheme;

    // 1024:21781: 62 x 62, padding 6, points 14, gap 4
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    QLineEdit* before = new QLineEdit(&window);
    Picker* picker = new Picker(&window);
    layout->addWidget(before);
    layout->addWidget(picker);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(picker->sizeHint(), QSize(Picker::EXTENT, Picker::EXTENT));
    QCOMPARE(picker->size(), QSize(Picker::EXTENT, Picker::EXTENT));
    QCOMPARE(picker->getPointRect(0), QRect(Picker::PADDING, Picker::PADDING, Picker::POINT, Picker::POINT));
    QCOMPARE(picker->getPointRect(8), QRect(Picker::PADDING + 2 * (Picker::POINT + Picker::GAP), Picker::PADDING + 2 * (Picker::POINT + Picker::GAP), Picker::POINT, Picker::POINT));
    QVERIFY(picker->getPointRect(9).isNull());
    QVERIFY(picker->getPointRect(-1).isNull());
    QCOMPARE(picker->getPointAt(QPoint(0, 0)), -1);

    // The alignments of the points (center for a missing part)
    QCOMPARE(Picker::getAlignment(0), Qt::Alignment(Qt::AlignLeft | Qt::AlignTop));
    QCOMPARE(Picker::getAlignment(4), Qt::Alignment(Qt::AlignHCenter | Qt::AlignVCenter));
    QCOMPARE(Picker::getAlignment(8), Qt::Alignment(Qt::AlignRight | Qt::AlignBottom));
    QCOMPARE(Picker::getAlignment(100), Qt::Alignment(Qt::AlignRight | Qt::AlignBottom));
    for (int i = 0; i < Picker::POINT_COUNT; ++i)
    {
        QCOMPARE(Picker::getIndex(Picker::getAlignment(i)), i);
        QVERIFY(!Picker::getPointName(i).isEmpty());
    }
    QCOMPARE(Picker::getIndex(Qt::Alignment()), 4);
    QCOMPARE(Picker::getIndex(Qt::AlignRight), 5);

    // A click chooses a point (also in the gap next to it); invalid indices are ignored
    QSignalSpy changed(picker, &Picker::currentIndexChanged);
    QCOMPARE(picker->getCurrentIndex(), 0);
    QTest::mouseClick(picker, Qt::LeftButton, Qt::NoModifier, picker->getPointRect(5).center());
    QCOMPARE(picker->getCurrentIndex(), 5);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(picker->accessibleDescription(), Picker::getPointName(5));
    QTest::mouseClick(picker, Qt::LeftButton, Qt::NoModifier, picker->getPointRect(5).center());
    QCOMPARE(changed.count(), 1);
    QTest::mouseClick(picker, Qt::LeftButton, Qt::NoModifier, picker->getPointRect(1).bottomLeft() + QPoint(0, 2));
    QCOMPARE(picker->getCurrentIndex(), 1);
    picker->setCurrentIndex(-1);
    picker->setCurrentIndex(9);
    QCOMPARE(picker->getCurrentIndex(), 1);
    picker->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
    QCOMPARE(picker->getCurrentIndex(), 6);
    QCOMPARE(picker->getAlignment(), Qt::Alignment(Qt::AlignLeft | Qt::AlignBottom));

    // The keyboard: the arrows move (not mirrored for right-to-left), Home / End
    picker->setFocus(Qt::TabFocusReason);
    QTest::keyClick(picker, Qt::Key_Up);
    QCOMPARE(picker->getCurrentIndex(), 3);
    QTest::keyClick(picker, Qt::Key_Right);
    QCOMPARE(picker->getCurrentIndex(), 4);
    window.setLayoutDirection(Qt::RightToLeft);
    QTest::keyClick(picker, Qt::Key_Right);
    QCOMPARE(picker->getCurrentIndex(), 5);
    QTest::keyClick(picker, Qt::Key_Right);
    QCOMPARE(picker->getCurrentIndex(), 5);
    QTest::keyClick(picker, Qt::Key_Down);
    QTest::keyClick(picker, Qt::Key_Down);
    QCOMPARE(picker->getCurrentIndex(), 8);
    QTest::keyClick(picker, Qt::Key_Home);
    QCOMPARE(picker->getCurrentIndex(), 0);
    QTest::keyClick(picker, Qt::Key_Left);
    QTest::keyClick(picker, Qt::Key_Up);
    QCOMPARE(picker->getCurrentIndex(), 0);
    QTest::keyClick(picker, Qt::Key_End);
    QCOMPARE(picker->getCurrentIndex(), 8);
    window.setLayoutDirection(Qt::LeftToRight);

    // Pixels: the chosen point bg/accent, another point bg/surface, the frame bg/subtle,
    // both themes at 1x and 2x; disabled: icon/disabled
    picker->setCurrentIndex(0);
    picker->clearFocus();
    QApplication::processEvents();
    for (const Scheme scheme : { Scheme::Light, Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        for (const qreal ratio : { 1.0, 2.0 })
        {
            QImage image(QSize(Picker::EXTENT, Picker::EXTENT) * ratio, QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(ratio);
            image.fill(Qt::transparent);
            picker->render(&image, QPoint(), QRegion(), QWidget::DrawChildren);
            auto pixel = [&](QPoint point) { return image.pixelColor(point * ratio); };
            QCOMPARE(pixel(picker->getPointRect(0).center()), token(Color::BgAccent));
            QCOMPARE(pixel(picker->getPointRect(4).center()), token(Color::BgSurface));
            QCOMPARE(pixel(QPoint(Picker::EXTENT / 2, 2)), token(Color::BgSubtle));
            QCOMPARE(countLeakPixels(image), 0);
        }
        picker->setEnabled(false);
        const QImage disabled = picker->grab().toImage();
        QCOMPARE(disabled.pixelColor(picker->getPointRect(0).center() * disabled.devicePixelRatio()), token(Color::IconDisabled));
        picker->setEnabled(true);
    }
    pdf::PDFThemeManager::getInstance()->setScheme(Scheme::Light);

    // Hover: the border of the point under the pointer is bg/accent; Leave clears it
    QTest::mouseMove(picker, picker->getPointRect(4).center());
    QCOMPARE(picker->getHoveredIndex(), 4);
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(picker, &leave);
    QCOMPARE(picker->getHoveredIndex(), -1);

    // A language change keeps the description
    QEvent language(QEvent::LanguageChange);
    QCoreApplication::sendEvent(picker, &language);
    QCOMPARE(picker->accessibleDescription(), Picker::getPointName(0));
}

void FluentComponentsTest::signatureCard()
{
    using Status = Painter::SignatureStatus;

    // Heights of the six variants of the design
    QCOMPARE(Painter::getSignatureCardSize(PanelSamples::signature(Status::Valid, false)), QSize(276, 127));
    QCOMPARE(Painter::getSignatureCardSize(PanelSamples::signature(Status::Valid, true)), QSize(276, 382));
    QCOMPARE(Painter::getSignatureCardSize(PanelSamples::signature(Status::Warning, false)), QSize(276, 127));
    QCOMPARE(Painter::getSignatureCardSize(PanelSamples::signature(Status::Warning, true)), QSize(276, 403));
    QCOMPARE(Painter::getSignatureCardSize(PanelSamples::signature(Status::Invalid, false)), QSize(276, 127));
    QCOMPARE(Painter::getSignatureCardSize(PanelSamples::signature(Status::Invalid, true)), QSize(276, 382));

    // The toggle 8 px from the trailing edge, centered on the header; the link of the root certificate
    const Painter::SignatureCardLayout layout = Painter::getSignatureCardLayout(PanelSamples::signature(Status::Warning, true), 276);
    QCOMPARE(layout.toggle, QRect(244, 17, 24, 24));
    QCOMPARE(layout.links.size(), 3);
    QVERIFY(layout.links.at(0).isEmpty());
    QCOMPARE(layout.links.at(2).topLeft(), QPoint(31, 403 - 13 - 16));
    QVERIFY(Painter::getSignatureCardLayout(PanelSamples::signature(Status::Warning, false), 276).links.at(2).isEmpty());

    // Texts of the status tag
    QCOMPARE(Painter::getSignatureStatusText(Status::Valid), QStringLiteral("Valid"));
    QCOMPARE(Painter::getSignatureStatusText(Status::Warning), QStringLiteral("Needs attention"));
    QCOMPARE(Painter::getSignatureStatusText(Status::Invalid), QStringLiteral("Invalid"));
    QCOMPARE(Painter::getSignatureStatusText(static_cast<Status>(9)), QStringLiteral("Invalid"));

    // The widget: the toggle expands and collapses it
    QWidget window;
    QVBoxLayout* windowLayout = new QVBoxLayout(&window);
    pdf::PDFSignatureCard* card = new pdf::PDFSignatureCard(&window);
    windowLayout->addWidget(card, 0, Qt::AlignLeft | Qt::AlignTop);
    window.resize(400, 500);
    QVERIFY(card->getToggleButton()->isHidden());
    PanelSamples::fill(card, PanelSamples::signature(Status::Warning, false));
    QVERIFY(!card->getToggleButton()->isHidden());
    QCOMPARE(card->getChainCaption(), QStringLiteral("Certificate chain"));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_COMPARE(card->size(), QSize(276, 127));
    QCOMPARE(card->getToggleButton()->geometry(), QRect(244, 17, 24, 24));
    QCOMPARE(card->getToggleButton()->getIconName(), QStringLiteral("chevron-down"));
    QCOMPARE(card->getToggleButton()->accessibleName(), QStringLiteral("Show details of Jane Novak"));
    QCOMPARE(card->getToggleButton()->toolTip(), QStringLiteral("Show details"));
    QVERIFY(card->getCertificateLink(2));
    QVERIFY(card->getCertificateLink(2)->isHidden());
    QCOMPARE(card->getCertificateLink(0), nullptr);
    QCOMPARE(card->getCertificateLink(7), nullptr);

    QSignalSpy expandedSpy(card, &pdf::PDFSignatureCard::expandedChanged);
    QTest::mouseClick(card->getToggleButton(), Qt::LeftButton);
    QVERIFY(card->isExpanded());
    QCOMPARE(expandedSpy.count(), 1);
    QTRY_COMPARE(card->size(), QSize(276, 403));
    QCOMPARE(card->getToggleButton()->getIconName(), QStringLiteral("chevron-up"));
    QCOMPARE(card->getToggleButton()->accessibleName(), QStringLiteral("Hide details of Jane Novak"));
    QVERIFY(card->getCertificateLink(2)->isVisible());
    QCOMPARE(card->getCertificateLink(2)->geometry().topLeft(), QPoint(31, 374));

    QSignalSpy linkSpy(card, &pdf::PDFSignatureCard::certificateLinkActivated);
    QTest::mouseClick(card->getCertificateLink(2), Qt::LeftButton);
    QCOMPARE(linkSpy.count(), 1);
    QCOMPARE(linkSpy.at(0).at(0).toInt(), 2);

    // Keyboard: the toggle by Space
    card->getToggleButton()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(card->getToggleButton(), Qt::Key_Space);
    QVERIFY(!card->isExpanded());
    QTest::keyClick(card->getToggleButton(), Qt::Key_Return);
    QVERIFY(card->isExpanded());
    QCOMPARE(expandedSpy.count(), 3);

    // Accessibility: the signer is the name, the texts the description
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(card);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Jane Novak"));
    const QString description = accessible->text(QAccessible::Description);
    QVERIFY(description.contains(QStringLiteral("Needs attention")));
    QVERIFY(description.contains(QStringLiteral("Hash algorithm: SHA-256")));
    QVERIFY(description.contains(QStringLiteral("Example Root CA, Not among trusted certificates")));

    // The widget draws as the painter with the toggle and the link, also right-to-left, disabled and at the scales
    for (const Status status : { Status::Valid, Status::Warning, Status::Invalid })
    {
        PanelSamples::fill(card, PanelSamples::signature(status, true));
        for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
        {
            card->setLayoutDirection(direction);
            for (const bool enabled : { true, false })
            {
                card->setEnabled(enabled);
                QTRY_COMPARE(card->height(), status == Status::Warning ? 403 : 382);
                for (const qreal scale : { 1.0, 1.5, 2.0 })
                {
                    Painter::SignatureCard content = card->getContent();
                    content.paintNested = true;
                    const QStyle::State flags = enabled ? QStyle::State_Enabled : QStyle::State_None;
                    const QString difference = PanelSamples::compareScaled(renderWidget(card, scale),
                                                                           renderInWidget([&](QPainter& painter) { Painter::paintSignatureCard(&painter, QRectF(QPointF(0, 0), card->size()), content, Painter::State(flags, direction)); }, card->size(), scale),
                                                                           scale, QStringLiteral("Signature card"));
                    QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("%1 %2 %3: %4").arg(int(status)).arg(int(direction)).arg(scale).arg(difference)));
                }
            }
        }
    }
    card->setEnabled(true);
    card->setLayoutDirection(Qt::LeftToRight);
    QCOMPARE(card->getToggleButton()->geometry(), QRect(244, 17, 24, 24));
    card->setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(card->getToggleButton()->geometry(), QRect(8, 17, 24, 24));
    card->setLayoutDirection(Qt::LeftToRight);

    // A card without details has nothing to expand; defensive values
    pdf::PDFSignatureCard bare;
    bare.setSigner(QStringLiteral("TSA"));
    QVERIFY(bare.getToggleButton()->isHidden());
    bare.setStatus(static_cast<Status>(42));
    QCOMPARE(bare.getStatus(), Status::Invalid);
    bare.setCertificates({ { QStringLiteral("A"), QString(), static_cast<Status>(-3), QString() } });
    QCOMPARE(bare.getCertificates().front().status, Status::Invalid);
    QVERIFY(!bare.getToggleButton()->isHidden());
    QCOMPARE(bare.heightForWidth(-50), Painter::getSignatureCardHeight(bare.getContent(), 0));
    delete bare.getToggleButton();
    bare.setExpanded(true);
    bare.resize(276, 300);
    QCOMPARE(countLeakPixels(renderWidget(&bare, 1.0)), 0);

    // A slot, which deletes the card, does not break it
    QPointer<pdf::PDFSignatureCard> deleted = new pdf::PDFSignatureCard();
    PanelSamples::fill(deleted, PanelSamples::signature(Status::Valid, false));
    connect(deleted.data(), &pdf::PDFSignatureCard::expandedChanged, deleted.data(), [&deleted]() { delete deleted.data(); });
    deleted->getToggleButton()->click();
    QVERIFY(!deleted);
}

void FluentComponentsTest::strengthMeter()
{
    using Level = Painter::StrengthLevel;
    using Criterion = Painter::StrengthCriterion;

    // Size of the design, the cells of the levels
    QCOMPARE(Painter::getStrengthMeterSize(PanelSamples::meter(Level::Empty)), QSize(300, 50));
    QCOMPARE(Painter::getStrengthMeterCellCount(), 20);
    const int cells[] = { 0, 3, 7, 11, 16, 20 };
    for (int i = 0; i < 6; ++i)
    {
        QCOMPARE(Painter::getStrengthMeterFilledCells(PanelSamples::meter(static_cast<Level>(i))), cells[i]);
    }
    QCOMPARE(Painter::getStrengthLevelText(PanelSamples::meter(Level::Empty)), QStringLiteral("Password strength"));
    QCOMPARE(Painter::getStrengthLevelText(PanelSamples::meter(Level::Moderate)), QStringLiteral("Moderate"));
    QCOMPARE(Painter::getStrengthLevelText(PanelSamples::meter(static_cast<Level>(99))), QStringLiteral("Password strength"));

    // The criteria of the design: 109, 44, 50, 52 wide, in one row at 300, wrapped at 180
    const QList<QRect> rects = Painter::getStrengthMeterCriterionRects(PanelSamples::meter(Level::Weak), 300);
    QCOMPARE(rects.size(), 4);
    QVERIFY2(qAbs(rects.at(0).width() - 109) <= 1, qPrintable(QString::number(rects.at(0).width())));
    QVERIFY(qAbs(rects.at(1).width() - 44) <= 1);
    QVERIFY(qAbs(rects.at(2).width() - 50) <= 1);
    QVERIFY(qAbs(rects.at(3).width() - 52) <= 1);
    QCOMPARE(rects.at(0).topLeft(), QPoint(0, 33));
    QCOMPARE(rects.at(1).left(), rects.at(0).right() + 1 + 4);
    QCOMPARE(rects.at(3).top(), 33);
    QCOMPARE(Painter::getStrengthMeterHeight(PanelSamples::meter(Level::Weak), 180), 71);
    QCOMPARE(Painter::getStrengthMeterHeight(PanelSamples::meter(Level::Weak), 60), 33 + 4 * 17 + 3 * 4);
    Painter::StrengthMeter hidden = PanelSamples::meter(Level::Weak);
    hidden.criteriaVisible = false;
    QCOMPARE(Painter::getStrengthMeterHeight(hidden, 300), 27);

    // The widget
    pdf::PDFStrengthMeter meter;
    QCOMPARE(meter.getTitle(), QStringLiteral("Password strength"));
    QCOMPARE(meter.sizeHint(), QSize(300, 50));
    QCOMPARE(meter.focusPolicy(), Qt::NoFocus);
    meter.setLevel(Level::Weak);
    meter.setDetail(QStringLiteral("42 of 128 bits"));
    meter.setSatisfiedCriteria(Criterion::Length);
    QCOMPARE(meter.getFilledCells(), 7);
    meter.setFilledCells(25);
    QCOMPARE(meter.getFilledCells(), 20);
    meter.setFilledCells(-7);
    QCOMPARE(meter.getFilledCells(), 7);
    meter.setLevel(static_cast<Level>(-1));
    QCOMPARE(meter.getLevel(), Level::Empty);
    meter.setLevel(Level::Weak);
    meter.setSatisfiedCriteria(Painter::StrengthCriteria(0xF0) | Criterion::Length);
    QCOMPARE(meter.getSatisfiedCriteria(), Painter::StrengthCriteria(Criterion::Length));
    meter.setCriterionText(Criterion::Length, QStringLiteral("16+ characters"));
    QCOMPARE(Painter::getStrengthCriterionText(meter.getContent(), Criterion::Length), QStringLiteral("16+ characters"));
    meter.setCriterionText(Criterion::None, QStringLiteral("x"));
    meter.setCriterionText(Criterion::Length, QString());
    QCOMPARE(Painter::getStrengthCriterionText(meter.getContent(), Criterion::Length), QStringLiteral("12+ characters"));
    QCOMPARE(meter.heightForWidth(180), 71);

    // Accessibility: the level and the criteria
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(&meter);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Password strength: Weak"));
    QVERIFY(accessible->text(QAccessible::Description).contains(QStringLiteral("42 of 128 bits")));
    QVERIFY(accessible->text(QAccessible::Description).contains(QStringLiteral("12+ characters: satisfied")));
    QVERIFY(accessible->text(QAccessible::Description).contains(QStringLiteral("Aa: not satisfied")));
    meter.setAccessibleName(QStringLiteral("New password"));
    meter.setLevel(Level::Strong);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("New password"));
    meter.setAccessibleName(QString());
    meter.setLevel(Level::Weak);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Password strength: Weak"));

    // The widget draws as the painter
    meter.resize(300, 50);
    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        meter.setLayoutDirection(direction);
        for (const qreal scale : { 1.0, 1.5, 2.0 })
        {
            const QString difference = describeDifference(renderWidget(&meter, scale),
                                                          renderInWidget([&](QPainter& painter) { Painter::paintStrengthMeter(&painter, QRectF(0, 0, 300, 50), meter.getContent(), state(QStyle::State_None, direction)); }, QSize(300, 50), scale), 3);
            QVERIFY2(difference.isEmpty(), qPrintable(difference));
        }
    }
}

void FluentComponentsTest::panelPixels_data()
{
    pixelProbes_data();
}

void FluentComponentsTest::panelPixels()
{
    using Level = Painter::StrengthLevel;
    using Status = Painter::SignatureStatus;

    QFETCH(QString, theme);
    QFETCH(qreal, scale);
    QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));

    const QColor surface = token(Color::BgSurface);
    const QStyle::State none = QStyle::State_None;

    // Color button: the field of the state, the swatch keeps its color
    const Painter::ColorButton button{ QColor(QStringLiteral("#111111")), QStringLiteral("Black"), false };
    auto buttonImage = [&](QStyle::State flags, bool enabled = true, bool open = false)
    {
        Painter::ColorButton content = button;
        content.open = open;
        const Painter::State buttonState(enabled ? (flags | QStyle::State_Enabled) : flags);
        return render([&](QPainter& painter) { Painter::paintColorButton(&painter, QRectF(0, 0, 260, 34), content, buttonState); }, QSize(260, 34), scale);
    };
    QImage image = buttonImage(none);
    QVERIFY(isNear(pixel(image, 130, 0), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, 130, 17), surface));
    QVERIFY(isNear(pixel(image, 18, 17), QColor(QStringLiteral("#111111"))));
    QVERIFY(isNear(pixel(image, 9, 17), token(Color::BorderStrong), 24));
    QVERIFY(isNear(pixel(image, 8, 17), surface));
    QVERIFY(isNear(pixel(image, 28, 17), token(Color::BorderStrong), 24));
    QVERIFY(colorDistance(strongestInk(image, QRect(235, 10, 14, 14), surface), token(Color::IconDefault)) < 60);
    QVERIFY(isNear(strongestInk(image, QRect(249, 10, 9, 14), surface), surface));
    QVERIFY(colorDistance(strongestInk(image, QRect(38, 9, 40, 16), surface), token(Color::TextPrimary)) < 40);
    image = buttonImage(QStyle::State_MouseOver);
    QVERIFY(isNear(pixel(image, 130, 0), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(image, 130, 17), surface));
    image = buttonImage(QStyle::State_Sunken);
    QVERIFY(isNear(pixel(image, 130, 0), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(image, 130, 17), token(Color::BgSubtle)));
    image = buttonImage(none, true, true);
    QVERIFY(isNear(pixel(image, 130, 17), token(Color::BgSubtle)));
    image = buttonImage(QStyle::State_HasFocus);
    QVERIFY(isNear(pixel(image, 130, 0), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 130, 1), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 130, 17), surface));
    image = buttonImage(none, false);
    QVERIFY(isNear(pixel(image, 130, 17), token(Color::BgMuted)));
    QVERIFY(isNear(pixel(image, 130, 0), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, 18, 17), QColor(QStringLiteral("#111111"))));
    QVERIFY(colorDistance(strongestInk(image, QRect(38, 9, 40, 16), token(Color::BgMuted)), token(Color::TextDisabled)) < 40);
    QVERIFY(colorDistance(strongestInk(image, QRect(235, 10, 14, 14), token(Color::BgMuted)), token(Color::IconDisabled)) < 60);
    image = render([&](QPainter& painter) { Painter::paintColorButton(&painter, QRectF(0, 0, 260, 34), Painter::ColorButton{ QColor(), QString(), false }, state(none)); }, QSize(260, 34), scale);
    QVERIFY(isNear(pixel(image, 12, 12), surface, 24));
    QVERIFY(colorDistance(strongestInk(image, QRect(12, 10, 14, 14), surface), token(Color::IconDanger)) < 80);

    // Palette: the panel, the selected swatch, the divider
    Painter::ColorPalette palette;
    for (const pdf::PDFColorPalette::Entry& entry : PanelSamples::suggested())
    {
        palette.suggested << entry.color;
    }
    for (const pdf::PDFColorPalette::Entry& entry : PanelSamples::recent())
    {
        palette.recent << entry.color;
    }
    palette.selected = 6;
    palette.moreText = QStringLiteral("More colors…");
    image = render([&](QPainter& painter) { Painter::paintColorPalette(&painter, QRectF(0, 0, 232, 209), palette, state(none)); }, QSize(232, 209), scale);
    QVERIFY(isNear(pixel(image, 116, 0), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, 116, 5), surface));
    QVERIFY(isNear(pixel(image, 12, 85), token(Color::BgAccent), 40));
    QVERIFY(isNear(pixel(image, 26, 85), QColor(QStringLiteral("#00ff7f"))));
    QVERIFY(isNear(pixel(image, 26, 49), QColor(QStringLiteral("#111111"))));
    QVERIFY(isNear(pixel(image, 116, 170), token(Color::BorderDivider)));
    QVERIFY(colorDistance(strongestInk(image, QRect(14, 181, 60, 16), surface), token(Color::TextAccent)) < 40);
    QCOMPARE(countLeakPixels(image), 0);

    // Signature card: the badge and the tag of the status, the border of the card
    for (const Status status : { Status::Valid, Status::Warning, Status::Invalid })
    {
        const pdf::PDFAccentHue hue = status == Status::Valid ? pdf::PDFAccentHue::Green : (status == Status::Warning ? pdf::PDFAccentHue::Amber : pdf::PDFAccentHue::Red);
        const Painter::SignatureCard card = PanelSamples::signature(status, true);
        const QSize size = Painter::getSignatureCardSize(card);
        image = render([&](QPainter& painter) { Painter::paintSignatureCard(&painter, QRectF(QPointF(0, 0), size), card, state(none)); }, size, scale);
        QVERIFY(isNear(pixel(image, 138, 0), token(Color::BorderDefault)));
        QVERIFY(isNear(pixel(image, 17, 29), token(pdf::PDFAccentHueColors::getColor(hue, pdf::PDFAccentHueColors::Role::Background))));
        QVERIFY(colorDistance(strongestInk(image, QRect(21, 21, 16, 16), token(pdf::PDFAccentHueColors::getColor(hue, pdf::PDFAccentHueColors::Role::Background))),
                              token(pdf::PDFAccentHueColors::getColor(hue, pdf::PDFAccentHueColors::Role::Icon))) < 60);
        QVERIFY(isNear(pixel(image, 13, 64), token(pdf::PDFAccentHueColors::getColor(hue, pdf::PDFAccentHueColors::Role::Border)), 24));
        QVERIFY(isNear(pixel(image, 17, 64), token(pdf::PDFAccentHueColors::getColor(hue, pdf::PDFAccentHueColors::Role::Background)), 8));
        QVERIFY(isNear(pixel(image, 138, 124), token(Color::BorderDivider)));
        QVERIFY(colorDistance(strongestInk(image, QRect(250, 23, 12, 12), surface), token(Color::IconMuted)) < 60);
        QCOMPARE(countLeakPixels(image), 0);
    }

    // Strength meter: the cells of the level, the empty track, the criteria
    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        const Painter::StrengthMeter meter = PanelSamples::meter(Level::Weak, Painter::StrengthCriterion::Length);
        image = render([&](QPainter& painter) { Painter::paintStrengthMeter(&painter, QRectF(0, 0, 300, 50), meter, state(none, direction)); }, QSize(300, 50), scale);
        const bool rtl = direction == Qt::RightToLeft;
        QVERIFY(isNear(pixel(image, rtl ? 250 : 50, 24), token(Color::AccentRedIcon)));
        QVERIFY(isNear(pixel(image, rtl ? 50 : 250, 24), token(Color::BorderDivider)));
        QVERIFY(isNear(pixel(image, rtl ? 296 : 3, 41), token(Color::AccentGreenBg), 8));
        QVERIFY(colorDistance(strongestInk(image, QRect(rtl ? 240 : 0, 0, 60, 15), surface), token(Color::AccentRedIcon)) < 40);
    }
    image = render([&](QPainter& painter) { Painter::paintStrengthMeter(&painter, QRectF(0, 0, 300, 50), PanelSamples::meter(Level::Strong), Painter::State(none)); }, QSize(300, 50), scale);
    QVERIFY(isNear(pixel(image, 50, 24), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(image, 3, 41), token(Color::BgMuted), 8));
    image = render([&](QPainter& painter) { Painter::paintStrengthMeter(&painter, QRectF(0, 0, 300, 50), PanelSamples::meter(Level::VeryStrong), state(none)); }, QSize(300, 50), scale);
    QVERIFY(isNear(pixel(image, 298, 24), token(Color::AccentGreenIcon), 24));

    // Continue with: the texts of the header and the caption, the nested choices
    const Painter::ContinueWith content = PanelSamples::continueWith();
    image = render([&](QPainter& painter) { Painter::paintContinueWith(&painter, QRectF(0, 0, 480, 93), content, state(none)); }, QSize(480, 93), scale);
    QVERIFY(colorDistance(strongestInk(image, QRect(0, 0, 100, 16), surface), token(Color::TextPrimary)) < 40);
    QVERIFY(colorDistance(strongestInk(image, QRect(0, 80, 200, 13), surface), token(Color::TextMuted)) < 60);
    QVERIFY(colorDistance(strongestInk(image, QRect(400, 0, 80, 16), surface), token(Color::TextAccent)) < 40);
    QVERIFY(isNear(pixel(image, 77, 24), token(Color::BorderDefault)));
    QCOMPARE(countLeakPixels(image), 0);
}

void FluentComponentsTest::panelWidgetsMatchPainter()
{
    // The composite widgets draw as the painter with the nested elements
    QWidget window;
    window.resize(900, 600);
    pdf::PDFContinueWith* continueWith = new pdf::PDFContinueWith(&window);
    continueWith->setChoices(PanelSamples::choices());
    continueWith->setGeometry(0, 0, 480, 93);
    pdf::PDFColorButton* button = new pdf::PDFColorButton(QColor(QStringLiteral("#111111")), QStringLiteral("Black"), &window);
    button->setGeometry(500, 0, 260, 34);
    window.setFocusPolicy(Qt::StrongFocus);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.setFocus(Qt::OtherFocusReason);

    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        continueWith->setLayoutDirection(direction);
        button->setLayoutDirection(direction);
        for (const int selected : { -1, 1 })
        {
            continueWith->setSelectedChoice(selected);
            for (const bool enabled : { true, false })
            {
                continueWith->setEnabled(enabled);
                button->setEnabled(enabled);
                const QStyle::State flags = enabled ? QStyle::State_Enabled : QStyle::State_None;
                for (const qreal scale : { 1.0, 1.5, 2.0 })
                {
                    const int tolerance = qFuzzyCompare(scale, 1.5) ? 24 : 3;
                    Painter::ContinueWith content = continueWith->getContent();
                    content.paintNested = true;
                    QString difference = PanelSamples::compareScaled(renderWidget(continueWith, scale),
                                                                     renderInWidget([&](QPainter& painter) { Painter::paintContinueWith(&painter, QRectF(0, 0, 480, 93), content, Painter::State(flags, direction)); }, QSize(480, 93), scale),
                                                                     scale, QStringLiteral("Continue with"));
                    QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Continue with %1 %2 %3: %4").arg(int(direction)).arg(selected).arg(scale).arg(difference)));

                    difference = describeDifference(renderWidget(button, scale),
                                                    renderInWidget([&](QPainter& painter) { Painter::paintColorButton(&painter, QRectF(0, 0, 260, 34), button->getContent(), Painter::State(flags, direction)); }, QSize(260, 34), scale), tolerance);
                    QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Color button %1 %2: %3").arg(int(direction)).arg(scale).arg(difference)));
                }
            }
        }
    }
}

void FluentComponentsTest::panelThemeAndDefensive()
{
    // The painters tolerate a null painter, empty, negative and huge rectangles and invalid values
    const QImage empty = render([](QPainter&) { }, QSize(40, 40));
    const qreal infinity = std::numeric_limits<qreal>::infinity();
    Painter::SignatureCard card = PanelSamples::signature(static_cast<Painter::SignatureStatus>(77), true);
    card.certificates << Painter::SignatureCertificate{ QString(), QString(), static_cast<Painter::SignatureStatus>(-1), QStringLiteral("Link") };
    Painter::StrengthMeter meter = PanelSamples::meter(static_cast<Painter::StrengthLevel>(55));
    meter.filledCells = 1000;
    meter.criteriaTexts = { QString(), QString(2000, QLatin1Char('x')) };
    Painter::ContinueWith content = PanelSamples::continueWith();
    content.selectedChoice = 99;
    content.choices << content.choices << content.choices;
    Painter::PageSelection selection;
    selection.error = QString(5000, QLatin1Char('e'));
    Painter::ColorPalette palette;
    palette.selected = -99;
    palette.suggested << QColor() << QColor(Qt::red);

    auto paintAll = [&](QPainter* painter, const QRectF& rect)
    {
        Painter::paintPageSelection(painter, rect, selection, Painter::State());
        Painter::paintContinueWith(painter, rect, content, Painter::State());
        Painter::paintColorButton(painter, rect, Painter::ColorButton(), Painter::State());
        Painter::paintColorPalette(painter, rect, palette, Painter::State());
        Painter::paintSignatureCard(painter, rect, card, Painter::State());
        Painter::paintStrengthMeter(painter, rect, meter, Painter::State());
    };

    QCOMPARE(render([&](QPainter& painter)
    {
        paintAll(nullptr, QRectF(0, 0, 40, 40));
        for (const QRectF& rect : { QRectF(0, 0, 0, 40), QRectF(0, 0, -10, 40), QRectF(0, 0, infinity, 40), QRectF(qQNaN(), 0, 40, 40), QRectF(0, 0, 1.0e9, 40) })
        {
            paintAll(&painter, rect);
        }
    }, QSize(40, 40)), empty);

    // Tiny and narrow rectangles draw inside of themselves only, the state of the painter is restored
    const QImage tiny = render([&](QPainter& painter)
    {
        painter.setPen(Qt::red);
        painter.setOpacity(0.5);
        paintAll(&painter, QRectF(10, 10, 3, 3));
        QCOMPARE(painter.pen().color(), QColor(Qt::red));
        QCOMPARE(painter.opacity(), 0.5);
    }, QSize(40, 40));
    for (int y = 0; y < 40; ++y)
    {
        for (int x = 0; x < 40; ++x)
        {
            if (x < 8 || y < 8 || x > 15 || y > 15)
            {
                QVERIFY2(isNear(tiny.pixelColor(x, y), empty.pixelColor(x, y), 0), qPrintable(QStringLiteral("%1, %2").arg(x).arg(y)));
            }
        }
    }

    // Layouts of negative and huge widths do not overflow
    for (const int width : { -100, 0, 1, std::numeric_limits<int>::max() })
    {
        QVERIFY(Painter::getPageSelectionHeight(selection, width) >= 0);
        QVERIFY(Painter::getContinueWithHeight(content, width) >= 0);
        QVERIFY(Painter::getSignatureCardHeight(card, width) >= 0);
        QVERIFY(Painter::getStrengthMeterHeight(meter, width) >= 0);
    }
    QCOMPARE(Painter::getContinueWithLayout(content, 480).choices.size(), 3);
    QCOMPARE(Painter::getStrengthMeterFilledCells(meter), 0);
    meter.level = Painter::StrengthLevel::Weak;
    QCOMPARE(Painter::getStrengthMeterFilledCells(meter), 20);
    QCOMPARE(Painter::normalized(static_cast<Painter::StrengthLevel>(55)), Painter::StrengthLevel::Empty);

    // A change of the theme repaints the widgets, nothing leaks in the dark theme
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    pdf::PDFPageSelection* pageSelection = new pdf::PDFPageSelection(&window);
    pageSelection->setPageCount(12);
    pdf::PDFContinueWith* continueWith = new pdf::PDFContinueWith(&window);
    continueWith->setChoices(PanelSamples::choices());
    pdf::PDFColorButton* button = new pdf::PDFColorButton(QColor(Qt::blue), QString(), &window);
    pdf::PDFColorPalette* inlinePalette = new pdf::PDFColorPalette(&window);
    inlinePalette->setSuggestedColors(PanelSamples::suggested());
    pdf::PDFSignatureCard* signature = new pdf::PDFSignatureCard(&window);
    PanelSamples::fill(signature, PanelSamples::signature(Painter::SignatureStatus::Warning, true));
    pdf::PDFStrengthMeter* strength = new pdf::PDFStrengthMeter(&window);
    strength->setLevel(Painter::StrengthLevel::Moderate);
    for (QWidget* widget : std::initializer_list<QWidget*>{ pageSelection, continueWith, button, inlinePalette, signature, strength })
    {
        layout->addWidget(widget, 0, Qt::AlignLeft);
    }
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const QImage light = renderWidget(&window, 1.0);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    QApplication::processEvents();
    const QImage dark = renderWidget(&window, 1.0);
    QVERIFY(light != dark);
    QCOMPARE(countLeakPixels(dark), 0);
    QCOMPARE(countLeakPixels(renderWidget(&window, 2.0)), 0);

    // A new language translates the default texts
    PanelTestTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    QTRY_COMPARE(pageSelection->getLabel(), QStringLiteral("Seiten"));
    QTRY_COMPARE(continueWith->getTitle(), QStringLiteral("Dann weiter mit"));
    QTRY_COMPARE(strength->getLevelText(), QStringLiteral("Mittel"));
    QTRY_COMPARE(signature->getChainCaption(), QStringLiteral("Zertifikatskette"));
    QVERIFY(QCoreApplication::removeTranslator(&translator));
    QTRY_COMPARE(pageSelection->getLabel(), QStringLiteral("Pages"));
    pageSelection->setLabel(QStringLiteral("Own"));
    QVERIFY(QCoreApplication::installTranslator(&translator));
    QApplication::processEvents();
    QCOMPARE(pageSelection->getLabel(), QStringLiteral("Own"));
    QVERIFY(QCoreApplication::removeTranslator(&translator));
}

void FluentComponentsTest::panelGallery()
{
    // Every variant of the sets has its sample (painted or a live widget) with the node of the design
    const std::vector<pdffluentgallery::GalleryPages::Page> pages = pdffluentgallery::GalleryPages::createPages(nullptr);
    QSet<QString> nodes;
    for (const pdffluentgallery::GalleryPages::Page& page : pages)
    {
        for (const pdffluentgallery::GallerySpecimen* specimen : page.widget->findChildren<pdffluentgallery::GallerySpecimen*>())
        {
            nodes.insert(specimen->getFigmaNode());
        }
        for (const QWidget* widget : page.widget->findChildren<QWidget*>())
        {
            const QString node = widget->property("galleryNode").toString();
            if (!node.isEmpty())
            {
                nodes.insert(node);
            }
        }
    }

    for (const char* node : { "417:1007", "455:1012", "455:1055", "455:1098", "455:1141", "455:1184", "455:1227", "156:4219", "156:4224", "156:4229", "156:4234", "706:149577",
                              "156:4240", "192:840", "192:853", "192:903", "192:916", "192:969", "192:982", "265:871", "265:909", "265:947", "264:853", "706:147295",
                              "706:147349", "706:147403", "706:147457" })
    {
        QVERIFY2(nodes.contains(QString::fromLatin1(node)), node);
    }

    for (const pdffluentgallery::GalleryPages::Page& page : pages)
    {
        delete page.widget;
    }
}

void FluentComponentsTest::textDirection()
{
    // One rule for every painter (components.md 6.6): the painter gets the
    // direction of the state, the alignments are absolute. The result does not
    // depend on the direction of the incoming painter, and the base direction of
    // a text is the direction of the state (not guessed from its content).
    struct Sample
    {
        QString name;
        QSize size;
        std::function<void(QPainter*, const QRectF&, const Painter::State&)> paint;
    };

    const QString text = QStringLiteral("Report v2.0 (draft).");
    const Painter::TextLink link{ text };
    const Painter::Tag tag{ text };
    const Painter::LinkRow linkRow{ text, QStringLiteral("Tell us (more)."), QStringLiteral("alert-circle"), std::nullopt };
    Painter::VersionRow versionRow;
    versionRow.version = QStringLiteral("1.5.1.0 (beta).");
    versionRow.date = QStringLiteral("Sep 28, 2026.");
    versionRow.summary = QStringLiteral("Fixes of the (OCR) dialog. More fixes of the (signature) panel.");
    Painter::IssueRow issueRow;
    issueRow.number = QStringLiteral("#412");
    issueRow.title = text;
    issueRow.description = QStringLiteral("Crash (rare).");
    issueRow.date = QStringLiteral("Sep 2.");
    Painter::PropertyRow propertyRow;
    propertyRow.key = QStringLiteral("Title:");
    propertyRow.value = text;
    Painter::FolderCard folderCard;
    folderCard.name = text;
    folderCard.path = QStringLiteral("C:/Documents (old).");
    folderCard.meta = QStringLiteral("Last used 2 days ago.");
    const Painter::ToolChoice toolChoice{ QStringLiteral("Add Page Numbers (fast)."), QStringLiteral("hash"), pdf::PDFAccentHue::Green };
    Painter::ToolCategoryHeader categoryHeader;
    categoryHeader.title = text;
    categoryHeader.description = QStringLiteral("Merge, split (and more).");
    categoryHeader.count = QStringLiteral("12 tools.");
    const Painter::SignatureCard signatureCard = PanelSamples::signature(Painter::SignatureStatus::Warning, true);
    const Painter::StrengthMeter meter = PanelSamples::meter(Painter::StrengthLevel::Moderate, Painter::StrengthCriterion::Length);
    Painter::PageSelection pageSelection;
    pageSelection.label = QStringLiteral("Pages:");
    pageSelection.total = QStringLiteral("48 pages.");
    pageSelection.filtersLabel = QStringLiteral("Only:");
    pageSelection.status = QStringLiteral("7 pages selected (1 – 12).");
    pageSelection.error = QStringLiteral("Page 60 does not exist (the document has 48 pages).");
    const Painter::ColorButton colorButton{ QColor(Qt::red), QStringLiteral("Red (dark)."), false };

    const QList<Sample> samples =
    {
        { QStringLiteral("Text link"), Painter::getTextLinkSize(link) + QSize(60, 0), [&](QPainter* painter, const QRectF& rect, const Painter::State& state) { Painter::paintTextLink(painter, rect, link, state); } },
        { QStringLiteral("Tag"), Painter::getTagSize(tag) + QSize(40, 0), [&](QPainter* painter, const QRectF& rect, const Painter::State& state) { Painter::paintTag(painter, rect, tag, state); } },
        { QStringLiteral("Link row"), Painter::getLinkRowSize(linkRow), [&](QPainter* painter, const QRectF& rect, const Painter::State& state) { Painter::paintLinkRow(painter, rect, linkRow, state); } },
        { QStringLiteral("Version row"), Painter::getVersionRowSize(versionRow), [&](QPainter* painter, const QRectF& rect, const Painter::State& state) { Painter::paintVersionRow(painter, rect, versionRow, state); } },
        { QStringLiteral("Issue row"), Painter::getIssueRowSize(issueRow), [&](QPainter* painter, const QRectF& rect, const Painter::State& state) { Painter::paintIssueRow(painter, rect, issueRow, state); } },
        { QStringLiteral("Property row"), Painter::getPropertyRowSize(propertyRow), [&](QPainter* painter, const QRectF& rect, const Painter::State& state) { Painter::paintPropertyRow(painter, rect, propertyRow, state); } },
        { QStringLiteral("Folder card"), Painter::getFolderCardSize(folderCard), [&](QPainter* painter, const QRectF& rect, const Painter::State& state) { Painter::paintFolderCard(painter, rect, folderCard, state); } },
        { QStringLiteral("Tool choice"), Painter::getToolChoiceSize(toolChoice), [&](QPainter* painter, const QRectF& rect, const Painter::State& state) { Painter::paintToolChoice(painter, rect, toolChoice, state); } },
        { QStringLiteral("Tool category header"), Painter::getToolCategoryHeaderSize(categoryHeader), [&](QPainter* painter, const QRectF& rect, const Painter::State& state) { Painter::paintToolCategoryHeader(painter, rect, categoryHeader, state); } },
        { QStringLiteral("Signature card"), Painter::getSignatureCardSize(signatureCard), [&](QPainter* painter, const QRectF& rect, const Painter::State& state) { Painter::paintSignatureCard(painter, rect, signatureCard, state); } },
        { QStringLiteral("Strength meter"), Painter::getStrengthMeterSize(meter), [&](QPainter* painter, const QRectF& rect, const Painter::State& state) { Painter::paintStrengthMeter(painter, rect, meter, state); } },
        { QStringLiteral("Page selection"), Painter::getPageSelectionSize(pageSelection), [&](QPainter* painter, const QRectF& rect, const Painter::State& state) { Painter::paintPageSelection(painter, rect, pageSelection, state); } },
        { QStringLiteral("Color button"), Painter::getColorButtonSize(colorButton), [&](QPainter* painter, const QRectF& rect, const Painter::State& state) { Painter::paintColorButton(painter, rect, colorButton, state); } }
    };

    for (const Sample& sample : samples)
    {
        for (const Qt::LayoutDirection stateDirection : { Qt::LeftToRight, Qt::RightToLeft })
        {
            const Painter::State sampleState(QStyle::State_Enabled, stateDirection);
            QImage images[3];
            const Qt::LayoutDirection painterDirections[3] = { Qt::LeftToRight, Qt::RightToLeft, Qt::LayoutDirectionAuto };
            for (int i = 0; i < 3; ++i)
            {
                images[i] = render([&](QPainter& painter)
                {
                    painter.setLayoutDirection(painterDirections[i]);
                    sample.paint(&painter, QRectF(QPointF(0, 0), sample.size), sampleState);
                    QCOMPARE(painter.layoutDirection(), painterDirections[i]);
                }, sample.size);
            }

            for (int i = 1; i < 3; ++i)
            {
                const QString difference = describeDifference(images[0], images[i], 0);
                QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("%1, state %2, painter %3: %4").arg(sample.name).arg(int(stateDirection)).arg(int(painterDirections[i])).arg(difference)));
            }
        }
    }

    // The base direction is the direction of the state: "abc." of a right-to-left
    // link is shown as ".abc" (with the direction guessed from the content it
    // would stay "abc."): the leftmost glyph is the dot, its ink is only a few
    // rows high (the letter "a" has the x-height)
    const Painter::TextLink trailingDot{ QStringLiteral("abc.") };
    const QSize dotSize = Painter::getTextLinkSize(trailingDot);
    auto leftmostInkHeight = [&](const QImage& image)
    {
        // The rows of the ink of the first three columns with ink
        const QColor background = token(Color::BgSurface);
        int first = -1;
        int top = -1;
        int bottom = -1;
        for (int x = 0; x < image.width() && (first < 0 || x < first + 3); ++x)
        {
            for (int y = 0; y < image.height(); ++y)
            {
                if (colorDistance(image.pixelColor(x, y), background) > 100)
                {
                    first = first < 0 ? x : first;
                    top = top < 0 ? y : qMin(top, y);
                    bottom = qMax(bottom, y);
                }
            }
        }
        return top >= 0 ? bottom - top + 1 : 0;
    };
    const QImage rightToLeftLink = render([&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(QPointF(0, 0), dotSize), trailingDot, state(QStyle::State_None, Qt::RightToLeft)); }, dotSize);
    const QImage leftToRightLink = render([&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(QPointF(0, 0), dotSize), trailingDot, state(QStyle::State_None, Qt::LeftToRight)); }, dotSize);
    QVERIFY2(leftmostInkHeight(rightToLeftLink) > 0 && leftmostInkHeight(rightToLeftLink) <= 3, qPrintable(QString::number(leftmostInkHeight(rightToLeftLink))));
    QVERIFY2(leftmostInkHeight(leftToRightLink) > 3, qPrintable(QString::number(leftmostInkHeight(leftToRightLink))));

    // Widgets of every step draw as the painter with a left-to-right painter for their right-to-left state
    QWidget window;
    window.resize(900, 700);
    window.setLayoutDirection(Qt::RightToLeft);
    pdf::PDFLinkRow* linkRowWidget = new pdf::PDFLinkRow(linkRow.title, linkRow.subtitle, linkRow.iconName, &window);
    linkRowWidget->setGeometry(QRect(QPoint(0, 0), Painter::getLinkRowSize(linkRow)));
    pdf::PDFToolChoice* choiceWidget = new pdf::PDFToolChoice(toolChoice.name, toolChoice.iconName, toolChoice.hue, &window);
    choiceWidget->setGeometry(QRect(QPoint(400, 0), Painter::getToolChoiceSize(toolChoice)));
    pdf::PDFStrengthMeter* meterWidget = new pdf::PDFStrengthMeter(&window);
    meterWidget->setLevel(meter.level);
    meterWidget->setDetail(meter.detail);
    meterWidget->setSatisfiedCriteria(meter.satisfied);
    meterWidget->setGeometry(QRect(QPoint(0, 100), Painter::getStrengthMeterSize(meterWidget->getContent())));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.setFocus(Qt::OtherFocusReason);

    QCOMPARE(describeDifference(renderWidget(linkRowWidget, 1.0),
                                renderInWidget([&](QPainter& painter) { Painter::paintLinkRow(&painter, QRectF(linkRowWidget->rect()), linkRowWidget->getContent(), linkRowWidget->getComponentState()); }, linkRowWidget->size()), 3), QString());
    QCOMPARE(describeDifference(renderWidget(choiceWidget, 1.0),
                                renderInWidget([&](QPainter& painter) { Painter::paintToolChoice(&painter, QRectF(choiceWidget->rect()), choiceWidget->getContent(), choiceWidget->getComponentState()); }, choiceWidget->size()), 3), QString());
    QCOMPARE(describeDifference(renderWidget(meterWidget, 1.0),
                                renderInWidget([&](QPainter& painter) { Painter::paintStrengthMeter(&painter, QRectF(meterWidget->rect()), meterWidget->getContent(), state(QStyle::State_None, Qt::RightToLeft)); }, meterWidget->size()), 3), QString());
}

void FluentComponentsTest::keyValueListDirection()
{
    // Review C3: the key lies at the leading edge of its column, the value at the trailing edge
    pdf::PDFKeyValueList list;
    list.addRow(QStringLiteral("Key"), QStringLiteral("Value"));
    list.resize(400, 28);
    const QColor surface = token(Color::BgSurface);

    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        list.setLayoutDirection(direction);
        const QImage image = renderWidget(&list, 1.0);
        const bool rtl = direction == Qt::RightToLeft;

        // Columns: the key 120 wide at the leading padding 10, the value after the gap 12
        const QRect keyLeading = rtl ? QRect(330, 0, 60, 28) : QRect(10, 0, 60, 28);
        const QRect keyTrailing = rtl ? QRect(270, 0, 55, 28) : QRect(75, 0, 55, 28);
        const QRect valueTrailing = rtl ? QRect(10, 0, 60, 28) : QRect(330, 0, 60, 28);
        const QRect valueLeading = rtl ? QRect(120, 0, 138, 28) : QRect(142, 0, 138, 28);

        QVERIFY2(colorDistance(strongestInk(image, keyLeading, surface), token(Color::TextTertiary)) < 60, rtl ? "rtl key" : "ltr key");
        QVERIFY2(isNear(strongestInk(image, keyTrailing, surface), surface), rtl ? "rtl key trailing" : "ltr key trailing");
        QVERIFY2(colorDistance(strongestInk(image, valueTrailing, surface), token(Color::TextPrimary)) < 60, rtl ? "rtl value" : "ltr value");
        QVERIFY2(isNear(strongestInk(image, valueLeading, surface), surface), rtl ? "rtl value leading" : "ltr value leading");
    }
}

void FluentComponentsTest::keyValueListElision()
{
    // O18: a row elides its value at the end (Data) or in the middle keeping the
    // extension (FileName - a path of a program); a value is always data
    pdf::PDFKeyValueList list;
    const QString path = QStringLiteral("C:\\Users\\Jane\\Downloads\\") + QString(80, QLatin1Char('x')) + QStringLiteral("\\invoice.pdf.exe");
    QCOMPARE(list.addRow(QStringLiteral("Program"), path, pdf::PDFComponentPainter::TextKind::FileName), 0);
    QCOMPARE(list.addRow(QStringLiteral("Data"), path), 1);
    QCOMPARE(list.addRow(QStringLiteral("Data"), path, pdf::PDFComponentPainter::TextKind::Interface), 2);
    QCOMPARE(list.getValueKind(0), pdf::PDFComponentPainter::TextKind::FileName);
    QCOMPARE(list.getValueKind(1), pdf::PDFComponentPainter::TextKind::Data);
    QCOMPARE(list.getValueKind(2), pdf::PDFComponentPainter::TextKind::Data);
    QCOMPARE(list.getValueKind(-1), pdf::PDFComponentPainter::TextKind::Data);
    QCOMPARE(list.getValueKind(3), pdf::PDFComponentPainter::TextKind::Data);

    const QFont font = pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::SmallMedium);
    const QString middle = pdf::PDFComponentPainter::elidedTextOfKind(path, font, 200, pdf::PDFComponentPainter::TextKind::FileName);
    const QString end = pdf::PDFComponentPainter::elidedTextOfKind(path, font, 200, pdf::PDFComponentPainter::TextKind::Data);
    QVERIFY2(middle.endsWith(QStringLiteral("invoice.pdf.exe")) && middle.startsWith(QStringLiteral("C:\\")), qPrintable(middle));
    QVERIFY(end.endsWith(QChar(0x2026)));
    QCOMPARE(pdf::PDFComponentPainter::elidedTextOfKind(path, font, 200, pdf::PDFComponentPainter::TextKind::Interface), end);
    QVERIFY(pdf::PDFComponentPainter::elidedTextOfKind(path, font, 0, pdf::PDFComponentPainter::TextKind::FileName).isEmpty());
    QCOMPARE(pdf::PDFComponentPainter::elidedTextOfKind(QStringLiteral("a.pdf"), font, 200, pdf::PDFComponentPainter::TextKind::FileName), QStringLiteral("a.pdf"));

    // The rows draw the elisions: the end of the FileName row has ink (the
    // extension), the rows differ; both directions
    list.resize(400, 3 * 28);
    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        list.setLayoutDirection(direction);
        const QImage image = renderWidget(&list, 1.0);
        const QImage fileRow = image.copy(QRect(0, 0, 400, 28));
        const QImage dataRow = image.copy(QRect(0, 28, 400, 28));
        QVERIFY(fileRow != dataRow);
        QVERIFY(dataRow == image.copy(QRect(0, 56, 400, 28)));
    }
}

void FluentComponentsTest::pageRangeReview()
{
    using Range = pdf::PDFPageRange;
    using Interval = Range::Interval;

    // Review C11: the separators of other scripts and keyboards
    const QList<QPair<QString, QList<Interval>>> cases =
    {
        { QStringLiteral("\u06F1-\u06F3\u060C \u06F8"), { { 1, 3 }, { 8, 8 } } },     // Persian digits, Arabic comma
        { QStringLiteral("\uFF11\uFF0D\uFF13\uFF0C\uFF18"), { { 1, 3 }, { 8, 8 } } },  // fullwidth digits, hyphen-minus and comma
        { QStringLiteral("1\u22123, 5\u20116, 8\u20109"), { { 1, 3 }, { 5, 6 }, { 8, 9 } } },  // minus sign, non-breaking hyphen, hyphen
        { QStringLiteral("2\u30014\u30016\u20137"), { { 2, 2 }, { 4, 4 }, { 6, 7 } } } // ideographic comma, en dash
    };
    for (const auto& testCase : cases)
    {
        const Range::Result result = Range::parse(testCase.first, 10);
        QVERIFY2(result.isValid(), qPrintable(result.errorMessage));
        QCOMPARE(result.intervals, testCase.second);
    }
    QVERIFY(Range::isListSeparator(QChar(0x060C)));
    QVERIFY(!Range::isListSeparator(QLatin1Char('-')));
    QVERIFY(Range::isRangeSeparator(QChar(0x2212)));
    QVERIFY(!Range::isRangeSeparator(QLatin1Char(',')));

    // Digits outside of the Basic Multilingual Plane are not numbers (documented)
    QVERIFY(!Range::parse(QString::fromUcs4(U"\U0001D7CF"), 10).isValid());

    // Review C22: the quoted part is isolated, a long part is not cut inside of a surrogate pair
    const QString rightToLeftPart = QStringLiteral("\u05D0\u05D1");
    QCOMPARE(Range::parse(rightToLeftPart, 10).errorMessage, QStringLiteral("Can't convert '\u2068\u05D0\u05D1\u2069' to a number."));
    const QString surrogates = QString(31, QLatin1Char('x')) + QString::fromUcs4(U"\U0001F600\U0001F600");
    const QString message = Range::parse(surrogates, 10).errorMessage;
    QVERIFY(message.contains(QString(31, QLatin1Char('x')) + QChar(0x2026)));
    for (qsizetype i = 0; i < message.size(); ++i)
    {
        QVERIFY(!message.at(i).isHighSurrogate() || (i + 1 < message.size() && message.at(i + 1).isLowSurrogate()));
    }

    // Review C20: intervals, which were not normalized, are tolerated
    Range::Result raw;
    raw.intervals = { { 5, 3 } };
    QCOMPARE(raw.getCount(), 0);
    QVERIFY(raw.getPages().isEmpty());
    raw.intervals = { { 1, std::numeric_limits<int>::max() } };
    QCOMPARE(raw.getCount(), qint64(std::numeric_limits<int>::max()));
    QCOMPARE(raw.getPages().size(), Range::MAX_PAGE_COUNT);

    // Review C9 and F5: the plural forms of the language, the count by the locale
    const QLocale previousLocale;
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QCOMPARE(Range::getMissingPageText(1600, 1592), QStringLiteral("Page 1600 does not exist – the document has 1,592 pages"));
    QCOMPARE(Range::getMissingPageText(2, 1), QStringLiteral("Page 2 does not exist – the document has 1 page"));
    QLocale::setDefault(QLocale(QLocale::Czech, QLocale::CzechRepublic));
    QCOMPARE(Range::getMissingPageText(1600, 1592), QStringLiteral("Page 1600 does not exist – the document has 1\u00A0592 pages"));
    QLocale::setDefault(previousLocale);
}

/// Translator, which gives the plural forms of the count of the pages (as Czech: 1, 2 to 4, 5 and more)
class PluralTestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(context);
        Q_UNUSED(disambiguation);

        const QByteArrayView source(sourceText);
        if (source == "%Ln pages" || source == "%Ln page")
        {
            return n == 1 ? QStringLiteral("%Ln strana") : (n >= 2 && n <= 4 ? QStringLiteral("%Ln strany") : QStringLiteral("%Ln stran"));
        }
        return QString();
    }

    virtual bool isEmpty() const override { return false; }
};

void FluentComponentsTest::pageSelectionReview()
{
    using Selection = pdf::PDFPageSelection;
    using Mode = Selection::Mode;
    using Filter = Selection::Filter;

    const QLocale previousLocale;
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));

    // Review C2: a slot, which deletes the widget, in setPageCount, setLabel and setCustomError
    for (int variant = 0; variant < 3; ++variant)
    {
        QPointer<Selection> deleted = new Selection();
        deleted->setPageCount(5);
        connect(deleted.data(), &Selection::resultChanged, deleted.data(), [&deleted]() { delete deleted.data(); });
        switch (variant)
        {
            case 0:
                deleted->setPageCount(8);
                break;
            case 1:
                deleted->setPageLabels({ QStringLiteral("i") });
                break;
            default:
                deleted->setCustomError(QStringLiteral("Locked"));
                break;
        }
        QVERIFY2(!deleted, qPrintable(QString::number(variant)));
    }

    // Review C15: the range field is placed and shown, when resultChanged comes after a switch to Range
    Selection selection;
    selection.setPageCount(1592);
    selection.setRange(QStringLiteral("1-3"));
    bool isFieldShown = false;
    QMetaObject::Connection connection = connect(&selection, &Selection::resultChanged, &selection, [&]()
    {
        isFieldShown = !selection.getRangeField()->isHidden() && selection.getRangeField()->height() == 34;
    });
    selection.resize(320, 200);
    selection.setMode(Mode::Range);
    QVERIFY(isFieldShown);
    disconnect(connection);

    // Review C7: inputs, which the mode does not use, do not change the result
    QSignalSpy resultSpy(&selection, &Selection::resultChanged);
    selection.setCurrentPage(7);
    selection.setSelectedPages({ 1, 2 });
    QList<QSizeF> sizes(1592, QSizeF(595, 842));
    selection.setPageSizes(sizes);
    QCOMPARE(resultSpy.count(), 0);
    selection.setFilters(Filter::Landscape);
    QCOMPARE(selection.getResultCount(), 0);
    sizes[1] = QSizeF(842, 595);
    selection.setPageSizes(sizes);
    QCOMPARE(selection.getResultPages(), QList<int>{ 2 });
    QCOMPARE(resultSpy.count(), 2);
    selection.setFilters(Filter::Even | Filter::Odd);
    QCOMPARE(selection.getResultCount(), 3);
    resultSpy.clear();
    selection.setFilters(Filter::None);
    QCOMPARE(resultSpy.count(), 0);

    // Review F5 and C9: the numbers of the locale, the plural forms of the language
    selection.setMode(Mode::All);
    QCOMPARE(selection.getContent().total, QStringLiteral("1,592 pages"));
    QCOMPARE(selection.getStatusText(), QStringLiteral("1,592 pages selected · 1 – 1,592"));
    selection.setRange(QStringLiteral("1600"));
    QCOMPARE(selection.getRangeError(), QStringLiteral("Page 1600 does not exist – the document has 1,592 pages"));
    selection.setMode(Mode::Current);
    QCOMPARE(selection.getStatusText(), QStringLiteral("1 page selected · 7"));
    PluralTestTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    QTRY_COMPARE(selection.getContent().total, QStringLiteral("1,592 stran"));
    selection.setPageCount(3);
    QCOMPARE(selection.getContent().total, QStringLiteral("3 strany"));
    selection.setPageCount(1);
    QCOMPARE(selection.getContent().total, QStringLiteral("1 strana"));
    QVERIFY(QCoreApplication::removeTranslator(&translator));
    QTRY_COMPARE(selection.getContent().total, QStringLiteral("1 page"));

    // Review C10: the selection, which becomes invalid, is announced assertively, a new message politely
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    Selection* shown = new Selection(&window);
    shown->setPageCount(48);
    shown->setMode(Mode::Range);
    layout->addWidget(shown);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
#if QT_CONFIG(accessibility) && QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    s_politeness.clear();
    const QAccessible::UpdateHandler previousHandler = QAccessible::installUpdateHandler(&FluentComponentsTest::collectAccessibleEvent);
    shown->setRange(QStringLiteral("60"));
    shown->setRange(QStringLiteral("600"));
    shown->setRange(QStringLiteral("6"));
    shown->setRange(QStringLiteral("70"));
    QAccessible::installUpdateHandler(previousHandler);
    QCOMPARE(s_politeness, (QList<int>{ int(QAccessible::AnnouncementPoliteness::Assertive), int(QAccessible::AnnouncementPoliteness::Polite), int(QAccessible::AnnouncementPoliteness::Assertive) }));
#endif

    // Review C24: accessible names of the application are kept over a new language
    shown->getRangeField()->setAccessibleName(QStringLiteral("Pages to print"));
    shown->getModeControl()->setAccessibleName(QStringLiteral("Print what"));
    PanelTestTranslator panelTranslator;
    QVERIFY(QCoreApplication::installTranslator(&panelTranslator));
    QTRY_COMPARE(shown->getLabel(), QStringLiteral("Seiten"));
    QCOMPARE(shown->getRangeField()->accessibleName(), QStringLiteral("Pages to print"));
    QCOMPARE(shown->getModeControl()->accessibleName(), QStringLiteral("Print what"));
    QVERIFY(QCoreApplication::removeTranslator(&panelTranslator));
    QTRY_COMPARE(shown->getLabel(), QStringLiteral("Pages"));

    // Review C23: a range longer than the field is cut, the widget keeps the text of the field
    const QString longRange = QString(Selection::MAX_RANGE_LENGTH + 100, QLatin1Char('1'));
    shown->setRange(longRange);
    QCOMPARE(shown->getRange().size(), Selection::MAX_RANGE_LENGTH);
    QCOMPARE(shown->getRange(), shown->getRangeField()->text());
    delete shown->getRangeField();
    shown->setRange(longRange + QStringLiteral("2"));
    QCOMPARE(shown->getRange().size(), Selection::MAX_RANGE_LENGTH);

    // Review C4: the count of the header ends at the trailing edge of the widget (not 1 px beyond it)
    Painter::PageSelection content;
    content.total = QStringLiteral("48 pages");
    const QFont totalFont = pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Caption);
    const qreal totalWidth = qCeil(pdf::PDFStylePainter::textWidth(content.total, totalFont));
    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        const bool rtl = direction == Qt::RightToLeft;
        const QImage widgetHeader = render([&](QPainter& painter) { Painter::paintPageSelection(&painter, QRectF(0, 0, 320, 156), content, state(QStyle::State_None, direction)); }, QSize(320, 156)).copy(0, 0, 320, 15);
        const QImage expected = render([&](QPainter& painter)
        {
            const QRectF rect(rtl ? 0.0 : 320.0 - totalWidth, 1.0, totalWidth, 13.0);
            painter.setLayoutDirection(direction);
            pdf::PDFStylePainter::text(&painter, rect, Qt::AlignVCenter | Qt::TextSingleLine | (rtl ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignAbsolute, content.total, totalFont, token(Color::TextTertiary));
        }, QSize(320, 156)).copy(0, 0, 320, 15);
        QCOMPARE(describeDifference(widgetHeader, expected, 3), QString());
    }

    // Review F2: the icon of an error of two lines is centered on the lines
    Painter::PageSelection wrapped;
    wrapped.error = QStringLiteral("Page 1600 does not exist – the document has 48 pages, check the range");
    const Painter::PageSelectionLayout wrappedLayout = Painter::getPageSelectionLayout(wrapped, 279);
    QCOMPARE(wrappedLayout.error.height(), 26);
    const QImage wrappedImage = render([&](QPainter& painter) { Painter::paintPageSelection(&painter, QRectF(0, 0, 279, wrappedLayout.height), wrapped, state(QStyle::State_None)); }, QSize(279, wrappedLayout.height));
    const int iconTop = wrappedLayout.error.top() + 6;
    QVERIFY(isNear(strongestInk(wrappedImage, QRect(0, wrappedLayout.error.top(), 14, 5), token(Color::BgSurface)), token(Color::BgSurface), 24));
    QVERIFY(colorDistance(strongestInk(wrappedImage, QRect(0, iconTop, 14, 14), token(Color::BgSurface)), token(Color::IconDanger)) < 80);

    QLocale::setDefault(previousLocale);
}

void FluentComponentsTest::pageSelectionHugeDocument()
{
    using Selection = pdf::PDFPageSelection;
    using Filter = Selection::Filter;

    // Review C7: ten million pages with a parity filter are counted, not materialized
    const QLocale previousLocale;
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));

    Selection selection;
    QElapsedTimer timer;
    timer.start();
    selection.setPageCount(pdf::PDFPageRange::MAX_PAGE_COUNT);
    selection.setFilters(Filter::Even);
    for (int page = 1; page <= 1000; ++page)
    {
        // The application follows the current page; the mode All does not use it
        selection.setCurrentPage(page);
    }
    selection.setPageLabels({ QStringLiteral("i") });
    QCOMPARE(selection.getResultCount(), pdf::PDFPageRange::MAX_PAGE_COUNT / 2);
    QCOMPARE(selection.getStatusText(), QStringLiteral("5,000,000 pages selected · 2 – 10,000,000"));
    QVERIFY2(timer.elapsed() < 1000, qPrintable(QString::number(timer.elapsed())));

    // A range with a parity filter: the first and the last page of the parity
    selection.setRange(QStringLiteral("3-9, 20-21"));
    selection.setMode(Selection::Mode::Range);
    QCOMPARE(selection.getResultCount(), 4);
    QCOMPARE(selection.getStatusText(), QStringLiteral("4 pages selected · 4 – 20"));
    QCOMPARE(selection.getResultIntervals(), (QList<pdf::PDFPageRange::Interval>{ { 4, 4 }, { 6, 6 }, { 8, 8 }, { 20, 20 } }));
    selection.setFilters(Filter::Odd);
    QCOMPARE(selection.getResultPages(), (QList<int>{ 3, 5, 7, 9, 21 }));
    selection.setRange(QStringLiteral("4"));
    QCOMPARE(selection.getResultCount(), 0);
    QVERIFY(selection.getResultIntervals().isEmpty());

    // The intervals are built only when they are asked for (and the count matches them)
    selection.setMode(Selection::Mode::All);
    selection.setFilters(Filter::None);
    QCOMPARE(selection.getResultIntervals(), (QList<pdf::PDFPageRange::Interval>{ { 1, pdf::PDFPageRange::MAX_PAGE_COUNT } }));

    QLocale::setDefault(previousLocale);
}

void FluentComponentsTest::continueWithReview()
{
    // Review F1: the row of the choices is as high as the tallest choice (46 for names of one line, 418:32221)
    const QList<Painter::ToolChoice> shortChoices = { Painter::ToolChoice{ QStringLiteral("Compress"), QStringLiteral("minimize-2"), pdf::PDFAccentHue::Violet },
                                                      Painter::ToolChoice{ QStringLiteral("Protect with Password"), QStringLiteral("lock"), pdf::PDFAccentHue::Red } };
    Painter::ContinueWith content = PanelSamples::continueWith();
    content.choices = shortChoices;
    QCOMPARE(Painter::getContinueWithHeight(content, 474), 91);
    QCOMPARE(Painter::getContinueWithLayout(content, 474).choices.at(1), QRect(241, 24, 233, 46));
    QCOMPARE(Painter::getContinueWithHeight(PanelSamples::continueWith(), 480), 93);
    content.choices.clear();
    QCOMPARE(Painter::getContinueWithHeight(content, 480), 37);

    // The Tool choice hugs its name, the height does not change with the selection
    const Painter::ToolChoice compress = shortChoices.at(0);
    const Painter::ToolChoice numbers = PanelSamples::choices().at(0);
    QCOMPARE(Painter::getToolChoiceSize(compress), QSize(154, 46));
    QCOMPARE(Painter::getToolChoiceSize(numbers), QSize(154, 48));
    pdf::PDFToolChoice choice(compress.name, compress.iconName, compress.hue);
    QCOMPARE(choice.sizeHint(), QSize(154, 46));
    QVERIFY(choice.sizePolicy().hasHeightForWidth());
    choice.setCheckable(true);
    choice.setSelected(true);
    QCOMPARE(choice.sizeHint(), QSize(154, 46));
    QCOMPARE(choice.heightForWidth(154), 46);
    choice.setName(numbers.name);
    QCOMPARE(choice.heightForWidth(154), 48);

    // Review F3: the minimal width keeps the longest word of each name whole
    const QFont nameFont = pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::SmallMedium);
    const Painter::ToolChoice password = shortChoices.at(1);
    const int minimalChoice = Painter::getToolChoiceMinimumSize(password).width();
    const int nameWidth = minimalChoice - 2 * 11 - 28 - 2 * 8 - 16;
    for (const QString& line : Painter::wrapText(password.name, nameFont, nameWidth))
    {
        QVERIFY2(password.name.split(QLatin1Char(' ')).contains(line) || line.contains(QLatin1Char(' ')), qPrintable(line));
    }
    QVERIFY(Painter::getLongestWordWidth(QStringLiteral("Protect with Password"), nameFont) >= pdf::PDFStylePainter::textWidth(QStringLiteral("Password"), nameFont) - 0.01);
    QCOMPARE(Painter::getLongestWordWidth(QString(), nameFont), 0.0);
    pdf::PDFContinueWith widget;
    widget.setChoices(PanelSamples::choices());
    const int minimalWidth = widget.minimumSizeHint().width();
    QVERIFY(minimalWidth >= 3 * Painter::getToolChoiceMinimumSize(password).width() + 2 * 8);
    const Painter::ContinueWithLayout narrow = Painter::getContinueWithLayout(widget.getContent(), minimalWidth);
    // The list is kept: a reference into the list returned by value would dangle
    const QList<Painter::ToolChoice> choices = PanelSamples::choices();
    for (int i = 0; i < 3; ++i)
    {
        const Painter::ToolChoice& tool = choices.at(i);
        for (const QString& line : Painter::wrapText(tool.name, nameFont, narrow.choices.at(i).width() - 2 * 11 - 28 - 2 * 8 - 16))
        {
            QVERIFY2(tool.name.split(QLatin1Char(' ')).contains(line.split(QLatin1Char(' ')).constLast()), qPrintable(line));
        }
    }

    // Review F4: the current tool in the caption, a custom pattern with both names
    widget.setSelectedChoice(0);
    QCOMPARE(widget.getCurrentCaption(), QStringLiteral("Add Page Numbers starts on the new document after this tool finishes successfully."));
    widget.setCurrentToolName(QStringLiteral("Merge"));
    QCOMPARE(widget.getCurrentCaption(), QStringLiteral("Add Page Numbers starts on the new document after Merge finishes successfully."));
    QCOMPARE(QAccessible::queryAccessibleInterface(&widget)->text(QAccessible::Description), widget.getCurrentCaption());
    widget.setSelectionCaption(QStringLiteral("%2 → %1 (100%)"));
    QCOMPARE(widget.getCurrentCaption(), QStringLiteral("Merge → Add Page Numbers (100%)"));
    QList<Painter::ToolChoice> tricky = PanelSamples::choices();
    tricky[0].name = QStringLiteral("Tool %2");
    widget.setChoices(tricky);
    widget.setSelectedChoice(0);
    QCOMPARE(widget.getCurrentCaption(), QStringLiteral("Merge → Tool %2 (100%)"));

    // Review C14: the slot of the cleared selection sees the new choices
    widget.setChoices(PanelSamples::choices());
    widget.setSelectedChoice(2);
    QStringList seenNames;
    connect(&widget, &pdf::PDFContinueWith::selectedChoiceChanged, &widget, [&](int index)
    {
        QCOMPARE(index, -1);
        for (const Painter::ToolChoice& tool : widget.getChoices())
        {
            seenNames << tool.name;
        }
    });
    widget.setChoices(shortChoices);
    QCOMPARE(seenNames, (QStringList{ QStringLiteral("Compress"), QStringLiteral("Protect with Password") }));
    QCOMPARE(widget.getSelectedChoice(), -1);
    for (int i = 0; i < 2; ++i)
    {
        QVERIFY(!widget.getChoiceWidget(i)->isSelected());
    }
}

void FluentComponentsTest::colorButtonReview()
{
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    pdf::PDFColorButton* button = new pdf::PDFColorButton(QColor(Qt::red), QStringLiteral("Signal red"), &window);
    layout->addWidget(button);
    window.resize(400, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // Review C1: a new color drops the explicit name of the previous one, also after a pick
    pdf::PDFColorPalette* palette = button->getPalette();
    palette->setSuggestedColors(PanelSamples::suggested());
    QCOMPARE(button->getShownName(), QStringLiteral("Signal red"));
    QVERIFY(button->showPalette());
    palette->getSuggestedSwatch(6)->click();
    QCOMPARE(button->getShownName(), QStringLiteral("Terminal green"));
    QVERIFY(button->getColorName().isEmpty());
    button->setColor(QColor(QStringLiteral("#123456")));
    QCOMPARE(button->getShownName(), QStringLiteral("#123456"));
    QCOMPARE(button->text(), QStringLiteral("#123456"));
    button->setColorName(QStringLiteral("Navy"));
    button->setColor(QColor(QStringLiteral("#111111")));
    QCOMPARE(button->getShownName(), QStringLiteral("Black"));
    button->setColor(QColor(QStringLiteral("#010203")));
    QCOMPARE(button->getShownName(), QStringLiteral("#010203"));

    // Review C12: the text follows the names of the palette
    QList<pdf::PDFColorPalette::Entry> named = PanelSamples::suggested();
    named << pdf::PDFColorPalette::Entry{ QColor(QStringLiteral("#010203")), QStringLiteral("Almost black") };
    palette->setSuggestedColors(named);
    QCOMPARE(button->text(), QStringLiteral("Almost black"));
    QCOMPARE(QAccessible::queryAccessibleInterface(button)->text(QAccessible::Name), QStringLiteral("Almost black"));

    // Review C17: the button is expandable, expanded while its palette is shown
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(button);
    QVERIFY(accessible->state().expandable);
    QVERIFY(!accessible->state().expanded);
    QVERIFY(accessible->state().collapsed);
    s_accessibleEvents.clear();
    const QAccessible::UpdateHandler previousHandler = QAccessible::installUpdateHandler(&FluentComponentsTest::collectAccessibleEvent);
    QVERIFY(button->showPalette());
    QVERIFY(accessible->state().expanded);
    QVERIFY(countAccessibleEvents(button, QAccessible::StateChanged) >= 1);
    palette->close();
    QTRY_VERIFY(!accessible->state().expanded);
    QAccessible::installUpdateHandler(previousHandler);

    // Review C8 and C28: the signal closed is queued, a slot may delete the palette; a palette opened again drops it
    QVERIFY(button->showPalette());
    palette->close();
    QVERIFY(button->showPalette());
    QSignalSpy closedSpy(palette, &pdf::PDFColorPalette::closed);
    QApplication::processEvents();
    QCOMPARE(closedSpy.count(), 0);
    QVERIFY(palette->isVisible());
    QPointer<pdf::PDFColorPalette> guard(palette);
    connect(palette, &pdf::PDFColorPalette::closed, palette, [&guard]() { delete guard.data(); });
    palette->close();
    QTRY_VERIFY(!guard);
    QVERIFY(!button->isPaletteVisible());

    // Review C19: the popover of the style draws its panel by the style
    pdf::PDFColorPalette* newPalette = button->getPalette();
    newPalette->setSuggestedColors(PanelSamples::suggested());
    QVERIFY(button->showPalette());
    QCOMPARE(countLeakPixels(renderWidget(newPalette, 1.0)), 0);
    newPalette->close();
}

void FluentComponentsTest::signatureAndMeterReview()
{
    using Level = Painter::StrengthLevel;

    // Review C13: the level Empty fills no cell, also with its own count of the cells
    pdf::PDFStrengthMeter meter;
    meter.setFilledCells(5);
    QCOMPARE(meter.getFilledCells(), 0);
    meter.setLevel(Level::Weak);
    QCOMPARE(meter.getFilledCells(), 5);
    meter.setLevel(Level::Empty);
    meter.resize(300, 50);
    const QImage empty = renderWidget(&meter, 1.0);
    QVERIFY(isNear(pixel(empty, 10, 24), token(Color::BorderDivider)));

    // Review C16: a new level of a visible meter is announced politely, the property and its signal
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    pdf::PDFStrengthMeter* shown = new pdf::PDFStrengthMeter(&window);
    layout->addWidget(shown);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QSignalSpy levelSpy(shown, &pdf::PDFStrengthMeter::levelChanged);
#if QT_CONFIG(accessibility) && QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    s_accessibleEvents.clear();
    s_politeness.clear();
    const QAccessible::UpdateHandler previousHandler = QAccessible::installUpdateHandler(&FluentComponentsTest::collectAccessibleEvent);
    shown->setLevel(Level::Weak);
    shown->setLevel(Level::Weak);
    shown->setLevel(Level::Empty);
    QAccessible::installUpdateHandler(previousHandler);
    QCOMPARE(countAccessibleEvents(shown, QAccessible::Announcement), 1);
    QCOMPARE(s_politeness, QList<int>{ int(QAccessible::AnnouncementPoliteness::Polite) });
#else
    shown->setLevel(Level::Weak);
    shown->setLevel(Level::Empty);
#endif
    QCOMPARE(levelSpy.count(), 2);
    QVERIFY(shown->setProperty("level", QVariant::fromValue(Level::Strong)));
    QCOMPARE(shown->getLevel(), Level::Strong);
    QCOMPARE(shown->property("level").value<Level>(), Level::Strong);

    // Review F6: the level is elided 8 px before the detail
    Painter::StrengthMeter longLevel = PanelSamples::meter(Level::Empty);
    longLevel.title = QString(80, QLatin1Char('W'));
    const QFont detailFont = pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::CaptionRegular);
    const int detailWidth = qCeil(pdf::PDFStylePainter::textWidth(longLevel.detail, detailFont));
    const QImage levelImage = render([&](QPainter& painter) { Painter::paintStrengthMeter(&painter, QRectF(0, 0, 300, 50), longLevel, state(QStyle::State_None)); }, QSize(300, 50));
    QVERIFY(isNear(strongestInk(levelImage, QRect(300 - detailWidth - 8, 0, 8, 15), token(Color::BgSurface)), token(Color::BgSurface), 8));

    // Review C17 and C29: the toggle of each card says its signer; the description of a certificate
    pdf::PDFSignatureCard card;
    PanelSamples::fill(&card, PanelSamples::signature(Painter::SignatureStatus::Valid, true));
    QCOMPARE(card.getToggleButton()->accessibleName(), QStringLiteral("Hide details of Jane Novak"));
    card.setSigner(QString());
    QCOMPARE(card.getToggleButton()->accessibleName(), QStringLiteral("Hide details"));
    QVERIFY(QAccessible::queryAccessibleInterface(&card)->text(QAccessible::Description).contains(QStringLiteral("Example Issuing CA 2, Intermediate")));
}

/// Models and contents of the delegates of the step 5 as the default variants of the design
class DelegateSamples
{
public:
    DelegateSamples() = delete;

    using Roles = pdf::PDFComponentRoles;

    /// A solid picture of a page of the color
    static QPixmap page(const QColor& color)
    {
        QPixmap pixmap(QSize(216, 296));
        pixmap.fill(color);
        return pixmap;
    }

    static QStandardItem* item(const QString& text, bool editable = false)
    {
        QStandardItem* item = new QStandardItem(text);
        item->setEditable(editable);
        return item;
    }

    static void thumbnails(QStandardItemModel* model)
    {
        for (int i = 0; i < 3; ++i)
        {
            QStandardItem* row = item(QString::number(307 + i));
            row->setData(page(QColor(200, 30, 30)), Roles::PixmapRole);
            model->appendRow(row);
        }
    }

    static void bookmarks(QStandardItemModel* model)
    {
        for (int i = 0; i < 3; ++i)
        {
            QStandardItem* row = item(QStringLiteral("Page %1").arg(17 + i));
            row->setData(QStringLiteral("Executive Summary"), Roles::SubtitleRole);
            row->setData(QStringLiteral("Auto"), Roles::TagRole);
            model->appendRow(row);
        }
    }

    static void notes(QStandardItemModel* model)
    {
        for (int i = 0; i < 3; ++i)
        {
            QStandardItem* row = item(QStringLiteral("Jakub Melka"));
            row->setData(QStringLiteral("Sep 28"), Roles::DateRole);
            row->setData(QStringLiteral("Check these numbers against the Q3 report before publishing."), Roles::TextRole);
            model->appendRow(row);
        }
    }

    static void attachments(QStandardItemModel* model)
    {
        for (int i = 0; i < 3; ++i)
        {
            QStandardItem* row = item(QStringLiteral("budget-%1.xlsx").arg(2026 + i));
            row->setData(QStringLiteral("Spreadsheet · 84 kB"), Roles::SubtitleRole);
            model->appendRow(row);
        }
    }

    static void searchResults(QStandardItemModel* model)
    {
        for (int i = 0; i < 3; ++i)
        {
            QStandardItem* row = item(QStringLiteral("Page %1").arg(308 + i));
            row->setData(QStringLiteral("…regional revenue grew by 18 % compared with the previous year…"), Roles::TextRole);
            row->setData(QPoint(10, 7), Roles::MatchRangesRole);
            model->appendRow(row);
        }
    }

    static void fonts(QStandardItemModel* model)
    {
        for (int i = 0; i < 3; ++i)
        {
            QStandardItem* row = item(QStringLiteral("Inter-SemiBold"));
            row->setData(QStringLiteral("Font family: Inter"), Roles::SubtitleRole);
            row->setData(QStringLiteral("TrueType"), Roles::TagRole);
            row->setData(i != 1, Roles::EmbeddedRole);
            model->appendRow(row);
        }
    }

    static void recentItems(QStandardItemModel* model)
    {
        for (int i = 0; i < 3; ++i)
        {
            QStandardItem* row = item(QStringLiteral("Project_Report.pdf"));
            row->setData(QStringLiteral("C:\\Users\\Jakub\\Documents · 24 pages · 2.4 MB"), Roles::SubtitleRole);
            row->setData(QStringLiteral("Page 12 of 24"), Roles::ProgressTextRole);
            row->setData(0.5, Roles::ProgressRole);
            row->setData(QStringLiteral("Today 10:24"), Roles::DateRole);
            row->setData(i, Roles::ItemTypeRole);
            model->appendRow(row);
        }
    }

    static void shortcuts(QStandardItemModel* model)
    {
        const char* keys[] = { "Ctrl+O", "Ctrl+S", "" };
        const char* actions[] = { "Open", "Save", "Compare" };
        for (int i = 0; i < 3; ++i)
        {
            QStandardItem* row = item(QString::fromLatin1(actions[i]), true);
            if (*keys[i])
            {
                row->setData(QVariant::fromValue(QKeySequence::fromString(QString::fromLatin1(keys[i]), QKeySequence::PortableText)), Roles::ShortcutRole);
            }
            model->appendRow(row);
        }
    }

    /// Sends a mouse event to the widget (also a move with a pressed button)
    static void sendMouse(QWidget* widget, QEvent::Type type, const QPoint& position, Qt::MouseButton button, Qt::MouseButtons buttons)
    {
        QMouseEvent event(type, QPointF(position), widget->mapToGlobal(QPointF(position)), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(widget, &event);
    }

    /// Returns the option of the item of the view, as the view gives it to its delegate
    static QStyleOptionViewItem viewOption(QAbstractItemView* view, const QModelIndex& index)
    {
        QStyleOptionViewItem option;
        option.initFrom(view);
        option.widget = view;
        option.rect = view->visualRect(index);
        option.state &= ~(QStyle::State_MouseOver | QStyle::State_HasFocus);
        return option;
    }
};

/// Delegate of the application, which remembers the state of the items it paints
class StateRecordingDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    virtual void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        states[index.row()] = option.state;
        QStyledItemDelegate::paint(painter, option, index);
    }

    mutable QMap<int, QStyle::State> states;
};

QImage FluentComponentsTest::renderDelegate(QAbstractItemDelegate* delegate, const QModelIndex& index, QStyle::State flags, QSize size, qreal scale, Qt::LayoutDirection direction)
{
    QStyleOptionViewItem option;
    option.rect = QRect(QPoint(0, 0), size);
    option.state = flags;
    option.direction = direction;
    option.widget = nullptr;
    return render([&](QPainter& painter) { delegate->paint(&painter, option, index); }, size, scale);
}

QPair<int, int> FluentComponentsTest::edgeInkHeights(const QImage& image, const QRect& area)
{
    // The heights of the ink of the first three columns with ink from the left
    // and from the right (a dot or an ellipsis is a few rows high, a letter more)
    const QColor background = token(Color::BgSurface);
    auto hasInk = [&](int x, int y) { return colorDistance(pixel(image, x, y), background) > 100; };
    auto columnsHeight = [&](int first, int step)
    {
        int top = -1;
        int bottom = -1;
        for (int x = first, i = 0; i < 3 && x >= area.left() && x <= area.right(); x += step, ++i)
        {
            for (int y = area.top(); y <= area.bottom(); ++y)
            {
                if (hasInk(x, y))
                {
                    top = top < 0 ? y : qMin(top, y);
                    bottom = qMax(bottom, y);
                }
            }
        }
        return top >= 0 ? bottom - top + 1 : 0;
    };
    auto firstColumn = [&](int start, int step)
    {
        for (int x = start; x >= area.left() && x <= area.right(); x += step)
        {
            for (int y = area.top(); y <= area.bottom(); ++y)
            {
                if (hasInk(x, y))
                {
                    return x;
                }
            }
        }
        return -1;
    };

    const int left = firstColumn(area.left(), 1);
    const int right = firstColumn(area.right(), -1);
    if (left < 0 || right < 0)
    {
        return qMakePair(0, 0);
    }
    return qMakePair(columnsHeight(left, 1), columnsHeight(right, -1));
}

void FluentComponentsTest::dataTextDirection()
{
    // Review I-43: the base direction of a text of data is its own (first strong
    // character, FSI of UAX #9), the texts of the user interface keep the
    // direction of the component; elision keeps working
    QCOMPARE(Painter::getDataTextDirection(QStringLiteral("abc")), Qt::LeftToRight);
    QCOMPARE(Painter::getDataTextDirection(QStringLiteral("\u05D0\u05D1 abc")), Qt::RightToLeft);
    QCOMPARE(Painter::getDataTextDirection(QStringLiteral("12 (\u0627\u0644)")), Qt::RightToLeft);
    QCOMPARE(Painter::getDataTextDirection(QStringLiteral("2026-10-17")), Qt::LeftToRight);
    QCOMPARE(Painter::getDataTextDirection(QString()), Qt::LeftToRight);
    QCOMPARE(Painter::getDataTextDirection(QStringLiteral("\u2068abc\u2069 \u05D0")), Qt::RightToLeft);

    // drawDataText: a right-to-left painter draws Latin data as a left-to-right
    // painter, the alignment is kept; a relative alignment is resolved in the
    // direction of the painter of the caller
    const QFont font = pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::BodySemiBold);
    const QColor color = token(Color::TextPrimary);
    const QString elided = Painter::elidedText(QStringLiteral("Annual_Report_2026_final_version.pdf"), font, 150.0);
    QVERIFY(elided.endsWith(QChar(0x2026)));
    const QRectF textRect(0, 0, 200, 20);
    const QImage reference = render([&](QPainter& painter)
    {
        painter.setLayoutDirection(Qt::LeftToRight);
        pdf::PDFStylePainter::text(&painter, textRect, Qt::AlignRight | Qt::AlignAbsolute | Qt::AlignVCenter, elided, font, color);
    }, QSize(200, 20));
    const QImage data = render([&](QPainter& painter)
    {
        painter.setLayoutDirection(Qt::RightToLeft);
        Painter::drawDataText(&painter, textRect, Qt::AlignRight | Qt::AlignAbsolute | Qt::AlignVCenter, elided, font, color);
        QCOMPARE(painter.layoutDirection(), Qt::RightToLeft);
    }, QSize(200, 20));
    QCOMPARE(describeDifference(reference, data, 0), QString());
    const QImage relative = render([&](QPainter& painter)
    {
        painter.setLayoutDirection(Qt::RightToLeft);
        Painter::drawDataText(&painter, textRect, Qt::AlignLeft | Qt::AlignVCenter, elided, font, color);
    }, QSize(200, 20));
    QCOMPARE(describeDifference(reference, relative, 0), QString());
    const QImage interface = render([&](QPainter& painter)
    {
        painter.setLayoutDirection(Qt::RightToLeft);
        pdf::PDFStylePainter::text(&painter, textRect, Qt::AlignRight | Qt::AlignAbsolute | Qt::AlignVCenter, elided, font, color);
    }, QSize(200, 20));
    QVERIFY(!describeDifference(reference, interface, 0).isEmpty());
    Painter::drawDataText(nullptr, textRect, Qt::AlignLeft, elided, font, color);

    // Property row: the value is data, the key a text of the interface
    Painter::PropertyRow property;
    property.key = QStringLiteral("abc.");
    property.value = QStringLiteral("Annual_Report_2026_final_version_of_the_board.pdf");
    QImage image = render([&](QPainter& painter) { Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), property, state(QStyle::State_None, Qt::RightToLeft)); }, QSize(326, 30));
    // The value (elided) lies at the right of the actions, from x = 36: "A" at its
    // left, the ellipsis at its right end (in its own direction)
    QPair<int, int> heights = edgeInkHeights(image, QRect(30, 6, 150, 18));
    QVERIFY2(heights.first > 4 && heights.second > 0 && heights.second <= 3, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));
    // The key of the interface ("abc." right to left): the dot at the left
    heights = edgeInkHeights(image, QRect(186, 6, 136, 18));
    QVERIFY2(heights.first > 0 && heights.first <= 3 && heights.second > 4, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));

    // A short value "abc." in right-to-left: the dot stays at its right end
    property.value = QStringLiteral("abc.");
    image = render([&](QPainter& painter) { Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), property, state(QStyle::State_None, Qt::RightToLeft)); }, QSize(326, 30));
    heights = edgeInkHeights(image, QRect(30, 6, 150, 18));
    QVERIFY2(heights.first > 4 && heights.second > 0 && heights.second <= 3, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));

    // Left-to-right interface with Hebrew data: the dot of "אבג." lies at its left
    property.value = QStringLiteral("\u05D0\u05D1\u05D2.");
    image = render([&](QPainter& painter) { Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), property, state(QStyle::State_None, Qt::LeftToRight)); }, QSize(326, 30));
    heights = edgeInkHeights(image, QRect(150, 6, 140, 18));
    QVERIFY2(heights.first > 0 && heights.first <= 3 && heights.second > 4, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));

    // Recent file row: the name of a file elided in the middle keeps its
    // extension at its right end in a right-to-left row
    Painter::RecentFileRow file;
    file.fileName = QStringLiteral("Annual_Report_2026_with_all_the_appendices_and_the_corrections.pdf");
    file.path = QStringLiteral("abc.");
    file.starVisible = false;
    image = render([&](QPainter& painter) { Painter::paintRecentFileRow(&painter, QRectF(0, 0, 381, 51), file, state(QStyle::State_None, Qt::RightToLeft)); }, QSize(381, 51));
    heights = edgeInkHeights(image, QRect(30, 10, 312, 16));
    QVERIFY2(heights.first > 4 && heights.second > 4, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));
    QVERIFY(Painter::elidedFileName(file.fileName, pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::BodySemiBold), 299).endsWith(QStringLiteral("ections.pdf")));
    // The path is data too: its dot at the right end
    heights = edgeInkHeights(image, QRect(30, 28, 312, 13));
    QVERIFY2(heights.first > 4 && heights.second > 0 && heights.second <= 3, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));

    // Bookmark row (a delegate): the title is data
    const Painter::BookmarkRow bookmark{ QStringLiteral("abc."), QString(), QString() };
    image = render([&](QPainter& painter) { Painter::paintBookmarkRow(&painter, QRectF(0, 0, 276, 46), bookmark, state(QStyle::State_None, Qt::RightToLeft)); }, QSize(276, 46));
    heights = edgeInkHeights(image, QRect(10, 15, 230, 16));
    QVERIFY2(heights.first > 4 && heights.second > 0 && heights.second <= 3, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));

    // Key value list: the value is data, its dot stays at its end in right-to-left
    pdf::PDFKeyValueList list;
    list.addRow(QStringLiteral("Key"), QStringLiteral("abc."));
    list.resize(400, 28);
    list.setLayoutDirection(Qt::RightToLeft);
    image = renderWidget(&list, 1.0);
    heights = edgeInkHeights(image, QRect(4, 0, 200, 28));
    QVERIFY2(heights.first > 4 && heights.second > 0 && heights.second <= 3, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));

    // Folder card and Signature card: the name, the path and the signer are data
    Painter::FolderCard folder;
    folder.name = QStringLiteral("abc.");
    folder.path.clear();
    folder.metaVisible = false;
    folder.paintNested = false;
    image = render([&](QPainter& painter) { Painter::paintFolderCard(&painter, QRectF(0, 0, 252, 84), folder, state(QStyle::State_None, Qt::RightToLeft)); }, QSize(252, 84));
    heights = edgeInkHeights(image, QRect(36, 18, 156, 48));
    QVERIFY2(heights.first > 4 && heights.second > 0 && heights.second <= 3, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));

    // Review RS6: the name of a collection (Collection row, the title of a Tool
    // category header of a collection) is data. Right-to-left interface with a Latin
    // name: the dot of "abc." at its right end; the text area of the row lies between
    // the chevron (left) and the circle (right), the title centered without the meta
    Painter::CollectionRow collection;
    collection.name = QStringLiteral("abc.");
    image = render([&](QPainter& painter) { Painter::paintCollectionRow(&painter, QRectF(0, 0, 324, 52), collection, state(QStyle::State_None, Qt::RightToLeft)); }, QSize(324, 52));
    heights = edgeInkHeights(image, QRect(30, 14, 245, 24));
    QVERIFY2(heights.first > 4 && heights.second > 0 && heights.second <= 3, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));

    // Left-to-right interface with a Hebrew name: the dot of "אבג." at its left end
    collection.name = QStringLiteral("אבג.");
    image = render([&](QPainter& painter) { Painter::paintCollectionRow(&painter, QRectF(0, 0, 324, 52), collection, state(QStyle::State_None, Qt::LeftToRight)); }, QSize(324, 52));
    heights = edgeInkHeights(image, QRect(50, 14, 240, 24));
    QVERIFY2(heights.first > 0 && heights.first <= 3 && heights.second > 4, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));

    // A long Latin name in right-to-left: elided at its end - the ellipsis at the right
    collection.name = QStringLiteral("Annual invoices and receipts of all the departments of the company");
    image = render([&](QPainter& painter) { Painter::paintCollectionRow(&painter, QRectF(0, 0, 324, 52), collection, state(QStyle::State_None, Qt::RightToLeft)); }, QSize(324, 52));
    heights = edgeInkHeights(image, QRect(30, 14, 245, 24));
    QVERIFY2(heights.first > 4 && heights.second > 0 && heights.second <= 3, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));

    // The create row "New collection" is a text of the interface: in right-to-left
    // the dot of "abc." is at its left end
    Painter::CollectionRow create;
    create.name = QStringLiteral("abc.");
    create.isCreate = true;
    image = render([&](QPainter& painter) { Painter::paintCollectionRow(&painter, QRectF(0, 0, 324, 54), create, state(QStyle::State_None, Qt::RightToLeft)); }, QSize(324, 54));
    heights = edgeInkHeights(image, QRect(14, 14, 250, 26));
    QVERIFY2(heights.first > 0 && heights.first <= 3 && heights.second > 4, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));

    // Tool category header: the title of a collection is data (isTitleData), the name
    // of a category a text of the interface
    Painter::ToolCategoryHeader header;
    header.title = QStringLiteral("abc.");
    header.isTitleData = true;
    image = render([&](QPainter& painter) { Painter::paintToolCategoryHeader(&painter, QRectF(0, 0, 393, 61), header, state(QStyle::State_None, Qt::RightToLeft)); }, QSize(393, 61));
    heights = edgeInkHeights(image, QRect(14, 18, 318, 24));
    QVERIFY2(heights.first > 4 && heights.second > 0 && heights.second <= 3, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));
    header.isTitleData = false;
    image = render([&](QPainter& painter) { Painter::paintToolCategoryHeader(&painter, QRectF(0, 0, 393, 61), header, state(QStyle::State_None, Qt::RightToLeft)); }, QSize(393, 61));
    heights = edgeInkHeights(image, QRect(14, 18, 318, 24));
    QVERIFY2(heights.first > 0 && heights.first <= 3 && heights.second > 4, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));
    header.title = QStringLiteral("אבג.");
    header.isTitleData = true;
    image = render([&](QPainter& painter) { Painter::paintToolCategoryHeader(&painter, QRectF(0, 0, 393, 61), header, state(QStyle::State_None, Qt::LeftToRight)); }, QSize(393, 61));
    heights = edgeInkHeights(image, QRect(60, 18, 318, 24));
    QVERIFY2(heights.first > 0 && heights.first <= 3 && heights.second > 4, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));

    // The widget passes the kind to its painter
    pdf::PDFToolCategoryHeader categoryHeader(QStringLiteral("abc."), QString(), QString(), QStringLiteral("layers"), pdf::PDFAccentHue::Blue);
    QVERIFY(!categoryHeader.isTitleData());
    QVERIFY(!categoryHeader.getContent().isTitleData);
    categoryHeader.setTitleData(true);
    QVERIFY(categoryHeader.isTitleData());
    QVERIFY(categoryHeader.getContent().isTitleData);
}

void FluentComponentsTest::delegateSizes()
{
    // Size hints of the delegates against the cards of the design
    QStandardItemModel model;
    QStandardItem* row = DelegateSamples::item(QStringLiteral("Jakub Melka"));
    row->setData(QStringLiteral("Sep 28"), pdf::PDFComponentRoles::DateRole);
    row->setData(QStringLiteral("Check these numbers against the Q3 report before publishing."), pdf::PDFComponentRoles::TextRole);
    model.appendRow(row);
    const QModelIndex index = model.index(0, 0);

    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 276, 10);
    option.state = QStyle::State_Enabled;

    pdf::PDFThumbnailDelegate thumbnail;
    pdf::PDFBookmarkDelegate bookmark;
    pdf::PDFNoteDelegate note;
    pdf::PDFAttachmentDelegate attachment;
    pdf::PDFSearchResultDelegate search;
    pdf::PDFFontDelegate font;
    pdf::PDFRecentItemDelegate recent;
    pdf::PDFShortcutDelegate shortcut;

    QCOMPARE(thumbnail.sizeHint(option, index), QSize(124, 183));
    QCOMPARE(bookmark.sizeHint(option, index), QSize(276, 46));
    QCOMPARE(note.sizeHint(option, index), QSize(276, 63));
    QCOMPARE(attachment.sizeHint(option, index), QSize(276, 48));
    QCOMPARE(search.sizeHint(option, index), QSize(276, 62));

    // Without the width the default width of the component
    option.rect = QRect();
    QCOMPARE(font.sizeHint(option, index), QSize(692, 48));
    QCOMPARE(recent.sizeHint(option, index), QSize(802, 56));
    QCOMPARE(shortcut.sizeHint(option, index), QSize(780, 40));
    QCOMPARE(note.sizeHint(option, index), QSize(276, 63));

    // The painter sizes of the default variants
    QCOMPARE(Painter::getThumbnailItemSize(Painter::ThumbnailItem{ QStringLiteral("308"), QPixmap(), QSize(108, 148) }), QSize(124, 183));
    QCOMPARE(Painter::getBookmarkRowSize(Painter::BookmarkRow{ QStringLiteral("Page 17"), QStringLiteral("Executive Summary"), QStringLiteral("Auto") }), QSize(276, 46));
    Painter::NoteRow noteRow;
    noteRow.author = QStringLiteral("Jakub Melka");
    noteRow.date = QStringLiteral("Sep 28");
    noteRow.text = QStringLiteral("Check these numbers against the Q3 report before publishing.");
    QCOMPARE(Painter::getNoteRowSize(noteRow), QSize(276, 63));
    noteRow.text = QStringLiteral("Agreed.");
    QCOMPARE(Painter::getNoteRowSize(noteRow), QSize(276, 48));
    noteRow.text.clear();
    QCOMPARE(Painter::getNoteRowSize(noteRow), QSize(276, 48));
    QCOMPARE(Painter::getAttachmentRowSize(Painter::AttachmentRow()), QSize(276, 48));
    Painter::SearchResultRow searchRow;
    searchRow.page = QStringLiteral("Page 308");
    searchRow.snippet = QStringLiteral("…regional revenue grew by 18 % compared with the previous year…");
    QCOMPARE(Painter::getSearchResultRowSize(searchRow), QSize(276, 62));
    searchRow.snippet = QStringLiteral("…revenue…");
    QCOMPARE(Painter::getSearchResultRowSize(searchRow), QSize(276, 47));
    searchRow.snippet = QString(400, QLatin1Char('w'));
    QCOMPARE(Painter::getSearchResultRowSize(searchRow), QSize(276, 62));
    QCOMPARE(Painter::getFontRowSize(Painter::FontRow()), QSize(692, 48));
    QCOMPARE(Painter::getRecentItemRowSize(Painter::RecentItemRow()), QSize(802, 56));
    QCOMPARE(Painter::getShortcutRowSize(Painter::ShortcutRow()), QSize(780, 40));

    // Geometry of the nested parts of the cards
    const Painter::State ltr = state(QStyle::State_None);
    QCOMPARE(Painter::getAttachmentRowSaveRect(QRectF(0, 0, 276, 48), ltr), QRectF(246, 12, 24, 24));
    QCOMPARE(Painter::getFontRowCopyRect(QRectF(0, 0, 692, 48), ltr), QRectF(660, 12, 24, 24));
    QCOMPARE(Painter::getRecentItemRowMoreRect(QRectF(0, 0, 802, 56), ltr), QRectF(778, 20, 16, 16));
    QCOMPARE(Painter::getRecentItemRowStarRect(QRectF(0, 0, 802, 56), ltr), QRectF(740, 16, 24, 24));
    const QRectF keyRect = Painter::getShortcutRowKeyRect(QRectF(0, 0, 780, 40), Painter::ShortcutRow{ QStringLiteral("Open…"), QStringLiteral("Ctrl+O"), QStringLiteral("folder-open"), false }, ltr);
    QCOMPARE(keyRect.right(), 770.0);
    QCOMPARE(keyRect.top(), 8.0);
    QCOMPARE(keyRect.height(), 23.0);
    const QRectF placeholder = Painter::getShortcutRowKeyRect(QRectF(0, 0, 780, 40), Painter::ShortcutRow{ QStringLiteral("Open…"), QString(), QStringLiteral("folder-open"), false }, ltr);
    QVERIFY2(qAbs(placeholder.width() - 96.0) <= 2.0, qPrintable(QString::number(placeholder.width())));
    const QRectF recording = Painter::getShortcutRowKeyRect(QRectF(0, 0, 780, 40), Painter::ShortcutRow{ QStringLiteral("Open…"), QString(), QStringLiteral("folder-open"), true }, ltr);
    QVERIFY2(qAbs(recording.width() - 92.0) <= 2.0, qPrintable(QString::number(recording.width())));
    QCOMPARE(Painter::getThumbnailItemPageRect(QRectF(0, 0, 124, 183), Painter::ThumbnailItem()), QRectF(8, 8, 108, 148));

    // The size is stable in each state (the focus does not move the content)
    QStandardItemModel shortcuts;
    DelegateSamples::shortcuts(&shortcuts);
    option.rect = QRect(0, 0, 780, 40);
    for (const QStyle::State flags : { QStyle::State(QStyle::State_Enabled), QStyle::State_Enabled | QStyle::State_HasFocus, QStyle::State_Enabled | QStyle::State_MouseOver | QStyle::State_Sunken })
    {
        option.state = flags;
        QCOMPARE(shortcut.sizeHint(option, shortcuts.index(0, 0)), QSize(780, 40));
    }

    // Real views: a tree view asks without the width (the width of its column)
    QTreeView tree;
    tree.setHeaderHidden(true);
    tree.setRootIsDecorated(false);
    QStandardItemModel notes;
    DelegateSamples::notes(&notes);
    tree.setModel(&notes);
    tree.setItemDelegate(&note);
    tree.resize(300, 300);
    tree.header()->resizeSection(0, 276);
    tree.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tree));
    QCOMPARE(tree.visualRect(notes.index(0, 0)).height(), 63);
    tree.setItemDelegate(nullptr);
}

void FluentComponentsTest::delegatePixels_data()
{
    pixelProbes_data();
}

void FluentComponentsTest::delegatePixels()
{
    QFETCH(QString, theme);
    QFETCH(qreal, scale);
    QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));

    using Roles = pdf::PDFComponentRoles;

    const QColor surface = token(Color::BgSurface);
    const QColor hover = blend(token(Color::BgHover), surface);
    const QColor pressed = blend(token(Color::BgPressed), surface);
    const QColor selected = blend(token(Color::BgAccentSubtle), surface);
    const QColor selectedHover = blend(token(Color::BgAccentSubtleHover), surface);
    const QColor selectedPressed = blend(token(Color::BgAccentSubtlePressed), surface);
    const QStyle::State enabled = QStyle::State_Enabled;
    const QStyle::State over = enabled | QStyle::State_MouseOver;
    const QStyle::State down = enabled | QStyle::State_MouseOver | QStyle::State_Sunken;
    const QStyle::State focus = enabled | QStyle::State_HasFocus;
    const QStyle::State sel = enabled | QStyle::State_Selected;

    auto isCloser = [](const QColor& color, const QColor& expected, const QColor& other)
    {
        return colorDistance(color, expected) < colorDistance(color, other);
    };

    // The backgrounds of the states of the rows with the axis Selected
    struct BackgroundProbe
    {
        QAbstractItemDelegate* delegate;
        QSize size;
        QPoint point;
        const char* name;
    };

    QStandardItemModel thumbnails;
    DelegateSamples::thumbnails(&thumbnails);
    QStandardItemModel bookmarks;
    DelegateSamples::bookmarks(&bookmarks);
    QStandardItemModel notes;
    DelegateSamples::notes(&notes);
    QStandardItemModel attachments;
    DelegateSamples::attachments(&attachments);
    QStandardItemModel searchResults;
    DelegateSamples::searchResults(&searchResults);
    QStandardItemModel fonts;
    DelegateSamples::fonts(&fonts);
    QStandardItemModel recentItems;
    DelegateSamples::recentItems(&recentItems);
    QStandardItemModel shortcuts;
    DelegateSamples::shortcuts(&shortcuts);

    pdf::PDFThumbnailDelegate thumbnail;
    pdf::PDFBookmarkDelegate bookmark;
    pdf::PDFNoteDelegate note;
    pdf::PDFAttachmentDelegate attachment;
    pdf::PDFSearchResultDelegate search;
    pdf::PDFFontDelegate font;
    pdf::PDFRecentItemDelegate recent;
    pdf::PDFShortcutDelegate shortcut;

    const QList<std::tuple<QAbstractItemDelegate*, QModelIndex, QSize, QPoint, QString>> selectable =
    {
        { &thumbnail, thumbnails.index(0, 0), QSize(124, 183), QPoint(20, 179), QStringLiteral("thumbnail") },
        { &bookmark, bookmarks.index(0, 0), QSize(276, 46), QPoint(140, 2), QStringLiteral("bookmark") },
        { &note, notes.index(0, 0), QSize(276, 63), QPoint(140, 2), QStringLiteral("note") },
        { &attachment, attachments.index(0, 0), QSize(276, 48), QPoint(140, 2), QStringLiteral("attachment") },
        { &search, searchResults.index(0, 0), QSize(276, 62), QPoint(140, 2), QStringLiteral("search") }
    };
    for (const auto& [delegate, index, size, point, name] : selectable)
    {
        const QList<QPair<QStyle::State, QColor>> expected =
        {
            { enabled, surface }, { over, hover }, { down, pressed }, { focus, surface },
            { sel, selected }, { sel | QStyle::State_MouseOver, selectedHover }, { sel | QStyle::State_MouseOver | QStyle::State_Sunken, selectedPressed }
        };
        for (const auto& [flags, color] : expected)
        {
            const QImage image = renderDelegate(delegate, index, flags, size, scale);
            QVERIFY2(isNear(pixel(image, point.x(), point.y()), color, 2), qPrintable(QStringLiteral("%1 %2: %3").arg(name).arg(int(flags)).arg(pixel(image, point.x(), point.y()).name())));
        }

        // The focus ring of 2 px inside (I-21), the selected item keeps its fill
        QImage image = renderDelegate(delegate, index, focus, size, scale);
        QVERIFY2(isNear(pixel(image, size.width() / 2, 0), token(Color::BorderFocus)), qPrintable(name));
        QVERIFY2(isNear(pixel(image, size.width() / 2, 1), token(Color::BorderFocus)), qPrintable(name));
        QVERIFY2(isNear(pixel(image, size.width() / 2, size.height() - 1), token(Color::BorderFocus)), qPrintable(name));
        image = renderDelegate(delegate, index, sel | QStyle::State_HasFocus, size, scale);
        QVERIFY2(isNear(pixel(image, size.width() / 2, 0), token(Color::BorderFocus)), qPrintable(name));
        const int ringFree = point.y() > size.height() / 2 ? point.y() - 2 : point.y() + 2;
        QVERIFY2(isNear(pixel(image, point.x(), ringFree), selected, 2), qPrintable(name));

        // A disabled item has no hover and no press (O-08), the selection keeps its fill
        image = renderDelegate(delegate, index, QStyle::State_MouseOver | QStyle::State_Sunken, size, scale);
        QVERIFY2(isNear(pixel(image, point.x(), point.y()), surface, 2), qPrintable(name));
    }

    // Thumbnail item: the picture of the model in the page, the border of the
    // page (bg/accent 2 px when selected), the label in the pill
    const QRectF pageRect = Painter::getThumbnailItemPageRect(QRectF(0, 0, 124, 183), thumbnail.getContent(thumbnails.index(0, 0)));
    QImage image = renderDelegate(&thumbnail, thumbnails.index(0, 0), enabled, QSize(124, 183), scale);
    QVERIFY(isNear(pixel(image, pageRect.center().x(), pageRect.center().y()), QColor(200, 30, 30), 2));
    QVERIFY(isNear(pixel(image, pageRect.left() - 1, pageRect.center().y()), token(Color::BorderStrong), 24));
    QVERIFY(isCloser(strongestInk(image, QRect(40, 162, 44, 15), surface), token(Color::TextTertiary), token(Color::IconOnAccent)));
    image = renderDelegate(&thumbnail, thumbnails.index(0, 0), sel, QSize(124, 183), scale);
    QVERIFY(isNear(pixel(image, pageRect.left() - 1, pageRect.center().y()), token(Color::BgAccent), 2));
    QVERIFY(isNear(pixel(image, pageRect.left() - 2, pageRect.center().y()), token(Color::BgAccent), 2));
    QVERIFY(isNear(pixel(image, 62 - 15, 162 + 7), token(Color::BgAccent), 2));
    QVERIFY(isCloser(strongestInk(image, QRect(48, 163, 28, 13), token(Color::BgAccent)), token(Color::IconOnAccent), token(Color::TextTertiary)));
    image = renderDelegate(&thumbnail, thumbnails.index(0, 0), QStyle::State_Selected, QSize(124, 183), scale);
    QVERIFY(isNear(pixel(image, pageRect.left() - 1, pageRect.center().y()), token(Color::BorderStrong), 24));
    QVERIFY(isNear(pixel(image, 62 - 15, 162 + 7), token(Color::BgMuted), 2));

    // Bookmark row: the filled bookmark icon/accent (I-13), the title text/accent when selected
    image = renderDelegate(&bookmark, bookmarks.index(0, 0), enabled, QSize(276, 46), scale);
    QVERIFY2(isNear(pixel(image, 18, 21), token(Color::IconAccent), 24), qPrintable(pixel(image, 18, 21).name()));
    QVERIFY(isCloser(strongestInk(image, QRect(36, 8, 60, 16), surface), token(Color::TextPrimary), token(Color::TextAccent)));
    QVERIFY(isNear(pixel(image, 276 - 10 - 20, 12), token(Color::BorderDefault), 24));
    image = renderDelegate(&bookmark, bookmarks.index(0, 0), sel, QSize(276, 46), scale);
    QVERIFY(isCloser(strongestInk(image, QRect(36, 8, 60, 16), selected), token(Color::TextAccent), token(Color::TextPrimary)));
    image = renderDelegate(&bookmark, bookmarks.index(0, 0), QStyle::State_None, QSize(276, 46), scale);
    QVERIFY2(isNear(pixel(image, 18, 21), token(Color::IconDisabled), 24), qPrintable(pixel(image, 18, 21).name()));

    // Note row: the badge of the hue at the top, the author text/accent when selected
    image = renderDelegate(&note, notes.index(0, 0), enabled, QSize(276, 63), scale);
    QVERIFY(isNear(pixel(image, 14, 24), token(Color::AccentAmberBg), 2));
    QVERIFY(isCloser(strongestInk(image, QRect(50, 8, 60, 15), surface), token(Color::TextPrimary), token(Color::TextAccent)));
    QVERIFY(countPixels(image, QRect(50, 25, 216, 30), token(Color::TextSecondary), 24) > 0);
    image = renderDelegate(&note, notes.index(0, 0), sel, QSize(276, 63), scale);
    QVERIFY(isCloser(strongestInk(image, QRect(50, 8, 60, 15), selected), token(Color::TextAccent), token(Color::TextPrimary)));

    // Attachment row: the save icon icon/muted, icon/default on the hovered row
    image = renderDelegate(&attachment, attachments.index(0, 0), enabled, QSize(276, 48), scale);
    QVERIFY(isCloser(strongestInk(image, QRect(251, 17, 14, 14), surface), token(Color::IconMuted), token(Color::IconDefault)));
    QVERIFY(isNear(pixel(image, 12, 24), token(Color::AccentNeutralBg), 2));
    image = renderDelegate(&attachment, attachments.index(0, 0), over, QSize(276, 48), scale);
    QVERIFY(isCloser(strongestInk(image, QRect(251, 17, 14, 14), hover), token(Color::IconDefault), token(Color::IconMuted)));
    image = renderDelegate(&attachment, attachments.index(0, 0), sel, QSize(276, 48), scale);
    QVERIFY(isCloser(strongestInk(image, QRect(50, 9, 80, 16), selected), token(Color::TextAccent), token(Color::TextPrimary)));
    // The own states of the button (as Icon button): bg/hover over the hovered row
    Painter::AttachmentRow attachmentRow = attachment.getContent(attachments.index(0, 0));
    attachmentRow.saveFlags = QStyle::State_MouseOver;
    image = render([&](QPainter& painter) { Painter::paintAttachmentRow(&painter, QRectF(0, 0, 276, 48), attachmentRow, state(QStyle::State_MouseOver)); }, QSize(276, 48), scale);
    QVERIFY(isNear(pixel(image, 258, 13), blend(token(Color::BgHover), hover), 3));
    QVERIFY(isCloser(strongestInk(image, QRect(251, 17, 14, 14), blend(token(Color::BgHover), hover)), token(Color::IconDefault), token(Color::IconStrong)));
    attachmentRow.saveFlags = QStyle::State_MouseOver | QStyle::State_Sunken;
    image = render([&](QPainter& painter) { Painter::paintAttachmentRow(&painter, QRectF(0, 0, 276, 48), attachmentRow, state(QStyle::State_MouseOver)); }, QSize(276, 48), scale);
    QVERIFY(isNear(pixel(image, 258, 13), blend(token(Color::BgPressed), hover), 3));
    // The override of the hovered row holds also for the pressed button (review F6 of step 5)
    QVERIFY(isCloser(strongestInk(image, QRect(251, 17, 14, 14), blend(token(Color::BgPressed), hover)), token(Color::IconDefault), token(Color::IconStrong)));
    // The pressed row with the pressed button too
    image = render([&](QPainter& painter) { Painter::paintAttachmentRow(&painter, QRectF(0, 0, 276, 48), attachmentRow, state(QStyle::State_MouseOver | QStyle::State_Sunken)); }, QSize(276, 48), scale);
    QVERIFY(isCloser(strongestInk(image, QRect(251, 17, 14, 14), blend(token(Color::BgPressed), pressed)), token(Color::IconDefault), token(Color::IconStrong)));

    // Search result row: the found term Semi Bold text/primary, the rest text/secondary, the page text/accent when selected
    image = renderDelegate(&search, searchResults.index(0, 0), enabled, QSize(276, 62), scale);
    QVERIFY(countPixels(image, QRect(10, 24, 256, 30), token(Color::TextPrimary), 16) > 4);
    QVERIFY(countPixels(image, QRect(10, 24, 256, 30), token(Color::TextSecondary), 16) > 4);
    QVERIFY(isCloser(strongestInk(image, QRect(10, 8, 60, 13), surface), token(Color::TextMuted), token(Color::TextAccent)));
    image = renderDelegate(&search, searchResults.index(0, 0), sel, QSize(276, 62), scale);
    QVERIFY(isCloser(strongestInk(image, QRect(10, 8, 60, 13), selected), token(Color::TextAccent), token(Color::TextMuted)));
    QVERIFY(countPixels(image, QRect(10, 24, 256, 30), token(Color::TextPrimary), 16) > 4);

    // Font row: the copy icon on the hovered and pressed row only, the dot of the status, no selection
    const QRect copyArea(665, 17, 14, 14);
    image = renderDelegate(&font, fonts.index(0, 0), enabled, QSize(692, 48), scale);
    QVERIFY(isNear(strongestInk(image, copyArea, surface), surface, 2));
    QVERIFY(isNear(pixel(image, 524, 24), token(Color::AccentGreenIcon), 24));
    QVERIFY(isNear(pixel(image, 12, 24), token(Color::AccentNeutralBg), 2));
    image = renderDelegate(&font, fonts.index(0, 0), over, QSize(692, 48), scale);
    QVERIFY(isCloser(strongestInk(image, copyArea, hover), token(Color::IconDefault), token(Color::IconMuted)));
    image = renderDelegate(&font, fonts.index(0, 0), down, QSize(692, 48), scale);
    QVERIFY(isNear(pixel(image, 300, 2), pressed, 2));
    QVERIFY(!isNear(strongestInk(image, copyArea, pressed), pressed, 8));
    image = renderDelegate(&font, fonts.index(0, 0), focus, QSize(692, 48), scale);
    QVERIFY(isNear(strongestInk(image, copyArea, surface), surface, 2));
    QVERIFY(isNear(pixel(image, 300, 0), token(Color::BorderFocus)));
    image = renderDelegate(&font, fonts.index(0, 0), sel, QSize(692, 48), scale);
    QVERIFY(isNear(pixel(image, 300, 2), surface, 2));
    image = renderDelegate(&font, fonts.index(1, 0), enabled, QSize(692, 48), scale);
    QVERIFY(isNear(pixel(image, 524, 24), token(Color::AccentAmberIcon), 24));

    // Recent item row: the menu icon/default under the pointer, the progress bar,
    // the circles of the folder (Neutral) and of the workspace (Violet), no selection
    const QRect moreArea(778, 20, 16, 16);
    image = renderDelegate(&recent, recentItems.index(0, 0), enabled, QSize(802, 56), scale);
    QVERIFY(isCloser(strongestInk(image, moreArea, surface), token(Color::IconMuted), token(Color::IconDefault)));
    QVERIFY(isNear(pixel(image, 490, 36), token(Color::BgAccent), 2));
    QVERIFY(isNear(pixel(image, 600, 36), token(Color::BorderDefault), 2));
    image = renderDelegate(&recent, recentItems.index(0, 0), over, QSize(802, 56), scale);
    QVERIFY(isNear(pixel(image, 400, 3), hover, 2));
    QVERIFY(isCloser(strongestInk(image, moreArea, hover), token(Color::IconDefault), token(Color::IconMuted)));
    image = renderDelegate(&recent, recentItems.index(0, 0), focus, QSize(802, 56), scale);
    QVERIFY(isNear(pixel(image, 400, 0), token(Color::BorderFocus)));
    QVERIFY(isCloser(strongestInk(image, moreArea, surface), token(Color::IconMuted), token(Color::IconDefault)));
    image = renderDelegate(&recent, recentItems.index(0, 0), sel, QSize(802, 56), scale);
    QVERIFY(isNear(pixel(image, 400, 3), surface, 2));
    image = renderDelegate(&recent, recentItems.index(1, 0), enabled, QSize(802, 56), scale);
    QVERIFY(isNear(pixel(image, 18, 28), token(Color::AccentNeutralBg), 2));
    QVERIFY(isNear(pixel(image, 490, 36), surface, 2));
    image = renderDelegate(&recent, recentItems.index(2, 0), enabled, QSize(802, 56), scale);
    QVERIFY(isNear(pixel(image, 18, 28), token(Color::AccentVioletBg), 2));
    recentItems.setData(recentItems.index(0, 0), true, Roles::PinnedRole);
    image = renderDelegate(&recent, recentItems.index(0, 0), enabled, QSize(802, 56), scale);
    QVERIFY(isNear(pixel(image, 752, 28), token(Color::IconSun), 40));

    // Shortcut row: the key cap bg/subtle with border/default, the placeholder
    // without a fill and a dashed border, the recording cap with the ring and text/accent
    Painter::ShortcutRow shortcutRow = shortcut.getContent(shortcuts.index(0, 0));
    QRectF capRect = Painter::getShortcutRowKeyRect(QRectF(0, 0, 780, 40), shortcutRow, state(QStyle::State_None));
    image = renderDelegate(&shortcut, shortcuts.index(0, 0), enabled, QSize(780, 40), scale);
    QVERIFY(isNear(pixel(image, capRect.left() + 3, capRect.center().y()), token(Color::BgSubtle), 2));
    QVERIFY(isNear(pixel(image, capRect.center().x(), capRect.top()), token(Color::BorderDefault), 4));
    QVERIFY(isCloser(strongestInk(image, capRect.adjusted(4, 2, -4, -2).toRect(), token(Color::BgSubtle)), token(Color::TextSecondary), token(Color::TextAccent)));
    image = renderDelegate(&shortcut, shortcuts.index(0, 0), over, QSize(780, 40), scale);
    QVERIFY(isNear(pixel(image, 300, 3), hover, 2));
    QVERIFY(isNear(pixel(image, capRect.left() + 3, capRect.center().y()), token(Color::BgSubtle), 2));
    shortcutRow.recording = true;
    capRect = Painter::getShortcutRowKeyRect(QRectF(0, 0, 780, 40), shortcutRow, state(QStyle::State_None));
    image = render([&](QPainter& painter) { Painter::paintShortcutRow(&painter, QRectF(0, 0, 780, 40), shortcutRow, state(QStyle::State_MouseOver | QStyle::State_HasFocus)); }, QSize(780, 40), scale);
    QVERIFY(isNear(pixel(image, capRect.center().x(), capRect.top()), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, capRect.center().x(), capRect.top() + 1), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 300, 3), surface, 2));
    QVERIFY(isNear(pixel(image, 300, 0), surface, 2));
    QVERIFY(isCloser(strongestInk(image, capRect.adjusted(4, 3, -4, -3).toRect(), token(Color::BgSubtle)), token(Color::TextAccent), token(Color::TextSecondary)));
    const Painter::ShortcutRow emptyRow = shortcut.getContent(shortcuts.index(2, 0));
    QVERIFY(emptyRow.shortcut.isEmpty());
    capRect = Painter::getShortcutRowKeyRect(QRectF(0, 0, 780, 40), emptyRow, state(QStyle::State_None));
    image = renderDelegate(&shortcut, shortcuts.index(2, 0), enabled, QSize(780, 40), scale);
    QVERIFY(isNear(pixel(image, capRect.left() + 3, capRect.center().y()), surface, 2));
    int dashes = 0;
    int gaps = 0;
    for (int x = qCeil(capRect.left()) + 8; x < capRect.right() - 8; ++x)
    {
        const QColor color = pixel(image, x, capRect.top());
        dashes += isNear(color, token(Color::BorderStrong), 30) ? 1 : 0;
        gaps += isNear(color, surface, 30) ? 1 : 0;
    }
    QVERIFY2(dashes > 10 && gaps > 10, qPrintable(QStringLiteral("%1 %2").arg(dashes).arg(gaps)));
}

void FluentComponentsTest::delegatePressTracking()
{
    // Review I-23: the delegate tracks the press itself
    QStandardItemModel model;
    DelegateSamples::bookmarks(&model);
    pdf::PDFBookmarkDelegate delegate;
    QWidget window;
    window.resize(320, 260);
    pdf::PDFListView* view = new pdf::PDFListView(&window);
    view->setGeometry(0, 0, 320, 260);
    view->setModel(&model);
    view->setItemDelegate(&delegate);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QWidget* viewport = view->viewport();
    const QModelIndex first = model.index(0, 0);
    const QModelIndex second = model.index(1, 0);
    const QPoint firstCenter = view->visualRect(first).center();
    const QPoint secondCenter = view->visualRect(second).center();
    auto isSunken = [&](const QModelIndex& index)
    {
        return delegate.getItemState(DelegateSamples::viewOption(view, index), index).isPressed();
    };

    // Press, move out (with the button), back in, release
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, firstCenter, Qt::LeftButton, Qt::LeftButton);
    QCOMPARE(QModelIndex(delegate.getPressedIndex()), first);
    QVERIFY(isSunken(first));
    QVERIFY(!isSunken(second));
    // The press selects the row (single selection): exactly the selected pressed fill
    QVERIFY(view->selectionModel()->isSelected(first));
    QImage image = renderWidget(view, 1.0);
    const QColor pressedFill = pixel(image, view->viewport()->geometry().left() + 140, view->viewport()->geometry().top() + view->visualRect(first).top() + 2);
    QVERIFY2(isNear(pressedFill, blend(token(Color::BgAccentSubtlePressed), token(Color::BgSurface)), 3), qPrintable(pressedFill.name()));
    DelegateSamples::sendMouse(viewport, QEvent::MouseMove, secondCenter, Qt::NoButton, Qt::LeftButton);
    QVERIFY(!isSunken(first));
    QVERIFY(!isSunken(second));
    DelegateSamples::sendMouse(viewport, QEvent::MouseMove, firstCenter, Qt::NoButton, Qt::LeftButton);
    QVERIFY(isSunken(first));
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, firstCenter, Qt::LeftButton, Qt::NoButton);
    QVERIFY(!delegate.getPressedIndex().isValid());
    QVERIFY(!isSunken(first));

    // Press on one row, release on another one
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, firstCenter, Qt::LeftButton, Qt::LeftButton);
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, secondCenter, Qt::LeftButton, Qt::NoButton);
    QVERIFY(!delegate.getPressedIndex().isValid());
    QVERIFY(!isSunken(first));
    QVERIFY(!isSunken(second));

    // The right button does not press, the leave and the hide end the press
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, firstCenter, Qt::RightButton, Qt::RightButton);
    QVERIFY(!delegate.getPressedIndex().isValid());
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, firstCenter, Qt::RightButton, Qt::NoButton);
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, firstCenter, Qt::LeftButton, Qt::LeftButton);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(viewport, &leave);
    QVERIFY(!isSunken(first));
    view->hide();
    QVERIFY(!delegate.getPressedIndex().isValid());
    view->show();

    // A reset of the model during the press
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, firstCenter, Qt::LeftButton, Qt::LeftButton);
    QVERIFY(delegate.getPressedIndex().isValid());
    model.clear();
    QVERIFY(!delegate.getPressedIndex().isValid());
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, firstCenter, Qt::LeftButton, Qt::NoButton);
    DelegateSamples::bookmarks(&model);
    QVERIFY(!isSunken(model.index(0, 0)));

    // A disabled item is not pressed
    model.item(0)->setEnabled(false);
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, view->visualRect(model.index(0, 0)).center(), Qt::LeftButton, Qt::LeftButton);
    QVERIFY(!delegate.getPressedIndex().isValid());
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, view->visualRect(model.index(0, 0)).center(), Qt::LeftButton, Qt::NoButton);

    // An option with State_Sunken (a view, which tracks the press itself) is pressed
    QStyleOptionViewItem option = DelegateSamples::viewOption(view, model.index(1, 0));
    option.state |= QStyle::State_Sunken;
    QVERIFY(delegate.getItemState(option, model.index(1, 0)).isPressed());
    view->setItemDelegate(nullptr);
}

void FluentComponentsTest::delegateActions()
{
    // Actions inside of a row: hover, press, click, tooltips; a click on an
    // action neither selects nor activates the item
    QStandardItemModel model;
    DelegateSamples::attachments(&model);
    pdf::PDFAttachmentDelegate delegate;
    QWidget window;
    window.resize(320, 260);
    pdf::PDFListView* view = new pdf::PDFListView(&window);
    view->setGeometry(0, 0, 320, 260);
    view->setModel(&model);
    view->setItemDelegate(&delegate);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QWidget* viewport = view->viewport();
    const QModelIndex first = model.index(0, 0);
    const QRect saveRect = delegate.getActionRect(view, first, pdf::PDFAttachmentDelegate::SaveAction);
    QVERIFY(!saveRect.isEmpty());
    QVERIFY(view->visualRect(first).contains(saveRect));
    QCOMPARE(saveRect.size(), QSize(24, 24));
    QCOMPARE(saveRect.right(), view->visualRect(first).right() - 6);

    QSignalSpy triggered(&delegate, &pdf::PDFRowDelegate::actionTriggered);
    QSignalSpy clicked(view, &QAbstractItemView::clicked);

    // Hover of the action
    DelegateSamples::sendMouse(viewport, QEvent::MouseMove, saveRect.center(), Qt::NoButton, Qt::NoButton);
    QCOMPARE(delegate.getHoveredAction(), int(pdf::PDFAttachmentDelegate::SaveAction));
    QCOMPARE(delegate.getActionFlags(DelegateSamples::viewOption(view, first), first, pdf::PDFAttachmentDelegate::SaveAction), QStyle::State(QStyle::State_MouseOver));
    DelegateSamples::sendMouse(viewport, QEvent::MouseMove, view->visualRect(first).center(), Qt::NoButton, Qt::NoButton);
    QCOMPARE(delegate.getHoveredAction(), -1);

    // Click of the action
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, saveRect.center());
    QCOMPARE(triggered.count(), 1);
    QCOMPARE(triggered.at(0).at(0).value<QModelIndex>(), first);
    QCOMPARE(triggered.at(0).at(1).toInt(), int(pdf::PDFAttachmentDelegate::SaveAction));
    QCOMPARE(clicked.count(), 0);
    QVERIFY(!view->selectionModel()->isSelected(first));

    // The press of the action draws it pressed (not the row), a release outside does nothing
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, saveRect.center(), Qt::LeftButton, Qt::LeftButton);
    QCOMPARE(delegate.getPressedAction(), int(pdf::PDFAttachmentDelegate::SaveAction));
    QVERIFY(delegate.getActionFlags(DelegateSamples::viewOption(view, first), first, pdf::PDFAttachmentDelegate::SaveAction).testFlag(QStyle::State_Sunken));
    QVERIFY(!delegate.getItemState(DelegateSamples::viewOption(view, first), first).isPressed());
    DelegateSamples::sendMouse(viewport, QEvent::MouseMove, view->visualRect(first).center(), Qt::NoButton, Qt::LeftButton);
    QVERIFY(!delegate.getActionFlags(DelegateSamples::viewOption(view, first), first, pdf::PDFAttachmentDelegate::SaveAction).testFlag(QStyle::State_Sunken));
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, view->visualRect(first).center(), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(triggered.count(), 1);

    // Press on the action of one row, release on the action of another row
    const QRect secondSave = delegate.getActionRect(view, model.index(1, 0), pdf::PDFAttachmentDelegate::SaveAction);
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, saveRect.center(), Qt::LeftButton, Qt::LeftButton);
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, secondSave.center(), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(triggered.count(), 1);

    // A click of the row selects it (no action)
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, view->visualRect(first).center());
    QCOMPARE(triggered.count(), 1);
    QCOMPARE(clicked.count(), 1);
    QVERIFY(view->selectionModel()->isSelected(first));

    // The double click of an action is a second click of it
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, saveRect.center(), Qt::LeftButton, Qt::LeftButton);
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, saveRect.center(), Qt::LeftButton, Qt::NoButton);
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonDblClick, saveRect.center(), Qt::LeftButton, Qt::LeftButton);
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, saveRect.center(), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(triggered.count(), 3);
    QCOMPARE(clicked.count(), 1);

    // Tooltip of the action, the tooltip of the item elsewhere
    model.setData(first, QStringLiteral("Tooltip of the item"), Qt::ToolTipRole);
    QHelpEvent toolTip(QEvent::ToolTip, saveRect.center(), viewport->mapToGlobal(saveRect.center()));
    QApplication::sendEvent(viewport, &toolTip);
    QCOMPARE(pdf::PDFToolTip::getTitle(), QStringLiteral("Save to a file"));
    const QPoint itemPoint = view->visualRect(first).center();
    QHelpEvent itemToolTip(QEvent::ToolTip, itemPoint, viewport->mapToGlobal(itemPoint));
    QApplication::sendEvent(viewport, &itemToolTip);
    QCOMPARE(pdf::PDFToolTip::getTitle(), QStringLiteral("Tooltip of the item"));
    const QPoint secondPoint = view->visualRect(model.index(1, 0)).center();
    QHelpEvent emptyToolTip(QEvent::ToolTip, secondPoint, viewport->mapToGlobal(secondPoint));
    QApplication::sendEvent(viewport, &emptyToolTip);
    QVERIFY(!pdf::PDFToolTip::isVisible());
    pdf::PDFToolTip::hideText();

    // A disabled item: its action does nothing
    const int count = int(triggered.count());
    model.item(2)->setEnabled(false);
    const QRect thirdSave = delegate.getActionRect(view, model.index(2, 0), pdf::PDFAttachmentDelegate::SaveAction);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, thirdSave.center());
    delegate.triggerAction(view, model.index(2, 0), pdf::PDFAttachmentDelegate::SaveAction);
    QCOMPARE(int(triggered.count()), count);

    // A disabled view
    view->setEnabled(false);
    delegate.triggerAction(view, first, pdf::PDFAttachmentDelegate::SaveAction);
    QCOMPARE(int(triggered.count()), count);
    view->setEnabled(true);

    // The star of the Recent item row writes the pinned state into the model; the menu button
    QStandardItemModel recentModel;
    DelegateSamples::recentItems(&recentModel);
    pdf::PDFRecentItemDelegate recent;
    view->setFixedWidth(820);
    view->setModel(&recentModel);
    view->setItemDelegate(&recent);
    QApplication::processEvents();
    QSignalSpy recentTriggered(&recent, &pdf::PDFRowDelegate::actionTriggered);
    const QModelIndex file = recentModel.index(0, 0);
    const QRect starRect = recent.getActionRect(view, file, pdf::PDFRecentItemDelegate::PinAction);
    QVERIFY(!starRect.isEmpty());
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, starRect.center());
    QCOMPARE(recentModel.data(file, pdf::PDFComponentRoles::PinnedRole).toBool(), true);
    QCOMPARE(recentTriggered.count(), 1);
    QCOMPARE(recent.getActions(DelegateSamples::viewOption(view, file), file).at(0).text, QStringLiteral("Unpin"));
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, starRect.center());
    QCOMPARE(recentModel.data(file, pdf::PDFComponentRoles::PinnedRole).toBool(), false);
    const QRect moreRect = recent.getActionRect(view, file, pdf::PDFRecentItemDelegate::MenuAction);
    QCOMPARE(moreRect.size(), QSize(16, 16));
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, moreRect.center());
    QCOMPARE(recentTriggered.count(), 3);
    QCOMPARE(recentTriggered.at(2).at(1).toInt(), int(pdf::PDFRecentItemDelegate::MenuAction));

    // The star under the pointer is drawn hovered
    DelegateSamples::sendMouse(viewport, QEvent::MouseMove, starRect.center(), Qt::NoButton, Qt::NoButton);
    QCOMPARE(recent.getActionFlags(DelegateSamples::viewOption(view, file), file, pdf::PDFRecentItemDelegate::PinAction), QStyle::State(QStyle::State_MouseOver));
    view->setItemDelegate(nullptr);
}

void FluentComponentsTest::delegateKeyboardAndMenu()
{
    // The actions are reachable by the context menu and by their shortcuts (components.md 2.6)
    QStandardItemModel model;
    DelegateSamples::fonts(&model);
    pdf::PDFFontDelegate delegate;
    QWidget window;
    window.resize(720, 200);
    pdf::PDFListView* view = new pdf::PDFListView(&window);
    view->setGeometry(0, 0, 720, 200);
    view->setModel(&model);
    view->setItemDelegate(&delegate);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QSignalSpy triggered(&delegate, &pdf::PDFRowDelegate::actionTriggered);

    // Ctrl+C on the current item copies the name of the font (also over a shortcut of the application)
    QAction applicationCopy(&window);
    applicationCopy.setShortcut(QKeySequence::Copy);
    window.addAction(&applicationCopy);
    QSignalSpy applicationTriggered(&applicationCopy, &QAction::triggered);
    QApplication::clipboard()->clear();
    view->setFocus(Qt::TabFocusReason);
    view->setCurrentIndex(model.index(1, 0));
    QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("Inter-SemiBold"));
    QCOMPARE(triggered.count(), 1);
    QCOMPARE(triggered.at(0).at(0).value<QModelIndex>(), model.index(1, 0));
    QCOMPARE(applicationTriggered.count(), 0);

    // The focus ring of the current item after the keyboard
    QTest::keyClick(view, Qt::Key_Down);
    QCOMPARE(view->currentIndex(), model.index(2, 0));
    QStyleOptionViewItem option = DelegateSamples::viewOption(view, model.index(2, 0));
    option.state |= QStyle::State_HasFocus;
    QVERIFY(delegate.getItemState(option, model.index(2, 0)).hasFocusRing());

    // The context menu of the item by the mouse
    const QPoint point = view->visualRect(model.index(0, 0)).center();
    QContextMenuEvent mouseMenu(QContextMenuEvent::Mouse, point, view->viewport()->mapToGlobal(point));
    QApplication::sendEvent(view->viewport(), &mouseMenu);
    QMenu* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    QVERIFY(menu);
    QCOMPARE(menu->actions().size(), 1);
    QVERIFY(menu->actions().at(0)->text().startsWith(QStringLiteral("Copy name\t")));
    menu->actions().at(0)->trigger();
    QCOMPARE(triggered.count(), 2);
    QCOMPARE(triggered.at(1).at(0).value<QModelIndex>(), model.index(0, 0));
    menu->close();
    QApplication::processEvents();

    // The context menu of the current item by the keyboard
    QContextMenuEvent keyboardMenu(QContextMenuEvent::Keyboard, QPoint(), view->mapToGlobal(QPoint()));
    QApplication::sendEvent(view, &keyboardMenu);
    menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    QVERIFY(menu);
    menu->actions().at(0)->trigger();
    QCOMPARE(triggered.at(2).at(0).value<QModelIndex>(), model.index(2, 0));
    menu->close();
    QApplication::processEvents();

    // A view with its own context menu policy, or the delegate with the menu turned off, shows none
    delegate.setContextMenuEnabled(false);
    QApplication::sendEvent(view->viewport(), &mouseMenu);
    QCOMPARE(QApplication::activePopupWidget(), nullptr);
    delegate.setContextMenuEnabled(true);
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    QApplication::sendEvent(view->viewport(), &mouseMenu);
    QCOMPARE(QApplication::activePopupWidget(), nullptr);

    // The application adds the actions to its own menu; a menu of an item
    // without actions is not created
    QMenu ownMenu;
    QCOMPARE(delegate.addActionsToMenu(&ownMenu, view, model.index(0, 0)), 1);
    QCOMPARE(delegate.addActionsToMenu(&ownMenu, view, QModelIndex()), 0);
    QCOMPARE(delegate.addActionsToMenu(nullptr, view, model.index(0, 0)), 0);
    pdf::PDFBookmarkDelegate bookmarks;
    QCOMPARE(bookmarks.createContextMenu(view, model.index(0, 0), view), nullptr);

    // A menu, whose model has been reset, triggers nothing
    QMenu* staleMenu = delegate.createContextMenu(view, model.index(0, 0), view);
    QVERIFY(staleMenu);
    model.clear();
    staleMenu->actions().at(0)->trigger();
    QCOMPARE(triggered.count(), 3);
    delete staleMenu;

    // A font without a name: the copy is disabled
    DelegateSamples::fonts(&model);
    model.setData(model.index(0, 0), QString(), Qt::DisplayRole);
    QVERIFY(!delegate.getActions(DelegateSamples::viewOption(view, model.index(0, 0)), model.index(0, 0)).at(0).enabled);
    view->setItemDelegate(nullptr);
}

void FluentComponentsTest::shortcutRecording()
{
    // Shortcut row: the recording is the editor of the delegate
    QStandardItemModel model;
    DelegateSamples::shortcuts(&model);
    pdf::PDFShortcutDelegate delegate;
    QWidget window;
    window.resize(820, 200);
    pdf::PDFListView* view = new pdf::PDFListView(&window);
    view->setGeometry(0, 0, 820, 160);
    view->setModel(&model);
    view->setItemDelegate(&delegate);
    QLineEdit* other = new QLineEdit(&window);
    other->setGeometry(0, 165, 200, 30);
    QAction applicationAction(&window);
    applicationAction.setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_K));
    window.addAction(&applicationAction);
    QSignalSpy applicationTriggered(&applicationAction, &QAction::triggered);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QSignalSpy changed(&delegate, &pdf::PDFShortcutDelegate::shortcutChanged);
    QSignalSpy conflict(&delegate, &pdf::PDFShortcutDelegate::shortcutConflict);
    const QModelIndex first = model.index(0, 0);
    auto shortcutOf = [&](int row) { return pdf::PDFShortcutDelegate::getShortcut(model.index(row, 0)); };

    // A click on the key cap starts the recording, the row shows it
    const QRect capRect = delegate.getActionRect(view, first, pdf::PDFShortcutDelegate::RecordAction);
    QVERIFY(!capRect.isEmpty());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, capRect.center());
    QVERIFY(delegate.isRecording(first));
    QCOMPARE(QModelIndex(delegate.getRecordingIndex()), first);
    QVERIFY(delegate.getContent(first).recording);
    pdf::PDFShortcutRecorder* recorder = qobject_cast<pdf::PDFShortcutRecorder*>(QApplication::focusWidget());
    QVERIFY(recorder);
    QCOMPARE(recorder->accessibleName(), Painter::getRecordShortcutText());

    // A lone modifier waits, the key with modifiers is recorded (not the shortcut of the application)
    QTest::keyClick(recorder, Qt::Key_Control, Qt::ControlModifier);
    QVERIFY(delegate.isRecording(first));
    QTest::keyClick(recorder, Qt::Key_K, Qt::ControlModifier);
    QApplication::processEvents();
    QCOMPARE(shortcutOf(0), QKeySequence(Qt::ControlModifier | Qt::Key_K));
    QVERIFY(!delegate.isRecording(first));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(applicationTriggered.count(), 0);

    // Escape cancels the recording, the shortcut stays
    view->setFocus();
    view->setCurrentIndex(first);
    QTest::keyClick(view, Qt::Key_Return);
    QVERIFY(delegate.isRecording(first));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QApplication::processEvents();
    QVERIFY(!delegate.isRecording(first));
    QCOMPARE(shortcutOf(0), QKeySequence(Qt::ControlModifier | Qt::Key_K));

    // The loss of the focus cancels it
    view->setFocus();
    QTest::keyClick(view, Qt::Key_F2);
    QVERIFY(delegate.isRecording(first));
    other->setFocus();
    QApplication::processEvents();
    QVERIFY(!delegate.isRecording(first));
    QCOMPARE(shortcutOf(0), QKeySequence(Qt::ControlModifier | Qt::Key_K));

    // Tab cancels it too
    view->setFocus();
    QTest::keyClick(view, Qt::Key_Space);
    QVERIFY(delegate.isRecording(first));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
    QApplication::processEvents();
    QVERIFY(!delegate.isRecording(first));

    // A conflict: Ctrl+S is the shortcut of the second row
    view->setFocus();
    QTest::keyClick(view, Qt::Key_Return);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_S, Qt::ControlModifier);
    QApplication::processEvents();
    QCOMPARE(conflict.count(), 1);
    QCOMPARE(conflict.at(0).at(0).value<QModelIndex>(), first);
    QCOMPARE(conflict.at(0).at(1).value<QKeySequence>(), QKeySequence(Qt::ControlModifier | Qt::Key_S));
    QCOMPARE(conflict.at(0).at(2).value<QModelIndexList>(), QModelIndexList({ model.index(1, 0) }));
    QCOMPARE(shortcutOf(0), QKeySequence(Qt::ControlModifier | Qt::Key_K));
    QVERIFY(!delegate.isRecording(first));

    // Accepted conflicts are written
    delegate.setConflictsAccepted(true);
    view->setFocus();
    QTest::keyClick(view, Qt::Key_Return);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_S, Qt::ControlModifier);
    QApplication::processEvents();
    QCOMPARE(conflict.count(), 2);
    QCOMPARE(shortcutOf(0), QKeySequence(Qt::ControlModifier | Qt::Key_S));
    delegate.setConflictsAccepted(false);
    QCOMPARE(pdf::PDFShortcutDelegate::findConflicts(first, QKeySequence(Qt::ControlModifier | Qt::Key_S)).size(), 1);
    QVERIFY(pdf::PDFShortcutDelegate::findConflicts(first, QKeySequence()).isEmpty());

    // The placeholder of an action without a shortcut starts the recording too
    const QModelIndex third = model.index(2, 0);
    QVERIFY(delegate.getContent(third).shortcut.isEmpty());
    const QRect placeholder = delegate.getActionRect(view, third, pdf::PDFShortcutDelegate::RecordAction);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, placeholder.center());
    QVERIFY(delegate.isRecording(third));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Delete);
    QApplication::processEvents();
    QCOMPARE(shortcutOf(2), QKeySequence(Qt::Key_Delete));

    // The context menu removes the shortcut
    delegate.triggerAction(view, third, pdf::PDFShortcutDelegate::ClearAction);
    QVERIFY(shortcutOf(2).isEmpty());
    QVERIFY(delegate.getContent(third).shortcut.isEmpty());
    QVERIFY(!delegate.getActions(DelegateSamples::viewOption(view, third), third).at(1).enabled);

    // A reset of the model during the recording
    view->setFocus();
    view->setCurrentIndex(first);
    QTest::keyClick(view, Qt::Key_Return);
    QVERIFY(delegate.isRecording(first));
    model.clear();
    QApplication::processEvents();
    QVERIFY(!delegate.getRecordingIndex().isValid());
    QTest::keyClick(&window, Qt::Key_A);

    // A row, which is not editable, does not record
    DelegateSamples::shortcuts(&model);
    model.item(0)->setEditable(false);
    QVERIFY(!delegate.getActions(DelegateSamples::viewOption(view, model.index(0, 0)), model.index(0, 0)).at(0).enabled);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, delegate.getActionRect(view, model.index(0, 0), pdf::PDFShortcutDelegate::RecordAction).center());
    QVERIFY(!delegate.isRecording(model.index(0, 0)));

    // The recorder alone: lone modifiers are not keys, the sequence of the key
    pdf::PDFShortcutRecorder standalone;
    QSignalSpy recorded(&standalone, &pdf::PDFShortcutRecorder::recorded);
    QSignalSpy canceled(&standalone, &pdf::PDFShortcutRecorder::canceled);
    QVERIFY(!pdf::PDFShortcutRecorder::isRecordableKey(Qt::Key_Shift));
    QVERIFY(!pdf::PDFShortcutRecorder::isRecordableKey(0));
    QVERIFY(pdf::PDFShortcutRecorder::isRecordableKey(Qt::Key_F5));
    QTest::keyClick(&standalone, Qt::Key_F5, Qt::ShiftModifier);
    QCOMPARE(recorded.count(), 1);
    QCOMPARE(standalone.getKeySequence(), QKeySequence(Qt::ShiftModifier | Qt::Key_F5));
    QTest::keyClick(&standalone, Qt::Key_Escape);
    QCOMPARE(canceled.count(), 0);
    QCOMPARE(recorded.count(), 1);
    view->setItemDelegate(nullptr);
}

void FluentComponentsTest::treeViewPressed()
{
    // Review I-23: PDFTreeView tracks the press of the rows of the presets
    // Outline and Certificate and gives State_Sunken to the delegates
    QStandardItemModel model;
    for (int i = 0; i < 3; ++i)
    {
        model.appendRow(DelegateSamples::item(QStringLiteral("Chapter %1").arg(i + 1)));
    }
    model.item(0)->appendRow(DelegateSamples::item(QStringLiteral("Section")));

    pdf::PDFTreeView tree;
    tree.setHeaderHidden(true);
    tree.setRowStyle(pdf::PDFTreeView::RowStyle::Outline);
    tree.setModel(&model);
    tree.resize(300, 200);
    tree.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tree));
    tree.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&tree));

    QWidget* viewport = tree.viewport();
    const QModelIndex second = model.index(1, 0);
    const QRect rect = tree.visualRect(second);
    const QPoint point(rect.right() - 20, rect.center().y());
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    QCOMPARE(tree.getPressedRow(), second);

    // The pressed selected row: bg/accent-subtle-pressed
    const QPoint probe = viewport->geometry().topLeft() + QPoint(rect.right() - 4, rect.top() + 3);
    QImage image = renderWidget(&tree, 1.0);
    QVERIFY2(isNear(pixel(image, probe.x(), probe.y()), blend(token(Color::BgAccentSubtlePressed), token(Color::BgSurface)), 3), qPrintable(pixel(image, probe.x(), probe.y()).name()));
    DelegateSamples::sendMouse(viewport, QEvent::MouseMove, QPoint(point.x(), tree.visualRect(model.index(2, 0)).center().y()), Qt::NoButton, Qt::LeftButton);
    QCOMPARE(tree.getPressedRow(), QModelIndex());
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(tree.getPressedRow(), QModelIndex());

    // An unselected pressed row: bg/pressed (Ctrl+press keeps it unselected after the toggle)
    tree.selectionModel()->clearSelection();
    tree.setSelectionMode(QAbstractItemView::NoSelection);
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    image = renderWidget(&tree, 1.0);
    QVERIFY2(isNear(pixel(image, probe.x(), probe.y()), blend(token(Color::BgPressed), token(Color::BgSurface)), 3), qPrintable(pixel(image, probe.x(), probe.y()).name()));

    // A delegate of the application gets the flag, the delegate is not replaced
    StateRecordingDelegate recording;
    tree.setItemDelegate(&recording);
    viewport->repaint();
    QVERIFY(recording.states.value(1).testFlag(QStyle::State_Sunken));
    QVERIFY(!recording.states.value(2).testFlag(QStyle::State_Sunken));
    QCOMPARE(tree.itemDelegate(), &recording);
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    recording.states.clear();
    viewport->repaint();
    QVERIFY(!recording.states.value(1).testFlag(QStyle::State_Sunken));

    // A reset of the model and the hide end the press
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    QVERIFY(tree.getPressedRow().isValid());
    tree.hide();
    QCOMPARE(tree.getPressedRow(), QModelIndex());
    tree.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tree));
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    model.clear();
    QCOMPARE(tree.getPressedRow(), QModelIndex());
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);

    // The preset Default does not track the press
    for (int i = 0; i < 3; ++i)
    {
        model.appendRow(DelegateSamples::item(QStringLiteral("Row %1").arg(i + 1)));
    }
    tree.setRowStyle(pdf::PDFTreeView::RowStyle::Default);
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, tree.visualRect(model.index(0, 0)).center(), Qt::LeftButton, Qt::LeftButton);
    QCOMPARE(tree.getPressedRow(), QModelIndex());
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, tree.visualRect(model.index(0, 0)).center(), Qt::LeftButton, Qt::NoButton);

    // The preset Certificate of a table: every cell of the pressed row
    QStandardItemModel table(2, 3);
    tree.setRowStyle(pdf::PDFTreeView::RowStyle::Certificate);
    tree.setModel(&table);
    tree.setItemDelegate(&recording);
    for (int column = 0; column < 3; ++column)
    {
        tree.setColumnWidth(column, 90);
    }
    QApplication::processEvents();
    recording.states.clear();
    const QPoint cell = tree.visualRect(table.index(1, 2)).center();
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, cell, Qt::LeftButton, Qt::LeftButton);
    QCOMPARE(tree.getPressedRow(), table.index(1, 0));
    viewport->repaint();
    QVERIFY2(recording.states.value(1).testFlag(QStyle::State_Sunken), qPrintable(QStringLiteral("%1 %2 %3").arg(recording.states.size()).arg(int(recording.states.value(1))).arg(int(recording.states.value(0)))));
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, cell, Qt::LeftButton, Qt::NoButton);
    tree.setItemDelegate(nullptr);
}

void FluentComponentsTest::delegateLifetime()
{
    // The model or the delegate deleted while the view is shown, a delegate shared by two views
    QWidget window;
    window.resize(640, 260);
    pdf::PDFListView* firstView = new pdf::PDFListView(&window);
    firstView->setGeometry(0, 0, 300, 260);
    pdf::PDFListView* secondView = new pdf::PDFListView(&window);
    secondView->setGeometry(320, 0, 300, 260);
    QStandardItemModel* firstModel = new QStandardItemModel();
    DelegateSamples::attachments(firstModel);
    QStandardItemModel secondModel;
    DelegateSamples::attachments(&secondModel);
    pdf::PDFAttachmentDelegate* delegate = new pdf::PDFAttachmentDelegate();
    firstView->setModel(firstModel);
    secondView->setModel(&secondModel);
    firstView->setItemDelegate(delegate);
    secondView->setItemDelegate(delegate);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QSignalSpy triggered(delegate, &pdf::PDFRowDelegate::actionTriggered);
    const QRect secondSave = delegate->getActionRect(secondView, secondModel.index(0, 0), pdf::PDFAttachmentDelegate::SaveAction);
    QTest::mouseClick(secondView->viewport(), Qt::LeftButton, Qt::NoModifier, secondSave.center());
    QCOMPARE(triggered.count(), 1);

    // A slot, which deletes the model of the clicked row
    connect(delegate, &pdf::PDFRowDelegate::actionTriggered, &window, [&](const QModelIndex& index, int)
    {
        if (index.model() == firstModel)
        {
            delete firstModel;
            firstModel = nullptr;
        }
    });
    const QRect firstSave = delegate->getActionRect(firstView, firstModel->index(0, 0), pdf::PDFAttachmentDelegate::SaveAction);
    DelegateSamples::sendMouse(firstView->viewport(), QEvent::MouseButtonPress, firstSave.center(), Qt::LeftButton, Qt::LeftButton);
    DelegateSamples::sendMouse(firstView->viewport(), QEvent::MouseButtonRelease, firstSave.center(), Qt::LeftButton, Qt::NoButton);
    QVERIFY(!firstModel);
    firstView->viewport()->repaint();
    DelegateSamples::sendMouse(firstView->viewport(), QEvent::MouseMove, firstSave.center(), Qt::NoButton, Qt::NoButton);
    DelegateSamples::sendMouse(firstView->viewport(), QEvent::MouseButtonPress, firstSave.center(), Qt::LeftButton, Qt::LeftButton);
    DelegateSamples::sendMouse(firstView->viewport(), QEvent::MouseButtonRelease, firstSave.center(), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(delegate->getPressedIndex(), QModelIndex());

    // The model deleted during the press of a row
    QStandardItemModel* pressedModel = new QStandardItemModel();
    DelegateSamples::attachments(pressedModel);
    firstView->setModel(pressedModel);
    QApplication::processEvents();
    DelegateSamples::sendMouse(firstView->viewport(), QEvent::MouseButtonPress, firstView->visualRect(pressedModel->index(0, 0)).center(), Qt::LeftButton, Qt::LeftButton);
    QVERIFY(delegate->getPressedIndex().isValid());
    delete pressedModel;
    QVERIFY(!delegate->getPressedIndex().isValid());
    DelegateSamples::sendMouse(firstView->viewport(), QEvent::MouseButtonRelease, QPoint(20, 20), Qt::LeftButton, Qt::NoButton);
    firstView->viewport()->repaint();

    // The delegate deleted while the views are shown
    delete delegate;
    QApplication::processEvents();
    DelegateSamples::sendMouse(secondView->viewport(), QEvent::MouseMove, secondSave.center(), Qt::NoButton, Qt::NoButton);
    QTest::mouseClick(secondView->viewport(), Qt::LeftButton, Qt::NoModifier, secondSave.center());
    secondView->viewport()->repaint();

    // A view deleted before the delegate
    pdf::PDFBookmarkDelegate survivor;
    pdf::PDFListView* temporary = new pdf::PDFListView(&window);
    temporary->setModel(&secondModel);
    temporary->setItemDelegate(&survivor);
    temporary->show();
    temporary->viewport()->repaint();
    QVERIFY(survivor.isWatching(temporary));
    DelegateSamples::sendMouse(temporary->viewport(), QEvent::MouseButtonPress, temporary->visualRect(secondModel.index(0, 0)).center(), Qt::LeftButton, Qt::LeftButton);
    QVERIFY(survivor.getPressedIndex().isValid());
    delete temporary;
    QCOMPARE(survivor.getWatchedViewCount(), 0);
    QVERIFY(!survivor.getPressedIndex().isValid());
    QCOMPARE(survivor.getHoveredAction(), -1);
    survivor.watchView(nullptr);
    QPainter* nullPainter = nullptr;
    QStyleOptionViewItem option;
    survivor.paint(nullPainter, option, secondModel.index(0, 0));
}

void FluentComponentsTest::delegateDirectionAndTheme()
{
    // Right-to-left views mirror the rows and their actions; the theme changes repaint them
    QStandardItemModel model;
    DelegateSamples::attachments(&model);
    pdf::PDFAttachmentDelegate delegate;
    QWidget window;
    window.resize(320, 200);
    window.setLayoutDirection(Qt::RightToLeft);
    pdf::PDFListView* view = new pdf::PDFListView(&window);
    view->setGeometry(0, 0, 320, 200);
    view->setModel(&model);
    view->setItemDelegate(&delegate);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QModelIndex first = model.index(0, 0);
    const QRect itemRect = view->visualRect(first);
    const QRect saveRect = delegate.getActionRect(view, first, pdf::PDFAttachmentDelegate::SaveAction);
    QCOMPARE(saveRect.left(), itemRect.left() + 6);
    QSignalSpy triggered(&delegate, &pdf::PDFRowDelegate::actionTriggered);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, saveRect.center());
    QCOMPARE(triggered.count(), 1);

    // The badge at the right edge of the row (rendered by the view)
    const QPoint origin = view->viewport()->geometry().topLeft() + itemRect.topLeft();
    QImage image = renderWidget(view, 1.0);
    QVERIFY(isNear(pixel(image, origin.x() + itemRect.width() - 12, origin.y() + 24), token(Color::AccentNeutralBg), 2));

    // The dark theme: the colors of the theme without any leak
    view->selectionModel()->select(first, QItemSelectionModel::ClearAndSelect);
    QTest::mouseMove(view->viewport(), QPoint(20, view->viewport()->height() - 4));
    QHoverEvent hoverLeave(QEvent::HoverLeave, QPointF(-1, -1), QPointF(-1, -1), QPointF(20, 20));
    QApplication::sendEvent(view->viewport(), &hoverLeave);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    QApplication::processEvents();
    image = renderWidget(view, 1.0);
    QVERIFY(isNear(pixel(image, origin.x() + itemRect.width() - 12, origin.y() + 24), token(Color::AccentNeutralBg), 2));
    const QColor viewBackground = pixel(image, origin.x() + 140, view->height() - 12);
    QVERIFY2(isNear(pixel(image, origin.x() + 140, origin.y() + 2), blend(token(Color::BgAccentSubtle), viewBackground), 3), qPrintable(pixel(image, origin.x() + 140, origin.y() + 2).name()));
    QCOMPARE(countLeakPixels(renderWidget(&window, 2.0)), 0);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    // Each delegate in both directions with the same state: mirrored content
    QStandardItemModel recentModel;
    DelegateSamples::recentItems(&recentModel);
    pdf::PDFRecentItemDelegate recent;
    const QImage leftToRight = renderDelegate(&recent, recentModel.index(1, 0), QStyle::State_Enabled, QSize(802, 56), 1.0, Qt::LeftToRight);
    const QImage rightToLeft = renderDelegate(&recent, recentModel.index(1, 0), QStyle::State_Enabled, QSize(802, 56), 1.0, Qt::RightToLeft);
    QVERIFY(isNear(pixel(leftToRight, 18, 28), token(Color::AccentNeutralBg), 2));
    QVERIFY(isNear(pixel(rightToLeft, 802 - 19, 28), token(Color::AccentNeutralBg), 2));
    QStyleOptionViewItem rtlOption;
    rtlOption.rect = QRect(0, 0, 802, 56);
    rtlOption.state = QStyle::State_Enabled;
    rtlOption.direction = Qt::RightToLeft;
    QCOMPARE(recent.getActions(rtlOption, recentModel.index(1, 0)).at(1).rect, QRectF(8, 20, 16, 16));

    // The painter of a delegate does not depend on the direction of the incoming painter
    for (const Qt::LayoutDirection painterDirection : { Qt::LeftToRight, Qt::RightToLeft, Qt::LayoutDirectionAuto })
    {
        const QImage painted = render([&](QPainter& painter)
        {
            painter.setLayoutDirection(painterDirection);
            QStyleOptionViewItem option = rtlOption;
            recent.paint(&painter, option, recentModel.index(1, 0));
            QCOMPARE(painter.layoutDirection(), painterDirection);
        }, QSize(802, 56));
        QCOMPARE(describeDifference(painted, rightToLeft, 0), QString());
    }
    view->setItemDelegate(nullptr);
}

void FluentComponentsTest::delegateDefensive()
{
    using Roles = pdf::PDFComponentRoles;

    // Missing roles, other types, an invalid index: the defaults
    QStandardItemModel model;
    QStandardItem* item = DelegateSamples::item(QString());
    item->setData(QStringLiteral("not a hue"), Roles::HueRole);
    item->setData(99, Roles::ItemTypeRole);
    item->setData(QStringLiteral("x"), Roles::ProgressRole);
    item->setData(QStringLiteral("yes"), Roles::EmbeddedRole);
    item->setData(QStringLiteral("garbage"), Roles::MatchRangesRole);
    item->setData(QVariant::fromValue(QSize(3, 3)), Roles::PixmapRole);
    item->setData(42, Roles::ShortcutRole);
    model.appendRow(item);
    const QModelIndex index = model.index(0, 0);

    pdf::PDFNoteDelegate note;
    QCOMPARE(note.getContent(index).hue, pdf::PDFAccentHue::Amber);
    QCOMPARE(note.getContent(index).iconName, QStringLiteral("highlighter"));
    QCOMPARE(note.getContent(QModelIndex()).author, QString());
    pdf::PDFRecentItemDelegate recent;
    QCOMPARE(recent.getContent(index).type, Painter::RecentItemType::File);
    QVERIFY(recent.getContent(index).progress < 0.0);
    QVERIFY(recent.getContent(index).pixmap.isNull());
    pdf::PDFFontDelegate font;
    QCOMPARE(font.getContent(index).embedded, true);
    QCOMPARE(font.getContent(QModelIndex()).embedded, true);
    pdf::PDFShortcutDelegate shortcut;
    QVERIFY(shortcut.getContent(index).shortcut.isEmpty());
    QVERIFY(pdf::PDFShortcutDelegate::getShortcut(QModelIndex()).isEmpty());
    pdf::PDFSearchResultDelegate search;
    QVERIFY(search.getContent(index).matches.isEmpty());

    // Ranges of the found terms: QPoint, a list, invalid and too long ranges
    QCOMPARE(pdf::PDFSearchResultDelegate::getMatchRanges(QPoint(2, 3)).size(), 1);
    QCOMPARE(pdf::PDFSearchResultDelegate::getMatchRanges(QVariantList{ QPoint(0, 1), QPoint(-1, 2), QPoint(3, 0), QStringLiteral("x") }).size(), 1);
    QCOMPARE(pdf::PDFSearchResultDelegate::getMatchRanges(QVariant::fromValue(QList<QPoint>{ QPoint(1, 2), QPoint(4, 2) })).size(), 2);
    QVERIFY(pdf::PDFSearchResultDelegate::getMatchRanges(QVariant()).isEmpty());
    Painter::SearchResultRow searchRow;
    searchRow.page = QStringLiteral("Page 1");
    searchRow.snippet = QStringLiteral("abc");
    searchRow.matches = { Painter::TextRange{ 2, 1000 }, Painter::TextRange{ 500, 3 }, Painter::TextRange{ std::numeric_limits<int>::max(), std::numeric_limits<int>::max() } };
    QImage image = render([&](QPainter& painter) { Painter::paintSearchResultRow(&painter, QRectF(0, 0, 276, 47), searchRow, state(QStyle::State_None)); }, QSize(276, 47));
    QCOMPARE(countLeakPixels(image), 0);

    // The picture from Qt::DecorationRole, from a QImage
    QImage picture(20, 30, QImage::Format_ARGB32);
    picture.fill(Qt::red);
    model.setData(index, picture, Qt::DecorationRole);
    model.setData(index, QVariant(), Roles::PixmapRole);
    QVERIFY(!pdf::PDFRowDelegate::getPixmap(index).isNull());
    QVERIFY(pdf::PDFRowDelegate::getPixmap(QModelIndex()).isNull());

    // Readers of the roles
    QCOMPARE(pdf::PDFRowDelegate::getBool(index, Roles::EmbeddedRole, false), false);
    QCOMPARE(pdf::PDFRowDelegate::getReal(index, Roles::ProgressRole, 0.25), 0.25);
    model.setData(index, qQNaN(), Roles::ProgressRole);
    QCOMPARE(pdf::PDFRowDelegate::getReal(index, Roles::ProgressRole, 0.25), 0.25);
    QCOMPARE(pdf::PDFRowDelegate::getInt(QModelIndex(), Roles::HueRole, 7), 7);
    QCOMPARE(pdf::PDFRowDelegate::getHue(index, Roles::HueRole, pdf::PDFAccentHue::Teal), pdf::PDFAccentHue::Teal);

    // Every delegate: paint and size of an invalid index, a null painter, empty rectangles
    pdf::PDFThumbnailDelegate thumbnail;
    pdf::PDFBookmarkDelegate bookmark;
    pdf::PDFAttachmentDelegate attachment;
    const QList<pdf::PDFRowDelegate*> delegates = { &thumbnail, &bookmark, &note, &attachment, &search, &font, &recent, &shortcut };
    for (pdf::PDFRowDelegate* delegate : delegates)
    {
        QStyleOptionViewItem option;
        option.rect = QRect(0, 0, 300, 60);
        option.state = QStyle::State_Enabled | QStyle::State_MouseOver | QStyle::State_Selected | QStyle::State_HasFocus;
        image = render([&](QPainter& painter) { delegate->paint(&painter, option, QModelIndex()); delegate->paint(&painter, option, index); }, QSize(300, 60));
        QCOMPARE(countLeakPixels(image), 0);
        delegate->paint(nullptr, option, index);
        QVERIFY(delegate->sizeHint(option, QModelIndex()).isValid());
        QVERIFY(delegate->getActions(option, QModelIndex()).isEmpty());
        QCOMPARE(delegate->getActionAt(option, QModelIndex(), QPointF(10, 10)), -1);
        option.rect = QRect(0, 0, -5, -5);
        image = render([&](QPainter& painter) { delegate->paint(&painter, option, index); }, QSize(10, 10));
        QCOMPARE(countLeakPixels(image), 0);
        option.rect = QRect(0, 0, 3, 3);
        image = render([&](QPainter& painter) { delegate->paint(&painter, option, index); }, QSize(10, 10));
        QCOMPARE(countLeakPixels(image), 0);
        delegate->triggerAction(nullptr, QModelIndex(), 0);
        delegate->triggerAction(nullptr, index, 99);
        QCOMPARE(delegate->getActionRect(nullptr, index, 0), QRect());
        QCOMPARE(delegate->createContextMenu(nullptr, QModelIndex(), nullptr), nullptr);
        QHelpEvent help(QEvent::ToolTip, QPoint(1, 1), QPoint(1, 1));
        QVERIFY(!delegate->helpEvent(&help, nullptr, option, QModelIndex()));
    }

    // Invalid sizes of the page slot of a thumbnail
    thumbnail.setPageSize(QSize(-1, 10));
    QCOMPARE(thumbnail.getPageSize(), QSize(108, 148));
    thumbnail.setPageSize(QSize(100000, 100000));
    QCOMPARE(thumbnail.getPageSize(), QSize(4096, 4096));
    thumbnail.setPageSize(QSize(80, 100000));
    QCOMPARE(thumbnail.getPageSize(), QSize(80, 4096));
    thumbnail.setPageSize(QSize(0, 0));
    QCOMPARE(thumbnail.getPageSize(), QSize(108, 148));
    QSignalSpy sizeChanged(&thumbnail, &QAbstractItemDelegate::sizeHintChanged);
    thumbnail.setPageSize(QSize(80, 110));
    QCOMPARE(thumbnail.getPageSize(), QSize(80, 110));
    QCOMPARE(sizeChanged.count(), 1);
    thumbnail.setPageSize(QSize(80, 110));
    QCOMPARE(sizeChanged.count(), 1);

    // The painters: null painter, empty, negative, huge and not finite rectangles, invalid enumerations
    const QList<QRectF> rects = { QRectF(), QRectF(0, 0, -10, 10), QRectF(0, 0, 1e9, 1e9), QRectF(0, 0, qQNaN(), 10), QRectF(0, 0, 2, 2) };
    Painter::RecentItemRow recentRow;
    recentRow.type = static_cast<Painter::RecentItemType>(77);
    recentRow.progress = std::numeric_limits<qreal>::infinity();
    Painter::NoteRow noteRow;
    noteRow.hue = static_cast<pdf::PDFAccentHue>(-3);
    noteRow.iconName = QStringLiteral("no-such-icon");
    noteRow.text = QString(5000, QLatin1Char('x'));
    for (const QRectF& rect : rects)
    {
        image = render([&](QPainter& painter)
        {
            const Painter::State flags = state(QStyle::State_MouseOver | QStyle::State_Sunken | QStyle::State_HasFocus | QStyle::State_Selected);
            Painter::paintThumbnailItem(&painter, rect, Painter::ThumbnailItem{ QString(1000, QLatin1Char('9')), QPixmap(), QSize(0, 0) }, flags);
            Painter::paintBookmarkRow(&painter, rect, Painter::BookmarkRow{ QString(), QString(), QString(500, QLatin1Char('t')) }, flags);
            Painter::paintNoteRow(&painter, rect, noteRow, flags);
            Painter::paintAttachmentRow(&painter, rect, Painter::AttachmentRow(), flags);
            Painter::paintSearchResultRow(&painter, rect, searchRow, flags);
            Painter::paintFontRow(&painter, rect, Painter::FontRow(), flags);
            Painter::paintRecentItemRow(&painter, rect, recentRow, flags);
            Painter::paintShortcutRow(&painter, rect, Painter::ShortcutRow{ QString(), QString(3000, QLatin1Char('K')), QString(), true }, flags);
        }, QSize(40, 40));
        QCOMPARE(countLeakPixels(image), 0);
        Painter::paintThumbnailItem(nullptr, rect, Painter::ThumbnailItem(), state(QStyle::State_None));
        Painter::paintShortcutRow(nullptr, rect, Painter::ShortcutRow(), state(QStyle::State_None));
    }
    QCOMPARE(Painter::normalized(static_cast<Painter::RecentItemType>(77)), Painter::RecentItemType::File);
    QCOMPARE(Painter::getAttachmentRowSaveRect(QRectF(), state(QStyle::State_None)), QRectF());
    QCOMPARE(Painter::getShortcutRowKeyRect(QRectF(0, 0, 30, 40), Painter::ShortcutRow{ QString(), QStringLiteral("Ctrl+O"), QString(), false }, state(QStyle::State_None)).width(), 0.0);
    QVERIFY(Painter::getNoteRowHeight(noteRow, -100) >= 48);
    QVERIFY(Painter::getNoteRowHeight(noteRow, std::numeric_limits<int>::max()) >= 48);
    QVERIFY(Painter::getSearchResultRowHeight(searchRow, std::numeric_limits<int>::min()) >= 29);
    QVERIFY(Painter::getThumbnailItemSize(Painter::ThumbnailItem{ QString(100000, QLatin1Char('9')), QPixmap(), QSize(108, 148) }).width() <= 4096 + 16);
}

void FluentComponentsTest::delegateGallery()
{
    // Every variant of the eight sets has its sample with the node of the design,
    // the live views use the delegates
    const std::vector<pdffluentgallery::GalleryPages::Page> pages = pdffluentgallery::GalleryPages::createPages(nullptr);
    QSet<QString> nodes;
    QMap<QString, int> delegateViews;
    for (const pdffluentgallery::GalleryPages::Page& page : pages)
    {
        for (const pdffluentgallery::GallerySpecimen* specimen : page.widget->findChildren<pdffluentgallery::GallerySpecimen*>())
        {
            nodes.insert(specimen->getFigmaNode());
        }
        for (const QAbstractItemView* view : page.widget->findChildren<QAbstractItemView*>())
        {
            if (qobject_cast<pdf::PDFRowDelegate*>(view->itemDelegate()))
            {
                delegateViews[page.id] += 1;
            }
        }
    }

    for (const char* node : { "191:593", "191:616", "191:639", "191:662", "191:685", "191:708", "191:731", "191:754",
                              "191:847", "191:855", "191:863", "191:871", "191:879", "191:887", "191:895", "191:903",
                              "191:918", "191:928", "191:938", "191:948", "191:958", "191:968", "191:978", "191:988",
                              "191:1005", "191:1015", "191:1025", "191:1035", "706:149934", "706:149944", "706:149954", "706:149964",
                              "191:1049", "191:1052", "191:1055", "191:1058", "191:1061", "191:1064", "191:1067", "191:1070",
                              "206:880", "206:895", "706:147131", "706:147147", "206:912", "206:927", "706:147161", "706:147177",
                              "79:802", "79:822", "79:842", "79:862", "79:882", "79:900", "79:918", "79:936", "79:954", "79:972", "79:990", "79:1008",
                              "145:2880", "145:2886", "145:2892", "145:2898", "706:149339" })
    {
        QVERIFY2(nodes.contains(QString::fromLatin1(node)), node);
    }

    QCOMPARE(delegateViews.value(QStringLiteral("19-shell-home")), 3);
    QCOMPARE(delegateViews.value(QStringLiteral("21-forms")), 3);
    // 16 views of the document delegates and the two views of one shared delegate (lifetime test)
    QCOMPARE(delegateViews.value(QStringLiteral("22-document")), 18);

    // The states Pressed and Focus of the presets Outline and Certificate (review F4 of step 5)
    QSet<QString> liveNodes;
    for (const pdffluentgallery::GalleryPages::Page& page : pages)
    {
        for (const QWidget* widget : page.widget->findChildren<QWidget*>())
        {
            liveNodes.insert(widget->property("galleryNode").toString());
        }
    }
    for (const char* node : { "191:798", "191:805", "191:826", "191:833", "706:147107", "706:147113", "706:147119", "706:147125" })
    {
        QVERIFY2(liveNodes.contains(QString::fromLatin1(node)), node);
    }
    QCOMPARE(delegateViews.value(QStringLiteral("23-panels")), 3);

    for (const pdffluentgallery::GalleryPages::Page& page : pages)
    {
        delete page.widget;
    }
}

/// Helpers of the tests of the review of the step 5 (delegates)
class DelegateReviewSamples
{
public:
    DelegateReviewSamples() = delete;

    /// Sends a hover event to the widget (as QApplication sends it after a move)
    static void sendHover(QWidget* widget, QEvent::Type type, const QPoint& position)
    {
        QHoverEvent event(type, QPointF(position), widget->mapToGlobal(QPointF(position)), QPointF(position));
        QApplication::sendEvent(widget, &event);
    }

    /// Returns the logical rectangle of the item in the coordinates of the rendered view
    static QRect itemRectInView(QAbstractItemView* view, const QModelIndex& index)
    {
        return view->visualRect(index).translated(view->viewport()->geometry().topLeft());
    }
};

/// List view of an application with its own class and context menu
class ApplicationListView : public QListView
{
    Q_OBJECT

public:
    using QListView::QListView;

    int contextMenus = 0;

protected:
    virtual void contextMenuEvent(QContextMenuEvent* event) override
    {
        ++contextMenus;
        event->accept();
    }
};

/// Model, which counts the reads of the pictures (a model of thumbnails renders the page there)
class PictureCountingModel : public QStandardItemModel
{
public:
    using QStandardItemModel::QStandardItemModel;

    virtual QVariant data(const QModelIndex& index, int role) const override
    {
        if (role == Qt::DecorationRole || role == pdf::PDFComponentRoles::PixmapRole)
        {
            ++pictureReads;
        }
        return QStandardItemModel::data(index, role);
    }

    mutable int pictureReads = 0;
};

/// Model, which refuses to write the shortcut
class RefusingShortcutModel : public QStandardItemModel
{
public:
    using QStandardItemModel::QStandardItemModel;

    virtual bool setData(const QModelIndex& index, const QVariant& value, int role) override
    {
        if (role == pdf::PDFComponentRoles::ShortcutRole)
        {
            return false;
        }
        return QStandardItemModel::setData(index, value, role);
    }
};

/// Model, whose items are edited through a buddy, which does not exist (the view refuses the editor)
class InvalidBuddyModel : public QStandardItemModel
{
public:
    using QStandardItemModel::QStandardItemModel;

    virtual QModelIndex buddy(const QModelIndex& index) const override
    {
        Q_UNUSED(index);
        return QModelIndex();
    }
};

/// Exposes the width of an item, which the delegates compute
class WidthProbeDelegate : public pdf::PDFNoteDelegate
{
public:
    using pdf::PDFNoteDelegate::PDFNoteDelegate;

    static int width(const QStyleOptionViewItem& option, const QModelIndex& index) { return getItemWidth(option, index); }
};

void FluentComponentsTest::delegateViewLifetime()
{
    // Review C1: the lifetime of the watched views in every order. The views and
    // viewports are compared by pointers only, a dying view is never asked for
    // its viewport (a dangling pointer of Qt during the destructor of QWidget).
    QStandardItemModel model;
    DelegateSamples::attachments(&model);

    // (1) The delegate is a child of the view (the pattern of Qt), the view is
    // deleted after a paint, with the hover and the press tracked
    {
        pdf::PDFListView* view = new pdf::PDFListView();
        view->setModel(&model);
        QPointer<pdf::PDFAttachmentDelegate> delegate = new pdf::PDFAttachmentDelegate(view);
        QVERIFY(delegate->isWatching(view));
        view->setItemDelegate(delegate.data());
        view->resize(320, 200);
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view));
        view->viewport()->repaint();
        const QRect saveRect = delegate->getActionRect(view, model.index(0, 0), pdf::PDFAttachmentDelegate::SaveAction);
        DelegateReviewSamples::sendHover(view->viewport(), QEvent::HoverMove, saveRect.center());
        QCOMPARE(delegate->getHoveredAction(), int(pdf::PDFAttachmentDelegate::SaveAction));
        DelegateSamples::sendMouse(view->viewport(), QEvent::MouseButtonPress, view->visualRect(model.index(1, 0)).center(), Qt::LeftButton, Qt::LeftButton);
        QVERIFY(delegate->getPressedIndex().isValid());
        delete view;
        QVERIFY(!delegate);
    }

    // (2) The view and the delegate are children of one window, both orders of their creation
    for (const bool isDelegateFirst : { true, false })
    {
        QWidget* window = new QWidget();
        window->resize(320, 200);
        QPointer<pdf::PDFAttachmentDelegate> delegate;
        if (isDelegateFirst)
        {
            delegate = new pdf::PDFAttachmentDelegate(window);
        }
        pdf::PDFListView* view = new pdf::PDFListView(window);
        view->setGeometry(0, 0, 320, 200);
        if (!isDelegateFirst)
        {
            delegate = new pdf::PDFAttachmentDelegate(window);
        }
        view->setModel(&model);
        view->setItemDelegate(delegate.data());
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        view->viewport()->repaint();
        QVERIFY(delegate->isWatching(view));
        DelegateSamples::sendMouse(view->viewport(), QEvent::MouseButtonPress, view->visualRect(model.index(0, 0)).center(), Qt::LeftButton, Qt::LeftButton);
        delete window;
        QVERIFY(!delegate);
    }

    // (3) The view is deleted before the delegate: the delegate forgets it
    pdf::PDFBookmarkDelegate survivor;
    {
        pdf::PDFListView* view = new pdf::PDFListView();
        view->setModel(&model);
        view->setItemDelegate(&survivor);
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view));
        view->viewport()->repaint();
        QVERIFY(survivor.isWatching(view));
        QCOMPARE(survivor.getWatchedViewCount(), 1);
        DelegateSamples::sendMouse(view->viewport(), QEvent::MouseButtonPress, view->visualRect(model.index(0, 0)).center(), Qt::LeftButton, Qt::LeftButton);
        QVERIFY(survivor.getPressedIndex().isValid());
        delete view;
        QCOMPARE(survivor.getWatchedViewCount(), 0);
        QVERIFY(!survivor.getPressedIndex().isValid());
    }

    // (4) The delegate is deleted before the view: the filters and the hover
    // attribute, which the delegate has set, are removed
    {
        pdf::PDFListView view;
        view.setModel(&model);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const bool hadHover = view.viewport()->testAttribute(Qt::WA_Hover);
        pdf::PDFAttachmentDelegate* delegate = new pdf::PDFAttachmentDelegate();
        view.setItemDelegate(delegate);
        view.viewport()->repaint();
        QVERIFY(delegate->isWatching(&view));
        QVERIFY(view.viewport()->testAttribute(Qt::WA_Hover));
        view.setItemDelegate(nullptr);
        delete delegate;
        QCOMPARE(view.viewport()->testAttribute(Qt::WA_Hover), hadHover);
        DelegateSamples::sendMouse(view.viewport(), QEvent::MouseButtonPress, QPoint(20, 20), Qt::LeftButton, Qt::LeftButton);
        DelegateSamples::sendMouse(view.viewport(), QEvent::MouseButtonRelease, QPoint(20, 20), Qt::LeftButton, Qt::NoButton);
        view.viewport()->repaint();
    }

    // (5) setItemDelegate of another delegate: the view is not watched any more
    {
        pdf::PDFListView view;
        view.setModel(&model);
        pdf::PDFAttachmentDelegate first;
        pdf::PDFBookmarkDelegate second;
        view.setItemDelegate(&first);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.viewport()->repaint();
        QVERIFY(first.isWatching(&view));
        view.setItemDelegate(&second);
        QTRY_VERIFY(!first.isWatching(&view));
        view.viewport()->repaint();
        QVERIFY(second.isWatching(&view));

        // A delegate of a column is still in use (it is not unwatched)
        view.setItemDelegateForColumn(0, &first);
        view.viewport()->repaint();
        QVERIFY(first.isWatching(&view));
        view.setItemDelegate(nullptr);
        QApplication::processEvents();
        QVERIFY(first.isWatching(&view));
        view.setItemDelegateForColumn(0, nullptr);
        QTRY_VERIFY(!first.isWatching(&view));
    }

    // (6) One delegate for two views, one view deleted: the other still works
    {
        QWidget window;
        window.resize(640, 200);
        pdf::PDFAttachmentDelegate delegate;
        pdf::PDFListView* firstView = new pdf::PDFListView(&window);
        firstView->setGeometry(0, 0, 300, 200);
        pdf::PDFListView* secondView = new pdf::PDFListView(&window);
        secondView->setGeometry(320, 0, 300, 200);
        for (pdf::PDFListView* view : { firstView, secondView })
        {
            view->setModel(&model);
            view->setItemDelegate(&delegate);
        }
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        firstView->viewport()->repaint();
        secondView->viewport()->repaint();
        QCOMPARE(delegate.getWatchedViewCount(), 2);
        const QRect saveRect = delegate.getActionRect(firstView, model.index(0, 0), pdf::PDFAttachmentDelegate::SaveAction);
        DelegateReviewSamples::sendHover(firstView->viewport(), QEvent::HoverMove, saveRect.center());
        QCOMPARE(delegate.getHoveredAction(), int(pdf::PDFAttachmentDelegate::SaveAction));
        delete firstView;
        QCOMPARE(delegate.getWatchedViewCount(), 1);
        QCOMPARE(delegate.getHoveredAction(), -1);
        DelegateReviewSamples::sendHover(secondView->viewport(), QEvent::HoverMove, saveRect.center());
        QCOMPARE(delegate.getHoveredAction(), int(pdf::PDFAttachmentDelegate::SaveAction));
        secondView->setItemDelegate(nullptr);
    }

    // (7) A viewport replaced by setViewport: the new one is watched, the old one is deleted
    {
        QStandardItemModel recentModel;
        DelegateSamples::recentItems(&recentModel);
        pdf::PDFRecentItemDelegate delegate;
        QListView view;
        view.setModel(&recentModel);
        view.setItemDelegate(&delegate);
        view.resize(830, 200);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.viewport()->repaint();
        QPointer<QWidget> oldViewport = view.viewport();
        view.setViewport(new QWidget());
        QVERIFY(!oldViewport);
        QApplication::processEvents();
        view.viewport()->repaint();
        QVERIFY(delegate.isWatching(&view));
        const QRect starRect = delegate.getActionRect(&view, recentModel.index(0, 0), pdf::PDFRecentItemDelegate::PinAction);
        DelegateReviewSamples::sendHover(view.viewport(), QEvent::HoverMove, starRect.center());
        QCOMPARE(delegate.getHoveredAction(), int(pdf::PDFRecentItemDelegate::PinAction));
        QSignalSpy triggered(&delegate, &pdf::PDFRowDelegate::actionTriggered);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, starRect.center());
        QCOMPARE(triggered.count(), 1);
        QCOMPARE(delegate.getLastActionView(), &view);
        view.setItemDelegate(nullptr);
    }

    // (8) watchView / unwatchView of the application, null pointers
    {
        pdf::PDFBookmarkDelegate delegate;
        QListView view;
        delegate.watchView(nullptr);
        delegate.unwatchView(nullptr);
        delegate.watchView(&view);
        delegate.watchView(&view);
        QCOMPARE(delegate.getWatchedViewCount(), 1);
        delegate.unwatchView(&view);
        QCOMPARE(delegate.getWatchedViewCount(), 0);
        QVERIFY(!delegate.isWatching(nullptr));
    }
}

void FluentComponentsTest::delegateLostRelease()
{
    // Review C2: a press, whose release never comes (a modal dialog opened by a
    // slot of a double click, a drag of the platform), does not stay pressed
    QStandardItemModel model;
    DelegateSamples::bookmarks(&model);
    pdf::PDFBookmarkDelegate delegate;
    QWidget window;
    window.resize(660, 260);
    pdf::PDFListView* view = new pdf::PDFListView(&window);
    view->setGeometry(0, 0, 320, 220);
    view->setModel(&model);
    view->setItemDelegate(&delegate);
    pdf::PDFListView* secondView = new pdf::PDFListView(&window);
    secondView->setGeometry(330, 0, 320, 220);
    secondView->setModel(&model);
    secondView->setItemDelegate(&delegate);
    QLineEdit* other = new QLineEdit(&window);
    other->setGeometry(0, 225, 200, 30);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QWidget* viewport = view->viewport();
    const QPoint center = view->visualRect(model.index(0, 0)).center();
    auto press = [&]()
    {
        DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
        return delegate.getPressedIndex().isValid();
    };

    // A hover move without a pressed button (the state of the application)
    QVERIFY(press());
    DelegateReviewSamples::sendHover(viewport, QEvent::HoverMove, center);
    QVERIFY(!delegate.getPressedIndex().isValid());

    // A mouse move without the button (not delivered to the viewport, its hover move is)
    QVERIFY(press());
    DelegateSamples::sendMouse(viewport, QEvent::MouseMove, center + QPoint(2, 0), Qt::NoButton, Qt::NoButton);
    QVERIFY(!delegate.getPressedIndex().isValid());

    // A move with the button keeps the press (the hover move of this move too)
    QVERIFY(press());
    DelegateSamples::sendMouse(viewport, QEvent::MouseMove, center + QPoint(3, 0), Qt::NoButton, Qt::LeftButton);
    QVERIFY(delegate.getPressedIndex().isValid());
    QVERIFY(delegate.getItemState(DelegateSamples::viewOption(view, model.index(0, 0)), model.index(0, 0)).isPressed());
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    QVERIFY(!delegate.getPressedIndex().isValid());

    // A blocking modal window, the loss of the focus, a drag
    QVERIFY(press());
    QEvent blocked(QEvent::WindowBlocked);
    QApplication::sendEvent(&window, &blocked);
    QVERIFY(!delegate.getPressedIndex().isValid());
    QEvent unblocked(QEvent::WindowUnblocked);
    QApplication::sendEvent(&window, &unblocked);

    view->setFocus();
    QVERIFY(press());
    other->setFocus();
    QVERIFY(!delegate.getPressedIndex().isValid());

    // (a view with drag and drop gets the drag over itself)
    view->setDragDropMode(QAbstractItemView::DragDrop);
    QVERIFY(press());
    QMimeData mimeData;
    QDragEnterEvent dragEnter(center, Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &dragEnter);
    QVERIFY(!delegate.getPressedIndex().isValid());

    // A modal dialog opened by a slot of a double click
    view->setFocus();
    bool hasDialogRun = false;
    connect(view, &QAbstractItemView::doubleClicked, &window, [&]()
    {
        QDialog dialog(&window);
        dialog.setModal(true);
        QTimer::singleShot(0, &dialog, &QDialog::reject);
        dialog.exec();
        hasDialogRun = true;
    });
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonDblClick, center, Qt::LeftButton, Qt::LeftButton);
    QVERIFY(hasDialogRun);
    QVERIFY(!delegate.getPressedIndex().isValid());
    QVERIFY(!delegate.getItemState(DelegateSamples::viewOption(view, model.index(0, 0)), model.index(0, 0)).isPressed());

    // Review C12: the hide of another view of the delegate keeps the press of the first one
    QVERIFY(press());
    secondView->hide();
    QVERIFY(delegate.getPressedIndex().isValid());
    secondView->setEnabled(false);
    QVERIFY(delegate.getPressedIndex().isValid());
    view->hide();
    QVERIFY(!delegate.getPressedIndex().isValid());
    view->setItemDelegate(nullptr);
    secondView->setItemDelegate(nullptr);
}

void FluentComponentsTest::treeViewLostRelease()
{
    // Review C2: the same for the presets of PDFTreeView
    QStandardItemModel model;
    for (int i = 0; i < 3; ++i)
    {
        model.appendRow(DelegateSamples::item(QStringLiteral("Chapter %1").arg(i + 1)));
    }

    QWidget window;
    window.resize(320, 260);
    pdf::PDFTreeView* tree = new pdf::PDFTreeView(&window);
    tree->setGeometry(0, 0, 300, 200);
    tree->setHeaderHidden(true);
    tree->setRowStyle(pdf::PDFTreeView::RowStyle::Outline);
    tree->setModel(&model);
    QLineEdit* other = new QLineEdit(&window);
    other->setGeometry(0, 220, 200, 30);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QWidget* viewport = tree->viewport();
    const QRect rect = tree->visualRect(model.index(1, 0));
    const QPoint point(rect.right() - 20, rect.center().y());
    auto press = [&]()
    {
        DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
        return tree->getPressedRow().isValid();
    };

    QVERIFY(press());
    DelegateReviewSamples::sendHover(viewport, QEvent::HoverMove, point);
    QVERIFY(!tree->getPressedRow().isValid());

    QVERIFY(press());
    DelegateSamples::sendMouse(viewport, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
    QVERIFY(tree->getPressedRow().isValid());
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    QVERIFY(!tree->getPressedRow().isValid());

    QVERIFY(press());
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(viewport, &leave);
    QVERIFY(!tree->getPressedRow().isValid());

    QVERIFY(press());
    QEvent blocked(QEvent::WindowBlocked);
    QApplication::sendEvent(&window, &blocked);
    QVERIFY(!tree->getPressedRow().isValid());
    QEvent unblocked(QEvent::WindowUnblocked);
    QApplication::sendEvent(&window, &unblocked);

    tree->setFocus();
    QVERIFY(press());
    other->setFocus();
    QVERIFY(!tree->getPressedRow().isValid());

    tree->setDragDropMode(QAbstractItemView::DragDrop);
    QVERIFY(press());
    QMimeData mimeData;
    QDragEnterEvent dragEnter(point, Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &dragEnter);
    QVERIFY(!tree->getPressedRow().isValid());

    tree->setFocus();
    bool hasDialogRun = false;
    connect(tree, &QAbstractItemView::doubleClicked, &window, [&]()
    {
        QDialog dialog(&window);
        dialog.setModal(true);
        QTimer::singleShot(0, &dialog, &QDialog::reject);
        dialog.exec();
        hasDialogRun = true;
    });
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    DelegateSamples::sendMouse(viewport, QEvent::MouseButtonDblClick, point, Qt::LeftButton, Qt::LeftButton);
    QVERIFY(hasDialogRun);
    QVERIFY(!tree->getPressedRow().isValid());
}

void FluentComponentsTest::treeViewRowFocusAndSelection()
{
    // Review F2: the focus ring of the presets encloses the whole row (the area
    // of the branches and all columns); review F3: the selection stays accent
    // without the focus of the view; review C20: the check box of a pressed row
    // is not pressed
    QStandardItemModel model;
    QStandardItem* chapter = DelegateSamples::item(QStringLiteral("Market Analysis"));
    chapter->appendRow(DelegateSamples::item(QStringLiteral("4.1 Regional revenue")));
    chapter->appendRow(DelegateSamples::item(QStringLiteral("4.2 Customer segments")));
    model.appendRow(chapter);
    model.appendRow(DelegateSamples::item(QStringLiteral("Conclusion")));

    QWidget window;
    window.resize(320, 300);
    pdf::PDFTreeView* tree = new pdf::PDFTreeView(&window);
    tree->setGeometry(0, 0, 300, 200);
    tree->setHeaderHidden(true);
    tree->setRowStyle(pdf::PDFTreeView::RowStyle::Outline);
    tree->setModel(&model);
    tree->expandAll();
    QLineEdit* other = new QLineEdit(&window);
    other->setGeometry(0, 220, 200, 30);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    // The keyboard moves the current row to the nested item
    tree->setFocus(Qt::TabFocusReason);
    tree->setCurrentIndex(model.index(0, 0));
    QTest::keyClick(tree, Qt::Key_Down);
    const QModelIndex nested = model.index(0, 0, model.index(0, 0));
    QCOMPARE(tree->currentIndex(), nested);
    QVERIFY(pdf::PDFWidgetStyle::isFocusRingVisible(tree));

    const QColor focus = token(Color::BorderFocus);
    QImage image = renderWidget(tree, 1.0);
    const QRect viewportRect = tree->viewport()->geometry();
    const QRect rowRect = tree->visualRect(nested);
    const int y = viewportRect.top() + rowRect.center().y();
    // The ring starts at the start of the row (the indentation of the level), not at the cell
    QVERIFY2(isNear(pixel(image, viewportRect.left() + 0, y), focus), qPrintable(pixel(image, viewportRect.left(), y).name()));
    QVERIFY2(isNear(pixel(image, viewportRect.left() + 1, y), focus), qPrintable(pixel(image, viewportRect.left() + 1, y).name()));
    QVERIFY(rowRect.left() >= 18);
    QVERIFY2(!isNear(pixel(image, viewportRect.left() + rowRect.left() + 1, y), focus, 20), "no ring of the cell");
    QVERIFY(isNear(pixel(image, viewportRect.left() + 140, viewportRect.top() + rowRect.top()), focus));

    // Without the focus of the view the selection stays bg/accent-subtle (not bg/muted)
    other->setFocus();
    QVERIFY(!tree->hasFocus());
    tree->selectionModel()->select(nested, QItemSelectionModel::ClearAndSelect);
    image = renderWidget(tree, 1.0);
    const QColor fill = pixel(image, viewportRect.left() + 140, viewportRect.top() + rowRect.top() + 3);
    QVERIFY2(isNear(fill, blend(token(Color::BgAccentSubtle), token(Color::BgSurface)), 3), qPrintable(fill.name()));
    const QColor branchFill = pixel(image, viewportRect.left() + 3, viewportRect.top() + rowRect.top() + 3);
    QVERIFY2(isNear(branchFill, blend(token(Color::BgAccentSubtle), token(Color::BgSurface)), 3), qPrintable(branchFill.name()));
    QVERIFY(!isNear(fill, token(Color::BgMuted), 2));

    // Certificate: the ring across all columns of the current row
    QStandardItemModel table(2, 3);
    for (int row = 0; row < 2; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            QStandardItem* item = DelegateSamples::item(QStringLiteral("Cell %1 %2").arg(row).arg(column));
            if (column == 0)
            {
                item->setCheckable(true);
                item->setCheckState(Qt::Unchecked);
            }
            table.setItem(row, column, item);
        }
    }
    tree->setRowStyle(pdf::PDFTreeView::RowStyle::Certificate);
    tree->setRootIsDecorated(false);
    tree->setModel(&table);
    for (int column = 0; column < 3; ++column)
    {
        tree->setColumnWidth(column, 90);
    }
    tree->setFocus(Qt::TabFocusReason);
    tree->setCurrentIndex(table.index(0, 0));
    QTest::keyClick(tree, Qt::Key_Down);
    QCOMPARE(tree->currentIndex().row(), 1);
    QApplication::processEvents();
    image = renderWidget(tree, 1.0);
    const QRect certificateViewport = tree->viewport()->geometry();
    const QRect cellRect = tree->visualRect(table.index(1, 2));
    QVERIFY(isNear(pixel(image, certificateViewport.left() + cellRect.center().x(), certificateViewport.top() + cellRect.top()), focus));
    // No ring of the current cell (column 0) inside of the row
    const QRect firstCell = tree->visualRect(table.index(1, 0));
    QVERIFY(!isNear(pixel(image, certificateViewport.left() + firstCell.right() - 1, certificateViewport.top() + firstCell.center().y()), focus, 20));

    // Review C20: the press of the row does not press its check box
    QStyleOptionViewItem checkOption = DelegateSamples::viewOption(tree, table.index(0, 0));
    checkOption.features |= QStyleOptionViewItem::HasCheckIndicator;
    const QRect checkRect = tree->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &checkOption, tree);
    QVERIFY(!checkRect.isEmpty());
    other->setFocus();
    tree->selectionModel()->clearSelection();
    tree->setSelectionMode(QAbstractItemView::NoSelection);
    const QPoint checkCenter = certificateViewport.topLeft() + checkRect.center();
    const QImage unpressed = renderWidget(tree, 1.0);
    DelegateSamples::sendMouse(tree->viewport(), QEvent::MouseButtonPress, tree->visualRect(table.index(0, 2)).center(), Qt::LeftButton, Qt::LeftButton);
    QVERIFY(tree->getPressedRow().isValid());
    const QImage pressedImage = renderWidget(tree, 1.0);
    QVERIFY(isNear(pixel(pressedImage, certificateViewport.left() + cellRect.center().x(), certificateViewport.top() + tree->visualRect(table.index(0, 2)).top() + 3),
                   blend(token(Color::BgPressed), token(Color::BgSurface)), 3));
    QVERIFY2(isNear(pixel(pressedImage, checkCenter.x(), checkCenter.y()), pixel(unpressed, checkCenter.x(), checkCenter.y()), 2),
             qPrintable(pixel(pressedImage, checkCenter.x(), checkCenter.y()).name() + QLatin1Char(' ') + pixel(unpressed, checkCenter.x(), checkCenter.y()).name()));
    DelegateSamples::sendMouse(tree->viewport(), QEvent::MouseButtonRelease, tree->visualRect(table.index(0, 2)).center(), Qt::LeftButton, Qt::NoButton);
}

void FluentComponentsTest::shortcutSharedDelegate()
{
    // Review C3: one shortcut delegate for two views; C8: the keys of the start
    // beat the shortcuts of the application; C13: signals after real work only;
    // C21: Tab moves the focus on, the combination is normalized
    QTest::failOnWarning(QRegularExpression(QStringLiteral("does not belong to this view")));

    QStandardItemModel firstModel;
    DelegateSamples::shortcuts(&firstModel);
    QStandardItemModel secondModel;
    DelegateSamples::shortcuts(&secondModel);
    pdf::PDFShortcutDelegate delegate;
    QWidget window;
    window.resize(820, 420);
    pdf::PDFListView* firstView = new pdf::PDFListView(&window);
    firstView->setGeometry(0, 0, 820, 150);
    firstView->setModel(&firstModel);
    firstView->setItemDelegate(&delegate);
    pdf::PDFListView* secondView = new pdf::PDFListView(&window);
    secondView->setGeometry(0, 160, 820, 150);
    secondView->setModel(&secondModel);
    secondView->setItemDelegate(&delegate);
    QLineEdit* other = new QLineEdit(&window);
    other->setGeometry(0, 320, 200, 30);
    QAction renameAction(&window);
    renameAction.setShortcut(QKeySequence(Qt::Key_F2));
    window.addAction(&renameAction);
    QAction returnAction(&window);
    returnAction.setShortcut(QKeySequence(Qt::Key_Return));
    window.addAction(&returnAction);
    QSignalSpy renameTriggered(&renameAction, &QAction::triggered);
    QSignalSpy returnTriggered(&returnAction, &QAction::triggered);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QSignalSpy changed(&delegate, &pdf::PDFShortcutDelegate::shortcutChanged);
    QSignalSpy triggered(&delegate, &pdf::PDFRowDelegate::actionTriggered);
    const QModelIndex first = firstModel.index(0, 0);
    const QModelIndex second = secondModel.index(1, 0);

    // F2 starts the recording, the application action on F2 does not fire (C8)
    firstView->setFocus();
    firstView->setCurrentIndex(first);
    QTest::keyClick(firstView, Qt::Key_F2);
    QVERIFY(delegate.isRecording(firstView, first));
    QVERIFY(!delegate.isRecording(secondView, first));
    QCOMPARE(renameTriggered.count(), 0);

    // The second view starts its recording: the first recorder loses the focus,
    // it is closed in its own view only (no warning of Qt)
    delegate.triggerAction(secondView, second, pdf::PDFShortcutDelegate::RecordAction);
    QVERIFY(delegate.isRecording(secondView, second));
    QApplication::processEvents();
    QVERIFY(!delegate.isRecording(firstView, first));
    QCOMPARE(firstView->indexWidget(first), nullptr);
    QCOMPARE(delegate.getRecordingCount(), 1);
    QCOMPARE(QModelIndex(delegate.getRecordingIndex()), second);

    // The key of the second recorder is written into the second model only
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_K, Qt::ControlModifier);
    QApplication::processEvents();
    QCOMPARE(pdf::PDFShortcutDelegate::getShortcut(second), QKeySequence(Qt::ControlModifier | Qt::Key_K));
    QCOMPARE(pdf::PDFShortcutDelegate::getShortcut(firstModel.index(1, 0)), QKeySequence(Qt::ControlModifier | Qt::Key_S));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(delegate.getRecordingCount(), 0);

    // The first view records again (it is not stuck in the editing state); the
    // Return of the keypad starts it, the application action on Return does not fire
    firstView->setFocus();
    firstView->setCurrentIndex(first);
    QTest::keyClick(firstView, Qt::Key_Enter, Qt::KeypadModifier);
    QVERIFY(delegate.isRecording(firstView, first));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QApplication::processEvents();
    QVERIFY(!delegate.isRecording(firstView, first));
    QTest::keyClick(firstView, Qt::Key_Return);
    QVERIFY(delegate.isRecording(firstView, first));
    QCOMPARE(returnTriggered.count(), 0);

    // Tab cancels and moves the focus to the next widget (C21)
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
    QApplication::processEvents();
    QVERIFY(!delegate.isRecording(firstView, first));
    QCOMPARE(QApplication::focusWidget(), secondView);
    QCOMPARE(pdf::PDFShortcutDelegate::getShortcut(first), QKeySequence(Qt::ControlModifier | Qt::Key_O));

    // Backtab moves it back
    secondView->setCurrentIndex(second);
    QTest::keyClick(secondView, Qt::Key_F2);
    QVERIFY(delegate.isRecording(secondView, second));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Backtab, Qt::ShiftModifier);
    QApplication::processEvents();
    QVERIFY(!delegate.isRecording(secondView, second));
    QCOMPARE(QApplication::focusWidget(), firstView);

    // A model, which refuses the shortcut: the editor closes, no change is reported (C13)
    RefusingShortcutModel refusingModel;
    DelegateSamples::shortcuts(&refusingModel);
    firstView->setModel(&refusingModel);
    firstView->setFocus();
    firstView->setCurrentIndex(refusingModel.index(0, 0));
    const int changes = int(changed.count());
    QTest::keyClick(firstView, Qt::Key_F2);
    QVERIFY(delegate.isRecording(firstView, refusingModel.index(0, 0)));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_J, Qt::ControlModifier);
    QApplication::processEvents();
    QVERIFY(!delegate.isRecording(firstView, refusingModel.index(0, 0)));
    QCOMPARE(int(changed.count()), changes);
    QCOMPARE(pdf::PDFShortcutDelegate::getShortcut(refusingModel.index(0, 0)), QKeySequence(Qt::ControlModifier | Qt::Key_O));

    // A view, which refuses the editor: the recording has not started, no signal (C13)
    InvalidBuddyModel buddyModel;
    DelegateSamples::shortcuts(&buddyModel);
    firstView->setModel(&buddyModel);
    const int triggers = int(triggered.count());
    QTest::ignoreMessage(QtWarningMsg, "edit: editing failed");
    delegate.triggerAction(firstView, buddyModel.index(0, 0), pdf::PDFShortcutDelegate::RecordAction);
    QVERIFY(!delegate.isRecording(firstView, buddyModel.index(0, 0)));
    QCOMPARE(int(triggered.count()), triggers);

    // The normalized combination (as QKeySequenceEdit)
    using Recorder = pdf::PDFShortcutRecorder;
    const QKeyEvent exclamation(QEvent::KeyPress, Qt::Key_Exclam, Qt::ShiftModifier, QStringLiteral("!"));
    QCOMPARE(Recorder::getKeyCombination(&exclamation), QKeyCombination(Qt::Key_Exclam));
    const QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(Recorder::getKeyCombination(&backtab), QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_Tab));
    const QKeyEvent altGr(QEvent::KeyPress, Qt::Key_At, Qt::ControlModifier | Qt::AltModifier, QStringLiteral("@"));
    QCOMPARE(Recorder::getKeyCombination(&altGr), QKeyCombination(Qt::Key_At));
    // Ctrl+Alt (the mapping of Windows for the layouts: shortcutControlAlt). The
    // keys below have the same base key in every layout, so the events give the
    // same combination on every platform and with any layout of the machine.
    const Qt::KeyboardModifiers controlAlt = Qt::ControlModifier | Qt::AltModifier;
    const QKeyEvent controlAltF5(QEvent::KeyPress, Qt::Key_F5, controlAlt, 0x3F, 0x74 /* VK_F5 */, 0, QString());
    QCOMPARE(Recorder::getKeyCombination(&controlAltF5), QKeyCombination(controlAlt, Qt::Key_F5));
    const QKeyEvent controlAltKeypad2(QEvent::KeyPress, Qt::Key_2, controlAlt | Qt::KeypadModifier, 0x50, 0x62 /* VK_NUMPAD2 */, 0, QStringLiteral("2"));
    QCOMPARE(Recorder::getKeyCombination(&controlAltKeypad2), QKeyCombination(controlAlt, Qt::Key_2));
    const QKeyEvent controlAltSpace(QEvent::KeyPress, Qt::Key_Space, controlAlt, 0x39, 0x20 /* VK_SPACE */, 0, QStringLiteral(" "));
    QCOMPARE(Recorder::getKeyCombination(&controlAltSpace), QKeyCombination(controlAlt, Qt::Key_Space));
    const QKeyEvent controlAltBacktab(QEvent::KeyPress, Qt::Key_Backtab, controlAlt | Qt::ShiftModifier, 0x0F, 0x09 /* VK_TAB */, 0, QString());
    QCOMPARE(Recorder::getKeyCombination(&controlAltBacktab), QKeyCombination(controlAlt | Qt::ShiftModifier, Qt::Key_Tab));
    // Ctrl alone is not changed by the virtual key
    const QKeyEvent controlOnly(QEvent::KeyPress, Qt::Key_F, Qt::ControlModifier, 0x21, 'F', 0, QString(QChar(0x06)));
    QCOMPARE(Recorder::getKeyCombination(&controlOnly), QKeyCombination(Qt::ControlModifier, Qt::Key_F));
#ifdef Q_OS_WIN
    // The lookup of the layout: keys with the same character in every layout
    QCOMPARE(int(Recorder::getBaseCharacter(0x74 /* VK_F5 */, 0x3F)), 0);
    QCOMPARE(int(Recorder::getBaseCharacter(0x62 /* VK_NUMPAD2 */, 0x50)), int(u'2'));
    QCOMPARE(int(Recorder::getBaseCharacter(0x20 /* VK_SPACE */, 0x39)), int(u' '));
    QCOMPARE(int(Recorder::getBaseCharacter(0x0D /* VK_RETURN */, 0x1C)), int(u'\r'));
    QCOMPARE(int(Recorder::getBaseCharacter(0, 0)), 0);
    QCOMPARE(int(Recorder::getBaseCharacter(0x1FF, 0)), 0);
    // A key of the layout of the machine: the event is the pure mapping of its base character
    const char16_t baseF = Recorder::getBaseCharacter('F', 0x21);
    QVERIFY(baseF != 0);
    const QKeyEvent controlAltF(QEvent::KeyPress, Qt::Key_BracketLeft, controlAlt, 0x21, 'F', 0, QStringLiteral("["));
    QCOMPARE(Recorder::getKeyCombination(&controlAltF), Recorder::getWindowsKeyCombination(Qt::Key_BracketLeft, controlAlt, QStringLiteral("["), baseF));
    QCOMPARE(Recorder::getKeyCombination(&controlAltF), QKeyCombination(controlAlt, Qt::Key(QChar(baseF).toUpper().unicode())));
#else
    // Other platforms: no lookup, the event is not changed by its virtual key
    QCOMPARE(int(Recorder::getBaseCharacter('F', 0x21)), 0);
    const QKeyEvent controlAltF(QEvent::KeyPress, Qt::Key_BracketLeft, controlAlt, 0x21, 'F', 0, QStringLiteral("["));
    QCOMPARE(Recorder::getKeyCombination(&controlAltF), QKeyCombination(Qt::Key_BracketLeft));
#endif
    const QKeyEvent shiftLetter(QEvent::KeyPress, Qt::Key_A, Qt::ShiftModifier, QStringLiteral("A"));
    QCOMPARE(Recorder::getKeyCombination(&shiftLetter), QKeyCombination(Qt::ShiftModifier, Qt::Key_A));
    const QKeyEvent controlShift(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier, QString(QChar(0x0B)));
    QCOMPARE(Recorder::getKeyCombination(&controlShift), QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_K));
    QCOMPARE(Recorder::getKeyCombination(nullptr), QKeyCombination());

    firstView->setItemDelegate(nullptr);
    secondView->setItemDelegate(nullptr);
}

void FluentComponentsTest::shortcutControlAlt_data()
{
    // Ctrl+Alt on Windows (review C1, C2 of the lifetime test): the key of a
    // layout, the Qt key and the text of the event as Qt gives them for the key
    // press (qtKey[modifiers] of QWindowsKeyMapper::translateKeyEventInternal,
    // 0 when the level has no character), the recorded combination, which
    // must be a candidate of the shortcut map of Qt for the same key press, a
    // combination, which must not be recorded (it is no candidate: the rule of
    // the virtual key of A-Z / 0-9 before the review), and another candidate of
    // the same key press (the AltGr symbol alone, the Latin key of a Cyrillic
    // layout), which fires on the same key press too.
    using Key = WindowsShortcutCandidates::LayoutKey;
    QTest::addColumn<Key>("key");
    QTest::addColumn<int>("eventKey");
    QTest::addColumn<int>("modifiers");
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("recorded");
    QTest::addColumn<int>("rejected");
    QTest::addColumn<int>("alsoFires");

    const int controlAlt = int(Qt::ControlModifier | Qt::AltModifier);
    const int controlAltShift = controlAlt | int(Qt::ShiftModifier);
    const int keypad = int(Qt::KeypadModifier);
    auto row = [](const char* tag, Key key, int eventKey, int modifiers, const QString& text, int recorded, int rejected = 0, int alsoFires = 0)
    {
        QTest::newRow(tag) << key << eventKey << modifiers << text << recorded << rejected << alsoFires;
    };
    auto character = [](char16_t value) { return QString(QChar(value)); };

    // Czech QWERTZ (00000405): the digit row "+ěščřžýáíé" with the digits on Shift
    row("cs F, AltGr [", Key{ 'F', u'f', u'F', u'[' }, Qt::Key_BracketLeft, controlAlt, QStringLiteral("["), controlAlt | Qt::Key_F, 0, Qt::Key_BracketLeft);
    row("cs V, AltGr @", Key{ 'V', u'v', u'V', u'@' }, Qt::Key_At, controlAlt, QStringLiteral("@"), controlAlt | Qt::Key_V, 0, Qt::Key_At);
    row("cs E, AltGr euro", Key{ 'E', u'e', u'E', u'€' }, 0x20AC, controlAlt, character(u'€'), controlAlt | Qt::Key_E, 0, 0x20AC);
    row("cs 2 = e caron, AltGr dead caron", Key{ '2', u'ě', u'2', u'ˇ' }, 0x02C7, controlAlt, QString(), controlAlt | 0x011A, controlAlt | Qt::Key_2);
    row("cs Shift+2 = E caron", Key{ '2', u'ě', u'2', u'ˇ' }, 0, controlAltShift, QString(), controlAltShift | 0x011A, controlAltShift | Qt::Key_2);
    row("cs 0 = e acute", Key{ '0', u'é', u'0', u'˝' }, 0x02DD, controlAlt, QString(), controlAlt | 0x00C9, controlAlt | Qt::Key_0);
    row("cs OEM_4 = u acute, AltGr division", Key{ 0xDB, u'ú', u'/', u'÷' }, 0x00F7, controlAlt, character(u'÷'), controlAlt | 0x00DA, 0, 0x00F7);
    row("cs OEM_2 = dead acute", Key{ 0xBF, u'´', u'ˇ' }, 0, controlAlt, QString(), controlAlt | 0x00B4);
    row("cs Ctrl+2 without Alt", Key{ '2', u'ě', u'2', u'ˇ' }, 0x011A, int(Qt::ControlModifier), QString(), int(Qt::ControlModifier) | 0x011A);

    // German QWERTZ (00000407)
    row("de Q, AltGr @", Key{ 'Q', u'q', u'Q', u'@' }, Qt::Key_At, controlAlt, QStringLiteral("@"), controlAlt | Qt::Key_Q, 0, Qt::Key_At);
    row("de 8, AltGr [", Key{ '8', u'8', u'(', u'[' }, Qt::Key_BracketLeft, controlAlt, QStringLiteral("["), controlAlt | Qt::Key_8, 0, Qt::Key_BracketLeft);
    row("de OEM_4 = sharp s, AltGr backslash", Key{ 0xDB, u'ß', u'?', u'\\' }, Qt::Key_Backslash, controlAlt, QStringLiteral("\\"), controlAlt | 0x00DF, 0, Qt::Key_Backslash);
    row("de OEM_5 = dead circumflex", Key{ 0xDC, u'^', u'°' }, 0, controlAlt, QString(), controlAlt | Qt::Key_AsciiCircum);
    row("de Shift+M, AltGr micro", Key{ 'M', u'm', u'M', u'µ' }, 0, controlAltShift, QString(), controlAltShift | Qt::Key_M);

    // French AZERTY (0000040C): the digit row "&é\"'(-è_çà" with the digits on Shift
    row("fr 1 = ampersand", Key{ '1', u'&', u'1' }, 0, controlAlt, QString(), controlAlt | Qt::Key_Ampersand, controlAlt | Qt::Key_1);
    row("fr 2 = e acute, AltGr dead tilde", Key{ '2', u'é', u'2', u'~' }, Qt::Key_AsciiTilde, controlAlt, QString(), controlAlt | 0x00C9, controlAlt | Qt::Key_2, Qt::Key_AsciiTilde);
    row("fr Shift+2", Key{ '2', u'é', u'2', u'~' }, 0, controlAltShift, QString(), controlAltShift | 0x00C9, controlAltShift | Qt::Key_2);
    row("fr 0 = a grave, AltGr @", Key{ '0', u'à', u'0', u'@' }, Qt::Key_At, controlAlt, QStringLiteral("@"), controlAlt | 0x00C0, controlAlt | Qt::Key_0, Qt::Key_At);
    row("fr A", Key{ 'A', u'a', u'A' }, 0, controlAlt, QString(), controlAlt | Qt::Key_A);

    // Russian (00000419): no AltGr level, the Latin key is the fall back candidate
    row("ru F = cyrillic a", Key{ 'F', u'а', u'А' }, 0, controlAlt, QString(), controlAlt | 0x0410, 0, controlAlt | Qt::Key_F);
    row("ru OEM_3 = yo", Key{ 0xC0, u'ё', u'Ё' }, 0, controlAlt, QString(), controlAlt | 0x0401);
    row("ru 2", Key{ '2', u'2', u'"' }, 0, controlAlt, QString(), controlAlt | Qt::Key_2);

    // US: Ctrl+Alt+Shift+2 keeps the digit, "@" with Ctrl+Alt is another candidate
    row("us Shift+2", Key{ '2', u'2', u'@' }, Qt::Key_At, controlAltShift, QString(), controlAltShift | Qt::Key_2, 0, controlAlt | Qt::Key_At);

    // Keys without a character: the Qt key of the event (KeyTbl of Qt)
    row("keypad 2", Key{ 0x62, u'2', u'2', 0, 0, Qt::Key_2 }, Qt::Key_2, controlAlt | keypad, QStringLiteral("2"), controlAlt | Qt::Key_2);
    row("keypad End without NumLock", Key{ 0x23, 0, 0, 0, 0, Qt::Key_End }, Qt::Key_End, controlAlt | keypad, QString(), controlAlt | Qt::Key_End);
    row("keypad decimal comma", Key{ 0x6E, u',', u',', 0, 0, Qt::Key_Period }, Qt::Key_Comma, controlAlt | keypad, QStringLiteral(","), controlAlt | Qt::Key_Comma);
    row("keypad Enter", Key{ 0x0D, u'\r', u'\r', 0, 0, Qt::Key_Return, true }, Qt::Key_Enter, controlAlt | keypad, QStringLiteral("\r"), controlAlt | Qt::Key_Enter);
    row("F5", Key{ 0x74, 0, 0, 0, 0, Qt::Key_F5 }, Qt::Key_F5, controlAlt, QString(), controlAlt | Qt::Key_F5);
    row("Shift+F5", Key{ 0x74, 0, 0, 0, 0, Qt::Key_F5 }, Qt::Key_F5, controlAltShift, QString(), controlAltShift | Qt::Key_F5);
    row("Space", Key{ 0x20, u' ', u' ', 0, 0, Qt::Key_Space }, Qt::Key_Space, controlAlt, QStringLiteral(" "), controlAlt | Qt::Key_Space);
    row("Escape", Key{ 0x1B, u'\x1B', u'\x1B', 0, 0, Qt::Key_Escape }, Qt::Key_Escape, controlAlt, QString(), controlAlt | Qt::Key_Escape);
    row("Shift+Tab", Key{ 0x09, u'\t', u'\t', 0, 0, Qt::Key_Tab }, Qt::Key_Backtab, controlAltShift, QString(), controlAltShift | Qt::Key_Tab, 0, controlAlt | Qt::Key_Backtab);
}

void FluentComponentsTest::shortcutControlAlt()
{
    using Candidates = WindowsShortcutCandidates;
    using Recorder = pdf::PDFShortcutRecorder;
    QFETCH(Candidates::LayoutKey, key);
    QFETCH(int, eventKey);
    QFETCH(int, modifiers);
    QFETCH(QString, text);
    QFETCH(int, recorded);
    QFETCH(int, rejected);
    QFETCH(int, alsoFires);

    const Qt::KeyboardModifiers keyMods = Qt::KeyboardModifiers::fromInt(modifiers);
    const QString candidates = Candidates::describe(key, keyMods);

    // The pure mapping of Windows with the base character of the layout
    const QKeyCombination combination = Recorder::getWindowsKeyCombination(eventKey, keyMods, text, key.base);
    QVERIFY2(combination == QKeyCombination::fromCombined(recorded),
             qPrintable(QStringLiteral("recorded %1, expected %2").arg(QKeySequence(combination).toString(QKeySequence::PortableText),
                                                                         QKeySequence(QKeyCombination::fromCombined(recorded)).toString(QKeySequence::PortableText))));

    // The shortcut map of Qt matches the recorded combination on the same key press
    QVERIFY2(Candidates::isMatched(combination, key, keyMods), qPrintable(candidates));

    // The rule before the review would record a combination, which never fires (C1)
    if (rejected)
    {
        QVERIFY2(!Candidates::isMatched(QKeyCombination::fromCombined(rejected), key, keyMods), qPrintable(candidates));
    }

    // Another shortcut, which fires on the same key press (the AltGr symbol alone, C2)
    if (alsoFires)
    {
        QVERIFY2(Candidates::isMatched(QKeyCombination::fromCombined(alsoFires), key, keyMods), qPrintable(candidates));
    }

    // Without the base character (a key without a character, an event of the
    // test) the mapping is the one of the other platforms
    QCOMPARE(Recorder::getWindowsKeyCombination(eventKey, keyMods, text, 0), Recorder::getKeyCombination(eventKey, keyMods, text));
}

void FluentComponentsTest::shortcutControlAltActions()
{
    // Review C3 of the lifetime test: the shortcuts of the actions of the
    // delegates match the keys as the recorder records them
    using Recorder = pdf::PDFShortcutRecorder;
    const Qt::KeyboardModifiers controlAlt = Qt::ControlModifier | Qt::AltModifier;

    QStandardItemModel model;
    DelegateSamples::fonts(&model);
    ControlAltFontDelegate delegate(QKeySequence(controlAlt | Qt::Key_F));
    QWidget window;
    window.resize(720, 200);
    pdf::PDFListView* view = new pdf::PDFListView(&window);
    view->setGeometry(0, 0, 720, 200);
    view->setModel(&model);
    view->setItemDelegate(&delegate);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QSignalSpy triggered(&delegate, &pdf::PDFRowDelegate::actionTriggered);
    const QModelIndex index = model.index(1, 0);
    view->setFocus(Qt::TabFocusReason);
    view->setCurrentIndex(index);

    // An event without a virtual key: Ctrl+Alt+F on every platform
    const QKeyEvent synthesized(QEvent::KeyPress, Qt::Key_F, controlAlt);
    QVERIFY(delegate.isKeyHandled(view, index, &synthesized));

    // Ctrl+Alt+F of the Czech layout of Windows: the Qt key "[" of the AltGr
    // level with the virtual key F. On Windows the base key of the layout of
    // the machine (F on every Latin layout) is the shortcut of the action.
    const QKeyEvent czech(QEvent::KeyPress, Qt::Key_BracketLeft, controlAlt, 0x21, 'F', 0, QStringLiteral("["));
    const QList<QKeyCombination> combinations = Recorder::getMatchingKeyCombinations(&czech);
    QVERIFY(!combinations.isEmpty());
    QCOMPARE(combinations.first(), Recorder::getKeyCombination(&czech));
    QVERIFY(combinations.contains(QKeyCombination(controlAlt, Qt::Key_BracketLeft)));
    QVERIFY(combinations.contains(QKeyCombination(Qt::Key_BracketLeft)));
#ifdef Q_OS_WIN
    if (QChar(Recorder::getBaseCharacter('F', 0x21)).toUpper() == QLatin1Char('F'))
    {
        QVERIFY(delegate.isKeyHandled(view, index, &czech));
        QKeyEvent press(QEvent::KeyPress, Qt::Key_BracketLeft, controlAlt, 0x21, 'F', 0, QStringLiteral("["));
        QVERIFY(delegate.handleKey(view, index, &press));
        QCOMPARE(triggered.count(), 1);
        triggered.clear();
    }
    else
    {
        qWarning("The layout of the machine has no Latin F, the action key of Ctrl+Alt+F is not checked");
    }
#else
    QVERIFY(!delegate.isKeyHandled(view, index, &czech));
#endif

    // An action with the AltGr symbol alone fires on the same key press (as a shortcut of Qt)
    ControlAltFontDelegate symbolDelegate{ QKeySequence(Qt::Key_BracketLeft) };
    QVERIFY(symbolDelegate.isKeyHandled(view, index, &czech));

    // No duplicates, the keypad modifier is dropped, nothing for nullptr
    const QKeyEvent keypadEnter(QEvent::KeyPress, Qt::Key_Enter, Qt::KeypadModifier);
    const QList<QKeyCombination> enterCombinations = Recorder::getMatchingKeyCombinations(&keypadEnter);
    QCOMPARE(enterCombinations.size(), 1);
    QCOMPARE(enterCombinations.first(), QKeyCombination(Qt::Key_Enter));
    QVERIFY(Recorder::getMatchingKeyCombinations(nullptr).isEmpty());

    // Through the view: the key wins over the shortcut of the application and triggers the action
    QAction applicationAction(&window);
    applicationAction.setShortcut(QKeySequence(controlAlt | Qt::Key_F));
    window.addAction(&applicationAction);
    QSignalSpy applicationTriggered(&applicationAction, &QAction::triggered);
    QTest::keyClick(view, Qt::Key_F, controlAlt);
    QCOMPARE(triggered.count(), 1);
    QCOMPARE(triggered.at(0).at(0).value<QModelIndex>(), index);
    QCOMPARE(applicationTriggered.count(), 0);

    // Another key is not the action
    QTest::keyClick(view, Qt::Key_G, controlAlt);
    QCOMPARE(triggered.count(), 1);

    view->setItemDelegate(nullptr);
}

void FluentComponentsTest::delegateReviewDetails()
{
    using Roles = pdf::PDFComponentRoles;

    // Review C4: the size never reads the picture of the item (a model of the
    // thumbnails renders the page in Qt::DecorationRole)
    PictureCountingModel pictures;
    QImage page(216, 296, QImage::Format_ARGB32);
    page.fill(QColor(200, 30, 30));
    for (int i = 0; i < 50; ++i)
    {
        QStandardItem* item = DelegateSamples::item(QString::number(i + 1));
        item->setData(page, Qt::DecorationRole);
        item->setData(QStringLiteral("Project_Report.pdf"), Roles::SubtitleRole);
        pictures.appendRow(item);
    }
    pdf::PDFThumbnailDelegate thumbnails;
    pdf::PDFRecentItemDelegate recent;
    QStyleOptionViewItem option;
    option.state = QStyle::State_Enabled;
    pictures.pictureReads = 0;
    for (int i = 0; i < 50; ++i)
    {
        QCOMPARE(thumbnails.sizeHint(option, pictures.index(i, 0)), QSize(124, 183));
        QCOMPARE(recent.sizeHint(option, pictures.index(i, 0)), QSize(802, 56));
    }
    QCOMPARE(pictures.pictureReads, 0);
    QImage canvas = render([&](QPainter& painter)
    {
        QStyleOptionViewItem paintOption = option;
        paintOption.rect = QRect(0, 0, 124, 183);
        thumbnails.paint(&painter, paintOption, pictures.index(0, 0));
    }, QSize(124, 183));
    QVERIFY(pictures.pictureReads > 0);
    QVERIFY(isNear(pixel(canvas, 62, 80), QColor(200, 30, 30), 2));

    // The size of the model (Qt::SizeHintRole) wins
    pictures.setData(pictures.index(1, 0), QSize(90, 120), Qt::SizeHintRole);
    QCOMPARE(thumbnails.sizeHint(option, pictures.index(1, 0)), QSize(90, 120));

    // A QImage is converted once: the same pixmap (cache key) at each repaint
    const QPixmap firstPixmap = pdf::PDFRowDelegate::getPixmap(pictures.index(0, 0));
    const QPixmap secondPixmap = pdf::PDFRowDelegate::getPixmap(pictures.index(0, 0));
    QVERIFY(!firstPixmap.isNull());
    QCOMPARE(firstPixmap.cacheKey(), secondPixmap.cacheKey());

    // Review C25: a QIcon of a file model, a date and a number formatted by the locale
    QPixmap iconPixmap(40, 40);
    iconPixmap.fill(Qt::blue);
    pictures.setData(pictures.index(2, 0), QIcon(iconPixmap), Qt::DecorationRole);
    QCOMPARE(pdf::PDFRowDelegate::getPixmap(pictures.index(2, 0)).size(), QSize(40, 40));
    const QDate date(2026, 10, 17);
    pictures.setData(pictures.index(3, 0), date, Roles::DateRole);
    QCOMPARE(pdf::PDFRowDelegate::getString(pictures.index(3, 0), Roles::DateRole), QLocale().toString(date, QLocale::ShortFormat));
    pictures.setData(pictures.index(3, 0), 2.5, Roles::TextRole);
    QCOMPARE(pdf::PDFRowDelegate::getString(pictures.index(3, 0), Roles::TextRole), QLocale().toString(2.5, 'g', 6));
    pictures.setData(pictures.index(3, 0), QVariant::fromValue(QSize(1, 1)), Roles::TagRole);
    QCOMPARE(pdf::PDFRowDelegate::getString(pictures.index(3, 0), Roles::TagRole, QStringLiteral("default")), QStringLiteral("default"));

    // Review C5: the heights of the snippets are remembered by their texts and
    // the width (the same result as the painter, the row after the elision)
    pdf::PDFSearchResultDelegate search;
    QStandardItemModel results;
    DelegateSamples::searchResults(&results);
    QStyleOptionViewItem widthOption;
    widthOption.rect = QRect(0, 0, 276, 1);
    for (int repeat = 0; repeat < 3; ++repeat)
    {
        QCOMPARE(search.sizeHint(widthOption, results.index(0, 0)).height(), 62);
    }
    results.setData(results.index(0, 0), QStringLiteral("…revenue…"), Roles::TextRole);
    QCOMPARE(search.sizeHint(widthOption, results.index(0, 0)).height(), 47);
    Painter::SearchResultRow longRow;
    longRow.page = QStringLiteral("Page 1");
    longRow.snippet = QString(4000, QLatin1Char('w'));
    QCOMPARE(Painter::getSearchResultRowHeight(longRow, 276), 62);
    const QImage firstPaint = render([&](QPainter& painter) { Painter::paintSearchResultRow(&painter, QRectF(0, 0, 276, 62), longRow, state(QStyle::State_None)); }, QSize(276, 62));
    const QImage secondPaint = render([&](QPainter& painter) { Painter::paintSearchResultRow(&painter, QRectF(0, 0, 276, 62), longRow, state(QStyle::State_None)); }, QSize(276, 62));
    QCOMPARE(describeDifference(firstPaint, secondPaint, 0), QString());

    // Review C6: an index of another model (a source index of a proxy) is harmless
    QStandardItemModel attachments;
    DelegateSamples::attachments(&attachments);
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&attachments);
    pdf::PDFAttachmentDelegate attachmentDelegate;
    QListView proxyView;
    proxyView.setModel(&proxy);
    proxyView.setItemDelegate(&attachmentDelegate);
    proxyView.resize(320, 200);
    proxyView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&proxyView));
    QSignalSpy attachmentTriggered(&attachmentDelegate, &pdf::PDFRowDelegate::actionTriggered);
    attachmentDelegate.triggerAction(&proxyView, attachments.index(0, 0), pdf::PDFAttachmentDelegate::SaveAction);
    QCOMPARE(attachmentTriggered.count(), 0);
    QCOMPARE(attachmentDelegate.getActionRect(&proxyView, attachments.index(0, 0), pdf::PDFAttachmentDelegate::SaveAction), QRect());
    QMenu foreignMenu;
    QCOMPARE(attachmentDelegate.addActionsToMenu(&foreignMenu, &proxyView, attachments.index(0, 0)), 0);
    QCOMPARE(attachmentDelegate.createContextMenu(&proxyView, attachments.index(0, 0), nullptr), nullptr);
    attachmentDelegate.triggerAction(&proxyView, proxy.index(0, 0), pdf::PDFAttachmentDelegate::SaveAction);
    QCOMPARE(attachmentTriggered.count(), 1);
    QCOMPARE(attachmentDelegate.getLastActionView(), &proxyView);

    // Review C11: the tooltip of an action hides over the empty area of the view
    proxy.setFilterFixedString(QStringLiteral("2026"));
    QApplication::processEvents();
    QCOMPARE(proxy.rowCount(), 1);
    const QRect saveRect = attachmentDelegate.getActionRect(&proxyView, proxy.index(0, 0), pdf::PDFAttachmentDelegate::SaveAction);
    QHelpEvent actionTip(QEvent::ToolTip, saveRect.center(), proxyView.viewport()->mapToGlobal(saveRect.center()));
    QApplication::sendEvent(proxyView.viewport(), &actionTip);
    QVERIFY(pdf::PDFToolTip::isVisible());
    const QPoint empty(saveRect.center().x(), proxyView.viewport()->height() - 5);
    QVERIFY(!proxyView.indexAt(empty).isValid());
    QHelpEvent emptyTip(QEvent::ToolTip, empty, proxyView.viewport()->mapToGlobal(empty));
    QApplication::sendEvent(proxyView.viewport(), &emptyTip);
    QVERIFY(!pdf::PDFToolTip::isVisible());
    proxyView.setItemDelegate(nullptr);

    // Review C14: an action of a menu, whose view has been destroyed, does nothing
    QListView* temporaryView = new QListView();
    temporaryView->setModel(&attachments);
    QMenu* staleMenu = attachmentDelegate.createContextMenu(temporaryView, attachments.index(0, 0), nullptr);
    QVERIFY(staleMenu);
    delete temporaryView;
    staleMenu->actions().at(0)->trigger();
    QCOMPARE(attachmentTriggered.count(), 1);
    delete staleMenu;

    // Review C15: a slot of dataChanged deletes the delegate during the pin
    QStandardItemModel recentModel;
    DelegateSamples::recentItems(&recentModel);
    QListView recentView;
    recentView.setModel(&recentModel);
    QPointer<pdf::PDFRecentItemDelegate> deletedDelegate = new pdf::PDFRecentItemDelegate();
    recentView.setItemDelegate(deletedDelegate.data());
    QMetaObject::Connection deleting = connect(&recentModel, &QAbstractItemModel::dataChanged, &recentView, [&]()
    {
        recentView.setItemDelegate(nullptr);
        delete deletedDelegate.data();
    });
    deletedDelegate->triggerAction(&recentView, recentModel.index(0, 0), pdf::PDFRecentItemDelegate::PinAction);
    QVERIFY(!deletedDelegate);
    QCOMPARE(recentModel.data(recentModel.index(0, 0), Roles::PinnedRole).toBool(), true);
    disconnect(deleting);

    // Review C16: no generic editor over a component row
    QStandardItemModel editable;
    DelegateSamples::bookmarks(&editable);
    editable.item(0)->setEditable(true);
    pdf::PDFBookmarkDelegate bookmarks;
    QCOMPARE(bookmarks.createEditor(nullptr, QStyleOptionViewItem(), editable.index(0, 0)), nullptr);
    QListView editableView;
    editableView.setModel(&editable);
    editableView.setItemDelegate(&bookmarks);
    editableView.resize(320, 200);
    editableView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&editableView));
    editableView.edit(editable.index(0, 0));
    QCOMPARE(editableView.indexWidget(editable.index(0, 0)), nullptr);
    editableView.setItemDelegate(nullptr);

    // Review C17: a view class of an application keeps its own context menu
    QStandardItemModel fonts;
    DelegateSamples::fonts(&fonts);
    pdf::PDFFontDelegate fontDelegate;
    ApplicationListView applicationView;
    applicationView.setModel(&fonts);
    applicationView.setItemDelegate(&fontDelegate);
    applicationView.resize(720, 200);
    applicationView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&applicationView));
    QVERIFY(fontDelegate.isContextMenuEnabled());
    const QPoint fontPoint = applicationView.visualRect(fonts.index(0, 0)).center();
    QContextMenuEvent applicationMenu(QContextMenuEvent::Mouse, fontPoint, applicationView.viewport()->mapToGlobal(fontPoint));
    QApplication::sendEvent(applicationView.viewport(), &applicationMenu);
    QCOMPARE(QApplication::activePopupWidget(), nullptr);
    QCOMPARE(applicationView.contextMenus, 1);
    fontDelegate.setContextMenuEnabled(true);
    QContextMenuEvent forcedMenu(QContextMenuEvent::Mouse, fontPoint, applicationView.viewport()->mapToGlobal(fontPoint));
    QApplication::sendEvent(applicationView.viewport(), &forcedMenu);
    QMenu* shownMenu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    QVERIFY(shownMenu);
    shownMenu->close();
    QApplication::processEvents();
    applicationView.setItemDelegate(nullptr);

    // Review C30: the menu of the keyboard of an item scrolled out of the view lies at the viewport
    QStandardItemModel many;
    for (int i = 0; i < 40; ++i)
    {
        QStandardItem* item = DelegateSamples::item(QStringLiteral("Inter-%1").arg(i));
        many.appendRow(item);
    }
    pdf::PDFListView scrolledView;
    scrolledView.setModel(&many);
    scrolledView.setItemDelegate(&fontDelegate);
    scrolledView.resize(720, 200);
    scrolledView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&scrolledView));
    scrolledView.setCurrentIndex(many.index(0, 0));
    scrolledView.verticalScrollBar()->setValue(scrolledView.verticalScrollBar()->maximum());
    QApplication::processEvents();
    QVERIFY(!scrolledView.viewport()->rect().intersects(scrolledView.visualRect(many.index(0, 0))));
    QContextMenuEvent keyboardMenu(QContextMenuEvent::Keyboard, QPoint(), scrolledView.mapToGlobal(QPoint()));
    QApplication::sendEvent(&scrolledView, &keyboardMenu);
    shownMenu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    QVERIFY(shownMenu);
    const QRect viewportGlobal(scrolledView.viewport()->mapToGlobal(QPoint(0, 0)), scrolledView.viewport()->size());
    QVERIFY2(shownMenu->geometry().top() <= viewportGlobal.bottom() + 24 && shownMenu->geometry().top() >= viewportGlobal.top() - 24,
             qPrintable(QStringLiteral("%1 %2 %3").arg(shownMenu->geometry().top()).arg(viewportGlobal.top()).arg(viewportGlobal.bottom())));
    shownMenu->close();
    QApplication::processEvents();
    scrolledView.setItemDelegate(nullptr);

    // Review C10: the action under the pointer after a scroll (the pointer has not moved)
    QStandardItemModel manyAttachments;
    for (int i = 0; i < 30; ++i)
    {
        manyAttachments.appendRow(DelegateSamples::item(QStringLiteral("file-%1.pdf").arg(i)));
    }
    pdf::PDFAttachmentDelegate hoverDelegate;
    pdf::PDFListView hoverView;
    hoverView.setModel(&manyAttachments);
    hoverView.setItemDelegate(&hoverDelegate);
    hoverView.resize(320, 200);
    hoverView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&hoverView));
    hoverView.viewport()->repaint();
    const QRect firstSave = hoverDelegate.getActionRect(&hoverView, manyAttachments.index(0, 0), pdf::PDFAttachmentDelegate::SaveAction);
    DelegateReviewSamples::sendHover(hoverView.viewport(), QEvent::HoverMove, firstSave.center());
    QVERIFY(hoverDelegate.getActionFlags(DelegateSamples::viewOption(&hoverView, manyAttachments.index(0, 0)), manyAttachments.index(0, 0), pdf::PDFAttachmentDelegate::SaveAction).testFlag(QStyle::State_MouseOver));
    hoverView.verticalScrollBar()->setValue(hoverView.verticalScrollBar()->maximum());
    hoverView.viewport()->repaint();
    const QModelIndex underPointer = hoverView.indexAt(firstSave.center());
    QVERIFY(underPointer.isValid());
    QVERIFY(underPointer.row() != 0);
    QVERIFY(!hoverDelegate.getActionFlags(DelegateSamples::viewOption(&hoverView, manyAttachments.index(0, 0)), manyAttachments.index(0, 0), pdf::PDFAttachmentDelegate::SaveAction).testFlag(QStyle::State_MouseOver));
    if (hoverDelegate.getActionRect(&hoverView, underPointer, pdf::PDFAttachmentDelegate::SaveAction).contains(firstSave.center()))
    {
        QVERIFY(hoverDelegate.getActionFlags(DelegateSamples::viewOption(&hoverView, underPointer), underPointer, pdf::PDFAttachmentDelegate::SaveAction).testFlag(QStyle::State_MouseOver));
    }
    hoverView.setItemDelegate(nullptr);

    // Review C9: the width of a nested item below the root index of a tree, the
    // column of the tree, the heights asked again after a resize of the column
    QStandardItemModel tree;
    QStandardItem* chapter = DelegateSamples::item(QStringLiteral("Chapter"));
    QStandardItem* section = DelegateSamples::item(QStringLiteral("Section"));
    QStandardItem* paragraph = DelegateSamples::item(QStringLiteral("Paragraph"));
    section->appendRow({ paragraph, DelegateSamples::item(QStringLiteral("Second column")) });
    chapter->appendRow({ section, DelegateSamples::item(QStringLiteral("Second column")) });
    tree.appendRow({ chapter, DelegateSamples::item(QStringLiteral("Second column")) });
    QTreeView treeView;
    treeView.setModel(&tree);
    treeView.setRootIndex(chapter->index());
    treeView.header()->setStretchLastSection(false);
    treeView.header()->resizeSection(0, 300);
    treeView.header()->resizeSection(1, 200);
    QStyleOptionViewItem treeOption;
    treeOption.widget = &treeView;
    QCOMPARE(WidthProbeDelegate::width(treeOption, paragraph->index()), 300 - 2 * treeView.indentation());
    QCOMPARE(WidthProbeDelegate::width(treeOption, section->index()), 300 - 1 * treeView.indentation());
    QCOMPARE(WidthProbeDelegate::width(treeOption, tree.index(0, 1, section->index())), 200);
    treeView.setTreePosition(1);
    QCOMPARE(WidthProbeDelegate::width(treeOption, paragraph->index()), 300);
    QCOMPARE(WidthProbeDelegate::width(treeOption, tree.index(0, 1, section->index())), 200 - 2 * treeView.indentation());

    pdf::PDFNoteDelegate notes;
    treeView.setItemDelegate(&notes);
    treeView.resize(520, 300);
    treeView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&treeView));
    treeView.viewport()->repaint();
    QSignalSpy sizeChanged(&notes, &QAbstractItemDelegate::sizeHintChanged);
    treeView.header()->resizeSection(0, 260);
    QVERIFY(sizeChanged.count() > 0);
    const int afterResize = int(sizeChanged.count());
    QEvent languageChange(QEvent::LanguageChange);
    QApplication::sendEvent(&treeView, &languageChange);
    QVERIFY(int(sizeChanged.count()) > afterResize);
    treeView.setItemDelegate(nullptr);

    // Review F1: the progress bar fills from the leading edge in every caller
    QImage bar = render([](QPainter& painter) { Painter::paintProgressBar(&painter, QRectF(0, 0, 100, 4), 0.5, Painter::State(QStyle::State_Enabled, Qt::RightToLeft)); }, QSize(100, 4));
    QVERIFY(isNear(pixel(bar, 75, 2), token(Color::BgAccent), 2));
    QVERIFY(isNear(pixel(bar, 25, 2), token(Color::BorderDefault), 2));
    bar = render([](QPainter& painter) { Painter::paintProgressBar(&painter, QRectF(0, 0, 100, 4), 0.5); }, QSize(100, 4));
    QVERIFY(isNear(pixel(bar, 25, 2), token(Color::BgAccent), 2));
    QStandardItemModel recentItems;
    DelegateSamples::recentItems(&recentItems);
    const QImage rightToLeft = renderDelegate(&recent, recentItems.index(0, 0), QStyle::State_Enabled, QSize(802, 56), 1.0, Qt::RightToLeft);
    QVERIFY2(isNear(pixel(rightToLeft, 802 - 1 - 490, 36), token(Color::BgAccent), 2), qPrintable(pixel(rightToLeft, 802 - 1 - 490, 36).name()));
    QVERIFY2(isNear(pixel(rightToLeft, 802 - 1 - 600, 36), token(Color::BorderDefault), 2), qPrintable(pixel(rightToLeft, 802 - 1 - 600, 36).name()));

    // Review C18 / C22: a text of data keeps the direction of the whole text,
    // also when the elision cuts its first strong character off
    const QFont font = pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Body);
    const QString text = QStringLiteral("2026-10-17 ") + QString(30, QChar(0x05D3));
    const QString elided = Painter::elidedText(text, font, 70.0);
    QCOMPARE(Painter::getDataTextDirection(text), Qt::RightToLeft);
    QCOMPARE(Painter::getDataTextDirection(elided), Qt::LeftToRight);
    const QRectF textRect(0, 0, 70, 20);
    const QImage elidedData = render([&](QPainter& painter) { Painter::drawElidedText(&painter, state(QStyle::State_None), textRect, text, font, Color::TextPrimary, Painter::TextKind::Data); }, QSize(70, 20));
    const QImage wholeDirection = render([&](QPainter& painter)
    {
        Painter::drawDataText(&painter, textRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::AlignAbsolute | Qt::TextSingleLine, elided, font, token(Color::TextPrimary), Qt::RightToLeft);
    }, QSize(70, 20));
    const QImage ownDirection = render([&](QPainter& painter)
    {
        Painter::drawDataText(&painter, textRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::AlignAbsolute | Qt::TextSingleLine, elided, font, token(Color::TextPrimary), Qt::LeftToRight);
    }, QSize(70, 20));
    QCOMPARE(describeDifference(elidedData, wholeDirection, 0), QString());
    QVERIFY(!describeDifference(elidedData, ownDirection, 0).isEmpty());
    Painter::drawElidedText(nullptr, state(QStyle::State_None), textRect, text, font, Color::TextPrimary, Painter::TextKind::FileName);

    // Review C7: in a tree the area of the branches of an item of a delegate of
    // the components is not filled by the style (one shape of the selection)
    QStandardItemModel nestedModel;
    DelegateSamples::bookmarks(&nestedModel);
    nestedModel.item(0)->appendRow(DelegateSamples::item(QStringLiteral("Nested")));
    pdf::PDFBookmarkDelegate nestedDelegate;
    pdf::PDFTreeView nestedTree;
    nestedTree.setHeaderHidden(true);
    nestedTree.setModel(&nestedModel);
    nestedTree.setItemDelegate(&nestedDelegate);
    nestedTree.expandAll();
    nestedTree.resize(320, 260);
    nestedTree.show();
    QVERIFY(QTest::qWaitForWindowExposed(&nestedTree));
    const QModelIndex nestedIndex = nestedModel.index(0, 0, nestedModel.index(0, 0));
    nestedTree.selectionModel()->select(nestedIndex, QItemSelectionModel::ClearAndSelect);
    const QImage nestedImage = renderWidget(&nestedTree, 1.0);
    const QRect nestedRect = DelegateReviewSamples::itemRectInView(&nestedTree, nestedIndex);
    QVERIFY(nestedRect.left() - nestedTree.viewport()->geometry().left() > 8);
    QVERIFY2(isNear(pixel(nestedImage, nestedTree.viewport()->geometry().left() + 4, nestedRect.top() + 4), token(Color::BgSurface), 2),
             qPrintable(pixel(nestedImage, nestedTree.viewport()->geometry().left() + 4, nestedRect.top() + 4).name()));
    QVERIFY(isNear(pixel(nestedImage, nestedRect.left() + 140, nestedRect.top() + 2), blend(token(Color::BgAccentSubtle), token(Color::BgSurface)), 3));
    nestedTree.setItemDelegate(nullptr);

    // Review C19: the title of a document tab is data (its dot stays at its end in right-to-left)
    pdf::PDFDocumentTabBar tabBar;
    tabBar.addTab(QStringLiteral("abc."));
    tabBar.setTabClosable(0, false);
    tabBar.setLayoutDirection(Qt::RightToLeft);
    tabBar.resize(240, 38);
    tabBar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabBar));
    const QImage tabImage = renderWidget(&tabBar, 1.0);
    const QPair<int, int> heights = edgeInkHeights(tabImage, tabBar.tabRect(0).adjusted(4, 4, -4, -4));
    QVERIFY2(heights.first > 4 && heights.second > 0 && heights.second <= 3, qPrintable(QStringLiteral("%1 %2").arg(heights.first).arg(heights.second)));
}

void FluentComponentsTest::delegateListWidth()
{
    // The rows fill the width of a list without the spacing, also after the
    // vertical scroll bar has appeared (a list does not lay its items out again,
    // when the scroll bar takes the place): no horizontal scroll bar (found at
    // the fixes of step 5, review F5)
    for (const int spacing : { 0, 2 })
    {
        for (const int copies : { 1, 4 })
        {
            QStandardItemModel model;
            for (int i = 0; i < copies; ++i)
            {
                DelegateSamples::bookmarks(&model);
            }
            pdf::PDFListView view;
            view.setModel(&model);
            pdf::PDFBookmarkDelegate delegate;
            view.setItemDelegate(&delegate);
            view.setSpacing(spacing);
            view.setResizeMode(QListView::Adjust);
            view.setFixedSize(296, 3 * 46 + 5 * 2 + 16);
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            QApplication::processEvents();
            const QString info = QStringLiteral("spacing %1 rows %2: viewport %3 item %4 hmax %5").arg(spacing).arg(model.rowCount())
                                     .arg(view.viewport()->width()).arg(view.visualRect(model.index(0, 0)).width()).arg(view.horizontalScrollBar()->maximum());
            QVERIFY2(!view.horizontalScrollBar()->isVisible(), qPrintable(info));
            QCOMPARE(view.horizontalScrollBar()->maximum(), 0);
            QVERIFY2(view.visualRect(model.index(0, 0)).width() <= view.viewport()->width() - 2 * spacing, qPrintable(info));
            QCOMPARE(view.verticalScrollBar()->isVisible(), copies > 1);
            view.setItemDelegate(nullptr);
        }
    }
}

// Step 4 (package C): the replies of the Notes panel are indented by IndentRole
// (note-row-reply 1025:21921 at x 24, the selection with the row) and a hidden
// annotation has the eye-off mark of StateIconRole (1026:21995)
void FluentComponentsTest::delegateNoteReplies()
{
    using Roles = pdf::PDFComponentRoles;
    using Painter = pdf::PDFComponentPainter;

    QStandardItemModel model;
    QStandardItem* note = new QStandardItem(QStringLiteral("Jakub Melka"));
    note->setData(QStringLiteral("Shorten this paragraph."), Roles::TextRole);
    model.appendRow(note);
    QStandardItem* reply = new QStandardItem(QStringLiteral("Jane Novak"));
    reply->setData(QStringLiteral("Agreed."), Roles::TextRole);
    reply->setData(QStringLiteral("corner-down-right"), Roles::IconNameRole);
    reply->setData(static_cast<int>(pdf::PDFAccentHue::Neutral), Roles::HueRole);
    reply->setData(24, Roles::IndentRole);
    model.appendRow(reply);
    QStandardItem* hidden = new QStandardItem(QStringLiteral("Peter Dvorak"));
    hidden->setData(QStringLiteral("Hidden on the page"), Roles::TextRole);
    hidden->setData(QStringLiteral("eye-off"), Roles::StateIconRole);
    model.appendRow(hidden);

    pdf::PDFNoteDelegate delegate;
    QCOMPARE(pdf::PDFNoteDelegate::getIndent(model.index(0, 0)), 0);
    QCOMPARE(pdf::PDFNoteDelegate::getIndent(model.index(1, 0)), 24);
    QCOMPARE(delegate.getContent(model.index(2, 0)).stateIconName, QStringLiteral("eye-off"));

    // The size: the whole width, the height of the row without the indentation
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 276, 48);
    const QSize replySize = delegate.sizeHint(option, model.index(1, 0));
    QCOMPARE(replySize.width(), 276);
    QCOMPARE(replySize.height(), Painter::getNoteRowHeight(delegate.getContent(model.index(1, 0)), 252));

    // A longer text wraps in the indented row: the cached height follows the indentation
    reply->setData(QStringLiteral("Agreed - keep only the first sentence of the paragraph."), Roles::TextRole);
    reply->setData(0, Roles::IndentRole);
    const int unindented = delegate.sizeHint(option, model.index(1, 0)).height();
    reply->setData(96, Roles::IndentRole);
    const int indented = delegate.sizeHint(option, model.index(1, 0)).height();
    QVERIFY(indented >= unindented);
    QCOMPARE(indented, Painter::getNoteRowHeight(delegate.getContent(model.index(1, 0)), 276 - 96));

    // Hostile indentations are bounded
    for (const QVariant& value : { QVariant(-5), QVariant(100000), QVariant(QStringLiteral("x")), QVariant() })
    {
        reply->setData(value, Roles::IndentRole);
        const int indent = pdf::PDFNoteDelegate::getIndent(model.index(1, 0));
        QVERIFY(indent >= 0 && indent <= pdf::PDFNoteDelegate::MAXIMUM_INDENT);
        QVERIFY(delegate.sizeHint(option, model.index(1, 0)).isValid());
    }
    reply->setData(QStringLiteral("Agreed."), Roles::TextRole);
    reply->setData(24, Roles::IndentRole);

    // The selected reply: the indentation keeps the background, the row is selected
    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        for (const qreal scale : { 1.0, 2.0 })
        {
            const QImage image = renderDelegate(&delegate, model.index(1, 0), QStyle::State_Enabled | QStyle::State_Selected, QSize(276, 48), scale, direction);
            const qreal indentX = direction == Qt::LeftToRight ? 12 : 276 - 12;
            const qreal rowX = direction == Qt::LeftToRight ? 24 + 4 : 252 - 4;
            QCOMPARE(pixel(image, indentX, 24).name(), token(Color::BgSurface).name());
            QVERIFY(colorDistance(pixel(image, rowX, 4), blend(token(Color::BgAccentSubtle), token(Color::BgSurface))) < 12);
            QCOMPARE(countLeakPixels(image), 0);
        }
    }

    // The eye-off mark at the trailing side (mirrored right-to-left), icon/muted
    const Painter::NoteRow hiddenRow = delegate.getContent(model.index(2, 0));
    const QRectF iconRect = Painter::getNoteRowStateIconRect(QRectF(0, 0, 276, 48), hiddenRow);
    QCOMPARE(iconRect.size(), QSizeF(14, 14));
    QCOMPARE(iconRect.right(), 272.0);
    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        const QImage image = renderDelegate(&delegate, model.index(2, 0), QStyle::State_Enabled, QSize(276, 48), 1.0, direction);
        const QRectF probe = direction == Qt::LeftToRight ? iconRect : QRectF(276 - iconRect.right(), iconRect.top(), iconRect.width(), iconRect.height());
        int inked = 0;
        for (int y = int(probe.top()); y < int(probe.bottom()); ++y)
        {
            for (int x = int(probe.left()); x < int(probe.right()); ++x)
            {
                inked += colorDistance(pixel(image, x, y), token(Color::BgSurface)) > 60 ? 1 : 0;
            }
        }
        QVERIFY2(inked > 8, qPrintable(QString::number(inked)));
    }

    // The text of a hidden row leaves the place of the icon free; without a text
    // the mark lies on the head line
    Painter::NoteRow noText = hiddenRow;
    noText.text.clear();
    QCOMPARE(Painter::getNoteRowStateIconRect(QRectF(0, 0, 276, 48), noText).top(), 8.5);
    QVERIFY(Painter::getNoteRowStateIconRect(QRectF(), hiddenRow).isEmpty());

    // The date of the head line keeps the gap before the mark of a row without a text
    // (the place of the mark is the same with and without the date)
    {
        Painter::NoteRow dated = noText;
        dated.date = QStringLiteral("Oct 19, 2026 14:05");
        Painter::NoteRow undated = noText;
        undated.date.clear();
        const QRectF markRect = Painter::getNoteRowStateIconRect(QRectF(0, 0, 276, 48), dated).adjusted(-2, 0, 0, 0);
        QImage datedImage(276, 48, QImage::Format_ARGB32_Premultiplied);
        QImage undatedImage(276, 48, QImage::Format_ARGB32_Premultiplied);
        datedImage.fill(Qt::transparent);
        undatedImage.fill(Qt::transparent);
        {
            QPainter painter(&datedImage);
            Painter::paintNoteRow(&painter, QRectF(0, 0, 276, 48), dated, Painter::State());
        }
        {
            QPainter painter(&undatedImage);
            Painter::paintNoteRow(&painter, QRectF(0, 0, 276, 48), undated, Painter::State());
        }
        QCOMPARE(datedImage.copy(markRect.toAlignedRect()), undatedImage.copy(markRect.toAlignedRect()));
        QVERIFY(datedImage != undatedImage);
    }
    Painter::paintNoteRow(nullptr, QRectF(0, 0, 276, 48), hiddenRow, Painter::State());
}

// R2: an invalid hue or role of an accent color is Neutral / Background (its
// index would be another token of the theme or none); the stamp chip, which
// takes the hue without normalizing it, draws an invalid hue as Neutral
void FluentComponentsTest::r2AccentHueDefensive()
{
    using Role = pdf::PDFAccentHueColors::Role;
    for (const int hue : { -1, -100, 7, 42 })
    {
        QCOMPARE(pdf::PDFAccentHueColors::getColor(static_cast<pdf::PDFAccentHue>(hue), Role::Icon), Color::AccentNeutralIcon);
        QCOMPARE(pdf::PDFAccentHueColors::getColor(static_cast<pdf::PDFAccentHue>(hue), Role::Background), Color::AccentNeutralBg);
    }
    for (const int role : { -1, 6, 100 })
    {
        QCOMPARE(pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Amber, static_cast<Role>(role)), Color::AccentAmberBg);
    }
    QCOMPARE(pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Green, Role::BorderStrong), Color::AccentGreenBorderStrong);

    pdf::PDFStampChip invalidChip(QStringLiteral("Draft"), static_cast<pdf::PDFAccentHue>(42));
    pdf::PDFStampChip neutralChip(QStringLiteral("Draft"), pdf::PDFAccentHue::Neutral);
    invalidChip.resize(invalidChip.sizeHint());
    neutralChip.resize(neutralChip.sizeHint());
    const QImage invalidImage = renderWidget(&invalidChip, 1.0);
    QCOMPARE(countLeakPixels(invalidImage), 0);
    QCOMPARE(describeDifference(invalidImage, renderWidget(&neutralChip, 1.0), 0), QString());
}

// R2: the actions of the menu of an item (PDFRowDelegate::addActionsToMenu)
// remember the delegate, the view and the item: a slot, which deletes the menu
// with the triggered action, a destroyed view, a menu without a view and a
// destroyed delegate
void FluentComponentsTest::r2DelegateMenuActionLifetime()
{
    QStandardItemModel model;
    DelegateSamples::attachments(&model);
    QListView view;
    view.setModel(&model);
    QPointer<pdf::PDFAttachmentDelegate> delegate = new pdf::PDFAttachmentDelegate(&view);
    view.setItemDelegate(delegate.data());
    view.resize(320, 240);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QSignalSpy triggered(delegate.data(), &pdf::PDFRowDelegate::actionTriggered);

    // (1) The slot of the signal deletes the menu with the action
    QPointer<QMenu> menu = new QMenu();
    QCOMPARE(delegate->addActionsToMenu(menu.data(), &view, model.index(1, 0)), 1);
    const QPointer<QAction> action = menu->actions().at(0);
    QVERIFY(action->isEnabled());
    connect(delegate.data(), &pdf::PDFRowDelegate::actionTriggered, menu.data(), [&menu]() { delete menu.data(); });
    action->trigger();
    QVERIFY(!menu);
    QVERIFY(!action);
    QCOMPARE(triggered.count(), 1);
    QCOMPARE(triggered.at(0).at(0).value<QModelIndex>(), model.index(1, 0));
    QCOMPARE(triggered.at(0).at(1).toInt(), int(pdf::PDFAttachmentDelegate::SaveAction));
    QCOMPARE(delegate->getLastActionView(), &view);

    // (2) A menu of a view, which has been destroyed meanwhile, does nothing
    QListView* otherView = new QListView();
    otherView->setModel(&model);
    QMenu staleMenu;
    QCOMPARE(delegate->addActionsToMenu(&staleMenu, otherView, model.index(0, 0)), 1);
    delete otherView;
    staleMenu.actions().at(0)->trigger();
    QCOMPARE(triggered.count(), 1);

    // (3) A menu of the application without a view triggers the action of the item
    QMenu viewlessMenu;
    QCOMPARE(delegate->addActionsToMenu(&viewlessMenu, nullptr, model.index(2, 0)), 1);
    viewlessMenu.actions().at(0)->trigger();
    QCOMPARE(triggered.count(), 2);
    QCOMPARE(triggered.at(1).at(0).value<QModelIndex>(), model.index(2, 0));
    QCOMPARE(delegate->getLastActionView(), nullptr);

    // (4) The delegate destroyed: the menu stays, its action does nothing
    QMenu orphanMenu;
    QCOMPARE(delegate->addActionsToMenu(&orphanMenu, &view, model.index(0, 0)), 1);
    view.setItemDelegate(nullptr);
    delete delegate.data();
    QVERIFY(!delegate);
    orphanMenu.actions().at(0)->trigger();
    QCOMPARE(orphanMenu.actions().size(), 1);
    QCOMPARE(triggered.count(), 2);
}

// R2: tagClicked gives the text of the clicked tag also to a later receiver,
// after a former one has replaced the tags (the card emits a copy of the
// text); a removed tag reports nothing more, the new tags their own texts
void FluentComponentsTest::r2VideoCardTagText()
{
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    pdf::PDFVideoCard* card = new pdf::PDFVideoCard(QStringLiteral("Getting started"), QStringLiteral("A tour"), QStringLiteral("2:45"), { QStringLiteral("OCR"), QStringLiteral("Basics") }, &window);
    layout->addWidget(card);
    window.resize(360, 200);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QApplication::processEvents();

    const QString replacement = QStringLiteral("A replacement tag with a long text");
    QStringList received;
    connect(card, &pdf::PDFVideoCard::tagClicked, card, [card, &replacement](const QString& tag)
    {
        if (tag != replacement)
        {
            card->setTags({ replacement });
        }
    });
    connect(card, &pdf::PDFVideoCard::tagClicked, card, [&received](const QString& tag) { received << tag; });

    const QList<pdf::PDFTag*> tags = card->getTagWidgets();
    QCOMPARE(tags.size(), 2);
    const QPointer<pdf::PDFTag> oldTag = tags.at(1);
    QTest::mouseClick(oldTag.data(), Qt::LeftButton);
    QCOMPARE(received, QStringList{ QStringLiteral("Basics") });

    QVERIFY(oldTag);
    oldTag->click();
    QCOMPARE(received.size(), 1);

    QTRY_VERIFY(!oldTag);
    QTRY_COMPARE(card->getTagWidgets().size(), 1);
    card->getTagWidgets().at(0)->click();
    QCOMPARE(received, (QStringList{ QStringLiteral("Basics"), replacement }));
}

// R1: the slots, comparators and helpers, which have replaced the lambdas of
// the panels, the palette, the color button and the signature card
void FluentComponentsTest::panelsMemberSlots()
{
    using Entry = pdf::PDFColorPalette::Entry;
    const QList<Entry> suggested = PanelSamples::suggested();

    // Document tabs: the state moves with its tab, the close button finds its
    // tab after a move, a close button deleted by the application leaves its tab
    pdf::PDFDocumentTabBar tabBar;
    tabBar.addTab(QStringLiteral("Home"));
    tabBar.addTab(QStringLiteral("A.pdf"));
    tabBar.addTab(QStringLiteral("B.pdf"));
    tabBar.setTabClosable(1, true);
    tabBar.setTabClosable(2, true);
    tabBar.setTabLocked(2, true);
    tabBar.setTabProgress(2, 0.25);
    auto closeButton = [&tabBar](int index) -> QAbstractButton*
    {
        QWidget* button = tabBar.tabButton(index, QTabBar::RightSide);
        return qobject_cast<QAbstractButton*>(button ? button : tabBar.tabButton(index, QTabBar::LeftSide));
    };
    QAbstractButton* closeB = closeButton(2);
    QVERIFY(closeB);
    tabBar.moveTab(2, 0);
    QVERIFY(tabBar.isTabLocked(0));
    QCOMPARE(tabBar.getTabProgress(0), 0.25);
    QVERIFY(!tabBar.isTabLocked(2));
    QCOMPARE(closeButton(0), closeB);
    QSignalSpy closeSpy(&tabBar, &QTabBar::tabCloseRequested);
    closeB->click();
    closeButton(2)->click();
    QCOMPARE(closeSpy.count(), 2);
    QCOMPARE(closeSpy.at(0).at(0).toInt(), 0);
    QCOMPARE(closeSpy.at(1).at(0).toInt(), 2);
    delete closeButton(2);
    QVERIFY(!tabBar.isTabClosable(2));
    QVERIFY(tabBar.isTabClosable(0));
    tabBar.resize(tabBar.sizeHint());
    QCOMPARE(countLeakPixels(tabBar.grab().toImage()), 0);

    // Page range: the comparators and the errors of the helpers
    const pdf::PDFPageRange::Result range = pdf::PDFPageRange::parse(QStringLiteral("9-10, 3, 1-2, 5"), 10);
    QVERIFY(range.isValid());
    QCOMPARE(pdf::PDFPageRange::format(range.intervals), QStringLiteral("1-3, 5, 9-10"));
    QVERIFY(range.contains(1) && range.contains(3) && range.contains(10));
    QVERIFY(!range.contains(0) && !range.contains(4) && !range.contains(8) && !range.contains(11));
    QCOMPARE(pdf::PDFPageRange::normalized({ { 4, 2 }, { 0, 3 }, { 7, 9 }, { 5, 6 }, { 5, 5 } }),
             (QList<pdf::PDFPageRange::Interval>{ { 5, 9 } }));
    QCOMPARE(pdf::PDFPageRange::parse(QStringLiteral("2, x"), 10).errorMessage, QStringLiteral("Can't convert '\u2068x\u2069' to a number."));
    QCOMPARE(pdf::PDFPageRange::parse(QStringLiteral("3-y"), 10).errorMessage, QStringLiteral("Can't convert '\u2068y\u2069' to a number."));
    QCOMPARE(pdf::PDFPageRange::parse(QStringLiteral("2-12"), 10).errorMessage, pdf::PDFPageRange::getMissingPageText(12, 10));
    QCOMPARE(pdf::PDFPageRange::parse(QStringLiteral("0"), 10).errorMessage, pdf::PDFPageRange::getMissingPageText(0, 10));

    // Page selection: the slots of the nested widgets
    pdf::PDFPageSelection selection;
    selection.setPageCount(10);
    QSignalSpy modeSpy(&selection, &pdf::PDFPageSelection::modeChanged);
    QSignalSpy rangeSpy(&selection, &pdf::PDFPageSelection::rangeChanged);
    QSignalSpy filtersSpy(&selection, &pdf::PDFPageSelection::filtersChanged);
    selection.getModeControl()->setCurrentIndex(3);
    QCOMPARE(selection.getMode(), pdf::PDFPageSelection::Mode::Range);
    QCOMPARE(modeSpy.count(), 1);
    selection.getRangeField()->setText(QStringLiteral("2-4"));
    QCOMPARE(selection.getRange(), QStringLiteral("2-4"));
    QCOMPARE(rangeSpy.count(), 1);
    QCOMPARE(selection.getResultCount(), qint64(3));
    selection.getFilterCheckBox(pdf::PDFPageSelection::Filter::Even)->setChecked(true);
    QCOMPARE(selection.getFilters(), pdf::PDFPageSelection::Filters(pdf::PDFPageSelection::Filter::Even));
    QCOMPARE(filtersSpy.count(), 1);
    QCOMPARE(selection.getResultPages(), (QList<int>{ 2, 4 }));
    delete selection.getFilterCheckBox(pdf::PDFPageSelection::Filter::Odd);
    selection.getFilterCheckBox(pdf::PDFPageSelection::Filter::Even)->setChecked(false);
    QCOMPARE(selection.getFilters(), pdf::PDFPageSelection::Filters());
    QCOMPARE(selection.getResultCount(), qint64(3));

    // Palette: a click on a recent or a suggested swatch selects its color, the
    // recent colors have no duplicates, "More colors…" emits its signal
    pdf::PDFColorPalette palette;
    palette.setSuggestedColors(suggested);
    palette.setRecentColors({ Entry{ QColor(1, 2, 3), QStringLiteral("Mine") }, Entry{ QColor(1, 2, 3), QStringLiteral("Twice") } });
    QCOMPARE(palette.getRecentColors().size(), 1);
    QSignalSpy selectedSpy(&palette, &pdf::PDFColorPalette::colorSelected);
    QSignalSpy moreSpy(&palette, &pdf::PDFColorPalette::moreColorsRequested);
    palette.getRecentSwatch(0)->click();
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(selectedSpy.at(0).at(0).value<QColor>(), QColor(1, 2, 3));
    QCOMPARE(palette.getCurrentColor(), QColor(1, 2, 3));
    palette.getSuggestedSwatch(3)->click();
    palette.getRecentSwatch(1)->click();
    QCOMPARE(selectedSpy.count(), 3);
    QCOMPARE(selectedSpy.at(2).at(0).value<QColor>(), QColor(1, 2, 3));
    QCOMPARE(palette.getRecentColors().size(), 2);
    QCOMPARE(palette.getRecentColors().at(0).color, QColor(1, 2, 3));
    QCOMPARE(palette.getRecentColors().at(1).color, suggested.at(3).color);
    QCOMPARE(palette.getRecentColors().at(1).name, suggested.at(3).name);
    palette.getMoreColorsLink()->click();
    QCOMPARE(moreSpy.count(), 1);

    // The popover: each hide queues the signal closed, an opening drops the pending ones
    QWidget host;
    host.resize(320, 240);
    QPushButton* anchor = new QPushButton(QStringLiteral("Color"), &host);
    anchor->setGeometry(10, 10, 100, 30);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    pdf::PDFColorPalette* popover = new pdf::PDFColorPalette(&host, Qt::Popup);
    popover->setSuggestedColors(suggested);
    QSignalSpy closedSpy(popover, &pdf::PDFColorPalette::closed);
    popover->popup(anchor);
    popover->close();
    popover->popup(anchor);
    QVERIFY(popover->isVisible());
    popover->close();
    QCOMPARE(closedSpy.count(), 0);
    QTRY_COMPARE(closedSpy.count(), 1);
    QApplication::processEvents();
    QCOMPARE(closedSpy.count(), 1);
    popover->popup(anchor);
    QVERIFY(popover->isVisible());
    delete anchor;
    QVERIFY(!popover->isVisible());
    QTRY_COMPARE(closedSpy.count(), 2);
    delete popover;

    // Color button: the color of the palette replaces the explicit name, the
    // same color is selected again without colorChanged, new colors of the
    // palette rename the shown color, a slot of colorChanged may delete the palette
    pdf::PDFColorButton button(QColor(Qt::black), QStringLiteral("Ink"));
    pdf::PDFColorPalette* buttonPalette = button.getPalette();
    buttonPalette->setSuggestedColors(suggested);
    QSignalSpy changedSpy(&button, &pdf::PDFColorButton::colorChanged);
    QSignalSpy buttonSelectedSpy(&button, &pdf::PDFColorButton::colorSelected);
    buttonPalette->getSuggestedSwatch(2)->click();
    QCOMPARE(button.getColor(), suggested.at(2).color);
    QCOMPARE(button.getColorName(), QString());
    QCOMPARE(button.getShownName(), suggested.at(2).name);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(buttonSelectedSpy.count(), 1);
    button.setColorName(QStringLiteral("Explicit"));
    buttonPalette->getSuggestedSwatch(2)->click();
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(buttonSelectedSpy.count(), 2);
    QCOMPARE(button.getColorName(), QString());
    QCOMPARE(button.text(), suggested.at(2).name);
    QList<Entry> renamed = suggested;
    renamed[2].name = QStringLiteral("Renamed");
    buttonPalette->setSuggestedColors(renamed);
    QCOMPARE(button.text(), QStringLiteral("Renamed"));
    QPointer<pdf::PDFColorPalette> paletteGuard(buttonPalette);
    QPointer<pdf::PDFColorButton> buttonGuard(&button);
    connect(&button, &pdf::PDFColorButton::colorChanged, buttonPalette, [&paletteGuard]() { paletteGuard->deleteLater(); });
    buttonPalette->getSuggestedSwatch(4)->click();
    QVERIFY(buttonGuard);
    QCOMPARE(buttonSelectedSpy.count(), 3);
    QCOMPARE(buttonSelectedSpy.at(2).at(0).value<QColor>(), suggested.at(4).color);
    QTRY_VERIFY(!paletteGuard);
    QCOMPARE(button.getShownName(), QStringLiteral("#CFD4DA"));

    // Signature card: a link emits the index of its certificate, also a link
    // created again after the application has deleted it
    pdf::PDFSignatureCard card;
    pdf::PDFSignatureCard::Certificate first;
    first.name = QStringLiteral("Signer");
    first.linkText = QStringLiteral("Trust signer");
    pdf::PDFSignatureCard::Certificate second;
    second.name = QStringLiteral("Root");
    second.linkText = QStringLiteral("Trust root");
    card.setCertificates({ first, second });
    card.setDetails({ pdf::PDFSignatureCard::Detail{ QStringLiteral("Hash"), QStringLiteral("SHA-256") } });
    const QString description = card.accessibleDescription();
    card.setDetails({ pdf::PDFSignatureCard::Detail{ QStringLiteral("Hash"), QStringLiteral("SHA-256") } });
    QCOMPARE(card.accessibleDescription(), description);
    card.setExpanded(true);
    QSignalSpy linkSpy(&card, &pdf::PDFSignatureCard::certificateLinkActivated);
    QSignalSpy expandedSpy(&card, &pdf::PDFSignatureCard::expandedChanged);
    card.getCertificateLink(1)->click();
    delete card.getCertificateLink(0);
    QCOMPARE(card.getCertificateLink(0), nullptr);
    first.linkText = QStringLiteral("Trust the signer");
    card.setCertificates({ first, second });
    QVERIFY(card.getCertificateLink(0));
    card.getCertificateLink(0)->click();
    QCOMPARE(linkSpy.count(), 2);
    QCOMPARE(linkSpy.at(0).at(0).toInt(), 1);
    QCOMPARE(linkSpy.at(1).at(0).toInt(), 0);
    card.getToggleButton()->click();
    QVERIFY(!card.isExpanded());
    QCOMPARE(expandedSpy.count(), 1);

    // The theme changes reach the slots of the panels
    pdf::PDFStrengthMeter meter;
    pdf::PDFContinueWith continueWith;
    pdf::PDFThemeManager* themeManager = pdf::PDFThemeManager::getInstance();
    const QString theme = themeManager->getTheme().getId();
    QVERIFY(themeManager->setTheme(theme == QStringLiteral("dark") ? QStringLiteral("light") : QStringLiteral("dark")));
    QVERIFY(themeManager->setTheme(theme));
    QCOMPARE(themeManager->getTheme().getId(), theme);

    // The graphics effect of the locked document moved to another widget and
    // given to it by the application does not use the deleted picture
    QWidget pictureHost;
    pictureHost.resize(320, 320);
    pdf::PDFLockedDocumentPicture* picture = new pdf::PDFLockedDocumentPicture(&pictureHost);
    QWidget* other = new QWidget(&pictureHost);
    other->setGeometry(170, 0, 140, 140);
    QGraphicsEffect* effect = picture->graphicsEffect();
    QVERIFY(effect);
    other->setGraphicsEffect(effect);
    QCOMPARE(picture->graphicsEffect(), nullptr);
    effect->setParent(other);
    delete picture;
    QCOMPARE(other->graphicsEffect(), effect);
    QVERIFY(!pictureHost.grab().isNull());

    // The logo is drawn from the SVG for a scale, whose size of the device pixels does not fit an integer
    QImage logoImage(64, 64, QImage::Format_ARGB32_Premultiplied);
    logoImage.fill(Qt::transparent);
    {
        QPainter logoPainter(&logoImage);
        logoPainter.scale(1e12, 1e12);
        pdf::PDFPreviewPainter::paintLogo(&logoPainter, QRectF(0.0, 0.0, 0.1, 0.1));
        logoPainter.resetTransform();
        logoPainter.scale(1e-9, 1e-9);
        pdf::PDFPreviewPainter::paintLogo(&logoPainter, QRectF(0.0, 0.0, 64.0, 64.0));
    }
}

// R2: performAction called directly by a subclass with an invalid index (the
// model of the index is null) does nothing (triggerAction checks the index)
void FluentComponentsTest::r2PerformActionInvalidIndex()
{
    class RecentProbe : public pdf::PDFRecentItemDelegate
    {
    public:
        void perform(int action) { performAction(nullptr, QModelIndex(), action); }
    };

    class ShortcutProbe : public pdf::PDFShortcutDelegate
    {
    public:
        void perform(int action) { performAction(nullptr, QModelIndex(), action); }
    };

    RecentProbe recent;
    QSignalSpy recentTriggered(&recent, &pdf::PDFRowDelegate::actionTriggered);
    recent.perform(pdf::PDFRecentItemDelegate::PinAction);
    QCOMPARE(recentTriggered.count(), 0);
    recent.perform(pdf::PDFRecentItemDelegate::MenuAction);
    QCOMPARE(recentTriggered.count(), 1);

    ShortcutProbe shortcut;
    QSignalSpy shortcutTriggered(&shortcut, &pdf::PDFRowDelegate::actionTriggered);
    QSignalSpy shortcutChanged(&shortcut, &pdf::PDFShortcutDelegate::shortcutChanged);
    shortcut.perform(pdf::PDFShortcutDelegate::ClearAction);
    shortcut.perform(pdf::PDFShortcutDelegate::RecordAction);
    QCOMPARE(shortcutTriggered.count(), 0);
    QCOMPARE(shortcutChanged.count(), 0);
}

void FluentComponentsTest::lambdasReviewFixes()
{
    // S9: a close button of a document tab takes itself out of its tab from its
    // own destructor (no slot of destroyed reaches the dying button); a button
    // deleted directly, a button removed by setTabClosable (deleteLater), a
    // button taken by the application and deleted after the tab bar, and the
    // tab bar deleted with its buttons (use after free is seen by valgrind)
    {
        QPointer<pdf::PDFDocumentTabBar> tabBar = new pdf::PDFDocumentTabBar();
        for (const QString& title : { QStringLiteral("Home"), QStringLiteral("A.pdf"), QStringLiteral("B.pdf"), QStringLiteral("C.pdf") })
        {
            tabBar->addTab(title);
        }
        for (int i = 1; i < 4; ++i)
        {
            tabBar->setTabClosable(i, true);
        }
        tabBar->resize(tabBar->sizeHint());
        tabBar->show();
        QVERIFY(QTest::qWaitForWindowExposed(tabBar));
        auto closeButton = [&tabBar](int index) -> QWidget*
        {
            QWidget* button = tabBar->tabButton(index, QTabBar::RightSide);
            return button ? button : tabBar->tabButton(index, QTabBar::LeftSide);
        };

        // Deleted directly: the tab forgets it while it is whole (the hide event reaches it)
        QPointer<QWidget> first = closeButton(1);
        QVERIFY(first && first->isVisible());
        delete first.data();
        QVERIFY(!closeButton(1));
        QVERIFY(!tabBar->isTabClosable(1));
        tabBar->setTabClosable(1, true);
        QVERIFY(tabBar->isTabClosable(1));
        QVERIFY(closeButton(1) && closeButton(1) != nullptr);

        // Removed by setTabClosable: deleted later, the tab bar is not called for another tab
        QPointer<QWidget> second = closeButton(2);
        tabBar->setTabClosable(2, false);
        QVERIFY(!closeButton(2));
        QVERIFY(second);
        tabBar->setTabClosable(2, true);
        QPointer<QWidget> newSecond = closeButton(2);
        QTRY_VERIFY(!second);
        QCOMPARE(closeButton(2), newSecond.data());

        // A tab removed by QTabBar::removeTab (its button is deleted later)
        QPointer<QWidget> removed = closeButton(3);
        tabBar->removeTab(3);
        QTRY_VERIFY(!removed);
        QCOMPARE(tabBar->count(), 3);

        // A button taken by the application outlives the tab bar
        QWidget holder;
        QPointer<QWidget> taken = closeButton(1);
        QVERIFY(taken);
        taken->setParent(&holder);
        QSignalSpy closeSpy(tabBar.data(), &QTabBar::tabCloseRequested);
        QPointer<QWidget> remaining = closeButton(2);
        QVERIFY(remaining);
        delete tabBar.data();
        QVERIFY(!tabBar && !remaining);
        QVERIFY(taken);
        delete taken.data();
        QCOMPARE(closeSpy.count(), 0);
    }

    // S3: the recorded sequence stays valid for every slot of shortcutConflict,
    // also when a slot deletes the view with the recorder (valgrind sees a read
    // of the freed member of the recorder)
    {
        QStandardItemModel model;
        DelegateSamples::shortcuts(&model);
        pdf::PDFShortcutDelegate delegate;
        QWidget window;
        window.resize(820, 200);
        QPointer<pdf::PDFListView> view = new pdf::PDFListView(&window);
        view->setGeometry(0, 0, 820, 160);
        view->setModel(&model);
        view->setItemDelegate(&delegate);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        QList<QKeySequence> seen;
        connect(&delegate, &pdf::PDFShortcutDelegate::shortcutConflict, &window, [&view](const QModelIndex&, const QKeySequence&, const QModelIndexList&) { delete view.data(); });
        connect(&delegate, &pdf::PDFShortcutDelegate::shortcutConflict, &window, [&seen](const QModelIndex&, const QKeySequence& sequence, const QModelIndexList&) { seen << sequence; });

        view->setFocus();
        view->setCurrentIndex(model.index(0, 0));
        QTest::keyClick(view, Qt::Key_Return);
        QPointer<QWidget> recorder = QApplication::focusWidget();
        QVERIFY(recorder && qobject_cast<pdf::PDFShortcutRecorder*>(recorder));
        // Only the press: the release would go to the deleted recorder
        QTest::keyPress(recorder.data(), Qt::Key_S, Qt::ControlModifier);
        QVERIFY(!view && !recorder);
        QCOMPARE(seen.size(), 1);
        QCOMPARE(seen.front(), QKeySequence(Qt::ControlModifier | Qt::Key_S));
        QApplication::processEvents();
    }

    // S10: the button groups and the timer of the state Copied are members, an
    // application cannot reach them by children(); a layout deleted by the
    // application (QWidget::layout() is public) lays nothing out
    {
        pdf::PDFNavList navList;
        navList.addItem(QStringLiteral("Home"), QStringLiteral("home"));
        QVERIFY(navList.findChildren<QButtonGroup*>().isEmpty());
        QVERIFY(navList.layout());
        delete navList.layout();
        QCOMPARE(navList.addItem(QStringLiteral("Tools"), QStringLiteral("tool")), 1);
        navList.addGroup();
        QCOMPARE(navList.getCount(), 2);
        navList.setCurrentIndex(1);
        QCOMPARE(navList.getCurrentIndex(), 1);

        pdf::PDFSegmentedControl segments;
        segments.addSegment(QStringLiteral("One"));
        QVERIFY(segments.findChildren<QButtonGroup*>().isEmpty());
        delete segments.layout();
        QCOMPARE(segments.addSegment(QStringLiteral("Two")), 1);
        segments.setCurrentIndex(1);
        QCOMPARE(segments.getCurrentIndex(), 1);
        QCOMPARE(segments.getCount(), 2);

        pdf::PDFContinueWith continueWith;
        QVERIFY(continueWith.findChildren<QButtonGroup*>().isEmpty());
        pdf::PDFColorPalette palette;
        QVERIFY(palette.findChildren<QButtonGroup*>().isEmpty());

        pdf::PDFPropertyRow row(QStringLiteral("Author"), QStringLiteral("Jakub"));
        QVERIFY(row.findChildren<QTimer*>().isEmpty());
        row.show();
        QVERIFY(QTest::qWaitForWindowExposed(&row));
        row.setCopiedDuration(10);
        row.copy();
        QVERIFY(row.isCopied());
        QTRY_VERIFY(!row.isCopied());
    }

    // S11: a segment of the mode control, which is not a mode, is refused and
    // the control shows the current mode again
    {
        pdf::PDFPageSelection selection;
        selection.setPageCount(10);
        pdf::PDFSegmentedControl* modes = selection.getModeControl();
        QVERIFY(modes);
        const int extra = modes->addSegment(QStringLiteral("Extra"));
        QVERIFY(extra >= 4);
        QSignalSpy modeSpy(&selection, &pdf::PDFPageSelection::modeChanged);
        modes->setCurrentIndex(extra);
        QCOMPARE(selection.getMode(), pdf::PDFPageSelection::Mode::All);
        QCOMPARE(modes->getCurrentIndex(), static_cast<int>(pdf::PDFPageSelection::Mode::All));
        QCOMPARE(modeSpy.count(), 0);
        modes->setCurrentIndex(static_cast<int>(pdf::PDFPageSelection::Mode::Range));
        QCOMPARE(selection.getMode(), pdf::PDFPageSelection::Mode::Range);
        modes->setCurrentIndex(extra);
        QCOMPARE(selection.getMode(), pdf::PDFPageSelection::Mode::Range);
        QCOMPARE(modes->getCurrentIndex(), static_cast<int>(pdf::PDFPageSelection::Mode::Range));
        QCOMPARE(modeSpy.count(), 1);
    }
}

void FluentComponentsTest::reviewNavigationRemoval()
{
    // F09: the indices are the positions of the items, also after the application
    // has deleted an item (the identifiers of the button group collided)
    pdf::PDFNavList list;
    list.addItem(QStringLiteral("A"), QString());
    list.addItem(QStringLiteral("B"), QString());
    list.addItem(QStringLiteral("C"), QString());
    QPointer<pdf::PDFNavItem> last = list.getItem(2);
    delete list.getItem(1);
    QCOMPARE(list.getItem(1), last.data());
    QCOMPARE(list.addItem(QStringLiteral("D"), QString()), 2);

    QSignalSpy changed(&list, &pdf::PDFNavList::currentIndexChanged);
    list.setCurrentIndex(1);
    QVERIFY(last->isChecked());
    QCOMPARE(list.getCurrentIndex(), 1);
    QCOMPARE(changed.last().at(0).toInt(), 1);
    list.setCurrentIndex(2);
    QVERIFY(!last->isChecked());
    QVERIFY(list.getItem(2)->isChecked());
    QCOMPARE(list.getCurrentIndex(), 2);
}

void FluentComponentsTest::reviewClipboardLifetime()
{
    // F02: the clipboard notifies its observers synchronously (offscreen, X11)
    QClipboard* clipboard = QGuiApplication::clipboard();

    // An observer deletes the delegate
    {
        clipboard->setText(QStringLiteral("before"));
        QStandardItemModel model(1, 1);
        model.setData(model.index(0, 0), QStringLiteral("font to copy"));
        QPointer<pdf::PDFFontDelegate> delegate = new pdf::PDFFontDelegate();
        connect(clipboard, &QClipboard::dataChanged, this, [delegate]() { delete delegate.data(); }, Qt::SingleShotConnection);
        delegate->triggerAction(nullptr, model.index(0, 0), pdf::PDFFontDelegate::CopyAction);
        QVERIFY(delegate.isNull());
        QCOMPARE(clipboard->text(), QStringLiteral("font to copy"));
    }

    // An observer removes the copied row: no action of an invalid index
    {
        clipboard->setText(QStringLiteral("before"));
        QStandardItemModel model(1, 1);
        model.setData(model.index(0, 0), QStringLiteral("another font"));
        pdf::PDFFontDelegate delegate;
        QSignalSpy triggered(&delegate, &pdf::PDFFontDelegate::actionTriggered);
        connect(clipboard, &QClipboard::dataChanged, &model, [&model]() { model.clear(); }, Qt::SingleShotConnection);
        delegate.triggerAction(nullptr, model.index(0, 0), pdf::PDFFontDelegate::CopyAction);
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(triggered.count(), 0);
    }
}

void FluentComponentsTest::reviewShortcutSelectionLifetime()
{
    // F03: a slot of currentChanged deletes the view before the recording starts.
    // The selection model is not a child of the view: QItemSelectionModel::setCurrentIndex
    // reads its own data after it has emitted currentChanged, so Qt itself does not
    // survive the deletion of the emitting selection model (the default one is a child
    // of the view) - an application must not delete the sender synchronously. Only the
    // view, whose loss the delegate must survive, is deleted inside of the signal.
    QStandardItemModel model(2, 1);
    model.setData(model.index(0, 0), QStringLiteral("first"));
    model.setData(model.index(1, 0), QStringLiteral("second"));
    pdf::PDFShortcutDelegate delegate;
    QItemSelectionModel selectionModel(&model);
    QPointer<pdf::PDFListView> view = new pdf::PDFListView();
    view->setModel(&model);
    view->setSelectionModel(&selectionModel);
    view->setItemDelegate(&delegate);
    view->setCurrentIndex(model.index(0, 0));
    connect(&selectionModel, &QItemSelectionModel::currentChanged, this, [view]() { delete view.data(); }, Qt::SingleShotConnection);
    delegate.triggerAction(view.data(), model.index(1, 0), pdf::PDFShortcutDelegate::RecordAction);
    QVERIFY(view.isNull());
    QCOMPARE(delegate.getRecordingCount(), 0);
}

void FluentComponentsTest::reviewPropertyEditorLifetime()
{
    // F04: a slot of the editor deletes the row, while the editing starts.
    // The slot takes the editor out of the row first: the editor is the sender, and
    // QWidgetLineControl uses its data after it has emitted textChanged / selectionChanged
    // (finishChange, emitCursorPositionChanged), so Qt itself does not survive the
    // deletion of the editor inside of its own signal - an application must not delete
    // the sender synchronously. Only the row, which guards its calls, is deleted.
    for (const bool isSelection : { false, true })
    {
        QPointer<pdf::PDFPropertyRow> row = new pdf::PDFPropertyRow();
        row->setEditable(true);
        row->setValue(QStringLiteral("new value"));
        QPointer<QLineEdit> editor = row->getEditor();
        auto deleteRow = [row, editor]()
        {
            if (editor)
            {
                editor->setParent(nullptr);
            }
            delete row.data();
        };
        if (isSelection)
        {
            connect(editor.data(), &QLineEdit::selectionChanged, this, deleteRow);
        }
        else
        {
            connect(editor.data(), &QLineEdit::textChanged, this, deleteRow);
        }
        QVERIFY(!row->startEditing());
        QVERIFY(row.isNull());
        QVERIFY(editor);
        delete editor.data();
    }
}

/// Gives the test the event filter of the delegate
class FluentComponentsTestFontDelegate : public pdf::PDFFontDelegate
{
public:
    using pdf::PDFFontDelegate::eventFilter;
};

void FluentComponentsTest::reviewDelegateFilterLifetime()
{
    // F06: a slot of sizeHintChanged deletes the watched view, the filter stops the event
    for (const bool isViewport : { false, true })
    {
        FluentComponentsTestFontDelegate delegate;
        QPointer<pdf::PDFListView> view = new pdf::PDFListView();
        delegate.watchView(view.data());
        connect(&delegate, &QAbstractItemDelegate::sizeHintChanged, this, [view]() { delete view.data(); }, Qt::SingleShotConnection);
        bool isHandled = false;
        if (isViewport)
        {
            QResizeEvent event(view->viewport()->size() + QSize(7, 0), view->viewport()->size());
            isHandled = delegate.eventFilter(view->viewport(), &event);
        }
        else
        {
            QEvent event(QEvent::FontChange);
            isHandled = delegate.eventFilter(view.data(), &event);
        }
        QVERIFY(view.isNull());
        QVERIFY(isHandled);
    }
}

void FluentComponentsTest::reviewSignatureDefaults()
{
    // F01: a signature, whose verification has not been supplied, is not shown as valid
    using Painter = pdf::PDFComponentPainter;
    QCOMPARE(Painter::SignatureCard().status, Painter::SignatureStatus::Warning);
    QCOMPARE(Painter::SignatureCertificate().status, Painter::SignatureStatus::Warning);
    pdf::PDFSignatureCard card;
    QCOMPARE(card.getStatus(), Painter::SignatureStatus::Warning);
}

void FluentComponentsTest::reviewDisplayLong()
{
    // F14: long is not cut to int (LP64)
    const long value = std::numeric_limits<long>::max();
    QCOMPARE(pdf::PDFRowDelegate::getDisplayString(QVariant::fromValue(value), QLocale::c()), QLocale::c().toString(static_cast<qlonglong>(value)));
}

void FluentComponentsTest::windowSizes()
{
    using Kind = Painter::WindowButtonKind;
    using Platform = Painter::WindowPlatform;

    // Window button 947:1475: 46 x 38, glyph 16; traffic light of macOS: circle 12 in a target of 20 x 38
    QCOMPARE(Painter::getWindowButtonSize(Platform::Windows), QSize(46, 38));
    QCOMPARE(Painter::getWindowButtonSize(Platform::Linux), QSize(46, 38));
    QCOMPARE(Painter::getWindowButtonSize(Platform::MacOS), QSize(20, 38));
    QCOMPARE(Painter::getWindowButtonIconExtent(), 16);
    QCOMPARE(Painter::getWindowButtonCircleExtent(), 12);

    // Window controls 947:1504: 138 x 38, macOS 80 x 38 (14 + 12 + 8 + 12 + 8 + 12 + 14)
    QCOMPARE(Painter::getWindowControlsSize(Platform::Windows), QSize(138, 38));
    QCOMPARE(Painter::getWindowControlsSize(Platform::Linux), QSize(138, 38));
    QCOMPARE(Painter::getWindowControlsSize(Platform::MacOS), QSize(80, 38));

    // Glyphs of the design
    QCOMPARE(Painter::getWindowButtonIconName(Kind::Minimize), QStringLiteral("window-minimize"));
    QCOMPARE(Painter::getWindowButtonIconName(Kind::Maximize), QStringLiteral("window-maximize"));
    QCOMPARE(Painter::getWindowButtonIconName(Kind::Restore), QStringLiteral("window-restore"));
    QCOMPARE(Painter::getWindowButtonIconName(Kind::Close), QStringLiteral("window-close"));
    for (const Kind kind : { Kind::Minimize, Kind::Maximize, Kind::Restore, Kind::Close })
    {
        QVERIFY(pdf::PDFThemeIcons::hasIcon(Painter::getWindowButtonIconName(kind)));
    }

    // Order of the buttons: Minimize, Maximize / Restore, Close; macOS close, minimize, zoom
    Painter::WindowControls controls;
    QCOMPARE(Painter::getWindowControlsButtons(controls), (QList<Kind>{ Kind::Minimize, Kind::Maximize, Kind::Close }));
    controls.maximized = true;
    QCOMPARE(Painter::getWindowControlsButtons(controls), (QList<Kind>{ Kind::Minimize, Kind::Restore, Kind::Close }));
    controls.platform = Platform::MacOS;
    QCOMPARE(Painter::getWindowControlsButtons(controls), (QList<Kind>{ Kind::Close, Kind::Minimize, Kind::Maximize }));

    // Rectangles of the buttons: from the leading edge, mirrored for right-to-left
    const QRectF area(0, 0, 138, 38);
    Painter::WindowControls windows;
    const Painter::State leftToRight(QStyle::State_Enabled, Qt::LeftToRight);
    const Painter::State rightToLeft(QStyle::State_Enabled, Qt::RightToLeft);
    QCOMPARE(Painter::getWindowControlsButtonRect(area, windows, Kind::Minimize, leftToRight), QRectF(0, 0, 46, 38));
    QCOMPARE(Painter::getWindowControlsButtonRect(area, windows, Kind::Maximize, leftToRight), QRectF(46, 0, 46, 38));
    QCOMPARE(Painter::getWindowControlsButtonRect(area, windows, Kind::Restore, leftToRight), QRectF(46, 0, 46, 38));
    QCOMPARE(Painter::getWindowControlsButtonRect(area, windows, Kind::Close, leftToRight), QRectF(92, 0, 46, 38));
    QCOMPARE(Painter::getWindowControlsButtonRect(area, windows, Kind::Close, rightToLeft), QRectF(0, 0, 46, 38));
    QCOMPARE(Painter::getWindowControlsButtonRect(area, windows, Kind::Minimize, rightToLeft), QRectF(92, 0, 46, 38));
    QCOMPARE(Painter::getWindowControlsButtonAt(area, windows, QPointF(100, 10), leftToRight), std::optional<Kind>(Kind::Close));
    QCOMPARE(Painter::getWindowControlsButtonAt(area, windows, QPointF(100, 10), rightToLeft), std::optional<Kind>(Kind::Minimize));
    QVERIFY(!Painter::getWindowControlsButtonAt(area, windows, QPointF(140, 10), leftToRight).has_value());

    // macOS: circles at 14, 34 and 54 (targets 10 + 3 x 20 + 10)
    Painter::WindowControls mac;
    mac.platform = Platform::MacOS;
    const QRectF macArea(0, 0, 80, 38);
    QCOMPARE(Painter::getWindowControlsButtonRect(macArea, mac, Kind::Close, leftToRight), QRectF(10, 0, 20, 38));
    QCOMPARE(Painter::getWindowControlsButtonRect(macArea, mac, Kind::Minimize, leftToRight), QRectF(30, 0, 20, 38));
    QCOMPARE(Painter::getWindowControlsButtonRect(macArea, mac, Kind::Maximize, leftToRight), QRectF(50, 0, 20, 38));
    QCOMPARE(Painter::getWindowControlsButtonRect(macArea, mac, Kind::Close, rightToLeft), QRectF(50, 0, 20, 38));

    // Widgets have the sizes of the design in every state
    pdf::PDFWindowButton button(Kind::Close, Platform::Windows);
    QCOMPARE(button.sizeHint(), QSize(46, 38));
    QCOMPARE(button.minimumSizeHint(), QSize(46, 38));
    button.setAttribute(Qt::WA_UnderMouse, true);
    button.setDown(true);
    button.setEnabled(false);
    button.setActivity(pdf::PDFWindowButton::Activity::Inactive);
    button.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(button.sizeHint(), QSize(46, 38));
    button.setPlatform(Platform::MacOS);
    QCOMPARE(button.sizeHint(), QSize(20, 38));
    QCOMPARE(button.focusPolicy(), Qt::NoFocus);

    pdf::PDFWindowControls group(Platform::Windows);
    QCOMPARE(group.sizeHint(), QSize(138, 38));
    group.setMaximized(true);
    group.setResizable(false);
    QCOMPARE(group.sizeHint(), QSize(138, 38));
    group.setPlatform(Platform::MacOS);
    QCOMPARE(group.sizeHint(), QSize(80, 38));
    QCOMPARE(group.minimumSizeHint(), QSize(80, 38));
    group.setSystemButtons(true);
    QCOMPARE(group.sizeHint(), QSize(80, 38));

    // The native platform of the build
#if defined(Q_OS_MACOS)
    QCOMPARE(Painter::getNativeWindowPlatform(), Platform::MacOS);
#elif defined(Q_OS_WIN)
    QCOMPARE(Painter::getNativeWindowPlatform(), Platform::Windows);
#else
    QCOMPARE(Painter::getNativeWindowPlatform(), Platform::Linux);
#endif
    QCOMPARE(pdf::PDFWindowControls().getPlatform(), Painter::getNativeWindowPlatform());
}

void FluentComponentsTest::windowPixels_data()
{
    QTest::addColumn<QString>("theme");
    QTest::addColumn<qreal>("scale");

    for (const char* theme : { "light", "dark" })
    {
        for (const qreal scale : { 1.0, 1.5, 2.0 })
        {
            QTest::newRow(qPrintable(QStringLiteral("%1@%2").arg(QString::fromLatin1(theme)).arg(scale))) << QString::fromLatin1(theme) << scale;
        }
    }
}

void FluentComponentsTest::windowPixels()
{
    using Kind = Painter::WindowButtonKind;
    using Platform = Painter::WindowPlatform;

    QFETCH(QString, theme);
    QFETCH(qreal, scale);
    QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));

    const QColor surface = token(Color::BgSurface);
    const QSize size(46, 38);
    const QRect glyphArea(15, 11, 16, 16);

    auto renderButton = [&](Kind kind, QStyle::State flags, bool windowActive)
    {
        const Painter::WindowButton button{ kind, Platform::Windows, windowActive };
        return render([&](QPainter& painter) { Painter::paintWindowButton(&painter, QRectF(QPointF(0, 0), size), button, Painter::State(flags)); }, size, scale);
    };
    auto hasGlyph = [&](const QImage& image, Color color)
    {
        return countPixels(image, glyphArea, token(color), 24) > 0;
    };

    const QStyle::State enabled = QStyle::State_Enabled;
    for (const Kind kind : { Kind::Minimize, Kind::Maximize, Kind::Restore, Kind::Close })
    {
        const bool isClose = kind == Kind::Close;
        const QString message = QStringLiteral("%1 %2@%3").arg(int(kind)).arg(theme).arg(scale);

        // Default: no background, glyph icon/default in the middle
        QImage image = renderButton(kind, enabled, true);
        QVERIFY2(isNear(pixel(image, 1, 1), surface), qPrintable(message));
        QVERIFY2(isNear(pixel(image, 44, 36), surface), qPrintable(message));
        QVERIFY2(hasGlyph(image, Color::IconDefault), qPrintable(message));
        QVERIFY2(countPixels(image, QRect(0, 0, 46, 10), token(Color::IconDefault), 24) == 0, qPrintable(message));

        // Hover: bg/hover (Close bg/danger) over the whole button, glyph icon/strong (Close icon/on-accent)
        image = renderButton(kind, enabled | QStyle::State_MouseOver, true);
        const QColor hover = isClose ? token(Color::BgDanger) : blend(token(Color::BgHover), surface);
        QVERIFY2(isNear(pixel(image, 0, 0), hover), qPrintable(message));
        QVERIFY2(isNear(pixel(image, 45, 37), hover), qPrintable(message));
        QVERIFY2(hasGlyph(image, isClose ? Color::IconOnAccent : Color::IconStrong), qPrintable(message));

        // Pressed: bg/pressed (Close bg/danger-pressed)
        image = renderButton(kind, enabled | QStyle::State_MouseOver | QStyle::State_Sunken, true);
        const QColor pressed = isClose ? token(Color::BgDangerPressed) : blend(token(Color::BgPressed), surface);
        QVERIFY2(isNear(pixel(image, 0, 0), pressed), qPrintable(message));
        QVERIFY2(isNear(pixel(image, 23, 3), pressed), qPrintable(message));
        QVERIFY2(hasGlyph(image, isClose ? Color::IconOnAccent : Color::IconStrong), qPrintable(message));

        // Inactive: no background, glyph icon/disabled; the hover looks as in an active window
        image = renderButton(kind, enabled, false);
        QVERIFY2(isNear(pixel(image, 1, 1), surface), qPrintable(message));
        QVERIFY2(hasGlyph(image, Color::IconDisabled), qPrintable(message));
        QVERIFY2(!hasGlyph(image, Color::IconDefault) || token(Color::IconDefault) == token(Color::IconDisabled), qPrintable(message));
        const QImage inactiveHover = renderButton(kind, enabled | QStyle::State_MouseOver, false);
        QCOMPARE(inactiveHover, renderButton(kind, enabled | QStyle::State_MouseOver, true));

        // Disabled: no hover, no press, glyph icon/disabled
        image = renderButton(kind, QStyle::State_MouseOver | QStyle::State_Sunken, true);
        QVERIFY2(isNear(pixel(image, 1, 1), surface), qPrintable(message));
        QVERIFY2(hasGlyph(image, Color::IconDisabled), qPrintable(message));
    }

    // Traffic lights of macOS: circles of 12 px in the middle of the target, the inactive window gray
    const QSize macSize(20, 38);
    struct Light
    {
        Kind kind;
        Color color;
    };
    for (const Light light : { Light{ Kind::Close, Color::WindowMacosClose }, Light{ Kind::Minimize, Color::WindowMacosMinimize }, Light{ Kind::Maximize, Color::WindowMacosZoom }, Light{ Kind::Restore, Color::WindowMacosZoom } })
    {
        for (const bool windowActive : { true, false })
        {
            const Painter::WindowButton button{ light.kind, Platform::MacOS, windowActive };
            for (const QStyle::State flags : { enabled, enabled | QStyle::State_MouseOver | QStyle::State_Sunken })
            {
                const QImage image = render([&](QPainter& painter) { Painter::paintWindowButton(&painter, QRectF(QPointF(0, 0), macSize), button, Painter::State(flags)); }, macSize, scale);
                QVERIFY(isNear(pixel(image, 10, 19), token(windowActive ? light.color : Color::WindowMacosInactive)));
                QVERIFY(isNear(pixel(image, 10, 12), surface));
                QVERIFY(isNear(pixel(image, 2, 19), surface));
                QVERIFY(isNear(pixel(image, 10, 22), token(windowActive ? light.color : Color::WindowMacosInactive)));
            }
        }

        // A disabled zoom button has the color of the inactive window
        const Painter::WindowButton button{ light.kind, Platform::MacOS, true };
        const QImage image = render([&](QPainter& painter) { Painter::paintWindowButton(&painter, QRectF(QPointF(0, 0), macSize), button, Painter::State(QStyle::State_None)); }, macSize, scale);
        QVERIFY(isNear(pixel(image, 10, 19), token(Color::WindowMacosInactive)));
    }

    // Group: the hovered Close (bg/danger) at the trailing edge, mirrored for right-to-left
    Painter::WindowControls controls;
    controls.hovered = Kind::Close;
    QImage image = render([&](QPainter& painter) { Painter::paintWindowControls(&painter, QRectF(0, 0, 138, 38), controls, Painter::State(enabled)); }, QSize(138, 38), scale);
    QVERIFY(isNear(pixel(image, 137, 1), token(Color::BgDanger)));
    QVERIFY(isNear(pixel(image, 91, 1), surface));
    QVERIFY(isNear(pixel(image, 1, 1), surface));
    image = render([&](QPainter& painter) { Painter::paintWindowControls(&painter, QRectF(0, 0, 138, 38), controls, Painter::State(enabled, Qt::RightToLeft)); }, QSize(138, 38), scale);
    QVERIFY(isNear(pixel(image, 1, 1), token(Color::BgDanger)));
    QVERIFY(isNear(pixel(image, 137, 1), surface));

    // A window of a fixed size: the Maximize button is disabled, its hover is not drawn
    controls.hovered = Kind::Maximize;
    controls.resizable = false;
    image = render([&](QPainter& painter) { Painter::paintWindowControls(&painter, QRectF(0, 0, 138, 38), controls, Painter::State(enabled)); }, QSize(138, 38), scale);
    QVERIFY(isNear(pixel(image, 47, 1), surface));
    QVERIFY(countPixels(image, QRect(61, 11, 16, 16), token(Color::IconDisabled), 24) > 0);

    // Pressed Restore of a maximized window
    controls.resizable = true;
    controls.maximized = true;
    controls.hovered.reset();
    controls.pressed = Kind::Restore;
    image = render([&](QPainter& painter) { Painter::paintWindowControls(&painter, QRectF(0, 0, 138, 38), controls, Painter::State(enabled)); }, QSize(138, 38), scale);
    QVERIFY(isNear(pixel(image, 47, 1), blend(token(Color::BgPressed), surface)));
    QVERIFY(isNear(pixel(image, 1, 1), surface));

    // The widgets draw as the painter functions (also with the hover of the system)
    // (a child widget: a window would fill its background)
    QWidget holder;
    pdf::PDFWindowControls* group = new pdf::PDFWindowControls(Platform::Windows, &holder);
    group->resize(group->sizeHint());
    group->setActivity(pdf::PDFWindowControls::Activity::Active);
    group->setHoveredButton(Kind::Close);
    Painter::WindowControls painted;
    painted.hovered = Kind::Close;
    QString difference = describeDifference(renderWidget(group, scale), renderInWidget([&](QPainter& painter) { Painter::paintWindowControls(&painter, QRectF(0, 0, 138, 38), painted, Painter::State(enabled)); }, QSize(138, 38), scale), 2);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));

    group->setPlatform(Platform::MacOS);
    group->resize(group->sizeHint());
    group->setActivity(pdf::PDFWindowControls::Activity::Inactive);
    painted.platform = Platform::MacOS;
    painted.windowActive = false;
    difference = describeDifference(renderWidget(group, scale), renderInWidget([&](QPainter& painter) { Painter::paintWindowControls(&painter, QRectF(0, 0, 80, 38), painted, Painter::State(enabled)); }, QSize(80, 38), scale), 2);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));

    // System buttons of macOS: nothing is drawn
    group->setSystemButtons(true);
    image = renderWidget(group, scale);
    QCOMPARE(countPixels(image, QRect(0, 0, 80, 38), surface, 0), 80 * 38);

    // No leak of the palette
    QCOMPARE(countLeakPixels(renderWidget(group, scale)), 0);
}

void FluentComponentsTest::windowControlsBehaviour()
{
    using Kind = Painter::WindowButtonKind;
    using Platform = Painter::WindowPlatform;

    QWidget window;
    window.resize(400, 100);
    QHBoxLayout* layout = new QHBoxLayout(&window);
    layout->setContentsMargins(10, 20, 30, 40);
    layout->addStretch(1);
    pdf::PDFWindowControls* controls = new pdf::PDFWindowControls(Platform::Windows, &window);
    layout->addWidget(controls);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // Buttons in the logical order with the names of the actions
    const QList<pdf::PDFWindowButton*> buttons = controls->getButtons();
    QCOMPARE(buttons.size(), 3);
    QCOMPARE(buttons[0]->getKind(), Kind::Minimize);
    QCOMPARE(buttons[1]->getKind(), Kind::Maximize);
    QCOMPARE(buttons[2]->getKind(), Kind::Close);
    QCOMPARE(controls->getButton(Kind::Restore), buttons[1]);
    QCOMPARE(QAccessible::queryAccessibleInterface(buttons[0])->text(QAccessible::Name), QStringLiteral("Minimize"));
    QCOMPARE(QAccessible::queryAccessibleInterface(buttons[1])->text(QAccessible::Name), QStringLiteral("Maximize"));
    QCOMPARE(QAccessible::queryAccessibleInterface(buttons[2])->text(QAccessible::Name), QStringLiteral("Close"));
    QCOMPARE(QAccessible::queryAccessibleInterface(buttons[2])->role(), QAccessible::Button);
    QCOMPARE(buttons[2]->toolTip(), QStringLiteral("Close"));
    QCOMPARE(controls->accessibleName(), QStringLiteral("Window controls"));
    for (pdf::PDFWindowButton* button : buttons)
    {
        QCOMPARE(button->focusPolicy(), Qt::NoFocus);
        QVERIFY(button->shortcut().isEmpty());
    }

    // Rectangles in the coordinates of the window: the group lies at the right edge before the margin
    const QPoint origin = controls->mapTo(&window, QPoint(0, 0));
    QCOMPARE(controls->getButtonRect(Kind::Minimize), QRect(origin, QSize(46, 38)));
    QCOMPARE(controls->getButtonRect(Kind::Maximize), QRect(origin + QPoint(46, 0), QSize(46, 38)));
    QCOMPARE(controls->getButtonRect(Kind::Close), QRect(origin + QPoint(92, 0), QSize(46, 38)));
    QCOMPARE(controls->getButtonAt(origin + QPoint(50, 5)), std::optional<Kind>(Kind::Maximize));
    QCOMPARE(controls->getButtonAt(origin + QPoint(137, 37)), std::optional<Kind>(Kind::Close));
    QVERIFY(!controls->getButtonAt(origin + QPoint(-1, 5)).has_value());
    QVERIFY(!controls->getButtonAt(QPoint(0, 0)).has_value());

    // Clicks emit the signals
    QSignalSpy minimizeSpy(controls, &pdf::PDFWindowControls::minimizeRequested);
    QSignalSpy maximizeSpy(controls, &pdf::PDFWindowControls::maximizeRestoreRequested);
    QSignalSpy closeSpy(controls, &pdf::PDFWindowControls::closeRequested);
    QTest::mouseClick(buttons[0], Qt::LeftButton, Qt::NoModifier, QPoint(23, 19));
    QTest::mouseClick(buttons[1], Qt::LeftButton, Qt::NoModifier, QPoint(23, 19));
    QTest::mouseClick(buttons[2], Qt::LeftButton, Qt::NoModifier, QPoint(23, 19));
    QCOMPARE(minimizeSpy.count(), 1);
    QCOMPARE(maximizeSpy.count(), 1);
    QCOMPARE(closeSpy.count(), 1);

    // The press action of assistive technologies
    QAccessibleActionInterface* action = QAccessible::queryAccessibleInterface(buttons[0])->actionInterface();
    QVERIFY(action);
    action->doAction(QAccessibleActionInterface::pressAction());
    QTRY_COMPARE(minimizeSpy.count(), 2);

    // Activation by the window (WM_NCLBUTTONUP over a button)
    QVERIFY(controls->activate(Kind::Close));
    QCOMPARE(closeSpy.count(), 2);
    QVERIFY(controls->activate(Kind::Restore));
    QCOMPARE(maximizeSpy.count(), 2);
    QVERIFY(!controls->activate(static_cast<Kind>(42)));

    // Maximized window: the middle button is Restore
    controls->setMaximized(true);
    QCOMPARE(buttons[1]->getKind(), Kind::Restore);
    QCOMPARE(QAccessible::queryAccessibleInterface(buttons[1])->text(QAccessible::Name), QStringLiteral("Restore"));
    QCOMPARE(buttons[1]->toolTip(), QStringLiteral("Restore"));
    QCOMPARE(controls->getButtonAt(origin + QPoint(50, 5)), std::optional<Kind>(Kind::Restore));
    QTest::mouseClick(buttons[1], Qt::LeftButton, Qt::NoModifier, QPoint(23, 19));
    QCOMPARE(maximizeSpy.count(), 3);

    // A window of a fixed size: Maximize is disabled, a click and the activation do nothing
    controls->setResizable(false);
    QVERIFY(!buttons[1]->isEnabled());
    QTest::mouseClick(buttons[1], Qt::LeftButton, Qt::NoModifier, QPoint(23, 19));
    QVERIFY(!controls->activate(Kind::Maximize));
    QCOMPARE(maximizeSpy.count(), 3);
    controls->setResizable(true);
    QVERIFY(buttons[1]->isEnabled());

    // Hover and press given by the window (non-client messages); Restore and Maximize are one button
    controls->setHoveredButton(Kind::Maximize);
    QCOMPARE(controls->getHoveredButton(), std::optional<Kind>(Kind::Maximize));
    QVERIFY(buttons[1]->isExternalHover());
    QVERIFY(buttons[1]->getPaintedState().isHovered());
    QVERIFY(!buttons[0]->getPaintedState().isHovered());
    controls->setPressedButton(Kind::Restore);
    QVERIFY(buttons[1]->isExternalPressed());
    QVERIFY(buttons[1]->getPaintedState().isPressed());
    QCOMPARE(controls->getPaintedControls().pressed, std::optional<Kind>(Kind::Restore));
    controls->setHoveredButton(Kind::Close);
    QVERIFY(!buttons[1]->isExternalHover());
    QVERIFY(buttons[2]->isExternalHover());
    controls->setHoveredButton(static_cast<Kind>(-3));
    QVERIFY(!controls->getHoveredButton().has_value());
    QVERIFY(!buttons[2]->isExternalHover());
    controls->setPressedButton(std::nullopt);
    QVERIFY(!buttons[1]->isExternalPressed());

    // A tooltip set by the application is kept
    buttons[2]->setToolTip(QStringLiteral("Close the window"));
    buttons[2]->setKind(Kind::Close);
    QEvent languageChange(QEvent::LanguageChange);
    QApplication::sendEvent(buttons[2], &languageChange);
    QCOMPARE(buttons[2]->toolTip(), QStringLiteral("Close the window"));
    controls->setAccessibleName(QStringLiteral("Caption buttons"));
    QApplication::sendEvent(controls, &languageChange);
    QCOMPARE(controls->accessibleName(), QStringLiteral("Caption buttons"));

    // Right-to-left: Close is the leftmost button
    controls->setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(buttons[2]->geometry(), QRect(0, 0, 46, 38));
    QCOMPARE(buttons[0]->geometry(), QRect(92, 0, 46, 38));
    QCOMPARE(controls->getButtonAt(origin + QPoint(5, 5)), std::optional<Kind>(Kind::Close));
    controls->setLayoutDirection(Qt::LeftToRight);
    QCOMPARE(buttons[2]->geometry(), QRect(92, 0, 46, 38));

    // macOS: close, minimize, zoom; the names of the native buttons, no tooltips
    controls->setPlatform(Platform::MacOS);
    QCOMPARE(controls->sizeHint(), QSize(80, 38));
    QTRY_COMPARE(controls->size(), QSize(80, 38));
    const QList<pdf::PDFWindowButton*> macButtons = controls->getButtons();
    QCOMPARE(macButtons.size(), 3);
    QCOMPARE(macButtons[0]->getKind(), Kind::Close);
    QCOMPARE(macButtons[2]->getKind(), Kind::Maximize);
    QCOMPARE(macButtons[0]->geometry(), QRect(10, 0, 20, 38));
    QCOMPARE(QAccessible::queryAccessibleInterface(macButtons[2])->text(QAccessible::Name), QStringLiteral("Zoom"));
    QVERIFY(macButtons[1]->toolTip().isEmpty());
    QVERIFY(macButtons[0]->toolTip() == QStringLiteral("Close the window"));

    // System buttons of macOS: only the place is kept
    controls->setSystemButtons(true);
    QVERIFY(!controls->hasOwnButtons());
    QVERIFY(controls->getButtons().isEmpty());
    QVERIFY(!controls->getButton(Kind::Close));
    QVERIFY(controls->getButtonRect(Kind::Close).isEmpty());
    QVERIFY(!controls->getButtonAt(controls->mapTo(&window, QPoint(15, 19))).has_value());
    QVERIFY(!controls->activate(Kind::Close));
    for (pdf::PDFWindowButton* button : macButtons)
    {
        QVERIFY(button->isHidden());
    }

    // The system buttons belong only to macOS
    controls->setPlatform(Platform::Linux);
    QVERIFY(controls->hasOwnButtons());
    QCOMPARE(controls->getButtons().size(), 3);
    QVERIFY(!controls->getButton(Kind::Close)->isHidden());
    controls->setSystemButtons(false);

    // A button deleted by the application is forgotten
    delete controls->getButton(Kind::Minimize);
    QVERIFY(!controls->getButton(Kind::Minimize));
    QCOMPARE(controls->getButtons().size(), 2);
    controls->setMaximized(false);
    controls->setPlatform(Platform::Windows);
    controls->resize(200, 38);
    QVERIFY(!controls->activate(Kind::Minimize));
    QVERIFY(controls->getButtonRect(Kind::Minimize).isEmpty());

    // The group deleted from inside of its own signal
    QPointer<pdf::PDFWindowControls> guard(controls);
    QObject::connect(controls, &pdf::PDFWindowControls::closeRequested, controls, [controls]() { delete controls; });
    QTest::mouseClick(controls->getButton(Kind::Close), Qt::LeftButton, Qt::NoModifier, QPoint(23, 19));
    QVERIFY(guard.isNull());

    // ... and by activate
    pdf::PDFWindowControls* second = new pdf::PDFWindowControls(Platform::Windows, &window);
    QPointer<pdf::PDFWindowControls> secondGuard(second);
    QObject::connect(second, &pdf::PDFWindowControls::maximizeRestoreRequested, second, [second]() { delete second; });
    QVERIFY(second->activate(Kind::Maximize));
    QVERIFY(secondGuard.isNull());
}

void FluentComponentsTest::windowActivity()
{
    using Kind = Painter::WindowButtonKind;
    using Activity = pdf::PDFWindowButton::Activity;

    // The buttons follow the activity of their window
    QWidget window;
    window.resize(300, 60);
    pdf::PDFWindowControls* controls = new pdf::PDFWindowControls(Painter::WindowPlatform::Windows, &window);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    pdf::PDFWindowButton* close = controls->getButton(Kind::Close);
    QCOMPARE(controls->getActivity(), Activity::FollowWindow);
    QCOMPARE(close->getActivity(), Activity::FollowWindow);
    QVERIFY(close->isWindowActive());
    QVERIFY(close->getPaintedButton().windowActive);
    QVERIFY(controls->getPaintedControls().windowActive);

    // The button is repainted, when its window is deactivated (the window
    // passes WindowDeactivate to its children, not ActivationChange)
    class PaintCounter : public QObject
    {
    public:
        int count = 0;

    protected:
        virtual bool eventFilter(QObject* watched, QEvent* event) override
        {
            if (event->type() == QEvent::Paint)
            {
                ++count;
            }
            return QObject::eventFilter(watched, event);
        }
    };
    PaintCounter paintCounter;
    close->installEventFilter(&paintCounter);
    QApplication::processEvents();
    paintCounter.count = 0;

    QWidget other;
    other.resize(100, 100);
    other.show();
    other.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&other));
    QTRY_VERIFY(!close->isWindowActive());
    QTRY_VERIFY(paintCounter.count > 0);
    close->removeEventFilter(&paintCounter);
    QVERIFY(!controls->getPaintedControls().windowActive);

    // The application overrides the activity
    controls->setActivity(Activity::Active);
    QVERIFY(close->isWindowActive());
    controls->setActivity(Activity::Inactive);
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QVERIFY(!close->isWindowActive());
    QVERIFY(!controls->getPaintedControls().windowActive);

    // An invalid value follows the window
    controls->setActivity(static_cast<Activity>(17));
    QCOMPARE(controls->getActivity(), Activity::FollowWindow);
    QVERIFY(close->isWindowActive());
    close->setActivity(static_cast<Activity>(-1));
    QCOMPARE(close->getActivity(), Activity::FollowWindow);

    // The theme changes the colors, not the sizes
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(controls->sizeHint(), QSize(138, 38));
    QCOMPARE(countLeakPixels(renderWidget(controls, 1.0)), 0);
}

void FluentComponentsTest::windowDefensive()
{
    using Kind = Painter::WindowButtonKind;
    using Platform = Painter::WindowPlatform;

    // Invalid values of the enumerations
    QCOMPARE(Painter::normalized(static_cast<Kind>(42)), Kind::Minimize);
    QCOMPARE(Painter::normalized(static_cast<Platform>(-7)), Painter::getNativeWindowPlatform());
    QCOMPARE(Painter::getWindowButtonSize(static_cast<Platform>(99)), Painter::getWindowButtonSize(Painter::getNativeWindowPlatform()));
    QCOMPARE(Painter::getWindowButtonIconName(static_cast<Kind>(42)), QStringLiteral("window-minimize"));
    QVERIFY(Painter::getWindowControlsButtonRect(QRectF(0, 0, 138, 38), Painter::WindowControls(), static_cast<Kind>(42), Painter::State()).isEmpty());
    QVERIFY(Painter::getWindowControlsButtonRect(QRectF(), Painter::WindowControls(), Kind::Close, Painter::State()).isEmpty());
    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    QVERIFY(Painter::getWindowControlsButtonRect(QRectF(nan, 0, 138, 38), Painter::WindowControls(), Kind::Close, Painter::State()).isEmpty());
    QVERIFY(!Painter::getWindowControlsButtonAt(QRectF(), Painter::WindowControls(), QPointF(1, 1), Painter::State()).has_value());

    pdf::PDFWindowButton button(static_cast<Kind>(42), static_cast<Platform>(99));
    QCOMPARE(button.getKind(), Kind::Minimize);
    QCOMPARE(button.getPlatform(), Painter::getNativeWindowPlatform());
    button.setKind(static_cast<Kind>(-1));
    QCOMPARE(button.getKind(), Kind::Minimize);
    QCOMPARE(pdf::PDFWindowButton::getActionName(static_cast<Kind>(42), Platform::Windows), QStringLiteral("Minimize"));

    pdf::PDFWindowControls controls(static_cast<Platform>(99));
    QCOMPARE(controls.getPlatform(), Painter::getNativeWindowPlatform());
    QVERIFY(!controls.getButton(static_cast<Kind>(42)));
    QVERIFY(controls.getButtonRect(static_cast<Kind>(42)).isEmpty());
    controls.setPressedButton(static_cast<Kind>(42));
    QVERIFY(!controls.getPressedButton().has_value());

    // Drawing tolerates a null painter, empty, negative and not finite rectangles, invalid contents
    Painter::WindowButton content{ static_cast<Kind>(42), static_cast<Platform>(99), true };
    Painter::paintWindowButton(nullptr, QRectF(0, 0, 46, 38), content, Painter::State());
    Painter::paintWindowControls(nullptr, QRectF(0, 0, 138, 38), Painter::WindowControls(), Painter::State());
    const QImage image = render([&](QPainter& painter)
    {
        Painter::paintWindowButton(&painter, QRectF(), content, Painter::State());
        Painter::paintWindowButton(&painter, QRectF(10, 10, -5, -5), content, Painter::State());
        Painter::paintWindowButton(&painter, QRectF(nan, 0, 46, 38), content, Painter::State());
        Painter::paintWindowControls(&painter, QRectF(0, 0, nan, 38), Painter::WindowControls(), Painter::State());
        Painter::WindowControls invalid;
        invalid.platform = static_cast<Platform>(99);
        invalid.hovered = static_cast<Kind>(42);
        invalid.pressed = static_cast<Kind>(-4);
        Painter::paintWindowControls(&painter, QRectF(0, 0, 4, 4), invalid, Painter::State(QStyle::State_Enabled | QStyle::State_HasFocus));
    }, QSize(46, 38));
    QCOMPARE(countLeakPixels(image), 0);
    QVERIFY(isNear(pixel(image, 30, 30), token(Color::BgSurface)));

    // A tiny button draws its glyph into its rectangle only
    const QImage tiny = render([&](QPainter& painter) { Painter::paintWindowButton(&painter, QRectF(0, 0, 6, 6), Painter::WindowButton{ Kind::Close, Platform::Windows, true }, Painter::State(QStyle::State_Enabled)); }, QSize(20, 20));
    QVERIFY(isNear(pixel(tiny, 10, 10), token(Color::BgSurface)));
}

void FluentComponentsTest::windowControlsApplication()
{
    using Kind = Painter::WindowButtonKind;
    using Platform = Painter::WindowPlatform;

    QWidget window;
    window.resize(400, 100);
    QWidget* bar = new QWidget(&window);
    bar->setGeometry(0, 0, 400, 38);
    pdf::PDFWindowControls* controls = new pdf::PDFWindowControls(Platform::Windows, bar);
    controls->setGeometry(262, 0, 138, 38);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // The group or its parent hidden (a top bar hidden in the full screen):
    // the window must not answer HTMAXBUTTON over the place of the buttons
    QCOMPARE(controls->getButtonRect(Kind::Maximize), QRect(308, 0, 46, 38));
    controls->hide();
    QVERIFY(controls->getButtonRect(Kind::Maximize).isEmpty());
    QVERIFY(!controls->getButtonAt(QPoint(310, 5)).has_value());
    controls->show();
    QCOMPARE(controls->getButtonAt(QPoint(310, 5)), std::optional<Kind>(Kind::Maximize));
    bar->hide();
    QVERIFY(controls->getButtonRect(Kind::Close).isEmpty());
    QVERIFY(!controls->getButtonAt(QPoint(360, 5)).has_value());
    bar->show();
    QCOMPARE(controls->getButtonRect(Kind::Close), QRect(354, 0, 46, 38));

    // A button hidden or disabled by the application (a window without
    // Minimize, a window, which cannot be closed now) stays so, whatever the
    // window tells the group
    const QList<QPointer<pdf::PDFWindowButton>> buttons = { controls->getButton(Kind::Minimize), controls->getButton(Kind::Maximize), controls->getButton(Kind::Close) };
    QSignalSpy minimizeSpy(controls, &pdf::PDFWindowControls::minimizeRequested);
    QSignalSpy closeSpy(controls, &pdf::PDFWindowControls::closeRequested);
    buttons[0]->hide();
    buttons[2]->setEnabled(false);
    controls->setHoveredButton(Kind::Maximize);
    controls->setPressedButton(Kind::Maximize);
    controls->setMaximized(true);
    controls->setResizable(false);
    controls->setResizable(true);
    controls->setActivity(pdf::PDFWindowControls::Activity::Inactive);
    controls->setPlatform(Platform::Linux);
    controls->setHoveredButton(std::nullopt);
    controls->setPressedButton(std::nullopt);
    QVERIFY(buttons[0]->isHidden());
    QVERIFY(!buttons[2]->isEnabled());
    QVERIFY(buttons[1]->isEnabled());
    QVERIFY(controls->getButtonRect(Kind::Minimize).isEmpty());
    QVERIFY(!controls->getButtonAt(QPoint(270, 5)).has_value());
    QVERIFY(!controls->activate(Kind::Minimize));
    QVERIFY(!controls->activate(Kind::Close));
    QCOMPARE(minimizeSpy.count(), 0);
    QCOMPARE(closeSpy.count(), 0);

    // The system buttons of macOS hide the buttons; without them the group
    // shows again only the buttons, which it has hidden
    controls->setPlatform(Platform::MacOS);
    controls->setSystemButtons(true);
    for (const QPointer<pdf::PDFWindowButton>& button : buttons)
    {
        QVERIFY(button && button->isHidden());
    }
    controls->setSystemButtons(false);
    QVERIFY(buttons[0]->isHidden());
    QVERIFY(!buttons[1]->isHidden());
    QVERIFY(!buttons[2]->isHidden());
    QVERIFY(!buttons[2]->isEnabled());

    // The application shows and enables the buttons again
    buttons[0]->show();
    buttons[2]->setEnabled(true);
    controls->setPlatform(Platform::Windows);
    QVERIFY(controls->activate(Kind::Minimize));
    QVERIFY(controls->activate(Kind::Close));
    QCOMPARE(minimizeSpy.count(), 1);
    QCOMPARE(closeSpy.count(), 1);
    QCOMPARE(controls->getButtonAt(QPoint(270, 5)), std::optional<Kind>(Kind::Minimize));
}

void FluentComponentsTest::documentTabLockClicked()
{
    // A click on the lock of a tab (press and release on it) emits lockClicked;
    // the rectangle of the lock follows the label, also right-to-left
    pdf::PDFDocumentTabBar tabBar;
    const int home = tabBar.addTab(pdf::PDFThemeIcons::getIcon(QStringLiteral("home")), QStringLiteral("Home"));
    const int report = tabBar.addTab(pdf::PDFThemeIcons::getIcon(QStringLiteral("file")), QStringLiteral("Report.pdf"));
    tabBar.setTabClosable(report, true);
    tabBar.resize(600, 38);
    tabBar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabBar));

    QVERIFY(tabBar.getTabLockRect(report).isEmpty());
    QVERIFY(tabBar.getTabLockRect(-1).isEmpty());
    QVERIFY(tabBar.getTabLockRect(42).isEmpty());
    QCOMPARE(tabBar.getLockAt(tabBar.tabRect(report).center()), -1);

    tabBar.setTabLocked(report, true);
    const QRect lock = tabBar.getTabLockRect(report);
    QCOMPARE(lock.size(), QSize(12, 12));
    QVERIFY(tabBar.tabRect(report).contains(lock));
    QCOMPARE(tabBar.getLockAt(lock.center()), report);
    QCOMPARE(tabBar.getLockAt(lock.topLeft() - QPoint(3, 3)), report);
    QCOMPARE(tabBar.getLockAt(lock.topLeft() - QPoint(6, 6)), -1);

    // The lock is drawn in the rectangle (amber)
    const QImage image = renderWidget(&tabBar, 1.0);
    QVERIFY(countPixels(image, lock, token(Color::AccentAmberIcon), 60) > 0);

    QSignalSpy spy(&tabBar, &pdf::PDFDocumentTabBar::lockClicked);
    QTest::mouseClick(&tabBar, Qt::LeftButton, Qt::NoModifier, lock.center());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.front().front().toInt(), report);
    QCOMPARE(tabBar.currentIndex(), report);

    // A release outside of the lock, the right button, a click on the label: nothing
    QTest::mousePress(&tabBar, Qt::LeftButton, Qt::NoModifier, lock.center());
    QTest::mouseRelease(&tabBar, Qt::LeftButton, Qt::NoModifier, tabBar.tabRect(home).center());
    QTest::mouseClick(&tabBar, Qt::RightButton, Qt::NoModifier, lock.center());
    QTest::mouseClick(&tabBar, Qt::LeftButton, Qt::NoModifier, tabBar.tabRect(report).topLeft() + QPoint(12, 18));
    QCOMPARE(spy.count(), 1);

    // An unsaved tab: the dot follows the lock; right-to-left mirrors them
    tabBar.setTabUnsaved(report, true);
    QCOMPARE(tabBar.getTabLockRect(report).size(), QSize(12, 12));
    tabBar.setLayoutDirection(Qt::RightToLeft);
    QCoreApplication::processEvents();
    const QRect mirrored = tabBar.getTabLockRect(report);
    QVERIFY(tabBar.tabRect(report).contains(mirrored));
    QTest::mouseClick(&tabBar, Qt::LeftButton, Qt::NoModifier, mirrored.center());
    QCOMPARE(spy.count(), 2);

    // The tab bar deleted from the slot of the signal
    QPointer<pdf::PDFDocumentTabBar> deleted = new pdf::PDFDocumentTabBar();
    deleted->addTab(QStringLiteral("Locked.pdf"));
    deleted->setTabLocked(0, true);
    deleted->resize(300, 38);
    deleted->show();
    QVERIFY(QTest::qWaitForWindowExposed(deleted.data()));
    QObject::connect(deleted.data(), &pdf::PDFDocumentTabBar::lockClicked, deleted.data(), [deleted]() { delete deleted.data(); });
    QTest::mouseClick(deleted.data(), Qt::LeftButton, Qt::NoModifier, deleted->getTabLockRect(0).center());
    QVERIFY(!deleted);
}

void FluentComponentsTest::documentTabPassword()
{
    // The tab bar with its protected functions
    class OptionTabBar : public pdf::PDFDocumentTabBar
    {
    public:
        using pdf::PDFDocumentTabBar::initStyleOption;
        using pdf::PDFDocumentTabBar::tabSizeHint;
    };

    OptionTabBar tabBar;
    const int home = tabBar.addTab(pdf::PDFThemeIcons::getIcon(QStringLiteral("home")), QStringLiteral("Home"));
    const int report = tabBar.addTab(pdf::PDFThemeIcons::getIcon(QStringLiteral("file")), QStringLiteral("Report.pdf"));
    const int bare = tabBar.addTab(QStringLiteral("Bare.pdf"));
    tabBar.setTabClosable(report, true);
    tabBar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabBar));

    QVERIFY(!tabBar.isTabPasswordProtected(report));
    QVERIFY(!tabBar.isTabPasswordProtected(-1));
    QVERIFY(!tabBar.isTabPasswordProtected(42));
    tabBar.setTabPasswordProtected(42, true);
    tabBar.setTabPasswordProtected(-1, true);

    // The icon password replaces the icon of the tab, the icon of the tab is kept
    const int reportWidth = tabBar.tabSizeHint(report).width();
    tabBar.setTabPasswordProtected(report, true);
    QVERIFY(tabBar.isTabPasswordProtected(report));
    QCOMPARE(tabBar.tabSizeHint(report).width(), reportWidth);
    QStyleOptionTab option;
    tabBar.initStyleOption(&option, report);
    QCOMPARE(pdf::PDFThemeIcons::getIconName(option.icon), QStringLiteral("password"));
    QCOMPARE(pdf::PDFThemeIcons::getIconName(tabBar.tabIcon(report)), QStringLiteral("file"));
    tabBar.initStyleOption(nullptr, report);

    // Assistive technologies hear it
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(&tabBar);
    QVERIFY(accessible);
    QCOMPARE(accessible->child(report)->text(QAccessible::Name), QStringLiteral("Report.pdf, password protected"));
    tabBar.setTabLocked(report, true);
    QCOMPARE(accessible->child(report)->text(QAccessible::Name), QStringLiteral("Report.pdf, password protected, locked"));

    // A tab without an icon gets the place of the icon
    const int bareWidth = tabBar.tabSizeHint(bare).width();
    tabBar.setTabPasswordProtected(bare, true);
    QCOMPARE(tabBar.tabSizeHint(bare).width(), bareWidth + 14 + 8);
    tabBar.setTabPasswordProtected(bare, false);
    QCOMPARE(tabBar.tabSizeHint(bare).width(), bareWidth);

    // The state moves with the tab
    tabBar.moveTab(report, home);
    QVERIFY(tabBar.isTabPasswordProtected(home));
    QVERIFY(!tabBar.isTabPasswordProtected(report));

    // Turning the protection off shows the icon of the tab again
    tabBar.setTabPasswordProtected(home, false);
    tabBar.initStyleOption(&option, home);
    QCOMPARE(pdf::PDFThemeIcons::getIconName(option.icon), QStringLiteral("file"));
    QCOMPARE(accessible->child(home)->text(QAccessible::Name), QStringLiteral("Report.pdf, locked"));

    // The protected tab is drawn with the icon password in both themes without a leak
    tabBar.setTabPasswordProtected(home, true);
    tabBar.setCurrentIndex(home);
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        const QImage image = renderWidget(&tabBar, 2.0);
        QCOMPARE(countLeakPixels(image), 0);
        const QRect tab = tabBar.tabRect(home);
        QVERIFY(countPixels(image, QRect(tab.left() + 16, tab.top() + 6 + 9, 14, 14), token(Color::IconAccent), 40) > 0);
    }

    // A renamed document (Save As of a protected document) is announced by its new name
    tabBar.setTabText(home, QStringLiteral("Copy.pdf"));
    QCOMPARE(accessible->child(home)->text(QAccessible::Name), QStringLiteral("Copy.pdf, password protected, locked"));
    tabBar.setTabPasswordProtected(home, false);
    tabBar.setTabLocked(home, false);
    tabBar.setTabText(home, QStringLiteral("Final.pdf"));
    QCOMPARE(accessible->child(home)->text(QAccessible::Name), QStringLiteral("Final.pdf"));

    // A tab inserted or removed before a tab with a state: QTabBar lays the
    // tabs out before tabInserted / tabRemoved, the widths follow the states
    OptionTabBar states;
    states.resize(1200, 38);
    states.addTab(pdf::PDFThemeIcons::getIcon(QStringLiteral("home")), QStringLiteral("Home"));
    states.addTab(QStringLiteral("Bare.pdf"));
    states.setTabPasswordProtected(1, true);
    states.setTabLocked(1, true);
    states.show();
    QVERIFY(QTest::qWaitForWindowExposed(&states));
    QAccessibleInterface* statesAccessible = QAccessible::queryAccessibleInterface(&states);
    QVERIFY(statesAccessible);

    states.insertTab(1, pdf::PDFThemeIcons::getIcon(QStringLiteral("file")), QStringLiteral("New.pdf"));
    for (int i = 0; i < states.count(); ++i)
    {
        QCOMPARE(states.tabRect(i).width(), states.tabSizeHint(i).width());
    }
    QVERIFY(!states.isTabPasswordProtected(1));
    QVERIFY(states.isTabPasswordProtected(2));
    QCOMPARE(statesAccessible->child(1)->text(QAccessible::Name), QStringLiteral("New.pdf"));
    QCOMPARE(statesAccessible->child(2)->text(QAccessible::Name), QStringLiteral("Bare.pdf, password protected, locked"));

    states.removeTab(1);
    for (int i = 0; i < states.count(); ++i)
    {
        QCOMPARE(states.tabRect(i).width(), states.tabSizeHint(i).width());
    }
    QCOMPARE(statesAccessible->child(1)->text(QAccessible::Name), QStringLiteral("Bare.pdf, password protected, locked"));
}

void FluentComponentsTest::pageElementPixels_data()
{
    pixelProbes_data();
}

void FluentComponentsTest::pageElementPixels()
{
    QFETCH(QString, theme);
    QFETCH(qreal, scale);
    QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));

    const QColor surface = token(Color::BgSurface);
    const Painter::State none = state(QStyle::State_None);
    const Painter::State rtl = state(QStyle::State_None, Qt::RightToLeft);
    const Painter::State disabled(QStyle::State_None);

    // Nearer to the first token than to the second one
    auto isCloser = [](const QColor& color, const QColor& expected, const QColor& other)
    {
        return colorDistance(color, expected) < colorDistance(color, other);
    };

    // Icon badge: a circle of the fill with the icon, the corners stay the surface
    Painter::IconBadge badge;
    badge.iconName = QStringLiteral("check");
    badge.extent = 40;
    badge.iconExtent = 20;
    badge.fill = Color::AccentBlueBg;
    badge.iconColor = Color::AccentBlueIcon;
    QImage image = render([&](QPainter& painter) { Painter::paintIconBadge(&painter, QRectF(0, 0, 40, 40), badge, none); }, QSize(40, 40), scale);
    const QColor blueFill = blend(token(Color::AccentBlueBg), surface);
    QVERIFY(isNear(pixel(image, 5, 20), blueFill));
    QVERIFY(isNear(pixel(image, 1, 1), surface));
    QVERIFY(isCloser(strongestInk(image, QRect(10, 10, 20, 20), blueFill), token(Color::AccentBlueIcon), blueFill));

    // A box of the radius 0 with a border, the badge keeps its size in a larger rectangle
    badge.radius = 0.0;
    badge.border = Color::AccentBlueBorder;
    image = render([&](QPainter& painter) { Painter::paintIconBadge(&painter, QRectF(0, 0, 60, 40), badge, none); }, QSize(60, 40), scale);
    QVERIFY(isNear(pixel(image, 2, 20), surface));
    QVERIFY(isNear(pixel(image, 10, 20), blend(token(Color::AccentBlueBorder), surface)));
    QVERIFY(isNear(pixel(image, 12, 2), blueFill));
    QVERIFY(isNear(pixel(image, 55, 20), surface));

    // Disabled: the icon icon/disabled; without the fill only the icon is drawn
    image = render([&](QPainter& painter) { Painter::paintIconBadge(&painter, QRectF(0, 0, 60, 40), badge, disabled); }, QSize(60, 40), scale);
    QVERIFY(isCloser(strongestInk(image, QRect(20, 10, 20, 20), blueFill), token(Color::IconDisabled), token(Color::AccentBlueIcon)));
    badge.fill = std::nullopt;
    badge.border = std::nullopt;
    image = render([&](QPainter& painter) { Painter::paintIconBadge(&painter, QRectF(0, 0, 40, 40), badge, none); }, QSize(40, 40), scale);
    QVERIFY(isNear(pixel(image, 2, 2), surface));
    QVERIFY(isCloser(strongestInk(image, QRect(10, 10, 20, 20), surface), token(Color::AccentBlueIcon), surface));

    // The logo of the application instead of the icon
    badge.logo = true;
    badge.fill = Color::BgSurface;
    image = render([&](QPainter& painter) { Painter::paintIconBadge(&painter, QRectF(0, 0, 40, 40), badge, none); }, QSize(40, 40), scale);
    QVERIFY(colorDistance(strongestInk(image, QRect(10, 10, 20, 20), surface), surface) > 60);

    // Status badge: the background and the border of the hue, the icon at the leading edge (mirrored)
    const Painter::StatusBadge status{ QStringLiteral("Up to date"), QStringLiteral("check"), pdf::PDFAccentHue::Green };
    const QSize statusSize = Painter::getStatusBadgeSize(status);
    QCOMPARE(statusSize.height(), 21);
    image = render([&](QPainter& painter) { Painter::paintStatusBadge(&painter, QRectF(QPointF(0, 0), QSizeF(statusSize)), status, none); }, statusSize, scale);
    const QColor greenFill = blend(token(Color::AccentGreenBg), surface);
    QVERIFY(isNear(pixel(image, statusSize.width() / 2, 0), blend(token(Color::AccentGreenBorder), surface)));
    QVERIFY(isNear(pixel(image, 22, 2), greenFill));
    QVERIFY(isCloser(strongestInk(image, QRect(9, 4, 12, 13), greenFill), token(Color::AccentGreenIcon), greenFill));
    QVERIFY(isCloser(strongestInk(image, QRect(26, 3, statusSize.width() - 36, 15), greenFill), token(Color::AccentGreenIcon), greenFill));
    image = render([&](QPainter& painter) { Painter::paintStatusBadge(&painter, QRectF(QPointF(0, 0), QSizeF(statusSize)), status, rtl); }, statusSize, scale);
    QVERIFY(isCloser(strongestInk(image, QRect(statusSize.width() - 21, 4, 12, 13), greenFill), token(Color::AccentGreenIcon), greenFill));

    // Fact pill: bg/surface with the border, the icon icon/muted, the label text/secondary
    const Painter::FactPill pill{ QStringLiteral("shield"), QStringLiteral("MIT License") };
    const QSize pillSize = Painter::getFactPillSize(pill);
    QCOMPARE(pillSize.height(), 25);
    // pill-Open source 111:2227: the border 1 and the paddings 9 / 11 inside of it, the
    // icon 13 and the gap 6 (review R2-D2)
    QCOMPARE(pillSize.width(), 1 + 9 + 13 + 6 + qCeil(pdf::PDFStylePainter::textWidth(pill.text, pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::SmallMedium))) + 11 + 1);
    image = render([&](QPainter& painter) { Painter::paintFactPill(&painter, QRectF(QPointF(0, 0), QSizeF(pillSize)), pill, none); }, pillSize, scale);
    QVERIFY(isNear(pixel(image, pillSize.width() / 2, 0), blend(token(Color::BorderDefault), surface)));
    QVERIFY(isNear(pixel(image, 24, 3), surface));
    QVERIFY(isCloser(strongestInk(image, QRect(9, 6, 13, 13), surface), token(Color::IconMuted), surface));
    QVERIFY(isCloser(strongestInk(image, QRect(28, 4, pillSize.width() - 39, 17), surface), token(Color::TextSecondary), surface));

    // Avatar: the circle of the hue with its border and the initials
    const Painter::Avatar avatar{ QStringLiteral("JM"), 56, 18, pdf::PDFAccentHue::Blue };
    image = render([&](QPainter& painter) { Painter::paintAvatar(&painter, QRectF(0, 0, 56, 56), avatar, none); }, QSize(56, 56), scale);
    QVERIFY(isNear(pixel(image, 1, 1), surface));
    QVERIFY(isNear(pixel(image, 28, 0), blend(token(Color::AccentBlueBorder), surface)));
    QVERIFY(isNear(pixel(image, 8, 28), blueFill));
    QVERIFY(isCloser(strongestInk(image, QRect(14, 16, 28, 24), blueFill), token(Color::AccentBlueIcon), blueFill));

    // Status row: the dot of an active item bg/accent, of another one border/strong, the tag at the trailing edge
    Painter::StatusRow row{ QStringLiteral("The new application"), QStringLiteral("One window"), QStringLiteral("In progress"), true };
    image = render([&](QPainter& painter) { Painter::paintStatusRow(&painter, QRectF(0, 0, 400, 46), row, none); }, QSize(400, 46), scale);
    QVERIFY(isNear(pixel(image, 14, 23), token(Color::BgAccent)));
    QVERIFY(isCloser(strongestInk(image, QRect(28, 8, 120, 16), surface), token(Color::TextPrimary), surface));
    const int tagWidth = Painter::getTagSize(Painter::Tag{ row.status }).width();
    QVERIFY(colorDistance(strongestInk(image, QRect(400 - 8 - tagWidth, 12, tagWidth, 21), surface), surface) > 30);
    QVERIFY(isNear(pixel(image, 300, 23), surface));
    row.active = false;
    image = render([&](QPainter& painter) { Painter::paintStatusRow(&painter, QRectF(0, 0, 400, 46), row, rtl); }, QSize(400, 46), scale);
    QVERIFY(isNear(pixel(image, 400 - 15, 23), token(Color::BorderStrong)));
    QVERIFY(colorDistance(strongestInk(image, QRect(8, 12, tagWidth, 21), surface), surface) > 30);

    // Tinted card: the hue with its border, without a hue bg/surface with border/default
    image = render([&](QPainter& painter) { Painter::paintTintedCard(&painter, QRectF(0, 0, 100, 60), pdf::PDFAccentHue::Green); }, QSize(100, 60), scale);
    QVERIFY(isNear(pixel(image, 50, 30), greenFill));
    QVERIFY(isNear(pixel(image, 50, 0), blend(token(Color::AccentGreenBorder), surface)));
    image = render([&](QPainter& painter) { painter.fillRect(QRectF(0, 0, 100, 60), token(Color::BgCanvas)); Painter::paintTintedCard(&painter, QRectF(0, 0, 100, 60), std::nullopt); }, QSize(100, 60), scale);
    QVERIFY(isNear(pixel(image, 50, 30), surface));
    QVERIFY(isNear(pixel(image, 0, 0), token(Color::BgCanvas)));

    // The widgets draw their painter functions (children: a window has the background bg/canvas)
    QWidget holder;
    pdf::PDFIconBadge iconBadge(QStringLiteral("download"), 40, 20, &holder);
    iconBadge.setHue(pdf::PDFAccentHue::Blue);
    iconBadge.resize(iconBadge.sizeHint());
    QString difference = describeDifference(renderWidget(&iconBadge, scale), renderInWidget([&](QPainter& painter) { Painter::paintIconBadge(&painter, QRectF(0, 0, 40, 40), iconBadge.getBadge(), none); }, QSize(40, 40), scale), 3);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
    pdf::PDFStatusBadge statusBadge(QStringLiteral("Up to date"), QStringLiteral("check"), pdf::PDFAccentHue::Green, &holder);
    statusBadge.resize(statusBadge.sizeHint());
    difference = describeDifference(renderWidget(&statusBadge, scale), renderInWidget([&](QPainter& painter) { Painter::paintStatusBadge(&painter, QRectF(QPointF(0, 0), QSizeF(statusSize)), status, none); }, statusSize, scale), 3);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
    pdf::PDFFactPill factPill(QStringLiteral("shield"), QStringLiteral("MIT License"), &holder);
    factPill.resize(factPill.sizeHint());
    difference = describeDifference(renderWidget(&factPill, scale), renderInWidget([&](QPainter& painter) { Painter::paintFactPill(&painter, QRectF(QPointF(0, 0), QSizeF(pillSize)), pill, none); }, pillSize, scale), 3);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
    pdf::PDFAvatar avatarWidget(QStringLiteral("JM"), 56, 18, &holder);
    avatarWidget.resize(avatarWidget.sizeHint());
    difference = describeDifference(renderWidget(&avatarWidget, scale), renderInWidget([&](QPainter& painter) { Painter::paintAvatar(&painter, QRectF(0, 0, 56, 56), avatar, none); }, QSize(56, 56), scale), 3);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
    pdf::PDFStatusRow statusRow(row.title, row.subtitle, row.status, false, &holder);
    statusRow.resize(400, 46);
    difference = describeDifference(renderWidget(&statusRow, scale), renderInWidget([&](QPainter& painter) { Painter::paintStatusRow(&painter, QRectF(0, 0, 400, 46), statusRow.getRow(), none); }, QSize(400, 46), scale), 3);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
    pdf::PDFTintedCard tintedCard(pdf::PDFAccentHue::Green, &holder);
    tintedCard.resize(100, 60);
    difference = describeDifference(renderWidget(&tintedCard, scale), renderInWidget([&](QPainter& painter) { Painter::paintTintedCard(&painter, QRectF(0, 0, 100, 60), pdf::PDFAccentHue::Green); }, QSize(100, 60), scale), 3);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));

    // No leak color in any of them
    for (QWidget* widget : std::initializer_list<QWidget*>{ &iconBadge, &statusBadge, &factPill, &avatarWidget, &statusRow, &tintedCard })
    {
        QCOMPARE(countLeakPixels(renderWidget(widget, scale)), 0);
    }
}

void FluentComponentsTest::pageElementWidgets()
{
    // Icon badge: sizes, setters, the colors of a hue, defensive values
    pdf::PDFIconBadge badge;
    QCOMPARE(badge.sizeHint(), QSize(40, 40));
    QCOMPARE(badge.minimumSizeHint(), badge.sizeHint());
    QVERIFY(badge.testAttribute(Qt::WA_TransparentForMouseEvents));
    QCOMPARE(badge.getRadius(), -1.0);
    badge.setExtent(-5);
    QCOMPARE(badge.getExtent(), 0);
    QCOMPARE(badge.sizeHint(), QSize(0, 0));
    badge.setExtent(100000);
    QCOMPARE(badge.sizeHint(), QSize(1024, 1024));
    badge.setExtent(72);
    badge.setIconExtent(-1);
    QCOMPARE(badge.getIconExtent(), 0);
    badge.setIconExtent(32);
    badge.setIconName(QStringLiteral("clock"));
    QCOMPARE(badge.getIconName(), QStringLiteral("clock"));
    badge.setRadius(std::numeric_limits<qreal>::quiet_NaN());
    QCOMPARE(badge.getRadius(), -1.0);
    badge.setRadius(12.0);
    QCOMPARE(badge.getRadius(), 12.0);
    badge.setHue(pdf::PDFAccentHue::Violet);
    QCOMPARE(badge.getFillColor(), std::optional<Color>(Color::AccentVioletBg));
    QCOMPARE(badge.getIconColor(), Color::AccentVioletIcon);
    QVERIFY(!badge.getBorderColor().has_value());
    badge.setHue(static_cast<pdf::PDFAccentHue>(1000));
    QCOMPARE(badge.getFillColor(), std::optional<Color>(Color::AccentNeutralBg));
    badge.setColors(std::nullopt, Color::IconDanger, Color::BorderDefault);
    QVERIFY(!badge.getFillColor().has_value());
    QCOMPARE(badge.getBorderColor(), std::optional<Color>(Color::BorderDefault));
    badge.setLogo(true);
    QVERIFY(badge.isLogo());
    QCOMPARE(badge.getBadge().extent, 72);

    // Setting the same values changes nothing
    badge.setLogo(true);
    badge.setExtent(72);
    badge.setIconExtent(32);
    badge.setIconName(QStringLiteral("clock"));
    badge.setRadius(12.0);
    QCOMPARE(badge.getBadge().radius, 12.0);

    // Defensive drawing: a null painter, empty and NaN rectangles, invalid colors and huge values
    Painter::IconBadge hostile;
    hostile.extent = std::numeric_limits<int>::max();
    hostile.iconExtent = std::numeric_limits<int>::min();
    hostile.radius = std::numeric_limits<qreal>::infinity();
    hostile.fill = static_cast<Color>(-3);
    hostile.border = static_cast<Color>(100000);
    hostile.iconColor = static_cast<Color>(-1);
    hostile.iconName = QStringLiteral("no-such-icon");
    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    QImage image = render([&](QPainter& painter)
    {
        Painter::paintIconBadge(nullptr, QRectF(0, 0, 10, 10), hostile, state(QStyle::State_None));
        Painter::paintIconBadge(&painter, QRectF(), hostile, state(QStyle::State_None));
        Painter::paintIconBadge(&painter, QRectF(0, 0, nan, 10), hostile, state(QStyle::State_None));
        Painter::paintIconBadge(&painter, QRectF(0, 0, 20, 20), hostile, state(QStyle::State_None));
        hostile.iconExtent = 10;
        Painter::paintIconBadge(&painter, QRectF(0, 0, 20, 20), hostile, state(QStyle::State_None));
        Painter::paintStatusBadge(nullptr, QRectF(0, 0, 10, 10), Painter::StatusBadge(), state(QStyle::State_None));
        Painter::paintStatusBadge(&painter, QRectF(0, 0, -5, 10), Painter::StatusBadge(), state(QStyle::State_None));
        Painter::paintStatusBadge(&painter, QRectF(0, 0, 8, 21), Painter::StatusBadge{ QStringLiteral("Long text"), QString(), static_cast<pdf::PDFAccentHue>(-1) }, state(QStyle::State_None));
        Painter::paintFactPill(nullptr, QRectF(0, 0, 10, 10), Painter::FactPill(), state(QStyle::State_None));
        Painter::paintFactPill(&painter, QRectF(0, 0, 5, 25), Painter::FactPill{ QString(), QStringLiteral("Text") }, state(QStyle::State_None));
        Painter::paintAvatar(nullptr, QRectF(0, 0, 10, 10), Painter::Avatar(), state(QStyle::State_None));
        Painter::paintAvatar(&painter, QRectF(0, 0, 30, 30), Painter::Avatar{ QStringLiteral("ABCDEFG"), -10, -10, pdf::PDFAccentHue::Blue }, state(QStyle::State_None));
        Painter::paintAvatar(&painter, QRectF(0, 0, 30, 30), Painter::Avatar{ QStringLiteral("ABCDEFG"), 30, 100000, static_cast<pdf::PDFAccentHue>(77) }, state(QStyle::State_None));
        Painter::paintStatusRow(nullptr, QRectF(0, 0, 10, 10), Painter::StatusRow(), state(QStyle::State_None));
        Painter::paintStatusRow(&painter, QRectF(0, 0, 20, 46), Painter::StatusRow{ QStringLiteral("Title"), QStringLiteral("Subtitle"), QStringLiteral("A long status"), true }, state(QStyle::State_None));
        Painter::paintStatusRow(&painter, QRectF(0, 0, 200, 46), Painter::StatusRow{ QStringLiteral("Title"), QString(), QString(), false }, Painter::State(QStyle::State_None));
        Painter::paintTintedCard(nullptr, QRectF(0, 0, 10, 10), std::nullopt);
        Painter::paintTintedCard(&painter, QRectF(), pdf::PDFAccentHue::Blue);
        Painter::paintTintedCard(&painter, QRectF(0, 0, 20, 20), static_cast<pdf::PDFAccentHue>(99));
    }, QSize(40, 40), 1.0);
    QCOMPARE(countLeakPixels(image), 0);
    QCOMPARE(Painter::getIconBadgeSize(hostile), QSize(1024, 1024));
    QCOMPARE(Painter::getAvatarSize(Painter::Avatar{ QString(), -1, 1, pdf::PDFAccentHue::Blue }), QSize(0, 0));

    // Status badge: the size follows the text, the accessible name is the text (also after a change)
    pdf::PDFStatusBadge statusBadge;
    QCOMPARE(statusBadge.sizeHint().height(), 21);
    statusBadge.setBadge(QStringLiteral("Up to date"), QStringLiteral("check"), pdf::PDFAccentHue::Green);
    const int width = statusBadge.sizeHint().width();
    QCOMPARE(statusBadge.accessibleName(), QStringLiteral("Up to date"));
    statusBadge.setBadge(QStringLiteral("Update available"), QStringLiteral("download"), pdf::PDFAccentHue::Amber);
    QVERIFY(statusBadge.sizeHint().width() > width);
    QCOMPARE(statusBadge.accessibleName(), QStringLiteral("Update available"));
    QCOMPARE(statusBadge.getHue(), pdf::PDFAccentHue::Amber);
    QCOMPARE(statusBadge.getIconName(), QStringLiteral("download"));
    QCOMPARE(statusBadge.getText(), QStringLiteral("Update available"));
    QCOMPARE(statusBadge.minimumSizeHint(), statusBadge.sizeHint());
    statusBadge.setAccessibleName(QStringLiteral("Own name"));
    statusBadge.setBadge(QStringLiteral("Previous release"), QStringLiteral("clock"), static_cast<pdf::PDFAccentHue>(-5));
    QCOMPARE(statusBadge.accessibleName(), QStringLiteral("Own name"));
    QCOMPARE(statusBadge.getHue(), pdf::PDFAccentHue::Neutral);

    // Fact pill
    pdf::PDFFactPill pill(QStringLiteral("github"), QStringLiteral("Open source"));
    QCOMPARE(pill.sizeHint().height(), 25);
    QCOMPARE(pill.accessibleName(), QStringLiteral("Open source"));
    const int pillWidth = pill.sizeHint().width();
    pill.setText(QStringLiteral("Open source software"));
    QVERIFY(pill.sizeHint().width() > pillWidth);
    QCOMPARE(pill.accessibleName(), QStringLiteral("Open source software"));
    pill.setText(QStringLiteral("Open source software"));
    pill.setIconName(QStringLiteral("shield"));
    QCOMPARE(pill.getIconName(), QStringLiteral("shield"));
    QCOMPARE(pill.getPill().text, pill.getText());
    QCOMPARE(pill.minimumSizeHint(), pill.sizeHint());
    pill.setIconName(QStringLiteral("shield"));
    pill.setAccessibleName(QStringLiteral("Own name"));
    pill.setText(QStringLiteral("Another fact"));
    QCOMPARE(pill.accessibleName(), QStringLiteral("Own name"));
    pdf::PDFFactPill emptyPill;
    QCOMPARE(emptyPill.sizeHint().height(), 25);

    // Avatar
    pdf::PDFAvatar avatar(QStringLiteral("JM"), 56, 18);
    QCOMPARE(avatar.sizeHint(), QSize(56, 56));
    QCOMPARE(avatar.minimumSizeHint(), QSize(56, 56));
    avatar.setExtent(40);
    avatar.setFontPixelSize(0);
    QCOMPARE(avatar.getFontPixelSize(), 1);
    avatar.setFontPixelSize(14);
    avatar.setInitials(QStringLiteral("AB"));
    avatar.setHue(pdf::PDFAccentHue::Green);
    QCOMPARE(avatar.getAvatar().initials, QStringLiteral("AB"));
    QCOMPARE(avatar.getHue(), pdf::PDFAccentHue::Green);
    QCOMPARE(avatar.sizeHint(), QSize(40, 40));
    QCOMPARE(avatar.getExtent(), 40);
    QCOMPARE(avatar.getInitials(), QStringLiteral("AB"));
    avatar.setExtent(40);
    avatar.setFontPixelSize(14);
    avatar.setInitials(QStringLiteral("AB"));
    avatar.setHue(pdf::PDFAccentHue::Green);
    QCOMPARE(avatar.getFontPixelSize(), 14);

    // Status row: sizes, accessibility (name = title, description = subtitle and status)
    pdf::PDFStatusRow row(QStringLiteral("The new application"), QStringLiteral("One window"), QStringLiteral("In progress"), true);
    QCOMPARE(row.sizeHint().height(), 46);
    QVERIFY(row.minimumSizeHint().width() < row.sizeHint().width());
    QCOMPARE(row.accessibleName(), QStringLiteral("The new application"));
    QCOMPARE(row.accessibleDescription(), QStringLiteral("One window, In progress"));
    row.setStatus(QString());
    QCOMPARE(row.accessibleDescription(), QStringLiteral("One window"));
    const int rowWidth = row.minimumSizeHint().width();
    row.setStatus(QStringLiteral("Planned"));
    QVERIFY(row.minimumSizeHint().width() > rowWidth);
    row.setTitle(QStringLiteral("Accessibility"));
    row.setSubtitle(QStringLiteral("Tagged PDF"));
    row.setActive(false);
    QVERIFY(!row.isActive());
    QCOMPARE(row.accessibleName(), QStringLiteral("Accessibility"));
    QCOMPARE(row.accessibleDescription(), QStringLiteral("Tagged PDF, Planned"));
    row.setAccessibleName(QStringLiteral("Own"));
    row.setTitle(QStringLiteral("Another"));
    QCOMPARE(row.accessibleName(), QStringLiteral("Own"));
    QCOMPARE(row.getTitle(), QStringLiteral("Another"));
    QCOMPARE(row.getSubtitle(), QStringLiteral("Tagged PDF"));
    QCOMPARE(row.getStatus(), QStringLiteral("Planned"));
    row.setAccessibleDescription(QStringLiteral("Own description"));
    row.setSubtitle(QStringLiteral("Other subtitle"));
    QCOMPARE(row.accessibleDescription(), QStringLiteral("Own description"));
    row.setTitle(QStringLiteral("Another"));
    row.setSubtitle(QStringLiteral("Other subtitle"));
    row.setStatus(QStringLiteral("Planned"));
    row.setActive(false);
    QCOMPARE(row.getRow().subtitle, QStringLiteral("Other subtitle"));

    // Tinted card
    pdf::PDFTintedCard card;
    QVERIFY(!card.getHue().has_value());
    card.setHue(pdf::PDFAccentHue::Blue);
    QCOMPARE(card.getHue(), std::optional<pdf::PDFAccentHue>(pdf::PDFAccentHue::Blue));
    card.setHue(static_cast<pdf::PDFAccentHue>(55));
    QCOMPARE(card.getHue(), std::optional<pdf::PDFAccentHue>(pdf::PDFAccentHue::Neutral));
    card.setHue(std::nullopt);
    card.setHue(std::nullopt);
    QVERIFY(!card.getHue().has_value());

    // The widgets follow the theme, the direction and the enabled state without a leak color
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    layout->addWidget(new pdf::PDFIconBadge(QStringLiteral("check"), 32, 16, &window));
    layout->addWidget(new pdf::PDFStatusBadge(QStringLiteral("Up to date"), QStringLiteral("check"), pdf::PDFAccentHue::Green, &window));
    layout->addWidget(new pdf::PDFFactPill(QStringLiteral("shield"), QStringLiteral("MIT"), &window));
    layout->addWidget(new pdf::PDFAvatar(QStringLiteral("JM"), 40, 14, &window));
    layout->addWidget(new pdf::PDFStatusRow(QStringLiteral("Title"), QStringLiteral("Subtitle"), QStringLiteral("Planned"), true, &window));
    layout->addWidget(new pdf::PDFTintedCard(pdf::PDFAccentHue::Blue, &window));
    window.resize(900, 60);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto checkChildren = [&]()
    {
        for (QWidget* child : window.findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
        {
            QCOMPARE(countLeakPixels(renderWidget(child, 1.0)), 0);
        }
    };
    for (const char* theme : { "dark", "light" })
    {
        QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(QString::fromLatin1(theme)));
        window.setLayoutDirection(Qt::RightToLeft);
        window.setEnabled(false);
        window.setFont(pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Small));
        checkChildren();
        window.setEnabled(true);
        window.setLayoutDirection(Qt::LeftToRight);
        checkChildren();
    }
}

void FluentComponentsTest::recentStates()
{
    QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light")));
    const QColor surface = token(Color::BgSurface);
    const QColor redFill = blend(token(Color::AccentRedBg), surface);

    // The badge of a missing file: accent/red/bg with text/danger
    const QSize badgeSize = Painter::getMissingFileBadgeSize();
    QCOMPARE(badgeSize.height(), 19);
    QImage image = render([&](QPainter& painter) { Painter::paintMissingFileBadge(&painter, QRectF(QPointF(0, 0), QSizeF(badgeSize)), state(QStyle::State_None)); }, badgeSize);
    QVERIFY(isNear(pixel(image, 5, 9), redFill));
    QVERIFY(countPixels(image, QRect(8, 2, badgeSize.width() - 16, 15), token(Color::TextDanger), 40) > 0);
    image = render([&](QPainter& painter)
    {
        Painter::paintMissingFileBadge(nullptr, QRectF(0, 0, 10, 10), state(QStyle::State_None));
        Painter::paintMissingFileBadge(&painter, QRectF(), state(QStyle::State_None));
    }, QSize(10, 10));
    QVERIFY(isNear(pixel(image, 5, 5), surface));

    // A locked thumbnail: bg/subtle with the lock icon/muted instead of the picture or the lines
    for (const Painter::ThumbnailSize size : { Painter::ThumbnailSize::Small, Painter::ThumbnailSize::Medium, Painter::ThumbnailSize::Large })
    {
        const QSize frame = Painter::getPageThumbnailSize(size);
        Painter::PageThumbnail thumbnail{ size, QPixmap(), true };
        image = render([&](QPainter& painter) { Painter::paintPageThumbnailFrame(&painter, QRectF(QPointF(0, 0), QSizeF(frame)), thumbnail, state(QStyle::State_None)); }, frame);
        QVERIFY(isNear(pixel(image, 3, frame.height() - 4), blend(token(Color::BgSubtle), surface)));
        const QRect center(frame.width() / 2 - 6, frame.height() / 2 - 6, 12, 12);
        QVERIFY(colorDistance(strongestInk(image, center, blend(token(Color::BgSubtle), surface)), token(Color::IconMuted)) < 60);
        image = render([&](QPainter& painter) { Painter::paintPageThumbnailFrame(&painter, QRectF(QPointF(0, 0), QSizeF(frame)), thumbnail, Painter::State(QStyle::State_None)); }, frame);
        QVERIFY(colorDistance(strongestInk(image, center, blend(token(Color::BgSubtle), surface)), token(Color::IconDisabled)) < 60);
    }

    // Recent item row: the missing file has the badge in the column of the progress
    // and no bar, the locked file the lock, an item without the star has its place empty
    Painter::RecentItemRow row;
    row.name = QStringLiteral("Building_Plan.pdf");
    row.details = QStringLiteral("D:\\Projects");
    row.progressText = QStringLiteral("Page 2 of 4");
    row.progress = 0.5;
    row.opened = QStringLiteral("Sep 3");
    const QRectF rect(0, 0, 802, 56);
    const QRectF star = Painter::getRecentItemRowStarRect(rect, state(QStyle::State_None));
    auto renderRow = [&](const Painter::RecentItemRow& content, Qt::LayoutDirection direction = Qt::LeftToRight)
    {
        return render([&](QPainter& painter) { Painter::paintRecentItemRow(&painter, rect, content, state(QStyle::State_None, direction)); }, QSize(802, 56));
    };
    image = renderRow(row);
    QVERIFY(colorDistance(strongestInk(image, star.toRect(), surface), surface) > 60);
    const QRect progressColumn(802 - 8 - 16 - 2 * 14 - 24 - 96 - 14 - 132, 0, 132, 56);
    QCOMPARE(countPixels(image, progressColumn, redFill, 3), 0);
    row.status = Painter::FileStatus::Missing;
    image = renderRow(row);
    QVERIFY(countPixels(image, progressColumn, redFill, 3) > 100);
    QCOMPARE(countPixels(image, progressColumn, token(Color::AccentGreenIcon), 3), 0);
    QVERIFY(countPixels(image, QRect(60, 8, 120, 20), token(Color::TextMuted), 40) > 0);
    QCOMPARE(countPixels(image, QRect(60, 8, 120, 20), token(Color::TextPrimary), 10), 0);
    image = renderRow(row, Qt::RightToLeft);
    QVERIFY(countPixels(image, QRect(802 - progressColumn.right() - 1, 0, 132, 56), redFill, 3) > 100);
    row.status = static_cast<Painter::FileStatus>(9);
    image = renderRow(row);
    QCOMPARE(countPixels(image, progressColumn, redFill, 3), 0);
    row.status = Painter::FileStatus::Available;
    row.starVisible = false;
    image = renderRow(row);
    QVERIFY(colorDistance(strongestInk(image, star.toRect(), surface), surface) < 10);
    row.starVisible = true;
    row.locked = true;
    image = renderRow(row);
    QVERIFY(isNear(pixel(image, 14, 28 + 12), blend(token(Color::BgSubtle), surface)));

    // Pinned card: the missing file has the badge at the bottom instead of the progress,
    // the locked one the lock in its thumbnail
    Painter::PinnedCard card;
    card.name = QStringLiteral("Project_Report.pdf");
    card.path = QStringLiteral("C:\\Users\\Jakub");
    card.progressText = QStringLiteral("Page 12 of 24");
    card.progress = 0.5;
    auto renderCard = [&](const Painter::PinnedCard& content) { return render([&](QPainter& painter) { Painter::paintPinnedCard(&painter, QRectF(0, 0, 259, 100), content, state(QStyle::State_None)); }, QSize(259, 100)); };
    image = renderCard(card);
    QCOMPARE(countPixels(image, QRect(81, 60, 140, 30), redFill, 3), 0);
    QVERIFY(countPixels(image, QRect(81, 82, 140, 6), token(Color::BgAccent), 20) > 0);
    card.status = Painter::FileStatus::Missing;
    image = renderCard(card);
    QVERIFY(countPixels(image, QRect(81, 60, 140, 30), redFill, 3) > 100);
    QCOMPARE(countPixels(image, QRect(81, 82, 140, 6), token(Color::BgAccent), 20), 0);
    QVERIFY(countPixels(image, QRect(81, 13, 140, 16), token(Color::TextMuted), 40) > 0);
    card.status = Painter::FileStatus::Available;
    card.locked = true;
    image = renderCard(card);
    QVERIFY(isNear(pixel(image, 16, 80), blend(token(Color::BgSubtle), surface)));

    // The widget of the pinned card: status, lock and the accessible description
    pdf::PDFPinnedCard pinned(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users\\Jakub"), QStringLiteral("Page 12 of 24"), 0.5);
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(&pinned);
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("C:\\Users\\Jakub, Page 12 of 24"));
    pinned.setStatus(pdf::PDFPinnedCard::Status::Missing);
    QCOMPARE(pinned.getStatus(), pdf::PDFPinnedCard::Status::Missing);
    QCOMPARE(pinned.getContent().status, Painter::FileStatus::Missing);
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("No longer exists, C:\\Users\\Jakub"));
    pinned.setLocked(true);
    QVERIFY(pinned.isLocked());
    QVERIFY(pinned.getContent().locked);
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("No longer exists, C:\\Users\\Jakub, Protected by a password"));
    pinned.setStatus(static_cast<pdf::PDFPinnedCard::Status>(42));
    QCOMPARE(pinned.getStatus(), pdf::PDFPinnedCard::Status::Available);
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("C:\\Users\\Jakub, Page 12 of 24, Protected by a password"));
    QVERIFY(pinned.setProperty("locked", false));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("C:\\Users\\Jakub, Page 12 of 24"));
    pinned.resize(pinned.sizeHint());
    pinned.setLocked(true);
    pinned.setStatus(pdf::PDFPinnedCard::Status::Missing);
    Painter::PinnedCard pinnedContent = pinned.getContent();
    pinnedContent.paintNested = true;
    const QString difference = describeDifference(renderWidget(&pinned, 1.0), renderInWidget([&](QPainter& painter) { Painter::paintPinnedCard(&painter, QRectF(0, 0, 259, 100), pinnedContent, state(QStyle::State_None)); }, QSize(259, 100), 1.0), 3);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));

    // The delegate: the roles StarVisibleRole, MissingRole and LockedRole; an item
    // without the star has only the menu action
    using Roles = pdf::PDFComponentRoles;
    QStandardItemModel model;
    QStandardItem* file = new QStandardItem(QStringLiteral("Building_Plan.pdf"));
    file->setData(true, Roles::MissingRole);
    file->setData(true, Roles::LockedRole);
    model.appendRow(file);
    QStandardItem* workspace = new QStandardItem(QStringLiteral("Thesis review"));
    workspace->setData(static_cast<int>(Painter::RecentItemType::Workspace), Roles::ItemTypeRole);
    workspace->setData(false, Roles::StarVisibleRole);
    model.appendRow(workspace);
    QStandardItem* other = new QStandardItem(QStringLiteral("Other.pdf"));
    other->setData(QStringLiteral("yes"), Roles::StarVisibleRole);
    model.appendRow(other);
    pdf::PDFRecentItemDelegate delegate;
    QCOMPARE(delegate.getContent(model.index(0, 0)).status, Painter::FileStatus::Missing);
    QVERIFY(delegate.getContent(model.index(0, 0)).locked);
    QVERIFY(delegate.getContent(model.index(0, 0)).starVisible);
    QVERIFY(!delegate.getContent(model.index(1, 0)).starVisible);
    QCOMPARE(delegate.getContent(model.index(1, 0)).status, Painter::FileStatus::Available);
    QVERIFY(delegate.getContent(model.index(2, 0)).starVisible);

    pdf::PDFListView view;
    view.setModel(&model);
    view.setItemDelegate(&delegate);
    view.resize(830, 3 * 56 + 16);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QCOMPARE(delegate.getActions(DelegateSamples::viewOption(&view, model.index(0, 0)), model.index(0, 0)).size(), 2);
    const QList<pdf::PDFRowDelegate::Action> workspaceActions = delegate.getActions(DelegateSamples::viewOption(&view, model.index(1, 0)), model.index(1, 0));
    QCOMPARE(workspaceActions.size(), 1);
    QCOMPARE(workspaceActions.front().id, int(pdf::PDFRecentItemDelegate::MenuAction));
    QVERIFY(delegate.getActionRect(&view, model.index(1, 0), pdf::PDFRecentItemDelegate::PinAction).isEmpty());
    QSignalSpy triggered(&delegate, &pdf::PDFRowDelegate::actionTriggered);
    delegate.triggerAction(&view, model.index(1, 0), pdf::PDFRecentItemDelegate::PinAction);
    QCOMPARE(triggered.count(), 0);
    QVERIFY(!model.index(1, 0).data(Roles::PinnedRole).toBool());
    delegate.triggerAction(&view, model.index(1, 0), pdf::PDFRecentItemDelegate::MenuAction);
    QCOMPARE(triggered.count(), 1);
    view.setItemDelegate(nullptr);
}

void FluentComponentsTest::messageBoxInitialFocus()
{
    // The own row of the buttons: Qt gives the first focus to the first button
    // created - the leading one, a destructive action, which Space would take.
    // The default (Primary) button gets it; a form keeps its field (FX5-2).
    using Position = pdf::PDFMessageBox::ActionPosition;
    QWidget host;
    host.resize(800, 600);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    // A confirmation: Delete File leading, Cancel the Primary button
    {
        QPointer<pdf::PDFMessageBox> box = new pdf::PDFMessageBox(pdf::PDFThemeIcons::Badge::Warning, QStringLiteral("Delete the file?"),
                                                                  QStringLiteral("This cannot be undone."), QDialogButtonBox::NoButton, &host);
        QPushButton* remove = box->addActionButton(QStringLiteral("Delete File"), Position::Leading, false);
        QPushButton* cancel = box->addActionButton(QStringLiteral("Cancel"), Position::Trailing, true);
        QSignalSpy removeSpy(remove, &QPushButton::clicked);
        QSignalSpy cancelSpy(cancel, &QPushButton::clicked);
        box->show();
        QVERIFY(QTest::qWaitForWindowExposed(box.data()));
        QCOMPARE(box->focusWidget(), static_cast<QWidget*>(cancel));
        QVERIFY(cancel->isDefault());
        QTest::keyClick(box->focusWidget(), Qt::Key_Space);
        QCOMPARE(removeSpy.count(), 0);
        QCOMPARE(cancelSpy.count(), 1);

        // Shown again: the first focus again on the Primary button
        box->hide();
        remove->setFocus();
        box->show();
        QVERIFY(QTest::qWaitForWindowExposed(box.data()));
        QCOMPARE(box->focusWidget(), static_cast<QWidget*>(remove));   // focused by the application
        delete box.data();
    }

    // A form: its first field takes the first focus, not the Primary button
    {
        QPointer<pdf::PDFMessageBox> box = new pdf::PDFMessageBox(pdf::PDFThemeIcons::Badge::Information, QStringLiteral("Rename"), QString(),
                                                                  QDialogButtonBox::NoButton, &host);
        pdf::PDFLineEdit* field = new pdf::PDFLineEdit(QStringLiteral("Name"));
        box->addContent(field);
        box->addActionButton(QStringLiteral("Cancel"), Position::Trailing, false);
        box->addActionButton(QStringLiteral("Rename"), Position::Trailing, true);
        box->show();
        QVERIFY(QTest::qWaitForWindowExposed(box.data()));
        QTRY_COMPARE(box->focusWidget(), static_cast<QWidget*>(field));
        delete box.data();
    }

    // A check box of a confirmation is a button too (Space would toggle it)
    {
        QPointer<pdf::PDFMessageBox> box = new pdf::PDFMessageBox(pdf::PDFThemeIcons::Badge::Warning, QStringLiteral("Import?"), QString(),
                                                                  QDialogButtonBox::NoButton, &host);
        box->addContent(new QCheckBox(QStringLiteral("Also trust the certificates")));
        box->addActionButton(QStringLiteral("Import"), Position::Leading, false);
        QPushButton* cancel = box->addActionButton(QStringLiteral("Cancel"), Position::Trailing, true);
        box->show();
        QVERIFY(QTest::qWaitForWindowExposed(box.data()));
        QCOMPARE(box->focusWidget(), static_cast<QWidget*>(cancel));
        delete box.data();
    }

    // A disabled Primary button and a row without a default button keep the
    // order of Qt (the dialog does not focus a button, which cannot be pressed)
    {
        QPointer<pdf::PDFMessageBox> box = new pdf::PDFMessageBox(pdf::PDFThemeIcons::Badge::Information, QStringLiteral("Title"), QString(),
                                                                  QDialogButtonBox::NoButton, &host);
        QPushButton* first = box->addActionButton(QStringLiteral("First"), Position::Leading, false);
        QPushButton* primary = box->addActionButton(QStringLiteral("Primary"), Position::Trailing, true);
        primary->setEnabled(false);
        box->show();
        QVERIFY(QTest::qWaitForWindowExposed(box.data()));
        QVERIFY(box->focusWidget() != primary);
        Q_UNUSED(first);
        delete box.data();
    }

    // Standard buttons: the default button of the button box as before
    {
        pdf::PDFMessageBox box(pdf::PDFThemeIcons::Badge::Warning, QStringLiteral("Title"), QStringLiteral("Text"), QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &host);
        box.setDefaultButton(QDialogButtonBox::Cancel);
        box.show();
        QVERIFY(QTest::qWaitForWindowExposed(&box));
        QCOMPARE(box.focusWidget(), static_cast<QWidget*>(box.getButtonBox()->button(QDialogButtonBox::Cancel)));
    }
}

void FluentComponentsTest::messageBoxCustomContent()
{
    QWidget host;
    host.resize(800, 600);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    QPointer<pdf::PDFMessageBox> box = new pdf::PDFMessageBox(pdf::PDFThemeIcons::Badge::Information, QStringLiteral("Rename workspace"),
                                                              QStringLiteral("A text of the dialog, which is long enough to wrap in the card of the width of the frame of the design."),
                                                              QDialogButtonBox::NoButton, &host);
    QVERIFY(box->isBadgeVisible());
    box->setBadgeVisible(false);
    QVERIFY(!box->isBadgeVisible());
    box->setBadgeVisible(true);
    QVERIFY(box->isBadgeVisible());
    box->setBadgeVisible(false);

    QCOMPARE(box->getCardWidth(), 0);
    QCOMPARE(box->getContentWidth(), 0);
    box->setCardWidth(-100);
    QCOMPARE(box->getCardWidth(), 0);
    box->setCardWidth(440);
    QCOMPARE(box->getCardWidth(), 440);
    QCOMPARE(box->getContentWidth(), 440 - 2 * 23);
    box->setCardWidth(440);

    // The content lies between the head and the buttons, in the order of the calls
    pdf::PDFLineEdit* field = new pdf::PDFLineEdit(QStringLiteral("Thesis review"));
    pdf::PDFLabel* note = new pdf::PDFLabel(QStringLiteral("Note"), pdf::PDFThemeFonts::Role::Small, Color::TextMuted);
    box->addContent(field);
    box->addContent(note);
    box->addContent(nullptr);
    QCOMPARE(field->parentWidget(), box.data());
    QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(box->layout());
    QVERIFY(mainLayout);
    QVERIFY(mainLayout->indexOf(field) < mainLayout->indexOf(note));
    QCOMPARE(mainLayout->indexOf(note), mainLayout->count() - 2);

    // The own row of the buttons: leading buttons first, trailing ones in the order of the calls
    QPushButton* cancel = box->addActionButton(QStringLiteral("Cancel"), pdf::PDFMessageBox::ActionPosition::Trailing, false);
    QPushButton* rename = box->addActionButton(QStringLiteral("Rename"), pdf::PDFMessageBox::ActionPosition::Trailing, true);
    QPushButton* remove = box->addActionButton(QStringLiteral("Delete"), pdf::PDFMessageBox::ActionPosition::Leading, false);
    QPushButton* other = box->addActionButton(QStringLiteral("Other"), pdf::PDFMessageBox::ActionPosition::Leading, false);
    QCOMPARE(box->getActionButtons(), QList<QPushButton*>({ remove, other, cancel, rename }));
    QVERIFY(rename->isDefault());
    QVERIFY(!cancel->isDefault());
    QVERIFY(!rename->autoDefault());
    QVERIFY(pdf::PDFWidgetStyleProperties::isSet(rename, pdf::PDFWidgetStyleProperties::PRIMARY));
    QVERIFY(!pdf::PDFWidgetStyleProperties::isSet(cancel, pdf::PDFWidgetStyleProperties::PRIMARY));
    QVERIFY(box->getButtonBox()->isHidden());

    // The width of the card, the height of the content at it, the field keeps the focus
    field->setFocus();
    box->show();
    QVERIFY(QTest::qWaitForWindowExposed(box.data()));
    const QMargins shadow = box->getShadowMargins();
    QCOMPARE(box->width(), 440 + shadow.left() + shadow.right());
    QCOMPARE(box->sizeHint().width(), box->width());
    QVERIFY(box->height() >= box->sizeHint().height() - 1);
    QTRY_VERIFY(field->hasFocus());
    QVERIFY(remove->geometry().left() < cancel->geometry().left());
    QVERIFY(cancel->geometry().left() < rename->geometry().left());

    // An action button does not close the dialog; Escape rejects it (no button box button)
    QSignalSpy finished(box.data(), &QDialog::finished);
    rename->click();
    QVERIFY(box->isVisible());
    QCOMPARE(finished.count(), 0);
    QTest::keyClick(box.data(), Qt::Key_Escape);
    QTRY_VERIFY(!box->isVisible());
    QCOMPARE(finished.count(), 1);

    // A deleted button leaves the row; the card follows its content again
    delete other;
    QCOMPARE(box->getActionButtons(), QList<QPushButton*>({ remove, cancel, rename }));
    box->setCardWidth(0);
    QCOMPARE(box->getContentWidth(), 0);
    QCOMPARE(box->maximumWidth(), QWIDGETSIZE_MAX);
    box->show();
    QVERIFY(QTest::qWaitForWindowExposed(box.data()));
    box->hide();

    // A message box with standard buttons keeps them beside the own row
    pdf::PDFMessageBox standard(pdf::PDFThemeIcons::Badge::Warning, QStringLiteral("Title"), QStringLiteral("Text"), QDialogButtonBox::Ok);
    QPushButton* extra = standard.addActionButton(QStringLiteral("Details"), pdf::PDFMessageBox::ActionPosition::Leading, false);
    QVERIFY(extra);
    QVERIFY(!standard.getButtonBox()->isHidden());

    // The badge, the button box and the row of the buttons deleted by the application
    {
        pdf::PDFMessageBox parts(pdf::PDFThemeIcons::Badge::Information, QStringLiteral("Title"), QStringLiteral("Text"), QDialogButtonBox::NoButton);
        QWidget* badgeWidget = nullptr;
        for (QWidget* child : parts.findChildren<QWidget*>())
        {
            if (child->accessibleName() == QStringLiteral("Information"))
            {
                badgeWidget = child;
            }
        }
        QVERIFY(badgeWidget);
        delete badgeWidget;
        QVERIFY(!parts.isBadgeVisible());
        parts.setBadgeVisible(true);
        QVERIFY(!parts.isBadgeVisible());

        delete parts.getButtonBox();
        QVERIFY(parts.addActionButton(QStringLiteral("First"), pdf::PDFMessageBox::ActionPosition::Trailing, false));

        // A row without its stretch puts a leading button at its end
        QBoxLayout* partsLayout = qobject_cast<QBoxLayout*>(parts.layout());
        QVERIFY(partsLayout);
        QBoxLayout* actions = qobject_cast<QBoxLayout*>(partsLayout->itemAt(partsLayout->count() - 1)->layout());
        QVERIFY(actions);
        for (int i = actions->count() - 1; i >= 0; --i)
        {
            if (actions->itemAt(i)->spacerItem())
            {
                delete actions->takeAt(i);
            }
        }
        QPushButton* leading = parts.addActionButton(QStringLiteral("Leading"), pdf::PDFMessageBox::ActionPosition::Leading, false);
        QCOMPARE(actions->indexOf(leading), actions->count() - 1);

        // Without the row: no button, the content at the end of the layout
        delete actions;
        QCOMPARE(parts.addActionButton(QStringLiteral("Late"), pdf::PDFMessageBox::ActionPosition::Trailing, false), nullptr);
        QVERIFY(parts.getActionButtons().isEmpty());
        QWidget* content = new QWidget();
        parts.addContent(content);
        QCOMPARE(partsLayout->indexOf(content), partsLayout->count() - 1);
    }

    // The parts deleted by the application
    delete box->layout();
    QCOMPARE(box->addActionButton(QStringLiteral("Late"), pdf::PDFMessageBox::ActionPosition::Trailing, false), nullptr);
    QVERIFY(box->getActionButtons().isEmpty());
    box->addContent(new QWidget(box.data()));
    box->setCardWidth(300);
    QCOMPARE(box->sizeHint().width(), 300 + box->getShadowMargins().left() + box->getShadowMargins().right());
    delete box.data();
}

void FluentComponentsTest::renderingIssueRowPixels_data()
{
    pixelProbes_data();
}

void FluentComponentsTest::renderingIssueRowPixels()
{
    QFETCH(QString, theme);
    QFETCH(qreal, scale);
    QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));

    const QColor surface = token(Color::BgSurface);
    Painter::RenderingIssueRow row;
    row.page = QStringLiteral("Page 12");
    row.message = QStringLiteral("Font ArialMT is not embedded. A substitute font is used.");
    row.severity = Painter::IssueSeverity::Warning;

    // Size of the design: 406 x 46 for a message of one line, a longer message wraps
    QCOMPARE(Painter::getRenderingIssueRowSize(row), QSize(406, 46));
    QCOMPARE(Painter::getRenderingIssueRowHeight(row, 406), 46);
    QVERIFY(Painter::getRenderingIssueRowHeight(row, 200) > 46);
    QCOMPARE((Painter::getRenderingIssueRowHeight(row, 200) - 46) % 15, 0);
    QCOMPARE(Painter::getRenderingIssueRowMinimumSize(row).height(), 46);
    const Painter::RenderingIssueRow empty;
    QCOMPARE(Painter::getRenderingIssueRowHeight(empty, 406), 46);

    // Severities: icons and colors, an invalid value is a warning
    QCOMPARE(Painter::getIssueSeverityIconName(Painter::IssueSeverity::Error), QStringLiteral("x-circle"));
    QCOMPARE(Painter::getIssueSeverityIconName(Painter::IssueSeverity::Warning), QStringLiteral("alert-triangle"));
    QCOMPARE(Painter::getIssueSeverityIconName(Painter::IssueSeverity::Information), QStringLiteral("info"));
    QCOMPARE(Painter::getIssueSeverityIconColor(Painter::IssueSeverity(77)), Color::AccentAmberIcon);
    QCOMPARE(Painter::normalized(Painter::IssueSeverity(-3)), Painter::IssueSeverity::Warning);

    const QRect iconArea(10, 8, 16, 16);
    const QRect pageArea(36, 8, 50, 13);
    const QRect messageArea(36, 23, 300, 15);

    // Nearer to the first token than to the second one
    auto isCloser = [](const QColor& color, const QColor& expected, const QColor& other)
    {
        return colorDistance(color, expected) < colorDistance(color, other);
    };

    // At rest: no fill, the icon accent/amber/icon, the page text/muted, the message text/secondary
    QImage image = render([&](QPainter& painter) { Painter::paintRenderingIssueRow(&painter, QRectF(0, 0, 406, 46), row, state(QStyle::State_None)); }, QSize(406, 46), scale);
    QVERIFY(isNear(pixel(image, 3, 3), surface));
    QVERIFY(isNear(pixel(image, 400, 40), surface));
    QVERIFY(isCloser(strongestInk(image, iconArea, surface), token(Color::AccentAmberIcon), token(Color::AccentRedIcon)));
    QVERIFY(isCloser(strongestInk(image, pageArea, surface), token(Color::TextMuted), surface));
    QVERIFY(isCloser(strongestInk(image, messageArea, surface), token(Color::TextSecondary), surface));
    QCOMPARE(countLeakPixels(image), 0);

    // Hover bg/hover, pressed bg/pressed (radius 8: the corner stays the surface)
    image = render([&](QPainter& painter) { Painter::paintRenderingIssueRow(&painter, QRectF(0, 0, 406, 46), row, state(QStyle::State_MouseOver)); }, QSize(406, 46), scale);
    QVERIFY(isNear(pixel(image, 400, 40), blend(token(Color::BgHover), surface)));
    QVERIFY(isNear(pixel(image, 0, 0), surface, 12));
    image = render([&](QPainter& painter) { Painter::paintRenderingIssueRow(&painter, QRectF(0, 0, 406, 46), row, state(QStyle::State_MouseOver | QStyle::State_Sunken)); }, QSize(406, 46), scale);
    QVERIFY(isNear(pixel(image, 400, 40), blend(token(Color::BgPressed), surface)));

    // Error: accent/red/icon; disabled: icon/disabled and text/disabled
    row.severity = Painter::IssueSeverity::Error;
    image = render([&](QPainter& painter) { Painter::paintRenderingIssueRow(&painter, QRectF(0, 0, 406, 46), row, state(QStyle::State_None)); }, QSize(406, 46), scale);
    QVERIFY(isCloser(strongestInk(image, iconArea, surface), token(Color::AccentRedIcon), token(Color::AccentAmberIcon)));
    image = render([&](QPainter& painter) { Painter::paintRenderingIssueRow(&painter, QRectF(0, 0, 406, 46), row, Painter::State(QStyle::State_None)); }, QSize(406, 46), scale);
    QVERIFY(isCloser(strongestInk(image, iconArea, surface), token(Color::IconDisabled), token(Color::AccentRedIcon)));
    QVERIFY(isCloser(strongestInk(image, messageArea, surface), token(Color::TextDisabled), token(Color::TextSecondary)));

    // Right-to-left: the icon at the right edge, nothing at the left edge
    image = render([&](QPainter& painter) { Painter::paintRenderingIssueRow(&painter, QRectF(0, 0, 406, 46), row, state(QStyle::State_None, Qt::RightToLeft)); }, QSize(406, 46), scale);
    QVERIFY(isCloser(strongestInk(image, QRect(406 - 26, 8, 16, 16), surface), token(Color::AccentRedIcon), surface));
    QVERIFY(isNear(strongestInk(image, iconArea, surface), surface, 8));

    // Defensive: a null painter, empty, NaN and narrow rectangles
    Painter::paintRenderingIssueRow(nullptr, QRectF(0, 0, 406, 46), row, state(QStyle::State_None));
    image = render([&](QPainter& painter)
    {
        Painter::paintRenderingIssueRow(&painter, QRectF(), row, state(QStyle::State_None));
        Painter::paintRenderingIssueRow(&painter, QRectF(0, 0, qQNaN(), 46), row, state(QStyle::State_None));
        Painter::paintRenderingIssueRow(&painter, QRectF(0, 0, 20, 46), row, state(QStyle::State_None));
    }, QSize(406, 46), scale);
    QVERIFY(isNear(pixel(image, 200, 30), surface));
}

void FluentComponentsTest::renderingIssueRowWidget()
{
    QWidget window;
    window.resize(500, 300);
    QVBoxLayout* layout = new QVBoxLayout(&window);
    QPointer<pdf::PDFRenderingIssueRow> row = new pdf::PDFRenderingIssueRow(QStringLiteral("Page 308"), QStringLiteral("Invalid arguments of the operator Tj. The operator was skipped."),
                                                                           pdf::PDFRenderingIssueRow::Severity::Error, &window);
    pdf::PDFRenderingIssueRow* second = new pdf::PDFRenderingIssueRow(QStringLiteral("Page 12"), QStringLiteral("Second"), pdf::PDFRenderingIssueRow::Severity::Warning, &window);
    layout->addWidget(row);
    layout->addWidget(second);
    layout->addStretch(1);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QCOMPARE(row->getPage(), QStringLiteral("Page 308"));
    QCOMPARE(row->getSeverity(), pdf::PDFRenderingIssueRow::Severity::Error);
    QCOMPARE(row->getContent().page, QStringLiteral("Page 308"));
    row->setSeverity(pdf::PDFRenderingIssueRow::Severity(42));
    QCOMPARE(row->getSeverity(), pdf::PDFRenderingIssueRow::Severity::Warning);
    row->setSeverity(pdf::PDFRenderingIssueRow::Severity::Error);
    row->setPage(QStringLiteral("Page 308"));

    // The height follows the width
    QVERIFY(row->hasHeightForWidth());
    QCOMPARE(row->heightForWidth(406), Painter::getRenderingIssueRowHeight(row->getContent(), 406));
    QVERIFY(row->heightForWidth(150) > row->heightForWidth(406));
    const int oldHeight = row->heightForWidth(406);
    row->setMessage(row->getMessage() + QStringLiteral(" And a much longer message, which takes the second and the third line of the row of the popover."));
    QVERIFY(row->heightForWidth(406) > oldHeight);

    // The widget draws the painter
    row->setFixedWidth(406);
    row->setFixedHeight(row->heightForWidth(406));
    QApplication::processEvents();
    const QImage widgetImage = renderWidget(row, 1.0);
    const QImage painterImage = renderInWidget([&](QPainter& painter) { Painter::paintRenderingIssueRow(&painter, QRectF(QPoint(), row->size()), row->getContent(), state(QStyle::State_None)); }, row->size());
    QCOMPARE(describeDifference(widgetImage, painterImage, 2), QString());

    // Accessibility: the page and the message, the severity as the description
    QCOMPARE(row->getAccessibleText(), QStringLiteral("Page 308: %1").arg(row->getMessage()));
    QCOMPARE(row->getAccessibleDescriptionText(), QStringLiteral("Error"));
    QCOMPARE(second->getAccessibleDescriptionText(), QStringLiteral("Warning"));
    second->setSeverity(pdf::PDFRenderingIssueRow::Severity::Information);
    QCOMPARE(second->getAccessibleDescriptionText(), QStringLiteral("Information"));

    // Click, Enter and Space activate it
    QSignalSpy clicked(row.data(), &QAbstractButton::clicked);
    QTest::mouseClick(row, Qt::LeftButton, Qt::NoModifier, QPoint(20, 10));
    QCOMPARE(clicked.count(), 1);
    row->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(row->hasFocus());
    QTest::keyClick(row, Qt::Key_Space);
    QCOMPARE(clicked.count(), 2);
    QTest::keyClick(row, Qt::Key_Return);
    QCOMPARE(clicked.count(), 3);

    // Deleted from its own signal (later, as the rule of the library says)
    connect(row.data(), &QAbstractButton::clicked, row.data(), [&row]() { row->deleteLater(); });
    QTest::mouseClick(row, Qt::LeftButton, Qt::NoModifier, QPoint(20, 10));
    QTRY_VERIFY(!row);
}

void FluentComponentsTest::messageBoxTextLines()
{
    // A plain text of a label in the lines of the design (QLabel has no line height):
    // the label lays the text out itself, the text stays a plain text
    const QString text = QStringLiteral("<b>Report</b> & \"data\"\nsecond line");
    pdf::PDFLabel label(text, pdf::PDFThemeFonts::Role::Small, Color::TextSecondary);
    label.setTextFormat(Qt::PlainText);
    QCOMPARE(label.getLineHeight(), 0);
    const int naturalHeight = label.sizeHint().height();
    QVERIFY(naturalHeight < 32);

    label.setLineHeight(16);
    QCOMPARE(label.getLineHeight(), 16);
    QCOMPARE(label.text(), text);
    QCOMPARE(label.textFormat(), Qt::PlainText);
    QCOMPARE(label.sizeHint().height(), 32);
    QCOMPARE(label.minimumSizeHint().height(), 32);
    QCOMPARE(label.minimumSizeHint().width(), label.sizeHint().width());
    label.setLineHeight(15);
    QCOMPARE(label.sizeHint().height(), 30);
    label.setLineHeight(-5);
    QCOMPARE(label.getLineHeight(), 0);
    QCOMPARE(label.sizeHint().height(), naturalHeight);
    label.setLineHeight(100000);
    QCOMPARE(label.getLineHeight(), pdf::PDFLabel::MAX_LINE_HEIGHT);
    label.setLineHeight(16);

    // A wrapped text: lines of 16 px for every width
    label.setWordWrap(true);
    QVERIFY(label.hasHeightForWidth());
    for (int width : { 40, 80, 160, 400, 1 })
    {
        const int height = label.heightForWidth(width);
        QVERIFY2(height >= 32 && height % 16 == 0, qPrintable(QString("%1: %2").arg(width).arg(height)));
    }
    QCOMPARE(label.heightForWidth(400), 32);

    // A rich text and a picture keep the layout of QLabel
    pdf::PDFLabel rich(QStringLiteral("<b>Rich</b>"), pdf::PDFThemeFonts::Role::Small, Color::TextSecondary);
    rich.setTextFormat(Qt::RichText);
    const QSize richSize = rich.sizeHint();
    rich.setLineHeight(40);
    QCOMPARE(rich.sizeHint(), richSize);

    // The selection by the mouse: a drag over the first line selects it, the
    // selection ends with a new text, a double click selects a word
    label.setWordWrap(false);
    label.setTextInteractionFlags(Qt::TextSelectableByMouse);
    label.setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label.resize(label.sizeHint());
    label.show();
    QVERIFY(QTest::qWaitForWindowExposed(&label));
    QVERIFY(!label.hasSelectedText());
    QTest::mousePress(&label, Qt::LeftButton, Qt::NoModifier, QPoint(0, 8));
    {
        QMouseEvent move(QEvent::MouseMove, QPointF(label.width() - 1, 8), label.mapToGlobal(QPointF(label.width() - 1, 8)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&label, &move);
    }
    QTest::mouseRelease(&label, Qt::LeftButton, Qt::NoModifier, QPoint(label.width() - 1, 8));
    QCOMPARE(label.selectedText(), QStringLiteral("<b>Report</b> & \"data\""));
    QVERIFY(label.hasSelectedText());
    QCOMPARE(countLeakPixels(label.grab().toImage()), 0);
    QTest::keySequence(&label, QKeySequence::Copy);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("<b>Report</b> & \"data\""));

    // The context menu of the selectable text (as the menu of QLabel): Copy of the
    // selection, Select All
    auto chooseFromMenu = [&label](int actionIndex)
    {
        QTimer::singleShot(0, &label, [actionIndex]()
        {
            QMenu* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!popup)
            {
                return;
            }
            const QList<QAction*> actions = popup->actions();
            if (actionIndex >= 0 && actionIndex < actions.size() && actions[actionIndex]->isEnabled())
            {
                popup->setActiveAction(actions[actionIndex]);
                QTest::keyClick(popup, Qt::Key_Return);
            }
            else
            {
                popup->close();
            }
        });
        QContextMenuEvent menuEvent(QContextMenuEvent::Mouse, QPoint(2, 2), label.mapToGlobal(QPoint(2, 2)));
        QApplication::sendEvent(&label, &menuEvent);
        return menuEvent.isAccepted();
    };
    QGuiApplication::clipboard()->setText(QStringLiteral("other"));
    QVERIFY(chooseFromMenu(0));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("<b>Report</b> & \"data\""));
    QVERIFY(chooseFromMenu(1));
    QCOMPARE(label.selectedText(), text);
    QVERIFY(chooseFromMenu(-1));
    QCOMPARE(label.selectedText(), text);
    QCOMPARE(QApplication::activePopupWidget(), nullptr);

    QTest::mouseDClick(&label, Qt::LeftButton, Qt::NoModifier, QPoint(2, 16 + 8));
    QCOMPARE(label.selectedText(), QStringLiteral("second"));
    label.setText(QStringLiteral("Other"));
    QVERIFY(!label.hasSelectedText());
    label.hide();

    // Message box 626:1744 / 955:148444: the content 23 / 21 from the edge of the card
    // (paddings 22 / 20 inside of the border of 1 px), the text Regular 12 in lines of
    // 16 px, the details Regular 11 in lines of 15 px, the content 394 px wide in a
    // card of 440
    QWidget parent;
    parent.resize(800, 600);
    const QString body = QStringLiteral("C:\\Users\\Jakub\\Documents\\Project_Report.pdf is open in another application. Close it and try again.");
    const QString details = QStringLiteral("Error 32: The process cannot access the file because it is being used by another process.");
    pdf::PDFMessageBox box(pdf::PDFThemeIcons::Badge::Critical, QStringLiteral("The file could not be written"), body, QDialogButtonBox::Ok, &parent);
    box.setWindowFlags(Qt::Widget);
    box.setDetailedText(details);
    box.setCardWidth(440);
    QCOMPARE(box.getContentWidth(), 394);
    parent.show();
    box.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));
    QVERIFY(box.getShadowMargins().isNull());
    QCOMPARE(box.layout()->contentsMargins(), QMargins(23, 21, 23, 21));

    pdf::PDFLabel* bodyLabel = nullptr;
    pdf::PDFLabel* detailsLabel = nullptr;
    for (pdf::PDFLabel* child : box.findChildren<pdf::PDFLabel*>())
    {
        if (child->text() == body)
        {
            bodyLabel = child;
        }
        else if (child->text() == details)
        {
            detailsLabel = child;
        }
    }
    QVERIFY(bodyLabel && detailsLabel);
    QCOMPARE(bodyLabel->getLineHeight(), 16);
    QCOMPARE(detailsLabel->getLineHeight(), 15);
    QCOMPARE(bodyLabel->textFormat(), Qt::PlainText);
    QVERIFY2(bodyLabel->height() >= 32 && bodyLabel->height() % 16 == 0, qPrintable(QString::number(bodyLabel->height())));
    QVERIFY2(detailsLabel->height() >= 30 && detailsLabel->height() % 15 == 0, qPrintable(QString::number(detailsLabel->height())));
    QCOMPARE(bodyLabel->textInteractionFlags(), Qt::TextInteractionFlags(Qt::TextSelectableByMouse));

    // Copy without a selection gives the whole message, with a selection the selected text
    QTest::keySequence(&box, QKeySequence::Copy);
    QString copied = QGuiApplication::clipboard()->text();
    QVERIFY2(copied.contains(body) && copied.contains(details), qPrintable(copied));
    QTest::mouseDClick(bodyLabel, Qt::LeftButton, Qt::NoModifier, QPoint(2, 8));
    QCOMPARE(bodyLabel->selectedText(), QStringLiteral("C"));
    QTest::keySequence(&box, QKeySequence::Copy);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("C"));
    QCOMPARE(countLeakPixels(box.grab().toImage()), 0);

    // The text of 941:135875 wraps into the three lines of the design in a card of 520;
    // the minimal height of the label is one line (QLabel), so a window of a card is
    // never taller than the text needs
    const QString history = QStringLiteral("Recent forgets which documents, folders and workspaces you opened and where you stopped reading. "
                                           "Your files are not touched, and saved workspaces stay in PDF4QT settings.");
    pdf::PDFMessageBox historyBox(pdf::PDFThemeIcons::Badge::Warning, QStringLiteral("Clear the Recent history?"), history, QDialogButtonBox::NoButton, &parent);
    historyBox.setCardWidth(520);
    historyBox.show();
    QVERIFY(QTest::qWaitForWindowExposed(&historyBox));
    pdf::PDFLabel* historyLabel = nullptr;
    for (pdf::PDFLabel* child : historyBox.findChildren<pdf::PDFLabel*>())
    {
        if (child->text() == history)
        {
            historyLabel = child;
        }
    }
    QVERIFY(historyLabel);
    QVERIFY2(historyLabel->width() == 426 && historyLabel->height() == 48, qPrintable(QString("%1 x %2").arg(historyLabel->width()).arg(historyLabel->height())));
}

void FluentComponentsTest::messageBoxBadgeIcon()
{
    QWidget host;
    host.resize(800, 600);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    QPointer<pdf::PDFMessageBox> box = new pdf::PDFMessageBox(pdf::PDFThemeIcons::Badge::Information, QStringLiteral("Open the web link?"), QStringLiteral("Text"), QDialogButtonBox::Cancel, &host);
    QCOMPARE(box->getBadgeIconName(), QString());
    box->setBadgeIconName(QStringLiteral("external-link"));
    QCOMPARE(box->getBadgeIconName(), QStringLiteral("external-link"));

    // The badge in the colors of the kind with another icon: the circle is the same, the icon differs
    auto paintBadge = [](const QString& iconName)
    {
        return render([&](QPainter& painter) { pdf::PDFThemeIcons::paintBadge(&painter, QRectF(0, 0, 36, 36), pdf::PDFThemeIcons::Badge::Information, iconName); }, QSize(36, 36), 2.0);
    };
    const QImage defaultBadge = paintBadge(QString());
    const QImage linkBadge = paintBadge(QStringLiteral("external-link"));
    const QImage unknownBadge = paintBadge(QStringLiteral("no-such-icon"));
    QCOMPARE(describeDifference(defaultBadge, unknownBadge, 0), QString());
    QVERIFY(!describeDifference(defaultBadge, linkBadge, 8).isEmpty());
    QVERIFY(isNear(pixel(linkBadge, 3, 18), blend(token(Color::AccentBlueBg), token(Color::BgSurface)), 12));

    const QImage oldOverload = render([&](QPainter& painter) { pdf::PDFThemeIcons::paintBadge(&painter, QRectF(0, 0, 36, 36), pdf::PDFThemeIcons::Badge::Information); }, QSize(36, 36), 2.0);
    QCOMPARE(describeDifference(defaultBadge, oldOverload, 0), QString());

    // A shown message box draws the icon of the badge
    box->show();
    QVERIFY(QTest::qWaitForWindowExposed(box.data()));
    box->setBadgeIconName(QString());
    QCOMPARE(box->getBadgeIconName(), QString());
    delete box.data();
}

void FluentComponentsTest::iconChoice()
{
    // Sizes of 284:2227: the selected choice 40 (ring 2, padding 2, badge 32), the others 36
    QCOMPARE(Painter::getIconChoiceSize(true), QSize(40, 40));
    QCOMPARE(Painter::getIconChoiceSize(false), QSize(36, 36));
    QCOMPARE(Painter::getIconChoiceRadius(), 10.0);

    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        for (qreal scale : { 1.0, 1.5, 2.0 })
        {
            const Painter::IconChoice choice{ QStringLiteral("camera") };

            // Selected: the ring border/focus at the edge, the Blue badge inside (padding 4 from the edge)
            const QImage selected = render([&](QPainter& painter) { Painter::paintIconChoice(&painter, QRectF(0, 0, 40, 40), choice, state(QStyle::State_On)); }, QSize(40, 40), scale);
            QVERIFY(isNear(pixel(selected, 20, 1), token(Color::BorderFocus), 12));
            QVERIFY(isNear(pixel(selected, 7, 20), pdf::PDFStylePainter::color(pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Blue, pdf::PDFAccentHueColors::Role::Background)), 12));
            QCOMPARE(countLeakPixels(selected), 0);

            // Unselected: the Neutral badge (padding 2), no ring
            const QImage unselected = render([&](QPainter& painter) { Painter::paintIconChoice(&painter, QRectF(0, 0, 36, 36), choice, state(QStyle::State_None)); }, QSize(36, 36), scale);
            QVERIFY(isNear(pixel(unselected, 5, 18), pdf::PDFStylePainter::color(pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Neutral, pdf::PDFAccentHueColors::Role::Background)), 12));
            QVERIFY(isNear(pixel(unselected, 18, 0.5), token(Color::BgSurface), 6));

            // Hover lays bg/hover under the badge
            const QImage hovered = render([&](QPainter& painter) { Painter::paintIconChoice(&painter, QRectF(0, 0, 36, 36), choice, state(QStyle::State_MouseOver)); }, QSize(36, 36), scale);
            QVERIFY(!isNear(pixel(hovered, 18, 1), token(Color::BgSurface), 1));
        }
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    // The widget: an exclusive group, the selected one larger, the arrows move the selection
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    QButtonGroup group;
    pdf::PDFIconChoice* rotate = new pdf::PDFIconChoice(QStringLiteral("rotate-cw"), QStringLiteral("Rotate"), &window);
    pdf::PDFIconChoice* camera = new pdf::PDFIconChoice(QStringLiteral("camera"), QStringLiteral("Camera"), &window);
    group.addButton(rotate);
    group.addButton(camera);
    layout->addWidget(rotate);
    layout->addWidget(camera);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    rotate->setSelected(true);
    QCOMPARE(rotate->sizeHint(), QSize(40, 40));
    QCOMPARE(camera->sizeHint(), QSize(36, 36));
    QCOMPARE(rotate->getFocusRing(), pdf::PDFComponentButton::FocusRing::Outside);
    QCOMPARE(rotate->getFocusRingRadius(), 10.0);
    rotate->setFocus(Qt::TabFocusReason);
    QTest::keyClick(rotate, Qt::Key_Right);
    QVERIFY(camera->isSelected());
    QVERIFY(!rotate->isSelected());
    QCOMPARE(camera->sizeHint(), QSize(40, 40));
    QCOMPARE(rotate->sizeHint(), QSize(36, 36));
    camera->click();
    QVERIFY(camera->isSelected());
    camera->setSelected(false);
    QVERIFY(!group.checkedButton());
    camera->setIconName(QStringLiteral("zap"));
    QCOMPARE(camera->getIconName(), QStringLiteral("zap"));

    // The widget draws as its painter
    camera->setSelected(true);
    layout->activate();
    QCOMPARE(camera->size(), QSize(40, 40));
    const QImage widgetImage = renderWidget(camera, 1.0);
    const QImage painterImage = renderInWidget([](QPainter& painter) { Painter::paintIconChoice(&painter, QRectF(0, 0, 40, 40), Painter::IconChoice{ QStringLiteral("zap") }, Painter::State(QStyle::State_Enabled | QStyle::State_On)); }, QSize(40, 40), 1.0);
    QVERIFY2(describeDifference(widgetImage, painterImage, 4).isEmpty(), qPrintable(describeDifference(widgetImage, painterImage, 4)));

    // Accessibility: the name of the meaning of the icon
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(rotate);
    QVERIFY(accessible);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Rotate"));
    QCOMPARE(rotate->toolTip(), QStringLiteral("Rotate"));

    // Disabled: the icon icon/disabled, no ring of the focus; defensive cases
    window.setEnabled(false);
    QVERIFY(!camera->getComponentState().isEnabled());
    Painter::paintIconChoice(nullptr, QRectF(0, 0, 40, 40), Painter::IconChoice(), Painter::State());
    const QImage empty = render([](QPainter& painter) { Painter::paintIconChoice(&painter, QRectF(), Painter::IconChoice(), Painter::State()); }, QSize(4, 4));
    QCOMPARE(countLeakPixels(empty), 0);
}

void FluentComponentsTest::toolRowWithoutStar()
{
    // 284:2304: the Tool row without the star gives its place to the texts
    Painter::ToolRow row;
    row.name = QStringLiteral("Rotate Pages");
    row.description = QStringLiteral("Turn pages in 90° steps");
    row.iconName = QStringLiteral("rotate-cw");
    row.hue = pdf::PDFAccentHue::Amber;
    Painter::ToolRow starless = row;
    starless.starVisible = false;
    QCOMPARE(Painter::getToolRowSize(starless), QSize(380, 48));
    QCOMPARE(Painter::getToolRowMinimumSize(row).width() - Painter::getToolRowMinimumSize(starless).width(), 38);

    // The tag reaches the padding of 6 instead of the star
    starless.tag = QStringLiteral("Advanced");
    QVERIFY(Painter::isToolRowTagShown(QRectF(0, 0, 380, 48), starless));
    row.tag = starless.tag;
    const int narrow = 8 + 32 + Painter::getTagSize(Painter::Tag{ starless.tag }).width() + 6 + 1;
    QVERIFY(Painter::isToolRowTagShown(QRectF(0, 0, narrow, 48), starless));
    QVERIFY(!Painter::isToolRowTagShown(QRectF(0, 0, narrow, 48), row));

    // No star is drawn: the place of the star is bg/surface
    starless.tag.clear();
    const QImage image = render([&](QPainter& painter) { Painter::paintToolRow(&painter, QRectF(0, 0, 380, 48), starless, state(QStyle::State_None)); }, QSize(380, 48));
    QCOMPARE(countPixels(image, QRect(346, 10, 28, 28), token(Color::IconMuted), 24), 0);

    // The widget hides its star
    pdf::PDFToolRow toolRow(row.name, row.description, row.iconName, row.hue);
    QVERIFY(toolRow.isStarVisible());
    toolRow.setStarVisible(false);
    QVERIFY(!toolRow.isStarVisible());
    toolRow.resize(380, 48);
    toolRow.show();
    QVERIFY(toolRow.getStarButton()->isHidden());
    QVERIFY(!toolRow.getContent().starVisible);
    toolRow.setTag(QStringLiteral("Advanced"));
    const int tagWidth = Painter::getTagSize(Painter::Tag{ QStringLiteral("Advanced") }).width();
    QCOMPARE(toolRow.getTagWidget()->geometry(), QRect(380 - 6 - tagWidth, 13, tagWidth, 21));
    toolRow.setStarVisible(true);
    QVERIFY(!toolRow.getStarButton()->isHidden());
    QCOMPARE(toolRow.getStarButton()->geometry(), QRect(346, 10, 28, 28));

    // Review RC6-2: a star hidden or deleted by the application gives its place too
    // (as PDFRecentFileRow) - the texts and the tag are not cut next to an empty place
    const int minimumWithStar = toolRow.minimumSizeHint().width();
    toolRow.getStarButton()->hide();
    QVERIFY(!toolRow.isStarVisible());
    QVERIFY(!toolRow.getContent().starVisible);
    toolRow.getStarButton()->show();
    QVERIFY(toolRow.getContent().starVisible);
    delete toolRow.getStarButton();
    QVERIFY(!toolRow.getStarButton());
    QVERIFY(!toolRow.isStarVisible());
    QVERIFY(!toolRow.getContent().starVisible);
    QCOMPARE(minimumWithStar - toolRow.minimumSizeHint().width(), 38);
    toolRow.setStarVisible(true);
    QVERIFY(!toolRow.isStarVisible());
}

QTEST_MAIN(FluentComponentsTest)

#include "tst_fluentcomponentstest.moc"
