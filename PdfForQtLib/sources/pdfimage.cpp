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

#include <openjpeg.h>
#include <jpeglib.h>

namespace pdf
{

struct PDFJPEG2000ImageData
{
    const QByteArray* byteArray = nullptr;
    OPJ_SIZE_T position = 0;

    static OPJ_SIZE_T read(void* p_buffer, OPJ_SIZE_T p_nb_bytes, void* p_user_data);
    static OPJ_BOOL seek(OPJ_OFF_T p_nb_bytes, void* p_user_data);
    static OPJ_OFF_T skip(OPJ_OFF_T p_nb_bytes, void* p_user_data);
};

struct PDFJPEGDCTSource
{
    jpeg_source_mgr sourceManager;
    const QByteArray* buffer = nullptr;
};

PDFImage PDFImage::createImage(const PDFDocument* document, const PDFStream* stream, PDFColorSpacePointer colorSpace)
{
    PDFImage image;
    image.m_colorSpace = colorSpace;

    // TODO: Implement ImageMask
    // TODO: Implement Mask
    // TODO: Implement Decode
    // TODO: Implement SMask
    // TODO: Implement SMaskInData

    const PDFDictionary* dictionary = stream->getDictionary();
    QByteArray content = document->getDecodedStream(stream);
    PDFDocumentDataLoaderDecorator loader(document);

    if (content.isEmpty())
    {
        throw PDFParserException(PDFTranslationContext::tr("Image has not data."));
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
        // TODO: Check, if mutex is needed
        // Used library is not thread safe. We must use a mutex!
        static QMutex mutex;
        QMutexLocker lock(&mutex);

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
            image.m_imageData = PDFImageData(components, bitsPerComponent, width, height, rowStride, qMove(buffer));
        }

        jpeg_destroy_decompress(&codec);
    }
    else if (imageFilterName == "JPXDecode")
    {
        PDFJPEG2000ImageData imageData;
        imageData.byteArray = &content;
        imageData.position = 0;

        opj_stream_t* stream = opj_stream_default_create(OPJ_TRUE);
        opj_stream_set_user_data(stream, &imageData, nullptr);
        opj_stream_set_user_data_length(stream, sizeof(PDFJPEG2000ImageData));
        opj_stream_set_read_function(stream, &PDFJPEG2000ImageData::read);
        opj_stream_set_seek_function(stream, &PDFJPEG2000ImageData::seek);
        opj_stream_set_skip_function(stream, &PDFJPEG2000ImageData::skip);

        opj_dparameters_t decompressParameters;
        opj_set_default_decoder_parameters(&decompressParameters);

        CODEC_FORMAT formats[] = { OPJ_CODEC_J2K, OPJ_CODEC_JPT, OPJ_CODEC_JP2, OPJ_CODEC_JPP, OPJ_CODEC_JPX };
        for (CODEC_FORMAT format : formats)
        {
            opj_codec_t* codec = opj_create_decompress(format);

            if (!codec)
            {
                // Codec is not present
                continue;
            }

            // Setup the decoder
            opj_setup_decoder(codec, &decompressParameters);

            // Try to read the header
            opj_image_t* image = nullptr;
            if (opj_read_header(stream, codec, &image))
            {
                if (opj_set_decode_area(codec, image, decompressParameters.DA_x0, decompressParameters.DA_y0, decompressParameters.DA_x1, decompressParameters.DA_y1))
                {
                    if (opj_decode(codec, stream, image))
                    {
                        if (opj_end_decompress(codec, stream))
                        {

                        }
                    }
                }
            }

            opj_destroy_codec(codec);
        }

        opj_stream_destroy(stream);
        stream = nullptr;
    }

    return image;
}

QImage PDFImage::getImage() const
{
    if (m_colorSpace)
    {
        return m_colorSpace->getImage(m_imageData);
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
