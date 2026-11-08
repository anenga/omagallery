#pragma once

#include <QColorSpace>
#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>

class QTemporaryDir;

// Quick, non-destructive image corrections.
//
// Crop, rotate in quarter turns, flip and resize, written as a new file beside
// the original. The original is opened read-only and never modified: a copy is
// saved with a numbered name so repeat corrections do not clobber each other,
// exactly like the matte composer.
//
// Order is fixed and matters, because the preview and the writer have to agree:
//
//   1. EXIF orientation is baked into the pixels on read (autoTransform).
//   2. The user's quarter turns are applied.
//   3. Horizontal and vertical flips are applied.
//   4. The crop rectangle is taken, in the rotated-and-flipped frame the
//      preview showed.
//   5. The result is resized to the requested edge, preserving aspect ratio.
//
// Metadata policy: the ICC color profile travels with the pixels; other EXIF
// tags (including the orientation tag, now baked in) are not copied to the
// copy. That is deliberate: a rotated copy that still claimed the old
// orientation would display sideways everywhere else.
class ImageEditor final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
  // An upper bound on the output, so a mistyped resize cannot ask for an
  // allocation that takes the machine down. Preserving aspect means the real
  // result is usually well under this.
  static constexpr qint64 kMaxOutputPixels = 40'000'000;

  struct Transform {
    int quarterTurns = 0;
    bool flipHorizontal = false;
    bool flipVertical = false;
    // A fine straighten angle in degrees, applied after the quarter turns and
    // flips. The image is scaled to fill the frame so no empty corners show,
    // and cropped back to the original size.
    qreal straightenDegrees = 0.0;
    // Normalized (0..1) fractions of the rotated-and-flipped frame, matching
    // what the editor overlay shows. A non-positive width or height means the
    // whole frame.
    qreal cropX = 0.0;
    qreal cropY = 0.0;
    qreal cropW = 1.0;
    qreal cropH = 1.0;
    // Output edge lengths. Zero means "derive from the other, keeping aspect";
    // both zero means no resize.
    int targetWidth = 0;
    int targetHeight = 0;
  };

  explicit ImageEditor(QObject* parent = nullptr);

  [[nodiscard]] bool busy() const { return m_busy; }

  // The post-EXIF-orientation pixel size, so the editor can label output sizes
  // and offer sensible presets. Reads only the header plus the orientation tag.
  Q_INVOKABLE QSize orientedSize(const QString& path) const;
  // Why a sheet's preview of |path| is refused for its size, or empty. Reads
  // only the header.
  Q_INVOKABLE QString previewProblem(const QString& path) const;

  // Writes "<stem>-edited.<ext>" beside the original, asynchronously. Crop
  // values are fractions (0..1) of the rotated-and-flipped frame; width or
  // height of zero means the full frame. Emits saved() or failed().
  Q_INVOKABLE void saveCopy(const QString& path, int quarterTurns, bool flipHorizontal,
                            bool flipVertical, qreal straightenDegrees, qreal cropX, qreal cropY,
                            qreal cropWidth, qreal cropHeight, int targetWidth, int targetHeight,
                            int jobId = 0);

  // Writes "<stem>-edited.<ext>" with the one-click touch-ups (TouchUp::Step
  // flags: Smart Crop, Auto Fix) applied to the EXIF-oriented picture, then,
  // when |upscaleFactor| is 2 or 4, enlarged with Real-ESRGAN. Emits
  // touchUpProgress() while upscaling, then saved() or failed().
  //
  // With |replaceOriginal| the result takes the original's place instead:
  // it is written as a copy first, then the original goes to the Trash (so
  // it can always be restored) and the copy is renamed to its name. saved()
  // then carries the original's path.
  Q_INVOKABLE void saveTouchUp(const QString& path, int steps, int upscaleFactor = 0,
                               int jobId = 0, bool replaceOriginal = false);
  // The picture turned and flipped (no crop or resize) in the original's
  // place, the same way: losslessly for a plain JPEG, original to the Trash.
  Q_INVOKABLE void replaceWithTurn(const QString& path, int quarterTurns, bool flipHorizontal,
                                   bool flipVertical, int jobId = 0);
  // Runs the touch-ups and AI Uprez now, so the result can be looked at
  // before it is saved. Emits touchUpProgress() while the tool runs, then
  // upscaleReady() with a file:// URL of the result (a private temporary PNG)
  // or failed(). The next saveTouchUp() for the same file, steps and factor
  // writes that result instead of upscaling again.
  Q_INVOKABLE void generateUpscale(const QString& path, int steps, int factor, int jobId = 0);
  // Stops a running generateUpscale() (nothing is emitted for it afterwards)
  // and forgets the generated result.
  Q_INVOKABLE void discardUpscale();
  // Whether a generated result for exactly this is waiting to be saved.
  Q_INVOKABLE bool hasGeneratedUpscale(const QString& path, int steps, int factor) const;

  // Whether |path| can be replaced by a corrected version in its own format:
  // not a raw, not animated, and a format Qt can write back.
  Q_INVOKABLE bool canReplace(const QString& path) const;
  // Whether the Real-ESRGAN command is installed; checked each call, so
  // installing it while Omaroll runs is picked up.
  Q_INVOKABLE bool upscalerAvailable() const;
  Q_INVOKABLE QString upscalerPackage() const;

  // Copies the selected region (same pipeline, no resize) to the clipboard.
  // Emits copied() or failed(). Nothing is written to disk.
  Q_INVOKABLE void copyRegion(const QString& path, int quarterTurns, bool flipHorizontal,
                              bool flipVertical, qreal straightenDegrees, qreal cropX, qreal cropY,
                              qreal cropWidth, qreal cropHeight, int jobId = 0);

  // Writes a copy of every path with the same rotate/flip/resize (no crop and
  // no straighten), one at a time, each with the existing collision handling.
  // Emits batchProgress after each file and batchFinished at the end.
  Q_INVOKABLE void saveCopies(const QStringList& paths, int quarterTurns, bool flipHorizontal,
                              bool flipVertical, int targetWidth, int targetHeight, int jobId = 0);

  Q_INVOKABLE int nextJobId() { return ++m_nextJobId; }
  Q_INVOKABLE bool isAnimated(const QString& path) const;
  [[nodiscard]] static bool needsNativeExport(const QString& source);
  [[nodiscard]] static QVariantMap exportImage(const QString& source,
                                               const QString& preferred,
                                               const QByteArray& format,
                                               int edge);

  // The pure pipeline, shared with the preview provider and unit tests.
  [[nodiscard]] static QImage apply(const QImage& source, const Transform& transform);

  // Where the copy would go, ignoring collisions at the time it is called.
  [[nodiscard]] static QString outputPathFor(const QString& sourcePath);

  // The first "<stem>-edited[-N].<ext>" that is free.
  [[nodiscard]] static QString availableOutputPath(const QString& sourcePath);

signals:
  void saved(const QString& outputPath, int jobId);
  void copied(int jobId);
  void failed(const QString& message, int jobId);
  void busyChanged();
  void batchProgress(int done, int total, int jobId);
  void touchUpProgress(qreal fraction, int jobId);
  void upscaleReady(const QString& previewUrl, const QSize& size, int jobId);
  // A save over the original failed after its copy was written; the copy
  // stays beside the original. failed() follows with why.
  void copyKept(const QString& outputPath, int jobId);
  void batchFinished(int succeeded, int failed, const QVariantList& results, int jobId);

private:
  // An upscaled result generated ahead of saving. The folder is shared with
  // any save reading it, so discarding never pulls the file from under one.
  struct Generated {
    QString source;
    QDateTime sourceModified;
    qint64 sourceBytes = -1;
    int steps = 0;
    int factor = 0;
    std::shared_ptr<QTemporaryDir> folder;
    QString file;
    QColorSpace colorSpace;
  };
  [[nodiscard]] bool matchesGenerated(const QString& path, int steps, int factor) const;

  Generated m_generated;
  std::shared_ptr<std::atomic_bool> m_generateCancel;
  bool m_busy = false;
  int m_nextJobId = 0;
};
