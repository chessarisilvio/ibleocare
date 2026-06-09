'use strict';
require('dotenv').config();

const express   = require('express');
const http      = require('http');
const { Server } = require('socket.io');
const path      = require('path');
const session   = require('express-session');
const bcrypt    = require('bcrypt');

const { testConnection, pool } = require('./db'); // Aggiunto 'pool' per leggere gli utenti
const { startMqtt }      = require('./mqtt');
const { avviaTuttiCron } = require('./cron');
const apiRoutes          = require('./routes/api');
const pushRoutes         = require('./routes/push');

const app    = express();
const server = http.createServer(app);
const io     = new Server(server, { cors: { origin: '*' } });

const PORT = process.env.PORT || 3000;

// ---- Middleware Base ----
app.use(express.json());

// ---- Configurazione Sessioni ----
app.use(session({
  secret: process.env.SESSION_SECRET || 'ibleocare_chiave_super_segreta',
  resave: false,
  saveUninitialized: false,
  cookie: { secure: false } // Impostare a true se in futuro userai HTTPS
}));

// ---- Il "Buttafuori" (Middleware di Autenticazione) ----
function requireAuth(req, res, next) {
  if (req.session && req.session.userId) {
    return next(); // Ha il pass, fallo passare
  } else {
    // Se è una chiamata API, dai errore 401. Altrimenti rimanda al login.
    if (req.path.startsWith('/api/')) {
      return res.status(401).json({ error: 'Non autorizzato' });
    }
    res.redirect('/login.html');
  }
}

// ---- Rotte Pubbliche (Visibili a tutti) ----
app.use(express.static(path.join(__dirname, 'public')));

// ---- Rotta di Login ----
app.post('/api/login', async (req, res) => {
  try {
    const { username, password } = req.body;
    
    // Cerca l'utente nel DB
    const result = await pool.query('SELECT * FROM utenti WHERE username = $1', [username]);
    if (result.rows.length === 0) {
      return res.status(401).json({ error: 'Utente non trovato' });
    }
    
    const user = result.rows[0];
    
    // Controlla la password
    const match = await bcrypt.compare(password, user.password_hash);
    if (match) {
      // Login effettuato con successo! Crea la sessione.
      req.session.userId = user.id;
      req.session.ruolo = user.ruolo;
      return res.json({ success: true });
    } else {
      return res.status(401).json({ error: 'Password errata' });
    }
  } catch (err) {
    console.error('[Auth Error]', err);
    res.status(500).json({ error: 'Errore interno del server' });
  }
});

// ---- Rotta di Logout ----
app.post('/api/logout', (req, res) => {
  req.session.destroy();
  res.json({ success: true });
});

// ---- Rotte Private (Protette dal Buttafuori) ----
app.use('/private', requireAuth, express.static(path.join(__dirname, 'private')));
app.use('/api/push', requireAuth, pushRoutes);
app.use('/api', requireAuth, apiRoutes);

// Fallback per rotte inesistenti
app.use((req, res, next) => {
  if (req.path.startsWith('/api')) return next();
  res.redirect('/');
});

// ---- Socket.IO ----
io.on('connection', (socket) => {
  console.log(`[Socket.IO] Client connesso: ${socket.id}`);
  socket.on('disconnect', () => {
    console.log(`[Socket.IO] Client disconnesso: ${socket.id}`);
  });
});

// ---- Boot ----
async function main() {
  await testConnection();
  startMqtt(io);
  avviaTuttiCron();

  server.listen(PORT, () => {
    console.log(`[Server] In ascolto su http://0.0.0.0:${PORT}`);
    console.log(`[Server] Homepage Pubblica: http://192.168.1.151:${PORT}`);
    console.log(`[Server] Dashboard Privata: http://192.168.1.151:${PORT}/private/dashboard.html`);
  });
}

main().catch((err) => {
  console.error('[Boot] Errore fatale:', err);
  process.exit(1);
});
