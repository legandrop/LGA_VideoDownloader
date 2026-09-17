// Protocolo v1 con el host nativo: armado y validacion de mensajes, envio con timeout y
// clasificacion de fallos. No toca la UI: devuelve siempre un resultado { kind, ... } que
// el popup y el service worker traducen a texto o badge.

import { HOST_NAME, PROTOCOL_VERSION, LIMITS, NATIVE_TIMEOUT_MS } from './config.js';

export function isDownloadableUrl(url) {
  if (typeof url !== 'string' || url.length === 0 || url.length > LIMITS.urlChars) return false;
  try {
    const u = new URL(url);
    return (u.protocol === 'http:' || u.protocol === 'https:') && u.hostname.length > 0;
  } catch {
    return false;
  }
}

// Solo texto para el log de la app; ninguna logica depende de esto.
export function detectBrowser() {
  if (navigator.brave) return 'brave';
  const brands = navigator.userAgentData?.brands?.map((b) => b.brand) ?? [];
  if (brands.includes('Microsoft Edge')) return 'edge';
  return 'chrome';
}

export function buildPing() {
  return { v: PROTOCOL_VERSION, type: 'ping' };
}

// Devuelve { ok:true, message } o { ok:false, kind }. cookies === null -> sin sesion.
export function buildDownload({ url, title, cookies }) {
  if (!isDownloadableUrl(url)) return { ok: false, kind: 'unsupported_page' };
  const message = {
    v: PROTOCOL_VERSION,
    type: 'download',
    url,
    title: typeof title === 'string' ? title.slice(0, LIMITS.titleChars) : '',
    browser: detectBrowser().slice(0, LIMITS.browserChars),
  };
  if (Array.isArray(cookies)) message.cookies = cookies.slice(0, LIMITS.cookies);
  const bytes = new TextEncoder().encode(JSON.stringify(message)).length;
  if (bytes > LIMITS.messageBytes) return { ok: false, kind: 'too_large' };
  return { ok: true, message };
}

// Mensajes de chrome.runtime.lastError que devuelve Chromium para Native Messaging.
function classifyTransportError(text) {
  const t = String(text || '');
  if (t.includes('host not found')) return 'not_installed';
  if (t.includes('Failed to start native messaging host')) return 'cannot_start';
  if (t.includes('forbidden')) return 'forbidden';
  return 'transport';
}

const KNOWN_ERRORS = new Set([
  'bad_request',
  'unsupported_version',
  'app_start_failed',
  'app_timeout',
  'internal',
]);

function cleanText(value, max) {
  return typeof value === 'string' ? value.slice(0, max) : '';
}

// Valida la respuesta del host. Todo lo que no cumple el contrato es 'bad_response'.
export function parseResponse(resp, expectedStatus) {
  if (!resp || typeof resp !== 'object' || Array.isArray(resp)) return { kind: 'bad_response' };
  if (resp.v !== PROTOCOL_VERSION) {
    return typeof resp.v === 'number' && resp.v > PROTOCOL_VERSION
      ? { kind: 'unsupported_version' }
      : { kind: 'bad_response' };
  }
  const info = {
    app: cleanText(resp.app, 32),
    extension: cleanText(resp.extension, 32),
  };
  if (resp.ok === true) {
    if (resp.status !== expectedStatus) return { kind: 'bad_response' };
    return {
      kind: expectedStatus,
      running: resp.running === true,
      launched: resp.launched === true,
      ...info,
    };
  }
  if (resp.ok === false) {
    const error = KNOWN_ERRORS.has(resp.error) ? resp.error : 'internal';
    return { kind: error, message: cleanText(resp.message, LIMITS.responseMessageChars), ...info };
  }
  return { kind: 'bad_response' };
}

export async function sendNative(message, expectedStatus) {
  let timer;
  const timeout = new Promise((resolve) => {
    timer = setTimeout(() => resolve({ timedOut: true }), NATIVE_TIMEOUT_MS);
  });
  try {
    const resp = await Promise.race([chrome.runtime.sendNativeMessage(HOST_NAME, message), timeout]);
    if (resp && resp.timedOut === true) return { kind: 'app_timeout' };
    return parseResponse(resp, expectedStatus);
  } catch (err) {
    return { kind: classifyTransportError(err?.message) };
  } finally {
    clearTimeout(timer);
  }
}
