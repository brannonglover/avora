import { Globe } from 'lucide-react-native';
import { useState } from 'react';
import { Image, StyleSheet, Text, View } from 'react-native';

import { accentRotation, colors } from './theme';
import { faviconUrl, hostOf } from './url';

function tintFor(host: string) {
  let hash = 0;
  for (let i = 0; i < host.length; i++) hash = (hash * 31 + host.charCodeAt(i)) | 0;
  return accentRotation[Math.abs(hash) % accentRotation.length];
}

export function Favicon({ url, size = 20 }: { url: string | null; size?: number }) {
  const [failedFor, setFailedFor] = useState<string | null>(null);
  const src = faviconUrl(url);
  const host = hostOf(url);

  if (!url) {
    return <Globe size={size * 0.9} color={colors.textTertiary} />;
  }

  if (!src || failedFor === src) {
    return (
      <View
        style={[
          styles.fallback,
          { width: size, height: size, borderRadius: size / 4, backgroundColor: tintFor(host) },
        ]}
      >
        <Text style={[styles.letter, { fontSize: size * 0.55 }]}>
          {(host[0] ?? '?').toUpperCase()}
        </Text>
      </View>
    );
  }

  return (
    <Image
      source={{ uri: src }}
      style={{ width: size, height: size, borderRadius: size / 5 }}
      onError={() => setFailedFor(src)}
    />
  );
}

const styles = StyleSheet.create({
  fallback: { alignItems: 'center', justifyContent: 'center' },
  letter: { color: colors.background, fontWeight: '700' },
});
