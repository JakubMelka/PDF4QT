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

#include "pdfappmcparguments.h"
#include "pdfappcontext.h"
#include "pdfapplog.h"
#include "pdfappmcptoolset.h"
#include "pdfappsysteminfo.h"
#include "pdfrepositoryhelper.h"

#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfutils.h"

#include "pdfmcpjson.h"
#include "pdfthemecolors.h"

#include <QDir>
#include <QFileInfo>

namespace pdf
{

/// Helpers of the arguments
class PDFAppMcpArgumentsHelper
{
public:
    PDFAppMcpArgumentsHelper() = delete;

    /// The canonical path with the junctions of NTFS resolved: QFileInfo resolves
    /// the symbolic links, not the junctions (mklink /J needs no privilege) - a
    /// junction to the data of PDF4QT or to another folder would pass the rules
    /// of the paths otherwise (fix wave FW7-H). An empty text for a junction
    /// without a target or too many of them. Other systems: unchanged.
    static QString resolveJunctions(const QString& canonicalPath)
    {
#ifdef Q_OS_WIN
        QString current = canonicalPath;
        for (int depth = 0; depth < 32; ++depth)
        {
            const QStringList parts = current.split(QLatin1Char('/'));
            QString prefix = parts.value(0);
            bool isReplaced = false;
            for (qsizetype i = 1; i < parts.size() && !isReplaced; ++i)
            {
                prefix += QLatin1Char('/') + parts[i];
                const QFileInfo info(prefix);
                if (!info.isJunction())
                {
                    continue;
                }

                // A mount point of a volume is a junction too: Qt gives its first
                // mount path - the drive of the volume, or the folder itself for a
                // volume without a drive letter - or "Volume{...}", which does not
                // exist, without one. The folder is the root of the volume then, not
                // a way to another place (only an administrator mounts a volume; a
                // junction to itself or to nothing reaches nothing either; review
                // RFW7-M - every file on such a volume was refused)
                const QString rawTarget = info.junctionTarget();
                const QString target = QFileInfo(rawTarget).canonicalFilePath();
                const bool isVolumeName = target.isEmpty() && QFileInfo(rawTarget).fileName().startsWith(QLatin1String("Volume{"), Qt::CaseInsensitive);
                if (isVolumeName || target.compare(prefix, Qt::CaseInsensitive) == 0)
                {
                    continue;
                }
                if (target.isEmpty())
                {
                    return QString();
                }
                const QStringList rest = parts.mid(i + 1);
                current = rest.isEmpty() ? target : target + QLatin1Char('/') + rest.join(QLatin1Char('/'));
                isReplaced = true;
            }
            if (!isReplaced)
            {
                return current;
            }
        }
        return QString();
#else
        return canonicalPath;
#endif
    }

    static QColor getPaletteColor(const QString& name)
    {
        static constexpr PDFThemeColors::Color COLORS[] =
        {
            PDFThemeColors::AnnotationYellow, PDFThemeColors::AnnotationOrange, PDFThemeColors::AnnotationRed,
            PDFThemeColors::AnnotationPink, PDFThemeColors::AnnotationPurple, PDFThemeColors::AnnotationBlue,
            PDFThemeColors::AnnotationGreen, PDFThemeColors::AnnotationBlack, PDFThemeColors::AnnotationWhite
        };

        const qsizetype index = PDFAppMcpToolSet::getColorNames().indexOf(name);
        if (index < 0 || index >= qsizetype(std::size(COLORS)))
        {
            return QColor();
        }

        // The canonical light-theme values (the swatches of the color row report them too)
        return getBuiltinColor(COLORS[index], PDFThemeColors::Scheme::Light);
    }

    /// Color of a token of a built-in theme
    static QColor getBuiltinColor(PDFThemeColors::Color color, PDFThemeColors::Scheme scheme)
    {
        for (const PDFThemeColors& theme : PDFThemeColors::getBuiltinThemes())
        {
            if (theme.getScheme() == scheme)
            {
                return theme.getColor(color);
            }
        }
        return QColor();
    }

    static bool isReservedDeviceName(const QString& fileName)
    {
        const QString base = fileName.section(QLatin1Char('.'), 0, 0).trimmed().toUpper();
        static const QStringList names = { QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
                                           QStringLiteral("CONIN$"), QStringLiteral("CONOUT$") };
        if (names.contains(base))
        {
            return true;
        }

        // COM1-9, LPT1-9 (also the superscript digits Windows accepts)
        if (base.size() == 4 && (base.startsWith(QLatin1String("COM")) || base.startsWith(QLatin1String("LPT"))))
        {
            const QChar digit = base[3];
            return (digit >= u'1' && digit <= u'9') || digit == QChar(0x00B9) || digit == QChar(0x00B2) || digit == QChar(0x00B3);
        }
        return false;
    }
};

std::optional<QColor> PDFAppMcpArguments::parseColor(const QString& text, QString* errorMessage)
{
    const QString value = text.trimmed().toLower();
    const QColor palette = PDFAppMcpArgumentsHelper::getPaletteColor(value);
    if (palette.isValid())
    {
        return palette;
    }

    if (value.size() == 7 && value.startsWith(QLatin1Char('#')))
    {
        bool ok = false;
        const uint rgb = value.mid(1).toUInt(&ok, 16);
        if (ok)
        {
            return QColor::fromRgb(QRgb(0xFF000000u | rgb));
        }
    }

    if (errorMessage)
    {
        *errorMessage = QStringLiteral("expected a palette name (%1) or \"#rrggbb\"").arg(PDFAppMcpToolSet::getColorNames().join(QStringLiteral(", ")));
    }
    return std::nullopt;
}

QString PDFAppMcpArguments::formatColor(const QColor& color)
{
    const QColor opaque = QColor(color.red(), color.green(), color.blue());
    for (const QString& name : PDFAppMcpToolSet::getColorNames())
    {
        if (PDFAppMcpArgumentsHelper::getPaletteColor(name) == opaque)
        {
            return name;
        }
    }
    return opaque.name(QColor::HexRgb);
}

std::optional<std::vector<PDFInteger>> PDFAppMcpArguments::parsePages(const QString& text, PDFInteger pageCount, int maximumPages, QString* errorMessage)
{
    if (pageCount <= 0)
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("the document has no pages");
        }
        return std::nullopt;
    }

    std::vector<PDFInteger> indices;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
    {
        const PDFInteger count = maximumPages > 0 ? qMin<PDFInteger>(pageCount, maximumPages) : pageCount;
        if (maximumPages > 0 && pageCount > maximumPages)
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("the document has %1 pages; pass at most %2 pages, such as \"1-%2\"").arg(pageCount).arg(maximumPages);
            }
            return std::nullopt;
        }

        indices.reserve(size_t(count));
        for (PDFInteger i = 0; i < count; ++i)
        {
            indices.push_back(i);
        }
        return indices;
    }

    if (trimmed.size() > 4096)
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("the page range is too long");
        }
        return std::nullopt;
    }

    // The range of the syntax of PDFClosedIntervalSet is checked against the
    // pages by PDFDocumentToolPageSelection (a range beyond the pages is refused
    // and never unfolded - PDFClosedIntervalSet::parse itself does not bound it)
    QString selectionError;
    const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(QVariant(trimmed), &selectionError);
    std::vector<PDFInteger> pages;
    if (selection)
    {
        const std::vector<QSizeF> pageSizes(size_t(qMin<PDFInteger>(pageCount, PDFDocumentToolPageSelection::MAXIMUM_PAGE_NUMBER)), QSizeF(1.0, 1.0));
        pages = selection->getPageIndices(pageSizes, &selectionError);
    }

    if (pages.empty())
    {
        if (errorMessage)
        {
            // One pass of the placeholders: a "%1" in the untrusted range is not
            // replaced by the next argument (review RC7-H)
            *errorMessage = QStringLiteral("invalid page range \"%1\" (the document has %2 pages): %3")
                                .arg(PDFMcpJson::sanitizeString(trimmed, PDFMcpJson::SanitizeFlag::RemoveBidi, 64), QString::number(pageCount), selectionError);
        }
        return std::nullopt;
    }

    if (maximumPages > 0 && pages.size() > size_t(maximumPages))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("the range has %1 pages; at most %2 pages per call").arg(pages.size()).arg(maximumPages);
        }
        return std::nullopt;
    }
    return pages;
}

std::vector<PDFInteger> PDFAppMcpArguments::findPagesByLabel(const PDFDocument* document, const QString& label)
{
    std::vector<PDFInteger> pages;
    if (!document || label.isEmpty())
    {
        return pages;
    }

    const PDFCatalog* catalog = document->getCatalog();
    const PDFInteger pageCount = PDFInteger(catalog->getPageCount());
    for (PDFInteger i = 0; i < pageCount; ++i)
    {
        if (getPageLabel(document, i) == label)
        {
            pages.push_back(i);
        }
    }
    return pages;
}

QString PDFAppMcpArguments::getPageLabel(const PDFDocument* document, PDFInteger pageIndex)
{
    if (!document)
    {
        return QString();
    }

    const QString label = document->getCatalog()->getPageLabel(pageIndex);
    return label.isEmpty() ? QString::number(pageIndex + 1) : label;
}

QString PDFAppMcpArguments::sanitizeDocumentText(const QString& text, int maximumLength)
{
    return PDFMcpJson::sanitizeString(text, PDFMcpJson::SanitizeFlag::RemoveBidi | PDFMcpJson::SanitizeFlag::KeepNewlines, maximumLength);
}

QString PDFAppMcpArguments::checkPath(const QString& path, bool requirePdfExtension, QString* errorMessage)
{
    return checkPath(path, requirePdfExtension, nullptr, errorMessage);
}

bool PDFAppMcpArguments::isInFolder(const QString& canonicalPath, const QString& folder)
{
    if (canonicalPath.isEmpty() || folder.isEmpty())
    {
        return false;
    }

    // The folder resolved as the checked path (checkPath): a folder of the data or of
    // the logs behind a junction of NTFS is its target (review RPKT 96-4)
    const QString linkedFolder = QFileInfo(folder).canonicalFilePath();
    const QString canonicalFolder = linkedFolder.isEmpty() ? QString() : PDFAppMcpArgumentsHelper::resolveJunctions(linkedFolder);
    if (canonicalFolder.isEmpty())
    {
        return false;
    }

    const Qt::CaseSensitivity sensitivity =
#ifdef Q_OS_WIN
            Qt::CaseInsensitive;
#else
            Qt::CaseSensitive;
#endif
    const QString prefix = canonicalFolder.endsWith(QLatin1Char('/')) ? canonicalFolder : canonicalFolder + QLatin1Char('/');
    return canonicalPath.compare(canonicalFolder, sensitivity) == 0 || canonicalPath.startsWith(prefix, sensitivity);
}

QString PDFAppMcpArguments::checkPath(const QString& path, bool requirePdfExtension, const PDFAppContext* context, QString* errorMessage)
{
    QString error;
    QString result;
    if (path.isEmpty() || path.size() > PDFAppMcpToolSet::MAXIMUM_PATH_LENGTH || path.contains(QChar(0)))
    {
        error = QStringLiteral("path must be an absolute path of at most %1 characters").arg(PDFAppMcpToolSet::MAXIMUM_PATH_LENGTH);
    }
    else if (PDFRepositoryHelper::isDevicePath(path) || PDFAppNetworkPaths::isNetworkPathByName(path) || PDFAppNetworkPaths().isNetworkPath(path))
    {
        error = QStringLiteral("paths of devices and of network locations are refused; use a local file");
    }
    else if (!QDir::isAbsolutePath(path))
    {
        error = QStringLiteral("path must be absolute");
    }
    else
    {
        const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
        const QFileInfo info(clean);
        const QString fileName = info.fileName();

        // A second ':' is an NTFS stream ("C:/a.pdf:stream")
        const qsizetype colon = clean.indexOf(QLatin1Char(':'));
        const bool hasStream = colon >= 0 && clean.indexOf(QLatin1Char(':'), colon + 1) >= 0;

        if (fileName.isEmpty() || hasStream || PDFAppMcpArgumentsHelper::isReservedDeviceName(fileName))
        {
            error = QStringLiteral("the file name is not allowed");
        }
        else if (requirePdfExtension && info.suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) != 0)
        {
            error = QStringLiteral("the file must have the extension .pdf");
        }
        else if (!QFileInfo(info.absolutePath()).isDir())
        {
            error = QStringLiteral("the folder of the file does not exist");
        }
        else if (info.isDir())
        {
            error = QStringLiteral("the path is a folder");
        }
        else if (info.isSymLink() && !info.exists())
        {
            // A dangling link would create its target - anywhere
            error = QStringLiteral("the path is a symbolic link, whose target does not exist");
        }
        else
        {
            // Links resolved: the file itself, when it exists, or its folder
            QString canonical;
            if (info.exists())
            {
                canonical = info.canonicalFilePath();
            }
            else
            {
                const QString folder = QFileInfo(info.absolutePath()).canonicalFilePath();
                canonical = folder.isEmpty() ? QString() : (folder.endsWith(QLatin1Char('/')) ? folder : folder + QLatin1Char('/')) + fileName;
            }
            canonical = canonical.isEmpty() ? QString() : PDFAppMcpArgumentsHelper::resolveJunctions(canonical);

            const QFileInfo canonicalInfo(canonical);
            if (canonical.isEmpty())
            {
                error = QStringLiteral("the path is not valid");
            }
            else if (PDFRepositoryHelper::isDevicePath(canonical) || PDFAppNetworkPaths::isNetworkPathByName(canonical) || PDFAppNetworkPaths().isNetworkPath(canonical))
            {
                error = QStringLiteral("the path leads to a device or a network location (a symbolic link); use a local file");
            }
            else if (canonicalInfo.isDir())
            {
                error = QStringLiteral("the path is a folder");
            }
            else if (PDFAppMcpArgumentsHelper::isReservedDeviceName(canonicalInfo.fileName()))
            {
                error = QStringLiteral("the file name is not allowed");
            }
            else if (requirePdfExtension && canonicalInfo.suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) != 0)
            {
                error = QStringLiteral("the file must have the extension .pdf (also the target of a symbolic link)");
            }
            else if ((context && !context->getStorageState().filePath.isEmpty() && isInFolder(canonical, QFileInfo(context->getStorageState().filePath).absolutePath())) ||
                     isInFolder(canonical, PDFAppLog::getLogDirectory()))
            {
                error = QStringLiteral("files in the folder of the data or of the logs of PDF4QT are refused");
            }
            else
            {
                result = PDFRepositoryHelper::normalizePath(canonical);
                if (result.isEmpty())
                {
                    error = QStringLiteral("the path is not valid");
                }
            }
        }
    }

    if (errorMessage)
    {
        *errorMessage = error;
    }
    return error.isEmpty() ? result : QString();
}

bool PDFAppMcpArguments::parseAnnotationId(const QString& id, qint64* objectNumber, qint64* generation)
{
    const QStringList parts = id.split(QLatin1Char(':'));
    if (parts.size() != 2 || parts[0].size() > 12 || parts[1].size() > 6)
    {
        return false;
    }

    bool okObject = false;
    bool okGeneration = false;
    const qint64 object = parts[0].toLongLong(&okObject);
    const qint64 gen = parts[1].toLongLong(&okGeneration);
    if (!okObject || !okGeneration || object <= 0 || gen < 0 || gen > 65535)
    {
        return false;
    }

    if (objectNumber)
    {
        *objectNumber = object;
    }
    if (generation)
    {
        *generation = gen;
    }
    return true;
}

QString PDFAppMcpArguments::formatAnnotationId(qint64 objectNumber, qint64 generation)
{
    return QStringLiteral("%1:%2").arg(objectNumber).arg(generation);
}

}   // namespace pdf
