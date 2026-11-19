-- Lets a signed-in user delete their own account from inside the app (App
-- Store guideline 5.1.1(v)) without the app holding the service-role key.
--
-- The caller removes their tab previews first, through the Storage API, which
-- deletes the stored files as well as their rows; deleting from
-- storage.objects in SQL would leave the files behind (newer Supabase
-- projects refuse it outright). This function then deletes the account's Spaces, items and devices
-- and the auth.users row itself, which also ends every session and refresh
-- token the account has. Other devices are signed out the next time they
-- refresh their session.
--
-- Runs as the function owner (security definer) because clients can't delete
-- from auth.users, nor from spaces and items, which have no delete policy. It
-- only ever touches auth.uid()'s own rows.
create function public.delete_account() returns void
language plpgsql security definer set search_path = '' as $$
declare
  uid uuid := auth.uid();
begin
  if uid is null then
    raise exception 'Not signed in' using errcode = '42501';
  end if;

  -- Refuse rather than orphan files: the app deletes previews first and
  -- retries if a device uploaded another one in between.
  if exists (
    select 1 from storage.objects
    where bucket_id = 'tab-previews' and (storage.foldername(name))[1] = uid::text
  ) then
    raise exception 'Tab previews remain; delete them first'
      using errcode = 'P0001', hint = 'previews_remain';
  end if;

  -- auth.users cascades to these too; deleting them first keeps the order
  -- obvious and doesn't depend on the foreign keys.
  delete from public.items where user_id = uid;
  delete from public.spaces where user_id = uid;
  delete from public.devices where user_id = uid;
  delete from auth.users where id = uid;
end;
$$;

revoke execute on function public.delete_account() from public, anon;
grant execute on function public.delete_account() to authenticated;
