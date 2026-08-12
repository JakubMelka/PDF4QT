// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

/// Persistent storage of annotation styles. Each annotation tool (or a group of
/// tools, which logically share the style, such as the two redaction tools) uses
/// its own style identified by a style id. Styles are stored in the application
/// settings, so the user's choice is remembered across the restarts of the
/// application, and it is not reset when the user switches between the tools.
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
};

/// Modeless tool window, which allows the user to select the style of the created
/// annotations. It is shared by all annotation tools - each tool declares, which
/// style items it actually uses, so the user gets the same way of setting the
/// annotation style regardless of the used tool.
///
/// The window intentionally has no close button - it is shown when an annotation
/// tool is activated and it is closed when the tool is deactivated. If the user
/// could close it, there would be no way of showing it again.
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

    /// Shows the style window. Call this when the tool is activated.
    /// \param parentWidget Parent widget of the style window
    void showStyleWindow(QWidget* parentWidget);

    /// Closes the style window. Call this when the tool is deactivated.
    void closeStyleWindow();

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
