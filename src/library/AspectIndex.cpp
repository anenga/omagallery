#include "library/AspectIndex.h"

#include <QImageIOHandler>
#include <QImageReader>
#include <QtConcurrent/QtConcurrentRun>

namespace {
constexpr int kBatchSize = 200;
}

AspectIndex::AspectIndex(QObject* parent) : QObject(parent) {
  m_kick.setSingleShot(true);
  m_kick.setInterval(0);
  connect(&m_kick, &QTimer::timeout, this, &AspectIndex::startBatch);
  connect(&m_watcher, &QFutureWatcher<QList<double>>::finished, this,
          &AspectIndex::batchFinished);
}

AspectIndex::~AspectIndex() {
  requestStop();
  m_watcher.waitForFinished();
}

void AspectIndex::requestStop() {
  m_stop->store(true);
  m_pending.clear();
}

double AspectIndex::readRatio(const QString& path) {
  QImageReader reader(path);
  reader.setAutoTransform(true);
  const QSize size = reader.size();
  if (size.width() <= 0 || size.height() <= 0) {
    return 0;
  }
  const bool turned = reader.transformation() & QImageIOHandler::TransformationRotate90;
  return turned ? double(size.height()) / size.width() : double(size.width()) / size.height();
}

double AspectIndex::ratio(const QString& path, qint64 stamp, bool video, bool document) {
  if (video) {
    return kVideoRatio;
  }
  if (document) {
    return kDocumentRatio;
  }
  const QString key = path + QChar(0x1f) + QString::number(stamp);
  const auto known = m_ratios.constFind(key);
  if (known != m_ratios.constEnd()) {
    return known.value() > 0 ? known.value() : kPhotoRatio;
  }
  if (!m_stop->load() && !path.isEmpty() && !m_inFlight.contains(key)) {
    m_pending.insert(key, path);
    if (!m_watcher.isRunning()) {
      m_kick.start();
    }
  }
  return kPhotoRatio;
}

void AspectIndex::startBatch() {
  if (m_watcher.isRunning() || m_pending.isEmpty() || m_stop->load()) {
    return;
  }
  m_batch.clear();
  QStringList paths;
  auto it = m_pending.begin();
  while (it != m_pending.end() && m_batch.size() < kBatchSize) {
    m_batch.append(it.key());
    paths.append(it.value());
    m_inFlight.insert(it.key());
    it = m_pending.erase(it);
  }
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
  bool changed = false;
  for (int i = 0; i < m_batch.size() && i < results.size(); ++i) {
    const QString& key = m_batch.at(i);
    m_inFlight.remove(key);
    // Unreadable files are remembered too, so they are not read again.
    m_ratios.insert(key, results.at(i));
    changed = changed || qAbs(results.at(i) - kPhotoRatio) > 0.01;
  }
  m_batch.clear();
  if (changed && !m_stop->load()) {
    ++m_revision;
    emit ratiosChanged();
  }
  if (!m_pending.isEmpty()) {
    m_kick.start();
  }
}
