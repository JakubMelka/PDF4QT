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

#ifndef PDFDOCPAGEORGANIZER_MAINWINDOW_H
#define PDFDOCPAGEORGANIZER_MAINWINDOW_H

#include "pdficontheme.h"

#include "pageitemmodel.h"
#include "pageitemdelegate.h"

#include <QMainWindow>
#include <QSignalMapper>

namespace Ui
{
class MainWindow;
}

namespace pdfdocpage
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
        RegroupBookmarks,
        RegroupAlternatingPages,
        RegroupAlternatingPagesReversed,

        GetSource,
        About,
        PrepareIconTheme
    };

private slots:
    void on_actionClose_triggered();
    void on_actionAddDocuments_triggered();
    void onMappedActionTriggered(int actionId);
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

}   // namespace pdfdocpage

#endif // PDFDOCPAGEORGANIZER_MAINWINDOW_H
