#include "mainwindow.h"
#include <QPainter>
#include <QDebug>
#include <QAudioFormat>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QFileInfo>
#include <QLinearGradient>
#include <QApplication>
#include <QStyle>
MainWindow::MainWindow(const QString &filePath, QWidget *parent)
    : QMainWindow(parent), decoder(nullptr), playbackTimer(nullptr),
      player(new QMediaPlayer(this)), audioOutput(new QAudioOutput(this))
{
    resize(800, 600);
    setWindowTitle("Audio XY Oscilloscope");
    if (!filePath.isEmpty()) {
        setAudioFile(filePath);
    }
}
MainWindow::~MainWindow()
{
    if (decoder) {
        decoder->stop();
        delete decoder;
    }
    if (playbackTimer) {
        playbackTimer->stop();
        delete playbackTimer;
    }
}
void MainWindow::setAudioFile(const QString &filePath)
{
    currentFilePath = filePath;
    if (!QFile::exists(currentFilePath)) {
        showError("文件不存在: " + currentFilePath);
        return;
    }
    if (decoder) {
        decoder->stop();
        delete decoder;
        decoder = nullptr;
    }
    if (playbackTimer) {
        playbackTimer->stop();
        delete playbackTimer;
        playbackTimer = nullptr;
    }
    player->stop();
    player->setSource(QUrl());
    allPoints.clear();
    visiblePoints.clear();
    audioPosition = 0;
    initializeAudio(currentFilePath);
}
void MainWindow::initializeAudio(const QString &filePath)
{
    player->setAudioOutput(audioOutput);
    player->setSource(QUrl::fromLocalFile(filePath));
    connect(player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        audioPosition = pos;
        update();
    });
    decoder = new QAudioDecoder(this);
    decoder->setSource(QUrl::fromLocalFile(filePath));
    connect(decoder, &QAudioDecoder::bufferReady, this, &MainWindow::processBuffer);
    connect(decoder, &QAudioDecoder::finished, this, [=]() {
        if (allPoints.isEmpty()) {
            showError("未能解码音频数据。可能格式不支持或文件损坏");
            return;
        }
        qDebug() << "总共点数:" << allPoints.size();
        totalDuration = player->duration();
        playbackTimer = new QTimer(this);
        connect(playbackTimer, &QTimer::timeout, this, &MainWindow::updatePlayback);
        playbackTimer->start(16);
        playbackPositionTimer.start();
        player->play();
    });
    decoder->start();
}
void MainWindow::processBuffer()
{
    QAudioBuffer buffer = decoder->read();
    if (!buffer.isValid()) return;
    QAudioFormat format = buffer.format();
    if (format.sampleFormat() == QAudioFormat::Int16 &&
        format.channelCount() == 2) {
        const qint16 *data = buffer.constData<qint16>();
        int sampleCount = buffer.sampleCount();
        for (int i = 0; i < sampleCount; i +=2) {
            qreal left = data[i] / 32768.0;
            qreal right = data[i+1] / 32768.0;
            allPoints.append(QPointF(left, right));
        }
    } else {
        qDebug() << "不支持的音频格式:" << format;
    }
}
void MainWindow::updatePlayback()
{
    if (playbackPositionTimer.isValid()) {
        audioPosition = qMin(playbackPositionTimer.elapsed(), totalDuration);
    }
    int samplePos = (audioPosition * SAMPLE_RATE) / 1000;
    if (samplePos >= allPoints.size()) {
        if (player->playbackState() == QMediaPlayer::PlayingState) {
            player->pause();
        }
        return;
    }
    int start = qMax(0, samplePos - 750);
    int end = qMin(allPoints.size(), samplePos + 750);
    visiblePoints.clear();
    visiblePoints.reserve(end - start);
    for (int i = start; i < end; ++i) {
        visiblePoints.append(allPoints[i]);
    }
    update();
}
void MainWindow::showError(const QString &message)
{
    qDebug() << "错误:" << message;
    update();
    if (decoder) {
        decoder->stop();
    }
    if (playbackTimer) {
        playbackTimer->stop();
    }
    player->stop();
}
void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::black);
    if (currentFilePath.isEmpty()) {
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 14));
        painter.drawText(rect(), Qt::AlignCenter, "音频无法打开,请重新启动程序");
        return;
    }
    int size = qMin(width(), height());
    int xOffset = (width() - size) / 2;
    int yOffset = (height() - size) / 2;
    QRect plotRect(xOffset, yOffset, size, size);
    painter.setPen(QPen(Qt::gray, 1, Qt::DotLine));
    painter.drawLine(plotRect.left(), plotRect.center().y(),
                    plotRect.right(), plotRect.center().y());
    painter.drawLine(plotRect.center().x(), plotRect.top(),
                    plotRect.center().x(), plotRect.bottom());
    if (!visiblePoints.isEmpty()) {
        QVector<QPoint> drawPoints;
        drawPoints.reserve(visiblePoints.size());
        for (const QPointF &point : visiblePoints) {
            int x = plotRect.left() + (1.0 + point.x()) * size / 2;
            int y = plotRect.top() + (1.0 - point.y()) * size / 2;
            drawPoints.append(QPoint(x, y));
        }
        QLinearGradient gradient(plotRect.left(), plotRect.top(), plotRect.right(), plotRect.bottom());
        gradient.setColorAt(0, Qt::green);
        gradient.setColorAt(0.5, Qt::cyan);
        gradient.setColorAt(1, Qt::blue);
        painter.setPen(QPen(gradient, 1.5));
        painter.drawPoints(drawPoints.constData(), drawPoints.size());
    }
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 9));
    painter.drawText(20, 30,
                    QString("文件: %1 | 点数: %2 | 时间: %3 ms")
                    .arg(QFileInfo(currentFilePath).fileName())
                    .arg((audioPosition * SAMPLE_RATE) / 1000)
                    .arg(audioPosition));
}
