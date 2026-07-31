#include "ui/about_dialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QSysInfo>
#include <QVBoxLayout>

namespace milah {
namespace {

/// The toolchain that compiled this file, which is the only one that can be
/// asked at compile time. Clang is tested first because it defines __GNUC__ as
/// well.
QString compilerDescription()
{
#if defined(__clang__)
    return QStringLiteral("Clang %1.%2.%3")
        .arg(__clang_major__)
        .arg(__clang_minor__)
        .arg(__clang_patchlevel__);
#elif defined(__GNUC__)
    return QStringLiteral("GCC %1.%2.%3")
        .arg(__GNUC__)
        .arg(__GNUC_MINOR__)
        .arg(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    return QStringLiteral("MSVC %1").arg(_MSC_VER);
#else
    return QStringLiteral("an unrecorded compiler");
#endif
}

/// Rich text that may carry links, laid out to a fixed width so it wraps the
/// same way whatever is in it.
QLabel *paragraph(const QString &html)
{
    auto *label = new QLabel(html);
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    // Links are the point of an attribution, and a version string is worth
    // being able to copy into a bug report.
    label->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    label->setOpenExternalLinks(true);
    return label;
}

} // namespace

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("About Milah"));
    setObjectName(QStringLiteral("aboutDialog"));

    QStringList build;
    // The date comes from CMake rather than __DATE__ on purpose. __DATE__ is
    // when this one file was last compiled, so an incremental build that did
    // not touch it would keep reporting an older day; the build script
    // reconfigures on every run, so the stamp is refreshed each time.
    build << QStringLiteral("Built %1 with %2 against Qt %3, for %4")
                 .arg(
                     QStringLiteral(MILAH_BUILD_DATE),
                     compilerDescription(),
                     QStringLiteral(QT_VERSION_STR),
                     QSysInfo::buildCpuArchitecture());
    // A portable folder can meet a different Qt than it was built with, and
    // that is worth knowing when something misbehaves.
    if (QLatin1String(qVersion()) != QLatin1String(QT_VERSION_STR)) {
        build << QStringLiteral("Running against Qt %1").arg(QLatin1String(qVersion()));
    }

    auto *heading = paragraph(
        QStringLiteral(
            "<h3 style='margin:0'>Milah %1</h3>"
            "<p style='margin:4px 0 0 0'>Comparing Hebrew New Testament "
            "manuscript witnesses and building a combined edition.</p>"
            "<p style='margin:10px 0 0 0'>Copyright &copy; 2026 "
            "Jonathan D. M&uuml;ller</p>"
            "<p style='margin:2px 0 0 0'>%2</p>")
            .arg(QStringLiteral(MILAH_VERSION), build.join(QStringLiteral("<br>"))));

    auto *licence = paragraph(QStringLiteral(
        "<p style='margin:0'>Milah is free software: you may redistribute and "
        "modify it under the terms of the "
        "<a href='https://www.gnu.org/licenses/gpl-3.0.html'>GNU General Public "
        "License, version&nbsp;3</a>, or any later version. It comes with no "
        "warranty.</p>"));

    // Wording follows app/data/README.md, which records what was actually used.
    auto *credits = paragraph(QStringLiteral(
        "<p style='margin:0'><b>The data Milah ships with</b></p>"
        "<ul style='margin:4px 0 0 0; -qt-list-indent:1'>"
        "<li>Strong's <i>Concise Dictionary of the Words in the Hebrew Bible</i> "
        "(James Strong, 1890), in the "
        "<a href='https://github.com/openscriptures/strongs'>OpenScriptures</a> "
        "machine-readable edition &mdash; public domain.</li>"
        "<li>The Westminster Leningrad Codex with morphology, from the "
        "OpenScriptures <a href='https://github.com/openscriptures/morphhb'>morphhb</a> "
        "project &mdash; CC&nbsp;BY&nbsp;4.0.</li>"
        "<li>TBESH, the Translators Brief lexicon of Extended Strongs for "
        "Hebrew, from <a href='https://www.STEPBible.org'>STEP Bible</a> "
        "(www.STEPBible.org) &mdash; CC&nbsp;BY&nbsp;4.0.</li>"
        "<li>A list of 1st&ndash;3rd century Hebrew word forms, compiled from "
        "the Mishnah and Tosefta in the "
        "<a href='https://github.com/Sefaria/Sefaria-Export'>Sefaria export</a>, "
        "using only versions in the public domain or under CC&nbsp;BY.</li>"
        "</ul>"
        "<p style='margin:8px 0 0 0'>The versions used and their licences are "
        "listed in full in <code>data/README.md</code>.</p>"));
    QFont small = credits->font();
    small.setPointSizeF(small.pointSizeF() * 0.92);
    credits->setFont(small);

    auto *mark = new QLabel;
    mark->setPixmap(QIcon(QStringLiteral(":/img/milah.png")).pixmap(64, 64));
    mark->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    auto *text = new QVBoxLayout;
    text->setSpacing(12);
    text->addWidget(heading);
    text->addWidget(licence);
    text->addWidget(credits);

    auto *body = new QHBoxLayout;
    body->setSpacing(16);
    body->addWidget(mark, 0, Qt::AlignTop);
    body->addLayout(text, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(16);
    layout->addLayout(body);
    layout->addWidget(buttons);

    // Word-wrapped rich text has no width of its own to speak of: left to
    // itself the layout would pick one from the longest unbreakable run and
    // make a very wide, very short dialog.
    setFixedWidth(560);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
}

} // namespace milah
