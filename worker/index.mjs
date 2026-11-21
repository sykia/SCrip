const redirect = "http://127.0.0.1:43821/callback";
function json(data, status = 200) {
  return Response.json(data, {status, headers: {"Cache-Control": "no-store"}});
}

async function readFormBody(request, maximum = 8192) {
  const header = request.headers.get("Content-Length");
  let declared;
  if (header !== null) {
    if (!/^\d+$/.test(header)) return {error: "length"};
    declared = Number(header);
    if (!Number.isSafeInteger(declared) || declared <= 0 || declared > maximum) return {error: "size"};
  }
  const reader = request.body?.getReader();
  if (!reader) return {error: "size"};
  const chunks = [];
  let total = 0;
  try {
    while (true) {
      const {done, value} = await reader.read();
      if (done) break;
      total += value.byteLength;
      if (total > maximum) {
        await reader.cancel();
        return {error: "size"};
      }
      chunks.push(value);
    }
  } catch {
    return {error: "body"};
  }
  if (!total || (declared !== undefined && declared !== total)) return {error: "length"};
  const bytes = new Uint8Array(total);
  let offset = 0;
  for (const chunk of chunks) {
    bytes.set(chunk, offset);
    offset += chunk.byteLength;
  }
  try { return {text: new TextDecoder("utf-8", {fatal: true}).decode(bytes)}; }
  catch { return {error: "body"}; }
}

const encoder = new TextEncoder();
function mediaTarget(value) {
  try {
    const url = new URL(value);
    return url.protocol === "https:" && !url.username && !url.password && !url.port
      && (url.hostname === "sndcdn.com" || url.hostname.endsWith(".sndcdn.com")
        || url.hostname === "media-streaming.soundcloud.cloud" || url.hostname.endsWith(".media-streaming.soundcloud.cloud")) ? url : null;
  } catch { return null; }
}
async function mediaKey(env) {
  return crypto.subtle.importKey("raw", encoder.encode("Sange media relay\n" + env.CLIENT_SECRET), {name: "HMAC", hash: "SHA-256"}, false, ["sign", "verify"]);
}
async function signedMediaUrl(value, origin, env, expires = Math.floor(Date.now() / 1000) + 1800, hint = "") {
  const target = mediaTarget(value);
  if (!target) throw new Error("media_origin");
  const signature = new Uint8Array(await crypto.subtle.sign("HMAC", await mediaKey(env), encoder.encode(expires + "\n" + target.href)));
  const sig = Array.from(signature, b => b.toString(16).padStart(2, "0")).join("");
  const extension = target.pathname.match(/\.(m3u8|m4s|mp4|ts|mp3|aac|key)$/i)?.[1] || hint;
  const out = new URL("/media/stream" + (extension ? "." + extension : ""), origin);
  out.search = new URLSearchParams({url: target.href, expires: String(expires), sig}).toString();
  return out.href;
}
async function boundedText(response, limit) {
  const reader = response.body.getReader();
  const chunks = []; let length = 0;
  for (;;) {
    const {done, value} = await reader.read();
    if (done) break;
    length += value.byteLength;
    if (length > limit) { await reader.cancel(); throw new Error("size"); }
    chunks.push(value);
  }
  const result = new Uint8Array(length); let offset = 0;
  for (const chunk of chunks) { result.set(chunk, offset); offset += chunk.byteLength; }
  return new TextDecoder().decode(result);
}
async function relayMedia(request, env, url) {
  if (!["GET", "HEAD"].includes(request.method)) return json({error: "method"}, 405);
  if (!env.CLIENT_SECRET || url.href.length > 16384) return json({error: "media_capability"}, 403);
  const target = mediaTarget(url.searchParams.get("url"));
  const expires = Number(url.searchParams.get("expires"));
  const now = Math.floor(Date.now() / 1000);
  const sig = url.searchParams.get("sig") || "";
  if (!target || !Number.isSafeInteger(expires) || expires < now || expires > now + 1800 || !/^[a-f0-9]{64}$/.test(sig))
    return json({error: "media_capability"}, 403);
  const bytes = new Uint8Array(sig.match(/../g).map(byte => parseInt(byte, 16)));
  if (!await crypto.subtle.verify("HMAC", await mediaKey(env), bytes, encoder.encode(expires + "\n" + target.href)))
    return json({error: "media_capability"}, 403);
  if (!env.MEDIA_LIMIT || !(await env.MEDIA_LIMIT.limit({key: request.headers.get("CF-Connecting-IP") || "unknown"})).success)
    return json({error: "rate_limit"}, 429);
  const headers = new Headers();
  const range = request.headers.get("Range");
  if (range && !/^bytes=\d+-\d*$/.test(range)) return json({error: "range"}, 400);
  if (range) headers.set("Range", range);
  try {
    let next = target, upstream;
    for (let redirects = 0; redirects <= 3; redirects++) {
      upstream = await fetch(next, {method: request.method, headers, redirect: "manual", signal: AbortSignal.timeout(20000)});
      if (![301, 302, 303, 307, 308].includes(upstream.status)) break;
      const location = mediaTarget(new URL(upstream.headers.get("Location") || "", next).href);
      await upstream.body?.cancel();
      if (!location || redirects === 3) return json({error: "upstream_redirect"}, 502);
      next = location;
    }
    if (![200, 206].includes(upstream.status)) { await upstream.body?.cancel(); return json({error: "media_unavailable"}, upstream.status >= 400 ? upstream.status : 502); }
    const out = new Headers({"Cache-Control": "no-store", "Content-Type": upstream.headers.get("Content-Type") || "application/octet-stream"});
    if (request.method === "GET" && (out.get("Content-Type").toLowerCase().includes("mpegurl") || next.pathname.endsWith(".m3u8"))) {
      const text = await boundedText(upstream, 512 * 1024);
      if (!text.trimStart().startsWith("#EXTM3U")) return json({error: "playlist"}, 502);
      const lines = [];
      for (const line of text.split(/\r?\n/)) {
        if (line.startsWith("#")) {
          let rewritten = line;
          for (const match of line.matchAll(/URI="([^"]+)"/g))
            rewritten = rewritten.replace(match[0], 'URI="' + await signedMediaUrl(new URL(match[1], next).href, url.origin, env, expires) + '"');
          lines.push(rewritten);
        } else lines.push(line.trim() ? await signedMediaUrl(new URL(line.trim(), next).href, url.origin, env, expires, text.includes("#EXT-X-MAP:") ? "m4s" : "ts") : line);
      }
      out.set("Content-Type", "application/vnd.apple.mpegurl");
      return new Response(lines.join("\n"), {headers: out});
    }
    for (const key of ["Content-Length", "Content-Range", "Accept-Ranges"]) if (upstream.headers.has(key)) out.set(key, upstream.headers.get(key));
    return new Response(request.method === "HEAD" ? null : upstream.body, {status: upstream.status, headers: out});
  } catch { return json({error: "upstream_unavailable"}, 502); }
}

// Public image CDN only: no caller-selected origins, credentials or redirects.
async function relayArtwork(request, env, url) {
  if (!["GET", "HEAD"].includes(request.method)) return json({error: "method"}, 405);
  const match = url.pathname.match(/^\/artwork\/(i[1-4]\.sndcdn\.com)(\/[A-Za-z0-9_/-]+\.(?:jpg|jpeg|png|webp))$/i);
  if (!match || url.search || match[2].includes("..")) return json({error: "image_path"}, 400);
  if (!env.MEDIA_LIMIT || !(await env.MEDIA_LIMIT.limit({key: request.headers.get("CF-Connecting-IP") || "unknown"})).success)
    return json({error: "rate_limit"}, 429);
  try {
    const upstream = await fetch("https://" + match[1] + match[2], {
      method: request.method, redirect: "manual", signal: AbortSignal.timeout(15000)
    });
    const type = upstream.headers.get("Content-Type")?.split(";")[0].trim().toLowerCase();
    if (upstream.status !== 200 || !["image/jpeg", "image/png", "image/webp"].includes(type)) {
      await upstream.body?.cancel();
      return json({error: "image_unavailable"}, 502);
    }
    return new Response(request.method === "HEAD" ? null : upstream.body, {headers: {
      "Content-Type": type, "Cache-Control": "public, max-age=86400", "X-Content-Type-Options": "nosniff"
    }});
  } catch { return json({error: "upstream_unavailable"}, 502); }
}

// Fixed-origin API relay: no arbitrary URLs, redirects, cookies or shared caching.
async function relayApi(request, env, url) {
  if (!["GET", "HEAD", "POST", "PUT", "DELETE"].includes(request.method)) return json({error: "method"}, 405);
  const auth = request.headers.get("Authorization") || "";
  if (!/^OAuth [A-Za-z0-9._~+\/-]{1,4096}={0,2}$/.test(auth)) return json({error: "authorization"}, 401);
  if (!env.API_LIMIT || !(await env.API_LIMIT.limit({key: request.headers.get("CF-Connecting-IP") || "unknown"})).success)
    return json({error: "rate_limit"}, 429);
  const path = url.pathname.slice(4);
  if (!path.startsWith("/") || path.startsWith("//") || /%(?:2f|5c|2e)/i.test(path) || path.includes("\\"))
    return json({error: "path"}, 400);
  const upstreamUrl = new URL("https://api.soundcloud.com" + path + url.search);
  if (upstreamUrl.hostname !== "api.soundcloud.com") return json({error: "path"}, 400);
  const headers = new Headers({Authorization: auth, Accept: "application/json"});
  let body;
  if (request.method !== "GET" && request.method !== "HEAD") {
    const type = request.headers.get("Content-Type")?.split(";")[0].trim().toLowerCase();
    if (type !== "application/json") return json({error: "content_type"}, 415);
    if (request.body && request.headers.get("Content-Length") !== "0") {
    const parsed = await readFormBody(request, 1024 * 1024);
    if (parsed.error) return json({error: parsed.error}, parsed.error === "size" ? 413 : 400);
    try { JSON.parse(parsed.text); } catch { return json({error: "json"}, 400); }
    body = parsed.text; headers.set("Content-Type", "application/json");
    }
  }
  try {
    const upstream = await fetch(upstreamUrl, {method: request.method, headers, body, redirect: "manual", signal: AbortSignal.timeout(20000)});
    const out = new Headers({"Cache-Control": "no-store", "Content-Type": "application/json"});
    for (const key of ["Retry-After", "X-RateLimit-Remaining", "X-RateLimit-Reset"]) {
      if (upstream.headers.has(key)) out.set(key, upstream.headers.get(key));
    }
    if (upstream.status >= 300 && upstream.status < 400) {
      const location = new URL(upstream.headers.get("Location") || "", upstreamUrl);
      if ([301, 302, 303, 307, 308].includes(upstream.status) && mediaTarget(location.href))
        return json({url: await signedMediaUrl(location.href, url.origin, env)});
      if (![301, 302, 303, 307, 308].includes(upstream.status) || location.origin !== "https://api.soundcloud.com") return json({error: "upstream_redirect"}, 502);
      out.set("Location", location.href);
      return new Response(null, {status: upstream.status, headers: out});
    }
    if ([204, 205, 304].includes(upstream.status) || request.method === "HEAD")
      return new Response(null, {status: upstream.status, headers: out});
    if (!upstream.headers.get("Content-Type")?.toLowerCase().includes("application/json")) {
      await upstream.body?.cancel();
      return json({error: "upstream_unavailable"}, 502);
    }
    if (upstream.ok && (upstreamUrl.pathname.startsWith("/tracks/") || upstreamUrl.pathname.startsWith("/media/"))) {
      const streams = JSON.parse(await boundedText(upstream, 8 * 1024 * 1024));
      for (const [key, value] of Object.entries(streams)) {
        if (typeof value === "string" && mediaTarget(value)) streams[key] = await signedMediaUrl(value, url.origin, env);
      }
      return json(streams);
    }
    return new Response(upstream.body, {status: upstream.status, headers: out});
  } catch { return json({error: "upstream_unavailable"}, 502); }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/health" && request.method === "GET")
      return json({ready: Boolean(env.CLIENT_ID && env.CLIENT_SECRET), api_relay: Boolean(env.API_LIMIT), media_relay: Boolean(env.MEDIA_LIMIT)}, env.CLIENT_ID && env.CLIENT_SECRET ? 200 : 503);
    if (url.pathname.startsWith("/artwork/")) return relayArtwork(request, env, url);
    if (url.pathname === "/media" || url.pathname.startsWith("/media/")) return relayMedia(request, env, url);
    if (url.pathname.startsWith("/api/")) return relayApi(request, env, url);
    if (url.pathname !== "/oauth/token" || url.search) return json({error: "not_found"}, 404);
    if (request.method !== "POST") return json({error: "method"}, 405);
    if (request.headers.get("Content-Type")?.split(";")[0].trim().toLowerCase() !== "application/x-www-form-urlencoded")
      return json({error: "content_type"}, 415);
    const body = await readFormBody(request);
    if (body.error) return json({error: body.error}, body.error === "size" ? 413 : 400);
    if (!(await env.LOGIN_LIMIT.limit({key: request.headers.get("CF-Connecting-IP") || "unknown"})).success)
      return json({error: "rate_limit"}, 429);
    if (!env.CLIENT_ID || !env.CLIENT_SECRET) return json({error: "configuration"}, 503);

    let input;
    try { input = new URLSearchParams(body.text); }
    catch { return json({error: "invalid_request"}, 400); }
    const entries = [...input.entries()];
    const grantEntries = entries.filter(([key]) => key === "grant_type");
    if (grantEntries.length !== 1) return json({error: "invalid_request"}, 400);
    const grant = grantEntries[0][1];
    const fields = grant === "authorization_code" ? ["code", "code_verifier"] :
      grant === "refresh_token" ? ["refresh_token"] : [];
    const expected = new Set(["grant_type", ...fields]);
    if (!fields.length || entries.length !== expected.size || entries.some(([key, value]) => !expected.has(key) || !value) ||
        fields.some(key => entries.filter(([name]) => name === key).length !== 1))
      return json({error: "invalid_request"}, 400);
    const form = new URLSearchParams({grant_type: grant, client_id: env.CLIENT_ID, client_secret: env.CLIENT_SECRET});
    for (const field of fields) form.set(field, input.get(field));
    if (grant === "authorization_code") {
      if (!/^[A-Za-z0-9._~-]{43,128}$/.test(form.get("code_verifier"))) return json({error: "invalid_verifier"}, 400);
      form.set("redirect_uri", redirect);
    }
    try {
      const upstream = await fetch("https://secure.soundcloud.com/oauth/token", {
        method: "POST", body: form, headers: {Accept: "application/json"}, redirect: "manual"
      });
      if (upstream.status >= 300 && upstream.status < 400) return json({error: "upstream_redirect"}, 502);
      if (!upstream.ok) return json({error: "token_exchange"}, upstream.status >= 400 ? upstream.status : 502);
      const {access_token, refresh_token, expires_in} = await upstream.json();
      if (typeof access_token !== "string" || !access_token || typeof refresh_token !== "string" || !refresh_token)
        return json({error: "upstream_response"}, 502);
      return json({access_token, refresh_token, expires_in});
    } catch {
      return json({error: "upstream_unavailable"}, 502);
    }
  }
};
