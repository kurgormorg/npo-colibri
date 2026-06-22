#ifndef FILEPROCESSOR_H
#define FILEPROCESSOR_H

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QWaitCondition>

struct ProcessingSettings
{
    QString inputDirectory;
    QString outputDirectory;
    QString fileMask;
    bool deleteInput = false;
    bool overwriteOutput = true;
    QByteArray xorKey;
    QSet<QString> processedKeys;
};

class FileProcessor : public QObject
{
    Q_OBJECT

public:
    explicit FileProcessor(QObject *parent = nullptr);

    void pause();
    void resume();
    void stop();

public slots:
    void process(const ProcessingSettings &settings);

signals:
    void started(int totalFiles, qint64 totalBytes);
    void fileStarted(const QString &filePath, qint64 fileSize);
    void progress(qint64 fileDone, qint64 fileTotal, qint64 totalDone, qint64 totalBytes);
    void fileFinished(const QString &inputPath,
                      const QString &outputPath,
                      const QString &inputKey,
                      const QString &outputKey,
                      bool success,
                      const QString &message);
    void statusChanged(const QString &message);
    void finished(bool stopped);

private:
    QStringList nameFilters(const QString &mask) const;
    QString processedKey(const QString &filePath) const;
    QString outputPathFor(const QString &inputPath, const ProcessingSettings &settings) const;
    QString uniqueOutputPath(const QString &path) const;
    bool waitIfPaused();
    bool shouldStop() const;
    bool processFile(const QString &inputPath,
                     const ProcessingSettings &settings,
                     qint64 *totalDone,
                     qint64 totalBytes,
                     QString *outputPath,
                     QString *message);

    mutable QMutex m_mutex;
    QWaitCondition m_pauseCondition;
    bool m_pauseRequested;
    bool m_stopRequested;
};

#endif // FILEPROCESSOR_H
