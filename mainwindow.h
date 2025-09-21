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
#include <functiondiscoverykeys_devpkey.h>
#define TX 1
#define TY 1
#define KEEPPOINT 2000
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
protected:
    void paintEvent(QPaintEvent *event) override;
private slots:
    void updateDisplay(){update();}
private:
    void initializeAudio();
    void showError(const QString &message);
    void captureAudio();
    QString formatToString(const WAVEFORMATEX *pwfx);
    bool convertToPCM16(const BYTE *pData, UINT32 numFrames, WAVEFORMATEX *pwfx);
    QVector<QPointF> points;
    QTimer *updateTimer;
    IMMDeviceEnumerator *deviceEnumerator;
    IMMDevice *device;
    IAudioClient *audioClient;
    IAudioCaptureClient *captureClient;
    bool isCapturing;
    WAVEFORMATEX *deviceFormat;
    UINT32 bufferFrameCount;
};
#endif
