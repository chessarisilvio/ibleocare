'use strict';
const cron = require('node-cron');
const { pool } = require('./db');

/**
 * Calcola la media oraria dell'ora precedente per tutti i nodi.
 * Eseguito ogni ora al minuto 1 (es. 14:01, 15:01 ...).
 */
function schedulaMediaOraria() {
  cron.schedule('1 * * * *', async () => {
    console.log('[Cron] Calcolo medie orarie...');
    try {
      await pool.query(`
        INSERT INTO medie_orarie (nodo_id, ora, temp_media, hum_media, soil_media, campioni)
        SELECT
          nodo_id,
          date_trunc('hour', ts - interval '1 hour') AS ora,
          ROUND(AVG(temp)::numeric, 2),
          ROUND(AVG(hum)::numeric, 2),
          ROUND(AVG(soil)::numeric, 1),
          COUNT(*)
        FROM letture_raw
        WHERE ts >= date_trunc('hour', NOW() - interval '1 hour')
          AND ts <  date_trunc('hour', NOW())
        GROUP BY nodo_id, date_trunc('hour', ts - interval '1 hour')
        ON CONFLICT (nodo_id, ora)
          DO UPDATE SET
            temp_media = EXCLUDED.temp_media,
            hum_media  = EXCLUDED.hum_media,
            soil_media = EXCLUDED.soil_media,
            campioni   = EXCLUDED.campioni
      `);
      console.log('[Cron] Medie orarie aggiornate.');
    } catch (err) {
      console.error('[Cron] Errore medie orarie:', err.message);
    }
  });
}

/**
 * Calcola la media giornaliera del giorno precedente per tutti i nodi.
 * Eseguito ogni giorno alle 00:05.
 */
function schedulaMediaGiornaliera() {
  cron.schedule('5 0 * * *', async () => {
    console.log('[Cron] Calcolo medie giornaliere...');
    try {
      await pool.query(`
        INSERT INTO medie_giornaliere (nodo_id, giorno, temp_media, hum_media, soil_media, campioni)
        SELECT
          nodo_id,
          (date_trunc('day', NOW()) - interval '1 day')::date AS giorno,
          ROUND(AVG(temp)::numeric, 2),
          ROUND(AVG(hum)::numeric, 2),
          ROUND(AVG(soil)::numeric, 1),
          COUNT(*)
        FROM letture_raw
        WHERE ts >= date_trunc('day', NOW() - interval '1 day')
          AND ts <  date_trunc('day', NOW())
        GROUP BY nodo_id
        ON CONFLICT (nodo_id, giorno)
          DO UPDATE SET
            temp_media = EXCLUDED.temp_media,
            hum_media  = EXCLUDED.hum_media,
            soil_media = EXCLUDED.soil_media,
            campioni   = EXCLUDED.campioni
      `);
      console.log('[Cron] Medie giornaliere aggiornate.');
    } catch (err) {
      console.error('[Cron] Errore medie giornaliere:', err.message);
    }
  });
}

/**
 * Elimina le letture grezze più vecchie di 10 giorni.
 * Eseguito ogni giorno alle 00:30.
 */
function schedulaPulizia() {
  cron.schedule('30 0 * * *', async () => {
    console.log('[Cron] Pulizia letture grezze > 10 giorni...');
    try {
      const res = await pool.query(`
        DELETE FROM letture_raw
        WHERE ts < NOW() - interval '10 days'
      `);
      console.log(`[Cron] Righe eliminate: ${res.rowCount}`);
    } catch (err) {
      console.error('[Cron] Errore pulizia:', err.message);
    }
  });
}

function avviaTuttiCron() {
  schedulaMediaOraria();
  schedulaMediaGiornaliera();
  schedulaPulizia();
  console.log('[Cron] Scheduler avviato (orario, giornaliero, pulizia 10gg)');
}

module.exports = { avviaTuttiCron };
