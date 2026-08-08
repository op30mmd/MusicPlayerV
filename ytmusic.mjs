// MusicPlayer - scrape the user's YouTube Music homepage (or any YT Music playlist)
// for browse mode. Uses the InnerTube browse API (same as the YT Music web frontend).
// Usage: node ytmusic.mjs <cookies.txt (Netscape format)> [playlist URL]
// Prints: id\ttitle\turl\t<video|playlist>  (one entry per line)

import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';

const cookiePath = process.argv[2];
const playlistUrl = process.argv[3];
if (!cookiePath) {
    console.error('ERROR: missing cookies file argument');
    process.exit(1);
}

const UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36';

const now = Math.floor(Date.now() / 1000);
const cookies = [];
const raw = readFileSync(cookiePath, 'utf8').replace(/\r/g, '');
for (const line of raw.split('\n')) {
    const p = line.split('\t');
    if (p.length < 7 || line.startsWith('#')) continue;
    const [domain, , , , expiry, name, value] = p;
    if (!domain.includes('youtube.com')) continue;
    if (expiry !== '0' && +expiry < now) continue;
    cookies.push({ name, value });
}
if (cookies.length === 0) {
    console.error('ERROR: no usable YouTube cookies (session expired?)');
    process.exit(1);
}
const cookieHeader = cookies.map(c => `${c.name}=${c.value}`).join('; ');

let client = { clientName: 'WEB_REMIX', clientVersion: '1.20260804.16.00' };
try {
    const shell = await fetch('https://music.youtube.com/', {
        headers: { 'Cookie': cookieHeader, 'User-Agent': UA },
    });
    const page = await shell.text();
    const m = page.match(/"INNERTUBE_CONTEXT":(\{.*?\}),"INNERTUBE_/s);
    if (m) {
        const ctx = JSON.parse(m[1]);
        if (ctx.client) client = ctx.client;
    }
} catch { /* use defaults */ }

const sapisid = cookies.find(c => c.name === 'SAPISID' || c.name === '__Secure-1PAPISID');
const origin = 'https://music.youtube.com';
const headers = {
    'Cookie': cookieHeader,
    'User-Agent': UA,
    'Content-Type': 'application/json',
    'Origin': origin,
    'X-Origin': origin,
    'X-Goog-AuthUser': '0',
};
if (sapisid) {
    const ts = Math.floor(Date.now() / 1000);
    const hash = createHash('sha1').update(`${ts} ${sapisid.value} ${origin}`).digest('hex');
    headers['Authorization'] = `SAPISIDHASH ${ts}_${hash}`;
}

let browseId = 'FEmusic_home';
if (playlistUrl) {
    const m = playlistUrl.match(/[?&]list=([A-Za-z0-9_-]+)/);
    if (!m) {
        console.error('ERROR: could not find list= id in playlist URL');
        process.exit(1);
    }
    let id = m[1];
    if (!id.startsWith('VL') && id.startsWith('PL')) id = 'VL' + id;
    browseId = id;
}

// The home feed only serves a rotating subset of shelves per request;
// fetch several times and merge (dedupe by id) to recover the full home,
// plus the liked-playlists endpoint so the user's own playlists always
// show up. Playlist drill-downs need a single response.
const browseIds = playlistUrl
    ? [browseId]
    : [browseId, browseId, browseId, browseId, 'FEmusic_liked_playlists'];
const results = await Promise.all(browseIds.map(id =>
    fetch('https://music.youtube.com/youtubei/v1/browse?prettyPrint=false', {
        method: 'POST',
        headers,
        body: JSON.stringify({ context: { client }, browseId: id }),
    }).then(r => r.ok ? r.json() : null).catch(() => null),
));
const datasets = results.filter(Boolean);
if (datasets.length === 0) {
    console.error('ERROR: browse API failed: HTTP error');
    process.exit(1);
}

const seen = new Set();
const out = [];

const walk = (o) => {
    if (!o || typeof o !== 'object') return;
    if (Array.isArray(o)) { for (const x of o) walk(x); return; }

    if (o.musicResponsiveListItemRenderer) {
        const r = o.musicResponsiveListItemRenderer;
        const run = r.flexColumns?.[0]?.musicResponsiveListItemFlexColumnRenderer?.text?.runs?.[0];
        if (run && run.text) {
            const vid = run.navigationEndpoint?.watchEndpoint?.videoId;
            const bid = run.navigationEndpoint?.browseEndpoint?.browseId;
            const id = vid || (bid && (bid.startsWith('PL') || bid.startsWith('OLAK') || bid.startsWith('RD') || bid.startsWith('VL')) ? bid : null);
            if (id && !seen.has(id)) {
                seen.add(id);
                const url = vid
                    ? `https://www.youtube.com/watch?v=${vid}`
                    : `https://music.youtube.com/playlist?list=${bid}`;
                out.push(`${id}\t${run.text}\t${url}\t${vid ? 'video' : 'playlist'}`);
            }
        }
        return;
    }

    if (o.title) {
        let videoId = null;
        let playlistId = null;
        if (o.videoId) videoId = o.videoId;
        if (o.playlistId) playlistId = o.playlistId;
        const nav = o.navigationEndpoint || {};
        if (nav.watchEndpoint?.videoId) videoId = nav.watchEndpoint.videoId;
        const bid = nav.browseEndpoint?.browseId;
        if (bid && (bid.startsWith('PL') || bid.startsWith('OLAK') || bid.startsWith('RD') || bid.startsWith('VL'))) playlistId = bid;
        const id = videoId || playlistId;
        if (id && !seen.has(id)) {
            seen.add(id);
            const t = o.title;
            const title = Array.isArray(t.runs) ? t.runs.map(r => r.text).join('') : (typeof t === 'string' ? t : '');
            const url = videoId
                ? `https://www.youtube.com/watch?v=${videoId}`
                : `https://music.youtube.com/playlist?list=${playlistId}`;
            out.push(`${id}\t${title}\t${url}\t${videoId ? 'video' : 'playlist'}`);
        }
    }
    for (const k of Object.keys(o)) walk(o[k]);
};
for (const data of datasets) walk(data);

const gf = datasets[0]?.responseContext?.serviceTrackingParams?.find(s => s.service === 'GFEEDBACK');
const loggedIn = gf?.params?.find(p => p.key === 'logged_in')?.value;
console.log(`# logged_in=${loggedIn ?? '?'} entries=${out.length}`);

console.log(out.join('\n'));
