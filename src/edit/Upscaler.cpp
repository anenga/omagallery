#include "edit/Upscaler.h"

#include <QColorSpace>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {

// A 12-megapixel scan takes a minute or two on an integrated GPU; give a
// slow machine room before calling it stuck.
constexpr int kTimeoutMs = 15 * 60 * 1000;

} // namespace

namespace Upscaler {

QString stoppedMessage() { return QStringLiteral("AI Uprez was stopped"); }

QString program() {
  return QStandardPaths::findExecutable(QString::fromLatin1(kProgram));
}

QString modelDirectory(const QString& programPath) {
  const QString model = QString::fromLatin1(kModel) + QStringLiteral(".param");
  QStringList candidates;
  if (!programPath.isEmpty()) {
    const QString binDir = QFileInfo(programPath).canonicalPath();
    candidates << binDir + QStringLiteral("/models");
    // /usr/bin/x -> /usr/share/x/models, the AUR packages' layout.
    candidates << QDir(binDir).absoluteFilePath(QStringLiteral("../share/") +
                                                QString::fromLatin1(kProgram) +
                                                QStringLiteral("/models"));
  }
  for (const QString& data : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
    candidates << data + QLatin1Char('/') + QString::fromLatin1(kProgram) + QStringLiteral("/models");
  }
  candidates << QStringLiteral("/opt/") + QString::fromLatin1(kProgram) + QStringLiteral("/models");
  for (const QString& candidate : std::as_const(candidates)) {
    if (QFileInfo::exists(candidate + QLatin1Char('/') + model)) {
      return QDir::cleanPath(candidate);
    }
  }
  return {};
}

QImage upscale(const QImage& image, int factor, qint64 maxPixels, QString* error,
               const std::function<void(qreal)>& progress, const std::atomic_bool* cancel) {
  const auto fail = [error](const QString& message) {
    if (error) *error = message;
    return QImage();
  };
  if (image.isNull()) return fail(QStringLiteral("Nothing to upscale"));
  factor = factor >= 4 ? 4 : 2;

  const qint64 sourcePixels = static_cast<qint64>(image.width()) * image.height();
  while (factor > 2 && sourcePixels * factor * factor > maxPixels) factor /= 2;
  if (sourcePixels * factor * factor > maxPixels) {
    return fail(QStringLiteral("This picture is already %1 megapixels; AI Uprez is for smaller scans")
                    .arg(sourcePixels / 1'000'000));
  }

  const QString tool = program();
  if (tool.isEmpty()) {
    return fail(QStringLiteral("Install %1 for AI Uprez").arg(QString::fromLatin1(kPackage)));
  }

  QTemporaryDir work;
  if (!work.isValid()) return fail(QStringLiteral("Could not make a private working folder"));
  const QString input = work.filePath(QStringLiteral("in.png"));
  const QString output = work.filePath(QStringLiteral("out.png"));
  {
    // The tool reads sRGB-ish 8-bit PNG; the colour space is put back after.
    QImageWriter writer(input, "png");
    writer.setCompression(1);
    if (!writer.write(image.convertToFormat(image.hasAlphaChannel() ? QImage::Format_ARGB32
                                                                    : QImage::Format_RGB32))) {
      return fail(QStringLiteral("Could not prepare the picture for upscaling"));
    }
  }

  QStringList arguments{QStringLiteral("-i"), input, QStringLiteral("-o"), output,
                        QStringLiteral("-n"), QString::fromLatin1(kModel),
                        QStringLiteral("-s"), QStringLiteral("4"),
                        QStringLiteral("-f"), QStringLiteral("png")};
  const QString models = modelDirectory(tool);
  if (!models.isEmpty()) arguments << QStringLiteral("-m") << models;

  QProcess process;
  process.setProgram(tool);
  process.setArguments(arguments);
  process.setStandardOutputFile(QProcess::nullDevice());
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start();
  if (!process.waitForStarted(10'000)) {
    return fail(QStringLiteral("Could not start %1").arg(QString::fromLatin1(kProgram)));
  }

  // Progress arrives on stderr as lines like "42.50%".
  static const QRegularExpression percent(QStringLiteral("(\\d+(?:\\.\\d+)?)%"));
  QByteArray log;
  QElapsedTimer clock;
  clock.start();
  while (process.state() != QProcess::NotRunning) {
    if (cancel && cancel->load()) {
      process.kill();
      process.waitForFinished(5'000);
      return fail(stoppedMessage());
    }
    if (clock.elapsed() > kTimeoutMs) {
      process.kill();
      process.waitForFinished(5'000);
      return fail(QStringLiteral("AI Uprez took too long and was stopped"));
    }
    process.waitForReadyRead(250);
    const QByteArray chunk = process.readAllStandardError();
    if (chunk.isEmpty()) continue;
    log += chunk;
    if (log.size() > 64 * 1024) log = log.right(16 * 1024);
    if (progress) {
      QRegularExpressionMatchIterator matches = percent.globalMatch(QString::fromLatin1(chunk));
      qreal last = -1;
      while (matches.hasNext()) last = matches.next().captured(1).toDouble();
      if (last >= 0) progress(qBound<qreal>(0.0, last / 100.0, 1.0));
    }
  }
  log += process.readAllStandardError();

  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
      !QFileInfo::exists(output)) {
    const QString detail = QString::fromLocal8Bit(log).trimmed().section(QLatin1Char('\n'), -1);
    return fail(detail.isEmpty()
                    ? QStringLiteral("AI Uprez failed; is a Vulkan GPU driver installed?")
                    : QStringLiteral("AI Uprez failed: %1").arg(detail.left(160)));
  }

  if (cancel && cancel->load()) return fail(stoppedMessage());
  QImage result(output);
  if (result.isNull()) return fail(QStringLiteral("Could not read the upscaled picture"));
  const QSize target = image.size() * factor;
  if (result.size() != target) {
    result = result.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }
  if (image.colorSpace().isValid()) result.setColorSpace(image.colorSpace());
  if (progress) progress(1.0);
  return result;
}

} // namespace Upscaler
