const CACHE_NAME = 'bmw-security-v2-online';
const urlsToCache = [
  './index.html',
  './manifest.json',
  // He quitado el icon.png de aquí para evitar errores si no existe localmente
  'https://cdn.tailwindcss.com',
  'https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css'
];

self.addEventListener('install', event => {
  event.waitUntil(
    caches.open(CACHE_NAME)
      .then(cache => {
        console.log('Cache abierta');
        // Intentamos cachear lo básico. Si las URLs externas fallan (CORS), 
        // el SW seguirá funcionando para el HTML local.
        return cache.addAll(urlsToCache).catch(err => console.log('Algunos archivos externos no se cachearon', err));
      })
  );
});

self.addEventListener('activate', event => {
  const cacheWhitelist = [CACHE_NAME];
  event.waitUntil(
    caches.keys().then(cacheNames => {
      return Promise.all(
        cacheNames.map(cacheName => {
          if (cacheWhitelist.indexOf(cacheName) === -1) {
            return caches.delete(cacheName);
          }
        })
      );
    })
  );
});

self.addEventListener('fetch', event => {
  event.respondWith(
    caches.match(event.request)
      .then(response => {
        return response || fetch(event.request);
      })
  );
});