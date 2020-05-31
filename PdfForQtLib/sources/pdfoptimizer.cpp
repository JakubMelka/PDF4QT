//    Copyright (C) 2020 Jakub Melka
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
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

#include "pdfoptimizer.h"

namespace pdf
{

PDFOptimizer::PDFOptimizer(OptimizationFlags flags, QObject* parent) :
    QObject(parent),
    m_flags(flags)
{

}

void PDFOptimizer::optimize()
{
    // Jakub Melka: We divide optimization into stages, each
    // stage can consist from multiple passes.
    constexpr auto stages = { OptimizationFlags(DereferenceSimpleObjects),
                              OptimizationFlags(RemoveNullObjects),
                              OptimizationFlags(RemoveUnusedObjects | MergeIdenticalObjects),
                              OptimizationFlags(ShrinkObjectStorage),
                              OptimizationFlags(RecompressFlateStreams) };

    int stage = 1;

    emit optimizationStarted();
    for (OptimizationFlags flags : stages)
    {
        emit optimizationProgress(tr("Stage %1").arg(stage++));
        OptimizationFlags currentSteps = flags & m_flags;

        int passIndex = 1;

        bool pass = true;
        while (pass)
        {
            emit optimizationProgress(tr("Pass %1").arg(passIndex++));
            pass = false;

            if (currentSteps.testFlag(DereferenceSimpleObjects))
            {
                pass = performDereferenceSimpleObjects() || pass;
            }
            if (currentSteps.testFlag(RemoveNullObjects))
            {
                pass = performRemoveNullObjects() || pass;
            }
            if (currentSteps.testFlag(RemoveUnusedObjects))
            {
                pass = performRemoveUnusedObjects() || pass;
            }
            if (currentSteps.testFlag(MergeIdenticalObjects))
            {
                pass = performMergeIdenticalObjects() || pass;
            }
            if (currentSteps.testFlag(ShrinkObjectStorage))
            {
                pass = performShrinkObjectStorage() || pass;
            }
            if (currentSteps.testFlag(RecompressFlateStreams))
            {
                pass = performRecompressFlateStreams() || pass;
            }
        }
    }
    emit optimizationFinished();
}

PDFOptimizer::OptimizationFlags PDFOptimizer::getFlags() const
{
    return m_flags;
}

void PDFOptimizer::setFlags(OptimizationFlags flags)
{
    m_flags = flags;
}

bool PDFOptimizer::performDereferenceSimpleObjects()
{
    return false;
}

bool PDFOptimizer::performRemoveNullObjects()
{
    return false;
}

bool PDFOptimizer::performRemoveUnusedObjects()
{
    return false;
}

bool PDFOptimizer::performMergeIdenticalObjects()
{
    return false;
}

bool PDFOptimizer::performShrinkObjectStorage()
{
    return false;
}

bool PDFOptimizer::performRecompressFlateStreams()
{
    return false;
}

}   // namespace pdf
