// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#ifndef PDFAPPTOOLIMAGEFORMATMENU_H
#define PDFAPPTOOLIMAGEFORMATMENU_H

#include "pdfappglobal.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>

#include <vector>

class QToolButton;
class QWidget;

namespace pdf
{

class PDFAppToolPanelPopup;
class PDFTileRow;

/// The menu Format of the image exports (PDF to Images 339:4239 / 356:11387, Extract Images
/// 341:7936): the common formats with their descriptions (PNG, JPEG, TIFF, WebP, BMP - those the
/// image writer has), a divider and More formats; More formats shows ALL FORMATS - every format
/// of the writer - in place of the common rows, until the menu closes. The check mark and the
/// text of the button ("Format: PNG") follow setFormat. A click on a format closes the menu and
/// emits formatChosen; the GUI changes its parameter (the menu does not know the model).
///
/// Ownership: the GUI owns the menu (its parent); the panel and the rows belong to the toolbar,
/// the menu holds them by QPointer. The rows and the panel are connected with the menu as the
/// context object.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolImageFormatMenu : public QObject
{
    Q_OBJECT

public:
    /// Fills the rows into the panel (empty, made by PDFAppToolPanelBuilder::addMenu)
    explicit PDFAppToolImageFormatMenu(PDFAppToolPanelPopup* panel, QObject* parent);
    virtual ~PDFAppToolImageFormatMenu() override;

    /// The common formats of the menu (PNG, JPEG, TIFF, WebP, BMP), those the writer has
    static QList<QByteArray> getCommonFormats();

    /// Every format of the writer of ALL FORMATS: the aliases jpg and tif are left out, when
    /// jpeg and tiff are there
    static QList<QByteArray> getAllFormats();

    /// The name of a format ("WebP" of 339:4239, the others in upper case)
    static QString getFormatTitle(const QByteArray& format);

    /// The description of a common format, empty for the others
    static QString getFormatDescription(const QByteArray& format);

    /// The chosen format: the check mark of its rows and the text of the button
    void setFormat(const QByteArray& format);
    const QByteArray& getFormat() const { return m_format; }

    /// More formats: ALL FORMATS in place of the common rows (until the menu closes)
    void showAllFormats();
    bool isAllFormatsShown() const { return m_isAllFormatsShown; }

    /// Parts (nullptr = none)
    PDFAppToolPanelPopup* getPanel() const { return m_panel.data(); }
    QToolButton* getButton() const;

    /// The row of the format, visible now (a common row, or the row of ALL FORMATS)
    PDFTileRow* getRow(const QByteArray& format) const;

Q_SIGNALS:
    /// A format row was clicked; the menu is closed already
    void formatChosen(const QByteArray& format);

private:
    void onRowClicked();
    void onMoreFormatsClicked();
    void onPanelClosed();

    /// The visibility and the check marks of the rows
    void updateRows();

    QPointer<PDFAppToolPanelPopup> m_panel;
    std::vector<QPointer<PDFTileRow>> m_commonRows;
    std::vector<QPointer<PDFTileRow>> m_allRows;
    QPointer<PDFTileRow> m_moreFormatsRow;
    QPointer<QWidget> m_divider;
    QPointer<QWidget> m_allFormatsCaption;
    QByteArray m_format;
    bool m_isAllFormatsShown = false;
};

}   // namespace pdf

#endif // PDFAPPTOOLIMAGEFORMATMENU_H
