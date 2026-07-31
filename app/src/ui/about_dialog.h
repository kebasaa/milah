#pragma once

#include <QDialog>

namespace milah {

/// What this build is: its version, who wrote it, what it may be done with,
/// and whose data it leans on.
///
/// Milah is handed round as a portable folder rather than installed, so a copy
/// of the executable has nothing else to say which build it is. The credits are
/// not decoration either: morphhb and TBESH are CC BY 4.0, which asks for
/// attribution wherever the data travels, and the data travels with every copy.
class AboutDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
};

} // namespace milah
