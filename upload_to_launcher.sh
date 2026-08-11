#!/bin/bash

# Script per caricare e flashare automaticamente il firmware Bruce compilato su M5Launcher tramite WebUI (WiFi)

if [ -z "$1" ]; then
    echo "Uso: $0 <IP_DEL_CARDPUTER>"
    echo "Esempio: $0 192.168.1.123"
    exit 1
fi

IP=$1
FILE="Bruce-m5stack-cardputer.bin"
REMOTE_PATH="/downloads/$FILE"

if [ ! -f "$FILE" ]; then
    echo "Errore: il file $FILE non esiste."
    echo "Assicurati di aver compilato il firmware prima di eseguire questo script."
    exit 1
fi

echo "1. Caricamento di $FILE su M5Launcher all'indirizzo $IP..."

# Upload del file binario in /downloads/
upload_res=$(curl -w "%{http_code}" -u admin:launcher -F "file=@$FILE" "http://$IP/upload?path=/downloads" -o /dev/null -s)

if [ "$upload_res" -eq 200 ] || [ "$upload_res" -eq 302 ]; then
    echo "   [OK] Caricamento completato con successo (HTTP $upload_res)."
else
    echo "   [ERRORE] Caricamento fallito. Codice HTTP: $upload_res"
    echo "   Verifica la connessione WiFi o se la password dell'M5Launcher è corretta."
    exit 1
fi

echo "2. Invio del comando di flash (UPDATE) per $REMOTE_PATH..."

# Invio della richiesta POST all'endpoint /UPDATE per flashare il file appena caricato
flash_res=$(curl -w "%{http_code}" -u admin:launcher -F "fileName=$REMOTE_PATH" "http://$IP/UPDATE" -o /dev/null -s)

if [ "$flash_res" -eq 200 ] || [ "$flash_res" -eq 302 ]; then
    echo "   [OK] Comando di flash inviato con successo (HTTP $flash_res)!"
    echo "   Il Cardputer dovrebbe iniziare il flash ed avviarsi con Bruce a breve."
else
    echo "   [ERRORE] Impossibile avviare il flash. Codice HTTP: $flash_res"
fi
