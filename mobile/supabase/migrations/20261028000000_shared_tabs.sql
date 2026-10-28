-- One shared set of Spaces, sidebar items and tabs per account. Every device
-- reads and edits the same rows, so a tab opened on the phone opens on every
-- Mac and closing it anywhere closes it everywhere. See docs/SYNC.md.
--
-- Rows are never deleted by clients: a deletion sets |deleted_at|, so a device
-- that was offline still hears about it. Writes are last-writer-wins per
-- column, by the server's clock.

create table public.spaces (
  id uuid primary key,
  user_id uuid not null default auth.uid() references auth.users (id) on delete cascade,
  name text not null,
  -- Lucide icon name, e.g. "briefcase".
  icon text not null default '',
  -- "#RRGGBB".
  color text not null default '',
  -- Fractional index; compare bytewise, hence the "C" collation.
  position text collate "C" not null,
  updated_at timestamptz not null default now(),
  -- The device id (public.devices.id) that last wrote the row.
  updated_by uuid,
  deleted_at timestamptz
);

create index spaces_user_updated_idx on public.spaces (user_id, updated_at);

create table public.items (
  -- A today tab's id is its Avora tab GUID, shared by every copy of the tab.
  id uuid primary key,
  user_id uuid not null default auth.uid() references auth.users (id) on delete cascade,
  space_id uuid not null,
  kind text not null check (kind in ('today', 'pinned', 'favorite', 'folder')),
  -- A pinned item inside a pinned folder points at the folder.
  parent_id uuid,
  -- Empty only for folders.
  url text not null default '',
  title text not null default '',
  favicon_url text,
  -- Object in the tab-previews bucket, "{user_id}/{id}.jpg"; null when the
  -- item has no preview or isn't allowed one.
  preview_path text,
  preview_updated_at timestamptz,
  position text collate "C" not null,
  last_active_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  updated_by uuid,
  deleted_at timestamptz,
  check (kind = 'folder' or url <> ''),
  check (url = '' or url ~* '^https?://')
);

create index items_user_updated_idx on public.items (user_id, updated_at);

alter table public.spaces enable row level security;
alter table public.items enable row level security;

create policy "Read own spaces" on public.spaces
  for select to authenticated using (user_id = (select auth.uid()));
create policy "Add own spaces" on public.spaces
  for insert to authenticated with check (user_id = (select auth.uid()));
create policy "Update own spaces" on public.spaces
  for update to authenticated
  using (user_id = (select auth.uid())) with check (user_id = (select auth.uid()));

create policy "Read own items" on public.items
  for select to authenticated using (user_id = (select auth.uid()));
create policy "Add own items" on public.items
  for insert to authenticated with check (user_id = (select auth.uid()));
create policy "Update own items" on public.items
  for update to authenticated
  using (user_id = (select auth.uid())) with check (user_id = (select auth.uid()));
-- No delete policy: clients tombstone. Tombstones are purged by
-- public.purge_tombstones() below.

-- The server clock decides every timestamp that ordering depends on.
create function public.shared_rows_touch() returns trigger
language plpgsql set search_path = '' as $$
begin
  new.updated_at := now();
  if tg_op = 'UPDATE' and new.deleted_at is not null and old.deleted_at is not null then
    new.deleted_at := old.deleted_at;
  elsif new.deleted_at is not null then
    new.deleted_at := now();
  end if;
  if tg_op = 'UPDATE' then
    new.user_id := old.user_id;
  end if;
  return new;
end;
$$;

create trigger spaces_touch before insert or update on public.spaces
  for each row execute function public.shared_rows_touch();
create trigger items_touch before insert or update on public.items
  for each row execute function public.shared_rows_touch();

-- Clients write through these two functions rather than a PostgREST upsert:
-- an upsert has to satisfy every NOT NULL column even when the row exists, so
-- it can't change one column of a row the device doesn't hold in full. Each
-- element of |changes| is an object with "id" and only the columns that
-- changed; a key that is present (even as null) is written, a missing key is
-- left alone. A row that doesn't exist yet is created from the same object,
-- which then needs every required column. Runs as the caller, so row-level
-- security applies; a row owned by another account is silently skipped.
create function public.sync_items(changes jsonb) returns void
language plpgsql security invoker set search_path = '' as $$
declare
  c jsonb;
  attempt int;
begin
  for c in select * from jsonb_array_elements(changes) loop
    for attempt in 1..2 loop
      update public.items i set
        space_id = case when c ? 'space_id' then (c->>'space_id')::uuid else i.space_id end,
        kind = case when c ? 'kind' then c->>'kind' else i.kind end,
        parent_id = case when c ? 'parent_id' then (c->>'parent_id')::uuid else i.parent_id end,
        url = case when c ? 'url' then coalesce(c->>'url', '') else i.url end,
        title = case when c ? 'title' then coalesce(c->>'title', '') else i.title end,
        favicon_url = case when c ? 'favicon_url' then c->>'favicon_url' else i.favicon_url end,
        preview_path = case when c ? 'preview_path' then c->>'preview_path' else i.preview_path end,
        preview_updated_at = case when c ? 'preview_updated_at'
          then (c->>'preview_updated_at')::timestamptz else i.preview_updated_at end,
        position = case when c ? 'position' then c->>'position' else i.position end,
        last_active_at = case when c ? 'last_active_at'
          then (c->>'last_active_at')::timestamptz else i.last_active_at end,
        deleted_at = case when c ? 'deleted_at' then (c->>'deleted_at')::timestamptz else i.deleted_at end,
        updated_by = (c->>'updated_by')::uuid
      where i.id = (c->>'id')::uuid;
      exit when found;
      insert into public.items (id, space_id, kind, parent_id, url, title, favicon_url,
                                preview_path, preview_updated_at, position, last_active_at,
                                updated_by, deleted_at)
      values ((c->>'id')::uuid, (c->>'space_id')::uuid, c->>'kind', (c->>'parent_id')::uuid,
              coalesce(c->>'url', ''), coalesce(c->>'title', ''), c->>'favicon_url',
              c->>'preview_path', (c->>'preview_updated_at')::timestamptz, c->>'position',
              (c->>'last_active_at')::timestamptz, (c->>'updated_by')::uuid,
              (c->>'deleted_at')::timestamptz)
      on conflict (id) do nothing;
      -- Lost a race with another device creating the same row: update it.
      exit when found;
    end loop;
  end loop;
end;
$$;

create function public.sync_spaces(changes jsonb) returns void
language plpgsql security invoker set search_path = '' as $$
declare
  c jsonb;
  attempt int;
begin
  for c in select * from jsonb_array_elements(changes) loop
    for attempt in 1..2 loop
      update public.spaces s set
        name = case when c ? 'name' then c->>'name' else s.name end,
        icon = case when c ? 'icon' then coalesce(c->>'icon', '') else s.icon end,
        color = case when c ? 'color' then coalesce(c->>'color', '') else s.color end,
        position = case when c ? 'position' then c->>'position' else s.position end,
        deleted_at = case when c ? 'deleted_at' then (c->>'deleted_at')::timestamptz else s.deleted_at end,
        updated_by = (c->>'updated_by')::uuid
      where s.id = (c->>'id')::uuid;
      exit when found;
      insert into public.spaces (id, name, icon, color, position, updated_by, deleted_at)
      values ((c->>'id')::uuid, c->>'name', coalesce(c->>'icon', ''), coalesce(c->>'color', ''),
              c->>'position', (c->>'updated_by')::uuid, (c->>'deleted_at')::timestamptz)
      on conflict (id) do nothing;
      exit when found;
    end loop;
  end loop;
end;
$$;

revoke execute on function public.sync_items(jsonb) from public, anon;
revoke execute on function public.sync_spaces(jsonb) from public, anon;
grant execute on function public.sync_items(jsonb) to authenticated;
grant execute on function public.sync_spaces(jsonb) to authenticated;

-- Tombstones older than 30 days. Run from the dashboard or pg_cron; a device
-- that has been offline longer than that does a full reload instead.
create function public.purge_tombstones() returns void
language sql set search_path = '' as $$
  delete from public.items where deleted_at < now() - interval '30 days';
  delete from public.spaces where deleted_at < now() - interval '30 days';
$$;
revoke execute on function public.purge_tombstones() from public, anon, authenticated;

alter publication supabase_realtime add table public.spaces;
alter publication supabase_realtime add table public.items;

-- The devices table now only lists signed-in devices; nobody reads its tabs.
alter table public.devices add column last_seen_at timestamptz not null default now();

-- Page previews: small JPEGs, private to their owner.
insert into storage.buckets (id, name, public, file_size_limit, allowed_mime_types)
values ('tab-previews', 'tab-previews', false, 262144, array['image/jpeg']);

create policy "Read own previews" on storage.objects
  for select to authenticated
  using (bucket_id = 'tab-previews' and (storage.foldername(name))[1] = (select auth.uid())::text);
create policy "Add own previews" on storage.objects
  for insert to authenticated
  with check (bucket_id = 'tab-previews' and (storage.foldername(name))[1] = (select auth.uid())::text);
create policy "Replace own previews" on storage.objects
  for update to authenticated
  using (bucket_id = 'tab-previews' and (storage.foldername(name))[1] = (select auth.uid())::text);
create policy "Remove own previews" on storage.objects
  for delete to authenticated
  using (bucket_id = 'tab-previews' and (storage.foldername(name))[1] = (select auth.uid())::text);
