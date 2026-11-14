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

// The lifetime scenarios of Create Audio Book and its overlay (step 4 of phase 2, package P4; plan
// 8): the job of the text stream, a click on the list and on the overlay, Skip, an edited text,
// a move, the question of Rebuild (356:14056), the popover of Select (341:10217), the run of a fake
// engine and its result dialog (341:11159); Play Sample, the Voice menu, the context menu of the
// list, Select by rectangle, a new Order with its question, and the message without a voice
// (341:11665)

#include "apptoolpageslifetime.h"

#include "pdfappaudiobookoverlay.h"
#include "pdfappdocumentspeech.h"
#include "pdfapptoolgui_audiobook.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfcomponentbuttons.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttoolshell.h"
#include "pdfdocumentwriter.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QtTest>

/// A synthesizer of the scenarios: a short block of silence-free 16 bit audio per text
class AudioBookLifetimeSynthesizer : public pdf::PDFSpeechSynthesizer
{
public:
    virtual bool setVoice(const QString&, QString*) override { return true; }
    virtual void setRate(double) override { }
    virtual void setVolume(double) override { }
    virtual bool synthesize(const QString& text, pdf::PDFAudioSink* sink, const pdf::PDFOperationControl*, QString*) override
    {
        const pdf::PDFAudioFormat format{ 16000, 1, pdf::PDFAudioFormat::SampleFormat::Int16 };
        return sink->writeAudio(format, QByteArray(int(text.size()) * 20, '\x10'));
    }
};

class AudioBookLifetimeFactory : public pdf::PDFSpeechSynthesizerFactory
{
public:
    explicit AudioBookLifetimeFactory(bool hasVoices = true) : m_hasVoices(hasVoices) { }

    virtual bool isAvailable() const override { return m_hasVoices; }
    virtual std::vector<pdf::PDFSpeechVoice> getVoices() const override
    {
        if (!m_hasVoices)
        {
            return { };
        }
        return { { QStringLiteral("Microsoft Zira"), QStringLiteral("en-US") }, { QStringLiteral("Microsoft Jakub"), QStringLiteral("cs-CZ") } };
    }
    virtual std::unique_ptr<pdf::PDFSpeechSynthesizer> createSynthesizer(QString*) const override { return m_hasVoices ? std::make_unique<AudioBookLifetimeSynthesizer>() : nullptr; }

private:
    bool m_hasVoices;
};

/// The engine of Play Sample (never a voice of the system): it speaks until Stop, its state is
/// told by its signal at once (a site of the recorder)
class AudioBookLifetimeSpeechEngine : public pdf::PDFAppSpeechEngine
{
public:
    explicit AudioBookLifetimeSpeechEngine(QObject* parent) : pdf::PDFAppSpeechEngine(parent) { }

    static pdf::PDFAppSpeechEngine* create(const QString&, QObject* parent) { return new AudioBookLifetimeSpeechEngine(parent); }

    virtual State getState() const override { return m_state; }
    virtual QString getErrorString() const override { return QString(); }
    virtual QList<QLocale> getLocales() const override { return { QLocale(QLocale::English, QLocale::UnitedStates) }; }
    virtual QLocale getLocale() const override { return QLocale(QLocale::English, QLocale::UnitedStates); }
    virtual void setLocale(const QLocale&) override { }
    virtual QList<pdf::PDFAppSpeechVoice> getVoices() const override { return { }; }
    virtual QString getVoice() const override { return m_voice; }
    virtual void setVoice(const QString& name) override { m_voice = name; }
    virtual void setRate(double) override { }
    virtual void setPitch(double) override { }
    virtual void setVolume(double) override { }
    virtual void say(const QString&) override { m_state = State::Speaking; Q_EMIT stateChanged(); }
    virtual void pause() override { }
    virtual void resume() override { }
    virtual void stop() override { m_state = State::Ready; Q_EMIT stateChanged(); }

private:
    State m_state = State::Ready;
    QString m_voice;
};

/// Helpers of the scenarios of package P4
class AudioBookLifetimeHelper
{
public:
    AudioBookLifetimeHelper() = delete;

    using Steps = ToolPagesLifetimeSteps;

    static pdf::PDFAppToolGUIAudioBook* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? qobject_cast<pdf::PDFAppToolGUIAudioBook*>(shell->getGUI()) : nullptr;
    }

    /// Two pages with three texts; the fake engine of the tool and of Play Sample (never a voice
    /// of the system; the factory of Play Sample stays for the process, no scenario speaks aloud)
    static pdf::PDFAppDocumentTab* openText(pdftest::AppToolShellFixture& fixture)
    {
        return openTextWithVoices(fixture, true);
    }

    /// The document of openText for an engine without a voice (341:11665)
    static pdf::PDFAppDocumentTab* openTextWithoutVoice(pdftest::AppToolShellFixture& fixture)
    {
        return openTextWithVoices(fixture, false);
    }

    static pdf::PDFAppDocumentTab* openTextWithVoices(pdftest::AppToolShellFixture& fixture, bool hasVoices)
    {
        fixture.context->setSpeechSynthesizerFactory(std::make_shared<AudioBookLifetimeFactory>(hasVoices));
        pdf::PDFAppSpeechEngine::setFactory(&AudioBookLifetimeSpeechEngine::create);

        pdf::PDFDocumentBuilder builder;
        const QStringList texts = { QStringLiteral("First paragraph of the book"), QStringLiteral("Second paragraph"), QStringLiteral("Third paragraph on the next page") };
        for (int pageIndex = 0; pageIndex < 2; ++pageIndex)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 595, 842));
            pdf::PDFPageContentStreamBuilder content(&builder);
            QPainter* painter = content.begin(page);
            QFont font(QStringLiteral("Inter"));
            font.setPixelSize(14);
            painter->setFont(font);
            for (int i = pageIndex == 0 ? 0 : 2; i < (pageIndex == 0 ? 2 : 3); ++i)
            {
                painter->drawText(QPointF(60, 100 + 300 * (i % 2)), texts[i]);
            }
            content.end(painter);
        }
        pdf::PDFDocument document = builder.build();
        const QString path = fixture.folder() + QStringLiteral("/lifetime-audiobook.pdf");
        if (!pdf::PDFDocumentWriter(nullptr).write(path, &document, false))
        {
            return nullptr;
        }
        pdf::PDFAppDocumentTab* tab = fixture.open(path);
        if (!tab || !pdftest::AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        return tab;
    }

    /// The stream has been read and the voices are known (the victim may die meanwhile)
    static bool waitStream(ToolPagesLifetimeRun& run)
    {
        Steps::wait(run, [&run]() { pdf::PDFAppToolGUIAudioBook* current = gui(run); return !current || (!current->isBuildingStream() && !current->isProbingVoices() && current->getItemModel() && current->getItemModel()->getItemCount() > 0); });
        Steps::settle();
        return true;
    }

    /// A click on the row of the list
    static bool clickRow(ToolPagesLifetimeRun& run, int row)
    {
        pdf::PDFAppToolGUIAudioBook* current = gui(run);
        QPointer<pdf::PDFListView> view = current ? current->getListView() : nullptr;
        if (!view || !view->model() || view->model()->rowCount() <= row)
        {
            return false;
        }
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(view->model()->index(row, 0)).center());
        Steps::settle();
        return true;
    }

    /// A click on the first item of the overlay
    static bool clickOverlay(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIAudioBook* current = gui(run);
        QPointer<pdf::PDFAppAudioBookOverlay> overlay = current ? current->getOverlay() : nullptr;
        if (!overlay || overlay->getItems().empty())
        {
            return true;
        }
        const QPoint point = overlay->getPageToWidgetTransform().mapRect(overlay->getItems().front().rect).center().toPoint();
        QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, point);
        Steps::settle();
        return true;
    }

    /// A new text of the selected item, committed by its pause
    static bool editText(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIAudioBook* current = gui(run);
        QPointer<pdf::PDFPlainTextEdit> editor = current ? current->getTextEditor() : nullptr;
        if (!editor || !editor->isEnabled())
        {
            return true;
        }
        editor->setPlainText(QStringLiteral("An edited text"));
        Steps::settle();

        // The GUI may be gone after the signals of the text
        current = gui(run);
        if (current)
        {
            current->commitEditedText();
        }
        Steps::settle();
        return true;
    }

    /// Rebuild and the answer Rebuild of its question
    static bool rebuild(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIAudioBook* current = gui(run);
        if (!current || !Steps::click(current->getRebuildLink()))
        {
            return false;
        }
        Steps::wait(run, [&run]() { pdf::PDFAppToolGUIAudioBook* now = gui(run); return !now || (now->getRebuildQuestion() && now->getRebuildQuestion()->isVisible()) || !now->getRebuildQuestion(); });
        current = gui(run);
        if (pdf::PDFMessageBox* question = current ? current->getRebuildQuestion() : nullptr)
        {
            Steps::click(question->findChild<QPushButton*>(QStringLiteral("audioBookRebuildConfirm")));
        }
        Steps::settle();
        return waitStream(run);
    }

    /// The popover of Select by contained text, a text and Select
    static bool selectByText(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIAudioBook* current = gui(run);
        if (!current)
        {
            return false;
        }
        current->openSelectPopover(pdf::PDFAppAudioBookSelectPopover::Kind::ContainedText);
        Steps::settle();
        current = gui(run);
        QPointer<pdf::PDFAppAudioBookSelectPopover> popover = current ? current->getSelectPopover() : nullptr;
        if (!popover)
        {
            return true;
        }
        QTest::keyClicks(popover->getField(), QStringLiteral("paragraph"));
        Steps::settle();
        if (popover)
        {
            Steps::click(popover->getSelectButton());
        }
        Steps::settle();
        return true;
    }

    /// Play Sample, or Stop Sample while it speaks (the engine of the sample tells its state at
    /// once; the engine made by the first click is watched by the recorder from the next step)
    static bool playSample(ToolPagesLifetimeRun& run)
    {
        // The button may lie below the visible part of the right panel: clicked as a button
        pdf::PDFAppToolGUIAudioBook* current = gui(run);
        QPointer<QPushButton> button = current ? current->getPlaySampleButton() : nullptr;
        if (button && button->isEnabled())
        {
            button->click();
        }
        Steps::settle();
        return true;
    }

    /// The Voice menu (341:9657) and a voice of another language
    static bool chooseVoice(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIAudioBook* current = gui(run);
        QPointer<pdf::PDFAppToolPanelPopup> menu = current ? current->getVoiceMenu() : nullptr;
        if (!menu || !Steps::click(menu->getButton()))
        {
            return true;
        }
        Steps::settle();
        if (!menu)
        {
            return true;
        }
        for (const QPointer<pdf::PDFTileRow>& row : menu->getRows())
        {
            if (row && row->objectName() == QStringLiteral("audioBookVoice-Microsoft Jakub"))
            {
                Steps::click(row);
                break;
            }
        }
        Steps::settle();
        return true;
    }

    /// A new Order with changes of the stream: the question of 356:14056 and Rebuild
    static bool changeOrder(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIAudioBook* current = gui(run);
        QPointer<pdf::PDFAppToolEnumMenu> menu = current ? current->getOrderMenu() : nullptr;
        if (!menu || !Steps::click(menu->getButton()))
        {
            return true;
        }
        Steps::settle();
        if (menu)
        {
            Steps::click(menu->getRow(QStringLiteral("content")));
        }
        Steps::wait(run, [&run]() { pdf::PDFAppToolGUIAudioBook* now = gui(run); return !now || now->isBuildingStream() || (now->getRebuildQuestion() && now->getRebuildQuestion()->isVisible()); });
        current = gui(run);
        if (pdf::PDFMessageBox* question = current ? current->getRebuildQuestion() : nullptr)
        {
            Steps::click(question->findChild<QPushButton*>(QStringLiteral("audioBookRebuildConfirm")));
        }
        Steps::settle();
        return waitStream(run);
    }

    /// The context menu of the list (2.7) on a row and its action Skip
    static bool contextMenu(ToolPagesLifetimeRun& run, int row)
    {
        pdf::PDFAppToolGUIAudioBook* current = gui(run);
        QPointer<pdf::PDFListView> view = current ? current->getListView() : nullptr;
        if (!view || !view->model() || view->model()->rowCount() <= row)
        {
            return true;
        }
        const QPoint position = view->visualRect(view->model()->index(row, 0)).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, position, view->viewport()->mapToGlobal(position));
        QApplication::sendEvent(view->viewport(), &event);
        Steps::settle();
        current = gui(run);
        QPointer<QMenu> menu = current ? current->getContextMenu() : nullptr;
        if (!menu)
        {
            return true;
        }
        for (QAction* action : menu->actions())
        {
            if (action->data().toString() == QStringLiteral("skip") && action->isEnabled())
            {
                action->trigger();
                break;
            }
        }
        Steps::settle();
        if (menu)
        {
            menu->close();
        }
        Steps::settle();
        return true;
    }

    /// Select by rectangle (360:13257): a rectangle over the paper
    static bool selectByRectangle(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIAudioBook* current = gui(run);
        if (!current)
        {
            return true;
        }
        current->selectByRectangleMode();
        Steps::settle();
        current = gui(run);
        QPointer<pdf::PDFAppAudioBookOverlay> overlay = current ? current->getOverlay() : nullptr;
        if (!overlay || overlay->getPaperRect().isEmpty())
        {
            return true;
        }
        const QRect paper = overlay->getPaperRect();
        QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, paper.topLeft() + QPoint(1, 1));
        if (overlay)
        {
            QTest::mouseMove(overlay, paper.bottomRight() - QPoint(1, 1));
        }
        if (overlay)
        {
            QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, paper.bottomRight() - QPoint(1, 1));
        }
        Steps::settle();
        return true;
    }

    /// The message of 341:11665 and its Close
    static bool closeNoVoiceMessage(ToolPagesLifetimeRun& run)
    {
        Steps::wait(run, [&run]() { pdf::PDFAppToolGUIAudioBook* now = gui(run); return !now || (now->getNoVoiceMessage() && now->getNoVoiceMessage()->isVisible()); });
        pdf::PDFAppToolGUIAudioBook* current = gui(run);
        if (pdf::PDFMessageBox* message = current ? current->getNoVoiceMessage() : nullptr)
        {
            Steps::click(message->findChild<QPushButton*>(QStringLiteral("audioBookNoVoiceClose")));
        }
        Steps::settle();
        return true;
    }
};

void ToolPagesLifetimePackages::registerAudioBookScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    using H = AudioBookLifetimeHelper;
    using Steps = ToolPagesLifetimeSteps;

    // Create Audio Book: the stream job, a row, the overlay, Skip, an edited text, a move, Select
    // by text, Rebuild with its question, the run of the fake engine to the result dialog
    ToolPagesLifetimeScenario audioBook;
    audioBook.name = QStringLiteral("audiobook");
    audioBook.toolId = QStringLiteral("convert.audiobook");
    audioBook.openDocument = &H::openText;
    audioBook.hasResultDialog = true;
    audioBook.hasPanel = true;
    audioBook.prepareRun = &H::waitStream;
    audioBook.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::waitStream);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::clickRow(run, 1); });
        steps->push_back(&H::clickOverlay);
        steps->push_back([](ToolPagesLifetimeRun& run) { pdf::PDFAppToolGUIAudioBook* current = H::gui(run); return current && Steps::click(current->getSkipButton()); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::clickRow(run, 2); });
        steps->push_back(&H::editText);
        steps->push_back([](ToolPagesLifetimeRun& run) { pdf::PDFAppToolGUIAudioBook* current = H::gui(run); return current && Steps::click(current->getMoveUpButton()); });
        steps->push_back(&H::selectByText);
        steps->push_back(&H::rebuild);
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolResultDialog* dialog = Steps::confirmToDialog(run);
            if (dialog)
            {
                dialog->reject();
            }
            Steps::settle();
            return true;
        });
    };
    scenarios->push_back(audioBook);

    // Create Audio Book, the lifetime gaps of RS4APP: Play Sample, the Voice menu, the context
    // menu of the list, Select by rectangle, a new Order with its question
    ToolPagesLifetimeScenario audioBookMenus;
    audioBookMenus.name = QStringLiteral("audiobookmenus");
    audioBookMenus.toolId = QStringLiteral("convert.audiobook");
    audioBookMenus.openDocument = &H::openText;
    audioBookMenus.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::waitStream);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::clickRow(run, 1); });
        steps->push_back(&H::playSample);
        steps->push_back(&H::playSample);
        steps->push_back(&H::playSample);
        steps->push_back(&H::chooseVoice);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::contextMenu(run, 2); });
        steps->push_back(&H::selectByRectangle);
        steps->push_back(&H::changeOrder);
    };
    scenarios->push_back(audioBookMenus);

    // Create Audio Book without a voice (341:11665): the message and its Close, then the list,
    // its filters and Rebuild still work
    ToolPagesLifetimeScenario audioBookNoVoice;
    audioBookNoVoice.name = QStringLiteral("audiobooknovoice");
    audioBookNoVoice.toolId = QStringLiteral("convert.audiobook");
    audioBookNoVoice.openDocument = &H::openTextWithoutVoice;
    audioBookNoVoice.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back(&H::waitStream);
        steps->push_back(&H::closeNoVoiceMessage);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::clickRow(run, 1); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIAudioBook* current = H::gui(run);
            return current && Steps::click(current->getFilterChip(pdf::PDFAppAudioBookItemModel::Filter::Skipped));
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIAudioBook* current = H::gui(run);
            return current && Steps::click(current->getFilterChip(pdf::PDFAppAudioBookItemModel::Filter::All));
        });
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::clickRow(run, 2); });
        steps->push_back(&H::rebuild);
    };
    scenarios->push_back(audioBookNoVoice);
}
