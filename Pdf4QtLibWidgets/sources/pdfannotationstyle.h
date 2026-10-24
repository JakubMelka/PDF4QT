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

#ifndef PDFANNOTATIONSTYLE_H
#define PDFANNOTATIONSTYLE_H

#include "pdfwidgetsglobal.h"
#include "pdfglobal.h"

#include <QColor>
#include <QPointer>
#include <QWidget>

class QLabel;
class QCheckBox;
class QPushButton;
class QDoubleSpinBox;

namespace pdf
{

/// Style of a newly created annotation. Not all items are used by all annotation
/// tools - for example a text markup annotation uses the stroke color only.
struct PDF4QTLIBWIDGETSSHARED_EXPORT PDFAnnotationStyle
{
    bool operator==(const PDFAnnotationStyle&) const = default;

    /// Color of the stroke (or the color of the text markup / redaction)
    QColor strokeColor = Qt::red;

    /// Color of the fill. Invalid color means, that the shape is not filled.
    QColor fillColor = QColor();

    /// Width of the pen used for the stroke
    PDFReal penWidth = 1.0;
};

/// Storage of the annotation styles. The application installs its own storage by
/// PDFAnnotationStyleSettings::setStorage (the new application keeps the styles in
/// its database); without it the styles are kept in QSettings
/// (PDFAnnotationStyleQSettingsStorage). The storage is called in the thread of the
/// user interface only. Values returned by a storage are untrusted: the caller
/// (PDFAnnotationStyleSettings::getStyle) bounds them.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFAnnotationStyleStorage
{
public:
    virtual ~PDFAnnotationStyleStorage();

    /// Returns the style stored for the style id, or \p defaultStyle, when nothing
    /// is stored (also for a damaged stored value).
    /// \param styleId Style identifier
    /// \param defaultStyle Style used, when nothing is stored yet
    virtual PDFAnnotationStyle getStyle(const QString& styleId, const PDFAnnotationStyle& defaultStyle) const = 0;

    /// Stores the style for the style id
    /// \param styleId Style identifier
    /// \param style Style to be stored
    virtual void setStyle(const QString& styleId, const PDFAnnotationStyle& style) = 0;
};

/// Storage of the annotation styles in QSettings (group AnnotationStyles of the
/// settings of the application) - the storage of the old applications, used when
/// no other storage is installed.
// TODO(remove-old-apps): the QSettings storage goes - the new application keeps the styles in its database (PDFAppAnnotationStyleStorage), no QSettings in the libraries
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFAnnotationStyleQSettingsStorage : public PDFAnnotationStyleStorage
{
public:
    virtual PDFAnnotationStyle getStyle(const QString& styleId, const PDFAnnotationStyle& defaultStyle) const override;
    virtual void setStyle(const QString& styleId, const PDFAnnotationStyle& style) override;

    /// Position of the style window remembered by closeStyleWindow (null = none)
    static QPoint getWindowPosition();
    static void setWindowPosition(QPoint position);

    /// Redaction color stored by the older versions of the old applications
    /// (group RedactTool), invalid color if none is stored
    static QColor getLegacyRedactColor();
};

/// Persistent storage of annotation styles. Each annotation tool (or a group of
/// tools, which logically share the style, such as the two redaction tools) uses
/// its own style identified by a style id. Styles are stored in the storage
/// installed by setStorage (the application settings by default), so the user's
/// choice is remembered across the restarts of the application, and it is not
/// reset when the user switches between the tools.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFAnnotationStyleSettings
{
public:
    /// Identifiers of the persisted styles. Tools using the same identifier
    /// share the style.
    static constexpr QLatin1String STYLE_HIGHLIGHT = QLatin1String("Highlight");
    static constexpr QLatin1String STYLE_UNDERLINE = QLatin1String("Underline");
    static constexpr QLatin1String STYLE_SQUIGGLY = QLatin1String("Squiggly");
    static constexpr QLatin1String STYLE_STRIKEOUT = QLatin1String("StrikeOut");
    static constexpr QLatin1String STYLE_REDACT = QLatin1String("Redact");
    static constexpr QLatin1String STYLE_SHAPE = QLatin1String("Shape");
    static constexpr QLatin1String STYLE_FREEHAND = QLatin1String("Freehand");

    /// Returns the style stored for the given style id. If no style has been
    /// stored yet, then \p defaultStyle is returned.
    /// \param styleId Style identifier
    /// \param defaultStyle Style used, when nothing is stored yet
    static PDFAnnotationStyle getStyle(const QString& styleId, const PDFAnnotationStyle& defaultStyle);

    /// Stores the style for the given style id.
    /// \param styleId Style identifier
    /// \param style Style to be stored
    static void setStyle(const QString& styleId, const PDFAnnotationStyle& style);

    /// Installs the storage of the styles (not owned; the caller removes it by
    /// setStorage(nullptr) before it is destroyed). nullptr restores the default
    /// QSettings storage.
    /// \param storage Storage, or nullptr
    static void setStorage(PDFAnnotationStyleStorage* storage);

    /// Returns the installed storage (the default QSettings storage, when no other
    /// one is installed) - never nullptr
    static PDFAnnotationStyleStorage* getStorage();

    /// Returns true, if the default QSettings storage is used
    static bool isDefaultStorage();

    /// Bounds a style read from a storage: an invalid stroke color is replaced by the
    /// stroke color of \p defaultStyle, the pen width is bounded to 0 - 100 (a non finite
    /// width is the width of \p defaultStyle), the colors are made opaque.
    /// \param style Style read from a storage
    /// \param defaultStyle Default style
    static PDFAnnotationStyle boundStyle(PDFAnnotationStyle style, const PDFAnnotationStyle& defaultStyle);

    /// Maximal pen width of a style
    static constexpr PDFReal MAXIMUM_PEN_WIDTH = 100.0;

private:
    static PDFAnnotationStyleStorage* s_storage;
};

/// Modeless tool window, which allows the user to select the style of the created
/// annotations. It is shared by all annotation tools - each tool declares, which
/// style items it actually uses, so the user gets the same way of setting the
/// annotation style regardless of the used tool.
///
/// The window intentionally has no close button - it is shown when an annotation
/// tool is activated and it is closed when the tool is deactivated. If the user
/// could close it, there would be no way of showing it again.
// TODO(remove-old-apps): the window and its position in QSettings go - the new application turns it off (PDFAnnotationStyleManager::setStyleWindowEnabled) and shows the style in its tool bar
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFAnnotationStyleWidget : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    enum StyleItem
    {
        StrokeColor = 0x0001,   ///< Color of the stroke / text markup / redaction
        FillColor   = 0x0002,   ///< Color of the fill (can be turned off by the user)
        PenWidth    = 0x0004    ///< Width of the pen
    };
    Q_DECLARE_FLAGS(StyleItems, StyleItem)

    /// Creates the style window.
    /// \param parent Parent widget (the draw widget)
    /// \param items Style items, which are edited by this window
    /// \param style Initial style
    explicit PDFAnnotationStyleWidget(QWidget* parent, StyleItems items, const PDFAnnotationStyle& style);
    virtual ~PDFAnnotationStyleWidget() override;

    const PDFAnnotationStyle& getStyle() const { return m_style; }

    /// Shows the window at the position, which the user used the last time.
    /// If the window was not moved yet, a default position is used, which
    /// doesn't cover the content of the page.
    void showStyleWindow();

    /// Hides the window, remembers its position and schedules the window
    /// for deletion. It is not safe to delete the window directly - this
    /// function is usually called from the handling of an event of a widget,
    /// which can still be used after this function returns.
    void closeStyleWindow();

signals:
    void styleChanged(const pdf::PDFAnnotationStyle& style);

private:
    void onStrokeColorButtonClicked();
    void onFillColorButtonClicked();
    void onFillEnabledToggled(bool checked);
    void onPenWidthChanged(double value);

    void updateColorButton(QPushButton* button, QColor color);
    void updateColorButtons();

    QColor selectColor(QColor initialColor, const QString& title);

    StyleItems m_items;
    PDFAnnotationStyle m_style;

    QPushButton* m_strokeColorButton;
    QPushButton* m_fillColorButton;
    QCheckBox* m_fillEnabledCheckBox;
    QDoubleSpinBox* m_penWidthEdit;
};

// Jakub Melka: operators must be declared inside the namespace, not at the global
// scope. Namespace 'pdf' declares its own operator| (for the OCState enumeration),
// which hides all operators declared at the global scope from the unqualified name
// lookup performed inside this namespace. If the operators were declared globally,
// combining the flags in any code inside namespace 'pdf' would silently fall back
// to the built-in integer operator and would not compile.
Q_DECLARE_OPERATORS_FOR_FLAGS(PDFAnnotationStyleWidget::StyleItems)

/// Manages the annotation style of a single tool - it loads and stores the style
/// in the application settings and it shows the style window while the tool is
/// active. All annotation tools use this class, so the way of setting the style
/// of a created annotation is the same for all of them.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFAnnotationStyleManager : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Creates the style manager.
    /// \param parent Parent object (the tool)
    /// \param items Style items, which are used by the tool
    /// \param defaultStyle Style used, when nothing is stored in the settings yet
    /// \param styleId Identifier of the persisted style
    explicit PDFAnnotationStyleManager(QObject* parent,
                                       PDFAnnotationStyleWidget::StyleItems items,
                                       PDFAnnotationStyle defaultStyle,
                                       QString styleId);

    const PDFAnnotationStyle& getStyle() const { return m_style; }

    /// Sets the style and stores it in the application settings.
    /// \param style New style
    void setStyle(PDFAnnotationStyle style);

    /// Sets the identifier of the persisted style and loads the style stored for
    /// it. Use this when a single tool creates several kinds of annotations, which
    /// have their own style (for example highlight / underline / strikeout).
    /// \param styleId Identifier of the persisted style
    /// \param defaultStyle Style used, when nothing is stored for the new style id
    void setStyleId(QString styleId, PDFAnnotationStyle defaultStyle);

    /// Reloads the style from the application settings. Several tools can share
    /// the same style id (all shape tools, or both redaction tools), and each of
    /// them has its own style manager with its own cached copy of the style. The
    /// cached copy must be refreshed before it is used, otherwise a tool would
    /// create annotations using an outdated style and it would eventually store
    /// that outdated style back over the choice made by the user in another tool.
    void reloadStyle();

    /// Shows the style window. Call this when the tool is activated. The style is
    /// reloaded from the settings first, so a style shared with another tool is
    /// up to date.
    /// \param parentWidget Parent widget of the style window
    void showStyleWindow(QWidget* parentWidget);

    /// Closes the style window. Call this when the tool is deactivated.
    void closeStyleWindow();

    /// Returns true, if the style window is shown now
    bool isStyleWindowShown() const { return !m_styleWidget.isNull(); }

    /// Turns the style window of all tools on or off (process wide). The new
    /// application turns it off - it shows the style in its own tool bar and sets
    /// it by setStyle. Turning it off closes no window, which is shown already.
    /// On by default.
    // TODO(remove-old-apps): the style window (PDFAnnotationStyleWidget) goes with its switch - the tool bar of the application sets the style
    static void setStyleWindowEnabled(bool enabled);
    static bool isStyleWindowEnabled();

signals:
    void styleChanged(const pdf::PDFAnnotationStyle& style);

private:
    void onStyleChanged(const PDFAnnotationStyle& style);

    /// Recreates the style window (if it is shown), so it displays the current style
    void recreateStyleWindow();

    PDFAnnotationStyleWidget::StyleItems m_items;
    PDFAnnotationStyle m_defaultStyle;
    QString m_styleId;
    PDFAnnotationStyle m_style;
    QPointer<PDFAnnotationStyleWidget> m_styleWidget;
};

}   // namespace pdf

#endif // PDFANNOTATIONSTYLE_H
