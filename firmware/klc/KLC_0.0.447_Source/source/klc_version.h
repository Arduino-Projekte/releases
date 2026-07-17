#pragma once

#define KLC_PROJECT_NAME "KLC"
#define KLC_PROJECT_FULL_NAME "KNX LED Controller"
#define KLC_VERSION "0.0.447"
#define KLC_CONFIG_SCHEMA_VERSION 64
#define KLC_RELEASE_NOTE "KLS1-Generationsabschluss: Hauptkonfiguration und Szenen werden ueber eine gemeinsame Configgeneration gebunden; Replacement wird erst nach erfolgreicher Pool-, PIO-/DMA- und LED-Backend-Aktivierung COMPLETE. LKG-, Previous- und Recovery-Fallback binden ihre KLS-Zielmenge neu. accepted-, applied- und persisted-Revision sind getrennt. 64-Bit-operation_id kombiniert persistente Bootgeneration und Zaehler und wird in JSON verlustfrei als String uebertragen. sessionStorage prueft Vorgangstyp, Szene, Revision und Bootgeneration und stellt bei fremden Vorgangen die Serverrevision wieder her. Runtime-Apply-, Rollback- und Storagefehler liefern getrennte Phasen; Slotdiagnose wird nach Schreibfehlern sofort neu gelesen. LKG-Promotion verwendet den zentralen Storage-Lease; alle Replacement-Ausloeser liefern ihre Vorgangs-ID."
#define KLC_RELEASE_PUBLISH false

#ifndef KLC_BUILD_DATE
#define KLC_BUILD_DATE __DATE__
#endif

#ifndef KLC_BUILD_TIME
#define KLC_BUILD_TIME __TIME__
#endif

#ifndef KLC_BUILD_LABEL
#define KLC_BUILD_LABEL "local"
#endif

#define KLC_BUILD_TIMESTAMP KLC_BUILD_DATE " " KLC_BUILD_TIME
