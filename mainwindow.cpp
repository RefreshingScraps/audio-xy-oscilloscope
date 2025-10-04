#include "mainwindow.h"
#ifndef KSDATAFORMAT_SUBTYPE_PCM
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM,0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
#endif
#ifndef KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT,0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
#endif
MainWindow::MainWindow(QWidget *parent): QMainWindow(parent), deviceEnumerator(nullptr), device(nullptr),audioClient(nullptr), captureClient(nullptr),isCapturing(false), deviceFormat(nullptr)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_TranslucentBackground);
    setGeometry(QApplication::primaryScreen()->geometry().width() - width() - 50, 50, 360, 360);
    CoInitialize(nullptr);
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&deviceEnumerator);
    if (FAILED(hr)) {
        QMessageBox::critical(this, "错误", "无法创建设备枚举器");
        return;
    }
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        QMessageBox::critical(this, "错误", "无法获取默认音频输出设备");
        return;
    }
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
    if (FAILED(hr)) {
        QMessageBox::critical(this, "错误", "无法激活音频客户端");
        return;
    }
    hr = audioClient->GetMixFormat(&deviceFormat);
    if (FAILED(hr)) {
        QMessageBox::critical(this, "错误", "无法获取混合格式");
        return;
    }
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 300000, 0, deviceFormat, nullptr);
    if (FAILED(hr)) {
        QMessageBox::critical(this, "错误", "无法初始化音频客户端");
        return;
    }
    hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
    if (FAILED(hr)) {
        QMessageBox::critical(this, "错误", "无法获取捕获客户端");
        return;
    }
    hr = audioClient->Start();
    if (FAILED(hr)) {
        QMessageBox::critical(this, "错误", "无法启动音频捕获");
        return;
    }
    isCapturing = true;
    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, [this](){ update(); });
    updateTimer->start(16);
    std::thread([this]() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        BYTE *pData;
        UINT32 numFramesAvailable;
        DWORD flags;
        UINT64 devicePosition, qpcPosition;
        QElapsedTimer timer;
        timer.start();
        while (isCapturing) {
            Sleep(1);
            HRESULT hr = captureClient->GetBuffer(&pData, &numFramesAvailable, &flags, &devicePosition, &qpcPosition);
            if (FAILED(hr)) {
                if (hr == AUDCLNT_E_BUFFER_ERROR) {
                    audioClient->Stop();
                    audioClient->Reset();
                    audioClient->Start();
                }
                continue;
            }
            if (numFramesAvailable == 0 || (flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                captureClient->ReleaseBuffer(numFramesAvailable);
                continue;
            }
            if (deviceFormat->wFormatTag == WAVE_FORMAT_PCM && deviceFormat->wBitsPerSample == 16) {
                const int16_t *samples = reinterpret_cast<const int16_t*>(pData);
                int sampleCount = numFramesAvailable * deviceFormat->nChannels;
                for (int i = 0; i < sampleCount; i += deviceFormat->nChannels) {
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
                    if (points.size() > KEEPPOINT) points.remove(0, points.size() - KEEPPOINT);
                }
            }
            else if ((deviceFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && deviceFormat->wBitsPerSample == 32) ||
                     (deviceFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && ((WAVEFORMATEXTENSIBLE*)deviceFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
                const float *floatSamples = reinterpret_cast<const float*>(pData);
                int sampleCount = numFramesAvailable * deviceFormat->nChannels;
                for (int i = 0; i < sampleCount; i += deviceFormat->nChannels) {
                    if (i + 1 >= sampleCount) break;
                    float left = qMax(-1.0f, qMin(1.0f, floatSamples[i]));
                    float right = qMax(-1.0f, qMin(1.0f, floatSamples[i + 1]));
#if TX
                    left = -left;
#endif
#if TY
                    right = -right;
#endif
                    points.append(QPointF(left, right));
                    if (points.size() > KEEPPOINT) points.remove(0, points.size() - KEEPPOINT);
                }
            }
            else if (deviceFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && ((WAVEFORMATEXTENSIBLE*)deviceFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_PCM && deviceFormat->wBitsPerSample == 32) {
                const int32_t *samples = reinterpret_cast<const int32_t*>(pData);
                int sampleCount = numFramesAvailable * deviceFormat->nChannels;
                for (int i = 0; i < sampleCount; i += deviceFormat->nChannels) {
                    if (i + 1 >= sampleCount) break;
                    float left = qMax(-1.0f, qMin(1.0f, samples[i] / 2147483648.0f));
                    float right = qMax(-1.0f, qMin(1.0f, samples[i + 1] / 2147483648.0f));
#if TX
                    left = -left;
#endif
#if TY
                    right = -right;
#endif
                    points.append(QPointF(left, right));
                    if (points.size() > KEEPPOINT) points.remove(0, points.size() - KEEPPOINT);
                }
            }
            else {
                QMessageBox::critical(nullptr, "错误", "不支持的音频格式");
                isCapturing = false;
                break;
            }
            captureClient->ReleaseBuffer(numFramesAvailable);
            if (timer.elapsed() > 100) timer.restart();
        }
    }).detach();
}
MainWindow::~MainWindow()
{
    isCapturing = false;
    if (captureClient) captureClient->Release();
    if (audioClient) {
        audioClient->Stop();
        audioClient->Release();
    }
    if (device) device->Release();
    if (deviceEnumerator) deviceEnumerator->Release();
    if (deviceFormat) CoTaskMemFree(deviceFormat);
    CoUninitialize();
    if (updateTimer) {
        updateTimer->stop();
        delete updateTimer;
    }
}
void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    if (points.isEmpty()) {
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 14));
        painter.drawText(rect(), Qt::AlignCenter, "等待音频输出...");
        return;
    }
    int size = qMin(width(), height());
    QRect plotRect((width() - size) / 2, (height() - size) / 2, size, size);
    int startIndex = qMax(0, points.size() - KEEPPOINT);
    QVector<QPoint> drawPoints;
    drawPoints.reserve(points.size() - startIndex);
    for (int i = startIndex; i < points.size(); i++) {
        const QPointF &point = points[i];
        int x = plotRect.left() + (1.0 + point.x()) * size / 2;
        int y = plotRect.top() + (1.0 - point.y()) * size / 2;
        drawPoints.append(QPoint(x, y));
    }
    painter.setPen(QPen(Qt::green, 2));
    if (!drawPoints.isEmpty()) painter.drawPoints(drawPoints.constData(), drawPoints.size());
}
