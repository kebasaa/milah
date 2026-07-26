#pragma once

#include <QMainWindow>

class QWebChannel;
class QWebEngineView;

namespace milah {

class WebBridge;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    QWebEngineView *m_webView;
    QWebChannel *m_channel;
    WebBridge *m_bridge;
};

} // namespace milah
