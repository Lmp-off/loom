# Loom: правила архитектуры

Это контекст для реализации, а не документация библиотеки.
Источник истины — код. Если код и этот файл расходятся, сначала проверь код и обнови файл.

Используй эти правила как жёсткие ограничения при изменении TDB, транспорта, Cache и LinkManager.

---

## 1. Владение и ответственность

- TDB — композиция `UPLINK` и `OUTLINK`. В TDB нет логики транспорта, Cache или GWP.
- `UPLINK` владеет:
  - обычным и priority входным `Buffer`;
  - одной `DistributionStrategy`;
  - пулом общих `Worker`;
  - лениво создаваемыми direct-воркерами по имени модуля;
  - потоком диспетчера.
- Входной `Buffer` — это буфер перегрузки, а не очередь задач воркеров.
- Priority `Buffer` — такая же очередь перегрузки, но для direct-трафика; диспетчер всегда читает её первой.
- Единственный потребитель обоих `Buffer` — диспетчер `UPLINK`.
- `Worker` не вызывает `Buffer::pop` и не забирает задачи из входных буферов.
- Диспетчер передаёт каждый пакет ровно одному воркеру по решению `DistributionStrategy` или напрямую выделенному воркеру priority-модуля.
- `Worker` владеет одним слотом задачи, своим потоком, счётчиками и конфигурацией `PacketHandler`/`OutgoingSink`.
- `OUTLINK` владеет обычной и priority исходящей очередью и пулом отправителей.
- `TransportManager` только роутит по `endpoint.type`; сначала пробует `ConnectionCache`.
- `LinkManager` владеет исходящими активными соединениями.
- `ConnectionCache` владеет реестром уже принятых входящих соединений; fd остаётся у транспорта.
- Cache только резолвит `module -> endpoint`.
- `LinkManager` и `ConnectionCache` процесс-глобальные, доступ через `get_link_manager()` и `get_connection_cache()`.

---

## 2. Входной буфер и распределение

- Транспорт вызывает `UPLINK::push`, пакет попадает в ограниченный входной `Buffer`.
- `Buffer` сглаживает всплески и защищает модуль от перегрузки.
- Если `Buffer` полон, `UPLINK::push` отбрасывает пакет и увеличивает счётчик `dropped`.
- Диспетчер — единственный потребитель `Buffer`: `pop` → `DistributionStrategy::dispatch` → `Worker::try_submit`.
- `DistributionStrategy` — единственное место выбора воркера.
- `RoundRobinStrategy` — стратегия по умолчанию; другую можно передать в `UPLINK::init` или `TDB::init`.
- Стратегия возвращает индекс свободного воркера или `npos`.
- У воркера один слот задачи: `try_submit` не блокирует и возвращает `false`, если воркер занят или слот заполнен.
- Если свободного воркера нет, диспетчер держит один пакет и ждёт; остальные остаются во входном `Buffer`.
- Воркеры не конкурируют за `Buffer` и не конкурируют за задачи между собой.
- Число воркеров и ёмкость входного `Buffer` настраиваются независимо.
- Диспетчер ждёт на общем `WakeChannel`; производители и освободившиеся воркеры будят его через `notify_one`.
- У каждого воркера своя условная переменная для ожидания задачи.
- Не добавлять неограниченные очереди на воркера: текущая модель — один слот.
- Пакет с `IncomingPacket::priority` попадает в priority `Buffer`, остальные — в обычный.
- Диспетчер сначала пытается `pop` из priority `Buffer`, затем из обычного.
- Для priority-модуля диспетчер сначала пробует его выделенный `Worker`.
- Выделенный `Worker` создаётся лениво, пока `direct_workers_.size() < max_direct_workers`.
- Если выделенного воркера нет или он занят, priority-пакет идёт в общий пул по обычной стратегии.
- Когда общий воркер освобождается, диспетчер снова первым делом разбирает priority `Buffer`.
- `max_direct_workers == kAutoDirectWorkers` означает «столько же, сколько общих воркеров»; `0` отключает выделенные воркеры.
- Один выделенный воркер обслуживает один модуль; это не меняет модель «один слот на воркера».

---

## 3. Входящий путь

```text
Transport::accept
  -> IncomingPacket{data, client_fd = -1, client_address = peer_module}
  -> TDB::pass_packet
  -> UPLINK::push
  -> входной Buffer перегрузки

Диспетчер UPLINK:
  -> Buffer::pop
  -> DistributionStrategy::dispatch
  -> Worker::try_submit

Worker:
  -> ожидание задачи в своём слоте
  -> GWPPacket::deserialize
  -> PacketHandler
  -> OutgoingSink
```

Правила:

- Транспорт читает кадры и не парсит GWP.
- Транспорт передаёт `client_fd = -1`.
- UDS `client_loop` читает несколько кадров с одного принятого fd.
- Принятый fd остаётся у транспорта и закрывается при ошибке, EOF или остановке транспорта.
- UDS-соединение начинается с handshake: LinkManager отправляет имя своего модуля и priority-флаг, `UdsTransport` запоминает их и кладёт имя в `IncomingPacket::client_address`.
- Сразу после handshake `UdsTransport` регистрирует соединение в `ConnectionCache` под именем модуля-отправителя.
- `client_address` нужен для логов, для поиска endpoint'а получателя ответа и для поиска уже открытого входящего соединения в `ConnectionCache`.
- `IncomingPacket::priority` берётся из handshake и определяет очередь UPLINK и direct-воркер.
- Только диспетчер читает входной `Buffer`.
- Worker не вызывает `Buffer::pop`; он принимает задачу через `try_submit`.
- Worker никогда не вызывает транспорт напрямую.
- Worker возвращает ответ только через `OutgoingSink`.

---

## 4. Исходящий путь

```text
PacketHandler
  -> OutgoingSink
  -> TDB::submit_outgoing
  -> OUTLINK::push (priority или обычная очередь)
  -> отправитель
  -> SendCallback
  -> TransportManager::send_outgoing
  -> ConnectionCache::send (уже принятое входящее соединение)
  -> при промахе:
       resolve_endpoint
       -> send_to(module, ep)
       -> Transport::send_to
       -> UdsTransport::send_to
       -> LinkManager::send
       -> активный Link / новый Link
```

Правила:

- `PacketHandler` формирует `OutgoingPacket.module` из `IncomingPacket::client_address` (имя отправителя, полученное из UDS-handshake).
- `OutgoingPacket.priority` наследуется от входящего запроса; ответ на priority-запрос идёт через priority-очередь OUTLINK.
- `TransportManager` резолвит endpoint:
  1. `ConnectionCache::send(module, frame)`.
  2. При промахе — Cache hit -> использовать endpoint.
  3. Промах Cache -> `server_api::locate(module)`.
  4. Пустой ответ ядра -> ошибка.
  5. `save_endpoint_to_cache(module, address)`.
  6. Повторно прочитать endpoint из Cache.
- Для нового запроса `ConnectionCache` переиспользуется только при совпадении priority-режима; ответы переиспользуются безусловно.
- `TransportManager` не владеет ни пулом соединений, ни `ConnectionCache`; он только вызывает их API.
- UDS-отправка идёт через `LinkManager`.
- Ключ `LinkManager`: `type|address|port`. Priority-режим endpoint'а хранится отдельно и задаёт флаг handshake.
- `LinkManager` переиспользует существующий Link для endpoint.
- При ошибке отправки Link инвалидируется и делается одна попытка переподключения.
- Смена priority инвалидирует Link: handshake отправляется только при открытии, поэтому режим нужно сообщить новым соединением.
- `LinkManager` сейчас поддерживает только `CONNECTION_UDS`.
- `prune_idle()` — заготовка под будущий фоновый поток очистки.
- `close_all()` вызывается при остановке Loom и очищает сохранённые priority-endpoint'ы.

---

## 5. Жизненный цикл

Запуск:

```text
start(module)
  -> TDB::init
     -> UPLINK::init(workers, queue_capacity, strategy, max_direct_workers)
        -> обычный и priority Buffer
        -> пул Worker
        -> поток диспетчера
     -> OUTLINK::init(senders, queue_capacity)
  -> TransportManager
  -> зарегистрировать UdsTransport
  -> set_tdb (Link reader -> TDB)
  -> start_all
  -> TDB::set_sender(TransportManager::send_outgoing)
  -> server_api::register_module(module, endpoint)
  -> LinkManager::set_local_module(module)
  -> ConnectionCache::clear()
```

Остановка:

```text
stop()
  -> TransportManager::stop_all()
     -> UdsTransport::stop()
        -> shutdown входящих соединений
        -> join client-loop потоков
        -> close fd
     -> LinkManager::set_incoming_sink(nullptr)
     -> LinkManager::close_all()
  -> TDB::shutdown()
     -> UPLINK::shutdown()
     -> OUTLINK::shutdown()
  -> ConnectionCache::clear()
  -> server_api::unregister_module(module)
  -> g_running = false
```

Правила:

- Сначала перестать принимать новые пакеты, затем дочитывать TDB.
- Link reader'ы нужно остановить до `TDB::shutdown()`, иначе поток может обратиться к уничтоженному TDB.
- Входящие fd закрываются только после join соответствующих client-loop потоков, чтобы `close()` не гонялся с `recv()`.
- Диспетчер останавливается после опустошения `Buffer`, но до воркеров: он должен успеть раздать остаток.
- Воркеры останавливаются после диспетчера: они дочитывают свой слот.
- Уничтожать `TransportManager` только после того, как TDB дочитан.
- Закрывать LinkManager и ConnectionCache только после TDB и TransportManager.
- `stop()` снимает модуль с регистрации в имитации ядра.

---

## 6. Жёсткие границы

Это реальные способы сломать архитектуру.

- Не заставлять воркеров читать входной `Buffer` или конкурировать за задачи.
- Не превращать входной `Buffer` в очередь задач воркеров.
- Не обходить `DistributionStrategy` при выборе воркера.
- Не добавлять неограниченные очереди на воркера.
- Не блокировать транспорт, пока все воркеры заняты: пакет остаётся в `Buffer` или отбрасывается.
- Не использовать одну общую условную переменную для слотов всех воркеров.
- Не возвращать старую модель, где все воркеры делают `pop` из общей очереди.
- Не парсить GWP в транспорте.
- Не резолвить `module -> endpoint` в транспорте.
- Не передавать fd входящего соединения в TDB; `client_fd` всегда `-1`.
- Не отдавать fd бизнес-логике: ответ может идти по принятому fd только через `ConnectionCache`.
- Не закрывать принятый fd после первого кадра.
- Не считать, что Cache всегда заполнен; при промахе обязателен запрос к ядру.
- Не менять порядок остановки.
- Не класть `module_name`, `endpoint` или `fd` в GWP.
- Не помещать бизнес-логику в Worker; использовать `PacketHandler`.
- Не включать старую стратегию, которая выбирает воркера в обход диспетчера.
- Не помещать `LinkManager` или `ConnectionCache` внутрь TDB, Worker, OUTLINK или TransportManager.
- Не вычитывать обычный `Buffer` раньше priority `Buffer`.
- Не создавать выделенный Worker сверх `max_direct_workers`.
- Не отправлять ответ на priority-запрос через обычную очередь OUTLINK.

---

## 7. Текущие ограничения

- `DistributionStrategy` по умолчанию — только round-robin среди свободных воркеров; priority-путь использует её как fallback.
- Нет тонких стратегий по загрузке; привязка к воркеру есть только у direct-модулей.
- У воркера один слот; отдельной очереди задач у него нет.
- При полном входном `Buffer` пакеты отбрасываются; обратного давления нет.
- Диспетчер ждёт с таймаутом 1 мс; отдельного событийного режима нет.
- Нет метрик задержки диспетчеризации и загрузки воркеров, только счётчики.
- `server_api::locate` — файловая имитация ядра, а не реальный запрос.
- Имитация ядра не имеет heartbeat, TTL и авторизации.
- UDS-handshake передаёт имя модуля и priority-флаг, но не проверяет имя и не аутентифицирует.
- Кодек `rpc_codec` поддерживает только `int`, `double`, `float`, `bool`, `string`, `void`; формат host-endian и не версионирован.
- `loom_call` синхронный; вложенный вызов из обработчика занимает воркер на всё время ожидания ответа.
- `module_send` — это вызов метода `message` с одним строковым аргументом, а не произвольный payload.
- `LinkManager` поддерживает только UDS.
- Фоновый поток очистки для `prune_idle()` и `ConnectionCache` не запущен.
- Нет лимита размера пула соединений и метрик.
- Входящие соединения обслуживаются потоком на соединение; epoll пока нет.
- `worker/pool.*` и `worker/rpc_manager.*` — неучаствующие заготовки.

---

## 8. Карта файлов

| Путь | Роль |
|---|---|
| `src/core/loom.cpp` | Жизненный цикл, сборка компонентов, PacketHandler, `loom_call` |
| `src/core/rpc_codec.*` | Кодек аргументов/результата RPC |
| `src/core/rpc_state.*` | Ожидающие RPC-вызовы: packet_id → response |
| `src/gateway/TDB/tdb.*` | Композиция UPLINK + OUTLINK |
| `src/gateway/TDB/uplink/uplink.*` | Входной Buffer + диспетчер + пул воркеров |
| `src/gateway/TDB/strategy/strategy.*` | `DistributionStrategy`, `RoundRobinStrategy` |
| `src/gateway/TDB/worker/worker.*` | Один слот задачи, GWP-разбор, `PacketHandler` |
| `src/gateway/TDB/outlink/outlink.*` | Общая исходящая очередь + пул отправителей |
| `src/gateway/TDB/common/wake_channel.hpp` | Мьютекс и условная переменная диспетчера |
| `src/gateway/TDB/buffer/buffer.*` | MPMC `RingBuffer<T>`, структуры пакетов |
| `src/transport/transport.h` | Контракт транспорта |
| `src/transport/transport_manager.*` | Роутинг и резолв через Cache/ядро |
| `src/transport/local/uds_transport.*` | UDS: `accept`, `send`, `client_loop` |
| `src/transport/local/uds.*` | Низкоуровневые UDS-функции |
| `src/transport/link.hpp/.cpp` | `Link` (duplex) и `LinkManager` |
| `src/transport/connection_cache.hpp/.cpp` | `InboundConnection` и `ConnectionCache` |
| `src/transport/handshake.hpp` | UDS-handshake с именем модуля и priority-флагом |
| `src/discovery/cache.*` | `module -> endpoint` |
| `src/core/server_core_api.*` | Имитация ядра: файловый реестр `module -> endpoint/priority` |
| `src/protocol/gwp.*` | GWP-сериализация |
| `brutal_tester/tests/test_tdb.cpp` | Тесты Buffer, диспетчера, стратегии, воркеров, priority-воркеров |
| `brutal_tester/tests/test_transport.cpp` | Тесты транспорта, LinkManager и переиспользования входящих соединений |
| `brutal_tester/tests/test_connect.cpp` | Тесты резолва, реестра имитации ядра и handshake |
| `brutal_tester/tests/test_rpc.cpp` | Сквозной тест: два модуля, `loom_call`, TDB, ответ, priority |
| `examples/calc_server.cpp` | Пример модуля-сервера |
| `examples/calc_client.cpp` | Пример модуля-клиента |

---

## 9. Чек-лист изменений

1. Сначала смотреть фактический код.
2. Сохранять владение: Buffer и диспетчер принадлежат UPLINK, воркеры не читают Buffer.
3. Сохранять правило: только `DistributionStrategy` выбирает воркера.
4. Сохранять один слот на воркера; не добавлять неограниченные очереди.
5. Сохранять отправку UDS через `LinkManager`, но сначала пробовать `ConnectionCache`.
6. Сохранять резолв при промахе Cache в `TransportManager`.
7. Сохранять priority-порядок: priority `Buffer` раньше обычного, priority OUTLINK раньше обычного.
8. Обновлять `test_tdb.cpp` / `test_transport.cpp` / `test_connect.cpp` / `test_rpc.cpp` при изменении поведения.
9. Запускать обычные тесты, ASan/UBSan и TSan.
10. Будущие задачи писать в `TODO.md`, а не в этот файл.
11. При изменении публичного API обновлять `README.md` и примеры в `examples/`.
