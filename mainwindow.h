#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QMainWindow>
#include <QVector>
#include <QPointF>
#include <QPaintEvent>
#include <QTimer>
#include <QDebug>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <thread>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QApplication>
#define TX 1
#define TY 1
#define KEEPPOINT 2000
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    QVector<QPointF> points;
    QTimer *updateTimer;
    IMMDeviceEnumerator *deviceEnumerator;
    IMMDevice *device;
    IAudioClient *audioClient;
    IAudioCaptureClient *captureClient;
    bool isCapturing;
    WAVEFORMATEX *deviceFormat;
};
#endif
