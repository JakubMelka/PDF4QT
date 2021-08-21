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

#ifndef AUDIOTEXTSTREAMEDITORDOCKWIDGET_H
#define AUDIOTEXTSTREAMEDITORDOCKWIDGET_H

#include "pdfdocumenttextfloweditormodel.h"

#include <QDockWidget>

class QToolBar;
class QLineEdit;

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

    QString getSelectionText() const;
    void clearSelectionText();

private:
    Ui::AudioTextStreamEditorDockWidget* ui;
    pdf::PDFDocumentTextFlowEditorModel* m_model;
    QToolBar* m_toolBar;
    QLineEdit* m_selectionTextEdit;
};

} // namespace pdfplugin

#endif // AUDIOTEXTSTREAMEDITORDOCKWIDGET_H
