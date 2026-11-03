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
#include "pdfapphomewidgets.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfapppage.h"
#include "pdfapppagelayout.h"

#include "pdfaccenthue.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"

#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

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

    static QString getDetailsSeparator()
    {
        return QStringLiteral(" · ");
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
        updateTagGeometry();
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
    return parts.join(PDFAppToolDocumentChooserHelper::getDetailsSeparator());
}

QString PDFAppToolDocumentRow::getShownDetails() const
{
    if (m_folder.isEmpty() || m_numbers.isEmpty())
    {
        return getDetails();
    }

    const QFont font = PDFAppToolDocumentChooserHelper::getDetailsFont();
    const QString numbers = PDFAppToolDocumentChooserHelper::getDetailsSeparator() + m_numbers;
    const int folderWidth = qMax(0, getTextWidth() - QFontMetrics(font).horizontalAdvance(numbers));
    return PDFComponentPainter::elidedTextOfKind(m_folder, font, folderWidth, PDFComponentPainter::TextKind::FileName) + numbers;
}

int PDFAppToolDocumentRow::getTextWidth() const
{
    // The texts take what the action and the Tag leave
    int textRight = width() - PADDING - CHEVRON - GAP - getActionWidth() - GAP;
    if (m_tag)
    {
        textRight -= m_tag->sizeHint().width() + GAP;
    }
    return qMax(0, textRight - (PADDING + ICON + GAP));
}

int PDFAppToolDocumentRow::getActionWidth() const
{
    return QFontMetrics(PDFAppToolDocumentChooserHelper::getActionFont()).horizontalAdvance(PDFAppToolDocumentChooserHelper::getActionText()) + 1;
}

QRect PDFAppToolDocumentRow::getActionRect() const
{
    const int actionWidth = getActionWidth();
    const QRect logical(width() - PADDING - CHEVRON - GAP - actionWidth, 0, actionWidth, height());
    return QStyle::visualRect(layoutDirection(), rect(), logical);
}

void PDFAppToolDocumentRow::updateTagGeometry()
{
    if (!m_tag)
    {
        return;
    }

    const QSize size = m_tag->sizeHint();
    const int right = width() - PADDING - CHEVRON - GAP - getActionWidth() - GAP;
    const QRect logical(right - size.width(), (height() - size.height()) / 2, size.width(), size.height());
    m_tag->setGeometry(QStyle::visualRect(layoutDirection(), rect(), logical));
}

void PDFAppToolDocumentRow::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateTagGeometry();
}

QSize PDFAppToolDocumentRow::getContentSizeHint() const
{
    return QSize(2 * PADDING + ICON + GAP + CHEVRON + GAP + getActionWidth() + GAP, HEIGHT);
}

void PDFAppToolDocumentRow::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    using Color = PDFThemeColors::Color;

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const PDFStylePainter::Radii radii(RADIUS);
    if (state.isEnabled() && state.isPressed())
    {
        PDFStylePainter::fill(painter, QRectF(rect), radii, Color::BgPressed);
    }
    else if (state.isEnabled() && state.isHovered())
    {
        PDFStylePainter::fill(painter, QRectF(rect), radii, Color::BgHover);
    }
    if (state.hasFocusRing())
    {
        PDFComponentPainter::paintFocusRingInside(painter, QRectF(rect), radii);
    }

    const Qt::LayoutDirection direction = layoutDirection();
    const QRect iconRect = QStyle::visualRect(direction, rect, QRect(rect.left() + PADDING, rect.top() + (rect.height() - ICON) / 2, ICON, ICON));
    PDFStylePainter::icon(painter, QRectF(iconRect), QStringLiteral("file-text"), state.isEnabled() ? Color::IconPdf : Color::IconDisabled);

    const QRect chevronRect = QStyle::visualRect(direction, rect, QRect(rect.right() + 1 - PADDING - CHEVRON, rect.top() + (rect.height() - CHEVRON) / 2, CHEVRON, CHEVRON));
    PDFStylePainter::icon(painter, QRectF(chevronRect), PDFAppPage::getDirectionalIconName(QStringLiteral("chevron-right"), direction), state.isEnabled() ? Color::IconMuted : Color::IconDisabled);

    const Color actionColor = !state.isEnabled() ? Color::TextDisabled : (state.isHovered() ? Color::TextAccent : Color::TextTertiary);
    PDFComponentPainter::drawElidedText(painter, state, QRectF(getActionRect().translated(rect.topLeft())), PDFAppToolDocumentChooserHelper::getActionText(),
                                        PDFAppToolDocumentChooserHelper::getActionFont(), actionColor);

    const int textLeft = PADDING + ICON + GAP;
    const int textWidth = getTextWidth();
    const QRect nameRect(rect.left() + textLeft, rect.top() + 9, textWidth, 16);
    const QRect detailsRect(rect.left() + textLeft, rect.top() + 27, textWidth, 13);
    PDFComponentPainter::drawElidedText(painter, state, QRectF(QStyle::visualRect(direction, rect, nameRect)), m_name, PDFThemeFonts::getFont(QFont::DemiBold, 13),
                                        state.isEnabled() ? Color::TextPrimary : Color::TextDisabled, PDFComponentPainter::TextKind::FileName);
    PDFComponentPainter::drawElidedText(painter, state, QRectF(QStyle::visualRect(direction, rect, detailsRect)), getShownDetails(), PDFAppToolDocumentChooserHelper::getDetailsFont(),
                                        state.isEnabled() ? Color::TextMuted : Color::TextDisabled, PDFComponentPainter::TextKind::Data);
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

QString PDFAppToolDocumentChooser::createNumbers(const PDFAppDocument* document) const
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
    return parts.join(PDFAppToolDocumentChooserHelper::getDetailsSeparator());
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

            const QString filePath = document->getFilePath();
            const QString folder = filePath.isEmpty() ? QString() : QDir::toNativeSeparators(QFileInfo(filePath).absolutePath());
            PDFAppToolDocumentRow* row = new PDFAppToolDocumentRow(document->getId(), document->getTitle(), folder, createNumbers(document), tagText, this);
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

}   // namespace pdf

#include "moc_pdfapptooldocumentchooser.cpp"
