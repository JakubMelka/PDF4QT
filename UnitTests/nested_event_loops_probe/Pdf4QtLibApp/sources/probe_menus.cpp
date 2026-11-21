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

// A probe of nested_event_loops.py --self-test (never compiled): tool buttons with a menu (QMenuPrivate::exec)

#include "probe_exec.h"

ProbeToolButton::ProbeToolButton(QWidget* parent) :
    QToolButton(parent)
{
    setMenu(new QMenu(this));                               // EXPECT ENTRY
    setPopupMode(QToolButton::InstantPopup);                // EXPECT ENTRY
    setPopupMode(DelayedPopup);                             // EXPECT ENTRY
}

void ProbeOwnMenuButton::setMenu(QMenu* menu)               // EXPECT NONE
{
    m_menu = menu;
}

void ProbeUser::buttons(QToolButton* button, ProbeOwnMenuButton* own)
{
    button->setMenu(m_menu);                                // EXPECT ENTRY
    button->setPopupMode(QToolButton::MenuButtonPopup);     // EXPECT ENTRY
    own->setMenu(m_menu);                                   // EXPECT COVERED
    m_action->setMenu(m_menu);                              // EXPECT ENTRY
    QToolButton::setMenu(m_menu);                           // EXPECT ENTRY
}

void ProbeUser::rows(QStyleOptionToolButton* option)
{
    QToolButton* row = new QToolButton();
    row->setMenu(m_menu);                                   // EXPECT ENTRY
    row->setMenu(nullptr);                                  // EXPECT NONE
    const bool split = option->features.testFlag(QStyleOptionToolButton::MenuButtonPopup);   // EXPECT NONE
}

void ProbeUser::otherRows()
{
    ProbeOwnMenuButton* row = new ProbeOwnMenuButton();
    row->setMenu(m_menu);                                   // EXPECT COVERED
}
