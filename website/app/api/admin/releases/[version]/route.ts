import { NextResponse, type NextRequest } from "next/server";
import { updateRelease } from "@/lib/release-store";
import type { ReleaseStatus } from "@/lib/release";

const STATUSES: ReleaseStatus[] = ["draft", "published", "discarded"];

function parseEdit(body: unknown) {
  if (typeof body !== "object" || body === null) return null;
  const { title, bullets, status } = body as Record<string, unknown>;
  if (title !== null && typeof title !== "string") return null;
  if (!Array.isArray(bullets) || bullets.some((b) => typeof b !== "string")) {
    return null;
  }
  if (status !== undefined && !STATUSES.includes(status as ReleaseStatus)) {
    return null;
  }
  return {
    title,
    bullets: bullets as string[],
    status: status as ReleaseStatus | undefined,
  };
}

export async function PATCH(
  request: NextRequest,
  { params }: RouteContext<"/api/admin/releases/[version]">,
) {
  if (process.env.NODE_ENV !== "development") {
    return new Response(null, { status: 404 });
  }
  // A page open in another tab can't send this type without passing CORS, so
  // requiring it keeps other sites off this unauthenticated local route.
  if (request.headers.get("content-type") !== "application/json") {
    return NextResponse.json({ error: "Expected JSON" }, { status: 415 });
  }

  const body: unknown = await request.json().catch(() => null);
  const edit = parseEdit(body);
  if (!edit) {
    return NextResponse.json({ error: "Invalid release" }, { status: 400 });
  }

  const { version } = await params;
  const result = await updateRelease(version, edit);
  if (!result.ok) {
    return NextResponse.json({ error: result.error }, { status: 422 });
  }
  return NextResponse.json({ release: result.release });
}
