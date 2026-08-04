# UDX710 Tools

[中文文档](README_CN.md)

UDX710 Tools is a web management service for UDX710-based 5G CPE devices. The
backend is aarch64 C with oFono D-Bus integration; the frontend is Vue 3. The
server listens on port `6677` by default and serves the built frontend from the
`dist/` directory beside the executable.

This version adds a persisted modem profile for vendor-specific AT commands and
oFono paths, plus native SMS-to-email delivery. Platform behavior and a modem's
private AT commands are configured separately.

## Main Features

- oFono D-Bus modem control, SIM information, data contexts, SMS, APN, traffic,
  charging and airplane-mode control.
- Modem profile import/export for private AT commands, oFono paths, band and
  cell query strategies.
- Native SMS email forwarding with a durable queue, asynchronous SMTP worker,
  retry records, test delivery and logs.
- SMS webhook forwarding, IPv6 proxy, Rathole, plugin and script management.
- UDX710 external USB gadget mode management for CDC-NCM, CDC-ECM and RNDIS.
  This is not the same thing as a modem USB-composition AT command.

## Runtime Requirements

The target device needs Linux aarch64, oFono, the system D-Bus socket, and the
`sqlite3` command line utility. The automated deployment package includes a
target-compatible curl with `SMTP` and `SMTPS` enabled; manual deployments must
provide a curl with those protocols.

The application database is `6677.db` in the server working directory. Do not
edit it while the service is running. Use the web UI or API to change settings.

## Build And Deploy

### Build the frontend

```bash
cd web
npm ci
npm run build
```

### Cross-compile the backend

The repository contains the aarch64 GLib headers and libraries expected by
`src/Makefile`. Install an aarch64 GNU toolchain, then run:

```bash
cd src
make clean
make
```

The output is `src/build/ofono-server`.

### Deployment layout

The executable reads static files from `./dist`, so deploy these files together:

```text
/home/root/6677/
  server
  curl
  start.sh
  dist/
```

Example `start.sh`:

```sh
#!/bin/sh
set -eu
cd /home/root/6677
exec ./server "${PORT:-6677}"
```

```sh
chmod 755 /home/root/6677/server /home/root/6677/start.sh
/home/root/6677/start.sh
```

Open `http://DEVICE_IP:6677` in a browser.

## Modem Profile

The modem profile keeps vendor-specific behavior out of C source. It is stored
in the SQLite `config` table using keys prefixed with `modem_profile.` and is
loaded into memory once at startup. Runtime AT execution does not spawn SQLite
processes for profile lookups.

In **System Settings -> Modem Profile**, choose the FM650 preset, review it,
then click Save. The page exports a profile as JSON and imports it through
`POST /api/modem-profile`.

### Field Reference

| Field | Purpose |
| --- | --- |
| `default_modem_path` | Fallback oFono modem path. |
| `slot1_modem_path`, `slot2_modem_path` | oFono paths matched to SIM slots. |
| `default_context_path` | Fallback oFono data-context path. |
| `sms_cnmi_enabled`, `sms_cnmi_disabled` | Commands sent by the SMS receive-fix toggle. |
| `advanced_strategy` | `bitmask_matrix` for SPRD matrix output; `list_csv` for list/CSV output. |
| `band_set_lte`, `band_set_nr` | Band-set command templates. |
| `cell_*` | Cell query, lock and CSV column-mapping fields. |
| `imei_query`, `imei_set` | IMEI command templates. Empty `imei_set` disables IMEI writing. |
| `iccid_query`, `imsi_query`, `airplane_query` | ICCID, IMSI, and airplane-mode query commands. Leave empty to disable collection. |
| `qos_query` | QCI and negotiated-rate query command. Leave empty to disable QoS collection. |
| `qos_response_prefix` | Prefix before the comma-separated QoS response values, such as `+CGEQOSRDP:`. |
| `qos_qci_index`, `qos_downlink_index`, `qos_uplink_index` | Zero-based QCI, downlink, and uplink field positions in the QoS response. |

Template validation rules:

- `bitmask_matrix`: LTE and NR band templates need two `%d`; `cell_lock`
  needs three `%s`.
- `list_csv`: LTE and NR band templates need one `%s`; LTE and NR cell-lock
  templates need two `%s` each.
- Other command fields must start with `AT` and cannot contain a newline or a
  `%` placeholder.

### FM650 Profile Example

This matches the current FM650 preset. It is a starting point, not a replacement
for device testing. Capture real `AT+GTCCINFO?` output before relying on the CSV
column numbers.

```json
{
  "name": "Fibocom FM650 / oFono",
  "default_modem_path": "/ril_0",
  "slot1_modem_path": "/ril_0",
  "slot2_modem_path": "/ril_1",
  "default_context_path": "/ril_0/context2",
  "advanced_network_enabled": true,
  "advanced_strategy": "list_csv",
  "lte_band_offset": 100,
  "nr_band_prefix": "50",
  "cell_lte_rat": 4,
  "cell_nr_rat": 9,
  "cell_serving_value": 1,
  "cell_arfcn_column": 6,
  "cell_pci_column": 7,
  "cell_band_column": 8,
  "cell_sinr_column": 10,
  "cell_rsrp_column": 12,
  "cell_rsrq_column": 13,
  "sms_cnmi_enabled": "AT+CNMI=2,1,0,0,0",
  "sms_cnmi_disabled": "AT+CNMI=2,0,0,0,0",
  "band_query_lte": "AT+GTACT?",
  "band_query_nr": "AT+GTACT?",
  "radio_off": "AT+CFUN=0",
  "radio_on": "AT+CFUN=1",
  "pdp_reactivate": "AT+CGACT=1,1",
  "band_reset_lte": "AT+GTACT=2,3,0",
  "band_reset_nr": "AT+GTACT=14,6,0",
  "band_set_lte": "AT+GTACT=2,3,%s",
  "band_set_nr": "AT+GTACT=14,6,%s",
  "cell_lte_serving": "AT+GTCCINFO?",
  "cell_lte_neighbor": "AT+GTCCINFO?",
  "cell_nr_serving": "AT+GTCCINFO?",
  "cell_nr_neighbor": "AT+GTCCINFO?",
  "cell_unlock_lte": "AT+GTCELLLOCK=0",
  "cell_unlock_nr": "AT+GTCELLLOCK=0",
  "cell_lock": "",
  "cell_lock_lte": "AT+GTCELLLOCK=2,0,0,%s,%s",
  "cell_lock_nr": "AT+GTCELLLOCK=2,1,0,%s,%s",
  "imei_query": "AT+CGSN",
  "imei_set": "",
  "iccid_query": "AT+CCID",
  "imsi_query": "AT+CIMI",
  "airplane_query": "AT+CFUN?",
  "qos_query": "AT+CGEQOSRDP",
  "qos_response_prefix": "+CGEQOSRDP:",
  "qos_qci_index": 1,
  "qos_downlink_index": 6,
  "qos_uplink_index": 7
}
```

Before enabling FM650 advanced network operations, retain these target-device
responses:

```text
AT+GTACT?
AT+GTACT=?
AT+GTCCINFO?
AT+GTCELLLOCK=?
```

`AT+GTUSBMODE` is intentionally not part of the UDX710 USB gadget setting. It
changes the FM650's own USB composition, uses profiles such as `34` through
`41`, and takes effect only after module reset or power cycle.

## SMS Email Forwarding

Configure this in **SMS -> Forwarding -> SMS Email Forwarding**. Configuration
is stored in SQLite, not in a plugin file. The password is not returned by the
read API; leaving the password blank when saving preserves the existing one.

| Field | Example | Notes |
| --- | --- | --- |
| SMTP server | `smtp.qq.com` | Host name only, without `https://`. |
| Port | `465` | 465 uses implicit TLS. Other ports use STARTTLS. |
| Login account | `name@qq.com` | SMTP account. |
| Password | SMTP authorization code | QQ and 163 normally require an authorization code. |
| From address | `name@qq.com` | Usually the SMTP account address. |
| To address | `ops@example.com` | One recipient per current configuration. |

When enabled, an incoming SMS is saved first and then added to
`sms_email_queue`. One worker sends it with `curl`; failed deliveries retry up
to five times with increasing delays. The log view reads `sms_email_log`.
Use **Send Test Email** after saving the configuration.

## API Summary

All APIs except login/status require the configured Bearer token.

| Endpoint | Method | Description |
| --- | --- | --- |
| `/api/modem-profile` | GET | Read the current modem profile. |
| `/api/modem-profile` | POST | Validate, persist and activate a profile. |
| `/api/modem-profile/reset` | POST | Restore the UDX710 SPRD default profile. |
| `/api/sms/email` | GET/POST | Read or save SMTP forwarding configuration. |
| `/api/sms/email/test` | POST | Queue a test email. |
| `/api/sms/email/logs?lines=10` | GET | Read recent delivery logs. |
| `/api/sms/webhook` | GET/POST | Configure SMS webhook forwarding. |
| `/api/usb/mode` | GET/POST | UDX710 external USB gadget configuration. |

## GitHub Actions

`.github/workflows/build.yml` builds the frontend and aarch64 backend on every
push, pull request and manual dispatch. It uploads a compressed deployment
package plus a SHA-256 checksum as an Actions artifact.

## Verification Notes

- `npm run build` validates the Vue application.
- The backend must be built with an aarch64 toolchain because it links the
  target GLib/GIO libraries included in this repository.
- Test the oFono paths, private AT response formats and `curl` SMTP support on
  each modem/device combination before production use.

## License

GPL-3.0. See [LICENSE](LICENSE).
