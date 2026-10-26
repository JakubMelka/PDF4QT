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

#include "pdfappbuiltintoolcollections.h"

#include "pdfdocumenttoolcatalog.h"

#include <QCoreApplication>

namespace pdf
{

QString PDFAppBuiltinToolCollection::getId() const
{
    return QString::fromLatin1(m_id);
}

QString PDFAppBuiltinToolCollection::getName() const
{
    return QCoreApplication::translate(PDFAppBuiltinToolCollections::TRANSLATION_CONTEXT, m_name);
}

QString PDFAppBuiltinToolCollection::getSourceName() const
{
    return QString::fromUtf8(m_name);
}

QString PDFAppBuiltinToolCollection::getIconName() const
{
    return QString::fromLatin1(m_iconName);
}

QStringList PDFAppBuiltinToolCollection::getToolIds() const
{
    QStringList toolIds;
    for (const QString& toolId : QString::fromLatin1(m_toolIds).split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        const QString trimmed = toolId.trimmed();
        if (PDFDocumentToolCatalog::isValidToolId(trimmed) && !toolIds.contains(trimmed))
        {
            toolIds << trimmed;
        }
    }
    return toolIds;
}

const std::vector<PDFAppBuiltinToolCollection>& PDFAppBuiltinToolCollections::getCollections()
{
    // Names, icons and tools of the side column of Figma Tools Overview 272:12821
    // (272:12947) and of Manage collections 283:1888 (283:2346)
    static const std::vector<PDFAppBuiltinToolCollection> collections =
    {
        PDFAppBuiltinToolCollection("prepare-for-sharing", QT_TRANSLATE_NOOP("pdf::PDFAppBuiltinToolCollections", "Prepare for sharing"), "mail",
                                    "optimize.compress,protect.sanitize,protect.password"),
        PDFAppBuiltinToolCollection("scan-to-searchable-pdf", QT_TRANSLATE_NOOP("pdf::PDFAppBuiltinToolCollections", "Scan to searchable PDF"), "camera",
                                    "convert.scan,convert.ocr,optimize.compress"),
        PDFAppBuiltinToolCollection("fix-a-scan", QT_TRANSLATE_NOOP("pdf::PDFAppBuiltinToolCollections", "Fix a scan"), "rotate-cw",
                                    "pages.rotate,pages.crop,pages.interleave,pages.remove"),
        PDFAppBuiltinToolCollection("sign-a-contract", QT_TRANSLATE_NOOP("pdf::PDFAppBuiltinToolCollections", "Sign a contract"), "pen-tool",
                                    "protect.sign-by-hand,protect.sign,protect.timestamp"),
        PDFAppBuiltinToolCollection("archive-documents", QT_TRANSLATE_NOOP("pdf::PDFAppBuiltinToolCollections", "Archive documents"), "briefcase",
                                    "pages.merge,edit.page-numbers,protect.password"),
        PDFAppBuiltinToolCollection("prepare-for-print", QT_TRANSLATE_NOOP("pdf::PDFAppBuiltinToolCollections", "Prepare for print"), "printer",
                                    "pages.boxes,review.output-preview,review.ink-coverage,review.soft-proof"),
        PDFAppBuiltinToolCollection("extract-content", QT_TRANSLATE_NOOP("pdf::PDFAppBuiltinToolCollections", "Extract content"), "download",
                                    "convert.extract-text,convert.extract-images,convert.pdf-to-images"),
    };
    return collections;
}

const PDFAppBuiltinToolCollection* PDFAppBuiltinToolCollections::findCollection(QStringView id)
{
    const int index = getIndex(id);
    return index >= 0 ? &getCollections()[size_t(index)] : nullptr;
}

int PDFAppBuiltinToolCollections::getIndex(QStringView id)
{
    if (id.isEmpty() || id.size() > MAXIMUM_ID_LENGTH)
    {
        return -1;
    }

    const std::vector<PDFAppBuiltinToolCollection>& collections = getCollections();
    for (size_t i = 0; i < collections.size(); ++i)
    {
        if (id == collections[i].getId())
        {
            return int(i);
        }
    }
    return -1;
}

const QStringList& PDFAppBuiltinToolCollections::getIconChoices()
{
    // Icon chooser of the collections editor 283:1888
    static const QStringList icons = { QStringLiteral("rotate-cw"), QStringLiteral("camera"), QStringLiteral("mail"), QStringLiteral("pen-tool"),
                                       QStringLiteral("briefcase"), QStringLiteral("printer"), QStringLiteral("download"), QStringLiteral("folder"),
                                       QStringLiteral("star"), QStringLiteral("zap") };
    return icons;
}

bool PDFAppBuiltinToolCollections::isValidIconName(const QString& iconName)
{
    return getIconChoices().contains(iconName);
}

}   // namespace pdf
