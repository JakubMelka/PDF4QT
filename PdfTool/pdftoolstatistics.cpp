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

#include "pdftoolstatistics.h"
#include "pdfobjectutils.h"

namespace pdftool
{

static PDFToolStatisticsApplication s_statisticsApplication;

QString PDFToolStatisticsApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "statistics";

        case Name:
            return PDFToolTranslationContext::tr("Statistics");

        case Description:
            return PDFToolTranslationContext::tr("Compute statistics of internal objects used in a document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolStatisticsApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    pdf::PDFObjectClassifier classifier;
    classifier.classify(&document);
    pdf::PDFObjectClassifier::Statistics statistics = classifier.calculateStatistics(&document);

    QLocale locale;

    PDFOutputFormatter formatter(options.outputStyle, options.outputCodec);
    formatter.beginDocument("info", PDFToolTranslationContext::tr("Information about document %1").arg(options.document));
    formatter.endl();

    {
        formatter.beginTable("statistics-objects-by-class", PDFToolTranslationContext::tr("Statistics by Object Class"));

        formatter.beginTableHeaderRow("header");
        formatter.writeTableHeaderColumn("class", PDFToolTranslationContext::tr("Class"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("percentage", PDFToolTranslationContext::tr("Percentage [%]"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("count", PDFToolTranslationContext::tr("Count [#]"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("space-usage", PDFToolTranslationContext::tr("Space Usage [bytes]"), Qt::AlignLeft);
        formatter.endTableHeaderRow();

        qint64 totalBytesCount = 0;
        for (const auto& item : statistics.statistics)
        {
            totalBytesCount += item.second.bytes;
        }

        auto addRow = [&](pdf::PDFObjectClassifier::Type type, QString classText)
        {
            auto it = statistics.statistics.find(type);

            if (it == statistics.statistics.cend())
            {
                // Jakub Melka: no type found
                return;
            }

            const pdf::PDFObjectClassifier::StatisticsItem& statisticsItem = it->second;

            qreal percentage = qreal(100.0) * qreal(statisticsItem.bytes) / qreal(totalBytesCount);

            formatter.beginTableRow("item", int(type));
            formatter.writeTableColumn("class", classText);
            formatter.writeTableColumn("percentage", locale.toString(percentage, 'f', 2), Qt::AlignRight);
            formatter.writeTableColumn("count", locale.toString(statisticsItem.count), Qt::AlignRight);
            formatter.writeTableColumn("byte-usage", locale.toString(statisticsItem.bytes), Qt::AlignRight);
            formatter.endTableRow();
        };

        addRow(pdf::PDFObjectClassifier::Page, PDFToolTranslationContext::tr("Page"));
        addRow(pdf::PDFObjectClassifier::ContentStream, PDFToolTranslationContext::tr("Content Stream"));
        addRow(pdf::PDFObjectClassifier::GraphicState, PDFToolTranslationContext::tr("Graphic State"));
        addRow(pdf::PDFObjectClassifier::ColorSpace, PDFToolTranslationContext::tr("Color Space"));
        addRow(pdf::PDFObjectClassifier::Pattern, PDFToolTranslationContext::tr("Pattern"));
        addRow(pdf::PDFObjectClassifier::Shading, PDFToolTranslationContext::tr("Shading"));
        addRow(pdf::PDFObjectClassifier::Image, PDFToolTranslationContext::tr("Image"));
        addRow(pdf::PDFObjectClassifier::Form, PDFToolTranslationContext::tr("Form"));
        addRow(pdf::PDFObjectClassifier::Font, PDFToolTranslationContext::tr("Font"));
        addRow(pdf::PDFObjectClassifier::Action, PDFToolTranslationContext::tr("Action"));
        addRow(pdf::PDFObjectClassifier::Annotation, PDFToolTranslationContext::tr("Annotation"));
        addRow(pdf::PDFObjectClassifier::None, PDFToolTranslationContext::tr("Other"));

        formatter.endTable();
    }

    formatter.endl();

    {
        formatter.beginTable("statistics-objects-by-type", PDFToolTranslationContext::tr("Statistics by Object Type"));

        formatter.beginTableHeaderRow("header");
        formatter.writeTableHeaderColumn("class", PDFToolTranslationContext::tr("Type") , Qt::AlignLeft);
        formatter.writeTableHeaderColumn("percentage", PDFToolTranslationContext::tr("Percentage [%]"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("count", PDFToolTranslationContext::tr("Count [#]"), Qt::AlignLeft);
        formatter.endTableHeaderRow();

        qint64 totalObjectCount = 0;
        for (pdf::PDFObject::Type type : pdf::PDFObject::getTypes())
        {
            const qint64 currentObjectCount = statistics.objectCountByType[size_t(type)];
            totalObjectCount += currentObjectCount;
        }

        if (totalObjectCount > 0)
        {
            for (pdf::PDFObject::Type type : pdf::PDFObject::getTypes())
            {
                const qint64 currentObjectCount = statistics.objectCountByType[size_t(type)];

                if (currentObjectCount == 0)
                {
                    continue;
                }

                qreal percentage = qreal(100.0) * qreal(currentObjectCount) / qreal(totalObjectCount);

                formatter.beginTableRow("item", int(type));
                formatter.writeTableColumn("type", pdf::PDFObjectUtils::getObjectTypeName(type));
                formatter.writeTableColumn("percentage", locale.toString(percentage, 'f', 2), Qt::AlignRight);
                formatter.writeTableColumn("count", locale.toString(currentObjectCount), Qt::AlignRight);
                formatter.endTableRow();
            }
        }

        formatter.endTable();
    }

    formatter.endDocument();

    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolStatisticsApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument;
}

}   // namespace pdftool
