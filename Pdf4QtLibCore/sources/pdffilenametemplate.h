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

#ifndef PDFFILENAMETEMPLATE_H
#define PDFFILENAMETEMPLATE_H

#include "pdfglobal.h"

#include <QCoreApplication>
#include <QDate>
#include <QString>
#include <QStringList>

#include <vector>

namespace pdf
{

/// Template of the names of the files of a tool, which writes more files (plan of step 3 of
/// phase 2, 4.2, K25, R14; B25, B26): Split, Extract one file per page, the outputs of Manage
/// Pages, the exports of Convert; PageMaster and PdfTool separate call it too.
///
/// Contract (the skeleton of step 3a declares the class, package A implements it):
///  - Tokens (K25, nothing is lost): "#" the number of the output from 1, "##..." the number
///    padded with zeros to the width of the run of '#' or of the number of the outputs,
///    whichever is wider; "@" the number of the first page of the output from 1; "%" the number
///    of the source file (Manage, more sources); "{source_base}", "{source_name}",
///    "{source_ext}" the name of the source file, "{source_page}" = "@", "{output_index}" =
///    "#", "{group_index}" the number of the group, "{group_name}" the title of the bookmark or
///    of the group, "{date}" the date in ISO 8601 (never QLocale). Every occurrence of a token
///    is replaced (B25: "part-#-of-#" never swallows the text between them); "{group_index}" is
///    not "{output_index}" (B26). PdfTool separate maps its "%" (= the page) to "@".
///  - The extension is a parameter (R14: ".pdf", ".png", ".txt"), added to the expanded name.
///  - Every expanded name is sanitized (sanitize): the result is always a plain file name.
///  - Collisions are case insensitive (findDuplicates), linear in the number of the names.
///  - At most MAXIMUM_OUTPUT_COUNT outputs (6 of the plan: "more than 10,000 files" is an error).
///  - A value; every function is reentrant.
class PDF4QTLIBCORESHARED_EXPORT PDFFileNameTemplate
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFFileNameTemplate)

public:
    /// Most outputs of one run
    static constexpr int MAXIMUM_OUTPUT_COUNT = 10000;

    /// Longest file name with its extension (UTF-16 units)
    static constexpr int MAXIMUM_FILE_NAME_LENGTH = 255;

    /// The values of the tokens of one output file
    struct Values
    {
        int outputIndex = 0;            ///< "#", "{output_index}": the number of the output from 1
        int outputCount = 0;            ///< Number of the outputs of the run (the width of "##")
        PDFInteger pageNumber = 0;      ///< "@", "{source_page}": the first page of the output from 1
        int sourceIndex = 0;            ///< "%": the number of the source file from 1
        int groupIndex = 0;             ///< "{group_index}": the number of the group from 1
        QString groupName;              ///< "{group_name}": the title of the bookmark or of the group
        QString sourceFileName;         ///< "{source_name}", "{source_base}", "{source_ext}": the name of the source file ("Handbook.pdf")
        QDate date;                     ///< "{date}"
    };

    PDFFileNameTemplate() = default;

    /// \param pattern The template written by the user ("Handbook_#")
    /// \param extension Extension of every file with its dot (".pdf")
    PDFFileNameTemplate(QString pattern, QString extension);

    const QString& getPattern() const { return m_pattern; }
    const QString& getExtension() const { return m_extension; }

    /// The template can name files: not empty, no character, which no file name may have,
    /// outside of the tokens; false and the translated reason
    bool isValid(QString* errorMessage) const;

    /// The file name of one output: the tokens replaced, sanitized, the extension added
    QString createFileName(const Values& values) const;

    /// Indices of the names, which equal (case insensitive) a name before them in the list
    static std::vector<int> findDuplicates(const QStringList& fileNames);

    /// A text made a plain file name: no control or bidirectional characters, no character
    /// forbidden by Windows, macOS or Linux, no "..", no trailing dot or space, not a reserved
    /// device name of Windows (isReservedDeviceName, moved from PDFAppToolPathRules), at most
    /// MAXIMUM_FILE_NAME_LENGTH with the extension
    static QString sanitize(const QString& name);

    /// The base name is a device of Windows (CON, PRN, AUX, NUL, COM1-9, LPT1-9, also with an
    /// extension, case insensitive)
    static bool isReservedDeviceName(const QString& name);

private:
    QString m_pattern;
    QString m_extension;
};

}   // namespace pdf

#endif // PDFFILENAMETEMPLATE_H
