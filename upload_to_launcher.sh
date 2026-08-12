#!/bin/bash

# Script per caricare e flashare automaticamente il firmware Bruce compilato su M5Launcher tramite WebUI (WiFi)
# Gestisce l'autenticazione tramite form e cookie di sessione.

if [ -z "$1" ]; then
    echo "Uso: $0 <IP_DEL_CARDPUTER> [USERNAME] [PASSWORD]"
    echo "Esempio: $0 192.168.1.123 admin launcher"
    exit 1
fi

IP=$1
USER=${2:-"admin"}
PASS=${3:-"launcher"}

FILE="Bruce-m5stack-cardputer.bin"
REMOTE_PATH="/downloads/$FILE"
COOKIE_FILE="/tmp/launcher_cookie.txt"

if [ ! -f "$FILE" ]; then
    echo "Errore: il file $FILE non esiste."
    echo "Assicurati di aver compilato il firmware prima di eseguire questo script."
    exit 1
fi

echo "1. Autenticazione su M5Launcher ($IP)..."
# Esegui il login e salva il cookie
curl -s -d "username=$USER&password=$PASS" -c "$COOKIE_FILE" "http://$IP/login" -o /dev/null

if [ ! -f "$COOKIE_FILE" ] || ! grep -q "ESP32SESSION" "$COOKIE_FILE"; then
    echo "   [AVVISO] Nessun cookie ESP32SESSION trovato nel file. Potrebbe non essere necessario il login"
    echo "            o le credenziali inserite ($USER / $PASS) sono errate."
fi

echo "2. Rimozione del file esistente $REMOTE_PATH per evitare conflitti..."
# Cancella il file se preesistente per prevenire l'errore di scrittura (HTTP 500)
curl -s -b "$COOKIE_FILE" "http://$IP/file?name=$REMOTE_PATH&action=delete" -o /dev/null

echo "3. Caricamento di $FILE su M5Launcher..."
# Carica il file binario usando il cookie di sessione (POST su / con folder=/downloads)
upload_res=$(curl -w "%{http_code}" -b "$COOKIE_FILE" -F "folder=/downloads" -F "file=@$FILE" "http://$IP/" -o /dev/null -s)

if [ "$upload_res" -eq 200 ] || [ "$upload_res" -eq 302 ]; then
    echo "   [OK] Caricamento completato con successo (HTTP $upload_res)."
else
    echo "   [ERRORE] Caricamento fallito. Codice HTTP: $upload_res"
    echo "   Verifica se l'endpoint di caricamento è corretto."
    exit 1
fi

echo "4. Invio del comando di flash per $REMOTE_PATH..."
# Richiede il flash del file caricato usando il cookie di sessione
flash_res=$(curl -w "%{http_code}" -b "$COOKIE_FILE" -F "fileName=$REMOTE_PATH" "http://$IP/UPDATE" -o /dev/null -s)

if [ "$flash_res" -eq 200 ] || [ "$flash_res" -eq 302 ]; then
    echo "   [OK] Comando di flash inviato con successo (HTTP $flash_res)!"
    echo "   Il Cardputer dovrebbe iniziare il flash ed avviarsi con Bruce a breve."
    # Rimuovi il file cookie temporaneo
    rm -f "$COOKIE_FILE"
else
    echo "   [ERRORE] Impossibile avviare il flash. Codice HTTP: $flash_res"
    rm -f "$COOKIE_FILE"
    exit 1
fi
