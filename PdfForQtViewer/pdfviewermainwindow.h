//    Copyright (C) 2019 Jakub Melka
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

#ifndef PDFVIEWERMAINWINDOW_H
#define PDFVIEWERMAINWINDOW_H

#include <QMainWindow>
#include <QSharedPointer>

namespace Ui
{
class PDFViewerMainWindow;
}

namespace pdf
{
class PDFWidget;
class PDFDocument;
}

namespace pdfviewer
{

class PDFViewerMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit PDFViewerMainWindow(QWidget *parent = nullptr);
    virtual ~PDFViewerMainWindow() override;

    virtual void closeEvent(QCloseEvent* event) override;

private:
    void onActionOpenTriggered();
    void onActionCloseTriggered();
    void onActionQuitTriggered();

    void readSettings();
    void writeSettings();

    void updateTitle();

    void openDocument(const QString& fileName);
    void setDocument(const pdf::PDFDocument* document);
    void closeDocument();

    Ui::PDFViewerMainWindow* ui;
    pdf::PDFWidget* m_pdfWidget;
    QSharedPointer<pdf::PDFDocument> m_pdfDocument;
    QString m_directory;
    QString m_currentFile;
};

}   // namespace pdfviewer

#endif // PDFVIEWERMAINWINDOW_H
