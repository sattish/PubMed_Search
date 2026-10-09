#include <QApplication>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("PubMedSearch"));
    QApplication::setApplicationName(QStringLiteral("PubMedSearch"));

    MainWindow window;
    window.show();

    return app.exec();
}
