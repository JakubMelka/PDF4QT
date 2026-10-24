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

#include "pdfcms.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfform.h"
#include "pdfprogress.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgetformmanager.h"
#include "pdfwidgettool.h"

#include <QtTest>
#include <QAction>
#include <QBuffer>
#include <QLayout>
#include <QPainter>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <set>
#include <vector>

using namespace pdf;

/// Tests of the form fields, which are filled by the user in the widget - the document
/// is changed by the mouse and the result is checked in the document and in the drawn page.
/// Fields with a text are checked by the drawn page only.
class FormFieldsTest : public QObject
{
    Q_OBJECT

private slots:
    void checkBoxWithAppearance_data();
    void checkBoxWithAppearance();
    void checkBoxWithoutCheckedAppearance_data();
    void checkBoxWithoutCheckedAppearance();
    void checkBoxCheckedStateFromValue_data();
    void checkBoxCheckedStateFromValue();
    void checkBoxWithoutAppearanceState();
    void checkBoxCheckedStateIsKept_data();
    void checkBoxCheckedStateIsKept();
    void checkBoxUncheckedAppearanceIsKept_data();
    void checkBoxUncheckedAppearanceIsKept();
    void radioButtons();
    void savedAndReopened();
    void textWithoutAppearance_data();
    void textWithoutAppearance();
    void choiceDefaultAppearance();
    void inheritedDefaultAppearance_data();
    void inheritedDefaultAppearance();
    void damagedFieldTree_data();
    void damagedFieldTree();
    void pageDecorationDefault();
    void readOnlyFormFlag();
    void pageTabOrder_data();
    void pageTabOrder();
};

/// Creates the documents with the form fields
class FormFieldsTestHelper
{
public:
    FormFieldsTestHelper() = delete;

    /// Appearance streams of the check box widget
    enum class Appearance
    {
        Complete,       ///< Normal appearance of both states
        CheckedOnly,    ///< Normal appearance of the checked state, unchecked state has only a down appearance
        None,           ///< No appearance dictionary
        UncheckedOnly,  ///< Normal appearance of the unchecked state only
        UncheckedDown,  ///< Normal appearance of the unchecked state only, both states have a down appearance
        UncheckedGreen, ///< Normal appearance of the unchecked state only, it is a green rectangle
        SingleStream    ///< Normal appearance is a single stream instead of the dictionary of the states
    };

    static constexpr QSizeF PAGE_SIZE = QSizeF(200, 200);
    static constexpr QRectF CHECK_BOX = QRectF(90, 90, 20, 20);
    static constexpr QRectF RADIO_BUTTON_A = QRectF(40, 90, 20, 20);
    static constexpr QRectF RADIO_BUTTON_B = QRectF(140, 90, 20, 20);
    static constexpr QRectF TEXT_FIELD = QRectF(40, 80, 120, 40);

    /// Field with a text, which is drawn from the value of the field
    enum class TextField
    {
        TextBox,
        ComboBox,
        ListBox
    };

    /// Appearance stream of the document itself is a green rectangle (checked), or it is empty (unchecked)
    static PDFObjectReference createAppearanceStream(PDFDocumentBuilder& builder, QRectF rectangle, bool checked)
    {
        PDFContentStreamBuilder content(PAGE_SIZE, PDFContentStreamBuilder::CoordinateSystem::PDF);
        QPainter* painter = content.begin();
        if (checked)
        {
            painter->fillRect(rectangle, Qt::green);
        }
        const PDFContentStreamBuilder::ContentStream stream = content.end(painter);
        const std::vector<PDFObject> objects = builder.copyFrom({ stream.resources, stream.contents }, stream.document.getStorage(), true);

        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << WrapName("XObject");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Subtype");
        factory << WrapName("Form");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("BBox");
        factory << rectangle;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Resources");
        factory << objects[0];
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(objects[1].getReference(), factory.takeObject());
        return objects[1].getReference();
    }

    static void addName(PDFObjectFactory& factory, const char* key, const QByteArray& name)
    {
        if (!name.isEmpty())
        {
            factory.beginDictionaryItem(key);
            factory << WrapName(name);
            factory.endDictionaryItem();
        }
    }

    static void addStates(PDFObjectFactory& factory, const char* key, const std::vector<std::pair<QByteArray, PDFObjectReference>>& states)
    {
        factory.beginDictionaryItem(key);
        factory.beginDictionary();
        for (const auto& state : states)
        {
            factory.beginDictionaryItem(state.first);
            factory << state.second;
            factory.endDictionaryItem();
        }
        factory.endDictionary();
        factory.endDictionaryItem();
    }

    static void beginWidget(PDFObjectFactory& factory, PDFObjectReference page, QRectF rectangle)
    {
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << WrapName("Annot");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Subtype");
        factory << WrapName("Widget");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Rect");
        factory << rectangle;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("P");
        factory << page;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("F");
        factory << 4;
        factory.endDictionaryItem();
    }

    static void beginField(PDFObjectFactory& factory, const QString& name, PDFInteger flags)
    {
        factory.beginDictionaryItem("FT");
        factory << WrapName("Btn");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("T");
        factory << name;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Ff");
        factory << flags;
        factory.endDictionaryItem();
    }

    static void addToPage(PDFDocumentBuilder& builder, PDFObjectReference page, const std::vector<PDFObjectReference>& widgets)
    {
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Annots");
        factory << widgets;
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.appendTo(page, factory.takeObject());
    }

    /// Creates a document with a single check box
    /// \param appearance Appearance streams of the widget
    /// \param onState Name of the checked state in the appearance dictionary
    /// \param value Value of the field (/V), nothing if empty
    /// \param defaultValue Default value of the field (/DV), nothing if empty
    /// \param appearanceState Appearance state of the widget (/AS), nothing if empty
    static PDFDocument createCheckBoxDocument(Appearance appearance,
                                              const QByteArray& onState,
                                              const QByteArray& value,
                                              const QByteArray& defaultValue,
                                              const QByteArray& appearanceState)
    {
        PDFDocumentBuilder builder;
        const PDFObjectReference page = builder.appendPage(QRectF(QPointF(0, 0), PAGE_SIZE));

        PDFObjectFactory factory;
        beginWidget(factory, page, CHECK_BOX);
        beginField(factory, "Check", 0);
        addName(factory, "V", value);
        addName(factory, "DV", defaultValue);
        addName(factory, "AS", appearanceState);

        if (appearance != Appearance::None)
        {
            const PDFObjectReference checked = createAppearanceStream(builder, CHECK_BOX, true);
            const PDFObjectReference unchecked = createAppearanceStream(builder, CHECK_BOX, false);

            factory.beginDictionaryItem("AP");
            factory.beginDictionary();
            switch (appearance)
            {
                case Appearance::Complete:
                    addStates(factory, "N", { { "Off", unchecked }, { onState, checked } });
                    break;

                case Appearance::CheckedOnly:
                    addStates(factory, "N", { { onState, checked } });
                    addStates(factory, "D", { { "Off", unchecked }, { onState, checked } });
                    break;

                case Appearance::UncheckedOnly:
                    addStates(factory, "N", { { "Off", unchecked } });
                    break;

                case Appearance::UncheckedDown:
                    addStates(factory, "N", { { "Off", unchecked } });
                    addStates(factory, "D", { { "Off", unchecked }, { onState, checked } });
                    break;

                case Appearance::UncheckedGreen:
                    addStates(factory, "N", { { "Off", checked } });
                    break;

                case Appearance::SingleStream:
                    factory.beginDictionaryItem("N");
                    factory << unchecked;
                    factory.endDictionaryItem();
                    break;

                default:
                    Q_ASSERT(false);
                    break;
            }
            factory.endDictionary();
            factory.endDictionaryItem();
        }

        factory.endDictionary();
        const PDFObjectReference widget = builder.addObject(factory.takeObject());
        addToPage(builder, page, { widget });
        builder.createAcroForm({ widget });
        return builder.build();
    }

    /// Creates a document with a group of two radio buttons with the states A and B,
    /// which can't be all turned off, the button A is selected.
    static PDFDocument createRadioButtonDocument()
    {
        PDFDocumentBuilder builder;
        const PDFObjectReference page = builder.appendPage(QRectF(QPointF(0, 0), PAGE_SIZE));

        std::vector<PDFObjectReference> widgets;
        for (const auto& [state, rectangle] : { std::pair<QByteArray, QRectF>{ "A", RADIO_BUTTON_A }, std::pair<QByteArray, QRectF>{ "B", RADIO_BUTTON_B } })
        {
            PDFObjectFactory factory;
            beginWidget(factory, page, rectangle);
            addName(factory, "AS", state == "A" ? state : QByteArray("Off"));
            factory.beginDictionaryItem("AP");
            factory.beginDictionary();
            addStates(factory, "N", { { "Off", createAppearanceStream(builder, rectangle, false) }, { state, createAppearanceStream(builder, rectangle, true) } });
            factory.endDictionary();
            factory.endDictionaryItem();
            factory.endDictionary();
            widgets.push_back(builder.addObject(factory.takeObject()));
        }

        PDFObjectFactory factory;
        factory.beginDictionary();
        beginField(factory, "Radio", PDFFormField::Radio | PDFFormField::NoToggleToOff);
        addName(factory, "V", "A");
        factory.beginDictionaryItem("Kids");
        factory << widgets;
        factory.endDictionaryItem();
        factory.endDictionary();
        const PDFObjectReference field = builder.addObject(factory.takeObject());

        for (const PDFObjectReference& widget : widgets)
        {
            factory.beginDictionary();
            factory.beginDictionaryItem("Parent");
            factory << field;
            factory.endDictionaryItem();
            factory.endDictionary();
            builder.mergeTo(widget, factory.takeObject());
        }

        addToPage(builder, page, widgets);
        builder.createAcroForm({ field });
        return builder.build();
    }

    /// Creates a document with a single field with the text 'Hello'. The field
    /// has its own default appearance, which differs from the one of the form.
    /// Choice fields have the options 'Hello' and 'World', the second option
    /// is selected in the list box, so the first one is drawn as a plain text.
    /// \param textField Type of the field
    /// \param content Content of the appearance stream of the widget
    /// \param needAppearances Viewer is asked to construct the appearance streams
    /// \param defaultAppearance Default appearance of the field itself, nothing if empty
    /// \param parentCount Number of the parent fields above the field. The type of the field
    ///        and the default appearance '0 g /Helv 20 Tf' are defined only by the root field.
    static PDFDocument createTextFieldDocument(TextField textField,
                                               const QByteArray& content,
                                               bool needAppearances,
                                               const QByteArray& defaultAppearance = "0 g /Helv 20 Tf",
                                               int parentCount = 0)
    {
        PDFDocumentBuilder builder;
        const PDFObjectReference page = builder.appendPage(QRectF(QPointF(0, 0), PAGE_SIZE));

        PDFDictionaryBuilder font;
        font.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Font"));
        font.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Type1"));
        font.addEntry(PDFInplaceOrMemoryString("BaseFont"), PDFObject::createName("Helvetica"));
        PDFDictionaryBuilder fonts;
        fonts.addEntry(PDFInplaceOrMemoryString("Helv"), PDFObject::createReference(builder.addObject(PDFObject::createDictionary(std::move(font)))));
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::move(fonts)));
        const PDFObject resourcesObject = PDFObject::createDictionary(std::move(resources));

        PDFObjectFactory streamFactory;
        streamFactory.beginDictionary();
        streamFactory.beginDictionaryItem("Type");
        streamFactory << WrapName("XObject");
        streamFactory.endDictionaryItem();
        streamFactory.beginDictionaryItem("Subtype");
        streamFactory << WrapName("Form");
        streamFactory.endDictionaryItem();
        streamFactory.beginDictionaryItem("BBox");
        streamFactory << QRectF(QPointF(0, 0), TEXT_FIELD.size());
        streamFactory.endDictionaryItem();
        streamFactory.beginDictionaryItem("Resources");
        streamFactory << resourcesObject;
        streamFactory.endDictionaryItem();
        streamFactory.beginDictionaryItem("Length");
        streamFactory << PDFInteger(content.size());
        streamFactory.endDictionaryItem();
        streamFactory.endDictionary();
        const PDFObject streamDictionary = streamFactory.takeObject();
        const PDFObjectReference stream = builder.addObject(PDFObject::createStream(PDFStream(PDFDictionaryBuilder(*streamDictionary.getDictionary()), QByteArray(content))));

        PDFObjectFactory factory;
        beginWidget(factory, page, TEXT_FIELD);
        if (parentCount == 0)
        {
            factory.beginDictionaryItem("FT");
            factory << WrapName(textField == TextField::TextBox ? "Tx" : "Ch");
            factory.endDictionaryItem();
        }
        factory.beginDictionaryItem("T");
        factory << QString("Text");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Ff");
        factory << PDFInteger(textField == TextField::ComboBox ? PDFFormField::Combo : PDFFormField::None);
        factory.endDictionaryItem();
        factory.beginDictionaryItem("V");
        factory << QString(textField == TextField::ListBox ? "World" : "Hello");
        factory.endDictionaryItem();
        if (!defaultAppearance.isEmpty())
        {
            factory.beginDictionaryItem("DA");
            factory << WrapString(defaultAppearance);
            factory.endDictionaryItem();
        }
        if (textField != TextField::TextBox)
        {
            factory.beginDictionaryItem("Opt");
            factory.beginArray();
            factory << QString("Hello");
            factory << QString("World");
            factory.endArray();
            factory.endDictionaryItem();
        }
        factory.beginDictionaryItem("AP");
        factory.beginDictionary();
        factory.beginDictionaryItem("N");
        factory << stream;
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.endDictionary();
        const PDFObjectReference widget = builder.addObject(factory.takeObject());
        addToPage(builder, page, { widget });

        PDFObjectReference field = widget;
        for (int i = 0; i < parentCount; ++i)
        {
            factory.beginDictionary();
            factory.beginDictionaryItem("T");
            factory << QString("Parent%1").arg(i);
            factory.endDictionaryItem();
            factory.beginDictionaryItem("Kids");
            factory << std::vector<PDFObjectReference>{ field };
            factory.endDictionaryItem();
            if (i + 1 == parentCount)
            {
                factory.beginDictionaryItem("FT");
                factory << WrapName(textField == TextField::TextBox ? "Tx" : "Ch");
                factory.endDictionaryItem();
                factory.beginDictionaryItem("DA");
                factory << WrapString("0 g /Helv 20 Tf");
                factory.endDictionaryItem();
            }
            factory.endDictionary();
            const PDFObjectReference parent = builder.addObject(factory.takeObject());

            factory.beginDictionary();
            factory.beginDictionaryItem("Parent");
            factory << parent;
            factory.endDictionaryItem();
            factory.endDictionary();
            builder.mergeTo(field, factory.takeObject());
            field = parent;
        }

        factory.beginDictionary();
        factory.beginDictionaryItem("NeedAppearances");
        factory << needAppearances;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("DR");
        factory << resourcesObject;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("DA");
        factory << WrapString("0 g /Helv 6 Tf");
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(builder.createAcroForm({ field }), factory.takeObject());
        return builder.build();
    }

    /// Saves the document to the memory and reads it again
    /// \param document Document
    /// \param[out] ok Document has been written and read
    static PDFDocument reopenDocument(const PDFDocument* document, bool& ok)
    {
        QByteArray data;
        QBuffer buffer(&data);
        ok = buffer.open(QIODevice::WriteOnly);

        PDFDocumentWriter writer(nullptr);
        ok = ok && bool(writer.write(&buffer, document));
        buffer.close();

        PDFDocumentReader reader(nullptr, nullptr, false, false);
        PDFDocument result = reader.readFromBuffer(data);
        ok = ok && reader.getReadingResult() == PDFDocumentReader::Result::OK;
        return result;
    }
};

/// The widget, the annotation manager, the form manager and the tools are connected
/// the same way, as the application connects them - a modified document is set to all
/// components. Mouse events are sent to the widget, so they are dispatched to the form
/// manager by the same way, as in the application.
struct FormFixture
{
    PDFDocumentPointer document;
    PDFCMSManager cms{ nullptr };
    PDFProgress progress{ nullptr };
    PDFWidget widget{ &cms, RendererEngine::QPainter, nullptr };
    PDFWidgetAnnotationManager annotations{ widget.getDrawWidgetProxy(), nullptr };
    PDFWidgetFormManager forms{ widget.getDrawWidgetProxy(), nullptr };
    std::array<QAction, 10> actions;
    PDFToolManager tools{ widget.getDrawWidgetProxy(),
        { &actions[0], &actions[1], &actions[2], &actions[3], &actions[4], &actions[5],
          &actions[6], &actions[7], &actions[8], &actions[9] }, nullptr, &widget };
    int modificationCount = 0;

    explicit FormFixture(PDFDocument initialDocument) :
        document(new PDFDocument(std::move(initialDocument)))
    {
        widget.resize(800, 800);
        widget.getDrawWidget()->getWidget()->resize(800, 800);
        widget.setAnnotationManager(&annotations);
        widget.setFormManager(&forms);
        widget.setToolManager(&tools);
        widget.getDrawWidgetProxy()->setProgress(&progress);
        forms.setAnnotationManager(&annotations);
        annotations.setFormManager(&forms);

        // Form fields are not highlighted, so the colors of the drawn page are not affected
        forms.setAppearanceFlags(PDFFormManager::FormAppearanceFlags());
        apply(PDFModifiedDocument(document, nullptr));

        QObject::connect(&forms, &PDFFormManager::documentModified, &forms, [this](PDFModifiedDocument modified)
        {
            ++modificationCount;
            PDFDocumentPointer oldDocument = document;
            document = modified;
            apply(PDFModifiedDocument(document, nullptr, modified.getFlags()));
        });
    }

    ~FormFixture()
    {
        tools.setDocument(PDFModifiedDocument());
        forms.setDocument(PDFModifiedDocument());
        annotations.setDocument(PDFModifiedDocument());
        widget.setDocument(PDFModifiedDocument(), {});
        widget.setFormManager(nullptr);
        widget.setAnnotationManager(nullptr);
        widget.setToolManager(nullptr);
    }

    void apply(PDFModifiedDocument modified)
    {
        widget.setDocument(modified, {});
        annotations.setDocument(modified);
        forms.setDocument(modified);
        tools.setDocument(modified);
    }

    /// Returns widget annotation of the first page
    PDFObjectReference widgetReference(size_t index = 0) const
    {
        return document->getCatalog()->getPage(0)->getAnnotations().at(index);
    }

    const PDFDictionary* dictionary(PDFObjectReference reference) const
    {
        return document->getDictionaryFromObject(document->getObjectByReference(reference));
    }

    /// Returns true, if dictionary has a name with the given value. Empty name is
    /// distinguished from the missing one - empty value means that there is no entry.
    bool hasName(PDFObjectReference reference, const char* key, const QByteArray& name) const
    {
        const PDFObject& object = document->getObject(dictionary(reference)->get(key));
        return name.isEmpty() ? object.isNull() : (object.isName() && object.getString() == name);
    }

    PDFObject appearance(PDFObjectReference reference) const
    {
        return dictionary(reference)->get("AP");
    }

    PDFObject downAppearance(PDFObjectReference reference) const
    {
        const PDFDictionary* appearanceDictionary = document->getDictionaryFromObject(appearance(reference));
        return appearanceDictionary ? appearanceDictionary->get("D") : PDFObject();
    }

    /// Returns the states, which have a stream in the normal appearance
    /// dictionary. Single stream is returned as the state with empty name.
    QByteArrayList normalStates(PDFObjectReference reference) const
    {
        QByteArrayList states;
        if (const PDFDictionary* appearanceDictionary = document->getDictionaryFromObject(appearance(reference)))
        {
            const PDFObject& normal = document->getObject(appearanceDictionary->get("N"));
            if (normal.isStream())
            {
                states << QByteArray();
            }
            else if (normal.isDictionary())
            {
                for (size_t i = 0; i < normal.getDictionary()->getCount(); ++i)
                {
                    if (document->getObject(normal.getDictionary()->getValue(i)).isStream())
                    {
                        states << normal.getDictionary()->getKey(i).getString();
                    }
                }
            }
        }

        std::sort(states.begin(), states.end());
        return states;
    }

    /// Returns the appearance stream of the state from the normal appearance dictionary
    PDFObject normalAppearance(PDFObjectReference reference, const QByteArray& state) const
    {
        const PDFDictionary* appearanceDictionary = document->getDictionaryFromObject(appearance(reference));
        const PDFDictionary* normal = appearanceDictionary ? document->getDictionaryFromObject(appearanceDictionary->get("N")) : nullptr;
        return normal ? normal->get(state) : PDFObject();
    }

    QRectF rectangle(PDFObjectReference reference) const
    {
        PDFDocumentDataLoaderDecorator loader(document.data());
        return loader.readRectangle(dictionary(reference)->get("Rect"), QRectF());
    }

    QPoint device(const QPointF& pagePoint) const
    {
        const PDFWidgetSnapshot snapshot = widget.getDrawWidgetProxy()->getSnapshot();
        return snapshot.items.empty() ? QPoint() : snapshot.items.front().pageToDeviceMatrix.map(pagePoint).toPoint();
    }

    /// Clicks by the left mouse button to the page point
    void click(const QPointF& pagePoint)
    {
        QWidget* target = widget.getDrawWidget()->getWidget();
        const QPoint position = device(pagePoint);

        QMouseEvent press(QEvent::MouseButtonPress, position, target->mapToGlobal(position), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, position, target->mapToGlobal(position), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &release);
    }

    /// Returns color of the drawn widget at the page point
    QColor color(const QPointF& pagePoint)
    {
        const QImage image = widget.getDrawWidget()->getWidget()->grab().toImage();
        return image.pixelColor(device(pagePoint) * image.devicePixelRatio());
    }

    /// Check box drawn by the library has a cross, which goes through its center,
    /// so the center is darker, than the point between the arms of the cross.
    bool isMarked(const QRectF& checkBox)
    {
        const QPointF center = checkBox.center();
        const QPointF reference = center + QPointF(checkBox.width() * 0.3, 0.0);
        return color(center).lightness() + 48 < color(reference).lightness();
    }

    /// Returns true, if something dark (a text) is drawn in the page rectangle
    bool hasDarkPixels(const QRectF& pageRectangle)
    {
        return darkPixelsHeight(pageRectangle) > 0;
    }

    /// Returns height of the dark area (a text) drawn in the page
    /// rectangle in the pixels, zero if nothing dark is drawn there.
    int darkPixelsHeight(const QRectF& pageRectangle)
    {
        const QImage image = widget.getDrawWidget()->getWidget()->grab().toImage();
        const PDFWidgetSnapshot snapshot = widget.getDrawWidgetProxy()->getSnapshot();
        if (snapshot.items.empty())
        {
            return 0;
        }

        const QRectF deviceRectangle = snapshot.items.front().pageToDeviceMatrix.mapRect(pageRectangle);
        const qreal ratio = image.devicePixelRatio();
        const QRect rectangle = QRectF(deviceRectangle.topLeft() * ratio, deviceRectangle.size() * ratio).toRect().intersected(image.rect());

        int count = 0;
        int top = rectangle.bottom() + 1;
        int bottom = rectangle.top() - 1;
        for (int y = rectangle.top(); y <= rectangle.bottom(); ++y)
        {
            for (int x = rectangle.left(); x <= rectangle.right(); ++x)
            {
                if (image.pixelColor(x, y).lightness() < 96)
                {
                    ++count;
                    top = qMin(top, y);
                    bottom = qMax(bottom, y);
                }
            }
        }

        return count >= 16 ? bottom - top + 1 : 0;
    }

    /// Checked appearance stream of the tested documents is a green rectangle
    bool isGreen(const QPointF& pagePoint)
    {
        const QColor pixel = color(pagePoint);
        return pixel.green() > 160 && pixel.red() < 128 && pixel.blue() < 128;
    }
};

Q_DECLARE_METATYPE(FormFieldsTestHelper::Appearance)
Q_DECLARE_METATYPE(FormFieldsTestHelper::TextField)

void FormFieldsTest::checkBoxWithAppearance_data()
{
    QTest::addColumn<FormFieldsTestHelper::Appearance>("appearance");
    QTest::addColumn<QByteArray>("onState");

    QTest::newRow("both states") << FormFieldsTestHelper::Appearance::Complete << QByteArray("Yes");
    QTest::newRow("both states, custom name") << FormFieldsTestHelper::Appearance::Complete << QByteArray("1");
    QTest::newRow("checked state only") << FormFieldsTestHelper::Appearance::CheckedOnly << QByteArray("On");
}

void FormFieldsTest::checkBoxWithAppearance()
{
    QFETCH(FormFieldsTestHelper::Appearance, appearance);
    QFETCH(QByteArray, onState);

    // Check box with its own appearance of the checked state only switches the appearance
    // state, appearance streams of the document are not replaced by the library.
    FormFixture fixture(FormFieldsTestHelper::createCheckBoxDocument(appearance, onState, "Off", QByteArray(), "Off"));
    const PDFObjectReference widget = fixture.widgetReference();
    const QPointF center = FormFieldsTestHelper::CHECK_BOX.center();
    const PDFObject originalAppearance = fixture.appearance(widget);
    QVERIFY(!fixture.isGreen(center));

    fixture.click(center);
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.hasName(widget, "V", onState));
    QVERIFY(fixture.hasName(widget, "AS", onState));
    QCOMPARE(fixture.appearance(widget), originalAppearance);
    QCOMPARE(fixture.rectangle(widget), FormFieldsTestHelper::CHECK_BOX);
    QVERIFY(fixture.isGreen(center));

    fixture.click(center);
    QCOMPARE(fixture.modificationCount, 2);
    QVERIFY(fixture.hasName(widget, "V", "Off"));
    QVERIFY(fixture.hasName(widget, "AS", "Off"));
    QCOMPARE(fixture.appearance(widget), originalAppearance);
    QVERIFY(!fixture.isGreen(center));
}

void FormFieldsTest::checkBoxWithoutCheckedAppearance_data()
{
    QTest::addColumn<FormFieldsTestHelper::Appearance>("appearance");

    QTest::newRow("no appearance") << FormFieldsTestHelper::Appearance::None;
    QTest::newRow("unchecked state only") << FormFieldsTestHelper::Appearance::UncheckedOnly;
    QTest::newRow("unchecked state only, down appearance") << FormFieldsTestHelper::Appearance::UncheckedDown;
    QTest::newRow("single stream") << FormFieldsTestHelper::Appearance::SingleStream;
}

void FormFieldsTest::checkBoxWithoutCheckedAppearance()
{
    QFETCH(FormFieldsTestHelper::Appearance, appearance);

    // Check box has no appearance stream of the checked state, so there is no name
    // of this state in the document. The state 'Yes' is used (not an empty name),
    // and appearance streams are created, so the check box is displayed as checked.
    FormFixture fixture(FormFieldsTestHelper::createCheckBoxDocument(appearance, "Yes", "Off", QByteArray(), "Off"));
    const PDFObjectReference widget = fixture.widgetReference();
    const QPointF center = FormFieldsTestHelper::CHECK_BOX.center();
    const PDFObject downAppearance = fixture.downAppearance(widget);
    QVERIFY(!fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));

    // Only the normal appearance is created, other appearances of the document are kept
    fixture.click(center);
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.hasName(widget, "V", "Yes"));
    QVERIFY(fixture.hasName(widget, "AS", "Yes"));
    QCOMPARE(fixture.normalStates(widget), QByteArrayList({ "Off", "Yes" }));
    QCOMPARE(fixture.downAppearance(widget), downAppearance);
    QCOMPARE(fixture.rectangle(widget), FormFieldsTestHelper::CHECK_BOX);
    QVERIFY(fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));

    // Appearance streams are created only once
    const PDFObject createdAppearance = fixture.appearance(widget);

    fixture.click(center);
    QCOMPARE(fixture.modificationCount, 2);
    QVERIFY(fixture.hasName(widget, "V", "Off"));
    QVERIFY(fixture.hasName(widget, "AS", "Off"));
    QCOMPARE(fixture.appearance(widget), createdAppearance);
    QVERIFY(!fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));

    fixture.click(center);
    QVERIFY(fixture.hasName(widget, "V", "Yes"));
    QVERIFY(fixture.hasName(widget, "AS", "Yes"));
    QCOMPARE(fixture.appearance(widget), createdAppearance);
    QVERIFY(fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));
}

void FormFieldsTest::checkBoxCheckedStateFromValue_data()
{
    QTest::addColumn<QByteArray>("value");
    QTest::addColumn<QByteArray>("defaultValue");
    QTest::addColumn<QByteArray>("expectedValue");

    QTest::newRow("checked") << QByteArray("On") << QByteArray() << QByteArray("Off");
    QTest::newRow("checked by default") << QByteArray("Off") << QByteArray("On") << QByteArray("On");
    QTest::newRow("unchecked by default") << QByteArray("Off") << QByteArray("Off") << QByteArray("Yes");
}

void FormFieldsTest::checkBoxCheckedStateFromValue()
{
    QFETCH(QByteArray, value);
    QFETCH(QByteArray, defaultValue);
    QFETCH(QByteArray, expectedValue);

    // Check box without the appearance streams is checked by its own value, if it has
    // some - checked check box is unchecked by a single click.
    FormFixture fixture(FormFieldsTestHelper::createCheckBoxDocument(FormFieldsTestHelper::Appearance::None, "Yes", value, defaultValue, value));
    const PDFObjectReference widget = fixture.widgetReference();
    const QPointF center = FormFieldsTestHelper::CHECK_BOX.center();
    QCOMPARE(fixture.isMarked(FormFieldsTestHelper::CHECK_BOX), value != "Off");

    fixture.click(center);
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.hasName(widget, "V", expectedValue));
    QVERIFY(fixture.hasName(widget, "AS", expectedValue));
    QCOMPARE(fixture.isMarked(FormFieldsTestHelper::CHECK_BOX), expectedValue != "Off");
}

void FormFieldsTest::checkBoxWithoutAppearanceState()
{
    // Check box has neither the value, nor the appearance state - it is not checked
    FormFixture fixture(FormFieldsTestHelper::createCheckBoxDocument(FormFieldsTestHelper::Appearance::None, "Yes", QByteArray(), QByteArray(), QByteArray()));
    const PDFObjectReference widget = fixture.widgetReference();
    const QPointF center = FormFieldsTestHelper::CHECK_BOX.center();
    QVERIFY(fixture.hasName(widget, "AS", QByteArray()));
    QVERIFY(!fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));

    fixture.click(center);
    QVERIFY(fixture.hasName(widget, "V", "Yes"));
    QVERIFY(fixture.hasName(widget, "AS", "Yes"));
    QVERIFY(fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));

    fixture.click(center);
    QVERIFY(fixture.hasName(widget, "V", "Off"));
    QVERIFY(fixture.hasName(widget, "AS", "Off"));
    QVERIFY(!fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));
}

void FormFieldsTest::checkBoxCheckedStateIsKept_data()
{
    QTest::addColumn<FormFieldsTestHelper::Appearance>("appearance");
    QTest::addColumn<bool>("reopen");

    QTest::newRow("no appearance") << FormFieldsTestHelper::Appearance::None << false;
    QTest::newRow("no appearance, reopened") << FormFieldsTestHelper::Appearance::None << true;
    QTest::newRow("unchecked state only") << FormFieldsTestHelper::Appearance::UncheckedOnly << false;
    QTest::newRow("unchecked state only, reopened") << FormFieldsTestHelper::Appearance::UncheckedOnly << true;
    QTest::newRow("unchecked state only, down appearance") << FormFieldsTestHelper::Appearance::UncheckedDown << false;
    QTest::newRow("single stream") << FormFieldsTestHelper::Appearance::SingleStream << false;
    QTest::newRow("single stream, reopened") << FormFieldsTestHelper::Appearance::SingleStream << true;
}

void FormFieldsTest::checkBoxCheckedStateIsKept()
{
    QFETCH(FormFieldsTestHelper::Appearance, appearance);
    QFETCH(bool, reopen);

    // Checked check box has no appearance stream of the checked state and no default value,
    // so the name of its checked state is only in its value. The name must not be lost, when
    // the check box is unchecked - it is checked by the same value again, also if the document
    // is saved and opened between the clicks.
    std::unique_ptr<FormFixture> fixture = std::make_unique<FormFixture>(FormFieldsTestHelper::createCheckBoxDocument(appearance, "On", "On", QByteArray(), "On"));
    const QPointF center = FormFieldsTestHelper::CHECK_BOX.center();

    auto reopenDocument = [&fixture, reopen]()
    {
        bool ok = true;
        if (reopen)
        {
            PDFDocument document = FormFieldsTestHelper::reopenDocument(fixture->document.data(), ok);
            fixture.reset();
            fixture = std::make_unique<FormFixture>(std::move(document));
        }
        return ok;
    };

    for (int i = 0; i < 2; ++i)
    {
        fixture->click(center);
        QVERIFY(fixture->hasName(fixture->widgetReference(), "V", "Off"));
        QVERIFY(fixture->hasName(fixture->widgetReference(), "AS", "Off"));
        QVERIFY(!fixture->isMarked(FormFieldsTestHelper::CHECK_BOX));
        QVERIFY(reopenDocument());

        fixture->click(center);
        QVERIFY(fixture->hasName(fixture->widgetReference(), "V", "On"));
        QVERIFY(fixture->hasName(fixture->widgetReference(), "AS", "On"));
        QCOMPARE(fixture->normalStates(fixture->widgetReference()), QByteArrayList({ "Off", "On" }));
        QVERIFY(fixture->isMarked(FormFieldsTestHelper::CHECK_BOX));
        QVERIFY(reopenDocument());
    }
}

void FormFieldsTest::checkBoxUncheckedAppearanceIsKept_data()
{
    QTest::addColumn<FormFieldsTestHelper::Appearance>("appearance");
    QTest::addColumn<QByteArray>("value");

    QTest::newRow("empty") << FormFieldsTestHelper::Appearance::UncheckedOnly << QByteArray("Off");
    QTest::newRow("empty, down appearance") << FormFieldsTestHelper::Appearance::UncheckedDown << QByteArray("Off");
    QTest::newRow("green") << FormFieldsTestHelper::Appearance::UncheckedGreen << QByteArray("Off");
    QTest::newRow("green, checked") << FormFieldsTestHelper::Appearance::UncheckedGreen << QByteArray("On");
}

void FormFieldsTest::checkBoxUncheckedAppearanceIsKept()
{
    QFETCH(FormFieldsTestHelper::Appearance, appearance);
    QFETCH(QByteArray, value);

    // Check box has its own appearance stream of the unchecked state, only the appearance
    // stream of the checked state is missing. The missing stream is created, but the stream
    // of the document is neither replaced, nor drawn differently. Name of the checked
    // state is the value of the checked check box, otherwise it is the name 'Yes'.
    const QByteArray onState = value != "Off" ? value : QByteArray("Yes");
    FormFixture fixture(FormFieldsTestHelper::createCheckBoxDocument(appearance, onState, value, QByteArray(), value));
    const PDFObjectReference widget = fixture.widgetReference();
    const QPointF center = FormFieldsTestHelper::CHECK_BOX.center();
    const QPointF beside = center + QPointF(FormFieldsTestHelper::CHECK_BOX.width() * 0.3, 0.0);
    const bool isGreen = appearance == FormFieldsTestHelper::Appearance::UncheckedGreen;
    const PDFObject uncheckedAppearance = fixture.normalAppearance(widget, "Off");
    const PDFObject downAppearance = fixture.downAppearance(widget);
    QVERIFY(uncheckedAppearance.isReference());

    bool isChecked = value != "Off";
    for (int i = 0; i < 4; ++i)
    {
        fixture.click(center);
        isChecked = !isChecked;

        QVERIFY(fixture.hasName(widget, "V", isChecked ? onState : QByteArray("Off")));
        QVERIFY(fixture.hasName(widget, "AS", isChecked ? onState : QByteArray("Off")));
        QCOMPARE(fixture.normalStates(widget), QByteArrayList({ "Off", onState }));
        QCOMPARE(fixture.normalAppearance(widget, "Off"), uncheckedAppearance);
        QCOMPARE(fixture.downAppearance(widget), downAppearance);
        QCOMPARE(fixture.rectangle(widget), FormFieldsTestHelper::CHECK_BOX);

        if (isChecked)
        {
            // Checked state is drawn by the created appearance stream
            QVERIFY(fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));
            QVERIFY(!fixture.isGreen(beside));
        }
        else
        {
            // Unchecked state is drawn by the appearance stream of the document,
            // so there is no border of the check box created by the library.
            QCOMPARE(fixture.isGreen(center), isGreen);
            QCOMPARE(fixture.isGreen(beside), isGreen);
            QVERIFY(!fixture.hasDarkPixels(FormFieldsTestHelper::CHECK_BOX));
        }
    }
}

void FormFieldsTest::radioButtons()
{
    // Radio buttons are distinguished by the names of their appearance states
    FormFixture fixture(FormFieldsTestHelper::createRadioButtonDocument());
    const PDFObjectReference buttonA = fixture.widgetReference(0);
    const PDFObjectReference buttonB = fixture.widgetReference(1);
    const QPointF centerA = FormFieldsTestHelper::RADIO_BUTTON_A.center();
    const QPointF centerB = FormFieldsTestHelper::RADIO_BUTTON_B.center();
    const PDFObject appearanceA = fixture.appearance(buttonA);
    const PDFObject appearanceB = fixture.appearance(buttonB);

    PDFDocumentDataLoaderDecorator loader(fixture.document.data());
    const PDFObjectReference field = loader.readReferenceFromDictionary(fixture.dictionary(buttonA), "Parent");
    QVERIFY(field.isValid());
    QVERIFY(fixture.isGreen(centerA));
    QVERIFY(!fixture.isGreen(centerB));

    fixture.click(centerB);
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.hasName(field, "V", "B"));
    QVERIFY(fixture.hasName(buttonA, "AS", "Off"));
    QVERIFY(fixture.hasName(buttonB, "AS", "B"));
    QVERIFY(!fixture.isGreen(centerA));
    QVERIFY(fixture.isGreen(centerB));

    // Selected radio button can't be turned off
    fixture.click(centerB);
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.hasName(field, "V", "B"));
    QVERIFY(fixture.hasName(buttonB, "AS", "B"));

    fixture.click(centerA);
    QCOMPARE(fixture.modificationCount, 2);
    QVERIFY(fixture.hasName(field, "V", "A"));
    QVERIFY(fixture.hasName(buttonA, "AS", "A"));
    QVERIFY(fixture.hasName(buttonB, "AS", "Off"));
    QCOMPARE(fixture.appearance(buttonA), appearanceA);
    QCOMPARE(fixture.appearance(buttonB), appearanceB);
}

void FormFieldsTest::savedAndReopened()
{
    QByteArray data;
    {
        FormFixture fixture(FormFieldsTestHelper::createCheckBoxDocument(FormFieldsTestHelper::Appearance::SingleStream, "Yes", "Off", QByteArray(), "Off"));
        fixture.click(FormFieldsTestHelper::CHECK_BOX.center());

        QBuffer buffer(&data);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        PDFDocumentWriter writer(nullptr);
        QVERIFY(writer.write(&buffer, fixture.document.data()));
    }

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(data);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    // Checked check box is stored in the document with its appearance, so it is
    // checked also in the applications, which do not create the missing appearance.
    FormFixture fixture(std::move(document));
    const PDFObjectReference widget = fixture.widgetReference();
    const QPointF center = FormFieldsTestHelper::CHECK_BOX.center();
    QVERIFY(fixture.hasName(widget, "V", "Yes"));
    QVERIFY(fixture.hasName(widget, "AS", "Yes"));
    QCOMPARE(fixture.normalStates(widget), QByteArrayList({ "Off", "Yes" }));
    QVERIFY(fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));

    fixture.click(center);
    QVERIFY(fixture.hasName(widget, "V", "Off"));
    QVERIFY(fixture.hasName(widget, "AS", "Off"));
    QVERIFY(!fixture.isMarked(FormFieldsTestHelper::CHECK_BOX));
}

void FormFieldsTest::textWithoutAppearance_data()
{
    QTest::addColumn<FormFieldsTestHelper::TextField>("textField");
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<bool>("needAppearances");
    QTest::addColumn<bool>("isTextDrawn");

    // Placeholders of the appearance streams, as they are written by the producers of the forms
    const QByteArray placeholder = "/Tx BMC\nEMC\n";
    const QByteArray emptyText = "/Tx BMC\nq\nBT\n0 g\n/Helv 20 Tf\nET\nQ\nEMC\n";
    const QByteArray background = "0 1 0 rg\n0 0 120 40 re f*\n/Tx BMC\nEMC\n";

    // Appearance stream with a text object - operators with the empty operands draw
    // no text, a text of the other ones is not visible (it is a space)
    auto text = [](const QByteArray& operators)
    {
        return QByteArray("/Tx BMC\nq\nBT\n0 g\n/Helv 20 Tf\n2 12 Td\n") + operators + QByteArray("\nET\nQ\nEMC\n");
    };
    const QByteArray ownText = text("( ) Tj");

    QTest::newRow("text box, placeholder") << FormFieldsTestHelper::TextField::TextBox << placeholder << true << true;
    QTest::newRow("text box, empty text object") << FormFieldsTestHelper::TextField::TextBox << emptyText << true << true;
    QTest::newRow("text box, background") << FormFieldsTestHelper::TextField::TextBox << background << true << true;
    QTest::newRow("combo box, placeholder") << FormFieldsTestHelper::TextField::ComboBox << placeholder << true << true;
    QTest::newRow("combo box, background") << FormFieldsTestHelper::TextField::ComboBox << background << true << true;
    QTest::newRow("list box, placeholder") << FormFieldsTestHelper::TextField::ListBox << placeholder << true << true;
    QTest::newRow("text box, own text") << FormFieldsTestHelper::TextField::TextBox << ownText << true << false;
    QTest::newRow("combo box, own text") << FormFieldsTestHelper::TextField::ComboBox << ownText << true << false;
    QTest::newRow("text box, empty Tj") << FormFieldsTestHelper::TextField::TextBox << text("() Tj") << true << true;
    QTest::newRow("text box, empty hexadecimal Tj") << FormFieldsTestHelper::TextField::TextBox << text("<> Tj") << true << true;
    QTest::newRow("text box, empty TJ") << FormFieldsTestHelper::TextField::TextBox << text("[] TJ") << true << true;
    QTest::newRow("text box, TJ with empty strings") << FormFieldsTestHelper::TextField::TextBox << text("[() -250 <> 120] TJ") << true << true;
    QTest::newRow("text box, empty quote") << FormFieldsTestHelper::TextField::TextBox << text("() '") << true << true;
    QTest::newRow("text box, empty double quote") << FormFieldsTestHelper::TextField::TextBox << text("0 0 () \"") << true << true;
    QTest::newRow("text box, more empty operators") << FormFieldsTestHelper::TextField::TextBox << text("() Tj\n[] TJ\n() '") << true << true;
    QTest::newRow("combo box, empty Tj") << FormFieldsTestHelper::TextField::ComboBox << text("() Tj") << true << true;
    QTest::newRow("list box, empty TJ") << FormFieldsTestHelper::TextField::ListBox << text("[] TJ") << true << true;
    QTest::newRow("text box, own text by TJ") << FormFieldsTestHelper::TextField::TextBox << text("[() -250 ( )] TJ") << true << false;
    QTest::newRow("text box, own text by quote") << FormFieldsTestHelper::TextField::TextBox << text("( ) '") << true << false;
    QTest::newRow("text box, own text by double quote") << FormFieldsTestHelper::TextField::TextBox << text("0 0 ( ) \"") << true << false;
    QTest::newRow("text box, own text after empty Tj") << FormFieldsTestHelper::TextField::TextBox << text("() Tj\n( ) Tj") << true << false;
    QTest::newRow("text box, own text before empty Tj") << FormFieldsTestHelper::TextField::TextBox << text("( ) Tj\n() Tj") << true << false;
    QTest::newRow("text box, appearance is valid") << FormFieldsTestHelper::TextField::TextBox << placeholder << false << false;
    QTest::newRow("text box, empty Tj, appearance is valid") << FormFieldsTestHelper::TextField::TextBox << text("() Tj") << false << false;
    QTest::newRow("list box, appearance is valid") << FormFieldsTestHelper::TextField::ListBox << placeholder << false << false;
}

void FormFieldsTest::textWithoutAppearance()
{
    QFETCH(FormFieldsTestHelper::TextField, textField);
    QFETCH(QByteArray, content);
    QFETCH(bool, needAppearances);
    QFETCH(bool, isTextDrawn);

    // Field has a value, but its appearance stream does not draw it. If the appearance streams
    // have to be constructed, the text is drawn from the value, the rest of the appearance
    // stream is kept and the document is not changed by the drawing.
    FormFixture fixture(FormFieldsTestHelper::createTextFieldDocument(textField, content, needAppearances));
    const PDFObjectReference widget = fixture.widgetReference();
    const PDFObject appearance = fixture.appearance(widget);
    const QPointF corner = FormFieldsTestHelper::TEXT_FIELD.topLeft() + QPointF(115, 35);

    QCOMPARE(fixture.forms.getForm()->isAppearanceUpdateNeeded(), needAppearances);
    QCOMPARE(fixture.hasDarkPixels(FormFieldsTestHelper::TEXT_FIELD), isTextDrawn);
    QCOMPARE(fixture.isGreen(corner), content.contains(" rg"));
    QCOMPARE(fixture.modificationCount, 0);
    QCOMPARE(fixture.appearance(widget), appearance);
}

void FormFieldsTest::choiceDefaultAppearance()
{
    // Default appearance of the field (font of the size 20) has a precedence over the default
    // appearance of the form (font of the size 6), so the text of the choice field is as high,
    // as the same text of the text box.
    const QByteArray placeholder = "/Tx BMC\nEMC\n";

    FormFixture textBox(FormFieldsTestHelper::createTextFieldDocument(FormFieldsTestHelper::TextField::TextBox, placeholder, true));
    const int height = textBox.darkPixelsHeight(FormFieldsTestHelper::TEXT_FIELD);
    QVERIFY(height > 0);

    for (const FormFieldsTestHelper::TextField textField : { FormFieldsTestHelper::TextField::ComboBox, FormFieldsTestHelper::TextField::ListBox })
    {
        FormFixture fixture(FormFieldsTestHelper::createTextFieldDocument(textField, placeholder, true));
        const PDFFormFieldChoice* field = dynamic_cast<const PDFFormFieldChoice*>(fixture.forms.getFormFieldForWidget(fixture.widgetReference()));
        QVERIFY(field);
        QCOMPARE(field->getDefaultAppearance(), QByteArray("0 g /Helv 20 Tf"));
        QCOMPARE(fixture.forms.getForm()->getDefaultAppearance().value_or(QByteArray()), QByteArray("0 g /Helv 6 Tf"));

        // Text of the font of the size 6 would have less than one third of the height
        const int choiceHeight = fixture.darkPixelsHeight(FormFieldsTestHelper::TEXT_FIELD);
        QVERIFY2(choiceHeight >= height * 3 / 4, qPrintable(QString("Height of the text is %1, height of the text box is %2.").arg(choiceHeight).arg(height)));
    }
}

void FormFieldsTest::inheritedDefaultAppearance_data()
{
    QTest::addColumn<FormFieldsTestHelper::TextField>("textField");
    QTest::addColumn<int>("parentCount");
    QTest::addColumn<QByteArray>("defaultAppearance");

    const QByteArray ownAppearance = "0 g /Helv 12 Tf";

    QTest::newRow("text box, parent") << FormFieldsTestHelper::TextField::TextBox << 1 << QByteArray();
    QTest::newRow("text box, two parents") << FormFieldsTestHelper::TextField::TextBox << 2 << QByteArray();
    QTest::newRow("text box, parent, own appearance") << FormFieldsTestHelper::TextField::TextBox << 1 << ownAppearance;
    QTest::newRow("text box, two parents, own appearance") << FormFieldsTestHelper::TextField::TextBox << 2 << ownAppearance;
    QTest::newRow("combo box, parent") << FormFieldsTestHelper::TextField::ComboBox << 1 << QByteArray();
    QTest::newRow("combo box, two parents") << FormFieldsTestHelper::TextField::ComboBox << 2 << QByteArray();
    QTest::newRow("combo box, three parents") << FormFieldsTestHelper::TextField::ComboBox << 3 << QByteArray();
    QTest::newRow("combo box, parent, own appearance") << FormFieldsTestHelper::TextField::ComboBox << 1 << ownAppearance;
    QTest::newRow("combo box, two parents, own appearance") << FormFieldsTestHelper::TextField::ComboBox << 2 << ownAppearance;
    QTest::newRow("list box, parent") << FormFieldsTestHelper::TextField::ListBox << 1 << QByteArray();
    QTest::newRow("list box, two parents") << FormFieldsTestHelper::TextField::ListBox << 2 << QByteArray();
    QTest::newRow("list box, parent, own appearance") << FormFieldsTestHelper::TextField::ListBox << 1 << ownAppearance;
}

void FormFieldsTest::inheritedDefaultAppearance()
{
    QFETCH(FormFieldsTestHelper::TextField, textField);
    QFETCH(int, parentCount);
    QFETCH(QByteArray, defaultAppearance);

    // Default appearance (font of the size 20) and the type of the field are defined by the root
    // field, the form has a default appearance with the font of the size 6. The field inherits
    // the default appearance of the root field, if it has no default appearance of its own.
    const QByteArray placeholder = "/Tx BMC\nEMC\n";
    const QByteArray inheritedAppearance = "0 g /Helv 20 Tf";
    const QByteArray expectedAppearance = defaultAppearance.isEmpty() ? inheritedAppearance : defaultAppearance;

    FormFixture reference(FormFieldsTestHelper::createTextFieldDocument(FormFieldsTestHelper::TextField::TextBox, placeholder, true));
    const int referenceHeight = reference.darkPixelsHeight(FormFieldsTestHelper::TEXT_FIELD);
    QVERIFY(referenceHeight > 0);

    FormFixture fixture(FormFieldsTestHelper::createTextFieldDocument(textField, placeholder, true, defaultAppearance, parentCount));
    const PDFFormField* field = fixture.forms.getFormFieldForWidget(fixture.widgetReference());
    QVERIFY(field);
    QCOMPARE(fixture.forms.getForm()->getDefaultAppearance().value_or(QByteArray()), QByteArray("0 g /Helv 6 Tf"));

    int level = 0;
    for (const PDFFormField* parentField = field->getParentField(); parentField; parentField = parentField->getParentField())
    {
        ++level;
    }
    QCOMPARE(level, parentCount);

    if (textField == FormFieldsTestHelper::TextField::TextBox)
    {
        const PDFFormFieldText* textFormField = dynamic_cast<const PDFFormFieldText*>(field);
        QVERIFY(textFormField);
        QCOMPARE(textFormField->getDefaultAppearance(), expectedAppearance);
    }
    else
    {
        const PDFFormFieldChoice* choiceFormField = dynamic_cast<const PDFFormFieldChoice*>(field);
        QVERIFY(choiceFormField);
        QCOMPARE(choiceFormField->isComboBox(), textField == FormFieldsTestHelper::TextField::ComboBox);
        QCOMPARE(choiceFormField->getDefaultAppearance(), expectedAppearance);
    }

    // Text of the font of the size 20 is as high, as the text of the text box with the same font,
    // text of the font of the size 12 is lower, but it is higher, than the text of the size 6.
    const int height = fixture.darkPixelsHeight(FormFieldsTestHelper::TEXT_FIELD);
    const QString message = QString("Height of the text is %1, height of the text of the size 20 is %2.").arg(height).arg(referenceHeight);
    if (defaultAppearance.isEmpty())
    {
        QVERIFY2(height * 4 >= referenceHeight * 3, qPrintable(message));
    }
    else
    {
        QVERIFY2(height * 4 < referenceHeight * 3 && height * 20 > referenceHeight * 9, qPrintable(message));
    }
}

void FormFieldsTest::damagedFieldTree_data()
{
    // Hand-written documents with a damaged tree of the form fields: objects 1 - 3
    // are the catalog, the page tree and the page, the fields start with the object 4.
    // Each row gives the dictionaries of the fields, the annotations of the page and
    // the expected number of the parsed fields and of the levels of the tree.
    QTest::addColumn<QByteArrayList>("fields");
    QTest::addColumn<QByteArray>("annotations");
    QTest::addColumn<int>("fieldCount");
    QTest::addColumn<int>("levelCount");
    QTest::addColumn<QString>("widgetFieldName");

    // The root (damaged with a parent) and the child reference each other, the child
    // references itself and the leaf is referenced twice
    QTest::newRow("cycle") << QByteArrayList({
        "<< /FT /Tx /T (root) /Parent 5 0 R /Kids [5 0 R] >>",
        "<< /T (child) /Parent 4 0 R /Kids [4 0 R 6 0 R 5 0 R 6 0 R] >>",
        "<< /T (leaf) /Parent 5 0 R /Subtype /Widget /Rect [10 10 100 30] /P 3 0 R >>" })
        << QByteArray("[6 0 R]") << 3 << 3 << QStringLiteral("root.child.leaf");

    // The child references itself
    QTest::newRow("self reference") << QByteArrayList({
        "<< /FT /Tx /T (root) /Kids [5 0 R] >>",
        "<< /T (child) /Parent 4 0 R /Kids [5 0 R] >>" })
        << QByteArray("[5 0 R]") << 2 << 2 << QString();

    // Each field references the next one twice: 2^40 fields without the check of the used fields
    const int sharedCount = 40;
    QByteArrayList shared;
    for (int i = 0; i < sharedCount; ++i)
    {
        const QByteArray parent = i > 0 ? " /Parent " + QByteArray::number(i + 3) + " 0 R" : QByteArray(" /FT /Tx");
        if (i + 1 < sharedCount)
        {
            const QByteArray next = QByteArray::number(i + 5) + " 0 R";
            shared << "<< /T (f" + QByteArray::number(i) + ")" + parent + " /Kids [" + next + " " + next + "] >>";
        }
        else
        {
            shared << "<< /T (leaf)" + parent + " /Subtype /Widget /Rect [10 10 100 30] /P 3 0 R >>";
        }
    }
    QStringList sharedNames;
    for (int i = 0; i + 1 < sharedCount; ++i)
    {
        sharedNames << QString("f%1").arg(i);
    }
    sharedNames << QStringLiteral("leaf");
    QTest::newRow("shared kids") << shared << ("[" + QByteArray::number(sharedCount + 3) + " 0 R]") << sharedCount << sharedCount << sharedNames.join('.');

    // A chain of 5000 fields would overflow the stack, only 129 levels are parsed
    const int chainCount = 5000;
    QByteArrayList chain;
    for (int i = 0; i < chainCount; ++i)
    {
        const QByteArray parent = i > 0 ? " /Parent " + QByteArray::number(i + 3) + " 0 R" : QByteArray(" /FT /Tx");
        const QByteArray kids = i + 1 < chainCount ? " /Kids [" + QByteArray::number(i + 5) + " 0 R]" : QByteArray();
        chain << "<< /T (f" + QByteArray::number(i) + ")" + parent + kids + " >>";
    }
    QTest::newRow("deep chain") << chain << QByteArray("[]") << PDFFormField::MAX_FIELD_TREE_DEPTH + 1 << PDFFormField::MAX_FIELD_TREE_DEPTH + 1 << QString();
}

void FormFieldsTest::damagedFieldTree()
{
    QFETCH(QByteArrayList, fields);
    QFETCH(QByteArray, annotations);
    QFETCH(int, fieldCount);
    QFETCH(int, levelCount);
    QFETCH(QString, widgetFieldName);

    QByteArrayList objects({
        "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [4 0 R] >> >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Annots " + annotations + " >>" });
    objects.append(fields);

    QByteArray pdf("%PDF-1.7\n");
    std::vector<qint64> offsets;
    for (qsizetype i = 0; i < objects.size(); ++i)
    {
        offsets.push_back(pdf.size());
        pdf += QByteArray::number(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const qint64 xref = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (qint64 offset : offsets)
    {
        pdf += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    }
    pdf += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(pdf);
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    const PDFForm form = PDFForm::parse(&document, document.getCatalog()->getFormObject());
    QCOMPARE(form.getFormFields().size(), size_t(1));

    // Count of the fields and of the levels of the tree, every field is parsed once
    int count = 0;
    int levels = 0;
    std::vector<std::pair<const PDFFormField*, int>> stack({ { form.getFormFields().front().get(), 1 } });
    std::set<PDFObjectReference> references;
    while (!stack.empty())
    {
        const auto [field, level] = stack.back();
        stack.pop_back();
        QVERIFY(field);
        QVERIFY(references.insert(field->getSelfReference()).second);
        ++count;
        levels = std::max(levels, level);
        for (const PDFFormFieldPointer& child : field->getChildFields())
        {
            QCOMPARE(child->getParentField(), field);
            stack.emplace_back(child.get(), level + 1);
        }
    }
    QCOMPARE(count, fieldCount);
    QCOMPARE(levels, levelCount);

    if (!widgetFieldName.isEmpty())
    {
        const PDFObjectReference widget(objects.size(), 0);
        const PDFFormField* field = form.getFormFieldForWidget(widget);
        QVERIFY(field);
        QCOMPARE(field->getName(PDFFormField::FullyQualified), widgetFieldName);
        QCOMPARE(field->getWidgets().size(), size_t(1));
    }

    // The document is shown in the widget with the form manager, as in the applications
    FormFixture fixture(std::move(document));
    QVERIFY(fixture.forms.hasAcroForm());
}

void FormFieldsTest::pageDecorationDefault()
{
    // The decoration of the pages (margin and shadow, PDFPageDecoration) is off by default,
    // so the old applications draw the pages as before: a decoration set and removed again
    // gives the same pixels as the view, which never had any
    FormFixture fixture(FormFieldsTestHelper::createCheckBoxDocument(FormFieldsTestHelper::Appearance::SingleStream, "Yes", "Off", QByteArray(), "Off"));
    PDFDrawWidgetProxy* proxy = fixture.widget.getDrawWidgetProxy();
    QWidget* canvas = fixture.widget.getDrawWidget()->getWidget();
    QCOMPARE(proxy->getPageDecoration(), PDFPageDecoration());
    QVERIFY(!proxy->getPageDecoration().hasShadow());

    // The fixture sets the sizes without showing the widget - it is shown here, so the
    // layout of the scrollbars gives the draw widget its size
    fixture.widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fixture.widget));
    auto settle = [&fixture]()
    {
        for (int i = 0; i < 3; ++i)
        {
            QCoreApplication::sendPostedEvents();
            if (QLayout* layout = fixture.widget.layout())
            {
                layout->activate();
            }
        }
    };

    proxy->performOperation(PDFDrawWidgetProxy::ZoomFitWidth);
    settle();
    const QImage original = canvas->grab().toImage();
    const PDFWidgetSnapshot originalSnapshot = proxy->getSnapshot();
    QCOMPARE(originalSnapshot.items.size(), size_t(1));
    const QRect originalRect = originalSnapshot.items.front().rect.toRect();
    QVERIFY(originalRect.left() <= 1);
    QCOMPARE(originalRect.top(), 0);

    // Margin and shadow: the fit width keeps the margin, the canvas next to the page is darker
    PDFPageDecoration decoration;
    decoration.margin = 24;
    decoration.shadowColor = QColor(0, 0, 0, 128);
    decoration.shadowOffset = QPointF(0.0, 2.0);
    decoration.shadowBlur = 10.0;
    proxy->setPageDecoration(decoration);
    settle();
    QCOMPARE(proxy->getPageDecoration(), decoration);
    const QRect decoratedRect = proxy->getSnapshot().items.front().rect.toRect();
    QCOMPARE(decoratedRect.top(), 24);
    QVERIFY(decoratedRect.left() >= 24 && decoratedRect.left() <= 26);
    const QImage decorated = canvas->grab().toImage();
    QVERIFY(decorated != original);
    const int middle = qMin(decoratedRect.center().y(), canvas->height() / 2);
    const qreal ratio = decorated.devicePixelRatio();
    const QColor canvasColor = decorated.pixelColor(QPoint(2, middle) * ratio);
    QVERIFY(decorated.pixelColor(QPoint(decoratedRect.left() - 1, middle) * ratio).lightness() < canvasColor.lightness());

    // Without the decoration the view is the same as before, pixel by pixel
    proxy->setPageDecoration(PDFPageDecoration());
    settle();
    QCOMPARE(canvas->size(), original.size() / original.devicePixelRatio());
    QCOMPARE(proxy->getSnapshot().items.front().rect.toRect(), originalRect);
    QCOMPARE(canvas->grab().toImage(), original);

    // Hostile values are bounded
    PDFPageDecoration hostile;
    hostile.margin = 1000000;
    hostile.shadowColor = Qt::black;
    hostile.shadowBlur = std::numeric_limits<qreal>::quiet_NaN();
    hostile.shadowOffset = QPointF(std::numeric_limits<qreal>::infinity(), -1e9);
    proxy->setPageDecoration(hostile);
    QCOMPARE(proxy->getPageDecoration().margin, 512);
    QCOMPARE(proxy->getPageDecoration().shadowBlur, 0.0);
    QCOMPARE(proxy->getPageDecoration().shadowOffset, QPointF(0.0, -32.0));
    canvas->grab();
    hostile.margin = -5;
    hostile.shadowBlur = 1e9;
    proxy->setPageDecoration(hostile);
    QCOMPARE(proxy->getPageDecoration().margin, 0);
    QCOMPARE(proxy->getPageDecoration().shadowBlur, 32.0);
    canvas->grab();
}

void FormFieldsTest::readOnlyFormFlag()
{
    // Check box: a read only form (a locked document) refuses the click, the flag
    // overrides EditReadOnlyFields, turning it off makes the field editable again
    {
        FormFixture fixture(FormFieldsTestHelper::createCheckBoxDocument(FormFieldsTestHelper::Appearance::Complete, "Yes", "Off", QByteArray(), "Off"));
        const PDFObjectReference widget = fixture.widgetReference();
        const QPointF center = FormFieldsTestHelper::CHECK_BOX.center();

        fixture.forms.setAppearanceFlags(PDFFormManager::FormAppearanceFlags(PDFFormManager::ReadOnlyForm) | PDFFormManager::EditReadOnlyFields);
        QVERIFY(fixture.forms.isReadOnlyForm());
        const PDFFormField* formField = fixture.forms.getFormFieldForWidget(widget);
        QVERIFY(formField);
        QVERIFY(fixture.forms.isReadOnly(formField));
        QVERIFY(fixture.forms.getEffectiveFieldFlags(formField).testFlag(PDFFormField::ReadOnly));

        fixture.click(center);
        QCOMPARE(fixture.modificationCount, 0);
        QVERIFY(fixture.hasName(widget, "AS", "Off"));
        QVERIFY(!fixture.forms.isFocused(widget));

        // Tab does not move the focus into a read only form
        QVERIFY(!fixture.forms.focusNextPrevFormField(true));

        // A value set by the user is refused, an internal value (a reset by the application) is not
        PDFFormField::SetValueParameters parameters;
        parameters.formManager = &fixture.forms;
        parameters.invokingFormField = fixture.forms.getFormFieldForWidget(widget);
        parameters.scope = PDFFormField::SetValueParameters::Scope::User;
        parameters.value = PDFObject::createName("Yes");
        fixture.forms.setFormFieldValue(parameters);
        QCOMPARE(fixture.modificationCount, 0);

        fixture.forms.setAppearanceFlags(PDFFormManager::FormAppearanceFlags());
        QVERIFY(!fixture.forms.isReadOnlyForm());
        fixture.click(center);
        QCOMPARE(fixture.modificationCount, 1);
        QVERIFY(fixture.hasName(widget, "AS", "Yes"));
    }

    // Text field: no editor is focused, the keys do not change the value
    {
        FormFixture fixture(FormFieldsTestHelper::createTextFieldDocument(FormFieldsTestHelper::TextField::TextBox, "/Tx BMC EMC", false));
        const PDFObjectReference widget = fixture.widgetReference();
        const QPointF center = FormFieldsTestHelper::TEXT_FIELD.center();
        QWidget* target = fixture.widget.getDrawWidget()->getWidget();

        fixture.forms.setAppearanceFlags(PDFFormManager::FormAppearanceFlags(PDFFormManager::ReadOnlyForm));
        fixture.click(center);
        QVERIFY(!fixture.forms.isFocused(widget));

        QKeyEvent press(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
        QCoreApplication::sendEvent(target, &press);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &enter);
        QCOMPARE(fixture.modificationCount, 0);

        // Editable again - the click focuses the editor
        fixture.forms.setAppearanceFlags(PDFFormManager::FormAppearanceFlags());
        fixture.click(center);
        QVERIFY(fixture.forms.isFocused(widget));

        // The flag set while the editor has the focus removes the focus
        fixture.forms.setAppearanceFlags(PDFFormManager::FormAppearanceFlags(PDFFormManager::ReadOnlyForm));
        QVERIFY(!fixture.forms.isFocused(widget));
    }
}

/// Documents of the tab order of the pages
class FormFieldsTabOrderHelper
{
public:
    FormFieldsTabOrderHelper() = delete;

    /// Page 200 x 200 (array Annots c, a, ro, hidden, b, d): a (20, 150), b (120, 150) in
    /// the top row, ro (20, 100, read only), hidden (70, 150, flag Hidden), d (20, 20), c
    /// (120, 20) in the bottom row, every widget 40 x 20; page 2: e; f is on no page
    static PDFDocument createDocument(const QByteArray& tabs, int rotate)
    {
        PDFDocumentBuilder builder;
        const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
        const PDFObjectReference secondPage = builder.appendPage(QRectF(0, 0, 200, 200));

        auto createField = [&builder](PDFObjectReference widgetPage, const QString& name, QRectF rectangle, PDFInteger flags, PDFInteger fieldFlags)
        {
            PDFObjectFactory factory;
            factory.beginDictionary();
            factory.beginDictionaryItem("Type");
            factory << WrapName("Annot");
            factory.endDictionaryItem();
            factory.beginDictionaryItem("Subtype");
            factory << WrapName("Widget");
            factory.endDictionaryItem();
            factory.beginDictionaryItem("Rect");
            factory << rectangle;
            factory.endDictionaryItem();
            if (widgetPage.isValid())
            {
                factory.beginDictionaryItem("P");
                factory << widgetPage;
                factory.endDictionaryItem();
            }
            factory.beginDictionaryItem("F");
            factory << flags;
            factory.endDictionaryItem();
            factory.beginDictionaryItem("FT");
            factory << WrapName("Tx");
            factory.endDictionaryItem();
            factory.beginDictionaryItem("T");
            factory << name;
            factory.endDictionaryItem();
            factory.beginDictionaryItem("Ff");
            factory << fieldFlags;
            factory.endDictionaryItem();
            factory.endDictionary();
            return builder.addObject(factory.takeObject());
        };

        const PDFObjectReference a = createField(page, QStringLiteral("a"), QRectF(20, 150, 40, 20), 4, 0);
        const PDFObjectReference b = createField(page, QStringLiteral("b"), QRectF(120, 150, 40, 20), 4, 0);
        const PDFObjectReference c = createField(page, QStringLiteral("c"), QRectF(120, 20, 40, 20), 4, 0);
        const PDFObjectReference d = createField(page, QStringLiteral("d"), QRectF(20, 20, 40, 20), 4, 0);
        const PDFObjectReference readOnly = createField(page, QStringLiteral("ro"), QRectF(20, 100, 40, 20), 4, PDFFormField::ReadOnly);
        const PDFObjectReference hidden = createField(page, QStringLiteral("hidden"), QRectF(70, 150, 40, 20), 4 | 2, 0);
        const PDFObjectReference e = createField(secondPage, QStringLiteral("e"), QRectF(20, 150, 40, 20), 4, 0);
        const PDFObjectReference f = createField(PDFObjectReference(), QStringLiteral("f"), QRectF(20, 150, 40, 20), 4, 0);
        FormFieldsTestHelper::addToPage(builder, page, { c, a, readOnly, hidden, b, d });
        FormFieldsTestHelper::addToPage(builder, secondPage, { e });

        PDFObjectFactory factory;
        factory.beginDictionary();
        if (!tabs.isEmpty())
        {
            factory.beginDictionaryItem("Tabs");
            factory << WrapName(tabs);
            factory.endDictionaryItem();
        }
        factory.beginDictionaryItem("Rotate");
        factory << PDFInteger(rotate);
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(page, factory.takeObject());

        builder.createAcroForm({ f, e, d, c, b, a, readOnly, hidden });
        return builder.build();
    }
};

void FormFieldsTest::pageTabOrder_data()
{
    QTest::addColumn<QByteArray>("tabs");
    QTest::addColumn<int>("rotate");
    QTest::addColumn<QStringList>("expected");

    // The order of the array Annots (no entry, S - structure, A, W)
    QTest::newRow("annotations") << QByteArray() << 0 << QStringList({ "c", "a", "b", "d", "e", "f" });
    QTest::newRow("structure") << QByteArray("S") << 0 << QStringList({ "c", "a", "b", "d", "e", "f" });
    QTest::newRow("rows") << QByteArray("R") << 0 << QStringList({ "a", "b", "d", "c", "e", "f" });
    QTest::newRow("columns") << QByteArray("C") << 0 << QStringList({ "a", "d", "b", "c", "e", "f" });

    // The page turned clockwise: its left edge is the top, its bottom is the left side
    QTest::newRow("rows rotated") << QByteArray("R") << 90 << QStringList({ "d", "a", "c", "b", "e", "f" });
    QTest::newRow("rows upside down") << QByteArray("R") << 180 << QStringList({ "c", "d", "b", "a", "e", "f" });
    QTest::newRow("columns rotated left") << QByteArray("C") << 270 << QStringList({ "b", "a", "c", "d", "e", "f" });
    QTest::newRow("hostile tabs") << QByteArray("Xyz") << 0 << QStringList({ "c", "a", "b", "d", "e", "f" });
}

void FormFieldsTest::pageTabOrder()
{
    QFETCH(QByteArray, tabs);
    QFETCH(int, rotate);
    QFETCH(QStringList, expected);

    FormFixture fixture(FormFieldsTabOrderHelper::createDocument(tabs, rotate));
    PDFWidgetFormManager& forms = fixture.forms;
    auto name = [&forms](PDFObjectReference widget)
    {
        const PDFFormField* field = forms.getFormFieldForWidget(widget);
        return field ? field->getName(PDFFormField::NameType::FullyQualified) : QString();
    };
    auto names = [&name](const std::vector<PDFObjectReference>& widgets)
    {
        QStringList result;
        for (const PDFObjectReference& widget : widgets)
        {
            result << name(widget);
        }
        return result;
    };

    // Off (the old applications): the order of the field tree, every widget, the focus
    // leaves the fields after the last one
    QVERIFY(!forms.isPageTabOrder());
    QCOMPARE(names(forms.getTabOrder()), QStringList({ "f", "e", "d", "c", "b", "a", "ro", "hidden" }));
    for (int i = 0; i < 8; ++i)
    {
        QVERIFY(forms.focusNextPrevFormField(true));
    }
    QVERIFY(!forms.focusNextPrevFormField(true));
    QVERIFY(!forms.getFocusedWidget().isValid());

    // On: the order of the pages and of their entry Tabs, without the read only field
    // and the hidden widget; the focus cycles
    forms.setPageTabOrder(true);
    QCOMPARE(names(forms.getTabOrder()), expected);
    for (int round = 0; round < 2; ++round)
    {
        for (const QString& field : expected)
        {
            QVERIFY(forms.focusNextPrevFormField(true));
            QCOMPARE(name(forms.getFocusedWidget()), field);
        }
    }
    QVERIFY(forms.focusNextPrevFormField(true));
    QCOMPARE(name(forms.getFocusedWidget()), expected.front());
    QVERIFY(forms.focusNextPrevFormField(false));
    QCOMPARE(name(forms.getFocusedWidget()), expected.back());
    QVERIFY(forms.focusNextPrevFormField(false));
    QCOMPARE(name(forms.getFocusedWidget()), expected.at(expected.size() - 2));

    // Shift+Tab without a focused field starts at the last one
    forms.setFocusToEditor(nullptr);
    QVERIFY(forms.focusNextPrevFormField(false));
    QCOMPARE(name(forms.getFocusedWidget()), expected.back());

    // The read only field is reached, when read only fields can be edited
    forms.setFocusToEditor(nullptr);
    forms.setAppearanceFlags(PDFFormManager::EditReadOnlyFields);
    QVERIFY(names(forms.getTabOrder()).contains(QStringLiteral("ro")));
    QVERIFY(!names(forms.getTabOrder()).contains(QStringLiteral("hidden")));

    // A read only form takes no focus
    forms.setAppearanceFlags(PDFFormManager::ReadOnlyForm);
    QVERIFY(!forms.focusNextPrevFormField(true));
    QVERIFY(!forms.getFocusedWidget().isValid());
    QCOMPARE(fixture.modificationCount, 0);
}

QTEST_MAIN(FormFieldsTest)

#include "tst_formfieldstest.moc"
