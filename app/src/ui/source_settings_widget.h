#pragma once

#include <QWidget>

class QVBoxLayout;

namespace milah {

class AppController;

/// Coverage of each loaded manuscript, which manuscript each translation is
/// shown against, and the review filters.
class SourceSettingsWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit SourceSettingsWidget(AppController *controller, QWidget *parent = nullptr);

    void refresh();

private:
    void clear();

    AppController *m_controller = nullptr;
    QVBoxLayout *m_layout = nullptr;
};

} // namespace milah
