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

#include "appdocumentfixture.h"

#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentcommands.h"
#include "pdfappdocumentpanel.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsearchcontroller.h"
#include "pdfappdocumentsearchindex.h"
#include "pdfappdocumentsearchpanel.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumentsignatures.h"
#include "pdfappdocumentspeech.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappnavigator.h"
#include "pdfdatabasestatement.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdftrustedcertificaterepository.h"

#include "pdfcertificatestore.h"
#include "pdfcolorconvertor.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdocumenttextindex.h"

#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfstylepainter.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"

#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QLabel>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTimeZone>
#include <QToolButton>
#include <QtMath>
#include <QtTest>

/// Speech engine of the tests: it records the said texts, the test ends a text
/// by finish() (the real engines are not available offscreen and under valgrind)
class FakeSpeechEngine : public pdf::PDFAppSpeechEngine
{
public:
    explicit FakeSpeechEngine(QObject* parent) :
        pdf::PDFAppSpeechEngine(parent)
    {
        s_last = this;
    }

    virtual ~FakeSpeechEngine() override
    {
        if (s_last == this)
        {
            s_last = nullptr;
        }
    }

    static pdf::PDFAppSpeechEngine* create(const QString& engineName, QObject* parent)
    {
        FakeSpeechEngine* engine = new FakeSpeechEngine(parent);
        engine->engineName = engineName;
        if (s_isBroken)
        {
            engine->state = State::Error;
        }
        return engine;
    }

    virtual State getState() const override { return state; }
    virtual QString getErrorString() const override { return state == State::Error ? QStringLiteral("No speech daemon") : QString(); }
    virtual QList<QLocale> getLocales() const override { return { QLocale(QLocale::English, QLocale::UnitedStates), QLocale(QLocale::German, QLocale::Germany) }; }
    virtual QLocale getLocale() const override { return locale; }
    virtual void setLocale(const QLocale& newLocale) override { locale = newLocale; }

    virtual QList<pdf::PDFAppSpeechVoice> getVoices() const override
    {
        if (locale.language() == QLocale::German)
        {
            return { { QStringLiteral("Hedda"), QStringLiteral("Hedda (Female, Adult)") } };
        }
        return { { QStringLiteral("Aria"), QStringLiteral("Aria (Female, Adult)") }, { QStringLiteral("Guy"), QStringLiteral("Guy (Male, Adult)") } };
    }

    virtual QString getVoice() const override { return voice; }
    virtual void setVoice(const QString& name) override
    {
        if (!name.isEmpty())
        {
            voice = name;
        }
    }
    virtual void setRate(double value) override { rate = value; }
    virtual void setPitch(double value) override { pitch = value; }
    virtual void setVolume(double value) override { volume = value; }

    virtual void say(const QString& text) override
    {
        said << text;
        setEngineState(State::Speaking);
    }

    virtual void pause() override { setEngineState(State::Paused); }
    virtual void resume() override { setEngineState(State::Speaking); }
    virtual void stop() override { setEngineState(State::Ready); }

    /// The said text has ended
    void finish() { setEngineState(State::Ready); }

    void setEngineState(State newState)
    {
        if (state != newState && state != State::Error)
        {
            state = newState;
            Q_EMIT stateChanged();
        }
    }

    static FakeSpeechEngine* s_last;
    static bool s_isBroken;

    QString engineName;
    State state = State::Ready;
    QStringList said;
    QLocale locale = QLocale(QLocale::English, QLocale::UnitedStates);
    QString voice = QStringLiteral("Aria");
    double rate = 0.0;
    double pitch = 0.0;
    double volume = 1.0;
};

FakeSpeechEngine* FakeSpeechEngine::s_last = nullptr;
bool FakeSpeechEngine::s_isBroken = false;

/// Package D of step 3: the search in the document (lazy cancellable text index,
/// search in a worker, results model, highlights, panel 198:4520, the field of the
/// toolbar, F3 / Shift+F3, ?q=, the safety limit), the signatures (background
/// verification, the status item 266:7859, the panel 195:2959, trusted certificates
/// of the user, the settings), the speech (panel 195:2499, the settings speech.*,
/// Read aloud) and the lifetime of all of them (tab, window, document deleted while
/// the workers run, dialogs open).
class AppDocumentSearchTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void commands();
    void search();
    void searchField();
    void nextPrevious();
    void searchOptions();
    void invalidExpression();
    void searchRoute();
    void hostileRouteQuery();
    void lazyIndex();
    void indexerCancel();
    void pageContentsChanged();
    void resultModel();
    void snippets();
    void pageLabels();
    void highlights();
    void truncation();
    void closeTabDuringIndexing();
    void closeTabDuringSearch();
    void deleteWindowDuringSearch();
    void deleteFromOwnSignals_data();
    void deleteFromOwnSignals();
    void closeSearch();
    void themesAndDirection();

    void signatures();
    void signaturesTrust();
    void signatureCards();
    void signaturesSettings();
    void unsignedDocument();
    void trustedRepository();
    void closeTabDuringVerification();
    void trustDialogLifetime();

    void speechPanel();
    void speechReading();
    void speechSettings();
    void speechCommand();
    void speechError();
    void speechLifetime();
    void qtSpeechEngine();

    void reviewNewQueryDropsResults();
    void reviewSignatureCardsKeepExpansion();
    void reviewTrustedRepositoryLimit();
    void reviewSpeechLongBlock();

    void screenshots();

private:
    static pdf::PDFAppDocumentSearchCommandHandler* handler(pdf::PDFAppDocumentTab* tab);
    static bool waitForSearch(pdf::PDFAppDocumentSearchController* controller, int timeout = 60000);

    /// The test runs under valgrind (the time limits are 20 times longer)
    static bool isValgrind() { return qEnvironmentVariable("LD_PRELOAD").contains(QLatin1String("vgpreload")); }
    static pdf::PDFAppDocumentTab* openReady(pdftest::AppDocumentFixture& fixture, const QString& file);
};

pdf::PDFAppDocumentSearchCommandHandler* AppDocumentSearchTest::handler(pdf::PDFAppDocumentTab* tab)
{
    return tab ? qobject_cast<pdf::PDFAppDocumentSearchCommandHandler*>(tab->findHandler(pdf::PDFAppCommands::find())) : nullptr;
}

bool AppDocumentSearchTest::waitForSearch(pdf::PDFAppDocumentSearchController* controller, int timeout)
{
    QElapsedTimer timer;
    timer.start();
    const qint64 limit = qint64(timeout) * (isValgrind() ? 20 : 1);
    while (controller && timer.elapsed() < limit)
    {
        const pdf::PDFAppDocumentSearchController::State state = controller->getState();
        if (state == pdf::PDFAppDocumentSearchController::State::Finished || state == pdf::PDFAppDocumentSearchController::State::Failed)
        {
            return true;
        }
        QTest::qWait(10);
    }
    return false;
}

pdf::PDFAppDocumentTab* AppDocumentSearchTest::openReady(pdftest::AppDocumentFixture& fixture, const QString& file)
{
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    if (!tab)
    {
        return nullptr;
    }

    QElapsedTimer timer;
    timer.start();
    while (tab && !tab->getDocument()->isReady() && timer.elapsed() < 30000)
    {
        QTest::qWait(10);
    }
    return (tab && tab->getDocument()->isReady()) ? tab : nullptr;
}

void AppDocumentSearchTest::initTestCase()
{
    // The style of the application (the panels are drawn by the tokens of the theme)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void AppDocumentSearchTest::init()
{
    FakeSpeechEngine::s_isBroken = false;
    pdf::PDFAppSpeechEngine::setFactory(&FakeSpeechEngine::create);
}

void AppDocumentSearchTest::cleanup()
{
    pdf::PDFAppSpeechEngine::setFactory(nullptr);
}

void AppDocumentSearchTest::commands()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Search.pdf"), 2));
    QVERIFY(tab);

    for (const QString& id : pdf::PDFAppDocumentCommandTable::getCommands(pdf::PDFAppDocumentPackage::Search))
    {
        QVERIFY2(tab->findHandler(id), qPrintable(id));
        QVERIFY2(fixture.window->getCommandAction(id), qPrintable(id));
    }
    QCOMPARE(fixture.window->getCommandAction(pdf::PDFAppCommands::find())->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_F));
    QCOMPARE(pdf::PDFAppDocumentPanelIds::search(), QStringLiteral("search"));

    // Find and Read aloud are enabled, Find next / previous need a text
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::find()));
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::readAloud()));
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::findNext()));
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::findPrevious()));
    QVERIFY(!tab->isCommandChecked(pdf::PDFAppCommands::readAloud()));
    QVERIFY(!tab->isCommandChecked(pdf::PDFAppCommands::find()));

    handler(tab)->startSearch(QStringLiteral("gamma"));
    QVERIFY(waitForSearch(handler(tab)->getSearchController()));
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::findNext()));
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::findPrevious()));

    // Find without the field of the toolbar shows the panel
    tab->executeCommand(pdf::PDFAppCommands::find());
    QCOMPARE(tab->getView()->getCurrentPanelId(), pdf::PDFAppDocumentPanelIds::search());

    // A command of another package is not handled
    QVERIFY(!handler(tab)->handles(pdf::PDFAppCommands::save()));
    QVERIFY(!handler(tab)->isEnabled(pdf::PDFAppCommands::save()));
    handler(tab)->execute(pdf::PDFAppCommands::save());
}

void AppDocumentSearchTest::search()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Search.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentSearchController* controller = handler(tab)->getSearchController();
    QCOMPARE(controller->getState(), pdf::PDFAppDocumentSearchController::State::Idle);

    handler(tab)->startSearch(QStringLiteral("gamma"));
    QCOMPARE(tab->getView()->getCurrentPanelId(), pdf::PDFAppDocumentPanelIds::search());
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getState(), pdf::PDFAppDocumentSearchController::State::Finished);
    QCOMPARE(controller->getResultCount(), 3);
    QCOMPARE(controller->getCurrentIndex(), 0);
    QVERIFY(!controller->isTruncated());
    QCOMPARE(controller->getQuery(), QStringLiteral("gamma"));
    QCOMPARE(handler(tab)->getSearchText(), QStringLiteral("gamma"));
    QCOMPARE(tab->getView()->getStatusBar()->getMessage(), QStringLiteral("Result 1 of 3"));

    // The panel: its own header, the results in the list
    pdf::PDFAppDocumentSearchPanel* panel = qobject_cast<pdf::PDFAppDocumentSearchPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::search()));
    QVERIFY(panel);
    QVERIFY(panel->hasOwnHeader());
    QCOMPARE(panel->getTitle(), QStringLiteral("Search results"));
    QCOMPARE(panel->getSubtitle(), QStringLiteral("3 results for “gamma”"));
    QCOMPARE(panel->getListView()->model()->rowCount(), 3);
    QCOMPARE(panel->getListView()->currentIndex().row(), 0);
    QVERIFY(qobject_cast<pdf::PDFSearchResultDelegate*>(panel->getListView()->itemDelegate()));
    QVERIFY(panel->getNextButton()->isEnabled());
    QVERIFY(panel->getNoteLabel()->isHidden());

    // Geometry of 198:4520: the chips at x 12, the result rows 275 wide at x 12 of the
    // sidebar, the first row 10 px below the chips (8 + the gap 2), 2 px between rows
    {
        fixture.settle();
        QWidget* sidebar = tab->getView()->getSidebar();
        QListView* list = panel->getListView();
        const QRect first = list->visualRect(list->model()->index(0, 0));
        const QRect second = list->visualRect(list->model()->index(1, 0));
        QPushButton* chip = panel->getOptionButton(2);
        QVERIFY(chip);
        QCOMPARE(chip->mapTo(sidebar, QPoint(0, 0)).x(), 12);
        QCOMPARE(list->viewport()->mapTo(sidebar, first.topLeft()).x(), 12);
        QCOMPARE(first.width(), sidebar->width() - 1 - 24);
        QCOMPARE(list->viewport()->mapTo(sidebar, first.topLeft()).y() - chip->mapTo(sidebar, QPoint(0, chip->height())).y(), 10);
        QCOMPARE(second.top() - first.bottom() - 1, 2);
    }

    // A click on a row makes it current
    panel->getListView()->setCurrentIndex(panel->getListView()->model()->index(2, 0));
    QCOMPARE(controller->getCurrentIndex(), 2);
    QCOMPARE(tab->getView()->getStatusBar()->getMessage(), QStringLiteral("Result 3 of 3"));

    // One result and no result
    handler(tab)->startSearch(QStringLiteral("Page 2"));
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getResultCount(), 1);
    QCOMPARE(panel->getSubtitle(), QStringLiteral("1 result for “Page 2”"));

    handler(tab)->startSearch(QStringLiteral("omega"));
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getResultCount(), 0);
    QCOMPARE(controller->getCurrentIndex(), -1);
    QCOMPARE(panel->getSubtitle(), QStringLiteral("No results for “omega”"));
    QVERIFY(!panel->getMessageLabel()->isHidden());
    QVERIFY(!panel->getNextButton()->isEnabled());
}

void AppDocumentSearchTest::searchField()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Field.pdf"), 3));
    QVERIFY(tab);
    QLineEdit* field = handler(tab)->getSearchField();
    if (!field)
    {
        QLineEdit* toolbarField = tab->getView()->findChild<QLineEdit*>(QStringLiteral("documentSearchField"));
        QVERIFY2(!toolbarField, "The field of the toolbar exists, but the handler has not connected it");
        QSKIP("The toolbar has no search field");
    }

    // Ctrl+F focuses the field
    tab->executeCommand(pdf::PDFAppCommands::find());
    QTRY_VERIFY(field->hasFocus());

    // Enter searches the text, Enter again goes to the next result
    field->setText(QStringLiteral("beta"));
    QTest::keyClick(field, Qt::Key_Return);
    pdf::PDFAppDocumentSearchController* controller = handler(tab)->getSearchController();
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getResultCount(), 3);
    const int first = controller->getCurrentIndex();
    QTest::keyClick(field, Qt::Key_Return);
    QCOMPARE(controller->getCurrentIndex(), (first + 1) % 3);

    // Esc closes the search: the field is empty, the panel before the search is back
    QTest::keyClick(field, Qt::Key_Escape);
    QVERIFY(field->text().isEmpty());
    QCOMPARE(controller->getResultCount(), 0);
    QVERIFY(controller->getQuery().isEmpty());
    QVERIFY(tab->getView()->getCurrentPanelId() != pdf::PDFAppDocumentPanelIds::search());

    // The route text fills the field
    handler(tab)->startSearch(QStringLiteral("alpha"));
    QCOMPARE(field->text(), QStringLiteral("alpha"));
}

void AppDocumentSearchTest::nextPrevious()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Next.pdf"), 4));
    QVERIFY(tab);
    pdf::PDFAppDocumentSearchController* controller = handler(tab)->getSearchController();

    // F3 without results does nothing
    tab->executeCommand(pdf::PDFAppCommands::findNext());
    QCOMPARE(controller->getState(), pdf::PDFAppDocumentSearchController::State::Idle);

    handler(tab)->startSearch(QStringLiteral("alpha"));
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getResultCount(), 4);
    QCOMPARE(controller->getCurrentIndex(), 0);

    // F3 / Shift+F3 by the actions of the window, wrapping around
    QAction* next = fixture.window->getCommandAction(pdf::PDFAppCommands::findNext());
    QAction* previous = fixture.window->getCommandAction(pdf::PDFAppCommands::findPrevious());
    QVERIFY(next && previous);
    QCOMPARE(next->shortcut(), QKeySequence(Qt::Key_F3));
    next->trigger();
    QCOMPARE(controller->getCurrentIndex(), 1);
    previous->trigger();
    previous->trigger();
    QCOMPARE(controller->getCurrentIndex(), 3);
    next->trigger();
    QCOMPARE(controller->getCurrentIndex(), 0);

    // The view shows the page of the current result
    controller->setCurrentIndex(3);
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(3));

    // An index out of range is ignored
    controller->setCurrentIndex(-5);
    controller->setCurrentIndex(100);
    QCOMPARE(controller->getCurrentIndex(), 3);

    // The panel buttons
    pdf::PDFAppDocumentSearchPanel* panel = qobject_cast<pdf::PDFAppDocumentSearchPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::search()));
    QVERIFY(panel);
    panel->getNextButton()->click();
    QCOMPARE(controller->getCurrentIndex(), 0);
    panel->getPreviousButton()->click();
    QCOMPARE(controller->getCurrentIndex(), 3);
    QCOMPARE(panel->getListView()->currentIndex().row(), 3);
}

void AppDocumentSearchTest::searchOptions()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Options.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentSearchController* controller = handler(tab)->getSearchController();

    handler(tab)->startSearch(QStringLiteral("The"));
    QVERIFY(waitForSearch(controller));
    const int insensitive = controller->getResultCount();     // "The" and "the" on each page
    QCOMPARE(insensitive, 4);

    pdf::PDFAppDocumentSearchPanel* panel = qobject_cast<pdf::PDFAppDocumentSearchPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::search()));
    QVERIFY(panel);
    for (int i = 0; i < 4; ++i)
    {
        QPushButton* option = panel->getOptionButton(i);
        QVERIFY(option);
        QVERIFY(option->isCheckable());
        QCOMPARE(option->size(), QSize(pdf::PDFAppDocumentSearchPanel::OPTION_WIDTH, 32));
    }
    QVERIFY(!panel->getOptionButton(4));
    QVERIFY(!panel->getOptionButton(-1));

    // Match case searches again
    panel->getOptionButton(0)->click();
    QVERIFY(controller->getOptions().caseSensitive);
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getResultCount(), 2);

    // Whole words: "alpha" is a whole word, "alph" is not
    panel->getOptionButton(0)->click();
    panel->getOptionButton(1)->click();
    handler(tab)->startSearch(QStringLiteral("alph"));
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getResultCount(), 0);

    // Regex and wildcards exclude each other
    panel->getOptionButton(1)->click();
    panel->getOptionButton(2)->click();
    QVERIFY(controller->getOptions().regularExpression);
    panel->getOptionButton(3)->click();
    QVERIFY(controller->getOptions().wildcards);
    QVERIFY(!controller->getOptions().regularExpression);
    QVERIFY(!panel->getOptionButton(2)->isChecked());
    handler(tab)->startSearch(QStringLiteral("alph*"));
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getResultCount(), 2);

    panel->getOptionButton(2)->click();
    QVERIFY(!panel->getOptionButton(3)->isChecked());
    handler(tab)->startSearch(QStringLiteral("Page [12]"));
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getResultCount(), 2);

    // Options without a text do not search
    handler(tab)->closeSearch();
    pdf::PDFDocumentTextSearchOptions options;
    controller->setOptions(options);
    QCOMPARE(controller->getState(), pdf::PDFAppDocumentSearchController::State::Idle);
}

void AppDocumentSearchTest::invalidExpression()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Regex.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentSearchController* controller = handler(tab)->getSearchController();

    pdf::PDFDocumentTextSearchOptions options;
    options.regularExpression = true;
    controller->setOptions(options);
    handler(tab)->startSearch(QStringLiteral("(unclosed"));
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getState(), pdf::PDFAppDocumentSearchController::State::Failed);
    QVERIFY(!controller->getError().isEmpty());

    pdf::PDFAppDocumentSearchPanel* panel = qobject_cast<pdf::PDFAppDocumentSearchPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::search()));
    QVERIFY(panel);
    QCOMPARE(panel->getSubtitle(), QStringLiteral("The search text is not valid"));
    QVERIFY(!panel->getMessageLabel()->isHidden());
    QCOMPARE(panel->getMessageLabel()->text(), controller->getError());
}

void AppDocumentSearchTest::searchRoute()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Route.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentSearchController* controller = handler(tab)->getSearchController();

    QSignalSpy textSpy(handler(tab), &pdf::PDFAppDocumentSearchCommandHandler::searchTextChanged);
    fixture.window->getNavigator()->navigateLink(QStringLiteral("/document/%1?q=beta").arg(tab->getDocumentId()), pdf::PDFAppNavigationMode::Push, tab);
    fixture.settle();
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getQuery(), QStringLiteral("beta"));
    QCOMPARE(controller->getResultCount(), 2);
    QCOMPARE(pdf::PDFAppDocumentSearchCommandHandler::getRouteQuery(tab), QStringLiteral("beta"));
    QVERIFY(textSpy.count() >= 1);

    // The same text again does not search again
    pdf::PDFAppDocumentSearchCommandHandler::applyRouteQuery(tab, QStringLiteral("beta"));
    QCOMPARE(controller->getState(), pdf::PDFAppDocumentSearchController::State::Finished);

    // Null and foreign objects
    pdf::PDFAppDocumentSearchCommandHandler::applyRouteQuery(nullptr, QStringLiteral("x"));
    QVERIFY(pdf::PDFAppDocumentSearchCommandHandler::getRouteQuery(nullptr).isEmpty());
    QVERIFY(!pdf::PDFAppDocumentSearchCommandHandler::findHandler(fixture.window));
    QCOMPARE(pdf::PDFAppDocumentSearchCommandHandler::findHandler(tab->getView()), handler(tab));
    QCOMPARE(pdf::PDFAppDocumentSearchCommandHandler::findHandler(tab->getDocument()), handler(tab));

    // Closed search: no text of the route
    handler(tab)->closeSearch();
    QVERIFY(pdf::PDFAppDocumentSearchCommandHandler::getRouteQuery(tab).isEmpty());
}

void AppDocumentSearchTest::hostileRouteQuery()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Hostile.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentSearchController* controller = handler(tab)->getSearchController();

    // A text longer than the limit is an error, control characters and broken
    // UTF-16 are only texts
    pdf::PDFAppDocumentSearchCommandHandler::applyRouteQuery(tab, QString(5000, QLatin1Char('x')));
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getState(), pdf::PDFAppDocumentSearchController::State::Failed);

    QString broken = QStringLiteral("a");
    broken += QChar(0xD800);
    broken += QChar(0x0001);
    pdf::PDFAppDocumentSearchCommandHandler::applyRouteQuery(tab, broken);
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getResultCount(), 0);

    fixture.window->getNavigator()->navigateLink(QStringLiteral("/document/%1?q=%25%00%FF").arg(tab->getDocumentId()), pdf::PDFAppNavigationMode::Push, tab);
    fixture.settle();
}

void AppDocumentSearchTest::lazyIndex()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Lazy.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentTextIndexer* indexer = handler(tab)->getIndexer();

    // Nothing is indexed until the first search
    fixture.settle();
    QVERIFY(!indexer->isBuilding());
    QVERIFY(!indexer->getIndex());
    QVERIFY(!tab->getDocument()->getTextIndex());
    QCOMPARE(indexer->getProgress(), 0);

    QSignalSpy readySpy(indexer, &pdf::PDFAppDocumentTextIndexer::indexReady);
    QVERIFY(!indexer->requestIndex());
    QVERIFY(indexer->isBuilding());
    QVERIFY(!indexer->requestIndex());     // one build at a time
    QTRY_VERIFY_WITH_TIMEOUT(indexer->getIndex() != nullptr, 30000);
    QCOMPARE(readySpy.count(), 1);
    QVERIFY(indexer->requestIndex());
    QCOMPARE(indexer->getIndex(), tab->getDocument()->getTextIndex());

    // Cancel without a build does nothing
    indexer->cancel();
    QVERIFY(indexer->getIndex());
}

void AppDocumentSearchTest::indexerCancel()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 2000));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentTextIndexer* indexer = handler(tab)->getIndexer();
    pdf::PDFAppDocumentSearchController* controller = handler(tab)->getSearchController();
    QVERIFY(!pdf::PDFAppDocumentTextIndexer::buildIndex(nullptr, nullptr));

    // Stop of the operation of the tab (close-running dialog) stops the build and the search
    handler(tab)->startSearch(QStringLiteral("gamma"));
    QVERIFY(indexer->isBuilding());
    QCOMPARE(controller->getState(), pdf::PDFAppDocumentSearchController::State::Indexing);
    pdf::PDFAppDocumentOperation* operation = nullptr;
    for (pdf::PDFAppDocumentOperation* item : document->getOperations())
    {
        if (item->getKind() == pdf::PDFAppDocumentOperationKind::Index)
        {
            operation = item;
        }
    }
    QVERIFY(operation);
    QVERIFY(!operation->isBlockingChanges());
    operation->requestCancel();
    QVERIFY(!indexer->isBuilding());
    QCOMPARE(controller->getState(), pdf::PDFAppDocumentSearchController::State::Idle);
    QVERIFY(!indexer->getIndex());

    // A change of the page contents during the build builds the new version
    handler(tab)->startSearch(QStringLiteral("gamma 1999"));
    QVERIFY(indexer->isBuilding());
    pdf::PDFAppDocumentSnapshot snapshot = document->getSnapshot();
    pdf::PDFDocumentPointer copy(new pdf::PDFDocument(*snapshot.document));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(copy, nullptr, pdf::PDFModifiedDocument::PageContents), QStringLiteral("Contents"), snapshot.version, pdf::PDFAppChangeSource::User));
    QVERIFY(indexer->isBuilding());
    QVERIFY(waitForSearch(controller, 120000));
    QCOMPARE(controller->getResultCount(), 1);
    QVERIFY(document->getTextIndex());

    // Cancel of a running build (a new version is indexed at the next request)
    snapshot = document->getSnapshot();
    copy.reset(new pdf::PDFDocument(*snapshot.document));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(copy, nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Reset"), snapshot.version, pdf::PDFAppChangeSource::User));
    QVERIFY(!indexer->requestIndex());
    QSignalSpy buildingSpy(indexer, &pdf::PDFAppDocumentTextIndexer::buildingChanged);
    indexer->cancel();
    QCOMPARE(buildingSpy.count(), 1);
    QVERIFY(!indexer->isBuilding());
    QCOMPARE(indexer->getProgress(), 0);
}

void AppDocumentSearchTest::pageContentsChanged()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Changed.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentSearchController* controller = handler(tab)->getSearchController();

    handler(tab)->startSearch(QStringLiteral("gamma"));
    QVERIFY(waitForSearch(controller));
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = document->getTextIndex();
    QVERIFY(index);

    // A change without new page contents keeps the index and the results
    pdf::PDFAppDocumentSnapshot snapshot = document->getSnapshot();
    pdf::PDFDocumentPointer annotationCopy(new pdf::PDFDocument(*snapshot.document));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(annotationCopy, nullptr, pdf::PDFModifiedDocument::Annotation), QStringLiteral("Annotation"), snapshot.version, pdf::PDFAppChangeSource::User));
    QCOMPARE(document->getTextIndex(), index);
    QCOMPARE(controller->getState(), pdf::PDFAppDocumentSearchController::State::Finished);

    // New page contents: the search runs again with a new index
    snapshot = document->getSnapshot();
    pdf::PDFDocumentPointer contentsCopy(new pdf::PDFDocument(*snapshot.document));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(contentsCopy, nullptr, pdf::PDFModifiedDocument::PageContents), QStringLiteral("Contents"), snapshot.version, pdf::PDFAppChangeSource::User));
    QVERIFY(!document->getTextIndex());
    QTRY_VERIFY_WITH_TIMEOUT(document->getTextIndex() != nullptr, 30000);
    QVERIFY(document->getTextIndex() != index);
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getResultCount(), 2);
}

void AppDocumentSearchTest::resultModel()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Model.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentSearchController* controller = handler(tab)->getSearchController();
    handler(tab)->startSearch(QStringLiteral("brown"));
    QVERIFY(waitForSearch(controller));

    pdf::PDFAppDocumentSearchResultModel* model = controller->getModel();
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(model->rowCount(model->index(0, 0)), 0);
    const QModelIndex index = model->index(1, 0);
    QCOMPARE(model->data(index, Qt::DisplayRole).toString(), QStringLiteral("Page 2"));
    const QString snippet = model->data(index, pdf::PDFComponentRoles::TextRole).toString();
    QVERIFY2(snippet.contains(QStringLiteral("brown")), qPrintable(snippet));
    QVERIFY(model->data(index, Qt::ToolTipRole).toString().contains(QStringLiteral("quick brown fox")));

    const QList<pdf::PDFComponentPainter::TextRange> ranges = pdf::PDFSearchResultDelegate::getMatchRanges(model->data(index, pdf::PDFComponentRoles::MatchRangesRole));
    QCOMPARE(ranges.size(), 1);
    QCOMPARE(snippet.mid(ranges.front().start, ranges.front().length), QStringLiteral("brown"));

    // Invalid rows and roles
    QVERIFY(!model->data(model->index(5, 0), Qt::DisplayRole).isValid());
    QVERIFY(!model->data(QModelIndex(), Qt::DisplayRole).isValid());
    QVERIFY(!model->data(index, Qt::DecorationRole).isValid());

    model->setResults(nullptr);
    QCOMPARE(model->rowCount(), 0);
}

void AppDocumentSearchTest::snippets()
{
    using Model = pdf::PDFAppDocumentSearchResultModel;

    qsizetype start = -2;
    QCOMPARE(Model::createSnippet(QStringLiteral("a  revenue   b"), 3, 7, &start), QStringLiteral("a revenue b"));
    QCOMPARE(start, qsizetype(2));

    // Long context: cut at spaces with an ellipsis
    const QString before = QStringLiteral("word ").repeated(40);
    const QString after = QStringLiteral(" tail").repeated(60);
    const QString context = before + QStringLiteral("MATCH") + after;
    const QString snippet = Model::createSnippet(context, before.size(), 5, &start);
    QVERIFY(snippet.startsWith(QChar(0x2026)));
    QVERIFY(snippet.endsWith(QChar(0x2026)));
    QVERIFY(snippet.size() <= Model::SNIPPET_BEFORE + Model::SNIPPET_AFTER + 10);
    QCOMPARE(snippet.mid(start, 5), QStringLiteral("MATCH"));

    // Unknown position: the beginning of the context
    QCOMPARE(Model::createSnippet(QStringLiteral(" abc "), -1, 0, &start), QStringLiteral("abc"));
    QCOMPARE(start, qsizetype(-1));
    QVERIFY(Model::createSnippet(QString(1000, QLatin1Char('x')), 5000, 3, &start).size() <= Model::SNIPPET_BEFORE + Model::SNIPPET_AFTER + 1);
    QCOMPARE(start, qsizetype(-1));
    QVERIFY(Model::createSnippet(QString(), 0, 1, nullptr).isEmpty());

    // An occurrence at the beginning and at the end
    QCOMPARE(Model::createSnippet(QStringLiteral("abc def"), 0, 3, &start), QStringLiteral("abc def"));
    QCOMPARE(start, qsizetype(0));
    QCOMPARE(Model::createSnippet(QStringLiteral("abc def"), 4, 3, &start), QStringLiteral("abc def"));
    QCOMPARE(start, qsizetype(4));
}

void AppDocumentSearchTest::pageLabels()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::pageLabels(fixture.folder()));
    QVERIFY(tab);

    const pdf::PDFDocument* document = tab->getDocument()->getDocument().data();
    QCOMPARE(pdf::PDFAppDocumentSearchResultModel::getPageText(document, 1), QStringLiteral("Page 2 (ii)"));
    QCOMPARE(pdf::PDFAppDocumentSearchResultModel::getPageText(document, 8), QStringLiteral("Page 9 (A-1)"));
    QCOMPARE(pdf::PDFAppDocumentSearchResultModel::getPageText(nullptr, 4), QStringLiteral("Page 5"));
}

void AppDocumentSearchTest::highlights()
{
    // Two occurrences on page 0, one on page 1; the second one is current
    std::shared_ptr<pdf::PDFAppDocumentSearchResults> results = std::make_shared<pdf::PDFAppDocumentSearchResults>();
    for (int i = 0; i < 3; ++i)
    {
        pdf::PDFDocumentTextSearchMatch match;
        match.pageIndex = i < 2 ? 0 : 1;
        const qreal x = 10 + i * 40;
        match.quadrilaterals << QPointF(x, 10) << QPointF(x + 30, 10) << QPointF(x, 30) << QPointF(x + 30, 30);
        match.boundingRect = match.quadrilaterals.boundingRect();
        results->result.results.push_back(match);
    }

    pdf::PDFAppDocumentSearchHighlighter highlighter;
    highlighter.setResults(results);
    highlighter.setCurrentIndex(1);

    pdf::PDFTextLayoutCache cache([](pdf::PDFInteger) { return pdf::PDFTextLayout(); });
    pdf::PDFTextLayoutGetter getter(&cache, 0);
    pdf::PDFColorConvertor convertor;
    QList<pdf::PDFRenderError> errors;

    QImage image(120, 50, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        highlighter.drawPage(&painter, 0, nullptr, getter, QTransform(), convertor, errors);
        highlighter.drawPage(&painter, 5, nullptr, getter, QTransform(), convertor, errors);
        highlighter.drawPage(nullptr, 0, nullptr, getter, QTransform(), convertor, errors);
    }

    // The occurrence (multiplied with white = the color), the current one with its
    // fill and border; nothing outside
    const pdf::PDFThemeManager* manager = pdf::PDFThemeManager::getInstance();
    const QColor match = manager->getColor(pdf::PDFThemeColors::SearchMatch);
    const QColor current = manager->getColor(pdf::PDFThemeColors::SearchMatchCurrent);
    const QColor border = manager->getColor(pdf::PDFThemeColors::SearchMatchBorder);
    auto near = [](QColor a, QColor b) { return qAbs(a.red() - b.red()) < 8 && qAbs(a.green() - b.green()) < 8 && qAbs(a.blue() - b.blue()) < 8; };
    QVERIFY(near(image.pixelColor(25, 20), match));
    QVERIFY(near(image.pixelColor(65, 20), current));
    QVERIFY(near(image.pixelColor(50, 20), border));
    QVERIFY(near(image.pixelColor(65, 10), border));
    QVERIFY(near(image.pixelColor(79, 20), border));
    QVERIFY(near(image.pixelColor(105, 20), Qt::white));    // page 1 is not drawn
    QVERIFY(near(image.pixelColor(5, 45), Qt::white));
    QVERIFY(errors.isEmpty());

    // A rotated occurrence is a polygon; no results draw nothing
    QCOMPARE(pdf::PDFAppDocumentSearchHighlighter::getMatchPath(results->result.results[0], QTransform().rotate(30)).elementCount() > 0, true);
    highlighter.setResults(nullptr);
    QImage empty(10, 10, QImage::Format_ARGB32_Premultiplied);
    empty.fill(Qt::white);
    {
        QPainter painter(&empty);
        highlighter.drawPage(&painter, 0, nullptr, getter, QTransform(), convertor, errors);
    }
    QCOMPARE(empty.pixelColor(5, 5), QColor(Qt::white));
}

void AppDocumentSearchTest::truncation()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 2000));
    QVERIFY(tab);
    pdf::PDFAppDocumentSearchController* controller = handler(tab)->getSearchController();

    // Every character of 2000 pages: more than the safety limit
    pdf::PDFDocumentTextSearchOptions options;
    options.regularExpression = true;
    controller->setOptions(options);

    QElapsedTimer timer;
    timer.start();
    handler(tab)->startSearch(QStringLiteral("."));
    QVERIFY(waitForSearch(controller, 300000));
    qInfo("2000 pages: index and 100 000 results in %lld ms", timer.elapsed());
    QCOMPARE(controller->getResultCount(), int(pdf::PDFAppDocumentSearchController::MAXIMUM_RESULTS));
    QVERIFY(controller->isTruncated());

    pdf::PDFAppDocumentSearchPanel* panel = qobject_cast<pdf::PDFAppDocumentSearchPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::search()));
    QVERIFY(panel);
    QVERIFY(!panel->getNoteLabel()->isHidden());
    QVERIFY(panel->getNoteLabel()->text().contains(QLocale().toString(100000)));

    // The list is virtualized: the last row can be made current
    controller->setCurrentIndex(controller->getResultCount() - 1);
    fixture.settle();
    QCOMPARE(panel->getListView()->currentIndex().row(), controller->getResultCount() - 1);

    // A search of a text is fast once the index exists
    timer.restart();
    controller->setOptions(pdf::PDFDocumentTextSearchOptions());
    handler(tab)->startSearch(QStringLiteral("gamma 1999"));
    QVERIFY(waitForSearch(controller));
    qInfo("2000 pages: text search %lld ms", timer.elapsed());
    QCOMPARE(controller->getResultCount(), 1);
}

void AppDocumentSearchTest::closeTabDuringIndexing()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 2000));
    QVERIFY(tab);

    handler(tab)->startSearch(QStringLiteral("gamma"));
    QVERIFY(handler(tab)->getIndexer()->isBuilding());
    QCOMPARE(handler(tab)->getSearchController()->getState(), pdf::PDFAppDocumentSearchController::State::Indexing);

    // The tab goes at once, the worker ends by itself
    QElapsedTimer timer;
    timer.start();
    delete tab;
    fixture.settle();
    // valgrind runs the threads one after another: the worker delays the GUI thread
    QVERIFY2(timer.elapsed() < (isValgrind() ? 60000 : 2000), qPrintable(QString::number(timer.elapsed())));

    // The application can open the document again meanwhile
    QTest::qWait(200);
    fixture.settle();
}

void AppDocumentSearchTest::closeTabDuringSearch()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 2000));
    QVERIFY(tab);
    QVERIFY(!handler(tab)->getIndexer()->requestIndex());
    QTRY_VERIFY_WITH_TIMEOUT(handler(tab)->getIndexer()->getIndex() != nullptr, 120000);

    // A search, which takes long (every character, regular expression)
    pdf::PDFDocumentTextSearchOptions options;
    options.regularExpression = true;
    handler(tab)->getSearchController()->setOptions(options);
    handler(tab)->startSearch(QStringLiteral("(.)"));
    QCOMPARE(handler(tab)->getSearchController()->getState(), pdf::PDFAppDocumentSearchController::State::Searching);

    // A new search cancels the running one
    handler(tab)->startSearch(QStringLiteral("(..)"));
    QCOMPARE(handler(tab)->getSearchController()->getState(), pdf::PDFAppDocumentSearchController::State::Searching);

    QElapsedTimer timer;
    timer.start();
    delete tab;
    fixture.settle();
    // valgrind runs the threads one after another: the worker delays the GUI thread
    QVERIFY2(timer.elapsed() < (isValgrind() ? 60000 : 2000), qPrintable(QString::number(timer.elapsed())));
    QTest::qWait(200);
    fixture.settle();
}

void AppDocumentSearchTest::deleteWindowDuringSearch()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 2000));
    QVERIFY(tab);
    handler(tab)->startSearch(QStringLiteral("alpha"));
    QVERIFY(handler(tab)->getIndexer()->isBuilding());

    // The window with the tab, the view and the panels goes first, the context after it
    delete fixture.window.data();
    fixture.settle();
    QVERIFY(!fixture.window);
}

void AppDocumentSearchTest::deleteFromOwnSignals_data()
{
    QTest::addColumn<QString>("signal");
    QTest::addColumn<bool>("deleteWindow");
    for (const char* name : { "indexBuilding", "indexReady", "indexOperationStarted", "indexOperationFinished", "indexCanceledByChange", "searchStarted", "searchFinished", "searchText", "currentIndex", "verificationStarted",
                              "verificationFinished", "speechStarted", "speechStopped", "speechPaused", "speechResumed" })
    {
        QTest::newRow(QStringLiteral("%1-tab").arg(QLatin1String(name)).toLatin1().constData()) << QString::fromLatin1(name) << false;
        QTest::newRow(QStringLiteral("%1-window").arg(QLatin1String(name)).toLatin1().constData()) << QString::fromLatin1(name) << true;
    }
}

void AppDocumentSearchTest::deleteFromOwnSignals()
{
    // The application deletes the tab (or the window) synchronously from a signal
    // of the indexer, of the search, of the signature verification or of the
    // speech, also while the command dispatch is on the stack: the code of package
    // D must not use anything of the tab after the signal (valgrind finds the reads)
    QFETCH(QString, signal);
    QFETCH(bool, deleteWindow);

    pdftest::AppDocumentFixture fixture;
    const bool isSigned = signal.startsWith(QStringLiteral("verification"));
    const QString file = isSigned ? pdftest::AppDocumentSamples::signedDocument(fixture.folder())
                                  : pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Own signals.pdf"), 4);
    QPointer<pdf::PDFAppMainWindow> window = fixture.window;
    QPointer<pdf::PDFAppDocumentTab> tab = openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentSearchCommandHandler* searchHandler = handler(tab);
    QVERIFY(searchHandler);
    pdf::PDFAppDocumentSearchController* controller = searchHandler->getSearchController();
    pdf::PDFAppDocumentTextIndexer* indexer = searchHandler->getIndexer();
    pdf::PDFAppDocumentSignatureVerifier* verifier = searchHandler->getSignatureVerifier();

    QObject receiver;
    bool isDeleted = false;
    auto deleteNow = [&isDeleted, &tab, &window, deleteWindow]()
    {
        if (isDeleted)
        {
            return;
        }
        isDeleted = true;
        if (deleteWindow)
        {
            delete window.data();
        }
        else
        {
            delete tab.data();
        }
    };
    auto trigger = [&window](const QString& commandId)
    {
        QAction* action = window ? window->getCommandAction(commandId) : nullptr;
        if (!action || !action->isEnabled())
        {
            return false;
        }
        action->trigger();
        return true;
    };

    if (signal == QStringLiteral("indexBuilding"))
    {
        connect(indexer, &pdf::PDFAppDocumentTextIndexer::buildingChanged, &receiver, deleteNow);
        searchHandler->startSearch(QStringLiteral("alpha"));
    }
    else if (signal == QStringLiteral("indexReady"))
    {
        connect(indexer, &pdf::PDFAppDocumentTextIndexer::indexReady, &receiver, deleteNow);
        searchHandler->startSearch(QStringLiteral("alpha"));
    }
    else if (signal == QStringLiteral("indexOperationStarted") || signal == QStringLiteral("indexOperationFinished"))
    {
        // The operation of the build (the progress of the tab) begins and ends
        pdf::PDFAppDocument* document = tab->getDocument();
        const bool isStarted = signal == QStringLiteral("indexOperationStarted");
        connect(document, &pdf::PDFAppDocument::operationsChanged, &receiver, [document, isStarted, deleteNow]()
        {
            if (isStarted || document->getOperations().empty())
            {
                deleteNow();
            }
        });
        searchHandler->startSearch(QStringLiteral("alpha"));
    }
    else if (signal == QStringLiteral("indexCanceledByChange"))
    {
        // A change of the page contents stops the running build and starts a new one
        // (a large document, so the build still runs at the change)
        tab->getDocument()->disconnect(&receiver);
        QPointer<pdf::PDFAppDocumentTab> large = openReady(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 2000));
        QVERIFY(large);
        tab = large;
        searchHandler = handler(tab);
        indexer = searchHandler->getIndexer();
        searchHandler->startSearch(QStringLiteral("alpha"));
        QVERIFY(indexer->isBuilding());
        connect(indexer, &pdf::PDFAppDocumentTextIndexer::buildingChanged, &receiver, deleteNow);
        connect(tab->getDocument(), &pdf::PDFAppDocument::operationsChanged, &receiver, deleteNow);
        QVERIFY(trigger(pdf::PDFAppCommands::rotatePagesRight()));
    }
    else if (signal == QStringLiteral("searchStarted"))
    {
        // From the command of the window (Find with a text in the field)
        connect(controller, &pdf::PDFAppDocumentSearchController::stateChanged, &receiver, deleteNow);
        searchHandler->getSearchField()->setText(QStringLiteral("alpha"));
        QVERIFY(QMetaObject::invokeMethod(searchHandler->getSearchField(), "returnPressed"));
    }
    else if (signal == QStringLiteral("searchFinished"))
    {
        connect(controller, &pdf::PDFAppDocumentSearchController::stateChanged, &receiver, [controller, deleteNow]()
        {
            if (controller->getState() == pdf::PDFAppDocumentSearchController::State::Finished)
            {
                deleteNow();
            }
        });
        searchHandler->startSearch(QStringLiteral("alpha"));
    }
    else if (signal == QStringLiteral("searchText"))
    {
        connect(searchHandler, &pdf::PDFAppDocumentSearchCommandHandler::searchTextChanged, &receiver, deleteNow);
        searchHandler->startSearch(QStringLiteral("alpha"));
    }
    else if (signal == QStringLiteral("currentIndex"))
    {
        searchHandler->startSearch(QStringLiteral("alpha"));
        QVERIFY(waitForSearch(controller));
        QVERIFY(controller->getResultCount() > 1);
        connect(controller, &pdf::PDFAppDocumentSearchController::currentIndexChanged, &receiver, deleteNow);
        QVERIFY(trigger(pdf::PDFAppCommands::findNext()));
    }
    else if (isSigned)
    {
        QTRY_VERIFY_WITH_TIMEOUT(verifier->getVerification() && !verifier->isVerifying(), isValgrind() ? 600000 : 30000);
        connect(verifier, &pdf::PDFAppDocumentSignatureVerifier::verificationChanged, &receiver, [verifier, signal, deleteNow]()
        {
            if (signal == QStringLiteral("verificationStarted") || !verifier->isVerifying())
            {
                deleteNow();
            }
        });
        verifier->verify();
    }
    else if (signal == QStringLiteral("speechPaused") || signal == QStringLiteral("speechResumed"))
    {
        QVERIFY(trigger(pdf::PDFAppCommands::readAloud()));
        pdf::PDFAppDocumentSpeechController* speech = searchHandler->getSpeechController(false);
        QVERIFY(speech);
        QTRY_VERIFY_WITH_TIMEOUT(FakeSpeechEngine::s_last && !FakeSpeechEngine::s_last->said.isEmpty(), isValgrind() ? 600000 : 30000);
        if (signal == QStringLiteral("speechResumed"))
        {
            speech->pause();
            QCOMPARE(speech->getState(), pdf::PDFAppDocumentSpeechController::State::Paused);
        }
        connect(speech, &pdf::PDFAppDocumentSpeechController::stateChanged, &receiver, deleteNow);
        if (signal == QStringLiteral("speechResumed"))
        {
            speech->play();
        }
        else
        {
            speech->pause();
        }
    }
    else if (signal == QStringLiteral("speechStarted") || signal == QStringLiteral("speechStopped"))
    {
        if (signal == QStringLiteral("speechStopped"))
        {
            QVERIFY(trigger(pdf::PDFAppCommands::readAloud()));
            pdf::PDFAppDocumentSpeechController* speech = searchHandler->getSpeechController(false);
            QVERIFY(speech);
            QTRY_VERIFY_WITH_TIMEOUT(FakeSpeechEngine::s_last && !FakeSpeechEngine::s_last->said.isEmpty(), isValgrind() ? 600000 : 30000);
            connect(speech, &pdf::PDFAppDocumentSpeechController::stateChanged, &receiver, deleteNow);
            QVERIFY(trigger(pdf::PDFAppCommands::readAloud()));
        }
        else
        {
            pdf::PDFAppDocumentSpeechController* speech = searchHandler->getSpeechController(true);
            QVERIFY(speech);
            connect(speech, &pdf::PDFAppDocumentSpeechController::stateChanged, &receiver, deleteNow);
            QVERIFY(trigger(pdf::PDFAppCommands::readAloud()));
        }
    }
    else
    {
        QFAIL(qPrintable(signal));
    }

    QTRY_VERIFY_WITH_TIMEOUT(isDeleted, isValgrind() ? 600000 : 30000);
    fixture.settle();
    QThreadPool::globalInstance()->waitForDone(isValgrind() ? 600000 : 30000);
    QTest::qWait(50);
    fixture.settle();
    QVERIFY(deleteWindow ? !window : !tab);

    // The window, which has lost the tab, stays usable
    if (!deleteWindow)
    {
        QVERIFY(window);
        QVERIFY(openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("After.pdf"), 1)));
    }
}

void AppDocumentSearchTest::closeSearch()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Close.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    // The panel shown before the search comes back
    view->showPanel(pdf::PDFAppDocumentPanelIds::speech());
    QCOMPARE(view->getCurrentPanelId(), pdf::PDFAppDocumentPanelIds::speech());
    handler(tab)->startSearch(QStringLiteral("beta"));
    QVERIFY(waitForSearch(handler(tab)->getSearchController()));
    QCOMPARE(view->getCurrentPanelId(), pdf::PDFAppDocumentPanelIds::search());

    pdf::PDFAppDocumentSearchPanel* panel = qobject_cast<pdf::PDFAppDocumentSearchPanel*>(view->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::search()));
    QVERIFY(panel);
    panel->getCloseButton()->click();
    QCOMPARE(view->getCurrentPanelId(), pdf::PDFAppDocumentPanelIds::speech());
    QCOMPARE(handler(tab)->getSearchController()->getResultCount(), 0);
    QCOMPARE(panel->getSubtitle(), QStringLiteral("Type a text into the search field"));

    // Closing a closed search does nothing; an empty text closes the search
    handler(tab)->closeSearch();
    handler(tab)->startSearch(QString());
    QCOMPARE(handler(tab)->getSearchController()->getState(), pdf::PDFAppDocumentSearchController::State::Idle);
}

void AppDocumentSearchTest::themesAndDirection()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Theme.pdf"), 2));
    QVERIFY(tab);
    handler(tab)->startSearch(QStringLiteral("beta"));
    QVERIFY(waitForSearch(handler(tab)->getSearchController()));
    tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::speech());
    tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::search());

    // Dark theme, right-to-left, the language change and a narrow window repaint
    // the panels without a problem
    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Dark, pdf::PDFThemeColors::Scheme::Light })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        QApplication::setLayoutDirection(Qt::RightToLeft);
        fixture.window->resize(720, 480);
        fixture.settle();
        QEvent languageChange(QEvent::LanguageChange);
        for (QWidget* widget : tab->getView()->getSidebar()->findChildren<pdf::PDFAppDocumentPanel*>())
        {
            QCoreApplication::sendEvent(widget, &languageChange);
        }
        QVERIFY(!tab->getView()->grab().isNull());
        QApplication::setLayoutDirection(Qt::LeftToRight);
    }
}

void AppDocumentSearchTest::signatures()
{
    pdftest::AppDocumentFixture fixture;
    QString certificate;
    const QString file = pdftest::AppDocumentSamples::signedDocument(fixture.folder(), &certificate);
    QVERIFY(!file.isEmpty());
    QVERIFY(QFileInfo::exists(certificate));

    pdf::PDFAppDocumentTab* tab = openReady(fixture, file);
    QVERIFY(tab);
    QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(2));

    // The verification runs after the document is shown, in a worker
    pdf::PDFAppDocumentSignatureVerifier* verifier = handler(tab)->getSignatureVerifier();
    QTRY_VERIFY_WITH_TIMEOUT(verifier->getVerification() && !verifier->isVerifying(), 30000);
    QVERIFY(verifier->hasSignatures());
    QCOMPARE(tab->getDocument()->getSignatureResults().size(), size_t(1));
    const pdf::PDFSignatureVerificationResult& result = tab->getDocument()->getSignatureResults().front();

    // The self-signed certificate of the test is not trusted: the signature is intact
    QCOMPARE(pdf::PDFAppDocumentSignatureVerifier::getStatus(result, false), pdf::PDFComponentPainter::SignatureStatus::Warning);
    QCOMPARE(pdf::PDFAppDocumentSignatureVerifier::getStatus(result, true), pdf::PDFComponentPainter::SignatureStatus::Invalid);

    // The canvas got the results when they arrived (the signature fields of the
    // form manager read them from the proxy), not only at the next version
    QVERIFY(tab->getView()->getProxy());
    QCOMPARE(tab->getView()->getProxy()->getDocument(), tab->getDocument()->getDocument().data());
    QCOMPARE(tab->getView()->getProxy()->getSignatureVerificationResult().size(), size_t(1));
    QCOMPARE(tab->getView()->getProxy()->getSignatureVerificationResult().front().getSignatureFieldQualifiedName(), result.getSignatureFieldQualifiedName());

    // The status item
    pdf::PDFAppSignatureStatusItem* item = tab->getView()->findChild<pdf::PDFAppSignatureStatusItem*>();
    QVERIFY(item);
    QVERIFY(!item->isHidden());
    QCOMPARE(item->text(), QStringLiteral("1 signature · 1 needs attention"));

    // The panel: the banner and the expanded card with the chain
    item->click();
    QCOMPARE(tab->getView()->getCurrentPanelId(), pdf::PDFAppDocumentPanelIds::signatures());
    pdf::PDFAppDocumentSignaturesPanel* panel = qobject_cast<pdf::PDFAppDocumentSignaturesPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::signatures()));
    QVERIFY(panel);
    QCOMPARE(panel->getTitle(), QStringLiteral("Signatures"));
    QCOMPARE(panel->getSubtitle(), QStringLiteral("1 signature"));
    QVERIFY(panel->getBanner());
    QCOMPARE(panel->getCards().size(), size_t(1));
    pdf::PDFSignatureCard* card = panel->getCards().front();
    QCOMPARE(card->getStatus(), pdf::PDFComponentPainter::SignatureStatus::Warning);
    QVERIFY(card->isExpanded());
    QVERIFY(!card->getSigner().isEmpty());
    QVERIFY(card->getMeta().startsWith(QStringLiteral("Digital signature")));
    QVERIFY(!card->getDetails().isEmpty());
    QVERIFY(!card->getCertificates().isEmpty());
    QCOMPARE(card->getCertificates().back().linkText, QStringLiteral("Add to trusted certificates"));

    // The sidebar shows the tab of the panel
    QVERIFY(tab->getView()->getSidebar()->getRegistry().findFactory(pdf::PDFAppDocumentPanelIds::signatures())->isAvailable(tab->getDocument()));

    // Summary and status texts
    pdf::PDFAppSignatureSummary summary;
    summary.signatureCount = 2;
    summary.timestampCount = 1;
    summary.warningCount = 1;
    QCOMPARE(pdf::PDFAppDocumentSignatureVerifier::getStatusText(summary, true), QStringLiteral("2 signatures · 1 needs attention"));
    summary.warningCount = 0;
    QCOMPARE(pdf::PDFAppDocumentSignatureVerifier::getStatusText(summary, true), QStringLiteral("2 signatures · valid"));
    summary.invalidCount = 2;
    QCOMPARE(pdf::PDFAppDocumentSignatureVerifier::getStatusText(summary, true), QStringLiteral("2 signatures · 2 invalid"));
    QCOMPARE(pdf::PDFAppDocumentSignatureVerifier::getStatusText(summary, false), QStringLiteral("2 signatures · not verified"));
    summary.signatureCount = 0;
    QCOMPARE(pdf::PDFAppDocumentSignatureVerifier::getStatusText(summary, false), QStringLiteral("1 timestamp · not verified"));

    // An edit of the document keeps the results of the bytes of the file
    pdf::PDFAppDocumentSnapshot snapshot = tab->getDocument()->getSnapshot();
    pdf::PDFDocumentPointer copy(new pdf::PDFDocument(*snapshot.document));
    QVERIFY(tab->getDocument()->commit(pdf::PDFModifiedDocument(copy, nullptr, pdf::PDFModifiedDocument::Annotation), QStringLiteral("Edit"), snapshot.version, pdf::PDFAppChangeSource::User));
    fixture.settle();
    QVERIFY(!verifier->isVerifying());
    QCOMPARE(tab->getDocument()->getSignatureResults().size(), size_t(1));
    QCOMPARE(tab->getView()->getProxy()->getDocument(), tab->getDocument()->getDocument().data());
    QCOMPARE(tab->getView()->getProxy()->getSignatureVerificationResult().size(), size_t(1));
}

void AppDocumentSearchTest::signaturesTrust()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
    pdf::PDFAppDocumentTab* tab = openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentSignatureVerifier* verifier = handler(tab)->getSignatureVerifier();
    QTRY_VERIFY_WITH_TIMEOUT(verifier->getVerification() && !verifier->isVerifying(), 30000);

    QVERIFY(tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::signatures()));
    pdf::PDFAppDocumentSignaturesPanel* panel = qobject_cast<pdf::PDFAppDocumentSignaturesPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::signatures()));
    QVERIFY(panel);
    pdf::PDFSignatureCard* card = panel->getCards().front();
    const int linkIndex = card->getCertificates().size() - 1;
    QVERIFY(card->getCertificateLink(linkIndex));

    // Cancel of the confirmation trusts nothing
    Q_EMIT card->certificateLinkActivated(linkIndex);
    QVERIFY(panel->getTrustDialog());

    // O18: the confirmation names the subject, the issuer and the SHA-256
    // fingerprint (16 groups of 4 hexadecimal digits)
    {
        const pdf::PDFSignatureVerificationResult& result = tab->getDocument()->getSignatureResults().front();
        const int trustable = pdf::PDFAppDocumentSignatureVerifier::getTrustableCertificateIndex(result, { });
        QVERIFY(trustable >= 0);
        const QByteArray certificate = result.getCertificateInfos()[size_t(trustable)].getCertificateData();
        pdf::PDFKeyValueList* facts = panel->getTrustDialog()->findChild<pdf::PDFKeyValueList*>(QStringLiteral("certificateFactsList"));
        QVERIFY(facts);
        QCOMPARE(facts->getRowCount(), 2);
        QCOMPARE(facts->getValue(0), QStringLiteral("PDF4QT document tab test"));
        QCOMPARE(facts->getValue(1), QStringLiteral("PDF4QT document tab test"));
        QCOMPARE(facts->getValue(1), pdf::PDFCertificateInfo::getIssuerName(certificate));
        QLabel* fingerprint = panel->getTrustDialog()->findChild<QLabel*>(QStringLiteral("fingerprint"));
        QVERIFY(fingerprint);
        const QString hex = QString::fromLatin1(QCryptographicHash::hash(certificate, QCryptographicHash::Sha256).toHex().toUpper());
        QCOMPARE(fingerprint->text().size(), 64 + 15);
        QCOMPARE(fingerprint->text().split(QLatin1Char(' ')).size(), 16);
        QCOMPARE(QString(fingerprint->text()).remove(QLatin1Char(' ')), hex);
        QVERIFY(pdf::PDFCertificateInfo::getIssuerName(QByteArray("garbage")).isEmpty());
        QVERIFY(pdf::PDFCertificateInfo::getIssuerName(QByteArray()).isEmpty());
    }
    panel->getTrustDialog()->reject();
    fixture.settle();
    QVERIFY(!panel->getTrustDialog());
    pdf::PDFTrustedCertificateRepository repository(fixture.context->getDatabase());
    QVERIFY(repository.getCertificates().empty());

    // Trust: the certificate is stored, the signature becomes valid
    Q_EMIT card->certificateLinkActivated(linkIndex);
    QPointer<pdf::PDFMessageBox> dialog = panel->getTrustDialog();
    QVERIFY(dialog);
    QAbstractButton* trust = dialog->getButtonBox()->button(QDialogButtonBox::Ok);
    QVERIFY(trust);
    QCOMPARE(trust->text(), QStringLiteral("Trust"));
    trust->click();
    fixture.settle();
    QCOMPARE(repository.getCertificates().size(), size_t(1));
    QTRY_VERIFY_WITH_TIMEOUT(!verifier->isVerifying() && !tab->getDocument()->getSignatureResults().empty() &&
                             pdf::PDFAppDocumentSignatureVerifier::getStatus(tab->getDocument()->getSignatureResults().front(), false) == pdf::PDFComponentPainter::SignatureStatus::Valid, 30000);

    pdf::PDFAppSignatureStatusItem* item = tab->getView()->findChild<pdf::PDFAppSignatureStatusItem*>();
    QTRY_COMPARE(item->text(), QStringLiteral("1 signature · valid"));
    QTRY_VERIFY(!panel->getCards().empty());
    QCOMPARE(panel->getCards().front()->getStatus(), pdf::PDFComponentPainter::SignatureStatus::Valid);
    QVERIFY(panel->getCards().front()->getCertificates().back().linkText.isEmpty());
}

void AppDocumentSearchTest::signatureCards()
{
    using Status = pdf::PDFComponentPainter::SignatureStatus;
    using Verifier = pdf::PDFAppDocumentSignatureVerifier;
    using Result = pdf::PDFSignatureVerificationResult;

    auto certificate = [](const QString& name, pdf::PDFCertificateInfo::PublicKey key, int days)
    {
        pdf::PDFCertificateInfo info;
        info.setName(pdf::PDFCertificateInfo::CommonName, name);
        info.setPublicKey(key);
        info.setKeySize(2048);
        info.setNotValidAfter(QDateTime::currentDateTimeUtc().addDays(days));
        return info;
    };

    // A valid signature with a chain of three certificates
    Result valid(pdf::PDFSignature::Type::Sig, pdf::PDFObjectReference(), QStringLiteral("Signature1"));
    valid.setFlag(Result::Certificate_OK, true);
    valid.setFlag(Result::Signature_OK, true);
    valid.validate();
    valid.addHashAlgorithm(QStringLiteral("sha512"));
    valid.setSignatureDate(QDateTime(QDate(2026, 9, 28), QTime(14, 2), QTimeZone::UTC));
    valid.setTimestampDate(QDateTime(QDate(2026, 9, 28), QTime(14, 3), QTimeZone::UTC));
    valid.addCertificateInfo(certificate(QStringLiteral("Jane Novak"), pdf::PDFCertificateInfo::KeyRSA, 500));
    valid.addCertificateInfo(certificate(QStringLiteral("Example Issuing CA 2"), pdf::PDFCertificateInfo::KeyEC, 1500));
    valid.addCertificateInfo(certificate(QStringLiteral("Example Root CA"), pdf::PDFCertificateInfo::KeyDSA, 3000));
    QCOMPARE(Verifier::getStatus(valid, true), Status::Valid);
    pdf::PDFComponentPainter::SignatureCard card = Verifier::getCard(valid, false, {});
    QCOMPARE(card.signer, QStringLiteral("Jane Novak"));
    QVERIFY2(card.meta.startsWith(QStringLiteral("Digital signature · Sep 28, 2026")), qPrintable(card.meta));
    QCOMPARE(card.summary, QStringLiteral("The document has not been changed since it was signed."));
    QCOMPARE(card.certificates.size(), 3);
    QVERIFY(card.certificates[0].info.startsWith(QStringLiteral("Signer · RSA 2048 · valid until")));
    QVERIFY(card.certificates[1].info.startsWith(QStringLiteral("Intermediate · EC 2048")));
    QVERIFY(card.certificates[2].info.startsWith(QStringLiteral("Root · DSA 2048")));
    QCOMPARE(Verifier::getTrustableCertificateIndex(valid, {}), -1);
    bool hasHash = false;
    bool hasTimestamp = false;
    for (const pdf::PDFComponentPainter::SignatureDetail& detail : card.details)
    {
        hasHash = hasHash || (detail.key == QStringLiteral("Hash algorithm") && detail.value == QStringLiteral("SHA-512"));
        hasTimestamp = hasTimestamp || (detail.key == QStringLiteral("Timestamp") && detail.value == QStringLiteral("Embedded, valid"));
    }
    QVERIFY(hasHash);
    QVERIFY(hasTimestamp);

    // Expired certificate: needs attention; ignored expiration keeps the certificate valid
    Result expired(pdf::PDFSignature::Type::Sig, pdf::PDFObjectReference(), QStringLiteral("Signature2"));
    expired.setFlag(Result::Signature_OK, true);
    expired.addCertificateExpiredError();
    expired.addCertificateInfo(certificate(QString(), pdf::PDFCertificateInfo::KeyDH, -10));
    QCOMPARE(Verifier::getStatus(expired, false), Status::Warning);
    card = Verifier::getCard(expired, false, {});
    QCOMPARE(card.summary, QStringLiteral("The signature is intact, but the certificate of the signer has expired."));
    QCOMPARE(card.signer, QStringLiteral("Unknown"));
    QCOMPARE(card.certificates.front().status, Status::Warning);
    QVERIFY(card.certificates.front().info.contains(QStringLiteral("expired on")));
    QCOMPARE(Verifier::getCard(expired, true, {}).certificates.front().status, Status::Invalid);
    Result ignored = expired;
    ignored.setFlag(Result::Error_Certificate_Expired, false);
    QCOMPARE(Verifier::getCard(ignored, false, {}).certificates.front().status, Status::Valid);

    // Not covered bytes, a timestamp not verified, another warning
    Result covered(pdf::PDFSignature::Type::Sig, pdf::PDFObjectReference(), QStringLiteral("Signature3"));
    covered.setFlag(Result::Certificate_OK, true);
    covered.setFlag(Result::Signature_OK, true);
    covered.validate();
    covered.addSignatureNotCoveredBytesWarning(10);
    covered.addSignatureTimestampNotVerifiedWarning();
    QCOMPARE(Verifier::getStatus(covered, false), Status::Warning);
    card = Verifier::getCard(covered, false, {});
    QCOMPARE(card.summary, QStringLiteral("The signature is intact, but a part of the document is not covered by it."));
    bool notVerified = false;
    bool partOfDocument = false;
    for (const pdf::PDFComponentPainter::SignatureDetail& detail : card.details)
    {
        notVerified = notVerified || detail.value == QStringLiteral("Embedded, not verified");
        partOfDocument = partOfDocument || detail.value == QStringLiteral("A part of the document");
    }
    QVERIFY(notVerified);
    QVERIFY(partOfDocument);
    Result warning(pdf::PDFSignature::Type::Sig, pdf::PDFObjectReference(), QStringLiteral("Signature4"));
    warning.setFlag(Result::Certificate_OK, true);
    warning.setFlag(Result::Signature_OK, true);
    warning.validate();
    warning.addSignatureTimestampNotVerifiedWarning();
    QCOMPARE(Verifier::getCard(warning, false, {}).summary, QStringLiteral("The signature is intact, but it could not be verified completely."));
    QCOMPARE(Verifier::getCard(warning, true, {}).summary, warning.getWarnings().front());

    // Revoked certificate, broken signature, no handler, nothing known
    Result revoked(pdf::PDFSignature::Type::Sig, pdf::PDFObjectReference(), QStringLiteral("Signature5"));
    revoked.addCertificateRevokedError();
    revoked.addCertificateInfo(certificate(QStringLiteral("Revoked signer"), pdf::PDFCertificateInfo::KeyRSA, 100));
    QCOMPARE(Verifier::getStatus(revoked, false), Status::Invalid);
    card = Verifier::getCard(revoked, false, {});
    QCOMPARE(card.summary, QStringLiteral("The certificate of the signer has been revoked."));
    QCOMPARE(card.certificates.front().info, QStringLiteral("Revoked"));
    Result broken(pdf::PDFSignature::Type::Sig, pdf::PDFObjectReference(), QStringLiteral("Signature6"));
    broken.addInvalidSignatureError();
    QCOMPARE(Verifier::getCard(broken, false, {}).summary, QStringLiteral("The document has been changed since it was signed, or the signature is damaged."));
    Result noHandler(pdf::PDFSignature::Type::Sig, pdf::PDFObjectReference(), QStringLiteral("Signature7"));
    noHandler.addNoHandlerError(QByteArray("adbe.unknown"));
    QCOMPARE(Verifier::getStatus(noHandler, false), Status::Invalid);
    QCOMPARE(Verifier::getCard(noHandler, false, {}).summary, noHandler.getErrors().front());
    Result unknown(pdf::PDFSignature::Type::Sig, pdf::PDFObjectReference(), QStringLiteral("Signature8"));
    unknown.setFlag(Result::Error_Generic, true);
    QCOMPARE(Verifier::getCard(unknown, false, {}).summary, QStringLiteral("The signature is not valid."));

    // A document timestamp
    Result timestamp(pdf::PDFSignature::Type::DocTimeStamp, pdf::PDFObjectReference(), QStringLiteral("Timestamp"));
    timestamp.setFlag(Result::Certificate_OK, true);
    timestamp.setFlag(Result::Signature_OK, true);
    timestamp.validate();
    timestamp.setTimestampDate(QDateTime(QDate(2026, 9, 28), QTime(14, 3), QTimeZone::UTC));
    timestamp.addCertificateInfo(certificate(QStringLiteral("Example TSA"), pdf::PDFCertificateInfo::KeyRSA, 100));
    card = Verifier::getCard(timestamp, false, {});
    QCOMPARE(card.signer, QStringLiteral("Document timestamp"));
    QVERIFY2(card.meta.startsWith(QStringLiteral("Example TSA · Sep 28, 2026")), qPrintable(card.meta));
    QCOMPARE(card.summary, QStringLiteral("The document existed in this form at the stated time."));

    // The summary of all of them
    const pdf::PDFAppSignatureSummary summary = Verifier::getSummary({ valid, expired, covered, revoked, timestamp }, false);
    QCOMPARE(summary.signatureCount, 4);
    QCOMPARE(summary.timestampCount, 1);
    QCOMPARE(summary.validCount, 2);
    QCOMPARE(summary.warningCount, 2);
    QCOMPARE(summary.invalidCount, 1);

    // A panel without a document
    pdf::PDFAppDocumentSignaturesPanel panel;
    QCOMPARE(panel.getId(), pdf::PDFAppDocumentPanelIds::signatures());
    QCOMPARE(panel.getSubtitle(), QStringLiteral("No signatures"));
    QCOMPARE(panel.getIconName(), QStringLiteral("pen-tool"));
    panel.setDocument(nullptr, nullptr);
    QVERIFY(panel.getCards().empty());
    QVERIFY(!panel.getBanner());
    QVERIFY(!panel.getTrustDialog());
}

void AppDocumentSearchTest::signaturesSettings()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
    pdf::PDFAppDocumentTab* tab = openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentSignatureVerifier* verifier = handler(tab)->getSignatureVerifier();
    QTRY_VERIFY_WITH_TIMEOUT(verifier->getVerification() && !verifier->isVerifying(), 30000);
    QVERIFY(verifier->getVerification()->isVerified);

    // Verification turned off: the signatures are counted, not verified
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::signaturesVerify(), false).isError());
    QTRY_VERIFY_WITH_TIMEOUT(verifier->getVerification() && !verifier->isVerifying() && !verifier->getVerification()->isVerified, 30000);
    QVERIFY(verifier->hasSignatures());
    QVERIFY(tab->getDocument()->getSignatureResults().empty());
    pdf::PDFAppSignatureStatusItem* item = tab->getView()->findChild<pdf::PDFAppSignatureStatusItem*>();
    QCOMPARE(item->text(), QStringLiteral("1 signature · not verified"));
    QVERIFY(tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::signatures()));
    pdf::PDFAppDocumentSignaturesPanel* panel = qobject_cast<pdf::PDFAppDocumentSignaturesPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::signatures()));
    QVERIFY(panel && panel->getBanner());
    QVERIFY(panel->getCards().empty());

    // Strict mode and the system store: verified again
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::signaturesVerify(), true).isError());
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::signaturesStrict(), true).isError());
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::signaturesUseSystemStore(), false).isError());
    QTRY_VERIFY_WITH_TIMEOUT(verifier->getVerification() && !verifier->isVerifying() && verifier->getVerification()->isVerified &&
                             verifier->getVerification()->isStrict, 30000);
    QTRY_COMPARE(item->text(), QStringLiteral("1 signature · 1 invalid"));

    // The settings read for the worker
    const pdf::PDFAppSignatureSettings settings = pdf::PDFAppDocumentSignatureVerifier::readSettings(fixture.context.get());
    QVERIFY(settings.verify);
    QVERIFY(settings.strict);
    QVERIFY(!settings.useSystemStore);
    QVERIFY(!settings.ignoreExpiration);
    QVERIFY(pdf::PDFAppDocumentSignatureVerifier::readSettings(nullptr).verify);

    // The worker function: a null document, verification without a store
    QVERIFY(pdf::PDFAppDocumentSignatureVerifier::runVerification(nullptr, QByteArray(), settings, nullptr)->results.empty());
}

void AppDocumentSearchTest::unsignedDocument()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Unsigned.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentSignatureVerifier* verifier = handler(tab)->getSignatureVerifier();
    QTRY_VERIFY_WITH_TIMEOUT(verifier->getVerification() && !verifier->isVerifying(), 30000);
    QVERIFY(!verifier->hasSignatures());

    pdf::PDFAppSignatureStatusItem* item = tab->getView()->findChild<pdf::PDFAppSignatureStatusItem*>();
    QVERIFY(item);
    QVERIFY(item->isHidden());
    QVERIFY(!tab->getView()->getSidebar()->getRegistry().findFactory(pdf::PDFAppDocumentPanelIds::signatures())->isAvailable(tab->getDocument()));
    QVERIFY(!tab->getView()->getSidebar()->getRegistry().findFactory(pdf::PDFAppDocumentPanelIds::signatures())->isAvailable(nullptr));

    // A form field is not a signature
    pdf::PDFAppDocumentTab* form = openReady(fixture, pdftest::AppDocumentSamples::form(fixture.folder()));
    QVERIFY(form);
    QTRY_VERIFY_WITH_TIMEOUT(handler(form)->getSignatureVerifier()->getVerification() != nullptr, 30000);
    QVERIFY(!handler(form)->getSignatureVerifier()->hasSignatures());
}

void AppDocumentSearchTest::trustedRepository()
{
    pdftest::AppDocumentFixture fixture;
    QString certificatePath;
    QVERIFY(!pdftest::AppDocumentSamples::signedDocument(fixture.folder(), &certificatePath).isEmpty());

    pdf::PDFTrustedCertificateRepository repository(fixture.context->getDatabase());
    repository.setCurrentTime(1000);
    QVERIFY(repository.getCertificates().empty());

    // Not a certificate, empty, too large
    QVERIFY(repository.addCertificate(QByteArray("garbage")).isError());
    QVERIFY(repository.addCertificate(QByteArray()).isError());
    QVERIFY(repository.addCertificate(QByteArray(pdf::PDFTrustedCertificateRepository::MAXIMUM_CERTIFICATE_SIZE + 1, 'x')).isError());
    QVERIFY(!pdf::PDFTrustedCertificateRepository::isValidCertificate(QByteArray("x")));

    // A real certificate (of the signed sample), once
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::signedDocument(fixture.folder()));
    QVERIFY(tab);
    QTRY_VERIFY_WITH_TIMEOUT(!tab->getDocument()->getSignatureResults().empty(), 30000);
    const QByteArray data = tab->getDocument()->getSignatureResults().front().getCertificateInfos().front().getCertificateData();
    QVERIFY(pdf::PDFTrustedCertificateRepository::isValidCertificate(data));
    QVERIFY(!repository.addCertificate(data).isError());
    QVERIFY(!repository.addCertificate(data).isError());
    std::vector<pdf::PDFTrustedCertificate> certificates = repository.getCertificates();
    QCOMPARE(certificates.size(), size_t(1));
    QCOMPARE(certificates.front().added, qint64(1000));
    QVERIFY(repository.contains(data));

    // Hostile rows written by another version: skipped
    pdf::PDFDatabaseConnection* connection = fixture.context->getDatabase()->getConnection();
    {
        pdf::PDFDatabaseStatement statement(connection, "INSERT INTO trusted_certificate(data, added) VALUES (?1, ?2)");
        statement.bindBlob(1, QByteArray("not a certificate"));
        statement.bindInt64(2, -5);
        QVERIFY(statement.execute());
    }
    {
        pdf::PDFDatabaseStatement statement(connection, "INSERT INTO trusted_certificate(data, added) VALUES ('text', 'x')");
        QVERIFY(statement.execute());
    }
    certificates = repository.getCertificates();
    QCOMPARE(certificates.size(), size_t(1));

    QVERIFY(!repository.removeCertificate(certificates.front().id).isError());
    QVERIFY(!repository.contains(data));
    QVERIFY(!repository.removeCertificate(123456).isError());

    // Without a database
    pdf::PDFTrustedCertificateRepository none(nullptr);
    QVERIFY(none.getCertificates().empty());
    QVERIFY(none.addCertificate(data).isError());
}

void AppDocumentSearchTest::closeTabDuringVerification()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab);

    // The tab goes, whether the verification runs or not
    delete tab;
    fixture.settle();
    QTest::qWait(100);
    fixture.settle();

    tab = openReady(fixture, file);
    QVERIFY(tab);
    handler(tab)->getSignatureVerifier()->verify();
    QVERIFY(handler(tab)->getSignatureVerifier()->isVerifying());
    delete fixture.window.data();
    fixture.settle();
}

void AppDocumentSearchTest::trustDialogLifetime()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
    pdf::PDFAppDocumentTab* tab = openReady(fixture, file);
    QVERIFY(tab);
    QTRY_VERIFY_WITH_TIMEOUT(handler(tab)->getSignatureVerifier()->getVerification() && !handler(tab)->getSignatureVerifier()->isVerifying(), 30000);
    QVERIFY(tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::signatures()));
    pdf::PDFAppDocumentSignaturesPanel* panel = qobject_cast<pdf::PDFAppDocumentSignaturesPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::signatures()));
    QVERIFY(panel);

    // The tab is closed while the confirmation is open: the dialog goes with it
    pdf::PDFSignatureCard* card = panel->getCards().front();
    Q_EMIT card->certificateLinkActivated(card->getCertificates().size() - 1);
    QPointer<pdf::PDFMessageBox> dialog = panel->getTrustDialog();
    QVERIFY(dialog);
    delete tab;
    fixture.settle();
    QVERIFY(!dialog);

    // A second link while the dialog is open is ignored; the verification ends
    // while the dialog is open (the cards are rebuilt)
    tab = openReady(fixture, file);
    QVERIFY(tab);
    QTRY_VERIFY_WITH_TIMEOUT(handler(tab)->getSignatureVerifier()->getVerification() && !handler(tab)->getSignatureVerifier()->isVerifying(), 30000);
    QVERIFY(tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::signatures()));
    panel = qobject_cast<pdf::PDFAppDocumentSignaturesPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::signatures()));
    card = panel->getCards().front();
    Q_EMIT card->certificateLinkActivated(card->getCertificates().size() - 1);
    dialog = panel->getTrustDialog();
    QVERIFY(dialog);
    Q_EMIT card->certificateLinkActivated(card->getCertificates().size() - 1);
    QCOMPARE(panel->getTrustDialog(), dialog.data());
    handler(tab)->getSignatureVerifier()->verify();
    QTRY_VERIFY_WITH_TIMEOUT(!handler(tab)->getSignatureVerifier()->isVerifying(), 30000);
    QVERIFY(dialog);
    dialog->getButtonBox()->button(QDialogButtonBox::Ok)->click();
    fixture.settle();
    QTRY_VERIFY_WITH_TIMEOUT(!handler(tab)->getSignatureVerifier()->isVerifying(), 30000);
    QCOMPARE(pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).getCertificates().size(), size_t(1));

    // The window goes while a dialog is open
    QVERIFY(!panel->getCards().empty());
    card = panel->getCards().front();
    delete fixture.window.data();
    fixture.settle();
}

void AppDocumentSearchTest::speechPanel()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Speech.pdf"), 2));
    QVERIFY(tab);

    // The speech is created, when the panel is shown first
    QVERIFY(!handler(tab)->getSpeechController(false));
    QVERIFY(tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::speech()));
    pdf::PDFAppDocumentSpeechPanel* panel = qobject_cast<pdf::PDFAppDocumentSpeechPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::speech()));
    QVERIFY(panel);
    QVERIFY(handler(tab)->getSpeechController(false));
    QCOMPARE(panel->getTitle(), QStringLiteral("Speech"));
    QCOMPARE(panel->getSubtitle(), QStringLiteral("Read the document aloud"));
    QCOMPARE(panel->getIconName(), QStringLiteral("volume-2"));

    // The locales and voices of the engine
    QCOMPARE(panel->getLocaleComboBox()->count(), 2);
    QCOMPARE(panel->getLocaleComboBox()->currentText(), QStringLiteral("English (United States)"));
    QCOMPARE(panel->getVoiceComboBox()->count(), 2);
    QVERIFY(panel->getErrorLabel()->isHidden());
    QVERIFY(panel->getPlayButton()->isEnabled());
    QVERIFY(!panel->getPauseButton()->isEnabled());
    QVERIFY(!panel->getStopButton()->isEnabled());
    QVERIFY(panel->getFollowButton()->isChecked());
    QCOMPARE(panel->getPlayButton()->text(), QStringLiteral("Play"));

    // speech-sync of 195:2499 has icon/locate (as thumbnail-sync)
    const QSize iconSize = panel->getFollowButton()->iconSize();
    QCOMPARE(panel->getFollowButton()->icon().pixmap(iconSize).toImage(), pdf::PDFThemeIcons::getIcon(QStringLiteral("locate")).pixmap(iconSize).toImage());

    // The values of the sliders are the settings (default rate 0, volume 1)
    QCOMPARE(panel->getRateSlider()->value(), 0);
    QCOMPARE(panel->getVolumeSlider()->value(), 100);
    QCOMPARE(pdf::PDFAppDocumentSpeechController::getLocaleText(QLocale(QLocale::German, QLocale::Germany)), QStringLiteral("Deutsch (Deutschland) – German (Germany)"));
}

void AppDocumentSearchTest::speechReading()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Reading.pdf"), 3));
    QVERIFY(tab);
    QVERIFY(tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::speech()));
    pdf::PDFAppDocumentSpeechPanel* panel = qobject_cast<pdf::PDFAppDocumentSpeechPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::speech()));
    pdf::PDFAppDocumentSpeechController* speech = handler(tab)->getSpeechController(false);
    QVERIFY(panel && speech);
    FakeSpeechEngine* engine = FakeSpeechEngine::s_last;
    QVERIFY(engine);

    // Play: the index is built, the first block of the current page is said
    panel->getPlayButton()->click();
    QTRY_VERIFY_WITH_TIMEOUT(!engine->said.isEmpty(), 30000);
    QCOMPARE(speech->getState(), pdf::PDFAppDocumentSpeechController::State::Reading);
    QCOMPARE(speech->getCurrentPage(), pdf::PDFInteger(0));
    QVERIFY(engine->said.front().contains(QStringLiteral("Page 1")));
    QVERIFY(!panel->getNowReadingLabel()->isHidden());
    QCOMPARE(panel->getNowReadingLabel()->text(), QStringLiteral("Now reading · page 1"));
    QVERIFY(tab->isCommandChecked(pdf::PDFAppCommands::readAloud()));

    // now-reading text of 195:2499: Regular 12 with the lines 18 px apart
    {
        pdf::PDFLabel* textLabel = panel->getTextLabel();
        QVERIFY(textLabel && !textLabel->text().isEmpty());
        const int height = textLabel->heightForWidth(qMax(textLabel->width(), 100));
        QVERIFY2(height >= 18 && height % 18 == 0, qPrintable(QString::number(height)));
    }

    // A spoken word moves the border of the spoken part
    Q_EMIT engine->wordStarted(0, 4);
    QCOMPARE(speech->getSpokenLength(), qsizetype(4));
    Q_EMIT engine->wordStarted(-1, 4);
    Q_EMIT engine->wordStarted(100000, 4);
    QCOMPARE(speech->getSpokenLength(), qsizetype(4));

    // Pause and resume
    panel->getPauseButton()->click();
    QCOMPARE(speech->getState(), pdf::PDFAppDocumentSpeechController::State::Paused);
    QCOMPARE(panel->getPlayButton()->text(), QStringLiteral("Resume"));
    panel->getPlayButton()->click();
    QCOMPARE(speech->getState(), pdf::PDFAppDocumentSpeechController::State::Reading);

    // The blocks one after another, the pages follow
    int guard = 0;
    while (speech->getCurrentPage() < 1 && guard++ < 50)
    {
        engine->finish();
    }
    QCOMPARE(speech->getCurrentPage(), pdf::PDFInteger(1));
    QVERIFY(engine->said.last().contains(QStringLiteral("Page 2")) || engine->said.last().contains(QStringLiteral("2")));
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(1));

    // Without following, the view stays
    panel->getFollowButton()->click();
    QVERIFY(!speech->isFollowingReading());
    tab->getView()->goToPage(0);
    while (speech->getCurrentPage() < 2 && guard++ < 100)
    {
        engine->finish();
    }
    QCOMPARE(speech->getCurrentPage(), pdf::PDFInteger(2));
    fixture.settle();
    QCOMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(0));

    // The end of the document stops the reading
    while (speech->isReading() && guard++ < 150)
    {
        engine->finish();
    }
    QVERIFY(!speech->isReading());
    QVERIFY(panel->getNowReadingLabel()->parentWidget()->isHidden());

    // Stop in the middle
    panel->getPlayButton()->click();
    QVERIFY(speech->isReading());
    panel->getStopButton()->click();
    QCOMPARE(speech->getState(), pdf::PDFAppDocumentSpeechController::State::Stopped);
    QVERIFY(speech->getCurrentText().isEmpty());

    // New page contents stop the reading
    speech->play();
    QVERIFY(speech->isReading());
    pdf::PDFAppDocumentSnapshot snapshot = tab->getDocument()->getSnapshot();
    pdf::PDFDocumentPointer copy(new pdf::PDFDocument(*snapshot.document));
    QVERIFY(tab->getDocument()->commit(pdf::PDFModifiedDocument(copy, nullptr, pdf::PDFModifiedDocument::PageContents), QStringLiteral("Contents"), snapshot.version, pdf::PDFAppChangeSource::User));
    QVERIFY(!speech->isReading());
}

void AppDocumentSearchTest::speechSettings()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Settings.pdf"), 1));
    QVERIFY(tab);
    QVERIFY(tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::speech()));
    pdf::PDFAppDocumentSpeechPanel* panel = qobject_cast<pdf::PDFAppDocumentSpeechPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::speech()));
    QVERIFY(panel);
    FakeSpeechEngine* engine = FakeSpeechEngine::s_last;
    QVERIFY(engine);
    const pdf::PDFSettingsRepository settings = fixture.context->getSettings();

    // The sliders write the settings, the engine takes them
    panel->getRateSlider()->setValue(50);
    QCOMPARE(settings.getReal(pdf::PDFSettingKeys::speechRate()), 0.5);
    QCOMPARE(engine->rate, 0.5);
    panel->getPitchSlider()->setValue(-25);
    QCOMPARE(settings.getReal(pdf::PDFSettingKeys::speechPitch()), -0.25);
    panel->getVolumeSlider()->setValue(40);
    QCOMPARE(settings.getReal(pdf::PDFSettingKeys::speechVolume()), 0.4);
    QCOMPARE(engine->volume, 0.4);

    // The locale and the voice
    panel->getLocaleComboBox()->setCurrentIndex(1);
    Q_EMIT panel->getLocaleComboBox()->activated(1);
    QCOMPARE(settings.getString(pdf::PDFSettingKeys::speechLocale()), QStringLiteral("de_DE"));
    QCOMPARE(engine->locale.language(), QLocale::German);
    QCOMPARE(panel->getVoiceComboBox()->count(), 1);
    Q_EMIT panel->getVoiceComboBox()->activated(0);
    QCOMPARE(settings.getString(pdf::PDFSettingKeys::speechVoice()), QStringLiteral("Hedda"));
    QCOMPARE(engine->voice, QStringLiteral("Hedda"));

    // A change of the settings elsewhere (Settings › Speech) updates the panel
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::speechRate(), -1.0).isError());
    QTRY_COMPARE(panel->getRateSlider()->value(), -100);
    QTRY_COMPARE(engine->rate, -1.0);

    // A new engine of the setting speech.engine
    QPointer<FakeSpeechEngine> oldEngine = engine;
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::speechEngine(), QStringLiteral("other")).isError());
    QTRY_VERIFY(FakeSpeechEngine::s_last != oldEngine.data() && FakeSpeechEngine::s_last);
    QCOMPARE(FakeSpeechEngine::s_last->engineName, QStringLiteral("other"));
    fixture.settle();
    QVERIFY(!oldEngine);
}

void AppDocumentSearchTest::speechCommand()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Command.pdf"), 1));
    QVERIFY(tab);

    // Read aloud without the panel creates the speech and reads
    QAction* action = fixture.window->getCommandAction(pdf::PDFAppCommands::readAloud());
    QVERIFY(action);
    action->trigger();
    pdf::PDFAppDocumentSpeechController* speech = handler(tab)->getSpeechController(false);
    QVERIFY(speech);
    QTRY_VERIFY_WITH_TIMEOUT(FakeSpeechEngine::s_last && !FakeSpeechEngine::s_last->said.isEmpty(), 30000);
    QVERIFY(tab->isCommandChecked(pdf::PDFAppCommands::readAloud()));
    QTRY_VERIFY(action->isChecked());

    // Again: stop
    action->trigger();
    QVERIFY(!speech->isReading());
    QVERIFY(!tab->isCommandChecked(pdf::PDFAppCommands::readAloud()));
}

void AppDocumentSearchTest::speechError()
{
    FakeSpeechEngine::s_isBroken = true;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Error.pdf"), 1));
    QVERIFY(tab);
    QVERIFY(tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::speech()));
    pdf::PDFAppDocumentSpeechPanel* panel = qobject_cast<pdf::PDFAppDocumentSpeechPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::speech()));
    QVERIFY(panel);

    QVERIFY(!panel->getErrorLabel()->isHidden());
    QVERIFY(panel->getErrorLabel()->text().contains(QStringLiteral("No speech daemon")));
    QVERIFY(!panel->getPlayButton()->isEnabled());
    QVERIFY(!panel->getLocaleComboBox()->isEnabled());

    pdf::PDFAppDocumentSpeechController* speech = handler(tab)->getSpeechController(false);
    QVERIFY(!speech->isAvailable());
    speech->play();
    QTest::qWait(50);
    QVERIFY(FakeSpeechEngine::s_last->said.isEmpty());
}

void AppDocumentSearchTest::speechLifetime()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Lifetime.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = openReady(fixture, file);
    QVERIFY(tab);

    // The tab goes while reading
    tab->executeCommand(pdf::PDFAppCommands::readAloud());
    QTRY_VERIFY_WITH_TIMEOUT(FakeSpeechEngine::s_last && !FakeSpeechEngine::s_last->said.isEmpty(), 30000);
    delete tab;
    fixture.settle();
    QVERIFY(!FakeSpeechEngine::s_last);

    // The tab goes while the index is built for the speech
    tab = openReady(fixture, file);
    QVERIFY(tab);
    tab->executeCommand(pdf::PDFAppCommands::readAloud());
    delete tab;
    fixture.settle();

    // The engine is deleted by its owner while the panel shows it (an engine
    // replaced by a setting), the panel stays usable
    tab = openReady(fixture, file);
    QVERIFY(tab);
    QVERIFY(tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::speech()));
    delete FakeSpeechEngine::s_last;
    fixture.settle();
    pdf::PDFAppDocumentSpeechPanel* panel = qobject_cast<pdf::PDFAppDocumentSpeechPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::speech()));
    QVERIFY(panel);
    panel->getPlayButton()->click();
    QVERIFY(!panel->getErrorLabel()->isHidden());

    // The window goes while reading
    tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Lifetime2.pdf"), 2));
    QVERIFY(tab);
    tab->executeCommand(pdf::PDFAppCommands::readAloud());
    QTRY_VERIFY_WITH_TIMEOUT(FakeSpeechEngine::s_last && !FakeSpeechEngine::s_last->said.isEmpty(), 30000);
    delete fixture.window.data();
    fixture.settle();
}

void AppDocumentSearchTest::qtSpeechEngine()
{
    // The engine of QTextToSpeech: an unknown engine is the default engine of the
    // system (or an engine in the error state, when the system has none); nothing
    // is said by the test
    if (isValgrind())
    {
        // The speech-dispatcher plugin of QTextToSpeech leaves the event dispatcher of
        // its thread at the exit (a leak of Qt, not of the application); the engine is
        // tested on Windows
        QSKIP("The speech engine of the system is not tested under valgrind");
    }

    pdf::PDFAppSpeechEngine::setFactory(nullptr);
    QObject owner;
    pdf::PDFAppSpeechEngine* engine = pdf::PDFAppSpeechEngine::create(QStringLiteral("no-such-engine"), &owner);
    QVERIFY(engine);
    QVERIFY(qobject_cast<pdf::PDFAppQtSpeechEngine*>(engine));

    const pdf::PDFAppSpeechEngine::State state = engine->getState();
    if (state == pdf::PDFAppSpeechEngine::State::Error)
    {
        QVERIFY(!engine->getErrorString().isEmpty());
    }

    // Hostile values are bounded or ignored
    engine->setRate(5.0);
    engine->setPitch(-5.0);
    engine->setVolume(2.0);
    engine->setVoice(QStringLiteral("no such voice"));
    engine->setVoice(QString());
    engine->setLocale(QLocale(QLocale::Zulu, QLocale::Antarctica));
    const QList<QLocale> locales = engine->getLocales();
    if (!locales.isEmpty())
    {
        engine->setLocale(locales.front());
    }
    for (const pdf::PDFAppSpeechVoice& voice : engine->getVoices())
    {
        QVERIFY(!voice.name.isEmpty());
        QVERIFY(voice.displayText.startsWith(voice.name));
    }
    Q_UNUSED(engine->getVoice());
    Q_UNUSED(engine->getLocale());
    engine->pause();
    engine->resume();
    engine->stop();
    pdf::PDFAppSpeechEngine::setFactory(&FakeSpeechEngine::create);
}

void AppDocumentSearchTest::reviewNewQueryDropsResults()
{
    // A new text while another one is searched or shown: the old results are not
    // shown, walked by F3 / Enter nor kept in the route until the new search ends
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("New query.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentSearchCommandHandler* searchHandler = handler(tab);
    pdf::PDFAppDocumentSearchController* controller = searchHandler->getSearchController();

    searchHandler->startSearch(QStringLiteral("alpha"));
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getResultCount(), 3);
    QCOMPARE(searchHandler->getSearchText(), QStringLiteral("alpha"));

    // The new text: the old results are gone at once, the handler knows the text
    QSignalSpy stateSpy(controller, &pdf::PDFAppDocumentSearchController::stateChanged);
    searchHandler->startSearch(QStringLiteral("quick"));
    QCOMPARE(controller->getQuery(), QStringLiteral("quick"));
    QCOMPARE(controller->getResultCount(), 0);
    QCOMPARE(controller->getCurrentIndex(), -1);
    QCOMPARE(controller->getModel()->rowCount(QModelIndex()), 0);
    QCOMPARE(searchHandler->getSearchText(), QStringLiteral("quick"));
    QVERIFY(stateSpy.count() >= 1);

    // A third text while the second one is searched (the state stays Searching)
    const pdf::PDFAppDocumentSearchController::State state = controller->getState();
    stateSpy.clear();
    searchHandler->startSearch(QStringLiteral("gamma"));
    QCOMPARE(controller->getState(), state);
    QVERIFY(stateSpy.count() >= 1);
    QCOMPARE(searchHandler->getSearchText(), QStringLiteral("gamma"));
    QCOMPARE(pdf::PDFAppDocumentSearchCommandHandler::getRouteQuery(tab), QStringLiteral("gamma"));

    // Enter with the text being searched does not start it again
    QLineEdit* field = searchHandler->getSearchField();
    if (field && (controller->getState() == pdf::PDFAppDocumentSearchController::State::Searching || controller->getState() == pdf::PDFAppDocumentSearchController::State::Indexing))
    {
        QCOMPARE(field->text(), QStringLiteral("gamma"));
        QVERIFY(QMetaObject::invokeMethod(field, "returnPressed"));
        QCOMPARE(controller->getResultCount(), 0);
    }

    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getResultCount(), 3);
    for (const pdf::PDFDocumentTextSearchMatch& match : controller->getResults()->result.results)
    {
        QCOMPARE(match.matched, QStringLiteral("gamma"));
    }

    // Other options: the results of the old options are gone during the search
    pdf::PDFDocumentTextSearchOptions options = controller->getOptions();
    options.caseSensitive = !options.caseSensitive;
    controller->setOptions(options);
    QCOMPARE(controller->getResultCount(), 0);
    QVERIFY(waitForSearch(controller));
    QCOMPARE(controller->getResultCount(), 3);
}

void AppDocumentSearchTest::reviewSignatureCardsKeepExpansion()
{
    // A verification started again (a setting, a trusted certificate) shows no
    // cards meanwhile; the cards keep their expansion after it
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::signedDocument(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentSignatureVerifier* verifier = handler(tab)->getSignatureVerifier();
    QTRY_VERIFY_WITH_TIMEOUT(verifier->getVerification() && !verifier->isVerifying(), isValgrind() ? 600000 : 30000);

    QVERIFY(tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::signatures()));
    pdf::PDFAppDocumentSignaturesPanel* panel = qobject_cast<pdf::PDFAppDocumentSignaturesPanel*>(tab->getView()->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::signatures()));
    QVERIFY(panel);
    QTRY_VERIFY(!panel->getCards().empty());

    // The card needing attention is expanded by default; the user collapses it
    QVERIFY(panel->getCards().front()->isExpanded());
    panel->getCards().front()->setExpanded(false);

    verifier->verify();
    QVERIFY(verifier->isVerifying());
    QVERIFY(panel->getCards().empty());
    QTRY_VERIFY_WITH_TIMEOUT(!verifier->isVerifying() && !panel->getCards().empty(), isValgrind() ? 600000 : 30000);
    QVERIFY(!panel->getCards().front()->isExpanded());

    // ... and expands it again
    panel->getCards().front()->setExpanded(true);
    verifier->verify();
    QTRY_VERIFY_WITH_TIMEOUT(!verifier->isVerifying() && !panel->getCards().empty(), isValgrind() ? 600000 : 30000);
    QVERIFY(panel->getCards().front()->isExpanded());
}

void AppDocumentSearchTest::reviewTrustedRepositoryLimit()
{
    // Hostile rows of another version must not hide a certificate the user trusts
    // (the limit of the rows read used to be applied before the rows were checked)
    // and a trusted certificate is never added twice
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::signedDocument(fixture.folder()));
    QVERIFY(tab);
    QTRY_VERIFY_WITH_TIMEOUT(!tab->getDocument()->getSignatureResults().empty(), isValgrind() ? 600000 : 30000);
    const QByteArray data = tab->getDocument()->getSignatureResults().front().getCertificateInfos().front().getCertificateData();
    QVERIFY(pdf::PDFTrustedCertificateRepository::isValidCertificate(data));

    pdf::PDFDatabaseConnection* connection = fixture.context->getDatabase()->getConnection();
    auto insertRows = [connection](int count, const char* sql)
    {
        pdf::PDFDatabaseTransaction transaction(connection);
        for (int i = 0; i < count; ++i)
        {
            pdf::PDFDatabaseStatement statement(connection, sql);
            if (!statement.execute())
            {
                return false;
            }
        }
        return transaction.commit();
    };
    auto countRows = [connection](const QByteArray& value)
    {
        pdf::PDFDatabaseStatement statement(connection, "SELECT count(*) FROM trusted_certificate WHERE data = ?1");
        statement.bindBlob(1, value);
        return statement.next() ? statement.getInt64(0) : -1;
    };

    // Rows of other types and empty values first, then garbage blobs below the limit
    QVERIFY(insertRows(1500, "INSERT INTO trusted_certificate(data, added) VALUES ('text', 0)"));
    QVERIFY(insertRows(20, "INSERT INTO trusted_certificate(data, added) VALUES (x'', 0)"));
    QVERIFY(insertRows(pdf::PDFTrustedCertificateRepository::MAXIMUM_CERTIFICATE_COUNT - 1, "INSERT INTO trusted_certificate(data, added) VALUES (x'0102', 0)"));

    pdf::PDFTrustedCertificateRepository repository(fixture.context->getDatabase());
    QVERIFY(!repository.contains(data));
    QVERIFY(!repository.addCertificate(data).isError());
    QVERIFY(repository.contains(data));
    QVERIFY(!repository.addCertificate(data).isError());
    QCOMPARE(countRows(data), qint64(1));

    // The verification reads it
    const std::vector<pdf::PDFTrustedCertificate> certificates = repository.getCertificates();
    QCOMPARE(certificates.size(), size_t(1));
    QCOMPARE(certificates.front().data, data);
    QCOMPARE(pdf::PDFAppDocumentSignatureVerifier::readSettings(fixture.context.get()).trustedCertificates.size(), size_t(1));

    // Over the limit: refused with an error instead of a row, which nobody reads
    QVERIFY(!repository.removeCertificate(certificates.front().id).isError());
    QVERIFY(insertRows(5, "INSERT INTO trusted_certificate(data, added) VALUES (x'0102', 0)"));
    const pdf::PDFDatabaseError error = repository.addCertificate(data);
    QVERIFY(error.isError());
    QVERIFY(!error.getMessage().isEmpty());
    QVERIFY(!repository.contains(data));
    QCOMPARE(countRows(data), qint64(0));
}

void AppDocumentSearchTest::reviewSpeechLongBlock()
{
    // A block longer than the limit of one said text is said in parts: nothing of
    // it is left out (it used to be cut after 4000 characters)
    pdftest::AppDocumentFixture fixture;
    QStringList words;
    for (int i = 0; i < 1500; ++i)
    {
        words << QStringLiteral("w%1").arg(i);
    }
    const QString text = words.join(QLatin1Char(' '));
    QVERIFY(text.size() > 6000);

    const QString file = QDir(fixture.folder()).absoluteFilePath(QStringLiteral("Long block.pdf"));
    {
        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 14000, 100));
        pdf::PDFPageContentStreamBuilder content(&builder);
        QPainter* painter = content.begin(page);
        QVERIFY(painter);
        QFont font = painter->font();
        font.setPixelSize(2);
        painter->setFont(font);
        painter->drawText(QPointF(10, 50), text);
        content.end(painter);
        pdf::PDFDocument document = builder.build();
        QVERIFY(bool(pdf::PDFDocumentWriter(nullptr).write(file, &document, false)));
    }

    pdf::PDFAppDocumentTab* tab = openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentSpeechController* speech = handler(tab)->getSpeechController(true);
    QVERIFY(speech);
    FakeSpeechEngine* engine = FakeSpeechEngine::s_last;
    QVERIFY(engine);

    speech->play();
    QTRY_VERIFY_WITH_TIMEOUT(!engine->said.isEmpty(), isValgrind() ? 600000 : 30000);
    int guard = 0;
    while (speech->isReading() && guard++ < 50)
    {
        engine->finish();
    }
    QVERIFY(!speech->isReading());

    // The page is one block of one line (longer than one said text)
    std::shared_ptr<const pdf::PDFDocumentTextIndex> index = handler(tab)->getIndexer()->getIndex();
    QVERIFY(index);
    const QString pageText = index->getPageText(0).trimmed();
    QVERIFY(pageText.size() > 6000);
    QVERIFY(!pageText.contains(QChar(0x000A)));

    QVERIFY(engine->said.size() >= 2);
    QString spoken;
    for (const QString& part : engine->said)
    {
        QVERIFY(part.size() <= 4000);
        QVERIFY(!part.startsWith(QLatin1Char(' ')) && !part.endsWith(QLatin1Char(' ')));
        spoken += (spoken.isEmpty() ? QString() : QStringLiteral(" ")) + part;
    }

    // The words of the page in their order (the text layout may join or split
    // the white space differently)
    QCOMPARE(spoken.split(QLatin1Char(' '), Qt::SkipEmptyParts), words);
}

void AppDocumentSearchTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: the window with the panels of package D in
    // both themes at 1x and 2x, also 720 x 480 and right-to-left (the comparison
    // with Figma 198:4520 search, 195:2959 signatures, 195:2499 speech)
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    if (folder.isEmpty())
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }
    QDir().mkpath(folder);

    auto render = [](QWidget* widget, qreal ratio)
    {
        QImage image(QSize(qCeil(widget->width() * ratio), qCeil(widget->height() * ratio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(ratio);
        image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
        widget->render(&image, QPoint(), QRegion(), QWidget::RenderFlags(QWidget::DrawChildren | QWidget::DrawWindowBackground));
        return image;
    };

    struct Variant
    {
        QSize size;
        Qt::LayoutDirection direction;
        QString name;
    };

    for (const Variant& variant : { Variant{ QSize(1440, 960), Qt::LeftToRight, QStringLiteral("") },
                                    Variant{ QSize(720, 480), Qt::LeftToRight, QStringLiteral("-small") },
                                    Variant{ QSize(1440, 960), Qt::RightToLeft, QStringLiteral("-rtl") } })
    {
        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            QApplication::setLayoutDirection(variant.direction);
            const QString theme = scheme == pdf::PDFThemeColors::Scheme::Dark ? QStringLiteral("dark") : QStringLiteral("light");

            // The context applies the color scheme of its settings: the theme is set after it
            pdftest::AppDocumentFixture fixture(variant.size);
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::signedDocument(fixture.folder()));
            QVERIFY(tab);
            QTRY_VERIFY_WITH_TIMEOUT(handler(tab)->getSignatureVerifier()->getVerification() && !handler(tab)->getSignatureVerifier()->isVerifying(), 30000);

            auto save = [&](const QString& name)
            {
                fixture.settle();
                for (qreal ratio : { 1.0, 2.0 })
                {
                    QVERIFY(render(fixture.window, ratio).save(QDir(folder).absoluteFilePath(QStringLiteral("search-%1%2-%3-%4x.png").arg(name, variant.name, theme).arg(int(ratio)))));
                }
            };

            // Search results 198:4520 (Whole words on as in the design)
            pdf::PDFDocumentTextSearchOptions options;
            options.wholeWords = true;
            handler(tab)->getSearchController()->setOptions(options);
            handler(tab)->startSearch(QStringLiteral("alpha"));
            QVERIFY(waitForSearch(handler(tab)->getSearchController()));
            save(QStringLiteral("results"));

            // Signatures 195:2959
            QVERIFY(tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::signatures()));
            save(QStringLiteral("signatures"));

            // Speech 195:2499 while reading
            QVERIFY(tab->getView()->showPanel(pdf::PDFAppDocumentPanelIds::speech()));
            handler(tab)->getSpeechController(true)->play();
            QTRY_VERIFY_WITH_TIMEOUT(FakeSpeechEngine::s_last && !FakeSpeechEngine::s_last->said.isEmpty(), 30000);
            Q_EMIT FakeSpeechEngine::s_last->wordStarted(0, 4);
            save(QStringLiteral("speech"));
            handler(tab)->getSpeechController(true)->stop();
        }
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

QTEST_MAIN(AppDocumentSearchTest)
#include "tst_appdocumentsearchtest.moc"
