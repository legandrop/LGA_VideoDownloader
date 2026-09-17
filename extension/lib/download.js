// Un pedido de descarga completo: preferencia de sesion, cookies, envio y badge.
// Corre en el service worker, asi el pedido no se pierde si el popup se cierra.

import { STORAGE_KEYS } from './config.js';
import { collectCookies } from './cookies.js';
import { buildDownload, isDownloadableUrl, sendNative } from './protocol.js';

export async function readUseSession() {
  try {
    const data = await chrome.storage.local.get(STORAGE_KEYS.useSession);
    // Encendido por defecto.
    return data[STORAGE_KEYS.useSession] !== false;
  } catch {
    return true;
  }
}

async function flashBadge(ok) {
  try {
    await chrome.action.setBadgeBackgroundColor({ color: ok ? '#4c6b33' : '#a8463a' });
    await chrome.action.setBadgeText({ text: ok ? '✓' : '!' });
    setTimeout(() => {
      chrome.action.setBadgeText({ text: '' }).catch(() => {});
    }, ok ? 2000 : 5000);
  } catch {
    // El badge es informativo: si falla no afecta al pedido.
  }
}

export async function performDownload({ url, title }) {
  if (!isDownloadableUrl(url)) return { kind: 'unsupported_page' };
  const useSession = await readUseSession();
  let cookies = null;
  if (useSession) {
    try {
      cookies = await collectCookies(url);
    } catch {
      cookies = [];
    }
  }
  const built = buildDownload({ url, title, cookies });
  if (!built.ok) {
    await flashBadge(false);
    return { kind: built.kind };
  }
  const result = await sendNative(built.message, 'queued');
  await flashBadge(result.kind === 'queued');
  return result;
}
