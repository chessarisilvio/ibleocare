'use strict';
require('dotenv').config();
const webpush = require('web-push');

// Configura VAPID solo se le chiavi sono presenti
if (process.env.VAPID_PUBLIC_KEY && process.env.VAPID_PRIVATE_KEY) {
  webpush.setVapidDetails(
    process.env.VAPID_MAILTO,
    process.env.VAPID_PUBLIC_KEY,
    process.env.VAPID_PRIVATE_KEY
  );
} else {
  console.warn('[Push] VAPID keys mancanti in .env — push notifications disabilitate');
}

module.exports = webpush;
