| Supported Targets | ESP32-C6 |
| ----------------- | -------- |

# 🔥 Scheda di Controllo Caldaia & Ricarica Tablet

Questa scheda consente di **accendere una caldaia** chiudendo un contatto a relè e contemporaneamente **alimentare un tablet (termostato)** tramite una linea da 5V.  
È progettata per funzionare in modalità **automatica** (via microcontrollore ESP32-C3 o C6) oppure in **manuale** (fail-safe in caso di guasto al micro).

## ⚙️ Funzionalità principali

- 🔁 **Modalità selezionabile**: Manuale o Automatica tramite jumper e connettore esterno
- 🔌 **Controllo carica tablet**: Attiva/disattiva l’alimentazione 5V
- 🔧 **Bypass microcontrollore**: In caso di errore o assenza firmware
- 🟢 **LED di stato** per segnalazione accensione caldaia e carica tablet
- 🧠 Compatibile con **Seeed Studio XIAO ESP32-C3** e **ESP32-C6**

---

## 🧩 Schema a Blocchi

│ │ D1 │───> Comando Caldaia (Relè)
│ │ D2 │───> Comando Carica Tablet

## 🔌 Ingressi

- **+5V**: Alimentazione per tablet e microcontrollore
- **Selettore MANUALE/AUTOMATICO** (contatto):
  - **Chiusi** → modalità **MANUALE**
  - **Aperti** → modalità **AUTOMATICA** (comando da micro)
- **Jumper JP1**:
  - Chiuso → forza modalità **AUTOMATICA**
- **COMANDO_MANUALE** (valido solo se in MANUALE):
  - **Chiuso** → accensione caldaia
  - **Aperto** → spegnimento caldaia

## 🔁 Logica di funzionamento

| Modalità         | Stato Selettore | Stato COMANDO_MANUALE | Stato Caldaia         |
|------------------|------------------|------------------------|------------------------|
| AUTOMATICO       | Aperto           | Chiuso                 | Controllata da micro   |
| AUTOMATICO (ERR) | Aperto           | Aperto                 | Spenta (errore)        |
| MANUALE - Spenta | Chiuso           | Aperto                 | Spenta                 |
| MANUALE - Accesa | Chiuso           | Chiuso                 | Accesa                 |

---

## 🔋 Uscite

- **Contatto caldaia (relè)**: chiude/apre il circuito di accensione
- **5V OUT verso Tablet**: controllabile da micro tramite D2
- **LED Caldaia**: indica stato accensione
- **LED Carica**: indica stato alimentazione tablet

---

## 🧠 Microcontrollore supportato

- [XIAO ESP32-C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/)
- [XIAO ESP32-C6](https://wiki.seeedstudio.com/XIAO_ESP32C6_Getting_Started/)

---  
