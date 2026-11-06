#include <QApplication>
#include <QDir>
#include <QStandardPaths>

#include "ui/MainWindow.h"
#include "database/DatabaseManager.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Coworking Space Manager");
    app.setOrganizationName("CoworkingSpace");

    // Database path in app data dir
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QString dbPath = dataDir + "/coworking.db";

    if (!DatabaseManager::instance().initialize(dbPath)) {
        qWarning() << "Failed to initialize database:" << DatabaseManager::instance().lastError();
    }

    MainWindow w;
    w.showFullScreen();

    int ret = app.exec();

    DatabaseManager::instance().close();
    return ret;
}
