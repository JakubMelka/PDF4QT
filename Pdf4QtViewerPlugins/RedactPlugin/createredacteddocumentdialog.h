//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef CREATEREDACTEDDOCUMENTDIALOG_H
#define CREATEREDACTEDDOCUMENTDIALOG_H

#include <QDialog>

namespace Ui
{
class CreateRedactedDocumentDialog;
}

namespace pdfplugin
{

class CreateRedactedDocumentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CreateRedactedDocumentDialog(QString fileName, QColor fillColor, QWidget* parent);
    virtual ~CreateRedactedDocumentDialog() override;

    virtual void accept() override;

    QString getFileName() const;
    QColor getRedactColor() const;

    bool isCopyingTitle() const;
    bool isCopyingMetadata() const;
    bool isCopyingOutline() const;

private slots:
    void on_selectDirectoryButton_clicked();

private:
    void updateUi();

    Ui::CreateRedactedDocumentDialog* ui;
};

} // namespace pdfplugin

#endif // CREATEREDACTEDDOCUMENTDIALOG_H
