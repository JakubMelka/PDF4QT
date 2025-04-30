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

#ifndef AUDIOBOOKPLUGIN_H
#define AUDIOBOOKPLUGIN_H

#include "pdfplugin.h"
#include "pdfdocumenttextflow.h"
#include "pdfdocumenttextfloweditormodel.h"
#include "pdfdocumentdrawinterface.h"
#include "audiotextstreameditordockwidget.h"

#include <QObject>

namespace pdfplugin
{

class AudioBookPlugin : public pdf::PDFPlugin,
                        public pdf::IDocumentDrawInterface,
                        public pdf::IDrawWidgetInputInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "PDF4QT.AudioBookPlugin" FILE "AudioBookPlugin.json")

private:
    using BaseClass = pdf::PDFPlugin;

public:
    AudioBookPlugin();
    virtual ~AudioBookPlugin() override;

    virtual void setWidget(pdf::PDFWidget* widget) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual std::vector<QAction*> getActions() const override;
    virtual QString getPluginMenuName() const override;

    virtual void drawPage(QPainter* painter,
                          pdf::PDFInteger pageIndex,
                          const pdf::PDFPrecompiledPage* compiledPage,
                          pdf::PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const pdf::PDFColorConvertor& convertor,
                          QList<pdf::PDFRenderError>& errors) const override;

    // IDrawWidgetInputInterface interface
public:
    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event) override;
    virtual QString getTooltip() const override;
    virtual const std::optional<QCursor>& getCursor() const override;
    virtual int getInputPriority() const override;

private:
    void onCreateTextStreamTriggered();
    void onActivateSelection();
    void onDeactivateSelection();
    void onSelectByRectangle();
    void onSelectByContainedText();
    void onSelectByRegularExpression();
    void onSelectByPageList();
    void onRestoreOriginalText();
    void onEditedTextFlowChanged();
    void onTextStreamTableSelectionChanged();
    void onClear();
    void onMoveSelectionUp();
    void onMoveSelectionDown();
    void onCreateAudioBook();

    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF rectangle);

    void updateActions();

    std::optional<size_t> getItemIndexForPagePoint(QPoint pos) const;

    QAction* m_actionCreateTextStream;
    QAction* m_actionSynchronizeFromTableToGraphics;
    QAction* m_actionSynchronizeFromGraphicsToTable;
    QAction* m_actionActivateSelection;
    QAction* m_actionDeactivateSelection;
    QAction* m_actionSelectByRectangle;
    QAction* m_actionSelectByContainedText;
    QAction* m_actionSelectByRegularExpression;
    QAction* m_actionSelectByPageList;
    QAction* m_actionRestoreOriginalText;
    QAction* m_actionMoveSelectionUp;
    QAction* m_actionMoveSelectionDown;
    QAction* m_actionCreateAudioBook;
    QAction* m_actionClear;

    pdf::PDFDocumentTextFlowEditor m_textFlowEditor;
    AudioTextStreamEditorDockWidget* m_audioTextStreamDockWidget;
    pdf::PDFDocumentTextFlowEditorModel* m_audioTextStreamEditorModel;

    QString m_toolTip;
    std::optional<QCursor> m_cursor;
};

}   // namespace pdfplugin

#endif // AUDIOBOOKPLUGIN_H
