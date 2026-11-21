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

// A probe of nested_event_loops.py --self-test (never compiled): every line with an expectation must be reported so

class ProbeDialog : public QDialog
{
public:
    virtual int exec() override;                // EXPECT NONE
    void runInline() { exec(); }                // EXPECT ENTRY
    void runBase();
};

class ProbeToolButton : public QToolButton
{
public:
    explicit ProbeToolButton(QWidget* parent);
};

class ProbeOwnMenuButton : public QToolButton
{
public:
    void setMenu(QMenu* menu);                  // EXPECT NONE

private:
    QMenu* m_menu = nullptr;
};

class ProbeUser
{
public:
    void run(QObject* object, QWidget* window);
    void buttons(QToolButton* button, ProbeOwnMenuButton* own);

private:
    QList<QAction*> m_actions;
    QList<QDialog*> m_dialogs;
    QDialog* m_dialog = nullptr;
    QMenu* m_menu = nullptr;
    QAction* m_action = nullptr;
};
