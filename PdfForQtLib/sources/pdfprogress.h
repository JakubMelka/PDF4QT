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

#ifndef PDFPROGRESS_H
#define PDFPROGRESS_H

#include "pdfglobal.h"

#include <QObject>

#include <atomic>

namespace pdf
{

class PDFFORQTLIBSHARED_EXPORT PDFProgress : public QObject
{
    Q_OBJECT

public:
    explicit PDFProgress(QObject* parent) : QObject(parent) { }

    void start(size_t stepCount);
    void step();
    void finish();

signals:
    void progressStarted();
    void progressStep(int percentage);
    void progressFinished();

private:
    std::atomic<size_t> m_currentStep = 0;
    std::atomic<size_t> m_stepCount = 0;
    std::atomic<int> m_percentage = 0;
};

}   // namespace pdf

#endif // PDFPROGRESS_H
