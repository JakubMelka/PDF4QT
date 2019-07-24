//    Copyright (C) 2019 Jakub Melka
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

#include "pdfimage.h"
#include "pdfdocument.h"
#include "pdfconstants.h"
#include "pdfexception.h"
#include "pdfutils.h"

#include <openjpeg.h>
#include <jpeglib.h>

namespace pdf
{

struct PDFJPEG2000ImageData
{
    const QByteArray* byteArray = nullptr;
    OPJ_SIZE_T position = 0;
    std::vector<PDFRenderError> errors;

    static OPJ_SIZE_T read(void* p_buffer, OPJ_SIZE_T p_nb_bytes, void* p_user_data);
    static OPJ_BOOL seek(OPJ_OFF_T p_nb_bytes, void* p_user_data);
    static OPJ_OFF_T skip(OPJ_OFF_T p_nb_bytes, void* p_user_data);
};

struct PDFJPEGDCTSource
{
    jpeg_source_mgr sourceManager;
    const QByteArray* buffer = nullptr;
};

PDFImage PDFImage::createImage(const PDFDocument* document, const PDFStream* stream, PDFColorSpacePointer colorSpace, PDFRenderErrorReporter* errorReporter)
{
    PDFImage image;
    image.m_colorSpace = colorSpace;

    const PDFDictionary* dictionary = stream->getDictionary();
    QByteArray content = document->getDecodedStream(stream);
    PDFDocumentDataLoaderDecorator loader(document);

    if (content.isEmpty())
    {
        throw PDFParserException(PDFTranslationContext::tr("Image has not data."));
    }

    // TODO: Implement SMask
    // TODO: Implement SMaskInData

    for (const char* notImplementedKey : { "SMask", "SMaskInData" })
    {
        if (dictionary->hasKey(notImplementedKey))
        {
            throw PDFRendererException(RenderErrorType::NotImplemented, PDFTranslationContext::tr("Not implemented image property '%2'.").arg(QString::fromLatin1(notImplementedKey)));
        }
    }

    PDFImageData::MaskingType maskingType = PDFImageData::MaskingType::None;
    std::vector<PDFInteger> mask;
    std::vector<PDFReal> decode = loader.readNumberArrayFromDictionary(dictionary, "Decode");
    bool imageMask = loader.readBooleanFromDictionary(dictionary, "ImageMask", false);

    // Fill Mask
    if (dictionary->hasKey("Mask"))
    {
        const PDFObject& object = document->getObject(dictionary->get("Mask"));
        if (object.isArray())
        {
            maskingType = PDFImageData::MaskingType::ColorKeyMasking;
            mask = loader.readIntegerArray(object);
        }
        else if (object.isStream())
        {
            // TODO: Implement Mask Image
            maskingType = PDFImageData::MaskingType::Image;
            throw PDFRendererException(RenderErrorType::NotImplemented, PDFTranslationContext::tr("Mask image is not implemented."));
        }
    }

    if (imageMask)
    {
        maskingType = PDFImageData::MaskingType::ImageMask;
    }

    // Retrieve filters
    PDFObject filters;
    if (dictionary->hasKey(PDF_STREAM_DICT_FILTER))
    {
        filters = document->getObject(dictionary->get(PDF_STREAM_DICT_FILTER));
    }
    else if (dictionary->hasKey(PDF_STREAM_DICT_FILE_FILTER))
    {
        filters = document->getObject(dictionary->get(PDF_STREAM_DICT_FILE_FILTER));
    }

    // Retrieve filter parameters
    PDFObject filterParameters;
    if (dictionary->hasKey(PDF_STREAM_DICT_DECODE_PARMS))
    {
        filterParameters = document->getObject(dictionary->get(PDF_STREAM_DICT_DECODE_PARMS));
    }
    else if (dictionary->hasKey(PDF_STREAM_DICT_FDECODE_PARMS))
    {
        filterParameters = document->getObject(dictionary->get(PDF_STREAM_DICT_FDECODE_PARMS));
    }

    QByteArray imageFilterName;
    if (filters.isName())
    {
        imageFilterName = filters.getString();
    }
    else if (filters.isArray())
    {
        const PDFArray* filterArray = filters.getArray();
        const size_t filterCount = filterArray->getCount();

        if (filterCount)
        {
            const PDFObject& object = document->getObject(filterArray->getItem(filterCount - 1));
            if (object.isName())
            {
                imageFilterName = object.getString();
            }
        }
    }

    if (imageFilterName == "DCTDecode" || imageFilterName == "DCT")
    {
        int colorTransform = loader.readIntegerFromDictionary(dictionary, "ColorTransform", -1);

        jpeg_decompress_struct codec;
        jpeg_error_mgr errorManager;
        std::memset(&codec, 0, sizeof(jpeg_decompress_struct));
        std::memset(&errorManager, 0, sizeof(errorManager));

        PDFJPEGDCTSource source;
        source.buffer = &content;
        std::memset(&source.sourceManager, 0, sizeof(jpeg_source_mgr));

        auto errorMethod = [](j_common_ptr ptr)
        {
            char buffer[JMSG_LENGTH_MAX] = { };
            (ptr->err->format_message)(ptr, buffer);

            jpeg_destroy(ptr);
            throw PDFParserException(PDFTranslationContext::tr("Error reading JPEG (DCT) image: %1.").arg(QString::fromLatin1(buffer)));
        };

        auto fillInputBufferMethod = [](j_decompress_ptr decompress) -> boolean
        {
            PDFJPEGDCTSource* source = reinterpret_cast<PDFJPEGDCTSource*>(decompress->src);

            if (!source->sourceManager.next_input_byte)
            {
                const QByteArray* buffer = source->buffer;
                source->sourceManager.next_input_byte = reinterpret_cast<const JOCTET*>(buffer->constData());
                source->sourceManager.bytes_in_buffer = buffer->size();
                return TRUE;
            }

            return FALSE;
        };

        auto skipInputDataMethod = [](j_decompress_ptr decompress, long num_bytes)
        {
            PDFJPEGDCTSource* source = reinterpret_cast<PDFJPEGDCTSource*>(decompress->src);

            const size_t skippedBytes = qMin(source->sourceManager.bytes_in_buffer, static_cast<size_t>(num_bytes));
            source->sourceManager.next_input_byte += skippedBytes;
            source->sourceManager.bytes_in_buffer -= skippedBytes;
        };

        source.sourceManager.bytes_in_buffer = 0;
        source.sourceManager.next_input_byte = nullptr;
        source.sourceManager.init_source = [](j_decompress_ptr) { };
        source.sourceManager.fill_input_buffer = fillInputBufferMethod;
        source.sourceManager.skip_input_data = skipInputDataMethod;
        source.sourceManager.resync_to_restart = jpeg_resync_to_restart;
        source.sourceManager.term_source = [](j_decompress_ptr) { };

        jpeg_std_error(&errorManager);
        errorManager.error_exit = errorMethod;
        codec.err = &errorManager;

        jpeg_create_decompress(&codec);
        codec.src = reinterpret_cast<jpeg_source_mgr*>(&source);

        if (jpeg_read_header(&codec, TRUE) == JPEG_HEADER_OK)
        {
            // Determine color transform
            if (colorTransform == -1 && codec.saw_Adobe_marker)
            {
                colorTransform = codec.Adobe_transform;
            }

            // Set the input transform
            if (colorTransform > -1)
            {
                switch (codec.num_components)
                {
                    case 3:
                    {
                        codec.jpeg_color_space = colorTransform ? JCS_YCbCr : JCS_RGB;
                        break;
                    }

                    case 4:
                    {
                        codec.jpeg_color_space = colorTransform ? JCS_YCCK : JCS_CMYK;
                        break;
                    }

                    default:
                        break;
                }
            }

            jpeg_start_decompress(&codec);

            const JDIMENSION rowStride = codec.output_width * codec.output_components;
            JSAMPARRAY samples = codec.mem->alloc_sarray(reinterpret_cast<j_common_ptr>(&codec), JPOOL_IMAGE, rowStride, 1);
            JDIMENSION scanLineCount = codec.output_height;

            const unsigned int width = codec.output_width;
            const unsigned int height = codec.output_height;
            const unsigned int components = codec.output_components;
            const unsigned int bitsPerComponent =  8;
            QByteArray buffer(rowStride * height, 0);
            JSAMPROW rowData = reinterpret_cast<JSAMPROW>(buffer.data());

            while (scanLineCount)
            {
                JDIMENSION readCount = jpeg_read_scanlines(&codec, samples, 1);
                std::memcpy(rowData, samples[0], rowStride);
                scanLineCount -= readCount;
                rowData += rowStride;
            }

            jpeg_finish_decompress(&codec);
            image.m_imageData = PDFImageData(components, bitsPerComponent, width, height, rowStride, maskingType, qMove(buffer), qMove(mask), qMove(decode));
        }

        jpeg_destroy_decompress(&codec);
    }
    else if (imageFilterName == "JPXDecode")
    {
        PDFJPEG2000ImageData imageData;
        imageData.byteArray = &content;
        imageData.position = 0;

        auto warningCallback = [](const char* message, void* userData)
        {
            PDFJPEG2000ImageData* data = reinterpret_cast<PDFJPEG2000ImageData*>(userData);
            data->errors.push_back(PDFRenderError(RenderErrorType::Warning, PDFTranslationContext::tr("JPEG 2000 Warning: %1").arg(QString::fromLatin1(message))));
        };

        auto errorCallback = [](const char* message, void* userData)
        {
            PDFJPEG2000ImageData* data = reinterpret_cast<PDFJPEG2000ImageData*>(userData);
            data->errors.push_back(PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("JPEG 2000 Error: %1").arg(QString::fromLatin1(message))));
        };

        opj_dparameters_t decompressParameters;
        opj_set_default_decoder_parameters(&decompressParameters);

        const bool isIndexed = dynamic_cast<const PDFIndexedColorSpace*>(image.m_colorSpace.data());
        if (isIndexed)
        {
            // What is this flag for? When we have indexed color space, we do not want to resolve index to color
            // using the color map in the image. Instead of that, we just get indices and resolve them using
            // our color space.
            decompressParameters.flags |= OPJ_DPARAMETERS_IGNORE_PCLR_CMAP_CDEF_FLAG;
        }

        constexpr CODEC_FORMAT formats[] = { OPJ_CODEC_J2K, OPJ_CODEC_JP2, OPJ_CODEC_JPT, OPJ_CODEC_JPP, OPJ_CODEC_JPX };
        for (CODEC_FORMAT format : formats)
        {
            opj_codec_t* codec = opj_create_decompress(format);

            if (!codec)
            {
                // Codec is not present
                continue;
            }

            opj_set_warning_handler(codec, warningCallback, &imageData);
            opj_set_error_handler(codec, errorCallback, &imageData);

            opj_stream_t* stream = opj_stream_create(content.size(), OPJ_TRUE);
            opj_stream_set_user_data(stream, &imageData, nullptr);
            opj_stream_set_user_data_length(stream, content.size());
            opj_stream_set_read_function(stream, &PDFJPEG2000ImageData::read);
            opj_stream_set_seek_function(stream, &PDFJPEG2000ImageData::seek);
            opj_stream_set_skip_function(stream, &PDFJPEG2000ImageData::skip);

            // Reset the stream position, clear the data
            imageData.position = 0;
            imageData.errors.clear();

            opj_image_t* jpegImage = nullptr;

            // Setup the decoder
            if (opj_setup_decoder(codec, &decompressParameters))
            {
                // Try to read the header

                if (opj_read_header(stream, codec, &jpegImage))
                {
                    if (opj_set_decode_area(codec, jpegImage, decompressParameters.DA_x0, decompressParameters.DA_y0, decompressParameters.DA_x1, decompressParameters.DA_y1))
                    {
                        if (opj_decode(codec, stream, jpegImage))
                        {
                            if (opj_end_decompress(codec, stream))
                            {

                            }
                        }
                    }
                }
            }

            opj_stream_destroy(stream);
            opj_destroy_codec(codec);

            stream = nullptr;
            codec = nullptr;

            // If we have a valid image, then adjust it
            if (jpegImage)
            {
                // First we must check, if all components are valid (i.e has same width/height/precision)

                bool valid = true;
                const OPJ_UINT32 componentCount = jpegImage->numcomps;
                for (OPJ_UINT32 i = 1; i < componentCount; ++i)
                {
                    if (jpegImage->comps[0].w != jpegImage->comps[i].w ||
                        jpegImage->comps[0].h != jpegImage->comps[i].h ||
                        jpegImage->comps[0].prec != jpegImage->comps[i].prec ||
                        jpegImage->comps[0].sgnd != jpegImage->comps[i].sgnd)
                    {
                        valid = false;
                        break;
                    }
                }

                // TODO: Include alpha channel functionality - mask in image

                if (valid)
                {
                    const OPJ_UINT32 w = jpegImage->comps[0].w;
                    const OPJ_UINT32 h = jpegImage->comps[0].h;
                    const OPJ_UINT32 prec = jpegImage->comps[0].prec;
                    const OPJ_UINT32 sgnd = jpegImage->comps[0].sgnd;

                    int signumCorrection = (sgnd) ? (1 << (prec - 1)) : 0;
                    int shiftLeft = (jpegImage->comps[0].prec < 8) ? 8 - jpegImage->comps[0].prec : 0;
                    int shiftRight = (jpegImage->comps[0].prec > 8) ? jpegImage->comps[0].prec - 8 : 0;

                    auto transformValue = [signumCorrection, isIndexed, shiftLeft, shiftRight](int value) -> unsigned char
                    {
                        value += signumCorrection;

                        if (!isIndexed)
                        {
                            // Indexed color space should have at most 255 indices, do not modify indices in this case

                            if (shiftLeft > 0)
                            {
                                value = value << shiftLeft;
                            }
                            else if (shiftRight > 0)
                            {
                                // We clamp value to the lower part (so, we use similar algorithm as in 'floor' function).
                                //
                                value = value >> shiftRight;
                            }
                        }

                        value = qBound(0, value, 255);
                        return static_cast<unsigned char>(value);
                    };

                    // Variables for image data. We convert all components to the 8-bit format
                    unsigned int components = jpegImage->numcomps;
                    unsigned int bitsPerComponent = 8;
                    unsigned int width = w;
                    unsigned int height = h;
                    unsigned int stride = w * components;

                    QByteArray imageDataBuffer(components * width * height, 0);
                    for (unsigned int row = 0; row < h; ++row)
                    {
                        for (unsigned int col = 0; col < w; ++col)
                        {
                            for (unsigned int componentIndex = 0; componentIndex < components; ++ componentIndex)
                            {
                                int index = stride * row + col * components + componentIndex;
                                Q_ASSERT(index < imageDataBuffer.size());

                                imageDataBuffer[index] = transformValue(jpegImage->comps[componentIndex].data[w * row + col]);
                            }
                        }
                    }

                    image.m_imageData = PDFImageData(components, bitsPerComponent, width, height, stride, maskingType, qMove(imageDataBuffer), qMove(mask), qMove(decode));
                    valid = image.m_imageData.isValid();
                }
                else
                {
                    // Easiest way is to just add errors to the error list
                    imageData.errors.push_back(PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Incompatible color components for JPEG 2000 image.")));
                }

                opj_image_destroy(jpegImage);

                if (valid)
                {
                    // Image was successfully decoded
                    break;
                }
            }
        }

        // Report errors, if we have any
        if (!imageData.errors.empty())
        {
            for (const PDFRenderError& error : imageData.errors)
            {
                QString message = error.message.simplified().trimmed();
                if (error.type == RenderErrorType::Error)
                {
                    throw PDFRendererException(error.type, message);
                }
                else
                {
                    errorReporter->reportRenderError(error.type, message);
                }
            }
        }
    }
    else if (imageFilterName == "CCITTFaxDecode")
    {
        throw PDFRendererException(RenderErrorType::NotImplemented, PDFTranslationContext::tr("Not implemented image filter 'CCITFaxDecode'."));
    }
    else if (imageFilterName == "JBIG2Decode")
    {
        throw PDFRendererException(RenderErrorType::NotImplemented, PDFTranslationContext::tr("Not implemented image filter 'JBIG2Decode'."));
    }
    else if (colorSpace)
    {
        // We treat data as binary maybe compressed stream (for example by Flate/LZW method), but data can also be not compressed.
        const unsigned int components = static_cast<unsigned int>(colorSpace->getColorComponentCount());
        const unsigned int bitsPerComponent = static_cast<unsigned int>(loader.readIntegerFromDictionary(dictionary, "BitsPerComponent", 8));
        const unsigned int width = static_cast<unsigned int>(loader.readIntegerFromDictionary(dictionary, "Width", 0));
        const unsigned int height = static_cast<unsigned int>(loader.readIntegerFromDictionary(dictionary, "Height", 0));

        if (bitsPerComponent < 1 || bitsPerComponent > 32)
        {
            throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid number of bits per component (%1).").arg(bitsPerComponent));
        }

        if (width == 0 || height == 0)
        {
            throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid size of image (%1x%2)").arg(width).arg(height));
        }

        // Calculate stride
        const unsigned int stride = (components * bitsPerComponent * width + 7) / 8;

        QByteArray imageDataBuffer = document->getDecodedStream(stream);
        image.m_imageData = PDFImageData(components, bitsPerComponent, width, height, stride, maskingType, qMove(imageDataBuffer), qMove(mask), qMove(decode));
    }
    else if (imageMask)
    {
        // We intentionally have 8 bits in the following code, because if ImageMask is set to true, then "BitsPerComponent"
        // should have always value of 1.
        const unsigned int bitsPerComponent = static_cast<unsigned int>(loader.readIntegerFromDictionary(dictionary, "BitsPerComponent", 8));

        if (bitsPerComponent != 1)
        {
            throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid number bits of image mask (should be 1 bit instead of %1 bits).").arg(bitsPerComponent));
        }

        const unsigned int width = static_cast<unsigned int>(loader.readIntegerFromDictionary(dictionary, "Width", 0));
        const unsigned int height = static_cast<unsigned int>(loader.readIntegerFromDictionary(dictionary, "Height", 0));

        if (width == 0 || height == 0)
        {
            throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid size of image (%1x%2)").arg(width).arg(height));
        }

        // Calculate stride
        const unsigned int stride = (width + 7) / 8;

        QByteArray imageDataBuffer = document->getDecodedStream(stream);
        image.m_imageData = PDFImageData(1, bitsPerComponent, width, height, stride, maskingType, qMove(imageDataBuffer), qMove(mask), qMove(decode));
    }

    return image;
}

QImage PDFImage::getImage() const
{
    const bool isImageMask = m_imageData.getMaskingType() == PDFImageData::MaskingType::ImageMask;
    if (m_colorSpace && !isImageMask)
    {
        return m_colorSpace->getImage(m_imageData);
    }
    else if (isImageMask)
    {
        if (m_imageData.getBitsPerComponent() != 1)
        {
            throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid number bits of image mask (should be 1 bit instead of %1 bits).").arg(m_imageData.getBitsPerComponent()));
        }

        if (m_imageData.getWidth() == 0 || m_imageData.getHeight() == 0)
        {
            throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid size of image (%1x%2)").arg(m_imageData.getWidth()).arg(m_imageData.getHeight()));
        }

        QImage image(m_imageData.getWidth(), m_imageData.getHeight(), QImage::Format_Alpha8);

        const bool flip01 = !m_imageData.getDecode().empty() && qFuzzyCompare(m_imageData.getDecode().front(), 1.0);
        QDataStream stream(const_cast<QByteArray*>(&m_imageData.getData()), QIODevice::ReadOnly);
        PDFBitReader reader(&stream, m_imageData.getBitsPerComponent());

        for (unsigned int i = 0, rowCount = m_imageData.getHeight(); i < rowCount; ++i)
        {
            reader.seek(i * m_imageData.getStride());
            unsigned char* outputLine = image.scanLine(i);

            for (unsigned int j = 0; j < m_imageData.getWidth(); ++j)
            {
                const bool transparent = flip01 != static_cast<bool>(reader.read());
                *outputLine++ = transparent ? 0x00 : 0xFF;
            }
        }

        return image;
    }

    return QImage();
}

OPJ_SIZE_T PDFJPEG2000ImageData::read(void* p_buffer, OPJ_SIZE_T p_nb_bytes, void* p_user_data)
{
    PDFJPEG2000ImageData* data = reinterpret_cast<PDFJPEG2000ImageData*>(p_user_data);

    // Remaining length
    OPJ_OFF_T length = static_cast<OPJ_OFF_T>(data->byteArray->size()) - data->position;

    if (length < 0)
    {
        length = 0;
    }

    if (length > static_cast<OPJ_OFF_T>(p_nb_bytes))
    {
        length = static_cast<OPJ_OFF_T>(p_nb_bytes);
    }

    if (length > 0)
    {
        std::memcpy(p_buffer, data->byteArray->constData() + data->position, length);
        data->position += length;
    }

    if (length == 0)
    {
        return (OPJ_SIZE_T) - 1;
    }

    return length;
}

OPJ_BOOL PDFJPEG2000ImageData::seek(OPJ_OFF_T p_nb_bytes, void* p_user_data)
{
    PDFJPEG2000ImageData* data = reinterpret_cast<PDFJPEG2000ImageData*>(p_user_data);

    if (p_nb_bytes >= data->byteArray->size())
    {
        return OPJ_FALSE;
    }

    data->position = p_nb_bytes;
    return OPJ_TRUE;
}

OPJ_OFF_T PDFJPEG2000ImageData::skip(OPJ_OFF_T p_nb_bytes, void* p_user_data)
{
    PDFJPEG2000ImageData* data = reinterpret_cast<PDFJPEG2000ImageData*>(p_user_data);

    // Remaining length
    OPJ_OFF_T length = static_cast<OPJ_OFF_T>(data->byteArray->size()) - data->position;

    if (length < 0)
    {
        length = 0;
    }

    if (length > static_cast<OPJ_OFF_T>(p_nb_bytes))
    {
        length = static_cast<OPJ_OFF_T>(p_nb_bytes);
    }

    data->position += length;
    return length;
}

}   // namespace pdf
