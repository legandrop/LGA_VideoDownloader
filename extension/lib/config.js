// Constantes del protocolo v1 entre la extension y LGA Video Downloader.
// Es el UNICO lugar donde vive el nombre del host nativo: si cambia, cambia aca y en el
// JSON del host que instala la app (com.lga.videodownloader.json, campo "name").

export const HOST_NAME = 'com.lga.videodownloader';
export const PROTOCOL_VERSION = 1;

// Limites del contrato (los mismos que valida el host del lado de la app).
export const LIMITS = Object.freeze({
  messageBytes: 8 * 1024 * 1024,
  urlChars: 4096,
  titleChars: 512,
  browserChars: 32,
  cookies: 3000,
  // Respuesta del host: el navegador corta en 1 MB; la real ocupa < 1 KB.
  responseMessageChars: 300,
});

// El host termina siempre en <= 25 s (arranque de la app incluido). Margen propio por si
// el proceso queda colgado y el navegador no corta.
export const NATIVE_TIMEOUT_MS = 30000;

// Claves de chrome.storage.local. Nunca se guardan cookies.
export const STORAGE_KEYS = Object.freeze({
  useSession: 'useSession',
  reloadAttempt: 'reloadAttempt',
  lastMenuResult: 'lastMenuResult',
});
