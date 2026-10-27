import type { Session } from '@supabase/supabase-js';
import * as Linking from 'expo-linking';
import { createContext, use, useEffect, useMemo, useState } from 'react';

import { supabase } from './supabase';

type Auth = {
  // False when the build has no Supabase project configured.
  configured: boolean;
  // False until the stored session has been read.
  ready: boolean;
  session: Session | null;
  // Why the last magic link couldn't sign you in (expired, already used...).
  linkError: string | null;
  sendLink: (email: string) => Promise<void>;
  verifyCode: (email: string, code: string) => Promise<void>;
  signOut: () => Promise<void>;
};

const AuthContext = createContext<Auth | null>(null);

// Magic links come back to this route with ?code=... (PKCE).
export const AUTH_REDIRECT = Linking.createURL('account');

// useLinkingURL replays the launch URL on every reload, and a code can only be
// exchanged once.
const handledUrls = new Set<string>();

export function AuthProvider({ children }: { children: React.ReactNode }) {
  const [ready, setReady] = useState(!supabase);
  const [session, setSession] = useState<Session | null>(null);
  const [linkError, setLinkError] = useState<string | null>(null);
  const url = Linking.useLinkingURL();

  useEffect(() => {
    if (!supabase) return;
    // Fires INITIAL_SESSION first, so this also covers the stored session.
    const { data } = supabase.auth.onAuthStateChange((_event, next) => {
      setSession(next);
      setReady(true);
    });
    return () => data.subscription.unsubscribe();
  }, []);

  useEffect(() => {
    if (!url) return;
    completeMagicLink(url).then((error) => {
      if (error !== undefined) setLinkError(error);
    });
  }, [url]);

  const value = useMemo<Auth>(
    () => ({
      configured: !!supabase,
      ready,
      session,
      linkError,
      sendLink: async (email) => {
        setLinkError(null);
        const { error } = await requireClient().auth.signInWithOtp({
          email,
          options: { emailRedirectTo: AUTH_REDIRECT },
        });
        if (error) throw error;
      },
      verifyCode: async (email, code) => {
        const { error } = await requireClient().auth.verifyOtp({ email, token: code, type: 'email' });
        if (error) throw error;
        setLinkError(null);
      },
      // Local scope: signing out the phone shouldn't sign out the desktop.
      signOut: async () => {
        const { error } = await requireClient().auth.signOut({ scope: 'local' });
        if (error) throw error;
      },
    }),
    [ready, session, linkError],
  );

  return <AuthContext value={value}>{children}</AuthContext>;
}

export function useAuth(): Auth {
  const auth = use(AuthContext);
  if (!auth) throw new Error('useAuth must be used inside <AuthProvider>');
  return auth;
}

// Signs in from a magic-link URL. Resolves to the error to show, null on
// success, or undefined when the URL isn't a sign-in callback at all.
async function completeMagicLink(url: string): Promise<string | null | undefined> {
  if (!supabase || handledUrls.has(url)) return undefined;
  const params = callbackParams(url);
  if (!params.code && !params.error_description) return undefined;
  handledUrls.add(url);
  if (params.error_description) return params.error_description;
  const { error } = await supabase.auth.exchangeCodeForSession(params.code);
  // The usual cause is opening the link on a different device than the one
  // that asked for it; the code in the same email still works.
  return error ? 'That link didn’t work on this device. Enter the code from the email instead.' : null;
}

function requireClient() {
  if (!supabase) throw new Error('Sync isn’t set up in this build.');
  return supabase;
}

// Reads parameters from both the query and the fragment; Supabase reports
// errors in whichever one the flow uses.
function callbackParams(url: string): Record<string, string> {
  const params: Record<string, string> = {};
  for (const part of url.split(/[?#]/).slice(1)) {
    for (const pair of part.split('&')) {
      const [key, value = ''] = pair.split('=');
      if (key) params[decodeURIComponent(key)] = decodeURIComponent(value.replace(/\+/g, ' '));
    }
  }
  return params;
}
