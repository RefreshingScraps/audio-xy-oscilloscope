#include "mainwindow.h"
#ifndef KSDATAFORMAT_SUBTYPE_PCM
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM,0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
#endif
#ifndef KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT,0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
#endif

MainWindow::MainWindow(QWidget *parent): QMainWindow(parent), deviceEnumerator(nullptr), device(nullptr),audioClient(nullptr), captureClient(nullptr),isCapturing(false), deviceFormat(nullptr)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::ToolTip | Qt::WindowDoesNotAcceptFocus);
    setFocusPolicy(Qt::NoFocus);
    setGeometry(QApplication::primaryScreen()->geometry().width() - 410, 50, 360, 360);
    
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
    
    // 修正：设置合适的缓冲区大小
    REFERENCE_TIME bufferDuration = 100000; // 100ms
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 
                                bufferDuration, 0, deviceFormat, nullptr);
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
    updateTimer->start(33); // 约30fps
    
    std::thread([this]() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        BYTE *pData;
        UINT32 numFramesAvailable;
        DWORD flags;
        UINT64 devicePosition, qpcPosition;
        
        while (isCapturing) {
            HRESULT hr = captureClient->GetBuffer(&pData, &numFramesAvailable, &flags, &devicePosition, &qpcPosition);
            if (FAILED(hr)) {
                if (hr == AUDCLNT_S_BUFFER_EMPTY) {
                    Sleep(5);
                    continue;
                }
                if (hr == AUDCLNT_E_BUFFER_ERROR) {
                    audioClient->Stop();
                    audioClient->Reset();
                    audioClient->Start();
                }
                continue;
            }
            
            if (numFramesAvailable == 0) {
                captureClient->ReleaseBuffer(numFramesAvailable);
                Sleep(5);
                continue;
            }
            
            // 修正：处理静音标志
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                // 静音时添加零点
                for (UINT32 i = 0; i < numFramesAvailable; i++) {
                    points.append(QPointF(0.0f, 0.0f));
                    if (points.size() > KEEPPOINT) points.removeFirst();
                }
            } else {
                // 处理音频数据
                if (deviceFormat->wFormatTag == WAVE_FORMAT_PCM && deviceFormat->wBitsPerSample == 16) {
                    const int16_t *samples = reinterpret_cast<const int16_t*>(pData);
                    int sampleCount = numFramesAvailable * deviceFormat->nChannels;
                    
                    for (int i = 0; i < sampleCount; i += deviceFormat->nChannels) {
                        if (i + 1 >= sampleCount) break;
                        
                        float left = samples[i] / 32768.0f;
                        float right = samples[i + 1] / 32768.0f;
                        
                        // 修正：限制数值范围并应用反转
#if TX
                        left = -left;
#endif
#if TY
                        right = -right;
#endif
                        
                        // 修正：确保数值在有效范围内
                        left = qBound(-1.0f, left, 1.0f);
                        right = qBound(-1.0f, right, 1.0f);
                        
                        points.append(QPointF(left, right));
                        if (points.size() > KEEPPOINT) points.removeFirst();
                    }
                }
                else if ((deviceFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && deviceFormat->wBitsPerSample == 32) ||
                         (deviceFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && 
                          ((WAVEFORMATEXTENSIBLE*)deviceFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
                    const float *floatSamples = reinterpret_cast<const float*>(pData);
                    int sampleCount = numFramesAvailable * deviceFormat->nChannels;
                    
                    for (int i = 0; i < sampleCount; i += deviceFormat->nChannels) {
                        if (i + 1 >= sampleCount) break;
                        
                        float left = floatSamples[i];
                        float right = floatSamples[i + 1];
                        
#if TX
                        left = -left;
#endif
#if TY
                        right = -right;
#endif
                        
                        // 修正：确保数值在有效范围内
                        left = qBound(-1.0f, left, 1.0f);
                        right = qBound(-1.0f, right, 1.0f);
                        
                        points.append(QPointF(left, right));
                        if (points.size() > KEEPPOINT) points.removeFirst();
                    }
                }
                else if (deviceFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && 
                         ((WAVEFORMATEXTENSIBLE*)deviceFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_PCM && 
                         deviceFormat->wBitsPerSample == 32) {
                    const int32_t *samples = reinterpret_cast<const int32_t*>(pData);
                    int sampleCount = numFramesAvailable * deviceFormat->nChannels;
                    
                    for (int i = 0; i < sampleCount; i += deviceFormat->nChannels) {
                        if (i + 1 >= sampleCount) break;
                        
                        float left = samples[i] / 2147483648.0f;
                        float right = samples[i + 1] / 2147483648.0f;
                        
#if TX
                        left = -left;
#endif
#if TY
                        right = -right;
#endif
                        
                        left = qBound(-1.0f, left, 1.0f);
                        right = qBound(-1.0f, right, 1.0f);
                        
                        points.append(QPointF(left, right));
                        if (points.size() > KEEPPOINT) points.removeFirst();
                    }
                }
                else {
                    QMetaObject::invokeMethod(this, [this]() {
                        QMessageBox::critical(this, "错误", "不支持的音频格式");
                    });
                    isCapturing = false;
                    break;
                }
            }
            
            captureClient->ReleaseBuffer(numFramesAvailable);
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
    
    // 绘制半透明背景
    painter.fillRect(rect(), QColor(0, 0, 0, 128));
    
    if (points.isEmpty()) {
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 14));
        painter.drawText(rect(), Qt::AlignCenter, "等待音频输出...");
        return;
    }
    
    int size = qMin(width(), height()) - 20; // 留出边距
    QRect plotRect((width() - size) / 2, (height() - size) / 2, size, size);
    
    // 绘制坐标轴
    painter.setPen(QPen(Qt::gray, 1));
    painter.drawLine(plotRect.left(), plotRect.center().y(), plotRect.right(), plotRect.center().y()); // X轴
    painter.drawLine(plotRect.center().x(), plotRect.top(), plotRect.center().x(), plotRect.bottom()); // Y轴
    
    // 绘制边框
    painter.setPen(QPen(Qt::white, 2));
    painter.drawRect(plotRect);
    
    // 修正：使用更高效的方式绘制点
    QVector<QPoint> drawPoints;
    drawPoints.reserve(points.size());
    
    for (const QPointF &point : points) {
        // 修正：正确的坐标转换
        int x = plotRect.left() + (point.x() + 1.0f) * size / 2.0f;
        int y = plotRect.top() + (1.0f - point.y()) * size / 2.0f;
        
        // 确保点在绘图区域内
        if (x >= plotRect.left() && x <= plotRect.right() && y >= plotRect.top() && y <= plotRect.bottom()) {
            drawPoints.append(QPoint(x, y));
        }
    }
    
    // 修正：使用绘制点集而不是连线
    if (!drawPoints.isEmpty()) {
        painter.setPen(QPen(Qt::green, 1.5));
        painter.drawPoints(drawPoints.constData(), drawPoints.size());
    }
}
