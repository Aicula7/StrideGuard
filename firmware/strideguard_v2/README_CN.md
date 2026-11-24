# StrideGuard 鞋上无线步行采集版（V2）

连接：脚端 XIAO ESP32-C3 + LSM6DS3TR-C（电池） → BLE → 腕端 Feather ESP32-S3 + DRV2605L（USB 接 Windows 电脑）。不需要手机 App。

## 1. 烧录与文件位置

首次从旧版升级时，两块板子都需要 V2。已经烧录 V2 的脚端可直接配合本包新版腕端。新版腕端的开机日志含 `build=V2_BLE_FIX`。

分别打开对应文件夹里的同名 .ino：

- `foot_gait_monitor/foot_gait_monitor.ino`：烧到脚端 **XIAO ESP32C3**。
- `wrist_gait_monitor/wrist_gait_monitor.ino`：烧到腕端 **Adafruit Feather ESP32-S3 No PSRAM**（若实物型号不同，以实际型号为准）。
- .h 文件留在所属 .ino 同一个文件夹中，名称不要加 `(1)`。两处 `GaitEventProtocol.h` 完全一致。
- Arduino IDE 依赖：ESP32 板卡包、Adafruit LSM6DS、Adafruit Unified Sensor、Adafruit BusIO、Adafruit DRV2605。BLE/FreeRTOS 使用 ESP32 板卡包自带实现；不要另装同名 ArduinoBLE 库。
- 沿用之前已成功编译时的板卡包版本和板卡设置。此包未在本环境完成真实 ESP32 板卡编译/烧录，请先在 Arduino IDE 点 Verify；如报错，保留完整错误输出。
- USB CDC 配置沿用之前能看到串口输出的配置。脚端不等待电脑连接，可电池启动。

## 2. 先固定盒子、核对传感器方向

固定在鞋带/脚背，板子在盒内也不能移动。每次保持相同位置、同一只脚、同样朝向。

**默认立即可以做节奏和冲击测试。侧翻检测默认关闭，因为还不知道你的板子怎么装。**

侧翻的旋转轴必须是传感器上**沿着鞋的前后方向（脚跟到脚尖）**的轴。根据 IMU 小板轴标识/数据手册确定是 x/y/z；不是看到哪个数变化最大就直接选。坐着轻轻左右转脚，可结合三轴陀螺仪数据检查。不要故意崴脚。

确定后有两种设置方式：

1. 测试时在**腕端**串口输入对应 `x`、`y` 或 `z`。脚端远程确认该轴、清空 baseline 并重新校准，本次供电期间有效。
2. 长期固定朝向：修改脚端 `WalkingConfig.h` 的 `DefaultRollAxis`，将 `DefaultRollAxisConfirmed` 改为 `true`，重新烧录脚端。之后电池开机自动启用侧翻检测。

未确认时仍记录三轴数据、选定占位轴的旋转值，但不会发侧翻报警。时间和冲击检测照常工作。

## 3. 第一次测试顺序

1. 先给腕端接电脑 USB，打开腕端串口，波特率 **115200**。
2. 脚端装好固定后接电池，上电自动广播。腕端找到后输出 `# LINK FOOT_CONNECTED`。
3. 初次连接/重连后静止约 **3 秒**。`# STATUS ... acquisition=RUNNING` 后可正常直线走。
4. 输入 `r`（发送给腕端即可）清除可能在安装时采到的 baseline；等 `# CONTROL ACK command=r accepted=1`。
5. 正常直线行走到 `# BASELINE READY`。步行模式收集 **20 个有效同脚 interval**，通常至少 21 次检测到的同脚落地，约 40 次左右脚交替落地；拒收/漏检会延长时间。
6. 校准期间保持自然、稳定速度，不故意重踩、不转弯。校准完成后另走 30–50 步，检查漏检、重复检测及误报。
7. 再分别测慢走、快走、停走、转弯。转弯先单独标记，不纳入直线 baseline。

断开 BLE 时采集/判断暂停，baseline 保留；重连静止 3 秒后自动继续。断电会清空 baseline、暂停状态和远程轴设置（回到代码默认值）。只有一个脚端能被该腕端使用；附近多个同名设备时需先关掉其他脚端。

`p` 暂停的是落脚/报警判断，原始 IMU 数据仍发送，便于检查安装方向；`g` 继续时不会把暂停前后的动作拼成一个 interval。

## 4. 在腕端串口输入的指令

| 指令 | 意义 |
|---|---|
| `r` | 清空 baseline，重新校准，保留当前运行/暂停状态 |
| `p` / `g` | 暂停 / 继续检测，保留 baseline |
| `x` / `y` / `z` | 确认侧翻轴、开启侧翻检测，并清空 baseline |
| `n` | 关闭侧翻检测，并清空 baseline |
| `w` | 步行模式，20 intervals；每次都会清空 baseline |
| `d` | 手持模式，8 intervals；每次都会清空 baseline |
| `s` | 请求状态和已建立的 baseline |
| `t` | 切换电脑端原始 DATA 行显示；关闭后记录器也收不到 DATA 行，蓝牙仍传输 |
| `v` | 腕端振动测试，3 下 |
| `h` | 显示帮助与数据列名称 |

远程指令会先显示 SENT，收到 ACK 才表示脚端应用成功。没有连上时指令不会发送，也不会排队在未来执行。

新版腕端每次重连会输出 `GATT_CHECK`。`data=READY control=READY` 表示两个接口都已找到。腕端等到收到有效 V2 状态后才输出 `FOOT_CONNECTED protocol=V2`，再等 `acquisition=RUNNING` 开始走路。发现接口异常时，Bluedroid 会尝试刷新缓存并重连一次。如果最终显示 `control=USB_ONLY`，数据和报警仍可接收，脚端模式和轴设置需从脚端 USB 串口发送。脚端重启会恢复默认轴设置。如果出现 `DATA_CHARACTERISTIC_UNAVAILABLE` 或 `NO_VALID_V2_SESSION`，保留第一条 `GATT_CHECK` 和后续日志用于定位，暂缓走路采集。

## 5. 输出逐项说明

输出为英文标记，便于截图汇报和后续处理。所有 `foot_ms` 均为**脚端开机后的毫秒数**；日志接收顺序可能不同于时间顺序（报警优先）。跨脚端重启请用 `foot_boot` 区分；电脑接收时间不能用来代替步间隔。

### 原始 IMU 数据

`DATA,foot_ms,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps`

- `ax_g/ay_g/az_g`：传感器三轴加速度，单位 g，**包含重力**，未旋转到人体坐标系。
- `gx_dps/gy_dps/gz_dps`：三轴角速度，单位度/秒。
- 检测器约 100 Hz，发送约 50 Hz 原始数据（并非完整 100 Hz 原始记录），加速度量化到 0.001 g，角速度到 0.1 度/秒。无线波形可能没采到每个冲击峰值，应结合 CONTACT 的内部峰值。

### 每次候选落脚：`# CONTACT`

| 字段 | 意义 |
|---|---|
| `type` | 本次候选落脚的主要分类，见下表 |
| `stride_ms` / `interval_valid` | 同脚落脚间隔；无效/不可用时为 0，不能当作真实 0 ms |
| `peak_g` | 落脚后窗口内的合加速度峰值，包含重力 |
| `roll_peak_dps` / `roll_angle_deg` | 所选轴在落脚附近的角速度峰值及积分净角度；正负号由安装方向决定 |
| `baseline=数目/目标` | 已接受的 baseline interval 数量 |
| `baseline_sample_accepted` | 本次是否向 baseline 加入一个样本 |
| `timing_outliers=异常数/窗口数` | 最新最多 5 个有效 interval 中偏离数量；不是异常次数百分比 |
| `roll_enabled` | 是否已确认轴并启用侧翻检测 |
| `flags` | 可同时存在多个标记；ALERT 行会分别说明触发的异常 |

| `type` 分类 | 中文解释 |
|---|---|
| `CALIBRATING` | 正在学习正常步态 |
| `WITHIN_BASELINE` | 当前有效 interval 在个人时间范围内，且没有本次冲击/侧翻标记 |
| `TIMING_OUTLIER_NOT_YET_PERSISTENT` | 单次节奏偏离，尚未满足持续报警条件 |
| `TIMING_DEVIATION_TRIGGERED` | 此次触发持续节奏异常报警 |
| `TIMING_ALERT_ACTIVE` | 节奏报警状态尚未清除；需连续 5 个正常有效 interval 才恢复 |
| `HARD_LANDING` | 冲击峰值达到个人阈值 |
| `SUSPECTED_SUDDEN_ROLL` | 落脚附近快速侧向旋转，属于运动异常线索，不代表确诊崴脚 |
| `INTERVAL_UNAVAILABLE_OR_REJECTED` | 第一次落脚/暂停后第一步/间隔超界等，不参与当前时间判断 |
| `SENSOR_CLIPPED_REVIEW_REQUIRED` | 传感器接近量程，数据可能截断，此次不参与 baseline/异常判断 |

### 明确报警：`# ALERT type=...`

| 异常类型 | 触发规则（步行模式） | 腕端振动 |
|---|---|---|
| `TIMING_DEVIATION` | 最新一个 interval 超范围，最近 5 个中至少 3 个超范围；进入报警时通知一次 | 1 下 |
| `HARD_LANDING` | 冲击峰值 ≥ max(baseline 冲击中位数 + 0.35 g，中位数 × 1.35) | 2 下 |
| `SUSPECTED_SUDDEN_ROLL` | 所选轴落脚前 40 ms 至落脚后 100 ms 内，至少两次同向样本达到 250 度/秒，同时净角度达到 12 度 | 3 下 |

振动优先级：侧翻 > 重落脚 > 节奏，重叠时低优先级可能只记录不振动。若 `haptic=UNAVAILABLE_SERIAL_ONLY`，实际没有振动。`v` 可独立测试电机。

`# BASELINE READY` 显示个人 interval 中心值、容忍区间、冲击阈值。时间容忍为 max(100 ms，中心值 × 15%，4.4478 × MAD)。有效同脚 interval 为 450–1800 ms；太慢、停走或漏检可能直接拒收，**并不一定报警**。正常抬脚/摆动也可能被阈值法当作落脚，需用视频对照验证。

侧翻判定优先，发生时跳过该次时间/冲击判定；量程截断也会跳过判定。当前侧翻检测仅覆盖落脚附近窗口，并非随时的任意侧翻。阈值是原型参数，尚未用这次真实步行数据验证。

### 状态和数据质量

- `# STATUS`：约每秒一次，运行/暂停、校准状态、轴是否确认。
- `# QUALITY RAW_STREAM_GAP`：相邻收到的脚端原始时间戳相差明显大于 20 ms；可能采样调度或无线/接收掉点。
- `FOOT_COUNTERS sample_gaps`：检测器采样间隔超过 25 ms，清空尚未完成的时间证据；已建立的 baseline 保留。
- `invalid_imu_samples`：IMU 读取失败或非有限数。
- `tx_queue_drops` / `WRIST_DROPS`：发送端/接收端队列溢出；无线通知没有端到端确认，本版也不补发丢失数据。
- `MISSED_ALERTS`：报警序号出现缺口。
- `NO_FOOT_DATA_OVER_3_SECONDS`：已显示连接但持续没有有效数据，检查脚端电池、IMU、蓝牙和固件版本。
- 没有报警不代表设备一定工作正常；先确认 DATA、STATUS、CONTACT 都有持续输出。

## 6. Windows 保存数据（推荐）

只看串口窗口不会自动存文件。录制时**关闭 Arduino Serial Monitor/Plotter**，同一个 COM 口只能由一个程序使用。

在本包 `pc_logger` 文件夹打开 PowerShell：

```powershell
python -m pip install pyserial
python record_walk.py --list
python record_walk.py --port COM7
```

将 COM7 换成**腕端**的真实端口。终端仍可输入 r、p、g、x/y/z 等指令，再回车。原始数据默认写文件，不刷屏；报警、校准、落脚和状态会显示在终端。按 Ctrl+C 结束。

自动生成新的 `walk_日期_时间` 文件夹：

- `imu.csv`：电脑 UTC 时间、脚端 session、脚端时间戳、六轴数据。
- `events.csv`：全部状态、落脚、异常、控制响应。
- `serial_log.txt`：完整串口输出和电脑发送的指令。

建议每次重新上电/重新安装开始一个新录制。先记录自然直线走路，再单独录制速度变化和转弯。测试人、鞋、安装照片、路线、动作和大致起止时间另做标记；手机录视频对照实际落地次数。

## 7. 本包验证范围

包含检测器/协议/记录器的主机测试和 Arduino API 模拟下的 C++ 编译检查。它们不验证真实 ESP32 板卡库兼容性、BLE 吞吐、电机、电池、安装朝向或真实步态准确率。先 Verify 两套程序，再做短距离测试，关注 QUALITY 输出。
