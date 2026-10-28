// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_local_user.h"

#import <AppKit/AppKit.h>
#import <Collaboration/Collaboration.h>

#include "base/apple/foundation_util.h"
#include "base/strings/sys_string_conversions.h"

namespace avora {

namespace {

// Big enough for the menu's header at 2x; the account picture itself is often
// several hundred pixels square, and the avatar never draws it that large.
constexpr CGFloat kPhotoPixelSize = 128;

std::vector<uint8_t> PngFromImage(NSImage* image) {
  if (!image) {
    return {};
  }
  NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
      initWithBitmapDataPlanes:nullptr
                    pixelsWide:kPhotoPixelSize
                    pixelsHigh:kPhotoPixelSize
                 bitsPerSample:8
               samplesPerPixel:4
                      hasAlpha:YES
                      isPlanar:NO
                colorSpaceName:NSDeviceRGBColorSpace
                   bytesPerRow:0
                  bitsPerPixel:0];
  if (!rep) {
    return {};
  }

  // Drawn rather than taken from the image's own representations, which can
  // be a TIFF, a PDF, or several sizes at once.  Square-cropped from the
  // centre, as the account picture is shown everywhere else in macOS.
  const NSSize source = image.size;
  const CGFloat side = std::min(source.width, source.height);
  if (side <= 0) {
    return {};
  }
  const NSRect crop = NSMakeRect((source.width - side) / 2,
                                 (source.height - side) / 2, side, side);

  [NSGraphicsContext saveGraphicsState];
  NSGraphicsContext.currentContext =
      [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
  NSGraphicsContext.currentContext.imageInterpolation =
      NSImageInterpolationHigh;
  [image drawInRect:NSMakeRect(0, 0, kPhotoPixelSize, kPhotoPixelSize)
           fromRect:crop
          operation:NSCompositingOperationCopy
           fraction:1.0];
  [NSGraphicsContext restoreGraphicsState];

  NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG
                                  properties:@{}];
  if (!png.length) {
    return {};
  }
  const base::span<const uint8_t> bytes = base::apple::NSDataToSpan(png);
  return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

}  // namespace

LocalUser GetLocalUserBlocking() {
  LocalUser user;
  @autoreleasepool {
    // The local identity authority, not the default one: the default also
    // searches network directories, which is slow when they are configured
    // and pointless for the account that is logged in here.
    CBIdentity* identity =
        [CBIdentity identityWithName:NSUserName()
                           authority:[CBIdentityAuthority
                                         localIdentityAuthority]];
    NSString* full_name = identity.fullName.length ? identity.fullName
                                                   : NSFullUserName();
    user.full_name = base::SysNSStringToUTF16(full_name);
    user.photo_png = PngFromImage(identity.image);
  }
  return user;
}

}  // namespace avora
