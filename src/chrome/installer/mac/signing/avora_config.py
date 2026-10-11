# Copyright 2026 Avora. All rights reserved.

from signing.chromium_config import ChromiumCodeSignConfig


class AvoraCodeSignConfig(ChromiumCodeSignConfig):
    """Signing config for Avora.

    Chromium's own config deliberately returns no provisioning profile because
    it exists for unsigned test signing.  Avora needs one: macOS only honours
    com.apple.application-identifier -- which SafariLaunchAgent requires before
    it will hand over an ASWebAuthenticationSession -- when the entitlement is
    backed by a matching profile embedded in the bundle.

    The profile is read from the packaging directory.  |model.Distribution|
    appends the signing identity's SHA-1 hash to this basename, so the file on
    disk is named `Avora.<identity hash>.provisionprofile`.  See
    docs/MACOS_AUTH_SESSIONS.md.
    """

    @property
    def provisioning_profile_basename(self):
        return 'Avora'
