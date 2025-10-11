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
    REFERENCE_TIME bufferDuration = 1000000; // 100ms
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
    updateTimer->start(16); // ~60 FPS
    
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
                    Sleep(1);
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
                Sleep(1);
                continue;
            }
            
            // 修正：正确处理静音标志
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                // 静音时添加零点
                for (UINT32 i = 0; i < numFramesAvailable; i++) {
                    points.append(QPointF(0, 0));
                }
            } else {
                // 处理音频数据
                if (deviceFormat->wFormatTag == WAVE_FORMAT_PCM && deviceFormat->wBitsPerSample == 16) {
                    const int16_t *samples = reinterpret_cast<const int16_t*>(pData);
                    int sampleCount = numFramesAvailable * deviceFormat->nChannels;
                    
                    // 修正：正确处理单声道音频
                    if (deviceFormat->nChannels == 1) {
                        for (int i = 0; i < sampleCount; i++) {
                            float sample = samples[i] / 32768.0f;
                            points.append(QPointF(sample, sample));
                        }
                    } else {
                        // 立体声：左声道为X，右声道为Y
                        for (int i = 0; i < sampleCount; i += deviceFormat->nChannels) {
                            float left = samples[i] / 32768.0f;
                            float right = (deviceFormat->nChannels > 1) ? samples[i + 1] / 32768.0f : left;
                            
                            // 应用坐标变换
#if TX
                            left = -left;
#endif
#if TY
                            right = -right;
#endif
                            
                            points.append(QPointF(left, right));
                        }
                    }
                }
                else if ((deviceFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && deviceFormat->wBitsPerSample == 32) ||
                         (deviceFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && 
                          reinterpret_cast<WAVEFORMATEXTENSIBLE*>(deviceFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
                    const float *floatSamples = reinterpret_cast<const float*>(pData);
                    int sampleCount = numFramesAvailable * deviceFormat->nChannels;
                    
                    if (deviceFormat->nChannels == 1) {
                        for (int i = 0; i < sampleCount; i++) {
                            float sample = qMax(-1.0f, qMin(1.0f, floatSamples[i]));
                            points.append(QPointF(sample, sample));
                        }
                    } else {
                        for (int i = 0; i < sampleCount; i += deviceFormat->nChannels) {
                            float left = qMax(-1.0f, qMin(1.0f, floatSamples[i]));
                            float right = (deviceFormat->nChannels > 1) ? qMax(-1.0f, qMin(1.0f, floatSamples[i + 1])) : left;
                            
#if TX
                            left = -left;
#endif
#if TY
                            right = -right;
#endif
                            
                            points.append(QPointF(left, right));
                        }
                    }
                }
                else if (deviceFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && 
                         reinterpret_cast<WAVEFORMATEXTENSIBLE*>(deviceFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_PCM && 
                         deviceFormat->wBitsPerSample == 32) {
                    const int32_t *samples = reinterpret_cast<const int32_t*>(pData);
                    int sampleCount = numFramesAvailable * deviceFormat->nChannels;
                    
                    if (deviceFormat->nChannels == 1) {
                        for (int i = 0; i < sampleCount; i++) {
                            float sample = qMax(-1.0f, qMin(1.0f, samples[i] / 2147483648.0f));
                            points.append(QPointF(sample, sample));
                        }
                    } else {
                        for (int i = 0; i < sampleCount; i += deviceFormat->nChannels) {
                            float left = qMax(-1.0f, qMin(1.0f, samples[i] / 2147483648.0f));
                            float right = (deviceFormat->nChannels > 1) ? qMax(-1.0f, qMin(1.0f, samples[i + 1] / 2147483648.0f)) : left;
                            
#if TX
                            left = -left;
#endif
#if TY
                            right = -right;
#endif
                            
                            points.append(QPointF(left, right));
                        }
                    }
                }
                else {
                    QMessageBox::critical(nullptr, "错误", "不支持的音频格式");
                    isCapturing = false;
                    break;
                }
            }
            
            // 修正：限制点数量，避免内存过度增长
            if (points.size() > KEEPPOINT * 2) {
                points = points.mid(points.size() - KEEPPOINT);
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
    
    // 绘制网格
    painter.setPen(QPen(QColor(50, 50, 50), 1, Qt::DotLine));
    for (int i = 1; i < 4; i++) {
        int xPos = plotRect.left() + i * size / 4;
        int yPos = plotRect.top() + i * size / 4;
        painter.drawLine(xPos, plotRect.top(), xPos, plotRect.bottom());
        painter.drawLine(plotRect.left(), yPos, plotRect.right(), yPos);
    }
    
    // 修正：只绘制最近的点，避免性能问题
    int startIndex = qMax(0, points.size() - KEEPPOINT);
    QVector<QPoint> drawPoints;
    drawPoints.reserve(points.size() - startIndex);
    
    for (int i = startIndex; i < points.size(); i++) {
        const QPointF &point = points[i];
        
        // 修正：正确的坐标转换
        // X-Y模式：左声道控制X轴，右声道控制Y轴
        int x = plotRect.left() + (1.0 + point.x()) * size / 2;
        int y = plotRect.top() + (1.0 - point.y()) * size / 2; // Y轴需要翻转
        
        // 确保点在绘图区域内
        x = qMax(plotRect.left(), qMin(plotRect.right(), x));
        y = qMax(plotRect.top(), qMin(plotRect.bottom(), y));
        
        drawPoints.append(QPoint(x, y));
    }
    
    // 修正：使用绘制线条而不是单独的点，获得更好的视觉效果
    if (!drawPoints.isEmpty()) {
        painter.setPen(QPen(Qt::green, 1.5));
        
        // 对于大量点，可以绘制折线获得连续效果
        if (drawPoints.size() > 1) {
            for (int i = 1; i < drawPoints.size(); i++) {
                painter.drawLine(drawPoints[i-1], drawPoints[i]);
            }
        } else {
            painter.drawPoints(drawPoints.constData(), drawPoints.size());
        }
    }
    
    // 绘制边框和标题
    painter.setPen(QPen(Qt::white, 2));
    painter.drawRect(plotRect);
    painter.setFont(QFont("Arial", 10));
    painter.drawText(plotRect.adjusted(5, 5, -5, -5), Qt::AlignTop | Qt::AlignLeft, "X-Y Oscilloscope");
}
