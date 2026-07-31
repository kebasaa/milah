#include "core/project.h"

#include "core/osis.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonValue>

namespace milah {
namespace {

QJsonValue optionalString(const std::optional<QString> &value)
{
    if (!value.has_value()) {
        return QJsonValue(QJsonValue::Null);
    }
    return QJsonValue(*value);
}

std::optional<QString> toOptionalString(const QJsonValue &value)
{
    if (value.isNull() || value.isUndefined()) {
        return std::nullopt;
    }
    return value.toString();
}

QJsonObject referenceToJson(const VerseReference &reference)
{
    return QJsonObject{
        {QStringLiteral("id"), reference.id},
        {QStringLiteral("book"), reference.book},
        {QStringLiteral("chapter"), reference.chapter},
        {QStringLiteral("verse"), reference.verse},
    };
}

VerseReference referenceFromJson(const QJsonObject &json)
{
    VerseReference reference;
    reference.id = json.value(QStringLiteral("id")).toString();
    reference.book = json.value(QStringLiteral("book")).toString();
    reference.chapter = json.value(QStringLiteral("chapter")).toInt();
    reference.verse = json.value(QStringLiteral("verse")).toString();
    return reference;
}

QJsonObject draftToJson(const CombinedDraft &draft)
{
    QJsonArray columns;
    for (const ConsensusColumn &column : draft.columns) {
        columns.append(QJsonObject{
            {QStringLiteral("text"), optionalString(column.text)},
            {QStringLiteral("sourceId"), optionalString(column.sourceId)},
            {QStringLiteral("needsReview"), column.needsReview},
        });
    }

    return QJsonObject{
        {QStringLiteral("reference"), referenceToJson(draft.reference)},
        {QStringLiteral("columns"), columns},
        {QStringLiteral("manualText"), optionalString(draft.manualText)},
    };
}

CombinedDraft draftFromJson(const QJsonObject &json)
{
    CombinedDraft draft;
    draft.reference = referenceFromJson(json.value(QStringLiteral("reference")).toObject());
    draft.manualText = toOptionalString(json.value(QStringLiteral("manualText")));

    const QJsonArray columns = json.value(QStringLiteral("columns")).toArray();
    for (const QJsonValue &value : columns) {
        const QJsonObject entry = value.toObject();
        ConsensusColumn column;
        column.text = toOptionalString(entry.value(QStringLiteral("text")));
        column.sourceId = toOptionalString(entry.value(QStringLiteral("sourceId")));
        column.needsReview = entry.value(QStringLiteral("needsReview")).toBool();
        draft.columns.append(column);
    }

    return draft;
}

QJsonObject spanToJson(const TranslationSpan &span)
{
    QJsonObject json{
        {QStringLiteral("id"), span.id},
        {QStringLiteral("translationId"), span.translationId},
        {QStringLiteral("verseId"), span.verseId},
        {QStringLiteral("columnStart"), span.columnStart},
        {QStringLiteral("columnEnd"), span.columnEnd},
        {QStringLiteral("tokenStart"), span.tokenStart},
        {QStringLiteral("tokenEnd"), span.tokenEnd},
        {QStringLiteral("confidence"),
         span.confidence == SpanConfidence::Low ? QStringLiteral("low")
                                                : QStringLiteral("high")},
    };
    // Written only when it says something, so a span the editor has not touched
    // serialises exactly as it always did.
    if (span.removed) {
        json.insert(QStringLiteral("removed"), true);
    }
    return json;
}

TranslationSpan spanFromJson(const QJsonObject &json)
{
    TranslationSpan span;
    span.id = json.value(QStringLiteral("id")).toString();
    span.translationId = json.value(QStringLiteral("translationId")).toString();
    span.verseId = json.value(QStringLiteral("verseId")).toString();
    span.columnStart = json.value(QStringLiteral("columnStart")).toInt();
    span.columnEnd = json.value(QStringLiteral("columnEnd")).toInt();
    span.tokenStart = json.value(QStringLiteral("tokenStart")).toInt();
    span.tokenEnd = json.value(QStringLiteral("tokenEnd")).toInt();
    span.confidence =
        json.value(QStringLiteral("confidence")).toString() == QLatin1String("low")
        ? SpanConfidence::Low
        : SpanConfidence::High;
    span.removed = json.value(QStringLiteral("removed")).toBool();
    return span;
}

} // namespace

MilahProjectPayload projectPayload(
    const ProjectState &state,
    const QString &combinedOsis)
{
    QJsonArray sourceManifests;
    QStringList entries;

    for (int index = 0; index < state.sources.size(); ++index) {
        const SourceDocument &source = state.sources.at(index);
        const QString entry = QStringLiteral("sources/%1-%2.osis")
                                  .arg(index + 1, 3, 10, QLatin1Char('0'))
                                  .arg(source.id);
        entries.append(entry);
        sourceManifests.append(QJsonObject{
            {QStringLiteral("id"), source.id},
            {QStringLiteral("name"), source.name},
            {QStringLiteral("role"), sourceRoleToString(source.role)},
            {QStringLiteral("entry"), entry},
        });
    }

    QJsonArray associations;
    for (const TranslationAssociation &association : state.associations) {
        associations.append(QJsonObject{
            {QStringLiteral("translationId"), association.translationId},
            {QStringLiteral("manuscriptId"), association.manuscriptId},
        });
    }

    QJsonObject combined;
    for (auto item = state.combined.constBegin(); item != state.combined.constEnd(); ++item) {
        combined.insert(item.key(), draftToJson(item.value()));
    }

    QJsonArray spans;
    for (const TranslationSpan &span : state.translationSpans) {
        spans.append(spanToJson(span));
    }

    QJsonObject columnSplits;
    for (auto item = state.columnSplits.constBegin();
         item != state.columnSplits.constEnd();
         ++item) {
        QJsonArray columns;
        for (const int column : item.value()) {
            columns.append(column);
        }
        if (!columns.isEmpty()) {
            columnSplits.insert(item.key(), columns);
        }
    }

    QJsonObject chapterReferences;
    for (auto item = state.chapterReferences.constBegin();
         item != state.chapterReferences.constEnd();
         ++item) {
        if (!item.value().isEmpty()) {
            chapterReferences.insert(item.key(), item.value());
        }
    }

    QJsonObject verseReferences;
    for (auto item = state.verseReferences.constBegin();
         item != state.verseReferences.constEnd();
         ++item) {
        if (!item.value().isEmpty()) {
            verseReferences.insert(item.key(), item.value());
        }
    }

    QJsonObject combinedNotes;
    for (auto item = state.combinedNotes.constBegin();
         item != state.combinedNotes.constEnd();
         ++item) {
        if (!item.value().isEmpty()) {
            combinedNotes.insert(item.key(), item.value());
        }
    }

    QJsonObject interlinearWords;
    for (auto item = state.interlinearWords.constBegin();
         item != state.interlinearWords.constEnd();
         ++item) {
        if (!item.value().isEmpty()) {
            interlinearWords.insert(item.key(), item.value());
        }
    }

    QJsonValue location(QJsonValue::Null);
    if (state.location.has_value()) {
        location = QJsonObject{
            {QStringLiteral("book"), state.location->book},
            {QStringLiteral("chapter"), state.location->chapter},
        };
    }

    MilahProjectPayload payload;
    payload.suggestedName = QStringLiteral("Milah_Project.milah");
    payload.manifest = QJsonObject{
        {QStringLiteral("format"), QStringLiteral("milah-project")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("savedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("sources"), sourceManifests},
        {QStringLiteral("associations"), associations},
        {QStringLiteral("priorityManuscriptId"), state.priorityManuscriptId},
        {QStringLiteral("combined"), combined},
        {QStringLiteral("translationSpans"), spans},
        {QStringLiteral("columnSplits"), columnSplits},
        {QStringLiteral("chapterReferences"), chapterReferences},
        {QStringLiteral("verseReferences"), verseReferences},
        {QStringLiteral("combinedNotes"), combinedNotes},
        {QStringLiteral("interlinearWords"), interlinearWords},
        {QStringLiteral("location"), location},
    };

    for (int index = 0; index < state.sources.size(); ++index) {
        ProjectFile file;
        file.path = entries.at(index);
        file.contentBase64 =
            QString::fromLatin1(state.sources.at(index).rawOsis.toUtf8().toBase64());
        payload.files.append(file);
    }

    ProjectFile combinedFile;
    combinedFile.path = QStringLiteral("combined/combined.osis");
    combinedFile.contentBase64 =
        QString::fromLatin1(combinedOsis.toUtf8().toBase64());
    payload.files.append(combinedFile);

    return payload;
}

ProjectState restoreProject(const MilahProjectPayload &payload)
{
    const QJsonObject &manifest = payload.manifest;

    if (manifest.value(QStringLiteral("format")).toString()
            != QLatin1String("milah-project")
        || manifest.value(QStringLiteral("version")).toInt() != 1) {
        throw ProjectError(
            QStringLiteral("This Milah project version is not supported."));
    }

    QHash<QString, QString> filesByPath;
    for (const ProjectFile &file : payload.files) {
        filesByPath.insert(file.path, file.contentBase64);
    }

    ProjectState state;

    const QJsonArray sources = manifest.value(QStringLiteral("sources")).toArray();
    for (const QJsonValue &value : sources) {
        const QJsonObject entry = value.toObject();
        const QString path = entry.value(QStringLiteral("entry")).toString();
        const auto encoded = filesByPath.constFind(path);
        if (encoded == filesByPath.constEnd()) {
            throw ProjectError(
                QStringLiteral("Project source is missing: %1").arg(path));
        }

        ParseOptions options;
        options.id = entry.value(QStringLiteral("id")).toString();
        options.name = entry.value(QStringLiteral("name")).toString();
        options.role = sourceRoleFromString(entry.value(QStringLiteral("role")).toString());

        const QByteArray raw =
            QByteArray::fromBase64(encoded.value().toLatin1());
        state.sources.append(parseOsis(QString::fromUtf8(raw), options));
    }

    const QJsonArray associations =
        manifest.value(QStringLiteral("associations")).toArray();
    for (const QJsonValue &value : associations) {
        const QJsonObject entry = value.toObject();
        TranslationAssociation association;
        association.translationId = entry.value(QStringLiteral("translationId")).toString();
        association.manuscriptId = entry.value(QStringLiteral("manuscriptId")).toString();
        state.associations.append(association);
    }

    state.priorityManuscriptId =
        manifest.value(QStringLiteral("priorityManuscriptId")).toString();

    const QJsonObject combined = manifest.value(QStringLiteral("combined")).toObject();
    for (auto item = combined.constBegin(); item != combined.constEnd(); ++item) {
        state.combined.insert(item.key(), draftFromJson(item.value().toObject()));
    }

    const QJsonArray spans = manifest.value(QStringLiteral("translationSpans")).toArray();
    for (const QJsonValue &value : spans) {
        state.translationSpans.append(spanFromJson(value.toObject()));
    }

    // Absent from projects written before words could be divided, which simply
    // means no verse has been divided.
    const QJsonObject columnSplits =
        manifest.value(QStringLiteral("columnSplits")).toObject();
    for (auto item = columnSplits.constBegin(); item != columnSplits.constEnd(); ++item) {
        QList<int> columns;
        for (const QJsonValue &column : item.value().toArray()) {
            columns.append(column.toInt());
        }
        if (!columns.isEmpty()) {
            state.columnSplits.insert(item.key(), columns);
        }
    }

    // All absent from projects written before a chapter or verse could have its
    // own reference or a word its own note, which reads correctly as none.
    const QJsonObject chapterReferences =
        manifest.value(QStringLiteral("chapterReferences")).toObject();
    for (auto item = chapterReferences.constBegin();
         item != chapterReferences.constEnd();
         ++item) {
        const QString sourceId = item.value().toString();
        if (!sourceId.isEmpty()) {
            state.chapterReferences.insert(item.key(), sourceId);
        }
    }

    const QJsonObject verseReferences =
        manifest.value(QStringLiteral("verseReferences")).toObject();
    for (auto item = verseReferences.constBegin();
         item != verseReferences.constEnd();
         ++item) {
        const QString sourceId = item.value().toString();
        if (!sourceId.isEmpty()) {
            state.verseReferences.insert(item.key(), sourceId);
        }
    }

    const QJsonObject combinedNotes =
        manifest.value(QStringLiteral("combinedNotes")).toObject();
    for (auto item = combinedNotes.constBegin(); item != combinedNotes.constEnd(); ++item) {
        const QString note = item.value().toString();
        if (!note.isEmpty()) {
            state.combinedNotes.insert(item.key(), note);
        }
    }

    const QJsonObject interlinearWords =
        manifest.value(QStringLiteral("interlinearWords")).toObject();
    for (auto item = interlinearWords.constBegin();
         item != interlinearWords.constEnd();
         ++item) {
        const QString word = item.value().toString();
        if (!word.isEmpty()) {
            state.interlinearWords.insert(item.key(), word);
        }
    }

    const QJsonValue location = manifest.value(QStringLiteral("location"));
    if (location.isObject()) {
        const QJsonObject entry = location.toObject();
        Location parsed;
        parsed.book = entry.value(QStringLiteral("book")).toString();
        parsed.chapter = entry.value(QStringLiteral("chapter")).toInt();
        state.location = parsed;
    }

    return state;
}

QJsonObject payloadToJson(const MilahProjectPayload &payload)
{
    QJsonArray files;
    for (const ProjectFile &file : payload.files) {
        files.append(QJsonObject{
            {QStringLiteral("path"), file.path},
            {QStringLiteral("contentBase64"), file.contentBase64},
        });
    }

    return QJsonObject{
        {QStringLiteral("manifest"), payload.manifest},
        {QStringLiteral("files"), files},
    };
}

MilahProjectPayload payloadFromJson(const QJsonObject &json)
{
    MilahProjectPayload payload;
    payload.manifest = json.value(QStringLiteral("manifest")).toObject();

    const QJsonArray files = json.value(QStringLiteral("files")).toArray();
    for (const QJsonValue &value : files) {
        const QJsonObject entry = value.toObject();
        ProjectFile file;
        file.path = entry.value(QStringLiteral("path")).toString();
        file.contentBase64 = entry.value(QStringLiteral("contentBase64")).toString();
        payload.files.append(file);
    }

    return payload;
}

} // namespace milah
