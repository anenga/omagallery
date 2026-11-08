#pragma once

#include <QImage>
#include <QString>

#include <atomic>
#include <functional>

// AI upscaling through Real-ESRGAN, run locally on the GPU.
//
// Omaroll does not bundle a model: it drives the realesrgan-ncnn-vulkan
// command when it is installed (AUR: realesrgan-ncnn-vulkan or
// realesrgan-ncnn-vulkan-bin). Nothing leaves the machine. The picture goes
// to a private temporary PNG, the tool writes a 4x PNG beside it, and the
// result is scaled to the factor asked for. Without the tool, the touch-up
// sheet shows the option greyed with the package to install.
namespace Upscaler {

inline constexpr const char* kProgram = "realesrgan-ncnn-vulkan";
inline constexpr const char* kPackage = "realesrgan-ncnn-vulkan";
// The general photo model. The anime models smear skin and foliage.
inline constexpr const char* kModel = "realesrgan-x4plus";

// The installed command, or empty.
[[nodiscard]] QString program();
[[nodiscard]] inline bool available() { return !program().isEmpty(); }

// The folder holding the model files, when it is not where the tool looks by
// default (beside its own binary). Empty means let the tool decide.
[[nodiscard]] QString modelDirectory(const QString& programPath);

// |image| enlarged |factor| times (2 or 4), never past |maxPixels|. Blocks
// until the tool finishes; call it off the GUI thread. |progress| receives
// 0..1 as the tool reports it. Setting |*cancel| stops the tool within a
// quarter second. On failure returns a null image and sets |error|.
[[nodiscard]] QImage upscale(const QImage& image, int factor, qint64 maxPixels, QString* error,
                             const std::function<void(qreal)>& progress = {},
                             const std::atomic_bool* cancel = nullptr);

// The message upscale() fails with when |cancel| stopped it.
[[nodiscard]] QString stoppedMessage();

} // namespace Upscaler
