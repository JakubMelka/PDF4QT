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

#include "pdfapptoolgui_split.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolhost.h"
#include "pdfapptooloutputfiles.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolrunner.h"

#include "pdfcatalog.h"
#include "pdfcomponentbuttons.h"
#include "pdfdocumentpageoperations.h"
#include "pdfdocumenttool_merge.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdrawspacecontroller.h"
#include "pdffluentwidgets.h"
#include "pdfpagerangelist.h"
#include "pdfpainterutils.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace pdf
{

/// Helpers of Split and Extract Pages
class PDFAppToolGUISplitHelper
{
public:
    PDFAppToolGUISplitHelper() = delete;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// The slots of the pages of the tiles in the size Small (383:3315: the tile 85 x 128 with the
    /// page 73 x 100; 385:9405: the tile 121 x 194 with the page 109 x 150)
    static constexpr QSize SPLIT_SLOT = QSize(73, 100);
    static constexpr QSize EXTRACT_SLOT = QSize(109, 150);

    /// Widths of the panels of the menus of the toolbar (383:4447)
    static constexpr int RULE_PANEL_WIDTH = 300;
    static constexpr int OPTIMIZE_PANEL_WIDTH = 270;
    static constexpr int OUTPUT_PANEL_WIDTH = 270;

    /// The rows of the cards Summary are 36 apart (383:3315, 385:9405): 28 + the gap 8
    static constexpr int SUMMARY_ROW_GAP = 8;

    /// At selected pages (383:5715): the tiles 4 apart, the hint 10 above them; Extract
    /// (385:9405): the hint 12 below the tiles
    static constexpr int AT_PAGES_SPACING = 4;
    static constexpr int AT_PAGES_HINT_GAP = 10;
    static constexpr int EXTRACT_HINT_GAP = 12;

    /// 383:6771: the parts of "Start a new file at" 10 apart, the card of the bookmarks (padding
    /// 6 / 8 + the border, its view pads every row by 1), at most so many rows without a scroll
    static constexpr int BOOKMARKS_GAP = 10;
    static constexpr int BOOKMARKS_PADDING_V = 6;
    static constexpr int BOOKMARKS_PADDING_H = 8;
    static constexpr int BOOKMARKS_VISIBLE_ROWS = 10;

    /// 383:5715 card of Cuts: padding 12 + the border, gap 8
    static constexpr int CUTS_PADDING = 13;
    static constexpr int CUTS_GAP = 8;

    /// The dots of the files (383:5715: blue, green, violet, teal, ...)
    static PDFAccentHue getFileHue(size_t fileIndex)
    {
        static const PDFAccentHue hues[] = { PDFAccentHue::Blue, PDFAccentHue::Green, PDFAccentHue::Violet, PDFAccentHue::Teal };
        return hues[fileIndex % std::size(hues)];
    }

    /// "1–12", "7", "1–2", "1, 3, 5" (the one text of the page lists, owner D3)
    static QString getPageNumbers(const std::vector<PDFInteger>& pages)
    {
        return PDFAppLocale::formatPageNumbers(pages, 3);
    }

    /// "Pages 1–12", "Page 7", "Pages 1–2", "Pages 1, 3, 5"
    static QString getPagesText(const std::vector<PDFInteger>& pages)
    {
        if (pages.size() == 1)
        {
            return PDFAppToolGUISplit::tr("Page %1").arg(getPageNumbers(pages));
        }
        return PDFAppToolGUISplit::tr("Pages %1").arg(getPageNumbers(pages));
    }

    /// A container of a part of a panel with its own builder (a part, which is hidden as a whole)
    static QWidget* createPart(PDFAppToolPanelBuilder& builder, const QString& objectName)
    {
        QWidget* part = new QWidget();
        part->setObjectName(objectName);
        builder.addWidget(part);
        return part;
    }

    /// The numbers from 1 of the rows (sorted) with ranges: "1, 2, 5-7"
    static QString formatNumbers(const std::vector<int>& rows)
    {
        QStringList parts;
        for (size_t i = 0; i < rows.size();)
        {
            size_t last = i;
            while (last + 1 < rows.size() && rows[last + 1] == rows[last] + 1)
            {
                ++last;
            }
            if (last > i + 1)
            {
                parts << QStringLiteral("%1-%2").arg(rows[i] + 1).arg(rows[last] + 1);
            }
            else
            {
                for (size_t j = i; j <= last; ++j)
                {
                    parts << QString::number(rows[j] + 1);
                }
            }
            i = last + 1;
        }
        return parts.join(QStringLiteral(", "));
    }

    /// The name equals \p name without the case (the names of the files of a folder)
    static bool isSameName(const QString& name, const QString& other)
    {
        return !name.isEmpty() && name.compare(other, Qt::CaseInsensitive) == 0;
    }
};

// -------------------------------------------------------- PDFAppToolGUISplit

/// Registers the tool and its GUI under the id, when they are not registered yet
template<typename Tool, typename GUI>
class PDFAppToolGUISplitRegistration
{
public:
    static void registerTool()
    {
        const QString toolId = QString::fromLatin1(Tool::TOOL_ID);
        if (!PDFDocumentToolRegistry::isImplemented(toolId))
        {
            PDFDocumentToolRegistry::registerTool(std::make_shared<Tool>());
        }
        if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
        {
            PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<GUI>>());
        }
    }
};

void PDFAppToolGUISplit::registerSplitTools()
{
    PDFAppToolGUISplitRegistration<PDFDocumentToolSplit, PDFAppSplitToolGUI>::registerTool();
    PDFAppToolGUISplitRegistration<PDFDocumentToolExtract, PDFAppExtractToolGUI>::registerTool();
}

QStringList PDFAppToolGUISplit::getOpenDocumentPaths(const PDFDocumentToolGUI* gui)
{
    QStringList paths;
    const PDFAppContext* context = gui ? gui->getContext() : nullptr;
    const PDFAppDocumentRegistry* registry = context ? context->getDocumentRegistry() : nullptr;
    if (!registry)
    {
        return paths;
    }
    for (const PDFAppDocument* document : registry->getDocuments())
    {
        const QString canonicalPath = document && !document->getFilePath().isEmpty() ? QFileInfo(document->getFilePath()).canonicalFilePath() : QString();
        if (!canonicalPath.isEmpty())
        {
            paths << canonicalPath;
        }
    }
    return paths;
}

// -------------------------------------------------------- PDFAppToolNumberField

PDFAppToolNumberField::PDFAppToolNumberField(PDFAppToolParameterModel* model, const QString& parameter, const QString& suffix, QWidget* parent) :
    BaseClass(QString(), suffix, parent),
    m_model(model),
    m_parameter(parameter)
{
    connect(this, &QLineEdit::editingFinished, this, &PDFAppToolNumberField::commit);
    if (m_model)
    {
        connect(m_model.data(), &PDFAppToolParameterModel::changed, this, &PDFAppToolNumberField::onParameterChanged);
    }
    readValue();
}

PDFAppToolNumberField::~PDFAppToolNumberField() = default;

QString PDFAppToolNumberField::formatValue(const QVariant& value) const
{
    if (value.typeId() == QMetaType::Double)
    {
        return PDFAppLocale::getUiLocale().toString(value.toDouble(), 'g', QLocale::FloatingPointShortest);
    }
    return PDFAppLocale::formatNumber(value.toLongLong());
}

void PDFAppToolNumberField::readValue()
{
    const QString text = m_model ? formatValue(m_model->getValue(m_parameter)) : QString();
    if (text != this->text())
    {
        setText(text);
    }
}

void PDFAppToolNumberField::onParameterChanged(const QString& name)
{
    // Undo and Redo of the parameter
    if (name == m_parameter)
    {
        readValue();
    }
}

void PDFAppToolNumberField::commit()
{
    // The loss of the focus of a field, which is hidden or closed with its tab (its tool may be
    // half destroyed then) or disabled during a run, writes nothing
    if (!m_model || !isVisible() || !isEnabled())
    {
        return;
    }

    // The number in the locale of the user interface (also a plain number)
    const QString text = this->text().trimmed();
    const QLocale locale = PDFAppLocale::getUiLocale();
    const PDFDocumentToolParameterDefinition* definition = PDFDocumentToolParameters::find(m_model->getDefinitions(), m_parameter);
    bool isValid = false;
    QVariant value;
    if (definition && definition->getType() == PDFDocumentToolParameterType::Real)
    {
        double number = locale.toDouble(text, &isValid);
        if (!isValid)
        {
            number = text.toDouble(&isValid);
        }
        value = number;
    }
    else
    {
        qint64 number = locale.toLongLong(text, &isValid);
        if (!isValid)
        {
            number = text.toLongLong(&isValid);
        }
        value = number;
    }

    if (!isValid || value == m_model->getValue(m_parameter))
    {
        readValue();
        return;
    }

    // One undo step; a refused value goes back to the value of the model (the model announces
    // an accepted one, its slots may rebuild the toolbar - the last call)
    QPointer<PDFAppToolNumberField> guard(this);
    const QString error = m_model->setValue(m_parameter, value);
    if (guard && !error.isEmpty())
    {
        readValue();
    }
}

// -------------------------------------------------------- PDFAppSplitBookmarkModel

PDFAppSplitBookmarkModel::PDFAppSplitBookmarkModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppSplitBookmarkModel::~PDFAppSplitBookmarkModel() = default;

void PDFAppSplitBookmarkModel::setBookmarks(std::vector<PDFOutlineBreakPoints::Bookmark> bookmarks)
{
    if (bookmarks == m_bookmarks)
    {
        return;
    }

    beginResetModel();
    m_bookmarks = std::move(bookmarks);
    m_checked.assign(m_bookmarks.size(), 0);
    endResetModel();
}

std::vector<char> PDFAppSplitBookmarkModel::getCheckedByValue(const QString& value) const
{
    std::vector<char> checked(m_bookmarks.size(), 0);
    const QString trimmed = value.trimmed();
    if (trimmed == QLatin1String(PDFDocumentToolSplit::BOOKMARKS_TOP_LEVEL))
    {
        for (size_t i = 0; i < m_bookmarks.size(); ++i)
        {
            checked[i] = m_bookmarks[i].level == 0 ? 1 : 0;
        }
        return checked;
    }

    // The numbers of the bookmarks from 1 (the parser of the core); an invalid value checks nothing
    if (trimmed.isEmpty() || m_bookmarks.empty())
    {
        return checked;
    }
    QString error;
    const std::optional<PDFPageRangeList> numbers = PDFPageRangeList::parse(trimmed, PDFInteger(m_bookmarks.size()), &error);
    if (numbers)
    {
        for (const PDFInteger row : numbers->getPageIndices())
        {
            if (row >= 0 && size_t(row) < checked.size())
            {
                checked[size_t(row)] = 1;
            }
        }
    }
    return checked;
}

void PDFAppSplitBookmarkModel::setValue(const QString& value)
{
    std::vector<char> checked = getCheckedByValue(value);
    if (checked == m_checked)
    {
        return;
    }

    int first = -1;
    int last = -1;
    for (size_t i = 0; i < checked.size(); ++i)
    {
        if (checked[i] != m_checked[i])
        {
            first = first < 0 ? int(i) : first;
            last = int(i);
        }
    }
    m_checked = std::move(checked);
    if (first >= 0)
    {
        Q_EMIT dataChanged(index(first), index(last), { Qt::CheckStateRole, Qt::AccessibleDescriptionRole });
    }
}

std::vector<int> PDFAppSplitBookmarkModel::getCheckedRows() const
{
    std::vector<int> rows;
    for (size_t i = 0; i < m_checked.size(); ++i)
    {
        if (m_checked[i])
        {
            rows.push_back(int(i));
        }
    }
    return rows;
}

QString PDFAppSplitBookmarkModel::createValue(const std::vector<int>& rows) const
{
    std::vector<int> topLevel;
    for (size_t i = 0; i < m_bookmarks.size(); ++i)
    {
        if (m_bookmarks[i].level == 0)
        {
            topLevel.push_back(int(i));
        }
    }
    if (!rows.empty() && rows == topLevel)
    {
        return QString::fromLatin1(PDFDocumentToolSplit::BOOKMARKS_TOP_LEVEL);
    }
    return PDFAppToolGUISplitHelper::formatNumbers(rows);
}

int PDFAppSplitBookmarkModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_bookmarks.size());
}

QVariant PDFAppSplitBookmarkModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || size_t(index.row()) >= m_bookmarks.size())
    {
        return QVariant();
    }

    const PDFOutlineBreakPoints::Bookmark& bookmark = m_bookmarks[size_t(index.row())];
    const bool isChecked = m_checked[size_t(index.row())] != 0;
    switch (role)
    {
        case Qt::DisplayRole:
        case Qt::ToolTipRole:
        case Qt::AccessibleTextRole:
            return bookmark.title;

        case Qt::CheckStateRole:
            return isChecked ? Qt::Checked : Qt::Unchecked;

        case LevelRole:
            return bookmark.level;

        case PageTextRole:
            // 383:6771 "p. 8"
            return bookmark.pageIndex >= 0 ? tr("p. %1").arg(PDFAppLocale::formatNumber(bookmark.pageIndex + 1)) : QString();

        case Qt::AccessibleDescriptionRole:
        {
            const QString state = isChecked ? tr("starts a new file") : tr("not used");
            return bookmark.pageIndex >= 0 ? tr("page %1 · %2").arg(PDFAppLocale::formatNumber(bookmark.pageIndex + 1), state) : tr("no page · %1").arg(state);
        }

        default:
            break;
    }
    return QVariant();
}

Qt::ItemFlags PDFAppSplitBookmarkModel::flags(const QModelIndex& index) const
{
    return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren : Qt::NoItemFlags;
}

// -------------------------------------------------------- PDFAppSplitBookmarkDelegate

PDFAppSplitBookmarkDelegate::PDFAppSplitBookmarkDelegate(QObject* parent) :
    BaseClass(parent)
{

}

QRect PDFAppSplitBookmarkDelegate::getCheckRect(const QRect& rect, int level, bool isRightToLeft)
{
    // The children are indented by 20 per level (383:6771 2.1 Windows); the depth of a hostile
    // outline is bounded, so the row keeps its texts
    const int indent = qBound(0, level, MAXIMUM_INDENT_LEVEL) * INDENT;
    const int top = rect.top() + (rect.height() - CHECK_SIZE) / 2;
    const int left = isRightToLeft ? rect.right() + 1 - indent - CHECK_SIZE : rect.left() + indent;
    return QRect(left, top, CHECK_SIZE, CHECK_SIZE);
}

void PDFAppSplitBookmarkDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (!painter || !index.isValid())
    {
        return;
    }

    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    const bool isRightToLeft = option.direction == Qt::RightToLeft;
    const PDFComponentPainter::State state(option.state, option.direction);
    const QRect rect = option.rect;

    // The check box of the design (Checkbox 18)
    const QRect checkRect = getCheckRect(rect, index.data(PDFAppSplitBookmarkModel::LevelRole).toInt(), isRightToLeft);
    PDFStylePainter::IndicatorState indicator;
    indicator.enabled = option.state.testFlag(QStyle::State_Enabled);
    indicator.hover = option.state.testFlag(QStyle::State_MouseOver);
    indicator.focusVisible = option.state.testFlag(QStyle::State_HasFocus) && option.state.testFlag(QStyle::State_KeyboardFocusChange);
    indicator.on = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
    PDFStylePainter::indicator(painter, QRectF(checkRect), PDFStylePainter::IndicatorKind::CheckBox, indicator);

    // The page at the trailing edge (Regular 11 text/secondary), the title between (Regular 13)
    const QString pageText = index.data(PDFAppSplitBookmarkModel::PageTextRole).toString();
    const QFont pageFont = PDFThemeFonts::getFont(QFont::Normal, 11);
    const int pageWidth = pageText.isEmpty() ? 0 : int(std::ceil(PDFStylePainter::textWidth(pageText, pageFont))) + 1;
    const QRect pageRect = isRightToLeft ? QRect(rect.left(), rect.top(), pageWidth, rect.height()) : QRect(rect.right() + 1 - pageWidth, rect.top(), pageWidth, rect.height());
    if (!pageText.isEmpty())
    {
        PDFComponentPainter::drawElidedText(painter, state, QRectF(pageRect), pageText, pageFont, indicator.enabled ? PDFThemeColors::TextSecondary : PDFThemeColors::TextDisabled,
                                            PDFComponentPainter::TextKind::Interface, true);
    }

    const int pageGap = pageWidth > 0 ? PAGE_GAP : 0;
    const int labelLeft = isRightToLeft ? pageRect.right() + 1 + pageGap : checkRect.right() + 1 + CHECK_GAP;
    const int labelRight = isRightToLeft ? checkRect.left() - CHECK_GAP : pageRect.left() - pageGap;
    const QRect labelRect(labelLeft, rect.top(), qMax(0, labelRight - labelLeft), rect.height());
    PDFComponentPainter::drawElidedText(painter, state, QRectF(labelRect), index.data(Qt::DisplayRole).toString(), PDFThemeFonts::getFont(QFont::Normal, 13),
                                        indicator.enabled ? PDFThemeColors::TextPrimary : PDFThemeColors::TextDisabled, PDFComponentPainter::TextKind::Data);
}

QSize PDFAppSplitBookmarkDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize(1, ROW_HEIGHT);
}

// -------------------------------------------------------- PDFAppSplitFileRowDelegate

PDFAppSplitFileRowDelegate::PDFAppSplitFileRowDelegate(QObject* parent) :
    BaseClass(parent)
{
    setKind(Kind::Pending);
}

QSize PDFAppSplitFileRowDelegate::getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const
{
    // 383:9088, 383:10232: the line of the state makes the row 13 + 1 higher (46 -> 59)
    const QSize size = BaseClass::getItemSize(option, index, width);
    const bool hasStatus = index.isValid() && !index.data(PDFAppToolPendingPagesModel::StatusRole).toString().isEmpty();
    return hasStatus ? QSize(size.width(), size.height() + STATUS_HEIGHT) : size;
}

void PDFAppSplitFileRowDelegate::paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const
{
    const QString status = index.isValid() ? index.data(PDFAppToolPendingPagesModel::StatusRole).toString() : QString();
    if (status.isEmpty())
    {
        BaseClass::paintItem(painter, option, index, state);
        return;
    }

    // The row with its tile in the middle (its texts below), then the three lines of 383:9088
    // file-1: the name Semi Bold 12, the pages Regular 11 text/secondary and the state, 8 below
    // the top of the row, 1 apart
    PDFComponentPainter::TileRow row = getContent(index);
    const QString title = row.title;
    const QString description = row.description;
    row.title.clear();
    row.description.clear();
    PDFComponentPainter::paintTileRow(painter, QRectF(option.rect), row, state);

    const QRect rect = option.rect;
    const bool isRightToLeft = option.direction == Qt::RightToLeft;
    const int textInset = PADDING + TILE + TILE_GAP;
    const int textWidth = qMax(0, rect.width() - textInset - PADDING);
    const int textLeft = isRightToLeft ? rect.right() + 1 - textInset - textWidth : rect.left() + textInset;
    int top = rect.top() + PADDING;
    const bool isEnabled = state.flags.testFlag(QStyle::State_Enabled);
    PDFComponentPainter::drawElidedText(painter, state, QRectF(textLeft, top, textWidth, TITLE_LINE), title, PDFThemeFonts::getFont(QFont::DemiBold, 12),
                                        isEnabled ? PDFThemeColors::TextPrimary : PDFThemeColors::TextDisabled, PDFComponentPainter::TextKind::FileName);
    top += TITLE_LINE + LINE_GAP;
    const QFont font = PDFThemeFonts::getFont(QFont::Normal, 11);
    PDFComponentPainter::drawElidedText(painter, state, QRectF(textLeft, top, textWidth, TEXT_LINE), description, font, isEnabled ? PDFThemeColors::TextSecondary : PDFThemeColors::TextDisabled);
    top += TEXT_LINE + LINE_GAP;
    const PDFThemeColors::Color statusColor = static_cast<PDFThemeColors::Color>(index.data(PDFAppToolPendingPagesModel::StatusColorRole).toInt());
    PDFComponentPainter::drawElidedText(painter, state, QRectF(textLeft, top, textWidth, TEXT_LINE), status, font, isEnabled ? statusColor : PDFThemeColors::TextDisabled);
}

// -------------------------------------------------------- PDFAppSplitToolGUI

PDFAppSplitToolGUI::PDFAppSplitToolGUI(QObject* parent) :
    BaseClass(parent)
{
    m_outputFiles = new PDFAppToolOutputFilesSection(this);
    m_bookmarkModel = new PDFAppSplitBookmarkModel(this);
}

PDFAppSplitToolGUI::~PDFAppSplitToolGUI() = default;

QString PDFAppSplitToolGUI::getRule() const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? model->getValue(PDFAppToolGUISplitHelper::name(PDFDocumentToolSplit::PARAMETER_RULE)).toString() : QString();
}

QSize PDFAppSplitToolGUI::getSlotSize() const
{
    return PDFAppToolGUISplitHelper::SPLIT_SLOT;
}

PDFAppToolPageGrid::TileSize PDFAppSplitToolGUI::getDefaultTileSize() const
{
    // Owner 2026-11-09: the tiles of the frames (Thumbnails: Small 383:3315)
    return PDFAppToolPageGrid::TileSize::Small;
}

QString PDFAppSplitToolGUI::getSelectionLabel() const
{
    return tr("Pages to split");
}

QString PDFAppSplitToolGUI::getPendingTitle() const
{
    // 383:3315 "Files to create · 4"
    return tr("Files to create");
}

QString PDFAppSplitToolGUI::getGridHeaderText() const
{
    if (getRule() == QLatin1String(PDFDocumentToolSplit::RULE_AT_PAGES))
    {
        // 383:5715
        return tr("Click between two pages to start a new file");
    }

    // 383:3315 "4 files will be created"
    return m_files.empty() ? tr("No file will be created") : tr("%Ln files will be created", nullptr, int(m_files.size()));
}

QString PDFAppSplitToolGUI::getGridHintText() const
{
    if (getRule() == QLatin1String(PDFDocumentToolSplit::RULE_AT_PAGES))
    {
        // 383:5715
        return tr("The dot shows the file a page goes to. Click a cut line to remove it, or edit the page numbers on the right.");
    }

    // 383:3315: the cards say it themselves
    return QString();
}

int PDFAppSplitToolGUI::findFile(const QString& fileName) const
{
    for (size_t i = 0; i < m_files.size() && !fileName.isEmpty(); ++i)
    {
        if (m_files[i].fileName == fileName)
        {
            return int(i);
        }
    }
    return -1;
}

void PDFAppSplitToolGUI::onTargetsUpdated()
{
    // The files of the values now: the same functions as the run (PDFDocumentToolSplit)
    m_files.clear();
    m_filesError.clear();
    m_fileOfPage.assign(size_t(getPageCount()), -1);

    const PDFDocumentPointer document = getShownDocument();
    const PDFAppToolParameterModel* model = getModel();
    if (!document || !model)
    {
        return;
    }

    const QVariantMap values = model->getValues();
    std::optional<std::vector<PDFDocumentToolFileOutput::File>> files = PDFDocumentToolSplit::createFiles(document.data(), values, &m_filesError);
    if (!files)
    {
        return;
    }

    const PDFAppDocument* appDocument = getDocument();
    const QString sourceName = appDocument ? QFileInfo(appDocument->getFilePath()).fileName() : QString();
    QString nameError;
    PDFDocumentToolFileOutput::createFileNames(&*files, sourceName, values, QDate::currentDate(), &nameError);
    if (!nameError.isEmpty())
    {
        m_filesError = nameError;
    }

    m_files = std::move(*files);
    for (size_t i = 0; i < m_files.size(); ++i)
    {
        for (const PDFInteger page : m_files[i].pages)
        {
            if (size_t(page) < m_fileOfPage.size())
            {
                m_fileOfPage[size_t(page)] = int(i);
            }
        }
    }
    m_selectedFile = m_files.empty() ? 0 : qBound(0, m_selectedFile, int(m_files.size()) - 1);
}

std::vector<PDFInteger> PDFAppSplitToolGUI::getPendingPages() const
{
    // A row per file, keyed by its first page
    std::vector<PDFInteger> pages;
    pages.reserve(m_files.size());
    for (const PDFDocumentToolFileOutput::File& file : m_files)
    {
        pages.push_back(file.pages.front());
    }
    return pages;
}

QString PDFAppSplitToolGUI::getFileDescription(const PDFDocumentToolFileOutput::File& file) const
{
    using Helper = PDFAppToolGUISplitHelper;

    QString description = Helper::getPagesText(file.pages);
    if (!file.title.isEmpty() && getRule() == QLatin1String(PDFDocumentToolSplit::RULE_AT_BOOKMARKS))
    {
        // 383:6771 "Introduction · pages 1–6"
        description = file.pages.size() == 1 ? tr("%1 · page %2").arg(file.title, Helper::getPageNumbers(file.pages))
                                             : tr("%1 · pages %2").arg(file.title, Helper::getPageNumbers(file.pages));
    }
    if (file.estimatedSize >= 0)
    {
        // 383:7775 "Pages 1–17 · about 9.8 MB"
        description = tr("%1 · about %2").arg(description, PDFAppLocale::formatDataSize(file.estimatedSize, 1, QLocale::DataSizeTraditionalFormat));
    }
    return description;
}

bool PDFAppSplitToolGUI::isDuplicateName(int fileIndex) const
{
    // 383:9088: the names of the files of a folder are compared without the case
    if (m_filesError.isEmpty() || fileIndex < 0 || size_t(fileIndex) >= m_files.size())
    {
        return false;
    }
    const QString& name = m_files[size_t(fileIndex)].fileName;
    for (size_t i = 0; i < m_files.size(); ++i)
    {
        if (int(i) != fileIndex && PDFAppToolGUISplitHelper::isSameName(name, m_files[i].fileName))
        {
            return true;
        }
    }
    return false;
}

PDFAppToolPendingPagesModel::Row PDFAppSplitToolGUI::createPendingRow(PDFInteger page) const
{
    // 383:3315: the tile with the number, "Handbook_1.pdf", "Pages 1–12" (383:7775 "· about 9.8
    // MB"); the row of the selected file has the accent tile, the others the muted one (383:6771);
    // the line of the state: "Same name as another file" (383:9088), "Written" (383:10232)
    PDFAppToolPendingPagesModel::Row row;
    row.page = page;
    const int fileIndex = size_t(page) < m_fileOfPage.size() ? m_fileOfPage[size_t(page)] : -1;
    if (fileIndex < 0)
    {
        return row;
    }

    const PDFDocumentToolFileOutput::File& file = m_files[size_t(fileIndex)];
    row.tileText = PDFAppLocale::formatNumber(fileIndex + 1);
    row.title = file.fileName;
    row.description = getFileDescription(file);
    row.tone = fileIndex == m_selectedFile ? PDFComponentPainter::TileRowTone::Accent : PDFComponentPainter::TileRowTone::Muted;
    if (isDuplicateName(fileIndex))
    {
        row.status = tr("Same name as another file");
        row.statusColor = PDFThemeColors::TextDanger;
    }
    else if (m_writtenFiles.count(fileIndex))
    {
        row.status = tr("Written");
        row.statusColor = PDFAccentHueColors::getColor(PDFAccentHue::Green, PDFAccentHueColors::Role::Icon);
    }
    return row;
}

PDFAppToolPageGridModel::Tile PDFAppSplitToolGUI::createTile(PDFInteger page) const
{
    PDFAppToolPageGridModel::Tile tile;
    const int fileIndex = size_t(page) < m_fileOfPage.size() ? m_fileOfPage[size_t(page)] : -1;
    if (fileIndex < 0)
    {
        // A page, which is not split (no file at all: an error of the rule, the page as it is)
        tile.isDimmed = !m_files.empty();
        return tile;
    }

    // 383:5715: the dot of the file, the cut before the first page of a file (the cards of the
    // files of 383:3315 have only the numbers)
    if (getRule() == QLatin1String(PDFDocumentToolSplit::RULE_AT_PAGES))
    {
        const PDFDocumentToolFileOutput::File& file = m_files[size_t(fileIndex)];
        tile.dotColor = PDFAppToolGUISplitHelper::getFileHue(size_t(fileIndex));
        tile.markerBefore = fileIndex > 0 && file.pages.front() == page;
    }
    return tile;
}

bool PDFAppSplitToolGUI::isPendingListShown(const std::vector<PDFInteger>& pendingPages) const
{
    return !pendingPages.empty();
}

void PDFAppSplitToolGUI::onPendingLinkActivated(PDFInteger page)
{
    // The rows of the files have no link
    Q_UNUSED(page);
}

PDFPageSelection::Mode PDFAppSplitToolGUI::getEntryModeWithoutSelection() const
{
    // 383:3315: Pages to split "All"
    return PDFPageSelection::Mode::All;
}

void PDFAppSplitToolGUI::clickPage(int page, Qt::KeyboardModifiers modifiers)
{
    PDFAppToolParameterModel* model = getModel();
    if (!model || getRule() != QLatin1String(PDFDocumentToolSplit::RULE_AT_PAGES))
    {
        // The cards of the files (383:3315): the pages of a file come from its rule
        Q_UNUSED(modifiers);
        return;
    }

    // 383:5715: a click between two pages sets or removes the cut before the page (the page 1
    // starts the first file always); one undo step, the model announces it last
    const PDFInteger pageCount = getPageCount();
    if (page <= 0 || page >= pageCount)
    {
        return;
    }

    const QString key = PDFAppToolGUISplitHelper::name(PDFDocumentToolSplit::PARAMETER_CUTS);
    QString error;
    const std::optional<PDFPageRangeList> cuts = PDFPageRangeList::parse(model->getValue(key).toString(), pageCount, &error);
    std::vector<PDFInteger> pages = cuts ? cuts->getPageIndices() : std::vector<PDFInteger>();
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    const auto it = std::find(pages.begin(), pages.end(), PDFInteger(page));
    if (it != pages.end())
    {
        pages.erase(it);
    }
    else
    {
        pages.insert(std::upper_bound(pages.begin(), pages.end(), PDFInteger(page)), PDFInteger(page));
    }

    QStringList numbers;
    for (const PDFInteger cut : pages)
    {
        numbers << QString::number(cut + 1);
    }
    model->setValue(key, numbers.join(QStringLiteral(", ")));
}

void PDFAppSplitToolGUI::clearCuts()
{
    // 383:5715 Clear Cuts: one undo step, the model announces it last
    if (PDFAppToolParameterModel* model = getModel())
    {
        model->setValue(PDFAppToolGUISplitHelper::name(PDFDocumentToolSplit::PARAMETER_CUTS), QString());
    }
}

void PDFAppSplitToolGUI::selectFile(int fileIndex)
{
    if (fileIndex < 0 || size_t(fileIndex) >= m_files.size())
    {
        return;
    }

    // The row of the file is selected (its tile accent), its card or its first page is shown
    m_selectedFile = fileIndex;
    const int firstPage = int(m_files[size_t(fileIndex)].pages.front());
    QPointer<PDFAppSplitToolGUI> guard(this);
    updateAll();
    if (guard && getGrid())
    {
        getGrid()->scrollToPage(firstPage);
    }
}

void PDFAppSplitToolGUI::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUISplitHelper;

    // 383:3315: Split: Every N pages, the field "12 pages per file" (M13: the Toolbar field 125,
    // no arrows), Optimize: Off, Thumbnails; 383:5715 Clear Cuts; 383:7775 the field "10 MB"
    builder.addEnumMenu(Helper::name(PDFDocumentToolSplit::PARAMETER_RULE), QStringLiteral("scissors"), tr("Split: %1"), Helper::RULE_PANEL_WIDTH);
    m_pagesPerFile = new PDFAppToolNumberField(getModel(), Helper::name(PDFDocumentToolSplit::PARAMETER_PAGES_PER_FILE), tr("pages per file"), nullptr);
    m_pagesPerFile->setObjectName(QStringLiteral("splitPagesPerFile"));
    m_pagesPerFile->setAccessibleName(tr("Pages per file"));
    builder.addWidget(m_pagesPerFile);
    m_sizeField = new PDFAppToolNumberField(getModel(), Helper::name(PDFDocumentToolSplit::PARAMETER_SIZE_MB), tr("MB", "megabytes"), nullptr);
    m_sizeField->setObjectName(QStringLiteral("splitSizeMB"));
    m_sizeField->setAccessibleName(tr("File size"));
    builder.addWidget(m_sizeField);
    m_clearCuts = builder.addAction(tr("Clear Cuts"), QStringLiteral("cuts-clear"), this, &PDFAppSplitToolGUI::clearCuts);
    m_clearCuts->setObjectName(QStringLiteral("splitClearCuts"));
    builder.addActionDivider();
    builder.addEnumMenu(Helper::name(PDFDocumentToolSplit::PARAMETER_OPTIMIZE_IMAGES), QStringLiteral("minimize-2"), tr("Optimize: %1"), Helper::OPTIMIZE_PANEL_WIDTH);
    addTileSizeMenu(builder);
    updateRuleParts();
}

void PDFAppSplitToolGUI::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    QPointer<PDFAppSplitToolGUI> guard(this);
    BaseClass::describeLeftPanel(builder);
    if (!guard)
    {
        return;
    }

    // Files to create (383:3315): a row of a file can be selected - it shows its card; the
    // rows have the line of a state (383:9088, 383:10232)
    PDFAppToolPendingList* list = getPendingList();
    QListView* view = list ? list->getView() : nullptr;
    if (view)
    {
        PDFAppSplitFileRowDelegate* delegate = new PDFAppSplitFileRowDelegate(view);
        delegate->setTitleKind(PDFComponentPainter::TextKind::FileName);
        view->setItemDelegate(delegate);
        view->setSelectionMode(QAbstractItemView::SingleSelection);
        view->setAccessibleName(tr("Files to create"));
        connect(view, &QAbstractItemView::clicked, this, &PDFAppSplitToolGUI::onFileRowClicked);
    }
    updateFileSelection();
}

void PDFAppSplitToolGUI::describeLeftPanelParts(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUISplitHelper;

    // 383:6771: "Start a new file at" with Top level, the hint and the checklist of the bookmarks
    // in the place of the page selection (At bookmarks)
    QWidget* part = Helper::createPart(builder, QStringLiteral("splitBookmarksPart"));
    QVBoxLayout* layout = new QVBoxLayout(part);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Helper::BOOKMARKS_GAP);

    QHBoxLayout* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);
    PDFLabel* title = PDFAppPageWidgets::createLabel(tr("Start a new file at"), QFont::DemiBold, 14, PDFThemeColors::TextPrimary, part);
    title->setObjectName(QStringLiteral("splitBookmarksTitle"));
    title->setMinimumWidth(1);
    header->addWidget(title, 1);
    m_topLevelLink = new PDFTextLink(tr("Top level"), part);
    m_topLevelLink->setObjectName(QStringLiteral("splitTopLevel"));
    connect(m_topLevelLink.data(), &PDFTextLink::clicked, this, &PDFAppSplitToolGUI::chooseTopLevel);
    header->addWidget(m_topLevelLink);
    layout->addLayout(header);

    m_bookmarksHint = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextSecondary, part);
    m_bookmarksHint->setObjectName(QStringLiteral("splitBookmarksHint"));
    m_bookmarksHint->setMinimumWidth(1);
    layout->addWidget(m_bookmarksHint);

    PDFAppToolCard* card = new PDFAppToolCard(part);
    card->setObjectName(QStringLiteral("splitBookmarksCard"));
    card->getBodyLayout()->setContentsMargins(Helper::BOOKMARKS_PADDING_H, Helper::BOOKMARKS_PADDING_V, Helper::BOOKMARKS_PADDING_H, Helper::BOOKMARKS_PADDING_V);
    // The rows fill the card (its padding is the padding of 383:6771, no inner margin of the view)
    QListView* view = new QListView(card);
    view->setObjectName(QStringLiteral("splitBookmarkView"));
    PDFAppPanelWidgets::prepareView(view);
    view->setAccessibleName(tr("Bookmarks"));
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setSpacing(PDFAppSplitBookmarkDelegate::ROW_GAP / 2);
    view->setUniformItemSizes(true);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setModel(m_bookmarkModel);
    view->setItemDelegate(new PDFAppSplitBookmarkDelegate(view));
    view->installEventFilter(this);
    connect(view, &QAbstractItemView::clicked, this, &PDFAppSplitToolGUI::onBookmarkClicked);
    card->getBodyLayout()->addWidget(view);
    m_bookmarkView = view;
    layout->addWidget(card);
    m_bookmarksPart = part;
    updateBookmarks();
}

void PDFAppSplitToolGUI::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUISplitHelper;

    // 383:5715 section-Cuts: the card with "New file starts at pages", its field and its hint
    QWidget* cuts = Helper::createPart(builder, QStringLiteral("splitCutsSection"));
    PDFAppToolPanelBuilder cutsBuilder(this, cuts, Qt::Vertical, PDFAppToolPanelBuilder::SECTION_GAP);
    cutsBuilder.addSection(tr("Cuts"));
    PDFAppToolCard* cutsCard = new PDFAppToolCard(cuts);
    cutsCard->setObjectName(QStringLiteral("splitCutsCard"));
    cutsCard->getBodyLayout()->setContentsMargins(Helper::CUTS_PADDING, Helper::CUTS_PADDING, Helper::CUTS_PADDING, Helper::CUTS_PADDING);
    cutsBuilder.addWidget(cutsCard);
    QWidget* cutsBody = new QWidget(cutsCard);
    cutsCard->getBodyLayout()->addWidget(cutsBody);
    PDFAppToolPanelBuilder cutsCardBuilder(this, cutsBody, Qt::Vertical, Helper::CUTS_GAP);
    cutsCardBuilder.addWidget(PDFAppPageWidgets::createText(tr("New file starts at pages"), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, nullptr));
    if (PDFAppSettingControl* control = cutsCardBuilder.addValueControl(Helper::name(PDFDocumentToolSplit::PARAMETER_CUTS)))
    {
        QWidget* field = control->getWidget();
        field->setObjectName(QStringLiteral("splitCutsField"));
        field->setMinimumWidth(0);
        field->setMaximumWidth(QWIDGETSIZE_MAX);
        field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    cutsCardBuilder.addHint(tr("Numbers separated by commas. The list follows the cuts in the preview."));
    cutsBuilder.endSection();
    m_cutsSection = cuts;

    // 383:7775 callout-blue "The size is approximate" at the top of the panel
    m_sizeCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("The size is approximate"),
                                       tr("It is estimated from the size of the source pages; the written files can be a little smaller or larger."));
    m_sizeCallout->setObjectName(QStringLiteral("splitSizeCallout"));

    // Output files with the callout of an invalid template at the top (383:9088)
    m_outputFiles->describe(builder);

    // 383:3315 Summary: Source, Rule, Files, Images
    builder.addSection(tr("Summary"));
    m_summary = builder.addKeyValues();
    m_summary->setRowGap(Helper::SUMMARY_ROW_GAP);
    m_summary->addRow(tr("Source"), QString());
    m_summary->addRow(tr("Rule"), QString());
    m_summary->addRow(tr("Files"), QString());
    m_summary->addRow(tr("Images"), QString());
    builder.endSection();
    updateAll();
}

void PDFAppSplitToolGUI::updateCanvas()
{
    PDFAppToolPageGrid* grid = getGrid();
    if (!grid)
    {
        return;
    }

    // At selected pages (383:5715): one grid of all pages, the tiles 4 apart, the hint above
    // them, a click between two pages places a cut. The other rules (383:3315): a card per file
    // with its pages - the grouped view of the grid
    const bool isAtPages = getRule() == QLatin1String(PDFDocumentToolSplit::RULE_AT_PAGES);
    grid->setTileSpacing(isAtPages ? PDFAppToolGUISplitHelper::AT_PAGES_SPACING : PDFAppToolPageGrid::TILE_SPACING);
    grid->setHintPlacement(isAtPages, PDFAppToolGUISplitHelper::AT_PAGES_HINT_GAP);
    grid->setGapClicks(isAtPages);
    if (PDFAppParagraph* hint = grid->getHintLabel())
    {
        hint->setVisible(!hint->text().isEmpty());
    }

    if (isAtPages)
    {
        grid->setRows(std::nullopt, {});
        return;
    }

    std::vector<int> rows;
    std::vector<PDFAppToolGroupedPageModel::Group> groups;
    groups.reserve(m_files.size());
    for (size_t i = 0; i < m_files.size(); ++i)
    {
        const PDFDocumentToolFileOutput::File& file = m_files[i];
        PDFAppToolGroupedPageModel::Group group;
        group.inputId = QString::number(i);
        group.inputIndex = int(i);
        group.iconName = QStringLiteral("file");
        group.title = file.fileName;
        group.subtitle = getFileDescription(file);
        group.rowCount = int(file.pages.size());
        if (isDuplicateName(int(i)))
        {
            // 383:9088
            group.tag = tr("Duplicate name");
        }
        else if (m_writtenFiles.count(int(i)))
        {
            // 383:10232
            group.tag = tr("Written");
        }
        groups.push_back(std::move(group));
        for (const PDFInteger page : file.pages)
        {
            rows.push_back(int(page));
        }
    }
    grid->setRows(std::move(rows), std::move(groups));
}

void PDFAppSplitToolGUI::updateFileSelection()
{
    // The row of the selected file (a new set of rows has no selection)
    PDFAppToolPendingList* list = getPendingList();
    QListView* view = list ? list->getView() : nullptr;
    QItemSelectionModel* selection = view ? view->selectionModel() : nullptr;
    const PDFAppToolPendingPagesModel* model = getPendingModel();
    if (!selection || !model)
    {
        return;
    }

    const QModelIndex index = model->index(m_selectedFile, 0);
    if (index.isValid() && !selection->isSelected(index))
    {
        const QSignalBlocker blocker(selection);
        selection->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
        view->viewport()->update();
    }
}

void PDFAppSplitToolGUI::updateBookmarks()
{
    using Helper = PDFAppToolGUISplitHelper;

    // The bookmarks of the version of the document shown now (read again only for a new version)
    const PDFDocumentPointer document = getShownDocument();
    if (m_bookmarkModel && document != m_bookmarkDocument)
    {
        m_bookmarkDocument = document;
        m_bookmarkModel->setBookmarks(document ? PDFOutlineBreakPoints::readBookmarks(document.data()) : std::vector<PDFOutlineBreakPoints::Bookmark>());
    }

    const PDFAppToolParameterModel* model = getModel();
    if (m_bookmarkModel && model)
    {
        m_bookmarkModel->setValue(model->getValue(Helper::name(PDFDocumentToolSplit::PARAMETER_BOOKMARKS)).toString());
    }

    const int count = m_bookmarkModel ? m_bookmarkModel->rowCount() : 0;
    if (m_bookmarksHint)
    {
        // 383:6771 "5 of 7 bookmarks · pages before the first bookmark join the first file"
        const int checked = m_bookmarkModel ? int(m_bookmarkModel->getCheckedRows().size()) : 0;
        m_bookmarksHint->setText(count == 0 ? tr("The document has no bookmarks. Choose another rule to split it.")
                                            : tr("%1 of %Ln bookmarks · pages before the first bookmark join the first file", nullptr, count).arg(PDFAppLocale::formatNumber(checked)));
    }
    if (m_bookmarkView)
    {
        // The card hugs its rows, a long outline scrolls in it
        const int visibleRows = qBound(1, count, Helper::BOOKMARKS_VISIBLE_ROWS);
        const int rowPitch = PDFAppSplitBookmarkDelegate::ROW_HEIGHT + PDFAppSplitBookmarkDelegate::ROW_GAP;
        m_bookmarkView->setFixedHeight(visibleRows * rowPitch + 2 * m_bookmarkView->frameWidth());
        if (QWidget* card = m_bookmarkView->parentWidget())
        {
            card->setVisible(count > 0);
        }
    }
    if (m_topLevelLink)
    {
        m_topLevelLink->setEnabled(count > 0);
    }
}

void PDFAppSplitToolGUI::toggleBookmark(int row)
{
    PDFAppToolParameterModel* model = getModel();
    if (!model || !m_bookmarkModel || row < 0 || row >= m_bookmarkModel->rowCount())
    {
        return;
    }

    std::vector<int> rows = m_bookmarkModel->getCheckedRows();
    const auto it = std::lower_bound(rows.begin(), rows.end(), row);
    if (it != rows.end() && *it == row)
    {
        rows.erase(it);
    }
    else
    {
        rows.insert(it, row);
    }

    // One undo step, the model announces it last
    model->setValue(PDFAppToolGUISplitHelper::name(PDFDocumentToolSplit::PARAMETER_BOOKMARKS), m_bookmarkModel->createValue(rows));
}

void PDFAppSplitToolGUI::chooseTopLevel()
{
    // 383:6771 Top level: one undo step, the model announces it last
    const PDFDocumentToolShell* shell = getShell();
    PDFAppToolParameterModel* model = getModel();
    if (model && shell && shell->getState() == PDFDocumentToolShell::State::Editing)
    {
        model->setValue(PDFAppToolGUISplitHelper::name(PDFDocumentToolSplit::PARAMETER_BOOKMARKS), QString::fromLatin1(PDFDocumentToolSplit::BOOKMARKS_TOP_LEVEL));
    }
}

void PDFAppSplitToolGUI::onBookmarkClicked(const QModelIndex& index)
{
    // From the event loop: Qt uses the view after its signal
    const PDFDocumentToolShell* shell = getShell();
    if (index.isValid() && shell && shell->getState() == PDFDocumentToolShell::State::Editing)
    {
        QMetaObject::invokeMethod(this, &PDFAppSplitToolGUI::toggleBookmark, Qt::QueuedConnection, index.row());
    }
}

void PDFAppSplitToolGUI::onFileRowClicked(const QModelIndex& index)
{
    // From the event loop: the rows are set again (a reset of the model of the view)
    if (index.isValid())
    {
        QMetaObject::invokeMethod(this, &PDFAppSplitToolGUI::selectFile, Qt::QueuedConnection, index.row());
    }
}

bool PDFAppSplitToolGUI::eventFilter(QObject* watched, QEvent* event)
{
    // Space toggles the current bookmark (the check box of the row); the type first, then the
    // view (a key press comes only to a living view, not to one in its destructor)
    if (event->type() == QEvent::KeyPress && m_bookmarkView && watched == m_bookmarkView)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        const QModelIndex current = m_bookmarkView->currentIndex();
        if ((keyEvent->key() == Qt::Key_Space || keyEvent->key() == Qt::Key_Select) && keyEvent->modifiers() == Qt::NoModifier && current.isValid())
        {
            onBookmarkClicked(current);
            return true;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppSplitToolGUI::updatePanels()
{
    using Helper = PDFAppToolGUISplitHelper;

    updateRuleParts();
    const QString rule = getRule();
    const PDFAppToolParameterModel* model = getModel();

    QStringList names;
    for (const PDFDocumentToolFileOutput::File& file : m_files)
    {
        names << file.fileName;
    }
    if (m_outputFiles)
    {
        // 383:3315 "4 files · # is replaced by the file number"; 383:9088 "Handbook.pdf × 4" and
        // the callout of equal names
        int duplicate = -1;
        for (int i = 0; i < int(m_files.size()) && duplicate < 0; ++i)
        {
            duplicate = isDuplicateName(i) ? i : -1;
        }
        if (duplicate >= 0)
        {
            const QString& name = m_files[size_t(duplicate)].fileName;
            int count = 0;
            for (const PDFDocumentToolFileOutput::File& file : m_files)
            {
                count += Helper::isSameName(name, file.fileName) ? 1 : 0;
            }
            m_outputFiles->setDuplicateNames(name, count, m_filesError,
                                             isEveryNameEqual() ? tr("Add # to the file name so that each file gets its own number, for example %1_#.").arg(getSourceBaseName()) : QString());
        }
        else
        {
            const QString hint = m_files.empty() ? QString() : tr("%Ln files · # is replaced by the file number", nullptr, int(m_files.size()));
            m_outputFiles->setPreview(names, hint, m_filesError);
        }
    }

    if (m_summary && m_summary->getRowCount() == 4)
    {
        const int pageCount = int(getTargetPages().size());
        m_summary->setValue(0, tr("%Ln pages", nullptr, pageCount));
        QString ruleText;
        if (const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), Helper::name(PDFDocumentToolSplit::PARAMETER_RULE)) : nullptr)
        {
            for (const PDFDocumentToolParameterOption& option : definition->getOptions())
            {
                if (option.id == rule)
                {
                    ruleText = option.getTitle();
                }
            }
        }
        if (rule == QLatin1String(PDFDocumentToolSplit::RULE_EVERY_N_PAGES) && model)
        {
            // 383:3315 "Every N pages · 12"
            ruleText = tr("%1 · %2").arg(ruleText, PDFAppLocale::formatNumber(model->getValue(Helper::name(PDFDocumentToolSplit::PARAMETER_PAGES_PER_FILE)).toLongLong()));
        }
        m_summary->setValue(1, ruleText);
        m_summary->setValue(2, PDFAppLocale::formatNumber(qint64(m_files.size())));
        const bool isOptimized = model && model->getValue(Helper::name(PDFDocumentToolSplit::PARAMETER_OPTIMIZE_IMAGES)).toString() != QLatin1String(PDFDocumentToolMerge::OPTIMIZE_OFF);
        m_summary->setValue(3, isOptimized ? tr("Recompressed") : tr("Stored as they are"));
    }

    // The rows of the canvas: its model announces them, the last thing (a slot may delete this GUI)
    updateFileSelection();
    updateCanvas();
}

void PDFAppSplitToolGUI::updateRuleParts()
{
    using Helper = PDFAppToolGUISplitHelper;

    // The parts of the rule (no signal: the toolbar and the panels describe themselves with it)
    const QString rule = getRule();
    const PDFAppToolParameterModel* model = getModel();
    const bool isAtPages = rule == QLatin1String(PDFDocumentToolSplit::RULE_AT_PAGES);
    const bool isAtBookmarks = rule == QLatin1String(PDFDocumentToolSplit::RULE_AT_BOOKMARKS);
    const bool isBySize = rule == QLatin1String(PDFDocumentToolSplit::RULE_BY_SIZE);
    if (m_pagesPerFile)
    {
        m_pagesPerFile->setVisible(rule == QLatin1String(PDFDocumentToolSplit::RULE_EVERY_N_PAGES));
    }
    if (m_sizeField)
    {
        m_sizeField->setVisible(isBySize);
    }
    if (m_clearCuts)
    {
        m_clearCuts->setVisible(isAtPages);
        m_clearCuts->setEnabled(model && !model->getValue(Helper::name(PDFDocumentToolSplit::PARAMETER_CUTS)).toString().trimmed().isEmpty());
    }
    if (m_cutsSection)
    {
        m_cutsSection->setVisible(isAtPages);
    }
    if (m_sizeCallout)
    {
        m_sizeCallout->setVisible(isBySize);
    }

    // 383:6771: the bookmarks in the place of the page selection
    if (PDFPageSelection* selection = getPageSelection())
    {
        selection->setVisible(!isAtBookmarks);
    }
    if (m_bookmarksPart)
    {
        m_bookmarksPart->setVisible(isAtBookmarks);
    }
    updateBookmarks();
}

QString PDFAppSplitToolGUI::getConfirmText() const
{
    // 383:3315 "Split into 4 Files"; 383:9088 the same text disabled (K53: with the number)
    return m_files.empty() ? tr("Split") : tr("Split into %Ln Files", nullptr, int(m_files.size()));
}

bool PDFAppSplitToolGUI::isConfirmEnabled() const
{
    // 383:9088: equal names disable the Primary button
    return BaseClass::isConfirmEnabled() && !m_files.empty() && m_filesError.isEmpty();
}

QString PDFAppSplitToolGUI::getStatusText() const
{
    // 383:3315 "Split · Handbook.pdf · 48 pages → 4 files · the open document is not changed"
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const PDFAppDocument* document = getDocument();
    const QString title = tool ? tool->getTitle() : QString();
    const QString fileName = document ? QFileInfo(document->getFilePath()).fileName() : QString();
    if (isEveryNameEqual())
    {
        // 383:9088
        return tr("%1 · the file name template gives the same name to every file").arg(title);
    }
    if (!m_filesError.isEmpty())
    {
        return tr("%1 · %2").arg(title, m_filesError);
    }
    return tr("%1 · %2 · %Ln pages → %3 · the open document is not changed", nullptr, int(getTargetPages().size()))
        .arg(title, fileName, tr("%Ln files", nullptr, int(m_files.size())));
}

QString PDFAppSplitToolGUI::getRunningCardTitle(int percentage, const QString& stepText) const
{
    // 383:10232 card "Writing the files"
    Q_UNUSED(percentage);
    Q_UNUSED(stepText);
    return tr("Writing the files");
}

QString PDFAppSplitToolGUI::getRunningCardStepText(const QString& stepText) const
{
    // 383:10232 "Handbook_3.pdf · file 3 of 4": the phase of the progress is the file
    const int fileIndex = findFile(stepText);
    if (fileIndex < 0)
    {
        return stepText;
    }
    return tr("%1 · file %2 of %3").arg(stepText, PDFAppLocale::formatNumber(fileIndex + 1), PDFAppLocale::formatNumber(qint64(m_files.size())));
}

QString PDFAppSplitToolGUI::getRunningStatusText(const QString& stepText) const
{
    // 383:10232 "Writing the files · Stop ends the operation and keeps the 2 files already written"
    // (the files before the file of the phase; a question of the run keeps the count)
    const int written = qMax(findFile(stepText), int(m_writtenFiles.size()));
    if (written == 0)
    {
        return tr("Writing the files · Stop ends the operation");
    }
    return tr("Writing the files · Stop ends the operation and keeps the %Ln files already written", nullptr, written);
}

void PDFAppSplitToolGUI::connectRunner()
{
    PDFDocumentToolShell* shell = getShell();
    PDFDocumentToolRunner* runner = shell ? shell->getRunner() : nullptr;
    if (runner == m_runner)
    {
        return;
    }
    if (m_runner)
    {
        disconnect(m_runner.data(), nullptr, this, nullptr);
    }

    // The runner emits from the timer of its polling and at the end of the job, both in the GUI
    // thread; the connections die with this GUI or with the runner
    m_runner = runner;
    if (m_runner)
    {
        connect(m_runner.data(), &PDFDocumentToolRunner::runProgress, this, &PDFAppSplitToolGUI::onRunProgress);
        connect(m_runner.data(), &PDFDocumentToolRunner::runFinished, this, &PDFAppSplitToolGUI::onRunFinished);
    }
}

void PDFAppSplitToolGUI::onRunProgress(int percentage, const QString& stepText)
{
    // 383:10232: the files before the file written now are written
    Q_UNUSED(percentage);
    const int fileIndex = findFile(stepText);
    if (fileIndex < 0)
    {
        return;
    }

    std::set<int> written;
    for (int i = 0; i < fileIndex; ++i)
    {
        written.insert(i);
    }
    if (written != m_writtenFiles)
    {
        m_writtenFiles = std::move(written);
        updateAll();
    }
}

void PDFAppSplitToolGUI::onRunFinished(const PDFDocumentToolResult& result)
{
    // The files of the result: a stopped run keeps the marks of its files (403:29978), a run,
    // which has written every file, has none (383:11374)
    std::set<int> written;
    for (const QString& path : result.writtenFiles)
    {
        const int fileIndex = findFile(QFileInfo(path).fileName());
        if (fileIndex >= 0)
        {
            written.insert(fileIndex);
        }
    }
    if (written.size() == m_files.size())
    {
        written.clear();
    }
    if (written != m_writtenFiles)
    {
        m_writtenFiles = std::move(written);
        updateAll();
    }
}

void PDFAppSplitToolGUI::onShellStateChanged(PDFDocumentToolShell::State state)
{
    switch (state)
    {
        case PDFDocumentToolShell::State::Running:
            m_writtenFiles.clear();
            connectRunner();
            break;

        case PDFDocumentToolShell::State::Editing:
            // The marks of a run end with its decision
            if (!m_writtenFiles.empty())
            {
                m_writtenFiles.clear();
                updateAll();
            }
            break;

        default:
            break;
    }
}

bool PDFAppSplitToolGUI::isEveryNameEqual() const
{
    // 383:9088: the template gives one name to more files (no # in it)
    if (m_filesError.isEmpty() || m_files.size() < 2)
    {
        return false;
    }
    for (const PDFDocumentToolFileOutput::File& file : m_files)
    {
        if (!PDFAppToolGUISplitHelper::isSameName(m_files.front().fileName, file.fileName))
        {
            return false;
        }
    }
    return true;
}

QString PDFAppSplitToolGUI::getSourceBaseName() const
{
    const PDFAppDocument* document = getDocument();
    return document ? QFileInfo(document->getFilePath()).completeBaseName() : QString();
}

void PDFAppSplitToolGUI::fillResultInput(PDFAppToolResultInput* input) const
{
    // 383:11374 "Split finished", 403:29978 "Split stopped" (the defaults of the dialog)
    Q_UNUSED(input);
}

void PDFAppSplitToolGUI::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    BaseClass::prepareEnvironment(environment);
    environment->protectedFilePaths = PDFAppToolGUISplit::getOpenDocumentPaths(this);
}

// -------------------------------------------------------- PDFAppExtractToolGUI

PDFAppExtractToolGUI::PDFAppExtractToolGUI(QObject* parent) :
    BaseClass(parent)
{
    m_outputFiles = new PDFAppToolOutputFilesSection(this);
}

PDFAppExtractToolGUI::~PDFAppExtractToolGUI() = default;

bool PDFAppExtractToolGUI::isFilePerPage() const
{
    const PDFAppToolParameterModel* model = getModel();
    return model && model->getValue(PDFAppToolGUISplitHelper::name(PDFDocumentToolExtract::PARAMETER_OUTPUT)).toString() == QLatin1String(PDFDocumentToolExtract::OUTPUT_FILE_PER_PAGE);
}

QSize PDFAppExtractToolGUI::getSlotSize() const
{
    return PDFAppToolGUISplitHelper::EXTRACT_SLOT;
}

PDFAppToolPageGrid::TileSize PDFAppExtractToolGUI::getDefaultTileSize() const
{
    // Owner 2026-11-09: the tiles of the frames (Thumbnails: Small 385:9405)
    return PDFAppToolPageGrid::TileSize::Small;
}

QString PDFAppExtractToolGUI::getSelectionLabel() const
{
    return tr("Pages to extract");
}

QString PDFAppExtractToolGUI::getPendingTitle() const
{
    return QString();
}

QString PDFAppExtractToolGUI::getGridHeaderText() const
{
    // 385:9405 "New document · 7 pages", 385:11029 "7 new files", 385:11855 "Nothing to extract yet"
    const int count = int(getTargetPages().size());
    if (count == 0)
    {
        return tr("Nothing to extract yet");
    }
    return isFilePerPage() ? tr("%Ln new files", nullptr, count) : tr("New document · %Ln pages", nullptr, count);
}

QString PDFAppExtractToolGUI::getGridHintText() const
{
    // 385:9405
    const PDFAppDocument* document = getDocument();
    const QString fileName = document ? QFileInfo(document->getFilePath()).fileName() : QString();
    return tr("The pages are copied – %1 stays as it is. Click a page in the thumbnails to add or remove it.").arg(fileName);
}

std::vector<PDFInteger> PDFAppExtractToolGUI::getPendingPages() const
{
    return {};
}

PDFAppToolPendingPagesModel::Row PDFAppExtractToolGUI::createPendingRow(PDFInteger page) const
{
    PDFAppToolPendingPagesModel::Row row;
    row.page = page;
    return row;
}

void PDFAppExtractToolGUI::onTargetsUpdated()
{
    // One file per page: the names of the files of the pages (385:11029 "Handbook_p1.pdf"), the
    // same function as the run
    m_pageFiles.clear();
    m_namesError.clear();
    if (!isFilePerPage() || !getModel())
    {
        return;
    }

    for (const PDFInteger page : getTargetPages())
    {
        m_pageFiles.push_back({ { page }, QString(), -1, QString() });
    }
    const PDFAppDocument* document = getDocument();
    PDFDocumentToolFileOutput::createFileNames(&m_pageFiles, document ? QFileInfo(document->getFilePath()).fileName() : QString(), getModel()->getValues(), QDate::currentDate(), &m_namesError);
}

PDFAppToolPageGridModel::Tile PDFAppExtractToolGUI::createTile(PDFInteger page) const
{
    // 385:9405: a page of the new document - its number and "from page 8"; 385:11029: the name of
    // its file and "page 8"; nothing red (red is a removal, RS3FIGMA H2)
    PDFAppToolPageGridModel::Tile tile;
    const std::vector<PDFInteger>& targets = getTargetPages();
    const auto it = std::lower_bound(targets.cbegin(), targets.cend(), page);
    if (it == targets.cend() || *it != page)
    {
        return tile;
    }

    const PDFDocumentPointer document = getShownDocument();
    const QString pageLabel = PDFAppPanelWidgets::getPageLabel(document ? document->getCatalog() : nullptr, page);
    const size_t position = size_t(it - targets.cbegin());
    if (isFilePerPage())
    {
        tile.label = position < m_pageFiles.size() && m_namesError.isEmpty() ? m_pageFiles[position].fileName : pageLabel;
        tile.caption = tr("page %1").arg(pageLabel);
    }
    else
    {
        tile.label = PDFAppLocale::formatNumber(qint64(position) + 1);
        tile.caption = tr("from page %1").arg(pageLabel);
    }
    return tile;
}

void PDFAppExtractToolGUI::onDeleteRequested(PDFInteger page)
{
    // Delete on a page of the result leaves it out (one undo step)
    std::vector<PDFInteger> pages = getTargetPages();
    pages.erase(std::remove(pages.begin(), pages.end(), page), pages.end());
    setTargetPages(pages);
}

void PDFAppExtractToolGUI::clickPage(int page, Qt::KeyboardModifiers modifiers)
{
    // 385:9405 "Click a page in the thumbnails to add or remove it": the canvas shows the result
    Q_UNUSED(page);
    Q_UNUSED(modifiers);
}

void PDFAppExtractToolGUI::clickThumbnail(int page, Qt::KeyboardModifiers modifiers)
{
    // The thumbnail adds or removes its page (Shift: the pages from the one clicked before)
    const PDFDocumentToolShell* shell = getShell();
    if (shell && shell->getState() == PDFDocumentToolShell::State::Editing)
    {
        BaseClass::clickPage(page, modifiers);
    }
}

void PDFAppExtractToolGUI::onThumbnailClicked(const QModelIndex& index)
{
    // From the event loop: Qt uses the view after its signal, the selection is set again
    if (index.isValid())
    {
        QMetaObject::invokeMethod(this, &PDFAppExtractToolGUI::clickThumbnail, Qt::QueuedConnection, index.row(), QGuiApplication::keyboardModifiers());
    }
}

bool PDFAppExtractToolGUI::isPendingListShown(const std::vector<PDFInteger>& pendingPages) const
{
    Q_UNUSED(pendingPages);
    return false;
}

void PDFAppExtractToolGUI::onPendingLinkActivated(PDFInteger page)
{
    Q_UNUSED(page);
}

void PDFAppExtractToolGUI::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUISplitHelper;

    // 385:9405: Output: One file, Thumbnails; the icon of the button follows the output
    // (updatePanels), the rows of the menu have the icons of their options (385:10213)
    if (PDFAppToolEnumMenu* menu = builder.addEnumMenu(Helper::name(PDFDocumentToolExtract::PARAMETER_OUTPUT), getOutputIconName(), tr("Output: %1"), Helper::OUTPUT_PANEL_WIDTH))
    {
        m_outputButton = menu->getButton();
    }
    addTileSizeMenu(builder);
}

QString PDFAppExtractToolGUI::getOutputIconName() const
{
    // 385:9405 icon/file, 385:11029 icon/split-pages
    return isFilePerPage() ? QStringLiteral("split-pages") : QStringLiteral("file");
}

void PDFAppExtractToolGUI::describeLeftPanelParts(PDFAppToolPanelBuilder& builder)
{
    // 385:9405: the thumbnails of the source below the page selection (as Crop 389:22382); the
    // selected thumbnails are the pages to extract. The view belongs to the panel, its model,
    // delegate and scheduler to the view.
    PDFAppThumbnailListView* view = new PDFAppThumbnailListView(builder.getWidget());
    view->setObjectName(QStringLiteral("extractThumbnails"));
    PDFAppPanelWidgets::prepareView(view);
    view->setSidePadding(0);
    view->setSelectionMode(QAbstractItemView::NoSelection);
    view->setAccessibleName(tr("Pages of the document"));
    m_thumbnailModel = new PDFAppThumbnailsModel(view);
    PDFThumbnailDelegate* delegate = new PDFThumbnailDelegate(view);
    delegate->setPageSize(THUMBNAIL_SLOT);
    view->setModel(m_thumbnailModel);
    view->setItemDelegate(delegate);
    PDFComponentPainter::ThumbnailItem item;
    item.pageSize = THUMBNAIL_SLOT;
    view->setItemSize(PDFComponentPainter::getThumbnailItemSize(item));
    m_thumbnailModel->setPictureSize(THUMBNAIL_SLOT, view->devicePixelRatioF());
    m_thumbnailScheduler = new PDFAppThumbnailScheduler(m_thumbnailModel, view, view);
    connect(view, &QAbstractItemView::clicked, this, &PDFAppExtractToolGUI::onThumbnailClicked);
    m_thumbnailView = view;
    builder.addWidget(view);
    builder.setFillsPanel(view);
    updateThumbnails();
}

void PDFAppExtractToolGUI::updateThumbnails()
{
    if (!m_thumbnailView || !m_thumbnailModel)
    {
        return;
    }

    PDFAppDocument* document = getDocument();
    const PDFAppToolHost* host = getHost();
    m_thumbnailModel->setDocument(getShownDocument(), document ? document->getId() : QString());
    if (m_thumbnailScheduler)
    {
        m_thumbnailScheduler->setProxy(host && host->getView() ? host->getView()->getProxy() : nullptr);
        m_thumbnailScheduler->setAppDocument(document);
    }

    // The selected thumbnails are the pages to extract (no signal: the selection follows the model)
    QItemSelectionModel* selectionModel = m_thumbnailView->selectionModel();
    if (!selectionModel)
    {
        return;
    }
    QItemSelection selection;
    const std::vector<PDFInteger>& pages = getTargetPages();
    for (size_t i = 0; i < pages.size();)
    {
        size_t last = i;
        while (last + 1 < pages.size() && pages[last + 1] == pages[last] + 1)
        {
            ++last;
        }
        const QModelIndex first = m_thumbnailModel->index(int(pages[i]), 0);
        const QModelIndex end = m_thumbnailModel->index(int(pages[last]), 0);
        if (first.isValid() && end.isValid())
        {
            selection.select(first, end);
        }
        i = last + 1;
    }
    const QSignalBlocker blocker(selectionModel);
    selectionModel->select(selection, QItemSelectionModel::ClearAndSelect);
    m_thumbnailView->viewport()->update();
}

void PDFAppExtractToolGUI::onDocumentChanged()
{
    QPointer<PDFAppExtractToolGUI> guard(this);
    BaseClass::onDocumentChanged();
    if (guard)
    {
        updateThumbnails();
    }
}

void PDFAppExtractToolGUI::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    // 385:11029 Output files (One file per page), 385:9405 Summary and the note
    m_outputFiles->describe(builder);
    builder.addSection(tr("Summary"));
    m_summary = builder.addKeyValues();
    m_summary->setRowGap(PDFAppToolGUISplitHelper::SUMMARY_ROW_GAP);
    m_summary->addRow(tr("Source"), QString());
    m_summary->addRow(tr("Pages"), QString());
    m_summary->addRow(tr("Output"), QString());
    builder.endSection();
    builder.addHint(tr("Extract Pages never changes the open document, so it also works when the document is locked. To delete pages, use Remove Pages."));
    updateAll();
}

void PDFAppExtractToolGUI::updatePanels()
{
    const std::vector<PDFInteger>& pages = getTargetPages();
    if (m_outputButton && PDFThemeIcons::getIconName(m_outputButton->icon()) != getOutputIconName())
    {
        m_outputButton->setIcon(PDFThemeIcons::getIcon(getOutputIconName()));
    }
    if (m_outputFiles)
    {
        m_outputFiles->setVisible(isFilePerPage());
        if (isFilePerPage())
        {
            // 385:11029 "Handbook_p1.pdf … Handbook_p12.pdf", "@ is replaced by the page number"
            QStringList names;
            for (const PDFDocumentToolFileOutput::File& file : m_pageFiles)
            {
                names << file.fileName;
            }
            m_outputFiles->setPreview(m_namesError.isEmpty() ? names : QStringList(), m_pageFiles.empty() ? QString() : tr("%Ln files · @ is replaced by the page number", nullptr, int(m_pageFiles.size())), m_namesError);
        }
    }

    if (m_summary && m_summary->getRowCount() == 3)
    {
        // 385:9405 Source "Handbook.pdf", Pages "7 of 48", Output "New document"; 385:11029
        // Output "7 files"; 385:11855 Pages "–"
        const PDFAppDocument* document = getDocument();
        const int count = int(pages.size());
        m_summary->setValue(0, document ? QFileInfo(document->getFilePath()).fileName() : QString());
        m_summary->setValue(1, count == 0 ? QStringLiteral("–") : tr("%1 of %2").arg(PDFAppLocale::formatNumber(count), PDFAppLocale::formatNumber(getPageCount())));
        m_summary->setValue(2, isFilePerPage() ? tr("%Ln files", nullptr, count) : tr("New document"));
    }

    updateThumbnails();

    // 385:9405: the canvas has only the pages of the result, the hint 12 below them (the model
    // announces new rows: the last thing, a slot may delete this GUI)
    if (PDFAppToolPageGrid* grid = getGrid())
    {
        std::vector<int> rows;
        rows.reserve(pages.size());
        for (const PDFInteger page : pages)
        {
            rows.push_back(int(page));
        }
        grid->setHintPlacement(false, PDFAppToolGUISplitHelper::EXTRACT_HINT_GAP);
        grid->setRows(std::move(rows), {});
    }
}

QString PDFAppExtractToolGUI::getConfirmText() const
{
    // 385:9405 "Extract 7 Pages", 385:11029 "Extract 7 Files"
    const int count = int(getTargetPages().size());
    if (count == 0)
    {
        return tr("Extract Pages");
    }
    return isFilePerPage() ? tr("Extract %Ln Files", nullptr, count) : tr("Extract %Ln Pages", nullptr, count);
}

bool PDFAppExtractToolGUI::isConfirmEnabled() const
{
    return BaseClass::isConfirmEnabled() && !getTargetPages().empty() && m_namesError.isEmpty();
}

QString PDFAppExtractToolGUI::getStatusText() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();
    const int count = int(getTargetPages().size());
    if (count == 0)
    {
        return BaseClass::getStatusText();
    }

    // 385:9405 "Extract Pages · 7 of 48 pages → a new document · Handbook.pdf is not changed",
    // 385:11029 "… → 7 files · …"
    const PDFAppDocument* document = getDocument();
    const QString fileName = document ? QFileInfo(document->getFilePath()).fileName() : QString();
    const QString pagesText = tr("%1 of %Ln pages", nullptr, int(getPageCount())).arg(PDFAppLocale::formatNumber(count));
    const QString resultText = isFilePerPage() ? tr("%Ln files", nullptr, count) : tr("a new document");
    return tr("%1 · %2 → %3 · %4 is not changed").arg(title, pagesText, resultText, fileName);
}

void PDFAppExtractToolGUI::fillResultInput(PDFAppToolResultInput* input) const
{
    // 385:12395, 403:31676 "Pages extracted"
    input->title = tr("Pages extracted");
}

void PDFAppExtractToolGUI::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    BaseClass::prepareEnvironment(environment);
    environment->protectedFilePaths = PDFAppToolGUISplit::getOpenDocumentPaths(this);
}

}   // namespace pdf

#include "moc_pdfapptoolgui_split.cpp"
