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

#include <memory>
#include <functional>

namespace pdf
{
class PDFStreamFilter;

using PDFObjectFetcher = std::function<const PDFObject&(const PDFObject&)>;

/// Storage for stream filters. Can retrieve stream filters by name. Using singleton
/// design pattern. Use static methods to retrieve filters.
class PDFStreamFilterStorage
{
public:
    /// Retrieves filter by filter name. If filter with that name doesn't exist,
    /// then nullptr is returned. This function is thread safe.
    /// \param filterName Name of the filter to be retrieved.
    static const PDFStreamFilter* getFilter(const QByteArray& filterName);

    /// Returns decoded data from the stream
    /// \param stream Stream containing the data
    /// \param objectFetcher Function which retrieves objects (for example, reads objects from reference)
    static QByteArray getDecodedStream(const PDFStream* stream, const PDFObjectFetcher& objectFetcher);

    /// Returns decoded data from the stream, without object fetching
    /// \param stream Stream containing the data
    /// \param objectFetcher Function which retrieves objects (for example, reads objects from reference)
    static QByteArray getDecodedStream(const PDFStream* stream);

private:
    explicit PDFStreamFilterStorage();

    static const PDFStreamFilterStorage* getInstance();

    /// Maps names to the instances of the stream filters
    std::map<QByteArray, std::unique_ptr<PDFStreamFilter>> m_filters;

    /// Filter stream names can be specified in simplified (shorter) form.
    /// This map maps shorter form to the longer form.
    std::map<QByteArray, QByteArray> m_abbreviations;
};

class PDFFORQTLIBSHARED_EXPORT PDFStreamFilter
{
public:
    explicit PDFStreamFilter() = default;
    virtual ~PDFStreamFilter() = default;

    /// Apply with object fetcher
    /// \param data Stream data to be decoded
    /// \param objectFetcher Function which retrieves objects (for example, reads objects from reference)
    /// \param parameters Stream parameters
    virtual QByteArray apply(const QByteArray& data, const PDFObjectFetcher& objectFetcher, const PDFObject& parameters) const = 0;

    /// Apply without object fetcher - it assumes no references exists in the streams dictionary
    /// \param data Stream data to be decoded
    /// \param parameters Stream parameters
    inline QByteArray apply(const QByteArray& data, const PDFObject& parameters) const
    {
        return apply(data, [](const PDFObject& object) -> const PDFObject& { return object; }, parameters);
    }
};

class PDFFORQTLIBSHARED_EXPORT PDFAsciiHexDecodeFilter : public PDFStreamFilter
{
public:
    explicit PDFAsciiHexDecodeFilter() = default;
    virtual ~PDFAsciiHexDecodeFilter() override = default;

    virtual QByteArray apply(const QByteArray& data, const PDFObjectFetcher& objectFetcher, const PDFObject& parameters) const override;
};

class PDFFORQTLIBSHARED_EXPORT PDFAscii85DecodeFilter : public PDFStreamFilter
{
public:
    explicit PDFAscii85DecodeFilter() = default;
    virtual ~PDFAscii85DecodeFilter() override = default;

    virtual QByteArray apply(const QByteArray& data, const PDFObjectFetcher& objectFetcher, const PDFObject& parameters) const override;
};

class PDFFORQTLIBSHARED_EXPORT PDFLzwDecodeFilter : public PDFStreamFilter
{
public:
    explicit PDFLzwDecodeFilter() = default;
    virtual ~PDFLzwDecodeFilter() override = default;

    virtual QByteArray apply(const QByteArray& data, const PDFObjectFetcher& objectFetcher, const PDFObject& parameters) const override;
};

class PDFFORQTLIBSHARED_EXPORT PDFFlateDecodeFilter : public PDFStreamFilter
{
public:
    explicit PDFFlateDecodeFilter() = default;
    virtual ~PDFFlateDecodeFilter() override = default;

    virtual QByteArray apply(const QByteArray& data, const PDFObjectFetcher& objectFetcher, const PDFObject& parameters) const override;
};

class PDFFORQTLIBSHARED_EXPORT PDFRunLengthDecodeFilter : public PDFStreamFilter
{
public:
    explicit PDFRunLengthDecodeFilter() = default;
    virtual ~PDFRunLengthDecodeFilter() override = default;

    virtual QByteArray apply(const QByteArray& data, const PDFObjectFetcher& objectFetcher, const PDFObject& parameters) const override;
};

}   // namespace pdf

#endif // PDFSTREAMFILTERS_H
