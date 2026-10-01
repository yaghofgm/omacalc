#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "engine.h"

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString input READ input WRITE setInput NOTIFY calculationChanged)
    Q_PROPERTY(QString display READ display NOTIFY calculationChanged)
    Q_PROPERTY(bool displayIsPreview READ displayIsPreview NOTIFY calculationChanged)
    Q_PROPERTY(QString alternate READ alternate NOTIFY calculationChanged)
    Q_PROPERTY(QString status READ status NOTIFY calculationChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QVariantList userDefinitions READ userDefinitions NOTIFY historyChanged)
    Q_PROPERTY(QVariantList functionReference READ functionReference CONSTANT)
    Q_PROPERTY(QVariantList constantReference READ constantReference CONSTANT)

    Q_PROPERTY(QString angleMode READ angleMode WRITE setAngleMode NOTIFY modesChanged)
    Q_PROPERTY(QString notation READ notation WRITE setNotation NOTIFY modesChanged)
    Q_PROPERTY(QString base READ base WRITE setBase NOTIFY modesChanged)
    Q_PROPERTY(bool polar READ polar WRITE setPolar NOTIFY modesChanged)

    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY textScaleChanged)
    Q_PROPERTY(QString themeBackground READ themeBackground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeForeground READ themeForeground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeAccent READ themeAccent NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeSelection READ themeSelection NOTIFY themeColorsChanged)

public:
    explicit Backend(QObject *parent = nullptr);

    QString input() const { return m_input; }
    void setInput(const QString &input);
    QString display() const { return m_display; }
    bool displayIsPreview() const { return m_displayIsPreview; }
    QString alternate() const { return m_alternate; }
    QString status() const { return m_status; }
    QVariantList history() const;
    QVariantList userDefinitions() const;
    QVariantList functionReference() const;
    QVariantList constantReference() const;

    QString angleMode() const;
    void setAngleMode(const QString &mode);
    QString notation() const;
    void setNotation(const QString &notation);
    QString base() const;
    void setBase(const QString &base);
    bool polar() const { return m_options.polar; }
    void setPolar(bool polar);

    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool darkMode);
    qreal textScale() const { return m_textScale; }
    void setTextScale(qreal textScale);
    QString themeBackground() const { return m_themeBackground; }
    QString themeForeground() const { return m_themeForeground; }
    QString themeAccent() const { return m_themeAccent; }
    QString themeSelection() const { return m_themeSelection; }

    // Commits the current input: on success it joins the history, becomes
    // ans, and the input clears. Returns whether it succeeded.
    Q_INVOKABLE bool evaluate();
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void forgetDefinitions();
    Q_INVOKABLE void copyResult() const;
    Q_INVOKABLE void copyText(const QString &text) const;
    Q_INVOKABLE QVariantMap windowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(int x, int y, int width, int height, bool maximized);

signals:
    void calculationChanged();
    void historyChanged();
    void modesChanged();
    void darkModeChanged();
    void textScaleChanged();
    void themeColorsChanged();

private:
    struct HistoryEntry {
        QString expression;
        Complex value;
        QString definedFunction;  // non-empty for "f(x) = …" lines
    };

    void refresh();
    QString formatEntry(const HistoryEntry &entry) const;
    void loadState();
    void saveState() const;
    void saveModes() const;
    void loadOmarchyTheme();
    void watchOmarchyTheme();

    Engine m_engine;
    Format::Options m_options;
    QList<HistoryEntry> m_history;
    QString m_input;
    QString m_display;
    bool m_displayIsPreview = false;
    QString m_alternate;
    QString m_status;
    QString m_lastPreview;

    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    QString m_themeBackground;
    QString m_themeForeground;
    QString m_themeAccent;
    QString m_themeSelection;
    QFileSystemWatcher m_themeWatcher;
};
