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

#ifndef PDFTOOLPARAMETERREPOSITORY_H
#define PDFTOOLPARAMETERREPOSITORY_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"
#include "pdfdocumenttoolparameter.h"

#include <QPointer>
#include <QString>
#include <QVariantMap>

namespace pdf
{

/// Last values of the parameters of the tools (table tool_parameter,
/// newapp_storage.md 4): what the start page of a tool of phase 2 shows the next
/// time. The values are written and read through the definitions of the
/// parameters of the tool (PDFDocumentToolParameterDefinition::toStorageValue /
/// fromStorageValue), so a stored value is always validated: a hostile or
/// foreign value (another version, another tool) gives the default. A Secret is
/// never stored and never read. Only ids of PDFDocumentToolCatalog are written. A
/// value equal to the default is not stored (a new default of a later version
/// applies). A value object usable in any thread; notified with the topic
/// getTopic (key = tool id). PROVISIONAL until the pilot tool Compress.
class PDF4QTLIBAPPSHARED_EXPORT PDFToolParameterRepository
{
public:
    static QString getTopic() { return QStringLiteral("tool_parameter"); }

    /// Most rows of one tool, which are read
    static constexpr int MAXIMUM_READ_ROWS = 256;

    explicit PDFToolParameterRepository(PDFDatabase* database);

    /// Values of all parameters of the tool: the stored valid values, the defaults
    /// of the others (a required parameter without a stored value is left out)
    QVariantMap getValues(const QString& toolId, const PDFDocumentToolParameterDefinitions& definitions) const;

    /// Stores the last values of the tool in one transaction: every parameter of
    /// \p definitions present in \p values, except Secrets (left out; a stored row of a
    /// Secret is deleted). Invalid values or definitions, an unknown tool or key
    /// refuse the whole write.
    PDFDatabaseError setValues(const QString& toolId, const PDFDocumentToolParameterDefinitions& definitions, const QVariantMap& values);

    /// Stores one value; a Secret is refused with an error
    PDFDatabaseError setValue(const QString& toolId, const PDFDocumentToolParameterDefinition& definition, const QVariant& value);

    /// Deletes the stored values of the tool (the defaults apply)
    PDFDatabaseError clearValues(const QString& toolId);

private:
    PDFDatabaseConnection* getConnection() const;

    QPointer<PDFDatabase> m_database;
};

}   // namespace pdf

#endif // PDFTOOLPARAMETERREPOSITORY_H
