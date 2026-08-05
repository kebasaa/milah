#include "ui/source_settings_widget.h"

#include "app_controller.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSet>
#include <QVBoxLayout>

namespace milah {
namespace {

QString displayTitle(const SourceDocument *source)
{
    return source->metadata.title.isEmpty() ? source->name : source->metadata.title;
}

/// What hovering a loaded text says about it: its terms first, then whatever
/// went wrong reading it.
///
/// The terms are here because a collation routinely holds texts under different
/// ones — a CC-licensed transcription beside an "all rights reserved"
/// translation — and an editor deciding what to quote or publish should not
/// have to open the XML to find out which is which.
QString sourceTooltip(const SourceDocument *source)
{
    QStringList lines;
    if (!source->metadata.rights.isEmpty()) {
        lines.append(QStringLiteral("Copyright: %1").arg(source->metadata.rights));
    }
    if (!source->metadata.license.isEmpty()) {
        lines.append(QStringLiteral("Licence: %1").arg(source->metadata.license));
    }
    lines.append(source->warnings);
    return lines.join(QLatin1Char('\n'));
}

} // namespace

SourceSettingsWidget::SourceSettingsWidget(AppController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(10, 10, 10, 10);
    m_layout->setSpacing(10);
    refresh();
}

void SourceSettingsWidget::clear()
{
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void SourceSettingsWidget::refresh()
{
    clear();

    const DocumentRefs manuscripts = m_controller->manuscripts();
    const DocumentRefs translations = m_controller->translations();

    if (!manuscripts.isEmpty()) {
        auto *coverage = new QGroupBox(QStringLiteral("Loaded manuscripts"));
        auto *coverageLayout = new QVBoxLayout(coverage);
        for (const SourceDocument *manuscript : manuscripts) {
            QSet<QString> chapters;
            for (const SourceVerse &verse : manuscript->verses) {
                chapters.insert(QStringLiteral("%1.%2")
                                    .arg(verse.reference.book)
                                    .arg(verse.reference.chapter));
            }

            auto *label = new QLabel(QStringLiteral("%1: %2 verses, %3 chapters%4")
                                         .arg(displayTitle(manuscript))
                                         .arg(manuscript->verses.size())
                                         .arg(chapters.size())
                                         .arg(manuscript->warnings.isEmpty()
                                                  ? QString()
                                                  : QStringLiteral("  ⚠")));
            label->setWordWrap(true);
            label->setToolTip(sourceTooltip(manuscript));
            coverageLayout->addWidget(label);
        }
        m_layout->addWidget(coverage);
    }

    if (!translations.isEmpty()) {
        auto *associationBox = new QGroupBox(QStringLiteral("Translations"));
        auto *form = new QFormLayout(associationBox);
        const QHash<QString, QString> associations = m_controller->associationMap();

        for (const SourceDocument *translation : translations) {
            auto *combo = new QComboBox;
            combo->addItem(QStringLiteral("Not displayed"), QString());
            for (const SourceDocument *manuscript : manuscripts) {
                combo->addItem(displayTitle(manuscript), manuscript->id);
            }

            const QString current = associations.value(translation->id);
            const int index = combo->findData(current);
            combo->setCurrentIndex(index >= 0 ? index : 0);

            const QString translationId = translation->id;
            connect(
                combo,
                &QComboBox::currentIndexChanged,
                this,
                [this, combo, translationId](int) {
                    m_controller->setAssociation(
                        translationId, combo->currentData().toString());
                });

            auto *label = new QLabel(displayTitle(translation));
            label->setWordWrap(true);
            label->setToolTip(sourceTooltip(translation));
            form->addRow(label, combo);
        }
        m_layout->addWidget(associationBox);
    }

    auto *filterBox = new QGroupBox(QStringLiteral("Review filters"));
    auto *filterLayout = new QVBoxLayout(filterBox);

    const ReviewFilters filters = m_controller->filters();
    struct FilterSpec
    {
        const char *label;
        bool ReviewFilters::*member;
    };
    static const FilterSpec specs[] = {
        {"Consensus ties", &ReviewFilters::ties},
        {"Manually edited", &ReviewFilters::manual},
        {"Missing readings", &ReviewFilters::missing},
        {"Uncertain translation", &ReviewFilters::uncertain},
    };

    for (const FilterSpec &spec : specs) {
        auto *check = new QCheckBox(QString::fromUtf8(spec.label));
        check->setChecked(filters.*(spec.member));
        const auto member = spec.member;
        connect(check, &QCheckBox::toggled, this, [this, member](bool checked) {
            ReviewFilters next = m_controller->filters();
            next.*member = checked;
            m_controller->setFilters(next);
        });
        filterLayout->addWidget(check);
    }
    m_layout->addWidget(filterBox);

    // No trailing stretch: the Notes panel sits directly under the filters, and
    // a stretch here would push it to the far bottom of the dock. Whatever
    // contains the two takes the stretch instead.
}

} // namespace milah
