import AsyncStorage from '@react-native-async-storage/async-storage';
import { createClient, type SupabaseClient } from '@supabase/supabase-js';
import * as aesjs from 'aes-js';
import * as Crypto from 'expo-crypto';
import * as SecureStore from 'expo-secure-store';
import { AppState } from 'react-native';

// Hermes has no Web Crypto. Without these, auth-js builds its PKCE verifier
// from Math.random and falls back to the "plain" challenge method.
type CryptoShim = { getRandomValues?: Crypto['getRandomValues']; subtle?: SubtleCrypto };
const globalCrypto = ((globalThis as { crypto?: CryptoShim }).crypto ??= {});
globalCrypto.getRandomValues ??= ((array: Parameters<typeof Crypto.getRandomValues>[0]) =>
  Crypto.getRandomValues(array)) as Crypto['getRandomValues'];
globalCrypto.subtle ??= {
  digest: (algorithm: AlgorithmIdentifier, data: BufferSource) => {
    const name = typeof algorithm === 'string' ? algorithm : algorithm.name;
    if (name.toUpperCase() !== 'SHA-256') throw new Error(`Unsupported digest: ${name}`);
    return Crypto.digest(Crypto.CryptoDigestAlgorithm.SHA256, data);
  },
} as SubtleCrypto;

// The session (with its refresh token) is too large for the keychain on some
// iOS versions, so the keychain holds a per-key AES key and AsyncStorage holds
// the ciphertext. This is the layout Supabase recommends for React Native.
class LargeSecureStore {
  private async encrypt(key: string, value: string) {
    const encryptionKey = Crypto.getRandomBytes(256 / 8);
    const cipher = new aesjs.ModeOfOperation.ctr(encryptionKey, new aesjs.Counter(1));
    const encrypted = cipher.encrypt(aesjs.utils.utf8.toBytes(value));
    await SecureStore.setItemAsync(key, aesjs.utils.hex.fromBytes(encryptionKey));
    return aesjs.utils.hex.fromBytes(encrypted);
  }

  private async decrypt(key: string, value: string) {
    const encryptionKeyHex = await SecureStore.getItemAsync(key);
    if (!encryptionKeyHex) return null;
    const cipher = new aesjs.ModeOfOperation.ctr(aesjs.utils.hex.toBytes(encryptionKeyHex), new aesjs.Counter(1));
    return aesjs.utils.utf8.fromBytes(cipher.decrypt(aesjs.utils.hex.toBytes(value)));
  }

  async getItem(key: string) {
    const encrypted = await AsyncStorage.getItem(key);
    return encrypted ? this.decrypt(key, encrypted) : null;
  }

  async setItem(key: string, value: string) {
    await AsyncStorage.setItem(key, await this.encrypt(key, value));
  }

  async removeItem(key: string) {
    await AsyncStorage.removeItem(key);
    await SecureStore.deleteItemAsync(key);
  }
}

const url = process.env.EXPO_PUBLIC_SUPABASE_URL;
const publishableKey = process.env.EXPO_PUBLIC_SUPABASE_PUBLISHABLE_KEY;

// null until .env.local provides a project; the browser works without one and
// the account screen explains what's missing.
export const supabase: SupabaseClient | null =
  url && publishableKey
    ? createClient(url, publishableKey, {
        auth: {
          storage: new LargeSecureStore(),
          autoRefreshToken: true,
          persistSession: true,
          // Callback URLs arrive through expo-linking; see auth.tsx.
          detectSessionInUrl: false,
          flowType: 'pkce',
        },
      })
    : null;

// React Native has no visibility events, so refresh tokens only while the app
// is in the foreground.
if (supabase) {
  AppState.addEventListener('change', (state) => {
    if (state === 'active') supabase.auth.startAutoRefresh();
    else supabase.auth.stopAutoRefresh();
  });
}
