# SynaptaNode — V1 Deferred Tasks

สิ่งที่ออกแบบไว้แล้วแต่ยังไม่ทำใน V1 นี้ เก็บไว้ทำในอนาคต

---

## Web App — สิ่งที่ต้องเพิ่ม

### บังคับ (ก่อน Dynamic Rules จะใช้งานได้จริง)

- [ ] **`agent_prompt.js`** — เพิ่ม rule JSON format และ rule topics เข้า context  
  AI จะได้รู้วิธีสร้าง/ลบ rule ผ่าน `mqtt_publish` ที่มีอยู่แล้ว

### ทางเลือก (เพิ่ม visibility ให้ AI)

- [ ] **`agentSkills.js`** — เพิ่ม skill ใหม่ชื่อ `manage_rules_node`  
  ทำ request-response กับ ESP32 เพื่ออ่าน rules ที่มีอยู่กลับมา  
  Pattern เหมือน `mqttWaitForStream` ที่ hub ใช้อยู่แล้ว

- [ ] **`data.js`** — ลงทะเบียน skill `manage_rules_node` เข้า DEFAULT_SETTINGS.skills

> สร้าง/ลบ rule ทำได้แล้วด้วย `mqtt_publish` ที่มีอยู่  
> `manage_rules_node` เพิ่มแค่ความสามารถ "อ่านกลับ" เท่านั้น

---

## Web App — Manifest integration (ฝั่ง node พร้อมแล้ว)

Library publish manifest อัตโนมัติตอน MQTT connect:

**Topic:** `{base}/nodes/{nodeId}/manifest` (retained)

**Payload:**
```json
{
  "nodeId": "node-A1B2C3",
  "baseTopic": "Mylab/smarthome",
  "devices": [
    {"id":"bedroom-relay","room":"bedroom","type":"digital",
     "stateTopic":"Mylab/smarthome/bedroom/bedroom-relay/state",
     "cmdTopic":"Mylab/smarthome/bedroom/bedroom-relay/set"},
    {"id":"bedroom-temp","room":"bedroom","type":"sensor",
     "stateTopic":"Mylab/smarthome/bedroom/bedroom-temp/state"}
  ]
}
```

`SENSOR` มีแค่ `stateTopic` (publish only)

### สิ่งที่ฝั่ง Web App ต้องทำ

- [ ] Subscribe `{base}/nodes/+/manifest` ตอน MQTT connect
- [ ] Parse JSON → merge เข้า KG (`src/utils/kg.js`) อัตโนมัติ
- [ ] Onboarding flow: ถ้ามี manifest ของ node ใหม่ → ขึ้น notify "พบ node ใหม่ มี X devices — เพิ่มเลยไหม?"
- [ ] Cleanup: ถ้า node `status = offline` นานเกิน N นาที → mark devices เป็น stale ใน UI

ผลลัพธ์: user ไม่ต้องกรอก device ใน web app แบบ manual อีก

---

## Dynamic Rules — Concept (Library พร้อมแล้ว รอ Web App)

Library รองรับ Dynamic Rules ครบแล้ว รวมถึง example `05_DynamicRules` ด้วย แต่ Web App ยังไม่รองรับ เก็บ spec ไว้ตรงนี้สำหรับ v2

### Rule JSON format
```json
{
  "id":        "rule-01",
  "condition": { "device": "bedroom-temp", "op": ">", "value": 30 },
  "action":    { "device": "bedroom-ac",   "set": true },
  "persist":   true
}
```

| Field | Description |
|-------|-------------|
| `id` | Unique rule identifier |
| `condition.device` | Device ID to read value from |
| `condition.op` | Operator: `>` `<` `>=` `<=` `==` `!=` |
| `condition.value` | Threshold to compare against |
| `action.device` | Device ID to control |
| `action.set` | `true`/`false` for NODE_DIGITAL, integer for NODE_ANALOG |
| `persist` | `true` = save to NVRAM (survives reboot), `false` = RAM only |

Capacity: up to **20 rules** total.

### Rule MQTT topics

Node ID auto-derived from MAC address — check Serial Monitor after `Synapta.begin()`.

| Topic | Direction | Payload |
|-------|-----------|---------|
| `{base}/nodes/{nodeId}/rules/set` | Web App → ESP32 | Rule JSON |
| `{base}/nodes/{nodeId}/rules/delete` | Web App → ESP32 | Rule ID string |
| `{base}/nodes/{nodeId}/rules/request` | Web App → ESP32 | any (triggers list response) |
| `{base}/nodes/{nodeId}/rules/list` | ESP32 → Web App | JSON array of all rules |

### Trigger behaviour
Rising-edge detection: action fires **once** when condition transitions `false → true`. Does not repeat while condition stays true.

### Node online/offline (LWT)
| Topic | Payload |
|-------|---------|
| `{base}/nodes/{nodeId}/status` | `"online"` หรือ `"offline"` (broker publish อัตโนมัติ) |

### สิ่งที่ต้องทำใน Web App ก่อน Dynamic Rules จะใช้ได้

- [ ] **`agent_prompt.js`** — เพิ่ม rule JSON format และ rule topics เข้า AI context
- [ ] **`agentSkills.js`** — เพิ่ม skill `manage_rules_node` (request-response เพื่ออ่าน rules กลับมา)
- [ ] **`data.js`** — ลงทะเบียน skill `manage_rules_node` เข้า DEFAULT_SETTINGS.skills

> สร้าง/ลบ rule ทำได้แล้วด้วย `mqtt_publish` ที่มีอยู่ — `manage_rules_node` เพิ่มแค่ความสามารถ "อ่านกลับ"

---

## Library — Feature ที่เลื่อนออกไป

- [ ] **OTA update** — Web App ส่ง URL ผ่าน MQTT, ESP32 download firmware จาก Hub โดยตรง (local IP)
- [ ] **AND/OR conditions** ใน Dynamic Rules (ตอนนี้รองรับแค่ condition เดียว)
- [ ] **Array of actions** ใน Dynamic Rules (ตอนนี้รองรับแค่ action เดียว)
- [ ] **Time-based conditions** เช่น `"time == 08:00"`
- [ ] **WiFi Provisioning** — เปิด hotspot ให้ตั้งค่าครั้งแรกผ่านมือถือ

---

## Library — Quick wins รอบหน้า

- [ ] **PWM channel limit check** — ESP32 core 2.x มี 16 channel; ตอนนี้ wrap-around แบบเงียบ ๆ ที่ device ที่ 17
- [ ] **Payload validation ใน `_handleMessage`** — `toInt()` คืน 0 ถ้า parse ไม่ได้ → ปิดอุปกรณ์เงียบ ๆ
- [ ] **Normalize id เหมือน room** — ตอนนี้ room lower-case + dash, id ผ่านตรง ๆ → topic อาจมีช่องว่าง
- [ ] **Device-level health topic** — sensor read NaN → publish `health = error`
- [ ] **Topic conflict warning** — ถ้า 2 nodes ประกาศ device id+room ซ้ำกัน
- [ ] **Multi-instance Synapta** — รองรับ 2 broker (local + cloud) บนบอร์ดเดียว
