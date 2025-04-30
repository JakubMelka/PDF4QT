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

#include "pdfoutputformatter.h"

#include <QMutex>
#include <QTextStream>
#include <QXmlStreamWriter>
#include <QCoreApplication>
#include <QDataStream>
#include <QStringEncoder>

#include <stack>

#ifdef Q_OS_WIN
#include "Windows.h"
#endif

namespace pdftool
{

class PDFOutputFormatterImpl
{
public:
    explicit PDFOutputFormatterImpl() = default;
    virtual ~PDFOutputFormatterImpl() = default;

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
    virtual void beginElement(PDFOutputFormatter::Element type, QString name, QString description = QString(), Qt::Alignment alignment = Qt::AlignLeft, int reference = 0) = 0;

    /// Ends current element. Must match with a call of \p beginElement
    virtual void endElement() = 0;

    /// Get result string in unicode.
    virtual QString getString() const = 0;

    /// Ends current line (for formatters, that support it)
    virtual void endl() { }
};

class PDFTextOutputFormatterImpl : public PDFOutputFormatterImpl
{
public:
    PDFTextOutputFormatterImpl();

    virtual void beginElement(PDFOutputFormatter::Element type, QString name, QString description, Qt::Alignment alignment, int reference) override;
    virtual void endElement() override;
    virtual QString getString() const override;
    virtual void endl() override;

private:
    static constexpr const int INDENT_STEP = 2;

    void writeIndent();

    struct TableCell
    {
        QString text;
        Qt::Alignment alignment;
    };

    const TableCell& getTableCell(size_t row, size_t column) const;

    QString m_string;
    QTextStream m_streamWriter;
    int m_indent;
    std::stack<PDFOutputFormatter::Element> m_elementStack;
    std::vector<std::vector<TableCell>> m_table;
};

class PDFXmlOutputFormatterImpl : public PDFOutputFormatterImpl
{
public:
    PDFXmlOutputFormatterImpl();

    virtual void beginElement(PDFOutputFormatter::Element type, QString name, QString description, Qt::Alignment alignment, int reference) override;
    virtual void endElement() override;
    virtual QString getString() const override;

private:
    QString m_string;
    QString m_namespace;
    QString m_prefix;
    QXmlStreamWriter m_streamWriter;
    int m_depth;
    std::stack<PDFOutputFormatter::Element> m_elementStack;
};

class PDFHtmlOutputFormatterImpl : public PDFOutputFormatterImpl
{
public:
    PDFHtmlOutputFormatterImpl();

    virtual void beginElement(PDFOutputFormatter::Element type, QString name, QString description, Qt::Alignment alignment, int reference) override;
    virtual void endElement() override;
    virtual QString getString() const override;
    virtual void endl() override;

private:
    QString m_string;
    QXmlStreamWriter m_streamWriter;
    int m_depth;
    int m_headerDepth;
    std::stack<PDFOutputFormatter::Element> m_elementStack;
};

PDFTextOutputFormatterImpl::PDFTextOutputFormatterImpl() :
    m_string(),
    m_streamWriter(&m_string, QIODevice::WriteOnly),
    m_indent(0),
    m_elementStack()
{

}

void PDFTextOutputFormatterImpl::beginElement(PDFOutputFormatter::Element type, QString name, QString description, Qt::Alignment alignment, int reference)
{
    Q_UNUSED(name);
    Q_UNUSED(reference);

    m_elementStack.push(type);

    switch (type)
    {
        case PDFOutputFormatter::Element::Text:
        case PDFOutputFormatter::Element::Root:
        {
            writeIndent();
            m_streamWriter << description << Qt::endl;
            m_indent += INDENT_STEP;
            break;
        }

        case PDFOutputFormatter::Element::Table:
        case PDFOutputFormatter::Element::Header:
        {
            writeIndent();
            m_streamWriter << description << Qt::endl;
            m_indent += INDENT_STEP;
            break;
        }

        case PDFOutputFormatter::Element::TableRow:
        case PDFOutputFormatter::Element::TableHeaderRow:
        {
            m_table.emplace_back();
            break;
        }

        case PDFOutputFormatter::Element::TableHeaderColumn:
        case PDFOutputFormatter::Element::TableColumn:
        {
            TableCell cell;
            cell.text = QString(" %1 ").arg(description);
            cell.alignment = alignment;

            Q_ASSERT(!m_table.empty());
            m_table.back().emplace_back(qMove(cell));
            break;
        }

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }
}

void PDFTextOutputFormatterImpl::endElement()
{
    PDFOutputFormatter::Element type = m_elementStack.top();
    m_elementStack.pop();

    switch (type)
    {
        case PDFOutputFormatter::Element::Text:
        case PDFOutputFormatter::Element::Root:
        {
            m_indent -= INDENT_STEP;
            break;
        }

        case PDFOutputFormatter::Element::Table:
        {
            // Print the table
            const size_t rows = m_table.size();
            const size_t columns = (*std::max_element(m_table.cbegin(), m_table.cend(), [](const auto& l, const auto& r) { return l.size() < r.size(); })).size();

            // Detect maximal column size
            std::vector<int> columnSize(columns, 0);
            for (size_t row = 0; row < rows; ++row)
            {
                for (size_t column = 0; column < columns; ++column)
                {
                    const TableCell& tableCell = getTableCell(row, column);
                    columnSize[column] = qMax(columnSize[column], tableCell.text.size());
                }
            }

            // Print cells
            m_streamWriter.setPadChar(QChar(QChar::Space));
            for (size_t row = 0; row < rows; ++row)
            {
                for (size_t column = 0; column < columns; ++column)
                {
                    m_streamWriter.setFieldWidth(0);
                    writeIndent();

                    const TableCell& tableCell = getTableCell(row, column);
                    m_streamWriter.setFieldWidth(columnSize[column]);

                    if (tableCell.alignment.testFlag(Qt::AlignLeft))
                    {
                        m_streamWriter.setFieldAlignment(QTextStream::AlignLeft);
                    }
                    else if (tableCell.alignment.testFlag(Qt::AlignCenter))
                    {
                        m_streamWriter.setFieldAlignment(QTextStream::AlignCenter);
                    }
                    else if (tableCell.alignment.testFlag(Qt::AlignRight))
                    {
                        m_streamWriter.setFieldAlignment(QTextStream::AlignRight);
                    }

                    m_streamWriter << tableCell.text;
                }

                m_streamWriter.setFieldWidth(0);
                m_streamWriter << Qt::endl;
            }

            m_indent -= INDENT_STEP;
            m_table.clear();
            break;
        }

        case PDFOutputFormatter::Element::Header:
        {
            m_indent -= INDENT_STEP;
            break;
        }

        case PDFOutputFormatter::Element::TableRow:
        case PDFOutputFormatter::Element::TableHeaderRow:
        case PDFOutputFormatter::Element::TableHeaderColumn:
        case PDFOutputFormatter::Element::TableColumn:
        {
            break;
        }

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }
}

QString PDFTextOutputFormatterImpl::getString() const
{
    return m_string;
}

void PDFTextOutputFormatterImpl::endl()
{
    m_streamWriter << Qt::endl;
}

void PDFTextOutputFormatterImpl::writeIndent()
{
    QString str(m_indent, QChar(QChar::Space));
    m_streamWriter << str;
}

const PDFTextOutputFormatterImpl::TableCell& PDFTextOutputFormatterImpl::getTableCell(size_t row, size_t column) const
{
    if (row < m_table.size())
    {
        const auto& columns = m_table[row];
        if (column < columns.size())
        {
            return columns[column];
        }
    }

    static const TableCell dummy;
    return dummy;
}

PDFHtmlOutputFormatterImpl::PDFHtmlOutputFormatterImpl() :
    m_string(),
    m_streamWriter(&m_string),
    m_depth(0),
    m_headerDepth(1),
    m_elementStack()
{

}

void PDFHtmlOutputFormatterImpl::beginElement(PDFOutputFormatter::Element type, QString name, QString description, Qt::Alignment alignment, int reference)
{
    Q_UNUSED(reference);
    m_elementStack.push(type);

    auto writeTableCellAlignment = [this, alignment]()
    {
        if (alignment.testFlag(Qt::AlignLeft))
        {
            m_streamWriter.writeAttribute("align", "left");
        }
        if (alignment.testFlag(Qt::AlignCenter))
        {
            m_streamWriter.writeAttribute("align", "center");
        }
        if (alignment.testFlag(Qt::AlignRight))
        {
            m_streamWriter.writeAttribute("align", "right");
        }
    };

    switch (type)
    {
        case PDFOutputFormatter::Element::Root:
        {
            m_streamWriter.writeStartDocument();

            QString title = QString("%1 - Processed by %2 %3").arg(description, QCoreApplication::applicationName(), QCoreApplication::applicationVersion());
            m_streamWriter.writeStartElement("html");
            m_streamWriter.writeStartElement("head");
            m_streamWriter.writeTextElement("title", title);
            m_streamWriter.writeEndElement();
            m_streamWriter.writeStartElement("body");
            m_streamWriter.writeStartElement("p");
            m_streamWriter.writeTextElement("h1", title);
            m_streamWriter.writeEndElement();
            break;
        }

        case PDFOutputFormatter::Element::Header:
        {
            // Just print single paragraph with header
            ++m_headerDepth;
            int headerTagDepth = qBound(1, m_headerDepth, 6);
            QString headerTag = QString("h%1").arg(headerTagDepth);
            m_streamWriter.writeStartElement("p");
            m_streamWriter.writeTextElement(headerTag, description);
            m_streamWriter.writeEndElement();
            break;
        }

        case PDFOutputFormatter::Element::Text:
        {
            m_streamWriter.writeTextElement("p", description);
            break;
        }

        case PDFOutputFormatter::Element::Table:
        {
            m_streamWriter.writeStartElement("table");
            break;
        }

        case PDFOutputFormatter::Element::TableHeaderRow:
        {
            m_streamWriter.writeStartElement("tr");
            break;
        }

        case PDFOutputFormatter::Element::TableHeaderColumn:
        {
            m_streamWriter.writeStartElement("th");
            writeTableCellAlignment();
            m_streamWriter.writeCharacters(description);
            m_streamWriter.writeEndElement();
            break;
        }

        case PDFOutputFormatter::Element::TableRow:
        {
            m_streamWriter.writeStartElement("tr");
            break;
        }

        case PDFOutputFormatter::Element::TableColumn:
        {
            m_streamWriter.writeStartElement("td");
            writeTableCellAlignment();
            m_streamWriter.writeCharacters(description);
            m_streamWriter.writeEndElement();
            break;
        }

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    // Increment depth by one
    ++m_depth;
}

void PDFHtmlOutputFormatterImpl::endElement()
{
    PDFOutputFormatter::Element type = m_elementStack.top();
    m_elementStack.pop();
    --m_depth;

    switch (type)
    {
        case PDFOutputFormatter::Element::Root:
        {
            Q_ASSERT(m_depth == 0);
            m_streamWriter.writeEndElement();
            m_streamWriter.writeEndElement();
            m_streamWriter.writeEndDocument();
            break;
        }

        case PDFOutputFormatter::Element::Header:
        {
            // Just decrement header depth
            --m_headerDepth;
            break;
        }
        case PDFOutputFormatter::Element::Table:
        case PDFOutputFormatter::Element::TableHeaderRow:
        case PDFOutputFormatter::Element::TableRow:
        {
            m_streamWriter.writeEndElement();
            break;
        }

        case PDFOutputFormatter::Element::TableHeaderColumn:
        case PDFOutputFormatter::Element::TableColumn:
        case PDFOutputFormatter::Element::Text:
        {
            // Do nothing...
            break;
        }

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }
}

QString PDFHtmlOutputFormatterImpl::getString() const
{
    QString html = m_string;
    html.remove(0, html.indexOf("?>") + 2);
    html.prepend("<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" \"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">");
    return html;
}

void PDFHtmlOutputFormatterImpl::endl()
{
    m_streamWriter.writeStartElement("br");
    m_streamWriter.writeEndElement();
}

PDFXmlOutputFormatterImpl::PDFXmlOutputFormatterImpl() :
    m_string(),
    m_streamWriter(&m_string),
    m_depth(0)
{
    m_streamWriter.setAutoFormatting(true);
    m_streamWriter.setAutoFormattingIndent(2);

    m_namespace = "https://github.com/JakubMelka/PDF4QT";
    m_prefix = "pdftool";
}

void PDFXmlOutputFormatterImpl::beginElement(PDFOutputFormatter::Element type, QString name, QString description, Qt::Alignment alignment, int reference)
{
    Q_UNUSED(alignment);

    m_elementStack.push(type);

    switch (type)
    {
        case PDFOutputFormatter::Element::Root:
        {
            m_streamWriter.writeStartDocument();

            QString comment = QString("Processed by %1 %2").arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion());

            m_streamWriter.writeComment(comment);
            m_streamWriter.writeNamespace(m_namespace, m_prefix);

            m_streamWriter.writeStartElement(m_namespace, name);
            break;
        }

        case PDFOutputFormatter::Element::Text:
        case PDFOutputFormatter::Element::TableColumn:
        case PDFOutputFormatter::Element::TableHeaderColumn:
        {
            if (reference != 0)
            {
                m_streamWriter.writeStartElement(m_namespace, name);
                m_streamWriter.writeAttribute(m_namespace, "ref", QString::number(reference));
                m_streamWriter.writeCharacters(description);
                m_streamWriter.writeEndElement();
            }
            else
            {
                m_streamWriter.writeTextElement(m_namespace, name, description);
            }
            break;
        }

        default:
        {
            m_streamWriter.writeStartElement(m_namespace, name);

            if (!description.isEmpty())
            {
                m_streamWriter.writeAttribute(m_namespace, "description", description);
            }

            if (reference > 0)
            {
                m_streamWriter.writeAttribute(m_namespace, "ref", QString::number(reference));
            }
            break;
        }
    }

    // Increment depth by one
    ++m_depth;
}

void PDFXmlOutputFormatterImpl::endElement()
{
    PDFOutputFormatter::Element type = m_elementStack.top();
    m_elementStack.pop();
    --m_depth;

    switch (type)
    {
        case PDFOutputFormatter::Element::Text:
        case PDFOutputFormatter::Element::TableColumn:
        case PDFOutputFormatter::Element::TableHeaderColumn:
            break;

        default:
            m_streamWriter.writeEndElement();
            break;
    }

    // Do we finish the document? If yes, then tell stream writer to end the document
    if (m_depth == 0)
    {
        m_streamWriter.writeEndDocument();
    }
}

QString PDFXmlOutputFormatterImpl::getString() const
{
    return m_string;
}

PDFOutputFormatter::PDFOutputFormatter(Style style) :
    m_impl(nullptr)
{
    switch (style)
    {
        case Style::Text:
            m_impl = new PDFTextOutputFormatterImpl();
            break;

        case Style::Xml:
            m_impl = new PDFXmlOutputFormatterImpl();
            break;

        case Style::Html:
            m_impl = new PDFHtmlOutputFormatterImpl();
            break;
    }

    Q_ASSERT(m_impl);
}

PDFOutputFormatter::~PDFOutputFormatter()
{
    delete m_impl;
}

void PDFOutputFormatter::beginElement(PDFOutputFormatter::Element type, QString name, QString description, Qt::Alignment alignment, int reference)
{
    m_impl->beginElement(type, name, description, alignment, reference);
}

void PDFOutputFormatter::endElement()
{
    m_impl->endElement();
}

void PDFOutputFormatter::endl()
{
    m_impl->endl();
}

QString PDFOutputFormatter::getString() const
{
    return m_impl->getString();
}

void PDFConsole::writeText(QString text, QStringConverter::Encoding encoding)
{
#ifdef Q_OS_WIN
    HANDLE outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!WriteConsoleW(outputHandle, text.utf16(), text.size(), nullptr, nullptr))
    {
        // Write console failed. This can happen only, if outputHandle is not handle
        // to console screen buffer, but, for example a file or a pipe.

        QStringEncoder encoder(encoding);
        QByteArray encodedData = encoder.encode(text);
        WriteFile(outputHandle, encodedData.constData(), encodedData.size(), nullptr, nullptr);
    }
#else
    QTextStream(stdout) << text;
#endif
}

QMutex s_writeErrorMutex;

void PDFConsole::writeError(QString text, QStringConverter::Encoding encoding)
{
    if (text.isEmpty())
    {
        return;
    }

    QMutexLocker lock(&s_writeErrorMutex);

    text += "\n";

#ifdef Q_OS_WIN
    HANDLE outputHandle = GetStdHandle(STD_ERROR_HANDLE);
    if (!WriteConsoleW(outputHandle, text.utf16(), text.size(), nullptr, nullptr))
    {
        // Write console failed. This can happen only, if outputHandle is not handle
        // to console screen buffer, but, for example a file or a pipe.

        QStringEncoder encoder(encoding);
        QByteArray encodedData = encoder.encode(text);
        WriteFile(outputHandle, encodedData.constData(), encodedData.size(), nullptr, nullptr);
    }
#else
    QTextStream stream(stdout);
    stream << text;
    stream << Qt::endl;
#endif
}

void PDFConsole::writeData(const QByteArray& data)
{
    if (!data.isEmpty())
    {
        QTextStream stream(stdout);
        stream.device()->write(data);
    }
}

}   // pdftool
