#pragma once

#include "core/types.h"

#include <QJsonObject>
#include <QString>

#include <stdexcept>

namespace milah {

class ProjectError : public std::runtime_error
{
public:
    explicit ProjectError(const QString &message)
        : std::runtime_error(message.toStdString())
        , m_message(message)
    {
    }

    QString message() const { return m_message; }

private:
    QString m_message;
};

/// Packs editor state and the current Combined OSIS into the payload a
/// `.milah` archive is written from.
MilahProjectPayload projectPayload(
    const ProjectState &state,
    const QString &combinedOsis);

/// Rebuilds editor state from a project payload, re-parsing each embedded
/// source. Throws ProjectError for an unsupported version or a missing source.
ProjectState restoreProject(const MilahProjectPayload &payload);

QJsonObject payloadToJson(const MilahProjectPayload &payload);
MilahProjectPayload payloadFromJson(const QJsonObject &json);

} // namespace milah
