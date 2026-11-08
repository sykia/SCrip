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
