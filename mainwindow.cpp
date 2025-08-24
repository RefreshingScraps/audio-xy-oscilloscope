/********mainwindow.cpp********/
#include "mainwindow.h"
#include <QPainter>
#include <QLinearGradient>
#include <QFont>
#include <thread>
#include <chrono>
#include <QMessageBox>
#include <mmreg.h>
#include <QElapsedTimer>

// 定义必要的GUID
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
    setWindowTitle("XYOscilloscope-made by linux-rm");

    // Initialize COM library
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
        // Already in the correct format
        const int16_t *samples = reinterpret_cast<const int16_t*>(pData);
        int sampleCount = numFrames * pwfx->nChannels;

        for (int i = 0; i < sampleCount; i += pwfx->nChannels) {
            if (i + 1 >= sampleCount) break; // Ensure we have at least 2 channels

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
        // Convert from 32-bit float to 16-bit PCM
        const float *floatSamples = reinterpret_cast<const float*>(pData);
        int sampleCount = numFrames * pwfx->nChannels;

        for (int i = 0; i < sampleCount; i += pwfx->nChannels) {
            if (i + 1 >= sampleCount) break; // Ensure we have at least 2 channels

            float left = floatSamples[i];
            float right = floatSamples[i + 1];

            // Clamp values to [-1.0, 1.0]
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
        // 32-bit PCM in extensible format
        const int32_t *samples = reinterpret_cast<const int32_t*>(pData);
        int sampleCount = numFrames * pwfx->nChannels;

        for (int i = 0; i < sampleCount; i += pwfx->nChannels) {
            if (i + 1 >= sampleCount) break; // Ensure we have at least 2 channels

            float left = samples[i] / 2147483648.0f;  // Divide by 2^31
            float right = samples[i + 1] / 2147483648.0f;

            // Clamp values to [-1.0, 1.0]
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

    // Create device enumerator
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void**)&deviceEnumerator);
    if (FAILED(hr)) {
        showError("无法创建设备枚举器");
        return;
    }

    // Get default audio render (output) device
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        showError("无法获取默认音频输出设备");
        return;
    }

    // Get device name for debugging
    LPWSTR deviceId = nullptr;
    device->GetId(&deviceId);
    qDebug() << "Audio device ID:" << QString::fromWCharArray(deviceId);
    CoTaskMemFree(deviceId);

    // Activate audio client
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
    if (FAILED(hr)) {
        showError("无法激活音频客户端");
        return;
    }

    // Get mix format
    hr = audioClient->GetMixFormat(&deviceFormat);
    if (FAILED(hr)) {
        showError("无法获取混合格式");
        return;
    }

    // Log the actual device format
    qDebug() << "Device format:" << formatToString(deviceFormat);

    // 计算最小延迟的缓冲区大小
    REFERENCE_TIME hnsRequestedDuration = 300000; // 30ms 缓冲区，减少延迟
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK,
                                 hnsRequestedDuration, 0, deviceFormat, nullptr);

    if (FAILED(hr)) {
        showError("无法初始化音频客户端");
        return;
    }

    // 获取实际的缓冲区大小
    hr = audioClient->GetBufferSize(&bufferFrameCount);
    if (FAILED(hr)) {
        showError("无法获取缓冲区大小");
        return;
    }

    qDebug() << "Buffer frame count:" << bufferFrameCount;

    // Get capture client
    hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
    if (FAILED(hr)) {
        showError("无法获取捕获客户端");
        return;
    }

    // Start capturing
    hr = audioClient->Start();
    if (FAILED(hr)) {
        showError("无法启动音频捕获");
        return;
    }

    isCapturing = true;

    // Start capture thread with higher priority
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
        // 使用更短的等待时间
        Sleep(1); // 减少到1ms

        hr = captureClient->GetBuffer(&pData, &numFramesAvailable, &flags, &devicePosition, &qpcPosition);
        if (FAILED(hr)) {
            if (hr == AUDCLNT_E_BUFFER_ERROR) {
                // 缓冲区错误，重置客户端
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
            // Silent data, skip processing
            hr = captureClient->ReleaseBuffer(numFramesAvailable);
            continue;
        }

        // Process the audio data
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

        // 每100ms打印一次性能信息
        if (timer.elapsed() > 100) {
            qDebug() << "Points:" << points.size() << "Frames:" << numFramesAvailable;
            timer.restart();
        }
    }
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
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

    painter.setPen(QPen(Qt::gray, 1, Qt::DotLine));
    painter.drawLine(plotRect.left(), plotRect.center().y(), plotRect.right(), plotRect.center().y());
    painter.drawLine(plotRect.center().x(), plotRect.top(), plotRect.center().x(), plotRect.bottom());

    // 只绘制最新的点，减少绘制延迟
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

    QLinearGradient gradient(plotRect.left(), plotRect.top(), plotRect.right(), plotRect.bottom());
    gradient.setColorAt(0, Qt::green);
    gradient.setColorAt(0.5, Qt::cyan);
    gradient.setColorAt(1, Qt::blue);
    painter.setPen(QPen(gradient, 1.5));

    if (!drawPoints.isEmpty()) {
        painter.drawPoints(drawPoints.constData(), drawPoints.size());
    }

    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 9));
    painter.drawText(0, 10, QString("点数:%1, 反相 X:%2,Y:%3").arg(points.size()).arg(TX).arg(TY));

    // Display audio format information
    if (deviceFormat) {
        painter.drawText(0, 25, "音频格式: " + formatToString(deviceFormat));
    }

    // 显示延迟信息
    painter.drawText(0, 40, QString("缓冲区大小: %1 帧").arg(bufferFrameCount));
}

void MainWindow::showError(const QString &message)
{
    qDebug() << "错误:" << message;
    setWindowTitle("错误: " + message);

    // Show error message box
    QMessageBox::critical(this, "错误", message);
}
