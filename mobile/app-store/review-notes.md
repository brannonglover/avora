# App Review notes — reply to Guideline 2.1 (Information Needed)

Paste everything between the two rules into **App Store Connect → App Review
Information → Notes**. The field takes 4000 characters, and this block fits.
Upload the screen recording under **Attachment** in the same section (see the
recording checklist at the end of this file).

---

1. SCREEN RECORDING
Attached in App Review Information. It was recorded on an iPhone running the latest iOS and starts at app launch. It shows browsing, tabs, Spaces, the tab switcher, creating an account with an email code, syncing, signing out, signing in with the demo account below, and deleting an account (Account → Delete Account).

2. PURPOSE AND AUDIENCE
Avora is a web browser for people who keep many tabs open and want them organized. Tabs sit in three places: Favorites, Pinned (tabs that stay) and Today (everything else, cleared in one tap). Spaces keep separate sets of tabs for Personal, Work and so on. The address bar is at the bottom for one-handed use, and you swipe it to move between tabs. It is the iPhone and iPad companion to Avora for Mac (avorabrowser.com). An optional free account syncs the same Spaces and tabs between iPhone, iPad and Mac. Without an account, everything stays on the device. Avora can be set as the default browser (the com.apple.developer.web-browser entitlement was granted for com.avora.browser on 18 Nov 2026).

3. HOW TO USE IT
No sign-in is needed to browse. Type a search or address in the bottom bar. Swipe the bar left or right to change tabs, swipe it up (or tap the tab count) to open the tab switcher, use + for a new tab and ⋯ for Pin, Share and Close. The Space button in the bottom bar switches Spaces.

Account (optional): open the tab switcher and tap the person icon. Normal users enter their email and get a one-time code. They type it in or tap the link. There are no passwords. Because App Review can't receive that email, the demo account below signs in with a password. When that address is typed, a password field appears.
  Email: appreview@avorabrowser.com
  Password: (the one in Sign-In Information)
Once signed in, tabs and Spaces sync with any other device on the same account.

Account deletion: Account → Delete Account (below Sign Out). This permanently deletes the account, its synced tabs, Spaces, device records and tab preview images, and then signs out. If you delete the demo account, we will recreate it before the next submission.

4. EXTERNAL SERVICES
- Supabase: email sign-in, the database that holds synced tabs and Spaces, realtime sync between devices, and storage for small tab preview images. Previews are never taken of pages that have a password field.
- Resend: sends the sign-in code emails.
- Google: the default search engine for address-bar searches, and Google's favicon service for site icons.
- Apple WebKit (WKWebView) renders all web pages.
There are no payments, in-app purchases, ads, analytics, tracking or AI services.

5. REGIONS
The app works the same in every region. There are no region-specific features or content. Websites themselves, including the default Google search, load according to the user's own network.

6. REGULATED OR THIRD-PARTY MATERIAL
Not applicable. Avora isn't in a regulated industry and has no licensed or protected content. It shows only the websites the user chooses to visit.

User-generated content: Avora has no social features. Nothing a user does is visible to any other user, because synced tabs are private to the account that created them. So there is no content to report or block.

---

## Recording checklist (for you, before resubmitting)

Record on a physical iPhone running the latest iOS, using the build you're
submitting (install it from TestFlight). Use Control Center → Screen Recording.
The recording must **start with launching the app** from the Home Screen.

1. Launch Avora from the Home Screen and let the splash finish.
2. Type a search in the bottom bar, open a result, then type an address (for example apple.com).
3. Open a few tabs with `+`. Swipe the bar left and right to switch, then swipe up to open the tab switcher.
4. Pin a tab (`⋯` → Pin), switch Spaces, close a tab and tap **Undo**.
5. **Registration:** open the person icon → enter a throwaway address you can read (for example a `+review` alias) → enter the emailed code. Show your tabs syncing if a Mac is handy.
6. **Delete Account** on that throwaway account. Show the confirmation, then the sign-in form coming back.
7. **Login:** sign in with `appreview@avorabrowser.com` and its password. This shows the reviewer's path.
8. Sign out.

Keep the throwaway account for steps 5 and 6 so the demo account still exists
when the reviewer uses it.

Before you submit:
- In Supabase **Authentication → Users**, check that `appreview@avorabrowser.com`
  exists and is confirmed, and that the password in App Store Connect works.
  Try it on the phone during step 7.
- Enter the email and password under **App Review Information → Sign-in required**.
- Check that the build you attach includes **Delete Account**. It landed in
  `1c5ec1b`, and the delete_account migration has to be applied to the
  production Supabase project.
