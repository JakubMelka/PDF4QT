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

#include "pdfapptoolgui_signature.h"
#include "pdfapptoolgui_signbyhand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentpermissions.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsignatures.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolgui_certificates.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolpasswordcard.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcertificatemanager.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocmdppermissions.h"
#include "pdfdocument.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdrawwidget.h"
#include "pdfform.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfsignaturehandler.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdftimestampclient.h"
#include "pdfwidgetstyleproperties.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace pdf
{

/// Sizes and texts of Sign with Certificate and Add Timestamp (434:4338, 435:7037)
class PDFAppToolGUISignatureHelper
{
public:
    PDFAppToolGUISignatureHelper() = delete;

    static constexpr const char* FIELD_PROPERTY = "pdf4qtSignatureField";
    static constexpr const char* APPEARANCE_PROPERTY = "pdf4qtSignatureAppearance";

    /// field-certificate-password 434:5116: the label and the field 4 apart
    static constexpr int FIELD_GAP = 4;

    /// card 434:5123: padding 12 + the border, the fields 8 apart, the label and its control 4 apart
    static constexpr int DETAILS_PADDING = 13;
    static constexpr int DETAILS_GAP = 8;

    /// card of section-Signature 434:4938 / section-Result 435:7185: the Key value rows of 28 are 8 apart
    static constexpr int SUMMARY_ROW_GAP = 8;

    /// facts 434:8518: the rows on the filled note, padding 4 / 12
    static constexpr int FACTS_PADDING_X = 12;
    static constexpr int FACTS_PADDING_Y = 4;

    /// empty-state 434:6889: padding 16 + the border, the parts 8 apart, the tile 40 with the icon 18
    static constexpr int EMPTY_PADDING = 17;
    static constexpr int EMPTY_GAP = 8;
    static constexpr int EMPTY_TILE = 40;
    static constexpr int EMPTY_ICON = 18;
    static constexpr int EMPTY_TILE_RADIUS = 8;

    /// The walk over the fields of the parsed form (finite: PDFForm::parse keeps one visited set and
    /// a depth limit); \p depth guards this recursion as well
    static void collectEmptyFields(const PDFDocument* document, const PDFFormField* field, int depth, std::vector<PDFAppToolSignatureField>* fields)
    {
        constexpr int MAXIMUM_DEPTH = 64;
        if (!field || depth > MAXIMUM_DEPTH)
        {
            return;
        }

        // Empty: /V is no signature dictionary (missing, null, a reference to nothing)
        if (field->getFieldType() == PDFFormField::FieldType::Signature && !document->getObject(field->getValue()).isDictionary())
        {
            PDFAppToolSignatureField signatureField;
            signatureField.name = field->getName(PDFFormField::FullyQualified);
            const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(field->getSelfReference()));
            if (dictionary && dictionary->hasKey("Lock"))
            {
                // 12.8.2.4: a field with /Lock needs a FieldMDP reference, which is not written (Q6.3)
                signatureField.disabledReason = PDFAppToolGUISignature::tr("The form locks other fields with this signature – it cannot be signed here");
            }
            PDFDocumentDataLoaderDecorator loader(document);
            for (const PDFFormWidget& widget : field->getWidgets())
            {
                const PDFDictionary* widgetDictionary = document->getDictionaryFromObject(document->getObjectByReference(widget.getWidget()));
                const QRectF rect = widgetDictionary ? loader.readRectangle(widgetDictionary->get("Rect"), QRectF()) : QRectF();
                signatureField.isVisible = signatureField.isVisible || (rect.width() > 0.0 && rect.height() > 0.0);
            }
            if (!signatureField.name.isEmpty())
            {
                fields->push_back(std::move(signatureField));
            }
        }

        for (const PDFFormFieldPointer& child : field->getChildFields())
        {
            collectEmptyFields(document, child.get(), depth + 1, fields);
        }
    }

    /// The host of an address of an authority without its user info (R6-28), the whole address of a
    /// text, which is no URL
    static QString getAuthorityHost(const QString& url)
    {
        const QUrl parsed(url);
        return parsed.host().isEmpty() ? PDFTimestampClient::getDisplayUrl(parsed) : parsed.host();
    }

    /// The fact Time of the result dialog (434:9276, 435:8356: "Sep 30, 2026 · 14:32:08 UTC") in the
    /// language of the user interface (O17): the core gives the time as data (ISO 8601), its own text of
    /// the fact is independent of any locale (MCP, PdfTool)
    static void formatTimeFact(PDFDocumentToolResult* result, PDFAppToolGUISignature::Mode mode)
    {
        using Tool = PDFDocumentToolSignature;
        const char* name = mode == PDFAppToolGUISignature::Mode::Sign ? Tool::DATA_SIGNING_TIME : Tool::DATA_TIMESTAMP_TIME;
        const QDateTime time = QDateTime::fromString(result->data.value(QLatin1String(name)).toString(), Qt::ISODate);
        if (!time.isValid())
        {
            return;
        }

        const QDateTime utc = time.toUTC();
        const QString clock = PDFAppLocale::getUiLocale().toString(utc.time(), PDFAppToolGUISignature::tr("HH:mm:ss", "Format of the time of a signature (QLocale::toString): 14:32:08"));
        for (PDFDocumentToolFact& fact : result->facts)
        {
            if (fact.id == QLatin1String(Tool::FACT_TIME))
            {
                fact.value = PDFAppToolGUISignature::tr("%1 · %2 UTC").arg(PDFAppLocale::formatDate(utc.date()), clock);
            }
        }
    }
};

// ------------------------------------------------------------ PDFAppToolSignatureField

std::vector<PDFAppToolSignatureField> PDFAppToolSignatureField::getEmptyFields(const PDFDocument* document)
{
    std::vector<PDFAppToolSignatureField> fields;
    if (!document)
    {
        return fields;
    }
    const PDFForm form = PDFForm::parse(document, document->getCatalog()->getFormObject());
    if (!form.isAcroForm())
    {
        return fields;
    }
    for (const PDFFormFieldPointer& field : form.getFormFields())
    {
        PDFAppToolGUISignatureHelper::collectEmptyFields(document, field.get(), 0, &fields);
    }
    return fields;
}

// ------------------------------------------------------------ PDFAppToolGUISignature

PDFAppToolGUISignature::PDFAppToolGUISignature(QObject* parent) :
    BaseClass(parent),
    m_provider(nullptr)
{
    m_passwordTimer = new QTimer(this);
    m_passwordTimer->setSingleShot(true);
    m_passwordTimer->setInterval(PASSWORD_CHECK_DELAY);
    connect(m_passwordTimer.data(), &QTimer::timeout, this, &PDFAppToolGUISignature::onPasswordTimer);
}

PDFAppToolGUISignature::~PDFAppToolGUISignature()
{
    // 6.1: the password goes with the tool (also the one kept for a retry)
    if (m_passwordCard)
    {
        disconnect(m_passwordCard.data(), nullptr, this, nullptr);
    }
    clearSecrets();
    forgetRetryPassword();
    dropCertificateRunValues();

    // A running check reports nothing more (its job holds only copies of values, it ends by itself)
    if (m_passwordWatcher)
    {
        disconnect(m_passwordWatcher.data(), nullptr, this, nullptr);
    }

    // The dialogs belong to the tool: they close with it and report nothing more
    m_isRetrying = false;
    for (QWidget* dialog : std::initializer_list<QWidget*>{ m_createDialog.data(), m_importDialog.data(), m_questionDialog.data(), m_timestampErrorDialog.data() })
    {
        if (dialog)
        {
            disconnect(dialog, nullptr, this, nullptr);
            dialog->close();
        }
    }
}

void PDFAppToolGUISignature::registerSignatureTools()
{
    for (const Mode mode : { Mode::Sign, Mode::Timestamp })
    {
        const QString toolId = PDFDocumentToolSignature::getToolId(mode);
        if (!PDFDocumentToolRegistry::isImplemented(toolId))
        {
            PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolSignature>(mode));
        }
        if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
        {
            PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUISignature>>());
        }
    }
}

PDFAppToolGUISignature::Mode PDFAppToolGUISignature::getMode() const
{
    const PDFDocumentToolCatalogEntry* entry = getTool();
    const std::optional<Mode> mode = entry ? PDFDocumentToolSignature::findMode(entry->getId()) : std::nullopt;
    return mode.value_or(Mode::Sign);
}

void PDFAppToolGUISignature::ensureParts()
{
    if (!m_authorityPart)
    {
        // Ownership: child objects of this GUI
        m_provider = PDFAppToolCertificateProvider(getContext());
        m_authorityPart = new PDFAppToolTimestampAuthorityPart(getContext(), this);
        const QString url = getModel() ? getModel()->getValue(QString::fromLatin1(PDFDocumentToolSignature::PARAMETER_TSA_URL)).toString() : QString();
        m_authorityPart->setUrl(url);
        connect(m_authorityPart.data(), &PDFAppToolTimestampAuthorityPart::urlChanged, this, &PDFAppToolGUISignature::onUrlChanged);
    }

    if (getMode() == Mode::Sign && !m_drawingPart)
    {
        m_drawingPart = new PDFAppToolDrawingPart(getContext(), PDFAppToolDrawingPart::Layout::SignatureAppearance, this);
        connect(m_drawingPart.data(), &PDFAppToolDrawingPart::drawingChanged, this, &PDFAppToolGUISignature::onDrawingChanged);
        connect(m_drawingPart.data(), &PDFAppToolDrawingPart::selectionChanged, this, &PDFAppToolGUISignature::onDrawingSelectionChanged);
        connect(m_drawingPart.data(), &PDFAppToolDrawingPart::clearRequested, this, &PDFAppToolGUISignature::onClearRequested);
        m_drawingPart->bindParameter(getModel(), QString::fromLatin1(PDFDocumentToolSignature::PARAMETER_DRAWING));
        if (PDFWidget* widget = getHost() ? getHost()->getPdfWidget() : nullptr)
        {
            m_drawingPart->attachToView(widget->getDrawWidgetProxy());
        }
    }

    if (!m_verifier)
    {
        m_verifier = getVerifier();
        if (m_verifier)
        {
            connect(m_verifier.data(), &PDFAppDocumentSignatureVerifier::verificationChanged, this, &PDFAppToolGUISignature::onVerificationChanged);
        }
    }
}

// ------------------------------------------------------------------- toolbar

void PDFAppToolGUISignature::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Tool = PDFDocumentToolSignature;

    ensureParts();
    m_appearanceRows.clear();
    if (getMode() != Mode::Sign)
    {
        // 435:7037: no menu
        return;
    }

    // 434:5308: Signature type and Format in one panel (the owner answer Q6.1)
    PDFAppToolEnumMenu* typeMenu = builder.addEnumMenu(QString::fromLatin1(Tool::PARAMETER_TYPE), QStringLiteral("sign-certificate"), QStringLiteral("%1"), TYPE_PANEL_WIDTH, tr("Signature type"));
    if (typeMenu)
    {
        typeMenu->addGroup(QString::fromLatin1(Tool::PARAMETER_FORMAT), tr("Format"));

        // 434:5884: "Signature with timestamp" shows the icon of its row (the timestamp)
        typeMenu->setButtonIconFollowsOption(true);
    }

    // 434:5327: Visible / Invisible and the empty signature fields (Q6.3)
    m_appearancePanel = builder.addMenu(QStringLiteral("appearance"), QStringLiteral("eye"), tr("Visible"), APPEARANCE_PANEL_WIDTH);
    updateAppearanceMenu();
    builder.addActionDivider();

    // 434:5062 Draw, Arrange, Clear All
    m_drawingPart->describeToolbar(builder);
    updateParts();
}

void PDFAppToolGUISignature::updateAppearanceMenu()
{
    using Tool = PDFDocumentToolSignature;
    using Helper = PDFAppToolGUISignatureHelper;

    if (!m_appearancePanel)
    {
        return;
    }

    if (m_appearanceRows.empty())
    {
        m_appearancePanel->addGroupCaption(tr("Appearance"));
        PDFTileRow* visibleRow = m_appearancePanel->addRow(tr("Visible"), tr("The drawn appearance is placed on the page"), QStringLiteral("eye"), PDFTileRow::Kind::MenuItem);
        visibleRow->setObjectName(QStringLiteral("signatureAppearance-visible"));
        visibleRow->setProperty(Helper::APPEARANCE_PROPERTY, QString::fromLatin1(Tool::APPEARANCE_VISIBLE));
        PDFTileRow* invisibleRow = m_appearancePanel->addRow(tr("Invisible"), tr("Recorded in the document only, nothing on the page"), QStringLiteral("eye-off"), PDFTileRow::Kind::MenuItem);
        invisibleRow->setObjectName(QStringLiteral("signatureAppearance-invisible"));
        invisibleRow->setProperty(Helper::APPEARANCE_PROPERTY, QString::fromLatin1(Tool::APPEARANCE_INVISIBLE));
        m_appearanceRows.emplace_back(QString::fromLatin1(Tool::APPEARANCE_VISIBLE), visibleRow);
        m_appearanceRows.emplace_back(QString::fromLatin1(Tool::APPEARANCE_INVISIBLE), invisibleRow);

        // Q6.3: "In field …" of each empty signature field; a field, which cannot be signed, is
        // Disabled with its reason (R6-25)
        const std::vector<PDFAppToolSignatureField>& fields = getEmptyFields();
        if (!fields.empty())
        {
            m_appearancePanel->addGroupDivider();
            m_appearancePanel->addGroupCaption(tr("Empty signature fields"));
        }
        for (const PDFAppToolSignatureField& field : fields)
        {
            PDFTileRow* row = m_appearancePanel->addRow(tr("In field %1").arg(field.name), field.disabledReason.isEmpty() ? tr("The signature goes into the field prepared by the form") : field.disabledReason,
                                                        QStringLiteral("signature"), PDFTileRow::Kind::MenuItem);
            row->setObjectName(QStringLiteral("signatureField-%1").arg(field.name));
            row->setTitleKind(PDFTileRow::TextKind::Data);
            row->setProperty(Helper::FIELD_PROPERTY, field.name);
            row->setEnabled(field.disabledReason.isEmpty());
            m_appearanceRows.emplace_back(field.name, row);
        }
        for (const auto& item : m_appearanceRows)
        {
            item.second->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
            connect(item.second.data(), &PDFTileRow::clicked, this, &PDFAppToolGUISignature::onAppearanceRowClicked);
        }
    }

    // The chosen row and the text of the button
    const QString field = getChosenField();
    const bool isVisible = isVisibleAppearance();
    for (const auto& item : m_appearanceRows)
    {
        if (!item.second)
        {
            continue;
        }
        const bool isField = item.second->property(Helper::FIELD_PROPERTY).isValid();
        const bool isChosen = isField ? item.first == field : (field.isEmpty() && (item.first == QLatin1String(Tool::APPEARANCE_VISIBLE)) == isVisible);
        item.second->setChecked(isChosen);
    }
    if (QToolButton* button = m_appearancePanel->getButton())
    {
        button->setText(!field.isEmpty() ? tr("In field %1").arg(field) : (isVisible ? tr("Visible") : tr("Invisible")));
        button->setIcon(PDFThemeIcons::getIcon(isVisible || !field.isEmpty() ? QStringLiteral("eye") : QStringLiteral("eye-off")));
    }
}

PDFTileRow* PDFAppToolGUISignature::getAppearanceRow(const QString& id) const
{
    for (const auto& item : m_appearanceRows)
    {
        if (item.first == id)
        {
            return item.second.data();
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------- left panel

void PDFAppToolGUISignature::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    ensureParts();
    if (getMode() == Mode::Sign)
    {
        describeSignLeftPanel(builder);
    }
    else
    {
        describeTimestampLeftPanel(builder);
    }
    updateParts();
}

void PDFAppToolGUISignature::describeSignLeftPanel(PDFAppToolPanelBuilder& builder)
{
    using Tool = PDFDocumentToolSignature;
    using Helper = PDFAppToolGUISignatureHelper;

    // The password typed before a change of the language goes with its card (6.1)
    clearSecrets();

    // header 434:5080: Signer, Certificate that signs the document
    m_leftHeader = builder.addPanelHeader(tr("Signer"), tr("Certificate that signs the document"));
    m_leftHeader->setObjectName(QStringLiteral("signatureSignerHeader"));

    // card-certificates 434:5084 (the rows of the list for signing, P5; no caption in the frame)
    m_certificateList = new PDFAppToolCertificateList(builder.getWidget());
    m_certificateList->setUsage(PDFAppToolCertificateUsage::Signing);
    if (QWidget* caption = m_certificateList->findChild<QWidget*>(QStringLiteral("certificateCaption")))
    {
        caption->hide();
    }
    connect(m_certificateList.data(), &PDFAppToolCertificateList::certificateChosen, this, &PDFAppToolGUISignature::onCertificateChosen);
    builder.addWidget(m_certificateList);

    // empty-state 434:6889: No certificate for signing yet, Create Certificate… (Primary), Import .pfx…
    PDFAppToolCard* empty = new PDFAppToolCard(builder.getWidget());
    empty->setObjectName(QStringLiteral("signatureEmptyState"));
    empty->getBodyLayout()->setContentsMargins(Helper::EMPTY_PADDING, Helper::EMPTY_PADDING, Helper::EMPTY_PADDING, Helper::EMPTY_PADDING);
    empty->getBodyLayout()->setSpacing(Helper::EMPTY_GAP);
    PDFIconBadge* tile = new PDFIconBadge(QStringLiteral("certificate"), Helper::EMPTY_TILE, Helper::EMPTY_ICON, empty);
    tile->setColors(PDFThemeColors::BgAccentSubtle, PDFThemeColors::IconAccent);
    tile->setRadius(Helper::EMPTY_TILE_RADIUS);
    empty->getBodyLayout()->addWidget(tile, 0, Qt::AlignHCenter);
    empty->getBodyLayout()->addWidget(PDFAppPageWidgets::createLabel(tr("No certificate for signing yet"), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, empty), 0, Qt::AlignHCenter);
    const bool isCreationAvailable = PDFAppToolCertificatePresetPart(getContext(), nullptr).isCreationAvailable();
    PDFAppParagraph* emptyText = PDFAppPageWidgets::createText(isCreationAvailable ? tr("Create a self-signed certificate or import a .pfx file to sign documents.")
                                                                                   : tr("In portable mode no certificate can be created or imported – its private key would stay on this computer."),
                                                               PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextSecondary, empty);
    emptyText->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    emptyText->setMinimumWidth(1);
    empty->getBodyLayout()->addWidget(emptyText);
    QPushButton* createButton = PDFAppPageWidgets::createButton(tr("Create Certificate…"), QStringLiteral("certificate-add"), true, empty);
    createButton->setObjectName(QStringLiteral("signatureEmptyCreate"));
    QPushButton* importButton = PDFAppPageWidgets::createButton(tr("Import .pfx…"), QStringLiteral("import"), false, empty);
    importButton->setObjectName(QStringLiteral("signatureEmptyImport"));
    for (QPushButton* button : { createButton, importButton })
    {
        button->setEnabled(isCreationAvailable);
        button->setToolTip(isCreationAvailable ? QString() : PDFAppToolCertificatePresetPart::getPortableText());
        empty->getBodyLayout()->addWidget(button);
    }
    connect(createButton, &QPushButton::clicked, this, &PDFAppToolGUISignature::onCreateClicked);
    connect(importButton, &QPushButton::clicked, this, &PDFAppToolGUISignature::onImportClicked);
    builder.addWidget(empty);
    m_emptyState = empty;

    // field-certificate-password 434:5116: "Password of jane_novak.pfx" and the field
    QWidget* passwordFrame = new QWidget(builder.getWidget());
    passwordFrame->setObjectName(QStringLiteral("signaturePasswordFrame"));
    QVBoxLayout* passwordLayout = PDFAppPageWidgets::createColumn(passwordFrame, Helper::FIELD_GAP);
    m_passwordLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Medium, 12, PDFThemeColors::TextSecondary, passwordFrame);
    m_passwordLabel->setObjectName(QStringLiteral("signaturePasswordLabel"));
    passwordLayout->addWidget(m_passwordLabel);
    m_passwordCard = new PDFAppToolPasswordCard(PDFAppToolPasswordCard::Kind::Certificate, passwordFrame);
    m_passwordCard->setObjectName(QStringLiteral("signaturePasswordCard"));
    m_passwordCard->setAlgorithm(PDFSecurityHandlerFactory::Certificate);
    connect(m_passwordCard.data(), &PDFAppToolPasswordCard::passwordChanged, this, &PDFAppToolGUISignature::onPasswordChanged);
    passwordLayout->addWidget(m_passwordCard);

    // The label of 434:5116 is the label of the field (QAccessible::Labelled relation, the focus by its
    // mnemonic); its text names the field (updateParts)
    m_passwordLabel->setBuddy(m_passwordCard->getPasswordField());
    m_noPasswordText = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, passwordFrame);
    m_noPasswordText->setObjectName(QStringLiteral("signatureNoPassword"));
    passwordLayout->addWidget(m_noPasswordText);
    builder.addWidget(passwordFrame);
    m_passwordFrame = passwordFrame;

    // section-Details 434:5118: Details with the tag Optional, Reason and Contact info
    PDFAppToolSectionHeader* detailsHeader = builder.addSection(tr("Details"));
    PDFTag* optionalTag = new PDFTag(tr("Optional"), detailsHeader);
    optionalTag->setObjectName(QStringLiteral("signatureOptionalTag"));
    detailsHeader->addTitleWidget(optionalTag);
    PDFAppToolCard* details = new PDFAppToolCard(builder.getWidget());
    details->setObjectName(QStringLiteral("signatureDetailsCard"));
    details->getBodyLayout()->setContentsMargins(Helper::DETAILS_PADDING, Helper::DETAILS_PADDING, Helper::DETAILS_PADDING, Helper::DETAILS_PADDING);
    details->getBodyLayout()->setSpacing(Helper::DETAILS_GAP);
    const std::pair<const char*, QString> fields[] = { { Tool::PARAMETER_REASON, tr("Reason") }, { Tool::PARAMETER_CONTACT_INFO, tr("Contact info") } };
    for (const auto& [parameter, label] : fields)
    {
        QWidget* field = new QWidget(details);
        QVBoxLayout* fieldLayout = PDFAppPageWidgets::createColumn(field, Helper::FIELD_GAP);
        fieldLayout->addWidget(PDFAppPageWidgets::createLabel(label, QFont::Normal, 12, PDFThemeColors::TextSecondary, field));
        if (PDFAppSettingControl* control = builder.createValueControl(QString::fromLatin1(parameter), field))
        {
            control->getWidget()->setObjectName(QStringLiteral("signatureField-") + QString::fromLatin1(parameter));
            control->getWidget()->setAccessibleName(label);

            // Text field 434:4502: the width of the card (249), not the fixed width of a settings control
            // (260), which would widen the left panel beyond its 300
            control->getWidget()->setMinimumWidth(1);
            control->getWidget()->setMaximumWidth(QWIDGETSIZE_MAX);
            fieldLayout->addWidget(control->getWidget());
        }
        details->getBodyLayout()->addWidget(field);
    }
    builder.addWidget(details);
    builder.endSection();

    // section-Timestamp authority 434:6110 (Signature with timestamp only)
    QWidget* authority = new QWidget(builder.getWidget());
    authority->setObjectName(QStringLiteral("signatureAuthoritySection"));
    QVBoxLayout* authorityLayout = PDFAppPageWidgets::createColumn(authority, PDFAppToolPanelBuilder::SECTION_GAP);
    authorityLayout->addWidget(new PDFAppToolSectionHeader(tr("Timestamp authority"), QString(), authority));
    m_authorityPart->setHintText(tr("RFC 3161 server that confirms the signing time. Type your own address or pick one from the list."));
    authorityLayout->addWidget(m_authorityPart->createContent(authority));
    builder.addWidget(authority);
    m_authoritySection = authority;

    reloadCertificates(QString());
}

void PDFAppToolGUISignature::describeTimestampLeftPanel(PDFAppToolPanelBuilder& builder)
{
    // header 435:7056: Signatures, Already in this document; the cards of the verification
    m_leftHeader = builder.addPanelHeader(tr("Signatures"), tr("Already in this document"));
    m_leftHeader->setObjectName(QStringLiteral("timestampSignaturesHeader"));
    QWidget* cards = new QWidget(builder.getWidget());
    cards->setObjectName(QStringLiteral("timestampSignatureCards"));
    PDFAppPageWidgets::createColumn(cards, PDFAppToolPanelBuilder::SECTION_GAP);
    builder.addWidget(cards);
    m_cardsContainer = cards;

    // 435:7091: the note below the cards
    m_timestampNote = builder.addHint(QString());
    m_timestampNote->setObjectName(QStringLiteral("timestampNote"));

    // 435:7096: No certificate is needed
    m_noCertificateCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("No certificate is needed"),
                                                tr("A document timestamp is issued by the timestamp authority. It proves that the document existed in this form at that time – it does not say who signed it."));
    m_noCertificateCallout->setObjectName(QStringLiteral("timestampNoCertificateCallout"));
    updateSignatureCards();
}

// --------------------------------------------------------------- right panel

void PDFAppToolGUISignature::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    ensureParts();

    // R6-4 (1), plan 2.6: a certified document, which forbids the run (Disabled with its reason)
    m_certifiedCallout = builder.addCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), tr("The document cannot be signed"), QString());
    m_certifiedCallout->setObjectName(QStringLiteral("signatureCertifiedCallout"));
    m_certifiedCallout->setCompactStyle();

    if (getMode() == Mode::Sign)
    {
        describeSignRightPanel(builder);
    }
    else
    {
        describeTimestampRightPanel(builder);
    }

    // Plan 2.6, Q6.17: the copy includes the unsaved changes (430:2481 style)
    m_unsavedCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), getMode() == Mode::Sign ? tr("The signed copy includes the unsaved changes.") : tr("The timestamped copy includes the unsaved changes."));
    m_unsavedCallout->setObjectName(QStringLiteral("signatureUnsavedCallout"));
    m_unsavedCallout->setCompactStyle();
    updateParts();
}

void PDFAppToolGUISignature::describeSignRightPanel(PDFAppToolPanelBuilder& builder)
{
    // header 434:5203: Appearance, "3 elements on page 308" / "Nothing drawn yet"
    m_rightHeader = builder.addPanelHeader(tr("Appearance"), QString());
    m_rightHeader->setObjectName(QStringLiteral("signatureAppearanceHeader"));

    // 434:6889: No appearance drawn
    m_noAppearanceCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("No appearance drawn"),
                                               tr("Use Draw to add text, a freehand curve, marks, shapes or an SVG image. Without graphics the signature is saved as invisible."));
    m_noAppearanceCallout->setObjectName(QStringLiteral("signatureNoAppearanceCallout"));

    // section-Elements 434:5207
    QWidget* elements = new QWidget(builder.getWidget());
    elements->setObjectName(QStringLiteral("signatureElementsSection"));
    QVBoxLayout* elementsLayout = PDFAppPageWidgets::createColumn(elements, PDFAppToolPanelBuilder::SECTION_GAP);
    elementsLayout->addWidget(new PDFAppToolSectionHeader(tr("Elements"), QString(), elements));
    elementsLayout->addWidget(m_drawingPart->createElementList(elements));
    builder.addWidget(elements);
    m_elementsSection = elements;

    // section-Style of the text label 434:5233
    QWidget* style = m_drawingPart->createStylePanel(builder.getWidget());
    builder.addWidget(style);
    m_styleSection = style;

    // section-Signature 434:5292: Type, Format, Appearance, Signer, Saved as
    builder.addSection(tr("Signature"));
    m_summaryRows = builder.addKeyValues();
    m_summaryRows->setObjectName(QStringLiteral("signatureSummary"));
    m_summaryRows->setRowGap(PDFAppToolGUISignatureHelper::SUMMARY_ROW_GAP);
    builder.endSection();
}

void PDFAppToolGUISignature::describeTimestampRightPanel(PDFAppToolPanelBuilder& builder)
{
    // header 435:7172: Timestamp authority, RFC 3161 server; section Address 435:7176
    m_rightHeader = builder.addPanelHeader(tr("Timestamp authority"), tr("RFC 3161 server"));
    m_rightHeader->setObjectName(QStringLiteral("timestampAuthorityHeader"));
    builder.addSection(tr("Address"));
    m_authorityPart->setHintText(tr("Pick an authority from the list or type your own address. The last used address is remembered."));
    builder.addWidget(m_authorityPart->createContent(builder.getWidget()));
    builder.endSection();

    // section-Result 435:7185: Type, Appearance, Signatures, Saved as
    builder.addSection(tr("Result"));
    m_summaryRows = builder.addKeyValues();
    m_summaryRows->setObjectName(QStringLiteral("timestampSummary"));
    m_summaryRows->setRowGap(PDFAppToolGUISignatureHelper::SUMMARY_ROW_GAP);
    builder.endSection();
}

QWidget* PDFAppToolGUISignature::createOverlay(QWidget* canvasArea)
{
    ensureParts();
    if (m_drawingPart)
    {
        QWidget* hint = m_drawingPart->createCanvasHint(canvasArea);
        updateCanvasHint();
        return hint;
    }

    // 435:7037: the hint of a document timestamp
    PDFAppToolDrawingHint* hint = new PDFAppToolDrawingHint(canvasArea);
    hint->setText(tr("A document timestamp is invisible – it shows in the Signatures panel"));
    return hint;
}

// ------------------------------------------------------------------- state

const std::vector<PDFAppToolSignatureField>& PDFAppToolGUISignature::getEmptyFields() const
{
    if (!m_fields)
    {
        const PDFAppDocument* document = getDocument();
        const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
        std::vector<PDFAppToolSignatureField> fields = PDFAppToolSignatureField::getEmptyFields(pdfDocument.data());

        // R6-25: the rules of the core (/SV, the permissions) decide the other reasons
        const std::shared_ptr<const PDFDocumentTool> tool = getTool() ? PDFDocumentToolRegistry::getTool(getTool()->getId()) : nullptr;
        if (tool && document && document->isReady())
        {
            const PDFDocumentToolContext context = PDFDocumentToolRunner::createContext(document->getSnapshot());
            for (PDFAppToolSignatureField& field : fields)
            {
                if (!field.disabledReason.isEmpty())
                {
                    continue;
                }
                QVariantMap parameters = getModel() ? getModel()->getValues() : QVariantMap();
                parameters.insert(QString::fromLatin1(PDFDocumentToolSignature::PARAMETER_FIELD), field.name);
                for (const PDFDocumentToolParameterError& error : tool->validate(context, parameters))
                {
                    if (error.parameter == QLatin1String(PDFDocumentToolSignature::PARAMETER_FIELD))
                    {
                        field.disabledReason = error.message;
                        break;
                    }
                }
            }
        }
        m_fields = std::move(fields);
    }
    return *m_fields;
}

QString PDFAppToolGUISignature::getChosenField() const
{
    return getModel() ? getModel()->getValue(QString::fromLatin1(PDFDocumentToolSignature::PARAMETER_FIELD)).toString() : QString();
}

bool PDFAppToolGUISignature::isTimestampRequested() const
{
    if (getMode() == Mode::Timestamp)
    {
        return true;
    }
    return getModel() && getModel()->getValue(QString::fromLatin1(PDFDocumentToolSignature::PARAMETER_TYPE)).toString() == QLatin1String(PDFDocumentToolSignature::TYPE_SIGNATURE_TIMESTAMP);
}

bool PDFAppToolGUISignature::isVisibleAppearance() const
{
    return getMode() == Mode::Sign && getModel() &&
           getModel()->getValue(QString::fromLatin1(PDFDocumentToolSignature::PARAMETER_APPEARANCE)).toString() != QLatin1String(PDFDocumentToolSignature::APPEARANCE_INVISIBLE);
}

QString PDFAppToolGUISignature::getSignerName() const
{
    const PDFAppToolCertificateItem* item = m_certificateList ? m_certificateList->getChosenItem() : nullptr;
    return item ? item->getTitle() : QString();
}

PDFAppDocumentSignatureVerifier* PDFAppToolGUISignature::getVerifier() const
{
    const PDFAppDocument* document = getDocument();
    PDFAppDocumentSearchCommandHandler* handler = document ? PDFAppDocumentSearchCommandHandler::findHandler(document) : nullptr;
    return handler ? handler->getSignatureVerifier() : nullptr;
}

int PDFAppToolGUISignature::getEarlierSignatureCount() const
{
    const PDFAppDocumentSignatureVerifier* verifier = m_verifier.data();
    const PDFAppSignatureVerificationPointer verification = verifier ? verifier->getVerification() : nullptr;
    return verification ? int(verification->results.size()) : 0;
}

QString PDFAppToolGUISignature::getCertificationRefusal() const
{
    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    if (!pdfDocument)
    {
        return QString();
    }

    // R6-4, 12.8.2.2.1, Table 257: a document certified with P = 1 takes no signature; a document
    // timestamp only in a revision of nothing else - not with the unsaved changes of the tab
    if (PDFDocMDPPermissions::getCertificationLevel(pdfDocument.data()) != PDFDocMDPPermissions::Level::NoChanges)
    {
        return QString();
    }
    if (getMode() == Mode::Sign)
    {
        return tr("The document is certified with no changes allowed. A signature would break the certification.");
    }
    if (document->isModified())
    {
        return tr("This document is certified with no changes allowed. Undo the unsaved changes to add a timestamp.");
    }
    return QString();
}

void PDFAppToolGUISignature::updateParts()
{
    const PDFAppDocument* document = getDocument();
    const QString refusal = getCertificationRefusal();
    if (m_certifiedCallout)
    {
        m_certifiedCallout->setTexts(getMode() == Mode::Sign ? tr("The document cannot be signed") : tr("The timestamp cannot be added"), refusal);
        m_certifiedCallout->setVisible(!refusal.isEmpty());
    }
    if (m_unsavedCallout)
    {
        m_unsavedCallout->setVisible(document && document->isModified() && refusal.isEmpty());
    }
    if (m_authoritySection)
    {
        m_authoritySection->setVisible(isTimestampRequested());
    }

    if (getMode() == Mode::Sign)
    {
        const bool hasRows = !m_certificates.empty();
        const PDFAppToolCertificateItem* item = m_certificateList ? m_certificateList->getChosenItem() : nullptr;
        if (m_certificateList)
        {
            m_certificateList->setVisible(hasRows);
        }
        if (m_emptyState)
        {
            m_emptyState->setVisible(!hasRows);
        }
        const bool isSystem = item && item->source == PDFAppToolCertificateItem::Source::SystemStore;
        const bool isPasswordNeeded = item && !isSystem && item->isPasswordProtected;
        if (m_passwordFrame)
        {
            m_passwordFrame->setVisible(hasRows);
            m_passwordFrame->setEnabled(item != nullptr);
        }
        if (m_passwordLabel)
        {
            // 434:5117 "Password of jane_novak.pfx"
            const QString fileName = item ? QFileInfo(item->filePath).fileName() : QString();
            m_passwordLabel->setText(isSystem ? tr("Private key") : (item ? tr("Password of %1").arg(fileName) : tr("Password of the certificate file")));

            // The field is named by its label for a screen reader (the label, never the password)
            if (m_passwordCard)
            {
                m_passwordCard->setAccessibleLabels(m_passwordLabel->text(), QString());
            }
        }
        if (m_passwordCard)
        {
            m_passwordCard->setVisible(!item || isPasswordNeeded);
            QString error = m_unlockError;
            if (error.isEmpty() && m_passwordState == PasswordState::Invalid)
            {
                // 434:7143
                error = tr("Password to open certificate is invalid.");
            }
            m_passwordCard->setErrorText(error);
        }
        if (m_noPasswordText)
        {
            // Plan 2.6: the PIN of a system certificate is asked by the system
            m_noPasswordText->setText(isSystem ? tr("The operating system asks for the PIN") : tr("Not needed – the certificate file has no password."));
            m_noPasswordText->setVisible(item && !isPasswordNeeded);
        }

        const bool isVisible = isVisibleAppearance() && getChosenField().isEmpty();
        const bool hasDrawing = m_drawingPart && !m_drawingPart->isEmpty();
        if (m_drawingPart)
        {
            // 434:6626: Draw, Arrange and Clear All are Disabled for an invisible signature
            m_drawingPart->setActionsEnabled(isVisible);
        }
        if (m_rightHeader)
        {
            QString subtitle;
            if (!isVisible && getChosenField().isEmpty())
            {
                // 434:6626
                subtitle = tr("Invisible signature");
            }
            else if (!getChosenField().isEmpty())
            {
                subtitle = tr("In the field %1").arg(getChosenField());
            }
            else if (hasDrawing)
            {
                // 434:5206 "3 elements on page 308"
                const std::vector<PDFInteger> pages = m_drawingPart->getPageIndices();
                const int count = m_drawingPart->getElementCount();
                if (pages.size() == 1)
                {
                    const QString page = PDFAppLocale::formatNumber(qint64(pages.front() + 1));
                    subtitle = count == 1 ? tr("1 element on page %1").arg(page) : tr("%Ln elements on page %1", nullptr, count).arg(page);
                }
                else
                {
                    subtitle = PDFAppToolGUISignByHand::getCountText(count, int(pages.size()));
                }
            }
            else
            {
                subtitle = tr("Nothing drawn yet");
            }
            m_rightHeader->setTexts(tr("Appearance"), subtitle);
        }
        if (m_noAppearanceCallout)
        {
            m_noAppearanceCallout->setVisible(isVisible && !hasDrawing);
        }
        if (m_elementsSection)
        {
            m_elementsSection->setVisible(isVisible && hasDrawing);
        }
        if (m_styleSection)
        {
            m_styleSection->setVisible(isVisible && hasDrawing);
        }
        updateAppearanceMenu();
        updateCanvasHint();
    }
    else
    {
        // 435:7091: the note; plan 2.6 Timestamp of a document without signatures: no note
        const int count = getEarlierSignatureCount();
        if (m_timestampNote)
        {
            m_timestampNote->setText(count == 1 ? tr("The timestamp covers the whole document including this signature. It stays valid.")
                                                : tr("The timestamp covers the whole document including these %Ln signatures. They stay valid.", nullptr, count));
            m_timestampNote->setVisible(count > 0);
        }
        if (m_leftHeader)
        {
            m_leftHeader->setVisible(count > 0);
        }
    }
    updateSummary();
}

void PDFAppToolGUISignature::updateSummary()
{
    using Tool = PDFDocumentToolSignature;

    if (!m_summaryRows)
    {
        return;
    }
    m_summaryRows->clear();

    if (getMode() == Mode::Sign)
    {
        // 434:5296 ... 434:5305 (1568:19247 Format)
        m_summaryRows->addRow(tr("Type"), isTimestampRequested() ? tr("With timestamp") : tr("Signature"));
        const QString format = getModel() ? getModel()->getValue(QString::fromLatin1(Tool::PARAMETER_FORMAT)).toString() : QString();
        m_summaryRows->addRow(tr("Format"), PDFDocumentToolSignature::getFormat(format) == PDFSignatureFormat::Pkcs7 ? tr("Compatible (PKCS#7)") : tr("PAdES"));

        QString appearance;
        const QString field = getChosenField();
        if (!field.isEmpty())
        {
            appearance = tr("In field %1").arg(field);
        }
        else if (!isVisibleAppearance())
        {
            appearance = tr("Invisible");
        }
        else if (m_drawingPart && !m_drawingPart->isEmpty())
        {
            // 434:7784: the visible signature goes to the first page with graphics
            appearance = tr("Visible · page %1").arg(PDFAppLocale::formatNumber(qint64(m_drawingPart->getPageIndices().front() + 1)));
        }
        else
        {
            appearance = tr("Visible · not drawn");
        }
        m_summaryRows->addRow(tr("Appearance"), appearance);
        const QString signer = getSignerName();
        m_summaryRows->addRow(tr("Signer"), signer.isEmpty() ? tr("Not selected") : signer, PDFComponentPainter::TextKind::Data);
    }
    else
    {
        // 435:7187: Type, Appearance, Signatures ("1 stays valid", %Ln), Saved as
        m_summaryRows->addRow(tr("Type"), tr("Timestamp"));
        m_summaryRows->addRow(tr("Appearance"), tr("Invisible"));
        const int count = getEarlierSignatureCount();
        // 435:7197 "1 stays valid" (R6-29: %Ln)
        QString signatures = tr("None");
        if (count > 0)
        {
            signatures = count == 1 ? tr("1 stays valid") : tr("%Ln stay valid", nullptr, count);
        }
        m_summaryRows->addRow(tr("Signatures"), signatures);
    }
    m_summaryRows->addRow(tr("Saved as"), tr("New file"));
}

void PDFAppToolGUISignature::updateCanvasHint()
{
    if (!m_drawingPart)
    {
        return;
    }

    // 434:4688 / 434:4338 (canvas-hint), 434:6626
    QString hint;
    if (!getChosenField().isEmpty())
    {
        hint = tr("The signature goes into the field %1 of the form").arg(getChosenField());
    }
    else if (!isVisibleAppearance())
    {
        hint = tr("Invisible signature – nothing is drawn on the page, the signature shows in the Signatures panel");
    }
    else if (m_drawingPart->isEmpty())
    {
        hint = tr("Draw the appearance on the page with the Draw tools – the signature field takes its outline");
    }
    else
    {
        hint = tr("Signature appearance on page %1 · drag to move, use the handles to resize").arg(PDFAppLocale::formatNumber(qint64(m_drawingPart->getPageIndices().front() + 1)));
    }
    m_drawingPart->setCanvasHintText(hint);
}

void PDFAppToolGUISignature::updateSignatureCards()
{
    if (!m_cardsContainer)
    {
        return;
    }

    for (const QPointer<PDFSignatureCard>& card : m_signatureCards)
    {
        if (card)
        {
            card->hide();
            card->deleteLater();
        }
    }
    m_signatureCards.clear();

    const PDFAppSignatureVerificationPointer verification = m_verifier ? m_verifier->getVerification() : nullptr;
    if (!verification)
    {
        return;
    }

    // 435:7061: the cards of the panel Signatures (R6-2 / P1a: the signer of the SignerInfo)
    const std::vector<QByteArray> trustedCertificates = PDFAppDocumentSignatureVerifier::readSettings(getContext()).trustedCertificates;
    for (size_t i = 0; i < verification->results.size(); ++i)
    {
        const PDFComponentPainter::SignatureCard content = PDFAppDocumentSignatureVerifier::getCard(verification->results[i], verification->isStrict, trustedCertificates);
        PDFSignatureCard* card = new PDFSignatureCard(m_cardsContainer);
        card->setObjectName(QStringLiteral("timestampSignatureCard%1").arg(i));
        card->setSigner(content.signer);
        card->setMeta(content.meta);
        card->setSummary(content.summary);
        card->setStatus(content.status);
        card->setDetails(content.details);
        card->setCertificates(content.certificates);
        card->setExpanded(false);
        if (QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(m_cardsContainer->layout()))
        {
            layout->addWidget(card);
        }
        m_signatureCards.emplace_back(card);
    }
}

void PDFAppToolGUISignature::notifyStateChanged()
{
    // The Primary button and the status bar read the parts; both emit (the last calls)
    QPointer<PDFAppToolGUISignature> guard(this);
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
}

void PDFAppToolGUISignature::reloadCertificates(const QString& filePath)
{
    QString choice = filePath;
    if (choice.isEmpty() && m_certificateList)
    {
        choice = m_certificateList->getChosenFilePath();
    }
    if (choice.isEmpty())
    {
        // A new list (the panels made again after a change of the language) keeps the choice of the user
        choice = m_chosenCertificatePath;
    }

    // The rows of the usage Signing: a certificate, which only signs, is no NoEncryptionUsage row (5.3, Q6.2)
    m_certificates = m_provider.getCertificates(PDFAppToolCertificateUsage::Signing);
    const PDFAppToolCertificateItem* chosen = nullptr;
    const PDFAppToolCertificateItem* first = nullptr;
    for (const PDFAppToolCertificateItem& item : m_certificates)
    {
        if (!item.isChoosable())
        {
            continue;
        }
        if (!first)
        {
            first = &item;
        }
        if (!choice.isEmpty() && item.filePath == choice)
        {
            chosen = &item;
        }
    }
    if (!chosen)
    {
        chosen = first;
    }

    m_chosenCertificatePath = chosen ? chosen->filePath : QString();
    if (m_certificateList)
    {
        m_certificateList->setCertificates(m_certificates);
        m_certificateList->choose(m_chosenCertificatePath);
    }
}

// ------------------------------------------------------------------- the shell

QString PDFAppToolGUISignature::getConfirmText() const
{
    // 434:4338 "Save Signed Copy", 435:7037 "Save Timestamped Copy"
    return getMode() == Mode::Sign ? tr("Save Signed Copy") : tr("Save Timestamped Copy");
}

bool PDFAppToolGUISignature::isConfirmEnabled() const
{
    if (!BaseClass::isConfirmEnabled() || !getCertificationRefusal().isEmpty())
    {
        return false;
    }

    // A question before the run is open (434:7495, 434:7784): the run waits for its answer
    if (m_pendingQuestion != Question::None || m_questionDialog || m_timestampErrorDialog)
    {
        return false;
    }

    // 435:7693: the address of the authority
    if (isTimestampRequested() && (!m_authorityPart || !m_authorityPart->isUrlValid()))
    {
        return false;
    }
    if (getMode() == Mode::Timestamp)
    {
        return true;
    }

    // 434:4688 (empty password), 434:7143 (a wrong password), 434:6889 (no certificate)
    const PDFAppToolCertificateItem* item = m_certificateList ? m_certificateList->getChosenItem() : nullptr;
    if (!item || !item->isChoosable() || !m_unlockError.isEmpty())
    {
        return false;
    }
    if (item->source == PDFAppToolCertificateItem::Source::SystemStore || !item->isPasswordProtected)
    {
        return true;
    }
    if (m_isRetrying && !m_retryPassword.isEmpty() && m_retryFilePath == item->filePath)
    {
        return true;
    }
    return m_passwordCard && !m_passwordCard->isEmpty() && m_passwordState != PasswordState::Invalid;
}

QString PDFAppToolGUISignature::getStatusText() const
{
    const QString refusal = getCertificationRefusal();
    if (!refusal.isEmpty())
    {
        return refusal;
    }
    if (isTimestampRequested() && m_authorityPart && !m_authorityPart->isUrlValid())
    {
        // 435:7693
        return tr("Address of the timestamp authority is not valid");
    }
    if (getMode() == Mode::Timestamp)
    {
        // 435:7037
        return tr("The timestamped copy is written to a new file · the open document stays unchanged");
    }

    const PDFAppToolCertificateItem* item = m_certificateList ? m_certificateList->getChosenItem() : nullptr;
    if (m_certificates.empty())
    {
        // 434:6889
        return tr("Create or import a certificate to sign");
    }
    if (!item)
    {
        return tr("Choose the certificate, which signs the document");
    }
    if (m_passwordState == PasswordState::Invalid || !m_unlockError.isEmpty())
    {
        return tr("Password to open certificate is invalid");
    }
    if (item->source == PDFAppToolCertificateItem::Source::PfxFile && item->isPasswordProtected && m_passwordCard && m_passwordCard->isEmpty())
    {
        // 434:4688
        return tr("Enter the certificate password to sign");
    }

    // 434:4338
    return tr("The signed copy is written to a new file · the open document stays unchanged");
}

std::vector<PDFSecurityHandler::Permission> PDFAppToolGUISignature::getRunPermissions() const
{
    // R6-8: the permissions of the values of the run (a new field: bits 4 and 6, an existing field:
    // bit 9 or 6) - the shell asks them by PDFAppDocumentPermissions::request with the parameters
    const std::shared_ptr<const PDFDocumentTool> tool = getTool() ? PDFDocumentToolRegistry::getTool(getTool()->getId()) : nullptr;
    return tool ? tool->getRequiredPermissions(BaseClass::getRunParameters()) : std::vector<PDFSecurityHandler::Permission>();
}

bool PDFAppToolGUISignature::handlesPermissionRefusal() const
{
    // The entry (coordinator, P4): the tool opens, when at least one way of the run is permitted -
    // a new field (the row of the catalog, bits 4 and 6) or an empty signature field, which can be
    // signed (bit 9 or 6, R6-8). The shell refuses the entry only when no way is permitted (its
    // owner password dialog, 339:10328); the run asks the permissions of its values
    // (PDFDocumentToolShell::getRunPermissionRequest: getRunPermissions and bit 9 or 6 of a field).
    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    const PDFDocumentToolCatalogEntry* entry = getTool();
    if (getMode() != Mode::Sign || !pdfDocument || !entry)
    {
        return false;
    }
    if (PDFAppDocumentPermissions::isAllowed(pdfDocument.data(), PDFAppPermissionRequest::createTool(entry->getId())))
    {
        // The default way is permitted: the shell does not refuse anyway
        return false;
    }

    const std::shared_ptr<const PDFDocumentTool> tool = PDFDocumentToolRegistry::getTool(entry->getId());
    if (!tool)
    {
        return false;
    }
    for (const PDFAppToolSignatureField& field : PDFAppToolSignatureField::getEmptyFields(pdfDocument.data()))
    {
        if (!field.disabledReason.isEmpty())
        {
            continue;
        }
        QVariantMap parameters;
        parameters.insert(QString::fromLatin1(PDFDocumentToolSignature::PARAMETER_FIELD), field.name);
        if (PDFAppDocumentPermissions::isAllowed(pdfDocument.data(), PDFAppPermissionRequest::createTool(entry->getId(), {}, parameters)))
        {
            return true;
        }
    }
    return false;
}

QString PDFAppToolGUISignature::getRunPassword() const
{
    const PDFAppToolCertificateItem* item = m_certificateList ? m_certificateList->getChosenItem() : nullptr;
    if (m_isRetrying && item && item->filePath == m_retryFilePath)
    {
        return m_retryPassword;
    }
    return m_passwordCard ? m_passwordCard->getPassword() : QString();
}

QVariantMap PDFAppToolGUISignature::getRunParameters() const
{
    using Tool = PDFDocumentToolSignature;

    // 6.1: the password never enters the model; it goes to the values of the run only
    QVariantMap values = BaseClass::getRunParameters();
    values.remove(QString::fromLatin1(Tool::PARAMETER_CERTIFICATE_PASSWORD));
    if (m_authorityPart)
    {
        values.insert(QString::fromLatin1(Tool::PARAMETER_TSA_URL), m_authorityPart->getUrl());
    }
    if (getMode() == Mode::Sign)
    {
        for (auto it = m_certificateRunValues.cbegin(); it != m_certificateRunValues.cend(); ++it)
        {
            values.insert(it.key(), it.value());
        }
    }
    return values;
}

bool PDFAppToolGUISignature::prepareCertificate(QVariantMap* values, std::vector<PDFCertificateEntry>* certificates, QString* errorMessage) const
{
    using Tool = PDFDocumentToolSignature;

    const PDFAppToolCertificateItem* item = m_certificateList ? m_certificateList->getChosenItem() : nullptr;
    if (!item || !item->isChoosable())
    {
        *errorMessage = tr("Choose a certificate, which can sign the document.");
        return false;
    }

    std::optional<PDFCertificateEntry> entry;
    QString fingerprint = item->fingerprint;
    QString password;
    if (item->source == PDFAppToolCertificateItem::Source::SystemStore)
    {
        // Q6.2: the key stays in the system, which asks for its PIN
        entry = m_provider.findEntry(item->filePath);
    }
    else
    {
        // The .pfx file opens by its password (the cache gets its certificate, Q5.11); the copy of
        // the password is cleared after it
        password = item->isPasswordProtected ? getRunPassword() : QString();
        entry = m_provider.unlock(item->filePath, password, errorMessage);
        if (entry)
        {
            const PDFAppToolCertificateItem unlocked = PDFAppToolCertificateProvider::createItem(*entry, entry->info, QDateTime::currentDateTimeUtc(), PDFAppToolCertificateUsage::Signing);
            fingerprint = unlocked.fingerprint;
            if (!unlocked.isChoosable())
            {
                *errorMessage = PDFAppToolCertificateProvider::getDescription(unlocked);
                entry.reset();
            }
        }
    }

    if (!entry)
    {
        PDFSecureMemory::clear(password);
        if (errorMessage->isEmpty())
        {
            *errorMessage = tr("Invalid certificate or password.");
        }
        return false;
    }

    values->insert(QString::fromLatin1(Tool::PARAMETER_CERTIFICATE), fingerprint);
    if (!password.isEmpty())
    {
        values->insert(QString::fromLatin1(Tool::PARAMETER_CERTIFICATE_PASSWORD), password);
    }
    certificates->push_back(std::move(*entry));
    PDFSecureMemory::clear(password);
    return true;
}

void PDFAppToolGUISignature::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    BaseClass::prepareEnvironment(environment);
    dropCertificateRunValues();
    if (!environment)
    {
        return;
    }

    // The questions before the run (434:7495, 434:7784): the run is refused, the question opens from
    // the event loop and its answer confirms again
    const Question question = getNeededQuestion();
    if (question != Question::None)
    {
        m_pendingQuestion = question;
        QMetaObject::invokeMethod(const_cast<PDFAppToolGUISignature*>(this), &PDFAppToolGUISignature::onPendingRunEnded, Qt::QueuedConnection);
        return;
    }

    // S6.6: the network only now, on the action of the user, and only for a timestamp
    if (isTimestampRequested())
    {
        environment->timestampClient = PDFTimestampClient::createNetworkClient();

        // The authority is trusted by the trust of the Signatures panel (the certificates of the user, the
        // system store by its setting; the core adds AATL) - the fact "· Authority not trusted" otherwise
        environment->certificateStore = PDFAppDocumentToolCertificates::createVerificationStore(getContext());
    }

    if (getMode() == Mode::Sign)
    {
        QString errorMessage;
        std::vector<PDFCertificateEntry> certificates;
        QPointer<const PDFAppToolGUISignature> guard(this);
        const bool isPrepared = prepareCertificate(&m_certificateRunValues, &certificates, &errorMessage);
        if (!guard)
        {
            // The unlock writes the cache of the certificates (its observers run): the GUI is gone
            return;
        }
        if (isPrepared)
        {
            environment->userCertificates = std::move(certificates);

            // Q6.2: the PIN dialog of a key of the store of Windows belongs to the window of the tool
            // (only its native handle goes to the run, the top-level window is native anyway)
            if (m_certificateList)
            {
                environment->parentWindow = quintptr(m_certificateList->window()->winId());
            }
        }
        else
        {
            // 433:3972 style: the field shows the error, the confirmation is Disabled until a change
            PDFAppToolGUISignature* self = const_cast<PDFAppToolGUISignature*>(this);
            self->m_unlockError = errorMessage;
            self->dropCertificateRunValues();
            QMetaObject::invokeMethod(self, &PDFAppToolGUISignature::onPendingRunEnded, Qt::QueuedConnection);
        }
    }
}

void PDFAppToolGUISignature::dropCertificateRunValues() const
{
    // The password of prepareEnvironment is overwritten, when nobody else holds it
    for (auto it = m_certificateRunValues.begin(); it != m_certificateRunValues.end(); ++it)
    {
        if (it.value().typeId() == QMetaType::QString)
        {
            QString text = it.value().toString();
            it.value() = QVariant();
            PDFSecureMemory::clear(text);
        }
    }
    m_certificateRunValues.clear();
}

void PDFAppToolGUISignature::forgetRetryPassword()
{
    PDFSecureMemory::clear(m_retryPassword);
    m_retryPassword.clear();
    m_retryFilePath.clear();
    m_isRetrying = false;
    m_retryAnsweredQuestion = Question::None;
}

void PDFAppToolGUISignature::confirmRetry()
{
    // The retry is the failed run again: its answered question is not asked again
    m_answeredQuestion = m_retryAnsweredQuestion;
    QPointer<PDFAppToolGUISignature> guard(this);
    PDFDocumentToolShell* shell = getShell();
    if (shell)
    {
        shell->confirm();
    }
    if (!guard)
    {
        return;
    }

    // R6-11 (review RS6APP): the kept password lives only until the error dialog closes and its run
    // starts - a retry, which did not start (refused by the permissions, a busy document, a failed
    // unlock), forgets it; the field is empty, the user types the password again
    shell = getShell();
    if (!shell || shell->getState() != PDFDocumentToolShell::State::Running)
    {
        forgetRetryPassword();
        dropCertificateRunValues();
        updateParts();
        notifyStateChanged();
    }
}

void PDFAppToolGUISignature::clearSecrets()
{
    // 6.1: the field, its undo history, the error of a failed unlock and the values of the run
    m_passwordGeneration++;
    if (m_passwordTimer)
    {
        m_passwordTimer->stop();
    }
    if (m_passwordCard)
    {
        m_passwordCard->clear();
        m_passwordCard->setErrorText(QString());
    }
    m_passwordState = PasswordState::NotChecked;
    m_unlockError.clear();
    dropCertificateRunValues();
}

void PDFAppToolGUISignature::fillResultInput(PDFAppToolResultInput* input) const
{
    // 434:9276, 435:8356: the dialog of the file only (FileOnly)
    if (getMode() == Mode::Sign)
    {
        input->title = tr("The document is signed");
        input->subtitle = tr("Choose where to save the signed copy. The open document stays unchanged.");
        input->statusText = tr("Document signed – save the signed copy");
        input->fileConfirmText = tr("Save Signed Copy");
        input->fileOpenText = tr("Open the signed copy in a new tab");
    }
    else
    {
        input->title = tr("The timestamp is added");
        input->subtitle = tr("Choose where to save the timestamped copy. The open document stays unchanged.");
        input->statusText = tr("Timestamp added – save the timestamped copy");
        input->fileConfirmText = tr("Save Timestamped Copy");
        input->fileOpenText = tr("Open the timestamped copy in a new tab");
    }
    PDFAppToolGUISignatureHelper::formatTimeFact(&input->result, getMode());
}

bool PDFAppToolGUISignature::confirmResult(const PDFDocumentToolResult& result)
{
    // R6-11: a successful run forgets the kept password
    Q_UNUSED(result);
    m_isRunPending = false;
    forgetRetryPassword();
    return true;
}

void PDFAppToolGUISignature::onParametersChanged(const QString& name)
{
    using Tool = PDFDocumentToolSignature;

    if (name == QLatin1String(Tool::PARAMETER_TSA_URL) && m_authorityPart && getModel())
    {
        // Undo / Redo of the address; the part emits its change (its slots may delete this GUI)
        QPointer<PDFAppToolGUISignature> guard(this);
        m_authorityPart->setUrl(getModel()->getValue(name).toString());
        if (!guard)
        {
            return;
        }
    }
    updateParts();
}

void PDFAppToolGUISignature::setContentEnabled(bool isEnabled)
{
    // K6.8 (review RS6APP): the drawing of a visible signature is no input while the tool runs
    BaseClass::setContentEnabled(isEnabled);
    if (m_drawingPart)
    {
        m_drawingPart->setInputEnabled(isEnabled);
    }
}

void PDFAppToolGUISignature::onRunStateChanged(bool isRunning)
{
    if (isRunning)
    {
        // 6.1: the field is emptied, when the run has taken its values; R6-11: the password of the
        // run is kept until its result decides (an error of the authority keeps it for Try Again)
        const PDFAppToolCertificateItem* item = m_certificateList ? m_certificateList->getChosenItem() : nullptr;
        const QString password = getRunPassword();
        if (getMode() == Mode::Sign && isTimestampRequested() && item && !password.isEmpty())
        {
            if (!m_isRetrying || m_retryFilePath != item->filePath)
            {
                PDFSecureMemory::clear(m_retryPassword);
                m_retryPassword = password;
                m_retryFilePath = item->filePath;
            }
        }
        if (!m_isRetrying)
        {
            m_retryAnsweredQuestion = m_answeredQuestion;
        }
        m_isRetrying = false;
        m_answeredQuestion = Question::None;
        clearSecrets();
        updateParts();
        return;
    }

    // The result decides the kept password: confirmResult, onRunNotAccepted, or nothing (a stopped
    // run) - the queued check forgets it, when no dialog of the authority has opened
    m_isRunPending = true;
    QMetaObject::invokeMethod(this, &PDFAppToolGUISignature::onPendingRunEnded, Qt::QueuedConnection);
}

void PDFAppToolGUISignature::onRunRefused()
{
    // The run did not start: the values prepared for it go (6.1); the field keeps its text
    dropCertificateRunValues();
}

void PDFAppToolGUISignature::onRunNotAccepted(const PDFDocumentToolResult& result)
{
    if (result.status == PDFDocumentToolResultStatus::Failed && result.data.contains(QLatin1String(PDFDocumentToolSignature::DATA_TIMESTAMP_ERROR)))
    {
        // 434:8518, 435:8117 (R6-11): the password stays until the dialog closes
        openTimestampErrorDialog(result);
        return;
    }

    m_isRunPending = false;
    forgetRetryPassword();
    BaseClass::onRunNotAccepted(result);
}

void PDFAppToolGUISignature::onPendingRunEnded()
{
    // A question asked by prepareEnvironment opens now (outside of the confirmation of the shell)
    if (m_pendingQuestion != Question::None)
    {
        const Question question = m_pendingQuestion;
        m_pendingQuestion = Question::None;
        openQuestion(question);
        return;
    }

    if (m_isRunPending && !m_timestampErrorDialog)
    {
        m_isRunPending = false;
        forgetRetryPassword();
    }
    updateParts();
    notifyStateChanged();
}

bool PDFAppToolGUISignature::handleEscape()
{
    return m_drawingPart && (m_drawingPart->endActiveElement() || m_drawingPart->clearSelection());
}

QString PDFAppToolGUISignature::getEscapeModeHint() const
{
    return m_drawingPart ? m_drawingPart->getEscapeHint() : QString();
}

bool PDFAppToolGUISignature::hasUnsavedWork() const
{
    return getMode() == Mode::Sign && m_drawingPart && !m_drawingPart->isEmpty();
}

PDFAppToolCloseQuestion PDFAppToolGUISignature::getCloseQuestion() const
{
    // No frame (owner 2026-11-20): the question of Sign by Hand 435:10500 for the drawing of the appearance
    const int count = m_drawingPart ? m_drawingPart->getElementCount() : 0;
    PDFAppToolCloseQuestion question;
    question.title = tr("Discard the drawing of the appearance?");
    question.text = count == 1 ? tr("Closing the tool removes the element you have drawn for the appearance of the signature. The document stays as it was.")
                               : tr("Closing the tool removes the %Ln elements you have drawn for the appearance of the signature. The document stays as it was.", nullptr, count);
    question.keepText = tr("Keep Drawing");
    question.discardText = tr("Discard Drawing");
    return question;
}

void PDFAppToolGUISignature::onDocumentChanged()
{
    // Another version (unlocked by the owner password, a change of an assistant): the fields and
    // their reasons again; the rows of the menu keep their fields (made again with the toolbar), a
    // field, which cannot be signed any more, is refused by the validation of the run
    m_fields.reset();
    updateParts();
    notifyStateChanged();
}

void PDFAppToolGUISignature::retranslate()
{
    // The panels are made again by the shell (the typed password goes with its card, 6.1)
    m_fields.reset();
    clearSecrets();
}

// ------------------------------------------------------------------- questions

PDFAppToolGUISignature::Question PDFAppToolGUISignature::getNeededQuestion() const
{
    if (getMode() != Mode::Sign || !isVisibleAppearance() || !m_drawingPart)
    {
        return Question::None;
    }
    if (m_drawingPart->isEmpty())
    {
        // 434:7495 for a new field and an empty field of the document alike: the core needs the drawing of
        // a visible appearance (the field takes it in its /Rect, Q6.3); Sign Invisibly signs the field invisibly
        return m_answeredQuestion == Question::NoGraphics ? Question::None : Question::NoGraphics;
    }
    if (!getChosenField().isEmpty())
    {
        // The drawing fits the field
        return Question::None;
    }
    if (m_drawingPart->getPageIndices().size() > 1)
    {
        return m_answeredQuestion == Question::MultiplePages ? Question::None : Question::MultiplePages;
    }
    return Question::None;
}

QWidget* PDFAppToolGUISignature::getDialogParent() const
{
    return getHost() && getHost()->getKeyWidget() ? getHost()->getKeyWidget()->window() : nullptr;
}

void PDFAppToolGUISignature::openQuestion(Question question)
{
    if (m_questionDialog || question == Question::None)
    {
        return;
    }

    PDFMessageBox* dialog = nullptr;
    QPushButton* acceptButton = nullptr;
    if (question == Question::NoGraphics)
    {
        // 434:7495: Back, Sign Invisibly (Primary)
        dialog = new PDFMessageBox(PDFThemeIcons::Badge::Warning, tr("Sign without a visible appearance?"),
                                   tr("No signature graphics were created, so the signature will have no visible appearance on the page."), QDialogButtonBox::NoButton, getDialogParent());
        QPushButton* backButton = dialog->addActionButton(tr("Back"), PDFMessageBox::ActionPosition::Trailing, false);
        acceptButton = dialog->addActionButton(tr("Sign Invisibly"), PDFMessageBox::ActionPosition::Trailing, true);
        acceptButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("eye-off")));
        connect(backButton, &QPushButton::clicked, dialog, &QDialog::reject);
    }
    else
    {
        // 434:7784: Back, Sign With Page N Graphics (Primary)
        const std::vector<PDFInteger> pages = m_drawingPart ? m_drawingPart->getPageIndices() : std::vector<PDFInteger>();
        const QString first = pages.empty() ? QString() : PDFAppLocale::formatNumber(qint64(pages.front() + 1));
        QStringList others;
        for (size_t i = 1; i < pages.size(); ++i)
        {
            others << PDFAppLocale::formatNumber(qint64(pages[i] + 1));
        }
        const QString last = others.isEmpty() ? QString() : others.takeLast();
        const QString otherPages = others.isEmpty() ? last : tr("%1 and %2").arg(others.join(tr(", ", "Separator of the numbers of pages")), last);
        // 434:7784 "Graphics on pages 12 and 308 are left out"; one other page: "on page 12"
        const QString text = pages.size() == 2 ? tr("A signature has one appearance. Only the graphics on page %1, the first page that contains them, will be used. Graphics on page %2 are left out.").arg(first, otherPages)
                                               : tr("A signature has one appearance. Only the graphics on page %1, the first page that contains them, will be used. Graphics on pages %2 are left out.").arg(first, otherPages);
        dialog = new PDFMessageBox(PDFThemeIcons::Badge::Warning, tr("Graphics are on more than one page"), text, QDialogButtonBox::NoButton, getDialogParent());
        QPushButton* backButton = dialog->addActionButton(tr("Back"), PDFMessageBox::ActionPosition::Trailing, false);
        acceptButton = dialog->addActionButton(tr("Sign With Page %1 Graphics").arg(first), PDFMessageBox::ActionPosition::Trailing, true);
        acceptButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("sign-certificate")));
        connect(backButton, &QPushButton::clicked, dialog, &QDialog::reject);
    }

    // new + QPointer, it deletes itself
    dialog->setObjectName(QStringLiteral("signatureQuestionDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setProperty("question", int(question));
    connect(acceptButton, &QPushButton::clicked, this, &PDFAppToolGUISignature::onQuestionAccepted);
    connect(dialog, &QDialog::finished, this, &PDFAppToolGUISignature::onQuestionFinished);
    m_questionDialog = dialog;
    m_openQuestion = question;
    dialog->open();
    notifyStateChanged();
}

void PDFAppToolGUISignature::onQuestionAccepted()
{
    using Tool = PDFDocumentToolSignature;

    const Question question = m_openQuestion;
    if (m_questionDialog)
    {
        // The closed dialog is deleted later: it is no open question any more
        disconnect(m_questionDialog.data(), nullptr, this, nullptr);
        m_questionDialog->close();
        m_questionDialog = nullptr;
    }
    m_openQuestion = Question::None;
    m_answeredQuestion = question;

    // 434:7495: the signature is invisible (one step of the model)
    QPointer<PDFAppToolGUISignature> guard(this);
    if (question == Question::NoGraphics && getModel())
    {
        getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_APPEARANCE), QString::fromLatin1(Tool::APPEARANCE_INVISIBLE));
        if (!guard)
        {
            return;
        }
    }

    // The answer confirms again (the last call: the run starts, the GUI may change)
    if (PDFDocumentToolShell* shell = getShell())
    {
        shell->confirm();
    }
}

void PDFAppToolGUISignature::onQuestionFinished()
{
    // Back, Esc: the user edits again (the dialog deletes itself later)
    m_openQuestion = Question::None;
    m_questionDialog = nullptr;
    notifyStateChanged();
}

void PDFAppToolGUISignature::openTimestampErrorDialog(const PDFDocumentToolResult& result)
{
    using Helper = PDFAppToolGUISignatureHelper;

    if (m_timestampErrorDialog)
    {
        return;
    }

    // The status of PDFTimestampClient::Result of the run (PDFDocumentToolSignature::DATA_TIMESTAMP_ERROR,
    // DATA_HTTP_STATUS); the text of the core follows 434:8518 / 435:8117, the rows say the authority
    // (its host without user info, R6-28) and the problem
    const QString status = result.data.value(QLatin1String(PDFDocumentToolSignature::DATA_TIMESTAMP_ERROR)).toString();
    const int httpStatus = result.data.value(QLatin1String(PDFDocumentToolSignature::DATA_HTTP_STATUS)).toInt();
    const QString url = m_authorityPart ? m_authorityPart->getUrl() : QString();
    const QString displayUrl = PDFTimestampClient::getDisplayUrl(QUrl(url));
    const QString authority = Helper::getAuthorityHost(url);
    const QString nothingWritten = getMode() == Mode::Sign ? tr("The document was not signed and nothing was written.") : tr("Nothing was written.");

    // 434:8518 "cannot be contacted" (no answer), 435:8117 "The timestamp was not added" (an answer,
    // which gives no timestamp); plan 2.6 for the other problems
    const bool isContactProblem = status == QLatin1String("timeout") || status == QLatin1String("no-network") || status == QLatin1String("no-transport");
    QString title = isContactProblem ? tr("The timestamp authority cannot be contacted") : (getMode() == Mode::Sign ? tr("The document was not signed") : tr("The timestamp was not added"));
    QString problem;
    QString fallbackText;
    if (status == QLatin1String("timeout"))
    {
        const int seconds = PDFTimestampClient::TIMEOUT_MILLISECONDS / 1000;
        problem = tr("Connection timed out");
        fallbackText = tr("%1 did not answer within %Ln seconds.", nullptr, seconds).arg(displayUrl);
    }
    else if (status == QLatin1String("no-network") || status == QLatin1String("no-transport"))
    {
        problem = tr("No network connection");
        fallbackText = tr("%1 cannot be reached.").arg(displayUrl);
    }
    else if (status == QLatin1String("http-error"))
    {
        problem = httpStatus == 503 ? tr("HTTP %1 · service unavailable").arg(httpStatus) : tr("HTTP %1").arg(httpStatus);
        fallbackText = tr("Timestamp authority “%1” answered with the http status code %2.").arg(displayUrl).arg(httpStatus);
    }
    else if (status == QLatin1String("rejected"))
    {
        problem = tr("The authority refused the request");
        fallbackText = tr("The timestamp authority refused the request.");
    }
    else if (status == QLatin1String("invalid-url"))
    {
        problem = tr("The address is not valid");
        fallbackText = tr("Address of the timestamp authority is not valid.");
    }
    else
    {
        problem = tr("The answer is not valid");
        fallbackText = tr("The answer of the timestamp authority is not valid.");
    }
    const QString text = result.message.isEmpty() ? fallbackText + QLatin1Char(' ') + nothingWritten : result.message;

    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Critical, title, text, QDialogButtonBox::NoButton, getDialogParent());
    dialog->setObjectName(QStringLiteral("signatureTimestampErrorDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setCardWidth(TIMESTAMP_ERROR_WIDTH);
    // facts 434:8518 / 435:8117: the rows on the filled note (as the facts of the result dialog,
    // padding 4 / 12)
    QFrame* facts = new QFrame(dialog);
    facts->setObjectName(QStringLiteral("signatureTimestampErrorFacts"));
    PDFWidgetStyleProperties::set(facts, PDFWidgetStyleProperties::NOTE);
    QVBoxLayout* factsLayout = new QVBoxLayout(facts);
    factsLayout->setContentsMargins(Helper::FACTS_PADDING_X, Helper::FACTS_PADDING_Y, Helper::FACTS_PADDING_X, Helper::FACTS_PADDING_Y);
    factsLayout->setSpacing(0);
    PDFKeyValueList* rows = new PDFKeyValueList(facts);
    rows->setObjectName(QStringLiteral("signatureTimestampErrorRows"));
    rows->addRow(tr("Authority"), authority, PDFComponentPainter::TextKind::Data);
    rows->addRow(tr("Problem"), problem);
    factsLayout->addWidget(rows);
    dialog->addContent(facts);

    // 434:8518: Close, Change Authority | Sign Without Timestamp, Try Again (Primary); 435:8117 without
    // Sign Without Timestamp
    QPushButton* closeButton = dialog->addActionButton(tr("Close"), PDFMessageBox::ActionPosition::Leading, false);
    QPushButton* changeButton = dialog->addActionButton(tr("Change Authority"), PDFMessageBox::ActionPosition::Leading, false);
    changeButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("server")));
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(changeButton, &QPushButton::clicked, this, &PDFAppToolGUISignature::onChangeAuthorityClicked);
    if (getMode() == Mode::Sign)
    {
        QPushButton* withoutButton = dialog->addActionButton(tr("Sign Without Timestamp"), PDFMessageBox::ActionPosition::Trailing, false);
        connect(withoutButton, &QPushButton::clicked, this, &PDFAppToolGUISignature::onSignWithoutTimestampClicked);
    }
    QPushButton* tryButton = dialog->addActionButton(tr("Try Again"), PDFMessageBox::ActionPosition::Trailing, true);
    tryButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("refresh-cw")));
    connect(tryButton, &QPushButton::clicked, this, &PDFAppToolGUISignature::onTryAgainClicked);
    connect(dialog, &QDialog::finished, this, &PDFAppToolGUISignature::onTimestampErrorFinished);
    m_timestampErrorDialog = dialog;
    dialog->open();
}

void PDFAppToolGUISignature::onTimestampErrorFinished()
{
    // R6-11: Close, Esc, Change Authority forget the password (Try Again and Sign Without Timestamp
    // set m_isRetrying before the dialog closes)
    m_timestampErrorDialog = nullptr;
    m_isRunPending = false;
    if (!m_isRetrying)
    {
        forgetRetryPassword();
    }
    updateParts();
    notifyStateChanged();
}

void PDFAppToolGUISignature::onTryAgainClicked()
{
    m_isRetrying = !m_retryPassword.isEmpty() || getMode() == Mode::Timestamp;
    if (m_timestampErrorDialog)
    {
        disconnect(m_timestampErrorDialog.data(), nullptr, this, nullptr);
        m_timestampErrorDialog->close();
        m_timestampErrorDialog = nullptr;
    }
    m_isRunPending = false;

    // The run starts with the kept password (or the password is forgotten)
    confirmRetry();
}

void PDFAppToolGUISignature::onSignWithoutTimestampClicked()
{
    using Tool = PDFDocumentToolSignature;

    m_isRetrying = !m_retryPassword.isEmpty();
    if (m_timestampErrorDialog)
    {
        disconnect(m_timestampErrorDialog.data(), nullptr, this, nullptr);
        m_timestampErrorDialog->close();
        m_timestampErrorDialog = nullptr;
    }
    m_isRunPending = false;

    // The type without the timestamp (one step of the model), then the run
    QPointer<PDFAppToolGUISignature> guard(this);
    if (getModel())
    {
        getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_TYPE), QString::fromLatin1(Tool::TYPE_SIGNATURE));
        if (!guard)
        {
            return;
        }
    }
    confirmRetry();
}

void PDFAppToolGUISignature::onChangeAuthorityClicked()
{
    // R6-11: the password is forgotten, the field of the address takes the focus
    m_isRetrying = false;
    if (m_timestampErrorDialog)
    {
        m_timestampErrorDialog->close();
    }
    if (m_authorityPart && m_authorityPart->getCombo())
    {
        m_authorityPart->getCombo()->setFocus(Qt::OtherFocusReason);
    }
}

// ------------------------------------------------------------------- password

void PDFAppToolGUISignature::onPasswordChanged()
{
    // 433:3972 style: an error of an unlock stays until the password changes; the live check starts
    // 500 ms after the last change (6.3), a running check is outdated
    m_unlockError.clear();
    m_passwordState = PasswordState::NotChecked;
    m_passwordGeneration++;
    if (m_passwordTimer)
    {
        m_passwordTimer->start();
    }
    updateParts();
    notifyStateChanged();
}

void PDFAppToolGUISignature::onPasswordTimer()
{
    startPasswordCheck();
}

bool PDFAppToolGUISignature::checkPassword(QByteArray pkcs12, QByteArray password)
{
    // The job of the live check (6.3): copies of values only; the key is dropped at once by the core
    const bool isValid = PDFSignatureFactory::isPasswordValid(pkcs12, password);
    PDFSecureMemory::clear(password);
    PDFSignatureHandler::releaseThreadResources();
    return isValid;
}

void PDFAppToolGUISignature::startPasswordCheck()
{
    const PDFAppToolCertificateItem* item = m_certificateList ? m_certificateList->getChosenItem() : nullptr;
    if (!item || item->source != PDFAppToolCertificateItem::Source::PfxFile || !m_passwordCard || m_passwordCard->isEmpty())
    {
        return;
    }
    const std::optional<PDFCertificateEntry> entry = m_provider.findEntry(item->filePath);
    if (!entry || entry->pkcs12.isEmpty())
    {
        return;
    }

    // One job at a time: a running one finishes, its result is dropped by the generation and the next
    // check starts after it
    if (m_passwordWatcher && m_passwordWatcher->isRunning())
    {
        return;
    }
    if (!m_passwordWatcher)
    {
        m_passwordWatcher = new QFutureWatcher<bool>(this);
        connect(m_passwordWatcher.data(), &QFutureWatcher<bool>::finished, this, &PDFAppToolGUISignature::onPasswordCheckFinished);
    }
    QString password = m_passwordCard->getPassword();
    QByteArray passwordData = password.toUtf8();
    PDFSecureMemory::clear(password);
    m_passwordWatcher->setProperty("generation", m_passwordGeneration);
    m_passwordWatcher->setFuture(QtConcurrent::run(&PDFAppToolGUISignature::checkPassword, entry->pkcs12, passwordData));
    PDFSecureMemory::clear(passwordData);
}

void PDFAppToolGUISignature::onPasswordCheckFinished()
{
    if (!m_passwordWatcher)
    {
        return;
    }
    const bool isValid = m_passwordWatcher->result();
    if (m_passwordWatcher->property("generation").toInt() != m_passwordGeneration)
    {
        // A newer password: its check starts now
        startPasswordCheck();
        return;
    }
    m_passwordState = isValid ? PasswordState::Valid : PasswordState::Invalid;
    updateParts();
    notifyStateChanged();
}

// ------------------------------------------------------------------- slots

void PDFAppToolGUISignature::onCertificateChosen()
{
    // The choice of the user outlives its list (reloadCertificates); 6.1: a password typed for another
    // file is not kept
    m_chosenCertificatePath = m_certificateList ? m_certificateList->getChosenFilePath() : QString();
    clearSecrets();
    updateParts();
    notifyStateChanged();
}

void PDFAppToolGUISignature::onUrlChanged()
{
    using Tool = PDFDocumentToolSignature;

    // The address is the value of tsaUrl (not secret); an invalid one is no value of the model
    if (m_authorityPart && getModel() && m_authorityPart->isUrlValid())
    {
        // The model emits its change (the shell and its slots may delete this GUI)
        QPointer<PDFAppToolGUISignature> guard(this);
        getModel()->setValue(QString::fromLatin1(Tool::PARAMETER_TSA_URL), m_authorityPart->getUrl());
        if (!guard)
        {
            return;
        }
    }
    updateParts();
    notifyStateChanged();
}

void PDFAppToolGUISignature::onDrawingChanged()
{
    m_answeredQuestion = Question::None;
    updateParts();
    notifyStateChanged();
}

void PDFAppToolGUISignature::onDrawingSelectionChanged()
{
    updateParts();
}

void PDFAppToolGUISignature::onClearRequested()
{
    // A drawing of a signature is small; Undo brings it back (the clear of Sign by Hand asks)
    if (m_drawingPart)
    {
        m_drawingPart->clearAll();
    }
}

void PDFAppToolGUISignature::onAppearanceRowClicked()
{
    using Tool = PDFDocumentToolSignature;
    using Helper = PDFAppToolGUISignatureHelper;

    QObject* row = sender();
    if (!row || !getModel())
    {
        return;
    }
    if (m_appearancePanel)
    {
        m_appearancePanel->close();
    }

    QVariantMap values;
    const QVariant field = row->property(Helper::FIELD_PROPERTY);
    if (field.isValid())
    {
        // Q6.3: the field takes the signature, the drawing fits its /Rect
        values.insert(QString::fromLatin1(Tool::PARAMETER_FIELD), field.toString());
        values.insert(QString::fromLatin1(Tool::PARAMETER_APPEARANCE), QString::fromLatin1(Tool::APPEARANCE_VISIBLE));
    }
    else
    {
        values.insert(QString::fromLatin1(Tool::PARAMETER_FIELD), QString());
        values.insert(QString::fromLatin1(Tool::PARAMETER_APPEARANCE), row->property(Helper::APPEARANCE_PROPERTY).toString());
    }

    // One step (the last call: the model emits, the GUI follows it)
    getModel()->setValues(values);
}

void PDFAppToolGUISignature::onCreateClicked()
{
    if (m_createDialog && m_createDialog->isVisible())
    {
        m_createDialog->raise();
        return;
    }

    // new + QPointer: the dialog deletes itself, when it closes (the dialog of step 5)
    PDFAppCertificateCreateDialog* dialog = new PDFAppCertificateCreateDialog(getContext(), getDialogParent());
    m_createDialog = dialog;
    connect(dialog, &PDFAppCertificateCreateDialog::certificateCreated, this, &PDFAppToolGUISignature::onCertificateCreated);
    dialog->open();
}

void PDFAppToolGUISignature::onImportClicked()
{
    if (m_importDialog && m_importDialog->isVisible())
    {
        m_importDialog->raise();
        return;
    }

    // Plan 7.2: the file only from the file dialog; it runs an event loop - the GUI may be gone
    QPointer<PDFAppToolGUISignature> guard(this);
    const QString filePath = PDFAppPanelFileDialogs::getOpenFileName(getDialogParent(), tr("Import Certificate"), QString(), PDFAppCertificateImportDialog::getFileFilter());
    if (!guard || filePath.isEmpty())
    {
        return;
    }

    PDFAppCertificateImportDialog* dialog = new PDFAppCertificateImportDialog(getContext(), filePath, getDialogParent());
    m_importDialog = dialog;
    connect(dialog, &PDFAppCertificateImportDialog::certificateImported, this, &PDFAppToolGUISignature::onCertificateImported);

    // The last call: the import may report its file at once
    dialog->start();
}

void PDFAppToolGUISignature::onCertificateCreated(const QString& fileName)
{
    // 434:6889: the new certificate comes back chosen
    reloadCertificates(fileName);
    onCertificateChosen();
}

void PDFAppToolGUISignature::onCertificateImported(const QString& fileName)
{
    reloadCertificates(fileName);
    onCertificateChosen();
}

void PDFAppToolGUISignature::onVerificationChanged()
{
    updateSignatureCards();
    updateParts();
}

}   // namespace pdf

#include "moc_pdfapptoolgui_signature.cpp"
