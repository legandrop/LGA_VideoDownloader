// Lectura de cookies para un pedido de descarga.
// Solo se llama desde performDownload(), que a su vez solo corre por un clic del usuario
// (boton Download del popup o item del menu contextual). Nunca al abrir el popup.

import { LIMITS } from './config.js';

// Segundo nivel generico de ccTLD: example.co.uk, example.com.ar, etc.
const SECOND_LEVEL = new Set(['co', 'com', 'net', 'org', 'gov', 'edu', 'ac']);

// Alias de dominios que comparten sesion.
const ALIASES = Object.freeze({
  'youtu.be': ['youtube.com'],
  'youtube-nocookie.com': ['youtube.com'],
  'x.com': ['x.com', 'twitter.com'],
  'twitter.com': ['twitter.com', 'x.com'],
});

function isIpAddress(host) {
  return /^\d{1,3}(\.\d{1,3}){3}$/.test(host) || host.includes(':');
}

// Dominio registrable por heuristica (sin Public Suffix List, a proposito: sin dependencias).
export function registrableDomain(hostname) {
  const host = String(hostname || '').toLowerCase().replace(/\.$/, '');
  if (!host || isIpAddress(host)) return host;
  const labels = host.split('.');
  if (labels.length <= 2) return host;
  const tld = labels[labels.length - 1];
  const second = labels[labels.length - 2];
  const take = tld.length === 2 && SECOND_LEVEL.has(second) ? 3 : 2;
  return labels.slice(-take).join('.');
}

export function cookieDomainsFor(url) {
  const base = registrableDomain(new URL(url).hostname);
  if (!base) return [];
  return ALIASES[base] ? [...ALIASES[base]] : [base];
}

function hasControlChars(value) {
  return typeof value !== 'string' || /[\t\r\n]/.test(value);
}

// Devuelve la lista estructurada del protocolo v1. Sin partitionKey: solo cookies no
// particionadas (las de primera parte), que es lo que cookies.txt puede representar.
export async function collectCookies(url) {
  const seen = new Set();
  const out = [];
  for (const domain of cookieDomainsFor(url)) {
    const list = await chrome.cookies.getAll({ domain });
    for (const c of list) {
      if (out.length >= LIMITS.cookies) return out;
      if (!c.domain || [c.name, c.value, c.domain, c.path].some(hasControlChars)) continue;
      const id = JSON.stringify([c.domain, c.path, c.name]);
      if (seen.has(id)) continue;
      seen.add(id);
      const cookie = {
        domain: c.domain,
        hostOnly: Boolean(c.hostOnly),
        path: c.path,
        secure: Boolean(c.secure),
        httpOnly: Boolean(c.httpOnly),
        name: c.name,
        value: c.value,
      };
      // Ausente = cookie de sesion.
      if (!c.session && Number.isFinite(c.expirationDate)) {
        cookie.expirationDate = c.expirationDate;
      }
      out.push(cookie);
    }
  }
  return out;
}
