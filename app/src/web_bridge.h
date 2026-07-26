#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class QWidget;

namespace milah {

class WebBridge final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit WebBridge(QWidget *dialogParent);

    QString lastError() const;

    Q_INVOKABLE QVariantList openOsisFiles(const QString &role);
    Q_INVOKABLE QVariantMap openProject();
    Q_INVOKABLE bool saveProject(const QVariantMap &payload);
    Q_INVOKABLE bool exportCombinedOsis(const QString &osis);

signals:
    void lastErrorChanged();

private:
    void setLastError(const QString &message);

    QWidget *m_dialogParent;
    QString m_lastError;
};

} // namespace milah
