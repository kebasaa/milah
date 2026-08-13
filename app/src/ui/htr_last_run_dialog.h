#pragma once

#include <QDialog>

namespace milah {

/// What happened the last time a folio was handed to Kraken.
///
/// Exists because a recognition once ran for two minutes and produced nothing
/// at all — no words, no message, and nothing left behind to look at, because
/// the command, the output and the layout file had all lived in a temporary
/// directory that died with the function that made it. Every part of the
/// pipeline could be shown to work on its own and the composition still failed,
/// with no way in from outside.
///
/// So the run is kept, and this shows it: what Milah ran, what Kraken said, and
/// the layout file that came back. Three tabs and no cleverness — the point is
/// that the evidence exists at all.
class HtrLastRunDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit HtrLastRunDialog(QWidget *parent = nullptr);

    /// Whether there is anything to show, for the menu entry that offers it.
    static bool hasRun();
};

} // namespace milah
