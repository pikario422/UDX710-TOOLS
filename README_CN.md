# UDX710 Tools

[English](README.md)

UDX710 Tools 是运行在 UDX710（展锐）5G CPE 设备上的 Web 管理服务。后端为
aarch64 C 程序，通过 oFono D-Bus 控制模组；前端使用 Vue 3。默认监听端口为
`6677`，静态页面从服务程序同目录的 `dist/` 提供。

本版本将模组厂商相关的 AT 指令和 oFono 路径提取为可持久化的模组配置，并加入
原生短信转邮件功能。UDX710 平台能力与模组私有 AT 指令相互独立，替换同 CPU 平台
上的不同厂商模组时无需修改 C 源码。

## 主要功能

- 通过 oFono D-Bus 管理模组、SIM 卡、数据上下文、短信和 APN。
- 通过系统设置导入、导出和保存模组配置，支持不同厂商 AT 指令和 oFono 路径。
- 支持频段查询、锁频段、查小区、锁小区等高级网络功能，并按模组配置选择解析策略。
- 短信 Webhook 转发和原生 SMTP 邮件转发。邮件使用独立线程和持久化队列，不阻塞
  D-Bus 短信接收回调。
- 流量统计、充电控制、飞行模式、IPv6 代理、Rathole、插件和脚本管理。
- UDX710 外部 USB Gadget 模式管理，支持 CDC-NCM、CDC-ECM、RNDIS。

> USB Gadget 模式不是模组的 USB 组合模式。FM650 的 `AT+GTUSBMODE` 仅修改
> FM650 自身的 USB 组合，配置值如 `34` 到 `41` 需要模组重启或断电后生效，不能和
> 本项目的 USB 网络模式开关混用。

## 运行环境

目标设备需要：

- Linux aarch64。
- 正常工作的 oFono 和系统 D-Bus Socket。
- `sqlite3` 命令行工具。
- `curl`。使用短信邮件功能时，执行 `curl -V` 的输出必须包含 `SMTP` 和 `SMTPS`。

数据库默认是服务工作目录中的 `6677.db`。服务运行时不要直接修改数据库，应通过
Web 设置页或 API 修改配置。

## 构建与部署

### 构建前端

```bash
cd web
npm ci
npm run build
```

### 交叉编译后端

仓库内已包含 `src/Makefile` 所需的 aarch64 GLib/GIO 头文件和库。安装
`aarch64-linux-gnu` 工具链后执行：

```bash
cd src
make clean
make
```

输出文件为 `src/build/ofono-server`。

### 部署目录

服务程序从 `./dist` 加载前端资源，以下文件必须一起部署：

```text
/home/root/6677/
  server
  start.sh
  dist/
```

`start.sh` 示例：

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

浏览器访问 `http://设备IP:6677`。

## 模组配置

在 **系统设置 -> 模组配置** 中可选择预设、编辑、保存、导入和导出 JSON 配置。
配置存储在 SQLite `config` 表中，键名前缀为 `modem_profile.`。服务启动后会加载到
内存，运行 AT 指令时不会为了读取配置而启动 SQLite 进程。

### 字段说明

| 字段 | 说明 |
| --- | --- |
| `name` | 配置名称，用于识别模组厂商和型号。 |
| `default_modem_path` | oFono 默认模组路径。 |
| `slot1_modem_path`、`slot2_modem_path` | SIM 卡槽对应的 oFono 模组路径。 |
| `default_context_path` | 默认 oFono 数据上下文路径。 |
| `sms_cnmi_enabled`、`sms_cnmi_disabled` | 短信接收修复开关使用的 CNMI 命令。 |
| `advanced_network_enabled` | 是否允许使用高级网络页面的频段和小区操作。 |
| `advanced_strategy` | `bitmask_matrix` 为展锐掩码矩阵格式，`list_csv` 为列表/CSV 格式。 |
| `band_query_lte`、`band_query_nr` | LTE、NR 频段查询命令。 |
| `band_reset_lte`、`band_reset_nr` | 恢复 LTE、NR 频段默认值的命令。 |
| `band_set_lte`、`band_set_nr` | 设置频段命令模板。 |
| `radio_off`、`radio_on`、`pdp_reactivate` | 操作频段或小区前后的射频和数据上下文控制命令。 |
| `cell_lte_*`、`cell_nr_*` | LTE/NR 服务小区、邻区、小区锁定和解锁相关命令。 |
| `cell_*_column` | `list_csv` 策略中 ARFCN、PCI、频段和信号值的 CSV 列编号。 |
| `imei_query`、`imei_set` | IMEI 查询和写入命令。空的 `imei_set` 表示禁用 IMEI 写入。 |

### 命令模板校验规则

- 所有普通命令必须以 `AT` 开头，不能含换行或 `%` 占位符。
- `bitmask_matrix`：`band_set_lte` 和 `band_set_nr` 各必须有两个 `%d`；
  `cell_lock` 必须有三个 `%s`。
- `list_csv`：`band_set_lte` 和 `band_set_nr` 各必须有一个 `%s`；
  `cell_lock_lte` 和 `cell_lock_nr` 各必须有两个 `%s`。

### FM650 配置示例

以下配置对应当前的 Fibocom FM650 预设。它是适配起点，实际设备使用前应根据真实
命令返回调整 CSV 列编号和锁小区命令参数。

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
  "imei_set": ""
}
```

启用 FM650 高级网络操作前，建议先在目标设备的 AT 调试页保存下列命令的真实返回：

```text
AT+GTACT?
AT+GTACT=?
AT+GTCCINFO?
AT+GTCELLLOCK=?
```

## 短信转邮件

在 **短信 -> 转发 -> 短信邮件转发** 中配置。配置保存在 SQLite，不依赖插件文件。
读取接口不会返回 SMTP 密码；保存时将密码字段留空，会保留已有密码。

| 字段 | 示例 | 说明 |
| --- | --- | --- |
| SMTP 服务器 | `smtp.qq.com` | 仅填写主机名，不要填写 `https://`。 |
| 端口 | `465` | 465 使用隐式 TLS，其他端口使用 STARTTLS。 |
| 登录账号 | `name@qq.com` | SMTP 登录账号。 |
| 密码 | SMTP 授权码 | QQ、163 等邮箱通常需要授权码。 |
| 发件地址 | `name@qq.com` | 通常与 SMTP 登录账号一致。 |
| 收件地址 | `ops@example.com` | 当前版本一个配置对应一个收件人。 |

启用后，收到短信会先写入短信表，再加入 `sms_email_queue`。独立 worker 使用 `curl`
投递邮件；失败最多重试五次，并按递增延迟再次投递。投递记录在 `sms_email_log` 中，
保存配置后可使用 **发送测试邮件** 验证。

## API 摘要

除登录和状态接口外，接口需要携带已配置的 Bearer Token。

| 接口 | 方法 | 说明 |
| --- | --- | --- |
| `/api/modem-profile` | GET | 读取当前模组配置。 |
| `/api/modem-profile` | POST | 校验、保存并立即启用模组配置。 |
| `/api/modem-profile/reset` | POST | 恢复 UDX710 展锐默认配置。 |
| `/api/sms/email` | GET/POST | 获取或保存 SMTP 转发配置。 |
| `/api/sms/email/test` | POST | 将测试邮件加入投递队列。 |
| `/api/sms/email/logs?lines=10` | GET | 读取最近的邮件投递记录。 |
| `/api/sms/webhook` | GET/POST | 配置短信 Webhook 转发。 |
| `/api/usb/mode` | GET/POST | 管理 UDX710 外部 USB Gadget 模式。 |

## GitHub Actions 自动构建

`.github/workflows/build.yml` 会在 `push`、`pull_request` 和手动触发时执行：

1. 使用 Node.js 20 和 `npm ci` 构建前端。
2. 安装 `gcc-aarch64-linux-gnu` 与 `binutils-aarch64-linux-gnu`，交叉编译后端。
3. 通过 `readelf` 检查生成程序是否为 AArch64 ELF。
4. 生成 `UDX710-TOOLS-提交SHA.tar.gz` 和对应 SHA-256 文件，并作为 Actions Artifact
   保留 30 天。

下载 Artifact 后解压，得到可部署的 `6677/` 目录。

## 验证说明

- `npm run build` 已用于验证 Vue 前端构建。
- 后端需要 aarch64 交叉编译器，因为它链接的是仓库中携带的目标设备 GLib/GIO 库。
- 每种模组都应在真实设备上确认 oFono 路径、私有 AT 返回格式和 `curl` 的 SMTP 支持。

## 许可证

GPL-3.0，详见 [LICENSE](LICENSE)。
