#include "edit/TouchUp.h"

#include <QColorSpace>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {

// Detection runs on a copy no longer than this on its long edge. A scanner
// border is tens of pixels wide at full size, so this keeps several pixels of
// it while making the scan cheap on a 6000-pixel scan.
constexpr int kDetectEdge = 900;
// A line is border when this share of it sits within kLineTolerance of the
// line's median colour.
constexpr double kUniformShare = 0.85;
constexpr int kLineTolerance = 28;
// Borders are the scanner's backing, black or white or grey. A coloured
// uniform line (blue sky, a painted wall) is picture, not border.
constexpr int kNeutralChroma = 40;
// No side gives up more than this share of the picture.
constexpr double kMaxSideShare = 0.15;
// A shadow seam line: at least this share near its median, every pixel
// within this chroma for this share of the line, and darker than this grey
// (a white tub or sky is bright; the seam and backing are not).
constexpr double kSeamUniformShare = 0.5;
constexpr int kSeamPixelChroma = 24;
constexpr double kSeamNeutralShare = 0.9;
constexpr int kSeamMaxGrey = 200;
// Taken past the border on each trimmed side, as a share of the long edge.
constexpr double kEdgePad = 0.004;
// Lines that are not border, in a row, before a side's border is over.
constexpr int kMissedLines = 2;

struct LineClass {
  bool border = false;
  QRgb median = 0;
};

bool close(QRgb a, QRgb b) {
  return std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)),
                   std::abs(qBlue(a) - qBlue(b))}) < kLineTolerance;
}

// Whether a line is border. Either it is neutral and nearly all one colour
// (the backing, or the light strip at a print's edge), or it is part of the
// shadow seam a print's edge casts: neutral pixel by pixel, darker than a
// white strip, and mostly even, though it fades along its length.
LineClass classify(const std::vector<QRgb>& pixels) {
  if (pixels.empty()) return {};
  std::vector<int> r(pixels.size());
  std::vector<int> g(pixels.size());
  std::vector<int> b(pixels.size());
  for (size_t i = 0; i < pixels.size(); ++i) {
    r[i] = qRed(pixels[i]);
    g[i] = qGreen(pixels[i]);
    b[i] = qBlue(pixels[i]);
  }
  const auto median = [](std::vector<int> values) {
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
  };
  const QRgb centre = qRgb(median(r), median(g), median(b));
  const int chroma = std::max({qRed(centre), qGreen(centre), qBlue(centre)}) -
                     std::min({qRed(centre), qGreen(centre), qBlue(centre)});
  if (chroma > kNeutralChroma) return {false, centre};

  size_t near = 0;
  size_t neutral = 0;
  for (const QRgb pixel : pixels) {
    if (close(pixel, centre)) ++near;
    const int spread = std::max({qRed(pixel), qGreen(pixel), qBlue(pixel)}) -
                       std::min({qRed(pixel), qGreen(pixel), qBlue(pixel)});
    if (spread <= kSeamPixelChroma) ++neutral;
  }
  const double count = static_cast<double>(pixels.size());
  const double nearShare = static_cast<double>(near) / count;
  if (nearShare >= kUniformShare) return {true, centre};
  const bool seam = nearShare >= kSeamUniformShare && qGray(centre) < kSeamMaxGrey &&
                    static_cast<double>(neutral) >= kSeamNeutralShare * count;
  return {seam, centre};
}

// How many lines, counted in from one side, are wholly border.
template <typename LineAt>
int peel(int lineCount, LineAt lineAt) {
  const int limit = static_cast<int>(lineCount * kMaxSideShare);
  std::vector<QRgb> pixels;
  int peeled = 0;
  // A stray line that is not border (a speck of dust, a corner of the print
  // lifting into the first line) does not end the border, but a few do.
  int misses = 0;
  for (int i = 0; i < limit; ++i) {
    lineAt(i, pixels);
    const LineClass line = classify(pixels);
    if (!line.border) {
      if (++misses > kMissedLines) break;
      continue;
    }
    misses = 0;
    peeled = i + 1;
  }
  return peeled;
}

int percentile(const std::array<qint64, 256>& histogram, qint64 total, double fraction) {
  const qint64 target = static_cast<qint64>(fraction * static_cast<double>(total));
  qint64 running = 0;
  for (int value = 0; value < 256; ++value) {
    running += histogram[value];
    if (running > target) return value;
  }
  return 255;
}

} // namespace

namespace TouchUp {

QRectF detectBorder(const QImage& image) {
  const QRectF whole(0.0, 0.0, 1.0, 1.0);
  if (image.isNull() || image.width() < 16 || image.height() < 16) return whole;

  QImage small = image;
  if (std::max(image.width(), image.height()) > kDetectEdge) {
    small = image.scaled(kDetectEdge, kDetectEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  small = small.convertToFormat(QImage::Format_RGB32);
  const int w = small.width();
  const int h = small.height();

  const auto row = [&small, w](int y, std::vector<QRgb>& out) {
    const auto* line = reinterpret_cast<const QRgb*>(small.constScanLine(y));
    out.assign(line, line + w);
  };
  const auto column = [&small, h](int x, std::vector<QRgb>& out) {
    out.resize(static_cast<size_t>(h));
    for (int y = 0; y < h; ++y) {
      out[static_cast<size_t>(y)] = reinterpret_cast<const QRgb*>(small.constScanLine(y))[x];
    }
  };

  int top = peel(h, [&](int i, std::vector<QRgb>& out) { row(i, out); });
  int bottom = peel(h, [&](int i, std::vector<QRgb>& out) { row(h - 1 - i, out); });
  int left = peel(w, [&](int i, std::vector<QRgb>& out) { column(i, out); });
  int right = peel(w, [&](int i, std::vector<QRgb>& out) { column(w - 1 - i, out); });
  if (top + bottom + left + right == 0) return whole;

  // A print never sits perfectly square in a sheet-fed scanner, so its edge
  // runs at a slight slant, and the last border line blends into the first
  // picture line after scaling. Take a little more on each trimmed side so
  // no sliver of backing or shadow survives.
  const int pad = std::max(1, static_cast<int>(std::lround(std::max(w, h) * kEdgePad)));
  if (top > 0) top += pad;
  if (bottom > 0) bottom += pad;
  if (left > 0) left += pad;
  if (right > 0) right += pad;

  const int keptW = w - left - right;
  const int keptH = h - top - bottom;
  if (keptW < w / 2 || keptH < h / 2) return whole;

  return {static_cast<qreal>(left) / w, static_cast<qreal>(top) / h,
          static_cast<qreal>(keptW) / w, static_cast<qreal>(keptH) / h};
}

QImage autoFix(const QImage& image) {
  if (image.isNull()) return {};
  const QColorSpace space = image.colorSpace();
  QImage out = image.convertToFormat(image.hasAlphaChannel() ? QImage::Format_ARGB32
                                                             : QImage::Format_RGB32);
  const int w = out.width();
  const int h = out.height();

  // Sample about a million pixels; percentiles do not need every one.
  const qint64 pixels = static_cast<qint64>(w) * h;
  const int step = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(pixels) / 1.0e6)));

  std::array<std::array<qint64, 256>, 3> channel{};
  std::array<qint64, 256> luma{};
  qint64 samples = 0;
  for (int y = 0; y < h; y += step) {
    const auto* line = reinterpret_cast<const QRgb*>(out.constScanLine(y));
    for (int x = 0; x < w; x += step) {
      const QRgb p = line[x];
      ++channel[0][qRed(p)];
      ++channel[1][qGreen(p)];
      ++channel[2][qBlue(p)];
      ++luma[qGray(p)];
      ++samples;
    }
  }
  if (samples == 0) return image;

  const double lumaLow = percentile(luma, samples, 0.005);
  const double lumaHigh = percentile(luma, samples, 0.995);

  // Stretch each channel between its own ends, pulled 40% towards the
  // luminance ends: fully independent channels would neutralise a sunset,
  // fully linked ones would leave a faded print's cast alone.
  std::array<std::array<double, 256>, 3> stretched{};
  for (int c = 0; c < 3; ++c) {
    double low = 0.6 * percentile(channel[c], samples, 0.005) + 0.4 * lumaLow;
    double high = 0.6 * percentile(channel[c], samples, 0.995) + 0.4 * lumaHigh;
    // Never stretch a nearly flat channel into noise, and never treat a
    // low-key or high-key picture's whole range as clipping.
    low = std::min(low, 80.0);
    high = std::max(high, 150.0);
    for (int v = 0; v < 256; ++v) {
      stretched[c][v] = std::clamp((v - low) / (high - low), 0.0, 1.0);
    }
  }

  // Nudge the midtones towards a normal exposure, gently.
  double meanLuma = 0.0;
  for (int v = 0; v < 256; ++v) {
    // Approximate the stretched luminance with the average channel curve.
    const double s = (stretched[0][v] * 0.299 + stretched[1][v] * 0.587 + stretched[2][v] * 0.114);
    meanLuma += s * static_cast<double>(luma[v]);
  }
  meanLuma /= static_cast<double>(samples);
  double gamma = 1.0;
  if (meanLuma > 0.01 && (meanLuma < 0.40 || meanLuma > 0.54)) {
    gamma = std::clamp(std::log(0.46) / std::log(meanLuma), 0.85, 1.15);
  }

  std::array<std::array<uchar, 256>, 3> lut{};
  for (int c = 0; c < 3; ++c) {
    for (int v = 0; v < 256; ++v) {
      lut[c][v] = static_cast<uchar>(std::lround(255.0 * std::pow(stretched[c][v], gamma)));
    }
  }

  for (int y = 0; y < h; ++y) {
    auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
    for (int x = 0; x < w; ++x) {
      const QRgb p = line[x];
      line[x] = qRgba(lut[0][qRed(p)], lut[1][qGreen(p)], lut[2][qBlue(p)], qAlpha(p));
    }
  }

  if (space.isValid()) out.setColorSpace(space);
  return out;
}

QImage apply(const QImage& image, int steps) {
  if (image.isNull()) return {};
  QImage result = image;
  if (steps & SmartCrop) {
    const QRectF area = detectBorder(result);
    const QRect crop(qRound(area.x() * result.width()), qRound(area.y() * result.height()),
                     qRound(area.width() * result.width()),
                     qRound(area.height() * result.height()));
    const QRect bounded = crop.intersected(result.rect());
    if (!bounded.isEmpty() && bounded != result.rect()) {
      const QColorSpace space = result.colorSpace();
      result = result.copy(bounded);
      if (space.isValid()) result.setColorSpace(space);
    }
  }
  if (steps & AutoFix) {
    result = autoFix(result);
  }
  return result;
}

} // namespace TouchUp
