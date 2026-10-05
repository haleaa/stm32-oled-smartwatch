<template>
  <view class="container">

    <!-- ===== 定位栏 ===== -->
    <view class="card location-bar">
      <text class="label">当前城市：</text>
      <text class="city-name">{{ locationName }}</text>
      <button size="mini" type="default" @click="autoLocate" :disabled="locating">
        {{ locating ? '定位中...' : '重新定位' }}
      </button>
    </view>

    <!-- ===== 蓝牙状态栏 ===== -->
    <view class="card status-bar">
      <text class="label">蓝牙状态：</text>
      <text :class="['status-text', connected ? 'connected' : 'disconnected']">
        {{ connected ? '已连接' : (reconnecting ? '自动重连中...' : '未连接') }}
      </text>
      <text v-if="connectedDeviceName" class="device-name">{{ connectedDeviceName }}</text>
      <button v-if="connected" size="mini" type="warn" @click="disconnect" class="btn-mini">断开</button>
    </view>

    <!-- ===== 健康数据显示 ===== -->
    <view class="card health-panel">
      <text class="section-title">健康数据（来自蓝牙设备）</text>
      <view class="health-row">
        <view class="health-item">
          <text class="health-value">{{ healthData.step }}</text>
          <text class="health-label">步数</text>
        </view>
        <view class="health-item">
          <text class="health-value">{{ healthData.hr }}<text class="unit"> bpm</text></text>
          <text class="health-label">心率</text>
        </view>
        <view class="health-item">
          <text class="health-value">{{ healthData.spo2 }}<text class="unit"> %</text></text>
          <text class="health-label">血氧</text>
        </view>
      </view>
      <text class="health-update">最近更新：{{ healthData.lastUpdate || '——' }}</text>
    </view>

    <!-- ===== 扫描控制 ===== -->
    <view class="card">
      <button type="primary" @click="startScan" :disabled="scanning">开始扫描</button>
      <button v-if="scanning" type="default" @click="stopScan" class="mt10">停止扫描</button>

      <view class="device-list" v-if="deviceList.length > 0">
        <view v-for="(dev, idx) in deviceList" :key="idx" class="device-item"
              @click="connectDevice(dev.deviceId, dev.name)">
          <view class="device-info">
            <text class="device-name">{{ dev.name }}</text>
            <text class="device-id">{{ dev.deviceId }}</text>
          </view>
          <text class="rssi">{{ dev.RSSI }} dBm</text>
        </view>
      </view>
      <text v-else-if="scanning" class="tip">搜索中...</text>
    </view>

    <!-- ===== 发送控制 ===== -->
    <view class="card">
      <button type="primary" @click="sendAllPackets" :disabled="!connected">手动发送全部数据包</button>

      <view class="row mt10">
        <text>定时发送：</text>
        <switch :checked="autoSend" @change="toggleAutoSend" />
        <text v-if="autoSend">间隔(秒)：</text>
        <input v-if="autoSend" type="number" v-model="intervalSec" class="interval-input" />
      </view>
    </view>

    <!-- ===== 日志 ===== -->
    <view class="card">
      <text class="section-title">日志（最近50条）</text>
      <scroll-view scroll-y class="log-scroll">
        <view v-for="(log, idx) in logs" :key="idx" class="log-item">
          <text class="log-time">{{ log.time }}</text>
          <text class="log-content">{{ log.content }}</text>
        </view>
        <text v-if="logs.length === 0" class="tip">暂无日志</text>
      </scroll-view>
    </view>

  </view>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue';

// ============================================================
//  ★★★ 用户配置区（开源/分享前必须修改以下两项！）★★★
// ============================================================
// 【重要】天气服务使用和风天气开放平台，API Key 属于个人私密凭证：
//   1. 登录 https://dev.qweather.com/ 控制台，创建项目并获取你自己的 API Key；
//   2. 将下方 YOUR_QWEATHER_API_KEY 替换为你的 Key（请勿把真实 Key 提交到公开仓库）；
//   3. 将 QWEATHER_HOST 替换为控制台分配给你的“专属 API Host”，
//      形如 xxxxxxxx.re.qweatherapi.com；免费开发版也可填写 devapi.qweather.com；
//   4. 修改完成后重新使用 HBuilderX 打包，源码和新 APK 才可正常获取天气。
const QWEATHER_KEY  = 'YOUR_QWEATHER_API_KEY';                   // TODO: 必填，替换为你自己的和风天气 API Key
const QWEATHER_HOST = 'YOUR_PRIVATE_HOST.re.qweatherapi.com';    // TODO: 必填，替换为你的专属 API Host

const DEFAULT_LOCATION_ID   = '101010100';
const DEFAULT_LOCATION_NAME = '北京';

// ============================================================
//  BLE 固定参数
// ============================================================
const SERVICE_UUID    = '0000FFE0-0000-1000-8000-00805F9B34FB';
const WRITE_CHAR_UUID = '0000FFE1-0000-1000-8000-00805F9B34FB';
const TARGET_MTU      = 247;

// 本地存储键名
const STORAGE_KEY_LAST_DEVICE = 'last_ble_device';

// ============================================================
//  响应式状态
// ============================================================
const LOCATION       = ref(DEFAULT_LOCATION_ID);
const locationName   = ref(DEFAULT_LOCATION_NAME);
const locating       = ref(false);
const hasLocatedOnce = ref(false);
const pendingAutoSend = ref(false);

const scanning            = ref(false);
const deviceList          = ref([]);
const connected           = ref(false);
const connectedDeviceId   = ref('');
const connectedDeviceName = ref('');
const serviceId           = ref('');
const characteristicId    = ref('');
const notifyCharId        = ref('');
const currentMtu          = ref(TARGET_MTU);
const reconnecting        = ref(false);   // ★ 是否正在自动重连

// 健康数据
const healthData = ref({
  step: '--',
  hr: '--',
  spo2: '--',
  lastUpdate: ''
});

// 日志 & 定时
const logs        = ref([]);
const autoSend    = ref(false);
const intervalSec = ref(30);
let   timerId     = null;

// 接收缓冲区
let rxBuffer = '';

// ============================================================
//  工具函数
// ============================================================

function calcXOR(str) {
  let xor = 0;
  for (let i = 0; i < str.length; i++) {
    xor ^= str.charCodeAt(i);
  }
  return xor.toString(16).toUpperCase().padStart(2, '0');
}

function str2ab(str) {
  const bytes = new Uint8Array(str.length);
  for (let i = 0; i < str.length; i++) {
    bytes[i] = str.charCodeAt(i) & 0xFF;
  }
  return bytes.buffer;
}

function ab2str(buffer) {
  const bytes = new Uint8Array(buffer);
  let str = '';
  for (let i = 0; i < bytes.length; i++) {
    str += String.fromCharCode(bytes[i]);
  }
  return str;
}

function addLog(content) {
  const now = new Date();
  const pad = n => String(n).padStart(2, '0');
  const time = `${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`;
  logs.value.unshift({ time, content });
  if (logs.value.length > 50) logs.value.pop();
}

// ============================================================
//  ★★★ 天气文字/图标 → 英文大写（方案 B + 组合兜底） ★★★
// ============================================================
/**
 * 优先级：
 *   1. 如果 icon 有效 → 按和风 icon 代码映射（最准确、不受文字变动影响）
 *   2. icon 无效 → 退回文字模糊匹配
 * @param {string} text  中文天气（如"小雨"）
 * @param {string|number} icon  和风天气 icon 代码（如"305"）
 */
function weatherToEn(text, icon) {
  // ---------- 第一层：icon 映射 ----------
  if (icon !== undefined && icon !== null && icon !== '') {
    const n = parseInt(icon);
    if (!isNaN(n)) {
      // 100=晴，150=夜间晴
      if (n === 100 || n === 150) return 'SUNNY';
      // 101~103=多云/少云/晴间多云，151~153=夜间版本
      if (n >= 101 && n <= 103) return 'CLOUDY';
      if (n >= 151 && n <= 153) return 'CLOUDY';
      // 104=阴，154=夜间阴
      if (n === 104 || n === 154) return 'OVERCAST';
      // 300~399 = 各种雨（阵雨、雷阵雨、冻雨、暴雨等）
      if (n >= 300 && n <= 399) return 'RAINY';
      // 400~499 = 各种雪（含雨夹雪）
      if (n >= 400 && n <= 499) return 'SNOWY';
      // 500~515 = 雾/霾/沙尘/浮尘/扬沙等 → 归阴
      if (n >= 500 && n <= 515) return 'OVERCAST';
    }
  }

  // ---------- 第二层：文字模糊匹配兜底 ----------
  if (!text) return 'UNKNOWN';
  // 注意顺序：雪在雨前面（雨夹雪要归 SNOWY）
  if (text.includes('雪')) return 'SNOWY';
  if (text.includes('雨')) return 'RAINY';
  if (text.includes('晴')) return 'SUNNY';
  if (text.includes('云')) return 'CLOUDY';
  if (text.includes('阴') ||
      text.includes('雾') ||
      text.includes('霾') ||
      text.includes('尘') ||
      text.includes('沙')) return 'OVERCAST';

  return 'UNKNOWN';
}

// ============================================================
//  数据包生成（发送方向）
// ============================================================

function buildTimePacket() {
  const now = new Date();
  const pad = n => String(n).padStart(2, '0');
  const body = `TIME,${now.getFullYear()},${pad(now.getMonth() + 1)},${pad(now.getDate())},${pad(now.getHours())},${pad(now.getMinutes())},${pad(now.getSeconds())}`;
  return `$${body}*${calcXOR(body)}\n`;
}

/** 实时天气包：传入 now 对象，包含 text 和 icon */
function buildWeatherPacket(w) {
  const body = `WEATHER,${weatherToEn(w.text, w.icon)},${Math.round(Number(w.temp))},${Math.round(Number(w.tempMax))},${Math.round(Number(w.tempMin))},${Math.round(Number(w.humidity))}`;
  return `$${body}*${calcXOR(body)}\n`;
}

/** 预报包：每组含 textDay 和 iconDay */
function buildForecastPacket(dailyList) {
  let body = 'FORECAST';
  for (let i = 0; i < 3 && i < dailyList.length; i++) {
    const d = dailyList[i];
    body += `,${i + 1},${weatherToEn(d.text, d.iconDay)},${Math.round(Number(d.tempMax))},${Math.round(Number(d.tempMin))},${Math.round(Number(d.humidity))}`;
  }
  return `$${body}*${calcXOR(body)}\n`;
}

// ============================================================
//  接收数据解析（步数 / 心率 / 血氧）
// ============================================================
function parseIncoming(str) {
  rxBuffer += str;
  let idx;
  while ((idx = rxBuffer.indexOf('\n')) >= 0) {
    const line = rxBuffer.slice(0, idx);
    rxBuffer  = rxBuffer.slice(idx + 1);
    handleOnePacket(line.trim());
  }
}

function handleOnePacket(line) {
  if (!line.startsWith('$')) return;
  const starIdx = line.indexOf('*');
  if (starIdx < 0) return;

  const inner = line.slice(1, starIdx);
  const cmdEnd = inner.indexOf(',');
  const cmd    = cmdEnd >= 0 ? inner.slice(0, cmdEnd) : inner;
  const payload = cmdEnd >= 0 ? inner.slice(cmdEnd + 1) : '';
  const fields = payload ? payload.split(',') : [];

  const now = new Date();
  const pad = n => String(n).padStart(2, '0');
  const timeStr = `${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`;

  let updated = false;

  if (cmd === 'STEP' && fields.length >= 1) {
    healthData.value.step = parseInt(fields[0]) || 0;
    updated = true;
  } else if (cmd === 'HR' && fields.length >= 1) {
    healthData.value.hr = parseInt(fields[0]) || 0;
    updated = true;
  } else if (cmd === 'SPO2' && fields.length >= 1) {
    healthData.value.spo2 = parseInt(fields[0]) || 0;
    updated = true;
  } else if (cmd === 'HEALTH' && fields.length >= 3) {
    healthData.value.step = parseInt(fields[0]) || 0;
    healthData.value.hr   = parseInt(fields[1]) || 0;
    healthData.value.spo2 = parseInt(fields[2]) || 0;
    updated = true;
  }

  if (updated) {
    healthData.value.lastUpdate = timeStr;
    addLog(`收到 ${line}`);
  } else {
    console.log('未知数据包:', line);
  }
}

uni.onBLECharacteristicValueChange(res => {
  const str = ab2str(res.value);
  console.log('【接收到Notify】', str);
  parseIncoming(str);
});

// ============================================================
//  自动定位 + 反查城市ID
// ============================================================
async function autoLocate() {
  if (locating.value) return;
  locating.value = true;
  uni.showLoading({ title: '定位中...' });

  try {
    const pos = await new Promise((resolve, reject) => {
      uni.getLocation({
        type: 'wgs84',
        geocode: false,
        timeout: 8000,
        success: resolve,
        fail: reject
      });
    });

    const lon = Number(pos.longitude).toFixed(2);
    const lat = Number(pos.latitude).toFixed(2);
    console.log('【定位成功】', lon, lat);

    const url = `https://${QWEATHER_HOST}/geo/v2/city/lookup?location=${lon},${lat}&lang=zh`;
    const res = await new Promise((resolve, reject) => {
      uni.request({
        url,
        method: 'GET',
        header: { 'X-QW-Api-Key': QWEATHER_KEY },
        success: resolve,
        fail: reject
      });
    });

    let data = res.data;
    if (typeof data === 'string') {
      try { data = JSON.parse(data); } catch (e) {}
    }

    if (data && data.code === '200' && Array.isArray(data.location) && data.location.length > 0) {
      const city = data.location[0];
      LOCATION.value     = city.id;
      locationName.value = city.name;
      addLog(`定位成功：${city.name}（${city.id}）`);
    } else {
      LOCATION.value     = `${lon},${lat}`;
      locationName.value = '当前位置';
      addLog(`GeoAPI异常，改用经纬度查询`);
    }

    // ★ 首次定位成功 → 触发一次自动发送
    if (!hasLocatedOnce.value) {
      hasLocatedOnce.value = true;
      if (connected.value) {
        addLog('首次定位成功，自动发送一次数据包');
        setTimeout(() => { sendAllPackets(); }, 300);
      } else {
        pendingAutoSend.value = true;
        addLog('首次定位成功，等待蓝牙连接后自动发送');
      }
    }
  } catch (err) {
    console.error('【定位失败】', err);
    addLog('定位失败：' + (err.errMsg || err.message || '未知错误'));
    uni.showToast({ title: '定位失败', icon: 'none' });
  } finally {
    uni.hideLoading();
    locating.value = false;
  }
}

// ============================================================
//  天气 API
// ============================================================
function fetchWeather() {
  return new Promise((resolve, reject) => {
    uni.request({
      url: `https://${QWEATHER_HOST}/v7/weather/now?location=${LOCATION.value}`,
      method: 'GET',
      header: { 'X-QW-Api-Key': QWEATHER_KEY },
      success(nowRes) {
        let nowData = nowRes.data;
        if (typeof nowData === 'string') {
          try { nowData = JSON.parse(nowData); } catch (e) {}
        }
        if (!nowData || nowData.code !== '200') {
          return reject(new Error('实时天气接口错误: ' + (nowData ? nowData.code : 'undefined')));
        }
        const now = nowData.now;

        uni.request({
          url: `https://${QWEATHER_HOST}/v7/weather/3d?location=${LOCATION.value}`,
          method: 'GET',
          header: { 'X-QW-Api-Key': QWEATHER_KEY },
          success(dailyRes) {
            let dailyData = dailyRes.data;
            if (typeof dailyData === 'string') {
              try { dailyData = JSON.parse(dailyData); } catch (e) {}
            }
            if (!dailyData || dailyData.code !== '200') {
              return reject(new Error('预报接口错误: ' + (dailyData ? dailyData.code : 'undefined')));
            }
            const daily = dailyData.daily;
            const tomorrow = daily[0] || { tempMax: now.temp, tempMin: now.temp };

            resolve({
              now: {
                text: now.text,
                icon: now.icon,                    // ★ 带上 icon
                temp: now.temp,
                tempMax: tomorrow.tempMax,
                tempMin: tomorrow.tempMin,
                humidity: now.humidity
              },
              daily: daily.map(d => ({
                text: d.textDay,
                iconDay: d.iconDay,                // ★ 带上 iconDay
                tempMax: d.tempMax,
                tempMin: d.tempMin,
                humidity: d.humidity
              }))
            });
          },
          fail: reject
        });
      },
      fail: reject
    });
  });
}

// ============================================================
//  ★ 本地保存 / 清除上次连接设备
// ============================================================
function saveLastDevice(deviceId, name) {
  try {
    uni.setStorageSync(STORAGE_KEY_LAST_DEVICE, {
      deviceId,
      name: name || '',
      ts: Date.now()
    });
    console.log('已保存上次设备:', deviceId);
  } catch (e) {
    console.warn('保存设备信息失败', e);
  }
}

function clearLastDevice() {
  try {
    uni.removeStorageSync(STORAGE_KEY_LAST_DEVICE);
    console.log('已清除上次设备记录');
  } catch (e) {}
}

function getLastDevice() {
  try {
    return uni.getStorageSync(STORAGE_KEY_LAST_DEVICE) || null;
  } catch (e) {
    return null;
  }
}

// ============================================================
//  BLE 扫描
// ============================================================
function startScan() {
  uni.openBluetoothAdapter({
    success() {
      deviceList.value = [];
      scanning.value = true;
      uni.stopBluetoothDevicesDiscovery({ complete: () => {} });
      uni.startBluetoothDevicesDiscovery({
        allowDuplicatesKey: false,
        success() { console.log('开始扫描'); },
        fail(err) {
          scanning.value = false;
          uni.showToast({ title: '扫描启动失败', icon: 'none' });
        }
      });
    },
    fail() {
      uni.showToast({ title: '请先打开手机蓝牙', icon: 'none' });
    }
  });
}

function stopScan() {
  uni.stopBluetoothDevicesDiscovery({
    success() { scanning.value = false; }
  });
}

uni.onBluetoothDeviceFound(devices => {
  (devices.devices || []).forEach(d => {
    if (!d.name) return;
    if (deviceList.value.find(x => x.deviceId === d.deviceId)) return;
    deviceList.value.push({
      name: d.name,
      deviceId: d.deviceId,
      RSSI: d.RSSI
    });
  });
});

// ============================================================
//  BLE 连接 + MTU + 服务发现 + Notify订阅
// ============================================================
/**
 * @param {string} deviceId
 * @param {string} name
 * @param {boolean} isAutoReconnect  是否为自动重连（不弹错误 toast）
 */
function connectDevice(deviceId, name, isAutoReconnect = false) {
  if (!isAutoReconnect) stopScan();

  if (!isAutoReconnect) uni.showLoading({ title: '连接中...' });

  uni.createBLEConnection({
    deviceId,
    timeout: 10000,
    success() {
      uni.setBLEMTU({
        deviceId,
        mtu: TARGET_MTU,
        success(res) {
          currentMtu.value = res.mtu || TARGET_MTU;
          if (!isAutoReconnect) uni.hideLoading();
          discoverServices(deviceId, name, isAutoReconnect);
        },
        fail() {
          currentMtu.value = TARGET_MTU;
          if (!isAutoReconnect) uni.hideLoading();
          discoverServices(deviceId, name, isAutoReconnect);
        }
      });
    },
    fail(err) {
      if (!isAutoReconnect) {
        uni.hideLoading();
        uni.showToast({ title: '连接失败', icon: 'none' });
      } else {
        // 自动重连失败，仅记录日志
        console.warn('自动重连失败', err);
        addLog('自动重连失败，请手动扫描连接');
        reconnecting.value = false;
      }
    }
  });
}

function discoverServices(deviceId, name, isAutoReconnect = false) {
  uni.getBLEDeviceServices({
    deviceId,
    success(res) {
      const target = res.services.find(
        s => s.uuid.toUpperCase() === SERVICE_UUID
      );
      if (!target) {
        addLog('未找到目标服务 FFE0');
        if (!isAutoReconnect) uni.showToast({ title: '未找到目标服务 FFE0', icon: 'none' });
        uni.closeBLEConnection({ deviceId });
        reconnecting.value = false;
        return;
      }

      uni.getBLEDeviceCharacteristics({
        deviceId,
        serviceId: target.uuid,
        success(cRes) {
          // ① 找写入特征值 FFE1
          const writeCh = cRes.characteristics.find(
            c => c.uuid.toUpperCase() === WRITE_CHAR_UUID
          );
          if (!writeCh) {
            addLog('未找到写入特征值 FFE1');
            if (!isAutoReconnect) uni.showToast({ title: '未找到写入特征值 FFE1', icon: 'none' });
            uni.closeBLEConnection({ deviceId });
            reconnecting.value = false;
            return;
          }
          serviceId.value         = target.uuid;
          characteristicId.value  = writeCh.uuid;
          connectedDeviceId.value = deviceId;
          connectedDeviceName.value = name || deviceId;
          connected.value         = true;
          reconnecting.value      = false;

          // ② 找 Notify 特征值并订阅
          const notifyCh = cRes.characteristics.find(
            c => c.properties && (c.properties.notify || c.properties.indicate)
          );
          if (notifyCh) {
            notifyCharId.value = notifyCh.uuid;
            uni.notifyBLECharacteristicValueChange({
              state: true,
              deviceId,
              serviceId: target.uuid,
              characteristicId: notifyCh.uuid,
              success() {
                console.log('订阅通知成功:', notifyCh.uuid);
                addLog('已订阅通知特征值 ' + notifyCh.uuid.slice(4, 8).toUpperCase());
              },
              fail(err) {
                console.error('订阅通知失败', err);
                addLog('订阅通知失败，无法接收数据');
              }
            });
          } else {
            addLog('未找到支持 Notify 的特征值，无法接收数据');
          }

          addLog('已连接设备，MTU=' + currentMtu.value);
          if (!isAutoReconnect) {
            uni.showToast({ title: '连接就绪', icon: 'success' });
          } else {
            uni.showToast({ title: '已自动连接上次设备', icon: 'success' });
          }

          // ★ 连接成功后保存到本地，供下次自动重连
          saveLastDevice(deviceId, name);

          // ★ 首次定位挂起的自动发送
          if (pendingAutoSend.value) {
            pendingAutoSend.value = false;
            addLog('蓝牙已连接，触发首次自动发送');
            setTimeout(() => { sendAllPackets(); }, 500);
          }
        },
        fail(err) {
          console.error('获取特征值失败', err);
          reconnecting.value = false;
        }
      });
    },
    fail(err) {
      console.error('获取服务失败', err);
      reconnecting.value = false;
    }
  });
}

function disconnect() {
  stopAutoSend();
  if (connectedDeviceId.value) {
    if (notifyCharId.value) {
      uni.notifyBLECharacteristicValueChange({
        state: false,
        deviceId: connectedDeviceId.value,
        serviceId: serviceId.value,
        characteristicId: notifyCharId.value,
        complete: () => {}
      });
    }
    uni.closeBLEConnection({
      deviceId: connectedDeviceId.value,
      complete() {
        connected.value           = false;
        connectedDeviceId.value   = '';
        connectedDeviceName.value = '';
        serviceId.value           = '';
        characteristicId.value    = '';
        notifyCharId.value        = '';
        rxBuffer                  = '';
        addLog('已断开连接');
      }
    });
    // ★ 用户手动断开 → 清除上次设备记录（下次打开不会再自动连）
    clearLastDevice();
    addLog('已清除自动重连记录');
  }
}

// ============================================================
//  BLE 分包写入
// ============================================================
function writePacket(str) {
  return new Promise((resolve, reject) => {
    const bytes = new Uint8Array(str2ab(str));
    const chunkSize = currentMtu.value - 3;
    let offset = 0;

    function writeNext() {
      if (offset >= bytes.length) { resolve(); return; }
      const end   = Math.min(offset + chunkSize, bytes.length);
      const chunk = bytes.slice(offset, end);

      uni.writeBLECharacteristicValue({
        deviceId:         connectedDeviceId.value,
        serviceId:        serviceId.value,
        characteristicId: characteristicId.value,
        value:            chunk.buffer,
        writeType:        'writeNoResponse',
        success() {
          offset = end;
          if (offset < bytes.length) {
            setTimeout(writeNext, 10);
          } else {
            resolve();
          }
        },
        fail(err) {
          console.error('写入失败', err);
          reject(err);
        }
      });
    }
    writeNext();
  });
}

// ============================================================
//  发送全部数据包
// ============================================================
async function sendAllPackets() {
  if (!connected.value) {
    uni.showToast({ title: '请先连接设备', icon: 'none' });
    return;
  }
  try {
    const timePacket = buildTimePacket();
    await writePacket(timePacket);
    addLog('发送 ' + timePacket.replace('\n', ''));

    const weatherData = await fetchWeather();
    const weatherPacket = buildWeatherPacket(weatherData.now);
    await writePacket(weatherPacket);
    addLog('发送 ' + weatherPacket.replace('\n', ''));

    const forecastPacket = buildForecastPacket(weatherData.daily);
    await writePacket(forecastPacket);
    addLog('发送 ' + forecastPacket.replace('\n', ''));

  } catch (err) {
    console.error('发送流程出错', err);
    addLog('发送出错: ' + (err.message || '未知错误'));
  }
}

// ============================================================
//  定时发送
// ============================================================
function toggleAutoSend(e) {
  autoSend.value = e.detail.value;
  if (autoSend.value) startAutoSend();
  else stopAutoSend();
}

function startAutoSend() {
  stopAutoSend();
  const sec = Math.max(5, Number(intervalSec.value) || 30);
  timerId = setInterval(() => { sendAllPackets(); }, sec * 1000);
  addLog(`定时发送已开启，间隔 ${sec} 秒`);
}

function stopAutoSend() {
  if (timerId) {
    clearInterval(timerId);
    timerId = null;
    addLog('定时发送已关闭');
  }
}

// ============================================================
//  ★ 启动时尝试自动重连上次设备
// ============================================================
function tryAutoReconnect() {
  const last = getLastDevice();
  if (!last || !last.deviceId) {
    console.log('无上次设备记录，跳过自动重连');
    return;
  }

  addLog(`发现上次设备记录：${last.name || last.deviceId}，尝试自动重连...`);
  reconnecting.value = true;

  uni.openBluetoothAdapter({
    success: () => {
      // 稍等片刻，等蓝牙适配器就绪
      setTimeout(() => {
        connectDevice(last.deviceId, last.name, true);
      }, 500);
    },
    fail: () => {
      addLog('蓝牙未开启，无法自动重连');
      reconnecting.value = false;
    }
  });
}

// ============================================================
//  生命周期
// ============================================================
onMounted(() => {
  autoLocate();
  // ★ 启动后尝试自动重连上次设备
  tryAutoReconnect();
});

onUnmounted(() => {
  stopAutoSend();
  if (connectedDeviceId.value) {
    uni.closeBLEConnection({ deviceId: connectedDeviceId.value });
  }
  uni.closeBluetoothAdapter();
});
</script>

<style scoped>
.container {
  padding: 20rpx;
  padding-top: 60rpx; /* 新增：给顶部留出状态栏的空间 */
  background: #f5f6fa;
  min-height: 100vh;
}
.card {
  background: #fff;
  border-radius: 12rpx;
  padding: 24rpx;
  margin-bottom: 20rpx;
}

/* 定位栏 */
.location-bar {
  display: flex;
  align-items: center;
  gap: 12rpx;
}
.location-bar .label { font-size: 28rpx; color: #666; }
.location-bar .city-name { font-size: 30rpx; font-weight: bold; color: #07c160; flex: 1; }

/* 蓝牙状态栏 */
.status-bar { display: flex; align-items: center; flex-wrap: wrap; gap: 12rpx; }
.status-text { font-weight: bold; }
.connected    { color: #07c160; }
.disconnected { color: #999; }
.device-name  { font-size: 26rpx; color: #666; }
.btn-mini { margin-left: auto; }
.mt10 { margin-top: 16rpx; }

/* 健康数据面板 */
.health-panel { text-align: center; }
.health-row {
  display: flex;
  justify-content: space-around;
  padding: 20rpx 0;
}
.health-item { flex: 1; }
.health-value {
  font-size: 52rpx;
  font-weight: bold;
  color: #07c160;
  display: block;
}
.health-value .unit {
  font-size: 24rpx;
  color: #999;
  font-weight: normal;
}
.health-label {
  font-size: 24rpx;
  color: #666;
  display: block;
  margin-top: 8rpx;
}
.health-update {
  font-size: 22rpx;
  color: #999;
  display: block;
  margin-top: 10rpx;
}

/* 设备列表 */
.device-list { margin-top: 20rpx; }
.device-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 20rpx;
  border-bottom: 1rpx solid #eee;
}
.device-item:active { background: #f0f0f0; }
.device-info { flex: 1; }
.device-name { font-size: 30rpx; font-weight: 500; }
.device-id   { font-size: 22rpx; color: #999; margin-top: 4rpx; display: block; }
.rssi        { font-size: 24rpx; color: #07c160; }

/* 控制行 */
.row { display: flex; align-items: center; gap: 12rpx; }
.interval-input {
  width: 100rpx;
  border: 1rpx solid #ddd;
  border-radius: 8rpx;
  padding: 6rpx 12rpx;
  font-size: 28rpx;
}

/* 日志 */
.section-title { font-size: 28rpx; font-weight: bold; margin-bottom: 12rpx; display: block; }
.log-scroll { max-height: 600rpx; }
.log-item { padding: 12rpx 0; border-bottom: 1rpx solid #f0f0f0; }
.log-time    { font-size: 22rpx; color: #999; margin-right: 12rpx; }
.log-content { font-size: 24rpx; color: #333; word-break: break-all; }
.tip { color: #999; font-size: 26rpx; text-align: center; display: block; padding: 20rpx 0; }
</style>