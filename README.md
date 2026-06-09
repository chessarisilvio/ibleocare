# IbleoCare — Smart IoT Agriculture

Sistema IoT per il monitoraggio e l'automazione dell'irrigazione, sviluppato per il concorso **JA Italia**.

## Struttura del progetto

```
ibleocare/
├── iot-server/          # Backend Node.js + sito web
│   ├── public/          # Sito web pubblico (HTML/CSS)
│   ├── private/         # Dashboard riservata (autenticazione richiesta)
│   ├── routes/          # API REST
│   ├── index.js         # Entry point server
│   ├── db.js            # Connessione PostgreSQL
│   ├── mqtt.js          # Broker MQTT
│   ├── cron.js          # Task pianificati
│   └── push-sender.js   # Notifiche push (Web Push API)
├── firmware/
│   └── piantswebs_touch30s.ino  # Firmware Arduino UNO R4 WiFi
└── docs.txt             # Documentazione tecnica completa
```

## Stack tecnologico

- **Hardware**: Arduino UNO R4 WiFi + sensori capacitivi umidità suolo + DHT22
- **Firmware**: C++ (Arduino IDE)
- **Backend**: Node.js + Express + Socket.IO
- **Database**: PostgreSQL
- **Comunicazione**: MQTT (Mosquitto broker)
- **Frontend**: HTML5 + Bootstrap 5 + Chart.js
- **Notifiche**: Web Push API (VAPID)

## Setup (sviluppo locale)

### Prerequisiti
- Node.js 18+
- PostgreSQL 14+
- Mosquitto MQTT broker

### Avvio server

```bash
cd iot-server
npm install
cp .env.example .env   # configura le variabili d'ambiente
node index.js
```

### Variabili d'ambiente richieste (`.env`)

```
DATABASE_URL=postgresql://USER:PASS@localhost:5432/iot_piante
MQTT_BROKER=mqtt://localhost:1883
PORT=3000
VAPID_PUBLIC_KEY=...
VAPID_PRIVATE_KEY=...
VAPID_MAILTO=mailto:your@email.com
```

### Crea il primo admin

```bash
cd iot-server
node crea-admin.js
```

## Funzionamento

1. I sensori sul campo inviano dati ogni 30 secondi via MQTT
2. Il server Node.js riceve i dati e li salva su PostgreSQL
3. La dashboard live mostra temperatura, umidità aria, umidità suolo in tempo reale
4. In caso di soglie critiche, vengono inviate notifiche push al dispositivo dell'utente
5. Il modulo di controllo permette di attivare/disattivare l'irrigazione da remoto

## Team

Progetto realizzato da 9 studenti + professori supervisori nell'ambito di **JA Italia 2026**.

---

© 2026 IbleoCare Project — Team JA Italia
