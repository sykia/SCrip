import test from "node:test";
import assert from "node:assert/strict";
import worker from "../worker/index.mjs";

const env = {
  CLIENT_ID: "client-id",
  CLIENT_SECRET: "secret-value",
  LOGIN_LIMIT: {limit: async () => ({success: true})}
};
const validCode = "a".repeat(43);
const validParams = new URLSearchParams({
  grant_type: "authorization_code", code: "auth-code", code_verifier: validCode
});

function request(body, {length, contentType = "application/x-www-form-urlencoded"} = {}) {
  const headers = {"Content-Type": contentType};
  if (length !== undefined) headers["Content-Length"] = String(length);
  return new Request("https://worker.test/oauth/token", {method: "POST", headers, body});
}

async function response(req, upstream = async () => new Response(JSON.stringify({
  access_token: "access", refresh_token: "refresh", expires_in: 3600
}), {headers: {"Content-Type": "application/json"}})) {
  const previousFetch = globalThis.fetch;
  globalThis.fetch = upstream;
  try { return await worker.fetch(req, env); }
  finally { globalThis.fetch = previousFetch; }
}

test("accepts a valid authorization code form with an exact body length", async () => {
  const body = validParams.toString();
  const res = await response(request(body, {length: Buffer.byteLength(body)}));
  assert.equal(res.status, 200);
  assert.deepEqual(await res.json(), {access_token: "access", refresh_token: "refresh", expires_in: 3600});
});

test("accepts a valid refresh token form without Content-Length", async () => {
  const res = await response(request("grant_type=refresh_token&refresh_token=opaque"));
  assert.equal(res.status, 200);
});

test("rejects bodies over 8192 bytes even when Content-Length is absent", async () => {
  const res = await response(request("x".repeat(8193)));
  assert.equal(res.status, 413);
});

test("rejects malformed, oversized, and mismatched Content-Length", async () => {
  assert.equal((await response(request("a", {length: "1x"}))).status, 400);
  assert.equal((await response(request("a", {length: 8193}))).status, 413);
  assert.equal((await response(request("ab", {length: 1}))).status, 400);
});

test("rejects invalid grants, missing/extra/duplicate fields, and invalid PKCE", async () => {
  for (const body of [
    "grant_type=client_credentials",
    "grant_type=refresh_token",
    "grant_type=refresh_token&refresh_token=x&extra=y",
    "grant_type=refresh_token&refresh_token=x&refresh_token=y",
    "grant_type=authorization_code&code=x&code_verifier=short"
  ]) assert.equal((await response(request(body, {length: Buffer.byteLength(body)}))).status, 400, body);
});

test("does not expose upstream token or client secret on failure", async () => {
  const body = validParams.toString();
  const res = await response(request(body, {length: Buffer.byteLength(body)}), async () =>
    new Response(JSON.stringify({error: "bad", access_token: "leaked-token", client_secret: "secret-value"}), {status: 400}));
  const text = await res.text();
  assert.equal(res.status, 400);
  assert.doesNotMatch(text, /leaked-token|secret-value/);
});

const relayEnv = {...env, API_LIMIT: {limit: async () => ({success: true})}};
async function relay(req, upstream, bindings = relayEnv) {
  const previousFetch = globalThis.fetch;
  globalThis.fetch = upstream;
  try { return await worker.fetch(req, bindings); }
  finally { globalThis.fetch = previousFetch; }
}
function apiRequest(path = "/api/me", init = {}) {
  return new Request("https://worker.test" + path, {headers: {Authorization: "OAuth test-token", ...init.headers}, ...init});
}
test("API relay uses a fixed origin, passes OAuth and prevents caching", async () => {
  const res = await relay(apiRequest("/api/tracks?limit=2"), async (url, options) => {
    assert.equal(String(url), "https://api.soundcloud.com/tracks?limit=2");
    assert.equal(options.headers.get("Authorization"), "OAuth test-token");
    assert.equal(options.redirect, "manual");
    return Response.json({collection: []}, {headers: {"Set-Cookie": "private=x"}});
  });
  assert.equal(res.status, 200);
  assert.equal(res.headers.get("Cache-Control"), "no-store");
  assert.equal(res.headers.get("Set-Cookie"), null);
});
test("API relay rejects unauthenticated, unsafe and rate limited traffic before fetching", async () => {
  let calls = 0;
  const upstream = async () => { calls++; return Response.json({}); };
  assert.equal((await relay(new Request("https://worker.test/api/me"), upstream)).status, 401);
  assert.equal((await relay(apiRequest("/api//evil.example/me"), upstream)).status, 400);
  assert.equal((await relay(apiRequest("/api/%2fexample.org"), upstream)).status, 400);
  assert.equal((await relay(apiRequest(), upstream, {...relayEnv, API_LIMIT: {limit: async () => ({success: false})}})).status, 429);
  assert.equal(calls, 0);
});
test("API relay rejects external redirects and non-JSON error pages", async () => {
  assert.equal((await relay(apiRequest(), async () => new Response(null, {status: 302, headers: {Location: "https://attacker.example/"}}))).status, 502);
  const denied = await relay(apiRequest(), async () => new Response("private upstream page", {status: 403}));
  assert.equal(denied.status, 502);
  assert.doesNotMatch(await denied.text(), /private upstream/);
});
test("API relay preserves same-origin resolve redirects, limits and no-content responses", async () => {
  const redirected = await relay(apiRequest("/api/resolve?url=x"), async () => new Response(null, {status: 302, headers: {Location: "https://api.soundcloud.com/tracks/1"}}));
  assert.equal(redirected.headers.get("Location"), "https://api.soundcloud.com/tracks/1");
  const limited = await relay(apiRequest(), async () => Response.json({error: "limit"}, {status: 429, headers: {"Retry-After": "60"}}));
  assert.equal(limited.status, 429); assert.equal(limited.headers.get("Retry-After"), "60");
  assert.equal((await relay(apiRequest(), async () => new Response(null, {status: 204}))).status, 204);
});
test("API relay supports empty writes and bounded JSON bodies without forwarding cookies", async () => {
  const empty = apiRequest("/api/me/likes/tracks/1", {method: "PUT", headers: {Authorization: "OAuth test-token", "Content-Type": "application/json", "Content-Length": "0"}});
  assert.equal((await relay(empty, async (_, options) => { assert.equal(options.body, undefined); return new Response(null, {status: 204}); })).status, 204);
  const body = JSON.stringify({title: "A playlist"});
  const res = await relay(apiRequest("/api/playlists", {method: "POST", headers: {Authorization: "OAuth test-token", "Content-Type": "application/json", Cookie: "private=x"}, body}), async (_, options) => {
    assert.equal(options.headers.get("Cookie"), null); assert.equal(options.body, body); return Response.json({});
  });
  assert.equal(res.status, 200);
});

const mediaEnv = {...relayEnv, MEDIA_LIMIT: {limit: async () => ({success: true})}};
async function capability() {
  const res = await relay(apiRequest("/api/tracks/1/streams"), async () => Response.json({hls_aac_160_url: "https://cf-hls-media.sndcdn.com/a/playlist.m3u8?Policy=private"}), mediaEnv);
  assert.equal(res.status, 200);
  return (await res.json()).hls_aac_160_url;
}
test("media relay requires unexpired, untampered capabilities for SoundCloud CDN only", async () => {
  let calls = 0;
  const upstream = async () => {calls++;return new Response("audio");};
  assert.equal((await relay(new Request("https://worker.test/media?url=https://127.0.0.1/"),upstream,mediaEnv)).status,403);
  const signed = new URL(await capability());
  signed.searchParams.set("url","https://example.org/audio");
  assert.equal((await relay(new Request(signed),upstream,mediaEnv)).status,403);
  signed.searchParams.set("expires","1");
  assert.equal((await relay(new Request(signed),upstream,mediaEnv)).status,403);
  assert.equal(calls,0);
});
test("media relay rewrites HLS children and map/key URIs and preserves binary ranges", async () => {
  const signed = await capability();
  const playlist = await relay(new Request(signed), async (_, options) => {
    assert.equal(options.headers.has("Authorization"),false);
    return new Response('#EXTM3U\n#EXT-X-MAP:URI="init.mp4"\n#EXTINF:4,\npart.m4s\n', {headers:{"Content-Type":"application/vnd.apple.mpegurl"}});
  },mediaEnv);
  assert.equal(playlist.status,200);
  const text=await playlist.text();
  assert.match(text,/#EXT-X-MAP:URI="https:\/\/worker.test\/media\//);
  const child=text.split('\n').find(line=>line.startsWith('https://'));
  const range=await relay(new Request(child,{headers:{Range:"bytes=4-7",Authorization:"OAuth must-not-forward",Cookie:"private=x"}}),async (_,options)=>{
    assert.equal(options.headers.get('Range'),'bytes=4-7');
    assert.equal(options.headers.has('Authorization'),false);
    assert.equal(options.headers.has('Cookie'),false);
    return new Response(new Uint8Array([1,2,3,4]),{status:206,headers:{'Content-Type':'audio/mp4','Content-Range':'bytes 4-7/20','Content-Length':'4'}});
  },mediaEnv);
  assert.equal(range.status,206);assert.equal(range.headers.get('Content-Range'),'bytes 4-7/20');
  assert.equal((await range.arrayBuffer()).byteLength,4);
});
test("media relay refuses CDN redirects to arbitrary hosts", async()=>{
  const signed=await capability();
  const res=await relay(new Request(signed),async()=>new Response(null,{status:302,headers:{Location:'https://attacker.example/private'}}),mediaEnv);
  assert.equal(res.status,502);
});
test("stream resolution preserves same-origin 307 and signs CDN 307 destinations",async()=>{
  const canonical=await relay(apiRequest('/api/media/track/hls'),async()=>new Response(null,{status:307,headers:{Location:'https://api.soundcloud.com/media/track/redirected'}}),mediaEnv);
  assert.equal(canonical.status,307);
  const cdn=await relay(apiRequest('/api/media/track/hls'),async()=>new Response(null,{status:307,headers:{Location:'https://playback.media-streaming.soundcloud.cloud/playlist.m3u8'}}),mediaEnv);
  assert.equal(cdn.status,200);
  assert.match((await cdn.json()).url,/^https:\/\/worker.test\/media\//);
});

test("artwork relay uses only image CDN paths and strips caller credentials", async () => {
  const res = await relay(new Request('https://worker.test/artwork/i1.sndcdn.com/artworks-test-t500x500.jpg', {
    headers: {Authorization: 'OAuth private', Cookie: 'private=x'}
  }), async (url, options) => {
    assert.equal(String(url), 'https://i1.sndcdn.com/artworks-test-t500x500.jpg');
    assert.equal(options.headers, undefined);
    assert.equal(options.redirect, 'manual');
    return new Response(new Uint8Array([1,2,3]), {headers: {'Content-Type':'image/jpeg'}});
  }, mediaEnv);
  assert.equal(res.status, 200);
  assert.equal(res.headers.get('Content-Type'), 'image/jpeg');
  for (const path of ['/artwork/attacker.example/a.jpg', '/artwork/i1.sndcdn.com/a.svg', '/artwork/i1.sndcdn.com/a.jpg?url=x']) {
    const rejected = await relay(new Request('https://worker.test' + path), () => { throw new Error('must not fetch'); }, mediaEnv);
    assert.equal(rejected.status, 400);
  }
  const redirected = await relay(new Request('https://worker.test/artwork/i1.sndcdn.com/a.jpg'), async () =>
    new Response(null, {status:302, headers:{Location:'https://attacker.example/a.jpg'}}), mediaEnv);
  assert.equal(redirected.status, 502);
});
