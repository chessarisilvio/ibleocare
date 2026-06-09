'use strict';
const express = require('express');
const router = express.Router();
const { pool } = require('../db');
const { refreshSoglie } = require('../mqtt');

// POST /api/push/subscribe
// Body: { nodo_id, subscription: { endpoint, keys: { p256dh, auth } } }
router.post('/subscribe', async (req, res) => {
  const { nodo_id, subscription } = req.body;
  if (!subscription || !subscription.endpoint) {
    return res.status(400).json({ error: 'subscription mancante' });
  }

  await pool.query(
    `INSERT INTO push_subscriptions (nodo_id, endpoint, subscription)
     VALUES ($1, $2, $3)
     ON CONFLICT (endpoint)
       DO UPDATE SET subscription=$3, nodo_id=$1`,
    [nodo_id || null, subscription.endpoint, JSON.stringify(subscription)]
  );

  res.json({ ok: true });
});

// DELETE /api/push/unsubscribe
// Body: { endpoint }
router.delete('/unsubscribe', async (req, res) => {
  const { endpoint } = req.body;
  if (!endpoint) return res.status(400).json({ error: 'endpoint mancante' });
  await pool.query('DELETE FROM push_subscriptions WHERE endpoint=$1', [endpoint]);
  // Aggiorna cache soglie
  refreshSoglie().catch(console.error);
  res.json({ ok: true });
});

module.exports = router;
