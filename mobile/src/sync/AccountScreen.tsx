import { router } from 'expo-router';
import { Laptop, Mail, Smartphone, X } from 'lucide-react-native';
import { useEffect, useState } from 'react';
import {
  ActivityIndicator,
  Alert,
  KeyboardAvoidingView,
  Platform,
  Pressable,
  ScrollView,
  StyleSheet,
  Switch,
  Text,
  TextInput,
  View,
} from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { setPreviewsEnabled, usePreviewsEnabled } from '../browser/previews';
import { colors, radius } from '../browser/theme';
import { useAuth } from './auth';
import { getDeviceId, updatedAgo, useDevices } from './devices';

const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
// Supabase's OTP length is configurable from 6 to 10 digits.
const CODE_RE = /^\d{6,10}$/;

export function AccountScreen() {
  const auth = useAuth();
  const insets = useSafeAreaInsets();

  const close = () => (router.canGoBack() ? router.back() : router.replace('/'));

  let body: React.ReactNode;
  if (!auth.configured) body = <NotConfigured />;
  else if (!auth.ready) body = <ActivityIndicator color={colors.textSecondary} style={styles.spinner} />;
  else if (auth.session) body = <SignedIn email={auth.session.user.email ?? ''} onSignedOut={close} />;
  else body = <SignIn />;

  return (
    <KeyboardAvoidingView style={styles.root} behavior={Platform.OS === 'ios' ? 'padding' : undefined}>
      <View style={[styles.header, { paddingTop: insets.top + 8 }]}>
        <Pressable hitSlop={10} onPress={close} style={styles.close} accessibilityLabel="Close">
          <X size={18} color={colors.textSecondary} />
        </Pressable>
      </View>
      <ScrollView
        contentContainerStyle={[styles.content, { paddingBottom: insets.bottom + 24 }]}
        keyboardShouldPersistTaps="handled"
      >
        {body}
      </ScrollView>
    </KeyboardAvoidingView>
  );
}

function SignIn() {
  const { sendLink, verifyCode, linkError } = useAuth();
  const [email, setEmail] = useState('');
  const [sentTo, setSentTo] = useState<string | null>(null);
  const [code, setCode] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const run = async (task: () => Promise<void>) => {
    setBusy(true);
    setError(null);
    try {
      await task();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  const send = (to: string) =>
    run(async () => {
      await sendLink(to);
      setSentTo(to);
      setCode('');
    });

  const shownError = error ?? linkError;

  if (!sentTo) {
    const trimmed = email.trim();
    const valid = EMAIL_RE.test(trimmed);
    return (
      <>
        <Text style={styles.title}>Sign in to Avora</Text>
        <Text style={styles.lede}>
          Your Spaces and tabs, the same on this phone and every Mac you sign in on. Tabs open here are added to
          your account.
        </Text>
        <TextInput
          style={styles.input}
          value={email}
          onChangeText={setEmail}
          placeholder="you@example.com"
          placeholderTextColor={colors.textTertiary}
          keyboardType="email-address"
          textContentType="emailAddress"
          autoComplete="email"
          autoCapitalize="none"
          autoCorrect={false}
          autoFocus
          returnKeyType="send"
          onSubmitEditing={() => valid && !busy && send(trimmed)}
          editable={!busy}
          accessibilityLabel="Email address"
        />
        <PrimaryButton label="Email me a sign-in link" busy={busy} disabled={!valid} onPress={() => send(trimmed)} />
        {shownError ? <Text style={styles.error}>{shownError}</Text> : null}
        <Text style={styles.footnote}>No password needed. Use the same email you signed in with on your Mac.</Text>
      </>
    );
  }

  const codeValid = CODE_RE.test(code);
  return (
    <>
      <View style={styles.badge}>
        <Mail size={22} color={colors.accent} />
      </View>
      <Text style={styles.title}>Check your email</Text>
      <Text style={styles.lede}>
        We sent a sign-in link to <Text style={styles.strong}>{sentTo}</Text>. Open it on this phone, or enter the
        code from the same email.
      </Text>
      <TextInput
        style={[styles.input, styles.codeInput]}
        value={code}
        onChangeText={(text) => setCode(text.replace(/\D/g, ''))}
        placeholder="123456"
        placeholderTextColor={colors.textTertiary}
        keyboardType="number-pad"
        textContentType="oneTimeCode"
        autoComplete="one-time-code"
        maxLength={10}
        returnKeyType="done"
        onSubmitEditing={() => codeValid && !busy && run(() => verifyCode(sentTo, code))}
        editable={!busy}
        accessibilityLabel="Sign-in code"
      />
      <PrimaryButton
        label="Sign In"
        busy={busy}
        disabled={!codeValid}
        onPress={() => run(() => verifyCode(sentTo, code))}
      />
      {shownError ? <Text style={styles.error}>{shownError}</Text> : null}
      <View style={styles.links}>
        <Pressable hitSlop={8} disabled={busy} onPress={() => send(sentTo)}>
          <Text style={styles.link}>Resend email</Text>
        </Pressable>
        <Pressable
          hitSlop={8}
          disabled={busy}
          onPress={() => {
            setSentTo(null);
            setError(null);
          }}
        >
          <Text style={styles.link}>Use a different email</Text>
        </Pressable>
      </View>
    </>
  );
}

function SignedIn({ email, onSignedOut }: { email: string; onSignedOut: () => void }) {
  const { signOut } = useAuth();
  const devices = useDevices();
  const previewsOn = usePreviewsEnabled();
  const [thisDevice, setThisDevice] = useState<string | null>(null);
  useEffect(() => {
    getDeviceId().then(setThisDevice);
  }, []);

  const confirmSignOut = () =>
    Alert.alert('Sign out?', 'Your tabs stay on this phone but stop syncing with your other devices.', [
      { text: 'Cancel', style: 'cancel' },
      {
        text: 'Sign Out',
        style: 'destructive',
        onPress: () =>
          signOut().then(onSignedOut, (e: unknown) =>
            Alert.alert('Couldn’t sign out', e instanceof Error ? e.message : String(e)),
          ),
      },
    ]);

  return (
    <>
      <View style={styles.avatar}>
        <Text style={styles.avatarText}>{email.slice(0, 1).toUpperCase()}</Text>
      </View>
      <Text style={styles.title}>Signed in</Text>
      <Text style={styles.lede}>{email}</Text>

      <Text style={styles.sectionLabel}>Your devices</Text>
      <View style={styles.card}>
        {devices.length ? (
          devices.map((device, i) => {
            const Icon = device.platform === 'ios' || device.platform === 'android' ? Smartphone : Laptop;
            const self = device.id === thisDevice;
            return (
              <View key={device.id} style={[styles.deviceRow, i > 0 && styles.divider]}>
                <Icon size={20} color={colors.textSecondary} />
                <View style={styles.deviceText}>
                  <Text style={styles.deviceName} numberOfLines={1}>
                    {self ? `This ${device.name}` : device.name}
                  </Text>
                  <Text style={styles.deviceDetail}>
                    {self ? 'Signed in' : `Last seen ${updatedAgo(device.lastSeenAt)}`}
                  </Text>
                </View>
              </View>
            );
          })
        ) : (
          <Text style={styles.empty}>
            Sign in to Avora on your Mac with this email and you’ll have the same tabs on both.
          </Text>
        )}
      </View>

      <Text style={[styles.sectionLabel, styles.sectionGap]}>Tab previews</Text>
      <View style={styles.card}>
        <View style={styles.deviceRow}>
          <View style={styles.deviceText}>
            <Text style={styles.deviceName}>Share page previews</Text>
            <Text style={styles.deviceDetail}>
              A small picture of each tab, shown on your devices while it loads. Never taken of pages with a password
              field.
            </Text>
          </View>
          <Switch value={previewsOn} onValueChange={setPreviewsEnabled} accessibilityLabel="Share page previews" />
        </View>
      </View>

      <Pressable onPress={confirmSignOut} style={({ pressed }) => [styles.signOut, pressed && styles.pressed]}>
        <Text style={styles.signOutText}>Sign Out</Text>
      </Pressable>
    </>
  );
}

function NotConfigured() {
  return (
    <>
      <Text style={styles.title}>Sync isn’t set up</Text>
      <Text style={styles.lede}>
        This build has no Supabase project. Add EXPO_PUBLIC_SUPABASE_URL and EXPO_PUBLIC_SUPABASE_PUBLISHABLE_KEY to
        .env.local and restart the dev server.
      </Text>
    </>
  );
}

function PrimaryButton({
  label,
  busy,
  disabled,
  onPress,
}: {
  label: string;
  busy: boolean;
  disabled: boolean;
  onPress: () => void;
}) {
  const inactive = busy || disabled;
  return (
    <Pressable
      onPress={onPress}
      disabled={inactive}
      style={({ pressed }) => [styles.button, inactive && styles.buttonDisabled, pressed && styles.pressed]}
      accessibilityRole="button"
      accessibilityState={{ disabled: inactive, busy }}
    >
      {busy ? <ActivityIndicator color={colors.background} /> : <Text style={styles.buttonText}>{label}</Text>}
    </Pressable>
  );
}

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: colors.background },
  header: { flexDirection: 'row', justifyContent: 'flex-end', paddingHorizontal: 16, paddingTop: 14 },
  close: {
    width: 32,
    height: 32,
    borderRadius: 16,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: colors.fill,
  },
  content: { paddingHorizontal: 24, paddingTop: 24, maxWidth: 480, width: '100%', alignSelf: 'center' },
  spinner: { marginTop: 80 },
  title: { color: colors.text, fontSize: 28, fontWeight: '700' },
  lede: { color: colors.textSecondary, fontSize: 16, lineHeight: 23, marginTop: 8, marginBottom: 24 },
  strong: { color: colors.text, fontWeight: '600' },
  input: {
    height: 50,
    paddingHorizontal: 16,
    borderRadius: radius.row,
    backgroundColor: colors.surface,
    borderWidth: 1,
    borderColor: colors.border,
    color: colors.text,
    fontSize: 17,
  },
  codeInput: { fontSize: 22, letterSpacing: 6, fontVariant: ['tabular-nums'] },
  button: {
    height: 50,
    marginTop: 12,
    borderRadius: radius.pill,
    backgroundColor: colors.accent,
    alignItems: 'center',
    justifyContent: 'center',
  },
  buttonDisabled: { opacity: 0.45 },
  buttonText: { color: colors.background, fontSize: 16, fontWeight: '700' },
  pressed: { opacity: 0.8 },
  error: { color: colors.danger, fontSize: 14, lineHeight: 20, marginTop: 12 },
  footnote: { color: colors.textTertiary, fontSize: 13, lineHeight: 19, marginTop: 16 },
  links: { flexDirection: 'row', justifyContent: 'space-between', marginTop: 20 },
  link: { color: colors.accent, fontSize: 15 },
  badge: {
    width: 48,
    height: 48,
    borderRadius: 24,
    backgroundColor: colors.accentSoft,
    alignItems: 'center',
    justifyContent: 'center',
    marginBottom: 16,
  },
  avatar: {
    width: 56,
    height: 56,
    borderRadius: 28,
    backgroundColor: colors.accentSoft,
    alignItems: 'center',
    justifyContent: 'center',
    marginBottom: 16,
  },
  avatarText: { color: colors.accent, fontSize: 24, fontWeight: '700' },
  sectionLabel: {
    color: colors.textTertiary,
    fontSize: 12,
    fontWeight: '600',
    textTransform: 'uppercase',
    letterSpacing: 0.6,
    marginBottom: 8,
    paddingHorizontal: 4,
  },
  sectionGap: { marginTop: 28 },
  card: { backgroundColor: colors.surface, borderRadius: radius.card, overflow: 'hidden' },
  deviceRow: { flexDirection: 'row', alignItems: 'center', gap: 12, paddingHorizontal: 14, paddingVertical: 12 },
  divider: { borderTopWidth: StyleSheet.hairlineWidth, borderTopColor: colors.border },
  deviceText: { flex: 1 },
  deviceName: { color: colors.text, fontSize: 15 },
  deviceDetail: { color: colors.textTertiary, fontSize: 12, marginTop: 1 },
  empty: { color: colors.textTertiary, fontSize: 14, lineHeight: 20, padding: 14 },
  signOut: {
    height: 50,
    marginTop: 32,
    borderRadius: radius.pill,
    backgroundColor: colors.surface,
    alignItems: 'center',
    justifyContent: 'center',
  },
  signOutText: { color: colors.danger, fontSize: 16, fontWeight: '600' },
});
