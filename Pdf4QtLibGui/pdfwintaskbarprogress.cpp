// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pdfwintaskbarprogress.h"

#include <QWindow>

#if defined(Q_OS_WIN) && !defined(PDF4QT_NO_TASKBAR_PROGRESS)
#define PDF4QT_TASKBAR_PROGRESS
#endif

#ifdef PDF4QT_TASKBAR_PROGRESS
#include <ShObjIdl.h>
#endif

namespace pdfviewer
{

#ifdef PDF4QT_TASKBAR_PROGRESS
class PDFWinTaskBarProgressImpl
{
public:
    PDFWinTaskBarProgressImpl()
    {
        HRESULT result = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, __uuidof(ITaskbarList3), reinterpret_cast<void**>(&m_taskBarList));

        if (FAILED(result))
        {
            qWarning("Failed to create task bar progress");
            m_taskBarList = nullptr;
        }
        else
        {
            HRESULT taskBarInitResult = m_taskBarList->HrInit();

            if (FAILED(taskBarInitResult))
            {
                qWarning("Failed to initialize task bar progress");
                m_taskBarList->Release();
                m_taskBarList = nullptr;
            }
        }
    }

    ~PDFWinTaskBarProgressImpl()
    {
        if (m_taskBarList)
        {
            m_taskBarList->Release();
            m_taskBarList = nullptr;
        }
    }

    inline HWND getHandle() const
    {
        return m_window ? reinterpret_cast<HWND>(m_window->winId()) : HWND();
    }

    inline void updateProgress() const
    {
        if (m_window && m_taskBarList)
        {
            const pdf::PDFReal range = m_progressMax - m_progressMin;
            const pdf::PDFReal offset = m_value - m_progressMin;

            const ULONGLONG max = 1000;
            if (!qFuzzyIsNull(range))
            {
                const ULONGLONG progress = max * offset / range;
                m_taskBarList->SetProgressValue(getHandle(), progress, max);
            }
            else
            {
                m_taskBarList->SetProgressValue(getHandle(), 0, max);
            }

            TBPFLAG stateFlag = TBPF_NOPROGRESS;
            if (m_progressBarVisible)
            {
                stateFlag = TBPF_NORMAL;
            }

            m_taskBarList->SetProgressState(getHandle(), stateFlag);
        }
    }

    inline void setWindow(QWindow* window)
    {
        if (m_window = window)
        {
            m_window = window;
            updateProgress();
        }
    }

    inline void reset()
    {
        setValue(m_progressMin);
    }

    inline void show()
    {
        if (!m_progressBarVisible)
        {
            m_progressBarVisible = true;
            updateProgress();
        }
    }

    inline void hide()
    {
        if (m_progressBarVisible)
        {
            m_progressBarVisible = false;
            updateProgress();
        }
    }

    inline void setValue(pdf::PDFInteger value)
    {
        if (m_value != value)
        {
            m_value = qBound(m_progressMin, value, m_progressMax);
            updateProgress();
        }
    }

    inline void setRange(pdf::PDFInteger min, pdf::PDFInteger max)
    {
        Q_ASSERT(min < max);

        if (m_progressMin != min || m_progressMax != max)
        {
            m_progressMin = min;
            m_progressMax = max;
            m_value = qBound(min, m_value, max);
            updateProgress();
        }
    }

private:
    QWindow* m_window = nullptr;
    ITaskbarList3* m_taskBarList = nullptr;

    // Progress bar
    bool m_progressBarVisible = false;
    pdf::PDFInteger m_progressMin = 0;
    pdf::PDFInteger m_progressMax = 100;
    pdf::PDFInteger m_value = 0;
};

#else

class PDFWinTaskBarProgressImpl
{
public:
    constexpr PDFWinTaskBarProgressImpl() = default;
    constexpr ~PDFWinTaskBarProgressImpl() = default;

    constexpr void setWindow(QWindow* window) { Q_UNUSED(window); }

    constexpr void reset() { }
    constexpr void show() { }
    constexpr void hide() { }

    constexpr void setValue(pdf::PDFInteger value) { Q_UNUSED(value); }
    constexpr void setRange(pdf::PDFInteger min, pdf::PDFInteger max) { Q_UNUSED(min); Q_UNUSED(max); }
};

#endif

PDFWinTaskBarProgress::PDFWinTaskBarProgress(QObject* parent) :
    QObject(parent),
    m_impl(new PDFWinTaskBarProgressImpl())
{

}

PDFWinTaskBarProgress::~PDFWinTaskBarProgress()
{
    delete m_impl;
}

void PDFWinTaskBarProgress::setWindow(QWindow* window)
{
    m_impl->setWindow(window);
}

void PDFWinTaskBarProgress::reset()
{
    m_impl->reset();
}

void PDFWinTaskBarProgress::show()
{
    m_impl->show();
}

void PDFWinTaskBarProgress::hide()
{
    m_impl->hide();
}

void PDFWinTaskBarProgress::setValue(pdf::PDFInteger value)
{
    m_impl->setValue(value);
}

void PDFWinTaskBarProgress::setRange(pdf::PDFInteger min, pdf::PDFInteger max)
{
    m_impl->setRange(min, max);
}

}   // namespace pdfviewer
