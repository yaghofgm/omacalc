#include "backend.h"

#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QRect>
#include <QSettings>
#include <QTextStream>
#include <QVariantMap>
#include <cmath>

namespace {
const auto windowGeometrySetting = QStringLiteral("window/geometry");
constexpr int historyLimit = 200;

QString complexToString(const Complex &value) {
    return QString::number(value.real(), 'g', 17) + QLatin1Char(',')
        + QString::number(value.imag(), 'g', 17);
}

Complex complexFromString(const QString &text) {
    const QStringList parts = text.split(QLatin1Char(','));
    return Complex(parts.value(0).toDouble(), parts.value(1).toDouble());
}
}

Backend::Backend(QObject *parent) : QObject(parent) {
    loadOmarchyTheme();
    watchOmarchyTheme();
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
    loadState();
    refresh();
}

void Backend::setInput(const QString &input) {
    if (m_input == input)
        return;
    m_input = input;
    refresh();
}

// Recomputes everything derived from the input: while typing the display
// previews the result without committing it; with no input it shows ans.
void Backend::refresh() {
    m_status.clear();
    m_alternate.clear();

    if (m_input.trimmed().isEmpty()) {
        m_lastPreview.clear();
        m_displayIsPreview = false;
        m_display = Format::value(m_engine.ans, m_options);
        if (!m_history.isEmpty() && m_history.last().definedFunction.isEmpty())
            m_alternate = Format::alternate(m_engine.ans, m_options);
        emit calculationChanged();
        return;
    }

    m_displayIsPreview = true;
    const Engine::Result result = m_engine.evaluate(m_input, false);
    if (result.ok && result.definedFunction) {
        m_display = m_lastPreview = result.assignedName + QStringLiteral("(…)");
        m_status = QStringLiteral("Enter defines %1").arg(result.assignedName);
    } else if (result.ok) {
        m_display = m_lastPreview = Format::value(result.value, m_options);
        m_alternate = Format::alternate(result.value, m_options);
        if (!result.assignedName.isEmpty())
            m_status = QStringLiteral("Enter stores %1").arg(result.assignedName);
    } else {
        // Keep the last good preview on screen while the expression is
        // momentarily incomplete, and say what is wrong underneath.
        m_display = m_lastPreview;
        m_status = result.error;
    }
    emit calculationChanged();
}

bool Backend::evaluate() {
    if (m_input.trimmed().isEmpty())
        return false;

    const Engine::Result result = m_engine.evaluate(m_input, true);
    if (!result.ok) {
        m_status = result.error.isEmpty() ? QStringLiteral("Nothing to calculate") : result.error;
        emit calculationChanged();
        return false;
    }

    HistoryEntry entry;
    entry.expression = m_input.trimmed();
    // The engine closes dangling parentheses; show them closed in history.
    const int unclosed = entry.expression.count(QLatin1Char('('))
        - entry.expression.count(QLatin1Char(')'));
    if (unclosed > 0)
        entry.expression += QString(unclosed, QLatin1Char(')'));
    entry.value = result.value;
    if (result.definedFunction)
        entry.definedFunction = result.assignedName;
    m_history << entry;
    while (m_history.size() > historyLimit)
        m_history.removeFirst();

    m_input.clear();
    m_lastPreview.clear();
    refresh();
    if (result.definedFunction)
        m_display = QStringLiteral("Defined %1").arg(result.assignedName);
    emit calculationChanged();
    emit historyChanged();
    saveState();
    return true;
}

QString Backend::formatEntry(const HistoryEntry &entry) const {
    if (!entry.definedFunction.isEmpty())
        return QStringLiteral("defined");
    return Format::value(entry.value, m_options);
}

QVariantList Backend::history() const {
    QVariantList list;
    list.reserve(m_history.size());
    for (const HistoryEntry &entry : m_history) {
        QVariantMap map;
        map.insert(QStringLiteral("expression"), entry.expression);
        map.insert(QStringLiteral("result"), formatEntry(entry));
        list << map;
    }
    return list;
}

QVariantList Backend::userDefinitions() const {
    QVariantList list;
    for (auto it = m_engine.variables.cbegin(); it != m_engine.variables.cend(); ++it) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), it.key());
        map.insert(QStringLiteral("description"), Format::value(it.value(), m_options));
        list << map;
    }
    for (auto it = m_engine.functions.cbegin(); it != m_engine.functions.cend(); ++it) {
        QVariantMap map;
        map.insert(QStringLiteral("name"),
                   it.key() + QLatin1Char('(') + it.value().parameters.join(QStringLiteral(", "))
                       + QLatin1Char(')'));
        map.insert(QStringLiteral("description"), it.value().body);
        list << map;
    }
    return list;
}

QVariantList Backend::functionReference() const {
    QVariantList list;
    for (const auto &item : Engine::functionReference()) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), item.first);
        map.insert(QStringLiteral("description"), item.second);
        list << map;
    }
    return list;
}

QVariantList Backend::constantReference() const {
    QVariantList list;
    Format::Options options;
    for (const auto &item : Engine::constantReference()) {
        Complex value;
        Engine::constantValue(item.first, &value);
        QVariantMap map;
        map.insert(QStringLiteral("name"), item.first);
        map.insert(QStringLiteral("description"), item.second);
        map.insert(QStringLiteral("value"), Format::value(value, options));
        list << map;
    }
    return list;
}

void Backend::clearHistory() {
    m_history.clear();
    m_engine.ans = 0;
    refresh();
    emit historyChanged();
    saveState();
}

void Backend::forgetDefinitions() {
    m_engine.variables.clear();
    m_engine.functions.clear();
    refresh();
    emit historyChanged();
    saveState();
}

QString Backend::angleMode() const {
    return m_engine.angleUnit == Engine::AngleUnit::Degrees ? QStringLiteral("deg")
                                                            : QStringLiteral("rad");
}

void Backend::setAngleMode(const QString &mode) {
    const auto unit = mode == QStringLiteral("deg") ? Engine::AngleUnit::Degrees
                                                    : Engine::AngleUnit::Radians;
    if (unit == m_engine.angleUnit)
        return;
    m_engine.angleUnit = unit;
    m_options.angleUnit = unit;
    saveModes();
    emit modesChanged();
    emit historyChanged();
    refresh();
}

QString Backend::notation() const {
    switch (m_options.notation) {
    case Format::Notation::Scientific: return QStringLiteral("sci");
    case Format::Notation::Engineering: return QStringLiteral("eng");
    default: return QStringLiteral("norm");
    }
}

void Backend::setNotation(const QString &notation) {
    Format::Notation value = Format::Notation::Normal;
    if (notation == QStringLiteral("sci"))
        value = Format::Notation::Scientific;
    else if (notation == QStringLiteral("eng"))
        value = Format::Notation::Engineering;
    if (value == m_options.notation)
        return;
    m_options.notation = value;
    saveModes();
    emit modesChanged();
    emit historyChanged();
    refresh();
}

QString Backend::base() const {
    switch (m_options.base) {
    case Format::Base::Hexadecimal: return QStringLiteral("hex");
    case Format::Base::Binary: return QStringLiteral("bin");
    case Format::Base::Octal: return QStringLiteral("oct");
    default: return QStringLiteral("dec");
    }
}

void Backend::setBase(const QString &base) {
    Format::Base value = Format::Base::Decimal;
    if (base == QStringLiteral("hex"))
        value = Format::Base::Hexadecimal;
    else if (base == QStringLiteral("bin"))
        value = Format::Base::Binary;
    else if (base == QStringLiteral("oct"))
        value = Format::Base::Octal;
    if (value == m_options.base)
        return;
    m_options.base = value;
    saveModes();
    emit modesChanged();
    emit historyChanged();
    refresh();
}

void Backend::setPolar(bool polar) {
    if (polar == m_options.polar)
        return;
    m_options.polar = polar;
    saveModes();
    emit modesChanged();
    emit historyChanged();
    refresh();
}

void Backend::copyResult() const {
    copyText(m_display);
}

void Backend::copyText(const QString &text) const {
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

void Backend::loadState() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("calculator"));
    const bool degrees = settings.value(QStringLiteral("angle")).toString() == QStringLiteral("deg");
    m_engine.angleUnit = degrees ? Engine::AngleUnit::Degrees : Engine::AngleUnit::Radians;
    m_options.angleUnit = m_engine.angleUnit;
    const QString notation = settings.value(QStringLiteral("notation")).toString();
    m_options.notation = notation == QStringLiteral("sci") ? Format::Notation::Scientific
        : notation == QStringLiteral("eng") ? Format::Notation::Engineering
                                            : Format::Notation::Normal;
    const QString base = settings.value(QStringLiteral("base")).toString();
    m_options.base = base == QStringLiteral("hex") ? Format::Base::Hexadecimal
        : base == QStringLiteral("bin") ? Format::Base::Binary
        : base == QStringLiteral("oct") ? Format::Base::Octal
                                        : Format::Base::Decimal;
    m_options.polar = settings.value(QStringLiteral("polar"), false).toBool();
    m_engine.ans = complexFromString(settings.value(QStringLiteral("ans")).toString());

    const QVariantMap variables = settings.value(QStringLiteral("variables")).toMap();
    for (auto it = variables.cbegin(); it != variables.cend(); ++it) {
        if (!Engine::isReserved(it.key()))
            m_engine.variables.insert(it.key(), complexFromString(it.value().toString()));
    }
    const QVariantMap functions = settings.value(QStringLiteral("functions")).toMap();
    for (auto it = functions.cbegin(); it != functions.cend(); ++it) {
        const QStringList parts = it.value().toStringList();
        if (parts.size() != 2 || Engine::isReserved(it.key()))
            continue;
        Engine::UserFunction function;
        function.parameters = parts.at(0).split(QLatin1Char(','), Qt::SkipEmptyParts);
        function.body = parts.at(1);
        m_engine.functions.insert(it.key(), function);
    }

    const int count = settings.beginReadArray(QStringLiteral("history"));
    for (int k = 0; k < count; ++k) {
        settings.setArrayIndex(k);
        HistoryEntry entry;
        entry.expression = settings.value(QStringLiteral("expression")).toString();
        entry.value = complexFromString(settings.value(QStringLiteral("value")).toString());
        entry.definedFunction = settings.value(QStringLiteral("defined")).toString();
        if (!entry.expression.isEmpty())
            m_history << entry;
    }
    settings.endArray();
}

void Backend::saveModes() const {
    QSettings settings;
    settings.beginGroup(QStringLiteral("calculator"));
    settings.setValue(QStringLiteral("angle"), angleMode());
    settings.setValue(QStringLiteral("notation"), notation());
    settings.setValue(QStringLiteral("base"), base());
    settings.setValue(QStringLiteral("polar"), m_options.polar);
}

void Backend::saveState() const {
    saveModes();
    QSettings settings;
    settings.beginGroup(QStringLiteral("calculator"));
    settings.setValue(QStringLiteral("ans"), complexToString(m_engine.ans));

    QVariantMap variables;
    for (auto it = m_engine.variables.cbegin(); it != m_engine.variables.cend(); ++it)
        variables.insert(it.key(), complexToString(it.value()));
    settings.setValue(QStringLiteral("variables"), variables);

    QVariantMap functions;
    for (auto it = m_engine.functions.cbegin(); it != m_engine.functions.cend(); ++it) {
        functions.insert(it.key(), QStringList{it.value().parameters.join(QLatin1Char(',')),
                                               it.value().body});
    }
    settings.setValue(QStringLiteral("functions"), functions);

    settings.remove(QStringLiteral("history"));
    settings.beginWriteArray(QStringLiteral("history"), m_history.size());
    for (int k = 0; k < m_history.size(); ++k) {
        settings.setArrayIndex(k);
        settings.setValue(QStringLiteral("expression"), m_history.at(k).expression);
        settings.setValue(QStringLiteral("value"), complexToString(m_history.at(k).value));
        if (!m_history.at(k).definedFunction.isEmpty())
            settings.setValue(QStringLiteral("defined"), m_history.at(k).definedFunction);
    }
    settings.endArray();
}

QVariantMap Backend::windowGeometry() const {
    const QSettings settings;
    const QRect geometry = settings.value(windowGeometrySetting).toRect();
    QVariantMap map;
    // Positions can legitimately be negative on monitors left of or above the
    // primary, so validity travels separately instead of being encoded as -1.
    map.insert(QStringLiteral("valid"), geometry.isValid());
    map.insert(QStringLiteral("x"), geometry.x());
    map.insert(QStringLiteral("y"), geometry.y());
    map.insert(QStringLiteral("width"), geometry.width());
    map.insert(QStringLiteral("height"), geometry.height());
    map.insert(QStringLiteral("maximized"),
               settings.value(QStringLiteral("window/maximized"), false).toBool());
    return map;
}

void Backend::saveWindowGeometry(int x, int y, int width, int height, bool maximized) {
    QSettings settings;
    settings.setValue(windowGeometrySetting, QRect(x, y, width, height));
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}

void Backend::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    loadOmarchyTheme();
    emit darkModeChanged();
}

void Backend::setTextScale(qreal textScale) {
    if (qFuzzyCompare(m_textScale, textScale))
        return;

    m_textScale = textScale;
    emit textScaleChanged();
}

void Backend::loadOmarchyTheme() {
    m_themeBackground = m_darkMode ? QStringLiteral("#101010") : QStringLiteral("#ffffff");
    m_themeForeground = m_darkMode ? QStringLiteral("#eeeeee") : QStringLiteral("#222324");
    m_themeAccent = m_darkMode ? QStringLiteral("#5584aa") : QStringLiteral("#2077b2");
    m_themeSelection = m_darkMode ? QStringLiteral("#186a9a") : QStringLiteral("#2077b2");

    const QString colorsPath = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    QString themeMode;
    QFile file(colorsPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;

            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0)
                continue;

            const QString key = line.left(equals).trimmed();
            QString value = line.mid(equals + 1).trimmed();
            if (value.size() >= 2
                    && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                        || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
                value = value.mid(1, value.size() - 2);

            if (key == QStringLiteral("mode"))
                themeMode = value;
            else if (key == QStringLiteral("background"))
                m_themeBackground = value;
            else if (key == QStringLiteral("foreground"))
                m_themeForeground = value;
            else if (key == QStringLiteral("accent"))
                m_themeAccent = value;
            else if (key == QStringLiteral("selection"))
                m_themeSelection = value;
        }
    }

    bool themeModeKnown = false;
    bool themeIsDark = m_darkMode;
    if (themeMode == QStringLiteral("dark")) {
        themeIsDark = true;
        themeModeKnown = true;
    } else if (themeMode == QStringLiteral("light")) {
        themeIsDark = false;
        themeModeKnown = true;
    } else {
        const QColor background(m_themeBackground);
        if (background.isValid()) {
            const double luminance = 0.299 * background.redF()
                + 0.587 * background.greenF() + 0.114 * background.blueF();
            themeIsDark = luminance < 0.5;
            themeModeKnown = true;
        }
    }
    if (themeModeKnown && themeIsDark != m_darkMode) {
        m_darkMode = themeIsDark;
        emit darkModeChanged();
    }

    emit themeColorsChanged();
}

void Backend::watchOmarchyTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);

    const QString currentDir = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current");
    const QString themeDir = currentDir + QStringLiteral("/theme");
    const QString colorsPath = themeDir + QStringLiteral("/colors.toml");

    if (QDir(currentDir).exists())
        m_themeWatcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_themeWatcher.addPath(themeDir);
    if (QFile::exists(colorsPath))
        m_themeWatcher.addPath(colorsPath);
}
