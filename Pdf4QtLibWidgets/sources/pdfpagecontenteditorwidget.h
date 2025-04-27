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

#ifndef PDFPAGECONTENTEDITORWIDGET_H
#define PDFPAGECONTENTEDITORWIDGET_H

#include "pdfwidgetsglobal.h"
#include "pdfglobal.h"

#include <QDockWidget>
#include <QSignalMapper>

#include <set>

class QToolButton;
class QListWidgetItem;

namespace Ui
{
class PDFPageContentEditorWidget;
}

namespace pdf
{
class PDFPageContentScene;
class PDFPageContentElement;
class PDFPageContentEditorStyleSettings;

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageContentEditorWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit PDFPageContentEditorWidget(QWidget* parent);
    virtual ~PDFPageContentEditorWidget() override;

    /// Adds external action to the tool box
    void addAction(QAction* action);

    QToolButton* getToolButtonForOperation(int operation) const;

    /// Update items in list widget
    void updateItemsInListWidget();

    PDFPageContentScene* scene() const;
    void setScene(PDFPageContentScene* newScene);

    std::set<PDFInteger> getSelection() const;
    void setSelection(const std::set<PDFInteger>& selection);

    /// Loads style from element, element can be nullptr
    /// \param element Element
    void loadStyleFromElement(const PDFPageContentElement* element);

signals:
    void operationTriggered(int operation);
    void itemSelectionChangedByUser();

    void penChanged(const QPen& pen);
    void brushChanged(const QBrush& brush);
    void fontChanged(const QFont& font);
    void alignmentChanged(Qt::Alignment alignment);
    void textAngleChanged(pdf::PDFReal angle);
    void editElementRequest(pdf::PDFInteger elementId);

private:
    void onActionTriggerRequest(QObject* actionObject);
    void onActionChanged();
    void onItemSelectionChanged();
    void onItemDoubleClicked(QListWidgetItem* item);

    Ui::PDFPageContentEditorWidget* ui;
    PDFPageContentEditorStyleSettings* m_settingsWidget;
    QSignalMapper m_actionMapper;
    QSignalMapper m_operationMapper;
    int m_toolBoxColumnCount;
    QSize m_toolButtonIconSize;
    PDFPageContentScene* m_scene;
    bool m_selectionChangeEnabled;
    bool m_updatesEnabled;
};

}   // namespace pdf

#endif // PDFPAGECONTENTEDITORWIDGET_H
