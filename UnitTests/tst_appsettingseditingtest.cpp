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

// Settings › Editing, Security and Speech (package C of step 5, plan 5): the
// rows of the sections and their consumers in an open document (undo / redo
// limits, author, forms, markup bar, links, signatures, speech), the table of the
// trusted certificates (owner decision Q4) and the speech section with its own
// engine (fake engines - no real speech offscreen).

#include "appsettingsfixture.h"

#include "pdfappannotatecommands.h"
#include "pdfappannotationcreator.h"
#include "pdfappcertificatetable.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentactions.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentproperties.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsignatures.h"
#include "pdfappdocumentspeech.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfapphomedialogs.h"
#include "pdfappmainwindow.h"
#include "pdfappmarkupbar.h"
#include "pdfapprestoredefaultsdialog.h"
#include "pdfappsettingcontrol.h"
#include "pdfappsettingrow.h"
#include "pdfappsettingspage.h"
#include "pdfappsettingssection.h"
#include "pdfappsettingssection_security.h"
#include "pdfappsettingssection_speech.h"
#include "pdfappundostack.h"
#include "pdfdatabasenotifier.h"
#include "pdfdatabasestatement.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdftrustedcertificaterepository.h"

#include "pdfaction.h"
#include "pdfannotation.h"
#include "pdfcertificatestore.h"
#include "pdfdocumentbuilder.h"
#include "pdfutils.h"
#include "pdfwidgetformmanager.h"

#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QCheckBox>
#include <QHeaderView>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSemaphore>
#include <QThread>
#include <QTranslator>
#include <QtTest>

#include <atomic>
#include <limits>

/// Speech engine of the tests: it records the said texts, the test ends a text
/// by finish() (no real engine offscreen and under the sanitizers)
class SettingsFakeSpeechEngine : public pdf::PDFAppSpeechEngine
{
public:
    explicit SettingsFakeSpeechEngine(QObject* parent) :
        pdf::PDFAppSpeechEngine(parent)
    {
        s_last = this;
        ++s_created;
        ++s_alive;
    }

    virtual ~SettingsFakeSpeechEngine() override
    {
        --s_alive;
        if (s_last == this)
        {
            s_last = nullptr;
        }
    }

    static pdf::PDFAppSpeechEngine* create(const QString& engineName, QObject* parent)
    {
        SettingsFakeSpeechEngine* engine = new SettingsFakeSpeechEngine(parent);
        engine->engineName = engineName;
        engine->state = s_isBroken ? State::Error : s_initialState;
        return engine;
    }

    virtual State getState() const override { return state; }
    virtual QString getErrorString() const override { return state == State::Error ? QStringLiteral("No speech daemon") : QString(); }
    virtual QList<QLocale> getLocales() const override
    {
        if (state == State::Initializing || state == State::Error)
        {
            return { };
        }
        return { QLocale(QLocale::English, QLocale::UnitedStates), QLocale(QLocale::German, QLocale::Germany) };
    }
    virtual QLocale getLocale() const override { return locale; }
    virtual void setLocale(const QLocale& newLocale) override
    {
        if (newLocale.language() == QLocale::English || newLocale.language() == QLocale::German)
        {
            locale = newLocale;
        }
    }

    virtual QList<pdf::PDFAppSpeechVoice> getVoices() const override
    {
        if (state == State::Initializing || state == State::Error)
        {
            return { };
        }
        if (locale.language() == QLocale::German)
        {
            return { { QStringLiteral("Hedda"), QStringLiteral("Hedda (Female, Adult)") } };
        }
        return { { QStringLiteral("Aria"), QStringLiteral("Aria (Female, Adult)") }, { QStringLiteral("Guy"), QStringLiteral("Guy (Male, Adult)") } };
    }

    virtual QString getVoice() const override { return voice; }
    virtual void setVoice(const QString& name) override
    {
        for (const pdf::PDFAppSpeechVoice& item : getVoices())
        {
            if (item.name == name)
            {
                voice = name;
            }
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

    /// The engine has started (asynchronous engines)
    void becomeReady()
    {
        state = State::Ready;
        Q_EMIT stateChanged();
        Q_EMIT voicesChanged();
    }

    void setEngineState(State newState)
    {
        if (state != newState && state != State::Error)
        {
            state = newState;
            Q_EMIT stateChanged();
        }
    }

    static SettingsFakeSpeechEngine* s_last;
    static bool s_isBroken;
    static State s_initialState;
    static int s_created;
    static int s_alive;

    QString engineName;
    State state = State::Ready;
    QStringList said;
    QLocale locale = QLocale(QLocale::English, QLocale::UnitedStates);
    QString voice = QStringLiteral("Aria");
    double rate = 0.0;
    double pitch = 0.0;
    double volume = 1.0;
};

SettingsFakeSpeechEngine* SettingsFakeSpeechEngine::s_last = nullptr;
bool SettingsFakeSpeechEngine::s_isBroken = false;
pdf::PDFAppSpeechEngine::State SettingsFakeSpeechEngine::s_initialState = pdf::PDFAppSpeechEngine::State::Ready;
int SettingsFakeSpeechEngine::s_created = 0;
int SettingsFakeSpeechEngine::s_alive = 0;

/// Thread writing a trusted certificate by its own connection (another instance)
class CertificateWriterThread : public QThread
{
public:
    CertificateWriterThread(pdf::PDFDatabase* database, QByteArray data) :
        m_database(database),
        m_data(std::move(data))
    {
    }

    bool isSuccess() const { return m_isSuccess; }

protected:
    virtual void run() override
    {
        pdf::PDFTrustedCertificateRepository repository(m_database);
        m_isSuccess = !repository.addCertificate(m_data).isError();
    }

private:
    pdf::PDFDatabase* m_database;
    QByteArray m_data;
    bool m_isSuccess = false;
};

/// Translator of the language test
class EditingTestTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray source(sourceText);
        const QByteArray contextName(context);
        if (contextName == "pdf::PDFAppSecuritySettingsSection" && source == "Trusted certificates")
        {
            return QString::fromUtf8("Vertrauenswürdige Zertifikate");
        }
        if (contextName == "pdf::PDFAppSpeechSettingsSection" && source == "Play sample")
        {
            return QStringLiteral("Probe abspielen");
        }
        if (contextName == "pdf::PDFAppCertificateTableModel" && source == "Certificate")
        {
            return QStringLiteral("Zertifikat");
        }
        return QString();
    }
};

/// Helpers of the tests of the package
class EditingTestHelper
{
public:
    EditingTestHelper() = delete;

    static pdf::PDFAppDocumentTab* openReady(pdftest::AppSettingsFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        if (!tab)
        {
            return nullptr;
        }

        QPointer<pdf::PDFAppDocumentTab> guard(tab);
        const bool isReady = QTest::qWaitFor([&guard]()
        {
            return guard && guard->getDocument() && guard->getDocument()->isReady() && guard->getView() && guard->getView()->getPageCount() > 0;
        }, 20000 * pdftest::AppDocumentFixture::getTimeoutFactor());
        fixture.window->activateTab(guard.data());
        pdftest::AppDocumentFixture::settle();
        return isReady ? guard.data() : nullptr;
    }

    /// Changes the title of the document (one undo step)
    static bool change(pdf::PDFAppDocument* document, const QString& title)
    {
        pdf::PDFDocumentBuilder builder(document->getDocument().data());
        builder.setDocumentTitle(title);
        pdf::PDFDocumentPointer changed(new pdf::PDFDocument(builder.build()));
        return document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::None), title, document->getVersion(), pdf::PDFAppChangeSource::User);
    }

    static pdf::PDFAppDocumentSearchCommandHandler* searchHandler(pdf::PDFAppDocumentTab* tab)
    {
        return tab ? qobject_cast<pdf::PDFAppDocumentSearchCommandHandler*>(tab->findHandler(pdf::PDFAppCommands::find())) : nullptr;
    }

    static pdf::PDFAppDocumentPropertiesCommandHandler* propertiesHandler(pdf::PDFAppDocumentTab* tab)
    {
        return tab ? qobject_cast<pdf::PDFAppDocumentPropertiesCommandHandler*>(tab->findHandler(pdf::PDFAppCommands::properties())) : nullptr;
    }

    static std::vector<const pdf::PDFAction*> linkActions(const pdf::PDFDocument* document, std::vector<pdf::PDFAnnotationPtr>& annotations)
    {
        std::vector<const pdf::PDFAction*> actions;
        for (const pdf::PDFObjectReference& reference : document->getCatalog()->getPage(0)->getAnnotations())
        {
            pdf::PDFAnnotationPtr annotation = pdf::PDFAnnotation::parse(&document->getStorage(), reference);
            const pdf::PDFLinkAnnotation* link = dynamic_cast<const pdf::PDFLinkAnnotation*>(annotation.get());
            actions.push_back(link ? link->getAction() : nullptr);
            annotations.push_back(annotation);
        }
        return actions;
    }

    /// Stores a raw value (a hostile row of another version) without validation
    static bool storeRaw(pdf::PDFAppContext* context, const QString& key, const QVariant& value)
    {
        pdf::PDFDatabaseStatement statement(context->getDatabase()->getConnection(), "INSERT OR REPLACE INTO setting(key, value, modified) VALUES (?1, ?2, 0)");
        statement.bindText(1, key);
        statement.bindVariant(2, value);
        return statement.execute();
    }

    /// Stores a raw row of the trusted certificates (a damaged row of another version)
    static bool storeRawCertificate(pdf::PDFAppContext* context, const QVariant& value)
    {
        pdf::PDFDatabaseStatement statement(context->getDatabase()->getConnection(), "INSERT INTO trusted_certificate(data, added) VALUES (?1, 0)");
        statement.bindVariant(1, value);
        return statement.execute();
    }

    /// DER data of the certificates of the Adobe Approved Trust List (real certificates)
    static QByteArray certificate(int index)
    {
        static const pdf::PDFCertificateEntries entries = pdf::PDFCertificateStore::getAATLCertificates();
        return index >= 0 && size_t(index) < entries.size() ? entries[size_t(index)].info.getCertificateData() : QByteArray();
    }

    /// Two system certificates of the tests
    static pdf::PDFCertificateEntries systemCertificates()
    {
        pdf::PDFCertificateEntries entries;
        for (int index : { 5, 6 })
        {
            if (std::optional<pdf::PDFCertificateInfo> info = pdf::PDFCertificateInfo::getCertificateInfo(certificate(index)))
            {
                pdf::PDFCertificateEntry entry;
                entry.type = pdf::PDFCertificateEntry::EntryType::System;
                entry.info = *info;
                entries.push_back(entry);
            }
        }
        return entries;
    }

    /// The system store of certificateTableWorkerAfterPageGone, which answers late
    /// (the page may be gone meanwhile): a reading waits until the test opens the
    /// gate (a fixed delay passes under valgrind before the page is shown), the
    /// finished readings are counted; cleanup() opens the gate, so a failed test
    /// leaves no blocked thread of the pool
    static inline QSemaphore s_systemStoreGate{ 0 };
    static inline std::atomic<int> s_systemStoreReadings = 0;

    static pdf::PDFCertificateEntries slowSystemCertificates()
    {
        s_systemStoreGate.tryAcquire(1, 60000 * pdftest::AppDocumentFixture::getTimeoutFactor());
        pdf::PDFCertificateEntries entries = systemCertificates();
        ++s_systemStoreReadings;
        return entries;
    }

    static void openSystemStoreGate()
    {
        s_systemStoreGate.release(100);
    }

    static void closeSystemStoreGate()
    {
        while (s_systemStoreGate.tryAcquire())
        {
        }
    }

    static QStringList engines()
    {
        return { QStringLiteral("winrt"), QStringLiteral("sapi"), QStringLiteral(""), QStringLiteral("winrt"), QStringLiteral("custom-x") };
    }

    static pdf::PDFAppSecuritySettingsSection* securitySection(pdf::PDFAppSettingsPage* page)
    {
        return page ? qobject_cast<pdf::PDFAppSecuritySettingsSection*>(page->getSection()) : nullptr;
    }

    static pdf::PDFAppSpeechSettingsSection* speechSection(pdf::PDFAppSettingsPage* page)
    {
        return page ? qobject_cast<pdf::PDFAppSpeechSettingsSection*>(page->getSection()) : nullptr;
    }

    static pdf::PDFAppCertificateTable* table(pdf::PDFAppSettingsPage* page)
    {
        pdf::PDFAppSecuritySettingsSection* section = securitySection(page);
        return section ? section->getCertificateTable() : nullptr;
    }

    static bool waitForTable(pdf::PDFAppCertificateTable* table)
    {
        QPointer<pdf::PDFAppCertificateTable> guard(table);
        const bool isLoaded = QTest::qWaitFor([&guard]() { return !guard || !guard->isLoadingSystemCertificates(); }, 20000 * pdftest::AppDocumentFixture::getTimeoutFactor());

        // The layouts of the page take the new rows
        pdftest::AppDocumentFixture::settle();
        return isLoaded && guard;
    }

    /// Chooses the item of the data by the arrows of the closed combo box (activated)
    static bool chooseItem(QComboBox* comboBox, const QVariant& data)
    {
        const int target = comboBox ? comboBox->findData(data) : -1;
        if (target < 0 || !comboBox->isEnabled())
        {
            return false;
        }

        comboBox->setFocus();
        int current = comboBox->currentIndex();
        for (int guard = 0; current != target && guard < 1000; ++guard)
        {
            QTest::keyClick(comboBox, current < target ? Qt::Key_Down : Qt::Key_Up);
            const int next = comboBox->currentIndex();
            if (next == current)
            {
                break;
            }
            current = next;
        }
        // The written value may delete the page (a slot of its signals)
        QPointer<QComboBox> guard(comboBox);
        pdftest::AppDocumentFixture::settle();
        return guard && guard->currentIndex() == target;
    }

    /// Clicks a row of the table (with the modifiers)
    static void clickRow(pdf::PDFTreeView* view, int row, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        const QModelIndex index = view->model()->index(row, pdf::PDFAppCertificateTableModel::NameColumn);
        view->scrollTo(index);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, modifiers, view->visualRect(index).center());
        pdftest::AppDocumentFixture::settle();
    }

    static QStringList texts(const QWidget* parent, const QString& objectName)
    {
        QStringList result;
        for (const pdf::PDFLabel* label : parent->findChildren<pdf::PDFLabel*>(objectName))
        {
            result << label->text();
        }
        return result;
    }

    /// Texts of the title and of the text of a message box (its direct labels)
    static QStringList dialogTexts(const QWidget* dialog)
    {
        QStringList result;
        for (const pdf::PDFLabel* label : dialog->findChildren<pdf::PDFLabel*>(Qt::FindDirectChildrenOnly))
        {
            result << label->text();
        }
        return result;
    }

    static QStringList itemTexts(const QComboBox* comboBox)
    {
        QStringList result;
        for (int i = 0; comboBox && i < comboBox->count(); ++i)
        {
            result << comboBox->itemText(i);
        }
        return result;
    }

    static QImage render(QWidget* widget, qreal devicePixelRatio)
    {
        QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
        widget->render(&image, QPoint(), QRegion(), QWidget::RenderFlags(QWidget::DrawChildren));
        return image;
    }

    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
        int count = 0;
        const QImage converted = image.convertToFormat(QImage::Format_RGB32);
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                count += (line[x] & 0xFFFFFF) == leak ? 1 : 0;
            }
        }
        return count;
    }

    static QScrollArea* scrollArea(QWidget* page)
    {
        for (QWidget* widget = page; widget; widget = widget->parentWidget())
        {
            if (QScrollArea* area = qobject_cast<QScrollArea*>(widget))
            {
                return area;
            }
        }
        return nullptr;
    }

    static int userRowCount(pdf::PDFAppContext* context)
    {
        return int(pdf::PDFTrustedCertificateRepository(context->getDatabase()).getRows().size());
    }
};

class AppSettingsEditingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void sectionsSmoke();
    void editingRowsWriteAndApply();
    void undoRedoLimits();
    void authorRowsEnableAndSanitize();
    void formsRowsApplyLive();
    void markupBarRow();
    void linkRowsAffectActionHandler();
    void signatureRowsReverify();
    void certificateTable();
    void certificateTableDamagedRows();
    void certificateTableOtherInstance();
    void certificateTableWorkerAfterPageGone();
    void certificateTableGeometry();
    void speechEnginesAndVoices();
    void speechEngineAsynchronous();
    void speechEngineError();
    void speechSameAsPanel();
    void playSample();
    void hostileStoredValues();
    void otherInstanceChange();
    void restoreDefaults();
    void searchAndAnchors();
    void deletionMatrix();
    void deleteFromOwnSignals();
    void themesAndScales();
    void rightToLeft();
    void minimalWindow();
    void keyboardAndAccessibility();
    void languageChange();
    void screenshots();
};

void AppSettingsEditingTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void AppSettingsEditingTest::init()
{
    // Fake engines everywhere (the section and the documents), two certificates of
    // the system store, the engines of the tests
    SettingsFakeSpeechEngine::s_isBroken = false;
    SettingsFakeSpeechEngine::s_initialState = pdf::PDFAppSpeechEngine::State::Ready;
    pdf::PDFAppSpeechEngine::setFactory(&SettingsFakeSpeechEngine::create);
    pdf::PDFAppSpeechSettingsSection::setEngineListProvider(&EditingTestHelper::engines);
    pdf::PDFAppCertificateTable::setSystemCertificatesProvider(&EditingTestHelper::systemCertificates);
}

void AppSettingsEditingTest::cleanup()
{
    pdf::PDFAppSpeechEngine::setFactory(nullptr);
    pdf::PDFAppSpeechSettingsSection::setEngineListProvider(nullptr);
    pdf::PDFAppCertificateTable::setSystemCertificatesProvider(nullptr);
    EditingTestHelper::openSystemStoreGate();
    pdf::PDFAppDocumentActionHandler::setLaunchHandler(nullptr);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppSettingsEditingTest::sectionsSmoke()
{
    // Every section of the package opens with its rows of the definitions
    pdftest::AppSettingsFixture fixture;
    for (const QString& sectionId : { QStringLiteral("editing"), QStringLiteral("security"), QStringLiteral("speech") })
    {
        pdf::PDFAppSettingsPage* page = fixture.openSettings(sectionId);
        QVERIFY2(page, qPrintable(sectionId));
        QCOMPARE(page->getSectionId(), sectionId);
        QVERIFY(page->getSection());
        for (const pdf::PDFSettingDefinition* definition : fixture.context->getSettingRegistry()->getSectionDefinitions(sectionId))
        {
            QVERIFY2(page->findRow(definition->getAnchor()), qPrintable(definition->getKey()));
        }
    }

    // The groups of Editing in the order of 146:3196 (Author identity above Forms)
    QStringList editingGroups;
    for (const pdf::PDFAppSettingGroup* group : fixture.openSettings(QStringLiteral("editing"))->getSection()->getGroups())
    {
        editingGroups << group->getTitle();
    }
    QCOMPARE(editingGroups, QStringList({ QStringLiteral("Undo history"), QStringLiteral("Author identity"), QStringLiteral("Forms"), QStringLiteral("Annotations") }));

    // The custom parts: the table of the certificates, the combo boxes of the engine, Preview
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("security"));
    QVERIFY(EditingTestHelper::table(page));
    QVERIFY(page->getSection()->findGroup(pdf::PDFAppSecuritySettingsSection::getCertificatesGroupKey()));
    page = fixture.openSettings(QStringLiteral("speech"));
    pdf::PDFAppSpeechSettingsSection* speech = EditingTestHelper::speechSection(page);
    QVERIFY(speech);
    QVERIFY(speech->getEngineComboBox() && speech->getLocaleComboBox() && speech->getVoiceComboBox());
    QCOMPARE(speech->getPlayButton()->text(), QStringLiteral("Play sample"));
    QCOMPARE(page->findRow(QStringLiteral("preview"))->getDescription(), QStringLiteral("Hear the current voice and settings"));
}

void AppSettingsEditingTest::editingRowsWriteAndApply()
{
    // Every row of Editing writes its key and the open document applies it
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = EditingTestHelper::openReady(fixture, pdftest::AppDocumentSamples::form(fixture.folder()));
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocument> document = tab->getDocument();
    pdf::PDFWidgetFormManager* formManager = tab->getView()->getFormManager();
    QVERIFY(formManager);

    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("editing"));
    QVERIFY(page);
    QVERIFY(fixture.window->getCurrentTab() != static_cast<pdf::PDFAppTab*>(tab));

    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("maximum-undo-steps")), qint64(7)));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("maximum-redo-steps")), qint64(4)));
    QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::editingUndoLimit()), qint64(7));
    QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::editingRedoLimit()), qint64(4));
    QVERIFY(EditingTestHelper::change(document, QStringLiteral("A")));
    QCOMPARE(document->getUndoStack()->getLimit(), 7);
    QCOMPARE(document->getUndoStack()->getRedoLimit(), 4);

    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("highlight-form-fields")), false));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("highlight-required-form-fields")), false));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("allow-editing-of-read-only-fields")), true));
    QTRY_VERIFY(!formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::HighlightFields));
    QTRY_VERIFY(!formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::HighlightRequiredFields));
    QTRY_VERIFY(formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::EditReadOnlyFields));

    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("author-of-new-annotations")), pdf::PDFSettingValues::authorModeCustomName()));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("custom-author-name")), QStringLiteral("Jane Novak")));
    QTRY_COMPARE(pdf::PDFAuthorSettings::getAuthorName(), QStringLiteral("Jane Novak"));

    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("show-markup-bar-when-text-is-selected")), false));
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::annotationsShowMarkupBar()), false);
    QVERIFY(page->getRestoreButton()->isEnabled());
}

void AppSettingsEditingTest::undoRedoLimits()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument() && tab->getDocument()->isReady());
    QPointer<pdf::PDFAppDocument> document = tab->getDocument();
    pdf::PDFAppUndoStack* stack = document->getUndoStack();

    QVERIFY(fixture.openSettings(QStringLiteral("editing")));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("maximum-undo-steps")), qint64(3)));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("maximum-redo-steps")), qint64(1)));

    // Five changes: three undo steps
    std::vector<pdf::PDFDocumentPointer> versions = { document->getDocument() };
    for (int i = 0; i < 5; ++i)
    {
        QVERIFY(EditingTestHelper::change(document, QStringLiteral("T%1").arg(i)));
        versions.push_back(document->getDocument());
    }
    QCOMPARE(stack->getUndoCount(), size_t(3));

    // Two undo steps: only the nearest redo step is kept (the redo limit 1)
    QVERIFY(stack->undo());
    QCOMPARE(stack->getRedoCount(), size_t(1));
    QVERIFY(stack->undo());
    QCOMPARE(stack->getRedoCount(), size_t(1));
    QCOMPARE(stack->getUndoCount(), size_t(1));
    QVERIFY(stack->redo());
    QCOMPARE(document->getDocument(), versions[4]);
    QVERIFY(!stack->redo());

    // Redo limit 0: an undo leaves nothing to redo
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("maximum-redo-steps")), qint64(0)));
    QVERIFY(stack->undo());
    QCOMPARE(stack->getRedoCount(), size_t(0));
    QCOMPARE(stack->getRedoLimit(), 0);
    QVERIFY(!stack->canRedo());

    // A higher redo limit keeps more steps again
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("maximum-redo-steps")), qint64(10)));
    QVERIFY(EditingTestHelper::change(document, QStringLiteral("U1")));
    QVERIFY(EditingTestHelper::change(document, QStringLiteral("U2")));
    QVERIFY(stack->undo());
    QVERIFY(stack->undo());
    QCOMPARE(stack->getRedoCount(), size_t(2));

    // Undo limit 0: no undo at all
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("maximum-undo-steps")), qint64(0)));
    QVERIFY(EditingTestHelper::change(document, QStringLiteral("V")));
    QVERIFY(!stack->canUndo());
    QVERIFY(!stack->undo());

    // The stack itself: a lower redo limit drops the farthest steps at once, the
    // saved version in a dropped step is not reachable any more
    // (the settings are read at the commits and the undo steps)
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::editingUndoLimit(), qint64(10)).isError());
    QVERIFY(document->commit(pdf::PDFModifiedDocument(versions[1], nullptr, pdf::PDFModifiedDocument::None), QStringLiteral("W1"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(versions[2], nullptr, pdf::PDFModifiedDocument::None), QStringLiteral("W2"), document->getVersion(), pdf::PDFAppChangeSource::User));
    stack->setSavedPosition();
    QVERIFY(stack->undo());
    QVERIFY(stack->undo());
    QCOMPARE(stack->getRedoCount(), size_t(2));
    QCOMPARE(stack->getUnsavedChangeCount(), 2);
    stack->setRedoLimit(1);
    QCOMPARE(stack->getRedoCount(), size_t(1));
    QCOMPARE(stack->getUnsavedChangeCount(), -1);
    stack->setRedoLimit(-5);
    QCOMPARE(stack->getRedoLimit(), 0);
    QCOMPARE(stack->getRedoCount(), size_t(0));

    // The saved version and the redo limit 0 (review RC5-BC): the document answers
    // "modified" for the close / quit dialogs exactly when its version is not the
    // saved one, also when the saved version is gone with a dropped redo step
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::editingRedoLimit(), qint64(0)).isError());
    QVERIFY(EditingTestHelper::change(document, QStringLiteral("S1")));
    document->setSaved();
    QVERIFY(!document->isModified());
    QCOMPARE(document->getUnsavedChangeCount(), 0);

    // A change after the save and its undo: back at the saved version
    QVERIFY(EditingTestHelper::change(document, QStringLiteral("S2")));
    QVERIFY(document->isModified());
    QCOMPARE(document->getUnsavedChangeCount(), 1);
    QCOMPARE(pdf::PDFAppCloseUnsavedDialog::getChangesText(document), QStringLiteral("1 unsaved change"));
    QVERIFY(stack->undo());
    QVERIFY(!document->isModified());
    QCOMPARE(document->getUnsavedChangeCount(), 0);
    QVERIFY(!stack->canRedo());

    // An undo of the saved version: the saved version is gone with the redo step
    // (the redo limit 0) - modified, an unknown count, the dialogs say so
    QVERIFY(stack->undo());
    QVERIFY(!stack->canRedo());
    QVERIFY(document->isModified());
    QCOMPARE(document->getUnsavedChangeCount(), -1);
    QCOMPARE(pdf::PDFAppCloseUnsavedDialog::getChangesText(document), QStringLiteral("Unsaved changes"));
    QCOMPARE(pdf::PDFAppCloseUnsavedDialog::getText(document), QStringLiteral("The document has unsaved changes. If you close the tab without saving, the changes are lost."));

    // Further changes and undo steps never reach "saved" again, a save does
    QVERIFY(EditingTestHelper::change(document, QStringLiteral("S3")));
    QVERIFY(stack->undo());
    QVERIFY(document->isModified());
    QCOMPARE(document->getUnsavedChangeCount(), -1);
    document->setSaved();
    QVERIFY(!document->isModified());
    QCOMPARE(document->getUnsavedChangeCount(), 0);
}

void AppSettingsEditingTest::authorRowsEnableAndSanitize()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(fixture.openSettings(QStringLiteral("editing")));
    pdf::PDFAppSettingRow* mode = fixture.findRow(QStringLiteral("author-of-new-annotations"));
    pdf::PDFAppSettingRow* name = fixture.findRow(QStringLiteral("custom-author-name"));
    QVERIFY(mode && name);

    // Anonymous by default (O20): the custom name is disabled
    QCOMPARE(mode->getControl()->getShownValue(), QVariant(pdf::PDFSettingValues::authorModeAnonymous()));
    QVERIFY(!name->getControlWrap()->isEnabled());
    QVERIFY(!pdftest::AppSettingsFixture::setControlValue(name, QStringLiteral("x")));

    QVERIFY(pdftest::AppSettingsFixture::setControlValue(mode, pdf::PDFSettingValues::authorModeCustomName()));
    QTRY_VERIFY(name->getControlWrap()->isEnabled());

    // A name with a bidirectional override and too many characters: the field keeps
    // 256 characters, the author of the annotations has no format characters
    pdf::PDFLineEdit* field = name->getControl()->getLineEdit();
    field->setFocus();
    QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(field, QStringLiteral("Eve"));
    QTest::sendKeyEvent(QTest::Click, field, Qt::Key_unknown, QString(QChar(0x202E)), Qt::NoModifier);
    QTest::keyClicks(field, QStringLiteral("evil") + QString(300, QLatin1Char('a')));
    QTest::keyClick(field, Qt::Key_Return);
    fixture.settle();
    const QString stored = fixture.settings().getString(pdf::PDFSettingKeys::annotationsCustomAuthorName());
    QVERIFY(stored.size() <= 256);
    QVERIFY(stored.startsWith(QStringLiteral("Eve")));
    QTRY_VERIFY(pdf::PDFAuthorSettings::getAuthorName().startsWith(QStringLiteral("Eve")));
    QVERIFY(!pdf::PDFAuthorSettings::getAuthorName().contains(QChar(0x202E)));
    QVERIFY(pdf::PDFAuthorSettings::getAuthorName().size() <= 256);

    // The system user name: the custom name is disabled again, the stored name stays
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(mode, pdf::PDFSettingValues::authorModeSystemUserName()));
    QTRY_VERIFY(!name->getControlWrap()->isEnabled());
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::annotationsCustomAuthorName()), stored);
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(mode, pdf::PDFSettingValues::authorModeAnonymous()));
    QTRY_COMPARE(pdf::PDFAuthorSettings::getAuthorName(), pdf::PDFAuthorSettings::getAnonymousAuthorName());
}

void AppSettingsEditingTest::formsRowsApplyLive()
{
    // The forms rows of the page and the highlight of an open form at once,
    // also a change of another instance
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = EditingTestHelper::openReady(fixture, pdftest::AppDocumentSamples::form(fixture.folder()));
    QVERIFY(tab);
    QPointer<pdf::PDFWidgetFormManager> formManager = tab->getView()->getFormManager();
    QVERIFY(formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::HighlightFields));

    QVERIFY(fixture.openSettings(QStringLiteral("editing")));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("highlight-form-fields")), false));
    QTRY_VERIFY(!formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::HighlightFields));
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::formsHighlightFields(), true));
    QTRY_VERIFY(formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::HighlightFields));
    QTRY_COMPARE(fixture.findRow(QStringLiteral("highlight-form-fields"))->getControl()->getShownValue(), QVariant(true));
}

void AppSettingsEditingTest::markupBarRow()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = EditingTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Markup.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppAnnotationCreator* creator = pdf::PDFAppAnnotateCommandHandler::findCreator(tab->getView());
    QVERIFY(creator);

    QVERIFY(fixture.openSettings(QStringLiteral("editing")));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("show-markup-bar-when-text-is-selected")), false));
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::annotationsShowMarkupBar()), false);

    // The selection of the document shows no bar
    fixture.window->activateTab(tab);
    fixture.settle();
    tab->getView()->selectAllText();
    QTRY_VERIFY(creator->hasTextSelection());
    fixture.settle();
    QVERIFY(!creator->getMarkupBar() || !creator->getMarkupBar()->isVisible());

    // On again (another instance): the bar of the selection
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::annotationsShowMarkupBar(), true));
    QTRY_VERIFY(creator->getMarkupBar() && creator->getMarkupBar()->isVisible());
}

void AppSettingsEditingTest::linkRowsAffectActionHandler()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = EditingTestHelper::openReady(fixture, pdftest::AppDocumentSamples::links(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentActionHandler* handler = EditingTestHelper::propertiesHandler(tab)->getActionHandler();
    std::vector<pdf::PDFAnnotationPtr> annotations;
    const std::vector<const pdf::PDFAction*> actions = EditingTestHelper::linkActions(tab->getDocument()->getDocument().data(), annotations);
    QVERIFY(actions.size() > 2 && actions[2]);
    int launched = 0;
    pdf::PDFAppDocumentActionHandler::setLaunchHandler([&launched](const pdf::PDFAppExternalLaunch&) { ++launched; return true; });

    // Web links turned off by the row: the info bar of the document, nothing opens
    QVERIFY(fixture.openSettings(QStringLiteral("security")));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("allow-uri-link-launch")), false));
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::securityOpenLinks()), false);
    fixture.window->activateTab(tab);
    fixture.settle();
    handler->performAction(actions[2]);
    fixture.settle();
    QCOMPARE(launched, 0);
    QVERIFY(tab->getView()->findInfoBar(pdf::PDFAppDocumentActionHandler::getLinkBlockedBarId()));

    // Security Settings of the bar: the row of the setting, highlighted
    Q_EMIT tab->getView()->infoBarActionTriggered(pdf::PDFAppDocumentActionHandler::getLinkBlockedBarId(), pdf::PDFAppDocumentActionHandler::getSecuritySettingsActionId());
    fixture.settle();
    pdf::PDFAppSettingsPage* page = fixture.settingsPage();
    QVERIFY(page);
    QCOMPARE(page->getSectionId(), QStringLiteral("security"));
    QVERIFY(page->findRow(QStringLiteral("allow-uri-link-launch"))->isHighlighted());

    // Programs: the row writes the key the handler reads at the click
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::securityLaunchFiles()), false);
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("allow-application-launch")), true));
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::securityLaunchFiles()), true);
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("allow-uri-link-launch")), true));
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::securityOpenLinks()), true);
}

void AppSettingsEditingTest::signatureRowsReverify()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = EditingTestHelper::openReady(fixture, pdftest::AppDocumentSamples::signedDocument(fixture.folder()));
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentSignatureVerifier> verifier = EditingTestHelper::searchHandler(tab)->getSignatureVerifier();
    const int timeout = 30000 * pdftest::AppDocumentFixture::getTimeoutFactor();
    QTRY_VERIFY_WITH_TIMEOUT(verifier->getVerification() && !verifier->isVerifying(), timeout);
    QVERIFY(verifier->getVerification()->isVerified);
    QVERIFY(!verifier->getVerification()->isStrict);

    QVERIFY(fixture.openSettings(QStringLiteral("security")));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("signature-verification")), false));
    QTRY_VERIFY_WITH_TIMEOUT(verifier->getVerification() && !verifier->isVerifying() && !verifier->getVerification()->isVerified, timeout);

    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("signature-verification")), true));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("strict-mode")), true));
    QTRY_VERIFY_WITH_TIMEOUT(verifier->getVerification() && !verifier->isVerifying() && verifier->getVerification()->isVerified && verifier->getVerification()->isStrict, timeout);

    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("ignore-expired-certificates")), true));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("use-system-certificate-store")), false));
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::signaturesIgnoreExpiration()), true);
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::signaturesUseSystemStore()), false);
    QTRY_VERIFY_WITH_TIMEOUT(verifier->getVerification() && !verifier->isVerifying(), timeout);
}

void AppSettingsEditingTest::certificateTable()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFTrustedCertificateRepository repository(fixture.context->getDatabase());
    QVERIFY(!EditingTestHelper::certificate(1).isEmpty());
    QVERIFY(!repository.addCertificate(EditingTestHelper::certificate(0)).isError());
    QVERIFY(!repository.addCertificate(EditingTestHelper::certificate(1)).isError());

    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("security"));
    pdf::PDFAppCertificateTable* table = EditingTestHelper::table(page);
    QVERIFY(table);
    QVERIFY(EditingTestHelper::waitForTable(table));
    pdf::PDFAppCertificateTableModel* model = table->getModel();
    pdf::PDFTreeView* view = table->getView();

    // Own certificates first, then the system ones (sorted); the columns of 147:3786
    QCOMPARE(model->rowCount(), 4);
    QCOMPARE(model->getCount(pdf::PDFAppCertificateEntry::Type::User), 2);
    QCOMPARE(model->getCount(pdf::PDFAppCertificateEntry::Type::System), 2);
    QCOMPARE(model->data(model->index(0, pdf::PDFAppCertificateTableModel::TypeColumn)).toString(), QStringLiteral("User"));
    QCOMPARE(model->data(model->index(2, pdf::PDFAppCertificateTableModel::TypeColumn)).toString(), QStringLiteral("System"));
    QVERIFY(model->getEntry(2)->name.compare(model->getEntry(3)->name, Qt::CaseInsensitive) <= 0);
    QCOMPARE(model->headerData(pdf::PDFAppCertificateTableModel::NameColumn, Qt::Horizontal).toString(), QStringLiteral("Certificate"));
    QCOMPARE(model->headerData(pdf::PDFAppCertificateTableModel::ValidToColumn, Qt::Horizontal).toString(), QStringLiteral("Valid to"));
    QVERIFY(!model->data(model->index(0, pdf::PDFAppCertificateTableModel::ValidFromColumn)).toString().isEmpty());
    QVERIFY(model->data(model->index(0, pdf::PDFAppCertificateTableModel::NameColumn), Qt::ToolTipRole).toString().contains(QStringLiteral("SHA-256")));
    QCOMPARE(view->visualRect(model->index(0, 0)).height(), 34);

    // The view shows all its rows without a scroll bar, the line of the trust list below them
    const QRect lastRow = view->visualRect(model->index(3, 0));
    QVERIFY2(lastRow.bottom() < view->viewport()->height(), qPrintable(QStringLiteral("%1 %2 %3 %4").arg(lastRow.bottom()).arg(view->viewport()->height()).arg(view->height()).arg(view->header()->height())));
    QVERIFY2(table->getTrustListLabel()->mapTo(table, QPoint(0, 0)).y() >= view->mapTo(table, QPoint(0, view->height())).y(),
             qPrintable(QStringLiteral("table %1 min %2 hint %3 card %4 view %5 at %6 label at %7 parent %8 %9").arg(table->height()).arg(table->minimumSizeHint().height()).arg(table->sizeHint().height())
                        .arg(table->findChild<QWidget*>(QStringLiteral("card"))->height()).arg(view->height()).arg(view->y()).arg(table->getTrustListLabel()->y())
                        .arg(table->parentWidget()->height()).arg(table->parentWidget()->metaObject()->className())));
    QCOMPARE(table->getStatusLabel()->text(), QStringLiteral("4 certificates"));
    QVERIFY(table->getTrustListLabel()->text().contains(QStringLiteral("Adobe Approved Trust List")));
    QVERIFY(table->getTrustListCount() > 0);
    QVERIFY(!table->getRemoveButton()->isEnabled());

    // One own certificate: "1 of 4 selected", Remove enabled
    EditingTestHelper::clickRow(view, 0);
    QCOMPARE(table->getStatusLabel()->text(), QStringLiteral("1 of 4 selected"));
    QVERIFY(table->getRemoveButton()->isEnabled());

    // A system certificate cannot be removed (also in a selection with an own one)
    EditingTestHelper::clickRow(view, 2, Qt::ControlModifier);
    QCOMPARE(table->getStatusLabel()->text(), QStringLiteral("2 of 4 selected"));
    QVERIFY(!table->getRemoveButton()->isEnabled());
    QVERIFY(!table->openRemoveDialog());
    QVERIFY(!table->getRemoveButton()->toolTip().isEmpty());

    // Two own certificates: the confirmation names them with their fingerprints, Cancel removes nothing
    EditingTestHelper::clickRow(view, 0);
    EditingTestHelper::clickRow(view, 1, Qt::ControlModifier);
    QVERIFY(table->getRemoveButton()->isEnabled());
    QTest::mouseClick(table->getRemoveButton(), Qt::LeftButton);
    fixture.settle();
    QPointer<pdf::PDFAppCardDialog> dialog = table->getRemoveDialog();
    QVERIFY(dialog && dialog->isVisible());
    // The first focus on the Primary Cancel, not on Remove (FX5-2)
    QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(dialog->findChild<QPushButton*>(QStringLiteral("cancelButton"))));
    QVERIFY(EditingTestHelper::dialogTexts(dialog).contains(QStringLiteral("Remove 2 certificates?")));
    const QStringList fingerprints = EditingTestHelper::texts(dialog, QStringLiteral("fingerprint"));
    QCOMPARE(fingerprints.size(), 2);
    QVERIFY(fingerprints.contains(pdf::PDFAppCertificateEntry::getFingerprint(EditingTestHelper::certificate(0))));
    QVERIFY(fingerprints.contains(pdf::PDFAppCertificateEntry::getFingerprint(EditingTestHelper::certificate(1))));
    QCOMPARE(EditingTestHelper::texts(dialog, QStringLiteral("certificateName")).size(), 2);
    QVERIFY(!table->getRemoveButton()->isEnabled());
    QTest::mouseClick(dialog->findChild<QPushButton*>(QStringLiteral("cancelButton")), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!dialog);
    QCOMPARE(EditingTestHelper::userRowCount(fixture.context.get()), 2);

    // Remove of one certificate: the title names it, the row goes
    EditingTestHelper::clickRow(view, 1);
    const QString name = model->getEntry(1)->name;
    QVERIFY(table->openRemoveDialog());
    dialog = table->getRemoveDialog();
    QVERIFY(EditingTestHelper::dialogTexts(dialog).contains(QString::fromUtf8("Remove the certificate “%1”?").arg(name)));
    QTest::mouseClick(dialog->findChild<QPushButton*>(QStringLiteral("removeButton")), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(EditingTestHelper::userRowCount(fixture.context.get()), 1);
    QTRY_COMPARE(model->rowCount(), 3);
    QCOMPARE(pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).getCertificates().front().data, EditingTestHelper::certificate(0));

    // The system store off: only the own certificate
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("use-system-certificate-store")), false));
    QVERIFY(EditingTestHelper::waitForTable(table));
    QTRY_COMPARE(model->rowCount(), 1);
    QCOMPARE(model->getCount(pdf::PDFAppCertificateEntry::Type::System), 0);

    // No certificate at all: the empty state
    EditingTestHelper::clickRow(view, 0);
    QVERIFY(table->openRemoveDialog());
    QTest::mouseClick(table->getRemoveDialog()->findChild<QPushButton*>(QStringLiteral("removeButton")), Qt::LeftButton);
    fixture.settle();
    QTRY_COMPARE(model->rowCount(), 0);
    QVERIFY(table->getEmptyLabel()->isVisible());
    QVERIFY(view->isHidden());
    QCOMPARE(table->getStatusLabel()->text(), QStringLiteral("0 certificates"));
}

void AppSettingsEditingTest::certificateTableDamagedRows()
{
    // Rows of another version or of a damaged file: listed and removable
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppContext* context = fixture.context.get();
    QVERIFY(EditingTestHelper::storeRawCertificate(context, QByteArray("not a certificate")));
    QVERIFY(EditingTestHelper::storeRawCertificate(context, QStringLiteral("a text")));
    QVERIFY(EditingTestHelper::storeRawCertificate(context, QByteArray(70000, 'x')));
    QVERIFY(EditingTestHelper::storeRawCertificate(context, QByteArray()));
    QVERIFY(EditingTestHelper::storeRawCertificate(context, EditingTestHelper::certificate(2)));
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::signaturesUseSystemStore(), false).isError());

    pdf::PDFAppCertificateTable* table = EditingTestHelper::table(fixture.openSettings(QStringLiteral("security")));
    QVERIFY(EditingTestHelper::waitForTable(table));
    pdf::PDFAppCertificateTableModel* model = table->getModel();
    QCOMPARE(model->rowCount(), 5);
    for (int row = 0; row < 4; ++row)
    {
        QVERIFY(model->getEntry(row)->isDamaged);
        QCOMPARE(model->getEntry(row)->name, QStringLiteral("Damaged certificate"));
        QVERIFY(model->data(model->index(row, pdf::PDFAppCertificateTableModel::ValidFromColumn)).toString().isEmpty());
    }
    QVERIFY(!model->getEntry(4)->isDamaged);

    // A damaged row and its confirmation (no fingerprint of empty data)
    EditingTestHelper::clickRow(table->getView(), 3);
    QVERIFY(table->openRemoveDialog());
    QCOMPARE(EditingTestHelper::texts(table->getRemoveDialog(), QStringLiteral("fingerprint")), QStringList({ QStringLiteral("Unknown") }));
    QTest::mouseClick(table->getRemoveDialog()->findChild<QPushButton*>(QStringLiteral("removeButton")), Qt::LeftButton);
    fixture.settle();
    QTRY_COMPARE(model->rowCount(), 4);

    // All damaged rows at once (Ctrl+A), the confirmation lists at most four
    QVERIFY(EditingTestHelper::storeRawCertificate(context, QByteArray("x")));
    QVERIFY(EditingTestHelper::storeRawCertificate(context, QByteArray("y")));
    table->reloadUserCertificates();
    QCOMPARE(model->rowCount(), 6);
    table->getView()->setFocus();
    table->getView()->selectAll();
    fixture.settle();
    QVERIFY(table->openRemoveDialog());
    QCOMPARE(EditingTestHelper::texts(table->getRemoveDialog(), QStringLiteral("fingerprint")).size(), pdf::PDFAppCertificateTable::MAXIMUM_DIALOG_CERTIFICATES);
    QCOMPARE(EditingTestHelper::texts(table->getRemoveDialog(), QStringLiteral("moreCertificates")), QStringList({ QStringLiteral("and 2 more certificates") }));
    QTest::mouseClick(table->getRemoveDialog()->findChild<QPushButton*>(QStringLiteral("removeButton")), Qt::LeftButton);
    fixture.settle();
    QTRY_COMPARE(model->rowCount(), 0);
    QCOMPARE(EditingTestHelper::userRowCount(context), 0);

    // Texts of a certificate are one safe line
    QString hostile = QStringLiteral("A") + QChar(0x202E) + QStringLiteral("B\nC") + QChar(0x0001) + QChar(0x200D);
    hostile += QString(1000, QLatin1Char('z'));
    const QString sanitized = pdf::PDFAppCertificateEntry::sanitizeText(hostile);
    QVERIFY(sanitized.startsWith(QStringLiteral("A B C")));
    QVERIFY(!sanitized.contains(QChar(0x202E)) && !sanitized.contains(QChar(0x0001)) && !sanitized.contains(QChar(0x200D)));
    QCOMPARE(sanitized.size(), pdf::PDFAppCertificateEntry::MAXIMUM_TEXT_LENGTH);
    QString pairs = QString(255, QLatin1Char('a')) + QString::fromUtf8("\xF0\x9F\x98\x80\xF0\x9F\x98\x80");
    const QString cut = pdf::PDFAppCertificateEntry::sanitizeText(pairs);
    QVERIFY(!cut.back().isHighSurrogate());
    QCOMPARE(pdf::PDFAppCertificateEntry::sanitizeText(QString(QChar(0xD800))), QString(QChar(0xFFFD)));
    QCOMPARE(pdf::PDFAppCertificateEntry::create(pdf::PDFAppCertificateEntry::Type::User, 7, QByteArray()).isDamaged, true);
    QCOMPARE(pdf::PDFAppCertificateEntry::getFingerprint(QByteArray("abc")).size(), 79);

    // A refused removal (a database, which refuses it): a status message of the page
    QVERIFY(EditingTestHelper::storeRawCertificate(context, QByteArray("z")));
    table->reloadUserCertificates();
    pdf::PDFDatabaseStatement block(context->getDatabase()->getConnection(), "CREATE TRIGGER block_certificate BEFORE DELETE ON trusted_certificate BEGIN SELECT RAISE(ABORT, 'blocked'); END");
    QVERIFY(block.execute());
    EditingTestHelper::clickRow(table->getView(), 0);
    QVERIFY(table->openRemoveDialog());
    QTest::mouseClick(table->getRemoveDialog()->findChild<QPushButton*>(QStringLiteral("removeButton")), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(model->rowCount(), 1);
    QVERIFY(fixture.settingsPage()->getStatusLabel()->text().contains(QStringLiteral("could not be removed")));
}

void AppSettingsEditingTest::certificateTableOtherInstance()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppCertificateTable* table = EditingTestHelper::table(fixture.openSettings(QStringLiteral("security")));
    QVERIFY(EditingTestHelper::waitForTable(table));
    pdf::PDFAppCertificateTableModel* model = table->getModel();
    QCOMPARE(model->rowCount(), 2);

    // A selected system certificate stays selected over a reload
    EditingTestHelper::clickRow(table->getView(), 1);
    const QByteArray selectedData = model->getEntry(1)->data;

    CertificateWriterThread thread(fixture.context->getDatabase(), EditingTestHelper::certificate(3));
    thread.start();
    QVERIFY(thread.wait(10000 * pdftest::AppDocumentFixture::getTimeoutFactor()));
    QVERIFY(thread.isSuccess());
    QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 3, 10000 * pdftest::AppDocumentFixture::getTimeoutFactor());
    QCOMPARE(model->getEntry(0)->type, pdf::PDFAppCertificateEntry::Type::User);
    const std::vector<pdf::PDFAppCertificateEntry> selected = table->getSelectedEntries();
    QCOMPARE(selected.size(), size_t(1));
    QCOMPARE(selected.front().data, selectedData);

    // The system store turned off by another instance
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::signaturesUseSystemStore(), false));
    QVERIFY(EditingTestHelper::waitForTable(table));
    QTRY_COMPARE(model->rowCount(), 1);
    QVERIFY(table->getSelectedEntries().empty());

    // Another instance removes the confirmed certificate and trusts a new one, which
    // gets the id of the removed row (INTEGER PRIMARY KEY): Remove removes only the
    // certificate the confirmation has named (review RC5-BC)
    pdf::PDFTrustedCertificateRepository repository(fixture.context->getDatabase());
    QVERIFY(!repository.addCertificate(EditingTestHelper::certificate(0)).isError());
    table->reloadUserCertificates();
    QCOMPARE(model->rowCount(), 2);
    const qint64 confirmedId = model->getEntry(1)->id;
    QCOMPARE(model->getEntry(1)->data, EditingTestHelper::certificate(0));
    EditingTestHelper::clickRow(table->getView(), 1);
    QVERIFY(table->openRemoveDialog());
    QVERIFY(EditingTestHelper::texts(table->getRemoveDialog(), QStringLiteral("fingerprint")).contains(pdf::PDFAppCertificateEntry::getFingerprint(EditingTestHelper::certificate(0))));

    pdf::PDFDatabaseStatement replace(fixture.context->getDatabase()->getConnection(), "DELETE FROM trusted_certificate WHERE id = ?1");
    replace.bindInt64(1, confirmedId);
    QVERIFY(replace.execute());
    QVERIFY(!repository.addCertificate(EditingTestHelper::certificate(1)).isError());
    bool isIdReused = false;
    for (const pdf::PDFTrustedCertificate& row : repository.getRows())
    {
        isIdReused = isIdReused || (row.id == confirmedId && row.data == EditingTestHelper::certificate(1));
    }
    QVERIFY(isIdReused);

    QTest::mouseClick(table->getRemoveDialog()->findChild<QPushButton*>(QStringLiteral("removeButton")), Qt::LeftButton);
    fixture.settle();
    QVERIFY(repository.contains(EditingTestHelper::certificate(1)));
    QVERIFY(repository.contains(EditingTestHelper::certificate(3)));
    QCOMPARE(EditingTestHelper::userRowCount(fixture.context.get()), 2);
    QTRY_COMPARE(model->rowCount(), 2);
}

void AppSettingsEditingTest::certificateTableWorkerAfterPageGone()
{
    pdf::PDFAppCertificateTable::setSystemCertificatesProvider(&EditingTestHelper::slowSystemCertificates);
    EditingTestHelper::closeSystemStoreGate();
    EditingTestHelper::s_systemStoreReadings = 0;
    const int timeout = 20000 * pdftest::AppDocumentFixture::getTimeoutFactor();

    // The page left while the system store is read
    {
        pdftest::AppSettingsFixture fixture;
        QPointer<pdf::PDFAppCertificateTable> table = EditingTestHelper::table(fixture.openSettings(QStringLiteral("security")));
        QVERIFY(table && table->isLoadingSystemCertificates());
        QVERIFY(table->getTrustListLabel()->text().contains(QStringLiteral("Reading")));
        fixture.navigate(QStringLiteral("/home"));
        QVERIFY(!table);
        EditingTestHelper::openSystemStoreGate();
        QTRY_COMPARE_WITH_TIMEOUT(EditingTestHelper::s_systemStoreReadings.load(), 1, timeout);
        fixture.settle();
    }

    // The window deleted, the store read twice (the setting changed meanwhile; the
    // reading with the store turned off does not ask the store)
    EditingTestHelper::closeSystemStoreGate();
    EditingTestHelper::s_systemStoreReadings = 0;
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppCertificateTable* table = EditingTestHelper::table(fixture.openSettings(QStringLiteral("security")));
        QVERIFY(table);
        QVERIFY(table->isLoadingSystemCertificates());
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::signaturesUseSystemStore(), false).isError());
        fixture.settle();
        QVERIFY(EditingTestHelper::waitForTable(table));
        QCOMPARE(table->getModel()->rowCount(), 0);
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::signaturesUseSystemStore(), true).isError());
        fixture.settle();
        QVERIFY(table->isLoadingSystemCertificates());
        delete fixture.window.data();
        fixture.settle();
        EditingTestHelper::openSystemStoreGate();
        QTRY_COMPARE_WITH_TIMEOUT(EditingTestHelper::s_systemStoreReadings.load(), 2, timeout);
        fixture.settle();
    }
}

void AppSettingsEditingTest::certificateTableGeometry()
{
    // card 147:3785: the rows 8 px inside the border at both sides, no empty band
    // below the last row, actions 147:3828 with the paddings 10 / 10 / 2 / 2
    pdftest::AppSettingsFixture fixture;
    QVERIFY(!pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).addCertificate(EditingTestHelper::certificate(0)).isError());
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("security"));
    pdf::PDFAppCertificateTable* table = EditingTestHelper::table(page);
    QVERIFY(EditingTestHelper::waitForTable(table));
    EditingTestHelper::scrollArea(page)->ensureWidgetVisible(table);
    fixture.settle();

    QWidget* card = table->findChild<QWidget*>(QStringLiteral("card"));
    QVERIFY(card);
    pdf::PDFTreeView* view = table->getView();
    const QRect viewport(view->viewport()->mapTo(card, QPoint(0, 0)), view->viewport()->size());
    QCOMPARE(viewport.left(), 1 + 8);
    QCOMPARE(card->width() - 1 - viewport.right(), 1 + 8);
    const int rows = table->getModel()->rowCount();
    QVERIFY(rows > 0 && rows <= pdf::PDFAppCertificateTable::MAXIMUM_VISIBLE_ROWS);
    QCOMPARE(view->visualRect(table->getModel()->index(rows - 1, 0)).bottom() + 1, view->viewport()->height());

    const QRect button(table->getRemoveButton()->mapTo(card, QPoint(0, 0)), table->getRemoveButton()->size());
    QCOMPARE(card->width() - 1 - button.right(), 1 + 8 + 2);
    QCOMPARE(card->height() - 1 - button.bottom(), 1 + 8 + 2);
    QCOMPARE(table->getStatusLabel()->mapTo(card, QPoint(0, 0)).x(), 1 + 8 + 10);
}

void AppSettingsEditingTest::speechEnginesAndVoices()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::speechVoice(), QStringLiteral("Nobody")).isError());
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("speech"));
    pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(page);
    QVERIFY(section);
    QTRY_VERIFY(section->getEngine());

    // The engines of the system by their names (944:9031), the id is the tooltip
    {
        pdf::PDFComboBox* engines = section->getEngineComboBox();
        QCOMPARE(EditingTestHelper::itemTexts(engines), QStringList({ QStringLiteral("Automatic"), QStringLiteral("Windows (WinRT)"), QStringLiteral("Windows (SAPI 5)"), QStringLiteral("custom-x") }));
        QCOMPARE(engines->itemData(1, Qt::ToolTipRole).toString(), QStringLiteral("winrt"));
        QCOMPARE(engines->currentIndex(), 0);
        QCOMPARE(engines->width(), 260);
        QCOMPARE(pdf::PDFAppSpeechSettingsSection::getEngineTitle(QStringLiteral("speechd")), QStringLiteral("Speech Dispatcher"));
        QCOMPARE(pdf::PDFAppSpeechSettingsSection::getEngineTitle(QStringLiteral("flite")), QStringLiteral("Festival Lite"));
        QCOMPARE(pdf::PDFAppSpeechSettingsSection::getEngineTitle(QStringLiteral("darwin")), QStringLiteral("macOS"));
    }

    // Locales and voices of the engine; the stored voice it does not offer
    pdf::PDFComboBox* locales = section->getLocaleComboBox();
    pdf::PDFComboBox* voices = section->getVoiceComboBox();
    QVERIFY(locales->isEnabled() && voices->isEnabled());
    QCOMPARE(EditingTestHelper::itemTexts(locales).value(0), QStringLiteral("English (United States)"));
    QVERIFY(EditingTestHelper::itemTexts(locales).value(1).startsWith(QStringLiteral("Deutsch")));
    QCOMPARE(locales->currentIndex(), 0);
    QCOMPARE(EditingTestHelper::itemTexts(voices), QStringList({ QStringLiteral("Aria (Female, Adult)"), QStringLiteral("Guy (Male, Adult)"), QStringLiteral("Nobody (not available)") }));
    QCOMPARE(voices->currentIndex(), 2);

    // The item "(not available)" is never written again; a voice of the engine is
    QVERIFY(EditingTestHelper::chooseItem(voices, QStringLiteral("Aria")));
    QVERIFY(EditingTestHelper::chooseItem(voices, QStringLiteral("Guy")));
    QTRY_COMPARE(fixture.settings().getString(pdf::PDFSettingKeys::speechVoice()), QStringLiteral("Guy"));
    QCOMPARE(EditingTestHelper::itemTexts(voices).size(), 2);
    QCOMPARE(SettingsFakeSpeechEngine::s_last->voice, QStringLiteral("Guy"));

    // Another locale: the voice of the old locale is reset (as the Speech panel does)
    QVERIFY(EditingTestHelper::chooseItem(locales, QStringLiteral("de_DE")));
    QTRY_COMPARE(fixture.settings().getString(pdf::PDFSettingKeys::speechLocale()), QStringLiteral("de_DE"));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::speechVoice()), QString());
    QTRY_COMPARE(EditingTestHelper::itemTexts(voices), QStringList({ QStringLiteral("Hedda (Female, Adult)") }));

    // Another engine: a new engine of the section
    QPointer<pdf::PDFAppSpeechEngine> oldEngine = section->getEngine();
    QVERIFY(EditingTestHelper::chooseItem(section->getEngineComboBox(), QStringLiteral("sapi")));
    QTRY_COMPARE(fixture.settings().getString(pdf::PDFSettingKeys::speechEngine()), QStringLiteral("sapi"));
    QTRY_VERIFY(section->getEngine() && section->getEngine() != oldEngine.data());
    QCOMPARE(SettingsFakeSpeechEngine::s_last->engineName, QStringLiteral("sapi"));
    QCOMPARE(SettingsFakeSpeechEngine::s_last->locale.language(), QLocale::German);

    // An engine of another computer is shown, not offered
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::speechEngine(), QStringLiteral("gone")).isError());
    fixture.settle();
    QCOMPARE(section->getEngineComboBox()->currentText(), QStringLiteral("gone (not available)"));
    QCOMPARE(SettingsFakeSpeechEngine::s_last->engineName, QStringLiteral("gone"));
    const int index = section->getEngineComboBox()->currentIndex();
    Q_EMIT section->getEngineComboBox()->activated(index);
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::speechEngine()), QStringLiteral("gone"));

    // A locale of another computer
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::speechLocale(), QStringLiteral("xx_YY")).isError());
    fixture.settle();
    QCOMPARE(locales->currentText(), QStringLiteral("xx_YY (not available)"));
    Q_EMIT locales->activated(locales->currentIndex());
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::speechLocale()), QStringLiteral("xx_YY"));

    // The values of the restore are shown by their names
    bool isEngineChange = false;
    for (const pdf::PDFAppSettingChange& change : section->getChangedValues())
    {
        if (change.key == pdf::PDFSettingKeys::speechEngine())
        {
            isEngineChange = true;
            QCOMPARE(change.defaultValue, QStringLiteral("Automatic"));
        }
    }
    QVERIFY(isEngineChange);
}

void AppSettingsEditingTest::speechEngineAsynchronous()
{
    // An engine, which starts asynchronously: the combo boxes wait for it
    SettingsFakeSpeechEngine::s_initialState = pdf::PDFAppSpeechEngine::State::Initializing;
    pdftest::AppSettingsFixture fixture;
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::speechLocale(), QStringLiteral("de_DE")).isError());
    pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(fixture.openSettings(QStringLiteral("speech")));
    QVERIFY(section);
    QTRY_VERIFY(section->getEngine());
    QVERIFY(!section->getLocaleComboBox()->isEnabled());
    QVERIFY(!section->getVoiceComboBox()->isEnabled());
    QVERIFY(section->getLocaleComboBox()->currentText().startsWith(QStringLiteral("Deutsch")));
    QCOMPARE(section->getVoiceComboBox()->currentText(), QStringLiteral("Automatic"));
    QVERIFY(!section->getPlayButton()->isEnabled());

    static_cast<SettingsFakeSpeechEngine*>(section->getEngine())->becomeReady();
    fixture.settle();
    QVERIFY(section->getLocaleComboBox()->isEnabled());
    QCOMPARE(section->getLocaleComboBox()->count(), 2);
    QVERIFY(section->getVoiceComboBox()->isEnabled());
    QVERIFY(section->getPlayButton()->isEnabled());
}

void AppSettingsEditingTest::speechEngineError()
{
    SettingsFakeSpeechEngine::s_isBroken = true;
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(fixture.openSettings(QStringLiteral("speech")));
    QVERIFY(section);
    QTRY_VERIFY(section->getEngine());

    // The synthesizer says it, its locale and voice are disabled, another one can be chosen
    QCOMPARE(section->getEngineRow()->getDescription(), QStringLiteral("The voice synthesizer is not available"));
    QCOMPARE(section->getEngineRow()->toolTip(), QStringLiteral("No speech daemon"));
    QVERIFY(!section->getLocaleComboBox()->isEnabled());
    QVERIFY(!section->getVoiceComboBox()->isEnabled());
    QVERIFY(!section->getPlayButton()->isEnabled());
    QVERIFY(section->getEngineComboBox()->isEnabled());
    section->playSample();
    QVERIFY(!section->isPlaying());

    SettingsFakeSpeechEngine::s_isBroken = false;
    QVERIFY(EditingTestHelper::chooseItem(section->getEngineComboBox(), QStringLiteral("winrt")));
    QTRY_COMPARE(section->getEngineRow()->getDescription(), QStringLiteral("Speech engine provided by the system"));
    QVERIFY(section->getLocaleComboBox()->isEnabled());
    QVERIFY(section->getPlayButton()->isEnabled());
}

void AppSettingsEditingTest::speechSameAsPanel()
{
    // The section and the speech of a document use the same keys: a change of one
    // applies to the other at once
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QVERIFY(tab);
    QTRY_VERIFY(tab->getDocument() && tab->getDocument()->isReady());
    QPointer<pdf::PDFAppDocumentSpeechController> controller = EditingTestHelper::searchHandler(tab)->getSpeechController(true);
    QVERIFY(controller && controller->getEngine());
    SettingsFakeSpeechEngine* documentEngine = static_cast<SettingsFakeSpeechEngine*>(controller->getEngine());

    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("speech"));
    pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(page);
    QVERIFY(section);
    QTRY_VERIFY(section->getEngine());

    QVERIFY(EditingTestHelper::chooseItem(section->getLocaleComboBox(), QStringLiteral("de_DE")));
    QTRY_COMPARE(documentEngine->locale.language(), QLocale::German);
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("volume")), 0.5));
    QTRY_COMPARE(documentEngine->volume, 0.5);
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("rate")), 0.3));
    QTRY_COMPARE(documentEngine->rate, 0.3);

    // The panel (its controller) writes, the section shows it
    controller->setPitch(-0.4);
    QTRY_COMPARE(fixture.findRow(QStringLiteral("pitch"))->getControl()->getShownValue().toDouble(), -0.4);
    controller->setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    QTRY_COMPARE(section->getLocaleComboBox()->currentData().toString(), QStringLiteral("en_US"));
    controller->setVoice(QStringLiteral("Guy"));
    QTRY_COMPARE(section->getVoiceComboBox()->currentData().toString(), QStringLiteral("Guy"));
}

void AppSettingsEditingTest::playSample()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::speechVolume(), 0.25).isError());
    pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(fixture.openSettings(QStringLiteral("speech")));
    QVERIFY(section);
    QTRY_VERIFY(section->getEngine());
    QPointer<SettingsFakeSpeechEngine> engine = static_cast<SettingsFakeSpeechEngine*>(section->getEngine());
    QPushButton* play = section->getPlayButton();
    const QRect playGeometry = play->geometry();

    // Play sample with the stored sound, Stop while it is said
    QTest::mouseClick(play, Qt::LeftButton);
    fixture.settle();
    QCOMPARE(engine->said, QStringList({ pdf::PDFAppSpeechSettingsSection::getSampleText() }));
    QCOMPARE(engine->volume, 0.25);
    QVERIFY(section->isPlaying());
    QCOMPARE(play->text(), QStringLiteral("Stop"));
    // The button keeps its place and width (stable buttons, FX5-2)
    QCOMPARE(play->geometry(), playGeometry);
    QVERIFY(play->width() >= play->sizeHint().width());
    QTest::mouseClick(play, Qt::LeftButton);
    fixture.settle();
    QVERIFY(!section->isPlaying());
    QCOMPARE(engine->state, pdf::PDFAppSpeechEngine::State::Ready);
    QCOMPARE(play->text(), QStringLiteral("Play sample"));

    // The end of the sample
    QTest::mouseClick(play, Qt::LeftButton);
    engine->finish();
    fixture.settle();
    QVERIFY(!section->isPlaying());
    QCOMPARE(play->text(), QStringLiteral("Play sample"));

    // Played again while it is said: from the start
    section->playSample();
    section->playSample();
    QCOMPARE(engine->said.size(), 4);
    QVERIFY(section->isPlaying());

    // The page left: the sample stops
    fixture.navigate(QStringLiteral("/home"));
    QVERIFY(!engine || engine->state == pdf::PDFAppSpeechEngine::State::Ready);

    // The window deleted while the sample is said
    {
        pdftest::AppSettingsFixture other;
        pdf::PDFAppSpeechSettingsSection* otherSection = EditingTestHelper::speechSection(other.openSettings(QStringLiteral("speech")));
        QVERIFY(otherSection);
        otherSection->playSample();
        QVERIFY(otherSection->isPlaying());
        delete other.window.data();
        other.settle();
    }

    // No engine is left behind (review RC5-BC): a new synthesizer replaces the
    // engine, the page deleted while the engine is starting or speaking deletes it
    fixture.settle();
    const int baseline = SettingsFakeSpeechEngine::s_alive;
    for (int variant = 0; variant < 2; ++variant)
    {
        pdftest::AppSettingsFixture other;
        pdf::PDFAppSpeechSettingsSection* otherSection = EditingTestHelper::speechSection(other.openSettings(QStringLiteral("speech")));
        QVERIFY(otherSection);
        QTRY_VERIFY(otherSection->getEngine());
        QCOMPARE(SettingsFakeSpeechEngine::s_alive, baseline + 1);

        QVERIFY(!other.settings().setValue(pdf::PDFSettingKeys::speechEngine(), QStringLiteral("sapi")).isError());
        QTRY_COMPARE(static_cast<SettingsFakeSpeechEngine*>(otherSection->getEngine())->engineName, QStringLiteral("sapi"));
        other.settle();
        QCOMPARE(SettingsFakeSpeechEngine::s_alive, baseline + 1);

        if (variant == 0)
        {
            // The new engine is starting, when the page goes
            SettingsFakeSpeechEngine::s_initialState = pdf::PDFAppSpeechEngine::State::Initializing;
            QVERIFY(!other.settings().setValue(pdf::PDFSettingKeys::speechEngine(), QStringLiteral("winrt")).isError());
            QTRY_COMPARE(static_cast<SettingsFakeSpeechEngine*>(otherSection->getEngine())->engineName, QStringLiteral("winrt"));
            QCOMPARE(otherSection->getEngine()->getState(), pdf::PDFAppSpeechEngine::State::Initializing);
            other.navigate(QStringLiteral("/home"));
        }
        else
        {
            // The sample is said, when the window goes
            otherSection->playSample();
            QVERIFY(otherSection->isPlaying());
            delete other.window.data();
        }
        other.settle();
        QCOMPARE(SettingsFakeSpeechEngine::s_alive, baseline);
        SettingsFakeSpeechEngine::s_initialState = pdf::PDFAppSpeechEngine::State::Ready;
    }
}

void AppSettingsEditingTest::hostileStoredValues()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppContext* context = fixture.context.get();
    QVERIFY(EditingTestHelper::storeRaw(context, pdf::PDFSettingKeys::editingUndoLimit(), QStringLiteral("abc")));
    QVERIFY(EditingTestHelper::storeRaw(context, pdf::PDFSettingKeys::editingRedoLimit(), qint64(99999)));
    QVERIFY(EditingTestHelper::storeRaw(context, pdf::PDFSettingKeys::annotationsAuthorMode(), QStringLiteral("root")));
    QVERIFY(EditingTestHelper::storeRaw(context, pdf::PDFSettingKeys::signaturesUseSystemStore(), QStringLiteral("maybe")));
    QVERIFY(EditingTestHelper::storeRaw(context, pdf::PDFSettingKeys::speechEngine(), QString(65536, QLatin1Char('e'))));
    QVERIFY(EditingTestHelper::storeRaw(context, pdf::PDFSettingKeys::speechLocale(), qint64(5)));
    QVERIFY(EditingTestHelper::storeRaw(context, pdf::PDFSettingKeys::speechVoice(), QStringLiteral("Evil") + QChar(0x202E) + QChar(0x0007) + QStringLiteral("voice")));
    QVERIFY(EditingTestHelper::storeRaw(context, pdf::PDFSettingKeys::speechVolume(), std::numeric_limits<double>::quiet_NaN()));

    QVERIFY(fixture.openSettings(QStringLiteral("editing")));
    QCOMPARE(fixture.findRow(QStringLiteral("maximum-undo-steps"))->getControl()->getShownValue(), QVariant(qint64(100)));
    QCOMPARE(fixture.findRow(QStringLiteral("maximum-redo-steps"))->getControl()->getShownValue(), QVariant(qint64(100)));
    QCOMPARE(fixture.findRow(QStringLiteral("author-of-new-annotations"))->getControl()->getShownValue(), QVariant(pdf::PDFSettingValues::authorModeAnonymous()));

    pdf::PDFAppCertificateTable* table = EditingTestHelper::table(fixture.openSettings(QStringLiteral("security")));
    QVERIFY(EditingTestHelper::waitForTable(table));
    QCOMPARE(table->getModel()->getCount(pdf::PDFAppCertificateEntry::Type::System), 2);

    pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(fixture.openSettings(QStringLiteral("speech")));
    QVERIFY(section);
    QTRY_VERIFY(section->getEngine());
    QCOMPARE(section->getEngineComboBox()->currentText(), QStringLiteral("Automatic"));
    QCOMPARE(SettingsFakeSpeechEngine::s_last->engineName, QString());
    const QString voice = section->getVoiceComboBox()->currentText();
    QVERIFY(voice.endsWith(QStringLiteral("(not available)")));
    QVERIFY(!voice.contains(QChar(0x202E)) && !voice.contains(QChar(0x0007)));
    QCOMPARE(fixture.findRow(QStringLiteral("volume"))->getControl()->getShownValue().toDouble(), 1.0);

    // A document applies the defaults of the hostile limits
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QTRY_VERIFY(tab && tab->getDocument() && tab->getDocument()->isReady());
    QVERIFY(EditingTestHelper::change(tab->getDocument(), QStringLiteral("X")));
    QCOMPARE(tab->getDocument()->getUndoStack()->getLimit(), 100);
    QCOMPARE(tab->getDocument()->getUndoStack()->getRedoLimit(), 100);

    // Nothing has been written back
    QCOMPARE(fixture.settings().getStoredValue(pdf::PDFSettingKeys::editingRedoLimit()), QVariant(qint64(99999)));
    QCOMPARE(fixture.settings().getStoredValue(pdf::PDFSettingKeys::speechLocale()), QVariant(qint64(5)));
}

void AppSettingsEditingTest::otherInstanceChange()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(fixture.openSettings(QStringLiteral("editing")));
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::editingRedoLimit(), qint64(7)));
    QTRY_COMPARE(fixture.findRow(QStringLiteral("maximum-redo-steps"))->getControl()->getShownValue(), QVariant(qint64(7)));
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::annotationsAuthorMode(), pdf::PDFSettingValues::authorModeCustomName()));
    QTRY_VERIFY(fixture.findRow(QStringLiteral("custom-author-name"))->getControlWrap()->isEnabled());

    pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(fixture.openSettings(QStringLiteral("speech")));
    QVERIFY(section);
    QTRY_VERIFY(section->getEngine());
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::speechEngine(), QStringLiteral("sapi")));
    QTRY_COMPARE(section->getEngineComboBox()->currentData().toString(), QStringLiteral("sapi"));
    QTRY_COMPARE(SettingsFakeSpeechEngine::s_last->engineName, QStringLiteral("sapi"));
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::speechVoice(), QStringLiteral("Guy")));
    QTRY_COMPARE(section->getVoiceComboBox()->currentData().toString(), QStringLiteral("Guy"));
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::speechRate(), 0.5));
    QTRY_COMPARE(SettingsFakeSpeechEngine::s_last->rate, 0.5);
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::speechEngine(), QVariant()));
    QTRY_COMPARE(section->getEngineComboBox()->currentIndex(), 0);

    pdf::PDFAppCertificateTable* table = EditingTestHelper::table(fixture.openSettings(QStringLiteral("security")));
    QVERIFY(EditingTestHelper::waitForTable(table));
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::securityOpenLinks(), false));
    QTRY_COMPARE(fixture.findRow(QStringLiteral("allow-uri-link-launch"))->getControl()->getShownValue(), QVariant(false));
}

void AppSettingsEditingTest::restoreDefaults()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFTrustedCertificateRepository repository(fixture.context->getDatabase());
    QVERIFY(!repository.addCertificate(EditingTestHelper::certificate(0)).isError());

    // Editing
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("editing"));
    QVERIFY(!page->getRestoreButton()->isEnabled());
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("maximum-redo-steps")), qint64(5)));
    QVERIFY(page->openRestoreDefaultsDialog());
    pdf::PDFAppRestoreDefaultsDialog* dialog = page->getRestoreDialog();
    QCOMPARE(dialog->getChangeCount(), 1);
    QCOMPARE(dialog->getFacts()->getValue(0), QString::fromUtf8("5 → 100"));
    QTest::mouseClick(dialog->getRestoreButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::editingRedoLimit()), qint64(100));
    QTRY_COMPARE(fixture.findRow(QStringLiteral("maximum-redo-steps"))->getControl()->getShownValue(), QVariant(qint64(100)));

    // Security: the certificates stay (the text says so)
    page = fixture.openSettings(QStringLiteral("security"));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("allow-uri-link-launch")), false));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("strict-mode")), true));
    QVERIFY(page->openRestoreDefaultsDialog());
    dialog = page->getRestoreDialog();
    QCOMPARE(dialog->getChangeCount(), 2);
    QVERIFY(EditingTestHelper::dialogTexts(dialog).join(QLatin1Char(' ')).contains(QStringLiteral("trusted certificates")));
    QTest::mouseClick(dialog->getRestoreButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::securityOpenLinks()), true);
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::signaturesStrict()), false);
    QCOMPARE(EditingTestHelper::userRowCount(fixture.context.get()), 1);
    QCOMPARE(EditingTestHelper::table(page)->getModel()->getCount(pdf::PDFAppCertificateEntry::Type::User), 1);
    QVERIFY(dialog == nullptr || !page->getRestoreDialog());
    QCOMPARE(pdf::PDFAppSecuritySettingsSection(fixture.context.get(), QStringLiteral("security")).getRestoreDefaultsText(1).contains(QStringLiteral("trusted certificates")), true);
    {
        // The count of more settings is a plural of the locale (%Ln), not a plain number
        const QString text = pdf::PDFAppSecuritySettingsSection(fixture.context.get(), QStringLiteral("security")).getRestoreDefaultsText(1234);
        QVERIFY2(text.startsWith(QStringLiteral("These 1,234 settings of the ")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("trusted certificates")));
        QVERIFY(!text.contains(QStringLiteral("%")));
    }

    // Speech: the names of the engine and the locale, a new engine after the restore
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::speechEngine(), QStringLiteral("sapi")).isError());
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::speechLocale(), QStringLiteral("de_DE")).isError());
    page = fixture.openSettings(QStringLiteral("speech"));
    pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(page);
    QTRY_VERIFY(section->getEngine());
    QCOMPARE(SettingsFakeSpeechEngine::s_last->engineName, QStringLiteral("sapi"));
    QVERIFY(page->openRestoreDefaultsDialog());
    dialog = page->getRestoreDialog();
    QStringList values;
    for (int i = 0; i < dialog->getFacts()->getRowCount(); ++i)
    {
        values << dialog->getFacts()->getValue(i);
    }
    QVERIFY2(values.contains(QString::fromUtf8("Windows (SAPI 5) → Automatic")), qPrintable(values.join(QLatin1Char('|'))));
    QVERIFY2(values.contains(QString::fromUtf8("Deutsch (Deutschland) – German (Germany) → Automatic")), qPrintable(values.join(QLatin1Char('|'))));
    QTest::mouseClick(dialog->getRestoreButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::speechEngine()), QString());
    QTRY_COMPARE(SettingsFakeSpeechEngine::s_last->engineName, QString());
    QCOMPARE(section->getEngineComboBox()->currentIndex(), 0);
}

void AppSettingsEditingTest::searchAndAnchors()
{
    pdftest::AppSettingsFixture fixture;

    // The anchor of the table shows it
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("security"), pdf::PDFAppSecuritySettingsSection::getCertificatesAnchor());
    QVERIFY(page);
    QCOMPARE(page->getSection()->findAnchorWidget(pdf::PDFAppSecuritySettingsSection::getCertificatesAnchor()), EditingTestHelper::table(page));
    QVERIFY(EditingTestHelper::table(page)->isVisible());
    QVERIFY(!page->getSection()->findAnchorWidget(QStringLiteral("unknown")));

    // The rows of the package by their anchors
    page = fixture.openSettings(QStringLiteral("speech"), QStringLiteral("voice-synthesizer"));
    QVERIFY(page->findRow(QStringLiteral("voice-synthesizer"))->isHighlighted());
    page = fixture.openSettings(QStringLiteral("editing"), QStringLiteral("maximum-redo-steps"));
    QVERIFY(page->findRow(QStringLiteral("maximum-redo-steps"))->isHighlighted());

    // The filter: the table by its texts, the custom rows by their keywords
    page = fixture.openFilter(QStringLiteral("trusted certificates"));
    QVERIFY(page && page->isFiltering());
    pdf::PDFAppCertificateTable* table = nullptr;
    for (pdf::PDFAppSettingsSection* section : page->getSections())
    {
        if (pdf::PDFAppSecuritySettingsSection* security = qobject_cast<pdf::PDFAppSecuritySettingsSection*>(section))
        {
            table = security->getCertificateTable();
            QVERIFY(!security->isHidden());
        }
    }
    QVERIFY(table && table->isVisible());

    page = fixture.openFilter(QStringLiteral("aatl"));
    QVERIFY(page->getEmptyState()->isHidden());

    page = fixture.openFilter(QStringLiteral("play sample"));
    QVERIFY(page->findRow(QStringLiteral("preview")) && !page->findRow(QStringLiteral("preview"))->isHidden());
    QVERIFY(page->findRow(QStringLiteral("allow-uri-link-launch"))->isHidden());

    page = fixture.openFilter(QStringLiteral("synthesizer"));
    QVERIFY(!page->findRow(QStringLiteral("voice-synthesizer"))->isHidden());
    QVERIFY(page->findRow(QStringLiteral("rate"))->isHidden());

    page = fixture.openFilter(QStringLiteral("redo"));
    QVERIFY(!page->findRow(QStringLiteral("maximum-redo-steps"))->isHidden());

    // A query of no row of the package hides the table
    page = fixture.openFilter(QStringLiteral("magnifier"));
    for (pdf::PDFAppSettingsSection* section : page->getSections())
    {
        if (pdf::PDFAppSecuritySettingsSection* security = qobject_cast<pdf::PDFAppSecuritySettingsSection*>(section))
        {
            QVERIFY(!security->getCertificateTable()->isVisible());
        }
    }
}

void AppSettingsEditingTest::deletionMatrix()
{
    // The page left with the confirmation of Remove open: nothing is removed
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(!pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).addCertificate(EditingTestHelper::certificate(0)).isError());
        pdf::PDFAppCertificateTable* table = EditingTestHelper::table(fixture.openSettings(QStringLiteral("security")));
        QVERIFY(EditingTestHelper::waitForTable(table));
        EditingTestHelper::clickRow(table->getView(), 0);
        QVERIFY(table->openRemoveDialog());
        QPointer<pdf::PDFAppCardDialog> dialog = table->getRemoveDialog();
        fixture.navigate(QStringLiteral("/home"));
        QVERIFY(!dialog);
        QCOMPARE(EditingTestHelper::userRowCount(fixture.context.get()), 1);
    }

    // The window deleted with the confirmation open
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(!pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).addCertificate(EditingTestHelper::certificate(0)).isError());
        pdf::PDFAppCertificateTable* table = EditingTestHelper::table(fixture.openSettings(QStringLiteral("security")));
        QVERIFY(EditingTestHelper::waitForTable(table));
        EditingTestHelper::clickRow(table->getView(), 0);
        QVERIFY(table->openRemoveDialog());
        QPointer<pdf::PDFAppCardDialog> dialog = table->getRemoveDialog();
        delete fixture.window.data();
        fixture.settle();
        QVERIFY(!dialog);
        QCOMPARE(EditingTestHelper::userRowCount(fixture.context.get()), 1);
    }

    // The table deleted by the application, the dialog open; the section and its rows
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(!pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).addCertificate(EditingTestHelper::certificate(0)).isError());
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("security"));
        pdf::PDFAppCertificateTable* table = EditingTestHelper::table(page);
        QVERIFY(EditingTestHelper::waitForTable(table));
        EditingTestHelper::clickRow(table->getView(), 0);
        QVERIFY(table->openRemoveDialog());
        QPointer<pdf::PDFAppCardDialog> dialog = table->getRemoveDialog();
        delete table->getView();
        delete table->getModel();
        QVERIFY(!table->openRemoveDialog() || dialog);
        delete table;
        fixture.settle();
        QVERIFY(!dialog);
        QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::signaturesUseSystemStore(), false));
        page->getSection()->refresh(QString(), QString());
        delete page->getSection();
        fixture.settle();
    }

    // A choice of a combo box written by the event loop, the page gone before it
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(fixture.openSettings(QStringLiteral("speech")));
        QTRY_VERIFY(section->getEngine());
        pdf::PDFComboBox* voices = section->getVoiceComboBox();
        voices->setFocus();
        QTest::keyClick(voices, Qt::Key_Down);
        QVERIFY(fixture.settings().getString(pdf::PDFSettingKeys::speechVoice()).isEmpty());
        delete fixture.window.data();
        fixture.settle();
        QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::speechVoice()), QStringLiteral("Guy"));
    }

    // An open popup of a combo box, a sample said: the window goes
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(fixture.openSettings(QStringLiteral("speech")));
        QTRY_VERIFY(section->getEngine());
        section->playSample();
        section->getEngineComboBox()->showPopup();
        fixture.settle();
        delete fixture.window.data();
        fixture.settle();
    }

    // The engine deleted by the application, the parts of the section deleted
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(fixture.openSettings(QStringLiteral("speech")));
        QTRY_VERIFY(section->getEngine());
        delete section->getEngine();
        section->playSample();
        QTRY_VERIFY(section->getEngine());
        QVERIFY(section->isPlaying());
        delete section->getVoiceComboBox();
        delete section->getPlayButton();
        delete section->getEngineRow();
        QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::speechEngine(), QStringLiteral("sapi")));
        section->stopSample();
        section->commitPendingValues();
        fixture.navigate(QStringLiteral("/home"));
    }

    // The context closed before the page
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("security")));
        fixture.context->close();
        fixture.navigate(QStringLiteral("/settings/speech"));
        delete fixture.window.data();
        fixture.settle();
    }
}

void AppSettingsEditingTest::deleteFromOwnSignals()
{
    // The window deleted synchronously from the status message of a refused write
    // of a combo box (written from the event loop)
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(fixture.openSettings(QStringLiteral("speech")));
        QTRY_VERIFY(section->getEngine());
        pdf::PDFDatabaseStatement block(fixture.context->getDatabase()->getConnection(), "CREATE TRIGGER block_setting BEFORE INSERT ON setting BEGIN SELECT RAISE(ABORT, 'blocked'); END");
        QVERIFY(block.execute());
        QObject::connect(section, &pdf::PDFAppSettingsSection::statusMessageRequested, fixture.window.data(), [&fixture]() { delete fixture.window.data(); });
        QVERIFY(EditingTestHelper::chooseItem(section->getVoiceComboBox(), QStringLiteral("Guy")) || !fixture.window);
        fixture.settle();
        QVERIFY(!fixture.window);
    }

    // The status message of a refused write of a combo box is shown by the page
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("speech"));
        pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(page);
        QTRY_VERIFY(section->getEngine());
        pdf::PDFDatabaseStatement block(fixture.context->getDatabase()->getConnection(), "CREATE TRIGGER block_setting BEFORE INSERT ON setting BEGIN SELECT RAISE(ABORT, 'blocked'); END");
        QVERIFY(block.execute());
        EditingTestHelper::chooseItem(section->getEngineComboBox(), QStringLiteral("sapi"));
        fixture.settle();
        QVERIFY(page->getStatusLabel()->text().contains(QStringLiteral("could not be saved")));
        QCOMPARE(section->getEngineComboBox()->currentIndex(), 0);
    }

    // The window deleted from the state of the engine, while the sample starts
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(fixture.openSettings(QStringLiteral("speech")));
        QTRY_VERIFY(section->getEngine());
        bool isDeleted = false;
        QObject::connect(section->getEngine(), &pdf::PDFAppSpeechEngine::stateChanged, fixture.window.data(), [&fixture, &isDeleted]()
        {
            if (!isDeleted)
            {
                isDeleted = true;
                delete fixture.window.data();
            }
        });
        QTest::mouseClick(section->getPlayButton(), Qt::LeftButton);
        fixture.settle();
        QVERIFY(!fixture.window);
    }

    // The window deleted from the end of the confirmation of Remove (later: the
    // dialog is its child); the certificate is removed before it goes
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(!pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).addCertificate(EditingTestHelper::certificate(0)).isError());
        pdf::PDFAppCertificateTable* table = EditingTestHelper::table(fixture.openSettings(QStringLiteral("security")));
        QVERIFY(EditingTestHelper::waitForTable(table));
        EditingTestHelper::clickRow(table->getView(), 0);
        QVERIFY(table->openRemoveDialog());
        pdf::PDFAppCardDialog* dialog = table->getRemoveDialog();
        QObject::connect(dialog, &QDialog::finished, fixture.window.data(), [&fixture]() { fixture.window->deleteLater(); });
        QTest::mouseClick(dialog->findChild<QPushButton*>(QStringLiteral("removeButton")), Qt::LeftButton);
        fixture.settle();
        QVERIFY(!fixture.window);
        QCOMPARE(EditingTestHelper::userRowCount(fixture.context.get()), 0);
    }

    // The window deleted synchronously from the status message of a refused removal
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(!pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).addCertificate(EditingTestHelper::certificate(0)).isError());
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("security"));
        pdf::PDFAppCertificateTable* table = EditingTestHelper::table(page);
        QVERIFY(EditingTestHelper::waitForTable(table));
        pdf::PDFDatabaseStatement block(fixture.context->getDatabase()->getConnection(), "CREATE TRIGGER block_certificate BEFORE DELETE ON trusted_certificate BEGIN SELECT RAISE(ABORT, 'blocked'); END");
        QVERIFY(block.execute());
        QObject::connect(table, &pdf::PDFAppCertificateTable::statusMessageRequested, fixture.window.data(), [&fixture]() { delete fixture.window.data(); });
        EditingTestHelper::clickRow(table->getView(), 0);
        QVERIFY(table->openRemoveDialog());
        QTest::mouseClick(table->getRemoveDialog()->findChild<QPushButton*>(QStringLiteral("removeButton")), Qt::LeftButton);
        fixture.settle();
        QVERIFY(!fixture.window);
    }

    // The window deleted from the notification of the removal
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(!pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).addCertificate(EditingTestHelper::certificate(0)).isError());
        pdf::PDFAppCertificateTable* table = EditingTestHelper::table(fixture.openSettings(QStringLiteral("security")));
        QVERIFY(EditingTestHelper::waitForTable(table));
        bool isDeleted = false;
        QObject::connect(fixture.context->getNotifier(), &pdf::PDFDatabaseNotifier::changed, fixture.window.data(), [&fixture, &isDeleted](const QString& topic)
        {
            if (!isDeleted && topic == pdf::PDFTrustedCertificateRepository::getTopic())
            {
                isDeleted = true;
                delete fixture.window.data();
            }
        });
        EditingTestHelper::clickRow(table->getView(), 0);
        QVERIFY(table->openRemoveDialog());
        QTest::mouseClick(table->getRemoveDialog()->findChild<QPushButton*>(QStringLiteral("removeButton")), Qt::LeftButton);
        fixture.settle();
        QTRY_VERIFY(!fixture.window);
    }
}

void AppSettingsEditingTest::themesAndScales()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(!pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).addCertificate(EditingTestHelper::certificate(0)).isError());
    QVERIFY(EditingTestHelper::storeRawCertificate(fixture.context.get(), QByteArray("damaged")));
    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        // The scheme of the context (Auto would follow the system)
        const bool isDark = scheme == pdf::PDFThemeColors::Scheme::Dark;
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::colorScheme(), isDark ? QStringLiteral("dark") : QStringLiteral("light")).isError());
        fixture.settle();
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        QCOMPARE(pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgSurface).lightness() < 128, isDark);
        for (const QString& sectionId : { QStringLiteral("editing"), QStringLiteral("security"), QStringLiteral("speech") })
        {
            pdf::PDFAppSettingsPage* page = fixture.openSettings(sectionId);
            QVERIFY(page);
            if (pdf::PDFAppCertificateTable* table = EditingTestHelper::table(page))
            {
                QVERIFY(EditingTestHelper::waitForTable(table));
                EditingTestHelper::clickRow(table->getView(), 0);
            }
            if (pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(page))
            {
                QTRY_VERIFY(section->getEngine());
                section->playSample();
            }
            for (qreal ratio : { 1.0, 1.5, 2.0 })
            {
                const QImage image = EditingTestHelper::render(page, ratio);
                QVERIFY2(EditingTestHelper::countLeakPixels(image) == 0, qPrintable(QStringLiteral("%1 at %2").arg(sectionId).arg(ratio)));
            }

            // The confirmation of Remove
            if (pdf::PDFAppCertificateTable* table = EditingTestHelper::table(page))
            {
                QVERIFY(table->openRemoveDialog());
                fixture.settle();
                for (qreal ratio : { 1.0, 2.0 })
                {
                    QVERIFY(EditingTestHelper::countLeakPixels(EditingTestHelper::render(table->getRemoveDialog(), ratio)) == 0);
                }
                table->getRemoveDialog()->reject();
                fixture.settle();
            }
        }
    }

    // The colors of the table follow the theme (data of the model)
    pdf::PDFAppCertificateTable* table = EditingTestHelper::table(fixture.openSettings(QStringLiteral("security")));
    QVERIFY(EditingTestHelper::waitForTable(table));
    pdf::PDFAppCertificateTableModel* model = table->getModel();
    const QModelIndex damaged = model->index(1, pdf::PDFAppCertificateTableModel::NameColumn);
    QCOMPARE(qvariant_cast<QBrush>(model->data(damaged, Qt::ForegroundRole)).color(), pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::TextDanger));
    QCOMPARE(qvariant_cast<QBrush>(model->data(model->index(0, pdf::PDFAppCertificateTableModel::OrganizationColumn), Qt::ForegroundRole)).color(),
             pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::TextSecondary));
}

void AppSettingsEditingTest::rightToLeft()
{
    QApplication::setLayoutDirection(Qt::RightToLeft);
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(!pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).addCertificate(EditingTestHelper::certificate(0)).isError());
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("security"));
        pdf::PDFAppCertificateTable* table = EditingTestHelper::table(page);
        QVERIFY(EditingTestHelper::waitForTable(table));

        // The columns from the right, Remove at the left of the status
        QHeaderView* header = table->getView()->header();
        QVERIFY(header->sectionViewportPosition(pdf::PDFAppCertificateTableModel::TypeColumn) > header->sectionViewportPosition(pdf::PDFAppCertificateTableModel::NameColumn));
        QVERIFY(table->getRemoveButton()->mapTo(table, QPoint(0, 0)).x() < table->getStatusLabel()->mapTo(table, QPoint(0, 0)).x());
        QCOMPARE(EditingTestHelper::countLeakPixels(EditingTestHelper::render(page, 1.0)), 0);
        fixture.screenshot(QStringLiteral("settings-security-rtl"));

        page = fixture.openSettings(QStringLiteral("speech"));
        pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(page);
        QTRY_VERIFY(section->getEngine());
        pdf::PDFAppSettingRow* preview = section->getPreviewRow();
        QVERIFY(preview->getControlWrap()->x() < preview->getTitleLabel()->mapTo(preview, QPoint(0, 0)).x());
        QCOMPARE(EditingTestHelper::countLeakPixels(EditingTestHelper::render(page, 1.0)), 0);
        fixture.screenshot(QStringLiteral("settings-speech-rtl"));
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppSettingsEditingTest::minimalWindow()
{
    pdftest::AppSettingsFixture fixture(QSize(720, 480));
    QVERIFY(!pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).addCertificate(EditingTestHelper::certificate(0)).isError());
    for (const QString& sectionId : { QStringLiteral("editing"), QStringLiteral("security"), QStringLiteral("speech") })
    {
        pdf::PDFAppSettingsPage* page = fixture.openSettings(sectionId);
        QVERIFY(page);
        fixture.settle();
        QScrollArea* area = EditingTestHelper::scrollArea(page);
        QVERIFY(area);
        QVERIFY2(area->horizontalScrollBar()->maximum() == 0 || !area->horizontalScrollBar()->isVisible(), qPrintable(sectionId));
        for (pdf::PDFAppSettingRow* row : page->getSection()->getRows())
        {
            const QRect control(row->getControlWrap()->mapTo(page, QPoint(0, 0)), row->getControlWrap()->size());
            QVERIFY2(control.right() <= page->width(), qPrintable(row->getAnchor()));
        }
        if (pdf::PDFAppCertificateTable* table = EditingTestHelper::table(page))
        {
            QVERIFY(EditingTestHelper::waitForTable(table));
            fixture.settle();
            const QRect rect(table->mapTo(page, QPoint(0, 0)), table->size());
            QVERIFY(rect.right() <= page->width());
            const QRect button(table->getRemoveButton()->mapTo(page, QPoint(0, 0)), table->getRemoveButton()->size());
            QVERIFY(button.right() <= page->width());
            QVERIFY(table->getView()->header()->sectionSize(pdf::PDFAppCertificateTableModel::NameColumn) > 0);
        }
        fixture.screenshot(QStringLiteral("settings-%1-720").arg(sectionId));
    }
}

void AppSettingsEditingTest::keyboardAndAccessibility()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(!pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).addCertificate(EditingTestHelper::certificate(0)).isError());
    pdf::PDFAppCertificateTable* table = EditingTestHelper::table(fixture.openSettings(QStringLiteral("security")));
    QVERIFY(EditingTestHelper::waitForTable(table));
    QCOMPARE(table->getView()->accessibleName(), QStringLiteral("Trusted certificates"));
    QVERIFY(!table->getRemoveButton()->accessibleDescription().isEmpty());

    // The keyboard selects a row, Delete asks, Esc cancels
    table->getView()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(table->getView(), Qt::Key_Home);
    QTest::keyClick(table->getView(), Qt::Key_Space);
    fixture.settle();
    if (table->getSelectedEntries().empty())
    {
        table->getView()->selectionModel()->select(table->getModel()->index(0, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    QCOMPARE(table->getSelectedEntries().size(), size_t(1));
    QTest::keyClick(table->getView(), Qt::Key_Delete);
    fixture.settle();
    QPointer<pdf::PDFAppCardDialog> dialog = table->getRemoveDialog();
    QVERIFY(dialog && dialog->isVisible());
    QTest::keyClick(dialog, Qt::Key_Escape);
    fixture.settle();
    QVERIFY(!dialog);
    QCOMPARE(EditingTestHelper::userRowCount(fixture.context.get()), 1);
    QTest::keyClick(table->getView(), Qt::Key_Delete, Qt::ShiftModifier);
    fixture.settle();
    QVERIFY(!table->getRemoveDialog());

    // The combo boxes of the speech have the names of their rows, Space plays the sample
    pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(fixture.openSettings(QStringLiteral("speech")));
    QTRY_VERIFY(section->getEngine());
    QCOMPARE(section->getEngineComboBox()->accessibleName(), QStringLiteral("Voice synthesizer"));
    QCOMPARE(section->getEngineComboBox()->accessibleDescription(), QStringLiteral("Speech engine provided by the system"));
    QCOMPARE(section->getLocaleComboBox()->accessibleName(), QStringLiteral("Locale"));
    QCOMPARE(section->getVoiceComboBox()->accessibleName(), QStringLiteral("Voice"));
    QVERIFY(section->getPlayButton()->focusPolicy() & Qt::TabFocus);
    section->getPlayButton()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(section->getPlayButton(), Qt::Key_Space);
    fixture.settle();
    QVERIFY(section->isPlaying());
}

void AppSettingsEditingTest::languageChange()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(!pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).addCertificate(EditingTestHelper::certificate(0)).isError());
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("security"));
    QVERIFY(EditingTestHelper::waitForTable(EditingTestHelper::table(page)));

    // The page is built again in the new language with its custom parts
    EditingTestTranslator translator;
    QCoreApplication::installTranslator(&translator);
    fixture.settle();
    QCOMPARE(fixture.settingsPage(), page);
    pdf::PDFAppSettingGroup* group = page->getSection()->findGroup(pdf::PDFAppSecuritySettingsSection::getCertificatesGroupKey());
    QVERIFY(group);
    QCOMPARE(group->getTitle(), QString::fromUtf8("Vertrauenswürdige Zertifikate"));
    pdf::PDFAppCertificateTable* table = EditingTestHelper::table(page);
    QVERIFY(EditingTestHelper::waitForTable(table));
    QCOMPARE(table->getModel()->headerData(pdf::PDFAppCertificateTableModel::NameColumn, Qt::Horizontal).toString(), QStringLiteral("Zertifikat"));
    QCOMPARE(table->getModel()->rowCount(), 3);
    QCoreApplication::removeTranslator(&translator);
    fixture.settle();

    page = fixture.openSettings(QStringLiteral("speech"));
    QCoreApplication::installTranslator(&translator);
    fixture.settle();
    pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(page);
    QVERIFY(section);
    QCOMPARE(section->getPlayButton()->text(), QStringLiteral("Probe abspielen"));
    QTRY_VERIFY(section->getEngine());
    QCOMPARE(section->getEngineComboBox()->count(), 4);
    QCoreApplication::removeTranslator(&translator);
    fixture.settle();
}

void AppSettingsEditingTest::screenshots()
{
    if (qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS").isEmpty())
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        const QString suffix = scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark");
        pdftest::AppSettingsFixture fixture;
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::colorScheme(), suffix).isError());
        fixture.settle();
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        QVERIFY(!pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).addCertificate(EditingTestHelper::certificate(0)).isError());
        QVERIFY(!pdf::PDFTrustedCertificateRepository(fixture.context->getDatabase()).addCertificate(EditingTestHelper::certificate(1)).isError());
        QVERIFY(fixture.openSettings(QStringLiteral("editing")));
        fixture.screenshot(QStringLiteral("settings-editing-%1").arg(suffix));

        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("security"));
        pdf::PDFAppCertificateTable* table = EditingTestHelper::table(page);
        QVERIFY(EditingTestHelper::waitForTable(table));
        EditingTestHelper::clickRow(table->getView(), 1);
        QScrollArea* area = EditingTestHelper::scrollArea(page);
        area->ensureWidgetVisible(table);
        fixture.settle();
        fixture.screenshot(QStringLiteral("settings-security-%1").arg(suffix));
        QVERIFY(table->openRemoveDialog());
        fixture.settle();
        table->getRemoveDialog()->grab().save(QDir(qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS")).absoluteFilePath(QStringLiteral("settings-security-remove-%1.png").arg(suffix)));
        table->getRemoveDialog()->reject();
        fixture.settle();

        pdf::PDFAppSpeechSettingsSection* section = EditingTestHelper::speechSection(fixture.openSettings(QStringLiteral("speech")));
        QTRY_VERIFY(section->getEngine());
        fixture.screenshot(QStringLiteral("settings-speech-%1").arg(suffix));
        section->playSample();
        fixture.settle();
        section->getPreviewRow()->grab().save(QDir(qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS")).absoluteFilePath(QStringLiteral("settings-speech-playing-%1.png").arg(suffix)));
    }
}

QTEST_MAIN(AppSettingsEditingTest)
#include "tst_appsettingseditingtest.moc"
