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

#include "pdfprogress.h"

namespace pdf
{

void PDFProgress::start(size_t stepCount)
{
    Q_ASSERT(stepCount > 0);

    m_currentStep = 0;
    m_stepCount = stepCount;
    m_percentage = 0;

    emit progressStarted();
}

void PDFProgress::step()
{
    // Atomically increment by one. We must add + 1 to the current step,
    // because fetch_add will return old value. Then we must test, if percentage
    // has been changed.
    size_t currentStep = m_currentStep.fetch_add(1) + 1;

    int newPercentage = int((size_t(100) * currentStep) / m_stepCount);
    int oldPercentage = m_percentage.load(std::memory_order_acquire);
    bool emitSignal = oldPercentage < newPercentage;
    do
    {
        emitSignal = oldPercentage < newPercentage;
    } while (oldPercentage < newPercentage && !m_percentage.compare_exchange_weak(oldPercentage, newPercentage, std::memory_order_release, std::memory_order_relaxed));

    if (emitSignal)
    {
        progressStep(newPercentage);
    }
}

void PDFProgress::finish()
{
    emit progressFinished();
}

} // namespace pdf
