// Popup: ping + control de version primero, despues la pestana y el boton Download.
// Todo texto dinamico entra con textContent (nunca innerHTML): el titulo de la pagina
// lo controla un sitio cualquiera.

import { STORAGE_KEYS } from './lib/config.js';
import { buildPing, isDownloadableUrl, sendNative } from './lib/protocol.js';

const $ = (id) => document.getElementById(id);
const root = document.querySelector('.popup');
const button = $('downloadButton');
const toggle = $('useSession');

const COPY = Object.freeze({
  not_installed: { tone: 'err', blocking: true, title: 'LGA Video Downloader is not installed', body: 'Install the app, then restart the browser.' },
  cannot_start: { tone: 'err', blocking: true, title: "Can't start LGA Video Downloader", body: 'Reinstall the app, then try again.' },
  forbidden: { tone: 'err', blocking: true, title: "The app doesn't accept this extension", body: 'Load the extension folder that comes with the app.' },
  unsupported_version: { tone: 'err', blocking: true, title: 'Update LGA Video Downloader', body: 'This extension needs a newer version of the app.' },
  app_timeout: { tone: 'err', title: "The app didn't answer", body: 'Open it and try again.' },
  app_start_failed: { tone: 'err', title: "Can't open LGA Video Downloader", body: 'If the app is updating, wait a moment and try again.' },
  bad_request: { tone: 'err', title: 'The app rejected this link', body: 'Try again from the video page.' },
  internal: { tone: 'err', title: "Couldn't add the download", body: 'Try again.' },
  too_large: { tone: 'err', title: 'Too many cookies for this site', body: 'Turn off Use my session and try again.' },
  bad_response: { tone: 'err', title: "Couldn't talk to the app", body: 'Try again. If it keeps failing, reinstall the app.' },
  transport: { tone: 'err', title: "Couldn't talk to the app", body: 'Try again. If it keeps failing, reinstall the app.' },
  unsupported_page: { tone: 'neutral', blocking: true, title: 'Open a video page to download it.', body: '' },
});

const MENU_RESULT_MAX_AGE_MS = 10 * 60 * 1000;

let tab = { url: '', title: '' };
let appRunning = null;
let blocked = false;

function setNotice(tone, title, body = '') {
  const notice = $('notice');
  if (!tone) {
    notice.hidden = true;
    return;
  }
  notice.dataset.tone = tone;
  $('noticeTitle').textContent = title;
  $('noticeBody').textContent = body;
  notice.hidden = false;
}

function showResult(kind, message = '') {
  const copy = COPY[kind] || COPY.internal;
  // El mensaje del host solo reemplaza al texto generico, nunca a los de instalacion.
  const body = message && (kind === 'internal' || kind === 'bad_request') ? message : copy.body;
  setNotice(copy.tone, copy.title, body);
  if (copy.blocking) blocked = true;
  root.dataset.state = kind === 'unsupported_page' ? 'unsupported' : 'error';
}

function setButton(label, { enabled, tone = '' }) {
  button.textContent = label;
  button.disabled = !enabled;
  if (tone) button.dataset.tone = tone;
  else delete button.dataset.tone;
}

function readyButton() {
  setButton('Download', { enabled: !blocked });
}

async function storageGet(key) {
  try {
    return (await chrome.storage.local.get(key))[key];
  } catch {
    return undefined;
  }
}

async function storageSet(key, value) {
  try {
    if (value === undefined) await chrome.storage.local.remove(key);
    else await chrome.storage.local.set({ [key]: value });
  } catch {
    // Preferencias de conveniencia: sin storage se usan los valores por defecto.
  }
}

// Devuelve true si la extension se va a recargar (el popup deja de importar).
async function checkVersion(pong) {
  const loaded = chrome.runtime.getManifest().version;
  const disk = pong.extension;
  if (!disk || disk === loaded) {
    await storageSet(STORAGE_KEYS.reloadAttempt, undefined);
    return false;
  }
  const previous = await storageGet(STORAGE_KEYS.reloadAttempt);
  if (previous && previous.loaded === loaded && previous.disk === disk) {
    // Ya se intento recargar y la version no cambio: la carpeta cargada no es la de la app.
    setNotice('warn', 'Extension out of date', `Version ${loaded} is loaded, the app ships ${disk}. Click Reload on the extension card.`);
    return false;
  }
  await storageSet(STORAGE_KEYS.reloadAttempt, { loaded, disk });
  root.dataset.state = 'reloading';
  setButton('Download', { enabled: false });
  setNotice('warn', 'Extension updated', 'Click the toolbar icon again.');
  setTimeout(() => chrome.runtime.reload(), 900);
  return true;
}

async function showPendingMenuFailure() {
  const last = await storageGet(STORAGE_KEYS.lastMenuResult);
  if (!last) return;
  await storageSet(STORAGE_KEYS.lastMenuResult, undefined);
  if (typeof last.at !== 'number' || Date.now() - last.at > MENU_RESULT_MAX_AGE_MS) return;
  const copy = COPY[last.kind] || COPY.internal;
  setNotice('err', 'Last right-click download failed', copy.title);
}

async function loadTab() {
  try {
    const [active] = await chrome.tabs.query({ active: true, currentWindow: true });
    tab = { url: active?.url || '', title: active?.title || '' };
  } catch {
    tab = { url: '', title: '' };
  }
  let host = '';
  if (isDownloadableUrl(tab.url)) host = new URL(tab.url).hostname.replace(/^www\./, '');
  $('pageTitle').textContent = tab.title || host || 'Untitled page';
  $('pageHost').textContent = host || tab.url.slice(0, 200);
  return Boolean(host);
}

async function onDownload() {
  if (blocked || button.disabled) return;
  root.dataset.state = 'sending';
  setButton('Sending…', { enabled: false });
  toggle.disabled = true;
  setNotice('run', 'Sending to the app…');
  const slow = setTimeout(() => {
    if (appRunning === false) setNotice('run', 'Opening LGA Video Downloader…');
  }, 1200);

  let result;
  try {
    result = await chrome.runtime.sendMessage({ type: 'lga-download', url: tab.url, title: tab.title });
  } catch {
    result = { kind: 'transport' };
  }
  clearTimeout(slow);
  toggle.disabled = false;

  if (result && result.kind === 'queued') {
    root.dataset.state = 'queued';
    setNotice(null);
    setButton('Added to queue', { enabled: false, tone: 'ok' });
    setTimeout(() => window.close(), 1200);
    return;
  }
  showResult(result?.kind, result?.message);
  readyButton();
}

async function init() {
  // Primero el ping: si la extension en disco es otra version, se recarga antes de nada.
  const pingPromise = sendNative(buildPing(), 'pong');
  const [supported, useSession] = await Promise.all([loadTab(), storageGet(STORAGE_KEYS.useSession)]);
  toggle.checked = useSession !== false;
  toggle.addEventListener('change', () => storageSet(STORAGE_KEYS.useSession, toggle.checked));
  button.addEventListener('click', onDownload);

  const ping = await pingPromise;
  if (ping.kind === 'pong') {
    appRunning = ping.running;
    if (await checkVersion(ping)) return;
  } else if (COPY[ping.kind]?.blocking) {
    showResult(ping.kind);
    readyButton();
    return;
  }

  if (!supported) {
    showResult('unsupported_page');
    readyButton();
    return;
  }
  root.dataset.state = 'idle';
  readyButton();
  if (ping.kind !== 'pong') showResult(ping.kind, ping.message);
  else await showPendingMenuFailure();
}

init();
