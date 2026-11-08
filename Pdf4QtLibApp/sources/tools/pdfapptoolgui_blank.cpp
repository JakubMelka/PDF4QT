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

#include "pdfapptoolgui_blank.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfapppagelayout.h"
#include "pdfapppagesizepreview.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocument.h"
#include "pdfdocumenttool_merge.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"

#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pdf
{

/// Helpers of the GUI of Blank PDF
class PDFAppBlankToolGUIHelper
{
public:
    PDFAppBlankToolGUIHelper() = delete;

    using Tool = PDFDocumentToolBlank;

    /// Property of a row of the menu of the size: the id of its size, or the size of a document
    static constexpr const char* SIZE_PROPERTY = "pdf4qtBlankSize";

    /// Steps of the keys Page Up / Page Down in a field
    static constexpr int PAGE_STEP = 10;

    /// The values of the sides of a custom size are kept to a millionth of a millimeter (a
    /// named size is no new value by the rounding of its conversion, 210.0000000001 mm)
    static constexpr double MILLIMETER_PRECISION = 1e6;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// Millimeters of points (mm = points x 25.4 / 72) to a millionth
    static double getMillimeters(double points)
    {
        return std::round(points * 25.4 / 72.0 * MILLIMETER_PRECISION) / MILLIMETER_PRECISION;
    }

    /// Icon of a size of the menu (337:2465: file-tall, presentation of 16:9, sliders of Custom)
    static QString getSizeIconName(const QString& sizeId)
    {
        if (sizeId == QLatin1String(Tool::SIZE_16X9))
        {
            return QStringLiteral("presentation");
        }
        if (sizeId == QLatin1String(Tool::SIZE_CUSTOM))
        {
            return QStringLiteral("sliders");
        }
        return QStringLiteral("file-tall");
    }

    /// A page is wider than high
    static bool isLandscape(const QSizeF& size)
    {
        return size.width() > size.height();
    }

    /// The count of a page count (a page count of the core is at most 10 000)
    static int toCount(qint64 value)
    {
        return int(qBound(qint64(0), value, qint64(std::numeric_limits<int>::max())));
    }
};

// ------------------------------------------------------------- PDFAppBlankPageModel

PDFAppBlankPageModel::PDFAppBlankPageModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppBlankPageModel::~PDFAppBlankPageModel() = default;

void PDFAppBlankPageModel::setPages(int pageCount, const QSizeF& pageSize)
{
    pageCount = qMax(0, pageCount);
    if (pageCount > m_pageCount)
    {
        beginInsertRows(QModelIndex(), m_pageCount, pageCount - 1);
        m_pageCount = pageCount;
        endInsertRows();
    }
    else if (pageCount < m_pageCount)
    {
        beginRemoveRows(QModelIndex(), pageCount, m_pageCount - 1);
        m_pageCount = pageCount;
        endRemoveRows();
    }

    if (m_pageSize != pageSize)
    {
        m_pageSize = pageSize;
        if (m_pageCount > 0)
        {
            // One change of all rows: their tiles have another size
            Q_EMIT dataChanged(index(0), index(m_pageCount - 1), { PDFComponentRoles::PageSizeRole, Qt::SizeHintRole });
        }
    }
}

int PDFAppBlankPageModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_pageCount;
}

QVariant PDFAppBlankPageModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.model() != this || index.row() < 0 || index.row() >= m_pageCount)
    {
        return QVariant();
    }

    switch (role)
    {
        case Qt::DisplayRole:
            return PDFAppLocale::formatNumber(index.row() + 1);

        case Qt::AccessibleTextRole:
            return PDFAppBlankToolGUI::tr("Page %1 of %2").arg(PDFAppLocale::formatNumber(index.row() + 1), PDFAppLocale::formatNumber(m_pageCount));

        case PDFComponentRoles::PageSizeRole:
            return m_pageSize;

        default:
            break;
    }
    return QVariant();
}

// ---------------------------------------------------------- PDFAppBlankPageDelegate

PDFAppBlankPageDelegate::PDFAppBlankPageDelegate(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppBlankPageDelegate::~PDFAppBlankPageDelegate() = default;

QSize PDFAppBlankPageDelegate::getPaperSize(const QSizeF& pageSize)
{
    if (!pageSize.isValid() || pageSize.isEmpty() || !std::isfinite(pageSize.width()) || !std::isfinite(pageSize.height()))
    {
        return QSize(SLOT_WIDTH, SLOT_HEIGHT);
    }

    // The paper of the proportions of the page fitted into the slot, whole pixels (92 x 130 of A4)
    const QSizeF fitted = pageSize.scaled(QSizeF(SLOT_WIDTH, SLOT_HEIGHT), Qt::KeepAspectRatio);
    return QSize(qBound(1, int(std::round(fitted.width())), SLOT_WIDTH), qBound(1, int(std::round(fitted.height())), SLOT_HEIGHT));
}

QSize PDFAppBlankPageDelegate::getTileSize(const QSizeF& pageSize)
{
    // 337:2109: the padding 8, the paper, the gap 6, the number 13, the padding 8
    return QSize(TILE_WIDTH, PADDING + getPaperSize(pageSize).height() + LABEL_GAP + LABEL_HEIGHT + PADDING);
}

QRect PDFAppBlankPageDelegate::getTileRect(const QRect& cell, const QSizeF& pageSize, Qt::LayoutDirection direction)
{
    // The tile at the leading side of its cell, the gaps of the grid after it
    const QSize tile = getTileSize(pageSize);
    const int left = direction == Qt::RightToLeft ? cell.right() + 1 - tile.width() : cell.left();
    return QRect(QPoint(left, cell.top()), tile);
}

QRect PDFAppBlankPageDelegate::getPaperRect(const QRect& cell, const QSizeF& pageSize, Qt::LayoutDirection direction)
{
    const QRect tile = getTileRect(cell, pageSize, direction);
    const QSize paper = getPaperSize(pageSize);
    return QRect(QPoint(tile.left() + (tile.width() - paper.width()) / 2, tile.top() + PADDING), paper);
}

QSize PDFAppBlankPageDelegate::getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const
{
    Q_UNUSED(option);
    Q_UNUSED(width);
    const QSize tile = getTileSize(index.data(PDFComponentRoles::PageSizeRole).toSizeF());
    return QSize(tile.width() + COLUMN_GAP, tile.height() + ROW_GAP);
}

void PDFAppBlankPageDelegate::paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const
{
    using Color = PDFThemeColors::Color;

    const QSizeF pageSize = index.data(PDFComponentRoles::PageSizeRole).toSizeF();
    const Qt::LayoutDirection direction = option.direction;
    const QRectF tile(getTileRect(option.rect, pageSize, direction));
    const QRectF paper(getPaperRect(option.rect, pageSize, direction));
    const bool isSelected = state.isSelected();
    const bool isEnabled = state.isEnabled();
    const PDFStylePainter::Radii radii(RADIUS);

    painter->setRenderHint(QPainter::Antialiasing, true);

    // thumbnail-1 337:2109: the selected tile bg/accent-subtle, the others the hover only
    if (isSelected)
    {
        PDFStylePainter::fill(painter, tile, radii, isEnabled ? Color::BgAccentSubtle : Color::BgMuted);
    }
    else if (state.isHovered())
    {
        PDFStylePainter::fill(painter, tile, radii, state.isPressed() ? Color::BgPressed : Color::BgHover);
    }

    // The paper (reading/paper/page) with its border inside: 2 px bg/accent of the selected
    // one (337:2110), 1 px border/default of the others (337:2113)
    PDFStylePainter::fillRect(painter, paper, Color::ReadingPaperPage);
    const bool isAccentBorder = isSelected && isEnabled;
    PDFStylePainter::strokeInside(painter, paper, PDFStylePainter::Radii(0.0), isAccentBorder ? Color::BgAccent : Color::BorderDefault, isSelected ? 2.0 : 1.0);

    // The number Medium 11, 6 px below the paper: text/accent of the selected page
    const QRectF label(tile.left(), paper.bottom() + LABEL_GAP, tile.width(), LABEL_HEIGHT);
    const Color textColor = !isEnabled ? Color::TextDisabled : (isSelected ? Color::TextAccent : Color::TextSecondary);
    PDFStylePainter::text(painter, label, Qt::AlignCenter | Qt::TextSingleLine, index.data(Qt::DisplayRole).toString(), PDFThemeFonts::getFont(QFont::Medium, 11), PDFStylePainter::color(textColor));

    if (state.hasFocusRing())
    {
        PDFStylePainter::focusRing(painter, tile, radii, false);
    }
}

// -------------------------------------------------------------- PDFAppBlankPageView

PDFAppBlankPageView::PDFAppBlankPageView(QWidget* parent) :
    BaseClass(parent)
{
    // 337:2108: the first tile at the leading edge and the top of the list
    setViewportMargins(0, 0, 0, 0);
    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setDragEnabled(false);
    setResizeMode(QListView::Adjust);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setLayoutMode(QListView::Batched);
    setBatchSize(500);
    setSpacing(0);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setAutoFillBackground(false);
}

PDFAppBlankPageView::~PDFAppBlankPageView() = default;

void PDFAppBlankPageView::setPageSize(const QSizeF& pageSize)
{
    const QSize tile = PDFAppBlankPageDelegate::getTileSize(pageSize);
    setGridSize(QSize(tile.width() + PDFAppBlankPageDelegate::COLUMN_GAP, tile.height() + PDFAppBlankPageDelegate::ROW_GAP));
}

// ------------------------------------------------------------ PDFAppBlankSizePanel

PDFAppBlankSizePanel::PDFAppBlankSizePanel(QWidget* parent) :
    BaseClass(nullptr, PANEL_WIDTH, parent)
{
    setObjectName(QStringLiteral("toolPanel-pageSize"));
}

PDFAppBlankSizePanel::~PDFAppBlankSizePanel() = default;

void PDFAppBlankSizePanel::addDocumentGroup()
{
    if (m_documentGroup)
    {
        return;
    }

    // divider 337:2510, caption 337:2512 "SAME SIZE AS AN OPEN DOCUMENT" and the rows (gap 2)
    m_documentGroup = new QWidget(this);
    m_documentGroup->setObjectName(QStringLiteral("toolBlankDocumentGroup"));
    QVBoxLayout* layout = new QVBoxLayout(m_documentGroup);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ITEM_GAP);
    getContentLayout()->addWidget(m_documentGroup);

    // The divider and the caption of the panel move into the group (hidden with it)
    QWidget* divider = addGroupDivider();
    PDFLabel* caption = addGroupCaption(tr("SAME SIZE AS AN OPEN DOCUMENT"));
    for (QWidget* widget : { divider, caption ? caption->parentWidget() : nullptr })
    {
        if (widget)
        {
            getContentLayout()->removeWidget(widget);
            widget->setParent(m_documentGroup);
            layout->addWidget(widget);
        }
    }
    m_documentGroup->hide();
}

void PDFAppBlankSizePanel::updateState()
{
    BaseClass::updateState();

    // K27: the rows are the open documents of the window now (F13), read as the panel opens
    clearDocumentRows();
    const QToolButton* button = getButton();
    const PDFAppMainWindow* window = button ? qobject_cast<const PDFAppMainWindow*>(button->window()) : nullptr;
    if (!m_documentGroup || !window)
    {
        return;
    }

    for (PDFAppDocumentTab* tab : window->getDocumentTabs())
    {
        const PDFAppDocument* appDocument = tab ? tab->getDocument() : nullptr;
        const PDFDocument* document = appDocument && appDocument->isReady() ? appDocument->getDocument().data() : nullptr;
        const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
        if (!catalog || catalog->getPageCount() == 0)
        {
            continue;
        }

        // The page shown to the user (Current page), the visible size: the crop box turned by /Rotate
        const PDFAppDocumentView* view = tab->getView();
        PDFInteger pageIndex = view ? view->getDisplayedPageIndex() : 0;
        if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
        {
            pageIndex = 0;
        }
        const PDFPage* page = catalog->getPage(size_t(pageIndex));
        const QSizeF pageSize = page ? page->getRotatedCropBox().size() : QSizeF();
        if (!pageSize.isValid() || pageSize.isEmpty())
        {
            continue;
        }

        const QString dimensions = tr("%1 × %2 mm").arg(PDFAppBlankToolGUI::formatMillimeters(pageSize.width() * 25.4 / 72.0), PDFAppBlankToolGUI::formatMillimeters(pageSize.height() * 25.4 / 72.0));
        const QString description = tr("Current page %1 · %2").arg(PDFAppLocale::formatNumber(pageIndex + 1), dimensions);
        PDFTileRow* row = new PDFTileRow(appDocument->getTitle(), description, QStringLiteral("file"), PDFTileRow::Kind::MenuItem, m_documentGroup);
        row->setObjectName(QStringLiteral("toolBlankDocument"));
        row->setTitleKind(PDFTileRow::TextKind::FileName);
        row->setProperty(PDFAppBlankToolGUIHelper::SIZE_PROPERTY, pageSize);
        connect(row, &PDFTileRow::clicked, this, &PDFAppBlankSizePanel::onDocumentRowClicked);
        m_documentGroup->layout()->addWidget(row);
        m_documentRows.emplace_back(row);
    }
    m_documentGroup->setVisible(!m_documentRows.empty());
}

void PDFAppBlankSizePanel::clearDocumentRows()
{
    // The rows of the last opening: the panel is closed, no row is a sender now
    for (const QPointer<PDFTileRow>& row : m_documentRows)
    {
        delete row.data();
    }
    m_documentRows.clear();
}

void PDFAppBlankSizePanel::onDocumentRowClicked()
{
    const QObject* row = sender();
    const QSizeF pageSize = row ? row->property(PDFAppBlankToolGUIHelper::SIZE_PROPERTY).toSizeF() : QSizeF();

    // The panel closes first (it may run code of the application), the signal is the last thing
    QPointer<PDFAppBlankSizePanel> guard(this);
    close();
    if (guard && pageSize.isValid())
    {
        Q_EMIT documentSizeChosen(pageSize);
    }
}

// -------------------------------------------------------------- PDFAppBlankToolGUI

PDFAppBlankToolGUI::PDFAppBlankToolGUI(QObject* parent) :
    BaseClass(parent)
{
    m_pageModel = new PDFAppBlankPageModel(this);
}

PDFAppBlankToolGUI::~PDFAppBlankToolGUI() = default;

void PDFAppBlankToolGUI::registerTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolBlank::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolBlank>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppBlankToolGUI>>());
    }
}

QString PDFAppBlankToolGUI::formatMillimeters(double millimeters)
{
    // K25: one decimal at most, no trailing zero ("215.9", "297", "5,080")
    if (!std::isfinite(millimeters))
    {
        return QString();
    }
    const double tenths = std::round(millimeters * 10.0);
    const QLocale locale = PDFAppLocale::getUiLocale();
    if (std::fmod(tenths, 10.0) == 0.0)
    {
        return locale.toString(qint64(tenths / 10.0));
    }
    return locale.toString(tenths / 10.0, 'f', 1);
}

QString PDFAppBlankToolGUI::formatDimensions(const QSizeF& pageSize)
{
    return tr("%1 × %2 mm").arg(formatMillimeters(pageSize.width() * 25.4 / 72.0), formatMillimeters(pageSize.height() * 25.4 / 72.0));
}

QString PDFAppBlankToolGUI::getParameterName(Field field)
{
    using Helper = PDFAppBlankToolGUIHelper;
    switch (field)
    {
        case Field::Pages:
            return Helper::name(Helper::Tool::PARAMETER_PAGE_COUNT);
        case Field::Width:
            return Helper::name(Helper::Tool::PARAMETER_WIDTH);
        case Field::Height:
            return Helper::name(Helper::Tool::PARAMETER_HEIGHT);
    }
    return QString();
}

PDFToolBarField* PDFAppBlankToolGUI::getField(Field field) const
{
    switch (field)
    {
        case Field::Pages:
            return m_pagesField;
        case Field::Width:
            return m_widthField;
        case Field::Height:
            return m_heightField;
    }
    return nullptr;
}

PDFAppBlankToolGUI::Field PDFAppBlankToolGUI::getFieldOf(const QObject* object) const
{
    if (object && object == m_widthField.data())
    {
        return Field::Width;
    }
    if (object && object == m_heightField.data())
    {
        return Field::Height;
    }
    return Field::Pages;
}

QString PDFAppBlankToolGUI::formatFieldValue(Field field, double value)
{
    // The fields show the value as it is (to a hundredth of a millimeter, no trailing zeros)
    const QLocale locale = PDFAppLocale::getUiLocale();
    if (field == Field::Pages || !std::isfinite(value))
    {
        return locale.toString(qint64(std::llround(std::isfinite(value) ? value : 0.0)));
    }

    const double hundredths = std::round(value * 100.0);
    if (std::fmod(hundredths, 100.0) == 0.0)
    {
        return locale.toString(qint64(hundredths / 100.0));
    }
    QString text = locale.toString(hundredths / 100.0, 'f', 2);
    if (text.endsWith(QLatin1Char('0')))
    {
        text.chop(1);
    }
    return text;
}

std::optional<double> PDFAppBlankToolGUI::parseField(Field field, const QString& text, QString* error) const
{
    using Tool = PDFDocumentToolBlank;

    // The number in the locale of the user interface, or with a dot (a value pasted from elsewhere)
    const QString trimmed = text.trimmed();
    const QLocale locale = PDFAppLocale::getUiLocale();
    bool isOk = false;
    double value = locale.toDouble(trimmed, &isOk);
    if (!isOk)
    {
        value = QLocale::c().toDouble(trimmed, &isOk);
    }

    if (field == Field::Pages)
    {
        const bool isWhole = isOk && std::isfinite(value) && std::floor(value) == value;
        if (!isWhole || value < Tool::MINIMUM_PAGE_COUNT || value > Tool::MAXIMUM_PAGE_COUNT)
        {
            *error = tr("The number of pages must be a whole number from %1 to %2.").arg(PDFAppLocale::formatNumber(Tool::MINIMUM_PAGE_COUNT), PDFAppLocale::formatNumber(Tool::MAXIMUM_PAGE_COUNT));
            return std::nullopt;
        }
        return value;
    }

    // K26: a side of 3 to 14 400 points
    if (!isOk || !Tool::isValidSide(Tool::getPoints(value)))
    {
        const QString limits = tr("from %1 to %2 mm").arg(formatMillimeters(Tool::MINIMUM_SIDE_MILLIMETERS), formatMillimeters(Tool::MAXIMUM_SIDE_MILLIMETERS));
        *error = field == Field::Width ? tr("The width must be a number %1.").arg(limits) : tr("The height must be a number %1.").arg(limits);
        return std::nullopt;
    }
    return value;
}

QVariantMap PDFAppBlankToolGUI::getEffectiveValues() const
{
    using Helper = PDFAppBlankToolGUIHelper;

    const PDFAppToolParameterModel* model = getModel();
    QVariantMap values = model ? model->getValues() : QVariantMap();
    for (const auto& [field, text] : m_fieldTexts)
    {
        QString error;
        const std::optional<double> value = parseField(field, text, &error);
        if (!value)
        {
            continue;
        }
        if (field == Field::Pages)
        {
            values.insert(getParameterName(field), qint64(*value));
        }
        else
        {
            values.insert(getParameterName(field), *value);
        }
    }

    // A custom size has the orientation of its sides (the core reads them)
    if (values.value(Helper::name(Helper::Tool::PARAMETER_PAGE_SIZE)).toString() == QLatin1String(Helper::Tool::SIZE_CUSTOM))
    {
        const bool isLandscape = values.value(Helper::name(Helper::Tool::PARAMETER_WIDTH)).toDouble() > values.value(Helper::name(Helper::Tool::PARAMETER_HEIGHT)).toDouble();
        values.insert(Helper::name(Helper::Tool::PARAMETER_ORIENTATION), Helper::name(isLandscape ? Helper::Tool::ORIENTATION_LANDSCAPE : Helper::Tool::ORIENTATION_PORTRAIT));
    }
    return values;
}

QSizeF PDFAppBlankToolGUI::getEffectivePageSize() const
{
    return PDFDocumentToolBlank::getPageSize(getEffectiveValues(), nullptr);
}

int PDFAppBlankToolGUI::getEffectivePageCount() const
{
    using Helper = PDFAppBlankToolGUIHelper;
    const qint64 count = getEffectiveValues().value(Helper::name(Helper::Tool::PARAMETER_PAGE_COUNT)).toLongLong();
    return count >= Helper::Tool::MINIMUM_PAGE_COUNT && count <= Helper::Tool::MAXIMUM_PAGE_COUNT ? Helper::toCount(count) : 0;
}

QString PDFAppBlankToolGUI::getFieldError() const
{
    for (Field field : { Field::Width, Field::Height, Field::Pages })
    {
        const auto it = m_fieldErrors.find(field);
        if (it != m_fieldErrors.cend() && !it->second.isEmpty())
        {
            return it->second;
        }
    }
    return QString();
}

QString PDFAppBlankToolGUI::getSizeTitle(const QString& sizeId) const
{
    using Helper = PDFAppBlankToolGUIHelper;

    // The title of the option of the core (translated with the definitions)
    const PDFAppToolParameterModel* model = getModel();
    const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), Helper::name(Helper::Tool::PARAMETER_PAGE_SIZE)) : nullptr;
    if (definition)
    {
        for (const PDFDocumentToolParameterOption& option : definition->getOptions())
        {
            if (option.id == sizeId)
            {
                return option.getTitle();
            }
        }
    }
    return sizeId;
}

QString PDFAppBlankToolGUI::getSizeDescription(const QVariantMap& values) const
{
    using Helper = PDFAppBlankToolGUIHelper;

    const QString sizeId = values.value(Helper::name(Helper::Tool::PARAMETER_PAGE_SIZE)).toString();
    const QSizeF pageSize = PDFDocumentToolBlank::getPageSize(values, nullptr);
    if (sizeId == QLatin1String(Helper::Tool::SIZE_CUSTOM))
    {
        // 356:10042 "Custom · 200 × 280 mm"
        return tr("Custom · %1").arg(formatDimensions(pageSize));
    }

    // 337:2140 "A4 portrait", 1390:47108 "16:9 landscape"
    const QString title = getSizeTitle(sizeId);
    return Helper::isLandscape(pageSize) ? tr("%1 landscape", "a named page size and its orientation").arg(title) : tr("%1 portrait", "a named page size and its orientation").arg(title);
}

// ------------------------------------------------------------------------- parts

void PDFAppBlankToolGUI::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppBlankToolGUIHelper;

    QWidget* parent = builder.getWidget();
    m_sizeRows.clear();

    // tb-page-size 337:2075 (Toolbar menu button, icon/file) with panel-page-size 337:2465
    QToolButton* sizeButton = PDFAppDocumentToolbar::createToolbarMenuButton(QStringLiteral("toolMenu-pageSize"), QStringLiteral("file"), parent);
    builder.addWidget(sizeButton);
    m_sizePanel = new PDFAppBlankSizePanel(sizeButton);
    if (PDFAppToolToolbar* toolbar = getToolbar())
    {
        toolbar->addPanel(sizeButton, m_sizePanel);
    }

    const PDFAppToolParameterModel* model = getModel();
    const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), Helper::name(Helper::Tool::PARAMETER_PAGE_SIZE)) : nullptr;
    if (definition)
    {
        // item-A4 337:2466 ...: the tile with the icon of the size, the title and the dimensions
        for (const PDFDocumentToolParameterOption& option : definition->getOptions())
        {
            PDFTileRow* row = m_sizePanel->addRow(option.getTitle(), option.getDescription(), Helper::getSizeIconName(option.id), PDFTileRow::Kind::MenuItem);
            row->setObjectName(QStringLiteral("toolOption-") + option.id);
            row->setProperty(Helper::SIZE_PROPERTY, option.id);
            row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
            connect(row, &PDFTileRow::clicked, this, &PDFAppBlankToolGUI::onSizeRowClicked);
            m_sizeRows[option.id] = row;
        }
    }
    m_sizePanel->addDocumentGroup();
    connect(m_sizePanel.data(), &PDFAppBlankSizePanel::documentSizeChosen, this, &PDFAppBlankToolGUI::onDocumentSizeChosen);

    // tf-W 356:10137, tf-H 356:10142: only for Custom
    m_widthGroup = createFieldGroup(Field::Width, tr("W", "the width of a page"), parent);
    builder.addWidget(m_widthGroup);
    m_heightGroup = createFieldGroup(Field::Height, tr("H", "the height of a page"), parent);
    builder.addWidget(m_heightGroup);

    // divider, tb-portrait 337:2082 and tb-landscape 337:2092 (Toolbar button, Active), divider
    builder.addActionDivider();
    m_portraitButton = PDFAppDocumentToolbar::createToolbarButton(QStringLiteral("toolPortrait"), QStringLiteral("file"), parent);
    m_portraitButton->setCheckable(true);
    connect(m_portraitButton.data(), &QToolButton::clicked, this, &PDFAppBlankToolGUI::onPortraitClicked);
    builder.addWidget(m_portraitButton);
    m_landscapeButton = PDFAppDocumentToolbar::createToolbarButton(QStringLiteral("toolLandscape"), QStringLiteral("file-wide"), parent);
    m_landscapeButton->setCheckable(true);
    connect(m_landscapeButton.data(), &QToolButton::clicked, this, &PDFAppBlankToolGUI::onLandscapeClicked);
    builder.addWidget(m_landscapeButton);
    for (QToolButton* button : { m_portraitButton.data(), m_landscapeButton.data() })
    {
        const QString text = button == m_portraitButton.data() ? tr("Portrait") : tr("Landscape");
        button->setToolTip(text);
        button->setAccessibleName(text);
    }
    builder.addActionDivider();

    // tf-Pages 337:2099
    builder.addWidget(createFieldGroup(Field::Pages, tr("Pages"), parent));

    // The shell says the Primary button and the status after the parts are made
    updateParts();
}

QWidget* PDFAppBlankToolGUI::createFieldGroup(Field field, const QString& label, QWidget* parent)
{
    // tf-Pages 337:2099: the label Medium 12 text/secondary, gap 6, the field; the paddings 8 / 4
    QWidget* group = new QWidget(parent);
    QHBoxLayout* layout = new QHBoxLayout(group);
    layout->setContentsMargins(8, 0, 4, 0);
    layout->setSpacing(6);

    PDFLabel* labelWidget = PDFAppPageWidgets::createLabel(label, QFont::Medium, 12, PDFThemeColors::TextSecondary, group);
    layout->addWidget(labelWidget);

    PDFToolBarField* edit = new PDFToolBarField(group);
    edit->setAlignment(Qt::AlignCenter);
    layout->addWidget(edit);
    labelWidget->setBuddy(edit);

    switch (field)
    {
        case Field::Pages:
            group->setObjectName(QStringLiteral("toolFieldGroup-pages"));
            edit->setObjectName(QStringLiteral("toolField-pageCount"));
            edit->setAccessibleName(tr("Number of pages"));
            m_pagesField = edit;
            break;

        case Field::Width:
        case Field::Height:
        {
            // field 356:10139: 75 wide with the unit mm
            const bool isWidth = field == Field::Width;
            group->setObjectName(isWidth ? QStringLiteral("toolFieldGroup-width") : QStringLiteral("toolFieldGroup-height"));
            edit->setObjectName(isWidth ? QStringLiteral("toolField-width") : QStringLiteral("toolField-height"));
            edit->setSuffix(tr("mm", "millimeters"));
            edit->setMinimumWidth(75);
            edit->setAccessibleName(isWidth ? tr("Width of a page in millimeters") : tr("Height of a page in millimeters"));
            if (isWidth)
            {
                m_widthField = edit;
            }
            else
            {
                m_heightField = edit;
            }
            break;
        }
    }

    // The text being edited survives the parts made again (a new language)
    const auto textIt = m_fieldTexts.find(field);
    if (textIt != m_fieldTexts.cend())
    {
        edit->setText(textIt->second);
        const auto errorIt = m_fieldErrors.find(field);
        if (errorIt != m_fieldErrors.cend())
        {
            edit->setInvalid(true, errorIt->second);
        }
    }
    else
    {
        const PDFAppToolParameterModel* model = getModel();
        edit->setText(formatFieldValue(field, model ? model->getValue(getParameterName(field)).toDouble() : 0.0));
    }

    connect(edit, &QLineEdit::textEdited, this, &PDFAppBlankToolGUI::onFieldTextEdited);
    connect(edit, &QLineEdit::editingFinished, this, &PDFAppBlankToolGUI::onFieldEditingFinished);
    edit->installEventFilter(this);
    return group;
}

void PDFAppBlankToolGUI::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    // sidebar-left 337:2003: x 12, width 275 (the frame of the panels has 10 / 11)
    if (QWidget* widget = builder.getWidget())
    {
        if (QLayout* layout = widget->layout())
        {
            layout->setContentsMargins(2, 0, 2, 0);
        }
    }

    // header 337:2104 "Pages / 3 blank pages", 10 above the thumbnails (the gap of the panel)
    m_pagesHeader = builder.addPanelHeader(tr("Pages"), QString());
    m_pagesHeader->setObjectName(QStringLiteral("toolBlankPagesHeader"));

    // thumbnails 337:2108: the tiles of the pages in rows, the gaps 8 / 6; 10 000 pages are
    // rows of a model (no widget per page)
    m_pageView = new PDFAppBlankPageView(builder.getWidget());
    m_pageView->setObjectName(QStringLiteral("toolBlankPages"));
    m_pageView->setAccessibleName(tr("Pages of the new document"));
    m_pageView->setItemDelegate(new PDFAppBlankPageDelegate(m_pageView));
    m_pageView->setModel(m_pageModel);
    connect(m_pageView->selectionModel(), &QItemSelectionModel::currentChanged, this, &PDFAppBlankToolGUI::onCurrentPageChanged);
    builder.addWidget(m_pageView);
    builder.setFillsPanel(m_pageView);

    updatePages();
}

void PDFAppBlankToolGUI::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    // section-Summary 337:2202
    builder.addSection(tr("Summary"));
    m_summary = builder.addKeyValues();
    m_summary->setObjectName(QStringLiteral("toolBlankSummary"));
    m_summary->addRow(tr("Pages"), QString());
    m_summary->addRow(tr("Page size"), QString());
    m_summary->addRow(tr("Orientation"), QString());
    builder.endSection();

    // callout-blue 337:2215 (the compact callout: radius 8, gap 4, the text Regular 11)
    m_infoCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("Create PDF opens the document right away"),
                                       tr("The new document opens in this tab as unsaved; there is no result dialog. Nothing is written to disk until you save it."));
    m_infoCallout->setObjectName(QStringLiteral("toolBlankInfo"));
    m_infoCallout->setCompactStyle();
    m_infoCallout->getTitleLabel()->setWordWrap(true);

    // A failed run (4.6, no frame: the red callout of the same parts), the tab stays
    m_errorCallout = builder.addCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), tr("The new document could not be created"), m_runError);
    m_errorCallout->setObjectName(QStringLiteral("toolBlankError"));
    m_errorCallout->setCompactStyle();
    m_errorCallout->getTitleLabel()->setWordWrap(true);
    m_errorCallout->setVisible(!m_runError.isEmpty());

    updateSummary();
}

QWidget* PDFAppBlankToolGUI::createCanvasPage(QWidget* parent)
{
    // document-canvas 337:2009: the bar, the stage with the paper and its dimensions
    m_preview = new PDFAppPageSizePreview(parent);
    connect(m_preview.data(), &PDFAppPageSizePreview::zoomChanged, this, &PDFAppBlankToolGUI::onPreviewZoomChanged);
    updatePreview();
    return m_preview.data();
}

// ------------------------------------------------------------------------- texts

QString PDFAppBlankToolGUI::getConfirmText() const
{
    return tr("Create PDF");
}

QString PDFAppBlankToolGUI::getConfirmIconName() const
{
    return QStringLiteral("file-plus");
}

bool PDFAppBlankToolGUI::isConfirmEnabled() const
{
    // K26: a field with an error disables Create PDF (setInputError), so does an invalid size
    return BaseClass::isConfirmEnabled() && !getEffectivePageSize().isEmpty() && getEffectivePageCount() > 0;
}

QString PDFAppBlankToolGUI::getStatusText() const
{
    using Helper = PDFAppBlankToolGUIHelper;

    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();

    // K26: the reason of a field with an error
    const QString error = getFieldError();
    if (!error.isEmpty())
    {
        return tr("%1 · %2").arg(title, error);
    }

    const QVariantMap values = getEffectiveValues();
    const int count = getEffectivePageCount();
    const QString pages = count == 1 ? tr("%Ln page", nullptr, count) : tr("%Ln pages", nullptr, count);
    const QSizeF pageSize = PDFDocumentToolBlank::getPageSize(values, nullptr);
    if (values.value(Helper::name(Helper::Tool::PARAMETER_PAGE_SIZE)).toString() == QLatin1String(Helper::Tool::SIZE_CUSTOM))
    {
        // 356:10057 "Blank PDF · 3 pages · custom size 200 × 280 mm"
        return tr("%1 · %2 · custom size %3").arg(title, pages, formatDimensions(pageSize));
    }

    // 337:2012 "Blank PDF · 3 pages · A4 portrait · 210 × 297 mm"
    return tr("%1 · %2 · %3 · %4").arg(title, pages, getSizeDescription(values), formatDimensions(pageSize));
}

QVariantMap PDFAppBlankToolGUI::getRunParameters() const
{
    return getEffectiveValues();
}

bool PDFAppBlankToolGUI::isResultDialogSkipped() const
{
    // 337:2215: Create PDF opens the document right away, in this tab (4.6)
    return true;
}

QString PDFAppBlankToolGUI::getOwnTabTitle() const
{
    // K1, 337:1979
    return tr("New document");
}

QString PDFAppBlankToolGUI::getOwnTabIconName() const
{
    return QStringLiteral("file");
}

QString PDFAppBlankToolGUI::getOwnTabPageText() const
{
    // status-page 337:2013 "Page 1 / 3"
    const int count = qMax(1, getEffectivePageCount() > 0 ? getEffectivePageCount() : m_pageModel->getPageCount());
    return tr("Page %1 / %2").arg(PDFAppLocale::formatNumber(qMin(m_currentPage + 1, count)), PDFAppLocale::formatNumber(count));
}

QString PDFAppBlankToolGUI::getOwnTabZoomText() const
{
    // status-zoom 337:2014: the zoom of the preview (K12, "53 %")
    if (!m_preview || m_preview->getZoom() <= 0.0)
    {
        return QString();
    }
    return tr("%1 %").arg(PDFAppLocale::formatNumber(qRound64(m_preview->getZoom() * 100.0)));
}

QString PDFAppBlankToolGUI::getStartPageText(PDFAppToolStartPageText text) const
{
    // choose-document 1392:4584
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(QString::fromLatin1(PDFDocumentToolBlank::TOOL_ID));
    const QString title = tool ? tool->getTitle() : QString();
    switch (text)
    {
        case PDFAppToolStartPageText::Title:
            return tr("Start a new document");
        case PDFAppToolStartPageText::Hint:
            return tr("%1 needs no document · size, orientation and pages are set in the tool").arg(title);
        case PDFAppToolStartPageText::Card:
            return tr("A new document with empty pages · nothing is written to disk until you save it");
        case PDFAppToolStartPageText::Note:
            return tr("%1 opens in its own tab “%2”. Create PDF turns this tab into the new unsaved document; the open documents are not changed.").arg(title, getOwnTabTitle());
    }
    return QString();
}

// -------------------------------------------------------------------------- flow

void PDFAppBlankToolGUI::onParametersChanged(const QString& name)
{
    // A change of the user, Undo or Redo: the fields of the changed values show the model
    for (Field field : { Field::Pages, Field::Width, Field::Height })
    {
        if (name == getParameterName(field))
        {
            resetFieldParts(field);
        }
    }

    // A new value is a new attempt: the reason of a failed run is gone
    m_runError.clear();
    finishUpdate();
}

void PDFAppBlankToolGUI::onRunStateChanged(bool isRunning)
{
    if (isRunning && !m_runError.isEmpty())
    {
        m_runError.clear();
        updateSummary();
    }
}

void PDFAppBlankToolGUI::onRunNotAccepted(const PDFDocumentToolResult& result)
{
    // 4.6: the reason in the red callout of the right panel, the tab stays (no info bar)
    m_runError = result.message.isEmpty() ? tr("The tool has failed.") : result.message;
    updateSummary();
    updateStatusText();
}

void PDFAppBlankToolGUI::retranslate()
{
    // The canvas page stays over a change of the language: its texts follow; the other parts
    // are made again by the shell
    updatePreview();
}

bool PDFAppBlankToolGUI::eventFilter(QObject* watched, QEvent* event)
{
    // Up / Down change the value of a field by one, Page Up / Page Down by ten (one undo step)
    if (event->type() == QEvent::KeyPress && (watched == m_pagesField.data() || watched == m_widthField.data() || watched == m_heightField.data()))
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        int steps = 0;
        switch (keyEvent->key())
        {
            case Qt::Key_Up:
                steps = 1;
                break;
            case Qt::Key_Down:
                steps = -1;
                break;
            case Qt::Key_PageUp:
                steps = PDFAppBlankToolGUIHelper::PAGE_STEP;
                break;
            case Qt::Key_PageDown:
                steps = -PDFAppBlankToolGUIHelper::PAGE_STEP;
                break;
            default:
                break;
        }
        if (steps != 0 && keyEvent->modifiers() == Qt::NoModifier)
        {
            stepField(getFieldOf(watched), steps);
            return true;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppBlankToolGUI::onSizeRowClicked()
{
    const QObject* row = sender();
    const QString sizeId = row ? row->property(PDFAppBlankToolGUIHelper::SIZE_PROPERTY).toString() : QString();

    // The panel closes first (it may run code of the application), then the size is chosen
    QPointer<PDFAppBlankToolGUI> guard(this);
    if (m_sizePanel)
    {
        m_sizePanel->close();
    }
    if (guard && !sizeId.isEmpty())
    {
        chooseSize(sizeId);
    }
}

void PDFAppBlankToolGUI::onDocumentSizeChosen(QSizeF pageSize)
{
    // K27: Custom with the size of the current page of the document
    chooseCustomSize(pageSize);
}

void PDFAppBlankToolGUI::chooseSize(const QString& sizeId)
{
    using Helper = PDFAppBlankToolGUIHelper;

    PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return;
    }

    const QString sizeParameter = Helper::name(Helper::Tool::PARAMETER_PAGE_SIZE);
    QPointer<PDFAppBlankToolGUI> guard(this);
    if (sizeId == QLatin1String(Helper::Tool::SIZE_CUSTOM))
    {
        // Custom starts with the size shown so far (the page does not jump)
        if (model->getValue(sizeParameter).toString() != sizeId)
        {
            QVariantMap values = model->getValues();
            const QSizeF pageSize = PDFDocumentToolBlank::getPageSize(values, nullptr);
            chooseCustomSize(pageSize);
        }
    }
    else if (sizeId == QLatin1String(Helper::Tool::SIZE_16X9))
    {
        // Q14: 16:9 is a presentation page on its side, one undo step
        QVariantMap values;
        values.insert(sizeParameter, sizeId);
        values.insert(Helper::name(Helper::Tool::PARAMETER_ORIENTATION), Helper::name(Helper::Tool::ORIENTATION_LANDSCAPE));
        model->setValues(values);
    }
    else
    {
        model->setValue(sizeParameter, sizeId);
    }

    // The row has checked itself: the value of the model decides (also for a refusal)
    if (guard)
    {
        updateSizeMenu();
    }
}

void PDFAppBlankToolGUI::chooseCustomSize(const QSizeF& pageSize)
{
    using Helper = PDFAppBlankToolGUIHelper;

    PDFAppToolParameterModel* model = getModel();
    if (!model || !pageSize.isValid() || pageSize.isEmpty())
    {
        return;
    }

    // The fields show the new sides: their texts being edited are gone
    m_fieldTexts.erase(Field::Width);
    m_fieldTexts.erase(Field::Height);

    QVariantMap values;
    values.insert(Helper::name(Helper::Tool::PARAMETER_PAGE_SIZE), Helper::name(Helper::Tool::SIZE_CUSTOM));
    values.insert(Helper::name(Helper::Tool::PARAMETER_WIDTH), Helper::getMillimeters(pageSize.width()));
    values.insert(Helper::name(Helper::Tool::PARAMETER_HEIGHT), Helper::getMillimeters(pageSize.height()));
    values.insert(Helper::name(Helper::Tool::PARAMETER_ORIENTATION), Helper::name(Helper::isLandscape(pageSize) ? Helper::Tool::ORIENTATION_LANDSCAPE : Helper::Tool::ORIENTATION_PORTRAIT));

    // A page of a document outside of the limits of a side (K26) is refused with its reason
    QPointer<PDFAppBlankToolGUI> guard(this);
    const QString error = model->setValues(values);
    if (!guard)
    {
        return;
    }
    if (guard && !error.isEmpty())
    {
        if (PDFAppToolHost* host = getHost())
        {
            host->showTransientMessage(error);
        }
    }
}

void PDFAppBlankToolGUI::onPortraitClicked()
{
    chooseOrientation(false);
}

void PDFAppBlankToolGUI::onLandscapeClicked()
{
    chooseOrientation(true);
}

void PDFAppBlankToolGUI::chooseOrientation(bool isLandscape)
{
    using Helper = PDFAppBlankToolGUIHelper;

    PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return;
    }

    const QString orientation = Helper::name(isLandscape ? Helper::Tool::ORIENTATION_LANDSCAPE : Helper::Tool::ORIENTATION_PORTRAIT);
    const QVariantMap effective = getEffectiveValues();
    const QSizeF pageSize = PDFDocumentToolBlank::getPageSize(effective, nullptr);
    const bool isCustom = effective.value(Helper::name(Helper::Tool::PARAMETER_PAGE_SIZE)).toString() == QLatin1String(Helper::Tool::SIZE_CUSTOM);

    QPointer<PDFAppBlankToolGUI> guard(this);
    if (isCustom && !pageSize.isEmpty() && Helper::isLandscape(pageSize) != isLandscape && pageSize.width() != pageSize.height())
    {
        // A custom size turns: its width and height are swapped (one undo step)
        m_fieldTexts.erase(Field::Width);
        m_fieldTexts.erase(Field::Height);
        QVariantMap values;
        values.insert(Helper::name(Helper::Tool::PARAMETER_WIDTH), effective.value(Helper::name(Helper::Tool::PARAMETER_HEIGHT)));
        values.insert(Helper::name(Helper::Tool::PARAMETER_HEIGHT), effective.value(Helper::name(Helper::Tool::PARAMETER_WIDTH)));
        values.insert(Helper::name(Helper::Tool::PARAMETER_ORIENTATION), orientation);
        model->setValues(values);
    }
    else
    {
        model->setValue(Helper::name(Helper::Tool::PARAMETER_ORIENTATION), orientation);
    }

    // The button has toggled itself: the size decides
    if (guard)
    {
        updateOrientationButtons();
    }
}

void PDFAppBlankToolGUI::onFieldTextEdited(const QString& text)
{
    applyFieldText(getFieldOf(sender()), text);
}

void PDFAppBlankToolGUI::applyFieldText(Field field, const QString& text)
{
    m_fieldTexts[field] = text;

    QString error;
    const std::optional<double> value = parseField(field, text, &error);
    PDFToolBarField* edit = getField(field);
    if (value)
    {
        m_fieldErrors.erase(field);
    }
    else
    {
        m_fieldErrors[field] = error;
    }
    if (edit)
    {
        edit->setInvalid(!value, error);
    }

    // The valid value is the value of the preview at once; the Primary button and the status
    // follow (the signals of the GUI are the last thing)
    finishUpdate();
}

void PDFAppBlankToolGUI::onFieldEditingFinished()
{
    commitField(getFieldOf(sender()));
}

bool PDFAppBlankToolGUI::commitField(Field field)
{
    using Helper = PDFAppBlankToolGUIHelper;

    PDFAppToolParameterModel* model = getModel();
    const auto textIt = m_fieldTexts.find(field);
    if (!model || textIt == m_fieldTexts.cend())
    {
        return false;
    }

    QString error;
    const std::optional<double> value = parseField(field, textIt->second, &error);
    if (!value)
    {
        // The error stays shown, until the text is valid
        return false;
    }

    // The value of the model is shown again (onParametersChanged); an equal value changes nothing
    m_fieldTexts.erase(field);
    QVariantMap values;
    if (field == Field::Pages)
    {
        values.insert(getParameterName(field), qint64(*value));
    }
    else
    {
        values.insert(getParameterName(field), *value);
        QVariantMap sides = getEffectiveValues();
        sides.insert(getParameterName(field), *value);
        const bool isLandscape = sides.value(Helper::name(Helper::Tool::PARAMETER_WIDTH)).toDouble() > sides.value(Helper::name(Helper::Tool::PARAMETER_HEIGHT)).toDouble();
        values.insert(Helper::name(Helper::Tool::PARAMETER_ORIENTATION), Helper::name(isLandscape ? Helper::Tool::ORIENTATION_LANDSCAPE : Helper::Tool::ORIENTATION_PORTRAIT));
    }

    QPointer<PDFAppBlankToolGUI> guard(this);
    model->setValues(values);
    if (guard)
    {
        // A value equal to the model has no change: the field shows the value of the model
        resetFieldParts(field);
        finishUpdate();
    }
    return true;
}

void PDFAppBlankToolGUI::stepField(Field field, int steps)
{
    using Tool = PDFDocumentToolBlank;

    // The value shown (a valid text being edited, else the value of the model) by the steps,
    // within the limits
    const QVariantMap values = getEffectiveValues();
    double value = values.value(getParameterName(field)).toDouble();
    if (field == Field::Pages)
    {
        value = qBound<double>(Tool::MINIMUM_PAGE_COUNT, std::round(value) + steps, Tool::MAXIMUM_PAGE_COUNT);
    }
    else
    {
        value = qBound(Tool::MINIMUM_SIDE_MILLIMETERS, std::round(value) + steps, Tool::MAXIMUM_SIDE_MILLIMETERS);
    }
    m_fieldTexts[field] = formatFieldValue(field, value);
    m_fieldErrors.erase(field);
    if (PDFToolBarField* edit = getField(field))
    {
        edit->setText(m_fieldTexts[field]);
        edit->setInvalid(false);
    }
    commitField(field);
}

void PDFAppBlankToolGUI::resetFieldParts(Field field)
{
    // No signal: the error of the input is cleared by finishUpdate
    m_fieldTexts.erase(field);
    m_fieldErrors.erase(field);
    if (PDFToolBarField* edit = getField(field))
    {
        const PDFAppToolParameterModel* model = getModel();
        edit->setText(formatFieldValue(field, model ? model->getValue(getParameterName(field)).toDouble() : 0.0));
        edit->setInvalid(false);
    }
}

void PDFAppBlankToolGUI::onCurrentPageChanged(const QModelIndex& current, const QModelIndex& previous)
{
    Q_UNUSED(previous);
    if (current.isValid())
    {
        m_currentPage = current.row();
    }

    // The view handles its mouse and its keys further: the texts follow from the event loop
    if (!m_isPageChangeQueued)
    {
        m_isPageChangeQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppBlankToolGUI::onPageChangeQueued, Qt::QueuedConnection);
    }
}

void PDFAppBlankToolGUI::onPageChangeQueued()
{
    m_isPageChangeQueued = false;
    updatePreview();
    updateStatusText();
}

void PDFAppBlankToolGUI::onPreviewZoomChanged()
{
    // The zoom item of the status bar (K12)
    updateStatusText();
}

// ----------------------------------------------------------------------- updates

void PDFAppBlankToolGUI::updateParts()
{
    updateSizeMenu();
    updateFieldGroups();
    updateOrientationButtons();
    updatePages();
    updatePreview();
    updateSummary();
}

void PDFAppBlankToolGUI::finishUpdate()
{
    updateParts();

    // The signals last: the errors of the inputs, the Primary button and the status bar; a
    // slot may delete the shell and this GUI with it
    QPointer<PDFAppBlankToolGUI> guard(this);
    for (Field field : { Field::Pages, Field::Width, Field::Height })
    {
        const auto it = m_fieldErrors.find(field);
        setInputError(getParameterName(field), it != m_fieldErrors.cend() ? it->second : QString());
        if (!guard)
        {
            return;
        }
    }
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
}

void PDFAppBlankToolGUI::updateSizeMenu()
{
    using Helper = PDFAppBlankToolGUIHelper;

    // "Size: A4" and the check of the chosen size
    const PDFAppToolParameterModel* model = getModel();
    const QString sizeId = model ? model->getValue(Helper::name(Helper::Tool::PARAMETER_PAGE_SIZE)).toString() : QString();
    if (QToolButton* button = getSizeButton())
    {
        button->setText(tr("Size: %1").arg(getSizeTitle(sizeId)));
        button->setToolTip(tr("Size of the pages"));
        button->setAccessibleName(button->text());
    }
    for (const auto& [id, row] : m_sizeRows)
    {
        if (row)
        {
            row->setChecked(id == sizeId);
        }
    }
}

void PDFAppBlankToolGUI::updateFieldGroups()
{
    using Helper = PDFAppBlankToolGUIHelper;

    // 356:9995: W and H only for a custom size; a hidden field keeps no text being edited
    const PDFAppToolParameterModel* model = getModel();
    const bool isCustom = model && model->getValue(Helper::name(Helper::Tool::PARAMETER_PAGE_SIZE)).toString() == QLatin1String(Helper::Tool::SIZE_CUSTOM);
    if (!isCustom)
    {
        for (Field field : { Field::Width, Field::Height })
        {
            if (m_fieldTexts.count(field))
            {
                resetFieldParts(field);
            }
        }
    }
    for (QWidget* group : { m_widthGroup.data(), m_heightGroup.data() })
    {
        if (group)
        {
            group->setVisible(isCustom);
        }
    }
}

void PDFAppBlankToolGUI::updateOrientationButtons()
{
    using Helper = PDFAppBlankToolGUIHelper;

    // The orientation of the shown size (a custom one by its sides); an invalid size keeps the parameter
    const QVariantMap values = getEffectiveValues();
    const QSizeF pageSize = PDFDocumentToolBlank::getPageSize(values, nullptr);
    const bool isLandscape = pageSize.isEmpty() ? values.value(Helper::name(Helper::Tool::PARAMETER_ORIENTATION)).toString() == QLatin1String(Helper::Tool::ORIENTATION_LANDSCAPE)
                                                : Helper::isLandscape(pageSize);
    if (m_portraitButton)
    {
        m_portraitButton->setChecked(!isLandscape);
    }
    if (m_landscapeButton)
    {
        m_landscapeButton->setChecked(isLandscape);
    }
}

void PDFAppBlankToolGUI::updatePages()
{
    // The tiles of the pages: the valid count and size, else the last valid ones
    const int count = getEffectivePageCount();
    const QSizeF pageSize = getEffectivePageSize();
    m_pageModel->setPages(count > 0 ? count : m_pageModel->getPageCount(), pageSize.isEmpty() ? m_pageModel->getPageSize() : pageSize);
    m_currentPage = qBound(0, m_currentPage, qMax(0, m_pageModel->getPageCount() - 1));

    if (m_pagesHeader)
    {
        const int shown = m_pageModel->getPageCount();
        m_pagesHeader->setTexts(tr("Pages"), shown == 1 ? tr("%Ln blank page", nullptr, shown) : tr("%Ln blank pages", nullptr, shown));
    }

    if (m_pageView)
    {
        m_pageView->setPageSize(m_pageModel->getPageSize());
    }
    if (m_pageView && m_pageView->selectionModel() && m_pageModel->getPageCount() > 0)
    {
        const QModelIndex index = m_pageModel->index(m_currentPage);
        if (m_pageView->selectionModel()->currentIndex() != index)
        {
            m_pageView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
        }
    }
}

void PDFAppBlankToolGUI::updatePreview()
{
    if (!m_preview)
    {
        return;
    }

    // preview-bar 337:2119 "Page 1 of 3", the paper with "210 mm" / "297 mm" and its caption
    const QVariantMap values = getEffectiveValues();
    const QSizeF pageSize = getEffectivePageSize().isEmpty() ? m_pageModel->getPageSize() : getEffectivePageSize();
    const int count = qMax(1, m_pageModel->getPageCount());
    m_preview->setTitle(tr("Page %1 of %2").arg(PDFAppLocale::formatNumber(qMin(m_currentPage + 1, count)), PDFAppLocale::formatNumber(count)));
    const QString caption = tr("%1 · every page of the new document has this size").arg(getSizeDescription(values));
    m_preview->setPage(pageSize, tr("%1 mm").arg(formatMillimeters(pageSize.width() * 25.4 / 72.0)), tr("%1 mm").arg(formatMillimeters(pageSize.height() * 25.4 / 72.0)),
                       getEffectivePageSize().isEmpty() ? QString() : caption);
}

void PDFAppBlankToolGUI::updateSummary()
{
    using Helper = PDFAppBlankToolGUIHelper;

    // section-Summary 337:2205: Pages "3", Page size "210 × 297 mm", Orientation "Portrait"
    if (m_summary && m_summary->getRowCount() == 3)
    {
        const int count = getEffectivePageCount();
        const QSizeF pageSize = getEffectivePageSize();
        m_summary->setValue(0, count > 0 ? PDFAppLocale::formatNumber(count) : QString());
        m_summary->setValue(1, pageSize.isEmpty() ? QString() : formatDimensions(pageSize));
        m_summary->setValue(2, pageSize.isEmpty() ? QString() : (Helper::isLandscape(pageSize) ? tr("Landscape") : tr("Portrait")));
    }

    if (m_errorCallout)
    {
        m_errorCallout->setTexts(tr("The new document could not be created"), m_runError);
        m_errorCallout->setVisible(!m_runError.isEmpty());
    }
}

QToolButton* PDFAppBlankToolGUI::getSizeButton() const
{
    return m_sizePanel ? m_sizePanel->getButton() : nullptr;
}

PDFTileRow* PDFAppBlankToolGUI::getSizeRow(const QString& sizeId) const
{
    const auto it = m_sizeRows.find(sizeId);
    return it != m_sizeRows.cend() ? it->second.data() : nullptr;
}

}   // namespace pdf

#include "moc_pdfapptoolgui_blank.cpp"
