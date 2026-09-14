// Image helper used by generate-icons.sh to derive Avora's branding assets from
// a single master PNG. Only depends on the system Swift toolchain + CoreGraphics
// so there is no ImageMagick/PIL dependency.
//
//   icon-tool resize   <in.png> <out.png> <size>
//   icon-tool inset    <in.png> <out.png> <canvas> <margin>
//   icon-tool mono     <in.png> <out.png> <size>
//   icon-tool wordmark <in.png> <out.png> <height> <rrggbb> [width]
//
// `wordmark` prints the canvas width it used so the caller can render the 2x
// variant at exactly twice the 1x width, which grit requires.

import CoreGraphics
import CoreText
import Foundation
import ImageIO
import UniformTypeIdentifiers

func fail(_ message: String) -> Never {
  FileHandle.standardError.write("icon-tool: \(message)\n".data(using: .utf8)!)
  exit(1)
}

func loadImage(_ path: String) -> CGImage {
  guard let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil),
        let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
    fail("could not read \(path)")
  }
  return image
}

func writePNG(_ image: CGImage, to path: String) {
  guard let destination = CGImageDestinationCreateWithURL(
          URL(fileURLWithPath: path) as CFURL, UTType.png.identifier as CFString, 1, nil) else {
    fail("could not create \(path)")
  }
  CGImageDestinationAddImage(destination, image, nil)
  if !CGImageDestinationFinalize(destination) {
    fail("could not write \(path)")
  }
}

func makeContext(width: Int, height: Int) -> CGContext {
  guard let context = CGContext(data: nil,
                                width: width,
                                height: height,
                                bitsPerComponent: 8,
                                bytesPerRow: width * 4,
                                space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
    fail("could not create a \(width)x\(height) bitmap")
  }
  context.interpolationQuality = .high
  return context
}

// Draws the source centred in a square canvas, leaving `margin` transparent
// pixels on every side. A `margin` of 0 produces a plain resize.
func square(_ image: CGImage, canvas: Int, margin: Int) -> CGImage {
  let context = makeContext(width: canvas, height: canvas)
  let side = canvas - 2 * margin
  context.draw(image, in: CGRect(x: margin, y: margin, width: side, height: side))
  guard let result = context.makeImage() else { fail("rendering failed") }
  return result
}

// Flattens the icon to a template image: luminance becomes alpha and every
// pixel becomes black, so the dark plate drops out and the "A" remains. This is
// what macOS expects for status tray icons.
func template(_ image: CGImage, size: Int) -> CGImage {
  let context = makeContext(width: size, height: size)
  context.draw(square(image, canvas: size, margin: 0),
               in: CGRect(x: 0, y: 0, width: size, height: size))
  guard let pixels = context.data else { fail("could not access pixel data") }
  let buffer = pixels.bindMemory(to: UInt8.self, capacity: size * size * 4)
  for index in stride(from: 0, to: size * size * 4, by: 4) {
    let alpha = Double(buffer[index + 3]) / 255.0
    // Un-premultiply before measuring luminance so the dark plate reads as ~0.
    let red = alpha > 0 ? Double(buffer[index + 0]) / 255.0 / alpha : 0
    let green = alpha > 0 ? Double(buffer[index + 1]) / 255.0 / alpha : 0
    let blue = alpha > 0 ? Double(buffer[index + 2]) / 255.0 / alpha : 0
    let luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue
    // The plate sits around 0.08 luminance and the glyph well above 0.45.
    let coverage = min(max((luminance - 0.18) / 0.42, 0), 1) * alpha
    buffer[index + 0] = 0
    buffer[index + 1] = 0
    buffer[index + 2] = 0
    buffer[index + 3] = UInt8((coverage * 255).rounded())
  }
  guard let result = context.makeImage() else { fail("rendering failed") }
  return result
}

func parseColor(_ hex: String) -> CGColor {
  guard hex.count == 6, let value = Int(hex, radix: 16) else { fail("color must be rrggbb") }
  return CGColor(srgbRed: CGFloat((value >> 16) & 0xFF) / 255.0,
                 green: CGFloat((value >> 8) & 0xFF) / 255.0,
                 blue: CGFloat(value & 0xFF) / 255.0,
                 alpha: 1)
}

// Lays out the mark followed by the product name, the way chrome://version
// expects its wordmark asset. Returns the rendered image and its width.
func wordmark(_ image: CGImage, height: Int, color: CGColor, width: Int?) -> (CGImage, Int) {
  let scale = CGFloat(height) / 32.0
  let gap = 7.0 * scale
  let fontSize = 23.0 * scale
  let font = CTFontCreateUIFontForLanguage(.system, fontSize, nil)
             ?? CTFontCreateWithName("Helvetica" as CFString, fontSize, nil)
  let attributed = NSAttributedString(string: "Avora", attributes: [
    kCTFontAttributeName as NSAttributedString.Key: font,
    kCTForegroundColorAttributeName as NSAttributedString.Key: color,
  ])
  let line = CTLineCreateWithAttributedString(attributed)
  let textWidth = CTLineGetTypographicBounds(line, nil, nil, nil)
  let canvasWidth = width ?? Int((CGFloat(height) + gap + CGFloat(textWidth) + 2 * scale).rounded(.up))

  let context = makeContext(width: canvasWidth, height: height)
  context.draw(image, in: CGRect(x: 0, y: 0, width: height, height: height))
  // Cap height of the system font is about 0.72em; centre it on the mark.
  context.textPosition = CGPoint(x: CGFloat(height) + gap,
                                 y: (CGFloat(height) - fontSize * 0.72) / 2)
  CTLineDraw(line, context)
  guard let result = context.makeImage() else { fail("rendering failed") }
  return (result, canvasWidth)
}

let arguments = Array(CommandLine.arguments.dropFirst())
guard arguments.count >= 4 else {
  fail("usage: icon-tool <resize|inset|mono|wordmark> <in> <out> <size> [...]")
}

let command = arguments[0]
let input = loadImage(arguments[1])
let output = arguments[2]
guard let size = Int(arguments[3]) else { fail("size must be an integer") }

switch command {
case "resize":
  writePNG(square(input, canvas: size, margin: 0), to: output)
case "inset":
  guard arguments.count == 5, let margin = Int(arguments[4]) else { fail("inset needs a margin") }
  writePNG(square(input, canvas: size, margin: margin), to: output)
case "mono":
  writePNG(template(input, size: size), to: output)
case "wordmark":
  guard arguments.count >= 5 else { fail("wordmark needs a colour") }
  let (image, width) = wordmark(input,
                                height: size,
                                color: parseColor(arguments[4]),
                                width: arguments.count > 5 ? Int(arguments[5]) : nil)
  writePNG(image, to: output)
  print(width)
default:
  fail("unknown command \(command)")
}
