require('dotenv').config();
const bcrypt = require('bcrypt');
const db = require('./db.js');

async function setup() {
  try {
    // 1. Crea la tabella se non esiste
    await db.pool.query(`
      CREATE TABLE IF NOT EXISTS utenti (
        id SERIAL PRIMARY KEY,
        username VARCHAR(50) UNIQUE NOT NULL,
        password_hash VARCHAR(255) NOT NULL,
        ruolo VARCHAR(20) DEFAULT 'admin'
      );
    `);
    console.log("[DB] Tabella 'utenti' pronta.");

    // 2. Imposta le credenziali per il primo accesso
    const username = 'admin';
    const passwordPlain = 'ibleocare2026';
    const saltRounds = 10;
    
    // 3. Cripta la password
    const hash = await bcrypt.hash(passwordPlain, saltRounds);

    // 4. Salva l'utente nel database
    await db.pool.query(`
      INSERT INTO utenti (username, password_hash)
      VALUES ($1, $2)
      ON CONFLICT (username) DO NOTHING;
    `, [username, hash]);

    console.log(`\n✅ Amministratore creato con successo!`);
    console.log(`👤 Username: ${username}`);
    console.log(`🔑 Password: ${passwordPlain}\n`);
    
    process.exit(0);
  } catch (err) {
    console.error("Errore durante la creazione:", err);
    process.exit(1);
  }
}

setup();
