#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QMainWindow>
#include <QAudioDecoder>
#include <QAudioBuffer>
#include <QVector>
#include <QPointF>
#include <QPaintEvent>
#include <QAudioFormat>
#include <QTimer>
#include <QAudioSink>
#include <QBuffer>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QDebug>
#include <QFile>
#include <QString>
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QString &filePath = QString(), QWidget *parent = nullptr);
    ~MainWindow();
    void setAudioFile(const QString &filePath);
protected:
    void paintEvent(QPaintEvent *event) override;
private slots:
    void processBuffer();
    void updatePlayback();
private:
    void initializeAudio(const QString &filePath);
    void showError(const QString &message);
    QAudioDecoder *decoder;
    QVector<QPointF> allPoints;
    QVector<QPointF> visiblePoints;
    static const int SAMPLE_RATE = 44100;
    QTimer *playbackTimer;
    QString currentFilePath;
    QMediaPlayer *player;
    qint64 audioPosition = 0;
    QAudioOutput *audioOutput;
    QElapsedTimer playbackPositionTimer;
    qint64 totalDuration = 0;
};
#endif
