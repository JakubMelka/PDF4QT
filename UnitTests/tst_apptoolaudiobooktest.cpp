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

// The GUI of Create Audio Book and its overlay (step 4 of phase 2, package P4; plan 8 row GUI, Figma
// 341:9229 ... 341:11665): the frame, the list and its counts, Read / Skip with Undo / Redo, an
// edited text and Restore, a move and the question of Rebuild (356:14056), the checks of the
// popovers of Select (341:10217), the overlay, the state without a voice (341:11665), Play Sample
// with the voice of the tool (R4-14 d), the run of a fake engine and its result dialog (341:11159).
// The engine of the tool is a fake one; nothing is ever played.

#include "apptoolshellfixture.h"

#include "pdfappaudiobookoverlay.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappdocumentspeech.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfapptoolgui_audiobook.h"
#include "pdfapptoolgui_extracttext.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfappworkspace.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_audiobook.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumentwriter.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfwidgetstyle.h"

#include <QCheckBox>
#include <QContextMenuEvent>
#include <QFile>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSemaphore>
#include <QSlider>
#include <QThread>
#include <QToolButton>
#include <QtTest>

#include <atomic>

using pdf::PDFAppToolGUIAudioBook;
using pdf::PDFDocumentToolShell;
using pdftest::AppToolShellFixture;

/// The fake engine of the GUI tests: 16 bit mono, 20 bytes per character; thread-safe (no state)
class GuiFakeSynthesizer : public pdf::PDFSpeechSynthesizer
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

class GuiFakeFactory : public pdf::PDFSpeechSynthesizerFactory
{
public:
    explicit GuiFakeFactory(bool isAvailable) : m_isAvailable(isAvailable) { }

    virtual bool isAvailable() const override { return m_isAvailable; }
    virtual std::vector<pdf::PDFSpeechVoice> getVoices() const override
    {
        if (!m_isAvailable)
        {
            return { };
        }
        return { { QStringLiteral("Microsoft Zira"), QStringLiteral("en-US"), pdf::PDFSpeechVoice::Gender::Female, pdf::PDFSpeechVoice::Age::Adult },
                 { QStringLiteral("Microsoft Jakub"), QStringLiteral("cs-CZ"), pdf::PDFSpeechVoice::Gender::Male, pdf::PDFSpeechVoice::Age::Adult } };
    }
    virtual std::unique_ptr<pdf::PDFSpeechSynthesizer> createSynthesizer(QString*) const override { return std::make_unique<GuiFakeSynthesizer>(); }

private:
    bool m_isAvailable;
};

/// An engine, which speaks "Third paragraph" only after release (at most 30 s): the run stays at
/// its third item meanwhile
class GuiHoldingSynthesizer : public GuiFakeSynthesizer
{
public:
    virtual bool synthesize(const QString& text, pdf::PDFAudioSink* sink, const pdf::PDFOperationControl* control, QString* errorMessage) override
    {
        if (text.contains(QStringLiteral("Third")))
        {
            released.tryAcquire(1, 30000);
        }
        return GuiFakeSynthesizer::synthesize(text, sink, control, errorMessage);
    }

    static inline QSemaphore released{ 0 };
};

class GuiHoldingFactory : public GuiFakeFactory
{
public:
    GuiHoldingFactory() : GuiFakeFactory(true) { }
    virtual std::unique_ptr<pdf::PDFSpeechSynthesizer> createSynthesizer(QString*) const override { return std::make_unique<GuiHoldingSynthesizer>(); }
};

/// An engine of the system, which starts slowly (winrt: about 5 s the first time): its voices
/// come only after release (at most 30 s, so a mutation, which waits for it in the GUI thread,
/// ends); counts the threads, which asked for them
class GuiSlowFactory : public GuiFakeFactory
{
public:
    GuiSlowFactory() : GuiFakeFactory(true) { }

    virtual std::vector<pdf::PDFSpeechVoice> getVoices() const override
    {
        if (QThread::currentThread() == QCoreApplication::instance()->thread())
        {
            isAskedInGuiThread = true;
        }
        released.tryAcquire(1, 30000);
        released.release();
        return GuiFakeFactory::getVoices();
    }

    void release() { released.release(); }

    mutable QSemaphore released;
    mutable std::atomic_bool isAskedInGuiThread = false;
};

/// The speech of Play Sample: it records the text, the voice, the speed and the volume
class GuiFakeSpeechEngine : public pdf::PDFAppSpeechEngine
{
public:
    explicit GuiFakeSpeechEngine(QObject* parent) : pdf::PDFAppSpeechEngine(parent) { s_last = this; }
    virtual ~GuiFakeSpeechEngine() override { if (s_last == this) { s_last = nullptr; } }

    static pdf::PDFAppSpeechEngine* create(const QString&, QObject* parent) { return new GuiFakeSpeechEngine(parent); }

    virtual State getState() const override { return state; }
    virtual QString getErrorString() const override { return QString(); }
    virtual QList<QLocale> getLocales() const override { return { QLocale(QLocale::English, QLocale::UnitedStates) }; }
    virtual QLocale getLocale() const override { return QLocale(QLocale::English, QLocale::UnitedStates); }
    virtual void setLocale(const QLocale&) override { }
    virtual QList<pdf::PDFAppSpeechVoice> getVoices() const override { return { }; }
    virtual QString getVoice() const override { return voice; }
    virtual void setVoice(const QString& name) override { voice = name; }
    virtual void setRate(double value) override { rate = value; }
    virtual void setPitch(double) override { }
    virtual void setVolume(double value) override { volume = value; }
    virtual void say(const QString& text) override { said << text; state = State::Speaking; Q_EMIT stateChanged(); }
    virtual void pause() override { }
    virtual void resume() override { }
    virtual void stop() override { state = State::Ready; Q_EMIT stateChanged(); }

    static inline GuiFakeSpeechEngine* s_last = nullptr;
    State state = State::Ready;
    QStringList said;
    QString voice;
    double rate = 0.0;
    double volume = 1.0;
};

/// Documents and parts of the tool Create Audio Book
class AudioBookGuiHelper
{
public:
    AudioBookGuiHelper() = delete;

    static inline const QString TOOL_ID = QStringLiteral("convert.audiobook");

    /// Pages, whose texts are drawn far apart (each one an item of the layout)
    static QString writeDocument(const QString& filePath, const std::vector<QStringList>& pages)
    {
        pdf::PDFDocumentBuilder builder;
        for (const QStringList& texts : pages)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 595, 842));
            if (texts.isEmpty())
            {
                continue;
            }
            pdf::PDFPageContentStreamBuilder content(&builder);
            QPainter* painter = content.begin(page);
            QFont font(QStringLiteral("Inter"));
            font.setPixelSize(14);
            painter->setFont(font);
            for (int i = 0; i < texts.size(); ++i)
            {
                painter->drawText(QPointF(60, 100 + 250 * i), texts[i]);
            }
            content.end(painter);
        }
        pdf::PDFDocument document = builder.build();
        pdf::PDFDocumentWriter writer(nullptr);
        writer.write(filePath, &document, false);
        return filePath;
    }

    static pdf::PDFAppDocumentTab* openReady(pdftest::AppDocumentFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        return tab;
    }

    static PDFAppToolGUIAudioBook* gui(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<PDFAppToolGUIAudioBook*>(shell->getGUI()) : nullptr;
    }

    static bool waitStream(PDFAppToolGUIAudioBook* audio)
    {
        return AppToolShellFixture::waitFor([audio]() { return audio && !audio->isBuildingStream() && audio->getItemModel()->getItemCount() > 0; });
    }

    /// Opens the tool on a document of two pages: "First paragraph", "Second paragraph" on page 1,
    /// "Third paragraph" on page 2 (items: Page 1, two texts, Page 2, one text)
    /// \p hasVoices: the fake engine of the context has two voices, or none can synthesize
    /// (341:11665); never the voices of the system
    static PDFAppToolGUIAudioBook* openTool(AppToolShellFixture& fixture, PDFDocumentToolShell** shellOut = nullptr, bool hasVoices = true)
    {
        // The one factory of the application: the GUI reads its voices, the runs synthesize by it
        fixture.context->setSpeechSynthesizerFactory(std::make_shared<GuiFakeFactory>(hasVoices));
        pdf::PDFAppDocumentTab* tab = openReady(fixture, writeDocument(fixture.path(QStringLiteral("Project_Report.pdf")), { { QStringLiteral("First paragraph"), QStringLiteral("Second paragraph") }, { QStringLiteral("Third paragraph") } }));
        PDFDocumentToolShell* shell = tab ? fixture.openTool(tab, TOOL_ID) : nullptr;
        if (shellOut)
        {
            *shellOut = shell;
        }
        PDFAppToolGUIAudioBook* audio = gui(shell);
        return waitStream(audio) && AppToolShellFixture::waitFor([audio]() { return audio && !audio->isProbingVoices(); }) ? audio : nullptr;
    }

    /// The index of the item of the text
    static qint64 findItem(const PDFAppToolGUIAudioBook* audio, const QString& text)
    {
        const pdf::PDFDocumentTextFlowEditor& editor = audio->getItemModel()->getEditor();
        for (size_t i = 0; i < editor.getItemCount(); ++i)
        {
            if (editor.getText(i).simplified() == text)
            {
                return qint64(i);
            }
        }
        return -1;
    }

    static QString rowValue(const pdf::PDFKeyValueList* list, const QString& key)
    {
        for (int i = 0; list && i < list->getRowCount(); ++i)
        {
            if (list->getKey(i) == key)
            {
                return list->getValue(i);
            }
        }
        return QString();
    }

    static QToolButton* findButton(PDFDocumentToolShell* shell, const QString& textStart)
    {
        for (QToolButton* button : shell->getToolActions()->findChildren<QToolButton*>())
        {
            if (!textStart.isEmpty() && button->text().startsWith(textStart))
            {
                return button;
            }
        }
        return nullptr;
    }
};

class AppToolAudioBookTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void registered();
    void frameAndCounts();
    void skipReadUndoRedo();
    void editedTextAndRestore();
    void moveAndRebuildQuestion();
    void selectPopoverChecks();
    void overlayItemsAndClick();
    void noVoice();
    void voicesInAJob();
    void playSampleUsesTheTool();
    void runAndSave();
    void sampleVoiceOfAnotherLanguage();
    void tenThousandItems();
    void catastrophicExpressionStaysQuick();
    void listRowsStartAtTheHeader();
    void contextMenuOfTheList();
    void contextMenuActionMayCloseTheTab();
    void runningCardShowsThePage();
    void newValueAtTheAddressOfAFreedOne();

    // Review RSRFUI
    void escapeCancelsRectangleFirst();
};

void AppToolAudioBookTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
    pdf::PDFAppSpeechEngine::setFactory(&GuiFakeSpeechEngine::create);
}

void AppToolAudioBookTest::cleanup()
{
    // Every fixture has its own context: its factory is given by openTool
}

void AppToolAudioBookTest::registered()
{
    // The context registers the tool with its GUI once (PDFAppBuiltinTools::registerAll)
    AppToolShellFixture fixture;
    QVERIFY(pdf::PDFDocumentToolRegistry::getImplementedTools().contains(AudioBookGuiHelper::TOOL_ID));
    const auto tool = pdf::PDFDocumentToolRegistry::getTool(AudioBookGuiHelper::TOOL_ID);
    QVERIFY(std::dynamic_pointer_cast<const pdf::PDFDocumentToolAudioBook>(tool));
    PDFAppToolGUIAudioBook::registerAudioBookTools();
    QCOMPARE(pdf::PDFDocumentToolRegistry::getTool(AudioBookGuiHelper::TOOL_ID), tool);
}

void AppToolAudioBookTest::frameAndCounts()
{
    // 341:9229: the toolbar, the Text stream with its counts, the canvas, the right panel
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = nullptr;
    PDFAppToolGUIAudioBook* audio = H::openTool(fixture, &shell);
    QVERIFY(audio);
    AppToolShellFixture::settle();
    fixture.screenshot(QStringLiteral("audiobook-341-9229"));

    // tool-actions 341:9249: Select, Sync, Order: Auto, Voice: <the first voice of the UI language or the first voice>
    QVERIFY(H::findButton(shell, QStringLiteral("Select")));
    QVERIFY(H::findButton(shell, QStringLiteral("Sync")));
    QVERIFY(H::findButton(shell, QStringLiteral("Order: Auto")));
    // 341:9229 "Voice: Zira": the short name of the voice "Microsoft Zira" (RS4FIGMA)
    const QString voice = shell->getModel()->getValue(QStringLiteral("voice")).toString();
    QVERIFY2(voice.startsWith(QStringLiteral("Microsoft ")), qPrintable(voice));
    QVERIFY(H::findButton(shell, QStringLiteral("Voice: ") + voice.mid(10)));
    QVERIFY(!H::findButton(shell, QStringLiteral("Voice: Microsoft")));
    QVERIFY(audio->getReadButton() && audio->getSkipButton() && audio->getMoveUpButton() && audio->getMoveDownButton());
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Create Audio Book"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // The stream of the layout: Page 1, First, Second, Page 2, Third - the page numbers are skipped
    // by default (R4-15), the three texts are read (341:9371 "5 items · 3 will be read")
    QCOMPARE(audio->getItemModel()->getItemCount(), size_t(5));
    QCOMPARE(audio->getItemModel()->getReadCount(), size_t(3));
    QCOMPARE(audio->getStreamTitle()->text(), QStringLiteral("Text stream"));
    QCOMPARE(audio->getStreamCount()->text(), QStringLiteral("5 items · 3 will be read"));
    QCOMPARE(audio->getRebuildLink()->text(), QStringLiteral("Rebuild"));
    QVERIFY(audio->getFilterChip(pdf::PDFAppAudioBookItemModel::Filter::All)->isChecked());
    QCOMPARE(audio->getFilterChip(pdf::PDFAppAudioBookItemModel::Filter::Skipped)->text(), QStringLiteral("Skipped"));
    QVERIFY(!audio->getEmptyLabel()->isVisible());

    // A row: "Page 1", the tag "Special", the state "Skipped" (341:9410)
    const QModelIndex pageRow = audio->getItemModel()->index(0, 0);
    QCOMPARE(pageRow.data(pdf::PDFAppAudioBookItemModel::PageTextRole).toString(), QStringLiteral("Page 1"));
    QCOMPARE(pageRow.data(pdf::PDFAppAudioBookItemModel::TypeTextRole).toString(), QStringLiteral("Special"));
    QCOMPARE(pageRow.data(pdf::PDFAppAudioBookItemModel::StateTextRole).toString(), QStringLiteral("Skipped"));
    const QModelIndex textRow = audio->getItemModel()->index(int(H::findItem(audio, QStringLiteral("First paragraph"))), 0);
    QCOMPARE(textRow.data(pdf::PDFAppAudioBookItemModel::TypeTextRole).toString(), QStringLiteral("Text"));
    QCOMPARE(textRow.data(pdf::PDFAppAudioBookItemModel::StateTextRole).toString(), QStringLiteral("Read"));

    // document-canvas 341:9262: the title of the page and the legend
    QVERIFY(audio->getCanvas()->isVisible());
    QCOMPARE(audio->getCanvas()->getTitleLabel()->text(), QStringLiteral("Page 1 · items of the text stream"));
    QCOMPARE(audio->getCanvas()->getZoomControl()->size(), QSize(120, 32));

    // section-Summary 341:9650: Items read, Estimated length, Estimated size
    QCOMPARE(H::rowValue(audio->getSummaryRows(), QStringLiteral("Items read")), QStringLiteral("3 of 5"));
    QVERIFY(H::rowValue(audio->getSummaryRows(), QStringLiteral("Estimated length")).startsWith(QStringLiteral("about ")));
    QVERIFY(H::rowValue(audio->getSummaryRows(), QStringLiteral("Estimated size")).startsWith(QStringLiteral("about ")));

    // section-Voice 341:9591: Speed · normal, Volume · 100 %, WAV (owner 14. 11.)
    QCOMPARE(audio->getSpeedLabel()->text(), QStringLiteral("Speed · normal"));
    QCOMPARE(audio->getVolumeLabel()->text(), QStringLiteral("Volume · 100 %"));
    QCOMPARE(audio->getFormatCombo()->currentText(), QStringLiteral("WAV"));
    QCOMPARE(audio->getPlaySampleButton()->text(), QStringLiteral("Play Sample"));

    // Nothing is selected: the panel of the item waits
    QVERIFY(!audio->getTextEditor()->isEnabled());
    QVERIFY(!audio->getReadCheckBox()->isEnabled());
    QVERIFY(!audio->getSkipButton()->isEnabled());

    // The status bar 341:9264
    QVERIFY2(shell->getStatusMessage().startsWith(QStringLiteral("Create Audio Book · Project_Report.pdf · 3 of 5 items will be read · about ")), qPrintable(shell->getStatusMessage()));
}

void AppToolAudioBookTest::skipReadUndoRedo()
{
    // Delete skips the selected items, Shift+Delete reads them again (341:9473); each edit is one
    // step of Undo, Undo and Redo give the stream back
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = nullptr;
    PDFAppToolGUIAudioBook* audio = H::openTool(fixture, &shell);
    QVERIFY(audio);
    pdf::PDFAppToolParameterModel* model = shell->getModel();
    QVERIFY(!model->canUndo());

    const qint64 second = H::findItem(audio, QStringLiteral("Second paragraph"));
    QVERIFY(second >= 0);
    audio->selectItems({ size_t(second) });
    QVERIFY(audio->getSkipButton()->isEnabled());
    QVERIFY(audio->getReadCheckBox()->isChecked());

    QTest::keyClick(audio->getListView(), Qt::Key_Delete);
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->getReadCount(), size_t(2));
    QCOMPARE(audio->getStreamCount()->text(), QStringLiteral("5 items · 2 will be read"));
    QVERIFY(!audio->getReadCheckBox()->isChecked());
    QVERIFY(model->canUndo());

    // The value of the run has the edited stream: the skipped item is not in it
    const pdf::PDFDocumentToolAudioBookStream stream = model->getValue(QStringLiteral("editedStream")).value<pdf::PDFDocumentToolAudioBookStream>();
    QVERIFY(stream.getEditor());
    QCOMPARE(stream.getItemCount(), qint64(5));
    const pdf::PDFDocumentTextFlow flow = stream.createFlow();
    for (const pdf::PDFDocumentTextFlow::Item& item : flow.getItems())
    {
        QVERIFY(!item.text.contains(QStringLiteral("Second")));
    }

    // The filter Skipped: the two page numbers and the skipped text
    QTest::mouseClick(audio->getFilterChip(pdf::PDFAppAudioBookItemModel::Filter::Skipped), Qt::LeftButton);
    QCOMPARE(audio->getItemModel()->rowCount(QModelIndex()), 3);
    QTest::mouseClick(audio->getFilterChip(pdf::PDFAppAudioBookItemModel::Filter::All), Qt::LeftButton);
    QCOMPARE(audio->getItemModel()->rowCount(QModelIndex()), 5);

    model->undo();
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->getReadCount(), size_t(3));
    model->redo();
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->getReadCount(), size_t(2));

    // Shift+Delete reads a page number (a special item) too
    audio->selectItems({ size_t(0), size_t(second) });
    QTest::keyClick(audio->getListView(), Qt::Key_Delete, Qt::ShiftModifier);
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->getReadCount(), size_t(4));
}

void AppToolAudioBookTest::editedTextAndRestore()
{
    // Selected item 341:9572: the edited text is read, the item is Modified (orange), Restore gives
    // the original text back
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = nullptr;
    PDFAppToolGUIAudioBook* audio = H::openTool(fixture, &shell);
    QVERIFY(audio);

    const qint64 first = H::findItem(audio, QStringLiteral("First paragraph"));
    audio->selectItems({ size_t(first) });
    QCOMPARE(audio->getTextEditor()->toPlainText().simplified(), QStringLiteral("First paragraph"));
    QCOMPARE(audio->getSelectedTag()->text(), QStringLiteral("Text · page 1"));
    QVERIFY(!audio->getRestoreLink()->isEnabled());

    audio->getTextEditor()->setPlainText(QStringLiteral("The first paragraph, read better"));
    audio->commitEditedText();
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->getItemState(size_t(first)), pdf::PDFAppAudioBookOverlay::State::Modified);
    QCOMPARE(audio->getItemModel()->index(int(first), 0).data(pdf::PDFAppAudioBookItemModel::StateTextRole).toString(), QStringLiteral("Modified"));
    QVERIFY(audio->getRestoreLink()->isEnabled());
    QTest::mouseClick(audio->getFilterChip(pdf::PDFAppAudioBookItemModel::Filter::Modified), Qt::LeftButton);
    QCOMPARE(audio->getItemModel()->rowCount(QModelIndex()), 1);
    QTest::mouseClick(audio->getFilterChip(pdf::PDFAppAudioBookItemModel::Filter::All), Qt::LeftButton);

    // The stream of the run reads the edited text
    const pdf::PDFDocumentToolAudioBookStream stream = shell->getModel()->getValue(QStringLiteral("editedStream")).value<pdf::PDFDocumentToolAudioBookStream>();
    bool hasEdited = false;
    const pdf::PDFDocumentTextFlow flow = stream.createFlow();
    for (const pdf::PDFDocumentTextFlow::Item& item : flow.getItems())
    {
        hasEdited = hasEdited || item.text == QStringLiteral("The first paragraph, read better");
    }
    QVERIFY(hasEdited);

    audio->selectItems({ size_t(first) });
    QTest::mouseClick(audio->getRestoreLink(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->getItemState(size_t(first)), pdf::PDFAppAudioBookOverlay::State::Read);
    QCOMPARE(audio->getItemModel()->getEditor().getText(size_t(first)).simplified(), QStringLiteral("First paragraph"));

    // A text typed just now (its pause has not come, it is not a step of Undo yet) is read by a run
    // started at once: the parameters of the run have it, the value of the model not yet
    audio->getTextEditor()->setPlainText(QStringLiteral("Typed just before the run"));
    const pdf::PDFDocumentToolAudioBookStream storedStream = shell->getModel()->getValue(QStringLiteral("editedStream")).value<pdf::PDFDocumentToolAudioBookStream>();
    bool isStored = false;
    const pdf::PDFDocumentTextFlow storedFlow = storedStream.createFlow();
    for (const pdf::PDFDocumentTextFlow::Item& item : storedFlow.getItems())
    {
        isStored = isStored || item.text == QStringLiteral("Typed just before the run");
    }
    QVERIFY(!isStored);
    const pdf::PDFDocumentToolAudioBookStream runStream = audio->getRunParameters().value(QStringLiteral("editedStream")).value<pdf::PDFDocumentToolAudioBookStream>();
    QVERIFY(!runStream.isEmpty());
    bool isRead = false;
    const pdf::PDFDocumentTextFlow runFlow = runStream.createFlow();
    for (const pdf::PDFDocumentTextFlow::Item& item : runFlow.getItems())
    {
        isRead = isRead || item.text == QStringLiteral("Typed just before the run");
    }
    QVERIFY(isRead);
}

void AppToolAudioBookTest::moveAndRebuildQuestion()
{
    // Alt+Up moves the item; Rebuild asks (356:14056) with the changes, Cancel keeps them,
    // Rebuild reads the stream again (and is one step of Undo)
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = nullptr;
    PDFAppToolGUIAudioBook* audio = H::openTool(fixture, &shell);
    QVERIFY(audio);

    const qint64 second = H::findItem(audio, QStringLiteral("Second paragraph"));
    audio->selectItems({ size_t(second) });
    QTest::keyClick(audio->getListView(), Qt::Key_Up, Qt::AltModifier);
    AppToolShellFixture::settle();
    QCOMPARE(H::findItem(audio, QStringLiteral("Second paragraph")), second - 1);
    QCOMPARE(audio->getItemModel()->getSelection(), std::set<size_t>({ size_t(second - 1) }));

    QTest::mouseClick(audio->getRebuildLink(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([audio]() { return audio->getRebuildQuestion() && audio->getRebuildQuestion()->isVisible(); }));
    AppToolShellFixture::settle();
    fixture.screenshot(QStringLiteral("audiobook-356-14056"));
    pdf::PDFMessageBox* question = audio->getRebuildQuestion();
    QCOMPARE(question->findChild<QPushButton*>(QStringLiteral("audioBookRebuildConfirm"))->text(), QStringLiteral("Rebuild"));
    const QList<pdf::PDFAppParagraph*> texts = question->findChildren<pdf::PDFAppParagraph*>();
    bool hasChanges = false;
    for (const pdf::PDFAppParagraph* text : texts)
    {
        hasChanges = hasChanges || text->text() == QStringLiteral("Your changes will be lost: the changed order. The same question appears when you change Order.");
    }
    QVERIFY(hasChanges);

    QTest::mouseClick(question->findChild<QPushButton*>(QStringLiteral("audioBookRebuildCancel")), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([audio]() { return !audio->getRebuildQuestion(); }));
    QCOMPARE(H::findItem(audio, QStringLiteral("Second paragraph")), second - 1);

    QTest::mouseClick(audio->getRebuildLink(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([audio]() { return audio->getRebuildQuestion() && audio->getRebuildQuestion()->isVisible(); }));
    QTest::mouseClick(audio->getRebuildQuestion()->findChild<QPushButton*>(QStringLiteral("audioBookRebuildConfirm")), Qt::LeftButton);
    QVERIFY(H::waitStream(audio));
    QVERIFY(AppToolShellFixture::waitFor([audio, second]() { return H::findItem(audio, QStringLiteral("Second paragraph")) == second; }));

    // Undo of Rebuild gives the moved stream back
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(H::findItem(audio, QStringLiteral("Second paragraph")), second - 1);
}

void AppToolAudioBookTest::selectPopoverChecks()
{
    // 341:10217: the line of the check of each popover, valid and invalid
    using H = AudioBookGuiHelper;
    using Kind = pdf::PDFAppAudioBookSelectPopover::Kind;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFAppToolGUIAudioBook* audio = H::openTool(fixture);
    QVERIFY(audio);

    PDFAppToolGUIAudioBook::SelectionCheck check = audio->checkSelection(Kind::RegularExpression, QStringLiteral("^(First|Third)"));
    QVERIFY(check.isValid);
    QCOMPARE(check.text, QStringLiteral("2 items match · they replace the current selection"));
    QCOMPARE(int(check.items.size()), 2);

    check = audio->checkSelection(Kind::RegularExpression, QStringLiteral("^(Page|Figure \\d+"));
    QVERIFY(!check.isValid);
    QVERIFY2(check.text.startsWith(QStringLiteral("Regular expression is not valid: missing closing parenthesis")), qPrintable(check.text));

    check = audio->checkSelection(Kind::ContainedText, QStringLiteral("paragraph"));
    QVERIFY(check.isValid);
    QCOMPARE(check.text, QStringLiteral("3 items contain the text · upper and lower case matter"));
    QCOMPARE(audio->checkSelection(Kind::ContainedText, QStringLiteral("PARAGRAPH")).items.size(), size_t(0));
    check = audio->checkSelection(Kind::ContainedText, QString());
    QVERIFY(!check.isValid);
    QCOMPARE(check.text, QStringLiteral("Type the text to look for."));

    check = audio->checkSelection(Kind::PageList, QStringLiteral("2"));
    QVERIFY(check.isValid);
    QCOMPARE(check.text, QStringLiteral("1 page · 2 items"));
    check = audio->checkSelection(Kind::PageList, QStringLiteral("1-2, 17"));
    QVERIFY(!check.isValid);
    QVERIFY2(check.text.contains(QStringLiteral("17")), qPrintable(check.text));

    // The popover itself: the field, the check and Select
    audio->openSelectPopover(Kind::ContainedText);
    QVERIFY(AppToolShellFixture::waitFor([audio]() { return audio->getSelectPopover() && audio->getSelectPopover()->isVisible(); }));
    pdf::PDFAppAudioBookSelectPopover* popover = audio->getSelectPopover();
    QVERIFY(!popover->getSelectButton()->isEnabled());
    QCOMPARE(popover->getCheckLabel()->text(), QStringLiteral("Type the text to look for."));
    QTest::keyClicks(popover->getField(), QStringLiteral("Third"));
    QVERIFY(popover->getSelectButton()->isEnabled());
    QTest::mouseClick(popover->getSelectButton(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->getSelection(), std::set<size_t>({ size_t(H::findItem(audio, QStringLiteral("Third paragraph"))) }));

    // List to page: the canvas shows the page of the selected item
    QCOMPARE(audio->getShownPageIndex(), pdf::PDFInteger(1));
}

void AppToolAudioBookTest::overlayItemsAndClick()
{
    // The overlay of the page (341:9229): one rectangle per item with a rectangle, in the color of
    // its state; a click selects, Ctrl adds; the rectangle mode selects the items inside
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFAppToolGUIAudioBook* audio = H::openTool(fixture);
    QVERIFY(audio);
    pdf::PDFAppAudioBookOverlay* overlay = audio->getOverlay();
    QVERIFY(overlay);
    QVERIFY(AppToolShellFixture::waitFor([overlay]() { return !overlay->getPaperRect().isEmpty(); }));
    QCOMPARE(int(overlay->getItems().size()), 2);
    for (const pdf::PDFAppAudioBookOverlay::Item& item : overlay->getItems())
    {
        QCOMPARE(item.state, pdf::PDFAppAudioBookOverlay::State::Read);
    }

    const pdf::PDFAppAudioBookOverlay::Item first = overlay->getItems().front();
    const QPoint point = overlay->getPageToWidgetTransform().mapRect(first.rect).center().toPoint();
    QCOMPARE(overlay->getItemAt(point), qint64(first.index));
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, point);
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->getSelection(), std::set<size_t>({ first.index }));
    bool isSelectedShown = false;
    for (const pdf::PDFAppAudioBookOverlay::Item& item : overlay->getItems())
    {
        isSelectedShown = isSelectedShown || (item.index == first.index && item.state == pdf::PDFAppAudioBookOverlay::State::Selected);
    }
    QVERIFY(isSelectedShown);

    // A click on the paper outside of the items selects nothing
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, overlay->getPaperRect().bottomRight() - QPoint(2, 2));
    AppToolShellFixture::settle();
    QVERIFY(audio->getItemModel()->getSelection().empty());

    // 360:13257: the rectangle over the whole paper selects both items; the hint is shown meanwhile
    audio->selectByRectangleMode();
    QVERIFY(overlay->isRectangleMode());
    QVERIFY(audio->getCanvas()->getRectangleHint()->isVisible());
    const QRect paper = overlay->getPaperRect();
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, paper.topLeft() + QPoint(1, 1));
    QTest::mouseMove(overlay, paper.bottomRight() - QPoint(1, 1));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, paper.bottomRight() - QPoint(1, 1));
    AppToolShellFixture::settle();
    QVERIFY(!overlay->isRectangleMode());
    QVERIFY(!audio->getCanvas()->getRectangleHint()->isVisible());
    QCOMPARE(int(audio->getItemModel()->getSelection().size()), 2);

    // Esc cancels the mode (owner 2026-11-17: the shortcut of the shell asks the GUI first, the tool
    // stays open); the next Esc leaves the tool
    audio->selectByRectangleMode();
    QVERIFY(QTest::qWaitForWindowActive(overlay->window()));
    overlay->setFocus();
    QTest::keyClick(overlay, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!overlay->isRectangleMode());
    QVERIFY(!audio->getCanvas()->getRectangleHint()->isVisible());
    QCOMPARE(int(audio->getItemModel()->getSelection().size()), 2);
    QVERIFY(audio->getShell() && audio->getShell()->getState() == PDFDocumentToolShell::State::Editing);
    QVERIFY(!audio->handleEscape());
}

void AppToolAudioBookTest::escapeCancelsRectangleFirst()
{
    // Owner 2026-11-17: one Esc ends only the first of a drag, an input mode, the tool. A rectangle
    // being drawn (360:13257) is dropped by the first Esc - the mode stays with its hint, the release
    // selects nothing; the second Esc ends the mode, the tool stays open
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFAppToolGUIAudioBook* audio = H::openTool(fixture);
    QVERIFY(audio);
    pdf::PDFAppAudioBookOverlay* overlay = audio->getOverlay();
    QVERIFY(overlay);
    QVERIFY(AppToolShellFixture::waitFor([overlay]() { return !overlay->getPaperRect().isEmpty(); }));

    audio->selectByRectangleMode();
    QVERIFY(QTest::qWaitForWindowActive(overlay->window()));
    overlay->setFocus();
    const QRect paper = overlay->getPaperRect();
    const QPoint end = paper.bottomRight() - QPoint(1, 1);
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, paper.topLeft() + QPoint(1, 1));
    QMouseEvent move(QEvent::MouseMove, QPointF(end), overlay->mapToGlobal(QPointF(end)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(overlay, &move);
    QVERIFY(overlay->isToolDragActive());

    QTest::keyClick(overlay, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!overlay->isToolDragActive());
    QVERIFY(overlay->isRectangleMode());
    QVERIFY(audio->getCanvas()->getRectangleHint()->isVisible());
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, end);
    AppToolShellFixture::settle();
    QVERIFY(overlay->isRectangleMode());
    QVERIFY(audio->getItemModel()->getSelection().empty());

    QTest::keyClick(overlay, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!overlay->isRectangleMode());
    QVERIFY(!audio->getCanvas()->getRectangleHint()->isVisible());
    QVERIFY(audio->getItemModel()->getSelection().empty());
    QVERIFY(audio->getShell() && audio->getShell()->getState() == PDFDocumentToolShell::State::Editing);
}

void AppToolAudioBookTest::noVoice()
{
    // 341:11665: no engine, which can synthesize - the message, Voice: none, the Primary button
    // disabled; Open Speech Settings only on Windows
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = nullptr;
    PDFAppToolGUIAudioBook* audio = H::openTool(fixture, &shell, false);
    QVERIFY(audio);
    QVERIFY(AppToolShellFixture::waitFor([audio]() { return audio->getNoVoiceMessage() && audio->getNoVoiceMessage()->isVisible(); }));
    AppToolShellFixture::settle();
    fixture.screenshot(QStringLiteral("audiobook-341-11665"));

    pdf::PDFMessageBox* message = audio->getNoVoiceMessage();
    QVERIFY(message->findChild<QPushButton*>(QStringLiteral("audioBookNoVoiceClose")));
#if defined(Q_OS_WIN)
    QVERIFY(message->findChild<QPushButton*>(QStringLiteral("audioBookOpenSpeechSettings")));
#else
    QVERIFY(!message->findChild<QPushButton*>(QStringLiteral("audioBookOpenSpeechSettings")));
#endif
    QVERIFY(H::findButton(shell, QStringLiteral("Voice: none")));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(!audio->getPlaySampleButton()->isEnabled());
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Create Audio Book · no text-to-speech voice is available"));

    QTest::mouseClick(message->findChild<QPushButton*>(QStringLiteral("audioBookNoVoiceClose")), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([audio]() { return !audio->getNoVoiceMessage(); }));
}

void AppToolAudioBookTest::voicesInAJob()
{
    // P4 open point 5: the engine of the system starts in seconds the first time (winrt about
    // 5 s, measured 2026-11-14), so the voices are looked for in one job, never in the GUI thread:
    // the tool opens at once, says nothing about "no voice" meanwhile (341:11665 is for a system
    // without voices), keeps Create Audio Book disabled and gets the voices at the end of the job
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    const std::shared_ptr<GuiSlowFactory> factory = std::make_shared<GuiSlowFactory>();
    fixture.context->setSpeechSynthesizerFactory(factory);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Project_Report.pdf")), { { QStringLiteral("First paragraph") } }));
    QVERIFY(tab);
    QPointer<PDFDocumentToolShell> shell = fixture.openTool(tab, H::TOOL_ID);
    QPointer<PDFAppToolGUIAudioBook> audio = H::gui(shell);
    QVERIFY(audio);
    QVERIFY(H::waitStream(audio));
    QVERIFY(audio->isProbingVoices());
    QVERIFY(!factory->isAskedInGuiThread);
    QVERIFY(!audio->getNoVoiceMessage());
    QVERIFY(shell->getStatusMessage() != QStringLiteral("Create Audio Book · no text-to-speech voice is available"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());

    factory->release();
    QVERIFY(AppToolShellFixture::waitFor([audio]() { return audio && !audio->isProbingVoices(); }));
    AppToolShellFixture::settle();
    QCOMPARE(audio->getVoices().size(), size_t(2));
    const QString voice = shell->getModel()->getValue(QStringLiteral("voice")).toString();
    QVERIFY2(voice == QStringLiteral("Microsoft Zira") || voice == QStringLiteral("Microsoft Jakub"), qPrintable(voice));
    QVERIFY(H::findButton(shell, QStringLiteral("Voice: ") + voice.mid(QStringLiteral("Microsoft ").size())));
    QVERIFY(!audio->getNoVoiceMessage());
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(!factory->isAskedInGuiThread);

    // The tool closed during the job: the job ends alone (its own copy of the factory), its result
    // is dropped, nothing of the closed tool runs
    const std::shared_ptr<GuiSlowFactory> slow = std::make_shared<GuiSlowFactory>();
    fixture.context->setSpeechSynthesizerFactory(slow);
    shell->close();
    AppToolShellFixture::settle();
    shell = fixture.openTool(tab, H::TOOL_ID);
    audio = H::gui(shell);
    QVERIFY(audio);
    QVERIFY(AppToolShellFixture::waitFor([audio]() { return audio && audio->isProbingVoices(); }));
    shell->close();
    AppToolShellFixture::settle();
    delete audio.data();
    QVERIFY(!audio);
    slow->release();
    QTest::qWait(100);
    AppToolShellFixture::settle();
    QVERIFY(fixture.window->isVisible());
}

void AppToolAudioBookTest::playSampleUsesTheTool()
{
    // R4-14 d: Play Sample says the selected item with the voice, the speed and the volume of the
    // tool (never the settings speech.* of Read aloud); Stop Sample while it speaks
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = nullptr;
    PDFAppToolGUIAudioBook* audio = H::openTool(fixture, &shell);
    QVERIFY(audio);

    shell->getModel()->setValue(QStringLiteral("voice"), QStringLiteral("Microsoft Jakub"));
    shell->getModel()->setValue(QStringLiteral("rate"), 0.5);
    shell->getModel()->setValue(QStringLiteral("volume"), 0.25);
    AppToolShellFixture::settle();
    QCOMPARE(audio->getVolumeLabel()->text(), QStringLiteral("Volume · 25 %"));
    QCOMPARE(audio->getSpeedLabel()->text(), QStringLiteral("Speed · 141 %"));
    QVERIFY(H::findButton(shell, QStringLiteral("Voice: Jakub")));

    audio->selectItems({ size_t(H::findItem(audio, QStringLiteral("Second paragraph"))) });
    QTest::mouseClick(audio->getPlaySampleButton(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(GuiFakeSpeechEngine::s_last);
    QCOMPARE(GuiFakeSpeechEngine::s_last->said.size(), qsizetype(1));
    QCOMPARE(GuiFakeSpeechEngine::s_last->said.front().simplified(), QStringLiteral("Second paragraph"));
    QCOMPARE(GuiFakeSpeechEngine::s_last->voice, QStringLiteral("Microsoft Jakub"));
    QCOMPARE(GuiFakeSpeechEngine::s_last->rate, 0.5);
    QCOMPARE(GuiFakeSpeechEngine::s_last->volume, 0.25);
    QCOMPARE(audio->getPlaySampleButton()->text(), QStringLiteral("Stop Sample"));

    QTest::mouseClick(audio->getPlaySampleButton(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(GuiFakeSpeechEngine::s_last->state, pdf::PDFAppSpeechEngine::State::Ready);
    QCOMPARE(audio->getPlaySampleButton()->text(), QStringLiteral("Play Sample"));
}

void AppToolAudioBookTest::runAndSave()
{
    // 341:11159: the run of the fake engine, Save as a file of a WAV file, the facts; the document
    // is not changed and the temporary file goes into the target
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = nullptr;
    PDFAppToolGUIAudioBook* audio = H::openTool(fixture, &shell);
    QVERIFY(audio);
    audio->selectItems({ size_t(H::findItem(audio, QStringLiteral("Second paragraph"))) });
    audio->skipSelected();

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() && shell->getResultDialog()->isVisible(); }));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    AppToolShellFixture::settle();
    fixture.screenshot(QStringLiteral("audiobook-341-11159"));
    QCOMPARE(dialog->getResultKind(), pdf::PDFAppToolResultDialog::ResultKind::Files);
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Save Audio Book"));
    QCOMPARE(H::rowValue(dialog->getFacts(), QStringLiteral("Items read")), QStringLiteral("2 of 5"));
    QCOMPARE(H::rowValue(dialog->getFacts(), QStringLiteral("Voice")), QStringLiteral("Microsoft Zira · normal speed"));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Create Audio Book finished · the audio book is waiting for your decision"));
    QVERIFY(dialog->getTargetFilePath().endsWith(QStringLiteral("Project_Report.wav")));

    const QPointer<PDFDocumentToolShell> shellGuard(shell);
    const QString target = dialog->getTargetFilePath();
    QTest::mouseClick(dialog->getConfirmButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([target]() { return QFile::exists(target); }));
    QVERIFY(AppToolShellFixture::waitFor([shellGuard]() { return !shellGuard || !shellGuard->getResultDialog(); }));
    QFile file(target);
    QVERIFY(file.open(QFile::ReadOnly));
    const QByteArray data = file.readAll();
    QCOMPARE(data.left(4), QByteArray("RIFF"));
    QCOMPARE(data.mid(8, 4), QByteArray("WAVE"));

    // Two items of 20 bytes per character and the pause of 600 ms between the pages
    const qint64 expected = 44 + 20 * (QStringLiteral("First paragraph").size() + QStringLiteral("Third paragraph").size()) + 2 * 9600;
    QCOMPARE(qint64(data.size()), expected);
}

void AppToolAudioBookTest::sampleVoiceOfAnotherLanguage()
{
    // R4-14 d: Play Sample speaks with the voice of the tool; the Voice menu offers the voices of all
    // languages of the engine (341:9657), so the voice of the sample may be of another language
    // than the current locale of the engine. The engine mock of Qt starts in en-GB with the voice
    // Bob (Bob, Anne); Kari is a voice of fi-FI.
    pdf::PDFAppQtSpeechEngine engine(QStringLiteral("mock"), nullptr);
    if (engine.getVoice() != QStringLiteral("Bob"))
    {
        QSKIP("The engine mock of Qt TextToSpeech is not installed.");
    }
    engine.setVoice(QStringLiteral("Kari"));
    QCOMPARE(engine.getVoice(), QStringLiteral("Kari"));
    engine.setVoice(QStringLiteral("Bob"));
    QCOMPARE(engine.getVoice(), QStringLiteral("Bob"));
}

void AppToolAudioBookTest::tenThousandItems()
{
    // The Text stream of a long book (plan 6, Tisíce položek): 10,000 items - select all, skip
    // them, Undo, a filter and a selection of one item stay quick (the list is a model with a
    // delegate; the selection and the rows are found without a search of the list for each item)
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = nullptr;
    PDFAppToolGUIAudioBook* audio = H::openTool(fixture, &shell);
    QVERIFY(audio);

    constexpr int ITEM_COUNT = 10000;
    pdf::PDFDocumentTextFlow::Items items;
    for (int i = 0; i < ITEM_COUNT; ++i)
    {
        items.push_back({ QRectF(60, 100 + (i % 20) * 30, 400, 20), i % 2, QStringLiteral("Paragraph %1 of a long book, which is read aloud").arg(i), pdf::PDFDocumentTextFlow::Text, {} });
    }
    pdf::PDFDocumentTextFlowEditor editor;
    editor.setTextFlow(pdf::PDFDocumentTextFlow(std::move(items)));
    shell->getModel()->setFollowingValue(QStringLiteral("editedStream"), QVariant::fromValue(pdf::PDFDocumentToolAudioBookStream(editor)));
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->rowCount(QModelIndex()), ITEM_COUNT);

    const double factor = qMax(1.0, qEnvironmentVariable("PDF4QT_TEST_TIMEOUT_FACTOR").toDouble());
    QElapsedTimer timer;
    timer.start();

    std::set<size_t> all;
    for (size_t i = 0; i < size_t(ITEM_COUNT); ++i)
    {
        all.insert(i);
    }
    audio->selectItems(all);
    QCOMPARE(audio->getListView()->selectionModel()->selectedRows().size(), qsizetype(ITEM_COUNT));
    audio->skipSelected();
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->getReadCount(), size_t(0));
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->getReadCount(), size_t(ITEM_COUNT));
    QTest::mouseClick(audio->getFilterChip(pdf::PDFAppAudioBookItemModel::Filter::Skipped), Qt::LeftButton);
    QTest::mouseClick(audio->getFilterChip(pdf::PDFAppAudioBookItemModel::Filter::All), Qt::LeftButton);
    audio->selectItems({ size_t(ITEM_COUNT - 1) });
    AppToolShellFixture::settle();
    QCOMPARE(audio->getListView()->selectionModel()->selectedRows().size(), qsizetype(1));
    audio->skipSelected();
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->getReadCount(), size_t(ITEM_COUNT - 1));

    QVERIFY2(timer.elapsed() < qint64(5000 * factor), qPrintable(QString::number(timer.elapsed())));
}

void AppToolAudioBookTest::catastrophicExpressionStaysQuick()
{
    // Plan 6 (Regulární výraz Audio Book): the check of Select by regular expression runs at every
    // key of the popover; the pattern (a+)+$ over items, which end by another character, needs an
    // exponential backtracking - every item ends quickly, the GUI does not freeze, no item matches
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = nullptr;
    PDFAppToolGUIAudioBook* audio = H::openTool(fixture, &shell);
    QVERIFY(audio);

    constexpr int ITEM_COUNT = 200;
    pdf::PDFDocumentTextFlow::Items items;
    for (int i = 0; i < ITEM_COUNT; ++i)
    {
        items.push_back({ QRectF(60, 100, 400, 20), 0, QString(40, QLatin1Char('a')) + QStringLiteral("!"), pdf::PDFDocumentTextFlow::Text, {} });
    }
    items.push_back({ QRectF(60, 200, 400, 20), 0, QStringLiteral("aaa"), pdf::PDFDocumentTextFlow::Text, {} });
    pdf::PDFDocumentTextFlowEditor editor;
    editor.setTextFlow(pdf::PDFDocumentTextFlow(std::move(items)));
    shell->getModel()->setFollowingValue(QStringLiteral("editedStream"), QVariant::fromValue(pdf::PDFDocumentToolAudioBookStream(editor)));
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->getItemCount(), size_t(ITEM_COUNT + 1));

    const double factor = qMax(1.0, qEnvironmentVariable("PDF4QT_TEST_TIMEOUT_FACTOR").toDouble());
    QElapsedTimer timer;
    timer.start();
    const PDFAppToolGUIAudioBook::SelectionCheck check = audio->checkSelection(pdf::PDFAppAudioBookSelectPopover::Kind::RegularExpression, QStringLiteral("(a+)+$"));
    QVERIFY2(timer.elapsed() < qint64(2000 * factor), qPrintable(QString::number(timer.elapsed())));
    QVERIFY(check.isValid);
    QCOMPARE(check.items, std::set<size_t>({ size_t(ITEM_COUNT) }));

    // The run reads skip by the same expression: the catastrophic items end quickly, the short
    // item matches and is skipped
    timer.restart();
    const QRegularExpression expression = pdf::PDFDocumentToolAudioBook::createExpression(QStringLiteral("(a+)+$"));
    QVERIFY(expression.isValid());
    int matches = 0;
    for (int i = 0; i < ITEM_COUNT; ++i)
    {
        matches += expression.match(QString(40, QLatin1Char('a')) + QStringLiteral("!")).hasMatch() ? 1 : 0;
    }
    QVERIFY2(timer.elapsed() < qint64(2000 * factor), qPrintable(QString::number(timer.elapsed())));
    QCOMPARE(matches, 0);
    QVERIFY(expression.match(QStringLiteral("aaa")).hasMatch());
}

void AppToolAudioBookTest::listRowsStartAtTheHeader()
{
    // 341:9256: the header (x 12), the chips (x 12) and item-list (x 12) start at one edge; the
    // bar of the state of a row is 6 from the edge of the row (RS4FIGMA: the rows were 6 px in)
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFAppToolGUIAudioBook* audio = H::openTool(fixture);
    QVERIFY(audio);
    AppToolShellFixture::settle();

    QWidget* window = fixture.window;
    const int headerLeft = audio->getStreamTitle()->mapTo(window, QPoint(0, 0)).x();
    const int chipsLeft = audio->getFilterChip(pdf::PDFAppAudioBookItemModel::Filter::All)->mapTo(window, QPoint(0, 0)).x();
    const QRect firstRow = audio->getListView()->visualRect(audio->getItemModel()->index(0, 0));
    const int rowLeft = audio->getListView()->viewport()->mapTo(window, firstRow.topLeft()).x();
    QCOMPARE(chipsLeft, headerLeft);
    QCOMPARE(rowLeft, headerLeft);
}

void AppToolAudioBookTest::contextMenuOfTheList()
{
    // Plan 2.7: the context menu of the list = Select / Read / Skip / Move; a right click on an item,
    // which is not selected, selects it; the actions act on the selection, as their buttons
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = nullptr;
    PDFAppToolGUIAudioBook* audio = H::openTool(fixture, &shell);
    QVERIFY(audio);
    AppToolShellFixture::settle();

    auto openMenu = [audio](size_t itemIndex) -> QMenu*
    {
        pdf::PDFListView* view = audio->getListView();
        const QPoint position = view->visualRect(audio->getItemModel()->index(audio->getItemModel()->getRow(itemIndex), 0)).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, position, view->viewport()->mapToGlobal(position));
        QApplication::sendEvent(view->viewport(), &event);
        AppToolShellFixture::settle();
        return audio->getContextMenu();
    };
    auto findAction = [](QMenu* menu, const QString& key) -> QAction*
    {
        for (QAction* action : menu->findChildren<QAction*>())
        {
            if (action->data().toString() == key)
            {
                return action;
            }
        }
        return nullptr;
    };

    const size_t second = size_t(H::findItem(audio, QStringLiteral("Second paragraph")));
    QMenu* menu = openMenu(second);
    QVERIFY(menu);
    QVERIFY(menu->isVisible());
    QCOMPARE(audio->getItemModel()->getSelection(), std::set<size_t>({ second }));

    // The rows of the menu: Select (its four ways), Read, Skip, Move Up, Move Down
    for (const char* key : { "rectangle", "text", "regex", "pages", "read", "skip", "up", "down" })
    {
        QVERIFY2(findAction(menu, QString::fromLatin1(key)), key);
    }
    QVERIFY(findAction(menu, QStringLiteral("skip"))->isEnabled());
    QVERIFY(findAction(menu, QStringLiteral("skip"))->text().startsWith(QStringLiteral("Skip")));

    // Skip: the item is skipped, one step of Undo
    findAction(menu, QStringLiteral("skip"))->trigger();
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->getItemState(second), pdf::PDFAppAudioBookOverlay::State::Skipped);
    QCOMPARE(audio->getItemModel()->getReadCount(), size_t(2));
    QVERIFY(shell->getModel()->canUndo());

    // Read again, then Move Up
    menu = openMenu(second);
    QVERIFY(menu);
    findAction(menu, QStringLiteral("read"))->trigger();
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->getReadCount(), size_t(3));
    menu = openMenu(second);
    QVERIFY(menu);
    findAction(menu, QStringLiteral("up"))->trigger();
    AppToolShellFixture::settle();
    QCOMPARE(H::findItem(audio, QStringLiteral("Second paragraph")), qint64(second) - 1);

    // Select › By contained text opens the popover of 341:10217
    menu = openMenu(size_t(H::findItem(audio, QStringLiteral("Third paragraph"))));
    QVERIFY(menu);
    QAction* byText = findAction(menu, QStringLiteral("text"));
    menu->close();
    byText->trigger();
    QVERIFY(AppToolShellFixture::waitFor([audio]() { return audio->getSelectPopover() && audio->getSelectPopover()->isVisible(); }));
    QCOMPARE(audio->getSelectPopover()->getKind(), pdf::PDFAppAudioBookSelectPopover::Kind::ContainedText);
    audio->getSelectPopover()->close();
    AppToolShellFixture::settle();
}

void AppToolAudioBookTest::contextMenuActionMayCloseTheTab()
{
    // P2AUDIOCRASH (lifetime convention "emit last"): an action of the context menu of the list emits
    // signals (Skip: one step of Undo), whose slots may close the document tab with the tool, the list
    // and the menu. QMenu reads and writes its own members after its signal triggered returns
    // (QMenuPrivate::_q_actionTriggered, activateCausedStack), so the action runs only after the
    // menu has finished its signal: the step of Undo comes after QAction::trigger() has returned,
    // never inside it, and a slot of it closes the tab safely (an access violation of the sweep
    // audiobookmenus-tab-0, about one run of twenty, before the fix)
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    fixture.context->setSpeechSynthesizerFactory(std::make_shared<GuiFakeFactory>(true));
    QPointer<pdf::PDFAppDocumentTab> tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Project_Report.pdf")), { { QStringLiteral("First paragraph"), QStringLiteral("Second paragraph") }, { QStringLiteral("Third paragraph") } }));
    QVERIFY(tab);
    QPointer<PDFDocumentToolShell> shell = fixture.openTool(tab, H::TOOL_ID);
    QPointer<PDFAppToolGUIAudioBook> audio = H::gui(shell);
    QVERIFY(H::waitStream(audio));
    QVERIFY(AppToolShellFixture::waitFor([&audio]() { return audio && !audio->isProbingVoices(); }));
    AppToolShellFixture::settle();

    // The context menu of "Second paragraph" and its Skip
    const size_t second = size_t(H::findItem(audio, QStringLiteral("Second paragraph")));
    pdf::PDFListView* view = audio->getListView();
    const QPoint position = view->visualRect(audio->getItemModel()->index(audio->getItemModel()->getRow(second), 0)).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, view->viewport()->mapToGlobal(position));
    QApplication::sendEvent(view->viewport(), &event);
    AppToolShellFixture::settle();
    QPointer<QMenu> menu = audio->getContextMenu();
    QVERIFY(menu);
    QAction* skip = nullptr;
    for (QAction* action : menu->actions())
    {
        skip = action->data().toString() == QStringLiteral("skip") ? action : skip;
    }
    QVERIFY(skip);
    QVERIFY(skip->isEnabled());

    // A slot of the step of Undo closes the tab - only outside of the signal of the menu (inside it the
    // deletion would free the menu under QMenu itself; the test only records the failure then)
    bool isTriggerReturned = false;
    int changesInsideTrigger = 0;
    int changesAfterTrigger = 0;
    QObject context;
    QObject::connect(shell->getModel(), &pdf::PDFAppToolUndoTarget::undoStateChanged, &context, [&]()
    {
        if (!isTriggerReturned)
        {
            ++changesInsideTrigger;
            return;
        }
        ++changesAfterTrigger;
        delete tab.data();
    });
    skip->trigger();
    isTriggerReturned = true;
    QCOMPARE(changesInsideTrigger, 0);

    // From the event loop: Skip has made its step of Undo, its slot has closed the tab with the tool and
    // the menu, the window lives on
    QVERIFY(AppToolShellFixture::waitFor([&tab]() { return !tab; }));
    AppToolShellFixture::settle();
    QCOMPARE(changesAfterTrigger, 1);
    QVERIFY(!shell);
    QVERIFY(!audio);
    QVERIFY(!menu);
    QVERIFY(fixture.window);
}

void AppToolAudioBookTest::runningCardShowsThePage()
{
    // 341:10706: the running card says the page of the item being spoken - "Page 2 · about … left ·
    // Stop ends the run" while the third read item ("Third paragraph", page 2) is spoken
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    fixture.context->setSpeechSynthesizerFactory(std::make_shared<GuiHoldingFactory>());
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Project_Report.pdf")), { { QStringLiteral("First paragraph"), QStringLiteral("Second paragraph") }, { QStringLiteral("Third paragraph") } }));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::TOOL_ID);
    PDFAppToolGUIAudioBook* audio = H::gui(shell);
    QVERIFY(H::waitStream(audio));
    QVERIFY(AppToolShellFixture::waitFor([audio]() { return !audio->isProbingVoices(); }));
    AppToolShellFixture::settle();

    while (GuiHoldingSynthesizer::released.tryAcquire())
    {
    }
    shell->confirm();
    QVERIFY(AppToolShellFixture::waitFor([shell]()
    {
        const pdf::PDFAppToolProgressCard* card = shell->getProgressCard();
        return card && card->getTimeLabel() && card->getTimeLabel()->text().startsWith(QStringLiteral("Page 2 · "));
    }));
    const QString timeText = shell->getProgressCard()->getTimeLabel()->text();
    QVERIFY2(timeText.endsWith(QStringLiteral("Stop ends the run")), qPrintable(timeText));
    QVERIFY2(shell->getProgressCard()->getTitleLabel()->text().startsWith(QStringLiteral("Creating audio · item ")), qPrintable(shell->getProgressCard()->getTitleLabel()->text()));

    GuiHoldingSynthesizer::released.release();
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() && shell->getResultDialog()->isVisible(); }));
}

void AppToolAudioBookTest::newValueAtTheAddressOfAFreedOne()
{
    // The GUI knows the value of the stream it has stored by the identity of its data. A value
    // replaced without the GUI hearing it (signals of the model blocked) and freed must never let a
    // new value at the same address look like the known one: the GUI keeps its value alive, the
    // new value is loaded. The new value is allocated right after the old one is freed, the way
    // the heap reuses the block.
    using H = AudioBookGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = nullptr;
    PDFAppToolGUIAudioBook* audio = H::openTool(fixture, &shell);
    QVERIFY(audio);
    audio->selectItems({ size_t(H::findItem(audio, QStringLiteral("Second paragraph"))) });
    audio->skipSelected();
    AppToolShellFixture::settle();
    QCOMPARE(audio->getItemModel()->getItemCount(), size_t(5));

    // Each attempt: the value known to the GUI (only the model holds it, a following value) is
    // replaced unheard and freed, then a value of another text comes - at the address of the freed
    // value, when the heap gives it back among the next allocations
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const void* knownIdentity = shell->getModel()->getValue(QStringLiteral("editedStream")).value<pdf::PDFDocumentToolAudioBookStream>().getIdentity();
        {
            const QSignalBlocker blocker(shell->getModel());
            shell->getModel()->setFollowingValue(QStringLiteral("editedStream"), QVariant::fromValue(pdf::PDFDocumentToolAudioBookStream()));
        }

        const QString text = QStringLiteral("Replacement stream %1").arg(attempt);
        pdf::PDFDocumentTextFlowEditor editor;
        editor.setTextFlow(pdf::PDFDocumentTextFlow({ { QRectF(60, 100, 400, 20), 0, text, pdf::PDFDocumentTextFlow::Text, {} } }));
        std::vector<pdf::PDFDocumentToolAudioBookStream> candidates;
        for (int i = 0; i < 256; ++i)
        {
            candidates.emplace_back(editor);
            if (candidates.back().getIdentity() == knownIdentity)
            {
                break;
            }
        }

        shell->getModel()->setFollowingValue(QStringLiteral("editedStream"), QVariant::fromValue(candidates.back()));
        candidates.clear();
        AppToolShellFixture::settle();
        QCOMPARE(audio->getItemModel()->getItemCount(), size_t(1));
        QCOMPARE(audio->getItemModel()->getEditor().getText(0), text);
    }
}

QTEST_MAIN(AppToolAudioBookTest)

#include "tst_apptoolaudiobooktest.moc"
