// Service worker: menu contextual y ejecucion de pedidos que inicia el popup.
// Sin content scripts, sin onMessageExternal, sin externally_connectable.

import { STORAGE_KEYS } from './lib/config.js';
import { performDownload } from './lib/download.js';
import { isDownloadableUrl } from './lib/protocol.js';

const MENU_ID = 'lga-download';
const WEB_PATTERNS = ['http://*/*', 'https://*/*'];

chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.removeAll(() => {
    chrome.contextMenus.create({
      id: MENU_ID,
      title: 'Download with LGA Video Downloader',
      contexts: ['link', 'page'],
      documentUrlPatterns: WEB_PATTERNS,
      targetUrlPatterns: WEB_PATTERNS,
    });
  });
});

chrome.contextMenus.onClicked.addListener((info, tab) => {
  if (info.menuItemId !== MENU_ID) return;
  const url = info.linkUrl || info.pageUrl;
  const title = info.linkUrl ? '' : tab?.title || '';
  if (!isDownloadableUrl(url)) return;
  performDownload({ url, title }).then(async (result) => {
    // El detalle de un fallo queda para la proxima apertura del popup.
    const value = result.kind === 'queued' ? null : { kind: result.kind, message: result.message || '', at: Date.now() };
    try {
      if (value) await chrome.storage.local.set({ [STORAGE_KEYS.lastMenuResult]: value });
      else await chrome.storage.local.remove(STORAGE_KEYS.lastMenuResult);
    } catch {
      // Sin storage no hay detalle diferido; el badge ya informo.
    }
  });
});

// Pedido del popup. Solo se aceptan mensajes de la propia pagina del popup.
chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  const fromPopup = sender.id === chrome.runtime.id && typeof sender.url === 'string'
    && sender.url.startsWith(chrome.runtime.getURL('popup.html'));
  if (!fromPopup || !msg || msg.type !== 'lga-download') return false;
  const url = typeof msg.url === 'string' ? msg.url : '';
  const title = typeof msg.title === 'string' ? msg.title : '';
  performDownload({ url, title }).then(sendResponse, () => sendResponse({ kind: 'internal' }));
  return true;
});
