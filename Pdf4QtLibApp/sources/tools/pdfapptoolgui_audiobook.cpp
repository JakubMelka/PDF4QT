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

#include "pdfapptoolgui_audiobook.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfapptoolgui_extracttext.h"
#include "pdfcomponentpanels.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentspeech.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfappworkspace.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcatalog.h"
#include "pdfdeferreddeletion.h"
#include "pdfdocument.h"
#include "pdfdocumenttool_audiobook.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdrawspacecontroller.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfocrjobcontroller.h"
#include "pdfpainterutils.h"
#include "pdfqtspeechsynthesizer.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSlider>
#include <QTextLayout>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>
#include <limits>

namespace pdf
{

/// Helpers of the GUI of Create Audio Book
class PDFAppToolGUIAudioBookHelper
{
public:
    PDFAppToolGUIAudioBookHelper() = delete;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// The tool of \p gui has ended (step A): the GUI is retired (its page, tab or shell closed) or its shell is
    /// closed already (the page left by a slot of a signal of the GUI, the GUI retired later with the page). Such a
    /// GUI opens no menu and no question any more.
    static bool isToolEnded(const PDFAppToolGUIAudioBook* gui)
    {
        if (PDFDeferredDeletion::isRetired(gui))
        {
            return true;
        }
        const PDFDocumentToolShell* shell = gui->getShell();
        return !shell || shell->getState() == PDFDocumentToolShell::State::Closed;
    }

    /// Property of a row of a menu: its identifier
    static constexpr const char* ROW_KEY_PROPERTY = "audioBookRowKey";

    /// Widths of the panels of the toolbar (341:9657)
    static constexpr int SELECT_MENU_WIDTH = 290;
    static constexpr int SYNC_MENU_WIDTH = 260;
    static constexpr int ORDER_MENU_WIDTH = 270;
    static constexpr int VOICE_MENU_WIDTH = 280;

    /// The scale of the sliders: -1 to 1 and 0 to 1 by hundredths
    static constexpr int SLIDER_SCALE = 100;

    /// Pause of the typing, after which an edited text goes into the stream (ms)
    static constexpr int COMMIT_DELAY = 700;

    /// Longest text of Play Sample
    static constexpr int SAMPLE_LENGTH = 300;

    static int toInt(size_t value)
    {
        return int(qMin<size_t>(value, size_t(std::numeric_limits<int>::max())));
    }

    /// "Page 308", "Document" for an item of no page
    static QString getPageText(PDFInteger pageIndex)
    {
        return pageIndex >= 0 ? PDFAppToolGUIAudioBook::tr("Page %1").arg(PDFAppLocale::formatNumber(pageIndex + 1)) : PDFAppToolGUIAudioBook::tr("Document");
    }

    /// "Female · adult · en-US" (341:9657)
    static QString getVoiceDescription(const PDFSpeechVoice& voice)
    {
        QStringList parts;
        switch (voice.gender)
        {
            case PDFSpeechVoice::Gender::Male:
                parts << PDFAppToolGUIAudioBook::tr("Male");
                break;
            case PDFSpeechVoice::Gender::Female:
                parts << PDFAppToolGUIAudioBook::tr("Female");
                break;
            case PDFSpeechVoice::Gender::Unknown:
                break;
        }
        switch (voice.age)
        {
            case PDFSpeechVoice::Age::Child:
                parts << PDFAppToolGUIAudioBook::tr("child");
                break;
            case PDFSpeechVoice::Age::Teenager:
                parts << PDFAppToolGUIAudioBook::tr("teenager");
                break;
            case PDFSpeechVoice::Age::Adult:
                parts << PDFAppToolGUIAudioBook::tr("adult");
                break;
            case PDFSpeechVoice::Age::Senior:
                parts << PDFAppToolGUIAudioBook::tr("senior");
                break;
            case PDFSpeechVoice::Age::Unknown:
                break;
        }
        if (!voice.languageCode.isEmpty())
        {
            parts << voice.languageCode;
        }
        return parts.join(QStringLiteral(" · "));
    }

    /// The caption of the language of a group of the voice menu ("ENGLISH")
    static QString getLanguageCaption(const QString& languageCode)
    {
        const QLocale::Language language = QLocale(languageCode).language();
        return language == QLocale::AnyLanguage || language == QLocale::C ? PDFAppToolGUIAudioBook::tr("OTHER") : QLocale::languageToString(language).toUpper();
    }

    /// The order of the groups of the voice menu: by the caption of the language
    static bool isLanguageBefore(const PDFSpeechVoice& left, const PDFSpeechVoice& right)
    {
        return getLanguageCaption(left.languageCode) < getLanguageCaption(right.languageCode);
    }

    /// The leading edge of a part of a row of the width \p width at \p offset from the start of the
    /// text (mirrored for right-to-left)
    static int place(bool isRightToLeft, int textLeft, int textWidth, int offset, int width)
    {
        return isRightToLeft ? textLeft + textWidth - offset - width : textLeft + offset;
    }

    /// A field of the card Voice and format (field-Speed 341:9607): the label 91 wide, the control
    static void addField(QWidget* card, QVBoxLayout* cardLayout, PDFLabel* label, QWidget* control)
    {
        QWidget* field = new QWidget(card);
        field->setFixedHeight(34);
        QHBoxLayout* fieldLayout = new QHBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(10);
        label->setParent(field);
        label->setFixedWidth(91);
        control->setParent(field);
        fieldLayout->addWidget(label);
        fieldLayout->addWidget(control, 1);
        cardLayout->addWidget(field);
    }

    /// "Speed · normal", "Speed · 150 %" (the speed of QTextToSpeech: 2^rate)
    static QString getSpeedText(int sliderValue)
    {
        return sliderValue == 0 ? PDFAppToolGUIAudioBook::tr("Speed · normal")
                                : PDFAppToolGUIAudioBook::tr("Speed · %1 %").arg(qRound(std::pow(2.0, double(sliderValue) / SLIDER_SCALE) * 100.0));
    }

    /// "about 4 h 20 min" (341:9229)
    static QString getAboutText(const QString& value)
    {
        return PDFAppToolGUIAudioBook::tr("about %1").arg(value);
    }

    /// The stream of the parameters (an empty value = none)
    static PDFDocumentToolAudioBookStream getStream(const PDFAppToolParameterModel* model)
    {
        return model ? model->getValue(name(PDFDocumentToolAudioBook::PARAMETER_EDITED_STREAM)).value<PDFDocumentToolAudioBookStream>() : PDFDocumentToolAudioBookStream();
    }

    /// The short name of a voice of the toolbar ("Voice: Zira", 341:9229): without the name of the
    /// vendor of Windows and the suffix of its desktop voices; the menu shows the whole name
    static QString getShortVoiceName(const QString& name)
    {
        QString result = name.trimmed();
        const QString vendor = QStringLiteral("Microsoft ");
        const QString desktop = QStringLiteral(" Desktop");
        if (result.startsWith(vendor) && result.size() > vendor.size())
        {
            result = result.mid(vendor.size());
        }
        if (result.endsWith(desktop) && result.size() > desktop.size())
        {
            result.chop(desktop.size());
        }
        return result;
    }

    /// The number of the item being spoken of the step of a run ("Creating audio · 212 of 1,712":
    /// 212 items are done, the 213th is spoken); 0 = not known. The numbers are formatted by the
    /// locale of the application: the digits of the first number are read up to its first letter.
    static qint64 getSpokenItemNumber(const QString& stepText)
    {
        const QString creating = PDFDocumentToolAudioBook::tr("Creating audio") + QStringLiteral(" · ");
        if (!stepText.startsWith(creating))
        {
            return 0;
        }

        qint64 done = 0;
        bool hasDigit = false;
        for (qsizetype i = creating.size(); i < stepText.size() && !stepText.at(i).isLetter(); ++i)
        {
            const int digit = stepText.at(i).digitValue();
            if (digit >= 0)
            {
                done = done * 10 + digit;
                hasDigit = true;
            }
        }
        return hasDigit ? done + 1 : 0;
    }

    /// The text of the state of an item
    static QString getStateText(PDFAppAudioBookOverlay::State state)
    {
        switch (state)
        {
            case PDFAppAudioBookOverlay::State::Read:
                return PDFAppToolGUIAudioBook::tr("Read");
            case PDFAppAudioBookOverlay::State::Selected:
                return PDFAppToolGUIAudioBook::tr("Selected");
            case PDFAppAudioBookOverlay::State::Modified:
                return PDFAppToolGUIAudioBook::tr("Modified");
            case PDFAppAudioBookOverlay::State::Skipped:
                return PDFAppToolGUIAudioBook::tr("Skipped");
        }
        return QString();
    }
};

/// The list of the Text stream (item-list 341:9400, x 12 as the header and the chips): its rows
/// start at its edges - no inner horizontal margin of PDFListView (its rows are rounded already)
class PDFAppAudioBookListView : public PDFListView
{
public:
    explicit PDFAppAudioBookListView(QWidget* parent) :
        PDFListView(parent)
    {
        setViewportMargins(0, 6, 0, 6);
    }
};

// --------------------------------------------------------- PDFAppAudioBookItemModel

PDFAppAudioBookItemModel::PDFAppAudioBookItemModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppAudioBookItemModel::~PDFAppAudioBookItemModel() = default;

int PDFAppAudioBookItemModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : PDFAppToolGUIAudioBookHelper::toInt(m_rows.size());
}

QVariant PDFAppAudioBookItemModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || size_t(index.row()) >= m_rows.size())
    {
        return QVariant();
    }

    const size_t itemIndex = m_rows[size_t(index.row())];
    const PDFAppAudioBookOverlay::State state = getItemState(itemIndex);
    switch (role)
    {
        case Qt::DisplayRole:
            return m_editor.getText(itemIndex);

        case Qt::ToolTipRole:
        case Qt::AccessibleTextRole:
            // The label of an item of the plugin (plan 4.8): its page, type and state with the text
            return PDFAppToolGUIAudioBook::tr("%1 · %2 · %3\n%4").arg(data(index, PageTextRole).toString(), data(index, TypeTextRole).toString(),
                                                                       data(index, StateTextRole).toString(), m_editor.getText(itemIndex));

        case PageTextRole:
            return PDFAppToolGUIAudioBookHelper::getPageText(m_editor.getPageIndex(itemIndex));

        case TypeTextRole:
            return m_editor.isItemTypeText(itemIndex) ? PDFAppToolGUIAudioBook::tr("Text") : PDFAppToolGUIAudioBook::tr("Special");

        case StateTextRole:
            return PDFAppToolGUIAudioBookHelper::getStateText(state);

        case StateRole:
            return int(state);

        case IsSelectedRole:
            return m_selection.count(itemIndex) > 0;

        case ItemIndexRole:
            return qint64(itemIndex);

        default:
            break;
    }
    return QVariant();
}

void PDFAppAudioBookItemModel::setEditor(PDFDocumentTextFlowEditor editor)
{
    beginResetModel();
    m_editor = std::move(editor);
    m_editor.deselect();
    m_selection.clear();
    m_isBuilding = false;
    updateRows();
    endResetModel();
}

void PDFAppAudioBookItemModel::setBuilding(bool isBuilding)
{
    beginResetModel();
    m_isBuilding = isBuilding;
    if (isBuilding)
    {
        m_editor.clear();
        m_selection.clear();
    }
    updateRows();
    endResetModel();
}

void PDFAppAudioBookItemModel::setFilter(Filter filter)
{
    if (m_filter == filter)
    {
        return;
    }

    beginResetModel();
    m_filter = filter;
    updateRows();
    endResetModel();
}

size_t PDFAppAudioBookItemModel::getItemIndex(int row) const
{
    return row >= 0 && size_t(row) < m_rows.size() ? m_rows[size_t(row)] : std::numeric_limits<size_t>::max();
}

int PDFAppAudioBookItemModel::getRow(size_t itemIndex) const
{
    // The rows are the items of the filter in the order of the items (updateRows): sorted
    auto it = std::lower_bound(m_rows.cbegin(), m_rows.cend(), itemIndex);
    return it != m_rows.cend() && *it == itemIndex ? PDFAppToolGUIAudioBookHelper::toInt(size_t(std::distance(m_rows.cbegin(), it))) : -1;
}

void PDFAppAudioBookItemModel::setSelection(std::set<size_t> selection)
{
    // Only items of the stream
    for (auto it = selection.begin(); it != selection.end();)
    {
        it = *it < m_editor.getItemCount() ? std::next(it) : selection.erase(it);
    }
    m_selection = std::move(selection);
    if (!m_rows.empty())
    {
        Q_EMIT dataChanged(index(0, 0), index(rowCount(QModelIndex()) - 1, 0), { IsSelectedRole });
    }
}

size_t PDFAppAudioBookItemModel::getReadCount() const
{
    size_t count = 0;
    for (size_t i = 0; i < m_editor.getItemCount(); ++i)
    {
        count += m_editor.isActive(i) ? 1 : 0;
    }
    return count;
}

size_t PDFAppAudioBookItemModel::getSkippedCount() const
{
    return m_editor.getItemCount() - getReadCount();
}

size_t PDFAppAudioBookItemModel::getModifiedCount() const
{
    size_t count = 0;
    for (size_t i = 0; i < m_editor.getItemCount(); ++i)
    {
        count += m_editor.isModified(i) ? 1 : 0;
    }
    return count;
}

qint64 PDFAppAudioBookItemModel::getReadCharacterCount() const
{
    qint64 count = 0;
    for (size_t i = 0; i < m_editor.getItemCount(); ++i)
    {
        if (m_editor.isActive(i))
        {
            count += m_editor.getText(i).trimmed().size();
        }
    }
    return count;
}

PDFAppAudioBookOverlay::State PDFAppAudioBookItemModel::getItemState(size_t itemIndex) const
{
    if (itemIndex >= m_editor.getItemCount() || m_editor.isRemoved(itemIndex))
    {
        return PDFAppAudioBookOverlay::State::Skipped;
    }
    return m_editor.isModified(itemIndex) ? PDFAppAudioBookOverlay::State::Modified : PDFAppAudioBookOverlay::State::Read;
}

void PDFAppAudioBookItemModel::setItemsRead(const std::set<size_t>& items, bool isRead)
{
    beginResetModel();
    for (size_t item : items)
    {
        if (item < m_editor.getItemCount())
        {
            if (isRead)
            {
                m_editor.addItem(item);
            }
            else
            {
                m_editor.removeItem(item);
            }
        }
    }
    updateRows();
    endResetModel();
}

void PDFAppAudioBookItemModel::setItemText(size_t itemIndex, const QString& text)
{
    if (itemIndex >= m_editor.getItemCount() || m_editor.getText(itemIndex) == text)
    {
        return;
    }

    beginResetModel();
    m_editor.setText(text, itemIndex);
    updateRows();
    endResetModel();
}

void PDFAppAudioBookItemModel::restoreTexts(const std::set<size_t>& items)
{
    beginResetModel();
    m_editor.deselect();
    for (size_t item : items)
    {
        if (item < m_editor.getItemCount())
        {
            m_editor.select(item, true);
        }
    }
    m_editor.restoreOriginalTexts();
    m_editor.deselect();
    updateRows();
    endResetModel();
}

void PDFAppAudioBookItemModel::moveSelection(bool isUp)
{
    if (m_selection.empty())
    {
        return;
    }

    beginResetModel();

    // The editor moves its selected items; the selection follows them
    m_editor.deselect();
    for (size_t item : m_selection)
    {
        m_editor.select(item, true);
    }
    if (isUp)
    {
        m_editor.moveSelectionUp();
    }
    else
    {
        m_editor.moveSelectionDown();
    }

    m_selection.clear();
    for (size_t i = 0; i < m_editor.getItemCount(); ++i)
    {
        if (m_editor.isSelected(i))
        {
            m_selection.insert(i);
        }
    }
    m_editor.deselect();
    updateRows();
    endResetModel();
}

PDFAppAudioBookItemModel::Changes PDFAppAudioBookItemModel::getChanges(const QStringList& readSpecial) const
{
    Changes changes;
    size_t lastOriginalIndex = 0;
    for (size_t i = 0; i < m_editor.getItemCount(); ++i)
    {
        const PDFDocumentTextFlowEditor::EditedItem* item = m_editor.getEditedItem(i);
        const bool isReadByDefault = PDFDocumentToolAudioBook::isReadByDefault(*item, readSpecial);
        if (isReadByDefault && m_editor.isRemoved(i))
        {
            ++changes.skipped;
        }
        if (!isReadByDefault && m_editor.isActive(i))
        {
            ++changes.read;
        }
        if (m_editor.isModified(i))
        {
            ++changes.edited;
        }
        if (i > 0 && item->originalIndex < lastOriginalIndex)
        {
            changes.isReordered = true;
        }
        lastOriginalIndex = item->originalIndex;
    }
    return changes;
}

void PDFAppAudioBookItemModel::updateRows()
{
    m_rows.clear();
    for (size_t i = 0; i < m_editor.getItemCount(); ++i)
    {
        bool isShown = true;
        switch (m_filter)
        {
            case Filter::All:
                break;
            case Filter::Skipped:
                isShown = m_editor.isRemoved(i);
                break;
            case Filter::Modified:
                isShown = m_editor.isModified(i);
                break;
        }
        if (isShown)
        {
            m_rows.push_back(i);
        }
    }
}

// --------------------------------------------------------- PDFAppAudioBookItemDelegate

PDFAppAudioBookItemDelegate::PDFAppAudioBookItemDelegate(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppAudioBookItemDelegate::~PDFAppAudioBookItemDelegate() = default;

QStringList PDFAppAudioBookItemDelegate::getTextLines(const QString& text, int width)
{
    const QFont font = PDFThemeFonts::getFont(QFont::Normal, 12);
    const QFontMetricsF metrics(font);
    QString simplified = text.simplified();

    QStringList lines;
    QTextLayout layout(simplified, font);
    layout.beginLayout();
    while (true)
    {
        QTextLine line = layout.createLine();
        if (!line.isValid())
        {
            break;
        }
        line.setLineWidth(qMax(1, width));
        if (lines.size() + 1 == MAXIMUM_LINES)
        {
            // The last line takes the rest, elided ("…")
            lines << metrics.elidedText(simplified.mid(line.textStart()), Qt::ElideRight, qMax(1, width));
            break;
        }
        lines << simplified.mid(line.textStart(), line.textLength()).trimmed();
    }
    layout.endLayout();
    if (lines.isEmpty())
    {
        lines << QString();
    }
    return lines;
}

QSize PDFAppAudioBookItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    // item 341:9401: 6 + the top line 21 + 2 + the lines of 15 + 6, rows 2 apart
    const int textWidth = option.rect.width() - TEXT_LEFT - 8;
    const int lineCount = int(getTextLines(index.data(Qt::DisplayRole).toString(), textWidth).size());
    return QSize(option.rect.width(), ROW_PADDING + TOP_HEIGHT + TEXT_GAP + lineCount * TEXT_LINE + ROW_PADDING + ROW_GAP);
}

void PDFAppAudioBookItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    const PDFThemeManager* manager = PDFThemeManager::getInstance();

    const QRect rect = option.rect.adjusted(0, 0, 0, -ROW_GAP);
    const bool isSelected = index.data(PDFAppAudioBookItemModel::IsSelectedRole).toBool();
    const PDFAppAudioBookOverlay::State state = PDFAppAudioBookOverlay::State(index.data(PDFAppAudioBookItemModel::StateRole).toInt());
    const bool isSkipped = state == PDFAppAudioBookOverlay::State::Skipped;
    const bool isRightToLeft = option.direction == Qt::RightToLeft;

    // The selected row: bg/accent-subtle, radius 8 (341:9437)
    if (isSelected)
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(manager->getColor(PDFThemeColors::BgAccentSubtle));
        painter->drawRoundedRect(QRectF(rect), 8.0, 8.0);
    }
    if (option.state.testFlag(QStyle::State_HasFocus))
    {
        painter->setPen(QPen(manager->getColor(PDFThemeColors::BorderFocus), 1.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);
    }

    // The bar of the state: 3 wide, 6 from the edge, rounded
    const QColor stateColor = PDFAppAudioBookOverlay::getStateColor(state);
    const int barX = isRightToLeft ? rect.right() - ROW_PADDING - 3 : rect.left() + ROW_PADDING;
    painter->setPen(Qt::NoPen);
    painter->setBrush(stateColor);
    painter->drawRoundedRect(QRectF(barX, rect.top() + ROW_PADDING, 3, rect.height() - 2 * ROW_PADDING), 1.5, 1.5);

    // The top line: "Page 308", the tag, the state
    const int textLeft = isRightToLeft ? rect.left() + 8 : rect.left() + TEXT_LEFT;
    const int textWidth = rect.width() - TEXT_LEFT - 8;
    const QFont smallFont = PDFThemeFonts::getFont(QFont::Normal, 11);
    const QFont stateFont = PDFThemeFonts::getFont(QFont::Medium, 11);
    const QFontMetrics smallMetrics(smallFont);
    const QFontMetrics stateMetrics(stateFont);
    const int topY = rect.top() + ROW_PADDING;

    const QString pageText = index.data(PDFAppAudioBookItemModel::PageTextRole).toString();
    const QString typeText = index.data(PDFAppAudioBookItemModel::TypeTextRole).toString();
    const QString stateText = index.data(PDFAppAudioBookItemModel::StateTextRole).toString();
    const int pageWidth = smallMetrics.horizontalAdvance(pageText);
    const int tagWidth = smallMetrics.horizontalAdvance(typeText) + 20;
    const int stateWidth = stateMetrics.horizontalAdvance(stateText);

    using Helper = PDFAppToolGUIAudioBookHelper;
    painter->setFont(smallFont);
    painter->setPen(manager->getColor(PDFThemeColors::TextTertiary));
    painter->drawText(QRect(Helper::place(isRightToLeft, textLeft, textWidth, 0, pageWidth), topY, pageWidth, TOP_HEIGHT), Qt::AlignVCenter, pageText);

    // The tag of 341:9406: bg/surface, border/default, radius 6, Regular 11 text/secondary
    const QRect tagRect(Helper::place(isRightToLeft, textLeft, textWidth, pageWidth + 6, tagWidth), topY, tagWidth, TOP_HEIGHT);
    painter->setPen(QPen(manager->getColor(PDFThemeColors::BorderDefault), 1.0));
    painter->setBrush(manager->getColor(PDFThemeColors::BgSurface));
    painter->drawRoundedRect(QRectF(tagRect).adjusted(0.5, 0.5, -0.5, -0.5), 6.0, 6.0);
    painter->setPen(manager->getColor(PDFThemeColors::TextSecondary));
    painter->drawText(tagRect, Qt::AlignCenter, typeText);

    // The state in words: Medium 11 text/tertiary (the bar has the color of the state, 341:9408)
    painter->setFont(stateFont);
    painter->setPen(manager->getColor(PDFThemeColors::TextTertiary));
    painter->drawText(QRect(Helper::place(isRightToLeft, textLeft, textWidth, pageWidth + 6 + tagWidth + 6, stateWidth), topY, stateWidth, TOP_HEIGHT), Qt::AlignVCenter, stateText);

    // The text: Regular 12 in lines of 15, crossed out and text/tertiary when skipped
    QFont textFont = PDFThemeFonts::getFont(QFont::Normal, 12);
    textFont.setStrikeOut(isSkipped);
    painter->setFont(textFont);
    painter->setPen(manager->getColor(isSkipped ? PDFThemeColors::TextTertiary : PDFThemeColors::TextPrimary));
    const QStringList lines = getTextLines(index.data(Qt::DisplayRole).toString(), textWidth);
    int y = topY + TOP_HEIGHT + TEXT_GAP;
    for (const QString& line : lines)
    {
        painter->drawText(QRect(textLeft, y, textWidth, TEXT_LINE), Qt::AlignVCenter | (isRightToLeft ? Qt::AlignRight : Qt::AlignLeft), line);
        y += TEXT_LINE;
    }
}

// --------------------------------------------------------- PDFAppAudioBookCanvas

PDFAppAudioBookCanvas::PDFAppAudioBookCanvas(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("audioBookCanvas"));

    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    m_content = new QWidget(this);
    outerLayout->addWidget(m_content);

    // document-canvas 341:9262: padding 20, gap 12
    QVBoxLayout* layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(GAP);

    // preview-bar 341:9474: the title Semi Bold 13, the segments Fit / 100 % (120 x 32)
    QWidget* bar = new QWidget(m_content);
    bar->setFixedHeight(BAR_HEIGHT);
    QHBoxLayout* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(8);
    m_title = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, bar);
    m_title->setObjectName(QStringLiteral("audioBookCanvasTitle"));
    barLayout->addWidget(m_title, 1);
    m_zoom = new PDFSegmentedControl(bar);
    m_zoom->setObjectName(QStringLiteral("audioBookCanvasZoom"));
    m_zoom->addSegment(QString());
    m_zoom->addSegment(QString());
    m_zoom->setFixedWidth(120);
    connect(m_zoom.data(), &PDFSegmentedControl::currentIndexChanged, this, &PDFAppAudioBookCanvas::onZoomChanged);
    barLayout->addWidget(m_zoom);
    layout->addWidget(bar);

    // legend 341:9489: a dot 10 and Regular 11 text/secondary, the items 14 apart
    QWidget* legend = new QWidget(m_content);
    legend->setObjectName(QStringLiteral("audioBookLegend"));
    QHBoxLayout* legendLayout = new QHBoxLayout(legend);
    legendLayout->setContentsMargins(0, 0, 0, 0);
    legendLayout->setSpacing(14);
    for (PDFAppAudioBookOverlay::State state : { PDFAppAudioBookOverlay::State::Read, PDFAppAudioBookOverlay::State::Selected, PDFAppAudioBookOverlay::State::Modified, PDFAppAudioBookOverlay::State::Skipped })
    {
        QWidget* item = new QWidget(legend);
        QHBoxLayout* itemLayout = new QHBoxLayout(item);
        itemLayout->setContentsMargins(0, 0, 0, 0);
        itemLayout->setSpacing(6);
        QLabel* dot = new QLabel(item);
        dot->setFixedSize(10, 10);
        dot->setProperty("audioBookState", int(state));
        dot->setObjectName(QStringLiteral("audioBookLegendDot"));
        itemLayout->addWidget(dot);
        PDFLabel* label = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextSecondary, item);
        label->setProperty("audioBookState", int(state));
        itemLayout->addWidget(label);
        legendLayout->addWidget(item);
        m_legendLabels.emplace_back(label);
    }
    legendLayout->addStretch(1);
    layout->addWidget(legend);

    // stage 341:9502: the page in a scroll area, the overlay over the picture
    m_pictureArea = new QScrollArea(m_content);
    m_pictureArea->setObjectName(QStringLiteral("audioBookStage"));
    m_pictureArea->setFrameShape(QFrame::NoFrame);
    m_pictureArea->setProperty("overlayScrollBars", true);
    m_pictureArea->viewport()->setAutoFillBackground(false);
    m_picture = new PDFAppToolPagePicture(m_pictureArea);
    m_pictureArea->setWidget(m_picture);
    m_pictureArea->setWidgetResizable(true);
    m_overlay = new PDFAppAudioBookOverlay(m_picture);
    layout->addWidget(m_pictureArea, 1);

    // The hint of Select by rectangle (360:13257): a dark pill at the bottom of the stage
    m_rectangleHint = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 12, PDFThemeColors::IconOnAccent, this);
    m_rectangleHint->setObjectName(QStringLiteral("audioBookRectangleHint"));
    m_rectangleHint->setAlignment(Qt::AlignCenter);
    m_rectangleHint->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_rectangleHint->setAutoFillBackground(false);
    m_rectangleHint->hide();

    updateTexts();
}

PDFAppAudioBookCanvas::~PDFAppAudioBookCanvas() = default;

void PDFAppAudioBookCanvas::setTitle(const QString& text)
{
    if (m_title)
    {
        m_title->setText(text);
    }
}

void PDFAppAudioBookCanvas::setRectangleHintShown(bool isShown)
{
    if (m_rectangleHint)
    {
        m_rectangleHint->setVisible(isShown);
        placeRectangleHint();
        m_rectangleHint->raise();
    }
}

void PDFAppAudioBookCanvas::setContentShown(bool isShown)
{
    if (m_content)
    {
        m_content->setVisible(isShown);
    }
}

void PDFAppAudioBookCanvas::updateTexts()
{
    if (m_zoom)
    {
        const QStringList segments = { tr("Fit"), tr("100 %") };
        const QList<QToolButton*> buttons = m_zoom->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
        for (qsizetype i = 0; i < buttons.size() && i < segments.size(); ++i)
        {
            buttons[i]->setText(segments[i]);
        }
        m_zoom->setAccessibleName(tr("Size of the page"));
    }
    for (const QPointer<PDFLabel>& label : m_legendLabels)
    {
        if (label)
        {
            label->setText(PDFAppToolGUIAudioBookHelper::getStateText(PDFAppAudioBookOverlay::State(label->property("audioBookState").toInt())));
        }
    }
    if (m_rectangleHint)
    {
        m_rectangleHint->setText(tr("Draw a rectangle · items completely inside are selected · Esc cancels"));
        placeRectangleHint();
    }
}

void PDFAppAudioBookCanvas::placeRectangleHint()
{
    if (!m_rectangleHint)
    {
        return;
    }

    // 360:13257: 36 high, 20 from the bottom, centered, padding 16 at the sides
    const int width = m_rectangleHint->fontMetrics().horizontalAdvance(m_rectangleHint->text()) + 32 + 22;
    const QRect hintRect((this->width() - width) / 2, height() - 20 - 36 - PADDING, width, 36);
    m_rectangleHint->setGeometry(hintRect);
}

void PDFAppAudioBookCanvas::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

void PDFAppAudioBookCanvas::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    placeRectangleHint();
}

void PDFAppAudioBookCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas: bg/track; the dots of the legend and the pill of the hint
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    painter.fillRect(rect(), manager->getColor(PDFThemeColors::BgTrack));

    if (m_content && m_content->isVisible())
    {
        for (const QLabel* dot : m_content->findChildren<QLabel*>(QStringLiteral("audioBookLegendDot")))
        {
            const QRect dotRect(dot->mapTo(this, QPoint(0, 0)), dot->size());
            painter.setPen(Qt::NoPen);
            painter.setBrush(PDFAppAudioBookOverlay::getStateColor(PDFAppAudioBookOverlay::State(dot->property("audioBookState").toInt())));
            painter.drawRoundedRect(QRectF(dotRect), 2.0, 2.0);
        }
    }
}

void PDFAppAudioBookCanvas::onZoomChanged(int index)
{
    if (!m_picture || !m_pictureArea)
    {
        return;
    }

    // Fit fills the stage, 100 % has the size of the page and scrolls
    const bool isFit = index == 0;
    m_picture->setFit(isFit);
    m_pictureArea->setWidgetResizable(isFit);
    if (!isFit)
    {
        m_picture->resize(m_picture->sizeHint().expandedTo(m_pictureArea->viewport()->size()));
    }
}

// --------------------------------------------------------- PDFAppAudioBookSelectPopover

PDFAppAudioBookSelectPopover::PDFAppAudioBookSelectPopover(Kind kind, PDFAppDocumentView* view, QWidget* parent) :
    BaseClass(view, WIDTH, parent),
    m_kind(kind)
{
    setObjectName(QStringLiteral("audioBookSelectPopover"));

    QString title;
    QString iconName;
    QString placeholder;
    switch (kind)
    {
        case Kind::ContainedText:
            title = PDFAppToolGUIAudioBook::tr("Select by contained text");
            iconName = QStringLiteral("search");
            placeholder = PDFAppToolGUIAudioBook::tr("Text to look for");
            break;
        case Kind::RegularExpression:
            title = PDFAppToolGUIAudioBook::tr("Select by regular expression");
            iconName = QStringLiteral("regex");
            placeholder = PDFAppToolGUIAudioBook::tr("Regular expression");
            break;
        case Kind::PageList:
            title = PDFAppToolGUIAudioBook::tr("Select by page list");
            iconName = QStringLiteral("hash");
            placeholder = PDFAppToolGUIAudioBook::tr("1-12, 17, 24-");
            break;
    }

    QVBoxLayout* layout = getContentLayout();
    layout->setSpacing(10);

    // The head: the icon in an accent tile 24 and Semi Bold 13 text/primary (341:10217)
    QWidget* head = new QWidget(this);
    QHBoxLayout* headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(10);
    PDFIconBadge* badge = new PDFIconBadge(head);
    badge->setIconName(iconName);
    headLayout->addWidget(badge);
    headLayout->addWidget(PDFAppPageWidgets::createLabel(title, QFont::DemiBold, 13, PDFThemeColors::TextPrimary, head), 1);
    layout->addWidget(head);

    m_field = new PDFLineEdit(this);
    m_field->setObjectName(QStringLiteral("audioBookSelectField"));
    m_field->setPlaceholderText(placeholder);
    m_field->setAccessibleName(title);
    connect(m_field.data(), &QLineEdit::textEdited, this, &PDFAppAudioBookSelectPopover::onTextEdited);
    connect(m_field.data(), &QLineEdit::returnPressed, this, &PDFAppAudioBookSelectPopover::onSelectClicked);
    layout->addWidget(m_field);

    // The line of the check: the icon 14 and Regular 11 (green / red)
    QWidget* checkRow = new QWidget(this);
    QHBoxLayout* checkLayout = new QHBoxLayout(checkRow);
    checkLayout->setContentsMargins(0, 0, 0, 0);
    checkLayout->setSpacing(6);
    m_checkIcon = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextSecondary, checkRow);
    m_check = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextSecondary, checkRow);
    m_check->setObjectName(QStringLiteral("audioBookSelectCheck"));
    m_check->setWordWrap(true);
    checkLayout->addWidget(m_check, 1);
    m_checkIcon->hide();
    layout->addWidget(checkRow);

    // Cancel / Select at the trailing edge
    QWidget* buttons = new QWidget(this);
    QHBoxLayout* buttonsLayout = new QHBoxLayout(buttons);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    buttonsLayout->setSpacing(8);
    buttonsLayout->addStretch(1);
    m_cancelButton = PDFAppPageWidgets::createButton(PDFAppToolGUIAudioBook::tr("Cancel"), QString(), false, buttons);
    m_cancelButton->setObjectName(QStringLiteral("audioBookSelectCancel"));
    connect(m_cancelButton.data(), &QPushButton::clicked, this, &QWidget::close);
    buttonsLayout->addWidget(m_cancelButton);
    m_selectButton = PDFAppPageWidgets::createButton(PDFAppToolGUIAudioBook::tr("Select"), QStringLiteral("check"), true, buttons);
    m_selectButton->setObjectName(QStringLiteral("audioBookSelectButton"));
    m_selectButton->setEnabled(false);
    connect(m_selectButton.data(), &QPushButton::clicked, this, &PDFAppAudioBookSelectPopover::onSelectClicked);
    buttonsLayout->addWidget(m_selectButton);
    layout->addWidget(buttons);
}

PDFAppAudioBookSelectPopover::~PDFAppAudioBookSelectPopover() = default;

void PDFAppAudioBookSelectPopover::setCheck(bool isValid, const QString& text)
{
    m_isValid = isValid;
    if (m_check)
    {
        m_check->setText(text);
        m_check->setTextColor(isValid ? PDFThemeColors::TextSecondary : PDFThemeColors::TextDanger);
    }
    if (m_selectButton)
    {
        m_selectButton->setEnabled(isValid);
    }
}

void PDFAppAudioBookSelectPopover::onTextEdited(const QString& text)
{
    Q_EMIT textChanged(text);
}

void PDFAppAudioBookSelectPopover::onSelectClicked()
{
    if (!m_isValid || !m_field)
    {
        return;
    }

    // The popover closes first: the selection runs the code of the GUI, the signal is last
    const QString text = m_field->text();
    QPointer<PDFAppAudioBookSelectPopover> guard(this);
    close();
    if (guard)
    {
        Q_EMIT selectRequested(text);
    }
}

// --------------------------------------------------------- PDFAppToolGUIAudioBook

PDFAppToolGUIAudioBook::PDFAppToolGUIAudioBook(QObject* parent) :
    BaseClass(parent)
{
    // Ownership: the model of the items and the timer of the edited text are children of the GUI
    m_itemModel = new PDFAppAudioBookItemModel(this);
    m_commitTimer = new QTimer(this);
    m_commitTimer->setSingleShot(true);
    m_commitTimer->setInterval(PDFAppToolGUIAudioBookHelper::COMMIT_DELAY);
    connect(m_commitTimer.data(), &QTimer::timeout, this, &PDFAppToolGUIAudioBook::commitEditedText);
    // The edited text is not lost when the tool closes before the rest ends (retire fires the timer)
    PDFDeferredDeletion::setFlushOnRetire(m_commitTimer.data());
    m_selectionTimer = new QTimer(this);
    m_selectionTimer->setSingleShot(true);
    m_selectionTimer->setInterval(0);
    connect(m_selectionTimer.data(), &QTimer::timeout, this, &PDFAppToolGUIAudioBook::applyListSelection);
}

void PDFAppToolGUIAudioBook::retire()
{
    BaseClass::retire();

    // A running job ends by itself, its result goes nowhere; the sample is silent at once (its signals reach this
    // GUI no more - the hook emits nothing of the GUI)
    cancelStreamJob();
    if (PDFAppSpeechEngine* engine = m_sampleEngine.data())
    {
        engine->disconnect(this);
        engine->stop();
    }
}

PDFAppToolGUIAudioBook::~PDFAppToolGUIAudioBook()
{
    // A running job ends by itself; its result goes nowhere (the watcher is a child of this GUI)
    cancelStreamJob();
    if (m_sampleEngine)
    {
        m_sampleEngine->stop();
    }
}

void PDFAppToolGUIAudioBook::registerAudioBookTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolAudioBook::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolAudioBook>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIAudioBook>>());
    }
}

QVariantMap PDFAppToolGUIAudioBook::getRunParameters() const
{
    // A text being typed, which is not in the stream yet (its pause has not come), is read too
    QVariantMap parameters = BaseClass::getRunParameters();
    if (m_hasPendingText && m_itemModel)
    {
        parameters.insert(PDFAppToolGUIAudioBookHelper::name(PDFDocumentToolAudioBook::PARAMETER_EDITED_STREAM), QVariant::fromValue(PDFDocumentToolAudioBookStream(m_itemModel->getEditor())));
    }
    return parameters;
}

void PDFAppToolGUIAudioBook::ensureVoices()
{
    if (m_isVoicesKnown || m_voicesWatcher)
    {
        return;
    }

    // The voices of the engine of the runs: the one factory of the application context, which
    // the runs get too (the factory finds them once; R4-15)
    const std::shared_ptr<PDFSpeechSynthesizerFactory> factory = getContext() ? getContext()->getSpeechSynthesizerFactory() : nullptr;
    if (!factory)
    {
        m_isVoicesKnown = true;
        return;
    }

    // One asynchronous job (CLAUDE.md: QtConcurrent for one job): the engine of the system starts
    // in seconds the first time (winrt about 5 s, then about 60 ms) - never in the GUI thread. The
    // job holds its own copy of the factory; a GUI deleted meanwhile deletes the watcher and the
    // result is dropped.
    m_voicesWatcher = new QFutureWatcher<std::vector<PDFSpeechVoice>>(this);
    connect(m_voicesWatcher.data(), &QFutureWatcherBase::finished, this, &PDFAppToolGUIAudioBook::onVoicesProbed);
    m_voicesWatcher->setFuture(QtConcurrent::run(&PDFAppToolGUIAudioBook::probeVoices, factory));
}

std::vector<PDFSpeechVoice> PDFAppToolGUIAudioBook::probeVoices(std::shared_ptr<PDFSpeechSynthesizerFactory> factory)
{
    // A worker: the factory is thread-safe (its probe is serialized by its mutex). One question
    // only: a factory, which has found no voice, probes again at each question (341:11665 "open
    // this tool again"), so the opening of the tool probes once.
    return factory ? factory->getVoices() : std::vector<PDFSpeechVoice>();
}

void PDFAppToolGUIAudioBook::onVoicesProbed()
{
    QFutureWatcher<std::vector<PDFSpeechVoice>>* watcher = m_voicesWatcher.data();
    if (!watcher || sender() != watcher)
    {
        return;
    }

    m_voices = watcher->result();
    m_voicesWatcher = nullptr;
    watcher->deleteLater();
    m_isVoicesKnown = true;

    // A voice, which is not there any more (a remembered value), or no voice yet: the first voice
    // of the language of the user interface, else the first voice - it follows, no step of Undo
    QPointer<PDFAppToolGUIAudioBook> guard(this);
    PDFAppToolParameterModel* model = getModel();
    if (model && !m_voices.empty())
    {
        const QString voice = model->getValue(PDFAppToolGUIAudioBookHelper::name(PDFDocumentToolAudioBook::PARAMETER_VOICE)).toString();
        bool isKnown = false;
        for (const PDFSpeechVoice& item : m_voices)
        {
            isKnown = isKnown || item.name == voice;
        }
        if (!isKnown)
        {
            const QString language = PDFAppLocale::getUiLocale().bcp47Name();
            const QString chosen = PDFDocumentToolAudioBook::getVoiceForLanguage(m_voices, language, m_voices.front().name, QString());
            model->setFollowingValue(PDFAppToolGUIAudioBookHelper::name(PDFDocumentToolAudioBook::PARAMETER_VOICE), chosen);
        }
    }
    if (!guard)
    {
        return;
    }

    // 341:11665: without a voice the message comes once, after the tool has opened (the canvas
    // exists); the voice menu, the actions and the states follow the voices
    if (m_voices.empty() && m_canvas && !m_isNoVoiceMessageShown)
    {
        QTimer::singleShot(0, this, &PDFAppToolGUIAudioBook::showNoVoiceMessage);
    }
    updateAll();
}

void PDFAppToolGUIAudioBook::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIAudioBookHelper;
    using Tool = PDFDocumentToolAudioBook;

    ensureVoices();

    // tool-actions 341:9249: Select, Read, Skip | up, down | Sync, Order, Voice
    m_selectMenu = builder.addMenu(QStringLiteral("audioBookSelect"), QStringLiteral("select-rect"), tr("Select"), Helper::SELECT_MENU_WIDTH);
    if (m_selectMenu)
    {
        struct Row { const char* key; const char* title; const char* description; const char* icon; };
        const Row rows[] = {
            { "rectangle", QT_TR_NOOP("By rectangle"), QT_TR_NOOP("Draw a rectangle on the page; items inside are selected"), "select-rect" },
            { "text", QT_TR_NOOP("By contained text"), QT_TR_NOOP("Items that contain the typed text, case sensitive"), "search" },
            { "regex", QT_TR_NOOP("By regular expression"), QT_TR_NOOP("Items that match a pattern, for example page numbers"), "regex" },
            { "pages", QT_TR_NOOP("By page list"), QT_TR_NOOP("Items on the pages 1-12,17,24-"), "hash" },
        };
        for (const Row& row : rows)
        {
            PDFTileRow* tileRow = m_selectMenu->addRow(tr(row.title), tr(row.description), QString::fromLatin1(row.icon), PDFTileRow::Kind::MenuItem);
            tileRow->setObjectName(QStringLiteral("audioBookSelect-") + QString::fromLatin1(row.key));
            tileRow->setProperty(Helper::ROW_KEY_PROPERTY, QString::fromLatin1(row.key));
            connect(tileRow, &PDFTileRow::clicked, this, &PDFAppToolGUIAudioBook::onSelectMenuRowClicked);
        }
    }

    m_readButton = builder.addAction(QString(), QStringLiteral("volume-2"), this, &PDFAppToolGUIAudioBook::readSelected);
    m_readButton->setObjectName(QStringLiteral("audioBookRead"));
    m_readButton->setToolTip(tr("Read the selected items (Shift+Delete)"));
    m_readButton->setAccessibleName(tr("Read"));
    m_skipButton = builder.addAction(QString(), QStringLiteral("volume-off"), this, &PDFAppToolGUIAudioBook::skipSelected);
    m_skipButton->setObjectName(QStringLiteral("audioBookSkip"));
    m_skipButton->setToolTip(tr("Skip the selected items (Delete)"));
    m_skipButton->setAccessibleName(tr("Skip"));
    builder.addActionDivider();
    m_moveUpButton = builder.addAction(QString(), QStringLiteral("arrow-up"), this, &PDFAppToolGUIAudioBook::moveSelectionUp);
    m_moveUpButton->setObjectName(QStringLiteral("audioBookMoveUp"));
    m_moveUpButton->setToolTip(tr("Move the selected items up (Alt+Up)"));
    m_moveUpButton->setAccessibleName(tr("Move up"));
    m_moveDownButton = builder.addAction(QString(), QStringLiteral("arrow-down"), this, &PDFAppToolGUIAudioBook::moveSelectionDown);
    m_moveDownButton->setObjectName(QStringLiteral("audioBookMoveDown"));
    m_moveDownButton->setToolTip(tr("Move the selected items down (Alt+Down)"));
    m_moveDownButton->setAccessibleName(tr("Move down"));
    builder.addActionDivider();

    m_syncMenu = builder.addMenu(QStringLiteral("audioBookSync"), QStringLiteral("link"), tr("Sync"), Helper::SYNC_MENU_WIDTH);
    if (m_syncMenu)
    {
        PDFTileRow* listToPage = m_syncMenu->addRow(tr("List to page"), tr("Selecting in the list scrolls the page to the item"), QStringLiteral("list"), PDFTileRow::Kind::MenuItem);
        listToPage->setObjectName(QStringLiteral("audioBookSync-listToPage"));
        listToPage->setProperty(Helper::ROW_KEY_PROPERTY, QStringLiteral("listToPage"));
        listToPage->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Toggle);
        connect(listToPage, &PDFTileRow::clicked, this, &PDFAppToolGUIAudioBook::onSyncMenuRowClicked);
        PDFTileRow* pageToList = m_syncMenu->addRow(tr("Page to list"), tr("Clicking an item on the page scrolls the list to it"), QStringLiteral("file"), PDFTileRow::Kind::MenuItem);
        pageToList->setObjectName(QStringLiteral("audioBookSync-pageToList"));
        pageToList->setProperty(Helper::ROW_KEY_PROPERTY, QStringLiteral("pageToList"));
        pageToList->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Toggle);
        connect(pageToList, &PDFTileRow::clicked, this, &PDFAppToolGUIAudioBook::onSyncMenuRowClicked);
    }

    m_orderMenu = builder.addEnumMenu(Helper::name(Tool::PARAMETER_ORDER), QStringLiteral("align-left"), tr("Order: %1"), Helper::ORDER_MENU_WIDTH);
    if (m_orderMenu && m_orderMenu->getPanel())
    {
        m_orderMenu->getPanel()->addGroupDivider();
        PDFLabel* note = m_orderMenu->getPanel()->addGroupCaption(tr("A new order rebuilds the text stream and discards your edits"));
        note->setWordWrap(true);
        note->setObjectName(QStringLiteral("audioBookOrderNote"));
    }

    m_voiceMenu = builder.addMenu(QStringLiteral("audioBookVoice"), QStringLiteral("mic"), tr("Voice: %1").arg(getVoiceTitle()), Helper::VOICE_MENU_WIDTH);
    updateVoiceMenu();
    updateSyncMenu();
    updateActions();
}

void PDFAppToolGUIAudioBook::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    // sidebar-left 341:9256: x 12, width 275
    if (QWidget* widget = builder.getWidget())
    {
        if (QLayout* layout = widget->layout())
        {
            layout->setContentsMargins(2, 0, 2, 0);
        }
    }

    // header 341:9368: "Text stream" Semi Bold 13 and the count Regular 11; the link Rebuild
    QWidget* header = new QWidget(builder.getWidget());
    header->setObjectName(QStringLiteral("audioBookStreamHeader"));
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    QWidget* titles = new QWidget(header);
    QVBoxLayout* titlesLayout = new QVBoxLayout(titles);
    titlesLayout->setContentsMargins(0, 0, 0, 0);
    titlesLayout->setSpacing(1);
    m_streamTitle = PDFAppPageWidgets::createLabel(tr("Text stream"), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, titles);
    m_streamTitle->setObjectName(QStringLiteral("audioBookStreamTitle"));
    m_streamCount = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextTertiary, titles);
    m_streamCount->setObjectName(QStringLiteral("audioBookStreamCount"));
    titlesLayout->addWidget(m_streamTitle);
    titlesLayout->addWidget(m_streamCount);
    headerLayout->addWidget(titles, 1);
    m_rebuildLink = new PDFTextLink(tr("Rebuild"), header);
    m_rebuildLink->setObjectName(QStringLiteral("audioBookRebuild"));
    connect(m_rebuildLink.data(), &PDFTextLink::clicked, this, &PDFAppToolGUIAudioBook::onRebuildClicked);
    headerLayout->addWidget(m_rebuildLink, 0, Qt::AlignVCenter);
    builder.addWidget(header);

    // filters 341:9376: the chips All, Skipped, Modified (gap 6)
    QWidget* chips = new QWidget(builder.getWidget());
    chips->setObjectName(QStringLiteral("audioBookFilters"));
    QHBoxLayout* chipsLayout = new QHBoxLayout(chips);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);
    m_filterChips.clear();
    for (PDFAppAudioBookItemModel::Filter filter : { PDFAppAudioBookItemModel::Filter::All, PDFAppAudioBookItemModel::Filter::Skipped, PDFAppAudioBookItemModel::Filter::Modified })
    {
        QString text = tr("All");
        if (filter == PDFAppAudioBookItemModel::Filter::Skipped)
        {
            text = tr("Skipped");
        }
        else if (filter == PDFAppAudioBookItemModel::Filter::Modified)
        {
            text = tr("Modified");
        }
        QPushButton* chip = new QPushButton(PDFAppPageWidgets::escapeMnemonic(text), chips);
        chip->setProperty(PDFWidgetStyleProperties::CHIP, true);
        chip->setCheckable(true);
        chip->setAutoDefault(false);
        chip->setProperty("audioBookFilter", int(filter));
        chip->setObjectName(QStringLiteral("audioBookFilter-%1").arg(int(filter)));
        connect(chip, &QPushButton::clicked, this, &PDFAppToolGUIAudioBook::onFilterChipClicked);
        chipsLayout->addWidget(chip);
        m_filterChips.emplace_back(chip);
    }
    chipsLayout->addStretch(1);
    builder.addWidget(chips);

    // item-list 341:9400: the rows of the items
    m_listView = new PDFAppAudioBookListView(builder.getWidget());
    m_listView->setObjectName(QStringLiteral("audioBookList"));
    m_listView->setModel(m_itemModel);
    m_listView->setItemDelegate(new PDFAppAudioBookItemDelegate(m_listView));
    m_listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_listView->setUniformItemSizes(false);
    m_listView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listView->setAccessibleName(tr("Text stream"));

    // item-list 341:9400: the rows lie on the panel itself, no frame and no background of a card
    m_listView->setFrameShape(QFrame::NoFrame);
    m_listView->viewport()->setAutoFillBackground(false);
    m_listView->setAutoFillBackground(false);
    m_listView->setProperty("overlayScrollBars", true);
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PDFAppToolGUIAudioBook::onListSelectionChanged);
    connect(m_listView.data(), &QListView::clicked, this, &PDFAppToolGUIAudioBook::onListActivated);

    // 2.7: the context menu of the list - Select, Read, Skip, Move
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listView.data(), &QWidget::customContextMenuRequested, this, &PDFAppToolGUIAudioBook::onListContextMenuRequested);
    builder.addWidget(m_listView);
    builder.setFillsPanel(m_listView);

    // The empty state and the reading of the stream (2.7: "This document has no text to read")
    m_emptyLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextSecondary, builder.getWidget());
    m_emptyLabel->setObjectName(QStringLiteral("audioBookEmpty"));
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    builder.addWidget(m_emptyLabel);

    // 341:9473
    m_listHint = builder.addHint(tr("Click an item here or on the page; Ctrl adds to the selection. Delete skips the selected items, Shift+Delete makes them read again."));
    m_listHint->setObjectName(QStringLiteral("audioBookListHint"));

    // The keys of the list (plan 4.7): Delete, Shift+Delete, Alt+Up, Alt+Down (eventFilter)
    m_listView->installEventFilter(this);

    if (PDFDocumentToolShell* shell = getShell())
    {
        connect(shell, &PDFDocumentToolShell::stateChanged, this, &PDFAppToolGUIAudioBook::onShellStateChanged, Qt::UniqueConnection);
    }

    // The stream of the value (Undo of a tool, which was open before), else a new one
    if (PDFAppToolGUIAudioBookHelper::getStream(getModel()).getEditor())
    {
        loadStreamFromModel();
    }
    else if (!isBuildingStream() && m_itemModel && m_itemModel->getItemCount() == 0)
    {
        startStreamJob(false);
    }
    m_lastOrder = getModel() ? getModel()->getValue(PDFAppToolGUIAudioBookHelper::name(PDFDocumentToolAudioBook::PARAMETER_ORDER)).toString() : QString();
    updateAll();
}

void PDFAppToolGUIAudioBook::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIAudioBookHelper;

    // section-Selected item 341:9572: the title, the tag "Text · page 308", the link Restore
    PDFAppToolSectionHeader* selectedHeader = builder.addSection(tr("Selected item"));
    m_selectedSection = selectedHeader;
    selectedHeader->setFixedHeight(21);
    m_selectedTag = new PDFTag(selectedHeader);
    m_selectedTag->setObjectName(QStringLiteral("audioBookSelectedTag"));
    if (QHBoxLayout* headerLayout = qobject_cast<QHBoxLayout*>(selectedHeader->layout()))
    {
        // The tag right after the title (before the stretch), the link at the trailing edge
        headerLayout->insertWidget(headerLayout->count() - 1, m_selectedTag, 0, Qt::AlignVCenter);
    }
    m_restoreLink = new PDFTextLink(tr("Restore"));
    m_restoreLink->setObjectName(QStringLiteral("audioBookRestore"));
    m_restoreLink->setToolTip(tr("Restore the original text of the selected items"));
    connect(m_restoreLink.data(), &PDFTextLink::clicked, this, &PDFAppToolGUIAudioBook::onRestoreClicked);
    selectedHeader->addTrailingWidget(m_restoreLink);

    // card 341:9580: the editor of the text, Read this item aloud, the note
    PDFTintedCard* selectedCard = new PDFTintedCard(builder.getWidget());
    selectedCard->setObjectName(QStringLiteral("audioBookSelectedCard"));
    QVBoxLayout* selectedLayout = new QVBoxLayout(selectedCard);
    selectedLayout->setContentsMargins(13, 13, 13, 13);
    selectedLayout->setSpacing(8);
    m_textEditor = new PDFPlainTextEdit(selectedCard);
    m_textEditor->setObjectName(QStringLiteral("audioBookTextEditor"));
    m_textEditor->setFixedHeight(107);
    m_textEditor->setAccessibleName(tr("Text of the selected item"));
    m_textEditor->installEventFilter(this);
    connect(m_textEditor.data(), &QPlainTextEdit::textChanged, this, &PDFAppToolGUIAudioBook::onTextEdited);
    selectedLayout->addWidget(m_textEditor);
    m_readCheckBox = new QCheckBox(tr("Read this item aloud"), selectedCard);
    m_readCheckBox->setObjectName(QStringLiteral("audioBookReadCheckBox"));
    connect(m_readCheckBox.data(), &QCheckBox::toggled, this, &PDFAppToolGUIAudioBook::onReadCheckBoxToggled);
    selectedLayout->addWidget(m_readCheckBox);
    selectedLayout->addWidget(PDFAppPageWidgets::createText(tr("Edit the text to fix how it is read. Skipped items are not read."), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextMuted, selectedCard));
    builder.addWidget(selectedCard);
    builder.endSection();

    // section-Voice 341:9591: Speed, Volume, File format, Play Sample and the note
    builder.addSection(tr("Voice and format"));
    PDFTintedCard* voiceCard = new PDFTintedCard(builder.getWidget());
    voiceCard->setObjectName(QStringLiteral("audioBookVoiceCard"));
    QVBoxLayout* voiceLayout = new QVBoxLayout(voiceCard);
    voiceLayout->setContentsMargins(13, 13, 13, 13);
    voiceLayout->setSpacing(8);

    m_speedLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextSecondary, voiceCard);
    m_speedLabel->setObjectName(QStringLiteral("audioBookSpeedLabel"));
    m_speedSlider = new QSlider(Qt::Horizontal, voiceCard);
    m_speedSlider->setObjectName(QStringLiteral("audioBookSpeed"));
    m_speedSlider->setRange(-Helper::SLIDER_SCALE, Helper::SLIDER_SCALE);
    m_speedSlider->setPageStep(10);
    m_speedSlider->setAccessibleName(tr("Speed"));
    connect(m_speedSlider.data(), &QSlider::valueChanged, this, &PDFAppToolGUIAudioBook::onSpeedChanged);
    connect(m_speedSlider.data(), &QSlider::sliderReleased, this, &PDFAppToolGUIAudioBook::onSliderReleased);
    Helper::addField(voiceCard, voiceLayout, m_speedLabel, m_speedSlider);

    m_volumeLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextSecondary, voiceCard);
    m_volumeLabel->setObjectName(QStringLiteral("audioBookVolumeLabel"));
    m_volumeSlider = new QSlider(Qt::Horizontal, voiceCard);
    m_volumeSlider->setObjectName(QStringLiteral("audioBookVolume"));
    m_volumeSlider->setRange(0, Helper::SLIDER_SCALE);
    m_volumeSlider->setPageStep(10);
    m_volumeSlider->setAccessibleName(tr("Volume"));
    connect(m_volumeSlider.data(), &QSlider::valueChanged, this, &PDFAppToolGUIAudioBook::onVolumeChanged);
    connect(m_volumeSlider.data(), &QSlider::sliderReleased, this, &PDFAppToolGUIAudioBook::onSliderReleased);
    Helper::addField(voiceCard, voiceLayout, m_volumeLabel, m_volumeSlider);

    // File format: WAV only (owner 14. 11.; Figma 341:9229 shows MP3)
    PDFLabel* formatLabel = PDFAppPageWidgets::createLabel(tr("File format"), QFont::Normal, 12, PDFThemeColors::TextSecondary, voiceCard);
    m_formatCombo = new PDFComboBox(voiceCard);
    m_formatCombo->setObjectName(QStringLiteral("audioBookFormat"));
    m_formatCombo->addItem(tr("WAV"), QString::fromLatin1(PDFDocumentToolAudioBook::FORMAT_WAV));
    m_formatCombo->setAccessibleName(tr("File format"));
    Helper::addField(voiceCard, voiceLayout, formatLabel, m_formatCombo);

    m_playSampleButton = PDFAppPageWidgets::createButton(tr("Play Sample"), QStringLiteral("play"), false, voiceCard);
    m_playSampleButton->setObjectName(QStringLiteral("audioBookPlaySample"));
    connect(m_playSampleButton.data(), &QPushButton::clicked, this, &PDFAppToolGUIAudioBook::onPlaySampleClicked);
    voiceLayout->addWidget(m_playSampleButton, 0, Qt::AlignLeading);
    voiceLayout->addWidget(PDFAppPageWidgets::createText(tr("The voice is chosen in the toolbar. The file name is asked when the audio book is ready."), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextMuted, voiceCard));
    builder.addWidget(voiceCard);
    builder.endSection();

    // section-Summary 341:9647
    builder.addSection(tr("Summary"));
    m_summaryRows = builder.addKeyValues();
    m_summaryRows->setObjectName(QStringLiteral("audioBookSummary"));
    builder.endSection();

    updateAll();
}

QWidget* PDFAppToolGUIAudioBook::createCanvasPage(QWidget* parent)
{
    m_canvas = new PDFAppAudioBookCanvas(parent);
    PDFAppToolHost* host = getHost();
    PDFAppDocumentView* view = host ? host->getView() : nullptr;
    // The page of the view without a soft proof kept on the tab (Q7.10)
    m_canvas->getPicture()->setView(view);
    if (view && m_connectedView != view)
    {
        PDFDeferredDeletion::connect(view, &PDFAppDocumentView::displayedPageChanged, this, &PDFAppToolGUIAudioBook::onDisplayedPageChanged);
        m_connectedView = view;
    }

    // Queued (one thread): the overlay emits inside of its mouse and key events; the selection
    // runs after them, its signals may delete the canvas with the overlay
    PDFAppAudioBookOverlay* overlay = m_canvas->getOverlay();
    connect(overlay, &PDFAppAudioBookOverlay::itemClicked, this, &PDFAppToolGUIAudioBook::onOverlayItemClicked, Qt::QueuedConnection);
    connect(overlay, &PDFAppAudioBookOverlay::rectangleSelected, this, &PDFAppToolGUIAudioBook::onOverlayRectangleSelected, Qt::QueuedConnection);
    connect(overlay, &PDFAppAudioBookOverlay::rectangleCanceled, this, &PDFAppToolGUIAudioBook::onOverlayRectangleCanceled, Qt::QueuedConnection);

    m_shownPageIndex = view ? qMax<PDFInteger>(0, view->getDisplayedPageIndex()) : 0;
    updateCanvas();

    // 341:11665: without a voice the message comes once, after the tool has opened (the voices
    // still being looked for: onVoicesProbed shows it)
    if (m_isVoicesKnown && m_voices.empty() && !m_isNoVoiceMessageShown)
    {
        QTimer::singleShot(0, this, &PDFAppToolGUIAudioBook::showNoVoiceMessage);
    }
    return m_canvas.data();
}

PDFAppAudioBookOverlay* PDFAppToolGUIAudioBook::getOverlay() const
{
    return m_canvas ? m_canvas->getOverlay() : nullptr;
}

QPushButton* PDFAppToolGUIAudioBook::getFilterChip(PDFAppAudioBookItemModel::Filter filter) const
{
    for (const QPointer<QPushButton>& chip : m_filterChips)
    {
        if (chip && chip->property("audioBookFilter").toInt() == int(filter))
        {
            return chip;
        }
    }
    return nullptr;
}

// --------------------------------------------------------- the stream

PDFAppToolGUIAudioBook::StreamJobResult PDFAppToolGUIAudioBook::createStreamJob(quint64 generation, PDFDocumentPointer document, QVariantMap parameters, std::shared_ptr<PDFOCRCancelToken> token)
{
    // A thread of the pool: only the values of the arguments (the document is shared and immutable)
    StreamJobResult result;
    result.generation = generation;
    QString errorMessage;
    result.flow = PDFDocumentToolAudioBook::createStream(document.data(), parameters, token.get(), nullptr, &errorMessage);
    result.readSpecial = PDFDocumentToolAudioBook::getReadSpecial(parameters, nullptr);
    return result;
}

bool PDFAppToolGUIAudioBook::isBuildingStream() const
{
    return m_streamWatcher && m_streamWatcher->isRunning();
}

void PDFAppToolGUIAudioBook::cancelStreamJob()
{
    if (m_streamToken)
    {
        m_streamToken->cancel();
        m_streamToken.reset();
    }
    if (m_streamWatcher)
    {
        // The late result is dropped: the watcher goes, the job ends by itself
        m_streamWatcher->disconnect(this);
        m_streamWatcher->deleteLater();
        m_streamWatcher = nullptr;
    }
}

void PDFAppToolGUIAudioBook::startStreamJob(bool isUndoStep)
{
    const PDFAppDocument* document = getDocument();
    const PDFAppToolParameterModel* model = getModel();
    if (!document || !document->getDocument() || !model || !m_itemModel)
    {
        return;
    }

    cancelStreamJob();
    m_isStreamJobUndoStep = isUndoStep;
    m_streamToken = std::make_shared<PDFOCRCancelToken>();

    // One asynchronous job (CLAUDE.md: QtConcurrent for one job); a new job replaces an old one
    m_streamWatcher = new QFutureWatcher<StreamJobResult>(this);
    connect(m_streamWatcher.data(), &QFutureWatcherBase::finished, this, &PDFAppToolGUIAudioBook::onStreamJobFinished);
    QVariantMap parameters = model->getValues();
    parameters.remove(PDFAppToolGUIAudioBookHelper::name(PDFDocumentToolAudioBook::PARAMETER_EDITED_STREAM));
    m_streamWatcher->setFuture(QtConcurrent::run(&PDFAppToolGUIAudioBook::createStreamJob, ++m_streamGeneration, document->getDocument(), parameters, m_streamToken));

    m_itemModel->setBuilding(true);
    updateAll();
}

void PDFAppToolGUIAudioBook::onStreamJobFinished()
{
    QFutureWatcher<StreamJobResult>* watcher = m_streamWatcher.data();
    if (!watcher || sender() != watcher || !m_itemModel)
    {
        return;
    }

    const StreamJobResult result = watcher->result();
    m_streamWatcher = nullptr;
    watcher->deleteLater();
    m_streamToken.reset();
    if (result.generation != m_streamGeneration)
    {
        return;
    }

    // The editor of the new stream: the items Read / Skip by the defaults of the tool (R4-13, R4-15)
    PDFDocumentTextFlowEditor editor;
    editor.setTextFlow(result.flow);
    for (size_t i = 0; i < editor.getItemCount(); ++i)
    {
        if (PDFDocumentToolAudioBook::isReadByDefault(*std::as_const(editor).getEditedItem(i), result.readSpecial))
        {
            editor.addItem(i);
        }
        else
        {
            editor.removeItem(i);
        }
    }
    m_itemModel->setEditor(std::move(editor));

    QPointer<PDFAppToolGUIAudioBook> guard(this);
    storeStream(m_isStreamJobUndoStep);
    if (guard)
    {
        updateAll();
    }
}

void PDFAppToolGUIAudioBook::storeStream(bool isUndoStep)
{
    PDFAppToolParameterModel* model = getModel();
    if (!model || !m_itemModel)
    {
        return;
    }

    // The value carries the state of the editor; this GUI knows its own value (no reload)
    const PDFDocumentToolAudioBookStream stream(m_itemModel->getEditor());
    m_storedStream = stream;
    const QString name = PDFAppToolGUIAudioBookHelper::name(PDFDocumentToolAudioBook::PARAMETER_EDITED_STREAM);
    if (isUndoStep)
    {
        model->setValue(name, QVariant::fromValue(stream));
    }
    else
    {
        model->setFollowingValue(name, QVariant::fromValue(stream));
    }
}

void PDFAppToolGUIAudioBook::loadStreamFromModel()
{
    const PDFDocumentToolAudioBookStream stream = PDFAppToolGUIAudioBookHelper::getStream(getModel());
    // The same data as the value known to this GUI (m_storedStream keeps it alive: the address is
    // never reused by another value while it is compared)
    if (!m_itemModel || stream == m_storedStream)
    {
        return;
    }

    // Undo / Redo: the editor of the value (an empty value starts a new stream)
    m_storedStream = stream;
    if (const PDFDocumentTextFlowEditor* editor = stream.getEditor())
    {
        cancelStreamJob();
        m_hasPendingText = false;
        m_itemModel->setEditor(*editor);
    }
    else if (!isBuildingStream())
    {
        startStreamJob(false);
    }
}

void PDFAppToolGUIAudioBook::commitEditedText()
{
    if (m_commitTimer)
    {
        m_commitTimer->stop();
    }
    if (!m_hasPendingText)
    {
        return;
    }
    m_hasPendingText = false;
    storeStream(true);
}

QStringList PDFAppToolGUIAudioBook::getReadSpecial() const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? PDFDocumentToolAudioBook::getReadSpecial(model->getValues(), nullptr) : QStringList();
}

// --------------------------------------------------------- edits

void PDFAppToolGUIAudioBook::selectItems(std::set<size_t> items)
{
    if (!m_itemModel)
    {
        return;
    }

    // A pending text goes into the stream first (a step of Undo: its signals may delete this GUI)
    QPointer<PDFAppToolGUIAudioBook> guard(this);
    commitEditedText();
    if (!guard || !m_itemModel)
    {
        return;
    }
    m_itemModel->setSelection(std::move(items));

    // The view follows the selection of the model (without its own signal)
    if (m_listView)
    {
        m_isUpdatingPanel = true;

        // The rows of the selected items ascend with the items: one range of each run of rows
        // (10,000 selected rows are one range, not 10,000 ranges)
        QItemSelection selection;
        int firstRow = -1;
        int rangeFirst = -1;
        int rangeLast = -1;
        for (size_t item : m_itemModel->getSelection())
        {
            const int row = m_itemModel->getRow(item);
            if (row < 0)
            {
                continue;
            }
            firstRow = firstRow < 0 ? row : qMin(firstRow, row);
            if (rangeLast >= 0 && row == rangeLast + 1)
            {
                rangeLast = row;
                continue;
            }
            if (rangeFirst >= 0)
            {
                selection.select(m_itemModel->index(rangeFirst, 0), m_itemModel->index(rangeLast, 0));
            }
            rangeFirst = row;
            rangeLast = row;
        }
        if (rangeFirst >= 0)
        {
            selection.select(m_itemModel->index(rangeFirst, 0), m_itemModel->index(rangeLast, 0));
        }
        m_listView->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
        if (firstRow >= 0)
        {
            m_listView->selectionModel()->setCurrentIndex(m_itemModel->index(firstRow, 0), QItemSelectionModel::NoUpdate);
            m_listView->scrollTo(m_itemModel->index(firstRow, 0));
        }
        m_isUpdatingPanel = false;
    }

    // Sync List to page: the page of the first selected item
    if (m_isListToPage && !m_itemModel->getSelection().empty())
    {
        const PDFInteger pageIndex = m_itemModel->getEditor().getPageIndex(*m_itemModel->getSelection().begin());
        if (pageIndex >= 0)
        {
            m_shownPageIndex = pageIndex;
        }
    }
    updateAll();
}

void PDFAppToolGUIAudioBook::readSelected()
{
    if (!m_itemModel || m_itemModel->getSelection().empty())
    {
        return;
    }
    QPointer<PDFAppToolGUIAudioBook> guard(this);
    commitEditedText();
    if (!guard || !m_itemModel)
    {
        return;
    }
    m_itemModel->setItemsRead(m_itemModel->getSelection(), true);
    storeStream(true);
    if (guard)
    {
        selectItems(m_itemModel->getSelection());
    }
}

void PDFAppToolGUIAudioBook::skipSelected()
{
    if (!m_itemModel || m_itemModel->getSelection().empty())
    {
        return;
    }
    QPointer<PDFAppToolGUIAudioBook> guard(this);
    commitEditedText();
    if (!guard || !m_itemModel)
    {
        return;
    }
    m_itemModel->setItemsRead(m_itemModel->getSelection(), false);
    storeStream(true);
    if (guard)
    {
        selectItems(m_itemModel->getSelection());
    }
}

void PDFAppToolGUIAudioBook::moveSelectionUp()
{
    if (!m_itemModel || m_itemModel->getSelection().empty())
    {
        return;
    }
    QPointer<PDFAppToolGUIAudioBook> guard(this);
    commitEditedText();
    if (!guard || !m_itemModel)
    {
        return;
    }
    m_itemModel->moveSelection(true);
    storeStream(true);
    if (guard)
    {
        selectItems(m_itemModel->getSelection());
    }
}

void PDFAppToolGUIAudioBook::moveSelectionDown()
{
    if (!m_itemModel || m_itemModel->getSelection().empty())
    {
        return;
    }
    QPointer<PDFAppToolGUIAudioBook> guard(this);
    commitEditedText();
    if (!guard || !m_itemModel)
    {
        return;
    }
    m_itemModel->moveSelection(false);
    storeStream(true);
    if (guard)
    {
        selectItems(m_itemModel->getSelection());
    }
}

void PDFAppToolGUIAudioBook::onDeleteShortcut()
{
    skipSelected();
}

void PDFAppToolGUIAudioBook::onShiftDeleteShortcut()
{
    readSelected();
}

void PDFAppToolGUIAudioBook::onTextEdited()
{
    if (m_isUpdatingPanel || !m_itemModel || !m_textEditor || m_itemModel->getSelection().size() != 1)
    {
        return;
    }

    // The list shows the text at once, the stream gets it at a pause of the typing
    const size_t item = *m_itemModel->getSelection().begin();
    m_isUpdatingPanel = true;
    m_itemModel->setItemText(item, m_textEditor->toPlainText());
    m_itemModel->setSelection({ item });
    m_isUpdatingPanel = false;
    m_hasPendingText = true;
    if (m_commitTimer)
    {
        m_commitTimer->start();
    }
    updateHeader();
    updateCanvas();
    updateSummary();
    if (m_restoreLink)
    {
        m_restoreLink->setEnabled(m_itemModel->getEditor().isModified(item));
    }

    // The editor is inside of its signal textChanged: the status (a signal, whose slots may delete
    // the panel with the editor) is updated from the event loop
    QMetaObject::invokeMethod(this, &PDFDocumentToolGUI::updateStatusText, Qt::QueuedConnection);
}

void PDFAppToolGUIAudioBook::onReadCheckBoxToggled(bool isChecked)
{
    if (m_isUpdatingPanel)
    {
        return;
    }

    // The check box is inside of its signal: the edit runs from the event loop
    QMetaObject::invokeMethod(this, isChecked ? &PDFAppToolGUIAudioBook::readSelected : &PDFAppToolGUIAudioBook::skipSelected, Qt::QueuedConnection);
}

void PDFAppToolGUIAudioBook::onRestoreClicked()
{
    if (!m_itemModel || m_itemModel->getSelection().empty())
    {
        return;
    }
    m_hasPendingText = false;
    if (m_commitTimer)
    {
        m_commitTimer->stop();
    }
    m_itemModel->restoreTexts(m_itemModel->getSelection());
    QPointer<PDFAppToolGUIAudioBook> guard(this);
    storeStream(true);
    if (guard)
    {
        selectItems(m_itemModel->getSelection());
    }
}

bool PDFAppToolGUIAudioBook::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_textEditor && event->type() == QEvent::FocusOut)
    {
        // The text goes into the stream from the event loop: the editor is inside of its event
        QMetaObject::invokeMethod(this, &PDFAppToolGUIAudioBook::commitEditedText, Qt::QueuedConnection);
    }

    if (watched == m_listView && event->type() == QEvent::KeyPress)
    {
        // The keys of the list: the edit runs from the event loop, after the view has finished its
        // event (the edit emits signals, whose slots may delete the view)
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        const Qt::KeyboardModifiers modifiers = keyEvent->modifiers() & (Qt::ShiftModifier | Qt::AltModifier | Qt::ControlModifier);
        if (keyEvent->key() == Qt::Key_Delete && modifiers == Qt::NoModifier)
        {
            QMetaObject::invokeMethod(this, &PDFAppToolGUIAudioBook::onDeleteShortcut, Qt::QueuedConnection);
            return true;
        }
        if (keyEvent->key() == Qt::Key_Delete && modifiers == Qt::ShiftModifier)
        {
            QMetaObject::invokeMethod(this, &PDFAppToolGUIAudioBook::onShiftDeleteShortcut, Qt::QueuedConnection);
            return true;
        }
        if ((keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down) && modifiers == Qt::AltModifier)
        {
            QMetaObject::invokeMethod(this, keyEvent->key() == Qt::Key_Up ? &PDFAppToolGUIAudioBook::moveSelectionUp : &PDFAppToolGUIAudioBook::moveSelectionDown, Qt::QueuedConnection);
            return true;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

// --------------------------------------------------------- list, overlay, popovers

void PDFAppToolGUIAudioBook::onFilterChipClicked()
{
    const QPushButton* chip = qobject_cast<const QPushButton*>(sender());
    if (!chip || !m_itemModel)
    {
        return;
    }
    const std::set<size_t> selection = m_itemModel->getSelection();
    m_itemModel->setFilter(PDFAppAudioBookItemModel::Filter(chip->property("audioBookFilter").toInt()));
    selectItems(selection);
}

void PDFAppToolGUIAudioBook::onListSelectionChanged()
{
    if (m_isUpdatingPanel || !m_listView || !m_itemModel || !m_selectionTimer)
    {
        return;
    }

    // The view is inside of its event (a press, a key): the selection is taken from the event loop,
    // after the view has finished it - the update emits signals, whose slots may delete the view
    m_selectionTimer->start();
}

void PDFAppToolGUIAudioBook::applyListSelection()
{
    if (!m_listView || !m_itemModel)
    {
        return;
    }

    std::set<size_t> items;
    for (const QModelIndex& index : m_listView->selectionModel()->selectedIndexes())
    {
        items.insert(m_itemModel->getItemIndex(index.row()));
    }
    selectItems(std::move(items));
}

void PDFAppToolGUIAudioBook::onListActivated(const QModelIndex& index)
{
    Q_UNUSED(index);
}

QMenu* PDFAppToolGUIAudioBook::getContextMenu() const
{
    return m_contextMenu.data();
}

void PDFAppToolGUIAudioBook::onListContextMenuRequested(const QPoint& position)
{
    if (!m_listView || !m_itemModel || m_itemModel->getItemCount() == 0 || PDFAppToolGUIAudioBookHelper::isToolEnded(this))
    {
        return;
    }

    // An item, which is not selected, is selected first (the menu acts on the selection)
    const QModelIndex index = m_listView->indexAt(position);
    if (index.isValid())
    {
        const size_t itemIndex = m_itemModel->getItemIndex(index.row());
        if (!m_itemModel->getSelection().count(itemIndex))
        {
            // The selection updates the list, the page and the panel (their signals run code, which may close the
            // tool: no menu of an ended tool)
            QPointer<PDFAppToolGUIAudioBook> guard(this);
            selectItems({ itemIndex });
            if (!guard || !m_listView || PDFAppToolGUIAudioBookHelper::isToolEnded(this))
            {
                return;
            }
        }
    }

    if (m_contextMenu)
    {
        m_contextMenu->close();
    }

    // 2.7 "the menu = the items Select / Read / Skip / Move": each action is enabled as its button
    // of the toolbar. new + QPointer, deleted when it closes; the actions run by triggered.
    m_contextMenu = new QMenu(m_listView);
    m_contextMenu->setObjectName(QStringLiteral("audioBookListMenu"));
    m_contextMenu->setAttribute(Qt::WA_DeleteOnClose);
    QMenu* selectMenu = m_contextMenu->addMenu(tr("Select"));
    selectMenu->setObjectName(QStringLiteral("audioBookListSelectMenu"));
    selectMenu->setEnabled(m_selectMenu && m_selectMenu->getButton() && m_selectMenu->getButton()->isEnabled());
    struct Row { const char* key; const char* title; };
    const Row selectRows[] = {
        { "rectangle", QT_TR_NOOP("By rectangle") },
        { "text", QT_TR_NOOP("By contained text") },
        { "regex", QT_TR_NOOP("By regular expression") },
        { "pages", QT_TR_NOOP("By page list") },
    };
    for (const Row& row : selectRows)
    {
        selectMenu->addAction(tr(row.title))->setData(QString::fromLatin1(row.key));
    }
    m_contextMenu->addSeparator();
    const std::pair<const char*, QToolButton*> actionRows[] = {
        { "read", m_readButton.data() }, { "skip", m_skipButton.data() }, { "up", m_moveUpButton.data() }, { "down", m_moveDownButton.data() }
    };
    for (const auto& [key, button] : actionRows)
    {
        QString title = tr("Read");
        QString shortcut = tr("Shift+Delete");
        if (qstrcmp(key, "skip") == 0)
        {
            title = tr("Skip");
            shortcut = tr("Delete");
        }
        else if (qstrcmp(key, "up") == 0)
        {
            title = tr("Move Up");
            shortcut = tr("Alt+Up");
            m_contextMenu->addSeparator();
        }
        else if (qstrcmp(key, "down") == 0)
        {
            title = tr("Move Down");
            shortcut = tr("Alt+Down");
        }
        QAction* action = m_contextMenu->addAction(title + QLatin1Char('\t') + shortcut);
        action->setData(QString::fromLatin1(key));
        action->setEnabled(button && button->isEnabled());
    }
    connect(m_contextMenu.data(), &QMenu::triggered, this, &PDFAppToolGUIAudioBook::onContextMenuTriggered);
    m_contextMenu->popup(m_listView->viewport()->mapToGlobal(position));
}

void PDFAppToolGUIAudioBook::onContextMenuTriggered(QAction* action)
{
    // The action runs from the event loop, not inside the signal of the menu: QMenu reads its own
    // members after its signal triggered returns (QMenuPrivate::_q_actionTriggered, and the
    // activation by the mouse writes them), and an action emits signals, whose slots may delete the
    // list with its menu (the tool, the tab). Only the key goes on, the action dies with the menu.
    const QString key = action ? action->data().toString() : QString();
    if (!key.isEmpty())
    {
        QMetaObject::invokeMethod(this, &PDFAppToolGUIAudioBook::onContextMenuActionQueued, Qt::QueuedConnection, key);
    }
}

void PDFAppToolGUIAudioBook::onContextMenuActionQueued(const QString& key)
{
    // Each branch is the last call (its signals may delete this GUI)
    if (key == QLatin1String("rectangle"))
    {
        selectByRectangleMode();
    }
    else if (key == QLatin1String("text"))
    {
        openSelectPopover(PDFAppAudioBookSelectPopover::Kind::ContainedText);
    }
    else if (key == QLatin1String("regex"))
    {
        openSelectPopover(PDFAppAudioBookSelectPopover::Kind::RegularExpression);
    }
    else if (key == QLatin1String("pages"))
    {
        openSelectPopover(PDFAppAudioBookSelectPopover::Kind::PageList);
    }
    else if (key == QLatin1String("read"))
    {
        readSelected();
    }
    else if (key == QLatin1String("skip"))
    {
        skipSelected();
    }
    else if (key == QLatin1String("up"))
    {
        moveSelectionUp();
    }
    else if (key == QLatin1String("down"))
    {
        moveSelectionDown();
    }
}

void PDFAppToolGUIAudioBook::onOverlayItemClicked(qint64 index, bool isAdding)
{
    if (!m_itemModel)
    {
        return;
    }

    std::set<size_t> items = isAdding ? m_itemModel->getSelection() : std::set<size_t>();
    if (index >= 0)
    {
        if (isAdding && items.count(size_t(index)))
        {
            items.erase(size_t(index));
        }
        else
        {
            items.insert(size_t(index));
        }
    }

    // Sync Page to list: the list shows the item (selectItems scrolls to it); without it the list
    // keeps its place. A click on the page never changes the page (List to page is for the list).
    const bool isListToPage = m_isListToPage;
    const int scrollValue = m_listView && m_listView->verticalScrollBar() ? m_listView->verticalScrollBar()->value() : 0;
    m_isListToPage = false;

    // selectItems emits signals, whose slots may delete this GUI
    QPointer<PDFAppToolGUIAudioBook> guard(this);
    selectItems(std::move(items));
    if (!guard)
    {
        return;
    }
    m_isListToPage = isListToPage;
    if (!m_isPageToList && m_listView && m_listView->verticalScrollBar())
    {
        m_listView->verticalScrollBar()->setValue(scrollValue);
    }
}

void PDFAppToolGUIAudioBook::onOverlayRectangleSelected(QRectF pageRectangle)
{
    if (!m_itemModel)
    {
        return;
    }

    // 360:13257: the items of the shown page completely inside of the rectangle
    std::set<size_t> items;
    const PDFDocumentTextFlowEditor& editor = m_itemModel->getEditor();
    for (size_t i = 0; i < editor.getItemCount(); ++i)
    {
        const PDFDocumentTextFlowEditor::EditedItem* item = editor.getEditedItem(i);
        if (item->pageIndex == m_shownPageIndex && !item->boundingRect.isEmpty() && pageRectangle.contains(item->boundingRect))
        {
            items.insert(i);
        }
    }
    if (m_canvas)
    {
        m_canvas->setRectangleHintShown(false);
    }
    selectItems(std::move(items));
}

void PDFAppToolGUIAudioBook::onOverlayRectangleCanceled()
{
    if (m_canvas)
    {
        m_canvas->setRectangleHintShown(false);
    }
    updateStatusText();
}

bool PDFAppToolGUIAudioBook::handleEscape()
{
    // The mode ends (its signal is queued: onOverlayRectangleCanceled hides the hint), the tool stays
    PDFAppAudioBookOverlay* overlay = getOverlay();
    return overlay && overlay->cancelRectangleMode();
}

bool PDFAppToolGUIAudioBook::hasUnsavedWork() const
{
    // RESCUX H2: every change of the text stream lives only in the tool - skipped or read items, edited texts, a
    // changed order (the same changes as the question of Rebuild 356:14056)
    return m_itemModel && m_itemModel->getChanges(getReadSpecial()).hasChanges();
}

PDFAppToolCloseQuestion PDFAppToolGUIAudioBook::getCloseQuestion() const
{
    // No frame (owner 2026-11-20): the question of Sign by Hand 435:10500 for the changes of the text stream, named
    // as the question of Rebuild names them (356:14056)
    const PDFAppAudioBookItemModel::Changes changes = m_itemModel ? m_itemModel->getChanges(getReadSpecial()) : PDFAppAudioBookItemModel::Changes();
    PDFAppToolCloseQuestion question;
    if (!changes.hasChanges())
    {
        return question;
    }
    question.title = tr("Discard your changes to the text stream?");
    question.text = tr("Closing the tool removes %1. The document stays as it was.").arg(getChangesText(changes));
    question.keepText = tr("Keep Editing");
    question.discardText = tr("Discard Changes");
    return question;
}

QString PDFAppToolGUIAudioBook::getChangesText(const PDFAppAudioBookItemModel::Changes& changes) const
{
    // "3 skipped items, 1 edited text and the changed order" (356:14056)
    QStringList parts;
    const int skipped = PDFAppToolGUIAudioBookHelper::toInt(changes.skipped + changes.read);
    const int edited = PDFAppToolGUIAudioBookHelper::toInt(changes.edited);
    if (skipped > 0)
    {
        parts << (changes.read > 0 ? tr("%Ln changed items", nullptr, skipped) : (skipped == 1 ? tr("%Ln skipped item", nullptr, skipped) : tr("%Ln skipped items", nullptr, skipped)));
    }
    if (edited > 0)
    {
        parts << (edited == 1 ? tr("%Ln edited text", nullptr, edited) : tr("%Ln edited texts", nullptr, edited));
    }
    if (changes.isReordered)
    {
        parts << tr("the changed order");
    }
    if (parts.isEmpty())
    {
        return QString();
    }
    if (parts.size() == 2)
    {
        return tr("%1 and %2").arg(parts[0], parts[1]);
    }
    if (parts.size() == 3)
    {
        return tr("%1, %2 and %3").arg(parts[0], parts[1], parts[2]);
    }
    return parts.front();
}

void PDFAppToolGUIAudioBook::selectByRectangleMode()
{
    if (!m_canvas || !m_canvas->getOverlay())
    {
        return;
    }
    m_canvas->getOverlay()->setRectangleMode(true);
    m_canvas->setRectangleHintShown(true);
    updateStatusText();
}

void PDFAppToolGUIAudioBook::onSelectMenuRowClicked()
{
    const QString key = sender() ? sender()->property(PDFAppToolGUIAudioBookHelper::ROW_KEY_PROPERTY).toString() : QString();

    // The panel closes first
    QPointer<PDFAppToolGUIAudioBook> guard(this);
    if (m_selectMenu)
    {
        m_selectMenu->close();
    }
    if (!guard)
    {
        return;
    }

    if (key == QLatin1String("rectangle"))
    {
        selectByRectangleMode();
    }
    else if (key == QLatin1String("text"))
    {
        openSelectPopover(PDFAppAudioBookSelectPopover::Kind::ContainedText);
    }
    else if (key == QLatin1String("regex"))
    {
        openSelectPopover(PDFAppAudioBookSelectPopover::Kind::RegularExpression);
    }
    else if (key == QLatin1String("pages"))
    {
        openSelectPopover(PDFAppAudioBookSelectPopover::Kind::PageList);
    }
}

void PDFAppToolGUIAudioBook::openSelectPopover(PDFAppAudioBookSelectPopover::Kind kind)
{
    if (m_selectPopover)
    {
        m_selectPopover->close();
    }

    PDFAppToolHost* host = getHost();
    QWidget* anchor = m_selectMenu && m_selectMenu->getButton() ? static_cast<QWidget*>(m_selectMenu->getButton()) : (host ? host->getCanvasArea() : nullptr);
    if (!anchor)
    {
        return;
    }

    // new + QPointer; deleted later, when it closes
    m_selectPopover = new PDFAppAudioBookSelectPopover(kind, host ? host->getView() : nullptr, host ? host->getCanvasArea() : anchor);
    // A dialog of the window owned by its creator (step A): retire closes it unanswered
    PDFDeferredDeletion::setOwner(m_selectPopover.data(), this);
    connect(m_selectPopover.data(), &PDFAppAudioBookSelectPopover::textChanged, this, &PDFAppToolGUIAudioBook::onSelectPopoverTextChanged);
    connect(m_selectPopover.data(), &PDFAppAudioBookSelectPopover::selectRequested, this, &PDFAppToolGUIAudioBook::onSelectPopoverSelected);
    connect(m_selectPopover.data(), &PDFAppDocumentPanelPopup::closed, this, &PDFAppToolGUIAudioBook::onSelectPopoverClosed);
    const SelectionCheck check = checkSelection(kind, QString());
    m_selectPopover->setCheck(check.isValid, check.text);
    m_selectPopover->popup(anchor, anchor->mapToGlobal(QPoint(0, anchor->height() + 4)).y());
    if (m_selectPopover && m_selectPopover->getField())
    {
        m_selectPopover->getField()->setFocus(Qt::PopupFocusReason);
    }
}

void PDFAppToolGUIAudioBook::onSelectPopoverTextChanged(const QString& text)
{
    if (m_selectPopover)
    {
        const SelectionCheck check = checkSelection(m_selectPopover->getKind(), text);
        m_selectPopover->setCheck(check.isValid, check.text);
    }
}

void PDFAppToolGUIAudioBook::onSelectPopoverSelected(const QString& text)
{
    PDFAppAudioBookSelectPopover* popover = qobject_cast<PDFAppAudioBookSelectPopover*>(sender());
    if (!popover)
    {
        return;
    }
    const SelectionCheck check = checkSelection(popover->getKind(), text);
    if (check.isValid)
    {
        selectItems(check.items);
    }
}

void PDFAppToolGUIAudioBook::onSelectPopoverClosed()
{
    if (QObject* popover = sender())
    {
        popover->deleteLater();
    }
}

PDFAppToolGUIAudioBook::SelectionCheck PDFAppToolGUIAudioBook::checkSelection(PDFAppAudioBookSelectPopover::Kind kind, const QString& text) const
{
    using Helper = PDFAppToolGUIAudioBookHelper;

    SelectionCheck check;
    if (!m_itemModel)
    {
        return check;
    }

    const PDFDocumentTextFlowEditor& editor = m_itemModel->getEditor();
    switch (kind)
    {
        case PDFAppAudioBookSelectPopover::Kind::ContainedText:
        {
            // 341:10217: case sensitive
            if (text.isEmpty())
            {
                check.text = tr("Type the text to look for.");
                return check;
            }
            for (size_t i = 0; i < editor.getItemCount(); ++i)
            {
                if (editor.getText(i).contains(text, Qt::CaseSensitive))
                {
                    check.items.insert(i);
                }
            }
            const int count = Helper::toInt(check.items.size());
            check.isValid = true;
            check.text = count == 1 ? tr("%Ln item contains the text · upper and lower case matter", nullptr, count) : tr("%Ln items contain the text · upper and lower case matter", nullptr, count);
            return check;
        }

        case PDFAppAudioBookSelectPopover::Kind::RegularExpression:
        {
            if (text.isEmpty())
            {
                check.text = tr("Type a regular expression.");
                return check;
            }
            if (text.size() > PDFDocumentToolAudioBook::MAXIMUM_SKIP_LENGTH)
            {
                check.text = tr("The regular expression is too long (at most %1 characters).").arg(PDFDocumentToolAudioBook::MAXIMUM_SKIP_LENGTH);
                return check;
            }
            // The match limit of the tool: a catastrophic pattern ends every item quickly (plan 6)
            const QRegularExpression expression = PDFDocumentToolAudioBook::createExpression(text);
            if (!expression.isValid())
            {
                check.text = tr("Regular expression is not valid: %1.").arg(expression.errorString());
                return check;
            }
            for (size_t i = 0; i < editor.getItemCount(); ++i)
            {
                if (expression.match(editor.getText(i)).hasMatch())
                {
                    check.items.insert(i);
                }
            }
            const int count = Helper::toInt(check.items.size());
            check.isValid = true;
            check.text = count == 1 ? tr("%Ln item matches · it replaces the current selection", nullptr, count) : tr("%Ln items match · they replace the current selection", nullptr, count);
            return check;
        }

        case PDFAppAudioBookSelectPopover::Kind::PageList:
        {
            const PDFAppDocument* document = getDocument();
            if (!document || !document->getDocument())
            {
                return check;
            }
            if (text.trimmed().isEmpty())
            {
                check.text = tr("Type the pages, for example 1-12, 17, 24-.");
                return check;
            }

            // The page ranges of the tools (339:5080): "Page 1700 is not in the document (1–1,592)."
            PDFDocumentToolPageSelection selection;
            selection.pages = text;
            QString errorMessage;
            const std::vector<PDFInteger> pages = selection.getPageIndices(document->getDocument().data(), &errorMessage);
            if (pages.empty())
            {
                check.text = errorMessage;
                return check;
            }
            const std::set<PDFInteger> pageSet(pages.cbegin(), pages.cend());
            for (size_t i = 0; i < editor.getItemCount(); ++i)
            {
                if (pageSet.count(editor.getPageIndex(i)))
                {
                    check.items.insert(i);
                }
            }
            const int pageCount = Helper::toInt(pages.size());
            const int itemCount = Helper::toInt(check.items.size());
            check.isValid = true;
            check.text = tr("%1 · %2").arg(pageCount == 1 ? tr("%Ln page", nullptr, pageCount) : tr("%Ln pages", nullptr, pageCount),
                                           itemCount == 1 ? tr("%Ln item", nullptr, itemCount) : tr("%Ln items", nullptr, itemCount));
            return check;
        }
    }
    return check;
}

void PDFAppToolGUIAudioBook::onSyncMenuRowClicked()
{
    const QString key = sender() ? sender()->property(PDFAppToolGUIAudioBookHelper::ROW_KEY_PROPERTY).toString() : QString();
    if (key == QLatin1String("listToPage"))
    {
        m_isListToPage = !m_isListToPage;
    }
    else if (key == QLatin1String("pageToList"))
    {
        m_isPageToList = !m_isPageToList;
    }
    updateSyncMenu();
}

void PDFAppToolGUIAudioBook::onVoiceMenuRowClicked()
{
    const QString voice = sender() ? sender()->property(PDFAppToolGUIAudioBookHelper::ROW_KEY_PROPERTY).toString() : QString();

    QPointer<PDFAppToolGUIAudioBook> guard(this);
    if (m_voiceMenu)
    {
        m_voiceMenu->close();
    }
    if (guard && getModel())
    {
        getModel()->setValue(PDFAppToolGUIAudioBookHelper::name(PDFDocumentToolAudioBook::PARAMETER_VOICE), voice);
    }
    if (guard)
    {
        updateVoiceMenu();
    }
}

void PDFAppToolGUIAudioBook::onDisplayedPageChanged(pdf::PDFInteger pageIndex)
{
    if (pageIndex >= 0)
    {
        showPage(pageIndex);
    }
}

void PDFAppToolGUIAudioBook::showPage(PDFInteger pageIndex)
{
    m_shownPageIndex = pageIndex;
    updateCanvas();
}

// --------------------------------------------------------- Rebuild, Order, no voice

void PDFAppToolGUIAudioBook::rebuildStream()
{
    startStreamJob(true);
}

void PDFAppToolGUIAudioBook::onRebuildClicked()
{
    // Also from the event loop (a new Order with changes): an ended tool asks nothing
    if (!m_itemModel || PDFAppToolGUIAudioBookHelper::isToolEnded(this))
    {
        return;
    }

    QPointer<PDFAppToolGUIAudioBook> guard(this);
    commitEditedText();
    if (!guard || PDFAppToolGUIAudioBookHelper::isToolEnded(this))
    {
        return;
    }
    const PDFAppAudioBookItemModel::Changes changes = m_itemModel ? m_itemModel->getChanges(getReadSpecial()) : PDFAppAudioBookItemModel::Changes();
    if (!changes.hasChanges())
    {
        rebuildStream();
        return;
    }

    // 356:14056: Rebuild the text stream? (new + QPointer, the answer by finished)
    if (m_rebuildQuestion)
    {
        return;
    }

    const QString changesText = getChangesText(changes);

    PDFAppToolHost* host = getHost();
    QWidget* parent = host ? host->getCanvasArea() : nullptr;
    PDFMessageBox* box = new PDFMessageBox(PDFThemeIcons::Badge::Warning, tr("Rebuild the text stream?"), tr("The text is read from the document again."), QDialogButtonBox::NoButton, parent);
    // A dialog of the window owned by its creator (step A): retire closes it unanswered
    PDFDeferredDeletion::setOwner(box, this);
    box->setObjectName(QStringLiteral("audioBookRebuildQuestion"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setCardWidth(480);
    if (parent)
    {
        box->setScrimTarget(parent);
    }
    box->addContent(PDFAppPageWidgets::createText(tr("Your changes will be lost: %1. The same question appears when you change Order.").arg(changesText), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, box));
    QPushButton* cancelButton = box->addActionButton(tr("Cancel"), PDFMessageBox::ActionPosition::Leading, false);
    cancelButton->setObjectName(QStringLiteral("audioBookRebuildCancel"));
    connect(cancelButton, &QPushButton::clicked, box, &QDialog::reject);
    QPushButton* rebuildButton = box->addActionButton(tr("Rebuild"), PDFMessageBox::ActionPosition::Trailing, true);
    rebuildButton->setObjectName(QStringLiteral("audioBookRebuildConfirm"));
    rebuildButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("refresh-cw")));
    connect(rebuildButton, &QPushButton::clicked, box, &QDialog::accept);
    connect(box, &QDialog::finished, this, &PDFAppToolGUIAudioBook::onRebuildQuestionFinished);
    m_rebuildQuestion = box;
    box->open();
}

void PDFAppToolGUIAudioBook::onRebuildQuestionFinished(int result)
{
    m_rebuildQuestion = nullptr;
    PDFAppToolParameterModel* model = getModel();
    const QString orderName = PDFAppToolGUIAudioBookHelper::name(PDFDocumentToolAudioBook::PARAMETER_ORDER);
    const QString order = model ? model->getValue(orderName).toString() : QString();
    if (result == QDialog::Accepted)
    {
        m_lastOrder = order;
        rebuildStream();
        return;
    }

    // Cancel of a new Order: the order goes back (one step of Undo is taken back)
    if (model && order != m_lastOrder && model->canUndo())
    {
        model->undo();
    }
}

void PDFAppToolGUIAudioBook::showNoVoiceMessage()
{
    if (!m_isVoicesKnown || !m_voices.empty() || m_isNoVoiceMessageShown || m_noVoiceMessage)
    {
        return;
    }
    m_isNoVoiceMessageShown = true;

    // 341:11665; Open Speech Settings only on Windows (K4.7, R4-14 a)
    PDFAppToolHost* host = getHost();
    QWidget* parent = host ? host->getCanvasArea() : nullptr;
    PDFMessageBox* box = new PDFMessageBox(PDFThemeIcons::Badge::Warning, tr("No voice is available"), tr("An audio book needs a text-to-speech voice of the system."), QDialogButtonBox::NoButton, parent);
    // A dialog of the window owned by its creator (step A): retire closes it unanswered
    PDFDeferredDeletion::setOwner(box, this);
    box->setObjectName(QStringLiteral("audioBookNoVoice"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setCardWidth(460);
    if (parent)
    {
        box->setScrimTarget(parent);
    }
    box->addContent(PDFAppPageWidgets::createText(tr("On Windows add a voice in Settings → Time & language → Speech and open this tool again. On other systems the audio book export is not supported yet."),
                                                  PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, box));
#if defined(Q_OS_WIN)
    QPushButton* settingsButton = box->addActionButton(tr("Open Speech Settings"), PDFMessageBox::ActionPosition::Leading, false);
    settingsButton->setObjectName(QStringLiteral("audioBookOpenSpeechSettings"));
    settingsButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("settings")));
    connect(settingsButton, &QPushButton::clicked, this, &PDFAppToolGUIAudioBook::onOpenSpeechSettings);
#endif
    QPushButton* closeButton = box->addActionButton(tr("Close"), PDFMessageBox::ActionPosition::Trailing, true);
    closeButton->setObjectName(QStringLiteral("audioBookNoVoiceClose"));
    connect(closeButton, &QPushButton::clicked, box, &QDialog::reject);
    connect(box, &QDialog::finished, this, &PDFAppToolGUIAudioBook::onNoVoiceMessageFinished);
    m_noVoiceMessage = box;
    box->open();
}

void PDFAppToolGUIAudioBook::onNoVoiceMessageFinished()
{
    m_noVoiceMessage = nullptr;
}

void PDFAppToolGUIAudioBook::onOpenSpeechSettings()
{
    // The settings of the speech of Windows; the message stays
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:speech")));
}

// --------------------------------------------------------- voice and format

void PDFAppToolGUIAudioBook::onSpeedChanged(int value)
{
    if (m_isUpdatingPanel || !getModel())
    {
        return;
    }
    if (m_speedLabel)
    {
        m_speedLabel->setText(PDFAppToolGUIAudioBookHelper::getSpeedText(value));
    }
    if (m_speedSlider && !m_speedSlider->isSliderDown())
    {
        // The slider is inside of its event: the value goes into the model from the event loop
        QMetaObject::invokeMethod(this, &PDFAppToolGUIAudioBook::onSliderReleased, Qt::QueuedConnection);
    }
}

void PDFAppToolGUIAudioBook::onVolumeChanged(int value)
{
    if (m_isUpdatingPanel || !getModel())
    {
        return;
    }
    if (m_volumeLabel)
    {
        m_volumeLabel->setText(tr("Volume · %1 %").arg(value));
    }
    if (m_volumeSlider && !m_volumeSlider->isSliderDown())
    {
        QMetaObject::invokeMethod(this, &PDFAppToolGUIAudioBook::onSliderReleased, Qt::QueuedConnection);
    }
}

void PDFAppToolGUIAudioBook::onSliderReleased()
{
    // A drag is one step of Undo, written when the slider is released
    PDFAppToolParameterModel* model = getModel();
    if (!model || !m_speedSlider || !m_volumeSlider)
    {
        return;
    }
    QVariantMap values;
    values.insert(PDFAppToolGUIAudioBookHelper::name(PDFDocumentToolAudioBook::PARAMETER_RATE), double(m_speedSlider->value()) / PDFAppToolGUIAudioBookHelper::SLIDER_SCALE);
    values.insert(PDFAppToolGUIAudioBookHelper::name(PDFDocumentToolAudioBook::PARAMETER_VOLUME), double(m_volumeSlider->value()) / PDFAppToolGUIAudioBookHelper::SLIDER_SCALE);
    model->setValues(values);
}

QString PDFAppToolGUIAudioBook::getSampleText() const
{
    if (!m_itemModel)
    {
        return QString();
    }

    const PDFDocumentTextFlowEditor& editor = m_itemModel->getEditor();
    QString text;
    if (!m_itemModel->getSelection().empty())
    {
        text = editor.getText(*m_itemModel->getSelection().begin());
    }
    for (size_t i = 0; text.trimmed().isEmpty() && i < editor.getItemCount(); ++i)
    {
        if (editor.isActive(i))
        {
            text = editor.getText(i);
        }
    }
    if (text.trimmed().isEmpty())
    {
        text = tr("This is a sample of the voice of the audio book.");
    }
    return text.left(PDFAppToolGUIAudioBookHelper::SAMPLE_LENGTH);
}

void PDFAppToolGUIAudioBook::onPlaySampleClicked()
{
    // Stop Sample
    if (m_sampleEngine && m_sampleEngine->getState() == PDFAppSpeechEngine::State::Speaking)
    {
        m_sampleEngine->stop();
        return;
    }

    // R4-14 d: the voice, the speed and the volume of the tool, never the settings speech.* of Read
    // aloud; the engine of the runs (the tests set their engine by PDFAppSpeechEngine::setFactory)
    const PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return;
    }
    if (!m_sampleEngine)
    {
        QString engineName;
        if (const PDFQtSpeechSynthesizerFactory* factory = dynamic_cast<const PDFQtSpeechSynthesizerFactory*>(getContext() ? getContext()->getSpeechSynthesizerFactory().get() : nullptr))
        {
            engineName = factory->getEngine();
        }
        m_sampleEngine = PDFAppSpeechEngine::create(engineName, this);
        if (!m_sampleEngine)
        {
            return;
        }
        connect(m_sampleEngine.data(), &PDFAppSpeechEngine::stateChanged, this, &PDFAppToolGUIAudioBook::onSampleStateChanged);
    }

    m_sampleEngine->setVoice(model->getValue(PDFAppToolGUIAudioBookHelper::name(PDFDocumentToolAudioBook::PARAMETER_VOICE)).toString());
    m_sampleEngine->setRate(model->getValue(PDFAppToolGUIAudioBookHelper::name(PDFDocumentToolAudioBook::PARAMETER_RATE)).toDouble());
    m_sampleEngine->setVolume(model->getValue(PDFAppToolGUIAudioBookHelper::name(PDFDocumentToolAudioBook::PARAMETER_VOLUME)).toDouble());

    // The engine runs foreign code: the GUI may be gone after it
    QPointer<PDFAppToolGUIAudioBook> guard(this);
    m_sampleEngine->say(getSampleText());
    if (guard)
    {
        updateVoiceAndFormat();
    }
}

void PDFAppToolGUIAudioBook::onSampleStateChanged()
{
    updateVoiceAndFormat();
}

// --------------------------------------------------------- the shell

void PDFAppToolGUIAudioBook::onParametersChanged(const QString& name)
{
    using Tool = PDFDocumentToolAudioBook;

    if (name == QLatin1String(Tool::PARAMETER_EDITED_STREAM))
    {
        loadStreamFromModel();
    }
    else if (name == QLatin1String(Tool::PARAMETER_ORDER))
    {
        const QString order = getModel() ? getModel()->getValue(name).toString() : QString();
        if (order != m_lastOrder)
        {
            // 356:14056: a new order rebuilds the stream; with changes the question first
            const PDFAppAudioBookItemModel::Changes changes = m_itemModel ? m_itemModel->getChanges(getReadSpecial()) : PDFAppAudioBookItemModel::Changes();
            if (changes.hasChanges())
            {
                QTimer::singleShot(0, this, &PDFAppToolGUIAudioBook::onRebuildClicked);
            }
            else
            {
                m_lastOrder = order;
                startStreamJob(false);
            }
        }
    }
    else if (name == QLatin1String(Tool::PARAMETER_VOICE))
    {
        updateVoiceMenu();
    }

    // The slots of the status may delete this GUI: the status is the last thing
    updateAll();
}

void PDFAppToolGUIAudioBook::onRunStateChanged(bool isRunning)
{
    // 341:10706: the run hides the Selected item, the sample stops
    QPointer<PDFAppToolGUIAudioBook> guard(this);
    commitEditedText();
    if (!guard)
    {
        return;
    }
    if (m_selectedSection && m_selectedSection->parentWidget())
    {
        m_selectedSection->setVisible(!isRunning);
        if (QWidget* card = m_selectedSection->parentWidget()->findChild<QWidget*>(QStringLiteral("audioBookSelectedCard")))
        {
            card->setVisible(!isRunning);
        }
    }
    if (isRunning && m_sampleEngine)
    {
        m_sampleEngine->stop();
    }
    BaseClass::onRunStateChanged(isRunning);
}

void PDFAppToolGUIAudioBook::onDocumentChanged()
{
    // A new version of the document: a stream without changes is read again, an edited one stays
    // (its texts are the user's)
    if (m_itemModel && !m_itemModel->getChanges(getReadSpecial()).hasChanges() && !isBuildingStream())
    {
        startStreamJob(false);
    }
    updateAll();
}

void PDFAppToolGUIAudioBook::onShellStateChanged(pdf::PDFDocumentToolShell::State state)
{
    Q_UNUSED(state);
    updateSelectedItem();
    updateVoiceAndFormat();
    updateActions();
}

// --------------------------------------------------------- the views

void PDFAppToolGUIAudioBook::updateAll()
{
    updateHeader();
    updateChips();
    updateCanvas();
    updateSelectedItem();
    updateVoiceAndFormat();
    updateSummary();
    updateActions();

    // The slots of these signals may delete this GUI: they are the last thing
    QPointer<PDFAppToolGUIAudioBook> guard(this);
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
}

void PDFAppToolGUIAudioBook::updateHeader()
{
    if (!m_itemModel)
    {
        return;
    }

    const int itemCount = PDFAppToolGUIAudioBookHelper::toInt(m_itemModel->getItemCount());
    const QString readCount = PDFAppLocale::formatNumber(qint64(m_itemModel->getReadCount()));
    if (m_streamCount)
    {
        // 341:9371: "1,840 items · 1,712 will be read"
        m_streamCount->setText(m_itemModel->isBuilding() ? tr("Reading the text of the document…")
                                                         : (itemCount == 1 ? tr("%Ln item · %1 will be read", nullptr, itemCount) : tr("%Ln items · %1 will be read", nullptr, itemCount)).arg(readCount));
    }
    if (m_emptyLabel)
    {
        const bool isEmpty = m_itemModel->getItemCount() == 0;
        m_emptyLabel->setVisible(isEmpty);
        m_emptyLabel->setText(m_itemModel->isBuilding() ? tr("Reading the text of the document…") : tr("This document has no text to read."));
    }
    if (m_rebuildLink)
    {
        m_rebuildLink->setEnabled(!m_itemModel->isBuilding());
    }
}

void PDFAppToolGUIAudioBook::updateChips()
{
    if (!m_itemModel)
    {
        return;
    }
    for (const QPointer<QPushButton>& chip : m_filterChips)
    {
        if (chip)
        {
            chip->setChecked(chip->property("audioBookFilter").toInt() == int(m_itemModel->getFilter()));
        }
    }
}

void PDFAppToolGUIAudioBook::updateCanvas()
{
    if (!m_canvas || !m_itemModel)
    {
        return;
    }

    const PDFDocumentToolShell* shell = getShell();
    const bool isRefused = shell && shell->isPermissionRefused();
    m_canvas->setContentShown(!isRefused);

    // The page of the canvas: its picture, its geometry for the overlay and its items
    const PDFAppDocument* appDocument = getDocument();
    const PDFDocument* document = appDocument && appDocument->getDocument() ? appDocument->getDocument().data() : nullptr;
    const size_t pageCount = document ? document->getCatalog()->getPageCount() : 0;
    if (m_shownPageIndex < 0 || size_t(m_shownPageIndex) >= pageCount)
    {
        m_shownPageIndex = pageCount > 0 ? 0 : -1;
    }

    m_canvas->getPicture()->setPageIndex(m_shownPageIndex);
    m_canvas->setTitle(m_shownPageIndex >= 0 ? tr("Page %1 · items of the text stream").arg(PDFAppLocale::formatNumber(m_shownPageIndex + 1)) : QString());

    PDFAppAudioBookOverlay* overlay = m_canvas->getOverlay();
    const PDFPage* page = m_shownPageIndex >= 0 ? document->getCatalog()->getPage(size_t(m_shownPageIndex)) : nullptr;
    if (page)
    {
        overlay->setPageGeometry(page->getMediaBox(), page->getPageRotation());
    }

    std::vector<PDFAppAudioBookOverlay::Item> items;
    const PDFDocumentTextFlowEditor& editor = m_itemModel->getEditor();
    for (size_t i = 0; i < editor.getItemCount(); ++i)
    {
        const PDFDocumentTextFlowEditor::EditedItem* item = editor.getEditedItem(i);
        if (item->pageIndex != m_shownPageIndex || item->boundingRect.isEmpty())
        {
            continue;
        }
        PDFAppAudioBookOverlay::Item overlayItem;
        overlayItem.rect = item->boundingRect;
        overlayItem.index = i;
        overlayItem.state = m_itemModel->getSelection().count(i) ? PDFAppAudioBookOverlay::State::Selected : m_itemModel->getItemState(i);
        items.push_back(overlayItem);
    }
    overlay->setItems(std::move(items));
}

void PDFAppToolGUIAudioBook::updateSelectedItem()
{
    if (!m_itemModel)
    {
        return;
    }

    const std::set<size_t>& selection = m_itemModel->getSelection();
    const PDFDocumentTextFlowEditor& editor = m_itemModel->getEditor();
    const bool isOne = selection.size() == 1;
    const size_t item = isOne ? *selection.begin() : 0;

    m_isUpdatingPanel = true;
    if (m_selectedTag)
    {
        // "Text · page 308"; more items: "3 items"
        QString tagText;
        if (isOne)
        {
            const QString type = editor.isItemTypeText(item) ? tr("Text") : tr("Special");
            const PDFInteger pageIndex = editor.getPageIndex(item);
            tagText = pageIndex >= 0 ? tr("%1 · page %2").arg(type, PDFAppLocale::formatNumber(pageIndex + 1)) : type;
        }
        else if (!selection.empty())
        {
            const int count = PDFAppToolGUIAudioBookHelper::toInt(selection.size());
            tagText = tr("%Ln items", nullptr, count);
        }
        m_selectedTag->setText(tagText);
        m_selectedTag->setVisible(!tagText.isEmpty());
    }
    if (m_restoreLink)
    {
        bool isModified = false;
        for (size_t index : selection)
        {
            isModified = isModified || editor.isModified(index);
        }
        m_restoreLink->setEnabled(isModified);
    }
    if (m_textEditor)
    {
        const QString text = isOne ? editor.getText(item) : QString();
        if (m_textEditor->toPlainText() != text)
        {
            m_textEditor->setPlainText(text);
        }
        m_textEditor->setEnabled(isOne);
        m_textEditor->setPlaceholderText(selection.empty() ? tr("Select an item in the list or on the page.") : (isOne ? QString() : tr("More items are selected.")));
    }
    if (m_readCheckBox)
    {
        bool isRead = !selection.empty();
        for (size_t index : selection)
        {
            isRead = isRead && editor.isActive(index);
        }
        m_readCheckBox->setChecked(isRead);
        m_readCheckBox->setEnabled(!selection.empty());
    }
    m_isUpdatingPanel = false;
}

void PDFAppToolGUIAudioBook::updateVoiceAndFormat()
{
    using Helper = PDFAppToolGUIAudioBookHelper;
    using Tool = PDFDocumentToolAudioBook;

    const PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return;
    }

    m_isUpdatingPanel = true;
    const int rate = qRound(model->getValue(Helper::name(Tool::PARAMETER_RATE)).toDouble() * Helper::SLIDER_SCALE);
    const int volume = qRound(model->getValue(Helper::name(Tool::PARAMETER_VOLUME)).toDouble() * Helper::SLIDER_SCALE);
    if (m_speedSlider && !m_speedSlider->isSliderDown())
    {
        m_speedSlider->setValue(rate);
    }
    if (m_volumeSlider && !m_volumeSlider->isSliderDown())
    {
        m_volumeSlider->setValue(volume);
    }
    if (m_speedLabel && m_speedSlider)
    {
        m_speedLabel->setText(Helper::getSpeedText(m_speedSlider->value()));
    }
    if (m_volumeLabel && m_volumeSlider)
    {
        m_volumeLabel->setText(tr("Volume · %1 %").arg(m_volumeSlider->value()));
    }
    if (m_playSampleButton)
    {
        // Stop Sample while the sample plays (2.7)
        const bool isPlaying = m_sampleEngine && m_sampleEngine->getState() == PDFAppSpeechEngine::State::Speaking;
        m_playSampleButton->setText(isPlaying ? tr("Stop Sample") : tr("Play Sample"));
        m_playSampleButton->setIcon(PDFThemeIcons::getIcon(isPlaying ? QStringLiteral("stop") : QStringLiteral("play")));
        m_playSampleButton->setEnabled(!m_voices.empty());
    }
    if (m_formatCombo)
    {
        m_formatCombo->setEnabled(!m_voices.empty());
    }
    m_isUpdatingPanel = false;
}

qint64 PDFAppToolGUIAudioBook::getEstimatedDuration() const
{
    const PDFAppToolParameterModel* model = getModel();
    const double rate = model ? model->getValue(PDFAppToolGUIAudioBookHelper::name(PDFDocumentToolAudioBook::PARAMETER_RATE)).toDouble() : 0.0;
    return m_itemModel ? PDFDocumentToolAudioBook::estimateDuration(m_itemModel->getReadCharacterCount(), rate) : 0;
}

void PDFAppToolGUIAudioBook::updateSummary()
{
    using Helper = PDFAppToolGUIAudioBookHelper;

    if (!m_summaryRows || !m_itemModel)
    {
        return;
    }

    // 341:9650: Items read, Estimated length, Estimated size (WAV of the voices, 16 bit mono)
    const qint64 duration = getEstimatedDuration();
    const qint64 size = (duration / 1000) * PDFDocumentToolAudioBook::ESTIMATED_BYTES_PER_SECOND + 44;
    const QString none = QStringLiteral("–");
    const bool hasItems = m_itemModel->getItemCount() > 0;
    m_summaryRows->clear();
    m_summaryRows->addRow(tr("Items read"), hasItems ? tr("%1 of %2").arg(PDFAppLocale::formatNumber(qint64(m_itemModel->getReadCount())), PDFAppLocale::formatNumber(qint64(m_itemModel->getItemCount()))) : none);
    m_summaryRows->addRow(tr("Estimated length"), hasItems ? Helper::getAboutText(PDFDocumentToolAudioBook::formatLength(duration)) : none);
    m_summaryRows->addRow(tr("Estimated size"), hasItems ? Helper::getAboutText(PDFDocumentToolFormat::formatSize(size)) : none);
}

void PDFAppToolGUIAudioBook::updateActions()
{
    // The actions need a selection; nothing below the result dialog (341:11159), for a refused
    // document and without a voice (341:11665: every action of the toolbar is disabled)
    const PDFDocumentToolShell* shell = getShell();
    const bool isEditing = shell && !shell->isPermissionRefused() && shell->getState() != PDFDocumentToolShell::State::Deciding && !m_voices.empty();
    for (QSlider* slider : { m_speedSlider.data(), m_volumeSlider.data() })
    {
        if (slider)
        {
            slider->setEnabled(isEditing);
        }
    }
    for (QWidget* widget : { static_cast<QWidget*>(m_formatCombo.data()), static_cast<QWidget*>(m_playSampleButton.data()), static_cast<QWidget*>(m_readCheckBox.data()),
                             static_cast<QWidget*>(m_textEditor.data()), static_cast<QWidget*>(m_restoreLink.data()) })
    {
        // Their own updates enable them by their state; below the dialog they are disabled
        if (widget && !isEditing)
        {
            widget->setEnabled(false);
        }
    }
    const bool hasSelection = m_itemModel && !m_itemModel->getSelection().empty();
    const bool hasItems = m_itemModel && m_itemModel->getItemCount() > 0;
    for (QToolButton* button : { m_readButton.data(), m_skipButton.data(), m_moveUpButton.data(), m_moveDownButton.data() })
    {
        if (button)
        {
            button->setEnabled(isEditing && hasSelection);
        }
    }
    for (PDFAppToolPanelPopup* menu : { m_selectMenu.data(), m_syncMenu.data() })
    {
        if (menu && menu->getButton())
        {
            menu->getButton()->setEnabled(isEditing && hasItems);
        }
    }
    if (m_orderMenu && m_orderMenu->getButton())
    {
        m_orderMenu->getButton()->setEnabled(isEditing && !m_voices.empty());
    }
    if (m_voiceMenu && m_voiceMenu->getButton())
    {
        m_voiceMenu->getButton()->setEnabled(isEditing && !m_voices.empty());
    }
}

QString PDFAppToolGUIAudioBook::getVoiceTitle() const
{
    const PDFAppToolParameterModel* model = getModel();
    const QString voice = model ? model->getValue(PDFAppToolGUIAudioBookHelper::name(PDFDocumentToolAudioBook::PARAMETER_VOICE)).toString() : QString();
    if (m_voices.empty())
    {
        return tr("none");
    }

    // 341:9229 "Voice: Zira": the short name (the menu has the whole names)
    return PDFAppToolGUIAudioBookHelper::getShortVoiceName(voice.isEmpty() ? m_voices.front().name : voice);
}

void PDFAppToolGUIAudioBook::updateVoiceMenu()
{
    using Helper = PDFAppToolGUIAudioBookHelper;

    if (!m_voiceMenu)
    {
        return;
    }
    if (m_voiceMenu->getButton())
    {
        m_voiceMenu->getButton()->setText(tr("Voice: %1").arg(getVoiceTitle()));
    }

    // 341:9657: the voices grouped by their language; the rows are made once
    const QString voice = getModel() ? getModel()->getValue(Helper::name(PDFDocumentToolAudioBook::PARAMETER_VOICE)).toString() : QString();
    if (m_voiceMenu->getRows().empty() && !m_voices.empty())
    {
        std::vector<PDFSpeechVoice> voices = m_voices;
        std::stable_sort(voices.begin(), voices.end(), &Helper::isLanguageBefore);
        QString lastCaption;
        for (const PDFSpeechVoice& item : voices)
        {
            const QString caption = Helper::getLanguageCaption(item.languageCode);
            if (caption != lastCaption)
            {
                m_voiceMenu->addGroupCaption(caption);
                lastCaption = caption;
            }
            PDFTileRow* row = m_voiceMenu->addRow(item.name, Helper::getVoiceDescription(item), QStringLiteral("mic"), PDFTileRow::Kind::MenuItem);
            row->setObjectName(QStringLiteral("audioBookVoice-") + item.name);
            row->setProperty(Helper::ROW_KEY_PROPERTY, item.name);
            row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
            connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUIAudioBook::onVoiceMenuRowClicked);
        }
    }
    for (const QPointer<PDFTileRow>& row : m_voiceMenu->getRows())
    {
        if (row)
        {
            const QSignalBlocker blocker(row);
            row->setChecked(row->property(Helper::ROW_KEY_PROPERTY).toString() == voice);
        }
    }
}

void PDFAppToolGUIAudioBook::updateSyncMenu()
{
    if (!m_syncMenu)
    {
        return;
    }
    for (const QPointer<PDFTileRow>& row : m_syncMenu->getRows())
    {
        if (row)
        {
            const QString key = row->property(PDFAppToolGUIAudioBookHelper::ROW_KEY_PROPERTY).toString();
            const QSignalBlocker blocker(row);
            row->setChecked(key == QLatin1String("listToPage") ? m_isListToPage : m_isPageToList);
        }
    }
}

// --------------------------------------------------------- texts

bool PDFAppToolGUIAudioBook::isConfirmEnabled() const
{
    // A voice, a stream with a read item (341:11665 disables Create Audio Book)
    return BaseClass::isConfirmEnabled() && !m_voices.empty() && m_itemModel && !m_itemModel->isBuilding() && !isBuildingStream() && m_itemModel->getReadCount() > 0;
}

QString PDFAppToolGUIAudioBook::getStatusText() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const PDFAppDocument* document = getDocument();
    const QString title = tool ? tool->getTitle() : QString();
    if (!document || !document->getDocument() || !m_itemModel)
    {
        return title;
    }

    const PDFDocumentToolShell* shell = getShell();
    if (shell && shell->isPermissionRefused())
    {
        return tr("%1 · the document does not allow copying its content").arg(title);
    }
    if (m_isVoicesKnown && m_voices.empty())
    {
        // 341:11665
        return tr("%1 · no text-to-speech voice is available").arg(title);
    }
    if (m_canvas && m_canvas->getOverlay() && m_canvas->getOverlay()->isRectangleMode())
    {
        // 360:13257
        return tr("%1 · select by rectangle").arg(title);
    }
    if (m_selectPopover)
    {
        // 341:10217
        return tr("%1 · choose the items to select").arg(title);
    }
    if (m_itemModel->isBuilding())
    {
        return tr("%1 · %2 · reading the text").arg(title, document->getTitle());
    }

    // 341:9264: "Create Audio Book · Project_Report.pdf · 1,712 of 1,840 items will be read · about 4 h 20 min"
    const int itemCount = PDFAppToolGUIAudioBookHelper::toInt(m_itemModel->getItemCount());
    return tr("%1 · %2 · %3 of %Ln items will be read · %4", nullptr, itemCount)
        .arg(title, document->getTitle(), PDFAppLocale::formatNumber(qint64(m_itemModel->getReadCount())), PDFAppToolGUIAudioBookHelper::getAboutText(PDFDocumentToolAudioBook::formatLength(getEstimatedDuration())));
}

QString PDFAppToolGUIAudioBook::getRunningStatusText(const QString& stepText) const
{
    // 341:10706: "Creating audio · item 212 of 1,712 · Stop ends the run, nothing is kept"
    return tr("%1 · Stop ends the run, nothing is kept").arg(getRunningCardTitle(0, stepText));
}

QString PDFAppToolGUIAudioBook::getRunningCardTitle(int percentage, const QString& stepText) const
{
    Q_UNUSED(percentage);

    // The step of the run is "Creating audio · 212 of 1,712"; the card says "item 212 of 1,712"
    const QString creating = PDFDocumentToolAudioBook::tr("Creating audio");
    if (stepText.startsWith(creating + QStringLiteral(" · ")))
    {
        return tr("%1 · item %2").arg(creating, stepText.mid(creating.size() + 3));
    }
    return stepText.isEmpty() ? tr("Reading pages…") : stepText;
}

QString PDFAppToolGUIAudioBook::getRunningCardStepText(const QString& stepText) const
{
    Q_UNUSED(stepText);
    return QString();
}

QString PDFAppToolGUIAudioBook::getRunningTimeText(int seconds) const
{
    // 341:10706: "Page 204 · about 3 min left · Stop ends the run"
    QString text;
    if (seconds < 0)
    {
        text = tr("Stop ends the run");
    }
    else if (seconds < 60)
    {
        text = tr("about %1 s left · Stop ends the run").arg(qMax(1, seconds));
    }
    else
    {
        text = tr("about %1 min left · Stop ends the run").arg((seconds + 59) / 60);
    }

    const PDFInteger pageIndex = getSpokenPageIndex();
    return pageIndex >= 0 ? tr("Page %1 · %2").arg(PDFAppLocale::formatNumber(qint64(pageIndex + 1)), text) : text;
}

PDFInteger PDFAppToolGUIAudioBook::getSpokenPageIndex() const
{
    // The run reads the items of the stream of the panel, which have a text, in their order
    // (PDFDocumentToolAudioBook::execute): the item of the number of the step is spoken
    const PDFDocumentToolShell* shell = getShell();
    const PDFDocumentToolRunner* runner = shell ? shell->getRunner() : nullptr;
    const qint64 number = runner ? PDFAppToolGUIAudioBookHelper::getSpokenItemNumber(runner->getStepText()) : 0;
    if (number <= 0 || !m_itemModel)
    {
        return -1;
    }

    const PDFDocumentTextFlowEditor& editor = m_itemModel->getEditor();
    qint64 readNumber = 0;
    PDFInteger lastPageIndex = -1;
    for (size_t i = 0; i < editor.getItemCount(); ++i)
    {
        if (editor.isActive(i) && !editor.getText(i).trimmed().isEmpty())
        {
            lastPageIndex = editor.getPageIndex(i);
            if (++readNumber == number)
            {
                break;
            }
        }
    }
    return lastPageIndex;
}

QString PDFAppToolGUIAudioBook::getBusyText() const
{
    // An export: the document stays free (as Extract Text)
    return QString();
}

QString PDFAppToolGUIAudioBook::getPermissionTitle() const
{
    return tr("This document does not allow copying");
}

QString PDFAppToolGUIAudioBook::getPermissionSubtitle() const
{
    return tr("Its author restricted the extraction of text and images.");
}

void PDFAppToolGUIAudioBook::fillResultInput(PDFAppToolResultInput* input) const
{
    // 341:11159
    input->subtitle = tr("Nothing has been written yet – choose where to save the audio book.");
    input->statusText = tr("%1 finished · the audio book is waiting for your decision").arg(getTool() ? getTool()->getTitle() : QString());
    input->fileDescription = tr("The audio book is written to a new WAV file. The document is not changed.");
    input->fileConfirmText = tr("Save Audio Book");
}

}   // namespace pdf

#include "moc_pdfapptoolgui_audiobook.cpp"
