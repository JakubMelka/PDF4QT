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

#ifndef PDFAPPANNOTATIONBAR_H
#define PDFAPPANNOTATIONBAR_H

#include "pdfappglobal.h"
#include "pdfappfloatingbar.h"

#include "pdfobject.h"

#include <QColor>
#include <QPointer>
#include <QString>

#include <vector>

namespace pdf
{

class PDFAppAnnotationController;
class PDFAppCommentPopupIcon;
class PDFAppDocumentView;
class PDFColorSwatchRow;
class PDFComboBox;
class PDFIconButton;
class PDFLabel;
class PDFSegmentedControl;
class PDFWidgetTool;

/// Annotation bar (Figma 1015:17319 single, 1018:18584 multi): the floating bar of
/// the active tool reused at the same place for the selected annotations (note
/// 1030:161012). Single: the icon, the type and the author, the Stroke / Fill
/// target (the types with a fill), the swatches, the line width, Comment,
/// Properties, More. Multi: "3 annotations" + "Page 308", the common color (no
/// ring for mixed colors), the width ("Mixed"), Align ×6, Distribute ×2, More.
///
/// A live change of the color or the width is one undo step (O20): the bar opens
/// a merge session of the controller for the property and the selection; another
/// control, a change of the selection, of the tool, the hide of the bar end it.
/// Locked document: only Comment. A Locked annotation (ISO 32000-2 Table 167): no
/// color, width and alignment; Comment, Properties (read only) and More stay.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAnnotationBar : public PDFAppFloatingBar
{
    Q_OBJECT

private:
    using BaseClass = PDFAppFloatingBar;

public:
    /// Widths offered by the width select (points)
    static std::vector<PDFReal> getWidths();

    /// Width of the width select (111 / 124 for "Mixed")
    static constexpr int WIDTH_SELECT = 111;
    static constexpr int WIDTH_SELECT_MIXED = 124;

    /// Width of the Stroke / Fill target (2 x 83)
    static constexpr int TARGET_WIDTH = 166;

    explicit PDFAppAnnotationBar(PDFAppDocumentView* view, QWidget* parent);
    virtual ~PDFAppAnnotationBar() override;

    PDFAppDocumentView* getView() const;

    /// Reads the selection again and shows / hides the bar
    void updateState();

    /// The bar edits the fill (else the stroke) of the selection
    bool isFillTarget() const;
    void setFillTarget(bool isFill);

    /// Applies a color / a width to the selection as the controls of the bar do
    /// (one merge session per property)
    void applyColor(const QColor& color);
    void applyWidth(PDFReal width);

    /// Opens the context menu of the selection below the More button
    void showMoreMenu();

    /// Parts of the bar (tests, accessibility)
    PDFAppCommentPopupIcon* getTypeIcon() const;
    PDFLabel* getNameLabel() const;
    PDFLabel* getAuthorLabel() const;
    PDFSegmentedControl* getTargetControl() const;
    PDFColorSwatchRow* getSwatchRow() const;
    PDFComboBox* getWidthComboBox() const;
    PDFIconButton* getCommentButton() const;
    PDFIconButton* getPropertiesButton() const;
    PDFIconButton* getMoreButton() const;
    PDFIconButton* getCommandButton(const QString& commandId) const;

    /// Hint of the selection shown in the status bar while the bar is shown
    /// (1015:17319 "Rectangle selected · drag to move, ...", 1018:18584
    /// "3 annotations selected on page 308 · ..."); empty = the bar shows none
    const QString& getStatusHint() const { return m_statusHint; }

    /// Narrow canvas: 0 = everything, 1 = no author, 2 = no name of the type,
    /// 3 = no Stroke / Fill target (the swatches fold into More first)
    int getCompaction() const { return m_compaction; }

protected:
    virtual bool event(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onSelectionChanged();
    void onStateChanged();
    void onActiveToolChanged(pdf::PDFWidgetTool* tool);
    void onColorSelected(const QColor& color);
    void onWidthActivated(int index);
    void onTargetChanged(int index);
    void onCommandButtonClicked();
    void onMoreClicked();
    void onDocumentChanged();

    void createWidgets();
    void retranslate();
    void updateControls();
    QString createStatusHint() const;
    void updateStatusHint(bool isShown);
    PDFIconButton* createCommandButton(const QString& commandId, const QString& iconName);
    void beginSession(const QString& property, const QString& description);
    void updateCompaction();
    void applyCompaction(int level);
    PDFAppAnnotationController* getController() const;

    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppCommentPopupIcon> m_typeIcon;
    QPointer<PDFLabel> m_nameLabel;
    QPointer<PDFLabel> m_authorLabel;
    QPointer<PDFAppFloatingBarDivider> m_targetDivider;
    QPointer<PDFSegmentedControl> m_targetControl;
    QPointer<PDFColorSwatchRow> m_swatchRow;
    QPointer<PDFAppFloatingBarDivider> m_widthDivider;
    QPointer<PDFComboBox> m_widthComboBox;
    QPointer<PDFAppFloatingBarDivider> m_alignDivider;
    std::vector<std::pair<QString, QPointer<PDFIconButton>>> m_commandButtons;
    QPointer<PDFAppFloatingBarDivider> m_actionsDivider;
    QPointer<PDFIconButton> m_moreButton;
    QString m_sessionKey;
    QString m_statusHint;
    quint64 m_sessionCounter = 0;
    bool m_isFillTarget = false;
    bool m_isAuthorShown = false;
    bool m_isTargetShown = false;
    int m_compaction = 0;
    bool m_isUpdating = false;
};

}   // namespace pdf

#endif // PDFAPPANNOTATIONBAR_H
