// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef AUDIOTEXTSTREAMEDITORDOCKWIDGET_H
#define AUDIOTEXTSTREAMEDITORDOCKWIDGET_H

#include "pdfdocumenttextfloweditormodel.h"

#include <QDockWidget>

class QToolBar;
class QLineEdit;
class QTableView;

namespace Ui
{
class AudioTextStreamEditorDockWidget;
}

namespace pdfplugin
{

struct AudioTextStreamActions
{
    QAction* actionCreateTextStream = nullptr;
    QAction* actionSynchronizeFromTableToGraphics = nullptr;
    QAction* actionSynchronizeFromGraphicsToTable = nullptr;
    QAction* actionActivateSelection = nullptr;
    QAction* actionDeactivateSelection = nullptr;
    QAction* actionSelectByRectangle = nullptr;
    QAction* actionSelectByContainedText = nullptr;
    QAction* actionSelectByRegularExpression = nullptr;
    QAction* actionSelectByPageList = nullptr;
    QAction* actionRestoreOriginalText = nullptr;
    QAction* actionMoveSelectionUp = nullptr;
    QAction* actionMoveSelectionDown = nullptr;
    QAction* actionCreateAudioBook = nullptr;
    QAction* actionClear = nullptr;
};

class AudioTextStreamEditorDockWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit AudioTextStreamEditorDockWidget(AudioTextStreamActions actions, QWidget* parent);
    virtual ~AudioTextStreamEditorDockWidget() override;

    pdf::PDFDocumentTextFlowEditorModel* getModel() const;
    void setModel(pdf::PDFDocumentTextFlowEditorModel* model);

    QToolBar* getToolBar() const { return m_toolBar; }
    QTableView* getTextStreamView() const;

    QString getSelectionText() const;
    void clearSelectionText();

    void goToIndex(size_t index);

private:
    Ui::AudioTextStreamEditorDockWidget* ui;
    pdf::PDFDocumentTextFlowEditorModel* m_model;
    QToolBar* m_toolBar;
    QLineEdit* m_selectionTextEdit;
};

} // namespace pdfplugin

#endif // AUDIOTEXTSTREAMEDITORDOCKWIDGET_H
