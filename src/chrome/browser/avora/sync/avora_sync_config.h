// Copyright 2026 Avora. All rights reserved.

#ifndef CHROME_BROWSER_AVORA_SYNC_AVORA_SYNC_CONFIG_H_
#define CHROME_BROWSER_AVORA_SYNC_AVORA_SYNC_CONFIG_H_

namespace avora::sync {

// The Supabase project behind Avora accounts and tab sync. The phone app talks
// to the same project; the contract both sides follow is docs/SYNC.md in the
// avoraapp repository.
inline constexpr char kSupabaseUrl[] =
    "https://hfrlczxdekeqtgmgtgor.supabase.co";

// The project's publishable key. It is meant to ship inside clients: it only
// identifies the project, and row-level security on public.devices is what
// keeps every account to its own rows. Never put a service-role key here.
inline constexpr char kSupabasePublishableKey[] =
    "sb_publishable_juaKuRt_ftlqip0r_FJ_3g_voLcvQ6U";

}  // namespace avora::sync

#endif  // CHROME_BROWSER_AVORA_SYNC_AVORA_SYNC_CONFIG_H_
