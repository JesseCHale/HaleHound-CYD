// ═══════════════════════════════════════════════════════════════════════════════
// BLE DATABASE - Bluetooth Numbers Database for HaleHound
// ═══════════════════════════════════════════════════════════════════════════════
// Source: Nordic Semiconductor bluetooth-numbers-database (v1.0.4, Feb 2026)
// https://github.com/NordicSemiconductor/bluetooth-numbers-database
//
// 94 Company IDs  |  56 Service UUIDs (16-bit only)  |  52 GAP Appearances
// All stored in PROGMEM — zero RAM cost, ~4 KB flash
// ═══════════════════════════════════════════════════════════════════════════════

#ifndef BLE_DATABASE_H
#define BLE_DATABASE_H

#include <Arduino.h>
#include <pgmspace.h>

// ═══════════════════════════════════════════════════════════════════════════════
// SECTION 1: BLUETOOTH COMPANY IDs (BT SIG Assigned Numbers)
// ═══════════════════════════════════════════════════════════════════════════════
// Extracted from manufacturer-specific advertising data (AD type 0xFF).
// First 2 bytes of manufacturer data = company ID (little-endian).
// Sorted by code for binary search.
// ═══════════════════════════════════════════════════════════════════════════════

struct BleCompanyEntry {
    uint16_t    code;
    const char* name;   // Short display name (max 14 chars)
};

static const char PROGMEM _cn_ericsson[]      = "Ericsson";
static const char PROGMEM _cn_nokia[]         = "Nokia";
static const char PROGMEM _cn_intel[]         = "Intel";
static const char PROGMEM _cn_microsoft[]     = "Microsoft";
static const char PROGMEM _cn_motorola[]      = "Motorola";
static const char PROGMEM _cn_infineon[]      = "Infineon";
static const char PROGMEM _cn_qualcomm[]      = "Qualcomm";
static const char PROGMEM _cn_ti[]            = "Texas Inst";
static const char PROGMEM _cn_broadcom[]      = "Broadcom";
static const char PROGMEM _cn_qualcomm2[]     = "Qualcomm";
static const char PROGMEM _cn_nxp[]           = "NXP";
static const char PROGMEM _cn_mediatek[]      = "MediaTek";
static const char PROGMEM _cn_marvell[]       = "Marvell";
static const char PROGMEM _cn_apple[]         = "Apple";
static const char PROGMEM _cn_sony[]          = "Sony";
static const char PROGMEM _cn_nordic[]        = "Nordic Semi";
static const char PROGMEM _cn_belkin[]        = "Belkin";
static const char PROGMEM _cn_realtek[]       = "Realtek";
static const char PROGMEM _cn_polar[]         = "Polar";
static const char PROGMEM _cn_samsung[]       = "Samsung";
static const char PROGMEM _cn_garmin[]        = "Garmin";
static const char PROGMEM _cn_bose[]          = "Bose";
static const char PROGMEM _cn_suunto[]        = "Suunto";
static const char PROGMEM _cn_lg[]            = "LG";
static const char PROGMEM _cn_beats[]         = "Beats";
static const char PROGMEM _cn_microchip[]     = "Microchip";
static const char PROGMEM _cn_dexcom[]        = "Dexcom";
static const char PROGMEM _cn_google[]        = "Google";
static const char PROGMEM _cn_bo[]            = "B&O";
static const char PROGMEM _cn_audi[]          = "Audi";
static const char PROGMEM _cn_fitbit[]        = "Fitbit";
static const char PROGMEM _cn_steelseries[]   = "SteelSeries";
static const char PROGMEM _cn_volkswagen[]    = "Volkswagen";
static const char PROGMEM _cn_sony2[]         = "Sony";
static const char PROGMEM _cn_cypress[]       = "Cypress";
static const char PROGMEM _cn_murata[]        = "Murata";
static const char PROGMEM _cn_amazon[]        = "Amazon";
static const char PROGMEM _cn_mercedes[]      = "Mercedes";
static const char PROGMEM _cn_google2[]       = "Google";
static const char PROGMEM _cn_nest[]          = "Nest";
static const char PROGMEM _cn_august[]        = "August";
static const char PROGMEM _cn_logitech[]      = "Logitech";
static const char PROGMEM _cn_philips[]       = "Philips";
static const char PROGMEM _cn_medtronic[]     = "Medtronic";
static const char PROGMEM _cn_omron[]         = "Omron";
static const char PROGMEM _cn_cisco[]         = "Cisco";
static const char PROGMEM _cn_tesla[]         = "Tesla";
static const char PROGMEM _cn_huawei[]        = "Huawei";
static const char PROGMEM _cn_oura[]          = "Oura";
static const char PROGMEM _cn_lenovo[]        = "Lenovo";
static const char PROGMEM _cn_samsung2[]      = "Samsung";
static const char PROGMEM _cn_espressif[]     = "Espressif";
static const char PROGMEM _cn_htc[]           = "HTC";
static const char PROGMEM _cn_gopro[]         = "GoPro";
static const char PROGMEM _cn_resmed[]        = "ResMed";
static const char PROGMEM _cn_xiaomi[]        = "Xiaomi";
static const char PROGMEM _cn_abbott[]        = "Abbott";
static const char PROGMEM _cn_withings[]      = "Withings";
static const char PROGMEM _cn_sennheiser[]    = "Sennheiser";
static const char PROGMEM _cn_shure[]         = "Shure";
static const char PROGMEM _cn_motorola2[]     = "Motorola";
static const char PROGMEM _cn_honeywell[]     = "Honeywell";
static const char PROGMEM _cn_nintendo[]      = "Nintendo";
static const char PROGMEM _cn_valve[]         = "Valve";
static const char PROGMEM _cn_sonos[]         = "Sonos";
static const char PROGMEM _cn_irobot[]        = "iRobot";
static const char PROGMEM _cn_audiotechnica[] = "AudioTechnica";
static const char PROGMEM _cn_tile[]          = "Tile";
static const char PROGMEM _cn_razer[]         = "Razer";
static const char PROGMEM _cn_ford[]          = "Ford";
static const char PROGMEM _cn_oneplus[]       = "OnePlus";
static const char PROGMEM _cn_peloton[]       = "Peloton";
static const char PROGMEM _cn_oppo[]          = "OPPO";
static const char PROGMEM _cn_roku[]          = "Roku";
static const char PROGMEM _cn_skullcandy[]    = "Skullcandy";
static const char PROGMEM _cn_tuya[]          = "Tuya";
static const char PROGMEM _cn_ecobee[]        = "ecobee";
static const char PROGMEM _cn_nanoleaf[]      = "Nanoleaf";
static const char PROGMEM _cn_vivo[]          = "Vivo";
static const char PROGMEM _cn_wyze[]          = "Wyze";
static const char PROGMEM _cn_realme[]        = "Realme";
static const char PROGMEM _cn_dji[]           = "DJI";
static const char PROGMEM _cn_chipolo[]       = "Chipolo";
static const char PROGMEM _cn_zte[]           = "ZTE";
static const char PROGMEM _cn_toyota[]        = "Toyota";
static const char PROGMEM _cn_dyson[]         = "Dyson";
static const char PROGMEM _cn_kaadas[]        = "Kaadas";
static const char PROGMEM _cn_yale[]          = "Yale";
static const char PROGMEM _cn_anker[]         = "Anker";
static const char PROGMEM _cn_nothing[]       = "Nothing";
static const char PROGMEM _cn_jlab[]          = "JLab";
static const char PROGMEM _cn_sonyhonda[]     = "Sony Honda";
static const char PROGMEM _cn_dynaudio[]      = "Dynaudio";
static const char PROGMEM _cn_polk[]          = "Polk";

// MUST be sorted by code — binary search depends on this
static const BleCompanyEntry PROGMEM bleCompanyDB[] = {
    { 0x0000, _cn_ericsson      },  // Ericsson AB
    { 0x0001, _cn_nokia         },  // Nokia Mobile Phones
    { 0x0002, _cn_intel         },  // Intel Corp.
    { 0x0006, _cn_microsoft     },  // Microsoft
    { 0x0008, _cn_motorola      },  // Motorola
    { 0x0009, _cn_infineon      },  // Infineon Technologies AG
    { 0x000A, _cn_qualcomm      },  // Qualcomm Technologies Intl
    { 0x000D, _cn_ti            },  // Texas Instruments Inc.
    { 0x000F, _cn_broadcom      },  // Broadcom Corporation
    { 0x001D, _cn_qualcomm2     },  // Qualcomm
    { 0x0025, _cn_nxp           },  // NXP B.V.
    { 0x0046, _cn_mediatek      },  // MediaTek, Inc.
    { 0x0048, _cn_marvell       },  // Marvell Technology Group
    { 0x004C, _cn_apple         },  // Apple, Inc.
    { 0x0056, _cn_sony          },  // Sony Ericsson Mobile
    { 0x0059, _cn_nordic        },  // Nordic Semiconductor ASA
    { 0x005C, _cn_belkin        },  // Belkin International
    { 0x005D, _cn_realtek       },  // Realtek Semiconductor
    { 0x006B, _cn_polar         },  // Polar Electro OY
    { 0x0075, _cn_samsung       },  // Samsung Electronics
    { 0x0087, _cn_garmin        },  // Garmin International
    { 0x009E, _cn_bose          },  // Bose Corporation
    { 0x009F, _cn_suunto        },  // Suunto Oy
    { 0x00C4, _cn_lg            },  // LG Electronics
    { 0x00CC, _cn_beats         },  // Beats Electronics
    { 0x00CD, _cn_microchip     },  // Microchip Technology
    { 0x00D0, _cn_dexcom        },  // Dexcom, Inc.
    { 0x00E0, _cn_google        },  // Google
    { 0x0103, _cn_bo            },  // Bang & Olufsen A/S
    { 0x010E, _cn_audi          },  // Audi AG
    { 0x0110, _cn_fitbit        },  // Fitbit LLC
    { 0x0111, _cn_steelseries   },  // Steelseries ApS
    { 0x011F, _cn_volkswagen    },  // Volkswagen AG
    { 0x012D, _cn_sony2         },  // Sony Corporation
    { 0x0131, _cn_cypress       },  // Cypress Semiconductor
    { 0x013C, _cn_murata        },  // Murata Manufacturing
    { 0x0171, _cn_amazon        },  // Amazon.com Services
    { 0x017C, _cn_mercedes      },  // Mercedes-Benz Group AG
    { 0x018E, _cn_google2       },  // Google LLC
    { 0x01B5, _cn_nest          },  // Nest Labs Inc.
    { 0x01D1, _cn_august        },  // August Home, Inc
    { 0x01DA, _cn_logitech      },  // Logitech International SA
    { 0x01DD, _cn_philips       },  // Koninklijke Philips N.V.
    { 0x01F9, _cn_medtronic     },  // Medtronic Inc.
    { 0x020E, _cn_omron         },  // Omron Healthcare
    { 0x021B, _cn_cisco         },  // Cisco Systems
    { 0x022B, _cn_tesla         },  // Tesla, Inc.
    { 0x027D, _cn_huawei        },  // HUAWEI Technologies
    { 0x02B2, _cn_oura          },  // Oura Health Oy
    { 0x02C5, _cn_lenovo        },  // Lenovo (Singapore)
    { 0x02DE, _cn_samsung2      },  // Samsung SDS Co., Ltd.
    { 0x02E5, _cn_espressif     },  // Espressif Systems
    { 0x02ED, _cn_htc           },  // HTC Corporation
    { 0x02F2, _cn_gopro         },  // GoPro, Inc.
    { 0x038D, _cn_resmed        },  // Resmed Ltd
    { 0x038F, _cn_xiaomi        },  // Xiaomi Inc.
    { 0x03BB, _cn_abbott        },  // Abbott
    { 0x03FF, _cn_withings      },  // Withings
    { 0x0494, _cn_sennheiser    },  // SENNHEISER electronic
    { 0x04AD, _cn_shure         },  // Shure Inc
    { 0x04EC, _cn_motorola2     },  // Motorola Solutions
    { 0x0526, _cn_honeywell     },  // Honeywell International
    { 0x0553, _cn_nintendo      },  // Nintendo Co., Ltd.
    { 0x055D, _cn_valve         },  // Valve Corporation
    { 0x05A7, _cn_sonos         },  // Sonos Inc
    { 0x0600, _cn_irobot        },  // iRobot Corporation
    { 0x0618, _cn_audiotechnica },  // Audio-Technica Corporation
    { 0x067C, _cn_tile          },  // Tile, Inc.
    { 0x068E, _cn_razer         },  // Razer Inc.
    { 0x0723, _cn_ford          },  // Ford Motor Company
    { 0x072F, _cn_oneplus       },  // OnePlus Electronics
    { 0x0768, _cn_peloton       },  // Peloton Interactive
    { 0x079A, _cn_oppo          },  // GuangDong Oppo Mobile
    { 0x07A2, _cn_roku          },  // Roku, Inc.
    { 0x07C9, _cn_skullcandy    },  // Skullcandy, Inc.
    { 0x07D0, _cn_tuya          },  // Hangzhou Tuya Information
    { 0x07D6, _cn_ecobee        },  // ecobee Inc.
    { 0x080B, _cn_nanoleaf      },  // Nanoleaf Canada Limited
    { 0x0837, _cn_vivo          },  // vivo Mobile Communication
    { 0x0870, _cn_wyze          },  // Wyze Labs, Inc
    { 0x08A4, _cn_realme        },  // Realme Chongqing Mobile
    { 0x08AA, _cn_dji           },  // SZ DJI TECHNOLOGY
    { 0x08C3, _cn_chipolo       },  // CHIPOLO d.o.o.
    { 0x0958, _cn_zte           },  // ZTE Corporation
    { 0x0977, _cn_toyota        },  // TOYOTA motor corporation
    { 0x0A12, _cn_dyson         },  // Dyson Technology Limited
    { 0x0B53, _cn_kaadas        },  // Kaadas Intelligent Tech
    { 0x0BDE, _cn_yale          },  // Yale
    { 0x0CC2, _cn_anker         },  // Anker Innovations Limited
    { 0x0CCB, _cn_nothing       },  // NOTHING TECHNOLOGY LIMITED
    { 0x0CE9, _cn_jlab          },  // PEAG, LLC dba JLab Audio
    { 0x0EDE, _cn_sonyhonda     },  // Sony Honda Mobility Inc.
    { 0x0EDF, _cn_dynaudio      },  // Dynaudio A/S
    { 0x0F7C, _cn_polk          },  // Polk Audio
};

static const int BLE_COMPANY_DB_SIZE = sizeof(bleCompanyDB) / sizeof(bleCompanyDB[0]);

// Binary search — O(log n), ~7 comparisons max for 94 entries
static const char* lookupCompanyName(uint16_t companyId) {
    int lo = 0, hi = BLE_COMPANY_DB_SIZE - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint16_t midCode = pgm_read_word(&bleCompanyDB[mid].code);
        if (midCode == companyId) {
            return (const char*)pgm_read_ptr(&bleCompanyDB[mid].name);
        } else if (midCode < companyId) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return nullptr;  // Not found
}

// ═══════════════════════════════════════════════════════════════════════════════
// SECTION 2: GATT SERVICE UUIDs (16-bit standard services only)
// ═══════════════════════════════════════════════════════════════════════════════
// These are the 16-bit UUIDs from the Bluetooth SIG GATT specification.
// Used to identify what a BLE device is advertising / offering.
// 128-bit vendor UUIDs (Apple, Nordic, etc.) are NOT included here —
// they require separate handling per-vendor.
// Sorted by UUID for binary search.
// ═══════════════════════════════════════════════════════════════════════════════

struct BleServiceEntry {
    uint16_t    uuid;
    const char* name;
};

static const char PROGMEM _sn_generic_access[]   = "Generic Access";
static const char PROGMEM _sn_generic_attr[]     = "Generic Attrib";
static const char PROGMEM _sn_immediate_alert[]  = "Immediate Alert";
static const char PROGMEM _sn_link_loss[]        = "Link Loss";
static const char PROGMEM _sn_tx_power[]         = "Tx Power";
static const char PROGMEM _sn_current_time[]     = "Current Time";
static const char PROGMEM _sn_ref_time[]         = "Ref Time Update";
static const char PROGMEM _sn_next_dst[]         = "Next DST Change";
static const char PROGMEM _sn_glucose[]          = "Glucose";
static const char PROGMEM _sn_health_thermo[]    = "Health Thermo";
static const char PROGMEM _sn_device_info[]      = "Device Info";
static const char PROGMEM _sn_heart_rate[]       = "Heart Rate";
static const char PROGMEM _sn_phone_alert[]      = "Phone Alert";
static const char PROGMEM _sn_battery[]          = "Battery";
static const char PROGMEM _sn_blood_pressure[]   = "Blood Pressure";
static const char PROGMEM _sn_alert_notif[]      = "Alert Notif";
static const char PROGMEM _sn_hid[]              = "HID";
static const char PROGMEM _sn_scan_params[]      = "Scan Params";
static const char PROGMEM _sn_running[]          = "Running Speed";
static const char PROGMEM _sn_automation[]        = "Automation IO";
static const char PROGMEM _sn_cycling_speed[]    = "Cycling Speed";
static const char PROGMEM _sn_cycling_power[]    = "Cycling Power";
static const char PROGMEM _sn_location_nav[]     = "Location & Nav";
static const char PROGMEM _sn_env_sensing[]      = "Env Sensing";
static const char PROGMEM _sn_body_comp[]        = "Body Composit";
static const char PROGMEM _sn_user_data[]        = "User Data";
static const char PROGMEM _sn_weight_scale[]     = "Weight Scale";
static const char PROGMEM _sn_bond_mgmt[]        = "Bond Mgmt";
static const char PROGMEM _sn_cgm[]              = "Cont Glucose";
static const char PROGMEM _sn_ip_support[]       = "IP Support";
static const char PROGMEM _sn_indoor_pos[]       = "Indoor Position";
static const char PROGMEM _sn_http_proxy[]       = "HTTP Proxy";
static const char PROGMEM _sn_transport[]        = "Transport Disc";
static const char PROGMEM _sn_obj_transfer[]     = "Object Transfer";
static const char PROGMEM _sn_fitness[]          = "Fitness Machine";
static const char PROGMEM _sn_mesh_prov[]        = "Mesh Provision";
static const char PROGMEM _sn_mesh_proxy[]       = "Mesh Proxy";
static const char PROGMEM _sn_reconnect[]        = "Reconnection";
static const char PROGMEM _sn_insulin[]          = "Insulin Deliver";
static const char PROGMEM _sn_binary_sensor[]    = "Binary Sensor";
static const char PROGMEM _sn_emergency[]        = "Emergency Conf";
static const char PROGMEM _sn_phys_activity[]    = "Physical Activ";
static const char PROGMEM _sn_audio_input[]      = "Audio Input";
static const char PROGMEM _sn_volume_ctrl[]      = "Volume Control";
static const char PROGMEM _sn_vol_offset[]       = "Vol Offset";
static const char PROGMEM _sn_coord_set[]        = "Coordinated Set";
static const char PROGMEM _sn_device_time[]      = "Device Time";
static const char PROGMEM _sn_media_ctrl[]       = "Media Control";
static const char PROGMEM _sn_gen_media[]        = "Gen Media Ctrl";
static const char PROGMEM _sn_cte[]              = "Const Tone Ext";
static const char PROGMEM _sn_telephone[]        = "Telephone Bear";
static const char PROGMEM _sn_gen_telephone[]    = "Gen Telephone";
static const char PROGMEM _sn_mic_ctrl[]         = "Microphone Ctrl";
static const char PROGMEM _sn_audio_stream[]     = "Audio Stream";
static const char PROGMEM _sn_broadcast_scan[]   = "Broadcast Scan";
static const char PROGMEM _sn_pub_audio_cap[]    = "Pub Audio Cap";
static const char PROGMEM _sn_basic_audio[]      = "Basic Audio";
static const char PROGMEM _sn_broadcast_audio[]  = "Broadcast Audio";
static const char PROGMEM _sn_common_audio[]     = "Common Audio";
static const char PROGMEM _sn_hearing_access[]   = "Hearing Access";
static const char PROGMEM _sn_tmap[]             = "Telephony Media";
static const char PROGMEM _sn_pub_broadcast[]    = "Pub Broadcast";
static const char PROGMEM _sn_esl[]              = "Elec Shelf Lbl";
static const char PROGMEM _sn_gaming_audio[]     = "Gaming Audio";
static const char PROGMEM _sn_mesh_solicit[]     = "Mesh Proxy Sol";

// Well-known vendor 16-bit service UUIDs (not GATT standard but very common)
static const char PROGMEM _sn_eddystone[]        = "Eddystone";
static const char PROGMEM _sn_fast_pair[]        = "Fast Pair";
static const char PROGMEM _sn_exposure_notif[]   = "Exposure Notif";
static const char PROGMEM _sn_dfu_secure[]       = "Secure DFU";
static const char PROGMEM _sn_philips_hue[]      = "Philips Hue";
static const char PROGMEM _sn_adafruit_ft[]      = "Adafruit FT";
static const char PROGMEM _sn_blecon[]           = "Blecon";

// MUST be sorted by UUID for binary search
static const BleServiceEntry PROGMEM bleServiceDB[] = {
    { 0x1800, _sn_generic_access   },
    { 0x1801, _sn_generic_attr     },
    { 0x1802, _sn_immediate_alert  },
    { 0x1803, _sn_link_loss        },
    { 0x1804, _sn_tx_power         },
    { 0x1805, _sn_current_time     },
    { 0x1806, _sn_ref_time         },
    { 0x1807, _sn_next_dst         },
    { 0x1808, _sn_glucose          },
    { 0x1809, _sn_health_thermo    },
    { 0x180A, _sn_device_info      },
    { 0x180D, _sn_heart_rate       },
    { 0x180E, _sn_phone_alert      },
    { 0x180F, _sn_battery          },
    { 0x1810, _sn_blood_pressure   },
    { 0x1811, _sn_alert_notif      },
    { 0x1812, _sn_hid              },
    { 0x1813, _sn_scan_params      },
    { 0x1814, _sn_running          },
    { 0x1815, _sn_automation       },
    { 0x1816, _sn_cycling_speed    },
    { 0x1818, _sn_cycling_power    },
    { 0x1819, _sn_location_nav     },
    { 0x181A, _sn_env_sensing      },
    { 0x181B, _sn_body_comp        },
    { 0x181C, _sn_user_data        },
    { 0x181D, _sn_weight_scale     },
    { 0x181E, _sn_bond_mgmt        },
    { 0x181F, _sn_cgm              },
    { 0x1820, _sn_ip_support       },
    { 0x1821, _sn_indoor_pos       },
    { 0x1822, _sn_http_proxy       },
    { 0x1823, _sn_http_proxy       },  // Pulse Oximeter uses same slot
    { 0x1824, _sn_transport        },
    { 0x1825, _sn_obj_transfer     },
    { 0x1826, _sn_fitness          },
    { 0x1827, _sn_mesh_prov        },
    { 0x1828, _sn_mesh_proxy       },
    { 0x1829, _sn_reconnect        },
    { 0x183A, _sn_insulin          },
    { 0x183B, _sn_binary_sensor    },
    { 0x183C, _sn_emergency        },
    { 0x183E, _sn_phys_activity    },
    { 0x1843, _sn_audio_input      },
    { 0x1844, _sn_volume_ctrl      },
    { 0x1845, _sn_vol_offset       },
    { 0x1846, _sn_coord_set        },
    { 0x1847, _sn_device_time      },
    { 0x1848, _sn_media_ctrl       },
    { 0x1849, _sn_gen_media        },
    { 0x184A, _sn_cte              },
    { 0x184B, _sn_telephone        },
    { 0x184C, _sn_gen_telephone    },
    { 0x184D, _sn_mic_ctrl         },
    { 0x184E, _sn_audio_stream     },
    { 0x184F, _sn_broadcast_scan   },
    { 0x1850, _sn_pub_audio_cap    },
    { 0x1851, _sn_basic_audio      },
    { 0x1852, _sn_broadcast_audio  },
    { 0x1853, _sn_common_audio     },
    { 0x1854, _sn_hearing_access   },
    { 0x1855, _sn_tmap             },
    { 0x1856, _sn_pub_broadcast    },
    { 0x1857, _sn_esl              },
    { 0x1858, _sn_gaming_audio     },
    { 0x1859, _sn_mesh_solicit     },
    // Vendor 16-bit UUIDs (assigned by BT SIG to specific companies)
    { 0xFD0D, _sn_blecon           },
    { 0xFD6F, _sn_exposure_notif   },
    { 0xFE0F, _sn_philips_hue     },
    { 0xFE2C, _sn_fast_pair        },
    { 0xFE59, _sn_dfu_secure       },
    { 0xFEAA, _sn_eddystone        },
    { 0xFEBB, _sn_adafruit_ft      },
};

static const int BLE_SERVICE_DB_SIZE = sizeof(bleServiceDB) / sizeof(bleServiceDB[0]);

// Binary search for service UUID name
static const char* lookupServiceName(uint16_t uuid) {
    int lo = 0, hi = BLE_SERVICE_DB_SIZE - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint16_t midUuid = pgm_read_word(&bleServiceDB[mid].uuid);
        if (midUuid == uuid) {
            return (const char*)pgm_read_ptr(&bleServiceDB[mid].name);
        } else if (midUuid < uuid) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════════
// SECTION 3: GAP APPEARANCE VALUES
// ═══════════════════════════════════════════════════════════════════════════════
// The appearance value from BLE advertising data tells you what the device
// CLAIMS to be. Category is bits [15:6], subcategory is bits [5:0].
// We store category-level names (52 entries). For subcategory detail,
// use lookupAppearanceDetail() which returns specific types like
// "Keyboard", "Smartwatch", "Earbud" etc.
// ═══════════════════════════════════════════════════════════════════════════════

struct BleAppearanceEntry {
    uint16_t    category;  // Upper 10 bits of appearance value
    const char* name;
};

static const char PROGMEM _ap_unknown[]       = "Unknown";
static const char PROGMEM _ap_phone[]         = "Phone";
static const char PROGMEM _ap_computer[]      = "Computer";
static const char PROGMEM _ap_watch[]         = "Watch";
static const char PROGMEM _ap_clock[]         = "Clock";
static const char PROGMEM _ap_display[]       = "Display";
static const char PROGMEM _ap_remote[]        = "Remote Ctrl";
static const char PROGMEM _ap_eyeglasses[]    = "Eye-glasses";
static const char PROGMEM _ap_tag[]           = "Tag";
static const char PROGMEM _ap_keyring[]       = "Keyring";
static const char PROGMEM _ap_media_player[]  = "Media Player";
static const char PROGMEM _ap_barcode[]       = "Barcode Scan";
static const char PROGMEM _ap_thermo[]        = "Thermometer";
static const char PROGMEM _ap_heart_rate[]    = "Heart Rate";
static const char PROGMEM _ap_blood_press[]   = "Blood Press";
static const char PROGMEM _ap_hid[]           = "HID";
static const char PROGMEM _ap_glucose[]       = "Glucose Meter";
static const char PROGMEM _ap_running[]       = "Run/Walk Sens";
static const char PROGMEM _ap_cycling[]       = "Cycling";
static const char PROGMEM _ap_control[]       = "Control Device";
static const char PROGMEM _ap_network[]       = "Network Device";
static const char PROGMEM _ap_sensor[]        = "Sensor";
static const char PROGMEM _ap_light_fix[]     = "Light Fixture";
static const char PROGMEM _ap_fan[]           = "Fan";
static const char PROGMEM _ap_hvac[]          = "HVAC";
static const char PROGMEM _ap_air_cond[]      = "Air Condition";
static const char PROGMEM _ap_humidifier[]    = "Humidifier";
static const char PROGMEM _ap_heating[]       = "Heating";
static const char PROGMEM _ap_access_ctrl[]   = "Access Control";
static const char PROGMEM _ap_motorized[]     = "Motorized Dev";
static const char PROGMEM _ap_power[]         = "Power Device";
static const char PROGMEM _ap_light_src[]     = "Light Source";
static const char PROGMEM _ap_window[]        = "Window Cover";
static const char PROGMEM _ap_audio_sink[]    = "Audio Sink";
static const char PROGMEM _ap_audio_src[]     = "Audio Source";
static const char PROGMEM _ap_vehicle[]       = "Vehicle";
static const char PROGMEM _ap_appliance[]     = "Appliance";
static const char PROGMEM _ap_wearable_aud[]  = "Wearable Audio";
static const char PROGMEM _ap_aircraft[]      = "Aircraft";
static const char PROGMEM _ap_av_equip[]      = "AV Equipment";
static const char PROGMEM _ap_display_eq[]    = "Display Equip";
static const char PROGMEM _ap_hearing_aid[]   = "Hearing Aid";
static const char PROGMEM _ap_gaming[]        = "Gaming";
static const char PROGMEM _ap_signage[]       = "Signage";
static const char PROGMEM _ap_pulse_ox[]      = "Pulse Oximeter";
static const char PROGMEM _ap_weight[]        = "Weight Scale";
static const char PROGMEM _ap_mobility[]      = "Mobility Dev";
static const char PROGMEM _ap_cgm[]           = "Cont Glucose";
static const char PROGMEM _ap_insulin[]       = "Insulin Pump";
static const char PROGMEM _ap_med_delivery[]  = "Med Delivery";
static const char PROGMEM _ap_spirometer[]    = "Spirometer";
static const char PROGMEM _ap_outdoor[]       = "Outdoor Sports";

// Sorted by category for binary search
static const BleAppearanceEntry PROGMEM bleAppearanceDB[] = {
    {  0, _ap_unknown       },
    {  1, _ap_phone         },
    {  2, _ap_computer      },
    {  3, _ap_watch         },
    {  4, _ap_clock         },
    {  5, _ap_display       },
    {  6, _ap_remote        },
    {  7, _ap_eyeglasses    },
    {  8, _ap_tag           },
    {  9, _ap_keyring       },
    { 10, _ap_media_player  },
    { 11, _ap_barcode       },
    { 12, _ap_thermo        },
    { 13, _ap_heart_rate    },
    { 14, _ap_blood_press   },
    { 15, _ap_hid           },
    { 16, _ap_glucose       },
    { 17, _ap_running       },
    { 18, _ap_cycling       },
    { 19, _ap_control       },
    { 20, _ap_network       },
    { 21, _ap_sensor        },
    { 22, _ap_light_fix     },
    { 23, _ap_fan           },
    { 24, _ap_hvac          },
    { 25, _ap_air_cond      },
    { 26, _ap_humidifier    },
    { 27, _ap_heating       },
    { 28, _ap_access_ctrl   },
    { 29, _ap_motorized     },
    { 30, _ap_power         },
    { 31, _ap_light_src     },
    { 32, _ap_window        },
    { 33, _ap_audio_sink    },
    { 34, _ap_audio_src     },
    { 35, _ap_vehicle       },
    { 36, _ap_appliance     },
    { 37, _ap_wearable_aud  },
    { 38, _ap_aircraft      },
    { 39, _ap_av_equip      },
    { 40, _ap_display_eq    },
    { 41, _ap_hearing_aid   },
    { 42, _ap_gaming        },
    { 43, _ap_signage       },
    { 49, _ap_pulse_ox      },
    { 50, _ap_weight        },
    { 51, _ap_mobility      },
    { 52, _ap_cgm           },
    { 53, _ap_insulin       },
    { 54, _ap_med_delivery  },
    { 55, _ap_spirometer    },
    { 81, _ap_outdoor       },
};

static const int BLE_APPEARANCE_DB_SIZE = sizeof(bleAppearanceDB) / sizeof(bleAppearanceDB[0]);

// Lookup appearance category name from raw appearance value
// Appearance format: [category (10 bits)][subcategory (6 bits)]
static const char* lookupAppearanceCategory(uint16_t appearance) {
    uint16_t category = appearance >> 6;
    int lo = 0, hi = BLE_APPEARANCE_DB_SIZE - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint16_t midCat = pgm_read_word(&bleAppearanceDB[mid].category);
        if (midCat == category) {
            return (const char*)pgm_read_ptr(&bleAppearanceDB[mid].name);
        } else if (midCat < category) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return nullptr;
}

// Get detailed appearance name for common subcategories.
// Returns specific type like "Keyboard", "Smartwatch", "Earbud" etc.
// Falls back to category name if subcategory not recognized.
static const char* lookupAppearanceDetail(uint16_t appearance) {
    // Check common subcategories that are useful for recon
    switch (appearance) {
        // Computer subtypes
        case 0x0081: return "Desktop";
        case 0x0083: return "Laptop";
        case 0x0087: return "Tablet";
        case 0x008D: return "IoT Gateway";
        // Watch subtypes
        case 0x00C1: return "Sports Watch";
        case 0x00C2: return "Smartwatch";
        // HID subtypes
        case 0x03C1: return "Keyboard";
        case 0x03C2: return "Mouse";
        case 0x03C3: return "Joystick";
        case 0x03C4: return "Gamepad";
        case 0x03C8: return "Barcode Scan";
        case 0x03CA: return "Presenter";
        // Network subtypes
        case 0x0501: return "Access Point";
        case 0x0502: return "Mesh Device";
        // Sensor subtypes
        case 0x0541: return "Motion Sensor";
        case 0x0543: return "Temp Sensor";
        case 0x0544: return "Humid Sensor";
        case 0x0545: return "Leak Sensor";
        case 0x0548: return "Contact Sens";
        case 0x054B: return "Light Sensor";
        // Access control subtypes
        case 0x0701: return "Access Door";
        case 0x0704: return "Access Lock";
        case 0x0708: return "Door Lock";
        case 0x0709: return "Locker";
        // Power subtypes
        case 0x0781: return "Power Outlet";
        case 0x0783: return "Smart Plug";
        case 0x0789: return "Power Bank";
        // Audio sink subtypes
        case 0x0841: return "Speaker";
        case 0x0842: return "Soundbar";
        case 0x0845: return "Speakerphone";
        // Vehicle subtypes
        case 0x08C1: return "Car";
        case 0x08C5: return "Scooter";
        // Appliance subtypes
        case 0x090C: return "Vacuum";
        case 0x090D: return "Robot Vacuum";
        // Wearable audio subtypes
        case 0x0941: return "Earbud";
        case 0x0942: return "Headset";
        case 0x0943: return "Headphones";
        case 0x0944: return "Neck Band";
        // Display equipment
        case 0x0A01: return "Television";
        case 0x0A02: return "Monitor";
        case 0x0A03: return "Projector";
        // Gaming subtypes
        case 0x0A81: return "Game Console";
        case 0x0A82: return "Handheld Game";
        default: break;
    }
    // Fall back to category name
    return lookupAppearanceCategory(appearance);
}

// ═══════════════════════════════════════════════════════════════════════════════
// CONVENIENCE: Extract company ID from raw manufacturer data
// ═══════════════════════════════════════════════════════════════════════════════

static uint16_t extractCompanyId(const uint8_t* mfgData, uint8_t len) {
    if (len < 2) return 0xFFFF;  // Invalid
    return mfgData[0] | (mfgData[1] << 8);  // Little-endian
}

// One-call: raw manufacturer data → company name string (or nullptr)
static const char* lookupCompanyFromMfgData(const uint8_t* mfgData, uint8_t len) {
    uint16_t id = extractCompanyId(mfgData, len);
    if (id == 0xFFFF) return nullptr;
    return lookupCompanyName(id);
}

#endif // BLE_DATABASE_H
