#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCloseEvent>
#include <QButtonGroup>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      m_timer(new QTimer(this)),
      m_totalFiles(0),
      m_finishedFiles(0),
      m_processingRunning(false),
      m_pauseActive(false)
{
    ui->setupUi(this);

    ui->maskEdit->setText("*.txt");
    ui->hexKeyEdit->setText("1234567890ABCDEF");
    ui->intervalSpinBox->setRange(1, 86400);
    ui->intervalSpinBox->setValue(10);
    ui->overwriteRadio->setChecked(true);
    ui->onceRadio->setChecked(true);
    ui->deleteInputCheck->setChecked(false);
    ui->pauseButton->setEnabled(false);
    ui->stopButton->setEnabled(false);
    ui->fileProgressBar->setRange(0, 100);
    ui->totalProgressBar->setRange(0, 100);
    ui->statusLabel->setText("Готово");

    ui->overwriteRadio->setAutoExclusive(false);
    ui->renameRadio->setAutoExclusive(false);
    ui->onceRadio->setAutoExclusive(false);
    ui->timerRadio->setAutoExclusive(false);

    QButtonGroup *conflictGroup = new QButtonGroup(this);
    conflictGroup->setExclusive(true);
    conflictGroup->addButton(ui->overwriteRadio);
    conflictGroup->addButton(ui->renameRadio);

    QButtonGroup *modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addButton(ui->onceRadio);
    modeGroup->addButton(ui->timerRadio);

    connect(ui->browseInputButton, &QPushButton::clicked, this, &MainWindow::browseInputDirectory);
    connect(ui->browseOutputButton, &QPushButton::clicked, this, &MainWindow::browseOutputDirectory);
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::startClicked);
    connect(ui->pauseButton, &QPushButton::clicked, this, &MainWindow::pauseClicked);
    connect(ui->stopButton, &QPushButton::clicked, this, &MainWindow::stopClicked);
    connect(ui->timerRadio, &QRadioButton::toggled, this, [this]() { updateControls(); });
    connect(m_timer, &QTimer::timeout, this, &MainWindow::runProcessing);

    updateControls();
}

MainWindow::~MainWindow()
{
    stopWorkerAndWait();
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    stopWorkerAndWait();
    event->accept();
}

void MainWindow::browseInputDirectory()
{
    const QString dir = QFileDialog::getExistingDirectory(this, "Выберите папку поиска", ui->inputDirEdit->text());
    if (!dir.isEmpty()) {
        ui->inputDirEdit->setText(dir);
        if (ui->outputDirEdit->text().isEmpty()) {
            ui->outputDirEdit->setText(dir);
        }
    }
}

void MainWindow::browseOutputDirectory()
{
    const QString dir = QFileDialog::getExistingDirectory(this, "Выберите папку результата", ui->outputDirEdit->text());
    if (!dir.isEmpty()) {
        ui->outputDirEdit->setText(dir);
    }
}

void MainWindow::startClicked()
{
    ProcessingSettings settings;
    if (!buildSettings(&settings)) {
        return;
    }

    if (ui->timerRadio->isChecked()) {
        m_timer->start(ui->intervalSpinBox->value() * 1000);
        ui->statusLabel->setText("Таймер запущен");
    } else {
        m_timer->stop();
    }

    runProcessing();
    updateControls();
}

void MainWindow::pauseClicked()
{
    if (!m_processor) {
        return;
    }

    if (m_pauseActive) {
        m_processor->resume();
        m_pauseActive = false;
        ui->pauseButton->setText("Пауза");
        ui->statusLabel->setText("Обработка продолжена");
    } else {
        m_processor->pause();
        m_pauseActive = true;
        ui->pauseButton->setText("Продолжить");
        ui->statusLabel->setText("Пауза");
    }
}

void MainWindow::stopClicked()
{
    m_timer->stop();
    if (m_processor) {
        m_processor->stop();
        ui->statusLabel->setText("Остановка...");
    }
    updateControls();
}

void MainWindow::runProcessing()
{
    if (m_processingRunning) {
        return;
    }

    ProcessingSettings settings;
    if (!buildSettings(&settings)) {
        m_timer->stop();
        updateControls();
        return;
    }

    settings.processedKeys = m_processedKeys;

    m_processingRunning = true;
    m_pauseActive = false;
    m_totalFiles = 0;
    m_finishedFiles = 0;
    ui->pauseButton->setText("Пауза");
    ui->fileProgressBar->setValue(0);
    ui->totalProgressBar->setValue(0);
    ui->currentFileLabel->setText("-");
    ui->statusLabel->setText("Поиск файлов...");
    updateControls();

    QThread *thread = new QThread(this);
    FileProcessor *processor = new FileProcessor();
    processor->moveToThread(thread);

    m_workerThread = thread;
    m_processor = processor;

    connect(thread, &QThread::started, processor, [processor, settings]() {
        processor->process(settings);
    });
    connect(processor, &FileProcessor::started, this, &MainWindow::onWorkerStarted);
    connect(processor, &FileProcessor::fileStarted, this, &MainWindow::onFileStarted);
    connect(processor, &FileProcessor::progress, this, &MainWindow::onProgress);
    connect(processor, &FileProcessor::fileFinished, this, &MainWindow::onFileFinished);
    connect(processor, &FileProcessor::statusChanged, ui->statusLabel, &QLabel::setText);
    connect(processor, &FileProcessor::finished, this, &MainWindow::onWorkerFinished);
    connect(processor, &FileProcessor::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, processor, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
}

void MainWindow::onWorkerStarted(int totalFiles, qint64 totalBytes)
{
    m_totalFiles = totalFiles;
    m_finishedFiles = 0;
    ui->totalProgressBar->setValue(totalBytes == 0 ? 100 : 0);
    ui->statusLabel->setText(QString("Найдено файлов: %1").arg(totalFiles));
}

void MainWindow::onFileStarted(const QString &filePath, qint64 fileSize)
{
    ui->fileProgressBar->setValue(fileSize == 0 ? 100 : 0);
    ui->currentFileLabel->setText(QFileInfo(filePath).fileName());
    ui->statusLabel->setText(QString("Обработка: %1").arg(filePath));
}

void MainWindow::onProgress(qint64 fileDone, qint64 fileTotal, qint64 totalDone, qint64 totalBytes)
{
    const int filePercent = fileTotal > 0 ? static_cast<int>((fileDone * 100) / fileTotal) : 100;
    const int totalPercent = totalBytes > 0 ? static_cast<int>((totalDone * 100) / totalBytes) : 100;
    ui->fileProgressBar->setValue(filePercent);
    ui->totalProgressBar->setValue(totalPercent);
}

void MainWindow::onFileFinished(const QString &inputPath,
                                const QString &outputPath,
                                const QString &inputKey,
                                const QString &outputKey,
                                bool success,
                                const QString &message)
{
    ++m_finishedFiles;
    if (success) {
        m_processedKeys.insert(inputKey);
        if (!outputKey.isEmpty()) {
            m_processedKeys.insert(outputKey);
        }
    }

    ui->statusLabel->setText(QString("%1/%2: %3 -> %4 (%5)")
                                 .arg(m_finishedFiles)
                                 .arg(m_totalFiles)
                                 .arg(QFileInfo(inputPath).fileName())
                                 .arg(QFileInfo(outputPath).fileName())
                                 .arg(message));
}

void MainWindow::onWorkerFinished(bool stopped)
{
    m_processingRunning = false;
    m_pauseActive = false;
    m_processor = nullptr;
    m_workerThread = nullptr;
    ui->pauseButton->setText("Пауза");

    if (stopped) {
        ui->statusLabel->setText("Обработка остановлена");
    } else if (m_totalFiles == 0) {
        ui->statusLabel->setText("Подходящих файлов нет");
    } else {
        ui->statusLabel->setText("Обработка завершена");
    }

    updateControls();
}

bool MainWindow::buildSettings(ProcessingSettings *settings)
{
    const QString inputDir = ui->inputDirEdit->text().trimmed();
    const QString outputDir = ui->outputDirEdit->text().trimmed();
    const QString mask = ui->maskEdit->text().trimmed();
    QString hexKey = ui->hexKeyEdit->text().trimmed();
    if (hexKey.startsWith("0x", Qt::CaseInsensitive)) {
        hexKey = hexKey.mid(2);
    }

    if (inputDir.isEmpty() || !QDir(inputDir).exists()) {
        QMessageBox::warning(this, "Ошибка настроек", "Укажите существующую папку поиска файлов.");
        return false;
    }
    if (outputDir.isEmpty()) {
        QMessageBox::warning(this, "Ошибка настроек", "Укажите папку для сохранения результата.");
        return false;
    }
    if (mask.isEmpty()) {
        QMessageBox::warning(this, "Ошибка настроек", "Укажите маску входных файлов.");
        return false;
    }

    const QRegularExpression hexPattern("^[0-9A-Fa-f]{16}$");
    if (!hexPattern.match(hexKey).hasMatch()) {
        QMessageBox::warning(this, "Ошибка настроек", "XOR-значение должно содержать ровно 16 hex-символов.");
        return false;
    }

    settings->inputDirectory = QFileInfo(inputDir).absoluteFilePath();
    settings->outputDirectory = QFileInfo(outputDir).absoluteFilePath();
    settings->fileMask = mask;
    settings->deleteInput = ui->deleteInputCheck->isChecked();
    settings->overwriteOutput = ui->overwriteRadio->isChecked();
    settings->xorKey = QByteArray::fromHex(hexKey.toLatin1());
    return true;
}

void MainWindow::updateControls()
{
    const bool active = m_processingRunning;
    ui->startButton->setEnabled(!active || ui->timerRadio->isChecked());
    ui->pauseButton->setEnabled(active);
    ui->stopButton->setEnabled(active || m_timer->isActive());
    ui->inputDirEdit->setEnabled(!active);
    ui->outputDirEdit->setEnabled(!active);
    ui->browseInputButton->setEnabled(!active);
    ui->browseOutputButton->setEnabled(!active);
    ui->maskEdit->setEnabled(!active);
    ui->deleteInputCheck->setEnabled(!active);
    ui->overwriteRadio->setEnabled(!active);
    ui->renameRadio->setEnabled(!active);
    ui->onceRadio->setEnabled(!active);
    ui->timerRadio->setEnabled(!active);
    ui->intervalSpinBox->setEnabled(!active && ui->timerRadio->isChecked());
    ui->hexKeyEdit->setEnabled(!active);
}

void MainWindow::stopWorkerAndWait()
{
    m_timer->stop();
    if (m_processor) {
        m_processor->stop();
    }
    if (m_workerThread) {
        m_workerThread->wait();
    }
}
