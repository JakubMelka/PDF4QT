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

#include "pdfappmanagepanels.h"

#include "pdfapppagelayout.h"
#include "pdfapptoolgui_images.h"
#include "pdfapptooloutputfiles.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfdocumenttoolgui.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QVBoxLayout>

namespace pdf
{

// ------------------------------------------------------------- PDFAppManageListRow

PDFAppManageListRow::PDFAppManageListRow(QWidget* parent) :
    BaseClass(parent)
{
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(GAP);

    // icon-tile 387:12752: 30, radius 8, the icon 16
    m_tile = new PDFIconBadge(QString(), TILE, ICON, this);
    m_tile->setRadius(RADIUS);
    layout->addWidget(m_tile, 0, Qt::AlignVCenter);

    // text 387:12758: the title Semi Bold 12 (a line of 15), 1 below it Regular 11 text/tertiary
    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(1);
    m_title = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 12, PDFThemeColors::TextPrimary, this);
    m_title->setLineHeight(PDFAppToolInputRow::NAME_HEIGHT);
    m_title->setMinimumWidth(1);
    textLayout->addWidget(m_title);
    m_text = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextTertiary, this);
    m_text->setLineHeight(PDFAppToolInputRow::LINE_HEIGHT);
    m_text->setWordWrap(true);
    m_text->setMinimumWidth(1);
    textLayout->addWidget(m_text);
    layout->addLayout(textLayout, 1);

    // link-restore 387:12755: Semi Bold 13 text/accent
    m_link = new PDFTextLink(this);
    connect(m_link.data(), &PDFTextLink::clicked, this, &PDFAppManageListRow::onLinkClicked);
    layout->addWidget(m_link, 0, Qt::AlignVCenter);
}

PDFAppManageListRow::~PDFAppManageListRow() = default;

void PDFAppManageListRow::setContent(const Content& content)
{
    if (m_content == content && !m_title->text().isEmpty())
    {
        return;
    }

    m_content = content;
    m_tile->setIconName(content.iconName);
    // 387:12752 bg/accent-subtle (the newest checkpoint), 387:12762 bg/muted
    m_tile->setColors(content.isAccent ? PDFThemeColors::BgAccentSubtle : PDFThemeColors::BgMuted,
                      content.isAccent ? PDFThemeColors::IconAccent : PDFThemeColors::IconDefault);
    m_title->setText(content.title);
    m_text->setText(content.text);
    m_text->setVisible(!content.text.isEmpty());
    m_link->setText(content.linkText);
    m_link->setVisible(!content.linkText.isEmpty());
    m_link->setAccessibleDescription(content.title);
    setToolTip(content.title + (content.text.isEmpty() ? QString() : QStringLiteral("\n") + content.text));
}

void PDFAppManageListRow::onLinkClicked()
{
    Q_EMIT linkActivated(m_content.key);
}

void PDFAppManageListRow::mouseReleaseEvent(QMouseEvent* event)
{
    BaseClass::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && isEnabled())
    {
        // Last: a slot may rebuild the panel
        Q_EMIT clicked(m_content.key);
    }
}

// ------------------------------------------------------------- PDFAppManageSourcesPanel

PDFAppManageSourcesPanel::PDFAppManageSourcesPanel(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("manageSourcesPanel"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ROW_GAP);

    // header + source-A ... 387:12723: the panel Files of Merge with the texts of Manage
    m_sources = new PDFAppToolInputFilesPanel(this);
    m_sources->setObjectName(QStringLiteral("manageSources"));
    connect(m_sources.data(), &PDFAppToolInputFilesPanel::rowClicked, this, &PDFAppManageSourcesPanel::sourceClicked);
    connect(m_sources.data(), &PDFAppToolInputFilesPanel::linkActivated, this, &PDFAppManageSourcesPanel::sourceLinkActivated);
    layout->addWidget(m_sources);

    // header 387:12748: "Checkpoints" Semi Bold 13 8 below the top of the header and the link
    m_checkpointsSection = new QWidget(this);
    m_checkpointsSection->setObjectName(QStringLiteral("manageCheckpoints"));
    QVBoxLayout* checkpointsLayout = new QVBoxLayout(m_checkpointsSection);
    checkpointsLayout->setContentsMargins(PDFAppToolInputFilesPanel::SIDE_MARGIN, 0, PDFAppToolInputFilesPanel::SIDE_MARGIN, 0);
    checkpointsLayout->setSpacing(ROW_GAP);
    QWidget* checkpointsHeader = new QWidget(m_checkpointsSection);
    checkpointsHeader->setFixedHeight(HEADER_HEIGHT);
    QHBoxLayout* checkpointsHeaderLayout = new QHBoxLayout(checkpointsHeader);
    checkpointsHeaderLayout->setContentsMargins(0, 8, 0, 0);
    checkpointsHeaderLayout->setSpacing(8);
    m_checkpointsTitle = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, checkpointsHeader);
    m_checkpointsTitle->setMinimumWidth(1);
    checkpointsHeaderLayout->addWidget(m_checkpointsTitle, 1);
    m_saveCheckpoint = new PDFTextLink(checkpointsHeader);
    m_saveCheckpoint->setObjectName(QStringLiteral("manageSaveCheckpoint"));
    connect(m_saveCheckpoint.data(), &PDFTextLink::clicked, this, &PDFAppManageSourcesPanel::onSaveCheckpointClicked);
    checkpointsHeaderLayout->addWidget(m_saveCheckpoint);
    checkpointsLayout->addWidget(checkpointsHeader);
    m_checkpointRowsContainer = new QWidget(m_checkpointsSection);
    QVBoxLayout* checkpointRowsLayout = new QVBoxLayout(m_checkpointRowsContainer);
    checkpointRowsLayout->setContentsMargins(0, 0, 0, 0);
    checkpointRowsLayout->setSpacing(ROW_GAP);
    checkpointsLayout->addWidget(m_checkpointRowsContainer);
    layout->addWidget(m_checkpointsSection);

    // header 387:12772 "Removed pages" and the row of the bin 387:12779
    m_removedSection = new QWidget(this);
    m_removedSection->setObjectName(QStringLiteral("manageRemoved"));
    QVBoxLayout* removedLayout = new QVBoxLayout(m_removedSection);
    removedLayout->setContentsMargins(PDFAppToolInputFilesPanel::SIDE_MARGIN, 0, PDFAppToolInputFilesPanel::SIDE_MARGIN, 0);
    removedLayout->setSpacing(ROW_GAP);
    QWidget* removedHeader = new QWidget(m_removedSection);
    removedHeader->setFixedHeight(HEADER_HEIGHT);
    QHBoxLayout* removedHeaderLayout = new QHBoxLayout(removedHeader);
    removedHeaderLayout->setContentsMargins(0, 8, 0, 0);
    m_removedTitle = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, removedHeader);
    m_removedTitle->setMinimumWidth(1);
    removedHeaderLayout->addWidget(m_removedTitle, 1);
    removedLayout->addWidget(removedHeader);
    m_removedRow = new PDFAppManageListRow(m_removedSection);
    m_removedRow->setObjectName(QStringLiteral("manageRemovedRow"));
    connect(m_removedRow.data(), &PDFAppManageListRow::linkActivated, this, &PDFAppManageSourcesPanel::onRemovedLinkActivated);
    connect(m_removedRow.data(), &PDFAppManageListRow::clicked, this, &PDFAppManageSourcesPanel::onRemovedRowClicked);
    removedLayout->addWidget(m_removedRow);
    m_removedRowsContainer = new QWidget(m_removedSection);
    QVBoxLayout* removedRowsLayout = new QVBoxLayout(m_removedRowsContainer);
    removedRowsLayout->setContentsMargins(0, 0, 0, 0);
    removedRowsLayout->setSpacing(0);
    m_removedRowsContainer->hide();
    removedLayout->addWidget(m_removedRowsContainer);
    layout->addWidget(m_removedSection);
    layout->addStretch(1);

    m_checkpointsSection->hide();
    m_removedSection->hide();
    retranslate();
}

PDFAppManageSourcesPanel::~PDFAppManageSourcesPanel() = default;

void PDFAppManageSourcesPanel::retranslate()
{
    m_checkpointsTitle->setText(tr("Checkpoints"));
    m_saveCheckpoint->setText(tr("Save checkpoint"));
    m_removedTitle->setText(tr("Removed pages"));
}

void PDFAppManageSourcesPanel::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

void PDFAppManageSourcesPanel::setSources(const QString& title, const QString& subtitle, const std::vector<PDFAppToolInputRow::Content>& rows, const QString& hint)
{
    m_sources->setHeader(title, subtitle);
    m_sources->setRows(rows);
    m_sources->setHint(hint);

    // 387:12748: the header of Checkpoints lies 10 below the last source - an empty hint takes
    // no place (409:32928 has the hint only without sources)
    if (PDFAppParagraph* hintLabel = m_sources->getHintLabel())
    {
        hintLabel->setVisible(!hint.isEmpty());
    }
}

void PDFAppManageSourcesPanel::setCheckpoints(const std::vector<PDFAppManageListRow::Content>& rows, bool isShown)
{
    m_checkpointsSection->setVisible(isShown);
    updateRows(m_checkpointRowsContainer, &m_checkpointRows, rows, true);
}

void PDFAppManageSourcesPanel::setRemoved(const PDFAppManageListRow::Content& row, const std::vector<PDFAppManageListRow::Content>& itemRows)
{
    m_removedSection->setVisible(!row.title.isEmpty());
    m_removedRow->setContent(row);
    updateRows(m_removedRowsContainer, &m_removedItemRows, itemRows, false);
    m_removedRowsContainer->setVisible(m_isRemovedExpanded && !itemRows.empty());
}

void PDFAppManageSourcesPanel::setRemovedExpanded(bool isExpanded)
{
    m_isRemovedExpanded = isExpanded;
    m_removedRowsContainer->setVisible(isExpanded && !m_removedItemRows.empty());
}

void PDFAppManageSourcesPanel::updateRows(QWidget* container, std::vector<QPointer<PDFAppManageListRow>>* rows, const std::vector<PDFAppManageListRow::Content>& contents, bool isCheckpoint)
{
    // The rows are made only when their number grows; an extra row is deleted later (it may be
    // the sender of a click)
    while (rows->size() > contents.size())
    {
        if (rows->back())
        {
            rows->back()->hide();
            rows->back()->deleteLater();
        }
        rows->pop_back();
    }
    while (rows->size() < contents.size())
    {
        PDFAppManageListRow* row = new PDFAppManageListRow(container);
        if (isCheckpoint)
        {
            row->setObjectName(QStringLiteral("manageCheckpointRow"));
            connect(row, &PDFAppManageListRow::linkActivated, this, &PDFAppManageSourcesPanel::onCheckpointLinkActivated);
        }
        else
        {
            row->setObjectName(QStringLiteral("manageRemovedItemRow"));
            connect(row, &PDFAppManageListRow::linkActivated, this, &PDFAppManageSourcesPanel::onRemovedItemLinkActivated);
        }
        container->layout()->addWidget(row);
        rows->push_back(row);
    }
    for (size_t i = 0; i < contents.size(); ++i)
    {
        if ((*rows)[i])
        {
            (*rows)[i]->setContent(contents[i]);
            (*rows)[i]->show();
        }
    }
}

void PDFAppManageSourcesPanel::setCurrentSource(const QString& sourceId)
{
    m_sources->setCurrentInput(sourceId);
}

void PDFAppManageSourcesPanel::setEditable(bool isEditable)
{
    m_sources->setEditable(isEditable);
    m_saveCheckpoint->setEnabled(isEditable);
    m_checkpointsSection->setEnabled(isEditable);
    m_removedSection->setEnabled(isEditable);
}

std::vector<PDFAppManageListRow*> PDFAppManageSourcesPanel::getCheckpointRows() const
{
    std::vector<PDFAppManageListRow*> rows;
    for (const QPointer<PDFAppManageListRow>& row : m_checkpointRows)
    {
        if (row)
        {
            rows.push_back(row.data());
        }
    }
    return rows;
}

std::vector<PDFAppManageListRow*> PDFAppManageSourcesPanel::getRemovedItemRows() const
{
    std::vector<PDFAppManageListRow*> rows;
    for (const QPointer<PDFAppManageListRow>& row : m_removedItemRows)
    {
        if (row)
        {
            rows.push_back(row.data());
        }
    }
    return rows;
}

void PDFAppManageSourcesPanel::onSaveCheckpointClicked()
{
    Q_EMIT saveCheckpointRequested();
}

void PDFAppManageSourcesPanel::onCheckpointLinkActivated(const QString& key)
{
    Q_EMIT restoreCheckpointRequested(key);
}

void PDFAppManageSourcesPanel::onRemovedLinkActivated(const QString& key)
{
    Q_UNUSED(key);
    Q_EMIT restoreAllRequested();
}

void PDFAppManageSourcesPanel::onRemovedRowClicked(const QString& key)
{
    Q_UNUSED(key);

    // The removed items expand below the row of the bin (plan 2, no frame)
    setRemovedExpanded(!m_isRemovedExpanded);
    Q_EMIT removedExpandedChanged(m_isRemovedExpanded);
}

void PDFAppManageSourcesPanel::onRemovedItemLinkActivated(const QString& key)
{
    Q_EMIT restoreRequested(key);
}

// ------------------------------------------------------------- PDFAppManageResultPanel

PDFAppManageResultPanel::PDFAppManageResultPanel(PDFDocumentToolGUI* gui, PDFAppToolOutputFilesSection* outputFiles, QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("manageResultPanel"));
    PDFAppToolPanelBuilder builder(gui, this, Qt::Vertical, gui ? gui->getRightPanelSpacing() : 14);

    // 396:26763: Output files above Result (the outputs of more files)
    if (outputFiles)
    {
        outputFiles->describe(builder);
    }

    // section-Selection 387:13234: the Tag "2 pages", the link Rename, the rows of the selection
    m_selection = builder.addSelectedItemCard(QString());
    m_selection->setObjectName(QStringLiteral("manageSelection"));
    connect(m_selection.data(), &PDFAppToolSelectedItemCard::linkActivated, this, &PDFAppManageResultPanel::onRenameActivated);
    if (PDFKeyValueList* values = m_selection->getValues())
    {
        values->setRowGap(KEY_VALUE_ROW_GAP);
    }

    // section-Result 387:13274: the header and the card of the rows
    m_resultSection = new QWidget(this);
    m_resultSection->setObjectName(QStringLiteral("manageResultSection"));
    QVBoxLayout* resultLayout = new QVBoxLayout(m_resultSection);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(PDFAppToolPanelBuilder::SECTION_GAP);
    PDFAppToolSectionHeader* resultHeader = new PDFAppToolSectionHeader(QString(), QString(), m_resultSection);
    m_resultTitle = resultHeader->getTitleLabel();
    resultLayout->addWidget(resultHeader);
    PDFAppToolCard* resultCard = new PDFAppToolCard(m_resultSection);
    m_resultValues = new PDFKeyValueList(resultCard);
    m_resultValues->setObjectName(QStringLiteral("manageResultValues"));
    m_resultValues->setRowGap(KEY_VALUE_ROW_GAP);
    resultCard->getBodyLayout()->addWidget(m_resultValues);
    resultLayout->addWidget(resultCard);
    builder.addWidget(m_resultSection);

    // callout-info of the tab of a document (1479:34632) and callout-red of its lock (1479:35631)
    // with the link "Enter owner password…"
    m_infoCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), QString(), QString());
    m_infoCallout->setObjectName(QStringLiteral("manageInfoCallout"));
    m_infoCallout->setCompactStyle();
    m_infoCallout->hide();
    m_lockCallout = builder.addCallout(PDFAccentHue::Red, QStringLiteral("lock"), QString(), QString());
    m_lockCallout->setObjectName(QStringLiteral("manageLockCallout"));
    m_lockCallout->setCompactStyle();
    m_calloutLink = new PDFTextLink(m_lockCallout);
    m_calloutLink->setObjectName(QStringLiteral("manageCalloutLink"));
    connect(m_calloutLink.data(), &PDFTextLink::clicked, this, &PDFAppManageResultPanel::onCalloutLinkClicked);
    m_lockCallout->getBodyLayout()->addWidget(m_calloutLink, 0, Qt::AlignLeading);
    m_lockCallout->hide();

    // 387:13287: Regular 11 text/muted
    m_note = builder.addHint(QString());
    m_note->setObjectName(QStringLiteral("manageNote"));

    // 409:32739 "How Manage Pages works": the steps in a card
    m_howSection = new QWidget(this);
    m_howSection->setObjectName(QStringLiteral("manageHowSection"));
    QVBoxLayout* howLayout = new QVBoxLayout(m_howSection);
    howLayout->setContentsMargins(0, 0, 0, 0);
    howLayout->setSpacing(PDFAppToolPanelBuilder::SECTION_GAP);
    PDFAppToolSectionHeader* howHeader = new PDFAppToolSectionHeader(QString(), QString(), m_howSection);
    m_howTitle = howHeader->getTitleLabel();
    howLayout->addWidget(howHeader);
    PDFAppToolCard* howCard = new PDFAppToolCard(m_howSection);
    howCard->getBodyLayout()->setContentsMargins(PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H);
    m_howText = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, howCard);
    m_howText->setObjectName(QStringLiteral("manageHowText"));
    howCard->getBodyLayout()->addWidget(m_howText);
    howLayout->addWidget(howCard);
    builder.addWidget(m_howSection);

    retranslate();
    setContent(m_content);
}

PDFAppManageResultPanel::~PDFAppManageResultPanel() = default;

void PDFAppManageResultPanel::retranslate()
{
    m_selection->getTitleLabel()->setText(tr("Selection"));
    m_resultTitle->setText(tr("Result"));
    m_howTitle->setText(tr("How Manage Pages works"));
    m_howText->setText(tr("1. Add files or open a saved assembly.\n2. Arrange the pages: drag, group, sort, rotate, remove.\n3. Save Assembly keeps the arrangement for later.\n4. Create PDF writes the result – the source files are never changed."));
}

void PDFAppManageResultPanel::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

void PDFAppManageResultPanel::setContent(const Content& content)
{
    m_content = content;

    m_howSection->setVisible(content.isEmpty);
    m_selection->setVisible(!content.isEmpty && content.hasSelection);
    m_selection->setTagText(content.selectionTag);
    m_selection->setLinkText(content.canRename ? tr("Rename") : QString());
    if (PDFKeyValueList* values = m_selection->getValues())
    {
        if (values->getRowCount() != int(content.selectionRows.size()))
        {
            values->clear();
            for (const auto& [key, value] : content.selectionRows)
            {
                values->addRow(key, value);
            }
        }
        else
        {
            for (size_t i = 0; i < content.selectionRows.size(); ++i)
            {
                if (values->getKey(int(i)) != content.selectionRows[i].first)
                {
                    values->clear();
                    for (const auto& [key, value] : content.selectionRows)
                    {
                        values->addRow(key, value);
                    }
                    break;
                }
                values->setValue(int(i), content.selectionRows[i].second);
            }
        }
    }

    m_resultSection->setVisible(!content.isEmpty);
    bool isSameKeys = m_resultValues->getRowCount() == int(content.resultRows.size());
    for (size_t i = 0; isSameKeys && i < content.resultRows.size(); ++i)
    {
        isSameKeys = m_resultValues->getKey(int(i)) == content.resultRows[i].first;
    }
    if (!isSameKeys)
    {
        m_resultValues->clear();
        for (const auto& [key, value] : content.resultRows)
        {
            m_resultValues->addRow(key, value);
        }
    }
    else
    {
        for (size_t i = 0; i < content.resultRows.size(); ++i)
        {
            m_resultValues->setValue(int(i), content.resultRows[i].second);
        }
    }

    const bool hasCallout = !content.isEmpty && !content.calloutTitle.isEmpty();
    const bool isLock = content.calloutHue == PDFAccentHue::Red;
    PDFAppToolCallout* callout = isLock ? m_lockCallout.data() : m_infoCallout.data();
    if (hasCallout)
    {
        callout->setTexts(content.calloutTitle, content.calloutText);
    }
    m_infoCallout->setVisible(hasCallout && !isLock);
    m_lockCallout->setVisible(hasCallout && isLock);
    m_calloutLink->setText(content.calloutLink);
    m_calloutLink->setVisible(!content.calloutLink.isEmpty());

    m_note->setText(content.note);
    m_note->setVisible(!content.isEmpty && !content.note.isEmpty());
}

void PDFAppManageResultPanel::onRenameActivated()
{
    Q_EMIT renameRequested();
}

void PDFAppManageResultPanel::onCalloutLinkClicked()
{
    Q_EMIT calloutLinkActivated();
}

}   // namespace pdf

#include "moc_pdfappmanagepanels.cpp"
