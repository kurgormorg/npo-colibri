#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "fileprocessor.h"

#include <QMainWindow>
#include <QPointer>
#include <QSet>

class QCloseEvent;
class QThread;
class QTimer;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void browseInputDirectory();
    void browseOutputDirectory();
    void startClicked();
    void pauseClicked();
    void stopClicked();
    void runProcessing();
    void onWorkerStarted(int totalFiles, qint64 totalBytes);
    void onFileStarted(const QString &filePath, qint64 fileSize);
    void onProgress(qint64 fileDone, qint64 fileTotal, qint64 totalDone, qint64 totalBytes);
    void onFileFinished(const QString &inputPath,
                        const QString &outputPath,
                        const QString &inputKey,
                        const QString &outputKey,
                        bool success,
                        const QString &message);
    void onWorkerFinished(bool stopped);

private:
    bool buildSettings(ProcessingSettings *settings);
    void updateControls();
    void stopWorkerAndWait();

    Ui::MainWindow *ui;
    QTimer *m_timer;
    QPointer<QThread> m_workerThread;
    QPointer<FileProcessor> m_processor;
    QSet<QString> m_processedKeys;
    int m_totalFiles;
    int m_finishedFiles;
    bool m_processingRunning;
    bool m_pauseActive;
};

#endif // MAINWINDOW_H
