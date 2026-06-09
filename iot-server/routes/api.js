'use strict';
const express = require('express');
const router = express.Router();
const { pool } = require('../db');

// GET /api/nodi — lista tutti i nodi con ultimo stato live
router.get('/nodi', async (req, res) => {
  const result = await pool.query(
    `SELECT id, nome, tipo_pianta, soglia_allarme,
            ultimo_aggiornamento, ultimo_temp, ultimo_hum, ultimo_soil
     FROM nodi
     ORDER BY id`
  );
  res.json(result.rows);
});

// GET /api/nodo/:id/live — ultimo dato di un singolo nodo
router.get('/nodo/:id/live', async (req, res) => {
  const result = await pool.query(
    `SELECT ultimo_aggiornamento AS ts, ultimo_temp AS temp,
            ultimo_hum AS hum, ultimo_soil AS soil
     FROM nodi WHERE id = $1`,
    [req.params.id]
  );
  if (!result.rows.length) return res.status(404).json({ error: 'Nodo non trovato' });
  res.json(result.rows[0]);
});

// GET /api/nodo/:id/raw?n=60 — ultime N letture grezze (default 60)
router.get('/nodo/:id/raw', async (req, res) => {
  const n = Math.min(parseInt(req.query.n) || 60, 1000);
  const result = await pool.query(
    `SELECT ts, temp, hum, soil
     FROM letture_raw
     WHERE nodo_id = $1
     ORDER BY ts DESC
     LIMIT $2`,
    [req.params.id, n]
  );
  res.json(result.rows.reverse());
});

// GET /api/nodo/:id/orarie?n=24 — ultime N medie orarie (default 24)
router.get('/nodo/:id/orarie', async (req, res) => {
  const n = Math.min(parseInt(req.query.n) || 24, 240);
  const result = await pool.query(
    `SELECT ora, temp_media, hum_media, soil_media, campioni
     FROM medie_orarie
     WHERE nodo_id = $1
     ORDER BY ora DESC
     LIMIT $2`,
    [req.params.id, n]
  );
  res.json(result.rows.reverse());
});

// GET /api/nodo/:id/giornaliere?n=10 — ultimi N giorni (default 10)
router.get('/nodo/:id/giornaliere', async (req, res) => {
  const n = Math.min(parseInt(req.query.n) || 10, 30);
  const result = await pool.query(
    `SELECT giorno, temp_media, hum_media, soil_media, campioni
     FROM medie_giornaliere
     WHERE nodo_id = $1
     ORDER BY giorno DESC
     LIMIT $2`,
    [req.params.id, n]
  );
  res.json(result.rows.reverse());
});

// GET /api/vapid-public — chiave pubblica VAPID per il frontend
router.get('/vapid-public', (req, res) => {
  res.json({ publicKey: process.env.VAPID_PUBLIC_KEY || '' });
});

module.exports = router;
