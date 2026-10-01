#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

#include <complex>

using Complex = std::complex<double>;

// The expression language behind the calculator: a recursive-descent parser
// that evaluates as it goes, over complex numbers so impedances and phasors
// work without a separate mode. Real inputs stay on the real code paths, so
// sqrt(2) never grows a stray 0j.
class Engine {
public:
    enum class AngleUnit { Degrees, Radians };

    struct UserFunction {
        QStringList parameters;
        QString body;
    };

    struct Result {
        bool ok = false;
        Complex value;
        QString error;
        // Set when the input was "name = expr" or "f(x) = expr".
        QString assignedName;
        bool definedFunction = false;
    };

    // Evaluates one line. With commit false nothing is stored, which is what
    // the live preview uses while typing.
    Result evaluate(const QString &input, bool commit);

    AngleUnit angleUnit = AngleUnit::Radians;
    Complex ans = 0;
    QMap<QString, Complex> variables;
    QMap<QString, UserFunction> functions;

    static bool isReserved(const QString &name);
    static bool isConstant(const QString &name);
    static bool constantValue(const QString &name, Complex *value);
    static bool isBuiltinFunction(const QString &name);

    // One entry per builtin, for the reference panel: {name, description}.
    static QList<QPair<QString, QString>> functionReference();
    static QList<QPair<QString, QString>> constantReference();

private:
    friend class Parser;
    int m_callDepth = 0;
};

// Display formatting for engine values.
namespace Format {
enum class Notation { Normal, Scientific, Engineering };
enum class Base { Decimal, Hexadecimal, Binary, Octal };

struct Options {
    Notation notation = Notation::Normal;
    Base base = Base::Decimal;
    bool polar = false;
    Engine::AngleUnit angleUnit = Engine::AngleUnit::Radians;
};

QString real(double value, Notation notation);
QString value(const Complex &value, const Options &options);
// Integer representation in another base, or empty when value is not an
// integer that fits in 64 bits.
QString integer(const Complex &value, Base base);
// A second, differently-shaped rendering worth showing under the result:
// the other complex form, other bases for integers, or SI notation.
QString alternate(const Complex &value, const Options &options);
}
