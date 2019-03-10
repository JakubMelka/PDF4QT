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

#include "pdffunction.h"
#include "pdfflatarray.h"
#include "pdfparser.h"
#include "pdfdocument.h"

namespace pdf
{

PDFFunction::PDFFunction(uint32_t m, uint32_t n, std::vector<PDFReal>&& domain, std::vector<PDFReal>&& range) :
    m_m(m),
    m_n(n),
    m_domain(std::move(domain)),
    m_range(std::move(range))
{

}

PDFFunctionPtr PDFFunction::createFunction(const PDFDocument* document, const PDFObject& object)
{
    PDFParsingContext context(nullptr);
    return createFunctionImpl(document, object, &context);
}

PDFFunctionPtr PDFFunction::createFunctionImpl(const PDFDocument* document, const PDFObject& object, PDFParsingContext* context)
{
    PDFParsingContext::PDFParsingContextObjectGuard guard(context, &object);

    const PDFDictionary* dictionary = nullptr;
    QByteArray streamData;

    const PDFObject& dereferencedObject = document->getObject(object);
    if (dereferencedObject.isName() && dereferencedObject.getString() == "Identity")
    {
        return std::make_shared<PDFIdentityFunction>();
    }
    else if (dereferencedObject.isDictionary())
    {
        dictionary = dereferencedObject.getDictionary();
    }
    else if (dereferencedObject.isStream())
    {
        const PDFStream* stream = dereferencedObject.getStream();
        dictionary = stream->getDictionary();
        streamData = document->getDecodedStream(stream);
    }

    if (!dictionary)
    {
        throw PDFParserException(PDFParsingContext::tr("Function dictionary expected."));
    }

    PDFDocumentDataLoaderDecorator loader(document);
    const PDFInteger functionType = loader.readIntegerFromDictionary(dictionary, "FunctionType", -1);
    std::vector<PDFReal> domain = loader.readNumberArrayFromDictionary(dictionary, "Domain");
    std::vector<PDFReal> range = loader.readNumberArrayFromDictionary(dictionary, "Range");

    // Domain is required for all function
    if (domain.empty())
    {
        throw PDFParserException(PDFParsingContext::tr("Fuction has invalid domain."));
    }

    if ((functionType == 0 || functionType == 4) && range.empty())
    {
        throw PDFParserException(PDFParsingContext::tr("Fuction has invalid range."));
    }

    switch (functionType)
    {
        case 0:
        {
            // Sampled function
            std::vector<PDFInteger> size = loader.readIntegerArrayFromDictionary(dictionary, "Size");
            const size_t bitsPerSample = loader.readIntegerFromDictionary(dictionary, "BitsPerSample", 0);
            std::vector<PDFReal> encode = loader.readNumberArrayFromDictionary(dictionary, "Encode");
            std::vector<PDFReal> decode = loader.readNumberArrayFromDictionary(dictionary, "Decode");

            if (size.empty() || !std::all_of(size.cbegin(), size.cend(), [](PDFInteger size) { return size >= 1; }))
            {
                throw PDFParserException(PDFParsingContext::tr("Sampled function has invalid sample size."));
            }

            if (bitsPerSample < 1 || bitsPerSample > 32)
            {
                throw PDFParserException(PDFParsingContext::tr("Sampled function has invalid count of bits per sample."));
            }

            if (encode.empty())
            {
                // Construct default array according to the PDF 1.7 specification
                encode.resize(2 * size.size(), 0);
                for (size_t i = 0, count = size.size(); i < count; ++i)
                {
                    encode[2 * i + 1] = size[i] - 1;
                }
            }

            if (decode.empty())
            {
                // Default decode array is same as range, see PDF 1.7 specification
                decode = range;
            }

            const size_t m = size.size();
            const size_t n = range.size() / 2;

            if (n == 0)
            {
                throw PDFParserException(PDFParsingContext::tr("Sampled function hasn't any output."));
            }

            if (domain.size() != encode.size())
            {
                throw PDFParserException(PDFParsingContext::tr("Sampled function has invalid encode array."));
            }

            if (range.size() != decode.size())
            {
                throw PDFParserException(PDFParsingContext::tr("Sampled function has invalid decode array."));
            }

            const uint64_t sampleMaxValueInteger = (static_cast<uint64_t>(1) << static_cast<uint64_t>(bitsPerSample)) - 1;
            const PDFReal sampleMaxValue = sampleMaxValueInteger;

            // Load samples - first see, how much of them will be needed.
            const PDFInteger sampleCount = std::accumulate(size.cbegin(), size.cend(), 1, [](PDFInteger a, PDFInteger b) { return a * b; } ) * n;
            std::vector<PDFReal> samples;
            samples.resize(sampleCount, 0.0);

            // We must use 64 bit, because we can have 32 bit values
            uint64_t buffer = 0;
            uint64_t bitsWritten = 0;
            uint64_t bitMask = sampleMaxValueInteger;

            QDataStream reader(&streamData, QIODevice::ReadOnly);
            for (PDFReal& sample : samples)
            {
                while (bitsWritten < bitsPerSample)
                {
                    if (!reader.atEnd())
                    {
                        uint8_t currentByte = 0;
                        reader >> currentByte;
                        buffer = (buffer << 8) | currentByte;
                        bitsWritten += 8;
                    }
                    else
                    {
                        throw PDFParserException(PDFParsingContext::tr("Not enough samples for sampled function."));
                    }
                }

                // Now we have enough bits to read the data
                uint64_t sampleUint = (buffer >> (bitsWritten - bitsPerSample)) & bitMask;
                bitsWritten -= bitsPerSample;
                sample = sampleUint;
            }

            std::vector<uint32_t> sizeAsUint;
            std::transform(size.cbegin(), size.cend(), std::back_inserter(sizeAsUint), [](PDFInteger integer) { return static_cast<uint32_t>(integer); });

            return std::make_shared<PDFSampledFunction>(static_cast<uint32_t>(m), static_cast<uint32_t>(n), std::move(domain), std::move(range), std::move(sizeAsUint), std::move(samples), std::move(encode), std::move(decode), sampleMaxValue);
        }
        case 2:
        {
            // Exponential function
            std::vector<PDFReal> c0 = loader.readNumberArrayFromDictionary(dictionary, "C0");
            std::vector<PDFReal> c1 = loader.readNumberArrayFromDictionary(dictionary, "C1");
            const PDFReal exponent = loader.readNumberFromDictionary(dictionary, "N", 1.0);

            if (domain.size() != 2)
            {
                throw PDFParserException(PDFParsingContext::tr("Exponential function can have only one input value."));
            }

            if (exponent < 0.0 && domain[0] <= 0.0)
            {
                throw PDFParserException(PDFParsingContext::tr("Invalid domain of exponential function."));
            }

            if (!qFuzzyIsNull(std::fmod(exponent, 1.0)) && domain[0] < 0.0)
            {
                throw PDFParserException(PDFParsingContext::tr("Invalid domain of exponential function."));
            }

            const uint32_t m = 1;

            // Determine n.
            uint32_t n = static_cast<uint32_t>(std::max({ static_cast<size_t>(1), range.size() / 2, c0.size(), c1.size() }));

            // Resolve default values
            if (c0.empty())
            {
                c0.resize(n, 0.0);
            }
            if (c1.empty())
            {
                c1.resize(n, 1.0);
            }

            if (c0.size() != n)
            {
                throw PDFParserException(PDFParsingContext::tr("Invalid parameter of exponential function (at x = 0.0)."));
            }
            if (c1.size() != n)
            {
                throw PDFParserException(PDFParsingContext::tr("Invalid parameter of exponential function (at x = 1.0)."));
            }

            return std::make_shared<PDFExponentialFunction>(1, n, std::move(domain), std::move(range), std::move(c0), std::move(c1), exponent);
        }
        case 3:
        {
            // Stitching function
            std::vector<PDFReal> bounds = loader.readNumberArrayFromDictionary(dictionary, "Bounds");
            std::vector<PDFReal> encode = loader.readNumberArrayFromDictionary(dictionary, "Encode");

            if (domain.size() != 2)
            {
                throw PDFParserException(PDFParsingContext::tr("Stitching function can have only one input value."));
            }

            if (dictionary->hasKey("Functions"))
            {
                const PDFObject& functions = document->getObject(dictionary->get("Functions"));
                if (functions.isArray())
                {
                    const PDFArray* array = functions.getArray();
                    if (array->getCount() != bounds.size() + 1)
                    {
                        throw PDFParserException(PDFParsingContext::tr("Stitching function has different function count. Expected %1, actual %2.").arg(array->getCount()).arg(bounds.size() + 1));
                    }

                    std::vector<PDFStitchingFunction::PartialFunction> partialFunctions;
                    partialFunctions.resize(array->getCount());

                    if (encode.size() != partialFunctions.size() * 2)
                    {
                        throw PDFParserException(PDFParsingContext::tr("Stitching function has invalid encode array. Expected %1 items, actual %2.").arg(partialFunctions.size() * 2).arg(encode.size()));
                    }

                    std::vector<PDFReal> boundsAdjusted;
                    boundsAdjusted.resize(bounds.size() + 2);
                    boundsAdjusted.front() = domain.front();
                    boundsAdjusted.back() = domain.back();
                    std::copy(bounds.cbegin(), bounds.cend(), std::next(boundsAdjusted.begin()));

                    Q_ASSERT(boundsAdjusted.size() == partialFunctions.size() + 1);

                    uint32_t n = 0;
                    for (size_t i = 0; i < partialFunctions.size(); ++i)
                    {
                        PDFStitchingFunction::PartialFunction& partialFunction = partialFunctions[i];
                        partialFunction.function = createFunctionImpl(document, array->getItem(i), context);
                        partialFunction.bound0 = boundsAdjusted[i];
                        partialFunction.bound1 = boundsAdjusted[i + 1];
                        partialFunction.encode0 = encode[2 * i];
                        partialFunction.encode1 = encode[2 * i + 1];

                        const uint32_t nLocal = partialFunction.function->getOutputVariableCount();

                        if (n == 0)
                        {
                            n = nLocal;
                        }
                        else if (n != nLocal)
                        {
                            throw PDFParserException(PDFParsingContext::tr("Functions in stitching function has different number of output variables."));
                        }
                    }

                    return std::make_shared<PDFStitchingFunction>(1, n, std::move(domain), std::move(range), std::move(partialFunctions));
                }
                else
                {
                    throw PDFParserException(PDFParsingContext::tr("Stitching function has invalid functions."));
                }
            }
            else
            {
                throw PDFParserException(PDFParsingContext::tr("Stitching function hasn't functions array."));
            }
        }
        case 4:
        {
            // Postscript function
        }

        default:
        {
            throw PDFParserException(PDFParsingContext::tr("Invalid function type: %1.").arg(functionType));
        }
    }

    return nullptr;
}

PDFSampledFunction::PDFSampledFunction(uint32_t m, uint32_t n,
                                       std::vector<PDFReal>&& domain,
                                       std::vector<PDFReal>&& range,
                                       std::vector<uint32_t>&& size,
                                       std::vector<PDFReal>&& samples,
                                       std::vector<PDFReal>&& encoder,
                                       std::vector<PDFReal>&& decoder,
                                       PDFReal sampleMaximalValue) :
    PDFFunction(m, n, std::move(domain), std::move(range)),
    m_hypercubeNodeCount(1 << m_m),
    m_size(std::move(size)),
    m_samples(std::move(samples)),
    m_encoder(std::move(encoder)),
    m_decoder(std::move(decoder)),
    m_sampleMaximalValue(sampleMaximalValue)
{
    // Asserts, that we get sane input
    Q_ASSERT(m > 0);
    Q_ASSERT(n > 0);
    Q_ASSERT(m_size.size() == m);
    Q_ASSERT(m_domain.size() == 2 * m);
    Q_ASSERT(m_range.size() == 2 * n);
    Q_ASSERT(m_domain.size() == m_encoder.size());
    Q_ASSERT(m_range.size() == m_decoder.size());

    m_hypercubeNodeOffsets.resize(m_hypercubeNodeCount, 0);

    const uint32_t lastInputVariableIndex = m_m - 1;

    // Calculate hypercube offsets. Offsets are indexed in bits, from the lowest
    // bit to the highest. We assume, that we do not have more, than 32 input
    // variables (we probably run out of memory in that time). Example:
    //
    // We have m = 3, f(x_0, x_1, x_2) is sampled function of 3 variables, n = 1.
    // We have 2, 4, 6 samples for x_0, x_1 and x_2 (so sample count differs).
    // Then the i-th bit corresponds to variable x_i. We will have m_hypercubeNodeCount == 8,
    // hypercube offset indices are from 0 to 7.
    //      m_hypercubeNodeOffsets[0] =  0;     - f(0, 0, 0)
    //		m_hypercubeNodeOffsets[1] =  1;     - f(1, 0, 0)
    //		m_hypercubeNodeOffsets[2] =  2;     - f(0, 1, 0)
    //		m_hypercubeNodeOffsets[3] =  3;     - f(1, 1, 0)
    //		m_hypercubeNodeOffsets[4] =  8;     - f(0, 0, 1)    2 * 4 = 8
    //		m_hypercubeNodeOffsets[5] =  9;     - f(1, 0, 1)    2 * 4 + 1 (for x_1 = 1, x_2 = 0) = 8
    //		m_hypercubeNodeOffsets[6] = 10;     - f(0, 1, 1)    2 * 4 + 2 (for x_1 = 0, x_2 = 1) = 9
    //		m_hypercubeNodeOffsets[7] = 11;     - f(1, 1, 1)    2 * 4 + 2 + 1 = 11
    for (uint32_t i = 0; i < m_hypercubeNodeCount; ++i)
    {
        uint32_t index = 0;
        uint32_t mask = i;
        for (uint32_t j = lastInputVariableIndex; j > 0; --j)
        {
            uint32_t bit = 0;
            if (m_size[j] > 1)
            {
                // We shift mask, so we are accessing bits from highest to lowest in reverse order
                bit = (mask >> lastInputVariableIndex) & static_cast<uint32_t>(1);
            }

            index = (index + bit) * m_size[j - 1];
            mask = mask << 1;
        }

        uint32_t lastBit = 0;
        if (m_size[0] > 1)
        {
            lastBit = (mask >> lastInputVariableIndex) & static_cast<uint32_t>(1);
        }

        m_hypercubeNodeOffsets[i] = (index + lastBit) * m_n;
    }
}

PDFFunction::FunctionResult PDFSampledFunction::apply(const_iterator x_1,
                                                      const_iterator x_m,
                                                      iterator y_1,
                                                      iterator y_n) const
{
    const size_t m = std::distance(x_1, x_m);
    const size_t n = std::distance(y_1, y_n);

    if (m != m_m)
    {
        return PDFTranslationContext::tr("Invalid number of operands for function. Expected %1, provided %2.").arg(m_m).arg(m);
    }
    if (n != m_n)
    {
        return PDFTranslationContext::tr("Invalid number of output variables for function. Expected %1, provided %2.").arg(m_n).arg(n);
    }

    PDFFlatArray<uint32_t, DEFAULT_OPERAND_COUNT> encoded;
    PDFFlatArray<PDFReal, DEFAULT_OPERAND_COUNT> encoded0;
    PDFFlatArray<PDFReal, DEFAULT_OPERAND_COUNT> encoded1;

    for (uint32_t i = 0; i < m_m; ++i)
    {
        const PDFReal x = *std::next(x_1, i);

        // First clamp it in the function domain
        const PDFReal xClamped = clampInput(i, x);
        const PDFReal xEncoded = interpolate(xClamped, m_domain[2 * i], m_domain[2 * i + 1], m_encoder[2 * i], m_encoder[2 * i + 1]);
        const PDFReal xClampedToSamples = qBound<PDFReal>(0, xEncoded, m_size[i]);

        uint32_t xRounded = static_cast<uint32_t>(xClampedToSamples);
        if (xRounded == m_size[i] && m_size[i] > 1)
        {
            // We want one value before the end (so we can use the "hypercube" algorithm)
            xRounded = m_size[i] - 2;
        }

        const PDFReal x1 = xClampedToSamples - static_cast<PDFReal>(xRounded);
        const PDFReal x0 = 1.0 - x1;
        encoded.push_back(xRounded);
        encoded0.push_back(x0);
        encoded1.push_back(x1);
    }

    // Index (offset) for hypercube node (0, 0, ..., 0)
    uint32_t baseOffset = 0;
    for (uint32_t i = m_m - 1; i > 0; --i)
    {
        baseOffset = (baseOffset + encoded[i]) * m_size[i - 1];
    }
    baseOffset = (baseOffset + encoded[0]) * m_n;

    // Samples for hypercube nodes (for each hypercube node, single
    // sample is fetched). Of course, size of this array is 2^m, so
    // it can be very huge.
    PDFFlatArray<PDFReal, DEFAULT_OPERAND_COUNT> hyperCubeSamples;
    hyperCubeSamples.resize(m_hypercubeNodeCount);

    for (uint32_t outputIndex = 0; outputIndex < m_n; ++outputIndex)
    {
        // Load samples into hypercube
        for (uint32_t i = 0; i < m_hypercubeNodeCount; ++i)
        {
            const uint32_t offset = baseOffset + m_hypercubeNodeOffsets[i] + outputIndex;
            hyperCubeSamples[i] = (offset < m_samples.size()) ? m_samples[offset] : 0.0;
        }

        // We have loaded samples into the hypercube. Now, in each round of algorithm,
        // reduce the hypercube dimension by 1. At the end, we will have hypercube
        // with dimension 0, e.g. node.
        uint32_t currentHypercubeNodeCount = m_hypercubeNodeCount;
        for (uint32_t i = 0; i < m_m; ++i)
        {
            for (uint32_t j = 0; j < currentHypercubeNodeCount; j += 2)
            {
                hyperCubeSamples[j / 2] = encoded0[i] * hyperCubeSamples[j] + encoded1[i] * hyperCubeSamples[j + 1];
            }

            // We have reduced the hypercube node count 2 times - we have
            // reduced it by one dimension.
            currentHypercubeNodeCount = currentHypercubeNodeCount / 2;
        }

        const PDFReal outputValue = hyperCubeSamples[0];
        const PDFReal outputValueDecoded = interpolate(outputValue, 0.0, m_sampleMaximalValue, m_decoder[2 * outputIndex], m_decoder[2 * outputIndex + 1]);
        const PDFReal outputValueClamped = clampOutput(outputIndex, outputValueDecoded);
        *std::next(y_1, outputIndex) = outputValueClamped;
    }

    return true;
}

PDFExponentialFunction::PDFExponentialFunction(uint32_t m, uint32_t n,
                                               std::vector<PDFReal>&& domain,
                                               std::vector<PDFReal>&& range,
                                               std::vector<PDFReal>&& c0,
                                               std::vector<PDFReal>&& c1,
                                               PDFReal exponent) :
    PDFFunction(m, n, std::move(domain), std::move(range)),
    m_c0(std::move(c0)),
    m_c1(std::move(c1)),
    m_exponent(exponent),
    m_isLinear(qFuzzyCompare(exponent, 1.0))
{
    Q_ASSERT(m == 1);
    Q_ASSERT(m_c0.size() == n);
    Q_ASSERT(m_c1.size() == n);
}

PDFFunction::FunctionResult PDFExponentialFunction::apply(PDFFunction::const_iterator x_1,
                                                          PDFFunction::const_iterator x_m,
                                                          PDFFunction::iterator y_1,
                                                          PDFFunction::iterator y_n) const
{
    const size_t m = std::distance(x_1, x_m);
    const size_t n = std::distance(y_1, y_n);

    if (m != m_m)
    {
        return PDFTranslationContext::tr("Invalid number of operands for function. Expected %1, provided %2.").arg(m_m).arg(m);
    }
    if (n != m_n)
    {
        return PDFTranslationContext::tr("Invalid number of output variables for function. Expected %1, provided %2.").arg(m_n).arg(n);
    }

    Q_ASSERT(m == 1);
    const PDFReal x = clampInput(0, *x_1);

    if (!m_isLinear)
    {
        // Perform exponential interpolation
        size_t index = 0;
        for (PDFFunction::iterator y = y_1; y != y_n; ++y, ++index)
        {
            *y = m_c0[index] + std::pow(x, m_exponent) * (m_c1[index] - m_c0[index]);
        }
    }
    else
    {
        // Perform linear interpolation
        size_t index = 0;
        for (PDFFunction::iterator y = y_1; y != y_n; ++y, ++index)
        {
            *y = mix(x, m_c0[index], m_c1[index]);
        }
    }

    if (hasRange())
    {
        size_t index = 0;
        for (PDFFunction::iterator y = y_1; y != y_n; ++y, ++index)
        {
            *y = clampOutput(index, *y);
        }
    }

    return true;
}

PDFStitchingFunction::PDFStitchingFunction(uint32_t m, uint32_t n,
                                           std::vector<PDFReal>&& domain,
                                           std::vector<PDFReal>&& range,
                                           std::vector<PDFStitchingFunction::PartialFunction>&& partialFunctions) :
    PDFFunction(m, n, std::move(domain), std::move(range)),
    m_partialFunctions(std::move(partialFunctions))
{
    Q_ASSERT(m == 1);
}

PDFStitchingFunction::~PDFStitchingFunction()
{

}

PDFFunction::FunctionResult PDFStitchingFunction::apply(const_iterator x_1,
                                                        const_iterator x_m,
                                                        iterator y_1,
                                                        iterator y_n) const
{
    const size_t m = std::distance(x_1, x_m);
    const size_t n = std::distance(y_1, y_n);

    if (m != m_m)
    {
        return PDFTranslationContext::tr("Invalid number of operands for function. Expected %1, provided %2.").arg(m_m).arg(m);
    }
    if (n != m_n)
    {
        return PDFTranslationContext::tr("Invalid number of output variables for function. Expected %1, provided %2.").arg(m_n).arg(n);
    }

    Q_ASSERT(m == 1);
    const PDFReal x = clampInput(0, *x_1);

    // First search for partial function, which defines our range. Use algorithm
    // similar to the std::lower_bound.
    size_t count = m_partialFunctions.size();
    size_t functionIndex = 0;
    while (count > 0)
    {
        const size_t step = count / 2;
        const size_t current = functionIndex + step;

        if (m_partialFunctions[current].bound1 < x)
        {
            functionIndex = current + 1;
            count = count - functionIndex;
        }
        else
        {
            count = current;
        }
    }
    if (functionIndex == m_partialFunctions.size())
    {
        --functionIndex;
    }
    const PartialFunction& function = m_partialFunctions[functionIndex];

    // Encode the value into the input range of the function
    const PDFReal xEncoded = interpolate(x, function.bound0, function.bound1, function.encode0, function.encode1);
    FunctionResult result = function.function->apply(&xEncoded, &xEncoded + 1, y_1, y_n);

    if (hasRange())
    {
        size_t index = 0;
        for (PDFFunction::iterator y = y_1; y != y_n; ++y, ++index)
        {
            *y = clampOutput(index, *y);
        }
    }

    return result;
}

PDFIdentityFunction::PDFIdentityFunction() :
    PDFFunction(0, 0, std::vector<PDFReal>(), std::vector<PDFReal>())
{

}

PDFFunction::FunctionResult PDFIdentityFunction::apply(const_iterator x_1,
                                                       const_iterator x_m,
                                                       iterator y_1,
                                                       iterator y_n) const
{
    const size_t m = std::distance(x_1, x_m);
    const size_t n = std::distance(y_1, y_n);

    if (m != n)
    {
        return PDFTranslationContext::tr("Invalid number of operands for identity function. Expected %1, provided %2.").arg(n).arg(m);
    }

    std::copy(x_1, x_m, y_1);
    return true;
}

}   // namespace pdf
