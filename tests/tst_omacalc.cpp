#include <QtTest>
#include <QClipboard>
#include <QGuiApplication>

#include "backend.h"
#include "engine.h"

namespace {
QString run(Engine &engine, const QString &input,
            Format::Notation notation = Format::Notation::Normal) {
    const Engine::Result result = engine.evaluate(input, true);
    if (!result.ok)
        return QStringLiteral("error: ") + result.error;
    Format::Options options;
    options.notation = notation;
    options.angleUnit = engine.angleUnit;
    return Format::value(result.value, options);
}

QString run(const QString &input) {
    Engine engine;
    return run(engine, input);
}

bool fails(const QString &input) {
    Engine engine;
    return !engine.evaluate(input, true).ok;
}
}

class OmacalcTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void init() {
        QSettings settings;
        settings.clear();
    }

    void arithmetic_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("precedence") << "42 × 3 + 7" << "133";
        QTest::newRow("ascii operators") << "10 - 4 / 2" << "8";
        QTest::newRow("parentheses") << "(2 + 3) * 4" << "20";
        QTest::newRow("float noise hidden") << "0.1 + 0.2" << "0.3";
        QTest::newRow("power right assoc") << "2^3^2" << "512";
        QTest::newRow("double star") << "2**10" << "1024";
        QTest::newRow("negation below power") << "-2^2" << "-4";
        QTest::newRow("negative exponent") << "2^-1" << "0.5";
        QTest::newRow("unicode minus") << "5 − 7" << "-2";
        QTest::newRow("implicit multiply") << "2(3 + 4)" << "14";
        QTest::newRow("implicit binds tighter") << "1/2pi" << "0.159154943091895";
        QTest::newRow("pi symbol") << "2π" << "6.28318530717959";
        QTest::newRow("factorial") << "5!" << "120";
        QTest::newRow("percent") << "50%" << "0.5";
        QTest::newRow("modulo") << "7 % 3" << "1";
        QTest::newRow("mod keyword floors") << "-7 mod 3" << "2";
        QTest::newRow("squared") << "3²" << "9";
        QTest::newRow("inverse") << "4⁻¹" << "0.25";
        QTest::newRow("sqrt symbol") << "√16 + ∛27" << "7";
        QTest::newRow("scientific literal") << "1.5e3 + 2E-1" << "1500.2";
        QTest::newRow("auto close") << "2(3 + sqrt(16" << "14";
        QTest::newRow("big") << "2^60" << "1.15292150460685e18";
    }

    void arithmetic() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(run(input), expected);
    }

    void siSuffixes_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("kilo") << "4.7k" << "4700";
        QTest::newRow("micro u") << "10u * 1k" << "0.01";
        QTest::newRow("micro sign") << "2.2µ * 1M" << "2.2";
        QTest::newRow("nano") << "100n" << "1e-7";
        QTest::newRow("pico") << "1p" << "1e-12";
        QTest::newRow("milli") << "5m" << "0.005";
        QTest::newRow("giga") << "3G" << "3000000000";
        QTest::newRow("not a suffix before a name") << "2pi" << "6.28318530717959";
        QTest::newRow("rc corner") << "1/(2pi*1k*159n)" << "1000.97448485469";
    }

    void siSuffixes() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(run(input), expected);
    }

    void engineeringNotation_data() {
        QTest::addColumn<double>("value");
        QTest::addColumn<QString>("expected");
        QTest::newRow("kilo") << 4700.0 << "4.7k";
        QTest::newRow("micro") << 2.2e-6 << "2.2µ";
        QTest::newRow("milli boundary") << 0.001 << "1m";
        QTest::newRow("unit") << 1.0 << "1";
        QTest::newRow("hundreds") << 470.0 << "470";
        QTest::newRow("negative nano") << -33e-9 << "-33n";
        QTest::newRow("beyond prefixes") << 5e18 << "5e18";
        QTest::newRow("round up to next prefix") << 999.9999999999999 << "1k";
    }

    void engineeringNotation() {
        QFETCH(double, value);
        QFETCH(QString, expected);
        QCOMPARE(Format::real(value, Format::Notation::Engineering), expected);
    }

    void scientificNotation() {
        QCOMPARE(Format::real(1234, Format::Notation::Scientific), QStringLiteral("1.234e3"));
        QCOMPARE(Format::real(-0.00052, Format::Notation::Scientific), QStringLiteral("-5.2e-4"));
    }

    void engineeringOutputReadsBack() {
        Engine engine;
        for (const char *text : {"4.7k", "2.2µ", "3 + 4.7k·j", "1e-3j"}) {
            const Engine::Result result = engine.evaluate(QString::fromUtf8(text), false);
            QVERIFY2(result.ok, text);
        }
        QCOMPARE(run(engine, "4.7k + 2.2k j", Format::Notation::Engineering),
                 QStringLiteral("4.7k + 2.2k·j"));
    }

    void trigonometry() {
        Engine engine;
        QCOMPARE(run(engine, "sin(pi/2)"), QStringLiteral("1"));
        QCOMPARE(run(engine, "sin(pi)"), QStringLiteral("0"));
        QCOMPARE(run(engine, "cos(pi/2)"), QStringLiteral("0"));
        QCOMPARE(run(engine, "sin(30°)"), QStringLiteral("0.5"));

        engine.angleUnit = Engine::AngleUnit::Degrees;
        QCOMPARE(run(engine, "sin(30)"), QStringLiteral("0.5"));
        QCOMPARE(run(engine, "sin(180)"), QStringLiteral("0"));
        QCOMPARE(run(engine, "cos(-90)"), QStringLiteral("0"));
        QCOMPARE(run(engine, "sin 30 * 2 - 1"), QStringLiteral("0"));
        QCOMPARE(run(engine, "asin(1)"), QStringLiteral("90"));
        QCOMPARE(run(engine, "atan2(1, 1)"), QStringLiteral("45"));
        QVERIFY(run(engine, "tan(90)").startsWith(QStringLiteral("error")));
    }

    void functions_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("log10") << "log(1000)" << "3";
        QTest::newRow("log base") << "log(8, 2)" << "3";
        QTest::newRow("ln e") << "ln(e)" << "1";
        QTest::newRow("odd root of negative") << "root(-27, 3)" << "-3";
        QTest::newRow("nCr") << "nCr(10, 3)" << "120";
        QTest::newRow("nPr") << "nPr(5, 2)" << "20";
        QTest::newRow("gcd") << "gcd(48, 18, 12)" << "6";
        QTest::newRow("lcm") << "lcm(4, 6)" << "12";
        QTest::newRow("round digits") << "round(3.14159, 2)" << "3.14";
        QTest::newRow("mean") << "mean(1, 2, 3, 4)" << "2.5";
        QTest::newRow("median") << "median(5, 1, 3)" << "3";
        QTest::newRow("stdev") << "stdev(2, 4, 4, 4, 5, 5, 7, 9)" << "2.1380899352994";
        QTest::newRow("hypot") << "hypot(3, 4)" << "5";
        QTest::newRow("db") << "db(10)" << "20";
        QTest::newRow("undb") << "undb(-6)" << "0.501187233627272";
        QTest::newRow("parallel operator") << "10k || 10k" << "5000";
        QTest::newRow("parallel symbol") << "1k ∥ 1k ∥ 1k" << "333.333333333333";
        QTest::newRow("par function") << "par(6, 3)" << "2";
        QTest::newRow("parallel before plus") << "1k + 2k || 2k" << "2000";
        QTest::newRow("juxtaposed function") << "2 sqrt(9)" << "6";
        QTest::newRow("function without parens") << "ln 1" << "0";
    }

    void functions() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(run(input), expected);
    }

    void complexNumbers_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("sqrt negative") << "sqrt(-4)" << "2j";
        QTest::newRow("j squared exact") << "j^2" << "-1";
        QTest::newRow("rectangular") << "(3 + 4j)(1 - 2j)" << "11 - 2j";
        QTest::newRow("magnitude") << "abs(3 + 4j)" << "5";
        QTest::newRow("conjugate") << "conj(1 + j)" << "1 - j";
        QTest::newRow("euler") << "e^(j pi)" << "-1";
        QTest::newRow("ln negative") << "ln(-1)" << "3.14159265358979j";
        QTest::newRow("capacitor impedance") << "1/(j 2pi*1k*1u)" << "-159.154943091895j";
    }

    void complexNumbers() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(run(input), expected);
    }

    void polar() {
        Engine engine;
        engine.angleUnit = Engine::AngleUnit::Degrees;
        QCOMPARE(run(engine, "2∠90"), QStringLiteral("2j"));
        QCOMPARE(run(engine, "10@180"), QStringLiteral("-10"));

        Format::Options options;
        options.polar = true;
        options.angleUnit = Engine::AngleUnit::Degrees;
        QCOMPARE(Format::value(Complex(0, 5), options), QStringLiteral("5 ∠ 90°"));
        QCOMPARE(Format::value(Complex(3, 4), options), QStringLiteral("5 ∠ 53.130102354156°"));
        // Polar output reads back in either angle unit thanks to the ° mark.
        engine.angleUnit = Engine::AngleUnit::Radians;
        QCOMPARE(run(engine, "5 ∠ 90°"), QStringLiteral("5j"));
    }

    void programmer() {
        QCOMPARE(run("0xFF + 0b1010 + 0o17"), QStringLiteral("280"));
        QCOMPARE(run("0b1111_0000 & 0x3C"), QStringLiteral("48"));
        QCOMPARE(run("5 | 2"), QStringLiteral("7"));
        QCOMPARE(run("6 xor 3"), QStringLiteral("5"));
        QCOMPARE(run("1 << 10"), QStringLiteral("1024"));
        QCOMPARE(run("~0"), QStringLiteral("-1"));
        QVERIFY(fails("1.5 & 1"));

        QCOMPARE(Format::integer(255, Format::Base::Hexadecimal), QStringLiteral("0xFF"));
        QCOMPARE(Format::integer(10, Format::Base::Binary), QStringLiteral("0b1010"));
        QCOMPARE(Format::integer(255, Format::Base::Binary), QStringLiteral("0b1111_1111"));
        QCOMPARE(Format::integer(-8, Format::Base::Octal), QStringLiteral("-0o10"));
        QVERIFY(Format::integer(1.5, Format::Base::Hexadecimal).isEmpty());
    }

    void variablesAndAns() {
        Engine engine;
        QCOMPARE(run(engine, "R1 = 10k"), QStringLiteral("10000"));
        QCOMPARE(run(engine, "R2 = 4.7k"), QStringLiteral("4700"));
        QCOMPARE(run(engine, "R1 || R2"), QStringLiteral("3197.27891156463"));
        QCOMPARE(run(engine, "ans * 2"), QStringLiteral("6394.55782312925"));
        QCOMPARE(run(engine, "2R1"), QStringLiteral("20000"));
        QVERIFY(!engine.evaluate(QStringLiteral("pi = 3"), true).ok);
        QVERIFY(!engine.evaluate(QStringLiteral("sin = 3"), true).ok);
        // Physical constants step aside for a variable of the same name.
        QCOMPARE(run(engine, "c"), QStringLiteral("299792458"));
        QCOMPARE(run(engine, "c = 100n"), QStringLiteral("1e-7"));
        QCOMPARE(run(engine, "c * 1M"), QStringLiteral("0.1"));
    }

    void previewHasNoSideEffects() {
        Engine engine;
        engine.evaluate(QStringLiteral("x = 5"), false);
        QVERIFY(!engine.variables.contains(QStringLiteral("x")));
        engine.evaluate(QStringLiteral("f(x) = x"), false);
        QVERIFY(!engine.functions.contains(QStringLiteral("f")));
        engine.evaluate(QStringLiteral("42"), false);
        QCOMPARE(engine.ans, Complex(0));
    }

    void userFunctions() {
        Engine engine;
        QVERIFY(engine.evaluate(QStringLiteral("f(x) = 3x^2 + 1"), true).definedFunction);
        QCOMPARE(run(engine, "f(2)"), QStringLiteral("13"));
        QVERIFY(engine.evaluate(QStringLiteral("fc(r, c) = 1/(2pi r c)"), true).ok);
        QCOMPARE(run(engine, "fc(1k, 1u)", Format::Notation::Engineering),
                 QStringLiteral("159.154943091895"));
        QVERIFY(run(engine, "f(1, 2)").startsWith(QStringLiteral("error")));
        // Typos are caught at definition time; math errors at the trial point are not.
        QVERIFY(!engine.evaluate(QStringLiteral("g(x) = x + nope"), true).ok);
        QVERIFY(engine.evaluate(QStringLiteral("h(x) = 1/(x - 1)"), true).ok);
        QVERIFY(engine.evaluate(QStringLiteral("loop(x) = loop(x)"), true).ok);
        QCOMPARE(run(engine, "loop(1)"), QStringLiteral("error: Too much recursion"));
    }

    void errors_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("error");
        QTest::newRow("divide by zero") << "1/0" << "Division by zero";
        QTest::newRow("unknown name") << "foo + 1" << "Unknown name 'foo'";
        QTest::newRow("unknown function") << "foo(1)" << "Unknown function 'foo'";
        QTest::newRow("stray close") << "1 + 2)" << "Unexpected ')'";
        QTest::newRow("dangling operator") << "1 +" << "Incomplete expression";
        QTest::newRow("adjacent numbers") << "2 3" << "Missing operator between numbers";
        QTest::newRow("adjacent si numbers") << "1k 1u" << "Missing operator between numbers";
        QTest::newRow("arity") << "sin(1, 2)" << "sin takes 1 argument";
        QTest::newRow("overflow") << "10^400" << "Overflow";
        QTest::newRow("log zero") << "ln(0)" << "Logarithm of zero";
    }

    void errors() {
        QFETCH(QString, input);
        QFETCH(QString, error);
        Engine engine;
        const Engine::Result result = engine.evaluate(input, true);
        QVERIFY(!result.ok);
        QCOMPARE(result.error, error);
    }

    void backendPreviewAndCommit() {
        Backend calculator;
        QCOMPARE(calculator.display(), QStringLiteral("0"));

        calculator.setInput(QStringLiteral("6 × 7"));
        QCOMPARE(calculator.display(), QStringLiteral("42"));
        QVERIFY(calculator.displayIsPreview());
        QVERIFY(calculator.history().isEmpty());

        // An incomplete expression keeps the last good preview.
        calculator.setInput(QStringLiteral("6 × 7 +"));
        QCOMPARE(calculator.display(), QStringLiteral("42"));
        QCOMPARE(calculator.status(), QStringLiteral("Incomplete expression"));

        calculator.setInput(QStringLiteral("6 × 7"));
        QVERIFY(calculator.evaluate());
        QCOMPARE(calculator.input(), QString());
        QCOMPARE(calculator.display(), QStringLiteral("42"));
        QVERIFY(!calculator.displayIsPreview());
        QCOMPARE(calculator.history().size(), 1);
        const QVariantMap entry = calculator.history().first().toMap();
        QCOMPARE(entry.value(QStringLiteral("expression")).toString(), QStringLiteral("6 × 7"));
        QCOMPARE(entry.value(QStringLiteral("result")).toString(), QStringLiteral("42"));
        QCOMPARE(calculator.alternate(), QStringLiteral("0x2A   0b10_1010"));

        calculator.setInput(QStringLiteral("1/0"));
        QVERIFY(!calculator.evaluate());
        QCOMPARE(calculator.status(), QStringLiteral("Division by zero"));
        QCOMPARE(calculator.history().size(), 1);
    }

    void backendModesReformatHistory() {
        Backend calculator;
        calculator.setInput(QStringLiteral("4700"));
        calculator.evaluate();
        calculator.setNotation(QStringLiteral("eng"));
        QCOMPARE(calculator.display(), QStringLiteral("4.7k"));
        QCOMPARE(calculator.history().last().toMap().value(QStringLiteral("result")).toString(),
                 QStringLiteral("4.7k"));
        calculator.setBase(QStringLiteral("hex"));
        QCOMPARE(calculator.display(), QStringLiteral("0x125C"));
        calculator.setBase(QStringLiteral("dec"));

        calculator.setAngleMode(QStringLiteral("deg"));
        calculator.setInput(QStringLiteral("sin(90)"));
        QCOMPARE(calculator.display(), QStringLiteral("1"));
    }

    void backendPersistsState() {
        {
            Backend calculator;
            calculator.setAngleMode(QStringLiteral("deg"));
            calculator.setInput(QStringLiteral("R = 10k"));
            calculator.evaluate();
            calculator.setInput(QStringLiteral("f(x) = 2x"));
            calculator.evaluate();
        }
        Backend restored;
        QCOMPARE(restored.angleMode(), QStringLiteral("deg"));
        QCOMPARE(restored.history().size(), 2);
        restored.setInput(QStringLiteral("f(R)"));
        QCOMPARE(restored.display(), QStringLiteral("20000"));
        restored.setInput(QString());
        QCOMPARE(restored.display(), QStringLiteral("10000"));  // ans survives too
    }

    void copiesResult() {
        Backend calculator;
        calculator.setInput(QStringLiteral("2 + 2"));
        calculator.evaluate();
        calculator.copyResult();
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("4"));
    }

private:
    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(OmacalcTest)
#include "tst_omacalc.moc"
