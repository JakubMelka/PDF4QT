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

#include "pdfappdocumentsearchpanel.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsearchcontroller.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumentview.h"
#include "pdfappworkspace.h"

#include "pdfcomponentdelegates.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLocale>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the search panel
class PDFAppDocumentSearchPanelHelper
{
public:
    PDFAppDocumentSearchPanelHelper() = delete;

    /// Button of the header (search-previous / next / close 198:4921: 32 x 32,
    /// radius 8, icon 18)
    static QToolButton* createHeaderButton(const QString& objectName, const QString& iconName, QWidget* parent)
    {
        QToolButton* button = new QToolButton(parent);
        button->setObjectName(objectName);
        button->setAutoRaise(true);
        button->setIcon(PDFThemeIcons::getIcon(iconName));
        button->setIconSize(QSize(18, 18));
        button->setFixedSize(32, 32);
        PDFWidgetStyleProperties::set(button, PDFWidgetStyleProperties::RADIUS, 8);
        return button;
    }

    /// Option chip (option-match-case 198:4935: 134 x 32, an on / off option)
    static QPushButton* createOptionButton(const QString& objectName, const QString& iconName, QWidget* parent)
    {
        QPushButton* button = new QPushButton(parent);
        button->setObjectName(objectName);
        button->setCheckable(true);
        button->setAutoDefault(false);
        button->setIcon(PDFThemeIcons::getIcon(iconName));
        button->setIconSize(QSize(14, 14));
        button->setFixedSize(PDFAppDocumentSearchPanel::OPTION_WIDTH, 32);
        PDFWidgetStyleProperties::set(button, PDFWidgetStyleProperties::CHIP);
        PDFWidgetStyleProperties::set(button, PDFWidgetStyleProperties::OPTION);
        return button;
    }
};

PDFAppDocumentSearchPanel::PDFAppDocumentSearchPanel(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("searchPanel"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // sidebar-header 198:4566: the title and the subtitle, previous / next / close
    QWidget* header = new QWidget(this);
    header->setObjectName(QStringLiteral("searchHeader"));
    header->setMinimumHeight(PDFAppDocumentSidebar::HEADER_MINIMUM_HEIGHT);
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(PDFAppDocumentSidebar::HEADER_PADDING_LEFT, PDFAppDocumentSidebar::HEADER_PADDING_TOP,
                                     PDFAppDocumentSidebar::HEADER_PADDING_RIGHT, PDFAppDocumentSidebar::HEADER_PADDING_BOTTOM);
    headerLayout->setSpacing(PDFAppDocumentSidebar::HEADER_GAP);

    QVBoxLayout* titleLayout = new QVBoxLayout();
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(1);
    m_title = new PDFAppHeaderText(PDFThemeFonts::getFont(QFont::DemiBold, 14), PDFThemeColors::TextPrimary, header);
    m_title->setObjectName(QStringLiteral("searchTitle"));
    m_subtitle = new PDFAppHeaderText(PDFThemeFonts::getFont(QFont::Normal, 11), PDFThemeColors::TextMuted, header);
    m_subtitle->setObjectName(QStringLiteral("searchSubtitle"));
    titleLayout->addWidget(m_title);
    titleLayout->addWidget(m_subtitle);
    headerLayout->addLayout(titleLayout, 1);

    m_previousButton = PDFAppDocumentSearchPanelHelper::createHeaderButton(QStringLiteral("searchPrevious"), QStringLiteral("match-prev"), header);
    m_nextButton = PDFAppDocumentSearchPanelHelper::createHeaderButton(QStringLiteral("searchNext"), QStringLiteral("match-next"), header);
    m_closeButton = PDFAppDocumentSearchPanelHelper::createHeaderButton(QStringLiteral("searchClose"), QStringLiteral("x"), header);
    headerLayout->addWidget(m_previousButton);
    headerLayout->addWidget(m_nextButton);
    headerLayout->addWidget(m_closeButton);
    connect(m_previousButton, &QToolButton::clicked, this, &PDFAppDocumentSearchPanel::onPreviousClicked);
    connect(m_nextButton, &QToolButton::clicked, this, &PDFAppDocumentSearchPanel::onNextClicked);
    connect(m_closeButton, &QToolButton::clicked, this, &PDFAppDocumentSearchPanel::onCloseClicked);
    layout->addWidget(header);

    // sidebar-content 198:4933: paddings 12 / 2 / 12 / 16; search-options 198:4934:
    // two columns of chips 134 x 32, gaps 6, 8 below
    QWidget* content = new QWidget(this);
    content->setObjectName(QStringLiteral("searchContent"));
    QVBoxLayout* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(12, 2, 12, 16);
    contentLayout->setSpacing(0);

    QGridLayout* optionsLayout = new QGridLayout();
    optionsLayout->setContentsMargins(0, 0, 0, 8);
    optionsLayout->setHorizontalSpacing(6);
    optionsLayout->setVerticalSpacing(6);
    m_matchCaseButton = PDFAppDocumentSearchPanelHelper::createOptionButton(QStringLiteral("optionMatchCase"), QStringLiteral("match-case"), content);
    m_wholeWordsButton = PDFAppDocumentSearchPanelHelper::createOptionButton(QStringLiteral("optionWholeWords"), QStringLiteral("whole-word"), content);
    m_regexButton = PDFAppDocumentSearchPanelHelper::createOptionButton(QStringLiteral("optionRegex"), QStringLiteral("regex"), content);
    m_wildcardsButton = PDFAppDocumentSearchPanelHelper::createOptionButton(QStringLiteral("optionWildcards"), QStringLiteral("wildcard"), content);
    optionsLayout->addWidget(m_matchCaseButton, 0, 0);
    optionsLayout->addWidget(m_wholeWordsButton, 0, 1);
    optionsLayout->addWidget(m_regexButton, 1, 0);
    optionsLayout->addWidget(m_wildcardsButton, 1, 1);
    optionsLayout->setColumnStretch(2, 1);
    for (QPushButton* button : { m_matchCaseButton.data(), m_wholeWordsButton.data(), m_regexButton.data(), m_wildcardsButton.data() })
    {
        connect(button, &QPushButton::toggled, this, &PDFAppDocumentSearchPanel::onOptionToggled);
    }
    contentLayout->addLayout(optionsLayout);

    m_messageLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextMuted, content);
    m_messageLabel->setObjectName(QStringLiteral("searchMessage"));
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setContentsMargins(10, 8, 10, 8);
    m_messageLabel->setTextFormat(Qt::PlainText);
    contentLayout->addWidget(m_messageLabel);

    // The occurrences (Search result row 191:1073, the spacing of the list 2)
    m_listView = new PDFListView(content);
    m_listView->setObjectName(QStringLiteral("searchResults"));
    PDFAppPanelWidgets::prepareView(m_listView);
    m_listView->setSpacing(1);
    m_listView->setUniformItemSizes(false);
    m_listView->setLayoutMode(QListView::Batched);
    m_listView->setBatchSize(200);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_delegate = new PDFSearchResultDelegate(m_listView);
    m_listView->setItemDelegate(m_delegate);
    contentLayout->addWidget(m_listView, 1);

    m_noteLabel = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, content);
    m_noteLabel->setObjectName(QStringLiteral("searchNote"));
    m_noteLabel->setWordWrap(true);
    m_noteLabel->setContentsMargins(10, 8, 10, 0);
    contentLayout->addWidget(m_noteLabel);

    layout->addWidget(content, 1);

    retranslate();
    updateState();
}

PDFAppDocumentSearchPanel::~PDFAppDocumentSearchPanel() = default;

QString PDFAppDocumentSearchPanel::getId() const
{
    return PDFAppDocumentPanelIds::search();
}

QString PDFAppDocumentSearchPanel::getTitle() const
{
    return tr("Search results");
}

QString PDFAppDocumentSearchPanel::getSubtitle() const
{
    if (!m_controller || m_controller->getQuery().isEmpty())
    {
        return tr("Type a text into the search field");
    }

    const QString query = m_controller->getQuery().simplified().left(64);
    switch (m_controller->getState())
    {
        case PDFAppDocumentSearchController::State::Indexing:
            return tr("Indexing the text… %1 %").arg(m_controller->getIndexProgress());

        case PDFAppDocumentSearchController::State::Searching:
            return tr("Searching for “%1”…").arg(query);

        case PDFAppDocumentSearchController::State::Failed:
            return tr("The search text is not valid");

        case PDFAppDocumentSearchController::State::Finished:
        {
            const int count = m_controller->getResultCount();
            if (count == 0)
            {
                return tr("No results for “%1”").arg(query);
            }
            return count == 1 ? tr("1 result for “%1”").arg(query) : tr("%1 results for “%2”").arg(QLocale().toString(count), query);
        }

        case PDFAppDocumentSearchController::State::Idle:
            break;
    }

    return tr("The search was stopped");
}

QString PDFAppDocumentSearchPanel::getIconName() const
{
    return QStringLiteral("search");
}

bool PDFAppDocumentSearchPanel::hasOwnHeader() const
{
    return true;
}

void PDFAppDocumentSearchPanel::setDocument(PDFAppDocument* document, PDFAppDocumentView* view)
{
    BaseClass::setDocument(document, view);

    if (m_controller)
    {
        m_controller->disconnect(this);
    }

    PDFAppDocumentSearchCommandHandler* handler = PDFAppDocumentSearchCommandHandler::findHandler(view ? static_cast<QObject*>(view) : static_cast<QObject*>(document));
    m_controller = handler ? handler->getSearchController() : nullptr;
    m_listView->setModel(m_controller ? m_controller->getModel() : nullptr);
    if (m_controller)
    {
        connect(m_controller, &PDFAppDocumentSearchController::stateChanged, this, &PDFAppDocumentSearchPanel::updateState);
        connect(m_controller, &PDFAppDocumentSearchController::currentIndexChanged, this, &PDFAppDocumentSearchPanel::onCurrentIndexChanged);
    }
    if (m_listView->selectionModel())
    {
        connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged, this, &PDFAppDocumentSearchPanel::onCurrentRowChanged, Qt::UniqueConnection);
    }

    updateOptions();
    updateState();
}

PDFListView* PDFAppDocumentSearchPanel::getListView() const
{
    return m_listView.data();
}

QPushButton* PDFAppDocumentSearchPanel::getOptionButton(int index) const
{
    switch (index)
    {
        case 0:
            return m_matchCaseButton.data();
        case 1:
            return m_wholeWordsButton.data();
        case 2:
            return m_regexButton.data();
        case 3:
            return m_wildcardsButton.data();
        default:
            break;
    }
    return nullptr;
}

QToolButton* PDFAppDocumentSearchPanel::getPreviousButton() const
{
    return m_previousButton.data();
}

QToolButton* PDFAppDocumentSearchPanel::getNextButton() const
{
    return m_nextButton.data();
}

QToolButton* PDFAppDocumentSearchPanel::getCloseButton() const
{
    return m_closeButton.data();
}

PDFLabel* PDFAppDocumentSearchPanel::getMessageLabel() const
{
    return m_messageLabel.data();
}

PDFLabel* PDFAppDocumentSearchPanel::getNoteLabel() const
{
    return m_noteLabel.data();
}

void PDFAppDocumentSearchPanel::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
        updateState();
    }
}

void PDFAppDocumentSearchPanel::retranslate()
{
    m_previousButton->setToolTip(tr("Previous result (Shift+F3)"));
    m_previousButton->setAccessibleName(tr("Previous result"));
    m_nextButton->setToolTip(tr("Next result (F3)"));
    m_nextButton->setAccessibleName(tr("Next result"));
    m_closeButton->setToolTip(tr("Close the search (Esc)"));
    m_closeButton->setAccessibleName(tr("Close the search"));
    m_matchCaseButton->setText(tr("Match case"));
    m_wholeWordsButton->setText(tr("Whole words"));
    m_regexButton->setText(tr("Regex"));
    m_regexButton->setToolTip(tr("The text is a regular expression"));
    m_wildcardsButton->setText(tr("Wildcards"));
    m_wildcardsButton->setToolTip(tr("* matches any characters of a word, ? one character"));
    m_listView->setAccessibleName(tr("Search results"));
}

void PDFAppDocumentSearchPanel::updateOptions()
{
    const PDFDocumentTextSearchOptions options = m_controller ? m_controller->getOptions() : PDFDocumentTextSearchOptions();
    m_isUpdating = true;
    m_matchCaseButton->setChecked(options.caseSensitive);
    m_wholeWordsButton->setChecked(options.wholeWords);
    m_regexButton->setChecked(options.regularExpression);
    m_wildcardsButton->setChecked(options.wildcards && !options.regularExpression);
    m_isUpdating = false;
}

void PDFAppDocumentSearchPanel::updateState()
{
    QPointer<PDFAppDocumentSearchPanel> guard(this);

    const QString title = getTitle();
    const QString subtitle = getSubtitle();
    m_title->setHeaderText(title);
    m_subtitle->setHeaderText(subtitle);

    const int count = m_controller ? m_controller->getResultCount() : 0;
    m_previousButton->setEnabled(count > 0);
    m_nextButton->setEnabled(count > 0);
    m_closeButton->setEnabled(m_controller != nullptr);

    QString message;
    if (m_controller && m_controller->getState() == PDFAppDocumentSearchController::State::Failed)
    {
        message = m_controller->getError();
    }
    else if (!m_controller || m_controller->getQuery().isEmpty())
    {
        message = tr("Type a text into the search field above the document and press Enter.");
    }
    else if (m_controller->getState() == PDFAppDocumentSearchController::State::Finished && count == 0)
    {
        message = tr("Check the spelling, or turn off Match case and Whole words.");
    }
    m_messageLabel->setText(message);
    m_messageLabel->setVisible(!message.isEmpty());

    const bool isTruncated = m_controller && m_controller->isTruncated();
    m_noteLabel->setText(isTruncated ? tr("Only the first %1 results are shown. Search for a longer text to see the others.")
                                       .arg(QLocale().toString(qlonglong(PDFAppDocumentSearchController::MAXIMUM_RESULTS)))
                                     : QString());
    m_noteLabel->setVisible(isTruncated);

    Q_EMIT titleChanged();
    if (!guard)
    {
        return;
    }

    if (m_controller)
    {
        onCurrentIndexChanged(m_controller->getCurrentIndex());
    }
}

void PDFAppDocumentSearchPanel::onOptionToggled(bool checked)
{
    if (m_isUpdating || !m_controller)
    {
        return;
    }

    // Regex and wildcards are two syntaxes of the text: one of them at most
    QPushButton* button = qobject_cast<QPushButton*>(sender());
    if (checked && button == m_regexButton)
    {
        m_isUpdating = true;
        m_wildcardsButton->setChecked(false);
        m_isUpdating = false;
    }
    else if (checked && button == m_wildcardsButton)
    {
        m_isUpdating = true;
        m_regexButton->setChecked(false);
        m_isUpdating = false;
    }

    PDFDocumentTextSearchOptions options;
    options.caseSensitive = m_matchCaseButton->isChecked();
    options.wholeWords = m_wholeWordsButton->isChecked();
    options.regularExpression = m_regexButton->isChecked();
    options.wildcards = m_wildcardsButton->isChecked();
    m_controller->setOptions(options);
}

void PDFAppDocumentSearchPanel::onPreviousClicked()
{
    if (m_controller)
    {
        m_controller->goToPrevious();
    }
}

void PDFAppDocumentSearchPanel::onNextClicked()
{
    if (m_controller)
    {
        m_controller->goToNext();
    }
}

void PDFAppDocumentSearchPanel::onCloseClicked()
{
    // The handler restores the panel shown before the search (this panel may be
    // hidden by it, it is not deleted)
    if (PDFAppDocumentSearchCommandHandler* handler = PDFAppDocumentSearchCommandHandler::findHandler(getView()))
    {
        handler->closeSearch();
    }
    else if (m_controller)
    {
        m_controller->clear();
    }
}

void PDFAppDocumentSearchPanel::onCurrentRowChanged(const QModelIndex& current, const QModelIndex& previous)
{
    Q_UNUSED(previous);
    if (!m_isUpdating && m_controller && current.isValid() && current.row() != m_controller->getCurrentIndex())
    {
        m_controller->setCurrentIndex(current.row());
    }
}

void PDFAppDocumentSearchPanel::onCurrentIndexChanged(int index)
{
    if (!m_listView || !m_listView->model())
    {
        return;
    }

    const QModelIndex modelIndex = m_listView->model()->index(index, 0);
    m_isUpdating = true;
    if (modelIndex.isValid())
    {
        m_listView->setCurrentIndex(modelIndex);
        m_listView->scrollTo(modelIndex);
    }
    else
    {
        m_listView->clearSelection();
    }
    m_isUpdating = false;
}

}   // namespace pdf
