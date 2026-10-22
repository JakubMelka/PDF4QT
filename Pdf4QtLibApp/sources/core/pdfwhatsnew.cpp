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

#include "pdfwhatsnew.h"

#include "pdfdocumenttoolcatalog.h"
#include "pdfthemeiconengine.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QRegularExpression>

#include <algorithm>
#include <set>

namespace pdf
{

/// Helpers of the parser of the What's New files
class PDFWhatsNewParserHelper
{
public:
    PDFWhatsNewParserHelper() = delete;

    enum Section
    {
        None = -1,
        Overview = 0,
        Highlights,
        NewTools,
        FixedIssues,
        ReleaseNotes
    };

    static void addError(PDFWhatsNewParser::Result& result, int line, PDFWhatsNewError::Code code, const QString& message);
    static bool isIdentifier(const QString& text);
    static bool isBlank(const QString& line) { return line.trimmed().isEmpty(); }
    static QStringList splitParts(const QString& text);
    static int parseHeader(const QStringList& lines, const PDFWhatsNewParser::Options& options, PDFWhatsNewParser::Result& result);
    static void finishHighlight(PDFWhatsNewParser::Result& result, PDFWhatsNewHighlight* highlight, int line, bool hasIconLine, QStringList& description);
};

/// Helpers of the catalog of the What's New files
class PDFWhatsNewCatalogHelper
{
public:
    PDFWhatsNewCatalogHelper() = delete;

    /// The tool is in the static catalog of the tools
    static bool isToolKnown(const QString& id)
    {
        return PDFDocumentToolCatalog::findTool(id) != nullptr;
    }

    /// Order of the releases: the newest first
    static bool isNewer(const PDFWhatsNewRelease& left, const PDFWhatsNewRelease& right)
    {
        return QVersionNumber::compare(left.version.normalized(), right.version.normalized()) > 0;
    }
};

void PDFWhatsNewParserHelper::addError(PDFWhatsNewParser::Result& result, int line, PDFWhatsNewError::Code code, const QString& message)
{
    PDFWhatsNewError error;
    error.line = line;
    error.code = code;
    error.message = message;
    result.errors.push_back(std::move(error));
}

bool PDFWhatsNewParserHelper::isIdentifier(const QString& text)
{
    if (text.isEmpty() || text.size() > 64)
    {
        return false;
    }

    for (const QChar character : text)
    {
        const char16_t code = character.unicode();
        if (!((code >= u'a' && code <= u'z') || (code >= u'0' && code <= u'9') || code == u'.' || code == u'-' || code == u'_'))
        {
            return false;
        }
    }
    return true;
}

QStringList PDFWhatsNewParserHelper::splitParts(const QString& text)
{
    // Parts are separated by a middle dot " · "
    QStringList parts = text.split(QChar(0x00B7));
    for (QString& part : parts)
    {
        part = part.trimmed();
    }
    return parts;
}

QString PDFWhatsNewError::toString() const
{
    return QStringLiteral("%1: %2").arg(line).arg(message);
}

QString PDFWhatsNewRelease::getDisplayVersion() const
{
    return getDisplayVersion(version);
}

QString PDFWhatsNewRelease::getDisplayVersion(const QVersionNumber& version)
{
    if (version.isNull())
    {
        return QString();
    }

    QList<int> segments = version.segments();
    while (segments.size() < 3)
    {
        segments << 0;
    }
    return QStringLiteral("%1.%2.%3").arg(segments[0]).arg(segments[1]).arg(segments[2]);
}

QVersionNumber PDFWhatsNewParser::parseVersion(const QString& text)
{
    const QStringList parts = text.split(QChar(u'.'));
    if (parts.isEmpty() || parts.size() > 4)
    {
        return QVersionNumber();
    }

    QList<int> segments;
    for (const QString& part : parts)
    {
        if (part.isEmpty() || part.size() > 5)
        {
            return QVersionNumber();
        }
        for (const QChar character : part)
        {
            if (character.unicode() < u'0' || character.unicode() > u'9')
            {
                return QVersionNumber();
            }
        }
        segments << part.toInt();
    }
    return QVersionNumber(segments);
}

std::optional<PDFAccentHue> PDFWhatsNewParser::parseHue(const QString& name)
{
    for (PDFAccentHue hue : { PDFAccentHue::Blue, PDFAccentHue::Violet, PDFAccentHue::Amber, PDFAccentHue::Red,
                              PDFAccentHue::Teal, PDFAccentHue::Green, PDFAccentHue::Neutral })
    {
        if (getHueName(hue) == name)
        {
            return hue;
        }
    }
    return std::nullopt;
}

QString PDFWhatsNewParser::getHueName(PDFAccentHue hue)
{
    switch (hue)
    {
        case PDFAccentHue::Blue:
            return QStringLiteral("blue");
        case PDFAccentHue::Violet:
            return QStringLiteral("violet");
        case PDFAccentHue::Amber:
            return QStringLiteral("amber");
        case PDFAccentHue::Red:
            return QStringLiteral("red");
        case PDFAccentHue::Teal:
            return QStringLiteral("teal");
        case PDFAccentHue::Green:
            return QStringLiteral("green");
        case PDFAccentHue::Neutral:
            return QStringLiteral("neutral");
    }
    return QStringLiteral("neutral");
}

QStringList PDFWhatsNewParser::getSectionNames()
{
    return { QStringLiteral("Overview"), QStringLiteral("Highlights"), QStringLiteral("New tools"), QStringLiteral("Fixed issues"), QStringLiteral("Release notes") };
}

int PDFWhatsNewParserHelper::parseHeader(const QStringList& lines, const PDFWhatsNewParser::Options& options, PDFWhatsNewParser::Result& result)
{
    using Code = PDFWhatsNewError::Code;

    if (lines.isEmpty() || lines.first().trimmed() != QStringLiteral("---"))
    {
        addError(result, 1, Code::MissingHeader, QStringLiteral("The file must start with the header \"---\""));
        return -1;
    }

    std::set<QString> keys;
    int closingLine = -1;
    for (int i = 1; i < lines.size(); ++i)
    {
        const QString line = lines.at(i);
        const int lineNumber = i + 1;
        if (line.trimmed() == QStringLiteral("---"))
        {
            closingLine = i;
            break;
        }

        if (isBlank(line))
        {
            continue;
        }

        const qsizetype colon = line.indexOf(QChar(u':'));
        if (colon <= 0)
        {
            addError(result, lineNumber, Code::InvalidSyntax, QStringLiteral("Expected \"key: value\" in the header"));
            continue;
        }

        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        if (key != QStringLiteral("version") && key != QStringLiteral("date") && key != QStringLiteral("summary"))
        {
            addError(result, lineNumber, Code::UnknownKey, QStringLiteral("Unknown key \"%1\" of the header").arg(key.left(64)));
            continue;
        }

        if (!keys.insert(key).second)
        {
            addError(result, lineNumber, Code::DuplicateKey, QStringLiteral("The key \"%1\" is given twice").arg(key));
            continue;
        }

        if (key == QStringLiteral("version"))
        {
            result.release.version = PDFWhatsNewParser::parseVersion(value);
            if (result.release.version.isNull())
            {
                addError(result, lineNumber, Code::InvalidVersion, QStringLiteral("Invalid version \"%1\"").arg(value.left(64)));
            }
            else if (!options.expectedVersion.isNull() && result.release.version.normalized() != options.expectedVersion.normalized())
            {
                addError(result, lineNumber, Code::VersionMismatch, QStringLiteral("The version %1 differs from the name of the file (%2)")
                         .arg(result.release.version.toString(), options.expectedVersion.toString()));
            }
        }
        else if (key == QStringLiteral("date"))
        {
            if (value == QStringLiteral("unreleased"))
            {
                result.release.isUnreleased = true;
            }
            else
            {
                static const QRegularExpression datePattern(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
                result.release.date = datePattern.match(value).hasMatch() ? QDate::fromString(value, Qt::ISODate) : QDate();
                if (!result.release.date.isValid())
                {
                    addError(result, lineNumber, Code::InvalidDate, QStringLiteral("Invalid date \"%1\" (YYYY-MM-DD or unreleased)").arg(value.left(64)));
                }
            }
        }
        else
        {
            result.release.summary = value;
            if (value.isEmpty())
            {
                addError(result, lineNumber, Code::InvalidSummary, QStringLiteral("The summary is empty"));
            }
        }
    }

    if (closingLine < 0)
    {
        addError(result, int(lines.size()), Code::MissingHeader, QStringLiteral("The header is not closed by \"---\""));
        return -1;
    }

    for (const char* key : { "version", "date", "summary" })
    {
        if (keys.count(QString::fromLatin1(key)) == 0)
        {
            addError(result, closingLine + 1, Code::MissingKey, QStringLiteral("The header has no key \"%1\"").arg(QString::fromLatin1(key)));
        }
    }

    return closingLine + 1;
}

void PDFWhatsNewParserHelper::finishHighlight(PDFWhatsNewParser::Result& result, PDFWhatsNewHighlight* highlight, int line, bool hasIconLine, QStringList& description)
{
    if (!highlight)
    {
        return;
    }

    if (!hasIconLine)
    {
        addError(result, line, PDFWhatsNewError::Code::InvalidSyntax, QStringLiteral("The highlight \"%1\" has no line \"icon: <name> · hue: <hue>\"").arg(highlight->title.left(64)));
    }

    highlight->description = description.join(QChar(u' ')).trimmed();
    if (highlight->description.isEmpty())
    {
        addError(result, line, PDFWhatsNewError::Code::InvalidSyntax, QStringLiteral("The highlight \"%1\" has no description").arg(highlight->title.left(64)));
    }
    description.clear();
}

PDFWhatsNewParser::Result PDFWhatsNewParser::parse(const QString& text, const Options& options)
{
    using Helper = PDFWhatsNewParserHelper;
    using Code = PDFWhatsNewError::Code;

    Result result;
    if (text.size() > MAXIMUM_FILE_SIZE)
    {
        Helper::addError(result, 0, Code::TooLarge, QStringLiteral("The file is too large"));
        return result;
    }

    QString normalized = text;
    if (normalized.startsWith(QChar(0xFEFF)))
    {
        normalized.remove(0, 1);
    }
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QChar(u'\r'), QChar(u'\n'));

    if (normalized.trimmed().isEmpty())
    {
        Helper::addError(result, 1, Code::Empty, QStringLiteral("The file is empty"));
        return result;
    }

    const QStringList lines = normalized.split(QChar(u'\n'));
    const int bodyStart = Helper::parseHeader(lines, options, result);
    if (bodyStart < 0)
    {
        return result;
    }

    const QStringList sectionNames = getSectionNames();
    int section = Helper::None;
    QStringList overview;
    bool isOverviewClosed = false;
    QStringList releaseNotes;
    std::set<int> issueNumbers;
    std::set<QString> tools;

    PDFWhatsNewHighlight* highlight = nullptr;
    int highlightLine = 0;
    bool hasIconLine = false;
    QStringList highlightDescription;

    PDFWhatsNewIssue* issue = nullptr;

    for (int i = bodyStart; i < lines.size(); ++i)
    {
        const QString& line = lines.at(i);
        const int lineNumber = i + 1;

        // Everything after "## Release notes" is free Markdown
        if (section == Helper::ReleaseNotes)
        {
            releaseNotes << line;
            continue;
        }

        if (line.startsWith(QStringLiteral("## ")) || line.trimmed() == QStringLiteral("##"))
        {
            if (section == Helper::Highlights)
            {
                Helper::finishHighlight(result, highlight, highlightLine, hasIconLine, highlightDescription);
                highlight = nullptr;
            }
            issue = nullptr;

            const QString name = line.mid(2).trimmed();
            const int index = int(sectionNames.indexOf(name));
            if (index < 0)
            {
                Helper::addError(result, lineNumber, Code::UnknownSection, QStringLiteral("Unknown section \"%1\"").arg(name.left(64)));
                section = Helper::None;
                continue;
            }

            if (index <= section || (index == Helper::Overview && !overview.isEmpty()))
            {
                Helper::addError(result, lineNumber, Code::SectionOrder, QStringLiteral("The section \"%1\" is out of the order %2").arg(name, sectionNames.join(QStringLiteral(", "))));
            }
            section = index;
            continue;
        }

        switch (section)
        {
            case Helper::None:
                if (!Helper::isBlank(line))
                {
                    Helper::addError(result, lineNumber, Code::TextOutsideSection, QStringLiteral("A text outside of a section"));
                }
                break;

            case Helper::Overview:
                if (Helper::isBlank(line))
                {
                    isOverviewClosed = !overview.isEmpty();
                }
                else if (isOverviewClosed)
                {
                    Helper::addError(result, lineNumber, Code::InvalidSyntax, QStringLiteral("The overview is one paragraph"));
                }
                else
                {
                    overview << line.trimmed();
                }
                break;

            case Helper::Highlights:
            {
                const QString trimmed = line.trimmed();
                if (line.startsWith(QStringLiteral("### ")))
                {
                    Helper::finishHighlight(result, highlight, highlightLine, hasIconLine, highlightDescription);
                    result.release.highlights.emplace_back();
                    highlight = &result.release.highlights.back();
                    highlight->title = line.mid(4).trimmed();
                    highlightLine = lineNumber;
                    hasIconLine = false;
                    if (highlight->title.isEmpty())
                    {
                        Helper::addError(result, lineNumber, Code::InvalidSyntax, QStringLiteral("The highlight has no title"));
                    }
                }
                else if (trimmed.isEmpty())
                {
                    break;
                }
                else if (!highlight)
                {
                    Helper::addError(result, lineNumber, Code::InvalidSyntax, QStringLiteral("A highlight starts by \"### Title\""));
                }
                else if (!hasIconLine)
                {
                    hasIconLine = true;
                    bool hasIcon = false;
                    bool hasHue = false;
                    for (const QString& part : Helper::splitParts(trimmed))
                    {
                        const qsizetype colon = part.indexOf(QChar(u':'));
                        const QString key = colon > 0 ? part.left(colon).trimmed() : part;
                        const QString value = colon > 0 ? part.mid(colon + 1).trimmed() : QString();
                        if (key == QStringLiteral("icon") && colon > 0)
                        {
                            hasIcon = true;
                            highlight->iconName = value;
                            const bool isKnown = Helper::isIdentifier(value) && (!options.isIconKnown || options.isIconKnown(value));
                            if (!isKnown)
                            {
                                Helper::addError(result, lineNumber, Code::UnknownIcon, QStringLiteral("Unknown icon \"%1\"").arg(value.left(64)));
                            }
                        }
                        else if (key == QStringLiteral("hue") && colon > 0)
                        {
                            hasHue = true;
                            const std::optional<PDFAccentHue> hue = parseHue(value);
                            if (hue)
                            {
                                highlight->hue = *hue;
                            }
                            else
                            {
                                Helper::addError(result, lineNumber, Code::UnknownHue, QStringLiteral("Unknown hue \"%1\"").arg(value.left(64)));
                            }
                        }
                        else
                        {
                            Helper::addError(result, lineNumber, Code::UnknownKey, QStringLiteral("Unknown key \"%1\" of a highlight").arg(key.left(64)));
                        }
                    }

                    if (!hasIcon || !hasHue)
                    {
                        Helper::addError(result, lineNumber, Code::InvalidSyntax, QStringLiteral("Expected \"icon: <name> · hue: <hue>\""));
                    }
                }
                else
                {
                    highlightDescription << trimmed;
                }
                break;
            }

            case Helper::NewTools:
            {
                const QString trimmed = line.trimmed();
                if (trimmed.isEmpty())
                {
                    break;
                }

                if (!trimmed.startsWith(QStringLiteral("- ")))
                {
                    Helper::addError(result, lineNumber, Code::InvalidSyntax, QStringLiteral("Expected \"- <tool id>\""));
                    break;
                }

                const QString id = trimmed.mid(2).trimmed();
                if (!Helper::isIdentifier(id) || (options.isToolKnown && !options.isToolKnown(id)))
                {
                    Helper::addError(result, lineNumber, Code::UnknownTool, QStringLiteral("Unknown tool \"%1\"").arg(id.left(64)));
                }
                else if (!tools.insert(id).second)
                {
                    Helper::addError(result, lineNumber, Code::DuplicateTool, QStringLiteral("The tool \"%1\" is listed twice").arg(id));
                }
                else
                {
                    result.release.newTools << id;
                }
                break;
            }

            case Helper::FixedIssues:
            {
                if (Helper::isBlank(line))
                {
                    issue = nullptr;
                    break;
                }

                if (line.startsWith(QStringLiteral("  ")) && issue)
                {
                    // Indented line = description of the issue
                    issue->description = (issue->description + QChar(u' ') + line.trimmed()).trimmed();
                    break;
                }

                const QString trimmed = line.trimmed();
                if (!trimmed.startsWith(QStringLiteral("- #")))
                {
                    Helper::addError(result, lineNumber, Code::InvalidSyntax, QStringLiteral("Expected \"- #<number> · <date> · <title>\""));
                    issue = nullptr;
                    break;
                }

                const QStringList parts = Helper::splitParts(trimmed.mid(3));
                if (parts.size() < 2 || parts.size() > 3 || parts.last().isEmpty())
                {
                    Helper::addError(result, lineNumber, Code::InvalidSyntax, QStringLiteral("Expected \"- #<number> · <date> · <title>\""));
                    issue = nullptr;
                    break;
                }

                bool isNumber = false;
                const int number = parts.first().toInt(&isNumber);
                if (!isNumber || number <= 0 || number > MAXIMUM_ISSUE_NUMBER || parts.first().startsWith(QChar(u'+')))
                {
                    Helper::addError(result, lineNumber, Code::InvalidIssue, QStringLiteral("Invalid number of an issue \"%1\"").arg(parts.first().left(16)));
                    issue = nullptr;
                    break;
                }

                QDate date;
                if (parts.size() == 3)
                {
                    static const QRegularExpression datePattern(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
                    date = datePattern.match(parts.at(1)).hasMatch() ? QDate::fromString(parts.at(1), Qt::ISODate) : QDate();
                    if (!date.isValid())
                    {
                        Helper::addError(result, lineNumber, Code::InvalidDate, QStringLiteral("Invalid date \"%1\" of the issue").arg(parts.at(1).left(64)));
                    }
                }

                if (!issueNumbers.insert(number).second)
                {
                    Helper::addError(result, lineNumber, Code::DuplicateIssue, QStringLiteral("The issue #%1 is listed twice").arg(number));
                    issue = nullptr;
                    break;
                }

                result.release.fixedIssues.emplace_back();
                issue = &result.release.fixedIssues.back();
                issue->number = number;
                issue->date = date;
                issue->title = parts.last();
                break;
            }

            default:
                break;
        }
    }

    if (section == Helper::Highlights)
    {
        Helper::finishHighlight(result, highlight, highlightLine, hasIconLine, highlightDescription);
    }

    result.release.overview = overview.join(QChar(u' ')).trimmed();
    if (result.release.overview.isEmpty())
    {
        Helper::addError(result, bodyStart + 1, Code::MissingOverview, QStringLiteral("The section \"Overview\" is missing or empty"));
    }

    result.release.releaseNotes = releaseNotes.join(QChar(u'\n')).trimmed();
    return result;
}

PDFWhatsNewParser::Options PDFWhatsNewCatalog::createDefaultOptions()
{
    PDFWhatsNewParser::Options options;
    options.isIconKnown = &PDFThemeIcons::hasIcon;
    options.isToolKnown = &PDFWhatsNewCatalogHelper::isToolKnown;
    return options;
}

QVersionNumber PDFWhatsNewCatalog::getRunningVersion()
{
    return PDFWhatsNewParser::parseVersion(QStringLiteral(PDF4QT_PROJECT_VERSION));
}

QStringList PDFWhatsNewCatalog::getLanguageFallbacks(const QString& language)
{
    QStringList languages;
    if (language.isEmpty() || language == QStringLiteral("en") || language.startsWith(QStringLiteral("en_")))
    {
        return languages;
    }

    static const QRegularExpression languagePattern(QStringLiteral("^[a-z]{2,3}(_[A-Z]{2})?$"));
    if (!languagePattern.match(language).hasMatch())
    {
        return languages;
    }

    languages << language;
    const qsizetype underscore = language.indexOf(QChar(u'_'));
    if (underscore > 0)
    {
        languages << language.left(underscore);
    }
    return languages;
}

QVersionNumber PDFWhatsNewCatalog::parseFileName(const QString& fileName, QString* language)
{
    if (language)
    {
        language->clear();
    }

    if (!fileName.endsWith(QStringLiteral(".md")))
    {
        return QVersionNumber();
    }

    const QString base = fileName.chopped(3);
    const QVersionNumber version = PDFWhatsNewParser::parseVersion(base);
    if (!version.isNull())
    {
        return version;
    }

    // Translated file <version>.<language>.md
    const qsizetype dot = base.lastIndexOf(QChar(u'.'));
    if (dot <= 0)
    {
        return QVersionNumber();
    }

    const QString suffix = base.mid(dot + 1);
    static const QRegularExpression languagePattern(QStringLiteral("^[a-z]{2,3}(_[A-Z]{2})?$"));
    const QVersionNumber translatedVersion = PDFWhatsNewParser::parseVersion(base.left(dot));
    if (translatedVersion.isNull() || !languagePattern.match(suffix).hasMatch())
    {
        return QVersionNumber();
    }

    if (language)
    {
        *language = suffix;
    }
    return translatedVersion;
}

std::shared_ptr<const PDFWhatsNewCatalog> PDFWhatsNewCatalog::getBuiltin()
{
    static const std::shared_ptr<const PDFWhatsNewCatalog> catalog = std::make_shared<const PDFWhatsNewCatalog>(load());
    return catalog;
}

PDFWhatsNewCatalog PDFWhatsNewCatalog::load(const QString& directory)
{
    return load(directory, createDefaultOptions());
}

PDFWhatsNewCatalog PDFWhatsNewCatalog::load(const QString& directory, const PDFWhatsNewParser::Options& options)
{
    PDFWhatsNewCatalog catalog;
    catalog.m_directory = directory;
    catalog.m_options = options;

    const QDir folder(directory);
    const QStringList files = folder.entryList(QStringList() << QStringLiteral("*.md"), QDir::Files, QDir::Name);
    for (const QString& fileName : files)
    {
        QString language;
        const QVersionNumber version = parseFileName(fileName, &language);
        if (version.isNull())
        {
            catalog.m_errors << QStringLiteral("%1: not a file of a version").arg(fileName);
            continue;
        }

        if (!language.isEmpty())
        {
            // Translations are loaded on demand (getLocalizedRelease)
            continue;
        }

        QFile file(folder.filePath(fileName));
        if (file.size() > PDFWhatsNewParser::MAXIMUM_FILE_SIZE * 4 || !file.open(QFile::ReadOnly))
        {
            catalog.m_errors << QStringLiteral("%1: cannot be read").arg(fileName);
            continue;
        }

        PDFWhatsNewParser::Options fileOptions = options;
        fileOptions.expectedVersion = version;
        PDFWhatsNewParser::Result result = PDFWhatsNewParser::parse(QString::fromUtf8(file.readAll()), fileOptions);
        if (!result.isValid())
        {
            for (const PDFWhatsNewError& error : result.errors)
            {
                catalog.m_errors << QStringLiteral("%1:%2").arg(fileName, error.toString());
            }
            continue;
        }

        // Two files of one version (1.6.md, 1.6.0.0.md): the first one wins
        if (catalog.findRelease(version))
        {
            catalog.m_errors << QStringLiteral("%1: the version is given by another file").arg(fileName);
            continue;
        }

        catalog.m_releases.push_back(std::move(result.release));
    }

    std::sort(catalog.m_releases.begin(), catalog.m_releases.end(), &PDFWhatsNewCatalogHelper::isNewer);

    for (const QString& error : catalog.m_errors)
    {
        qWarning("What's New file %s", qPrintable(error));
    }

    return catalog;
}

const PDFWhatsNewRelease* PDFWhatsNewCatalog::findRelease(const QVersionNumber& version) const
{
    if (version.isNull())
    {
        return nullptr;
    }

    const QVersionNumber normalized = version.normalized();
    for (const PDFWhatsNewRelease& release : m_releases)
    {
        if (release.version.normalized() == normalized)
        {
            return &release;
        }
    }

    // The routes have the displayed version of three parts: 1.5.3 is the newest
    // release 1.5.3.x (1.5.3.1, when 1.5.3.0 was removed)
    if (version.segmentCount() <= 3)
    {
        const QString displayVersion = PDFWhatsNewRelease::getDisplayVersion(version);
        for (const PDFWhatsNewRelease& release : m_releases)
        {
            if (release.getDisplayVersion() == displayVersion)
            {
                return &release;
            }
        }
    }
    return nullptr;
}

PDFWhatsNewRelease PDFWhatsNewCatalog::getLocalizedRelease(const QVersionNumber& version, const QString& language) const
{
    const PDFWhatsNewRelease* englishRelease = findRelease(version);
    if (!englishRelease)
    {
        return PDFWhatsNewRelease();
    }

    const QDir folder(m_directory);
    for (const QString& candidate : getLanguageFallbacks(language))
    {
        const QString fileName = QStringLiteral("%1.%2.md").arg(englishRelease->version.toString(), candidate);
        QFile file(folder.filePath(fileName));
        if (!file.exists() || file.size() > PDFWhatsNewParser::MAXIMUM_FILE_SIZE * 4 || !file.open(QFile::ReadOnly))
        {
            continue;
        }

        PDFWhatsNewParser::Options options = m_options;
        options.expectedVersion = englishRelease->version;
        PDFWhatsNewParser::Result result = PDFWhatsNewParser::parse(QString::fromUtf8(file.readAll()), options);
        if (result.isValid())
        {
            result.release.language = candidate;
            return result.release;
        }

        for (const PDFWhatsNewError& error : result.errors)
        {
            qWarning("What's New file %s:%s", qPrintable(fileName), qPrintable(error.toString()));
        }
    }

    return *englishRelease;
}

}   // namespace pdf
