-- One row per browser install. Each device owns its row and replaces its
-- |tabs| snapshot wholesale whenever its tab strip changes; other devices of
-- the same account read it. See docs/SYNC.md for the tab shape.
create table public.devices (
  id uuid primary key,
  user_id uuid not null default auth.uid() references auth.users (id) on delete cascade,
  name text not null,
  platform text not null check (platform in ('macos', 'windows', 'linux', 'ios', 'android')),
  tabs jsonb not null default '[]'::jsonb check (jsonb_typeof(tabs) = 'array'),
  updated_at timestamptz not null default now()
);

create index devices_user_id_idx on public.devices (user_id);

alter table public.devices enable row level security;

create policy "Read own devices" on public.devices
  for select to authenticated using (user_id = (select auth.uid()));
create policy "Add own devices" on public.devices
  for insert to authenticated with check (user_id = (select auth.uid()));
create policy "Update own devices" on public.devices
  for update to authenticated
  using (user_id = (select auth.uid())) with check (user_id = (select auth.uid()));
create policy "Remove own devices" on public.devices
  for delete to authenticated using (user_id = (select auth.uid()));

-- Clients may send updated_at, but the server clock is the one that counts.
create function public.devices_touch() returns trigger
language plpgsql set search_path = '' as $$
begin
  new.updated_at := now();
  return new;
end;
$$;

create trigger devices_touch before insert or update on public.devices
  for each row execute function public.devices_touch();

-- Lets the phone hear about desktop changes as they happen.
alter publication supabase_realtime add table public.devices;
