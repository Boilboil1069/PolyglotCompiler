/**
 * @file     image_viewer.cpp
 * @brief    Image viewer value model implementation.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/viewer/image_viewer.h"

#include <algorithm>
#include <cctype>

namespace polyglot::tools::ui::viewer {

std::string ImageFormatName(ImageFormat f) {
  switch (f) {
    case ImageFormat::kPng:  return "png";
    case ImageFormat::kJpeg: return "jpeg";
    case ImageFormat::kWebp: return "webp";
    case ImageFormat::kGif:  return "gif";
    case ImageFormat::kSvg:  return "svg";
    case ImageFormat::kBmp:  return "bmp";
    case ImageFormat::kUnknown: return "unknown";
  }
  return "unknown";
}

namespace {

bool StartsWith(const std::vector<uint8_t> &b, std::initializer_list<uint8_t> s) {
  if (b.size() < s.size()) return false;
  size_t i = 0;
  for (auto v : s) { if (b[i++] != v) return false; }
  return true;
}

std::string LowerExt(const std::string &name) {
  auto pos = name.find_last_of('.');
  if (pos == std::string::npos) return {};
  std::string ext = name.substr(pos + 1);
  for (auto &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext;
}

}  // namespace

ImageFormat DetectImageFormat(const std::string &filename,
                              const std::vector<uint8_t> &bytes) {
  if (StartsWith(bytes, {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}))
    return ImageFormat::kPng;
  if (StartsWith(bytes, {0xFF, 0xD8, 0xFF})) return ImageFormat::kJpeg;
  if (bytes.size() >= 12 &&
      StartsWith(bytes, {'R', 'I', 'F', 'F'}) &&
      bytes[8] == 'W' && bytes[9] == 'E' &&
      bytes[10] == 'B' && bytes[11] == 'P')
    return ImageFormat::kWebp;
  if (StartsWith(bytes, {'G', 'I', 'F', '8'})) return ImageFormat::kGif;
  if (StartsWith(bytes, {'B', 'M'})) return ImageFormat::kBmp;
  // Text-y SVG: fall through to extension.
  auto ext = LowerExt(filename);
  if (ext == "svg") return ImageFormat::kSvg;
  if (ext == "png")  return ImageFormat::kPng;
  if (ext == "jpg" || ext == "jpeg") return ImageFormat::kJpeg;
  if (ext == "webp") return ImageFormat::kWebp;
  if (ext == "gif")  return ImageFormat::kGif;
  if (ext == "bmp")  return ImageFormat::kBmp;
  return ImageFormat::kUnknown;
}

std::string ChannelMaskName(ChannelMask m) {
  switch (m) {
    case ChannelMask::kAll:   return "all";
    case ChannelMask::kRed:   return "red";
    case ChannelMask::kGreen: return "green";
    case ChannelMask::kBlue:  return "blue";
    case ChannelMask::kAlpha: return "alpha";
  }
  return "all";
}

void ImageViewer::SetZoom(double z) {
  view_.zoom = std::clamp(z, 0.05, 64.0);
}

void ImageViewer::ZoomBy(double factor) {
  if (factor <= 0.0) return;
  SetZoom(view_.zoom * factor);
}

void ImageViewer::Pan(double dx, double dy) {
  view_.pan_x += dx;
  view_.pan_y += dy;
}

void ImageViewer::SetChannel(ChannelMask m) { view_.channel = m; }

void ImageViewer::ResetView() { view_ = ImageView{}; }

std::optional<Pixel> ImageViewer::Pick(const std::vector<uint8_t> &pixels,
                                       int x, int y) const {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return std::nullopt;
  size_t base = (static_cast<size_t>(y) * width_ + x) * 4;
  if (base + 3 >= pixels.size()) return std::nullopt;
  Pixel p;
  p.x = x; p.y = y;
  p.r = pixels[base + 0];
  p.g = pixels[base + 1];
  p.b = pixels[base + 2];
  p.a = pixels[base + 3];
  return p;
}

std::vector<uint8_t> ImageViewer::ApplyChannelSplit(
    const std::vector<uint8_t> &pixels) const {
  if (view_.channel == ChannelMask::kAll) return pixels;
  std::vector<uint8_t> out = pixels;
  for (size_t i = 0; i + 3 < out.size(); i += 4) {
    uint8_t r = out[i], g = out[i + 1], b = out[i + 2], a = out[i + 3];
    switch (view_.channel) {
      case ChannelMask::kRed:   out[i+0]=r; out[i+1]=0; out[i+2]=0; out[i+3]=255; break;
      case ChannelMask::kGreen: out[i+0]=0; out[i+1]=g; out[i+2]=0; out[i+3]=255; break;
      case ChannelMask::kBlue:  out[i+0]=0; out[i+1]=0; out[i+2]=b; out[i+3]=255; break;
      case ChannelMask::kAlpha: out[i+0]=a; out[i+1]=a; out[i+2]=a; out[i+3]=255; break;
      default: break;
    }
  }
  return out;
}

}  // namespace polyglot::tools::ui::viewer
