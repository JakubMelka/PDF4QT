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

#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdfnumbertreeloader.h"

namespace pdf
{

static constexpr const char* PDF_VIEWER_PREFERENCES_DICTIONARY = "ViewerPreferences";
static constexpr const char* PDF_VIEWER_PREFERENCES_HIDE_TOOLBAR = "HideToolbar";
static constexpr const char* PDF_VIEWER_PREFERENCES_HIDE_MENUBAR = "HideMenubar";
static constexpr const char* PDF_VIEWER_PREFERENCES_HIDE_WINDOW_UI = "HideWindowUI";
static constexpr const char* PDF_VIEWER_PREFERENCES_FIT_WINDOW = "FitWindow";
static constexpr const char* PDF_VIEWER_PREFERENCES_CENTER_WINDOW = "CenterWindow";
static constexpr const char* PDF_VIEWER_PREFERENCES_DISPLAY_DOCUMENT_TITLE = "DisplayDocTitle";
static constexpr const char* PDF_VIEWER_PREFERENCES_NON_FULLSCREEN_PAGE_MODE = "NonFullScreenPageMode";
static constexpr const char* PDF_VIEWER_PREFERENCES_DIRECTION = "Direction";
static constexpr const char* PDF_VIEWER_PREFERENCES_VIEW_AREA = "ViewArea";
static constexpr const char* PDF_VIEWER_PREFERENCES_VIEW_CLIP = "ViewClip";
static constexpr const char* PDF_VIEWER_PREFERENCES_PRINT_AREA = "PrintArea";
static constexpr const char* PDF_VIEWER_PREFERENCES_PRINT_CLIP = "PrintClip";
static constexpr const char* PDF_VIEWER_PREFERENCES_PRINT_SCALING = "PrintScaling";
static constexpr const char* PDF_VIEWER_PREFERENCES_DUPLEX = "Duplex";
static constexpr const char* PDF_VIEWER_PREFERENCES_PICK_TRAY_BY_PDF_SIZE = "PickTrayByPDFSize";
static constexpr const char* PDF_VIEWER_PREFERENCES_NUMBER_OF_COPIES = "NumCopies";
static constexpr const char* PDF_VIEWER_PREFERENCES_PRINT_PAGE_RANGE = "PrintPageRange";

PDFCatalog PDFCatalog::parse(const PDFObject& catalog, const PDFDocument* document)
{
    if (!catalog.isDictionary())
    {
        throw PDFException(PDFTranslationContext::tr("Catalog must be a dictionary."));
    }

    const PDFDictionary* catalogDictionary = catalog.getDictionary();
    Q_ASSERT(catalogDictionary);

    PDFCatalog catalogObject;
    catalogObject.m_viewerPreferences = PDFViewerPreferences::parse(catalog, document);
    catalogObject.m_pages = PDFPage::parse(document, catalogDictionary->get("Pages"));
    catalogObject.m_pageLabels = PDFNumberTreeLoader<PDFPageLabel>::parse(document, catalogDictionary->get("PageLabels"));

    if (catalogDictionary->hasKey("OCProperties"))
    {
        catalogObject.m_optionalContentProperties = PDFOptionalContentProperties::create(document, catalogDictionary->get("OCProperties"));
    }

    if (catalogDictionary->hasKey("Outlines"))
    {
        catalogObject.m_outlineRoot = PDFOutlineItem::parse(document, catalogDictionary->get("Outlines"));
    }

    if (catalogDictionary->hasKey("OpenAction"))
    {
        PDFObject openAction = document->getObject(catalogDictionary->get("OpenAction"));
        if (openAction.isArray())
        {
            catalogObject.m_openAction.reset(new PDFActionGoTo(PDFDestination::parse(document, openAction)));
        }
        if (openAction.isDictionary())
        {
            catalogObject.m_openAction = PDFAction::parse(document, openAction);
        }
    }

    PDFDocumentDataLoaderDecorator loader(document);
    if (catalogDictionary->hasKey("PageLayout"))
    {
        constexpr const std::array<std::pair<const char*, PageLayout>, 6> pageLayouts = {
            std::pair<const char*, PageLayout>{ "SinglePage", PageLayout::SinglePage },
            std::pair<const char*, PageLayout>{ "OneColumn", PageLayout::OneColumn },
            std::pair<const char*, PageLayout>{ "TwoColumnLeft", PageLayout::TwoColumnLeft },
            std::pair<const char*, PageLayout>{ "TwoColumnRight", PageLayout::TwoColumnRight },
            std::pair<const char*, PageLayout>{ "TwoPageLeft", PageLayout::TwoPagesLeft },
            std::pair<const char*, PageLayout>{ "TwoPageRight", PageLayout::TwoPagesRight }
        };

        catalogObject.m_pageLayout = loader.readEnumByName(catalogDictionary->get("PageLayout"), pageLayouts.begin(), pageLayouts.end(), PageLayout::SinglePage);
    }

    if (catalogDictionary->hasKey("PageMode"))
    {
        constexpr const std::array<std::pair<const char*, PageMode>, 6> pageModes = {
            std::pair<const char*, PageMode>{ "UseNone", PageMode::UseNone },
            std::pair<const char*, PageMode>{ "UseOutlines", PageMode::UseOutlines },
            std::pair<const char*, PageMode>{ "UseThumbs", PageMode::UseThumbnails },
            std::pair<const char*, PageMode>{ "FullScreen", PageMode::Fullscreen },
            std::pair<const char*, PageMode>{ "UseOC", PageMode::UseOptionalContent },
            std::pair<const char*, PageMode>{ "UseAttachments", PageMode::UseAttachments }
        };

        catalogObject.m_pageMode = loader.readEnumByName(catalogDictionary->get("PageMode"), pageModes.begin(), pageModes.end(), PageMode::UseNone);
    }

    return catalogObject;
}

PDFViewerPreferences PDFViewerPreferences::parse(const PDFObject& catalogDictionary, const PDFDocument* document)
{
    PDFViewerPreferences result;

    if (!catalogDictionary.isDictionary())
    {
        throw PDFException(PDFTranslationContext::tr("Catalog must be a dictionary."));
    }

    const PDFDictionary* dictionary = catalogDictionary.getDictionary();
    if (dictionary->hasKey(PDF_VIEWER_PREFERENCES_DICTIONARY))
    {
        const PDFObject& viewerPreferencesObject = document->getObject(dictionary->get(PDF_VIEWER_PREFERENCES_DICTIONARY));
        if (viewerPreferencesObject.isDictionary())
        {
            // Load the viewer preferences object
            const PDFDictionary* viewerPreferencesDictionary = viewerPreferencesObject.getDictionary();

            auto addFlag = [&result, viewerPreferencesDictionary, document] (const char* name, OptionFlag flag)
            {
                const PDFObject& flagObject = document->getObject(viewerPreferencesDictionary->get(name));
                if (!flagObject.isNull())
                {
                    if (flagObject.isBool())
                    {
                        result.m_optionFlags.setFlag(flag, flagObject.getBool());
                    }
                    else
                    {
                        throw PDFException(PDFTranslationContext::tr("Expected boolean value."));
                    }
                }
            };
            addFlag(PDF_VIEWER_PREFERENCES_HIDE_TOOLBAR, HideToolbar);
            addFlag(PDF_VIEWER_PREFERENCES_HIDE_MENUBAR, HideMenubar);
            addFlag(PDF_VIEWER_PREFERENCES_HIDE_WINDOW_UI, HideWindowUI);
            addFlag(PDF_VIEWER_PREFERENCES_FIT_WINDOW, FitWindow);
            addFlag(PDF_VIEWER_PREFERENCES_CENTER_WINDOW, CenterWindow);
            addFlag(PDF_VIEWER_PREFERENCES_DISPLAY_DOCUMENT_TITLE, DisplayDocTitle);
            addFlag(PDF_VIEWER_PREFERENCES_PICK_TRAY_BY_PDF_SIZE, PickTrayByPDFSize);

            // Non-fullscreen page mode
            const PDFObject& nonFullscreenPageMode = document->getObject(viewerPreferencesDictionary->get(PDF_VIEWER_PREFERENCES_NON_FULLSCREEN_PAGE_MODE));
            if (!nonFullscreenPageMode.isNull())
            {
                if (!nonFullscreenPageMode.isName())
                {
                    throw PDFException(PDFTranslationContext::tr("Expected name."));
                }

                QByteArray enumName = nonFullscreenPageMode.getString();
                if (enumName == "UseNone")
                {
                    result.m_nonFullScreenPageMode = NonFullScreenPageMode::UseNone;
                }
                else if (enumName == "UseOutlines")
                {
                    result.m_nonFullScreenPageMode = NonFullScreenPageMode::UseOutline;
                }
                else if (enumName == "UseThumbs")
                {
                    result.m_nonFullScreenPageMode = NonFullScreenPageMode::UseThumbnails;
                }
                else if (enumName == "UseOC")
                {
                    result.m_nonFullScreenPageMode = NonFullScreenPageMode::UseOptionalContent;
                }
                else
                {
                    throw PDFException(PDFTranslationContext::tr("Unknown viewer preferences settings."));
                }
            }

            // Direction
            const PDFObject& direction = document->getObject(viewerPreferencesDictionary->get(PDF_VIEWER_PREFERENCES_DIRECTION));
            if (!direction.isNull())
            {
                if (!direction.isName())
                {
                    throw PDFException(PDFTranslationContext::tr("Expected name."));
                }

                QByteArray enumName = direction.getString();
                if (enumName == "L2R")
                {
                    result.m_direction = Direction::LeftToRight;
                }
                else if (enumName == "R2L")
                {
                    result.m_direction = Direction::RightToLeft;
                }
                else
                {
                    throw PDFException(PDFTranslationContext::tr("Unknown viewer preferences settings."));
                }
            }

            auto addProperty = [&result, viewerPreferencesDictionary, document] (const char* name, Properties property)
            {
                const PDFObject& propertyObject = document->getObject(viewerPreferencesDictionary->get(name));
                if (!propertyObject.isNull())
                {
                    if (propertyObject.isName())
                    {
                        result.m_properties[property] = propertyObject.getString();
                    }
                    else
                    {
                        throw PDFException(PDFTranslationContext::tr("Expected name."));
                    }
                }
            };
            addProperty(PDF_VIEWER_PREFERENCES_VIEW_AREA, ViewArea);
            addProperty(PDF_VIEWER_PREFERENCES_VIEW_CLIP, ViewClip);
            addProperty(PDF_VIEWER_PREFERENCES_PRINT_AREA, PrintArea);
            addProperty(PDF_VIEWER_PREFERENCES_PRINT_CLIP, PrintClip);

            // Print scaling
            const PDFObject& printScaling = document->getObject(viewerPreferencesDictionary->get(PDF_VIEWER_PREFERENCES_PRINT_SCALING));
            if (!printScaling.isNull())
            {
                if (!printScaling.isName())
                {
                    throw PDFException(PDFTranslationContext::tr("Expected name."));
                }

                QByteArray enumName = printScaling.getString();
                if (enumName == "None")
                {
                    result.m_printScaling = PrintScaling::None;
                }
                else if (enumName == "AppDefault")
                {
                    result.m_printScaling = PrintScaling::AppDefault;
                }
                else
                {
                    throw PDFException(PDFTranslationContext::tr("Unknown viewer preferences settings."));
                }
            }

            // Duplex
            const PDFObject& duplex = document->getObject(viewerPreferencesDictionary->get(PDF_VIEWER_PREFERENCES_DUPLEX));
            if (!duplex.isNull())
            {
                if (!duplex.isName())
                {
                    throw PDFException(PDFTranslationContext::tr("Expected name."));
                }

                QByteArray enumName = duplex.getString();
                if (enumName == "Simplex")
                {
                    result.m_duplex = Duplex::Simplex;
                }
                else if (enumName == "DuplexFlipShortEdge")
                {
                    result.m_duplex = Duplex::DuplexFlipShortEdge;
                }
                else if (enumName == "DuplexFlipLongEdge")
                {
                    result.m_duplex = Duplex::DuplexFlipLongEdge;
                }
                else
                {
                    throw PDFException(PDFTranslationContext::tr("Unknown viewer preferences settings."));
                }
            }

            // Print page range
            const PDFObject& printPageRange = document->getObject(viewerPreferencesDictionary->get(PDF_VIEWER_PREFERENCES_PRINT_PAGE_RANGE));
            if (!printPageRange.isNull())
            {
                if (!duplex.isArray())
                {
                    throw PDFException(PDFTranslationContext::tr("Expected array of integers."));
                }

                // According to PDF Reference 1.7, this entry is ignored in following cases:
                //  1) Array size is odd
                //  2) Array contains negative numbers
                //
                // But what should we do, if we get 0? Pages in the PDF file are numbered from 1.
                // So if this situation occur, we also ignore the entry.
                const PDFArray* array = duplex.getArray();

                const size_t count = array->getCount();
                if (count % 2 == 0 && count > 0)
                {
                    bool badPageNumber = false;
                    int scanned = 0;
                    PDFInteger start = -1;

                    for (size_t i = 0; i < count; ++i)
                    {
                        const PDFObject& number = document->getObject(array->getItem(i));
                        if (number.isInt())
                        {
                            PDFInteger current = number.getInteger();

                            if (current <= 0)
                            {
                                badPageNumber = true;
                                break;
                            }

                            switch (scanned++)
                            {
                                case 0:
                                {
                                    start = current;
                                    break;
                                }

                                case 1:
                                {
                                    scanned = 0;
                                    result.m_printPageRanges.emplace_back(start, current);
                                    break;
                                }

                                default:
                                    Q_ASSERT(false);
                            }
                        }
                        else
                        {
                            throw PDFException(PDFTranslationContext::tr("Expected integer."));
                        }
                    }

                    // Did we get negative or zero value? If yes, clear the range.
                    if (badPageNumber)
                    {
                        result.m_printPageRanges.clear();
                    }
                }
            }

            // Number of copies
            const PDFObject& numberOfCopies = document->getObject(viewerPreferencesDictionary->get(PDF_VIEWER_PREFERENCES_NUMBER_OF_COPIES));
            if (!numberOfCopies.isNull())
            {
                if (numberOfCopies.isInt())
                {
                    result.m_numberOfCopies = numberOfCopies.getInteger();
                }
                else
                {
                    throw PDFException(PDFTranslationContext::tr("Expected integer."));
                }
            }
        }
        else if (!viewerPreferencesObject.isNull())
        {
            throw PDFException(PDFTranslationContext::tr("Viewer preferences must be a dictionary."));
        }
    }

    return result;
}

PDFPageLabel PDFPageLabel::parse(PDFInteger pageIndex, const PDFDocument* document, const PDFObject& object)
{
    const PDFObject& dereferencedObject = document->getObject(object);
    if (dereferencedObject.isDictionary())
    {
        std::array<std::pair<const char*, NumberingStyle>, 5> numberingStyles = { std::pair<const char*, NumberingStyle>{ "D", NumberingStyle::DecimalArabic},
                                                                                  std::pair<const char*, NumberingStyle>{ "R", NumberingStyle::UppercaseRoman },
                                                                                  std::pair<const char*, NumberingStyle>{ "r", NumberingStyle::LowercaseRoman },
                                                                                  std::pair<const char*, NumberingStyle>{ "A", NumberingStyle::UppercaseLetters},
                                                                                  std::pair<const char*, NumberingStyle>{ "a", NumberingStyle::LowercaseLetters} };

        const PDFDictionary* dictionary = dereferencedObject.getDictionary();
        const PDFDocumentDataLoaderDecorator loader(document);
        const NumberingStyle numberingStyle = loader.readEnumByName(dictionary->get("S"), numberingStyles.cbegin(), numberingStyles.cend(), NumberingStyle::None);
        const QString prefix = loader.readTextString(dictionary->get("P"), QString());
        const PDFInteger startNumber = loader.readInteger(dictionary->get("St"), 1);
        return PDFPageLabel(numberingStyle, prefix, pageIndex, startNumber);
    }
    else
    {
        throw PDFException(PDFTranslationContext::tr("Expected page label dictionary."));
    }

    return PDFPageLabel();
}

}   // namespace pdf
