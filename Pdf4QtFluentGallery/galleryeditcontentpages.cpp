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

// Samples of the components of Edit Content (stage E1, zadani_gui.md 4.25) on the
// page of the panels: Expander card, Path bar, toggle actions of a row (eye, lock)
// and the mixed state of the fields, the combo box and the row of swatches.

#include "gallerypages.h"

#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentsections.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"

#include <QHBoxLayout>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QtMath>

namespace pdffluentgallery
{

/// Rows of the objects of Edit Content in the gallery: a bookmark row with the
/// toggle actions eye (hidden) and lock (locked) at its trailing side, read from
/// two roles of the model (edit-content-v2-objects-nested 1647:22520)
class GalleryToggleRowDelegate : public pdf::PDFBookmarkDelegate
{
private:
    using BaseClass = pdf::PDFBookmarkDelegate;

public:
    static constexpr int HiddenRole = Qt::UserRole + 1;
    static constexpr int LockedRole = Qt::UserRole + 2;
    static constexpr int HideAction = 1;
    static constexpr int LockAction = 2;

    /// The action rectangles: 20 x 20 at the trailing side, 4 apart, 8 from the edge
    static constexpr int ACTION_EXTENT = 20;
    static constexpr int ACTION_GAP = 4;
    static constexpr int ACTION_MARGIN = 8;

    using BaseClass::BaseClass;

    virtual QList<Action> getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QList<Action> actions;
        if (!index.isValid())
        {
            return actions;
        }

        const QRectF rect(option.rect);
        const bool isRightToLeft = option.direction == Qt::RightToLeft;
        const qreal top = rect.top() + qFloor((rect.height() - ACTION_EXTENT) / 2.0);
        for (int i = 0; i < 2; ++i)
        {
            const qreal offset = ACTION_MARGIN + i * (ACTION_EXTENT + ACTION_GAP);
            const qreal left = isRightToLeft ? rect.left() + offset : rect.right() + 1.0 - offset - ACTION_EXTENT;

            Action action;
            action.id = i == 0 ? LockAction : HideAction;
            action.rect = QRectF(left, top, ACTION_EXTENT, ACTION_EXTENT);
            action.checkable = true;
            action.role = i == 0 ? LockedRole : HiddenRole;
            action.checked = index.data(action.role).toBool();
            action.text = i == 0 ? QStringLiteral("Lock") : QStringLiteral("Hide");
            action.iconName = i == 0 ? QStringLiteral("unlock") : QStringLiteral("eye");
            action.checkedIconName = i == 0 ? QStringLiteral("lock") : QStringLiteral("eye-off");
            actions.append(action);
        }
        return actions;
    }

protected:
    virtual bool hasActions() const override { return true; }

    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const pdf::PDFComponentPainter::State& state) const override
    {
        BaseClass::paintItem(painter, option, index, state);
        paintToggleActions(painter, option, index, state);
    }
};

void GalleryPages::addEditContentPanels(GalleryBuilder& builder)
{
    QWidget* page = builder.page();

    // Expander card: the sections of the right panel of Edit Content
    builder.block(QStringLiteral("Expander card (PDFExpanderCard) - heading, badge, Expand / Collapse, summary of a collapsed card"), QStringLiteral("1647:21843"));

    pdf::PDFExpanderCard* collapsed = new pdf::PDFExpanderCard(QStringLiteral("Transparency"), page);
    collapsed->setSummaryText(QStringLiteral("Opacity 100 % · blend mode Normal"));
    collapsed->setExpandedWidget(new pdf::PDFLabel(QStringLiteral("Fill opacity, Stroke opacity, Blend mode"), collapsed));
    collapsed->setFixedWidth(287);
    builder.live(QStringLiteral("Live collapsed (the link expands it)"), collapsed, QStringLiteral("1647:21843"));

    pdf::PDFExpanderCard* expanded = new pdf::PDFExpanderCard(QStringLiteral("Text"), page);
    expanded->setContentWidget(new pdf::PDFLabel(QStringLiteral("Font, size, line spacing"), expanded));
    expanded->setExpandedWidget(new pdf::PDFLabel(QStringLiteral("Word spacing, horizontal scale, rise"), expanded));
    expanded->setExpanded(true);
    expanded->setFixedWidth(287);
    builder.live(QStringLiteral("Live expanded"), expanded, QStringLiteral("1647:48204"));

    pdf::PDFExpanderCard* selection = new pdf::PDFExpanderCard(QStringLiteral("Selection"), page);
    selection->setBadgeText(QStringLiteral("Text"));
    QWidget* selectionContent = new QWidget(selection);
    QVBoxLayout* selectionLayout = new QVBoxLayout(selectionContent);
    selectionLayout->setContentsMargins(0, 0, 0, 0);
    selectionLayout->setSpacing(8);
    selectionLayout->addWidget(new pdf::PDFLabel(QStringLiteral("Text · 3 lines"), pdf::PDFThemeFonts::Role::BodySemiBold, pdf::PDFThemeColors::TextPrimary, selectionContent));
    selectionLayout->addWidget(new pdf::PDFLabel(QStringLiteral("274 characters · Calibri 9 pt"), pdf::PDFThemeFonts::Role::CaptionRegular, pdf::PDFThemeColors::TextTertiary, selectionContent));
    pdf::PDFPathBar* typePath = new pdf::PDFPathBar(QStringList{ QStringLiteral("Page 3"), QStringLiteral("Text") }, selectionContent);
    typePath->setBarSize(pdf::PDFPathBar::Size::Small);
    selectionLayout->addWidget(typePath);
    selection->setContentWidget(selectionContent);
    selection->setFixedWidth(287);
    builder.live(QStringLiteral("Live with a badge, no link"), selection, QStringLiteral("1647:21722"));
    builder.row();

    pdf::PDFExpanderCard* disabledCard = new pdf::PDFExpanderCard(QStringLiteral("Print production"), page);
    disabledCard->setSummaryText(QStringLiteral("Overprint off · Relative colorimetric"));
    disabledCard->setEnabled(false);
    disabledCard->setFixedWidth(287);
    builder.live(QStringLiteral("Live disabled"), disabledCard);

    pdf::PDFExpanderCard* rightToLeftCard = new pdf::PDFExpanderCard(QStringLiteral("Arrange"), page);
    rightToLeftCard->setSummaryText(QStringLiteral("Align · order · group · path operations"));
    rightToLeftCard->setLayoutDirection(Qt::RightToLeft);
    rightToLeftCard->setFixedWidth(287);
    builder.live(QStringLiteral("Live right-to-left"), rightToLeftCard);
    builder.row();

    // Path bar: breadcrumbs without routes (the bar of a group, the path of types)
    builder.block(QStringLiteral("Path bar (PDFPathBar) - links, the current segment, shortened in the middle"), QStringLiteral("1647:42965"));
    const QStringList groupPath{ QStringLiteral("Page 3"), QStringLiteral("Chart area"), QStringLiteral("Revenue chart") };
    builder.live(QStringLiteral("Live, the bar of a group"), new pdf::PDFPathBar(groupPath, page), QStringLiteral("1647:42965"));

    const QStringList longPath{ QStringLiteral("Page 3"), QStringLiteral("Header and footer"), QStringLiteral("Chart area"), QStringLiteral("Legend"),
                                QStringLiteral("Revenue chart"), QStringLiteral("Bars 2026") };
    pdf::PDFPathBar* shortened = new pdf::PDFPathBar(longPath, page);
    shortened->setFixedWidth(240);
    builder.live(QStringLiteral("Live shortened (240 px)"), shortened);

    pdf::PDFPathBar* small = new pdf::PDFPathBar(groupPath, page);
    small->setBarSize(pdf::PDFPathBar::Size::Small);
    builder.live(QStringLiteral("Live small (Selection card)"), small, QStringLiteral("1647:21730"));
    builder.row();

    pdf::PDFPathBar* disabledPath = new pdf::PDFPathBar(groupPath, page);
    disabledPath->setEnabled(false);
    builder.live(QStringLiteral("Live disabled"), disabledPath);
    pdf::PDFPathBar* rightToLeftPath = new pdf::PDFPathBar(groupPath, page);
    rightToLeftPath->setLayoutDirection(Qt::RightToLeft);
    builder.live(QStringLiteral("Live right-to-left"), rightToLeftPath);
    builder.row();

    // Toggle actions of a row: eye and lock, shown permanently while on
    builder.block(QStringLiteral("Toggle actions of a row (PDFRowDelegate::Action::checkable) - eye and lock, on: always, off: under the pointer"), QStringLiteral("1647:22520"));
    QStandardItemModel* model = new QStandardItemModel(page);
    struct ObjectSample
    {
        const char* title;
        const char* subtitle;
        bool hidden;
        bool locked;
    };
    static constexpr ObjectSample objectSamples[] =
    {
        { "Text", "Paragraph · 3 lines", false, false },
        { "Path", "Table rules", false, true },
        { "Shading", "Footer band", true, false },
        { "Image", "Logo", true, true }
    };
    for (const ObjectSample& sample : objectSamples)
    {
        QStandardItem* item = new QStandardItem(QString::fromUtf8(sample.title));
        item->setData(QString::fromUtf8(sample.subtitle), pdf::PDFComponentRoles::SubtitleRole);
        item->setData(sample.hidden, GalleryToggleRowDelegate::HiddenRole);
        item->setData(sample.locked, GalleryToggleRowDelegate::LockedRole);
        model->appendRow(item);
    }
    pdf::PDFListView* list = new pdf::PDFListView(page);
    list->setModel(model);
    list->setItemDelegate(new GalleryToggleRowDelegate(list));
    list->setFixedSize(300, 220);
    builder.live(QStringLiteral("Live list (click the eye or the lock)"), list, QStringLiteral("1647:22520"));
    builder.row();

    // Mixed state: a value, which differs between the selected objects
    builder.block(QStringLiteral("Mixed state (setMixed) - spin boxes, combo box, row of swatches"), QStringLiteral("1647:49437"));
    pdf::PDFSpinBox* mixedSpin = new pdf::PDFSpinBox(page);
    mixedSpin->setRange(0, 100);
    mixedSpin->setValue(50);
    mixedSpin->setMixed(true);
    mixedSpin->setFixedWidth(100);
    builder.live(QStringLiteral("Live spin box, Mixed"), mixedSpin, QStringLiteral("1647:49458"));

    pdf::PDFDoubleSpinBox* mixedDouble = new pdf::PDFDoubleSpinBox(page);
    mixedDouble->setRange(0.0, 100.0);
    mixedDouble->setValue(1.5);
    mixedDouble->setDisplaySuffix(QStringLiteral("pt"));
    mixedDouble->setMixed(true);
    mixedDouble->setFixedWidth(120);
    builder.live(QStringLiteral("Live double spin box, Mixed"), mixedDouble);

    // A suffix of Qt (setSuffix, a part of the edited text): only "Mixed" in the state
    pdf::PDFSpinBox* mixedSuffixed = new pdf::PDFSpinBox(page);
    mixedSuffixed->setRange(0, 100);
    mixedSuffixed->setSuffix(QStringLiteral(" pt"));
    mixedSuffixed->setValue(12);
    mixedSuffixed->setMixed(true);
    mixedSuffixed->setFixedWidth(100);
    builder.live(QStringLiteral("Live spin box with a suffix of Qt, Mixed"), mixedSuffixed);

    pdf::PDFComboBox* mixedCombo = new pdf::PDFComboBox(page);
    mixedCombo->addItems({ QStringLiteral("DeviceRGB"), QStringLiteral("DeviceCMYK"), QStringLiteral("DeviceGray") });
    mixedCombo->setMixed(true);
    mixedCombo->setFixedWidth(150);
    builder.live(QStringLiteral("Live combo box, Mixed"), mixedCombo, QStringLiteral("1647:49442"));

    pdf::PDFSpinBox* disabledMixed = new pdf::PDFSpinBox(page);
    disabledMixed->setMixed(true);
    disabledMixed->setEnabled(false);
    disabledMixed->setFixedWidth(100);
    builder.live(QStringLiteral("Live disabled, Mixed"), disabledMixed);
    builder.row();

    pdf::PDFColorSwatchRow* mixedRow = new pdf::PDFColorSwatchRow(page);
    mixedRow->setOptions(pdf::PDFColorSwatchRow::Option::Inline | pdf::PDFColorSwatchRow::Option::More);
    mixedRow->setColor(pdf::PDFStylePainter::color(pdf::PDFThemeColors::AnnotationBlack));
    mixedRow->setMixed(true);
    builder.live(QStringLiteral("Live row of swatches, Mixed (no swatch selected)"), mixedRow);
    builder.note(QStringLiteral("Entering and leaving the mixed state by the program emits no value signal; the user leaves it by entering a value. "
                                "A panel writes the value of a spin box in valueEntered (Enter, leaving the field after typing, a step) - also an unchanged value."));
}

}   // namespace pdffluentgallery
