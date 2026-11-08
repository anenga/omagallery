#pragma once

#include <QImage>
#include <QRectF>

// One-click touch-ups for scanned prints, applied on top of the correction
// pipeline and, like every correction, saved as a copy.
//
// Smart Crop finds the border a sheet-fed photo scanner (FastFoto and its
// kind) leaves around a print: a band of the dark backing, often with a thin
// light strip where the print's edge caught the light. It peels uniform,
// neutral lines off each side until it reaches picture content, so it never
// guesses at a subject and never trims a photo that has no border.
//
// Auto Fix is a levels stretch: each channel is stretched between its darkest
// and brightest few pixels, partly linked to the luminance so a real colour
// cast is corrected without inventing one, then the midtones are nudged
// towards a normal exposure. It is the classic fix for a faded print.
//
// Both are pure functions of the pixels, so the preview provider, the writer
// and the tests all agree.
namespace TouchUp {

enum Step : int {
  None = 0,
  SmartCrop = 1 << 0,
  AutoFix = 1 << 1,
};

// The picture's area as fractions (0..1) of |image|, or the whole frame
// (0, 0, 1, 1) when no border is found.
[[nodiscard]] QRectF detectBorder(const QImage& image);

// The levels-corrected picture, in the same size and colour space.
[[nodiscard]] QImage autoFix(const QImage& image);

// The steps in order: crop first, so the border's black and white never
// count as the picture's darkest and brightest pixels.
[[nodiscard]] QImage apply(const QImage& image, int steps);

} // namespace TouchUp
