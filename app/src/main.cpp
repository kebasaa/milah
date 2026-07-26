#include "main_window.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Milah"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MILAH_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("Milah"));

    milah::MainWindow window;
    window.show();
    return application.exec();
}
