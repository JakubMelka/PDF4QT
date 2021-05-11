//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftoolinfonameddestinations.h"
#include "pdfencoding.h"

namespace pdftool
{

static PDFToolInfoNamedDestinationsApplication s_infoNamedDestinationsApplication;

QString PDFToolInfoNamedDestinationsApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "info-dests";

        case Name:
            return PDFToolTranslationContext::tr("Info (Named Destinations)");

        case Description:
            return PDFToolTranslationContext::tr("Retrieve informations about named destinations in a document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolInfoNamedDestinationsApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    QString parseError;
    std::vector<pdf::PDFInteger> pages = options.getPageRange(document.getCatalog()->getPageCount(), parseError, false);

    if (!parseError.isEmpty())
    {
        PDFConsole::writeError(parseError, options.outputCodec);
        return ErrorInvalidArguments;
    }

    PDFOutputFormatter formatter(options.outputStyle, options.outputCodec);
    formatter.beginDocument("info-named-destinations", PDFToolTranslationContext::tr("Named destinations used in document %1").arg(options.document));
    formatter.endl();

    formatter.beginTable("named-dests", PDFToolTranslationContext::tr("Named Destinations"));

    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("no", PDFToolTranslationContext::tr("No."), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("page-number", PDFToolTranslationContext::tr("Page Number"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("type", PDFToolTranslationContext::tr("Type"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("left", PDFToolTranslationContext::tr("Left"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("top", PDFToolTranslationContext::tr("Top"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("right", PDFToolTranslationContext::tr("Right"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("bottom", PDFToolTranslationContext::tr("Bottom"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("zoom", PDFToolTranslationContext::tr("Zoom"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("name", PDFToolTranslationContext::tr("Name"), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    QLocale locale;

    struct DestinationItem
    {
        pdf::PDFInteger pageIndex = 0;
        QString name;
        pdf::PDFDestination destination;
    };
    std::vector<DestinationItem> destinationItems;

    for (const auto& destinationItem : document.getCatalog()->getNamedDestinations())
    {
        const QByteArray& name = destinationItem.first;
        const pdf::PDFDestination& destination = destinationItem.second;

        if (destination.getDestinationType() == pdf::DestinationType::Invalid)
        {
            continue;
        }

        // Search for page...
        pdf::PDFInteger pageIndex = destination.getPageIndex();
        pdf::PDFObjectReference pageReference = destination.getPageReference();
        if (pageReference.isValid())
        {
            pageIndex = document.getCatalog()->getPageIndexFromPageReference(pageReference);
        }

        ++pageIndex;
        if (!std::binary_search(pages.cbegin(), pages.cend(), pageIndex))
        {
            // This page is skipped
            continue;
        }

        DestinationItem item;
        item.pageIndex = pageIndex;
        item.name = pdf::PDFEncoding::convertSmartFromByteStringToUnicode(name, nullptr);
        item.destination = destination;
        destinationItems.emplace_back(qMove(item));
    }

    std::stable_sort(destinationItems.begin(), destinationItems.end(), [](const auto& l, const auto& r) { return l.pageIndex < r.pageIndex; });

    int ref = 1;
    for (const auto& destinationItem : destinationItems)
    {
        QString type, left, top, right, bottom, zoom;

        const pdf::PDFDestination& destination = destinationItem.destination;
        switch (destination.getDestinationType())
        {
            case pdf::DestinationType::Invalid:
            {
                type = PDFToolTranslationContext::tr("Invalid");
                break;
            }

            case pdf::DestinationType::Named:
            {
                type = PDFToolTranslationContext::tr("Named");
                break;
            }

            case pdf::DestinationType::XYZ:
            {
                type = PDFToolTranslationContext::tr("XYZ");
                left = locale.toString(destination.getLeft());
                top = locale.toString(destination.getTop());
                if (!qFuzzyIsNull(destination.getZoom()))
                {
                    zoom = locale.toString(destination.getZoom());
                }
                break;
            }

            case pdf::DestinationType::Fit:
            {
                type = PDFToolTranslationContext::tr("Fit");
                break;
            }

            case pdf::DestinationType::FitH:
            {
                type = PDFToolTranslationContext::tr("FitH");
                top = locale.toString(destination.getTop());
                break;
            }

            case pdf::DestinationType::FitV:
            {
                type = PDFToolTranslationContext::tr("FitV");
                left = locale.toString(destination.getLeft());
                break;
            }

            case pdf::DestinationType::FitR:
            {
                type = PDFToolTranslationContext::tr("FitR");
                left = locale.toString(destination.getLeft());
                top = locale.toString(destination.getTop());
                bottom = locale.toString(destination.getBottom());
                right = locale.toString(destination.getRight());
                break;
            }

            case pdf::DestinationType::FitB:
            {
                type = PDFToolTranslationContext::tr("FitB");
                break;
            }

            case pdf::DestinationType::FitBH:
            {
                type = PDFToolTranslationContext::tr("FitBH");
                top = locale.toString(destination.getTop());
                break;
            }

            case pdf::DestinationType::FitBV:
            {
                type = PDFToolTranslationContext::tr("FitBV");
                left = locale.toString(destination.getLeft());
                break;
            }

            default:
                Q_ASSERT(false);
                break;
        };

        formatter.beginTableRow("destination", ref);

        formatter.writeTableColumn("no", locale.toString(ref), Qt::AlignRight);
        formatter.writeTableColumn("page-number", locale.toString(destinationItem.pageIndex), Qt::AlignRight);
        formatter.writeTableColumn("type", type, Qt::AlignRight);
        formatter.writeTableColumn("left", left, Qt::AlignRight);
        formatter.writeTableColumn("top", top, Qt::AlignRight);
        formatter.writeTableColumn("right", right, Qt::AlignRight);
        formatter.writeTableColumn("bottom", bottom, Qt::AlignRight);
        formatter.writeTableColumn("zoom", zoom, Qt::AlignRight);
        formatter.writeTableColumn("name", destinationItem.name);

        formatter.endTableRow();
        ++ref;
    }

    formatter.endTable();

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolInfoNamedDestinationsApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector;
}

}   // namespace pdftool
