# MASTERPLAN: uForth Webhook Native Module (`wh-`)

Questo documento costituisce il blueprint di sviluppo e la specifica di implementazione dettagliata per il nuovo modulo nativo **Webhook (`src/modules/forth/natives/webhook.cpp`)**.

---

## 1. File Coinvolti e Modifiche al Codice

| File | Azione | Descrizione |
| :--- | :--- | :--- |
| `src/modules/forth/natives/webhook.cpp` | **Creazione** | Implementazione del server HTTP `ESPAsyncWebServer`, buffer circolare, gestore asincrono e funzioni native C++. |
| `src/modules/forth/natives/natives_internal.h` | **Modifica** | Dichiarazione di `webhook_bindings()` e `webhook_definitions()`. |
| `src/modules/forth/natives/natives.cpp` | **Modifica** | Registrazione di `webhook_bindings()` e `webhook_definitions()` nella catena di bootstrap. |
| `src/modules/forth/forth_repl.cpp` | **Modifica** | Inserimento dell'hook di pulizia automatica `webhook_cleanup()` all'uscita dalla REPL uForth. |

---

## 2. Strutture Dati C++ e Modello Logico

### A. Tabella delle Rotte Registrate (`RouteEntry`)
Memorizza le rotte registrate tramite `wh-route`, `wh-get` e `wh-post`:

```cpp
struct RouteEntry {
    char path[64];     // Percorso URL (es. "/api/data")
    CELL get_xt = 0;   // XT per metodo GET (0 = disabilitato)
    CELL post_xt = 0;  // XT per metodo POST (0 = disabilitato)
};
```

### B. Header degli Eventi nel Buffer Circolare (`uforth_ram`)
Ogni evento memorizzato nel buffer circolare occupa un header seguito dai byte del payload (se POST):

```
+--------------------+-------------------+---------------------+-------------------------+
| Handler XT (CELL)  | Req Type (1 byte) | Payload Len (uint16)| Payload Bytes (N bytes) |
| 2 Byte (uint16_t)  | 0 = GET, 1 = POST | 2 Byte (uint16_t)   | Presenti solo se POST   |
+--------------------+-------------------+---------------------+-------------------------+
```

### C. Stato del Ring Buffer e Tracciamento Client in Pausa (Zero Heap Allocation)
```cpp
#define MAX_PAUSED 4
static AsyncWebServer* g_webhook_server = nullptr;
static DCELL g_ring_ram_addr = 0;        // Indirizzo base in uforth_ram
static size_t g_ring_size_bytes = 0;     // Capienza totale in byte (num_cells * 8)
static size_t g_ring_head = 0;           // Write offset (C++ AsyncTCP)
static size_t g_ring_tail = 0;           // Read offset (uForth main thread)
static size_t g_pending_count = 0;       // Conteggio eventi in coda

// Array statico a memoria fissa (soli 16 byte) per tracciare i client messi in pausa con pause()
static AsyncWebServerRequest* g_paused[MAX_PAUSED] = { nullptr };
```

---

## 3. Algoritmo del Gestore Asincrono HTTP (`WebhookAsyncHandler`)

La classe `WebhookAsyncHandler` eredita da `AsyncWebHandler` e gestisce l'accept ed il chunking:

```
                  [ Richiesta HTTP in Arrivo ]
                               │
                               ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ 1. check_query_string()                                     │
 │    Contiene '?' -> Risponde 400 Bad Request                 │
 └─────────────────────────────────────────────────────────────┘
                               │ (No query string)
                               ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ 2. match_route_and_method()                                 │
 │    - Rotta non trovata -> return false (404)                │
 │    - Metodo Mismatch -> Risponde 405 Method Not Allowed     │
 └─────────────────────────────────────────────────────────────┘
                               │ (Rotta & Metodo OK)
                               ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ 3. check_ring_buffer_capacity()                             │
 │    Se POST e payload > spazio libero -> Pause TCP Client    │
 │    request->client()->pause() (TCP Zero-Window Flow Control)│
 └─────────────────────────────────────────────────────────────┘
                               │ (Spazio OK)
                               ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ 4. handleBody() [per POST]                                  │
 │    Scrive i byte del payload nel Ring Buffer in uforth_ram │
 └─────────────────────────────────────────────────────────────┘
                               │ (Richiesta Completa)
                               ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ 5. handleRequest()                                          │
 │    Invia 200 OK / 202 Accepted al client                     │
 └─────────────────────────────────────────────────────────────┘
```

---

## 4. Specifica Funzioni Native C++ (Bindings `wh-`)

### 1. `wh-start` `( port ram_addr num_cells -- status )`
- Estrae `num_cells`, `ram_addr`, `port`.
- Calcola `size_bytes = num_cells * sizeof(DCELL)` (ovvero `num_cells * 8`).
- Inizializza i puntatori `g_ring_head = 0`, `g_ring_tail = 0`, `g_pending_count = 0`.
- Istanzia ed avvia `g_webhook_server = new AsyncWebServer(port)`.
- Restituisce `0` (`UFORTH_OK`) in caso di successo, altrimenti codice di errore.

### 2. `wh-stop` `( -- )`
- Se `g_webhook_server != nullptr`:
  - Esegue `g_webhook_server->end()`.
  - Esegue `delete g_webhook_server; g_webhook_server = nullptr;`.
- Azzera la tabella delle rotte e i puntatori del buffer circolare.

### 3. `wh-route` `( get_xt post_xt dict_cstr_addr -- )`
- Legge l'indirizzo dal dizionario `uforth_dict[dict_cstr_addr]`.
- Estrae il testo dell'URL (tramite `memcpy` da `uforth_dict + dict_cstr_addr + 1`).
- Registra l'ingresso nella tabella `RouteEntry`.

### 4. `wh-get` `( get_xt dict_cstr_addr -- )`
- Invoca internamente la registrazione di `wh-route` impostando `post_xt = 0`.

### 5. `wh-post` `( post_xt dict_cstr_addr -- )`
- Invoca internamente la registrazione di `wh-route` impostando `get_xt = 0`.

### 6. `wh-advance-tail` `( -- )`
- Parola nativa di pulizia interna: incrementa `g_ring_tail` della dimensione dell'evento corrente (header per GET, header + payload per POST) e decrementa `g_pending_count`.
- **TCP Resume**: Se ci sono client in attesa messi in pausa (`request->client()->pause()`), controlla se lo spazio libero è tornato sufficiente ed esegue `client->resume()` per riaprire la finestra TCP.

### 7. `wh-tick` `( -- )`
- Legge gli eventi pendenti dal buffer circolare a partire da `g_ring_tail`.
- Per ogni evento trovato nella coda:
  - **Se GET**: Spinge `wh-advance-tail` (XT) e `get_xt` (XT).
  - **Se POST**: Spinge `addr` (indirizzo RAM payload), `len` (lunghezza payload), `wh-advance-tail` (XT) e `post_xt` (XT).
- Spinge sullo stack il numero totale di comandi da eseguire e lascia che `exec-xts` li consumi in ordine LIFO.

### 8. `wh-serve` `( -- )` (Parola Forth di Attesa Bloccante)
Definita nelle High-Level Definitions (`webhook_definitions`):
```forth
: wh-serve
    begin
        wh-tick
        10 ms
    key? 27 = until  \ Cicla finché l'utente non preme il tasto ESC (ASCII 27)
    wh-stop ;       \ Esegue automaticamente il teardown pulito all'uscita!
```

---

## 5. Sequenza di Esecuzione LIFO via `exec-xts`

### Sequenza Evento GET
$$\text{Stack generato da } \texttt{wh-tick}: \;\; ( \; \text{wh-advance-tail} \;\; \text{get\_xt} \; )$$
- `1.` Esegue `get_xt`: L'handler dell'utente viene eseguito con la firma `( -- )`.
- `2.` Esegue `wh-advance-tail`: Avanza il `Tail` liberando l'header GET nel buffer circolare.

### Sequenza Evento POST
$$\text{Stack generato da } \texttt{wh-tick}: \;\; ( \; \text{addr} \;\; \text{len} \;\; \text{wh-advance-tail} \;\; \text{post\_xt} \; )$$
- `1.` Esegue `post_xt`: L'handler dell'utente viene eseguito con la firma `( addr len -- )`.
- `2.` Esegue `wh-advance-tail`: Avanza il `Tail` liberando l'header + il payload POST nel buffer circolare.

---

## 6. Roadmap di Sviluppo

- [ ] **Fase 1**: Creazione di `src/modules/forth/natives/webhook.cpp` e registrazione dei prototipi in `natives_internal.h`.
- [ ] **Fase 2**: Implementazione delle strutture del Ring Buffer in `uforth_ram` e della logica di `wh-start` / `wh-stop`.
- [ ] **Fase 3**: Implementazione della classe `WebhookAsyncHandler` con le regole HTTP (Query string check -> 400, Method check -> 405, Capacity check -> 503).
- [ ] **Fase 4**: Implementazione delle parole di registrazione `wh-route`, `wh-get`, `wh-post`.
- [ ] **Fase 5**: Implementazione di `wh-tick` e `wh-advance-tail` per l'integrazione con `exec-xts`.
- [ ] **Fase 6**: Integrazione dell'hook `webhook_cleanup()` in `forth_repl.cpp` per l'auto-teardown.
- [ ] **Fase 7**: Collaudo del modulo con `marker` e `wh-serve`.
- [ ] **Fase 7**: Collaudo e verifica con `marker` e `forget`.
