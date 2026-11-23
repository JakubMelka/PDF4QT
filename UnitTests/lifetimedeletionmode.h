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

#ifndef LIFETIMEDELETIONMODE_H
#define LIFETIMEDELETIONMODE_H

#include "appdocumentfixture.h"
#include "pdfdeferreddeletion.h"

#include <QByteArray>
#include <QObject>
#include <QRegularExpression>
#include <QTest>

namespace pdftest
{

/// How the lifetime sweeps of the App delete their victims, and the switch of the synchronous deletion detector in
/// the App fixtures (step A, newapp_robustness.md 5.1, 11 point 8, 13 RR2-M5, RR-M7; A0 removed the nested event
/// loops, so there is no nested mode). The six App sweeps close their victims through LifetimeVictimCheck
/// (lifetimevictimcheck.h), which runs the checks of a retired victim.
class LifetimeDeletionMode
{
public:
    LifetimeDeletionMode() = delete;

    enum class Mode
    {
        /// The victim goes through the real single place of the application (AppDocumentFixture::retire: a tab by
        /// its close, a page by the navigation, a tool shell by the close of its page). It stays alive (inert) until
        /// the step returns to the test body, which settles. The victims window and MCP controller are not used (the
        /// application deletes them only after its event loop, RR-M7): the victim "end" closes the application after
        /// each step of the scenario instead.
        Deferred,

        /// Diagnostics: the victim retired by PDFDeferredDeletion::retire itself, not by the close of the application
        /// (the lists of the window are not checked) - the generic steps without the code of the close
        Retire,

        /// Diagnostics: the delete of today at the site of the emission (a deliberate synchronous deletion inside a
        /// PDFDeferredDeletion::SynchronousDeletionScope), with the deferred sites and states of the sweeps and the
        /// victims window and controller
        Synchronous
    };

    /// The mode of the App sweeps (newapp_robustness.md 5.1)
    static constexpr Mode DEFAULT_MODE = Mode::Deferred;

    /// The detector switch of the App fixtures: on since the integration of step A (INT2; PA0 had it off until the
    /// packages converted their deletions). PDF4QT_SYNCHRONOUS_DELETION_CHECK=0 turns it off for a diagnosis.
    static constexpr bool DEFAULT_SYNCHRONOUS_DELETION_CHECK = true;

    /// The name of the victim, which closes the application after a step (Deferred and Retire)
    static constexpr const char* END_VICTIM = "end";

    /// PDF4QT_LIFETIME_MODE = deferred | retire | sync (empty or unknown = DEFAULT_MODE)
    static Mode getMode()
    {
        bool ok = false;
        const Mode mode = parseMode(qgetenv("PDF4QT_LIFETIME_MODE"), &ok);
        return ok ? mode : DEFAULT_MODE;
    }

    /// The victim is retired (Deferred, Retire), not deleted synchronously
    static bool isRetiring() { return getMode() != Mode::Synchronous; }

    /// The sweeps have the victims window and MCP controller (Synchronous only; RR-M7)
    static bool hasWindowVictim() { return getMode() == Mode::Synchronous; }

    /// The name of the mode in PDF4QT_LIFETIME_MODE
    static const char* getModeName(Mode mode)
    {
        switch (mode)
        {
            case Mode::Deferred:
                return "deferred";
            case Mode::Retire:
                return "retire";
            case Mode::Synchronous:
                break;
        }
        return "sync";
    }

    /// \p ok false for an unknown text (the mode is DEFAULT_MODE then)
    static Mode parseMode(const QByteArray& text, bool* ok)
    {
        const QByteArray value = text.trimmed().toLower();
        *ok = true;
        if (value == "deferred")
        {
            return Mode::Deferred;
        }
        if (value == "retire")
        {
            return Mode::Retire;
        }
        if (value == "sync")
        {
            return Mode::Synchronous;
        }
        *ok = false;
        return DEFAULT_MODE;
    }

    /// The victim action at the site of an emission, by getMode(), without the checks of LifetimeVictimCheck. It
    /// never settles (the site is inside an emission): the sweep settles, when the step has returned to the test
    /// body (arch/tests.md, RR2-L6). \returns false, when the deferred close was refused (a question stays open)
    static bool act(QObject* victim, AppDocumentFixture* fixture)
    {
        if (!victim)
        {
            return false;
        }

        switch (getMode())
        {
            case Mode::Deferred:
                if (fixture)
                {
                    return fixture->retire(victim);
                }
                pdf::PDFDeferredDeletion::retire(victim);
                return true;

            case Mode::Retire:
                pdf::PDFDeferredDeletion::retire(victim);
                return true;

            case Mode::Synchronous:
                break;
        }

        pdf::PDFDeferredDeletion::SynchronousDeletionScope scope;
        delete victim;
        return true;
    }

    /// The detector reports in the App fixtures: PDF4QT_SYNCHRONOUS_DELETION_CHECK=1 / 0, otherwise
    /// DEFAULT_SYNCHRONOUS_DELETION_CHECK
    static bool isSynchronousDeletionCheck()
    {
        bool ok = false;
        const int value = qEnvironmentVariableIntValue("PDF4QT_SYNCHRONOUS_DELETION_CHECK", &ok);
        return ok ? value != 0 : DEFAULT_SYNCHRONOUS_DELETION_CHECK;
    }

    /// When the switch is on: the detector reports and the current test function fails on its message
    /// (QTest::failOnWarning holds for one test function - a fixture shared by more functions calls this in init()).
    /// The constructor of AppDocumentFixture calls it.
    static void installSynchronousDeletionCheck()
    {
        if (!isSynchronousDeletionCheck())
        {
            return;
        }

        pdf::PDFDeferredDeletion::setSynchronousDeletionReported(true);
        const QString message = QString::fromLatin1(pdf::PDFDeferredDeletion::SYNCHRONOUS_DELETION_MESSAGE);
        QTest::failOnWarning(QRegularExpression(QStringLiteral("^%1").arg(QRegularExpression::escape(message))));
    }
};

}   // namespace pdftest

#endif // LIFETIMEDELETIONMODE_H
