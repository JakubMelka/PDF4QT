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

// The GUI of Soft Proofing (step 7 of phase 2, plan 2.5, 4.7, 8; package P4): the frames 479:11760 …
// 485:17529, Keep Proofing On and the item of the status bar, no proof in the copies of the settings of a tab
// (Q7.10), the proof kept over a change of the settings of the application.

#include "apptoolshellfixture.h"
#include "icctestprofile.h"
#include "outputpreviewtestpdf.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappdocumentsave.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumentsoftproof.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfapppagerendercontext.h"
#include "pdfappprint.h"
#include "pdfappprintdialog.h"
#include "pdfappcommand.h"
#include "pdfapptoolbeforeafteroverlay.h"
#include "pdfapptoolgui_extracttext.h"
#include "pdfapptoolgui_softproof.h"
#include "pdfapptoolpagestage.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttool_extracttext.h"
#include "pdfdocumenttoolshell.h"
#include "pdfsettingdefinitions.h"

#include "pdfcomponentpanels.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdffluentwidgets.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QPixmapCache>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

using pdftest::AppToolShellFixture;

/// Helpers of the test
class AppToolSoftProofTestHelper
{
public:
    AppToolSoftProofTestHelper() = delete;

    static constexpr const char* TOOL_ID = "review.soft-proof";

    static pdf::PDFAppDocumentTab* openDocument(AppToolShellFixture& fixture)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Brochure_Print.pdf"), 3));
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        return tab;
    }

    static pdf::PDFAppToolGUISoftProof* gui(pdf::PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<pdf::PDFAppToolGUISoftProof*>(shell->getGUI()) : nullptr;
    }

    /// The settings of the view, after the scheduled application
    static pdf::PDFCMSSettings viewSettings(pdf::PDFAppDocumentTab* tab)
    {
        AppToolShellFixture::waitFor([tab]() { return !tab->getView()->isApplySettingsQueued(); });
        AppToolShellFixture::settle();
        return tab->getView()->getCMSManager()->getSettings();
    }

    /// The alarm color of the proofs of the copies: no color of the test document is near it
    static QColor alarmColor() { return QColor(255, 0, 255); }

    /// Pixels of the alarm color (the colors out of the gamut of a proof with the gamut check)
    static int countAlarm(const QImage& image)
    {
        const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
        int count = 0;
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                count += (qRed(line[x]) > 200 && qGreen(line[x]) < 80 && qBlue(line[x]) > 200) ? 1 : 0;
            }
        }
        return count;
    }

    /// Pixels of the saturated green of the document as sRGB shows it (no proof)
    static int countGreen(const QImage& image)
    {
        const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
        int count = 0;
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                count += (qRed(line[x]) < 60 && qGreen(line[x]) > 200 && qBlue(line[x]) < 60) ? 1 : 0;
            }
        }
        return count;
    }

    /// A picture of the page without the proof: the green square as it is, nothing in the alarm color
    static bool isUnproofed(const QImage& image)
    {
        return !image.isNull() && countAlarm(image) == 0 && countGreen(image) > 0;
    }

    /// Opens a document of one page 300 x 300 with a square of the saturated green of sRGB, which is out of the
    /// gamut of IccTestProfile (its a* -86 against |a*| <= 40), and keeps the proof of the test profile on its
    /// tab: Soft Proof and Gamut Check with the alarm color (Keep Proofing On, 485:17529)
    static pdf::PDFAppDocumentTab* openProofed(AppToolShellFixture& fixture)
    {
        const QString profileFolder = fixture.path(QStringLiteral("profiles"));
        QFile profileFile(QDir(profileFolder).filePath(QStringLiteral("TestPrinting.icc")));
        if (!QDir().mkpath(profileFolder) || !profileFile.open(QIODevice::WriteOnly) || profileFile.write(pdftest::IccTestProfile::createCmykProfile("Test Printing")) <= 0)
        {
            return nullptr;
        }
        profileFile.close();
        if (fixture.context->getSettings().setValue(pdf::PDFSettingKeys::cmsProfileDirectory(), profileFolder).isError())
        {
            return nullptr;
        }

        OutputPreviewTestPdf pdf;
        pdf.addPage("0 1 0 rg 50 50 200 200 re f", "<< >>", QByteArray(), "[0 0 300 300]");
        const QString filePath = fixture.path(QStringLiteral("Green.pdf"));
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly) || file.write(pdf.write()) <= 0)
        {
            return nullptr;
        }
        file.close();

        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }

        QString profileId;
        for (const pdf::PDFSoftProofingProfile& profile : tab->getView()->getCMSManager()->getSoftProofingProfiles())
        {
            if (profile.isAvailable && profile.fileName.endsWith(QStringLiteral("TestPrinting.icc")))
            {
                profileId = profile.id;
            }
        }
        if (profileId.isEmpty())
        {
            return nullptr;
        }

        pdf::PDFAppDocumentSoftProof* proof = pdf::PDFAppDocumentSoftProof::getSoftProof(tab->getView());
        proof->setProfile(profileId);
        proof->setAlarmColor(alarmColor());
        proof->setProofing(true);
        proof->setGamutChecking(true);
        proof->setKept(true);
        const pdf::PDFCMSSettings settings = viewSettings(tab);
        return proof->isKept() && settings.isSoftProofing && settings.isGamutChecking ? tab : nullptr;
    }

    /// The view itself shows the proof: the compiled page of the canvas has the alarm color (the precondition of
    /// a test of a copy - without it the test would pass with the proof leaked as well)
    static bool isViewProofed(pdf::PDFAppDocumentTab* tab)
    {
        pdf::PDFDrawWidgetProxy* proxy = tab->getView()->getProxy();
        return AppToolShellFixture::waitFor([proxy]() { return countAlarm(proxy->drawThumbnailImage(0, 150)) > 0; });
    }
};

class AppToolSoftProofTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void documentStateApplied();
    void keptOnlyWithProofAndStatusItem();
    void viewKeepsProofOverSettings();
    void copiesNeverHaveProof();
    void genericEngineFrame();
    void entryAndCloseGivesStateBack();
    void keepProofingOnAndTurnOff();
    void keepProofingOnNeverRuns();
    void pagePictureWithoutProof();
    void thumbnailsWithoutProof();
    void printPreviewWithoutProof();
    void printJobWithoutProof();
    void pageStageWithoutProof();
    void beforeAfterWithoutProof();
    void screenshots();
};

void AppToolSoftProofTest::initTestCase()
{
    // The geometry of the frames is the geometry of the Fluent style (review RS7FIGMA)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
}

void AppToolSoftProofTest::documentStateApplied()
{
    // The state of the proof of a tab over the settings of the application: off = no proof at all (also a
    // proof of the settings of an old version is dropped), on = the profile, the intent and the color
    pdf::PDFAppDocumentSoftProof proof;
    QSignalSpy changed(&proof, &pdf::PDFAppDocumentSoftProof::changed);
    pdf::PDFCMSSettings settings;
    settings.isSoftProofing = true;
    settings.deviceCMYK = QStringLiteral("cmyk");

    QVERIFY(!proof.isActive());
    const pdf::PDFCMSSettings off = proof.applyTo(settings);
    QVERIFY(!off.isSoftProofing);
    QVERIFY(!off.isGamutChecking);
    QCOMPARE(off.deviceCMYK, QStringLiteral("cmyk"));

    proof.setProfile(QStringLiteral("FOGRA39"));
    proof.setIntent(pdf::RenderingIntent::AbsoluteColorimetric);
    proof.setAlarmColor(Qt::green);
    proof.setProofing(true);
    proof.setProofing(true);
    QCOMPARE(changed.count(), 4);
    QVERIFY(proof.isActive());

    const pdf::PDFCMSSettings on = proof.applyTo(settings);
    QVERIFY(on.isSoftProofing);
    QVERIFY(!on.isGamutChecking);
    QCOMPARE(on.softProofingProfile, QStringLiteral("FOGRA39"));
    QCOMPARE(on.proofingIntent, pdf::RenderingIntent::AbsoluteColorimetric);
    QCOMPARE(on.outOfGamutColor, QColor(Qt::green));
    QCOMPARE(on.deviceCMYK, QStringLiteral("cmyk"));

    // Only the gamut check: active, the proof itself off
    proof.setProofing(false);
    proof.setGamutChecking(true);
    const pdf::PDFCMSSettings gamut = proof.applyTo(settings);
    QVERIFY(!gamut.isSoftProofing);
    QVERIFY(gamut.isGamutChecking);
}

void AppToolSoftProofTest::keptOnlyWithProofAndStatusItem()
{
    // Q7.10: Keep Proofing On keeps the proof only (the gamut check alone is never kept); 485:17529: the item
    // of the status bar "Soft proof: <profile>" is shown for a kept proof only
    QWidget parent;
    pdf::PDFAppDocumentSoftProof proof;
    QToolButton* item = proof.createStatusItem(&parent);
    QVERIFY(item);
    QCOMPARE(proof.createStatusItem(&parent), item);
    proof.setProfile(QStringLiteral("profiles/CoatedFOGRA39.icc"));
    proof.setProfileTitle(QStringLiteral("Coated FOGRA39"));
    QCOMPARE(item->text(), pdf::PDFAppDocumentSoftProof::tr("Soft proof: %1", "Item of the status bar of a kept soft proof, 485:17529; %1 is the profile").arg(QStringLiteral("Coated FOGRA39")));
    QVERIFY(!item->isVisibleTo(&parent));

    proof.setGamutChecking(true);
    proof.setKept(true);
    QVERIFY(!proof.isKept());
    QVERIFY(!item->isVisibleTo(&parent));

    proof.setProofing(true);
    proof.setKept(true);
    QVERIFY(proof.isKept());
    QVERIFY(item->isVisibleTo(&parent));

    // The proof off ends the kept state
    proof.setProofing(false);
    QVERIFY(!proof.isKept());
    QVERIFY(!item->isVisibleTo(&parent));
}

void AppToolSoftProofTest::viewKeepsProofOverSettings()
{
    // Plan 4.7: the view applies the proof of its tab over readCMSSettings - a change of the settings of the
    // application keeps it (before, every change dropped it)
    using H = AppToolSoftProofTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openDocument(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QVERIFY(!pdf::PDFAppDocumentSoftProof::findSoftProof(view));

    pdf::PDFAppDocumentSoftProof* proof = pdf::PDFAppDocumentSoftProof::getSoftProof(view);
    QVERIFY(proof);
    QCOMPARE(pdf::PDFAppDocumentSoftProof::findSoftProof(view), proof);
    QCOMPARE(pdf::PDFAppDocumentSoftProof::getSoftProof(view), proof);
    proof->setProfile(QStringLiteral("profile-id"));
    proof->setIntent(pdf::RenderingIntent::Saturation);
    proof->setProofing(true);
    pdf::PDFCMSSettings settings = H::viewSettings(tab);
    QVERIFY(settings.isSoftProofing);
    QCOMPARE(settings.softProofingProfile, QStringLiteral("profile-id"));
    QCOMPARE(settings.proofingIntent, pdf::RenderingIntent::Saturation);

    // Another setting of the color management: the proof stays
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::cmsAccuracy(), QStringLiteral("high")).isError());
    settings = H::viewSettings(tab);
    QCOMPARE(settings.accuracy, pdf::PDFCMSSettings::Accuracy::High);
    QVERIFY(settings.isSoftProofing);
    QCOMPARE(settings.softProofingProfile, QStringLiteral("profile-id"));

    // The proof off: no proof at all
    proof->setProofing(false);
    settings = H::viewSettings(tab);
    QVERIFY(!settings.isSoftProofing);
    QVERIFY(!settings.isGamutChecking);
}

void AppToolSoftProofTest::copiesNeverHaveProof()
{
    // Q7.10: a run of a tool takes the color management of the tab without the proof - a CMS of its own,
    // when the view proofs, the shared one of the view otherwise
    using H = AppToolSoftProofTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openDocument(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    const pdf::PDFCMSManager* manager = view->getCMSManager();
    const pdf::PDFDocument* document = tab->getDocument()->getDocument().data();

    QCOMPARE(pdf::PDFAppDocumentSoftProof::createCMSWithoutProofing(manager, document), manager->getCurrentCMS());

    pdf::PDFAppDocumentSoftProof* proof = pdf::PDFAppDocumentSoftProof::getSoftProof(view);
    proof->setGamutChecking(true);
    QVERIFY(H::viewSettings(tab).isGamutChecking);
    const pdf::PDFCMSPointer copy = pdf::PDFAppDocumentSoftProof::createCMSWithoutProofing(manager, document);
    QVERIFY(copy);
    QVERIFY(copy != manager->getCurrentCMS());
    QVERIFY(!pdf::PDFAppDocumentSoftProof::createCMSWithoutProofing(nullptr, document));
}

void AppToolSoftProofTest::genericEngineFrame()
{
    // 479:15335: the Generic engine cannot proof - the switches and the menus Disabled, the callout, Keep
    // Proofing On Disabled, the status message
    using H = AppToolSoftProofTestHelper;
    AppToolShellFixture fixture;
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::cmsSystem(), QStringLiteral("generic")).isError());
    pdf::PDFAppDocumentTab* tab = H::openDocument(fixture);
    QVERIFY(tab);
    QCOMPARE(H::viewSettings(tab).system, pdf::PDFCMSSettings::System::Generic);

    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(H::TOOL_ID));
    pdf::PDFAppToolGUISoftProof* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(gui->getAvailability(), pdf::PDFAppToolGUISoftProof::Availability::GenericEngine);
    QVERIFY(!gui->getProofButton()->isEnabled());
    QVERIFY(!gui->getProofButton()->isChecked());
    QVERIFY(!gui->getGamutButton()->isEnabled());
    QVERIFY(!gui->getProfilePanel()->getButton()->isEnabled());
    QVERIFY(!gui->getIntentPanel()->getButton()->isEnabled());
    QVERIFY(gui->getCallout()->isVisibleTo(gui->getCallout()->parentWidget()));
    QCOMPARE(gui->getCallout()->getTitleLabel()->text(), pdf::PDFAppToolGUISoftProof::tr("Soft proofing needs the Little CMS 2 color engine"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(shell->getToolbar()->getConfirmButton()->text(), pdf::PDFAppToolGUISoftProof::tr("Keep Proofing On"));
    // 479:15335: Close, never Cancel (RS7FIGMA)
    QCOMPARE(shell->getToolbar()->getCancelButton()->text(), pdf::PDFAppToolGUISoftProof::tr("Close"));
    QCOMPARE(gui->getStatusText(), pdf::PDFAppToolGUISoftProof::tr("Soft proofing is not available with the Generic color engine."));
    QCOMPARE(gui->getProofingRows()->getValue(0), pdf::PDFAppToolGUISoftProof::tr("Off"));
    QCOMPARE(gui->getProofingRows()->getValue(1), pdf::PDFAppToolGUISoftProof::tr("Off"));
    QVERIFY(!gui->getSoftProof()->isActive());
}

void AppToolSoftProofTest::entryAndCloseGivesStateBack()
{
    // 479:11760: the tool shows the proof while it is open; Close without Keep Proofing On gives the state of
    // the tab back. Without a CMYK profile (479:14751) nothing is proofed and the callout says why.
    using H = AppToolSoftProofTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openDocument(fixture);
    QVERIFY(tab);

    // The state of before: the gamut check alone
    QPointer<pdf::PDFAppDocumentSoftProof> proof = pdf::PDFAppDocumentSoftProof::getSoftProof(tab->getView());
    proof->setGamutChecking(true);

    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(H::TOOL_ID));
    pdf::PDFAppToolGUISoftProof* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(gui->getSoftProof(), proof.data());

    // The thumbnails on the left, the canvas of the page with the bar
    QVERIFY(gui->getThumbnailsPanel());
    QVERIFY(gui->getCanvasPage());
    QVERIFY(gui->getCanvasPage()->getPicture());
    QCOMPARE(gui->getCanvasPage()->getPicture()->getPageIndex(), pdf::PDFInteger(0));

    if (gui->getAvailability() == pdf::PDFAppToolGUISoftProof::Availability::NoProfile)
    {
        QVERIFY(!proof->isActive());
        QCOMPARE(gui->getCallout()->getTitleLabel()->text(), pdf::PDFAppToolGUISoftProof::tr("No CMYK profile found"));
        QCOMPARE(gui->getProfilePanel()->getButton()->text(), pdf::PDFAppToolGUISoftProof::tr("No profile"));
        QVERIFY(gui->getProfilePanel()->getButton()->isEnabled());
        QVERIFY(!gui->getProofButton()->isEnabled());
        QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
        QCOMPARE(gui->getCanvasPage()->getTitleLabel()->text(), pdf::PDFAppToolGUISoftProof::tr("Page %1 · screen colors", "479:15922").arg(QStringLiteral("1")));
    }
    else
    {
        // The gamut check of before stays; Keep Proofing On is enabled only with the proof (K7.9)
        QVERIFY(proof->isGamutChecking());
        QVERIFY(gui->getGamutButton()->isChecked());
        QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
        gui->getProofButton()->click();
        QVERIFY(proof->isProofing());
        QVERIFY(H::viewSettings(tab).isSoftProofing);
        QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
        gui->getGamutButton()->click();
        QVERIFY(!proof->isGamutChecking());
    }

    // Close (Cancel): the state of before the entry, nothing kept
    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(proof);
    QVERIFY(!proof->isProofing());
    QVERIFY(proof->isGamutChecking());
    QVERIFY(!proof->isKept());
    const pdf::PDFCMSSettings settings = H::viewSettings(tab);
    QVERIFY(!settings.isSoftProofing);
    QVERIFY(settings.isGamutChecking);
}

void AppToolSoftProofTest::keepProofingOnAndTurnOff()
{
    // 485:17529: Keep Proofing On (the Primary button, which never runs the tool) keeps the proof after the tool
    // closes, the item of the status bar shows it; Close of a later entry without a change gives the kept proof
    // back; 497:19702 "Keep Proofing On leaves it active after the tool is closed, until you turn it off here" -
    // the proof turned off in the tool and Close end it (RS7APP: Close gave the kept proof back, so it could not
    // be turned off at all)
    using H = AppToolSoftProofTestHelper;
    AppToolShellFixture fixture;

    // A CMYK profile in the profile folder of Settings (no profile of the system is needed)
    const QString profileFolder = fixture.path(QStringLiteral("profiles"));
    QVERIFY(QDir().mkpath(profileFolder));
    QFile profileFile(QDir(profileFolder).filePath(QStringLiteral("TestPrinting.icc")));
    QVERIFY(profileFile.open(QIODevice::WriteOnly));
    QVERIFY(profileFile.write(pdftest::IccTestProfile::createCmykProfile("Test Printing")) > 0);
    profileFile.close();
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::cmsProfileDirectory(), profileFolder).isError());

    pdf::PDFAppDocumentTab* tab = H::openDocument(fixture);
    QVERIFY(tab);
    const QString toolId = QString::fromLatin1(H::TOOL_ID);
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, toolId);
    pdf::PDFAppToolGUISoftProof* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(gui->getAvailability(), pdf::PDFAppToolGUISoftProof::Availability::Available);
    QPointer<pdf::PDFAppDocumentSoftProof> proof = gui->getSoftProof();
    QVERIFY(proof && proof->isProofing());
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // 479:11760 (RS7FIGMA): the document has no output intent - the proof shown as on is the proof of the first
    // profile of the list, named in the menu, the canvas and the status (never "proof of " with no profile)
    QString firstProfile;
    for (const pdf::PDFSoftProofingProfile& profile : tab->getView()->getCMSManager()->getSoftProofingProfiles())
    {
        if (profile.isAvailable)
        {
            firstProfile = !profile.name.isEmpty() ? profile.name : profile.fileName;
            break;
        }
    }
    QVERIFY(!firstProfile.isEmpty());
    QCOMPARE(gui->getProfilePanel()->getButton()->text(), firstProfile);
    QCOMPARE(gui->getCanvasPage()->getTitleLabel()->text(), pdf::PDFAppToolGUISoftProof::tr("Page %1 · proof of %2", "479:11760").arg(QStringLiteral("1"), firstProfile));
    QCOMPARE(gui->getStatusText(), pdf::PDFAppToolGUISoftProof::tr("The page is shown as %1 would print it.").arg(firstProfile));
    QVERIFY(!H::viewSettings(tab).softProofingProfile.isEmpty());

    // Keep Proofing On: the tool closes, the proof stays kept with its item
    shell->getToolbar()->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([&shell]() { return !shell || shell->getState() == pdf::PDFDocumentToolShell::State::Closed; }));
    AppToolShellFixture::settle();
    QVERIFY(proof);
    QVERIFY(proof->isProofing());
    QVERIFY(proof->isKept());
    QVERIFY(proof->getStatusItem() && proof->getStatusItem()->isVisible());
    QVERIFY(H::viewSettings(tab).isSoftProofing);

    // A later entry closed without a change: the kept proof comes back
    shell = fixture.openTool(tab, toolId);
    QVERIFY(H::gui(shell));
    QVERIFY(!proof->isKept());
    shell->cancel();
    QVERIFY(AppToolShellFixture::waitFor([&shell]() { return !shell || shell->getState() == pdf::PDFDocumentToolShell::State::Closed; }));
    AppToolShellFixture::settle();
    QVERIFY(proof->isProofing());
    QVERIFY(proof->isKept());

    // The proof turned off in the tool (Keep Proofing On is Disabled then, K7.9) and Close: no proof any more
    shell = fixture.openTool(tab, toolId);
    gui = H::gui(shell);
    QVERIFY(gui);
    gui->getProofButton()->click();
    QVERIFY(!proof->isProofing());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    shell->cancel();
    QVERIFY(AppToolShellFixture::waitFor([&shell]() { return !shell || shell->getState() == pdf::PDFDocumentToolShell::State::Closed; }));
    AppToolShellFixture::settle();
    QVERIFY(proof);
    QVERIFY(!proof->isProofing());
    QVERIFY(!proof->isGamutChecking());
    QVERIFY(!proof->isKept());
    QVERIFY(!proof->getStatusItem()->isVisible());
    const pdf::PDFCMSSettings settings = H::viewSettings(tab);
    QVERIFY(!settings.isSoftProofing);
    QVERIFY(!settings.isGamutChecking);
}

void AppToolSoftProofTest::keepProofingOnNeverRuns()
{
    // 485:17529: Keep Proofing On is the Primary button of a tool, which shows the proof itself - it never runs the
    // tool (the run of the core tool is the measurement of MCP and PdfTool). RS7APP L4: the shell gives the press to
    // PDFDocumentToolGUI::confirmWithoutRun, no environment, no run; the tool closes with the proof kept.
    using H = AppToolSoftProofTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openProofed(fixture);
    QVERIFY(tab);
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QString::fromLatin1(H::TOOL_ID));
    pdf::PDFAppToolGUISoftProof* gui = H::gui(shell);
    QVERIFY(gui);
    QPointer<pdf::PDFDocumentToolRunner> runner = shell->getRunner();
    QVERIFY(runner);
    QSignalSpy finished(runner.data(), &pdf::PDFDocumentToolRunner::runFinished);
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // Ctrl+Enter is the same confirmation as the button
    shell->confirm();
    QCOMPARE(runner->getState(), pdf::PDFDocumentToolRunner::State::Idle);
    QVERIFY(runner->getRunParameters().isEmpty());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());

    QVERIFY(AppToolShellFixture::waitFor([&shell]() { return !shell || shell->getState() == pdf::PDFDocumentToolShell::State::Closed; }));
    AppToolShellFixture::settle();
    QCOMPARE(finished.count(), 0);
    pdf::PDFAppDocumentSoftProof* proof = pdf::PDFAppDocumentSoftProof::findSoftProof(tab->getView());
    QVERIFY(proof && proof->isProofing() && proof->isKept());
}

void AppToolSoftProofTest::pagePictureWithoutProof()
{
    // Q7.10 "previews never": a proof kept on the tab is only the view of the tab - the picture of the page of
    // another tool (Extract Text, the shared PDFAppToolPagePicture of Audiobook too) is the page without the proof,
    // exactly as renderPage draws it with the settings of the view without the proof (the coordinator, fix wave)
    using H = AppToolSoftProofTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openProofed(fixture);
    QVERIFY(tab);
    QVERIFY(H::isViewProofed(tab));

    pdf::PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(pdf::PDFDocumentToolExtractText::TOOL_ID));
    pdf::PDFAppToolGUIExtractText* gui = shell ? qobject_cast<pdf::PDFAppToolGUIExtractText*>(shell->getGUI()) : nullptr;
    QVERIFY(gui && gui->getPreviewPage());
    pdf::PDFAppToolPagePicture* picture = gui->getPreviewPage()->getPicture();
    QVERIFY(picture);
    QVERIFY(!picture->isProofShown());
    QVERIFY(AppToolShellFixture::waitFor([picture]()
    {
        picture->update();
        return picture->getPageIndex() == 0 && !picture->getImage().isNull() && !picture->isRendering();
    }));
    const QImage image = picture->getImage().convertToFormat(QImage::Format_ARGB32);

    // The settings of the view proof; the same page with them has the alarm color, without them it has none
    const pdf::PDFCMSSettings settings = H::viewSettings(tab);
    QVERIFY(settings.isSoftProofing && settings.isGamutChecking);
    pdf::PDFDrawWidgetProxy* proxy = tab->getView()->getProxy();
    const pdf::PDFDocumentPointer document = tab->getDocument()->getDocument();
    const QImage proofed = pdf::PDFAppToolPagePicture::renderPage(document, 0, image.size(), settings, proxy->getFeatures(), proxy->getPaperColor(), nullptr);
    const QImage expected = pdf::PDFAppToolPagePicture::renderPage(document, 0, image.size(), settings.withoutProofing(), proxy->getFeatures(), proxy->getPaperColor(), nullptr);
    QVERIFY(H::countAlarm(proofed) > 0);
    QVERIFY(H::isUnproofed(expected));
    QVERIFY(H::isUnproofed(image));
    QCOMPARE(image, expected.convertToFormat(QImage::Format_ARGB32));
}

void AppToolSoftProofTest::thumbnailsWithoutProof()
{
    // Q7.10, plan 4.7 (copy site 1 of 6): the thumbnails never show the proof kept on the tab
    using H = AppToolSoftProofTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openProofed(fixture);
    QVERIFY(tab);
    QVERIFY(H::isViewProofed(tab));

    // The panel Thumbnails of the tab, its pictures drawn again with the proof on
    pdf::PDFAppDocumentSidebar* sidebar = tab->getView()->getSidebar();
    QVERIFY(sidebar && sidebar->showPanel(pdf::PDFAppDocumentPanelIds::thumbnails()));
    pdf::PDFAppThumbnailsPanel* panel = qobject_cast<pdf::PDFAppThumbnailsPanel*>(sidebar->findPanel(pdf::PDFAppDocumentPanelIds::thumbnails()));
    QVERIFY(panel);
    pdf::PDFAppThumbnailsModel* model = panel->getModel();
    model->invalidateAll();
    QPixmap pixmap;
    QVERIFY(AppToolShellFixture::waitFor([panel, model, &pixmap]()
    {
        panel->getListView()->viewport()->update();
        return model->hasPicture(0) && QPixmapCache::find(model->getKey(0), &pixmap);
    }));
    QVERIFY(H::isUnproofed(pixmap.toImage()));
}

void AppToolSoftProofTest::printPreviewWithoutProof()
{
    // Q7.10, plan 4.7 (copy site 2 of 6, PDFAppPrintController::print): the preview of the print dialog - the
    // printing - never has the proof kept on the tab
    using H = AppToolSoftProofTestHelper;
    AppToolShellFixture fixture;
    const QString folder = fixture.folder();
    pdf::PDFAppPrinters::setProvider([folder]()
    {
        pdf::PDFAppPrinterInfo info;
        info.name = QStringLiteral("Test Printer");
        info.isDefault = true;
        info.stateText = QStringLiteral("Ready");
        info.outputFile = QDir(folder).absoluteFilePath(QStringLiteral("printed.pdf"));
        return std::vector<pdf::PDFAppPrinterInfo>({ info });
    });
    const QScopeGuard restorePrinters([]() { pdf::PDFAppPrinters::setProvider(pdf::PDFAppPrinters::Provider()); });

    pdf::PDFAppDocumentTab* tab = H::openProofed(fixture);
    QVERIFY(tab);
    QVERIFY(H::isViewProofed(tab));
    pdf::PDFAppDocumentFileCommandHandler* handler = pdf::PDFAppDocumentFileCommandHandler::find(tab);
    QVERIFY(handler && handler->isEnabled(pdf::PDFAppCommands::print()));
    handler->execute(pdf::PDFAppCommands::print());
    QPointer<pdf::PDFAppPrintDialog> dialog = handler->getPrintController()->getDialog();
    QVERIFY(dialog);
    QVERIFY(AppToolShellFixture::waitFor([dialog]() { return !dialog || (!dialog->getPreview()->getImage().isNull() && !dialog->isPreviewRendering()); }));
    QVERIFY(dialog);
    QVERIFY(H::isUnproofed(dialog->getPreview()->getImage()));
    dialog->reject();
    AppToolShellFixture::settle();
}

void AppToolSoftProofTest::printJobWithoutProof()
{
    // Q7.10, plan 4.7 (copy site 3 of 6, PDFAppPrintController::startJob): the printed pages never have the proof
    // kept on the tab - the file of a test printer, drawn without any proof, shows the green as it is
    using H = AppToolSoftProofTestHelper;
    AppToolShellFixture fixture;
    const QString outputFile = fixture.path(QStringLiteral("printed.pdf"));
    pdf::PDFAppPrinters::setProvider([outputFile]()
    {
        pdf::PDFAppPrinterInfo info;
        info.name = QStringLiteral("Test Printer");
        info.isDefault = true;
        info.stateText = QStringLiteral("Ready");
        info.outputFile = outputFile;
        return std::vector<pdf::PDFAppPrinterInfo>({ info });
    });
    const QScopeGuard restorePrinters([]() { pdf::PDFAppPrinters::setProvider(pdf::PDFAppPrinters::Provider()); });

    pdf::PDFAppDocumentTab* tab = H::openProofed(fixture);
    QVERIFY(tab);
    QVERIFY(H::isViewProofed(tab));
    pdf::PDFAppDocumentFileCommandHandler* handler = pdf::PDFAppDocumentFileCommandHandler::find(tab);
    QVERIFY(handler && handler->isEnabled(pdf::PDFAppCommands::print()));
    handler->execute(pdf::PDFAppCommands::print());
    pdf::PDFAppPrintController* controller = handler->getPrintController();
    QPointer<pdf::PDFAppPrintDialog> dialog = controller->getDialog();
    QVERIFY(dialog);
    QSignalSpy finished(controller, &pdf::PDFAppPrintController::printFinished);
    dialog->getPrintButton()->click();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 60000);
    QVERIFY(controller->getLastResult().isCompleted);

    QFile file(outputFile);
    QVERIFY(file.open(QIODevice::ReadOnly));
    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    const pdf::PDFDocumentPointer printed(new pdf::PDFDocument(reader.readFromBuffer(file.readAll())));
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
    QVERIFY(printed->getCatalog()->getPageCount() >= 1);
    const QImage page = pdf::PDFAppToolPagePicture::renderPage(printed, 0, QSize(300, 420), pdf::PDFCMSSettings(), pdf::PDFAppPageRenderContext::getDefaultFeatures(), Qt::white, nullptr);
    QVERIFY(H::isUnproofed(page));
}

void AppToolSoftProofTest::pageStageWithoutProof()
{
    // Q7.10, plan 4.7 (copy site 4 of 6, PDFAppToolPageStage::startRender): the page of the stage of a tool (Now /
    // After of the page tools) never has the proof kept on the tab
    using H = AppToolSoftProofTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openProofed(fixture);
    QVERIFY(tab);
    QVERIFY(H::isViewProofed(tab));

    pdf::PDFAppToolPageStage stage(nullptr);
    stage.setProxy(tab->getView()->getProxy());
    stage.setPage(tab->getDocument()->getDocument(), tab->getDocument()->getId(), 0);
    stage.resize(400, 400);
    stage.show();
    QVERIFY(AppToolShellFixture::waitFor([&stage]() { return stage.getFinishedRenderCount() > 0 && !stage.isRenderPending(); }));
    AppToolShellFixture::settle();
    QVERIFY(H::isUnproofed(stage.grab().toImage()));
}

void AppToolSoftProofTest::beforeAfterWithoutProof()
{
    // Q7.10, plan 4.7 (copy site 5 of 6, PDFAppToolBeforeAfterOverlay::startRender): the pages of the result right
    // of the line of Before / After never have the proof kept on the tab (left of it the live canvas has it)
    using H = AppToolSoftProofTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openProofed(fixture);
    QVERIFY(tab);
    QVERIFY(H::isViewProofed(tab));

    pdf::PDFAppDocumentView* view = tab->getView();
    QPointer<pdf::PDFAppToolBeforeAfterOverlay> overlay = new pdf::PDFAppToolBeforeAfterOverlay(view->getPdfWidget(), view->getCanvasArea());
    const QScopeGuard deleteOverlay([&overlay]() { pdftest::AppDocumentFixture::deleteNow(overlay.data()); });
    overlay->setResult(tab->getDocument()->getDocument(), QStringLiteral("After"));
    overlay->setOverlayShown(true);
    overlay->setLinePosition(0);
    QWidget* canvas = AppToolShellFixture::canvasWidget(view);
    QVERIFY(AppToolShellFixture::waitFor([overlay, canvas]()
    {
        canvas->update();
        return overlay->getFinishedRenderCount() > 0 && !overlay->isRenderPending();
    }));
    AppToolShellFixture::settle();

    // Right of the first quarter: only the pages of the result (and the chip AFTER)
    const QImage image = canvas->grab().toImage();
    const QImage after = image.copy(image.width() / 4, 0, image.width() - image.width() / 4, image.height());
    QVERIFY(H::isUnproofed(after));
}

void AppToolSoftProofTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder> (review RS7FIGMA): the states of Soft Proofing under the ids of their frames
    // (479:11760 … 485:17529) in both themes, at 1x / 2x and right-to-left, for the comparison with Figma; no pixel
    // of the leak color in any of them (Fluent)
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    using H = AppToolSoftProofTestHelper;
    for (const char* variant : { "light", "dark", "rtl" })
    {
        const QString variantName = QString::fromLatin1(variant);
        const QString suffix = variantName == QStringLiteral("light") ? QString() : QStringLiteral("-") + variantName;
        QApplication::setLayoutDirection(variantName == QStringLiteral("rtl") ? Qt::RightToLeft : Qt::LeftToRight);
        const QScopeGuard guard([]() { QApplication::setLayoutDirection(Qt::LeftToRight); pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light); });

        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFThemeManager::getInstance()->setScheme(variantName == QStringLiteral("dark") ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
        AppToolShellFixture::settle();
        QStringList leaks;
        const auto shot = [&fixture, &suffix, &leaks](const QString& frame)
        {
            AppToolShellFixture::settle();
            if (AppToolShellFixture::countLeakPixels(fixture.screenshot(frame + suffix)) > 0)
            {
                leaks << frame + suffix;
            }
        };
        const auto openMenu = [&shot](QToolButton* button, const QString& frame)
        {
            QVERIFY(button);
            QTest::mouseClick(button, Qt::LeftButton);
            AppToolShellFixture::settle();
            shot(frame);
            if (QWidget* popup = QApplication::activePopupWidget())
            {
                popup->close();
            }
            AppToolShellFixture::settle();
        };

        const QString profileFolder = fixture.path(QStringLiteral("profiles"));
        QVERIFY(QDir().mkpath(profileFolder));
        QFile profileFile(QDir(profileFolder).filePath(QStringLiteral("CoatedFOGRA39.icc")));
        QVERIFY(profileFile.open(QIODevice::WriteOnly));
        QVERIFY(profileFile.write(pdftest::IccTestProfile::createCmykProfile("Coated FOGRA39")) > 0);
        profileFile.close();
        QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::cmsProfileDirectory(), profileFolder).isError());

        pdf::PDFAppDocumentTab* tab = H::openDocument(fixture);
        QVERIFY(tab);
        const QString toolId = QString::fromLatin1(H::TOOL_ID);
        QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, toolId);
        pdf::PDFAppToolGUISoftProof* gui = H::gui(shell);
        QVERIFY(gui);
        shot(QStringLiteral("479-11760"));
        openMenu(gui->getProfilePanel()->getButton(), QStringLiteral("479-12333"));
        openMenu(gui->getIntentPanel()->getButton(), QStringLiteral("497-19614"));
        gui->getGamutButton()->click();
        shot(QStringLiteral("479-12995"));
        gui->getProofButton()->click();
        shot(QStringLiteral("479-13573"));
        gui->getGamutButton()->click();
        shot(QStringLiteral("479-15922"));
        gui->getProofButton()->click();

        shell->getToolbar()->getConfirmButton()->click();
        QVERIFY(AppToolShellFixture::waitFor([&shell]() { return !shell || shell->getState() == pdf::PDFDocumentToolShell::State::Closed; }));
        shot(QStringLiteral("485-17529"));

        fixture.closeMessageBoxes();
        QVERIFY2(leaks.isEmpty(), qPrintable(leaks.join(QStringLiteral(", "))));
    }
}

QTEST_MAIN(AppToolSoftProofTest)

#include "tst_apptoolsoftprooftest.moc"
