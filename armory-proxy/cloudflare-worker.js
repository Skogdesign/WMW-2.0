/*
 * WoW Model Viewer - Armory import proxy (Cloudflare Worker)
 * --------------------------------------------------------------------------
 * Holds the Blizzard API credentials SERVER-SIDE so the desktop client never
 * ships a secret. It performs the OAuth client_credentials grant and the WoW
 * Profile "appearance" fetch, then returns Blizzard's JSON unchanged.
 *
 * The desktop client calls:
 *   https://<your-worker>/?region=<region>&realm=<realm-slug>&character=<name>
 *
 * Deploy: see README.md. Set BLIZZARD_CLIENT_ID and BLIZZARD_CLIENT_SECRET as
 * Worker *secrets* (never commit them). Set ACCESS_KEY to require a matching
 * &key= on every request, and add a Cloudflare Rate Limiting rule (both are
 * strongly recommended -- this single endpoint spends your one Blizzard quota).
 */

// Per-isolate token cache + single in-flight fetch (a cold-isolate burst then
// triggers ONE OAuth grant instead of one per request).
let cachedToken = null;        // { value, expiresAt (epoch ms) }
let inflightToken = null;      // Promise<string> | null

const REGION_LOCALE = { us: 'en_US', eu: 'en_GB', kr: 'ko_KR', tw: 'zh_TW' };
const UPSTREAM_TIMEOUT_MS = 12000;
const MAX_RESPONSE_BYTES = 512 * 1024; // appearance JSON is a few KB; this is a generous cap

function json(body, status) {
	return new Response(typeof body === 'string' ? body : JSON.stringify(body),
		{ status, headers: { 'Content-Type': 'application/json' } });
}

// Constant-time string compare (avoids leaking the key length / a byte-timing oracle).
function timingSafeEqual(a, b) {
	const enc = new TextEncoder();
	const ab = enc.encode(a), bb = enc.encode(b);
	let diff = ab.length ^ bb.length;
	const len = Math.max(ab.length, bb.length);
	for (let i = 0; i < len; i++)
		diff |= (ab[i] || 0) ^ (bb[i] || 0);
	return diff === 0;
}

// region may be "eu" (retail) or "classic-eu" / "classic1x-eu". Returns the
// Blizzard API host, the profile namespace, and a locale — or null if invalid.
function resolveRegion(region) {
	const m = /^(classic1x-|classic-)?(us|eu|kr|tw)$/.exec(region);
	if (!m)
		return null;

	const prefix = m[1] || '';
	const base = m[2];
	let nsKind = 'profile';
	if (prefix === 'classic-') nsKind = 'profile-classic';
	else if (prefix === 'classic1x-') nsKind = 'profile-classic1x';

	return {
		host: `${base}.api.blizzard.com`,
		namespace: `${nsKind}-${base}`,
		locale: REGION_LOCALE[base] || 'en_US',
	};
}

function fetchWithTimeout(url, opts) {
	const ctrl = new AbortController();
	const t = setTimeout(() => ctrl.abort(), UPSTREAM_TIMEOUT_MS);
	return fetch(url, { ...opts, signal: ctrl.signal }).finally(() => clearTimeout(t));
}

async function getToken(env) {
	const now = Date.now();
	if (cachedToken && cachedToken.expiresAt > now + 60000)
		return cachedToken.value;
	if (inflightToken)
		return inflightToken;

	inflightToken = (async () => {
		try {
			const basic = btoa(`${env.BLIZZARD_CLIENT_ID}:${env.BLIZZARD_CLIENT_SECRET}`);
			const res = await fetchWithTimeout('https://oauth.battle.net/token', {
				method: 'POST',
				headers: { 'Authorization': `Basic ${basic}`, 'Content-Type': 'application/x-www-form-urlencoded' },
				body: 'grant_type=client_credentials',
			});
			if (!res.ok)
				throw new Error(`token request failed: ${res.status}`);
			const data = await res.json();
			if (!data.access_token)
				throw new Error('token response missing access_token');
			const ttl = Math.min(Number(data.expires_in) || 3600, 86400);
			cachedToken = { value: data.access_token, expiresAt: now + ttl * 1000 };
			return cachedToken.value;
		} finally {
			inflightToken = null;
		}
	})();
	return inflightToken;
}

export default {
	async fetch(request, env) {
		if (!env.BLIZZARD_CLIENT_ID || !env.BLIZZARD_CLIENT_SECRET)
			return json({ error: 'proxy misconfigured: missing Blizzard credentials' }, 500);

		const url = new URL(request.url);

		// Optional shared-key gate (set the ACCESS_KEY secret to enable; recommended).
		if (env.ACCESS_KEY && !timingSafeEqual(url.searchParams.get('key') || '', env.ACCESS_KEY))
			return json({ error: 'unauthorized' }, 401);

		const region = (url.searchParams.get('region') || '').toLowerCase().trim();
		const realm = (url.searchParams.get('realm') || '').toLowerCase().trim();
		const character = (url.searchParams.get('character') || '').toLowerCase().trim();

		const r = resolveRegion(region);
		if (!r)
			return json({ error: 'invalid region' }, 400);
		// realm must be a slug; character: no slashes/whitespace, reasonable length.
		// (these values are interpolated into the upstream path -> guard against SSRF)
		if (!/^[a-z0-9-]{1,64}$/.test(realm))
			return json({ error: 'invalid realm slug' }, 400);
		if (character.length < 1 || character.length > 32 || /[\/\s?#]/.test(character))
			return json({ error: 'invalid character name' }, 400);

		try {
			const token = await getToken(env);
			const apiUrl = `https://${r.host}/profile/wow/character/${realm}/${encodeURIComponent(character)}/appearance`
				+ `?namespace=${r.namespace}&locale=${r.locale}`;

			const res = await fetchWithTimeout(apiUrl, { headers: { 'Authorization': `Bearer ${token}` } });

			const cl = Number(res.headers.get('content-length') || 0);
			if (cl > MAX_RESPONSE_BYTES)
				return json({ error: 'upstream response too large' }, 502);

			const body = await res.text();
			if (body.length > MAX_RESPONSE_BYTES)
				return json({ error: 'upstream response too large' }, 502);

			// Pass Blizzard's status + body through unchanged (e.g. 404 for a hidden/missing profile).
			return new Response(body, {
				status: res.status,
				headers: { 'Content-Type': 'application/json', 'Cache-Control': 'public, max-age=300' },
			});
		} catch (e) {
			// Log detail for the operator (wrangler tail); return a generic message to callers.
			console.log('armory proxy upstream error:', String(e && e.message || e));
			return json({ error: 'upstream error' }, 502);
		}
	},
};
