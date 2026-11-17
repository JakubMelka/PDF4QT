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

#ifndef PDFSTREAMFILTERS_H
#define PDFSTREAMFILTERS_H

#include "pdfobject.h"

#include <QByteArray>
#include <QString>

#include <memory>
#include <functional>

namespace pdf
{
class PDFStreamFilter;
class PDFSecurityHandler;
class PDFOperationControl;

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
    /// \param securityHandler Security handler for Crypt filters
    static QByteArray getDecodedStream(const PDFStream* stream, const PDFObjectFetcher& objectFetcher, const PDFSecurityHandler* securityHandler);

    /// Returns decoded data from the stream, without object fetching
    /// \param stream Stream containing the data
    /// \param objectFetcher Function which retrieves objects (for example, reads objects from reference)
    /// \param securityHandler Security handler for Crypt filters
    static QByteArray getDecodedStream(const PDFStream* stream, const PDFSecurityHandler* securityHandler);

    /// Tries to find stream data length using given filter. Stream will
    /// start at given \p offset in \p data. If stream length cannot be determined,
    /// then -1 is returned.
    /// \param data Buffer data
    /// \param filterName Filter name
    /// \param offset Offset to buffer, at which stream data starts
    static PDFInteger getStreamDataLength(const QByteArray& data, const QByteArray& filterName, PDFInteger offset);

    struct StreamFilters
    {
        bool valid = true;
        std::vector<const PDFStreamFilter*> filterObjects;
        std::vector<PDFObject> filterParameterObjects;
    };

    /// Returns stream filters along with it's parameters, for this stream
    /// \param stream Stream containing data
    /// \param objectFetcher Function which retrieves objects (for example, reads objects from reference)
    static StreamFilters getStreamFilters(const PDFStream* stream, const PDFObjectFetcher& objectFetcher);

private:
    explicit PDFStreamFilterStorage();

    static const PDFStreamFilterStorage* getInstance();

    /// Maps names to the instances of the stream filters
    std::map<QByteArray, std::unique_ptr<PDFStreamFilter>> m_filters;

    /// Filter stream names can be specified in simplified (shorter) form.
    /// This map maps shorter form to the longer form.
    std::map<QByteArray, QByteArray> m_abbreviations;
};

/// Decoding of a stream with an upper limit of the decoded size (plan of step 7 of phase 2, R7-3, owner
/// answer Q7.18 a): the Object Inspector, Structure as XML with the streams and the MCP action
/// review.object-inspector decode the streams of an untrusted file on request, a decompression bomb must
/// not end the process (no handling of a lack of memory, owner 2026-11-05). Only these new consumers call
/// it; the general limit of every decoding (rendering, SECURITY_TODO.md item 1) stays in phase 3.
///
/// Contract (package P1a implements it; the skeleton has a stub, which returns Status::Error):
///  - the filters of PDFStreamFilterStorage run in their order (Crypt, ASCIIHex, ASCII85, LZW, Flate,
///    RunLength) and each one stops as soon as its output would exceed \p maximumDecodedSize - also an
///    intermediate output of a chain of filters, never only the final one; the memory used is bounded by
///    about the limit plus the input, never by the declared or the real decoded size;
///  - a filter, which is not a decoder of the storage (DCTDecode, JPXDecode, JBIG2Decode, CCITTFaxDecode,
///    an unknown name), ends the decoding: the data decoded so far are the result with Status::Encoded and
///    the name in \p filterName (the inspector shows a hex dump, FD-3 / FD-4; an image is shown by PDFImage);
///  - an error of a filter (a broken Flate stream) gives Status::Error with the name of the filter and its
///    message (FD-4), never an exception to the caller;
///  - the cancellation is checked at least after each filter and each MiB of output (Status::Canceled);
///  - reentrant, the stream and the security handler are read only (a worker thread of the inspector).
class PDF4QTLIBCORESHARED_EXPORT PDFLimitedStreamDecoder
{
public:
    PDFLimitedStreamDecoder() = delete;

    /// The limit of the Object Inspector (plan 4.3, 6.2): 256 MiB of decoded data
    static constexpr qint64 INSPECTOR_MAXIMUM_DECODED_SIZE = qint64(256) * 1024 * 1024;

    enum class Status
    {
        Decoded,        ///< Every filter was applied, \p data are the decoded data
        Encoded,        ///< A filter, which is not a decoder of streams (an image filter, an unknown one), stopped the decoding: \p data are encoded by \p filterName
        LimitExceeded,  ///< The output of a filter would exceed the limit: \p data are empty
        Error,          ///< A filter has failed (\p filterName, \p errorMessage): \p data are empty
        Canceled        ///< The operation was canceled: \p data are empty
    };

    /// Result of a decoding
    class Result
    {
    public:
        Status status = Status::Error;
        QByteArray data;            ///< The decoded data (Decoded), the data before the stopping filter (Encoded)
        QByteArray filterName;      ///< The filter, which stopped or failed (Encoded, Error)
        QString errorMessage;       ///< Translated message (Error, LimitExceeded)
    };

    /// Decodes the stream with the limit of its decoded size
    /// \param stream The stream (nullptr = Error)
    /// \param objectFetcher Function which dereferences objects (the parameters of the filters)
    /// \param securityHandler Security handler for the Crypt filters (may be null)
    /// \param maximumDecodedSize Most bytes of the output of any filter (at least 1)
    /// \param control Cancellation (optional)
    static Result decode(const PDFStream* stream, const PDFObjectFetcher& objectFetcher, const PDFSecurityHandler* securityHandler, qint64 maximumDecodedSize, const PDFOperationControl* control);

    /// Decodes only the beginning of the stream (the header of an object stream, RS7CORE of step 7 of phase 2):
    /// the filters of the stream decode their data in their order (ISO 32000-2, 7.4.1), so the last filter
    /// stops, when its output has \p prefixSize bytes - the rest is never decoded. The filters before it, a
    /// last filter with a predictor (its rows) and the filters, whose output is bounded by their input
    /// (ASCIIHex, Crypt), decode their whole input with the limit \p maximumDecodedSize. The result is Decoded
    /// with the first min(\p prefixSize, decoded size) bytes of the decoded data; the other statuses as of
    /// decode (a prefix, which is not below \p maximumDecodedSize, is the decoding of decode cut to it).
    /// \param stream The stream (nullptr = Error)
    /// \param objectFetcher Function which dereferences objects (the parameters of the filters)
    /// \param securityHandler Security handler for the Crypt filters (may be null)
    /// \param prefixSize Bytes of the beginning of the decoded data (at least 0)
    /// \param maximumDecodedSize Most bytes of the output of any filter (at least 1)
    /// \param control Cancellation (optional)
    static Result decodePrefix(const PDFStream* stream, const PDFObjectFetcher& objectFetcher, const PDFSecurityHandler* securityHandler, qint64 prefixSize,
                               qint64 maximumDecodedSize, const PDFOperationControl* control);

private:
    /// decode (\p prefixSize < 0) and decodePrefix
    static Result decodeImpl(const PDFStream* stream, const PDFObjectFetcher& objectFetcher, const PDFSecurityHandler* securityHandler, qint64 maximumDecodedSize,
                             qint64 prefixSize, const PDFOperationControl* control);
};

class PDFStreamPredictor
{
public:
    /// Create predictor from stream parameters. If error occurs, exception is thrown.
    /// \param objectFetcher Function which retrieves objects (for example, reads objects from reference)
    /// \param parameters Parameters of the predictor (must be an dictionary)
    static PDFStreamPredictor createPredictor(const PDFObjectFetcher& objectFetcher, const PDFObject& parameters);

    /// Applies the predictor to the data.
    /// \param data Data to be decoded using predictor
    QByteArray apply(const QByteArray& data) const;

private:

    enum Predictor
    {
        NoPredictor = 1,
        TIFF = 2,
        PNG_None = 10,      ///< No prediction
        PNG_Sub = 11,       ///< Prediction based on previous byte
        PNG_Up  = 12,       ///< Prediction based on byte above
        PNG_Average = 13,   ///< Prediction based on average of previous nad current byte
        PNG_Paeth = 14,     ///< Nonlinear function
    };

    inline explicit PDFStreamPredictor() = default;

    inline explicit PDFStreamPredictor(Predictor predictor, int components, int bitsPerComponent, int columns) :
        m_predictor(predictor),
        m_components(components),
        m_bitsPerComponent(bitsPerComponent),
        m_columns(columns),
        m_stride(0)
    {
        m_stride = (m_columns * m_components * m_bitsPerComponent + 7) / 8;
    }

    /// Applies PNG predictor
    QByteArray applyPNGPredictor(const QByteArray& data) const;

    /// Applies TIFF predictor
    QByteArray applyTIFFPredictor(const QByteArray& data) const;

    Predictor m_predictor = NoPredictor;
    int m_components = 0;
    int m_bitsPerComponent = 0;
    int m_columns = 0;
    int m_stride = 0;
};

class PDF4QTLIBCORESHARED_EXPORT PDFStreamFilter
{
public:
    explicit PDFStreamFilter() = default;
    virtual ~PDFStreamFilter() = default;

    /// Apply with object fetcher
    /// \param data Stream data to be decoded
    /// \param objectFetcher Function which retrieves objects (for example, reads objects from reference)
    /// \param parameters Stream parameters
    virtual QByteArray apply(const QByteArray& data, const PDFObjectFetcher& objectFetcher, const PDFObject& parameters, const PDFSecurityHandler* securityHandler) const = 0;

    /// Apply without object fetcher - it assumes no references exists in the streams dictionary
    /// \param data Stream data to be decoded
    /// \param parameters Stream parameters
    inline QByteArray apply(const QByteArray& data, const PDFObject& parameters, const PDFSecurityHandler* securityHandler) const
    {
        return apply(data, [](const PDFObject& object) -> const PDFObject& { return object; }, parameters, securityHandler);
    }

    /// Tries to find stream data length. Stream will start at given \p offset in \p data.
    /// If stream length cannot be determined, then -1 is returned.
    /// \param data Buffer data
    /// \param offset Offset to buffer, at which stream data starts
    virtual PDFInteger getStreamDataLength(const QByteArray& data, PDFInteger offset) const;
};

class PDF4QTLIBCORESHARED_EXPORT PDFAsciiHexDecodeFilter : public PDFStreamFilter
{
public:
    explicit PDFAsciiHexDecodeFilter() = default;
    virtual ~PDFAsciiHexDecodeFilter() override = default;

    virtual QByteArray apply(const QByteArray& data,
                             const PDFObjectFetcher& objectFetcher,
                             const PDFObject& parameters,
                             const PDFSecurityHandler* securityHandler) const override;
};

class PDF4QTLIBCORESHARED_EXPORT PDFAscii85DecodeFilter : public PDFStreamFilter
{
public:
    explicit PDFAscii85DecodeFilter() = default;
    virtual ~PDFAscii85DecodeFilter() override = default;

    virtual QByteArray apply(const QByteArray& data,
                             const PDFObjectFetcher& objectFetcher,
                             const PDFObject& parameters,
                             const PDFSecurityHandler* securityHandler) const override;
};

class PDF4QTLIBCORESHARED_EXPORT PDFLzwDecodeFilter : public PDFStreamFilter
{
public:
    explicit PDFLzwDecodeFilter() = default;
    virtual ~PDFLzwDecodeFilter() override = default;

    virtual QByteArray apply(const QByteArray& data,
                             const PDFObjectFetcher& objectFetcher,
                             const PDFObject& parameters,
                             const PDFSecurityHandler* securityHandler) const override;
};

class PDF4QTLIBCORESHARED_EXPORT PDFFlateDecodeFilter : public PDFStreamFilter
{
public:
    explicit PDFFlateDecodeFilter() = default;
    virtual ~PDFFlateDecodeFilter() override = default;

    virtual QByteArray apply(const QByteArray& data,
                             const PDFObjectFetcher& objectFetcher,
                             const PDFObject& parameters,
                             const PDFSecurityHandler* securityHandler) const override;

    virtual PDFInteger getStreamDataLength(const QByteArray& data, PDFInteger offset) const override;

    /// Recompresses data. So, first, data are decompressed, and then
    /// recompressed again with maximal compress ratio possible.
    /// \param data Uncompressed data to be compressed
    static QByteArray compress(const QByteArray& decompressedData);

    /// Compress data with maximal compress ratio possible.
    /// \param data Compressed data to be recompressed
    static QByteArray recompress(const QByteArray& data);

private:
    static QByteArray uncompress(const QByteArray& data);
};

class PDF4QTLIBCORESHARED_EXPORT PDFRunLengthDecodeFilter : public PDFStreamFilter
{
public:
    explicit PDFRunLengthDecodeFilter() = default;
    virtual ~PDFRunLengthDecodeFilter() override = default;

    virtual QByteArray apply(const QByteArray& data,
                             const PDFObjectFetcher& objectFetcher,
                             const PDFObject& parameters,
                             const PDFSecurityHandler* securityHandler) const override;
};

class PDF4QTLIBCORESHARED_EXPORT PDFCryptFilter : public PDFStreamFilter
{
public:
    explicit PDFCryptFilter() = default;
    virtual ~PDFCryptFilter() override = default;

    virtual QByteArray apply(const QByteArray& data,
                             const PDFObjectFetcher& objectFetcher,
                             const PDFObject& parameters,
                             const PDFSecurityHandler* securityHandler) const override;
};

}   // namespace pdf

#endif // PDFSTREAMFILTERS_H
