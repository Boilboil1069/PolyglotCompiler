/**
 * @file     image_viewer.h
 * @brief    Image viewer value model: format detection, view
 *           transform (zoom / pan), pixel pick and channel split.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace polyglot::tools::ui::viewer {

enum class ImageFormat {
  kUnknown,
  kPng,
  kJpeg,
  kWebp,
  kGif,
  kSvg,
  kBmp,
};

std::string ImageFormatName(ImageFormat f);

/// Sniff the image format from the leading magic bytes.  Falls
/// back to the lower-cased file extension when the bytes are
/// inconclusive (SVG and most text-y formats).
ImageFormat DetectImageFormat(const std::string &filename,
                              const std::vector<uint8_t> &bytes);

struct Pixel {
  int x{0};
  int y{0};
  uint8_t r{0};
  uint8_t g{0};
  uint8_t b{0};
  uint8_t a{255};
};

enum class ChannelMask {
  kAll,
  kRed,
  kGreen,
  kBlue,
  kAlpha,
};

std::string ChannelMaskName(ChannelMask m);

struct ImageView {
  double zoom{1.0};        ///< 1.0 = 100 %.
  double pan_x{0.0};
  double pan_y{0.0};
  ChannelMask channel{ChannelMask::kAll};
};

/// Bound-aware view transform.  Zoom is clamped to [0.05, 64.0].
class ImageViewer {
 public:
  ImageViewer(int width, int height) : width_(width), height_(height) {}

  int width() const { return width_; }
  int height() const { return height_; }
  const ImageView &view() const { return view_; }

  void SetZoom(double zoom);
  void ZoomBy(double factor);
  void Pan(double dx, double dy);
  void SetChannel(ChannelMask m);
  void ResetView();

  /// Sample a pixel at image coordinates `(x, y)` from a
  /// row-major RGBA buffer (`pixels.size() == width*height*4`).
  std::optional<Pixel> Pick(const std::vector<uint8_t> &pixels,
                            int x, int y) const;

  /// Returns a copy of `pixels` with non-selected channels
  /// zeroed.  Pass-through when `channel == kAll`.
  std::vector<uint8_t> ApplyChannelSplit(
      const std::vector<uint8_t> &pixels) const;

 private:
  int width_;
  int height_;
  ImageView view_;
};

}  // namespace polyglot::tools::ui::viewer
