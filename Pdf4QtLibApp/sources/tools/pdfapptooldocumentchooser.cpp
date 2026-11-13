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

#include "pdfapptooldocumentchooser.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentregistry.h"
#include "pdfappfiledrop.h"
#include "pdfapphomewidgets.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfapppage.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolinputlist.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfaccenthue.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace pdf
{

/// Texts and places of the row
class PDFAppToolDocumentChooserHelper
{
public:
    PDFAppToolDocumentChooserHelper() = delete;

    static QString getActionText()
    {
        return PDFAppToolDocumentRow::tr("Use this document");
    }

    static QFont getActionFont()
    {
        return PDFThemeFonts::getFont(QFont::Medium, 12);
    }

    static QFont getDetailsFont()
    {
        return PDFThemeFonts::getFont(QFont::Normal, 11);
    }
};

// ---------------------------------------------------------- PDFAppToolDocumentRow

PDFAppToolDocumentRow::PDFAppToolDocumentRow(const QString& documentId, const QString& name, const QString& folder, const QString& numbers, const QString& tagText, QWidget* parent) :
    BaseClass(parent),
    m_documentId(documentId),
    m_name(name),
    m_folder(folder),
    m_numbers(numbers)
{
    setObjectName(QStringLiteral("toolDocument-") + documentId);
    setText(name);
    setFixedHeight(HEIGHT);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleDescription(tagText.isEmpty() ? getDetails() : QStringLiteral("%1, %2").arg(getDetails(), tagText));

    if (!tagText.isEmpty())
    {
        // A text of the row: the row takes the mouse over it
        m_tag = new PDFTag(tagText, this);
        m_tag->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_tag->setFocusPolicy(Qt::NoFocus);
        updateChildGeometry();
    }
}

PDFAppToolDocumentRow::~PDFAppToolDocumentRow() = default;

QString PDFAppToolDocumentRow::getDetails() const
{
    QStringList parts;
    if (!m_folder.isEmpty())
    {
        parts << m_folder;
    }
    if (!m_numbers.isEmpty())
    {
        parts << m_numbers;
    }
    return parts.join(PDFComponentPainter::getDetailSeparator());
}

PDFComponentPainter::TextWithDetail PDFAppToolDocumentRow::getShownDetailsLine() const
{
    // The folder is elided by its folders, the numbers stay whole - the layout of the
    // drawing (PDFComponentPainter::drawElidedTextWithDetail)
    return PDFComponentPainter::elidedTextWithDetail(m_folder, m_numbers, PDFAppToolDocumentChooserHelper::getDetailsFont(), getTextWidth(), PDFComponentPainter::TextKind::Path);
}

QString PDFAppToolDocumentRow::getShownDetails() const
{
    const PDFComponentPainter::TextWithDetail line = getShownDetailsLine();
    return line.text + line.detail;
}

void PDFAppToolDocumentRow::setCheckBoxVisible(bool isVisible)
{
    m_isCheckBoxVisible = isVisible;
    setCheckable(isVisible);
    updateChildGeometry();
    update();
}

QRect PDFAppToolDocumentRow::getCheckBoxRect() const
{
    if (!m_isCheckBoxVisible)
    {
        return QRect();
    }

    const QRect logical(PADDING, (height() - CHECK_BOX) / 2, CHECK_BOX, CHECK_BOX);
    return QStyle::visualRect(layoutDirection(), rect(), logical);
}

void PDFAppToolDocumentRow::setActionText(const QString& text)
{
    m_actionText = text;
    m_hasActionText = true;
    updateChildGeometry();
    update();
}

QString PDFAppToolDocumentRow::getActionText() const
{
    return m_hasActionText ? m_actionText : PDFAppToolDocumentChooserHelper::getActionText();
}

void PDFAppToolDocumentRow::setTrailing(Trailing trailing)
{
    m_trailing = trailing;
    updateChildGeometry();
    update();
}

QRect PDFAppToolDocumentRow::getTrailingRect() const
{
    if (m_trailing == Trailing::None)
    {
        return QRect();
    }

    const QRect logical(width() - PADDING - CHEVRON, (height() - CHEVRON) / 2, CHEVRON, CHEVRON);
    return QStyle::visualRect(layoutDirection(), rect(), logical);
}

void PDFAppToolDocumentRow::setRemovable(bool isRemovable)
{
    if (isRemovable && !m_removeButton)
    {
        // remove 531:3566: the icon x 16 (a button of 24 around it)
        m_removeButton = new PDFIconButton(QStringLiteral("x"), PDFIconButton::Size::Small, this);
        m_removeButton->setObjectName(QStringLiteral("removeInput"));
        m_removeButton->setAccessibleName(tr("Remove %1").arg(m_name));
        m_removeButton->setToolTip(tr("Remove"));
        connect(m_removeButton.data(), &PDFIconButton::clicked, this, &PDFAppToolDocumentRow::removeClicked);
    }
    else if (!isRemovable && m_removeButton)
    {
        delete m_removeButton.data();
    }
    updateChildGeometry();
    update();
}

int PDFAppToolDocumentRow::getTrailingEdge(int part) const
{
    int right = width() - PADDING;
    if (m_trailing != Trailing::None)
    {
        right -= CHEVRON + GAP;
    }
    if (part >= 1 && m_removeButton)
    {
        right -= REMOVE + GAP;
    }
    if (part >= 2)
    {
        const int actionWidth = getActionWidth();
        if (actionWidth > 0)
        {
            right -= actionWidth + GAP;
        }
    }
    return right;
}

int PDFAppToolDocumentRow::getTextLeft() const
{
    return PADDING + (m_isCheckBoxVisible ? CHECK_BOX + GAP : 0) + ICON + GAP;
}

int PDFAppToolDocumentRow::getTextWidth() const
{
    // The texts take what the action and the Tag leave
    int textRight = getTrailingEdge(2);
    if (m_tag)
    {
        textRight -= m_tag->sizeHint().width() + GAP;
    }
    return qMax(0, textRight - getTextLeft());
}

int PDFAppToolDocumentRow::getActionWidth() const
{
    const QString text = getActionText();
    return text.isEmpty() ? 0 : QFontMetrics(PDFAppToolDocumentChooserHelper::getActionFont()).horizontalAdvance(text) + 1;
}

QRect PDFAppToolDocumentRow::getActionRect() const
{
    const int actionWidth = getActionWidth();
    if (actionWidth <= 0)
    {
        return QRect();
    }

    const QRect logical(getTrailingEdge(1) - actionWidth, 0, actionWidth, height());
    return QStyle::visualRect(layoutDirection(), rect(), logical);
}

void PDFAppToolDocumentRow::updateChildGeometry()
{
    if (m_tag)
    {
        const QSize size = m_tag->sizeHint();
        const int right = getTrailingEdge(2);
        const QRect logical(right - size.width(), (height() - size.height()) / 2, size.width(), size.height());
        m_tag->setGeometry(QStyle::visualRect(layoutDirection(), rect(), logical));
    }

    if (m_removeButton)
    {
        // The button of 24 is centered on the place of the icon 16
        const QSize size = m_removeButton->sizeHint();
        const int center = getTrailingEdge(0) - REMOVE / 2;
        const QRect logical(center - size.width() / 2, (height() - size.height()) / 2, size.width(), size.height());
        m_removeButton->setGeometry(QStyle::visualRect(layoutDirection(), rect(), logical));
    }
}

void PDFAppToolDocumentRow::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateChildGeometry();
}

void PDFAppToolDocumentRow::keyPressEvent(QKeyEvent* event)
{
    // Alt+Up / Alt+Down move a checked row of the mode Multiple (531:3208, plan 4.3)
    const bool isMove = m_isCheckBoxVisible && isChecked() && event->modifiers() == Qt::AltModifier && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down);
    if (isMove)
    {
        event->accept();
        const int delta = event->key() == Qt::Key_Up ? -1 : 1;
        Q_EMIT moveRequested(delta);
        return;
    }
    BaseClass::keyPressEvent(event);
}

void PDFAppToolDocumentRow::mousePressEvent(QMouseEvent* event)
{
    // A press on the handle drags the row, it does not toggle its check box
    if (m_trailing == Trailing::Handle && event->button() == Qt::LeftButton && getTrailingRect().adjusted(-4, -8, 4, 8).contains(event->position().toPoint()))
    {
        m_isHandleDragged = true;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    BaseClass::mousePressEvent(event);
}

void PDFAppToolDocumentRow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isHandleDragged)
    {
        event->accept();
        return;
    }
    BaseClass::mouseMoveEvent(event);
}

void PDFAppToolDocumentRow::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_isHandleDragged)
    {
        m_isHandleDragged = false;
        unsetCursor();
        event->accept();
        const QPoint globalPosition = event->globalPosition().toPoint();
        Q_EMIT handleDropped(globalPosition);
        return;
    }
    BaseClass::mouseReleaseEvent(event);
}

QSize PDFAppToolDocumentRow::getContentSizeHint() const
{
    const int actionWidth = getActionWidth();
    return QSize(getTextLeft() + GAP + (actionWidth > 0 ? actionWidth + GAP : 0) + (m_removeButton ? REMOVE + GAP : 0) + (m_trailing != Trailing::None ? CHEVRON : 0) + PADDING, HEIGHT);
}

void PDFAppToolDocumentRow::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    using Color = PDFThemeColors::Color;

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // A checked row of the mode Multiple has bg/hover (531:3268)
    const PDFStylePainter::Radii radii(RADIUS);
    if (state.isEnabled() && state.isPressed())
    {
        PDFStylePainter::fill(painter, QRectF(rect), radii, Color::BgPressed);
    }
    else if (state.isEnabled() && (state.isHovered() || (m_isCheckBoxVisible && isChecked())))
    {
        PDFStylePainter::fill(painter, QRectF(rect), radii, Color::BgHover);
    }
    if (state.hasFocusRing())
    {
        PDFComponentPainter::paintFocusRingInside(painter, QRectF(rect), radii);
    }

    const Qt::LayoutDirection direction = layoutDirection();
    if (m_isCheckBoxVisible)
    {
        PDFStylePainter::IndicatorState indicator;
        indicator.enabled = state.isEnabled();
        indicator.hover = state.isEnabled() && state.isHovered();
        indicator.pressed = state.isEnabled() && state.isPressed();
        indicator.on = isChecked();
        PDFStylePainter::indicator(painter, QRectF(getCheckBoxRect().translated(rect.topLeft())), PDFStylePainter::IndicatorKind::CheckBox, indicator);
    }

    const int iconLeft = PADDING + (m_isCheckBoxVisible ? CHECK_BOX + GAP : 0);
    const QRect iconRect = QStyle::visualRect(direction, rect, QRect(rect.left() + iconLeft, rect.top() + (rect.height() - ICON) / 2, ICON, ICON));
    PDFStylePainter::icon(painter, QRectF(iconRect), QStringLiteral("file-text"), state.isEnabled() ? Color::IconPdf : Color::IconDisabled);

    switch (m_trailing)
    {
        case Trailing::Chevron:
            PDFStylePainter::icon(painter, QRectF(getTrailingRect().translated(rect.topLeft())), PDFAppPage::getDirectionalIconName(QStringLiteral("chevron-right"), direction),
                                  state.isEnabled() ? Color::IconMuted : Color::IconDisabled);
            break;
        case Trailing::Handle:
            PDFStylePainter::icon(painter, QRectF(getTrailingRect().translated(rect.topLeft())), QStringLiteral("more-vertical"), state.isEnabled() ? Color::IconMuted : Color::IconDisabled);
            break;
        case Trailing::None:
            break;
    }

    const QString actionText = getActionText();
    if (!actionText.isEmpty())
    {
        // An own action text (the order of the mode Multiple) is text/accent
        const Color actionColor = !state.isEnabled() ? Color::TextDisabled : ((state.isHovered() || m_hasActionText) ? Color::TextAccent : Color::TextTertiary);
        PDFComponentPainter::drawElidedText(painter, state, QRectF(getActionRect().translated(rect.topLeft())), actionText, PDFAppToolDocumentChooserHelper::getActionFont(), actionColor);
    }

    const int textLeft = getTextLeft();
    const int textWidth = getTextWidth();
    const QRect nameRect(rect.left() + textLeft, rect.top() + 9, textWidth, 16);
    const QRect detailsRect(rect.left() + textLeft, rect.top() + 27, textWidth, 13);
    PDFComponentPainter::drawElidedText(painter, state, QRectF(QStyle::visualRect(direction, rect, nameRect)), m_name, PDFThemeFonts::getFont(QFont::DemiBold, 13),
                                        state.isEnabled() ? Color::TextPrimary : Color::TextDisabled, PDFComponentPainter::TextKind::FileName);
    // The folder is data in its own direction, the numbers are a detail of the interface
    // in the direction of the row (review C18)
    PDFComponentPainter::drawElidedTextWithDetail(painter, state, QRectF(QStyle::visualRect(direction, rect, detailsRect)), m_folder, m_numbers, PDFAppToolDocumentChooserHelper::getDetailsFont(),
                                                  state.isEnabled() ? Color::TextMuted : Color::TextDisabled, PDFComponentPainter::TextKind::Path);
}

// ------------------------------------------------------ PDFAppToolDocumentChooser

PDFAppToolDocumentChooser::PDFAppToolDocumentChooser(PDFAppContext* context, const QString& toolId, QWidget* parent) :
    BaseClass(parent),
    m_context(context),
    m_toolId(toolId)
{
    setObjectName(QStringLiteral("toolDocumentChooser"));

    // document-list 277:2040: bg/surface, radius 12, padding 8
    QVBoxLayout* outer = PDFAppPageWidgets::createColumn(this, 0);
    PDFAppCard* card = PDFAppPageWidgets::createCard(8, 0, this);
    card->setObjectName(QStringLiteral("toolDocumentList"));
    outer->addWidget(card);

    // group-label 277:2041: paddings 6 / 0 / 6 / 10, Semi Bold 11 text/tertiary
    m_groupLabel = PDFAppPageWidgets::createLabel(tr("OPEN DOCUMENTS"), QFont::DemiBold, 11, PDFThemeColors::TextTertiary, card);
    m_groupLabel->setObjectName(QStringLiteral("toolDocumentGroup"));
    m_groupLabel->setContentsMargins(10, 6, 0, 6);
    card->getLayout()->addWidget(m_groupLabel);

    QVBoxLayout* rowsLayout = new QVBoxLayout();
    rowsLayout->setContentsMargins(0, 0, 0, 0);
    rowsLayout->setSpacing(0);
    m_rowsLayout = rowsLayout;
    card->getLayout()->addLayout(rowsLayout);

    // actions 277:2083: paddings 10 / 0 / 6 / 10, gap 12
    QHBoxLayout* actions = new QHBoxLayout();
    actions->setContentsMargins(10, 10, 0, 6);
    actions->setSpacing(12);
    m_openButton = PDFAppPageWidgets::createButton(tr("Open a file…"), QStringLiteral("folder-open"), false, card);
    m_openButton->setObjectName(QStringLiteral("toolOpenFile"));
    connect(m_openButton.data(), &QPushButton::clicked, this, &PDFAppToolDocumentChooser::openFileRequested);
    actions->addWidget(m_openButton, 0, Qt::AlignVCenter);

    m_dropZone = new PDFAppDropZone(PDFAppDropZone::Variant::Compact, card);
    m_dropZone->setObjectName(QStringLiteral("toolDropZone"));
    if (PDFLabel* dropText = m_dropZone->findChild<PDFLabel*>(QStringLiteral("dropText")))
    {
        dropText->setText(tr("or drag and drop a PDF here"));
        m_dropZone->setAccessibleName(dropText->text());
    }
    actions->addWidget(m_dropZone, 1);
    card->getLayout()->addLayout(actions);

    if (PDFAppDocumentRegistry* registry = context ? context->getDocumentRegistry() : nullptr)
    {
        connect(registry, &PDFAppDocumentRegistry::documentAdded, this, &PDFAppToolDocumentChooser::onDocumentChanged);
        connect(registry, &PDFAppDocumentRegistry::documentChanged, this, &PDFAppToolDocumentChooser::onDocumentChanged);
        connect(registry, &PDFAppDocumentRegistry::documentRemoved, this, &PDFAppToolDocumentChooser::onDocumentRemoved);
    }
    refresh();
}

PDFAppToolDocumentChooser::~PDFAppToolDocumentChooser() = default;

std::vector<PDFAppToolDocumentRow*> PDFAppToolDocumentChooser::getRows() const
{
    std::vector<PDFAppToolDocumentRow*> rows;
    for (const QPointer<PDFAppToolDocumentRow>& row : m_rows)
    {
        if (row)
        {
            rows.push_back(row.data());
        }
    }
    return rows;
}

QString PDFAppToolDocumentChooser::createNumbers(const PDFAppDocument* document)
{
    QStringList parts;
    const PDFInteger pageCount = document->getPageCount();
    if (pageCount > 0)
    {
        const int count = int(qMin<PDFInteger>(pageCount, std::numeric_limits<int>::max()));
        parts << (count == 1 ? tr("%Ln page", nullptr, count) : tr("%Ln pages", nullptr, count));
    }

    const qsizetype size = document->getSnapshot().originalData.size();
    if (size > 0)
    {
        parts << PDFDocumentToolFormat::formatSize(size);
    }
    return parts.join(PDFComponentPainter::getDetailSeparator());
}

QString PDFAppToolDocumentChooser::getFolder(const QString& filePath)
{
    return filePath.isEmpty() ? QString() : QDir::toNativeSeparators(QFileInfo(filePath).absolutePath());
}

void PDFAppToolDocumentChooser::refresh()
{
    const PDFAppDocumentRegistry* registry = m_context ? m_context->getDocumentRegistry() : nullptr;
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(m_toolId);
    if (!m_rowsLayout)
    {
        return;
    }

    // The focus stays on the document of the row, the rows are made again
    QString focusedId;
    for (const QPointer<PDFAppToolDocumentRow>& row : m_rows)
    {
        if (row && row->hasFocus())
        {
            focusedId = row->getDocumentId();
        }
        if (row)
        {
            row->hide();
            row->deleteLater();
        }
    }
    m_rows.clear();

    // F13: only the documents of this window; a file open in another window activates
    // its tab, when it is opened from this page (PDFAppMainWindow::openDocument)
    const PDFAppMainWindow* window = qobject_cast<const PDFAppMainWindow*>(this->window());
    if (registry)
    {
        for (PDFAppDocument* document : registry->getDocuments())
        {
            if (!document->isReady() || (window && !window->findDocumentTab(document->getId())))
            {
                continue;
            }

            const bool isUnavailable = document->isLocked() && tool && tool->getLockedAccess() == PDFDocumentToolLockedAccess::Unavailable;
            QString tagText;
            if (isUnavailable)
            {
                tagText = tr("Locked");
            }
            else if (document->isModified())
            {
                tagText = tr("Unsaved changes · working copy is used");
            }

            PDFAppToolDocumentRow* row = new PDFAppToolDocumentRow(document->getId(), document->getTitle(), getFolder(document->getFilePath()), createNumbers(document), tagText, this);
            row->setEnabled(!isUnavailable);
            connect(row, &PDFAppToolDocumentRow::clicked, this, &PDFAppToolDocumentChooser::onRowClicked);
            m_rowsLayout->addWidget(row);
            m_rows.push_back(row);
            if (row->getDocumentId() == focusedId)
            {
                row->setFocus();
            }
        }
    }

    if (m_groupLabel)
    {
        m_groupLabel->setVisible(!m_rows.empty());
    }
}

void PDFAppToolDocumentChooser::onRowClicked()
{
    const PDFAppToolDocumentRow* row = qobject_cast<const PDFAppToolDocumentRow*>(sender());
    if (row && row->isEnabled())
    {
        const QString documentId = row->getDocumentId();
        Q_EMIT documentChosen(documentId);
    }
}

void PDFAppToolDocumentChooser::onDocumentRemoved(const QString& id)
{
    Q_UNUSED(id);
    refresh();
}

void PDFAppToolDocumentChooser::onDocumentChanged(PDFAppDocument* document)
{
    Q_UNUSED(document);
    refresh();
}

void PDFAppToolDocumentChooser::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);
    refresh();
}

/// Data of a row of the chooser of more inputs
class PDFAppToolInputChooserRow
{
public:
    QString rowId;
    QString name;
    QString filePath;
    QString numbers;
    QString tagText;
    int order = -1;         ///< Place of the input in the result (-1: not checked)
    bool isChecked = false;

    /// Order of the rows of a group: the checked rows by their place in the result first,
    /// the others keep their order (a stable sort)
    static bool isBefore(const PDFAppToolInputChooserRow& left, const PDFAppToolInputChooserRow& right)
    {
        const int leftKey = left.isChecked ? left.order : std::numeric_limits<int>::max();
        const int rightKey = right.isChecked ? right.order : std::numeric_limits<int>::max();
        return leftKey < rightKey;
    }
};

// ------------------------------------------------------- PDFAppToolInputChooser

PDFAppToolInputRules PDFAppToolInputChooser::getInputRules(const QString& toolId)
{
    // The GUI of the tool is made only for its rules and deleted at once (no parent, no document)
    const std::shared_ptr<const PDFDocumentToolGUIFactory> factory = PDFDocumentToolGUIRegistry::getFactory(toolId);
    const std::unique_ptr<PDFDocumentToolGUI> gui(factory ? factory->createGUI() : nullptr);
    return gui ? gui->getInputRules() : PDFAppToolInputRules();
}

QString PDFAppToolInputChooser::getUnsupportedReason(const QString& toolId, const PDFAppToolInputRules& rules)
{
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
    const QString title = tool ? tool->getTitle() : QString();
    if (!rules.acceptsImages)
    {
        return tr("%1 needs PDF files").arg(title);
    }
    if (!rules.acceptsPdf)
    {
        return tr("%1 takes images").arg(title);
    }
    return QString();
}

QString PDFAppToolInputChooser::getPlaceText(int index) const
{
    if (index >= 0 && index < m_rules.slotTitles.size())
    {
        return m_rules.slotTitles.at(index);
    }
    return getOrderText(index);
}

PDFAppToolInputChooser::PDFAppToolInputChooser(PDFAppContext* context, const QString& toolId, PDFAppToolInputList* list, QWidget* parent) :
    BaseClass(parent),
    m_context(context),
    m_toolId(toolId),
    m_rules(getInputRules(toolId))
{
    setObjectName(QStringLiteral("toolInputChooser"));

    // document-list 531:3265: bg/surface, radius 12, padding 8
    QVBoxLayout* outer = PDFAppPageWidgets::createColumn(this, 0);
    PDFAppCard* card = PDFAppPageWidgets::createCard(8, 0, this);
    card->setObjectName(QStringLiteral("toolDocumentList"));
    outer->addWidget(card);

    // group-label 531:3266 / 531:3551: paddings 6 / 0 / 6 / 10, Semi Bold 11 text/tertiary
    m_documentGroupLabel = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 11, PDFThemeColors::TextTertiary, card);
    m_documentGroupLabel->setObjectName(QStringLiteral("toolDocumentGroup"));
    m_documentGroupLabel->setContentsMargins(10, 6, 0, 6);
    card->getLayout()->addWidget(m_documentGroupLabel);
    m_documentRowsLayout = new QVBoxLayout();
    m_documentRowsLayout->setContentsMargins(0, 0, 0, 0);
    m_documentRowsLayout->setSpacing(0);
    card->getLayout()->addLayout(m_documentRowsLayout);

    m_fileGroupLabel = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 11, PDFThemeColors::TextTertiary, card);
    m_fileGroupLabel->setObjectName(QStringLiteral("toolFileGroup"));
    m_fileGroupLabel->setContentsMargins(10, 6, 0, 6);
    card->getLayout()->addWidget(m_fileGroupLabel);
    m_fileRowsLayout = new QVBoxLayout();
    m_fileRowsLayout->setContentsMargins(0, 0, 0, 0);
    m_fileRowsLayout->setSpacing(0);
    card->getLayout()->addLayout(m_fileRowsLayout);

    // actions 531:3283: paddings 10 / 0 / 6 / 10, gap 12
    QHBoxLayout* actions = new QHBoxLayout();
    actions->setContentsMargins(10, 10, 0, 6);
    actions->setSpacing(12);
    m_addFilesButton = PDFAppPageWidgets::createButton(QString(), QStringLiteral("folder-open"), false, card);
    m_addFilesButton->setObjectName(QStringLiteral("toolAddFiles"));
    connect(m_addFilesButton.data(), &QPushButton::clicked, this, &PDFAppToolInputChooser::onAddFilesClicked);
    actions->addWidget(m_addFilesButton, 0, Qt::AlignVCenter);
    m_dropZone = new PDFAppDropZone(PDFAppDropZone::Variant::Compact, card);
    m_dropZone->setObjectName(QStringLiteral("toolDropZone"));
    m_dropText = m_dropZone->findChild<PDFLabel*>(QStringLiteral("dropText"));
    actions->addWidget(m_dropZone, 1);
    card->getLayout()->addLayout(actions);

    // cta 531:3583: the summary Regular 12 text/secondary, gap 12, the Primary button
    QHBoxLayout* cta = new QHBoxLayout();
    cta->setContentsMargins(0, 0, 0, 0);
    cta->setSpacing(12);
    m_summaryLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextSecondary, card);
    m_summaryLabel->setObjectName(QStringLiteral("toolInputSummary"));
    m_summaryLabel->setMinimumWidth(1);
    cta->addWidget(m_summaryLabel, 1);
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
    m_openToolButton = PDFAppPageWidgets::createButton(QString(), tool ? tool->getIconName() : QString(), true, card);
    m_openToolButton->setObjectName(QStringLiteral("toolOpenTool"));
    connect(m_openToolButton.data(), &QPushButton::clicked, this, &PDFAppToolInputChooser::onOpenToolClicked);
    cta->addWidget(m_openToolButton, 0, Qt::AlignVCenter);
    card->getLayout()->addLayout(cta);

    if (PDFAppDocumentRegistry* registry = context ? context->getDocumentRegistry() : nullptr)
    {
        connect(registry, &PDFAppDocumentRegistry::documentAdded, this, &PDFAppToolInputChooser::onDocumentChanged);
        connect(registry, &PDFAppDocumentRegistry::documentChanged, this, &PDFAppToolInputChooser::onDocumentChanged);
        connect(registry, &PDFAppDocumentRegistry::documentRemoved, this, &PDFAppToolInputChooser::onDocumentRemoved);
    }

    setInputList(list);
}

PDFAppToolInputChooser::~PDFAppToolInputChooser() = default;

PDFAppToolInputList* PDFAppToolInputChooser::getInputList() const
{
    return m_list.data();
}

void PDFAppToolInputChooser::setInputList(PDFAppToolInputList* list)
{
    if (m_list == list)
    {
        return;
    }

    if (m_list)
    {
        m_list->disconnect(this);
    }
    m_list = list;
    m_fileIds.clear();
    m_message.clear();
    if (list)
    {
        // K41: the list takes what the tool takes (before the connections: no row is built twice)
        list->setAcceptedInputs(m_rules.maximum, m_rules.acceptsPdf, m_rules.acceptsImages, getUnsupportedReason(m_toolId, m_rules));
        connect(list, &PDFAppToolInputList::inputsChanged, this, &PDFAppToolInputChooser::onListChanged);
        connect(list, &PDFAppToolInputList::inputChanged, this, &PDFAppToolInputChooser::onInputChanged);

        // The files of a list given with its inputs are the files of this chooser
        for (int i = 0; i < list->getCount(); ++i)
        {
            const PDFAppToolInput* input = list->getInput(i);
            if (input->kind != PDFAppToolInputKind::OpenTab)
            {
                m_fileIds << input->id;
            }
        }
    }
    refresh();
}

std::vector<PDFAppToolDocumentRow*> PDFAppToolInputChooser::getDocumentRows() const
{
    std::vector<PDFAppToolDocumentRow*> rows;
    for (const QPointer<PDFAppToolDocumentRow>& row : m_documentRows)
    {
        if (row)
        {
            rows.push_back(row.data());
        }
    }
    return rows;
}

std::vector<PDFAppToolDocumentRow*> PDFAppToolInputChooser::getFileRows() const
{
    std::vector<PDFAppToolDocumentRow*> rows;
    for (const QPointer<PDFAppToolDocumentRow>& row : m_fileRows)
    {
        if (row)
        {
            rows.push_back(row.data());
        }
    }
    return rows;
}

int PDFAppToolInputChooser::getDocumentCount() const
{
    int count = 0;
    for (int i = 0; m_list && i < m_list->getCount(); ++i)
    {
        const PDFAppToolInputState state = m_list->getInput(i)->state;
        if (state != PDFAppToolInputState::Damaged && state != PDFAppToolInputState::Unsupported && state != PDFAppToolInputState::NoPages)
        {
            ++count;
        }
    }
    return count;
}

QString PDFAppToolInputChooser::getOrderText(int index)
{
    // English ordinals by the last digits (11th, 12th, 13th); each a text of its own
    const int place = index + 1;
    const QString number = PDFAppLocale::formatNumber(place);
    const int lastTwo = place % 100;
    const int last = place % 10;
    if (last == 1 && lastTwo != 11)
    {
        return tr("%1st in the result", "an ordinal place in the result: 1st, 21st").arg(number);
    }
    if (last == 2 && lastTwo != 12)
    {
        return tr("%1nd in the result", "an ordinal place in the result: 2nd, 22nd").arg(number);
    }
    if (last == 3 && lastTwo != 13)
    {
        return tr("%1rd in the result", "an ordinal place in the result: 3rd, 23rd").arg(number);
    }
    return tr("%1th in the result", "an ordinal place in the result: 4th, 11th").arg(number);
}

std::vector<PDFAppDocument*> PDFAppToolInputChooser::getOpenDocuments() const
{
    // F13: the ready documents of the tabs of this window
    std::vector<PDFAppDocument*> documents;
    const PDFAppDocumentRegistry* registry = m_context ? m_context->getDocumentRegistry() : nullptr;
    const PDFAppMainWindow* window = qobject_cast<const PDFAppMainWindow*>(this->window());
    if (registry)
    {
        for (PDFAppDocument* document : registry->getDocuments())
        {
            if (document->isReady() && (!window || window->findDocumentTab(document->getId())))
            {
                documents.push_back(document);
            }
        }
    }
    return documents;
}

QString PDFAppToolInputChooser::getInputIdOfRow(const PDFAppToolDocumentRow* row) const
{
    if (!row || !m_list)
    {
        return QString();
    }

    // A row of a file has the id of its input, a row of an open document its document id
    if (m_fileIds.contains(row->getDocumentId()))
    {
        return row->getDocumentId();
    }
    for (int i = 0; i < m_list->getCount(); ++i)
    {
        const PDFAppToolInput* input = m_list->getInput(i);
        if (input->kind == PDFAppToolInputKind::OpenTab && input->documentId == row->getDocumentId())
        {
            return input->id;
        }
    }
    return QString();
}

PDFAppToolDocumentRow* PDFAppToolInputChooser::createInputRow(const QString& rowId, const QString& name, const QString& filePath, const QString& numbers, const QString& tagText, int order, bool isChecked, bool isFile)
{
    PDFAppToolDocumentRow* row = new PDFAppToolDocumentRow(rowId, name, PDFAppToolDocumentChooser::getFolder(filePath), numbers, tagText, this);
    row->setCheckBoxVisible(true);
    row->setChecked(isChecked);
    row->setActionText(isChecked && order >= 0 ? getPlaceText(order) : QString());
    row->setTrailing(isChecked ? PDFAppToolDocumentRow::Trailing::Handle : PDFAppToolDocumentRow::Trailing::None);
    row->setRemovable(isFile);
    connect(row, &PDFAppToolDocumentRow::toggled, this, &PDFAppToolInputChooser::onRowToggled);
    connect(row, &PDFAppToolDocumentRow::removeClicked, this, &PDFAppToolInputChooser::onRowRemoveClicked);
    connect(row, &PDFAppToolDocumentRow::moveRequested, this, &PDFAppToolInputChooser::onRowMoveRequested);
    connect(row, &PDFAppToolDocumentRow::handleDropped, this, &PDFAppToolInputChooser::onRowHandleDropped);
    return row;
}

void PDFAppToolInputChooser::refresh()
{
    m_isRefreshQueued = false;
    if (!m_documentRowsLayout || !m_fileRowsLayout)
    {
        return;
    }

    // The focus stays on its row; a row may be the sender of this change: the old rows go
    // from the event loop
    for (std::vector<QPointer<PDFAppToolDocumentRow>>* rows : { &m_documentRows, &m_fileRows })
    {
        for (const QPointer<PDFAppToolDocumentRow>& row : *rows)
        {
            if (row && row->hasFocus())
            {
                m_focusedRowId = row->getDocumentId();
            }
            if (row)
            {
                row->blockSignals(true);
                row->hide();
                row->deleteLater();
            }
        }
        rows->clear();
    }

    // The forgotten files leave the chooser
    if (m_list)
    {
        QStringList fileIds;
        for (const QString& id : m_fileIds)
        {
            if (m_list->findInput(id))
            {
                fileIds << id;
            }
        }
        m_fileIds = fileIds;
    }

    // OPEN DOCUMENTS: the checked documents in the order of the result, then the others
    using RowData = PDFAppToolInputChooserRow;
    std::vector<RowData> documentRows;
    for (PDFAppDocument* document : getOpenDocuments())
    {
        RowData rowData;
        rowData.rowId = document->getId();
        rowData.name = document->getTitle();
        rowData.filePath = document->getFilePath();
        rowData.numbers = PDFAppToolDocumentChooser::createNumbers(document);
        for (int i = 0; m_list && i < m_list->getCount(); ++i)
        {
            const PDFAppToolInput* input = m_list->getInput(i);
            if (input->kind == PDFAppToolInputKind::OpenTab && input->documentId == rowData.rowId)
            {
                rowData.isChecked = true;
                rowData.order = i;
                rowData.tagText = input->needsAttention() ? PDFAppToolInputList::getStateText(input->state) : QString();
            }
        }
        if (rowData.tagText.isEmpty() && document->isModified())
        {
            rowData.tagText = tr("Unsaved changes · current state is used");
        }
        documentRows.push_back(std::move(rowData));
    }

    std::vector<RowData> fileRows;
    for (const QString& id : m_fileIds)
    {
        const PDFAppToolInput* input = m_list ? m_list->findInput(id) : nullptr;
        if (!input)
        {
            continue;
        }

        RowData rowData;
        rowData.rowId = id;
        rowData.name = input->name;
        rowData.filePath = input->filePath;
        rowData.order = m_list->indexOf(id);
        rowData.isChecked = rowData.order >= 0;

        // K28: the numbers come from the reading; a protected file says so by its Tag
        QStringList numbers;
        if (input->state == PDFAppToolInputState::Loading)
        {
            numbers << PDFAppToolInputList::getStateText(input->state);
        }
        else if (input->pageCount > 0)
        {
            const int count = int(qMin<PDFInteger>(input->pageCount, std::numeric_limits<int>::max()));
            numbers << (count == 1 ? tr("%Ln page", nullptr, count) : tr("%Ln pages", nullptr, count));
        }
        if (input->fileSize > 0 && input->state != PDFAppToolInputState::Loading)
        {
            numbers << PDFDocumentToolFormat::formatSize(input->fileSize);
        }
        rowData.numbers = numbers.join(PDFComponentPainter::getDetailSeparator());
        rowData.tagText = input->state != PDFAppToolInputState::Loading ? PDFAppToolInputList::getStateText(input->state) : QString();
        fileRows.push_back(std::move(rowData));
    }

    for (std::vector<RowData>* rows : { &documentRows, &fileRows })
    {
        std::stable_sort(rows->begin(), rows->end(), &PDFAppToolInputChooserRow::isBefore);
    }

    for (const RowData& rowData : documentRows)
    {
        PDFAppToolDocumentRow* row = createInputRow(rowData.rowId, rowData.name, rowData.filePath, rowData.numbers, rowData.tagText, rowData.order, rowData.isChecked, false);
        m_documentRowsLayout->addWidget(row);
        m_documentRows.push_back(row);
    }
    for (const RowData& rowData : fileRows)
    {
        PDFAppToolDocumentRow* row = createInputRow(rowData.rowId, rowData.name, rowData.filePath, rowData.numbers, rowData.tagText, rowData.order, rowData.isChecked, true);
        m_fileRowsLayout->addWidget(row);
        m_fileRows.push_back(row);
    }

    for (const std::vector<QPointer<PDFAppToolDocumentRow>>* rows : { &m_documentRows, &m_fileRows })
    {
        for (const QPointer<PDFAppToolDocumentRow>& row : *rows)
        {
            if (row->getDocumentId() == m_focusedRowId)
            {
                row->setFocus();
            }
        }
    }

    updateTexts();
}

void PDFAppToolInputChooser::updateTexts()
{
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(m_toolId);
    const QString title = tool ? tool->getTitle() : QString();

    if (m_documentGroupLabel)
    {
        m_documentGroupLabel->setText(tr("OPEN DOCUMENTS"));
        m_documentGroupLabel->setVisible(!m_documentRows.empty());
    }
    if (m_fileGroupLabel)
    {
        // 551:149074 "FILES FROM DISK – none added yet"
        m_fileGroupLabel->setText(m_fileRows.empty() ? tr("FILES FROM DISK – none added yet") : tr("FILES FROM DISK"));
    }
    if (m_addFilesButton)
    {
        m_addFilesButton->setText(tr("Add files…"));
    }
    if (m_dropText)
    {
        if (!m_rules.acceptsImages)
        {
            // K29, K41: Interleave takes PDF files only
            m_dropText->setText(tr("or drag and drop PDF files here – they are added in the dropped order"));
        }
        else
        {
            m_dropText->setText(tr("or drag and drop PDF files and images here – they are added in the dropped order"));
        }
        if (m_dropZone)
        {
            m_dropZone->setAccessibleName(m_dropText->text());
        }
    }

    const int count = getDocumentCount();
    PDFInteger pageCount = 0;
    for (int i = 0; m_list && i < m_list->getCount(); ++i)
    {
        pageCount += m_list->getInput(i)->pageCount;
    }
    const int pages = int(qMin<PDFInteger>(pageCount, std::numeric_limits<int>::max()));

    QString summary;
    const bool isExactCount = m_rules.minimum == m_rules.maximum;
    if (count < m_rules.minimum && isExactCount)
    {
        // K41: Interleave takes exactly two documents (the front sides and the back sides); both
        // numbers are counts, each one has its own %Ln
        const QString need = tr("%1 needs exactly %Ln documents – check another open document or add a file", nullptr, m_rules.minimum).arg(title);
        summary = (count == 1 ? tr("%Ln document · %1", nullptr, count) : tr("%Ln documents · %1", nullptr, count)).arg(need);
    }
    else if (count < m_rules.minimum)
    {
        // 551:149074
        summary = (count == 1 ? tr("%Ln document · %1 needs at least two documents – check another open document or add a file", nullptr, count)
                              : tr("%Ln documents · %1 needs at least two documents – check another open document or add a file", nullptr, count)).arg(title);
    }
    else
    {
        // 531:3583
        const QString pagesText = pages == 1 ? tr("%Ln page", nullptr, pages) : tr("%Ln pages", nullptr, pages);
        summary = tr("%Ln documents", nullptr, count) + QStringLiteral(" · ") + pagesText + QStringLiteral(" · ") + tr("drag the handles to change the order");
    }
    if (!m_message.isEmpty())
    {
        summary = m_message + QStringLiteral(" · ") + summary;
    }
    if (m_summaryLabel)
    {
        m_summaryLabel->setText(summary);
    }

    if (m_openToolButton)
    {
        const bool isEnough = count >= m_rules.minimum && count <= m_rules.maximum;
        m_openToolButton->setText(isEnough ? tr("Open %1 with %Ln Documents", nullptr, count).arg(title) : tr("Open %1").arg(title));
        m_openToolButton->setEnabled(isEnough);
    }
}

void PDFAppToolInputChooser::addFiles(const QStringList& files)
{
    if (!m_list)
    {
        return;
    }

    // Each addition emits (the rows are built again from the event loop); the list may go
    QPointer<PDFAppToolInputList> list(m_list);
    QPointer<PDFAppToolInputChooser> guard(this);
    QStringList refused;
    for (const QString& file : files)
    {
        if (!guard || !list)
        {
            return;
        }

        // A file unchecked before comes back checked, read already
        const QString parkedId = list->findParkedFile(file);
        if (!parkedId.isEmpty() && m_fileIds.contains(parkedId))
        {
            QStringList ids = list->getInputIds();
            ids << parkedId;
            list->setInputIds(ids);
            continue;
        }

        const PDFAppToolInputList::AddResult result = list->addFile(file);
        if (!guard)
        {
            return;
        }
        if (result.id.isEmpty())
        {
            refused << PDFAppToolInputList::getRefusalText(result.refusal, result.name, list->getMaximumCount());
        }
        else
        {
            m_fileIds << result.id;
        }
    }

    if (guard)
    {
        m_message = refused.join(QStringLiteral(" · "));
        scheduleRefresh();
    }
}

bool PDFAppToolInputChooser::dropFiles(const QMimeData* mimeData)
{
    if (!PDFAppFileDrop::hasLocalFiles(mimeData))
    {
        return false;
    }

    // K8: PDF files and images, in the dropped order
    const PDFAppFileDrop::Result result = PDFAppFileDrop::getFiles(mimeData, PDFAppFileDrop::MAXIMUM_FILES, true);
    QStringList refused;
    for (const PDFAppFileDrop::RefusedItem& item : result.refused)
    {
        refused << tr("%1 – %2").arg(item.text, PDFAppFileDrop::getRefusalText(item.refusal));
    }

    QPointer<PDFAppToolInputChooser> guard(this);
    addFiles(result.files);
    if (guard && !refused.isEmpty())
    {
        m_message = (QStringList(m_message) + refused).join(QStringLiteral(" · "));
        if (m_message.startsWith(QStringLiteral(" · ")))
        {
            m_message = m_message.mid(3);
        }
        updateTexts();
    }
    return true;
}

void PDFAppToolInputChooser::onAddFilesClicked()
{
    // The native dialog runs its own event loop: the chooser may go meanwhile
    QPointer<PDFAppToolInputChooser> guard(this);
    const QStringList files = PDFAppFileDialogs::getOpenInputFileNames(window(), QString());
    if (guard && !files.isEmpty())
    {
        addFiles(files);
    }
}

void PDFAppToolInputChooser::onOpenToolClicked()
{
    const int count = getDocumentCount();
    if (count >= m_rules.minimum && count <= m_rules.maximum)
    {
        Q_EMIT openToolRequested();
    }
}

void PDFAppToolInputChooser::onRowToggled()
{
    const PDFAppToolDocumentRow* row = qobject_cast<const PDFAppToolDocumentRow*>(sender());
    if (!row || !m_list)
    {
        return;
    }

    m_message.clear();
    m_focusedRowId = row->getDocumentId();
    const bool isChecked = row->isChecked();
    const QString rowId = row->getDocumentId();
    if (m_fileIds.contains(rowId))
    {
        // A file: checked = in the list, unchecked = parked (no second reading)
        if (isChecked)
        {
            QStringList ids = m_list->getInputIds();
            ids << rowId;
            m_list->setInputIds(ids);
        }
        else
        {
            m_list->removeInput(rowId);
        }
        return;
    }

    // An open document: checked = a snapshot of now (R11), unchecked = gone
    if (isChecked)
    {
        for (PDFAppDocument* document : getOpenDocuments())
        {
            if (document->getId() == rowId)
            {
                // The list emits in this call: a slot may delete the chooser
                QPointer<PDFAppToolInputChooser> guard(this);
                const PDFAppToolInputList::AddResult result = m_list->addOpenDocument(document);
                if (guard && result.id.isEmpty())
                {
                    m_message = PDFAppToolInputList::getRefusalText(result.refusal, result.name);
                    scheduleRefresh();
                }
                return;
            }
        }
        scheduleRefresh();
        return;
    }

    const QString inputId = getInputIdOfRow(row);
    QPointer<PDFAppToolInputList> list(m_list);
    if (!inputId.isEmpty() && list->removeInput(inputId) && list)
    {
        QStringList kept = list->getParkedIds();
        kept.removeAll(inputId);
        list->dropParkedInputs(kept);
    }
}

void PDFAppToolInputChooser::onRowRemoveClicked()
{
    const PDFAppToolDocumentRow* row = qobject_cast<const PDFAppToolDocumentRow*>(sender());
    const QString inputId = row ? row->getDocumentId() : QString();
    if (!m_list || !m_fileIds.contains(inputId))
    {
        return;
    }

    // The file is forgotten: out of the list and out of the parked inputs
    m_message.clear();
    m_fileIds.removeAll(inputId);
    QPointer<PDFAppToolInputList> list(m_list);
    QPointer<PDFAppToolInputChooser> guard(this);
    list->removeInput(inputId);
    if (list)
    {
        QStringList kept = list->getParkedIds();
        kept.removeAll(inputId);
        list->dropParkedInputs(kept);
    }
    if (guard)
    {
        scheduleRefresh();
    }
}

void PDFAppToolInputChooser::onRowMoveRequested(int delta)
{
    const PDFAppToolDocumentRow* row = qobject_cast<const PDFAppToolDocumentRow*>(sender());
    const QString inputId = getInputIdOfRow(row);
    const int index = m_list && !inputId.isEmpty() ? m_list->indexOf(inputId) : -1;
    if (index < 0)
    {
        return;
    }

    m_focusedRowId = row->getDocumentId();
    m_message.clear();
    m_list->moveInput(inputId, index + delta);
}

void PDFAppToolInputChooser::onRowHandleDropped(const QPoint& globalPosition)
{
    const PDFAppToolDocumentRow* row = qobject_cast<const PDFAppToolDocumentRow*>(sender());
    const QString inputId = getInputIdOfRow(row);
    if (!m_list || inputId.isEmpty())
    {
        return;
    }

    // The input takes the place of the checked row under the pointer
    for (const std::vector<QPointer<PDFAppToolDocumentRow>>* rows : { &m_documentRows, &m_fileRows })
    {
        for (const QPointer<PDFAppToolDocumentRow>& target : *rows)
        {
            if (target && target != row && target->isChecked() && target->rect().contains(target->mapFromGlobal(globalPosition)))
            {
                const int targetIndex = m_list->indexOf(getInputIdOfRow(target));
                if (targetIndex >= 0)
                {
                    m_focusedRowId = row->getDocumentId();
                    m_message.clear();
                    m_list->moveInput(inputId, targetIndex);
                }
                return;
            }
        }
    }
}

void PDFAppToolInputChooser::scheduleRefresh()
{
    if (!m_isRefreshQueued)
    {
        m_isRefreshQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppToolInputChooser::refresh, Qt::QueuedConnection);
    }
}

void PDFAppToolInputChooser::onListChanged()
{
    scheduleRefresh();
}

void PDFAppToolInputChooser::onInputChanged(const QString& id)
{
    Q_UNUSED(id);
    scheduleRefresh();
}

void PDFAppToolInputChooser::onDocumentRemoved(const QString& id)
{
    // The snapshot of a closed tab stays an input (R11), but its row is gone: it leaves the
    // list too, nothing of it is shown any more
    if (m_list)
    {
        for (int i = 0; i < m_list->getCount(); ++i)
        {
            const PDFAppToolInput* input = m_list->getInput(i);
            if (input->kind == PDFAppToolInputKind::OpenTab && input->documentId == id)
            {
                const QString inputId = input->id;
                QPointer<PDFAppToolInputList> list(m_list);
                list->removeInput(inputId);
                if (list)
                {
                    QStringList kept = list->getParkedIds();
                    kept.removeAll(inputId);
                    list->dropParkedInputs(kept);
                }
                break;
            }
        }
    }
    scheduleRefresh();
}

void PDFAppToolInputChooser::onDocumentChanged(PDFAppDocument* document)
{
    Q_UNUSED(document);
    scheduleRefresh();
}

void PDFAppToolInputChooser::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);
    refresh();
}

void PDFAppToolInputChooser::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        scheduleRefresh();
    }
}

}   // namespace pdf

#include "moc_pdfapptooldocumentchooser.cpp"
