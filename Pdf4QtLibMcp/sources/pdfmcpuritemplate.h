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

#ifndef PDFMCPURITEMPLATE_H
#define PDFMCPURITEMPLATE_H

#include "pdf4qtlibmcp_export.h"

#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace pdf
{

/// URI template of RFC 6570 level 1 (plan 2.2): literal text and simple
/// variables {name}. A value of a variable is one or more characters of a path
/// segment: it never contains '/', '?' or '#'; it is percent-decoded by match
/// and percent-encoded (everything except the unreserved characters) by expand.
/// "pdf4qt://documents/{documentId}/pages/{page}/image/{dpi}"
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpUriTemplate
{
public:
    PDFMcpUriTemplate() = default;

    /// Parses the template; an invalid template (unbalanced braces, an empty or
    /// invalid name, two adjacent variables, an operator of a higher level) is
    /// not valid (isValid false)
    explicit PDFMcpUriTemplate(const QString& text);

    bool isValid() const { return m_isValid; }
    const QString& getText() const { return m_text; }

    /// Names of the variables in their order
    QStringList getVariables() const;

    /// Values of the variables of a matching URI (percent-decoded, valid UTF-8,
    /// no control characters); std::nullopt when the URI does not match
    std::optional<QMap<QString, QString>> match(const QString& uri) const;

    /// URI of the values (a missing value = empty)
    QString expand(const QMap<QString, QString>& values) const;

    /// Longest URI, which is matched
    static constexpr int MAXIMUM_URI_LENGTH = 2048;

private:
    /// Literal part or variable
    class Part
    {
    public:
        bool isVariable = false;
        QString text;   ///< Literal text or the name of the variable
    };

    QString m_text;
    std::vector<Part> m_parts;
    bool m_isValid = false;
};

}   // namespace pdf

#endif // PDFMCPURITEMPLATE_H
