#include "mainwindow.h"
#include <QApplication>
#include <QFileDialog>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName("XYOscilloscope");
    a.setApplicationVersion("1.0");
    a.setWindowIcon(QIcon(":/icons/app_icon.png"));
    QCommandLineParser parser;
    parser.setApplicationDescription("音频XY示波器");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "要打开的音频文件");
    parser.process(a);
    QString filePath;
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        filePath = args.first();
    } else {
        QStringList filters;
        filters << "无损音频 (*.wav *.flac *.aiff *.aif *.alac *.ape)"
                << "所有文件 (*)";
        filePath = QFileDialog::getOpenFileName(
            nullptr,
            "选择音频文件",
            QDir::homePath(),
            filters.join(";;")
            );
        if (filePath.isEmpty()) {
            return 0;
        }
    }
    if (!QFile::exists(filePath)) {
        QMessageBox::critical(nullptr, "错误", "文件不存在: " + filePath);
        return -1;
    }
    MainWindow w(filePath);
    w.show();
    return a.exec();
}
