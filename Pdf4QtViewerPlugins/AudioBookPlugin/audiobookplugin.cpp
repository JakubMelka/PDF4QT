//    Copyright (C) 2021 Jakub Melka
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

#include "audiobookplugin.h"
#include "pdfdrawwidget.h"
#include "pdfwidgettool.h"
#include "pdfutils.h"
#include "pdfwidgetutils.h"
#include "audiobookcreator.h"

#include <QAction>
#include <QPainter>
#include <QMainWindow>
#include <QMessageBox>
#include <QMouseEvent>
#include <QTableView>
#include <QFileDialog>
#include <QRegularExpression>

namespace pdfplugin
{

AudioBookPlugin::AudioBookPlugin() :
    pdf::PDFPlugin(nullptr),
    m_actionCreateTextStream(nullptr),
    m_actionSynchronizeFromTableToGraphics(nullptr),
    m_actionSynchronizeFromGraphicsToTable(nullptr),
    m_actionActivateSelection(nullptr),
    m_actionDeactivateSelection(nullptr),
    m_actionSelectByRectangle(nullptr),
    m_actionSelectByContainedText(nullptr),
    m_actionSelectByRegularExpression(nullptr),
    m_actionSelectByPageList(nullptr),
    m_actionRestoreOriginalText(nullptr),
    m_actionMoveSelectionUp(nullptr),
    m_actionMoveSelectionDown(nullptr),
    m_actionCreateAudioBook(nullptr),
    m_actionClear(nullptr),
    m_audioTextStreamDockWidget(nullptr),
    m_audioTextStreamEditorModel(nullptr)
{

}

AudioBookPlugin::~AudioBookPlugin()
{

}

void AudioBookPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    m_actionCreateTextStream = new QAction(QIcon(":/pdfplugins/audiobook/create-text-stream.svg"), tr("Create Text Stream for Audio Book"), this);
    m_actionCreateTextStream->setObjectName("actionAudioBook_CreateTextStream");
    connect(m_actionCreateTextStream, &QAction::triggered, this, &AudioBookPlugin::onCreateTextStreamTriggered);

    m_actionSynchronizeFromTableToGraphics = new QAction(QIcon(":/pdfplugins/audiobook/synchronize-from-table-to-graphics.svg"), tr("Synchronize Selection from Table to Graphics"), this);
    m_actionSynchronizeFromTableToGraphics->setObjectName("actionAudioBook_SynchronizeFromTableToGraphics");
    m_actionSynchronizeFromTableToGraphics->setCheckable(true);
    m_actionSynchronizeFromTableToGraphics->setChecked(true);

    m_actionSynchronizeFromGraphicsToTable = new QAction(QIcon(":/pdfplugins/audiobook/synchronize-from-graphics-to-table.svg"), tr("Synchronize Selection from Graphics to Table"), this);
    m_actionSynchronizeFromGraphicsToTable->setObjectName("actionAudioBook_SynchronizeFromGraphicsToTable");
    m_actionSynchronizeFromGraphicsToTable->setCheckable(true);
    m_actionSynchronizeFromGraphicsToTable->setChecked(true);

    m_actionActivateSelection = new QAction(QIcon(":/pdfplugins/audiobook/activate-selection.svg"), tr("Activate Selection"), this);
    m_actionActivateSelection->setObjectName("actionAudioBook_ActivateSelection");
    connect(m_actionActivateSelection, &QAction::triggered, this, &AudioBookPlugin::onActivateSelection);

    m_actionDeactivateSelection = new QAction(QIcon(":/pdfplugins/audiobook/deactivate-selection.svg"), tr("Deactivate Selection"), this);
    m_actionDeactivateSelection->setObjectName("actionAudioBook_DeactivateSelection");
    connect(m_actionDeactivateSelection, &QAction::triggered, this, &AudioBookPlugin::onDeactivateSelection);

    m_actionSelectByRectangle = new QAction(QIcon(":/pdfplugins/audiobook/select-by-rectangle.svg"), tr("Select by Rectangle"), this);
    m_actionSelectByRectangle->setObjectName("actionAudioBook_SelectByRectangle");
    connect(m_actionSelectByRectangle, &QAction::triggered, this, &AudioBookPlugin::onSelectByRectangle);

    m_actionSelectByContainedText = new QAction(QIcon(":/pdfplugins/audiobook/select-by-contained-text.svg"), tr("Select by Contained Text"), this);
    m_actionSelectByContainedText->setObjectName("actionAudioBook_SelectByContainedText");
    connect(m_actionSelectByContainedText, &QAction::triggered, this, &AudioBookPlugin::onSelectByContainedText);

    m_actionSelectByRegularExpression = new QAction(QIcon(":/pdfplugins/audiobook/select-by-regular-expression.svg"), tr("Select by Regular Expression"), this);
    m_actionSelectByRegularExpression->setObjectName("actionAudioBook_SelectByRegularExpression");
    connect(m_actionSelectByRegularExpression, &QAction::triggered, this, &AudioBookPlugin::onSelectByRegularExpression);

    m_actionSelectByPageList = new QAction(QIcon(":/pdfplugins/audiobook/select-by-page-list.svg"), tr("Select by Page List"), this);
    m_actionSelectByPageList->setObjectName("actionAudioBook_SelectByPageList");
    connect(m_actionSelectByPageList, &QAction::triggered, this, &AudioBookPlugin::onSelectByPageList);

    m_actionRestoreOriginalText = new QAction(QIcon(":/pdfplugins/audiobook/restore-original-text.svg"), tr("Restore Original Text"), this);
    m_actionRestoreOriginalText->setObjectName("actionAudioBook_RestoreOriginalText");
    connect(m_actionRestoreOriginalText, &QAction::triggered, this, &AudioBookPlugin::onRestoreOriginalText);

    m_actionMoveSelectionUp = new QAction(QIcon(":/pdfplugins/audiobook/move-selection-up.svg"), tr("Move Selection Up"), this);
    m_actionMoveSelectionUp->setObjectName("actionAudioBook_MoveSelectionUp");
    connect(m_actionMoveSelectionUp, &QAction::triggered, this, &AudioBookPlugin::onMoveSelectionUp);

    m_actionMoveSelectionDown = new QAction(QIcon(":/pdfplugins/audiobook/move-selection-down.svg"), tr("Move Selection Down"), this);
    m_actionMoveSelectionDown->setObjectName("actionAudioBook_MoveSelectionDown");
    connect(m_actionMoveSelectionDown, &QAction::triggered, this, &AudioBookPlugin::onMoveSelectionDown);

    m_actionCreateAudioBook = new QAction(QIcon(":/pdfplugins/audiobook/create-audio-book.svg"), tr("Create Audio Book"), this);
    m_actionCreateAudioBook->setObjectName("actionAudioBook_CreateAudioBook");
    connect(m_actionCreateAudioBook, &QAction::triggered, this, &AudioBookPlugin::onCreateAudioBook);

    m_actionClear = new QAction(QIcon(":/pdfplugins/audiobook/clear.svg"), tr("Clear Text Stream"), this);
    m_actionClear->setObjectName("actionAudioBook_Clear");
    connect(m_actionClear, &QAction::triggered, this, &AudioBookPlugin::onClear);

    m_widget->getDrawWidgetProxy()->registerDrawInterface(this);
    m_widget->addInputInterface(this);

    updateActions();
}

void AudioBookPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        if (m_audioTextStreamEditorModel)
        {
            m_audioTextStreamEditorModel->beginFlowChange();
        }
        m_textFlowEditor.clear();
        if (m_audioTextStreamEditorModel)
        {
            m_audioTextStreamEditorModel->endFlowChange();
        }

        updateActions();
    }
}

std::vector<QAction*> AudioBookPlugin::getActions() const
{
    return { m_actionCreateTextStream,
             m_actionSynchronizeFromTableToGraphics,
             m_actionSynchronizeFromGraphicsToTable,
             m_actionCreateAudioBook,
             m_actionClear };
}

void AudioBookPlugin::drawPage(QPainter* painter,
                               pdf::PDFInteger pageIndex,
                               const pdf::PDFPrecompiledPage* compiledPage,
                               pdf::PDFTextLayoutGetter& layoutGetter,
                               const QTransform& pagePointToDevicePointMatrix,
                               QList<pdf::PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    const qreal width = pdf::PDFWidgetUtils::scaleDPI_x(painter->device(), 1.0);

    QPen pen;
    pen.setWidthF(width);

    auto range = m_textFlowEditor.getItemsForPageIndex(pageIndex);
    for (auto it = range.first; it != range.second; ++it)
    {
        const size_t itemIndex = it->second;
        const pdf::PDFDocumentTextFlowEditor::EditedItem* item = m_textFlowEditor.getEditedItem(itemIndex);

        QRectF boundingRect = item->boundingRect;

        QColor color(Qt::green);
        if (m_textFlowEditor.isSelected(itemIndex))
        {
            color = Qt::yellow;
        }
        else if (m_textFlowEditor.isRemoved(itemIndex))
        {
            color = Qt::red;
        }
        else if (m_textFlowEditor.isModified(itemIndex))
        {
            color = QColor::fromRgb(0xFF, 0xA5, 0, 255);
        }

        QColor strokeColor = color;
        QColor fillColor = color;
        fillColor.setAlphaF(0.2);

        pen.setColor(strokeColor);
        painter->setPen(pen);
        painter->setBrush(QBrush(fillColor));

        QPainterPath path;
        path.addRect(boundingRect);
        path = pagePointToDevicePointMatrix.map(path);

        painter->drawPath(path);
    }
}

void AudioBookPlugin::onCreateTextStreamTriggered()
{
    Q_ASSERT(m_document);

    if (!m_audioTextStreamDockWidget)
    {
        AudioTextStreamActions actions;
        actions.actionCreateTextStream = m_actionCreateTextStream;
        actions.actionSynchronizeFromTableToGraphics = m_actionSynchronizeFromTableToGraphics;
        actions.actionSynchronizeFromGraphicsToTable = m_actionSynchronizeFromGraphicsToTable;
        actions.actionActivateSelection = m_actionActivateSelection;
        actions.actionDeactivateSelection = m_actionDeactivateSelection;
        actions.actionSelectByRectangle = m_actionSelectByRectangle;
        actions.actionSelectByContainedText = m_actionSelectByContainedText;
        actions.actionSelectByRegularExpression = m_actionSelectByRegularExpression;
        actions.actionSelectByPageList = m_actionSelectByPageList;
        actions.actionRestoreOriginalText = m_actionRestoreOriginalText;
        actions.actionMoveSelectionUp = m_actionMoveSelectionUp;
        actions.actionMoveSelectionDown = m_actionMoveSelectionDown;
        actions.actionCreateAudioBook = m_actionCreateAudioBook;
        actions.actionClear = m_actionClear;

        m_audioTextStreamDockWidget = new AudioTextStreamEditorDockWidget(actions, m_dataExchangeInterface->getMainWindow());
        m_audioTextStreamDockWidget->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
        m_dataExchangeInterface->getMainWindow()->addDockWidget(Qt::BottomDockWidgetArea, m_audioTextStreamDockWidget, Qt::Horizontal);
        m_audioTextStreamDockWidget->setFloating(false);

        Q_ASSERT(!m_audioTextStreamEditorModel);
        m_audioTextStreamEditorModel = new pdf::PDFDocumentTextFlowEditorModel(m_audioTextStreamDockWidget);
        m_audioTextStreamEditorModel->setEditor(&m_textFlowEditor);
        m_audioTextStreamDockWidget->setModel(m_audioTextStreamEditorModel);
        connect(m_audioTextStreamDockWidget->getTextStreamView()->selectionModel(), &QItemSelectionModel::selectionChanged, this, &AudioBookPlugin::onTextStreamTableSelectionChanged);
        connect(m_audioTextStreamEditorModel, &pdf::PDFDocumentTextFlowEditorModel::modelReset, this, &AudioBookPlugin::onEditedTextFlowChanged);
        connect(m_audioTextStreamEditorModel, &pdf::PDFDocumentTextFlowEditorModel::dataChanged, this, &AudioBookPlugin::onEditedTextFlowChanged);
    }

    m_audioTextStreamDockWidget->show();

    if (!m_textFlowEditor.isEmpty())
    {
        return;
    }

    pdf::PDFDocumentTextFlowFactory factory;
    factory.setCalculateBoundingBoxes(true);
    pdf::PDFDocumentTextFlow textFlow = factory.create(m_document, pdf::PDFDocumentTextFlowFactory::Algorithm::Auto);

    m_audioTextStreamEditorModel->beginFlowChange();
    m_textFlowEditor.setTextFlow(std::move(textFlow));
    m_audioTextStreamEditorModel->endFlowChange();
}

void AudioBookPlugin::onActivateSelection()
{
    m_audioTextStreamEditorModel->setSelectionActivated(true);
}

void AudioBookPlugin::onDeactivateSelection()
{
    m_audioTextStreamEditorModel->setSelectionActivated(false);
}

void AudioBookPlugin::onSelectByRectangle()
{
    m_widget->getToolManager()->pickRectangle(std::bind(&AudioBookPlugin::onRectanglePicked, this, std::placeholders::_1, std::placeholders::_2));
}

void AudioBookPlugin::onSelectByContainedText()
{
    QString text = m_audioTextStreamDockWidget->getSelectionText();

    if (!text.isEmpty())
    {
        m_audioTextStreamDockWidget->clearSelectionText();
        m_audioTextStreamEditorModel->selectByContainedText(text);
    }
    else
    {
        QMessageBox::critical(m_audioTextStreamDockWidget, tr("Error"), tr("Cannot select items by text, because text is empty."));
    }
}

void AudioBookPlugin::onSelectByRegularExpression()
{
    QString pattern = m_audioTextStreamDockWidget->getSelectionText();

    if (!pattern.isEmpty())
    {
        QRegularExpression expression(pattern);

        if (expression.isValid())
        {
            m_audioTextStreamDockWidget->clearSelectionText();
            m_audioTextStreamEditorModel->selectByRegularExpression(expression);
        }
        else
        {
            QMessageBox::critical(m_audioTextStreamDockWidget, tr("Error"), tr("Regular expression is not valid. %1").arg(expression.errorString()));
        }
    }
    else
    {
        QMessageBox::critical(m_audioTextStreamDockWidget, tr("Error"), tr("Cannot select items by regular expression, because regular expression definition is empty."));
    }
}

void AudioBookPlugin::onSelectByPageList()
{
    QString pageIndicesText = m_audioTextStreamDockWidget->getSelectionText();

    if (!pageIndicesText.isEmpty())
    {
        QString errorMessage;
        auto pageIndices = pdf::PDFClosedIntervalSet::parse(1, m_document->getCatalog()->getPageCount(), pageIndicesText, &errorMessage);

        if (errorMessage.isEmpty())
        {
            m_audioTextStreamDockWidget->clearSelectionText();
            m_audioTextStreamEditorModel->selectByPageIndices(pageIndices);
        }
        else
        {
            QMessageBox::critical(m_audioTextStreamDockWidget, tr("Error"), tr("Cannot select items by page indices, because page indices are invalid. %1").arg(errorMessage));
        }
    }
    else
    {
        QMessageBox::critical(m_audioTextStreamDockWidget, tr("Error"), tr("Cannot select items by page indices, because page indices are empty."));
    }
}

void AudioBookPlugin::onRestoreOriginalText()
{
    if (!m_textFlowEditor.isSelectionModified())
    {
        // Nothing to restore
        return;
    }

    if (QMessageBox::question(m_audioTextStreamDockWidget, tr("Question"), tr("Restore original texts in selected items? All changes will be lost."), QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
    {
        m_audioTextStreamEditorModel->restoreOriginalTexts();
    }
}

void AudioBookPlugin::onEditedTextFlowChanged()
{
    if (m_widget)
    {
        m_widget->getDrawWidget()->getWidget()->update();
    }

    updateActions();
}

void AudioBookPlugin::onTextStreamTableSelectionChanged()
{
    QTableView* tableView = m_audioTextStreamDockWidget->getTextStreamView();
    QModelIndexList indices = tableView->selectionModel()->selectedIndexes();

    if (m_actionSynchronizeFromTableToGraphics->isChecked() && !indices.empty())
    {
        // Jakub Melka: we will find first index, which has valid page number
        for (const QModelIndex& index : indices)
        {
            pdf::PDFInteger pageIndex = m_textFlowEditor.getPageIndex(index.row());

            if (pageIndex >= 0)
            {
                m_widget->getDrawWidgetProxy()->goToPage(pageIndex);
                break;
            }
        }
    }

    m_textFlowEditor.deselect();

    for (const QModelIndex& index : indices)
    {
        m_textFlowEditor.select(index.row(), true);
    }

    m_audioTextStreamEditorModel->notifyDataChanged();
}

void AudioBookPlugin::onClear()
{
    if (m_audioTextStreamEditorModel)
    {
        m_audioTextStreamEditorModel->clear();
    }
}

void AudioBookPlugin::onMoveSelectionUp()
{
    if (m_audioTextStreamEditorModel)
    {
        m_audioTextStreamEditorModel->moveSelectionUp();
        m_audioTextStreamDockWidget->getTextStreamView()->clearSelection();
    }
}

void AudioBookPlugin::onMoveSelectionDown()
{
    if (m_audioTextStreamEditorModel)
    {
        m_audioTextStreamEditorModel->moveSelectionDown();
        m_audioTextStreamDockWidget->getTextStreamView()->clearSelection();
    }
}

void AudioBookPlugin::onCreateAudioBook()
{
    pdf::IPluginDataExchange::VoiceSettings voiceSettings = m_dataExchangeInterface->getVoiceSettings();

    QString fileName = QFileDialog::getSaveFileName(m_widget, tr("Select Audio File"), voiceSettings.directory, tr("Audio stream (*.mp3)"));
    if (fileName.isEmpty())
    {
        return;
    }

    pdf::PDFOperationResult result = true;
    AudioBookCreator audioBookCreator;
    if (audioBookCreator.isInitialized())
    {
        AudioBookCreator::Settings settings;
        settings.audioFileName = fileName;
        settings.voiceName = voiceSettings.voiceName;
        settings.rate = voiceSettings.rate;
        settings.volume = voiceSettings.volume;
        pdf::PDFDocumentTextFlow textFlow = m_textFlowEditor.createEditedTextFlow();
        result = audioBookCreator.createAudioBook(settings, textFlow);
    }
    else
    {
        result = tr("Audio book creator cannot be initialized.");
    }

    if (!result)
    {
        QMessageBox::critical(m_widget, tr("Error"), result.getErrorMessage());
    }
}

void AudioBookPlugin::onRectanglePicked(pdf::PDFInteger pageIndex, QRectF rectangle)
{
    Q_UNUSED(pageIndex);
    m_audioTextStreamEditorModel->selectByRectangle(rectangle);
}

void AudioBookPlugin::updateActions()
{
    m_actionCreateTextStream->setEnabled(m_document);
    m_actionSynchronizeFromTableToGraphics->setEnabled(true);
    m_actionSynchronizeFromGraphicsToTable->setEnabled(true);
    m_actionActivateSelection->setEnabled(!m_textFlowEditor.isSelectionEmpty());
    m_actionDeactivateSelection->setEnabled(!m_textFlowEditor.isSelectionEmpty());
    m_actionSelectByRectangle->setEnabled(!m_textFlowEditor.isEmpty());
    m_actionSelectByContainedText->setEnabled(!m_textFlowEditor.isEmpty());
    m_actionSelectByRegularExpression->setEnabled(!m_textFlowEditor.isEmpty());
    m_actionSelectByPageList->setEnabled(!m_textFlowEditor.isEmpty());
    m_actionRestoreOriginalText->setEnabled(!m_textFlowEditor.isEmpty());
    m_actionMoveSelectionUp->setEnabled(!m_textFlowEditor.isEmpty());
    m_actionMoveSelectionDown->setEnabled(!m_textFlowEditor.isEmpty());
    m_actionCreateAudioBook->setEnabled(!m_textFlowEditor.isEmpty());
    m_actionClear->setEnabled(!m_textFlowEditor.isEmpty());
}

std::optional<size_t> AudioBookPlugin::getItemIndexForPagePoint(QPoint pos) const
{
    QPointF pagePoint;
    pdf::PDFInteger pageIndex = m_widget->getDrawWidgetProxy()->getPageUnderPoint(pos, &pagePoint);
    pdf::PDFDocumentTextFlowEditor::PageIndicesMappingRange itemRange = m_textFlowEditor.getItemsForPageIndex(pageIndex);

    for (auto it = itemRange.first; it != itemRange.second; ++it)
    {
        const pdf::PDFDocumentTextFlowEditor::EditedItem* item = m_textFlowEditor.getEditedItem(it->second);
        if (item->boundingRect.contains(pagePoint))
        {
            return it->second;
        }
    }

    return std::nullopt;
}

void AudioBookPlugin::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void AudioBookPlugin::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    if (m_textFlowEditor.isEmpty())
    {
        // Jakub Melka: do nothing, editor is empty
        return;
    }

    if (event->key() == Qt::Key_Delete)
    {
        m_audioTextStreamEditorModel->setSelectionActivated(event->modifiers().testFlag(Qt::ShiftModifier));
        event->accept();
    }
}

void AudioBookPlugin::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void AudioBookPlugin::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (m_textFlowEditor.isEmpty())
    {
        // Jakub Melka: do nothing, editor is empty
        return;
    }

    if (event->button() == Qt::LeftButton)
    {
        std::optional<size_t> index = getItemIndexForPagePoint(event->pos());
        if (index)
        {
            // Scroll to index, if we are synchronizing
            if (m_actionSynchronizeFromGraphicsToTable->isChecked() && m_audioTextStreamDockWidget)
            {
                m_audioTextStreamDockWidget->goToIndex(*index);
            }

            // Handle selection
            const bool add = event->modifiers() & Qt::ControlModifier;
            const bool remove = event->modifiers() & Qt::ShiftModifier;
            const bool deselect = !add && !remove;

            if (deselect)
            {
                m_textFlowEditor.deselect();
            }

            m_textFlowEditor.select(*index, !remove);

            if (m_audioTextStreamEditorModel)
            {
                m_audioTextStreamEditorModel->notifyDataChanged();
            }
        }
    }
}

void AudioBookPlugin::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void AudioBookPlugin::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void AudioBookPlugin::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (m_textFlowEditor.isEmpty())
    {
        // Jakub Melka: do nothing, editor is empty
        return;
    }

    std::optional<size_t> index = getItemIndexForPagePoint(event->pos());
    if (index)
    {
        m_toolTip = m_textFlowEditor.getText(*index);
    }
    else
    {
        m_toolTip = QString();
    }
}

void AudioBookPlugin::wheelEvent(QWidget* widget, QWheelEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

QString AudioBookPlugin::getTooltip() const
{
    return m_toolTip;
}

const std::optional<QCursor>& AudioBookPlugin::getCursor() const
{
    return m_cursor;
}

int AudioBookPlugin::getInputPriority() const
{
    return UserPriority;
}

}   // namespace pdfplugin
