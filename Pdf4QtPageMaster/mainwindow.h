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

#ifndef PDFPAGEMASTER_MAINWINDOW_H
#define PDFPAGEMASTER_MAINWINDOW_H

#include "pdficontheme.h"

#include "pageitemmodel.h"
#include "pageitemdelegate.h"

#include <QMainWindow>
#include <QSignalMapper>

namespace Ui
{
class MainWindow;
}

namespace pdfpagemaster
{

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent);
    virtual ~MainWindow() override;

    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;

    QSize getMinPageImageSize() const;
    QSize getDefaultPageImageSize() const;
    QSize getMaxPageImageSize() const;

    enum class Operation
    {
        Clear,
        CloneSelection,
        RemoveSelection,
        ReplaceSelection,
        RestoreRemovedItems,

        Undo,
        Redo,

        Cut,
        Copy,
        Paste,

        RotateLeft,
        RotateRight,

        Group,
        Ungroup,

        SelectNone,
        SelectAll,
        SelectEven,
        SelectOdd,
        SelectPortrait,
        SelectLandscape,
        InvertSelection,

        ZoomIn,
        ZoomOut,

        Unite,
        Separate,
        SeparateGrouped,

        InsertImage,
        InsertEmptyPage,
        InsertPDF,

        RegroupEvenOdd,
        RegroupPaired,
        RegroupOutline,
        RegroupAlternatingPages,
        RegroupAlternatingPagesReversed,
        RegroupReversed,

        GetSource,
        BecomeSponsor,
        About,
        PrepareIconTheme,
        ShowDocumentTitle,
    };

protected:
    virtual void resizeEvent(QResizeEvent* resizeEvent) override;

private slots:
    void on_actionClose_triggered();
    void on_actionAddDocuments_triggered();
    void onMappedActionTriggered(int actionId);
    void onWorkspaceCustomContextMenuRequested(const QPoint& point);
    void updateActions();

private:
    void loadSettings();
    void saveSettings();
    bool insertDocument(const QString& fileName, const QModelIndex& insertIndex);

    bool canPerformOperation(Operation operation) const;
    void performOperation(Operation operation);

    struct Settings
    {
        QString directory;
    };

    Ui::MainWindow* ui;

    pdf::PDFIconTheme m_iconTheme;
    PageItemModel* m_model;
    PageItemDelegate* m_delegate;
    Settings m_settings;
    QSignalMapper m_mapper;
    Qt::DropAction m_dropAction;
};

}   // namespace pdfpagemaster

#endif // PDFPAGEMASTER_MAINWINDOW_H
