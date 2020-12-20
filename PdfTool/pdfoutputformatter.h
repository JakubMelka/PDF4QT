//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFOUTPUTFORMATTER_H
#define PDFOUTPUTFORMATTER_H

#include <QString>

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

    explicit PDFOutputFormatter(Style style, QString codecName);
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
    static void writeText(QString text, QString codecName);

    /// Writes error to the console
    static void writeError(QString text, QString codecName);

    /// Writes binary data to the console
    static void writeData(const QByteArray& data);

private:
    explicit PDFConsole() = delete;
};

}   // namespace pdftool

#endif // PDFOUTPUTFORMATTER_H
