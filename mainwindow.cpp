#include "mainwindow.h"
#include <QPainter>
#include <QLinearGradient>
#include <QFont>
#include <thread>
#include <chrono>
#include <QMessageBox>
#include <mmreg.h>
#include <QElapsedTimer>
#ifndef KSDATAFORMAT_SUBTYPE_PCM
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM,
            0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
#endif
#ifndef KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT,
            0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
#endif
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), deviceEnumerator(nullptr), device(nullptr),
    audioClient(nullptr), captureClient(nullptr),
    isCapturing(false), deviceFormat(nullptr), bufferFrameCount(0)
{
    resize(360, 360);
    setWindowTitle("XY示波器-由linux-rm制作");
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    CoInitialize(nullptr);
    initializeAudio();
    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &MainWindow::updateDisplay);
    updateTimer->start(16);
}
MainWindow::~MainWindow()
{
    isCapturing = false;
    if (captureClient) {
        captureClient->Release();
    }
    if (audioClient) {
        audioClient->Stop();
        audioClient->Release();
    }
    if (device) {
        device->Release();
    }
    if (deviceEnumerator) {
        deviceEnumerator->Release();
    }
    if (deviceFormat) {
        CoTaskMemFree(deviceFormat);
    }
    CoUninitialize();
    if (updateTimer) {
        updateTimer->stop();
        delete updateTimer;
    }
}
QString MainWindow::formatToString(const WAVEFORMATEX *pwfx)
{
    QString formatStr;
    if (pwfx->wFormatTag == WAVE_FORMAT_PCM) {
        formatStr = "PCM";
    } else if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        formatStr = "IEEE Float";
    } else if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE *wfex = (WAVEFORMATEXTENSIBLE *)pwfx;
        if (wfex->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
            formatStr = "Extensible (PCM)";
        } else if (wfex->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
            formatStr = "Extensible (IEEE Float)";
        } else {
            formatStr = "Extensible (Unknown)";
        }
    } else {
        formatStr = QString("Unknown (0x%1)").arg(pwfx->wFormatTag, 0, 16);
    }
    return QString("%1, %2 channels, %3 Hz, %4 bits")
        .arg(formatStr)
        .arg(pwfx->nChannels)
        .arg(pwfx->nSamplesPerSec)
        .arg(pwfx->wBitsPerSample);
}
bool MainWindow::convertToPCM16(const BYTE *pData, UINT32 numFrames, WAVEFORMATEX *pwfx)
{
    if (pwfx->wFormatTag == WAVE_FORMAT_PCM && pwfx->wBitsPerSample == 16) {
        const int16_t *samples = reinterpret_cast<const int16_t*>(pData);
        int sampleCount = numFrames * pwfx->nChannels;
        for (int i = 0; i < sampleCount; i += pwfx->nChannels) {
            if (i + 1 >= sampleCount) break;
            float left = samples[i] / 32768.0f;
            float right = samples[i + 1] / 32768.0f;
#if TX
            left = -left;
#endif
#if TY
            right = -right;
#endif
            points.append(QPointF(left, right));
            if (points.size() > KEEPPOINT)
                points.remove(0, points.size() - KEEPPOINT);
        }
        return true;
    }
    else if ((pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && pwfx->wBitsPerSample == 32) ||
             (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
              ((WAVEFORMATEXTENSIBLE*)pwfx)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
        const float *floatSamples = reinterpret_cast<const float*>(pData);
        int sampleCount = numFrames * pwfx->nChannels;
        for (int i = 0; i < sampleCount; i += pwfx->nChannels) {
            if (i + 1 >= sampleCount) break;
            float left = floatSamples[i];
            float right = floatSamples[i + 1];
            left = qMax(-1.0f, qMin(1.0f, left));
            right = qMax(-1.0f, qMin(1.0f, right));
#if TX
            left = -left;
#endif
#if TY
            right = -right;
#endif
            points.append(QPointF(left, right));
            if (points.size() > KEEPPOINT)
                points.remove(0, points.size() - KEEPPOINT);
        }
        return true;
    }
    else if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
             ((WAVEFORMATEXTENSIBLE*)pwfx)->SubFormat == KSDATAFORMAT_SUBTYPE_PCM &&
             pwfx->wBitsPerSample == 32) {
        const int32_t *samples = reinterpret_cast<const int32_t*>(pData);
        int sampleCount = numFrames * pwfx->nChannels;
        for (int i = 0; i < sampleCount; i += pwfx->nChannels) {
            if (i + 1 >= sampleCount) break;
            float left = samples[i] / 2147483648.0f;
            float right = samples[i + 1] / 2147483648.0f;
            left = qMax(-1.0f, qMin(1.0f, left));
            right = qMax(-1.0f, qMin(1.0f, right));
#if TX
            left = -left;
#endif
#if TY
            right = -right;
#endif
            points.append(QPointF(left, right));
            if (points.size() > KEEPPOINT)
                points.remove(0, points.size() - KEEPPOINT);
        }
        return true;
    }
    return false;
}
void MainWindow::initializeAudio()
{
    HRESULT hr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void**)&deviceEnumerator);
    if (FAILED(hr)) {
        showError("无法创建设备枚举器");
        return;
    }
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        showError("无法获取默认音频输出设备");
        return;
    }
    LPWSTR deviceId = nullptr;
    device->GetId(&deviceId);
    qDebug() << "Audio device ID:" << QString::fromWCharArray(deviceId);
    CoTaskMemFree(deviceId);
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
    if (FAILED(hr)) {
        showError("无法激活音频客户端");
        return;
    }
    hr = audioClient->GetMixFormat(&deviceFormat);
    if (FAILED(hr)) {
        showError("无法获取混合格式");
        return;
    }
    qDebug() << "Device format:" << formatToString(deviceFormat);
    REFERENCE_TIME hnsRequestedDuration = 300000;
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK,
                                 hnsRequestedDuration, 0, deviceFormat, nullptr);
    if (FAILED(hr)) {
        showError("无法初始化音频客户端");
        return;
    }
    hr = audioClient->GetBufferSize(&bufferFrameCount);
    if (FAILED(hr)) {
        showError("无法获取缓冲区大小");
        return;
    }
    qDebug() << "Buffer frame count:" << bufferFrameCount;
    hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
    if (FAILED(hr)) {
        showError("无法获取捕获客户端");
        return;
    }
    hr = audioClient->Start();
    if (FAILED(hr)) {
        showError("无法启动音频捕获");
        return;
    }
    isCapturing = true;
    std::thread([this]() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        captureAudio();
    }).detach();
}
void MainWindow::captureAudio()
{
    HRESULT hr;
    BYTE *pData;
    UINT32 numFramesAvailable;
    DWORD flags;
    UINT64 devicePosition;
    UINT64 qpcPosition;
    QElapsedTimer timer;
    timer.start();
    while (isCapturing) {
        Sleep(1);
        hr = captureClient->GetBuffer(&pData, &numFramesAvailable, &flags, &devicePosition, &qpcPosition);
        if (FAILED(hr)) {
            if (hr == AUDCLNT_E_BUFFER_ERROR) {
                audioClient->Stop();
                audioClient->Reset();
                audioClient->Start();
            }
            continue;
        }
        if (numFramesAvailable == 0) {
            continue;
        }
        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            hr = captureClient->ReleaseBuffer(numFramesAvailable);
            continue;
        }
        if (!convertToPCM16(pData, numFramesAvailable, deviceFormat)) {
            qDebug() << "不支持的音频格式:" << formatToString(deviceFormat);
            showError("不支持的音频格式: " + formatToString(deviceFormat));
            isCapturing = false;
            break;
        }
        hr = captureClient->ReleaseBuffer(numFramesAvailable);
        if (FAILED(hr)) {
            break;
        }
        if (timer.elapsed() > 100)
            timer.restart();
    }
}
void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setRenderHint(QPainter::TextAntialiasing, false);
    painter.fillRect(rect(), Qt::black);
    if (points.isEmpty()) {
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 14));
        painter.drawText(rect(), Qt::AlignCenter, "等待音频输出...");
        return;
    }
    int size = qMin(width(), height());
    int xOffset = (width() - size) / 2;
    int yOffset = (height() - size) / 2;
    QRect plotRect(xOffset, yOffset, size, size);
    painter.setPen(QPen(Qt::white, 1, Qt::SolidLine));
    painter.drawLine(plotRect.left(), plotRect.center().y(), plotRect.right(), plotRect.center().y());
    painter.drawLine(plotRect.center().x(), plotRect.top(), plotRect.center().x(), plotRect.bottom());
    int startIndex = qMax(0, points.size() - KEEPPOINT);
    int pointsToDraw = points.size() - startIndex;
    QVector<QPoint> drawPoints;
    drawPoints.reserve(pointsToDraw);
    for (int i = startIndex; i < points.size(); i++) {
        const QPointF &point = points[i];
        int x = plotRect.left() + (1.0 + point.x()) * size / 2;
        int y = plotRect.top() + (1.0 - point.y()) * size / 2;
        drawPoints.append(QPoint(x, y));
    }
    painter.setPen(QPen(Qt::green, 2));
    if (!drawPoints.isEmpty()) {
        painter.drawPoints(drawPoints.constData(), drawPoints.size());
    }
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 9));
}
void MainWindow::showError(const QString &message)
{
    setWindowTitle("错误: " + message);
    QMessageBox::critical(this, "错误", message);
}
