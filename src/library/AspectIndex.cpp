#include "library/AspectIndex.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageIOHandler>
#include <QImageReader>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>

namespace {

constexpr int kBatchSize = 64;
constexpr quint32 kCacheMagic = 0x4f4d4153; // "OMAS"
constexpr quint16 kCacheVersion = 1;
constexpr quint32 kMaximumCacheEntries = 400000;

QString keyFor(const QString& path, qint64 stamp) {
  return path + QChar(0x1f) + QString::number(stamp);
}

// Forwards plain reads to a QFile. Qt's image plugins map a QFileDevice into
// memory; on a FUSE or SMB mount a read that fails behind that mapping is a
// SIGBUS. Through this device it is only a short read.
class ReadOnlyDevice final : public QIODevice {
public:
  explicit ReadOnlyDevice(const QString& path) : m_file(path) {}

  bool open(OpenMode mode) override {
    if (!m_file.open(QIODevice::ReadOnly)) {
      return false;
    }
    return QIODevice::open(mode & ~QIODevice::Text);
  }
  void close() override {
    m_file.close();
    QIODevice::close();
  }
  [[nodiscard]] qint64 size() const override { return m_file.size(); }
  bool seek(qint64 pos) override { return QIODevice::seek(pos) && m_file.seek(pos); }

protected:
  qint64 readData(char* data, qint64 maxSize) override {
    if (m_file.pos() != pos() && !m_file.seek(pos())) {
      return -1;
    }
    return m_file.read(data, maxSize);
  }
  qint64 writeData(const char*, qint64) override { return -1; }

private:
  QFile m_file;
};

QString cacheHome() {
  const QString configured = qEnvironmentVariable("XDG_CACHE_HOME");
  return configured.isEmpty() ? QDir::homePath() + QStringLiteral("/.cache") : configured;
}

} // namespace

AspectIndex::AspectIndex(QObject* parent, bool persistent)
    : QObject(parent), m_persistent(persistent) {
  m_kick.setSingleShot(true);
  m_kick.setInterval(0);
  connect(&m_kick, &QTimer::timeout, this, &AspectIndex::startBatch);
  m_notify.setSingleShot(true);
  m_notify.setInterval(1000);
  connect(&m_notify, &QTimer::timeout, this, [this] {
    ++m_revision;
    emit ratiosChanged();
  });
  m_saveTimer.setSingleShot(true);
  m_saveTimer.setInterval(5000);
  connect(&m_saveTimer, &QTimer::timeout, this, &AspectIndex::saveCache);
  connect(&m_watcher, &QFutureWatcher<QList<double>>::finished, this,
          &AspectIndex::batchFinished);
  if (m_persistent) {
    loadCache();
  }
}

AspectIndex::~AspectIndex() {
  requestStop();
  m_watcher.waitForFinished();
  saveCache();
}

void AspectIndex::requestStop() {
  m_stop->store(true);
  m_queue.clear();
  m_queued.clear();
  m_notify.stop();
}

QString AspectIndex::cachePath() {
  return cacheHome() + QStringLiteral("/omaroll/picture-shapes.index");
}

double AspectIndex::readRatio(const QString& path) {
  ReadOnlyDevice device(path);
  if (!device.open(QIODevice::ReadOnly)) {
    return 0;
  }
  QImageReader reader(&device);
  reader.setAutoTransform(true);
  const QSize size = reader.size();
  if (size.width() <= 0 || size.height() <= 0) {
    return 0;
  }
  const bool turned = reader.transformation() & QImageIOHandler::TransformationRotate90;
  return turned ? double(size.height()) / size.width() : double(size.width()) / size.height();
}

double AspectIndex::ratio(const QString& path, qint64 stamp, bool video, bool document) {
  const QString key = keyFor(path, stamp);
  const auto known = m_ratios.constFind(key);
  if (known != m_ratios.constEnd() && known.value() > 0) {
    return known.value();
  }
  if (video) {
    return kVideoRatio;
  }
  if (document) {
    return kDocumentRatio;
  }
  if (known != m_ratios.constEnd()) {
    return kPhotoRatio;
  }
  if (!m_stop->load() && !path.isEmpty() && !m_queued.contains(key)) {
    m_queue.append({key, path});
    m_queued.insert(key);
    if (!m_watcher.isRunning()) {
      m_kick.start();
    }
  }
  return kPhotoRatio;
}

void AspectIndex::learn(const QString& path, qint64 stamp, double ratio) {
  if (path.isEmpty() || !(ratio > 0) || m_stop->load()) {
    return;
  }
  const QString key = keyFor(path, stamp);
  const double known = m_ratios.value(key, 0);
  if (known > 0 && qAbs(known - ratio) / known < 0.02) {
    return;
  }
  m_ratios.insert(key, ratio);
  changed();
}

void AspectIndex::changed() {
  m_dirty = true;
  if (!m_notify.isActive()) {
    m_notify.start();
  }
  if (m_persistent && !m_saveTimer.isActive()) {
    m_saveTimer.start();
  }
}

void AspectIndex::startBatch() {
  if (m_watcher.isRunning() || m_queue.isEmpty() || m_stop->load()) {
    return;
  }
  m_batch.clear();
  QStringList paths;
  const int take = qMin(kBatchSize, int(m_queue.size()));
  for (int i = 0; i < take; ++i) {
    m_batch.append(m_queue.at(i).first);
    paths.append(m_queue.at(i).second);
  }
  m_queue.remove(0, take);
  const auto stop = m_stop;
  m_watcher.setFuture(QtConcurrent::run([paths, stop] {
    QList<double> out;
    out.reserve(paths.size());
    for (const QString& path : paths) {
      out.append(stop->load() ? 0.0 : readRatio(path));
    }
    return out;
  }));
}

void AspectIndex::batchFinished() {
  const QList<double> results = m_watcher.result();
  bool any = false;
  for (int i = 0; i < m_batch.size() && i < results.size(); ++i) {
    const QString& key = m_batch.at(i);
    m_queued.remove(key);
    // A shape already learned from the thumbnail stands.
    if (m_ratios.value(key, 0) > 0) {
      continue;
    }
    // Unreadable files are remembered too, so they are not read again.
    m_ratios.insert(key, results.at(i));
    any = any || qAbs(results.at(i) - kPhotoRatio) > 0.01;
    m_dirty = true;
  }
  m_batch.clear();
  if (m_stop->load()) {
    return;
  }
  if (any) {
    changed();
  } else if (m_dirty && m_persistent && !m_saveTimer.isActive()) {
    m_saveTimer.start();
  }
  if (!m_queue.isEmpty()) {
    m_kick.start();
  }
}

void AspectIndex::loadCache() {
  QFile file(cachePath());
  if (!file.open(QIODevice::ReadOnly)) {
    return;
  }
  QDataStream stream(&file);
  stream.setVersion(QDataStream::Qt_6_0);
  quint32 magic = 0;
  quint16 version = 0;
  quint32 count = 0;
  stream >> magic >> version >> count;
  if (magic != kCacheMagic || version != kCacheVersion || count > kMaximumCacheEntries) {
    return;
  }
  QHash<QString, double> loaded;
  loaded.reserve(count);
  for (quint32 i = 0; i < count; ++i) {
    QString key;
    float ratio = 0;
    stream >> key >> ratio;
    if (stream.status() != QDataStream::Ok || key.isEmpty()) {
      return;
    }
    loaded.insert(key, double(ratio));
  }
  m_ratios = std::move(loaded);
}

void AspectIndex::saveCache() {
  if (!m_persistent || !m_dirty) {
    return;
  }
  m_dirty = false;
  QDir().mkpath(QFileInfo(cachePath()).absolutePath());
  QSaveFile file(cachePath());
  if (!file.open(QIODevice::WriteOnly)) {
    return;
  }
  QDataStream stream(&file);
  stream.setVersion(QDataStream::Qt_6_0);
  const quint32 count = quint32(qMin<qsizetype>(m_ratios.size(), kMaximumCacheEntries));
  stream << kCacheMagic << kCacheVersion << count;
  quint32 written = 0;
  for (auto it = m_ratios.cbegin(); it != m_ratios.cend() && written < count; ++it, ++written) {
    stream << it.key() << float(it.value());
  }
  file.commit();
}
