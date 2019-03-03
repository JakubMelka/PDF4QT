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

namespace pdf
{

PDFFunction::PDFFunction(uint32_t m, uint32_t n, std::vector<PDFReal>&& domain, std::vector<PDFReal>&& range) :
    m_m(m),
    m_n(n),
    m_domain(std::move(domain)),
    m_range(std::move(range))
{

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
    const PDFReal x = clampInput(0, *x1);

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
    const PDFReal x = clampInput(0, *x1);

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

}   // namespace pdf
