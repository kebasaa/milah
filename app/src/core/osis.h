#pragma once

#include "core/types.h"

#include <QString>

#include <stdexcept>

namespace milah {

/// Raised for any OSIS the editor refuses to load: malformed XML, unbalanced
/// verse milestones, duplicate verse IDs, or a DTD/entity declaration.
class OsisError : public std::runtime_error
{
public:
    explicit OsisError(const QString &message)
        : std::runtime_error(message.toStdString())
        , m_message(message)
    {
    }

    QString message() const { return m_message; }

private:
    QString m_message;
};

struct ParseOptions
{
    QString id;
    QString name;
    SourceRole role = SourceRole::Manuscript;
};

/// Parses an OSIS document into verses, headings and milestones.
/// Throws OsisError on anything it will not accept.
SourceDocument parseOsis(const QString &rawOsis, const ParseOptions &options);

} // namespace milah
