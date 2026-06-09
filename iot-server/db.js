'use strict';
require('dotenv').config();
const { Pool } = require('pg');

const pool = new Pool({ connectionString: process.env.DATABASE_URL });

pool.on('error', (err) => {
  console.error('[DB] Errore pool:', err.message);
});

/**
 * Verifica la connessione al DB al boot.
 */
async function testConnection() {
  const client = await pool.connect();
  const res = await client.query('SELECT NOW()');
  client.release();
  console.log('[DB] Connesso a PostgreSQL:', res.rows[0].now);
}

module.exports = { pool, testConnection };
