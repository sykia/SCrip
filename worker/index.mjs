const redirect = "http://127.0.0.1:43821/callback";
function json(data, status = 200) {
  return Response.json(data, {status, headers: {"Cache-Control": "no-store"}});
}

async function readFormBody(request) {
  const header = request.headers.get("Content-Length");
  let declared;
  if (header !== null) {
    if (!/^\d+$/.test(header)) return {error: "length"};
    declared = Number(header);
    if (!Number.isSafeInteger(declared) || declared <= 0 || declared > 8192) return {error: "size"};
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
      if (total > 8192) {
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

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/health" && request.method === "GET")
      return json({ready: Boolean(env.CLIENT_ID && env.CLIENT_SECRET)}, env.CLIENT_ID && env.CLIENT_SECRET ? 200 : 503);
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
