//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfwidgettool.h"
#include "pdfdrawwidget.h"
#include "pdfcompiler.h"

#include <QLabel>
#include <QAction>
#include <QCheckBox>
#include <QLineEdit>
#include <QGridLayout>
#include <QPushButton>

namespace pdf
{

PDFWidgetTool::PDFWidgetTool(PDFDrawWidgetProxy* proxy, QObject* parent) :
    BaseClass(parent),
    m_active(false),
    m_document(nullptr),
    m_proxy(proxy)
{

}

PDFWidgetTool::~PDFWidgetTool()
{

}

void PDFWidgetTool::drawPage(QPainter* painter,
                             PDFInteger pageIndex,
                             const PDFPrecompiledPage* compiledPage,
                             PDFTextLayoutGetter& layoutGetter,
                             const QMatrix& pagePointToDevicePointMatrix) const
{
    for (PDFWidgetTool* tool : m_toolStack)
    {
        tool->drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix);
    }
}

void PDFWidgetTool::setDocument(const PDFDocument* document)
{
    if (m_document != document)
    {
        // We must turn off the tool, if we are changing the document
        setActive(false);
        m_document = document;
    }
}

void PDFWidgetTool::setActive(bool active)
{
    if (m_active != active)
    {
        m_active = active;

        if (active)
        {
            m_proxy->registerDrawInterface(this);
        }
        else
        {
            m_proxy->unregisterDrawInterface(this);
        }

        setActiveImpl(active);

        m_proxy->repaintNeeded();
        emit toolActivityChanged(active);
    }
}

void PDFWidgetTool::setActiveImpl(bool active)
{
    Q_UNUSED(active);
}

PDFFindTextTool::PDFFindTextTool(PDFDrawWidgetProxy* proxy, QAction* prevAction, QAction* nextAction, QObject* parent, QWidget* parentDialog) :
    BaseClass(proxy, parent),
    m_prevAction(prevAction),
    m_nextAction(nextAction),
    m_dialog(nullptr),
    m_parentDialog(parentDialog),
    m_caseSensitiveCheckBox(nullptr),
    m_wholeWordsCheckBox(nullptr),
    m_findTextEdit(nullptr),
    m_previousButton(nullptr),
    m_nextButton(nullptr),
    m_selectedResultIndex(0)
{
    PDFAsynchronousTextLayoutCompiler* compiler = getProxy()->getTextLayoutCompiler();
    connect(compiler, &PDFAsynchronousTextLayoutCompiler::textLayoutChanged, this, &PDFFindTextTool::performSearch);
    connect(m_prevAction, &QAction::triggered, this, &PDFFindTextTool::onActionPrevious);
    connect(m_nextAction, &QAction::triggered, this, &PDFFindTextTool::onActionNext);

    updateActions();
}

void PDFFindTextTool::drawPage(QPainter* painter,
                               PDFInteger pageIndex,
                               const PDFPrecompiledPage* compiledPage,
                               PDFTextLayoutGetter& layoutGetter,
                               const QMatrix& pagePointToDevicePointMatrix) const
{
    Q_UNUSED(compiledPage);

    const pdf::PDFTextSelection& textSelection = getTextSelection();
    pdf::PDFTextSelectionPainter textSelectionPainter(&textSelection);
    textSelectionPainter.draw(painter, pageIndex, layoutGetter, pagePointToDevicePointMatrix);
}

void PDFFindTextTool::clearResults()
{
    m_findResults.clear();
    m_selectedResultIndex = 0;
    m_textSelection.dirty();
}

void PDFFindTextTool::setActiveImpl(bool active)
{
    if (active)
    {
        Q_ASSERT(!m_dialog);

        // For find, we will need text layout
        getProxy()->getTextLayoutCompiler()->makeTextLayout();

        // Create dialog
        m_dialog = new QDialog(m_parentDialog, Qt::Tool);
        m_dialog->setWindowTitle(tr("Find"));

        QGridLayout* layout = new QGridLayout(m_dialog);
        m_dialog->setLayout(layout);

        // Jakub Melka: we will create following widgets:
        //  - text with label
        //  - line edit, where user can enter search text
        //  - 2 checkbox for settings
        //  - 2 push buttons (previous/next)

        m_findTextEdit = new QLineEdit(m_dialog);
        m_caseSensitiveCheckBox = new QCheckBox(tr("Case sensitive"), m_dialog);
        m_wholeWordsCheckBox = new QCheckBox(tr("Whole words only"), m_dialog);
        m_previousButton = new QPushButton(tr("Previous"), m_dialog);
        m_nextButton = new QPushButton(tr("Next"), m_dialog);

        connect(m_previousButton, &QPushButton::clicked, m_prevAction, &QAction::trigger);
        connect(m_nextButton, &QPushButton::clicked, m_nextAction, &QAction::trigger);
        connect(m_findTextEdit, &QLineEdit::editingFinished, this, &PDFFindTextTool::onSearchText);
        connect(m_caseSensitiveCheckBox, &QCheckBox::clicked, this, &PDFFindTextTool::onSearchText);
        connect(m_wholeWordsCheckBox, &QCheckBox::clicked, this, &PDFFindTextTool::onSearchText);

        layout->addWidget(new QLabel(tr("Search text"), m_dialog), 0, 0, 1, -1, Qt::AlignLeft);
        layout->addWidget(m_findTextEdit, 1, 0, 1, -1);
        layout->addWidget(m_caseSensitiveCheckBox, 2, 0, 1, -1, Qt::AlignLeft);
        layout->addWidget(m_wholeWordsCheckBox, 3, 0, 1, -1, Qt::AlignLeft);
        layout->addWidget(m_previousButton, 4, 0);
        layout->addWidget(m_nextButton, 4, 1);
        m_dialog->setFixedSize(m_dialog->sizeHint());

        PDFWidget* widget = getProxy()->getWidget();
        QPoint topRight = widget->mapToGlobal(widget->rect().topRight());
        QPoint topRightParent = m_parentDialog->mapFromGlobal(topRight);

        m_dialog->show();
        m_dialog->move(topRightParent - QPoint(m_dialog->width() * 1.1, 0));
        connect(m_dialog, &QDialog::rejected, this, [this] { setActive(false); });
    }
    else
    {
        Q_ASSERT(m_dialog);
        m_dialog->deleteLater();
        m_dialog = nullptr;
        m_caseSensitiveCheckBox = nullptr;
        m_wholeWordsCheckBox = nullptr;
        m_findTextEdit = nullptr;
        m_previousButton = nullptr;
        m_nextButton = nullptr;

        clearResults();
    }
}

void PDFFindTextTool::onSearchText()
{
    m_parameters.phrase = m_findTextEdit->text();
    m_parameters.isCaseSensitive = m_caseSensitiveCheckBox->isChecked();
    m_parameters.isWholeWordsOnly = m_wholeWordsCheckBox->isChecked();
    m_parameters.isSearchFinished = m_parameters.phrase.isEmpty();

    m_findResults.clear();
    m_textSelection.dirty();
    updateResultsUI();

    if (m_parameters.isSearchFinished)
    {
        // We have nothing to search for
        return;
    }

    pdf::PDFAsynchronousTextLayoutCompiler* compiler = getProxy()->getTextLayoutCompiler();
    if (compiler->isTextLayoutReady())
    {
        performSearch();
    }
    else
    {
        compiler->makeTextLayout();
    }
}

void PDFFindTextTool::onActionPrevious()
{
    if (!m_findResults.empty())
    {
        if (m_selectedResultIndex == 0)
        {
            m_selectedResultIndex = m_findResults.size() - 1;
        }
        else
        {
            --m_selectedResultIndex;
        }
        m_textSelection.dirty();
        getProxy()->repaintNeeded();
        getProxy()->goToPage(m_findResults[m_selectedResultIndex].textSelectionItems.front().first.pageIndex);
        updateTitle();
    }
}

void PDFFindTextTool::onActionNext()
{
    if (!m_findResults.empty())
    {
        m_selectedResultIndex = (m_selectedResultIndex + 1) % m_findResults.size();
        m_textSelection.dirty();
        getProxy()->repaintNeeded();
        getProxy()->goToPage(m_findResults[m_selectedResultIndex].textSelectionItems.front().first.pageIndex);
        updateTitle();
    }
}

void PDFFindTextTool::performSearch()
{
    if (m_parameters.isSearchFinished)
    {
        return;
    }

    clearResults();
    m_parameters.isSearchFinished = true;

    if (m_parameters.phrase.isEmpty())
    {
        return;
    }

    PDFAsynchronousTextLayoutCompiler* compiler = getProxy()->getTextLayoutCompiler();
    if (!compiler->isTextLayoutReady())
    {
        // Text layout is not ready yet
        return;
    }

    // Prepare string to search
    QString expression = m_parameters.phrase;

    bool useRegularExpression = false;
    if (m_parameters.isWholeWordsOnly)
    {
        expression = QString("\\b%1\\b").arg(QRegularExpression::escape(expression));
        useRegularExpression = true;
    }

    pdf::PDFTextFlow::FlowFlags flowFlags = pdf::PDFTextFlow::SeparateBlocks;

    const pdf::PDFTextLayoutStorage* textLayoutStorage = compiler->getTextLayoutStorage();
    if (!useRegularExpression)
    {
        // Use simple text search
        Qt::CaseSensitivity caseSensitivity = m_parameters.isCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        m_findResults = textLayoutStorage->find(expression, caseSensitivity, flowFlags);
    }
    else
    {
        // Use regular expression search
        QRegularExpression::PatternOptions patternOptions = QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::OptimizeOnFirstUsageOption;
        if (!m_parameters.isCaseSensitive)
        {
            patternOptions |= QRegularExpression::CaseInsensitiveOption;
        }

        QRegularExpression regularExpression(expression, patternOptions);
        m_findResults = textLayoutStorage->find(regularExpression, flowFlags);
    }

    std::sort(m_findResults.begin(), m_findResults.end());
    m_selectedResultIndex = 0;
    m_textSelection.dirty();
    getProxy()->repaintNeeded();

    updateResultsUI();
}

void PDFFindTextTool::updateActions()
{
    const bool isActive = this->isActive();
    const bool hasResults = !m_findResults.empty();
    const bool enablePrevious = isActive && hasResults;
    const bool enableNext = isActive && hasResults;

    m_prevAction->setEnabled(enablePrevious);
    m_nextAction->setEnabled(enableNext);
}

void PDFFindTextTool::updateResultsUI()
{
    m_selectedResultIndex = qBound(size_t(0), m_selectedResultIndex, m_findResults.size());

    updateActions();
    updateTitle();
}

void PDFFindTextTool::updateTitle()
{
    if (!m_dialog)
    {
        return;
    }

    if (m_findResults.empty())
    {
        m_dialog->setWindowTitle(tr("Find"));
    }
    else
    {
        m_dialog->setWindowTitle(tr("Find (%1/%2)").arg(m_selectedResultIndex + 1).arg(m_findResults.size()));
    }
}

PDFTextSelection PDFFindTextTool::getTextSelectionImpl() const
{
    pdf::PDFTextSelection result;

    for (size_t i = 0; i < m_findResults.size(); ++i)
    {
        const pdf::PDFFindResult& findResult = m_findResults[i];

        QColor color(Qt::blue);
        if (i == m_selectedResultIndex)
        {
            color = QColor(Qt::yellow);
        }

        result.addItems(findResult.textSelectionItems, color);
    }
    result.build();

    return result;
}

PDFToolManager::PDFToolManager(PDFDrawWidgetProxy* proxy, QAction* findPreviousAction, QAction* findNextAction, QObject* parent, QWidget* parentDialog) :
    BaseClass(parent),
    m_predefinedTools()
{
    m_predefinedTools[FindTextTool] = new PDFFindTextTool(proxy, findPreviousAction, findNextAction, this, parentDialog);

    for (PDFWidgetTool* tool : m_predefinedTools)
    {
        m_tools.insert(tool);
    }
}

void PDFToolManager::setDocument(const PDFDocument* document)
{
    for (PDFWidgetTool* tool : m_tools)
    {
        tool->setDocument(document);
    }
}

PDFWidgetTool* PDFToolManager::getActiveTool() const
{
    for (PDFWidgetTool* tool : m_tools)
    {
        if (tool->isActive())
        {
            return tool;
        }
    }

    return nullptr;
}

PDFFindTextTool* PDFToolManager::getFindTextTool() const
{
    return qobject_cast<PDFFindTextTool*>(m_predefinedTools[FindTextTool]);
}

}   // namespace pdf
