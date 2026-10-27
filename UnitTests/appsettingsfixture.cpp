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

#include "appsettingsfixture.h"

#include "pdfappnavigator.h"
#include "pdfapproute.h"
#include "pdfappsettingcontrol.h"
#include "pdfappsettingrow.h"
#include "pdfappsettingspage.h"
#include "pdfapptab.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingsrepository.h"

#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"

#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QSlider>
#include <QTest>
#include <QThread>

#include <cmath>

namespace pdftest
{

/// Thread writing one setting by its own connection (another instance)
class AppSettingsWriterThread : public QThread
{
public:
    AppSettingsWriterThread(pdf::PDFDatabase* database, const pdf::PDFSettingRegistry* registry, QString key, QVariant value) :
        m_database(database),
        m_registry(registry),
        m_key(std::move(key)),
        m_value(std::move(value))
    {
    }

    bool isSuccess() const { return m_isSuccess; }

protected:
    virtual void run() override
    {
        pdf::PDFSettingsRepository repository(m_database, m_registry);
        const pdf::PDFDatabaseError error = m_value.isValid() ? repository.setValue(m_key, m_value) : repository.resetValue(m_key);
        m_isSuccess = !error.isError();
    }

private:
    pdf::PDFDatabase* m_database;
    const pdf::PDFSettingRegistry* m_registry;
    QString m_key;
    QVariant m_value;
    bool m_isSuccess = false;
};

AppSettingsFixture::AppSettingsFixture(QSize size) :
    AppDocumentFixture(size)
{

}

AppSettingsFixture::~AppSettingsFixture() = default;

void AppSettingsFixture::navigate(const QString& route)
{
    if (window && window->getNavigator())
    {
        window->getNavigator()->navigate(pdf::PDFAppRoute::parse(route), pdf::PDFAppNavigationMode::Push);
    }
    settle();
}

pdf::PDFAppSettingsPage* AppSettingsFixture::openSettings(const QString& section, const QString& anchor)
{
    if (!window || !window->getNavigator())
    {
        return nullptr;
    }

    window->getNavigator()->navigate(pdf::PDFAppRoutes::settings(section, anchor), pdf::PDFAppNavigationMode::Push);
    settle();
    return settingsPage();
}

pdf::PDFAppSettingsPage* AppSettingsFixture::openFilter(const QString& query)
{
    if (!window || !window->getNavigator())
    {
        return nullptr;
    }

    window->getNavigator()->navigate(pdf::PDFAppRoutes::settingsFilter(query), pdf::PDFAppNavigationMode::Push);
    settle();
    return settingsPage();
}

pdf::PDFAppSettingsPage* AppSettingsFixture::settingsPage() const
{
    pdf::PDFAppTab* tab = window ? window->getCurrentTab() : nullptr;
    return tab ? qobject_cast<pdf::PDFAppSettingsPage*>(tab->getPage()) : nullptr;
}

pdf::PDFAppSettingRow* AppSettingsFixture::findRow(const QString& anchor) const
{
    pdf::PDFAppSettingsPage* page = settingsPage();
    return page ? page->findRow(anchor) : nullptr;
}

bool AppSettingsFixture::setControlValue(pdf::PDFAppSettingRow* row, const QVariant& value)
{
    using Kind = pdf::PDFAppSettingControl::Kind;

    pdf::PDFAppSettingControl* control = row ? row->getControl() : nullptr;
    if (!control || !control->getEditor() || !control->getEditor()->isEnabled())
    {
        return false;
    }

    QWidget* editor = control->getEditor();
    switch (control->getKind())
    {
        case Kind::CheckBox:
        {
            QCheckBox* checkBox = control->getCheckBox();
            if (checkBox->isChecked() != value.toBool())
            {
                QTest::mouseClick(checkBox, Qt::LeftButton, Qt::NoModifier, QPoint(9, checkBox->height() / 2));
            }
            break;
        }

        case Kind::ComboBox:
        {
            pdf::PDFComboBox* comboBox = control->getComboBox();
            const int target = comboBox->findData(value);
            if (target < 0)
            {
                return false;
            }

            // The arrows of a closed combo box choose the item (activated)
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

            // The arrows only show the item of a setting, whose change is expensive
            // (the language), Enter chooses it
            if (control->getDefinition().isCommittedOnActivation() && control->isBrowsing())
            {
                QTest::keyClick(comboBox, Qt::Key_Return);
            }
            break;
        }

        case Kind::SpinBox:
        case Kind::DoubleSpinBox:
        case Kind::TextField:
        case Kind::Path:
        {
            // Select the text, type the new one and press Enter
            QString text = value.toString();
            if (control->getKind() == Kind::DoubleSpinBox)
            {
                text = control->getDoubleSpinBox()->locale().toString(value.toDouble(), 'f', control->getDoubleSpinBox()->decimals());
            }
            else if (control->getKind() == Kind::SpinBox)
            {
                text = QString::number(value.toLongLong());
            }

            QWidget* target = editor;
            if (control->getKind() == Kind::SpinBox || control->getKind() == Kind::DoubleSpinBox)
            {
                target = editor->findChild<QLineEdit*>();
            }
            editor->setFocus();
            QTest::keyClick(target, Qt::Key_A, Qt::ControlModifier);
            if (text.isEmpty())
            {
                QTest::keyClick(target, Qt::Key_Delete);
            }
            else
            {
                QTest::keyClicks(target, text);
            }
            QTest::keyClick(target, Qt::Key_Return);
            break;
        }

        case Kind::Slider:
        {
            QSlider* slider = control->getSlider();
            const double factor = std::pow(10.0, control->getDefinition().getDecimals());
            const int target = int(std::llround(value.toDouble() * factor));
            if (target < slider->minimum() || target > slider->maximum())
            {
                return false;
            }

            // Home and the arrows, the value is written after the rest (or at once by the release)
            slider->setFocus();
            QTest::keyClick(slider, Qt::Key_Home);
            for (int guard = 0; slider->value() < target && guard < 100000; ++guard)
            {
                QTest::keyClick(slider, Qt::Key_Right);
            }
            control->commitPendingValue();
            break;
        }

        case Kind::Color:
        {
            // A color picked in the palette (the palette emits colorSelected of the button)
            pdf::PDFColorButton* button = control->getColorButton();
            const QColor color = pdf::PDFAppSettingControl::parseColor(value.toString());
            if (!color.isValid())
            {
                return false;
            }
            button->setColor(color);
            Q_EMIT button->colorSelected(color);
            break;
        }

        case Kind::Segmented:
        {
            // A click on the segment of the option (the page AI, step 7)
            pdf::PDFSegmentedControl* segmented = control->getSegmentedControl();
            const std::vector<pdf::PDFSettingOption> options = control->getDefinition().getOptions();
            int index = -1;
            for (size_t i = 0; i < options.size(); ++i)
            {
                if (options[i].id == value.toString())
                {
                    index = int(i);
                }
            }
            const QList<QAbstractButton*> buttons = segmented->findChildren<QAbstractButton*>();
            if (index < 0 || index >= buttons.size())
            {
                return false;
            }
            QTest::mouseClick(buttons[index], Qt::LeftButton);
            break;
        }
    }

    settle();
    return true;
}

pdf::PDFSettingsRepository AppSettingsFixture::settings() const
{
    return context->getSettings();
}

bool AppSettingsFixture::writeFromOtherConnection(const QString& key, const QVariant& value)
{
    if (!context || !context->getDatabase() || !context->getNotifier())
    {
        return false;
    }

    QSignalSpy spy(context->getNotifier(), &pdf::PDFDatabaseNotifier::changed);
    AppSettingsWriterThread thread(context->getDatabase(), context->getSettingRegistry(), key, value);
    thread.start();
    if (!thread.wait(10000 * getTimeoutFactor()) || !thread.isSuccess())
    {
        return false;
    }

    // The notifier of this connection polls the change log
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000 * getTimeoutFactor())
    {
        for (const QList<QVariant>& arguments : spy)
        {
            if (arguments.value(0).toString() == pdf::PDFSettingsRepository::getTopic() && arguments.value(1).toString() == key)
            {
                settle();
                return true;
            }
        }
        QTest::qWait(10);
    }
    return false;
}

pdf::PDFAppDocumentTab* AppSettingsFixture::openDocument(int pageCount)
{
    static int s_documentCounter = 0;
    const QString name = QStringLiteral("Settings-%1.pdf").arg(++s_documentCounter);
    return open(AppDocumentSamples::simple(folder(), name, pageCount));
}

QImage AppSettingsFixture::screenshot(const QString& name) const
{
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    if (folder.isEmpty() || !window)
    {
        return QImage();
    }

    QDir().mkpath(folder);
    const QImage image = window->grab().toImage();
    image.save(QDir(folder).absoluteFilePath(name + QStringLiteral(".png")));
    return image;
}

}   // namespace pdftest
