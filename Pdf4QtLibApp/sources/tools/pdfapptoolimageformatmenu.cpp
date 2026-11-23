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

#include "pdfapptoolimageformatmenu.h"

#include "pdfappdocumentmenus.h"
#include "pdfapptoolpanelpopup.h"

#include "pdfcomponentrows.h"
#include "pdfdeferreddeletion.h"
#include "pdfdocumenttool_pdftoimages.h"
#include "pdffluentwidgets.h"

#include <QSignalBlocker>
#include <QToolButton>

namespace pdf
{

/// Helpers of the menu Format
class PDFAppToolImageFormatMenuHelper
{
public:
    PDFAppToolImageFormatMenuHelper() = delete;

    /// The property of a row: its format
    static constexpr const char* FORMAT_PROPERTY = "pdf4qtImageFormat";

    /// The check marks of the rows of the format (no signal of the rows)
    static void setChecked(const std::vector<QPointer<PDFTileRow>>& rows, const QByteArray& format, bool isVisible)
    {
        for (const QPointer<PDFTileRow>& row : rows)
        {
            if (row)
            {
                row->setVisible(isVisible);
                const QSignalBlocker blocker(row);
                row->setChecked(row->property(FORMAT_PROPERTY).toByteArray() == format);
            }
        }
    }

    /// A row of a format
    static PDFTileRow* addRow(PDFAppToolPanelPopup* panel, const QByteArray& format, const QString& description, const QString& namePrefix)
    {
        PDFTileRow* row = panel->addRow(PDFAppToolImageFormatMenu::getFormatTitle(format), description, QStringLiteral("image"), PDFTileRow::Kind::MenuItem);
        row->setObjectName(namePrefix + QString::fromLatin1(format));
        row->setProperty(FORMAT_PROPERTY, format);
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        return row;
    }
};

PDFAppToolImageFormatMenu::PDFAppToolImageFormatMenu(PDFAppToolPanelPopup* panel, QObject* parent) :
    QObject(parent),
    m_panel(panel)
{
    if (!panel)
    {
        return;
    }

    // 339:4239, 341:7936: PNG, JPEG, TIFF, WebP, BMP, a divider, More formats
    for (const QByteArray& format : getCommonFormats())
    {
        PDFTileRow* row = PDFAppToolImageFormatMenuHelper::addRow(panel, format, getFormatDescription(format), QStringLiteral("toolFormat-"));
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolImageFormatMenu::onRowClicked);
        m_commonRows.push_back(row);
    }
    m_divider = panel->addDividerRow();
    m_moreFormatsRow = panel->addRow(tr("More formats"), tr("Every format the application can write"), QStringLiteral("more-horizontal"), PDFTileRow::Kind::MenuItem);
    m_moreFormatsRow->setObjectName(QStringLiteral("toolFormat-more"));
    connect(m_moreFormatsRow.data(), &PDFTileRow::clicked, this, &PDFAppToolImageFormatMenu::onMoreFormatsClicked);

    // 356:11387: ALL FORMATS - every format of the writer
    m_allFormatsCaption = panel->addGroupCaption(tr("ALL FORMATS"));
    for (const QByteArray& format : getAllFormats())
    {
        PDFTileRow* row = PDFAppToolImageFormatMenuHelper::addRow(panel, format, QString(), QStringLiteral("toolAllFormat-"));
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolImageFormatMenu::onRowClicked);
        m_allRows.push_back(row);
    }
    PDFDeferredDeletion::connect(panel, &PDFAppDocumentPanelPopup::closed, this, &PDFAppToolImageFormatMenu::onPanelClosed);
    updateRows();
}

PDFAppToolImageFormatMenu::~PDFAppToolImageFormatMenu() = default;

QList<QByteArray> PDFAppToolImageFormatMenu::getCommonFormats()
{
    QList<QByteArray> formats;
    const QList<QByteArray> supported = PDFDocumentToolImageFormat::getSupportedFormats();
    for (const QByteArray& format : { QByteArray("png"), QByteArray("jpeg"), QByteArray("tiff"), QByteArray("webp"), QByteArray("bmp") })
    {
        if (supported.contains(format))
        {
            formats << format;
        }
    }
    return formats;
}

QList<QByteArray> PDFAppToolImageFormatMenu::getAllFormats()
{
    QList<QByteArray> formats = PDFDocumentToolImageFormat::getSupportedFormats();
    if (formats.contains("jpeg"))
    {
        formats.removeAll("jpg");
    }
    if (formats.contains("tiff"))
    {
        formats.removeAll("tif");
    }
    return formats;
}

QString PDFAppToolImageFormatMenu::getFormatTitle(const QByteArray& format)
{
    if (format == "webp")
    {
        return QStringLiteral("WebP");
    }
    PDFDocumentToolImageFormat imageFormat;
    imageFormat.format = format;
    return imageFormat.getDisplayName();
}

QString PDFAppToolImageFormatMenu::getFormatDescription(const QByteArray& format)
{
    if (format == "png")
    {
        return tr("Lossless, best for text and drawings");
    }
    if (format == "jpeg")
    {
        return tr("Small files, best for photos");
    }
    if (format == "tiff")
    {
        return tr("Lossless, common in print");
    }
    if (format == "webp")
    {
        return tr("Small files, lossless or lossy");
    }
    if (format == "bmp")
    {
        return tr("Uncompressed");
    }
    return QString();
}

void PDFAppToolImageFormatMenu::setFormat(const QByteArray& format)
{
    m_format = format;
    if (QToolButton* button = getButton())
    {
        button->setText(tr("Format: %1").arg(getFormatTitle(format)));
    }
    updateRows();
}

void PDFAppToolImageFormatMenu::showAllFormats()
{
    m_isAllFormatsShown = true;
    updateRows();
}

QToolButton* PDFAppToolImageFormatMenu::getButton() const
{
    return m_panel ? m_panel->getButton() : nullptr;
}

PDFTileRow* PDFAppToolImageFormatMenu::getRow(const QByteArray& format) const
{
    for (const QPointer<PDFTileRow>& row : m_isAllFormatsShown ? m_allRows : m_commonRows)
    {
        if (row && row->property(PDFAppToolImageFormatMenuHelper::FORMAT_PROPERTY).toByteArray() == format)
        {
            return row.data();
        }
    }
    return nullptr;
}

void PDFAppToolImageFormatMenu::onRowClicked()
{
    const QObject* row = sender();
    const QByteArray format = row ? row->property(PDFAppToolImageFormatMenuHelper::FORMAT_PROPERTY).toByteArray() : QByteArray();
    if (format.isEmpty())
    {
        return;
    }

    // The menu closes after its choice (its closed signal runs our slot); the GUI changes its
    // parameter last (a menu retired meanwhile chooses nothing)
    if (m_panel)
    {
        m_panel->close();
    }
    if (!PDFDeferredDeletion::isRetired(this))
    {
        Q_EMIT formatChosen(format);
    }
}

void PDFAppToolImageFormatMenu::onMoreFormatsClicked()
{
    showAllFormats();
}

void PDFAppToolImageFormatMenu::onPanelClosed()
{
    // The next opening starts with the common formats
    m_isAllFormatsShown = false;
    updateRows();
}

void PDFAppToolImageFormatMenu::updateRows()
{
    // The common formats, or every format after More formats (356:11387); the check mark follows the value
    PDFAppToolImageFormatMenuHelper::setChecked(m_commonRows, m_format, !m_isAllFormatsShown);
    PDFAppToolImageFormatMenuHelper::setChecked(m_allRows, m_format, m_isAllFormatsShown);
    for (QWidget* widget : { static_cast<QWidget*>(m_moreFormatsRow.data()), m_divider.data() })
    {
        if (widget)
        {
            widget->setVisible(!m_isAllFormatsShown);
        }
    }
    if (m_allFormatsCaption)
    {
        m_allFormatsCaption->setVisible(m_isAllFormatsShown);
    }
    if (m_panel)
    {
        m_panel->adjustSize();
    }
}

}   // namespace pdf

#include "moc_pdfapptoolimageformatmenu.cpp"
