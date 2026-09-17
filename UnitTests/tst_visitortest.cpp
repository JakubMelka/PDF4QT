#include "pdfdocumentbuilder.h"
#include "pdfvisitor.h"

#include <QtTest>

template<pdf::PDFAbstractVisitor::Strategy strategy>
class CountingVisitor : public pdf::PDFAbstractVisitor
{
public:
    static constexpr Strategy VisitorStrategy = strategy;

    void visitInt(pdf::PDFInteger value) override
    {
        ++count;
        sum += value;
    }

    void merge(const CountingVisitor* other)
    {
        count += other->count.load();
        sum += other->sum.load();
    }

    std::atomic<int> count = 0;
    std::atomic<pdf::PDFInteger> sum = 0;
};

class VisitorTest : public QObject
{
    Q_OBJECT

private slots:
    void visitsEveryObject_data()
    {
        QTest::addColumn<bool>("parallel");
        QTest::newRow("single-threaded") << false;
        QTest::newRow("parallel") << true;
    }

    void visitsEveryObject()
    {
        QFETCH(bool, parallel);
        using Policy = pdf::PDFExecutionPolicy;
        const auto previous = Policy::isParallelizing(Policy::Scope::Content) ? Policy::Strategy::AlwaysMultithreaded :
                              Policy::isParallelizing(Policy::Scope::Page) ? Policy::Strategy::PageMultithreaded :
                                                                          Policy::Strategy::SingleThreaded;
        auto restore = qScopeGuard([previous]() { Policy::setStrategy(previous); });
        Policy::setStrategy(parallel ? Policy::Strategy::AlwaysMultithreaded : Policy::Strategy::SingleThreaded);

        pdf::PDFDocumentBuilder builder;
        for (int i = 1; i <= 1024; ++i)
        {
            builder.addObject(pdf::PDFObject::createInteger(i));
        }
        const pdf::PDFDocument document = builder.build();

        CountingVisitor<pdf::PDFAbstractVisitor::Strategy::Sequential> sequential;
        CountingVisitor<pdf::PDFAbstractVisitor::Strategy::Parallel> concurrent;
        CountingVisitor<pdf::PDFAbstractVisitor::Strategy::Merging> merging;
        pdf::PDFApplyVisitor(document, &sequential);
        pdf::PDFApplyVisitor(document, &concurrent);
        pdf::PDFApplyVisitor(document, &merging);

        QCOMPARE(sequential.count.load(), 1024);
        QCOMPARE(sequential.sum.load(), pdf::PDFInteger(1024 * 1025 / 2));
        QCOMPARE(concurrent.count.load(), sequential.count.load());
        QCOMPARE(concurrent.sum.load(), sequential.sum.load());
        QCOMPARE(merging.count.load(), sequential.count.load());
        QCOMPARE(merging.sum.load(), sequential.sum.load());
    }
};

QTEST_APPLESS_MAIN(VisitorTest)
#include "tst_visitortest.moc"
