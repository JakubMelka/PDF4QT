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

// A probe of nested_event_loops.py --self-test (never compiled): Qt's calendar runs exec in its month button
// (InstantPopup) and in its year editor (a QSpinBox); an own calendar declares both markers in its class body

class ProbeQtCalendar : public QCalendarWidget              // EXPECT ENTRY
{
};

class ProbeOwnCalendar : public QCalendarWidget             // EXPECT COVERED
{
public:
    static constexpr bool HAS_OWN_MONTH_MENU = true;
    static constexpr bool HAS_OWN_YEAR_EDITOR = true;
};

class ProbeHalfCalendar : public QCalendarWidget            // EXPECT ENTRY
{
public:
    static constexpr bool HAS_OWN_MONTH_MENU = true;
    // static constexpr bool HAS_OWN_YEAR_EDITOR = true; - a comment is no marker
};

class ProbeDateEdit : public QDateTimeEdit                  // EXPECT COVERED
{
public:
    void setCalendarPopupEnabled(bool enabled);             // EXPECT NONE
    void ensureCalendar();

protected:
    virtual void contextMenuEvent(QContextMenuEvent* event) override;
};

class ProbeQtDateEdit : public QDateTimeEdit                // EXPECT COVERED
{
public:
    void enable();

protected:
    virtual void contextMenuEvent(QContextMenuEvent* event) override;
};

class ProbeCalendarUser
{
public:
    void calendars(QWidget* parent, ProbeDateEdit* own, QDateTimeEdit* qt);
};
