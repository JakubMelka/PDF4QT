// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef PDFOUTPUTFORMATTER_H
#define PDFOUTPUTFORMATTER_H

#include <QString>
#include <QStringConverter>

namespace pdftool
{
class PDFOutputFormatterImpl;

/// Output formatter for text output in various format (text, xml, html...)
/// to the output console. Text output is in form of a structure tree,
/// for example, xml format.
class PDFOutputFormatter
{
public:
    enum class Style
    {
        Text,
        Xml,
        Html
    };

    explicit PDFOutputFormatter(Style style);
    ~PDFOutputFormatter();

    enum class Element
    {
        Root,               ///< Root element, this must be used only once at start/end of writing
        Header,             ///< Header
        Text,               ///< Ordinary text
        Table,              ///< Table of rows/columns (2D grid)
        TableHeaderRow,     ///< Table header row (consists of columns)
        TableHeaderColumn,  ///< Table header column
        TableRow,           ///< Table row (consists of columns)
        TableColumn         ///< Table column
    };

    /// Starts a new element in structure tree. Each call of this function must be
    /// accompanied with matching call to \p endElement function. Element can have
    /// internal name (in xml file), text description for user (as string), and reference
    /// number. Also alignment can also be specified. Element type and name must
    /// always be specified.
    /// \param type Element type
    /// \param name Internal element name (for example, xml tag if style is XML)
    /// \param description Text description for user
    /// \param alignment Cell alignment in table
    /// \param reference Reference number
    void beginElement(Element type, QString name, QString description = QString(), Qt::Alignment alignment = Qt::AlignLeft, int reference = 0);

    /// Ends current element. Must match with a call of \p beginElement
    void endElement();

    inline void beginDocument(QString name, QString description) { beginElement(Element::Root, name, description); }
    inline void endDocument() { endElement(); }
    inline void beginTable(QString name, QString description) { beginElement(Element::Table, name, description); }
    inline void endTable() { endElement(); }
    inline void beginTableHeaderRow(QString name) { beginElement(Element::TableHeaderRow, name); }
    inline void endTableHeaderRow() { endElement(); }
    inline void beginTableRow(QString name) { beginElement(Element::TableRow, name); }
    inline void beginTableRow(QString name, int reference) { beginElement(Element::TableRow, name, QString(), Qt::AlignLeft, reference); }
    inline void endTableRow() { endElement(); }
    inline void writeTableHeaderColumn(QString name, QString description, Qt::Alignment alignment = Qt::AlignLeft) { beginElement(Element::TableHeaderColumn, name, description, alignment); endElement(); }
    inline void writeTableColumn(QString name, QString description, Qt::Alignment alignment = Qt::AlignLeft) { beginElement(Element::TableColumn, name, description, alignment); endElement(); }
    inline void writeText(QString name, QString description, int reference = 0) { beginElement(Element::Text, name, description, Qt::AlignLeft, reference); endElement(); }
    inline void beginHeader(QString name, QString description, int reference = 0) { beginElement(Element::Header, name, description, Qt::AlignLeft, reference); }
    inline void endHeader() { endElement(); }

    /// Ends current line
    void endl();

    /// Get result string in unicode.
    QString getString() const;

private:
    PDFOutputFormatterImpl* m_impl;
};

class PDFConsole
{
public:

    /// Writes text to the console
    static void writeText(QString text, QStringConverter::Encoding encoding);

    /// Writes error to the console
    static void writeError(QString text, QStringConverter::Encoding encoding);

    /// Writes binary data to the console
    static void writeData(const QByteArray& data);

private:
    explicit PDFConsole() = delete;
};

}   // namespace pdftool

#endif // PDFOUTPUTFORMATTER_H
