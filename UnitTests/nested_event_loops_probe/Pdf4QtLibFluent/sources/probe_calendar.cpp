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

// A probe of nested_event_loops.py --self-test (never compiled): calendars and calendar popups

#include "probe_calendar.h"

void ProbeDateEdit::setCalendarPopupEnabled(bool enabled)   // EXPECT NONE
{
    setCalendarPopup(enabled);                              // EXPECT COVERED
}

void ProbeDateEdit::ensureCalendar()
{
    setCalendarWidget(new ProbeOwnCalendar(this));          // EXPECT NONE
}

void ProbeQtDateEdit::enable()
{
    setCalendarPopup(true);                                 // EXPECT ENTRY
    setCalendarWidget(new ProbeHalfCalendar(this));         // EXPECT NONE
}

void ProbeCalendarUser::calendars(QWidget* parent, ProbeDateEdit* own, QDateTimeEdit* qt)
{
    new QCalendarWidget(parent);                            // EXPECT ENTRY
    own->setCalendarPopupEnabled(true);                     // EXPECT COVERED
    qt->setCalendarPopup(true);                             // EXPECT ENTRY
    qt->setCalendarPopup(false);                            // EXPECT NONE
}
