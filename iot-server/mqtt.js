'use strict';
require('dotenv').config();
const mqttLib = require('mqtt');
const { pool } = require('./db');
const webpush = require('./push-sender');

let io; // Socket.IO instance, iniettata da index.js

// Cache soglie allarme per nodo { nodo_id: soglia }
const soglieAllarme = {};
// Stato precedente suolo per evitare push ripetute { nodo_id: bool }
const allarmeAttivo = {};

/**
 * Aggiorna la cache delle soglie dal DB.
 */
async function refreshSoglie() {
  const res = await pool.query('SELECT id, soglia_allarme FROM nodi');
  for (const row of res.rows) {
    soglieAllarme[row.id] = row.soglia_allarme;
  }
}

/**
 * Avvia il client MQTT e si iscrive ai topic dei nodi.
 * @param {object} socketIo - istanza Socket.IO
 */
function startMqtt(socketIo) {
  io = socketIo;

  refreshSoglie().catch(console.error);

  const client = mqttLib.connect(process.env.MQTT_BROKER, {
    clientId: 'iot-server-backend',
    clean: true,
    reconnectPeriod: 5000,
  });

  client.on('connect', () => {
    console.log('[MQTT] Connesso al broker:', process.env.MQTT_BROKER);
    // Iscriviti a tutti i nodi (wildcard singolo livello)
    client.subscribe('pianta/+/data', (err) => {
      if (err) console.error('[MQTT] Subscribe error:', err.message);
      else console.log('[MQTT] Iscritto a pianta/+/data');
    });
  });

  client.on('error', (err) => {
    console.error('[MQTT] Errore:', err.message);
  });

  client.on('message', async (topic, message) => {
    // topic: pianta/nodo1/data
    let payload;
    try {
      payload = JSON.parse(message.toString());
    } catch {
      console.warn('[MQTT] Payload non valido:', message.toString());
      return;
    }

    const { id: nodoId, temp, hum, soil } = payload;
    if (!nodoId) return;

    const ts = new Date();

    // 1. Auto-registra il nodo se non esiste ancora
    await pool.query(
      `INSERT INTO nodi (id, nome, tipo_pianta, soglia_allarme)
       VALUES ($1, $2, 1, 33)
       ON CONFLICT (id) DO NOTHING`,
      [nodoId, `nodo${nodoId}`]
    );

    // 2. Salva la lettura grezza
    await pool.query(
      `INSERT INTO letture_raw (nodo_id, ts, temp, hum, soil)
       VALUES ($1, $2, $3, $4, $5)`,
      [nodoId, ts, temp ?? null, hum ?? null, soil ?? null]
    );

    // 3. Aggiorna stato live del nodo
    await pool.query(
      `UPDATE nodi
       SET ultimo_aggiornamento=$1, ultimo_temp=$2, ultimo_hum=$3, ultimo_soil=$4
       WHERE id=$5`,
      [ts, temp ?? null, hum ?? null, soil ?? null, nodoId]
    );

    // 4. Emetti live via Socket.IO
    if (io) {
      io.emit('sensor_data', { id: nodoId, temp, hum, soil, ts });
    }

    // 5. Controllo allarme suolo e invio push se necessario
    const soglia = soglieAllarme[nodoId] ?? 33;
    const sottoSoglia = typeof soil === 'number' && soil < soglia;

    if (sottoSoglia && !allarmeAttivo[nodoId]) {
      allarmeAttivo[nodoId] = true;
      await sendPushAllarme(nodoId, soil);
    } else if (!sottoSoglia) {
      allarmeAttivo[nodoId] = false;
    }
  });

  return client;
}

/**
 * Invia push notification a tutti gli iscritti di un nodo.
 */
async function sendPushAllarme(nodoId, soil) {
  const res = await pool.query(
    'SELECT subscription FROM push_subscriptions WHERE nodo_id=$1',
    [nodoId]
  );
  if (!res.rows.length) return;

  const payload = JSON.stringify({
    title: `Pianta nodo${nodoId} — serve acqua!`,
    body: `Umidità suolo: ${soil}% — sotto la soglia di allarme`,
    icon: '/icon.png',
    tag: `allarme-nodo${nodoId}`,
  });

  for (const row of res.rows) {
    try {
      await webpush.sendNotification(row.subscription, payload);
    } catch (err) {
      if (err.statusCode === 410 || err.statusCode === 404) {
        // Subscription scaduta, rimuovi
        await pool.query(
          'DELETE FROM push_subscriptions WHERE endpoint=$1',
          [row.subscription.endpoint]
        );
        console.log('[Push] Subscription scaduta rimossa:', row.subscription.endpoint);
      } else {
        console.error('[Push] Errore invio:', err.message);
      }
    }
  }
}

module.exports = { startMqtt, refreshSoglie };
