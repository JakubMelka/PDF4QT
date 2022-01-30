//    Copyright (C) 2020-2022 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfwidgettool.h"
#include "pdfdrawwidget.h"
#include "pdfcompiler.h"
#include "pdfwidgetutils.h"
#include "pdfdbgheap.h"

#include <QLabel>
#include <QAction>
#include <QCheckBox>
#include <QLineEdit>
#include <QGridLayout>
#include <QPushButton>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QClipboard>
#include <QApplication>

namespace pdf
{

PDFWidgetTool::PDFWidgetTool(PDFDrawWidgetProxy* proxy, QObject* parent) :
    BaseClass(parent),
    m_active(false),
    m_document(nullptr),
    m_action(nullptr),
    m_proxy(proxy)
{

}

PDFWidgetTool::PDFWidgetTool(PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent) :
    BaseClass(parent),
    m_active(false),
    m_document(nullptr),
    m_action(action),
    m_proxy(proxy)
{
    updateActions();
}

PDFWidgetTool::~PDFWidgetTool()
{

}

void PDFWidgetTool::setDocument(const PDFModifiedDocument& document)
{
    if (m_document != document)
    {
        // We must turn off the tool, if we are changing the document. We turn off tool,
        // only if whole document is being reset.
        if (document.hasReset())
        {
            setActive(false);
        }

        m_document = document;

        for (PDFWidgetTool* tool : m_toolStack)
        {
            tool->setDocument(document);
        }

        updateActions();
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
        updateActions();

        m_proxy->repaintNeeded();
        emit toolActivityChanged(active);
    }
}

void PDFWidgetTool::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    if (PDFWidgetTool* tool = getTopToolstackTool())
    {
        tool->keyPressEvent(widget, event);
    }
}

void PDFWidgetTool::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    if (PDFWidgetTool* tool = getTopToolstackTool())
    {
        tool->keyReleaseEvent(widget, event);
    }
}

void PDFWidgetTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    if (PDFWidgetTool* tool = getTopToolstackTool())
    {
        tool->mousePressEvent(widget, event);
    }
}

void PDFWidgetTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    if (PDFWidgetTool* tool = getTopToolstackTool())
    {
        tool->mouseReleaseEvent(widget, event);
    }
}

void PDFWidgetTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    if (PDFWidgetTool* tool = getTopToolstackTool())
    {
        tool->mouseMoveEvent(widget, event);
    }
}

void PDFWidgetTool::wheelEvent(QWidget* widget, QWheelEvent* event)
{
    if (PDFWidgetTool* tool = getTopToolstackTool())
    {
        tool->wheelEvent(widget, event);
    }
}

const std::optional<QCursor>& PDFWidgetTool::getCursor() const
{
    // If we have active subtool, return its mouse cursor
    if (PDFWidgetTool* tool = getTopToolstackTool())
    {
        return tool->getCursor();
    }

    return m_cursor;
}

void PDFWidgetTool::setActiveImpl(bool active)
{
    for (PDFWidgetTool* tool : m_toolStack)
    {
        tool->setActive(active);
    }
}

void PDFWidgetTool::updateActions()
{
    if (m_action)
    {
        m_action->setChecked(isActive());
        m_action->setEnabled(m_document);
    }
}

PDFWidgetTool* PDFWidgetTool::getTopToolstackTool() const
{
    if (!m_toolStack.empty())
    {
        return m_toolStack.back();
    }

    return nullptr;
}

void PDFWidgetTool::addTool(PDFWidgetTool* tool)
{
    m_toolStack.push_back(tool);
}

void PDFWidgetTool::removeTool()
{
    m_toolStack.pop_back();
}

PDFFindTextTool::PDFFindTextTool(PDFDrawWidgetProxy* proxy, QAction* prevAction, QAction* nextAction, QObject* parent, QWidget* parentDialog) :
    BaseClass(proxy, parent),
    m_prevAction(prevAction),
    m_nextAction(nextAction),
    m_parentDialog(parentDialog),
    m_dialog(nullptr),
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
                               const QMatrix& pagePointToDevicePointMatrix,
                               QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(errors);

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
    BaseClass::setActiveImpl(active);

    if (active)
    {
        Q_ASSERT(!m_dialog);

        // For find, we will need text layout
        getProxy()->getTextLayoutCompiler()->makeTextLayout();

        // Create dialog
        m_dialog = new QDialog(m_parentDialog, Qt::Popup | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
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

        m_previousButton->setDefault(false);
        m_nextButton->setDefault(false);

        m_previousButton->setShortcut(m_prevAction->shortcut());
        m_nextButton->setShortcut(m_nextAction->shortcut());

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
        m_dialog->setFocus();
        m_findTextEdit->setFocus();
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
    if (!isActive())
    {
        return;
    }

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
    BaseClass::updateActions();

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

PDFSelectTextTool::PDFSelectTextTool(PDFDrawWidgetProxy* proxy, QAction* action, QAction* copyTextAction, QAction* selectAllAction, QAction* deselectAction, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_copyTextAction(copyTextAction),
    m_selectAllAction(selectAllAction),
    m_deselectAction(deselectAction),
    m_isCursorOverText(false)
{
    connect(copyTextAction, &QAction::triggered, this, &PDFSelectTextTool::onActionCopyText);
    connect(selectAllAction, &QAction::triggered, this, &PDFSelectTextTool::onActionSelectAll);
    connect(deselectAction, &QAction::triggered, this, &PDFSelectTextTool::onActionDeselect);

    updateActions();
}

void PDFSelectTextTool::drawPage(QPainter* painter,
                                 PDFInteger pageIndex,
                                 const PDFPrecompiledPage* compiledPage,
                                 PDFTextLayoutGetter& layoutGetter,
                                 const QMatrix& pagePointToDevicePointMatrix,
                                 QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(errors);

    pdf::PDFTextSelectionPainter textSelectionPainter(&m_textSelection);
    textSelectionPainter.draw(painter, pageIndex, layoutGetter, pagePointToDevicePointMatrix);
}

void PDFSelectTextTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::LeftButton)
    {
        QPointF pagePoint;
        const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
        if (pageIndex != -1)
        {
            m_selectionInfo.pageIndex = pageIndex;
            m_selectionInfo.selectionStartPoint = pagePoint;
            event->accept();
        }
        else
        {
            m_selectionInfo = SelectionInfo();
        }

        setSelection(pdf::PDFTextSelection());
        updateCursor();
    }
}

void PDFSelectTextTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::LeftButton)
    {
        if (m_selectionInfo.pageIndex != -1)
        {
            QPointF pagePoint;
            const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);

            if (m_selectionInfo.pageIndex == pageIndex)
            {
                // Jakub Melka: handle the selection
                PDFTextLayout textLayout = getProxy()->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);
                setSelection(textLayout.createTextSelection(pageIndex, m_selectionInfo.selectionStartPoint, pagePoint));
            }
            else
            {
                setSelection(pdf::PDFTextSelection());
            }

            m_selectionInfo = SelectionInfo();
            event->accept();
            updateCursor();
        }
    }
}

void PDFSelectTextTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    // We must make text layout. This is fast, because text layout is being
    // created only, if it doesn't exist. This function is also called only,
    // if tool is active.
    getProxy()->getTextLayoutCompiler()->makeTextLayout();

    QPointF pagePoint;
    const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
    PDFTextLayout textLayout = getProxy()->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);
    m_isCursorOverText = textLayout.isHoveringOverTextBlock(pagePoint);

    if (m_selectionInfo.pageIndex != -1)
    {
        if (m_selectionInfo.pageIndex == pageIndex)
        {
            // Jakub Melka: handle the selection
            setSelection(textLayout.createTextSelection(pageIndex, m_selectionInfo.selectionStartPoint, pagePoint));
        }
        else
        {
            setSelection(pdf::PDFTextSelection());
        }

        event->accept();
    }

    updateCursor();
}

void PDFSelectTextTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (active)
    {
        pdf::PDFAsynchronousTextLayoutCompiler* compiler = getProxy()->getTextLayoutCompiler();
        if (!compiler->isTextLayoutReady())
        {
            compiler->makeTextLayout();
        }
    }
    else
    {
        // Just clear the text selection
        setSelection(PDFTextSelection());
    }
}

void PDFSelectTextTool::updateActions()
{
    BaseClass::updateActions();

    const bool isActive = this->isActive();
    const bool hasSelection = !m_textSelection.isEmpty();
    m_selectAllAction->setEnabled(isActive);
    m_deselectAction->setEnabled(isActive && hasSelection);
    m_copyTextAction->setEnabled(isActive && hasSelection);
}

void PDFSelectTextTool::updateCursor()
{
    if (isActive())
    {
        if (m_isCursorOverText)
        {
            setCursor(QCursor(Qt::IBeamCursor));
        }
        else
        {
            setCursor(QCursor(Qt::ArrowCursor));
        }
    }
}

void PDFSelectTextTool::onActionCopyText()
{
    if (isActive())
    {
        // Jakub Melka: we must obey document permissions
        if (getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::CopyContent))
        {
            QStringList result;

            auto it = m_textSelection.begin();
            auto itEnd = m_textSelection.nextPageRange(it);
            while (it != m_textSelection.end())
            {
                const PDFInteger pageIndex = it->start.pageIndex;
                PDFTextLayout textLayout = getProxy()->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);
                result << textLayout.getTextFromSelection(it, itEnd, pageIndex);

                it = itEnd;
                itEnd = m_textSelection.nextPageRange(it);
            }

            QString text = result.join("\n\n");
            if (!text.isEmpty())
            {
                QApplication::clipboard()->setText(text, QClipboard::Clipboard);
            }
        }
    }
}

void PDFSelectTextTool::onActionSelectAll()
{
    if (isActive())
    {
        setSelection(getProxy()->getTextLayoutCompiler()->getTextSelectionAll(Qt::yellow));
    }
}

void PDFSelectTextTool::onActionDeselect()
{
    if (isActive())
    {
        setSelection(pdf::PDFTextSelection());
    }
}

void PDFSelectTextTool::setSelection(PDFTextSelection&& textSelection)
{
    if (m_textSelection != textSelection)
    {
        m_textSelection = qMove(textSelection);
        getProxy()->repaintNeeded();
        updateActions();
    }
}

PDFToolManager::PDFToolManager(PDFDrawWidgetProxy* proxy, Actions actions, QObject* parent, QWidget* parentDialog) :
    BaseClass(parent),
    m_predefinedTools()
{
    auto pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_predefinedTools[PickRectangleTool] = pickTool;
    m_predefinedTools[FindTextTool] = new PDFFindTextTool(proxy, actions.findPrevAction, actions.findNextAction, this, parentDialog);
    m_predefinedTools[SelectTextTool] = new PDFSelectTextTool(proxy, actions.selectTextToolAction, actions.copyTextAction, actions.selectAllAction, actions.deselectAction, this);
    m_predefinedTools[MagnifierTool] = new PDFMagnifierTool(proxy, actions.magnifierAction, this);
    m_predefinedTools[ScreenshotTool] = new PDFScreenshotTool(proxy, actions.screenshotToolAction, this);
    m_predefinedTools[ExtractImageTool] = new PDFExtractImageTool(proxy, actions.extractImageAction, this);

    for (PDFWidgetTool* tool : m_predefinedTools)
    {
        addTool(tool);
    }

    connect(pickTool, &PDFPickTool::rectanglePicked, this, &PDFToolManager::onRectanglePicked);
}

void PDFToolManager::addTool(PDFWidgetTool* tool)
{
    m_tools.insert(tool);
    connect(tool, &PDFWidgetTool::messageDisplayRequest, this, &PDFToolManager::messageDisplayRequest);

    if (QAction* action = tool->getAction())
    {
        m_actionsToTools[action] = tool;
        connect(action, &QAction::triggered, this, &PDFToolManager::onToolActionTriggered);
    }

    connect(tool, &PDFWidgetTool::toolActivityChanged, this, &PDFToolManager::onToolActivityChanged);
}

void PDFToolManager::pickRectangle(std::function<void (PDFInteger, QRectF)> callback)
{
    setActiveTool(nullptr);
    m_pickRectangleCallback = callback;
    setActiveTool(m_predefinedTools[PickRectangleTool]);
}

void PDFToolManager::setDocument(const PDFModifiedDocument& document)
{
    for (PDFWidgetTool* tool : m_tools)
    {
        tool->setDocument(document);
    }
}

void PDFToolManager::setActiveTool(PDFWidgetTool* tool)
{
    PDFWidgetTool* activeTool = getActiveTool();
    if (activeTool && activeTool != tool)
    {
        activeTool->setActive(false);
    }

    Q_ASSERT(!getActiveTool());

    if (tool)
    {
        tool->setActive(true);
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

PDFMagnifierTool* PDFToolManager::getMagnifierTool() const
{
    return qobject_cast<PDFMagnifierTool*>(m_predefinedTools[MagnifierTool]);
}

void PDFToolManager::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFToolManager::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    event->ignore();

    // Escape key cancels current tool
    PDFWidgetTool* activeTool = getActiveTool();
    if (event->key() == Qt::Key_Escape && activeTool)
    {
        activeTool->setActive(false);
        event->accept();
        return;
    }

    if (activeTool)
    {
        activeTool->keyPressEvent(widget, event);
    }
}

void PDFToolManager::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    event->ignore();

    if (PDFWidgetTool* activeTool = getActiveTool())
    {
        activeTool->keyReleaseEvent(widget, event);
    }
}

void PDFToolManager::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    event->ignore();

    if (PDFWidgetTool* activeTool = getActiveTool())
    {
        activeTool->mousePressEvent(widget, event);
    }
}

void PDFToolManager::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFToolManager::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    event->ignore();

    if (PDFWidgetTool* activeTool = getActiveTool())
    {
        activeTool->mouseReleaseEvent(widget, event);
    }
}

void PDFToolManager::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    event->ignore();

    if (PDFWidgetTool* activeTool = getActiveTool())
    {
        activeTool->mouseMoveEvent(widget, event);
    }
}

void PDFToolManager::wheelEvent(QWidget* widget, QWheelEvent* event)
{
    event->ignore();

    if (PDFWidgetTool* activeTool = getActiveTool())
    {
        activeTool->wheelEvent(widget, event);
    }
}

const std::optional<QCursor>& PDFToolManager::getCursor() const
{
    if (PDFWidgetTool* tool = getActiveTool())
    {
        return tool->getCursor();
    }

    static const std::optional<QCursor> dummy;
    return dummy;
}

void PDFToolManager::onToolActivityChanged(bool active)
{
    PDFWidgetTool* tool = qobject_cast<PDFWidgetTool*>(sender());

    if (active)
    {
        // When tool is activated outside, we must deactivate old active tool
        for (PDFWidgetTool* currentTool : m_tools)
        {
            if (currentTool->isActive() && currentTool != tool)
            {
                currentTool->setActive(false);
            }
        }
    }
    else
    {
        // Clear callback, if we are deactivating a tool
        if (tool == m_predefinedTools[PickRectangleTool])
        {
            m_pickRectangleCallback = nullptr;
        }
    }
}

void PDFToolManager::onToolActionTriggered(bool checked)
{
    PDFWidgetTool* tool = m_actionsToTools.at(qobject_cast<QAction*>(sender()));
    if (checked)
    {
        setActiveTool(tool);
    }
    else
    {
        tool->setActive(false);
    }
}

void PDFToolManager::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    if (m_pickRectangleCallback)
    {
        m_pickRectangleCallback(pageIndex, pageRectangle);
    }

    setActiveTool(nullptr);
}

PDFMagnifierTool::PDFMagnifierTool(PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_magnifierSize(200),
    m_magnifierZoom(2.0)
{
    setCursor(Qt::BlankCursor);
}

void PDFMagnifierTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();
}

void PDFMagnifierTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();
}

void PDFMagnifierTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();
    QPoint mousePos = event->pos();
    if (m_mousePos != mousePos)
    {
        m_mousePos = mousePos;
        getProxy()->repaintNeeded();
    }
}

void PDFMagnifierTool::drawPostRendering(QPainter* painter, QRect rect) const
{
    if (!m_mousePos.isNull())
    {
        QPainterPath path;
        path.addEllipse(m_mousePos, m_magnifierSize, m_magnifierSize);

        painter->save();

        // Clip the painter path for magnifier
        painter->setClipPath(path, Qt::IntersectClip);
        painter->fillRect(rect, getProxy()->getPaperColor());

        painter->scale(m_magnifierZoom, m_magnifierZoom);

        // Jakub Melka: this must be explained. We want to display the origin (mouse position)
        // at the same position to remain under scaling. If scale == 1, then we translate
        // by -m_mousePos + m_mousePos = (0, 0). Otherwise we are m_mousePos / scale away
        // from the original position. Example:
        //      m_mousePos = (100, 100), scale = 2
        //      we are translating by -(100, 100) + (50, 50) = -(50, 50),
        // because origin at (100, 100) is now at position (50, 50) after scale. So, if it has to remain
        // the same, we must translate by -(50, 50).
        painter->translate(m_mousePos * (1.0 / m_magnifierZoom - 1.0));
        getProxy()->drawPages(painter, rect, getProxy()->getFeatures());
        painter->restore();

        painter->setPen(Qt::black);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    }
}

void PDFMagnifierTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (!active)
    {
        m_mousePos = QPoint();
    }
}

PDFReal PDFMagnifierTool::getMagnifierZoom() const
{
    return m_magnifierZoom;
}

void PDFMagnifierTool::setMagnifierZoom(const PDFReal& magnifierZoom)
{
    m_magnifierZoom = magnifierZoom;
}

int PDFMagnifierTool::getMagnifierSize() const
{
    return m_magnifierSize;
}

void PDFMagnifierTool::setMagnifierSize(int magnifierSize)
{
    m_magnifierSize = magnifierSize;
}

PDFPickTool::PDFPickTool(PDFDrawWidgetProxy* proxy, PDFPickTool::Mode mode, QObject* parent) :
    BaseClass(proxy, parent),
    m_mode(mode),
    m_pageIndex(-1),
    m_drawSelectionRectangle(true),
    m_selectionRectangleColor(Qt::blue)
{
    setCursor((m_mode == Mode::Images) ? Qt::CrossCursor : Qt::BlankCursor);
    m_snapper.setSnapPointPixelSize(PDFWidgetUtils::scaleDPI_x(proxy->getWidget(), 10));
    m_snapper.setSnapPointTolerance(m_snapper.getSnapPointPixelSize());

    m_selectionRectangleColor.setAlphaF(0.25);

    connect(proxy, &PDFDrawWidgetProxy::drawSpaceChanged, this, &PDFPickTool::buildSnapData);
    connect(proxy, &PDFDrawWidgetProxy::pageImageChanged, this, &PDFPickTool::buildSnapData);
}

void PDFPickTool::drawPage(QPainter* painter,
                           PDFInteger pageIndex,
                           const PDFPrecompiledPage* compiledPage,
                           PDFTextLayoutGetter& layoutGetter,
                           const QMatrix& pagePointToDevicePointMatrix,
                           QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    // If we are picking rectangles, then draw current selection rectangle
    if (m_mode == Mode::Rectangles && m_drawSelectionRectangle && m_pageIndex == pageIndex && !m_pickedPoints.empty())
    {
        QPoint p1 = pagePointToDevicePointMatrix.map(m_pickedPoints.back()).toPoint();
        QPoint p2 = m_snapper.getSnappedPoint().toPoint();

        int xMin = qMin(p1.x(), p2.x());
        int xMax = qMax(p1.x(), p2.x());
        int yMin = qMin(p1.y(), p2.y());
        int yMax = qMax(p1.y(), p2.y());

        QRect selectionRectangle(xMin, yMin, xMax - xMin, yMax - yMin);
        if (selectionRectangle.isValid())
        {
            painter->fillRect(selectionRectangle, m_selectionRectangleColor);
        }
    }

    if (m_mode == Mode::Images && m_snapper.getSnappedImage())
    {
        const PDFSnapper::ViewportSnapImage* snappedImage = m_snapper.getSnappedImage();
        painter->fillPath(snappedImage->viewportPath, m_selectionRectangleColor);
    }
}

void PDFPickTool::drawPostRendering(QPainter* painter, QRect rect) const
{
    if (m_mode != Mode::Images)
    {
        m_snapper.drawSnapPoints(painter);

        QPoint snappedPoint = m_snapper.getSnappedPoint().toPoint();
        QPoint hleft = snappedPoint;
        QPoint hright = snappedPoint;
        QPoint vtop = snappedPoint;
        QPoint vbottom = snappedPoint;

        hleft.setX(0);
        hright.setX(rect.width());
        vtop.setY(0);
        vbottom.setY(rect.height());

        painter->setPen(Qt::black);
        painter->drawLine(hleft, hright);
        painter->drawLine(vtop, vbottom);
    }
}

void PDFPickTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();

    if (event->button() == Qt::LeftButton)
    {
        if (m_mode != Mode::Images)
        {
            // Try to perform pick point
            QPointF pagePoint;
            PDFInteger pageIndex = getProxy()->getPageUnderPoint(m_snapper.getSnappedPoint().toPoint(), &pagePoint);
            if (pageIndex != -1 &&    // We have picked some point on page
                    (m_pageIndex == -1 || m_pageIndex == pageIndex)) // We are under current page
            {
                m_pageIndex = pageIndex;
                m_pickedPoints.push_back(pagePoint);
                m_snapper.setReferencePoint(pageIndex, pagePoint);

                // Emit signal about picked point
                emit pointPicked(pageIndex, pagePoint);

                if (m_mode == Mode::Rectangles && m_pickedPoints.size() == 2)
                {
                    QPointF first = m_pickedPoints.front();
                    QPointF second = m_pickedPoints.back();

                    const qreal xMin = qMin(first.x(), second.x());
                    const qreal xMax = qMax(first.x(), second.x());
                    const qreal yMin = qMin(first.y(), second.y());
                    const qreal yMax = qMax(first.y(), second.y());

                    QRectF pageRectangle(xMin, yMin, xMax - xMin, yMax - yMin);
                    emit rectanglePicked(pageIndex, pageRectangle);

                    // We must reset tool, to pick next rectangle
                    resetTool();
                }

                buildSnapData();
                emit getProxy()->repaintNeeded();
            }
        }
        else
        {
            // Try to perform pick image
            if (const PDFSnapper::ViewportSnapImage* snappedImage = m_snapper.getSnappedImage())
            {
                emit imagePicked(snappedImage->image);
            }
        }
    }
    else if (event->button() == Qt::RightButton && m_mode != Mode::Images)
    {
        // Reset tool to enable new picking (right button means reset the tool)
        resetTool();
    }
}

void PDFPickTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();
}

void PDFPickTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();
    QPoint mousePos = event->pos();
    if (m_mousePosition != mousePos)
    {
        m_mousePosition = mousePos;
        m_snapper.updateSnappedPoint(m_mousePosition);
        emit getProxy()->repaintNeeded();
    }
}

QPointF PDFPickTool::getSnappedPoint() const
{
    return m_snapper.getSnappedPoint();
}

void PDFPickTool::setCustomSnapPoints(PDFInteger pageIndex, const std::vector<QPointF>& snapPoints)
{
    if (m_pageIndex == pageIndex)
    {
        m_snapper.setCustomSnapPoints(snapPoints);
    }
}

void PDFPickTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (active)
    {
        buildSnapData();
    }
    else
    {
        // Reset tool to reinitialize it for future use. If tool
        // is activated, then it should be in initial state.
        resetTool();
        m_snapper.clear();
    }
}

void PDFPickTool::resetTool()
{
    m_pickedPoints.clear();
    m_pageIndex = -1;
    m_snapper.clearReferencePoint();

    buildSnapData();
    getProxy()->repaintNeeded();
}

void PDFPickTool::buildSnapData()
{
    if (!isActive())
    {
        return;
    }

    if (m_mode == Mode::Images)
    {
        // Snap images
        m_snapper.buildSnapImages(getProxy()->getSnapshot());
    }
    else
    {
        // Snap points
        m_snapper.buildSnapPoints(getProxy()->getSnapshot());
    }
}

QColor PDFPickTool::getSelectionRectangleColor() const
{
    return m_selectionRectangleColor;
}

void PDFPickTool::setSelectionRectangleColor(QColor selectionRectangleColor)
{
    m_selectionRectangleColor = selectionRectangleColor;
}

void PDFPickTool::setDrawSelectionRectangle(bool drawSelectionRectangle)
{
    m_drawSelectionRectangle = drawSelectionRectangle;
}

PDFScreenshotTool::PDFScreenshotTool(PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_pickTool(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFScreenshotTool::onRectanglePicked);
}

void PDFScreenshotTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    PDFWidgetSnapshot snapshot = getProxy()->getSnapshot();
    if (const PDFWidgetSnapshot::SnapshotItem* pageSnapshot = snapshot.getPageSnapshot(pageIndex))
    {
        QRect selectedRectangle = pageSnapshot->pageToDeviceMatrix.mapRect(pageRectangle).toRect();
        if (selectedRectangle.isValid())
        {
            QImage image(selectedRectangle.size(), QImage::Format_RGB888);

            {
                QPainter painter(&image);
                painter.translate(-selectedRectangle.topLeft());
                getProxy()->drawPages(&painter, getProxy()->getWidget()->rect(), getProxy()->getFeatures() | PDFRenderer::DenyExtraGraphics);
            }

            QApplication::clipboard()->setImage(image, QClipboard::Clipboard);
            emit messageDisplayRequest(tr("Page contents of size %1 x %2 pixels were copied to the clipboard.").arg(image.width()).arg(image.height()), 5000);
        }
    }
}

PDFExtractImageTool::PDFExtractImageTool(PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_pickTool(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Images, this);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::imagePicked, this, &PDFExtractImageTool::onImagePicked);
}

void PDFExtractImageTool::updateActions()
{
    // Jakub Melka: We do not call base class implementation here, because
    // we must verify we have right to extract content (this tool extracts content)

    if (QAction* action = getAction())
    {
        action->setChecked(isActive());
        action->setEnabled(getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::CopyContent));
    }
}

void PDFExtractImageTool::onImagePicked(const QImage& image)
{
    if (!image.isNull())
    {
        QApplication::clipboard()->setImage(image, QClipboard::Clipboard);
        emit messageDisplayRequest(tr("Image of size %1 x %2 pixels was copied to the clipboard.").arg(image.width()).arg(image.height()), 5000);
    }
}

}   // namespace pdf
