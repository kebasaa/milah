#include "main_window.h"

#include "web_bridge.h"

#include <QUrl>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

namespace milah {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_webView(new QWebEngineView(this))
    , m_channel(new QWebChannel(this))
    , m_bridge(new WebBridge(this))
{
    setWindowTitle(QStringLiteral("Milah"));
    resize(1440, 900);
    setMinimumSize(900, 600);
    setCentralWidget(m_webView);

    m_channel->registerObject(QStringLiteral("milahNative"), m_bridge);
    m_webView->page()->setWebChannel(m_channel);
    m_webView->settings()->setAttribute(
        QWebEngineSettings::LocalContentCanAccessFileUrls,
        false);
    m_webView->settings()->setAttribute(
        QWebEngineSettings::LocalContentCanAccessRemoteUrls,
        false);
    m_webView->setContextMenuPolicy(Qt::NoContextMenu);
    m_webView->load(QUrl(QStringLiteral("qrc:/index.html")));
}

} // namespace milah
