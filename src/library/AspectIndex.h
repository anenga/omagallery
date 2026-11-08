#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <atomic>
#include <memory>

// The shape of each picture, for the gallery's masonry columns.
//
// Only image headers are read, never pixels, always off the GUI thread, and
// in library order so the top of the gallery settles first. Until a file has
// been read its kind's usual shape stands in. Shapes are kept on disk, so a
// library seen before lays out right from the first frame.
class AspectIndex final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int revision READ revision NOTIFY ratiosChanged)

public:
  // persistent: keep shapes in the cache between runs (off for the demo).
  explicit AspectIndex(QObject* parent = nullptr, bool persistent = false);
  ~AspectIndex() override;
  void requestStop();

  [[nodiscard]] int revision() const { return m_revision; }

  // Width over height. Unknown images are queued for reading. The stamp is
  // the file's mtime, so a rotated or rewritten picture is read again.
  [[nodiscard]] double ratio(const QString& path, qint64 stamp, bool video, bool document);

  // A shape seen another way, such as from a decoded thumbnail. Covers what
  // headers cannot tell: videos, PDFs, camera raws and unusual formats.
  Q_INVOKABLE void learn(const QString& path, qint64 stamp, double ratio);

  // What an image header says, with EXIF rotation applied. 0 when unreadable.
  // The file is read through plain reads, never mapped, so a network share
  // that drops out mid-read gives an error instead of killing the process.
  [[nodiscard]] static double readRatio(const QString& path);

  [[nodiscard]] static QString cachePath();

  static constexpr double kPhotoRatio = 1.5;
  static constexpr double kVideoRatio = 16.0 / 9.0;
  static constexpr double kDocumentRatio = 0.7071;

signals:
  // Coalesced: at most about once a second while a library is being read.
  void ratiosChanged();

private:
  void startBatch();
  void batchFinished();
  void changed();
  void loadCache();
  void saveCache();

  QHash<QString, double> m_ratios;
  // Library order: the first rows asked for are read first.
  QList<QPair<QString, QString>> m_queue;
  QSet<QString> m_queued;
  QFutureWatcher<QList<double>> m_watcher;
  QStringList m_batch;
  QTimer m_kick;
  QTimer m_notify;
  QTimer m_saveTimer;
  std::shared_ptr<std::atomic_bool> m_stop = std::make_shared<std::atomic_bool>(false);
  int m_revision = 0;
  bool m_persistent = false;
  bool m_dirty = false;
};
