'use strict';
// Service Worker — gestisce le notifiche push Chrome

self.addEventListener('push', (event) => {
  let data = {};
  try {
    data = event.data ? event.data.json() : {};
  } catch {
    data = { title: 'IbleoCare', body: event.data ? event.data.text() : '' };
  }

  const options = {
    body:    data.body  || 'Notifica dalla pianta',
    icon:    data.icon  || '/icon.svg',
    badge:   '/icon.svg',
    tag:     data.tag   || 'ibleocare',
    renotify: true,
    data:    { url: '/' },
  };

  event.waitUntil(
    self.registration.showNotification(data.title || 'IbleoCare', options)
  );
});

self.addEventListener('notificationclick', (event) => {
  event.notification.close();
  event.waitUntil(
    clients.matchAll({ type: 'window', includeUncontrolled: true }).then((list) => {
      for (const client of list) {
        if ('focus' in client) return client.focus();
      }
      return clients.openWindow(event.notification.data.url || '/');
    })
  );
});
