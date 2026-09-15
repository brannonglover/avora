// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/ui/views/avora/avora_lucide_icon.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#include "base/strings/string_number_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/avora/avora_space_icons.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/scoped_canvas.h"

namespace avora {

namespace {

bool IsSvgDigit(char c) {
  return c >= '0' && c <= '9';
}

bool IsSvgCommand(char c) {
  switch (c) {
    case 'M': case 'm': case 'L': case 'l': case 'H': case 'h':
    case 'V': case 'v': case 'C': case 'c': case 'S': case 's':
    case 'Q': case 'q': case 'T': case 't': case 'A': case 'a':
    case 'Z': case 'z':
      return true;
    default:
      return false;
  }
}

// Parses the subset of the SVG path grammar that Lucide emits: absolute and
// relative move/line/curve/arc commands, shorthand curves, implicit repeated
// parameter sets, and compact flag notation such as "a6 6 0 01-8.943 0".
//
// Elliptical arcs are converted to cubics here rather than handed to Skia so
// the whole icon pipeline stays on a handful of long-stable path primitives.
class SvgPathParser {
 public:
  explicit SvgPathParser(std::string_view data) : data_(data) {}

  // Returns false if the data is malformed, in which case |builder| holds
  // whatever was parsed up to that point and should be discarded.
  bool Parse(SkPathBuilder& builder) {
    char command = 0;
    while (true) {
      SkipSeparators();
      if (pos_ >= data_.size()) {
        return true;
      }
      if (IsSvgCommand(data_[pos_])) {
        command = data_[pos_++];
      } else if (command == 0) {
        return false;
      }
      if (!RunCommand(command, builder)) {
        return false;
      }
      // A second coordinate set after a moveto is an implicit lineto.
      if (command == 'M') {
        command = 'L';
      } else if (command == 'm') {
        command = 'l';
      }
    }
  }

 private:
  void SkipSeparators() {
    while (pos_ < data_.size() &&
           (data_[pos_] == ' ' || data_[pos_] == ',' || data_[pos_] == '\t' ||
            data_[pos_] == '\n' || data_[pos_] == '\r')) {
      ++pos_;
    }
  }

  bool ReadScalar(float& out) {
    SkipSeparators();
    const size_t start = pos_;
    if (pos_ < data_.size() && (data_[pos_] == '+' || data_[pos_] == '-')) {
      ++pos_;
    }
    bool has_digits = false;
    while (pos_ < data_.size() && IsSvgDigit(data_[pos_])) {
      ++pos_;
      has_digits = true;
    }
    if (pos_ < data_.size() && data_[pos_] == '.') {
      ++pos_;
      while (pos_ < data_.size() && IsSvgDigit(data_[pos_])) {
        ++pos_;
        has_digits = true;
      }
    }
    if (!has_digits) {
      pos_ = start;
      return false;
    }
    if (pos_ < data_.size() && (data_[pos_] == 'e' || data_[pos_] == 'E')) {
      const size_t exponent_start = pos_;
      ++pos_;
      if (pos_ < data_.size() && (data_[pos_] == '+' || data_[pos_] == '-')) {
        ++pos_;
      }
      bool has_exponent_digits = false;
      while (pos_ < data_.size() && IsSvgDigit(data_[pos_])) {
        ++pos_;
        has_exponent_digits = true;
      }
      if (!has_exponent_digits) {
        pos_ = exponent_start;
      }
    }

    double value = 0;
    if (!base::StringToDouble(data_.substr(start, pos_ - start), &value)) {
      return false;
    }
    out = static_cast<float>(value);
    return true;
  }

  // Arc flags may be written without separators, so they are always exactly
  // one character: "0" or "1".
  bool ReadFlag(bool& out) {
    SkipSeparators();
    if (pos_ >= data_.size() || (data_[pos_] != '0' && data_[pos_] != '1')) {
      return false;
    }
    out = data_[pos_++] == '1';
    return true;
  }

  bool ReadPoint(SkPoint& out, bool relative) {
    float x = 0;
    float y = 0;
    if (!ReadScalar(x) || !ReadScalar(y)) {
      return false;
    }
    out = relative ? SkPoint::Make(current_.x() + x, current_.y() + y)
                   : SkPoint::Make(x, y);
    return true;
  }

  SkPoint Reflected(SkPoint control) const {
    return SkPoint::Make(2 * current_.x() - control.x(),
                         2 * current_.y() - control.y());
  }

  bool RunCommand(char command, SkPathBuilder& builder) {
    const bool relative = command >= 'a' && command <= 'z';
    SkPoint point;

    switch (command) {
      case 'M':
      case 'm':
        if (!ReadPoint(point, relative)) {
          return false;
        }
        builder.moveTo(point.x(), point.y());
        current_ = point;
        subpath_start_ = point;
        break;

      case 'L':
      case 'l':
        if (!ReadPoint(point, relative)) {
          return false;
        }
        builder.lineTo(point.x(), point.y());
        current_ = point;
        break;

      case 'H':
      case 'h': {
        float x = 0;
        if (!ReadScalar(x)) {
          return false;
        }
        current_.set(relative ? current_.x() + x : x, current_.y());
        builder.lineTo(current_.x(), current_.y());
        break;
      }

      case 'V':
      case 'v': {
        float y = 0;
        if (!ReadScalar(y)) {
          return false;
        }
        current_.set(current_.x(), relative ? current_.y() + y : y);
        builder.lineTo(current_.x(), current_.y());
        break;
      }

      case 'C':
      case 'c': {
        SkPoint control1;
        SkPoint control2;
        if (!ReadPoint(control1, relative) || !ReadPoint(control2, relative) ||
            !ReadPoint(point, relative)) {
          return false;
        }
        builder.cubicTo(control1.x(), control1.y(), control2.x(), control2.y(),
                        point.x(), point.y());
        cubic_control_ = control2;
        current_ = point;
        break;
      }

      case 'S':
      case 's': {
        SkPoint control2;
        if (!ReadPoint(control2, relative) || !ReadPoint(point, relative)) {
          return false;
        }
        const SkPoint control1 =
            (previous_ == 'C' || previous_ == 'c' || previous_ == 'S' ||
             previous_ == 's')
                ? Reflected(cubic_control_)
                : current_;
        builder.cubicTo(control1.x(), control1.y(), control2.x(), control2.y(),
                        point.x(), point.y());
        cubic_control_ = control2;
        current_ = point;
        break;
      }

      case 'Q':
      case 'q': {
        SkPoint control;
        if (!ReadPoint(control, relative) || !ReadPoint(point, relative)) {
          return false;
        }
        builder.quadTo(control.x(), control.y(), point.x(), point.y());
        quad_control_ = control;
        current_ = point;
        break;
      }

      case 'T':
      case 't': {
        if (!ReadPoint(point, relative)) {
          return false;
        }
        const SkPoint control =
            (previous_ == 'Q' || previous_ == 'q' || previous_ == 'T' ||
             previous_ == 't')
                ? Reflected(quad_control_)
                : current_;
        builder.quadTo(control.x(), control.y(), point.x(), point.y());
        quad_control_ = control;
        current_ = point;
        break;
      }

      case 'A':
      case 'a': {
        float radius_x = 0;
        float radius_y = 0;
        float rotation = 0;
        bool large_arc = false;
        bool sweep = false;
        if (!ReadScalar(radius_x) || !ReadScalar(radius_y) ||
            !ReadScalar(rotation) || !ReadFlag(large_arc) ||
            !ReadFlag(sweep) || !ReadPoint(point, relative)) {
          return false;
        }
        AppendArc(builder, radius_x, radius_y, rotation, large_arc, sweep,
                  point);
        current_ = point;
        break;
      }

      case 'Z':
      case 'z':
        builder.close();
        current_ = subpath_start_;
        break;

      default:
        return false;
    }

    previous_ = command;
    return true;
  }

  // Endpoint-to-centre conversion from the SVG specification, appendix F.6,
  // followed by a cubic approximation of at most a quarter turn per segment.
  void AppendArc(SkPathBuilder& builder,
                 float radius_x,
                 float radius_y,
                 float rotation_degrees,
                 bool large_arc,
                 bool sweep,
                 SkPoint end) {
    if (current_ == end) {
      return;
    }
    radius_x = std::abs(radius_x);
    radius_y = std::abs(radius_y);
    if (radius_x == 0 || radius_y == 0) {
      builder.lineTo(end.x(), end.y());
      return;
    }

    const double phi = rotation_degrees * std::numbers::pi / 180.0;
    const double cos_phi = std::cos(phi);
    const double sin_phi = std::sin(phi);

    const double dx = (current_.x() - end.x()) / 2.0;
    const double dy = (current_.y() - end.y()) / 2.0;
    const double x1 = cos_phi * dx + sin_phi * dy;
    const double y1 = -sin_phi * dx + cos_phi * dy;

    double rx = radius_x;
    double ry = radius_y;
    const double scale =
        (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (scale > 1.0) {
      const double factor = std::sqrt(scale);
      rx *= factor;
      ry *= factor;
    }

    const double numerator = std::max(
        0.0, rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1);
    const double denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    double coefficient =
        denominator == 0 ? 0.0 : std::sqrt(numerator / denominator);
    if (large_arc == sweep) {
      coefficient = -coefficient;
    }

    const double cx1 = coefficient * rx * y1 / ry;
    const double cy1 = -coefficient * ry * x1 / rx;
    const double cx =
        cos_phi * cx1 - sin_phi * cy1 + (current_.x() + end.x()) / 2.0;
    const double cy =
        sin_phi * cx1 + cos_phi * cy1 + (current_.y() + end.y()) / 2.0;

    const double start_angle = std::atan2((y1 - cy1) / ry, (x1 - cx1) / rx);
    const double end_angle = std::atan2((-y1 - cy1) / ry, (-x1 - cx1) / rx);
    double sweep_angle = end_angle - start_angle;
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    if (!sweep && sweep_angle > 0) {
      sweep_angle -= kTwoPi;
    } else if (sweep && sweep_angle < 0) {
      sweep_angle += kTwoPi;
    }

    const int segments = std::max(
        1, static_cast<int>(
               std::ceil(std::abs(sweep_angle) / (std::numbers::pi / 2.0))));
    const double segment_angle = sweep_angle / segments;
    // Classic circular-arc approximation: scaling the endpoint tangents by
    // (4/3)tan(delta/4) keeps a quarter turn within ~0.03% of the true arc.
    const double alpha = 4.0 / 3.0 * std::tan(segment_angle / 4.0);

    double theta = start_angle;
    for (int i = 0; i < segments; ++i) {
      const double next_theta = theta + segment_angle;

      const double cos_start = std::cos(theta);
      const double sin_start = std::sin(theta);
      const double cos_end = std::cos(next_theta);
      const double sin_end = std::sin(next_theta);

      // Point and tangent on the rotated ellipse.
      auto point_at = [&](double c, double s) {
        return SkPoint::Make(
            static_cast<float>(cx + rx * c * cos_phi - ry * s * sin_phi),
            static_cast<float>(cy + rx * c * sin_phi + ry * s * cos_phi));
      };
      auto tangent_at = [&](double c, double s) {
        return SkPoint::Make(
            static_cast<float>(-rx * s * cos_phi - ry * c * sin_phi),
            static_cast<float>(-rx * s * sin_phi + ry * c * cos_phi));
      };

      const SkPoint from = point_at(cos_start, sin_start);
      const SkPoint to = point_at(cos_end, sin_end);
      const SkPoint from_tangent = tangent_at(cos_start, sin_start);
      const SkPoint to_tangent = tangent_at(cos_end, sin_end);

      builder.cubicTo(
          static_cast<float>(from.x() + alpha * from_tangent.x()),
          static_cast<float>(from.y() + alpha * from_tangent.y()),
          static_cast<float>(to.x() - alpha * to_tangent.x()),
          static_cast<float>(to.y() - alpha * to_tangent.y()), to.x(), to.y());

      theta = next_theta;
    }
  }

  std::string_view data_;
  size_t pos_ = 0;
  char previous_ = 0;
  SkPoint current_ = SkPoint::Make(0, 0);
  SkPoint subpath_start_ = SkPoint::Make(0, 0);
  SkPoint cubic_control_ = SkPoint::Make(0, 0);
  SkPoint quad_control_ = SkPoint::Make(0, 0);
};

class LucideImageSource : public gfx::CanvasImageSource {
 public:
  LucideImageSource(std::string_view icon_id,
                    int size,
                    SkColor color,
                    float stroke_width)
      : gfx::CanvasImageSource(gfx::Size(size, size)),
        icon_id_(icon_id),
        color_(color),
        stroke_width_(stroke_width) {}

  void Draw(gfx::Canvas* canvas) override {
    PaintLucideIcon(canvas, gfx::Rect(size()), icon_id_, color_,
                    stroke_width_);
  }

 private:
  std::string icon_id_;
  SkColor color_;
  float stroke_width_;
};

}  // namespace

SkPath ParseLucidePathData(std::string_view path_data) {
  SkPathBuilder builder;
  SvgPathParser parser(path_data);
  if (!parser.Parse(builder)) {
    return SkPath();
  }
  return builder.detach();
}

void PaintLucideIcon(gfx::Canvas* canvas,
                     const gfx::Rect& bounds,
                     std::string_view icon_id,
                     SkColor color,
                     float stroke_width) {
  const SkPath path = ParseLucidePathData(GetSpaceIconPathData(icon_id));
  if (path.isEmpty() || bounds.IsEmpty()) {
    return;
  }

  const float scale =
      std::min(bounds.width(), bounds.height()) / kLucideCanvasSize;

  gfx::ScopedCanvas scoped(canvas);
  canvas->sk_canvas()->translate(
      bounds.x() + (bounds.width() - kLucideCanvasSize * scale) / 2.0f,
      bounds.y() + (bounds.height() - kLucideCanvasSize * scale) / 2.0f);
  canvas->sk_canvas()->scale(scale, scale);

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(stroke_width);
  flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
  flags.setStrokeJoin(cc::PaintFlags::kRound_Join);
  flags.setColor(color);
  canvas->DrawPath(path, flags);
}

gfx::ImageSkia LucideIconImage(std::string_view icon_id,
                               int size,
                               SkColor color,
                               float stroke_width) {
  return gfx::CanvasImageSource::MakeImageSkia<LucideImageSource>(
      icon_id, size, color, stroke_width);
}

ui::ImageModel LucideIconImageModel(std::string_view icon_id,
                                    int size,
                                    SkColor color,
                                    float stroke_width) {
  return ui::ImageModel::FromImageSkia(
      LucideIconImage(icon_id, size, color, stroke_width));
}

}  // namespace avora
