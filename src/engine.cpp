#include "engine.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr int maxCallDepth = 64;

struct EvalError {
    QString message;
    // Syntax errors make a function definition invalid; math errors (say,
    // a division by zero at the trial argument) do not.
    bool syntax = false;
};

[[noreturn]] void fail(const QString &message, bool syntax = false) {
    throw EvalError{message, syntax};
}

struct Token {
    enum Kind { Number, Identifier, Operator, End };
    Kind kind = End;
    QString text;
    double number = 0;
    int position = 0;
};

bool isIdentifierStart(QChar character) {
    return character != QChar(0x03C0) && (character.isLetter() || character == QLatin1Char('_'));
}

bool isIdentifierPart(QChar character) {
    return isIdentifierStart(character) || character.isDigit();
}

// SI prefixes accepted directly after a number: 4.7k, 100n, 2.2µ.
bool siScale(QChar character, double *scale) {
    switch (character.unicode()) {
    case 'f': *scale = 1e-15; return true;
    case 'p': *scale = 1e-12; return true;
    case 'n': *scale = 1e-9; return true;
    case 'u': case 0x00B5: case 0x03BC: *scale = 1e-6; return true;
    case 'm': *scale = 1e-3; return true;
    case 'k': case 'K': *scale = 1e3; return true;
    case 'M': *scale = 1e6; return true;
    case 'G': *scale = 1e9; return true;
    case 'T': *scale = 1e12; return true;
    default: return false;
    }
}

QList<Token> tokenize(const QString &input) {
    QList<Token> tokens;
    const int length = input.size();
    int i = 0;

    auto at = [&](int index) { return index < length ? input.at(index) : QChar(); };
    auto pushOperator = [&](const QString &text, int position, int width) {
        Token token;
        token.kind = Token::Operator;
        token.text = text;
        token.position = position;
        tokens << token;
        i += width;
    };

    while (i < length) {
        const QChar c = input.at(i);
        const int start = i;

        if (c.isSpace()) {
            ++i;
            continue;
        }

        if (c.isDigit() || (c == QLatin1Char('.') && at(i + 1).isDigit())) {
            Token token;
            token.kind = Token::Number;
            token.position = start;

            const QChar radixMark = at(i + 1).toLower();
            if (c == QLatin1Char('0') && (radixMark == QLatin1Char('x')
                    || radixMark == QLatin1Char('b') || radixMark == QLatin1Char('o'))) {
                const int base = radixMark == QLatin1Char('x') ? 16
                    : radixMark == QLatin1Char('b') ? 2 : 8;
                i += 2;
                QString digits;
                while (i < length && (isIdentifierPart(input.at(i)))) {
                    if (input.at(i) != QLatin1Char('_'))
                        digits += input.at(i);
                    ++i;
                }
                bool ok = false;
                const qulonglong value = digits.toULongLong(&ok, base);
                if (!ok || digits.isEmpty())
                    fail(QStringLiteral("Bad base-%1 number").arg(base), true);
                token.number = double(value);
                tokens << token;
                continue;
            }

            QString digits;
            while (i < length && (input.at(i).isDigit() || input.at(i) == QLatin1Char('_'))) {
                if (input.at(i) != QLatin1Char('_'))
                    digits += input.at(i);
                ++i;
            }
            if (at(i) == QLatin1Char('.')) {
                digits += QLatin1Char('.');
                ++i;
                while (i < length && input.at(i).isDigit())
                    digits += input.at(i++);
            }
            // An exponent needs digits after it; a bare "2e" is 2 times e.
            if (at(i) == QLatin1Char('e') || at(i) == QLatin1Char('E')) {
                int j = i + 1;
                if (at(j) == QLatin1Char('+') || at(j) == QLatin1Char('-')
                        || at(j) == QChar(0x2212))
                    ++j;
                if (at(j).isDigit()) {
                    digits += QLatin1Char('e');
                    if (at(i + 1) == QChar(0x2212))
                        digits += QLatin1Char('-');
                    else if (j == i + 2)
                        digits += at(i + 1);
                    i = j;
                    while (i < length && input.at(i).isDigit())
                        digits += input.at(i++);
                }
            }
            bool ok = false;
            token.number = digits.toDouble(&ok);
            if (!ok)
                fail(QStringLiteral("Bad number '%1'").arg(digits), true);

            double scale = 1;
            if (siScale(at(i), &scale) && !isIdentifierPart(at(i + 1))) {
                token.number *= scale;
                ++i;
            }
            tokens << token;
            continue;
        }

        if (c == QChar(0x03C0)) {  // π stands alone so 2πr reads as 2·π·r.
            Token token;
            token.kind = Token::Identifier;
            token.text = QStringLiteral("pi");
            token.position = start;
            tokens << token;
            ++i;
            continue;
        }

        if (isIdentifierStart(c)) {
            QString name;
            while (i < length && isIdentifierPart(input.at(i)) && input.at(i) != QChar(0x03C0)) {
                const QChar part = input.at(i++);
                name += part == QChar(0x00B5) ? QChar(0x03BC) : part;  // micro sign → mu
            }
            Token token;
            token.kind = Token::Identifier;
            token.text = name;
            token.position = start;
            tokens << token;
            continue;
        }

        const QString two = input.mid(i, 2);
        if (two == QStringLiteral("**")) { pushOperator(QStringLiteral("^"), start, 2); continue; }
        if (two == QStringLiteral("||")) { pushOperator(QStringLiteral("∥"), start, 2); continue; }
        if (two == QStringLiteral("<<") || two == QStringLiteral(">>")) {
            pushOperator(two, start, 2);
            continue;
        }
        if (two == QStringLiteral("⁻¹")) { pushOperator(two, start, 2); continue; }

        switch (c.unicode()) {
        case '+': pushOperator(QStringLiteral("+"), start, 1); continue;
        case '-': case 0x2212: pushOperator(QStringLiteral("-"), start, 1); continue;
        case '*': case 0x00D7: case 0x00B7: case 0x22C5:
            pushOperator(QStringLiteral("*"), start, 1); continue;
        case '/': case 0x00F7: pushOperator(QStringLiteral("/"), start, 1); continue;
        case '^': pushOperator(QStringLiteral("^"), start, 1); continue;
        case '!': case '%': case '(': case ')': case ',': case '=': case '&': case '|': case '~':
            pushOperator(QString(c), start, 1); continue;
        case 0x00B0: pushOperator(QStringLiteral("°"), start, 1); continue;
        case 0x00B2: pushOperator(QStringLiteral("²"), start, 1); continue;
        case 0x00B3: pushOperator(QStringLiteral("³"), start, 1); continue;
        case 0x221A: pushOperator(QStringLiteral("√"), start, 1); continue;
        case 0x221B: pushOperator(QStringLiteral("∛"), start, 1); continue;
        case 0x2225: case 0x2016: pushOperator(QStringLiteral("∥"), start, 1); continue;
        case 0x2220: case '@': pushOperator(QStringLiteral("∠"), start, 1); continue;
        default:
            fail(QStringLiteral("Unexpected '%1'").arg(c), true);
        }
    }

    Token end;
    end.position = length;
    tokens << end;
    return tokens;
}

bool isReal(const Complex &value) { return value.imag() == 0; }

double requireReal(const Complex &value, const QString &what) {
    if (!isReal(value))
        fail(QStringLiteral("%1 needs a real number").arg(what));
    return value.real();
}

qint64 requireInteger(const Complex &value, const QString &what) {
    const double real = requireReal(value, what);
    if (real != std::floor(real) || std::abs(real) >= 9.2e18)
        fail(QStringLiteral("%1 needs an integer").arg(what));
    return qint64(real);
}

Complex integerPower(Complex base, long long exponent) {
    const bool invert = exponent < 0;
    unsigned long long n = invert ? -exponent : exponent;
    Complex result = 1;
    while (n) {
        if (n & 1)
            result *= base;
        base *= base;
        n >>= 1;
    }
    return invert ? Complex(1) / result : result;
}

Complex power(const Complex &base, const Complex &exponent) {
    if (base == Complex(0)) {
        if (exponent.real() < 0)
            fail(QStringLiteral("Division by zero"));
        return exponent == Complex(0) ? 1 : 0;
    }
    const bool integerExponent = isReal(exponent)
        && exponent.real() == std::floor(exponent.real());
    if (isReal(base) && isReal(exponent) && (base.real() > 0 || integerExponent))
        return std::pow(base.real(), exponent.real());
    // Repeated multiplication keeps j² at exactly -1.
    if (integerExponent && std::abs(exponent.real()) <= 64)
        return integerPower(base, (long long)exponent.real());
    return std::pow(base, exponent);
}

Complex divide(const Complex &a, const Complex &b) {
    if (b == Complex(0))
        fail(QStringLiteral("Division by zero"));
    if (isReal(a) && isReal(b))
        return a.real() / b.real();
    return a / b;
}

Complex squareRoot(const Complex &x) {
    if (isReal(x))
        return x.real() >= 0 ? Complex(std::sqrt(x.real())) : Complex(0, std::sqrt(-x.real()));
    return std::sqrt(x);
}

Complex logarithm(const Complex &x) {
    if (x == Complex(0))
        fail(QStringLiteral("Logarithm of zero"));
    if (isReal(x))
        return x.real() > 0 ? Complex(std::log(x.real())) : Complex(std::log(-x.real()), pi);
    return std::log(x);
}

double factorial(double n) {
    if (n < 0 && n == std::floor(n))
        fail(QStringLiteral("Factorial of a negative integer"));
    if (n != std::floor(n))
        return std::tgamma(n + 1);
    if (n > 170)
        fail(QStringLiteral("Overflow"));
    double result = 1;
    for (int k = 2; k <= int(n); ++k)
        result *= k;
    return result;
}

double combinations(double n, double r, bool ordered) {
    if (n != std::floor(n) || r != std::floor(r) || n < 0 || r < 0)
        fail(ordered ? QStringLiteral("nPr needs non-negative integers")
                     : QStringLiteral("nCr needs non-negative integers"));
    if (r > n)
        return 0;
    double result = 1;
    if (ordered) {
        for (double k = 0; k < r; ++k)
            result *= n - k;
    } else {
        r = std::min(r, n - r);
        for (double k = 1; k <= r; ++k)
            result = result * (n - r + k) / k;
        result = std::round(result);
    }
    return result;
}

qint64 greatestCommonDivisor(qint64 a, qint64 b) {
    a = std::llabs(a);
    b = std::llabs(b);
    while (b) {
        const qint64 t = a % b;
        a = b;
        b = t;
    }
    return a;
}

double flooredModulo(double a, double b) {
    if (b == 0)
        fail(QStringLiteral("Division by zero"));
    const double r = std::fmod(a, b);
    return (r != 0 && ((r < 0) != (b < 0))) ? r + b : r;
}

Complex parallel(const Complex &a, const Complex &b) {
    if (a == Complex(0) || b == Complex(0))
        return 0;
    const Complex sum = a + b;
    if (sum == Complex(0))
        fail(QStringLiteral("Parallel of opposite values"));
    return divide(a * b, sum);
}

struct BuiltinInfo {
    const char *name;
    int minArgs;
    int maxArgs;  // -1 for any number
    const char *help;
};

const BuiltinInfo builtins[] = {
    {"sin", 1, 1, "sine (in the current angle unit)"},
    {"cos", 1, 1, "cosine"},
    {"tan", 1, 1, "tangent"},
    {"sec", 1, 1, "secant"},
    {"csc", 1, 1, "cosecant"},
    {"cot", 1, 1, "cotangent"},
    {"asin", 1, 1, "inverse sine"},
    {"acos", 1, 1, "inverse cosine"},
    {"atan", 1, 1, "inverse tangent"},
    {"atan2", 2, 2, "atan2(y, x): angle of the point (x, y)"},
    {"sinh", 1, 1, "hyperbolic sine"},
    {"cosh", 1, 1, "hyperbolic cosine"},
    {"tanh", 1, 1, "hyperbolic tangent"},
    {"asinh", 1, 1, "inverse hyperbolic sine"},
    {"acosh", 1, 1, "inverse hyperbolic cosine"},
    {"atanh", 1, 1, "inverse hyperbolic tangent"},
    {"sqrt", 1, 1, "square root (√x)"},
    {"cbrt", 1, 1, "cube root (∛x)"},
    {"root", 2, 2, "root(x, n): n-th root"},
    {"exp", 1, 1, "e to the x"},
    {"ln", 1, 1, "natural logarithm"},
    {"log", 1, 2, "log(x) base 10, log(x, b) base b"},
    {"log10", 1, 1, "base-10 logarithm"},
    {"log2", 1, 1, "base-2 logarithm"},
    {"abs", 1, 1, "absolute value / magnitude"},
    {"arg", 1, 1, "phase angle of a complex number"},
    {"re", 1, 1, "real part"},
    {"im", 1, 1, "imaginary part"},
    {"conj", 1, 1, "complex conjugate"},
    {"polar", 2, 2, "polar(r, θ): same as r∠θ"},
    {"todeg", 1, 1, "radians → degrees"},
    {"torad", 1, 1, "degrees → radians"},
    {"floor", 1, 1, "round down"},
    {"ceil", 1, 1, "round up"},
    {"round", 1, 2, "round(x), round(x, digits)"},
    {"trunc", 1, 1, "drop the fractional part"},
    {"frac", 1, 1, "fractional part"},
    {"sign", 1, 1, "-1, 0 or 1"},
    {"min", 1, -1, "smallest argument"},
    {"max", 1, -1, "largest argument"},
    {"sum", 1, -1, "sum of the arguments"},
    {"mean", 1, -1, "arithmetic mean"},
    {"median", 1, -1, "median"},
    {"stdev", 2, -1, "sample standard deviation"},
    {"var", 2, -1, "sample variance"},
    {"hypot", 1, -1, "√(a² + b² + …)"},
    {"gcd", 2, -1, "greatest common divisor"},
    {"lcm", 2, -1, "least common multiple"},
    {"nCr", 2, 2, "combinations"},
    {"nPr", 2, 2, "permutations"},
    {"gamma", 1, 1, "gamma function"},
    {"par", 2, -1, "parallel combination, same as a ∥ b"},
    {"db", 1, 1, "20·log10|x| (amplitude ratio → dB)"},
    {"dbp", 1, 1, "10·log10|x| (power ratio → dB)"},
    {"undb", 1, 1, "10^(x/20) (dB → amplitude ratio)"},
    {"undbp", 1, 1, "10^(x/10) (dB → power ratio)"},
};

const BuiltinInfo *findBuiltin(const QString &name) {
    for (const BuiltinInfo &info : builtins) {
        if (name == QLatin1String(info.name))
            return &info;
    }
    if (name == QStringLiteral("ncr"))
        return findBuiltin(QStringLiteral("nCr"));
    if (name == QStringLiteral("npr"))
        return findBuiltin(QStringLiteral("nPr"));
    return nullptr;
}

struct ConstantInfo {
    const char *name;
    double real;
    double imaginary;
    const char *help;
    // Math constants can't be reassigned; physical ones can be shadowed by a
    // variable, so R = 10k works.
    bool fixed = false;
};

const ConstantInfo constants[] = {
    {"pi", pi, 0, "π", true},
    {"tau", 2 * pi, 0, "2π", true},
    {"e", 2.71828182845904523536, 0, "Euler's number", true},
    {"i", 0, 1, "imaginary unit", true},
    {"j", 0, 1, "imaginary unit", true},
    {"phi", 1.61803398874989484820, 0, "golden ratio", true},
    {"c", 299792458, 0, "speed of light, m/s"},
    {"h", 6.62607015e-34, 0, "Planck constant, J·s"},
    {"hbar", 6.62607015e-34 / (2 * pi), 0, "reduced Planck constant, J·s"},
    {"kB", 1.380649e-23, 0, "Boltzmann constant, J/K"},
    {"q", 1.602176634e-19, 0, "elementary charge, C"},
    {"me", 9.1093837139e-31, 0, "electron mass, kg"},
    {"mp", 1.67262192595e-27, 0, "proton mass, kg"},
    {"NA", 6.02214076e23, 0, "Avogadro constant, 1/mol"},
    {"R", 8.314462618, 0, "gas constant, J/(mol·K)"},
    {"eps0", 8.8541878188e-12, 0, "vacuum permittivity ε0, F/m"},
    {"ε0", 8.8541878188e-12, 0, "vacuum permittivity, F/m"},
    {"mu0", 1.25663706127e-6, 0, "vacuum permeability μ0, H/m"},
    {"μ0", 1.25663706127e-6, 0, "vacuum permeability, H/m"},
    {"Z0", 376.730313412, 0, "free-space impedance, Ω"},
    {"VT", 1.380649e-23 * 300 / 1.602176634e-19, 0, "thermal voltage kT/q at 300 K, V"},
    {"g", 9.80665, 0, "standard gravity, m/s²"},
    {"sigma", 5.670374419e-8, 0, "Stefan–Boltzmann constant, W/(m²·K⁴)"},
};

double toRadiansFactor(Engine::AngleUnit unit) {
    return unit == Engine::AngleUnit::Degrees ? pi / 180 : 1;
}

// Real sine and cosine that land exactly on 0, ±½ and ±1 at the angles
// where people expect them, so sin(180°) is 0 rather than 1.2e-16.
bool exactTrig(double degrees, bool cosine, double *result) {
    const double turn = std::fmod(degrees, 360.0);
    if (std::fmod(turn, 30.0) != 0)
        return false;
    int step = int(turn / 30.0);  // multiples of 30°
    if (cosine)
        step += 3;
    step = ((step % 12) + 12) % 12;
    static const double halfSteps[12] = {0, 0.5, NAN, 1, NAN, 0.5, 0, -0.5, NAN, -1, NAN, -0.5};
    if (std::isnan(halfSteps[step]))
        return false;
    *result = halfSteps[step];
    return true;
}

// Rounding noise like sin(π) = 1.2e-16 snaps to zero when the argument was
// large enough that the noise is all that is left.
double snapTrig(double result, double radians) {
    if (std::abs(radians) >= 1
            && std::abs(result) < 4 * std::numeric_limits<double>::epsilon() * std::abs(radians))
        return 0;
    return result;
}
}

class Parser {
public:
    Parser(Engine &engine, const QList<Token> &tokens, const QMap<QString, Complex> *locals)
        : m_engine(engine), m_tokens(tokens), m_locals(locals) {}

    Complex parseAll() {
        const Complex value = parseExpression();
        if (peek().kind != Token::End)
            unexpected();
        return value;
    }

    Complex parseExpression() { return parseBitOr(); }

    int position() const { return m_index; }

private:
    const Token &peek(int ahead = 0) const {
        return m_tokens.at(std::min(m_index + ahead, int(m_tokens.size()) - 1));
    }
    const Token &next() { return m_tokens.at(m_index++); }

    bool isOperator(const QString &text, int ahead = 0) const {
        const Token &token = peek(ahead);
        return token.kind == Token::Operator && token.text == text;
    }
    bool isWord(const QString &word) const {
        return peek().kind == Token::Identifier && peek().text == word;
    }
    bool accept(const QString &text) {
        if (!isOperator(text))
            return false;
        ++m_index;
        return true;
    }
    void expect(const QString &text) {
        if (accept(text))
            return;
        // Parentheses left open at the end close themselves: sin(30 is sin(30).
        if (text == QStringLiteral(")") && peek().kind == Token::End)
            return;
        unexpected();
    }
    [[noreturn]] void unexpected() const {
        const Token &token = peek();
        if (token.kind == Token::End)
            fail(QStringLiteral("Incomplete expression"), true);
        const QString text = token.kind == Token::Number
            ? QStringLiteral("number") : QStringLiteral("'%1'").arg(token.text);
        fail(QStringLiteral("Unexpected %1").arg(text), true);
    }

    // Tokens that can begin an operand, and so imply multiplication when
    // they follow one directly: 2π, 3(4 + 5), 2 sin(x).
    bool startsOperand() const {
        const Token &token = peek();
        if (token.kind == Token::Number)
            return true;
        if (token.kind == Token::Identifier)
            return token.text != QStringLiteral("mod") && token.text != QStringLiteral("xor");
        return token.kind == Token::Operator
            && (token.text == QStringLiteral("(") || token.text == QStringLiteral("√")
                || token.text == QStringLiteral("∛"));
    }

    Complex parseBitOr() {
        Complex value = parseBitXor();
        while (isOperator(QStringLiteral("|"))) {
            ++m_index;
            const qint64 right = requireInteger(parseBitXor(), QStringLiteral("|"));
            value = double(requireInteger(value, QStringLiteral("|")) | right);
        }
        return value;
    }

    Complex parseBitXor() {
        Complex value = parseBitAnd();
        while (isWord(QStringLiteral("xor"))) {
            ++m_index;
            const qint64 right = requireInteger(parseBitAnd(), QStringLiteral("xor"));
            value = double(requireInteger(value, QStringLiteral("xor")) ^ right);
        }
        return value;
    }

    Complex parseBitAnd() {
        Complex value = parseShift();
        while (isOperator(QStringLiteral("&"))) {
            ++m_index;
            const qint64 right = requireInteger(parseShift(), QStringLiteral("&"));
            value = double(requireInteger(value, QStringLiteral("&")) & right);
        }
        return value;
    }

    Complex parseShift() {
        Complex value = parseAdditive();
        while (isOperator(QStringLiteral("<<")) || isOperator(QStringLiteral(">>"))) {
            const bool left = next().text == QStringLiteral("<<");
            const qint64 amount = requireInteger(parseAdditive(), QStringLiteral("Shift"));
            if (amount < 0 || amount > 63)
                fail(QStringLiteral("Shift amount must be 0–63"));
            const qint64 operand = requireInteger(value, QStringLiteral("Shift"));
            value = double(left ? qint64(quint64(operand) << amount) : operand >> amount);
        }
        return value;
    }

    Complex parseAdditive() {
        Complex value = parseParallel();
        while (isOperator(QStringLiteral("+")) || isOperator(QStringLiteral("-"))) {
            const bool add = next().text == QStringLiteral("+");
            const Complex right = parseParallel();
            value = add ? value + right : value - right;
        }
        return value;
    }

    Complex parseParallel() {
        Complex value = parseMultiplicative();
        while (accept(QStringLiteral("∥")))
            value = parallel(value, parseMultiplicative());
        return value;
    }

    Complex parseMultiplicative() {
        Complex value = parsePolar();
        for (;;) {
            if (accept(QStringLiteral("*"))) {
                value *= parsePolar();
            } else if (accept(QStringLiteral("/"))) {
                value = divide(value, parsePolar());
            } else if (accept(QStringLiteral("%")) || (isWord(QStringLiteral("mod")) && ++m_index)) {
                const double right = requireReal(parsePolar(), QStringLiteral("mod"));
                value = flooredModulo(requireReal(value, QStringLiteral("mod")), right);
            } else {
                return value;
            }
        }
    }

    Complex parsePolar() {
        const Complex magnitude = parseImplicit();
        if (!accept(QStringLiteral("∠")))
            return magnitude;
        const Complex angle = parseImplicit();
        return magnitude * unitPhasor(requireReal(angle, QStringLiteral("∠")));
    }

    // Juxtaposition binds tighter than × and ÷, so 1/2π is 1/(2π).
    Complex parseImplicit() {
        Complex value = parseUnary();
        while (startsOperand()) {
            if (peek().kind == Token::Number && peek(-1).kind == Token::Number)
                fail(QStringLiteral("Missing operator between numbers"), true);
            value *= parsePower();
        }
        return value;
    }

    Complex parseUnary() {
        if (accept(QStringLiteral("-"))) {
            // Negate without minting a -0 imaginary part, which would flip
            // the branch cut: ln(-1) must be +πj.
            const Complex value = parseUnary();
            return Complex(-value.real(), value.imag() == 0 ? 0.0 : -value.imag());
        }
        if (accept(QStringLiteral("+")))
            return parseUnary();
        if (accept(QStringLiteral("~")))
            return double(~requireInteger(parseUnary(), QStringLiteral("~")));
        return parsePower();
    }

    Complex parsePower() {
        const Complex base = parsePostfix();
        if (accept(QStringLiteral("^")))
            return power(base, parseUnary());
        return base;
    }

    Complex parsePostfix() {
        Complex value = parsePrimary();
        for (;;) {
            if (accept(QStringLiteral("!"))) {
                value = factorial(requireReal(value, QStringLiteral("Factorial")));
            } else if (accept(QStringLiteral("°"))) {
                value *= m_engine.angleUnit == Engine::AngleUnit::Degrees ? 1.0 : pi / 180;
            } else if (accept(QStringLiteral("²"))) {
                value = power(value, 2);
            } else if (accept(QStringLiteral("³"))) {
                value = power(value, 3);
            } else if (accept(QStringLiteral("⁻¹"))) {
                value = divide(1, value);
            } else if (isOperator(QStringLiteral("%"))) {
                // "50%" is a percentage; "7 % 3" with an operand after is mod.
                ++m_index;
                if (startsOperand()) {
                    --m_index;
                    return value;
                }
                value /= 100.0;
            } else {
                return value;
            }
        }
    }

    Complex parsePrimary() {
        const Token &token = peek();
        if (token.kind == Token::Number) {
            ++m_index;
            return token.number;
        }
        if (accept(QStringLiteral("("))) {
            const Complex value = parseExpression();
            expect(QStringLiteral(")"));
            return value;
        }
        if (accept(QStringLiteral("√")))
            return squareRoot(parsePostfix());
        if (accept(QStringLiteral("∛")))
            return cubeRoot(parsePostfix());
        if (token.kind == Token::Identifier) {
            ++m_index;
            return identifier(token.text);
        }
        unexpected();
    }

    Complex identifier(const QString &name) {
        if (m_locals && m_locals->contains(name))
            return m_locals->value(name);

        const bool user = m_engine.functions.contains(name);
        const BuiltinInfo *builtin = user ? nullptr : findBuiltin(name);
        if (user || builtin) {
            QList<Complex> arguments;
            if (accept(QStringLiteral("("))) {
                if (!accept(QStringLiteral(")"))) {
                    do {
                        arguments << parseExpression();
                    } while (accept(QStringLiteral(",")));
                    expect(QStringLiteral(")"));
                }
            } else if (startsOperand()) {
                arguments << parsePower();  // sin 30, ln 2
            } else {
                fail(QStringLiteral("%1 needs an argument").arg(name), true);
            }
            return user ? callUser(name, arguments) : callBuiltin(*builtin, arguments);
        }

        if (name == QStringLiteral("ans"))
            return m_engine.ans;
        if (m_engine.variables.contains(name))
            return m_engine.variables.value(name);
        Complex constant;
        if (Engine::constantValue(name, &constant))
            return constant;
        if (isOperator(QStringLiteral("(")))
            fail(QStringLiteral("Unknown function '%1'").arg(name), true);
        fail(QStringLiteral("Unknown name '%1'").arg(name), true);
    }

    Complex callUser(const QString &name, const QList<Complex> &arguments) {
        const Engine::UserFunction function = m_engine.functions.value(name);
        if (arguments.size() != function.parameters.size())
            fail(QStringLiteral("%1 takes %2 argument%3").arg(name)
                     .arg(function.parameters.size())
                     .arg(function.parameters.size() == 1 ? QString() : QStringLiteral("s")));
        if (m_engine.m_callDepth >= maxCallDepth)
            fail(QStringLiteral("Too much recursion"));

        QMap<QString, Complex> locals;
        for (int k = 0; k < arguments.size(); ++k)
            locals.insert(function.parameters.at(k), arguments.at(k));

        ++m_engine.m_callDepth;
        struct DepthGuard {
            int &depth;
            ~DepthGuard() { --depth; }
        } guard{m_engine.m_callDepth};
        Parser body(m_engine, tokenize(function.body), &locals);
        return body.parseAll();
    }

    Complex toRadians(const Complex &angle) const {
        return angle * toRadiansFactor(m_engine.angleUnit);
    }
    Complex fromRadians(const Complex &angle) const {
        return angle / toRadiansFactor(m_engine.angleUnit);
    }

    double realTrig(double angle, int which) const {
        // which: 0 sin, 1 cos
        if (m_engine.angleUnit == Engine::AngleUnit::Degrees) {
            double exact = 0;
            if (exactTrig(angle, which == 1, &exact))
                return exact;
        }
        const double radians = angle * toRadiansFactor(m_engine.angleUnit);
        return snapTrig(which == 0 ? std::sin(radians) : std::cos(radians), radians);
    }

    Complex unitPhasor(double angle) const {
        return Complex(realTrig(angle, 1), realTrig(angle, 0));
    }

    Complex cubeRoot(const Complex &x) const {
        if (isReal(x))
            return std::cbrt(x.real());
        return std::pow(x, 1.0 / 3.0);
    }

    Complex callBuiltin(const BuiltinInfo &info, const QList<Complex> &args) {
        const QString name = QLatin1String(info.name);
        const int count = args.size();
        if (count < info.minArgs || (info.maxArgs >= 0 && count > info.maxArgs)) {
            QString expected;
            if (info.maxArgs < 0)
                expected = QStringLiteral("at least %1").arg(info.minArgs);
            else if (info.minArgs == info.maxArgs)
                expected = QString::number(info.minArgs);
            else
                expected = QStringLiteral("%1 or %2").arg(info.minArgs).arg(info.maxArgs);
            fail(QStringLiteral("%1 takes %2 argument%3").arg(name, expected,
                     info.maxArgs == 1 ? QString() : QStringLiteral("s")));
        }

        const Complex x = args.value(0);
        auto real = [&](int index) { return requireReal(args.at(index), name); };
        auto reals = [&]() {
            QList<double> values;
            for (int k = 0; k < count; ++k)
                values << real(k);
            return values;
        };

        // Trigonometry: real arguments stay real and exact where possible.
        if (name == QStringLiteral("sin") || name == QStringLiteral("cos")
                || name == QStringLiteral("tan") || name == QStringLiteral("sec")
                || name == QStringLiteral("csc") || name == QStringLiteral("cot")) {
            Complex s, c;
            if (isReal(x)) {
                s = realTrig(x.real(), 0);
                c = realTrig(x.real(), 1);
            } else {
                s = std::sin(toRadians(x));
                c = std::cos(toRadians(x));
            }
            if (name == QStringLiteral("sin")) return s;
            if (name == QStringLiteral("cos")) return c;
            if (name == QStringLiteral("tan")) {
                if (c == Complex(0)) fail(QStringLiteral("tan is undefined here"));
                return divide(s, c);
            }
            if (name == QStringLiteral("sec")) return divide(1, c);
            if (name == QStringLiteral("csc")) return divide(1, s);
            return divide(c, s);
        }
        if (name == QStringLiteral("asin") || name == QStringLiteral("acos")) {
            const bool sine = name == QStringLiteral("asin");
            if (isReal(x) && std::abs(x.real()) <= 1)
                return fromRadians(sine ? std::asin(x.real()) : std::acos(x.real()));
            return fromRadians(sine ? std::asin(x) : std::acos(x));
        }
        if (name == QStringLiteral("atan"))
            return fromRadians(isReal(x) ? Complex(std::atan(x.real())) : std::atan(x));
        if (name == QStringLiteral("atan2"))
            return fromRadians(std::atan2(real(0), real(1)));
        if (name == QStringLiteral("sinh"))
            return isReal(x) ? Complex(std::sinh(x.real())) : std::sinh(x);
        if (name == QStringLiteral("cosh"))
            return isReal(x) ? Complex(std::cosh(x.real())) : std::cosh(x);
        if (name == QStringLiteral("tanh"))
            return isReal(x) ? Complex(std::tanh(x.real())) : std::tanh(x);
        if (name == QStringLiteral("asinh"))
            return isReal(x) ? Complex(std::asinh(x.real())) : std::asinh(x);
        if (name == QStringLiteral("acosh"))
            return isReal(x) && x.real() >= 1 ? Complex(std::acosh(x.real())) : std::acosh(x);
        if (name == QStringLiteral("atanh")) {
            if (isReal(x) && std::abs(x.real()) == 1)
                fail(QStringLiteral("atanh is undefined at ±1"));
            return isReal(x) && std::abs(x.real()) < 1 ? Complex(std::atanh(x.real())) : std::atanh(x);
        }

        if (name == QStringLiteral("sqrt")) return squareRoot(x);
        if (name == QStringLiteral("cbrt")) return cubeRoot(x);
        if (name == QStringLiteral("root")) {
            const Complex n = args.at(1);
            if (n == Complex(0)) fail(QStringLiteral("Zeroth root"));
            if (isReal(x) && isReal(n) && x.real() < 0 && n.real() == std::floor(n.real())
                    && std::fmod(std::abs(n.real()), 2) == 1)
                return -std::pow(-x.real(), 1 / n.real());
            return power(x, divide(1, n));
        }
        if (name == QStringLiteral("exp"))
            return isReal(x) ? Complex(std::exp(x.real())) : std::exp(x);
        if (name == QStringLiteral("ln")) return logarithm(x);
        if (name == QStringLiteral("log")) {
            if (count == 2) return divide(logarithm(x), logarithm(args.at(1)));
            return divide(logarithm(x), std::log(10.0));
        }
        if (name == QStringLiteral("log10")) {
            if (isReal(x) && x.real() > 0) return std::log10(x.real());
            return divide(logarithm(x), std::log(10.0));
        }
        if (name == QStringLiteral("log2")) {
            if (isReal(x) && x.real() > 0) return std::log2(x.real());
            return divide(logarithm(x), std::log(2.0));
        }

        if (name == QStringLiteral("abs")) return std::abs(x);
        if (name == QStringLiteral("arg")) {
            if (x == Complex(0)) return 0;
            return fromRadians(std::arg(x));
        }
        if (name == QStringLiteral("re")) return x.real();
        if (name == QStringLiteral("im")) return x.imag();
        if (name == QStringLiteral("conj")) return std::conj(x);
        if (name == QStringLiteral("polar")) return real(0) * unitPhasor(real(1));
        if (name == QStringLiteral("todeg")) return real(0) * 180 / pi;
        if (name == QStringLiteral("torad")) return real(0) * pi / 180;

        if (name == QStringLiteral("floor")) return std::floor(real(0));
        if (name == QStringLiteral("ceil")) return std::ceil(real(0));
        if (name == QStringLiteral("trunc")) return std::trunc(real(0));
        if (name == QStringLiteral("frac")) return real(0) - std::trunc(real(0));
        if (name == QStringLiteral("sign")) return double((real(0) > 0) - (real(0) < 0));
        if (name == QStringLiteral("round")) {
            if (count == 1) return std::round(real(0));
            const double scale = std::pow(10.0, double(requireInteger(args.at(1), name)));
            return std::round(real(0) * scale) / scale;
        }

        if (name == QStringLiteral("min")) {
            const QList<double> values = reals();
            return *std::min_element(values.begin(), values.end());
        }
        if (name == QStringLiteral("max")) {
            const QList<double> values = reals();
            return *std::max_element(values.begin(), values.end());
        }
        if (name == QStringLiteral("sum") || name == QStringLiteral("mean")) {
            Complex total = 0;
            for (const Complex &value : args)
                total += value;
            return name == QStringLiteral("sum") ? total : total / double(count);
        }
        if (name == QStringLiteral("median")) {
            QList<double> values = reals();
            std::sort(values.begin(), values.end());
            const int middle = count / 2;
            return count % 2 ? values.at(middle) : (values.at(middle - 1) + values.at(middle)) / 2;
        }
        if (name == QStringLiteral("stdev") || name == QStringLiteral("var")) {
            const QList<double> values = reals();
            double mean = 0;
            for (double value : values) mean += value;
            mean /= count;
            double squares = 0;
            for (double value : values) squares += (value - mean) * (value - mean);
            const double variance = squares / (count - 1);
            return name == QStringLiteral("var") ? variance : std::sqrt(variance);
        }
        if (name == QStringLiteral("hypot")) {
            double squares = 0;
            for (const Complex &value : args) squares += std::norm(value);
            return std::sqrt(squares);
        }
        if (name == QStringLiteral("gcd") || name == QStringLiteral("lcm")) {
            qint64 result = requireInteger(args.at(0), name);
            for (int k = 1; k < count; ++k) {
                const qint64 value = requireInteger(args.at(k), name);
                if (name == QStringLiteral("gcd")) {
                    result = greatestCommonDivisor(result, value);
                } else {
                    const qint64 divisor = greatestCommonDivisor(result, value);
                    result = divisor ? std::llabs(result / divisor * value) : 0;
                }
            }
            return double(result);
        }
        if (name == QStringLiteral("nCr")) return combinations(real(0), real(1), false);
        if (name == QStringLiteral("nPr")) return combinations(real(0), real(1), true);
        if (name == QStringLiteral("gamma")) {
            const double value = real(0);
            if (value <= 0 && value == std::floor(value))
                fail(QStringLiteral("gamma is undefined at non-positive integers"));
            return std::tgamma(value);
        }
        if (name == QStringLiteral("par")) {
            Complex total = args.at(0);
            for (int k = 1; k < count; ++k)
                total = parallel(total, args.at(k));
            return total;
        }
        if (name == QStringLiteral("db") || name == QStringLiteral("dbp")) {
            const double magnitude = std::abs(x);
            if (magnitude == 0) fail(QStringLiteral("Logarithm of zero"));
            return (name == QStringLiteral("db") ? 20 : 10) * std::log10(magnitude);
        }
        if (name == QStringLiteral("undb")) return std::pow(10.0, real(0) / 20);
        if (name == QStringLiteral("undbp")) return std::pow(10.0, real(0) / 10);

        fail(QStringLiteral("Unknown function '%1'").arg(name), true);
    }

    Engine &m_engine;
    QList<Token> m_tokens;
    const QMap<QString, Complex> *m_locals;
    int m_index = 0;
};

Engine::Result Engine::evaluate(const QString &input, bool commit) {
    Result result;
    try {
        const QList<Token> tokens = tokenize(input);
        if (tokens.size() == 1)
            return result;  // Nothing typed; no error to report either.

        auto isOp = [&](int index, const char *text) {
            return index < tokens.size() && tokens.at(index).kind == Token::Operator
                && tokens.at(index).text == QLatin1String(text);
        };
        const Token &first = tokens.first();

        // f(x, y) = body
        if (first.kind == Token::Identifier && isOp(1, "(")) {
            QStringList parameters;
            int index = 2;
            bool shaped = true;
            if (!isOp(index, ")")) {
                for (;;) {
                    if (tokens.at(index).kind != Token::Identifier) { shaped = false; break; }
                    parameters << tokens.at(index++).text;
                    if (isOp(index, ",")) { ++index; continue; }
                    break;
                }
            }
            if (shaped && isOp(index, ")") && isOp(index + 1, "=")) {
                const QString name = first.text;
                if (isReserved(name))
                    fail(QStringLiteral("'%1' is built in").arg(name), true);
                if (parameters.removeDuplicates() > 0)
                    fail(QStringLiteral("Repeated parameter name"), true);
                const int bodyStart = tokens.at(index + 2).position;
                const QString body = input.mid(bodyStart).trimmed();
                if (body.isEmpty())
                    fail(QStringLiteral("Function body is empty"), true);

                // Trial-run the body so typos surface now rather than at the
                // first call. Math errors at the trial point don't count.
                const QMap<QString, UserFunction> savedFunctions = functions;
                functions.insert(name, UserFunction{parameters, body});
                try {
                    QMap<QString, Complex> locals;
                    for (const QString &parameter : parameters)
                        locals.insert(parameter, 1.0);
                    Parser trial(*this, tokenize(body), &locals);
                    trial.parseAll();
                } catch (const EvalError &error) {
                    if (error.syntax) {
                        functions = savedFunctions;
                        throw;
                    }
                }
                if (commit)
                    variables.remove(name);
                else
                    functions = savedFunctions;

                result.ok = true;
                result.definedFunction = true;
                result.assignedName = name;
                return result;
            }
        }

        // name = value
        int start = 0;
        if (first.kind == Token::Identifier && isOp(1, "=")) {
            if (isReserved(first.text) || first.text == QStringLiteral("ans"))
                fail(QStringLiteral("'%1' is built in").arg(first.text), true);
            result.assignedName = first.text;
            start = 2;
        }

        Parser parser(*this, tokens.mid(start), nullptr);
        const Complex value = parser.parseAll();
        if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
            fail(std::isnan(value.real()) || std::isnan(value.imag())
                     ? QStringLiteral("Undefined") : QStringLiteral("Overflow"));
        }

        result.ok = true;
        result.value = Complex(value.real() == 0 ? 0.0 : value.real(),
                               value.imag() == 0 ? 0.0 : value.imag());
        if (commit) {
            ans = result.value;
            if (!result.assignedName.isEmpty()) {
                variables.insert(result.assignedName, result.value);
                functions.remove(result.assignedName);
            }
        }
    } catch (const EvalError &error) {
        result = Result();
        result.error = error.message;
    }
    return result;
}

bool Engine::isReserved(const QString &name) {
    for (const ConstantInfo &info : constants) {
        if (info.fixed && name == QString::fromUtf8(info.name))
            return true;
    }
    return isBuiltinFunction(name) || name == QStringLiteral("ans")
        || name == QStringLiteral("mod") || name == QStringLiteral("xor");
}

bool Engine::isConstant(const QString &name) {
    Complex unused;
    return constantValue(name, &unused);
}

bool Engine::constantValue(const QString &name, Complex *value) {
    for (const ConstantInfo &info : constants) {
        if (name == QString::fromUtf8(info.name)) {
            *value = Complex(info.real, info.imaginary);
            return true;
        }
    }
    return false;
}

bool Engine::isBuiltinFunction(const QString &name) {
    return findBuiltin(name) != nullptr;
}

QList<QPair<QString, QString>> Engine::functionReference() {
    QList<QPair<QString, QString>> list;
    for (const BuiltinInfo &info : builtins)
        list << qMakePair(QString::fromUtf8(info.name), QString::fromUtf8(info.help));
    return list;
}

QList<QPair<QString, QString>> Engine::constantReference() {
    QList<QPair<QString, QString>> list;
    for (const ConstantInfo &info : constants) {
        const QString name = QString::fromUtf8(info.name);
        if (name == QStringLiteral("ε0") || name == QStringLiteral("μ0"))
            continue;  // Shown under their ASCII spellings.
        list << qMakePair(name, QString::fromUtf8(info.help));
    }
    return list;
}

namespace Format {
namespace {
QString tidyExponent(QString text) {
    // 1e+15 → 1e15, 1e-07 → 1e-7
    static const QRegularExpression exponent(QStringLiteral("e\\+?(-?)0*(\\d)"));
    return text.replace(exponent, QStringLiteral("e\\1\\2"));
}

QString trimMantissa(QString mantissa) {
    if (mantissa.contains(QLatin1Char('.'))) {
        while (mantissa.endsWith(QLatin1Char('0')))
            mantissa.chop(1);
        if (mantissa.endsWith(QLatin1Char('.')))
            mantissa.chop(1);
    }
    return mantissa;
}

// Fifteen significant digits keeps binary-float noise like
// 0.1 + 0.2 = 0.30000000000000004 out of the display.
constexpr int digits = 15;

void clean(double *re, double *im) {
    const double scale = std::max(std::abs(*re), std::abs(*im));
    if (std::abs(*im) <= 1e-13 * scale) *im = 0;
    if (std::abs(*re) <= 1e-13 * scale) *re = 0;
}
}

QString real(double value, Notation notation) {
    if (value == 0)
        return QStringLiteral("0");
    if (std::isnan(value))
        return QStringLiteral("nan");
    if (std::isinf(value))
        return value > 0 ? QStringLiteral("inf") : QStringLiteral("-inf");

    if (notation == Notation::Normal)
        return tidyExponent(QString::number(value, 'g', digits));

    if (notation == Notation::Scientific) {
        const QString text = QString::number(value, 'e', digits - 1);
        const int e = text.indexOf(QLatin1Char('e'));
        return trimMantissa(text.left(e)) + QLatin1Char('e')
            + QString::number(text.mid(e + 1).toInt());
    }

    // Engineering: exponent a multiple of three, shown as an SI prefix
    // where one exists.
    int exponent = int(std::floor(std::log10(std::abs(value)) / 3)) * 3;
    double mantissa = QString::number(value / std::pow(10.0, exponent), 'g', digits).toDouble();
    if (std::abs(mantissa) >= 1000) {
        exponent += 3;
        mantissa = QString::number(value / std::pow(10.0, exponent), 'g', digits).toDouble();
    } else if (std::abs(mantissa) < 1) {
        exponent -= 3;
        mantissa = QString::number(value / std::pow(10.0, exponent), 'g', digits).toDouble();
    }
    const QString mantissaText = QString::number(mantissa, 'g', digits);
    static const char *prefixes[] = {"f", "p", "n", "µ", "m", "", "k", "M", "G", "T"};
    if (exponent >= -15 && exponent <= 12)
        return mantissaText + QString::fromUtf8(prefixes[(exponent + 15) / 3]);
    return mantissaText + QLatin1Char('e') + QString::number(exponent);
}

QString integer(const Complex &value, Base base) {
    double re = value.real(), im = value.imag();
    clean(&re, &im);
    if (im != 0 || re != std::floor(re) || std::abs(re) >= 9.2e18)
        return QString();

    const bool negative = re < 0;
    const quint64 magnitude = quint64(std::abs(re));
    QString text;
    switch (base) {
    case Base::Decimal:
        text = QString::number(magnitude);
        break;
    case Base::Hexadecimal:
        text = QStringLiteral("0x") + QString::number(magnitude, 16).toUpper();
        break;
    case Base::Octal:
        text = QStringLiteral("0o") + QString::number(magnitude, 8);
        break;
    case Base::Binary: {
        QString bits = QString::number(magnitude, 2);
        for (int k = bits.size() - 4; k > 0; k -= 4)
            bits.insert(k, QLatin1Char('_'));
        text = QStringLiteral("0b") + bits;
        break;
    }
    }
    return negative ? QLatin1Char('-') + text : text;
}

QString value(const Complex &number, const Options &options) {
    double re = number.real(), im = number.imag();
    clean(&re, &im);

    if (im == 0) {
        if (options.base != Base::Decimal) {
            const QString text = integer(re, options.base);
            if (!text.isEmpty())
                return text;
        }
        return real(re, options.notation);
    }

    if (options.polar) {
        const bool degrees = options.angleUnit == Engine::AngleUnit::Degrees;
        const double angle = std::atan2(im, re) * (degrees ? 180 / pi : 1);
        return real(std::hypot(re, im), options.notation) + QStringLiteral(" ∠ ")
            + real(angle, Notation::Normal) + (degrees ? QStringLiteral("°") : QString());
    }

    // The j trails the number; after an SI prefix a dot keeps "2.2k·j" from
    // reading back as a name "kj".
    auto imaginary = [&](double magnitude) {
        if (magnitude == 1)
            return QStringLiteral("j");
        const QString text = real(magnitude, options.notation);
        return text.back().isLetter() ? text + QStringLiteral("·j") : text + QLatin1Char('j');
    };
    if (re == 0)
        return (im < 0 ? QStringLiteral("-") : QString()) + imaginary(std::abs(im));
    return real(re, options.notation) + (im < 0 ? QStringLiteral(" - ") : QStringLiteral(" + "))
        + imaginary(std::abs(im));
}

QString alternate(const Complex &number, const Options &options) {
    double re = number.real(), im = number.imag();
    clean(&re, &im);

    if (im != 0) {
        Options flipped = options;
        flipped.polar = !options.polar;
        return value(number, flipped);
    }

    if (re == std::floor(re) && std::abs(re) < 9007199254740992.0) {
        if (options.base != Base::Decimal)
            return integer(re, Base::Decimal);
        if (std::abs(re) < 10)
            return QString();
        QString text = integer(re, Base::Hexadecimal);
        if (std::abs(re) < 65536)
            text += QStringLiteral("   ") + integer(re, Base::Binary);
        return text;
    }

    const QString main = value(number, options);
    const QString other = real(re, options.notation == Notation::Engineering
                                       ? Notation::Normal : Notation::Engineering);
    return other == main ? QString() : other;
}
}
