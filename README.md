# Loom

**Библиотека для построения приложений из независимых модулей-процессов, взаимодействующих через C-ABI.**

<p>
  <img alt="Status" src="https://img.shields.io/badge/status-experimental-orange?style=flat-square">
  <img alt="C++17" src="https://img.shields.io/badge/C%2B%2B-17-blue?style=flat-square&logo=cplusplus&logoColor=white">
  <img alt="License" src="https://img.shields.io/badge/license-MIT-green?style=flat-square">
  <img alt="Platform" src="https://img.shields.io/badge/platform-linux-lightgrey?style=flat-square&logo=linux&logoColor=white">
  <img alt="Progress" src="https://img.shields.io/badge/progress-42%25-yellow?style=flat-square">
</p>

> ⚠️ **Проект в активной разработке.** Публичный API может меняться.

---

## 📖 Содержание

<details>
<summary>Развернуть</summary>

- [Назначение](#-назначение)
- [Прогресс разработки](#-прогресс-разработки)
- [Примеры использования](#-примеры-использования)
- [Архитектура](#-архитектура)
- [Компоненты](#-компоненты)
  - [GWP — Wire Protocol](#-gwp--wire-protocol)
  - [TDB — Task Distribution Block](#-tdb--task-distribution-block)
  - [Brutal Tester](#-brutal-tester)
- [Установка](#-установка)
- [Кроссязычность](#-кроссязычность)
- [Roadmap](#-roadmap)
- [Принципы](#-принципы)
- [Лицензия](#-лицензия)

</details>

---

## 🎯 Назначение

**Loom** — это библиотека для построения приложений, состоящих из изолированных модулей-процессов. Каждый модуль:

- 🧩 Работает в **собственном процессе**
- 🔌 Экспортирует функции через **C-ABI**
- 📝 Регистрирует методы по **строковому описанию сигнатуры**
- 🔗 Взаимодействует с другими модулями через **локальные сокеты**
- 📦 Обменивается пакетами через собственный протокол **GWP**

Библиотека автоматически генерирует адаптеры для зарегистрированных функций, выполняет поиск модулей, управляет транспортом и распределением входящих задач.

### ⚖️ Сравнение подходов

| Критерий | HTTP / RPC | Плагины `.so` | **Loom** |
|---|:---:|:---:|:---:|
| Изоляция падений | ✅ | ❌ | ✅ |
| Низкая задержка | ❌ | ✅ | ✅ |
| Кроссязычность | ✅ | ❌ | ✅ |
| Простой API | ❌ | ✅ | ✅ |
| Горячая замена | ✅ | ❌ | ✅ |

---

## 📊 Прогресс разработки

**Текущий статус: `42%`**

```
API и структура     ████████░░  80%
JIT-ядро            █████████░  90%
Транспорт UDS       ████████░░  80%
Транспорт UDP       ████░░░░░░  40%
GWP-протокол        ████░░░░░░  45%
TDB (buffer+uplink) ████░░░░░░  40%
Discovery           ██░░░░░░░░  20%
Server Core         ██░░░░░░░░  20%
RPC Manager         ██░░░░░░░░  25%
Тестирование        ██████░░░░  60%
Документация        ██████░░░░  60%
```

### ✅ Реализовано

| № | Компонент | Готовность | Состояние |
|:---:|:---|:---:|:---|
| 1 | **MetaLogger** | `100%` | Уровни, цвета, вкл/выкл |
| 2 | **Глобальное состояние** | `100%` | `g_running`, `g_flags`, heartbeat через `std::atomic` |
| 3 | **JIT-адаптеры** | `90%` | Генерация C++, компиляция, `dlopen`, кэш по сигнатуре |
| 4 | **Публичный C-API** | `80%` | `start`, `stop`, `use_module*`, `module_send`, `register_method` |
| 5 | **Транспорт UDS** | `80%` | Сокеты, listener-поток, `connect`/`send`, таймауты |
| 6 | **Реестр методов** | `80%` | `ModuleRegistry`, парсинг сигнатур, вызов через адаптер |
| 7 | **Кэш endpoint'ов** | `60%` | TTL, `get`/`set`; `remove`/`clear` объявлены, но не реализованы |

### 🚧 В работе

| № | Компонент | Готовность | Осталось |
|:---:|:---|:---:|:---|
| 8 | **GWP-протокол** | `45%` | Header, packet types, статусы, сериализация; нет версионирования и стриминга |
| 9 | **TDB (Buffer + UPLINK)** | `40%` | Ring buffer, обёртка UPLINK; воркеры и стратегии — заглушки |
| 10 | **Brutal Tester** | `60%` | 6 групп тестов, фильтры, тайминги; нет ассертов и unit-фреймворка |
| 11 | **CMake** | `60%` | Не подключены `capabilities`, `jit`, `server_core`, `global` |
| 12 | **UDP-транспорт** | `40%` | Приём пакетов, протокол, интеграция с GWP |
| 13 | **RPC Manager** | `25%` | Интерфейс + `promise`; нет реализации `call`/`on_response`/таймаутов |
| 14 | **Server Core** | `20%` | Реальный реестр модулей с heartbeat |

### 📋 Не начато

| № | Компонент | Готовность | Требуется |
|:---:|:---|:---:|:---|
| 15 | **Worker Pool + стратегии** | `0%` | Реальные воркеры, диспетчеризация, `DistributionStrategy` |
| 16 | **Дисковый кэш JIT** | `0%` | Сохранение `.so` по хэшу сигнатуры |
| 17 | **Shared memory** | `0%` | Передача больших payload'ов через fd |
| 18 | **Трассировка и метрики** | `0%` | Отправка событий в Server Core |
| 19 | **Кроссязычные биндинги** | `0%` | Python, Rust, Go |

---

## 💡 Примеры использования

### Модуль-сервер

```c
#include <loom.h>

int add(int a, int b) {
    return a + b;
}

std::string greet(const std::string& name) {
    return "Привет, " + name + "!";
}

int main() {
    start("calculator");

    register_method("add",   "int,int->int",   (void*)add);
    register_method("greet", "string->string", (void*)greet);

    // модуль ожидает входящие вызовы
}
```

### Модуль-клиент

```c
#include <loom.h>

int main() {
    start("app");
    use_module_direct("calculator");

    int result = call("calculator", "add", 2, 3);         // 5
    std::string msg = call("calculator", "greet", "мир"); // "Привет, мир!"
}
```

### Обмен сообщениями

```c
module_send("logger", "пользователь вошёл в систему");
```

---

## 🏗️ Архитектура

```
┌──────────────┐          ┌──────────────┐          ┌──────────────┐
│   Модуль A   │          │   Модуль B   │          │   Модуль C   │
│  (процесс 1) │          │  (процесс 2) │          │  (процесс 3) │
└──────┬───────┘          └──────┬───────┘          └──────┬───────┘
       │                         │                         │
       └─────────────┬───────────┴─────────────┬───────────┘
                     │                         │
              ┌──────▼──────┐           ┌──────▼──────┐
              │   Discovery │           │  Transport  │
              │  (cache)    │           │  UDS / UDP  │
              └─────────────┘           └──────┬──────┘
                                               │
                                        ┌──────▼──────┐
                                        │     GWP     │
                                        │  Wire Proto │
                                        └──────┬──────┘
                                               │
                                        ┌──────▼────────────┐
                                        │       TDB         │
                                        │  ┌────────────┐   │
                                        │  │   Buffer   │   │
                                        │  └─────┬──────┘   │
                                        │  ┌─────▼──────┐   │
                                        │  │   UPLINK   │   │
                                        │  └─────┬──────┘   │
                                        │  ┌─────▼──────┐   │
                                        │  │  Workers   │   │
                                        │  └────────────┘   │
                                        └──────┬────────────┘
                                               │
                                        ┌──────▼──────────┐
                                        │  JIT Adapters   │
                                        │  +  Registry    │
                                        └─────────────────┘
```

### Ключевые компоненты

| Компонент | Описание |
|:---|:---|
| 🧩 **Модуль** | Отдельный процесс, единица изоляции |
| 🔍 **Discovery** | Поиск модулей, кэш endpoint'ов с TTL |
| 🚚 **Transport** | UDS для локальной связи, UDP для сетевой |
| 📦 **GWP** | Бинарный wire-протокол: header, типы пакетов, статусы |
| ⚙️ **TDB** | Приём, буферизация и распределение входящих задач между воркерами |
| ⚡ **JIT-адаптеры** | Генерация C++ обёрток для **приёма** вызовов по строковой сигнатуре |
| 📋 **ModuleRegistry** | Хранит методы модуля + адаптеры, вызывает их |

---

## 🔧 Компоненты

### 📦 GWP — Wire Protocol

Собственный бинарный протокол поверх UDS/UDP. Определён в `src/protocol/gwp.h`.

**Структура пакета:**

```c
struct GWPHeader {
    uint32_t   magic;           // 0x4757505F ("GWP_")
    uint32_t   packet_id;       // ID для корреляции запрос-ответ
    PacketType type;            // REQUEST / RESPONSE / ASYNC_DATA / HEARTBEAT / CLOSE
    uint8_t    status;          // OK / ERROR / METHOD_NOT_FOUND / TIMEOUT / INVALID_PARAMS
    uint32_t   method_name_len;
    uint32_t   data_len;
};
```

| Тип пакета | Назначение |
|:---|:---|
| `REQUEST` | Вызов метода |
| `RESPONSE` | Ответ на вызов |
| `ASYNC_DATA` | Односторонняя отправка (`module_send`) |
| `HEARTBEAT` | Проверка живости модуля |
| `CLOSE` | Завершение соединения |

| Статус | Значение |
|:---|:---|
| `OK` | Успех |
| `ERROR` | Общая ошибка |
| `METHOD_NOT_FOUND` | Метод не зарегистрирован |
| `TIMEOUT` | Истёк таймаут |
| `INVALID_PARAMS` | Ошибка аргументов |

**Что уже работает:** сериализация, десериализация, network byte order, magic-проверка.

**Что осталось:** версионирование, стриминг больших payload'ов, сжатие, handshake.

---

### ⚙️ TDB — Task Distribution Block

Центральный компонент приёма и распределения задач. Определён в `src/gateway/TDB/`.

```
   входящий пакет
        │
        ▼
   ┌─────────┐      ┌───────────┐      ┌──────────┐
   │ Buffer  │ ──▶ │ UPLINK    │ ──▶ │ Workers  │
   │ (ring)  │      │ (dispatch)│      │ (pool)   │
   └─────────┘      └───────────┘      └──────────┘
```

| Подкомпонент | Статус | Описание |
|:---|:---:|:---|
| **Buffer** | ✅ Реализован | Lock-free кольцевой буфер на `std::atomic`, push/pop, `has_data` per-slot |
| **UPLINK** | 🚧 Частично | Инициализация пула воркеров, приём пакетов, счётчики |
| **Worker** | 📋 Заглушка | `start()` / `stop()` — интерфейс есть, логики нет |
| **DistributionStrategy** | 📋 Заглушка | Базовый класс без методов |

**Ключевые API:**

```cpp
class Buffer {
    bool push(IncomingPacket&& packet);          // от транспорта
    bool pop(size_t worker_id, IncomingPacket&); // воркером
    size_t size() const;
    bool full() const;
};

class UPLINK {
    bool init(size_t workers, DistributionStrategy*);
    void push(IncomingPacket&& packet);
    size_t queue_size() const;
    size_t workers_count() const;
};
```

Обеспечивает **изоляцию транспорта от обработки**: транспорт только кладёт пакеты в очередь, воркеры — забирают.

---

### 🧪 Brutal Tester

Собственный тестовый раннер в `brutal_tester/`. Не требует внешних фреймворков.

```bash
./brutal_tester -all                     # все тесты
./brutal_tester -only module             # только группу module
./brutal_tester -skip uds -skip jit      # исключить группы
./brutal_tester -list                    # список тестов
./brutal_tester -all -v                  # verbose (показывать skip)
```

**Группы тестов:**

| Группа | Что проверяет |
|:---|:---|
| `cache` | `set`/`get`/TTL/update/miss |
| `module` | `ModuleRegistry`, регистрация методов, вызов |
| `uds` | Создание сокета, connect, send/recv |
| `jit` | Генерация адаптера + вызов `int,int->int` |
| `connect` | `locate` + сохранение в кэш |
| `send_recv` | Полный цикл: кэш → listener → send → stop |

Каждый тест возвращает `int` (`0` = успех), раннер замеряет длительность и выводит сводку.

---

## 🚀 Установка

```bash
git clone https://github.com/yourname/loom.git
cd loom
mkdir build && cd build
cmake ..
make
sudo make install
```

### Требования

- 🐧 **Linux**
- 🔨 **CMake** ≥ 3.16
- ⚙️ **C++17**
- 📦 Поддержка `dlopen`
- 🧬 `g++` доступен в `PATH` (для JIT-компиляции)

---

## 🌍 Кроссязычность

Публичный API — чистый C, поэтому модули можно писать на **любом языке** с поддержкой C-ABI.

> 💡 **C-ABI даёт базовую совместимость «из коробки».** Биндинги (v1.0) добавят идиоматичные обёртки — чтобы не писать `ctypes`-бойлерплейт руками.

<details>
<summary><b>🐍 Python (через ctypes, без биндинга)</b></summary>

```python
from ctypes import CDLL
loom = CDLL("libloom.so")

loom.register_method_impl.argtypes = [
    ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p
]

def add(a, b):
    return a + b

cb = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_int)(add)
loom.register_method_impl(b"calc", b"add", b"int,int->int",
                          ctypes.cast(cb, ctypes.c_void_p))
```

</details>

<details>
<summary><b>🦀 Rust</b></summary>

```rust
#[no_mangle]
pub extern "C" fn square(x: i32) -> i32 { x * x }

loom::register("square", "int->int", square as *mut _);
```

</details>

<details>
<summary><b>⚡ C++</b></summary>

```cpp
#include <loom.h>

int add(int a, int b) { return a + b; }

int main() {
    start("calc");
    register_method("add", "int,int->int", (void*)add);
}
```

</details>

---

## 🗺️ Roadmap

### v0.2 — Wire & Server Core

- [x] GWP-протокол: header, типы пакетов, статусы — `████░░░░░░` 45%
- [ ] Server Core с heartbeat — `██░░░░░░░░` 20%
- [ ] Удалённый вызов методов `call(...)` — `██░░░░░░░░` 25%
- [ ] Приведение CMake в порядок — `██████░░░░` 60%

### v0.3 — TDB & Tests

- [ ] Реальные воркеры и стратегии распределения — `░░░░░░░░░░` 0%
- [ ] Дисковый кэш JIT-адаптеров — `░░░░░░░░░░` 0%
- [ ] Приём UDP + интеграция с GWP — `████░░░░░░` 40%
- [ ] Unit-тесты с ассертами — `░░░░░░░░░░` 0%

### v1.0 — Production Ready

- [ ] Shared memory для больших payload'ов — `░░░░░░░░░░` 0%
- [ ] Трассировка и метрики — `░░░░░░░░░░` 0%
- [ ] Python / Rust / Go биндинги — `░░░░░░░░░░` 0%
- [ ] Стабильный публичный API — `████████░░` 80%


