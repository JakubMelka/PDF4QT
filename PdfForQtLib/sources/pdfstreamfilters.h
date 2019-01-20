//    Copyright (C) 2018 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFSTREAMFILTERS_H
#define PDFSTREAMFILTERS_H

#include "pdfobject.h"

#include <QByteArray>

namespace pdf
{
class PDFDocument;

class PDFFORQTLIBSHARED_EXPORT PDFStreamFilter
{
public:
    explicit PDFStreamFilter() = default;
    virtual ~PDFStreamFilter() = default;

    virtual QByteArray apply(const QByteArray& data, const PDFDocument* document, const PDFObject& parameters) const = 0;
};

class PDFFORQTLIBSHARED_EXPORT PDFAsciiHexDecodeFilter : public PDFStreamFilter
{
public:
    explicit PDFAsciiHexDecodeFilter() = default;
    virtual ~PDFAsciiHexDecodeFilter() override = default;

    virtual QByteArray apply(const QByteArray& data, const PDFDocument* document, const PDFObject& parameters) const override;
};

class PDFFORQTLIBSHARED_EXPORT PDFAscii85DecodeFilter : public PDFStreamFilter
{
public:
    explicit PDFAscii85DecodeFilter() = default;
    virtual ~PDFAscii85DecodeFilter() override = default;

    virtual QByteArray apply(const QByteArray& data, const PDFDocument* document, const PDFObject& parameters) const override;
};

class PDFFORQTLIBSHARED_EXPORT PDFLzwDecodeFilter : public PDFStreamFilter
{
public:
    explicit PDFLzwDecodeFilter() = default;
    virtual ~PDFLzwDecodeFilter() override = default;

    virtual QByteArray apply(const QByteArray& data, const PDFDocument* document, const PDFObject& parameters) const override;
};

class PDFFORQTLIBSHARED_EXPORT PDFFlateDecodeFilter : public PDFStreamFilter
{
public:
    explicit PDFFlateDecodeFilter() = default;
    virtual ~PDFFlateDecodeFilter() override = default;

    virtual QByteArray apply(const QByteArray& data, const PDFDocument* document, const PDFObject& parameters) const override;
};

class PDFFORQTLIBSHARED_EXPORT PDFRunLengthDecodeFilter : public PDFStreamFilter
{
public:
    explicit PDFRunLengthDecodeFilter() = default;
    virtual ~PDFRunLengthDecodeFilter() override = default;

    virtual QByteArray apply(const QByteArray& data, const PDFDocument* document, const PDFObject& parameters) const override;
};

}   // namespace pdf

#endif // PDFSTREAMFILTERS_H
