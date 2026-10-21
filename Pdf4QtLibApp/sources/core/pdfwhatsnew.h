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

#ifndef PDFWHATSNEW_H
#define PDFWHATSNEW_H

#include "pdfappglobal.h"

#include "pdfaccenthue.h"

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVersionNumber>

#include <functional>
#include <optional>
#include <vector>

namespace pdf
{

/// Card of the section Highlights of a release
class PDF4QTLIBAPPSHARED_EXPORT PDFWhatsNewHighlight
{
public:
    QString title;
    QString description;
    QString iconName;
    PDFAccentHue hue = PDFAccentHue::Neutral;
};

/// Fixed issue of a release (number and title from RELEASES.txt)
class PDF4QTLIBAPPSHARED_EXPORT PDFWhatsNewIssue
{
public:
    int number = 0;

    /// Date of the fix (optional - an invalid date is not shown)
    QDate date;

    QString title;
    QString description;
};

/// One release of What's New (newapp_whatsnew.md 2): the header, the overview,
/// highlights, new tools (ids of the tool catalog), fixed issues and the free
/// Markdown of the full release notes
class PDF4QTLIBAPPSHARED_EXPORT PDFWhatsNewRelease
{
public:
    QVersionNumber version;

    /// Date of the release; invalid for a release, which is not released yet
    /// (header "date: unreleased")
    QDate date;
    bool isUnreleased = false;

    QString summary;
    QString overview;
    std::vector<PDFWhatsNewHighlight> highlights;
    QStringList newTools;
    std::vector<PDFWhatsNewIssue> fixedIssues;
    QString releaseNotes;

    /// Language of the file (empty = English)
    QString language;

    bool isValid() const { return !version.isNull(); }

    /// Version as it is shown (three parts: 1.6.0 of 1.6.0.0)
    QString getDisplayVersion() const;

    /// Text of the version in the routes (/whats-new/1.6.0)
    static QString getDisplayVersion(const QVersionNumber& version);
};

/// Error of a What's New file (line from 1, 0 = the whole file)
class PDF4QTLIBAPPSHARED_EXPORT PDFWhatsNewError
{
public:
    enum class Code
    {
        Empty,              ///< The file is empty
        TooLarge,           ///< The file is larger than PDFWhatsNewParser::MAXIMUM_FILE_SIZE
        MissingHeader,      ///< No header "---" or it is not closed
        UnknownKey,         ///< Unknown key of the header or of a highlight
        DuplicateKey,       ///< A key of the header is given twice
        MissingKey,         ///< A key of the header is missing
        InvalidVersion,     ///< The version is not digits separated by dots (1 - 4 parts)
        VersionMismatch,    ///< The version differs from the name of the file
        InvalidDate,        ///< The date is not an ISO date (or "unreleased" in the header)
        InvalidSummary,     ///< The summary is empty
        TextOutsideSection, ///< A text before the first section
        UnknownSection,     ///< Unknown section "## ..."
        SectionOrder,       ///< A section is out of the fixed order (or repeated)
        MissingOverview,    ///< The section Overview is missing or empty
        InvalidSyntax,      ///< A line, which does not have the form of its section
        UnknownIcon,        ///< Unknown icon of a highlight
        UnknownHue,         ///< Unknown hue of a highlight
        UnknownTool,        ///< An id, which is not a tool of the catalog
        DuplicateTool,      ///< A tool is listed twice
        DuplicateIssue,     ///< An issue is listed twice
        InvalidIssue        ///< A number of an issue out of the range
    };

    int line = 0;
    Code code = Code::InvalidSyntax;
    QString message;

    QString toString() const;
};

/// Parser of a What's New file (newapp_whatsnew.md 2, fixed form): a pure function
/// text -> release and errors with their lines. No GUI, no network; the icons and
/// the tools are checked by the functions of the options.
class PDF4QTLIBAPPSHARED_EXPORT PDFWhatsNewParser
{
public:
    PDFWhatsNewParser() = delete;

    /// Largest file, which is parsed
    static constexpr qsizetype MAXIMUM_FILE_SIZE = 256 * 1024;

    /// Largest number of an issue
    static constexpr int MAXIMUM_ISSUE_NUMBER = 1000000;

    class Options
    {
    public:
        /// Version of the name of the file (null = not checked)
        QVersionNumber expectedVersion;

        /// The icon exists (nullptr = every name of the form of an icon)
        std::function<bool(const QString&)> isIconKnown;

        /// The tool is in the catalog (nullptr = every id of the form of a tool)
        std::function<bool(const QString&)> isToolKnown;
    };

    class Result
    {
    public:
        PDFWhatsNewRelease release;
        std::vector<PDFWhatsNewError> errors;

        bool isValid() const { return errors.empty() && release.isValid(); }
    };

    static Result parse(const QString& text, const Options& options);

    /// Version of digits separated by dots (1 - 4 parts, each at most 5 digits),
    /// a null version for anything else
    static QVersionNumber parseVersion(const QString& text);

    /// Hue of its name (blue, violet, amber, red, teal, green, neutral)
    static std::optional<PDFAccentHue> parseHue(const QString& name);

    /// Name of the hue
    static QString getHueName(PDFAccentHue hue);

    /// Names of the sections in their fixed order
    static QStringList getSectionNames();
};

/// What's New files of the resources (:/whatsnew/<version>.md, translations
/// <version>.<language>.md). The list of the versions is the list of the English
/// files sorted by the version, newest first; a damaged file is not shown (its
/// errors are written to the log). The resources never change while the
/// application runs, so the catalog can be kept by a page.
class PDF4QTLIBAPPSHARED_EXPORT PDFWhatsNewCatalog
{
public:
    PDFWhatsNewCatalog() = default;

    /// Folder of the files in the resources
    static QString getResourceDirectory() { return QStringLiteral(":/whatsnew"); }

    /// Options with the icons of the design and the tools of the catalog
    static PDFWhatsNewParser::Options createDefaultOptions();

    /// Loads the English files of the folder (errors are logged and kept)
    static PDFWhatsNewCatalog load(const QString& directory = getResourceDirectory());
    static PDFWhatsNewCatalog load(const QString& directory, const PDFWhatsNewParser::Options& options);

    /// Releases sorted by the version, the newest first
    const std::vector<PDFWhatsNewRelease>& getReleases() const { return m_releases; }

    /// Release of the version (1.6.0 finds 1.6.0.0; a version of at most three parts
    /// finds the newest release of the same displayed version, 1.5.3 -> 1.5.3.1),
    /// nullptr if there is none
    const PDFWhatsNewRelease* findRelease(const QVersionNumber& version) const;

    /// Release of the language (file <version>.<language>.md, fallbacks pt_BR -> pt
    /// -> English); an invalid release, if the version has no file
    PDFWhatsNewRelease getLocalizedRelease(const QVersionNumber& version, const QString& language) const;

    /// Errors of the files ("1.6.0.0.md:12: Unknown section")
    const QStringList& getErrors() const { return m_errors; }

    /// Version of the running application (PDF4QT_PROJECT_VERSION)
    static QVersionNumber getRunningVersion();

    /// Languages tried for \p language ("pt_BR" -> "pt_BR", "pt"); empty for English
    static QStringList getLanguageFallbacks(const QString& language);

    /// The version of a file name ("1.6.0.0.md" -> 1.6.0.0, "1.6.0.0.cs.md" -> 1.6.0.0
    /// with the language "cs"); a null version for another name
    static QVersionNumber parseFileName(const QString& fileName, QString* language);

private:
    QString m_directory;
    PDFWhatsNewParser::Options m_options;
    std::vector<PDFWhatsNewRelease> m_releases;
    QStringList m_errors;
};

}   // namespace pdf

#endif // PDFWHATSNEW_H
