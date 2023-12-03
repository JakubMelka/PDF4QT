//    Copyright (C) 2022 Jakub Melka
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
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFPAGECONTENTEDITORWIDGET_H
#define PDFPAGECONTENTEDITORWIDGET_H

#include "pdfglobal.h"

#include <QDockWidget>
#include <QSignalMapper>

#include <set>

class QToolButton;

namespace Ui
{
class PDFPageContentEditorWidget;
}

namespace pdf
{
class PDFPageContentScene;
class PDFPageContentElement;
class PDFPageContentEditorStyleSettings;

class PDF4QTLIBSHARED_EXPORT PDFPageContentEditorWidget : public QDockWidget
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

private:
    void onActionTriggerRequest(QObject* actionObject);
    void onActionChanged();
    void onItemSelectionChanged();

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
