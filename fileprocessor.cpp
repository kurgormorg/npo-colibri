#include "fileprocessor.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace {
const qint64 BufferSize = 4 * 1024 * 1024;
}

FileProcessor::FileProcessor(QObject *parent)
    : QObject(parent),
      m_pauseRequested(false),
      m_stopRequested(false)
{
}

void FileProcessor::pause()
{
    QMutexLocker locker(&m_mutex);
    m_pauseRequested = true;
}

void FileProcessor::resume()
{
    QMutexLocker locker(&m_mutex);
    m_pauseRequested = false;
    m_pauseCondition.wakeAll();
}

void FileProcessor::stop()
{
    QMutexLocker locker(&m_mutex);
    m_stopRequested = true;
    m_pauseRequested = false;
    m_pauseCondition.wakeAll();
}

void FileProcessor::process(const ProcessingSettings &settings)
{
    {
        QMutexLocker locker(&m_mutex);
        m_stopRequested = false;
        m_pauseRequested = false;
    }

    QList<QString> files;
    qint64 totalBytes = 0;
    const QStringList filters = nameFilters(settings.fileMask);
    QDirIterator it(settings.inputDirectory, filters, QDir::Files | QDir::Readable);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString key = processedKey(path);
        if (settings.processedKeys.contains(key)) {
            continue;
        }

        const QFileInfo info(path);
        files.append(path);
        totalBytes += info.size();
    }

    emit started(files.size(), totalBytes);

    qint64 totalDone = 0;
    bool stopped = false;
    for (const QString &path : files) {
        if (shouldStop()) {
            stopped = true;
            break;
        }

        const QFileInfo info(path);
        emit fileStarted(path, info.size());

        QString outputPath;
        QString message;
        const QString inputKey = processedKey(path);
        const bool ok = processFile(path, settings, &totalDone, totalBytes, &outputPath, &message);
        const QString outputKey = ok ? processedKey(outputPath) : QString();
        emit fileFinished(path, outputPath, inputKey, outputKey, ok, message);

        if (!ok && shouldStop()) {
            stopped = true;
            break;
        }
    }

    emit finished(stopped);
}

QStringList FileProcessor::nameFilters(const QString &mask) const
{
    QString normalized = mask.trimmed();
    if (normalized.isEmpty()) {
        normalized = "*";
    }

    QStringList result;
    const QStringList parts = normalized.split(QRegularExpression("[;,\\s]+"), Qt::SkipEmptyParts);
    for (QString part : parts) {
        part = part.trimmed();
        if (part.isEmpty()) {
            continue;
        }
        if (part.startsWith('.')) {
            part.prepend('*');
        }
        result.append(part);
    }

    return result.isEmpty() ? QStringList{"*"} : result;
}

QString FileProcessor::processedKey(const QString &filePath) const
{
    const QFileInfo info(filePath);
    return QString("%1|%2|%3")
        .arg(info.absoluteFilePath())
        .arg(info.size())
        .arg(info.lastModified().toMSecsSinceEpoch());
}

QString FileProcessor::outputPathFor(const QString &inputPath, const ProcessingSettings &settings) const
{
    const QFileInfo inputInfo(inputPath);
    const QDir outputDir(settings.outputDirectory);
    const QString basePath = outputDir.filePath(inputInfo.fileName());
    return settings.overwriteOutput ? basePath : uniqueOutputPath(basePath);
}

QString FileProcessor::uniqueOutputPath(const QString &path) const
{
    if (!QFileInfo::exists(path)) {
        return path;
    }

    const QFileInfo info(path);
    const QDir dir = info.dir();
    const QString baseName = info.completeBaseName();
    const QString suffix = info.completeSuffix();

    for (int counter = 1; ; ++counter) {
        const QString fileName = suffix.isEmpty()
            ? QString("%1_%2").arg(baseName).arg(counter)
            : QString("%1_%2.%3").arg(baseName).arg(counter).arg(suffix);
        const QString candidate = dir.filePath(fileName);
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
}

bool FileProcessor::waitIfPaused()
{
    QMutexLocker locker(&m_mutex);
    while (m_pauseRequested && !m_stopRequested) {
        m_pauseCondition.wait(&m_mutex);
    }
    return !m_stopRequested;
}

bool FileProcessor::shouldStop() const
{
    QMutexLocker locker(&m_mutex);
    return m_stopRequested;
}

bool FileProcessor::processFile(const QString &inputPath,
                                const ProcessingSettings &settings,
                                qint64 *totalDone,
                                qint64 totalBytes,
                                QString *outputPath,
                                QString *message)
{
    QFile input(inputPath);
    if (!input.open(QIODevice::ReadOnly)) {
        *message = QString("Не удалось открыть входной файл: %1").arg(input.errorString());
        return false;
    }

    QDir dir;
    if (!dir.mkpath(settings.outputDirectory)) {
        *message = "Не удалось создать папку для результата";
        return false;
    }

    const QString finalOutputPath = outputPathFor(inputPath, settings);
    *outputPath = finalOutputPath;

    const bool sameFile = QFileInfo(inputPath).absoluteFilePath() == QFileInfo(finalOutputPath).absoluteFilePath();
    const QString actualOutputPath = sameFile ? finalOutputPath + ".tmp" : finalOutputPath;
    if (sameFile) {
        QFile::remove(actualOutputPath);
    }

    QFile output(actualOutputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *message = QString("Не удалось открыть выходной файл: %1").arg(output.errorString());
        return false;
    }

    const qint64 fileSize = input.size();
    qint64 fileDone = 0;
    QByteArray buffer;
    buffer.resize(BufferSize);

    while (!input.atEnd()) {
        if (!waitIfPaused()) {
            *message = "Обработка остановлена";
            output.close();
            QFile::remove(actualOutputPath);
            return false;
        }

        const qint64 readBytes = input.read(buffer.data(), buffer.size());
        if (readBytes < 0) {
            *message = QString("Ошибка чтения: %1").arg(input.errorString());
            output.close();
            QFile::remove(actualOutputPath);
            return false;
        }

        for (qint64 i = 0; i < readBytes; ++i) {
            buffer[i] = static_cast<char>(buffer[i] ^ settings.xorKey.at((fileDone + i) % settings.xorKey.size()));
        }

        if (output.write(buffer.constData(), readBytes) != readBytes) {
            *message = QString("Ошибка записи: %1").arg(output.errorString());
            output.close();
            QFile::remove(actualOutputPath);
            return false;
        }

        fileDone += readBytes;
        *totalDone += readBytes;
        emit progress(fileDone, fileSize, *totalDone, totalBytes);
    }

    input.close();
    output.close();

    if (sameFile) {
        if (!QFile::remove(finalOutputPath) || !QFile::rename(actualOutputPath, finalOutputPath)) {
            *message = "Не удалось заменить исходный файл результатом";
            QFile::remove(actualOutputPath);
            return false;
        }
    }

    if (settings.deleteInput && !sameFile && !QFile::remove(inputPath)) {
        *message = "Файл обработан, но удалить входной файл не удалось";
        return false;
    }

    *message = "Готово";
    return true;
}
