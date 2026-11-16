-- A Space's theme: the colours its chrome is tinted with, as the JSON text
-- described under "Space themes" in docs/SYNC.md. Empty means the default
-- look. Stored as text rather than jsonb so devices can diff it as the exact
-- string they wrote.
alter table public.spaces add column theme text not null default '';

-- As before, plus `theme`. A device that doesn't send `theme` leaves it alone,
-- so builds from before themes never clear one.
create or replace function public.sync_spaces(changes jsonb) returns void
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
        theme = case when c ? 'theme' then coalesce(c->>'theme', '') else s.theme end,
        position = case when c ? 'position' then c->>'position' else s.position end,
        deleted_at = case when c ? 'deleted_at' then (c->>'deleted_at')::timestamptz else s.deleted_at end,
        updated_by = (c->>'updated_by')::uuid
      where s.id = (c->>'id')::uuid;
      exit when found;
      insert into public.spaces (id, name, icon, color, theme, position, updated_by, deleted_at)
      values ((c->>'id')::uuid, c->>'name', coalesce(c->>'icon', ''), coalesce(c->>'color', ''),
              coalesce(c->>'theme', ''), c->>'position', (c->>'updated_by')::uuid,
              (c->>'deleted_at')::timestamptz)
      on conflict (id) do nothing;
      exit when found;
    end loop;
  end loop;
end;
$$;
