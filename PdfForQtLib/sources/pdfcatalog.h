//    Copyright (C) 2018-2019 Jakub Melka
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

#ifndef PDFCATALOG_H
#define PDFCATALOG_H

#include "pdfobject.h"
#include "pdfpage.h"
#include "pdfoptionalcontent.h"
#include "pdfoutline.h"

#include <QtCore>

#include <array>
#include <vector>
#include <utility>

namespace pdf
{

class PDFDocument;

/// Defines page layout. Default value is SinglePage. This enum specifies the page layout
/// to be used in viewer application.
enum class PageLayout
{
    SinglePage,     ///< Display one page at time (single page on screen)
    OneColumn,      ///< Displays pages in one column (continuous mode)
    TwoColumnLeft,  ///< Display pages in two continuous columns, odd numbered pages are on the left
    TwoColumnRight, ///< Display pages in two continuous columns, even numbered pages are on the left
    TwoPagesLeft,   ///< Display two pages on the screen, odd numbered pages are on the left
    TwoPagesRight   ///< Display two pages on the screen, even numbered pages are on the left
};

/// Specifies, how the document should be displayed in the viewer application.
enum class PageMode
{
    UseNone,            ///< Default value, neither document outline or thumbnails are visible
    UseOutlines,        ///< Document outline window is selected and visible
    UseThumbnails,      ///< Document thumbnails window is selected and visible
    Fullscreen,         ///< Use fullscreen mode, no menu bars, window controls, or any other window visible (presentation mode)
    UseOptionalContent, ///< Optional content group window is selected and visible
    UseAttachments,     ///< Attachments window is selected and visible
};

/// Represents page numbering definition object
class PDFPageLabel
{
public:

    enum class NumberingStyle
    {
        None,               ///< This means, only prefix is used, no numbering
        DecimalArabic,
        UppercaseRoman,
        LowercaseRoman,
        UppercaseLetters,
        LowercaseLetters
    };

    explicit inline PDFPageLabel() :
        m_numberingType(NumberingStyle::None),
        m_prefix(),
        m_pageIndex(0),
        m_startNumber(0)
    {

    }

    explicit inline PDFPageLabel(NumberingStyle numberingType, const QString& prefix, PDFInteger pageIndex, PDFInteger startNumber) :
        m_numberingType(numberingType),
        m_prefix(prefix),
        m_pageIndex(pageIndex),
        m_startNumber(startNumber)
    {

    }

    /// Comparison operator, works only with page indices (because they should be unique)
    bool operator<(const PDFPageLabel& other) const { return m_pageIndex < other.m_pageIndex; }

    /// Parses page label object from PDF object, according to PDF Reference 1.7, Table 8.10
    static PDFPageLabel parse(PDFInteger pageIndex, const PDFDocument* document, const PDFObject& object);

private:
    NumberingStyle m_numberingType;
    QString m_prefix;
    PDFInteger m_pageIndex;
    PDFInteger m_startNumber;
};

class PDFViewerPreferences
{
public:

    enum OptionFlag
    {
        None                = 0x0000,   ///< Empty flag
        HideToolbar         = 0x0001,   ///< Hide toolbar
        HideMenubar         = 0x0002,   ///< Hide menubar
        HideWindowUI        = 0x0004,   ///< Hide window UI (for example scrollbars, navigation controls, etc.)
        FitWindow           = 0x0008,   ///< Resize window to fit first displayed page
        CenterWindow        = 0x0010,   ///< Position of the document's window should be centered on the screen
        DisplayDocTitle     = 0x0020,   ///< Display documents title instead of file name (introduced in PDF 1.4)
        PickTrayByPDFSize   = 0x0040    ///< Pick tray by PDF size (printing option)
    };

    Q_DECLARE_FLAGS(OptionFlags, OptionFlag)

    /// This enum specifies, how to display document, when exiting full screen mode.
    enum class NonFullScreenPageMode
    {
        UseNone,
        UseOutline,
        UseThumbnails,
        UseOptionalContent
    };

    /// Predominant reading order of text.
    enum class Direction
    {
        LeftToRight,    ///< Default
        RightToLeft     ///< Reading order is right to left. Also used for vertical writing systems (Chinese/Japan etc.)
    };

    /// Printer settings - paper handling option to use when printing the document.
    enum class Duplex
    {
        None,
        Simplex,
        DuplexFlipShortEdge,
        DuplexFlipLongEdge
    };

    enum class PrintScaling
    {
        None,
        AppDefault
    };

    enum Properties
    {
        ViewArea,
        ViewClip,
        PrintArea,
        PrintClip,
        EndProperties
    };

    constexpr inline PDFViewerPreferences() = default;

    constexpr inline PDFViewerPreferences(const PDFViewerPreferences&) = default;
    constexpr inline PDFViewerPreferences(PDFViewerPreferences&&) = default;

    constexpr inline PDFViewerPreferences& operator=(const PDFViewerPreferences&) = default;
    constexpr inline PDFViewerPreferences& operator=(PDFViewerPreferences&&) = default;

    /// Parses viewer preferences from catalog dictionary. If object cannot be parsed, or error occurs,
    /// then exception is thrown.
    static PDFViewerPreferences parse(const PDFObject& catalogDictionary, const PDFDocument* document);

    OptionFlags getOptions() const { return m_optionFlags; }
    const QByteArray& getProperty(Properties property) const { return m_properties.at(property); }
    NonFullScreenPageMode getNonFullScreenPageMode() const { return m_nonFullScreenPageMode; }
    Direction getDirection() const { return m_direction; }
    Duplex getDuplex() const { return m_duplex; }
    PrintScaling getPrintScaling() const { return m_printScaling; }
    const std::vector<std::pair<PDFInteger, PDFInteger>>& getPrintPageRanges() const { return m_printPageRanges; }
    PDFInteger getNumberOfCopies() const { return m_numberOfCopies; }

private:
    OptionFlags m_optionFlags = None;
    std::array<QByteArray, EndProperties> m_properties;
    NonFullScreenPageMode m_nonFullScreenPageMode = NonFullScreenPageMode::UseNone;
    Direction m_direction = Direction::LeftToRight;
    Duplex m_duplex = Duplex::None;
    PrintScaling m_printScaling = PrintScaling::AppDefault;
    std::vector<std::pair<PDFInteger, PDFInteger>> m_printPageRanges;
    PDFInteger m_numberOfCopies = 1;
};

class PDFCatalog
{
public:
    constexpr inline PDFCatalog() = default;

    constexpr inline PDFCatalog(const PDFCatalog&) = default;
    constexpr inline PDFCatalog(PDFCatalog&&) = default;

    constexpr inline PDFCatalog& operator=(const PDFCatalog&) = default;
    constexpr inline PDFCatalog& operator=(PDFCatalog&&) = default;

    /// Returns viewer preferences of the application
    const PDFViewerPreferences* getViewerPreferences() const { return &m_viewerPreferences; }

    /// Returns the page count
    size_t getPageCount() const { return m_pages.size(); }

    /// Returns the page
    const PDFPage* getPage(size_t index) const { return &m_pages.at(index); }

    /// Returns optional content properties
    const PDFOptionalContentProperties* getOptionalContentProperties() const { return &m_optionalContentProperties; }

    /// Returns root pointer for outline items
    QSharedPointer<PDFOutlineItem> getOutlineRootPtr() const { return m_outlineRoot; }

    /// Parses catalog from catalog dictionary. If object cannot be parsed, or error occurs,
    /// then exception is thrown.
    static PDFCatalog parse(const PDFObject& catalog, const PDFDocument* document);

private:
    PDFViewerPreferences m_viewerPreferences;
    std::vector<PDFPage> m_pages;
    std::vector<PDFPageLabel> m_pageLabels;
    PDFOptionalContentProperties m_optionalContentProperties;
    QSharedPointer<PDFOutlineItem> m_outlineRoot;
};

}   // namespace pdf

#endif // PDFCATALOG_H
