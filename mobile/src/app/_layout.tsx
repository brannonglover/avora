import { Stack } from 'expo-router';
import * as SplashScreen from 'expo-splash-screen';
import { SafeAreaProvider } from 'react-native-safe-area-context';

import { colors } from '../browser/theme';
import { AnimatedSplash } from '../splash/AnimatedSplash';
import { AuthProvider } from '../sync/auth';

// AnimatedSplash takes over from the native splash once it's on screen.
SplashScreen.preventAutoHideAsync();

// A magic link can cold-start the app straight onto /account; keep the
// browser underneath so closing the sheet lands somewhere.
export const unstable_settings = { initialRouteName: 'index' };

export default function RootLayout() {
  return (
    <AuthProvider>
      <SafeAreaProvider style={{ backgroundColor: colors.background }}>
        <Stack screenOptions={{ headerShown: false, contentStyle: { backgroundColor: colors.background } }}>
          <Stack.Screen name="index" />
          {/* Full screen rather than a card sheet, whose rounded top corners
              let the dimmed page behind show through. */}
          <Stack.Screen name="account" options={{ presentation: 'fullScreenModal' }} />
        </Stack>
        <AnimatedSplash />
      </SafeAreaProvider>
    </AuthProvider>
  );
}
