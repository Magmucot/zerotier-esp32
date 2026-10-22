# Технический аудит Boilerplate4u/zerotier-esp32

Дата проверки: 22 октября 2026 г.  
Ревизия репозитория: 8a5210ec839ab2a2ac33ea56bc914f72682ba9dd (main).  
Встроенный ZeroTierOne: 55aa87c3728c603ddd49b369eaa880ae48a60fc1 (1.16.1-14-g55aa87c37).  
Сборочная среда: ESP-IDF v5.3.5, target esp32s3.

Исходники проекта не менялись. Отчёт, локальные пробы, журналы и сборочные артефакты расположены отдельно от репозитория в /workspace/zerotier-review. Рабочее дерево upstream чистое.

## 1. Executive Summary

Реальная готовность — исследовательский прототип. Работоспособность полного ZeroTier-клиента на ESP32 не подтверждена. Проект содержит два не сведённых пути: legacy приложение main/main.cpp и более развитый ESP32-ZeroTier-Lite адаптер. Собранный app использует legacy entry point; он передаёт нулевой ZT_Node_Config и падает при создании Node, а также не вызывает ZT_Node_join. Lite код компилируется, но текущая прошивка его не запускает.

Подтверждены опасные ошибки: malformed HELLO вызывает abort до аутентификации; NetworkConfig может писать за границы массивов capability/tag; lwIP callback после timeout обращается к объекту на уже освобождённом стеке; multicast путь потенциально расходует больше стека, чем выделено owner task. Исходный код также содержит приватную тестовую ZeroTier identity и по умолчанию печатает части сессионных ключей.

ESP-IDF 5.3.5 сборка для ESP32-S3 прошла: 1080 шагов, образ 760224 байта, 288352 байта свободно в минимальном app-разделе 1 MiB. Это подтверждает компилируемость legacy-приложения, но не корректность запуска Lite или dataplane. Host selftest и отдельные пробы подтверждают локальные свойства кода; тесты UDP/TCP были loopback, не ZeroTier overlay.

QEMU dataplane, реальная ESP32, авторизованная тестовая сеть, ARP/ICMP/TCP/UDP по виртуальной сети и восстановление после обрыва не проверялись. Внешний /qemu-project, на который ссылается README, в окружении отсутствовал. Поэтому README утверждения о прежних QEMU результатах не были независимо подтверждены на текущем HEAD.

Для пригодного клиента нужно сначала выбрать и запускать Lite entry point, исправить ошибки Node/bootstrap и parser boundaries, убрать тестовый секрет/логи ключей, вернуть канонический ZeroTier PoW, устранить lwIP lifetime и stack ошибки, подключить managed route hook к реальному IDF hook, затем пройти QEMU и аппаратные end-to-end тесты.

## 2. Текущая архитектура

Legacy путь в main/main.cpp создаёт ZeroTier Node через C API и гоняет background tasks. Вызов ZT_Node_new передаёт config=nullptr (строка 77), а Node.cpp без проверки копирует структуру конфига. Join сети в main отсутствует; старые frame callbacks только логируют. Storage callback содержит тестовую identity.

ESP32-ZeroTier-Lite состоит из ESP32LitePort, owner task, bounded очередей и frame pool, UDP underlay, ESP32LwIPGlue, виртуального EthernetTap и ESP32NetworkState для managed IPv4/route/multicast. Это наиболее перспективная архитектура. Однако подтверждённая сборка связывает app_main, zt_main_task и zt_virtual_frame_impl legacy приложения; Lite start entry point не является выбранным приложением.

Копия ZeroTierOne основана на baseline 55aa87c. Сверка с upstream ZeroTierOne/libzt выявила изменения примерно в двадцати основных node-файлах, а также service/osdep. Часть диффа добавляет диагностику и экспериментальные дампы. Значимые отличия: исключения Identity заменены abort; PoW память снижена до 64 KiB; ограничения массива NetworkConfig встраиваются в compile-time ABI; добавлен отдельный ESP32 порт. Это не libzt socket API, а отдельная ограниченная адаптация ZeroTierOne C API для IPv4/lwIP. Полная функциональность desktop ZeroTierOne не обещана: IPv6 и default route для первой версии вне объёма.

Основная фильтрация ZeroTier network rules и проверка подписи NetworkConfig сохранены относительно базовой версии. Подтверждённого обхода ACL не найдено. Но ошибки parser и маршрутизации остаются существенными.

История HEAD содержит 11 коммитов проекта. В GitHub metadata найден один открытый issue #1 и ноль открытых PR на момент получения данных. Комментарии issue включают пользовательские сообщения и патчи, но они не перепроверены и не считаются результатом этого аудита.

Корневая и компонентная CMake конфигурация жёстко использует путь /project. Сборка воспроизводится, если репозиторий примонтирован именно туда; из другого пути CMake не находит исходники. Dockerfile.dev не устанавливает ESP-IDF, а compose ожидает внешний /qemu-project.

## 3. Матрица готовности компонентов

Статусы: 1 — реализовано и протестировано; 2 — реализовано, но целевой сценарий не тестировался; 3 — частично реализовано; 4 — отсутствует; 5 — подтверждённая ошибка.

| Компонент | Статус | Реализация, evidence и проблемы |
|---|---:|---|
| Создание ZeroTier Node | 5 | Legacy main вызывает ZT_Node_new с null config; воспроизведён SIGSEGV. Lite имеет вызов в ESP32LitePort.cpp:123, но не запускается текущим app. |
| Identity: создание и загрузка | 5 | Legacy NVS storage и Lite state callbacks. Публичный приватный test identity в zt_storage_esp.cpp; PoW override несовместим с базовым ZeroTier. Provisioning и reboot не проверены. |
| Подключение к Central/control plane | 3 | ZeroTier core содержит discovery/HELLO/config request; Lite join существует. Legacy main не вызывает ZT_Node_join, Lite runtime не проверен. |
| Авторизация в private network | 3 | NetworkConfig/controller путь существует, но нет авторизованного тестового Network ID; parser имеет OOB на caps/tags. |
| NetworkConfig | 5 | При двух capability/tag parser пишет за конец массивов размером 1. Лимиты routes/IP также могут приводить к усечению. |
| Managed IPv4 | 3 | ESP32NetworkState::_syncIps и EthernetTap. Подтверждены баги при замене заполненного списка и изменении маски. Реальный netif не проверен. |
| Managed routes | 5 | Есть route state и hook, но IDF 5.3.5 уже задаёт другой IP4 route hook. Фактические lwIP relocations не ссылаются на ZeroTier hook; arbitrary custom routes не подтверждены. |
| VL1/VL2 | 2 | Core собран и host selftest прошёл. ESP32 callback/frame path не прогонялся end-to-end. |
| Обнаружение peers | 3 | Core и UDP path реализованы. Нестандартный PoW лимит мешает проверке неизвестных peers; malformed HELLO валит Node. |
| NAT traversal | 5 | Core traversal есть, но UDP socket остаётся с TTL=2 после probe. Реальный NAT traversal не проверялся. |
| Ethernet frames | 3 | Lite EthernetTap/frame pool есть; legacy callback лишь логирует. Lite не является текущим application entry point; multicast может переполнить stack. |
| ARP | 2 | Инфраструктура lwIP/ZeroTier multicast есть, но overlay ARP не запускался. |
| ICMP | 2 | Ожидается через виртуальный IP netif, overlay ping не тестировался. |
| TCP | 2 | lwIP TCP должен идти через netif; host TCP loopback тест не является overlay тестом. |
| UDP | 2 | UDP underlay и lwIP overlay path есть; loopback socket проба подтвердила лишь дефект TTL. |
| lwIP integration | 5 | Use-after-return при задержанном callback подтверждён ASan; route hook конфликтует с IDF. |
| Reconnect после потери underlay | 3 | Core имеет retry, но Wi-Fi event → повторный join и восстановление не подтверждались. |
| Восстановление после reboot | 3 | NVS есть, но erase может стереть всё хранилище; аппаратный power-cycle тест отсутствует. |
| Защита от malformed packets | 5 | Identity abort и NetworkConfig OOB воспроизведены. Нет fuzz/negative corpus. |

Ни ARP, ICMP, TCP, ни UDP через виртуальную ZeroTier сеть в этой проверке не получили статус «реализовано и протестировано».

## 4. Результаты сборки и тестирования

**Выполнено:**

- ESP-IDF v5.3.5, target esp32s3: idf.py build завершился успешно, 1080/1080 шагов. Лог: evidence/idf-canonical-build.log.
- zt_esp32.bin — 760224 байта; минимальный app partition — 1 MiB, свободно 288352 байта (27%). idf_size: flash code 486750 B, flash data 206204 B, DIRAM 67611/341760 B, IRAM 16383/16384 B. Лог: evidence/idf-size.log.
- В логе сборки нет строк compiler warning/error. Однако CMake делает ряд предупреждений нефатальными и включает отладочные -Og/-g3/-fno-inline.
- В ELF присутствуют app_main, zt_main_task и zt_virtual_frame_impl. Lite start не является точкой входа. Лог: evidence/linked-entrypoints.log.
- Host core selftest завершился exit 0. Он покрывает локальные crypto/protocol tests и socket loopback, а не overlay. Логи: evidence/selftest.log и evidence/selftest-build.log.
- Сгенерированные с 2 MiB и 64 KiB identity валидируются только при том же параметре. Логи: evidence/identity-*-validate-*.log.
- Host probes с sanitizer подтвердили OOB NetworkConfig, SIGABRT malformed HELLO и stack-use-after-return lwIP callback. State и TTL probes подтвердили описанные локальные расхождения.

**Не выполнено:**

- QEMU не запускался: /qemu-project отсутствовал и QEMU target/runtime не был доступен.
- Физическая ESP32-S3 не подключалась: heap/stack high-water, watchdog, PoW latency, flash/NVS recovery и длительная стабильность не измерялись.
- Не было собственного Network ID/controller и авторизованной ZeroTier сети. Идентификаторы/секреты из репозитория не использовались.
- Не проверялись join → ONLINE → NetworkConfig → managed IP → peer → ARP → ICMP/TCP/UDP, NAT traversal, reconnect или ACL на сетевом пути.

## 5. Подтверждённые ошибки

Для каждого дефекта указаны приоритет, место, причина, последствия, доказательство/воспроизведение, исправление, регрессионный тест и уверенность.

### P0-1. Legacy app передаёт null конфигурацию Node

1. **Приоритет:** P0, блокер старта собранного app.
2. **Файлы:** main/main.cpp:77; ZeroTierOne/node/Node.cpp:80.
3. **Причина:** ZT_Node_new вызывается с config=nullptr, затем Node конструктор копирует config через memcpy без проверки.
4. **Последствие:** SIGSEGV во время startup, до работы ZeroTier.
5. **Доказательство:** evidence/node-null-config.log — host probe завершилась SIGSEGV; ELF содержит legacy app_main.
6. **Воспроизведение:** тот же вызов ZT_Node_new с null config.
7. **Исправление:** передавать валидный ZT_Node_Config и callbacks либо перевести app на Lite bootstrap; добавить защиту API от null.
8. **Регрессия:** null config возвращает ошибку, не падая; на S3 проверить успешное создание Node.
9. **Уверенность:** высокая, воспроизведено.

### P0-2. Malformed HELLO вызывает удалённый abort

1. **Приоритет:** P0, удалённый отказ в обслуживании.
2. **Файлы:** ZeroTierOne/node/Identity.hpp:49,143,246,255; IncomingPacket.cpp:479-492.
3. **Причина:** исключения Identity заменены на abort; _doHELLO десериализует Identity до проверки аутентифицированного peer.
4. **Последствие:** некорректный HELLO может завершить Node; ESP-IDF может перезагрузить устройство в зависимости от panic config.
5. **Доказательство:** ZT_Node_processWirePacket с malformed unauthenticated HELLO завершился SIGABRT: evidence/node-malformed-hello.log. В baseline upstream ошибки обрабатываются исключениями.
6. **Воспроизведение:** отправить повреждённый HELLO в изолированной тестовой сети или запустить host probe.
7. **Исправление:** преобразовать deserialize в recoverable error и отбрасывать повреждённый пакет без abort.
8. **Регрессия:** malformed identity corpus/fuzz; после мусорного пакета корректный HELLO должен приниматься.
9. **Уверенность:** высокая, воспроизведено.

### P0-3. NetworkConfig parser пишет за массивы capability/tag

1. **Приоритет:** P0, повреждение памяти.
2. **Файлы:** components/zerotier_node/CMakeLists.txt:68-69 задаёт capacity 1; ZeroTierOne/node/NetworkConfig.cpp:493-505 и 507-519.
3. **Причина:** parser увеличивает count и записывает все сериализованные элементы в массив размером 1 без проверки capacity.
4. **Последствие:** повреждение соседних полей и crash. Конфигурация должна иметь подпись controller; для атаки нужны полномочия controller либо его компрометация, это не произвольный анонимный пакет.
5. **Доказательство:** UBSan с двумя capability сообщает index 2 out of bounds для Capability[1] на строке 504; два tag дают такое же на строке 518. Логи evidence/parser-caps.log, parser-tags.log.
6. **Воспроизведение:** передать тестовую сериализованную NetworkConfig с двумя caps или tags при проектных define.
7. **Исправление:** проверять capacity до каждой записи; при переполнении целиком отклонять config и сохранять предыдущее состояние.
8. **Регрессия:** ASan/UBSan для 0, max и max+1, проверка атомарного отказа.
9. **Уверенность:** высокая, sanitizer воспроизведение.

### P0-4. lwIP callback разыменовывает stack context после timeout

1. **Приоритет:** P0, use-after-return/возможная порча памяти.
2. **Файлы:** ZeroTierOne/osdep/ESP32LwIPGlue.cpp:109-128; callback использует ZtLwipCall около строки 30.
3. **Причина:** captureTcpipThread передаёт адрес локального объекта callback-у, ждёт timeout, освобождает semaphore и выходит. Поздний callback получает уже недействительный stack pointer.
4. **Последствие:** use-after-return и crash при задержке tcpip thread.
5. **Доказательство:** delayed callback harness под ASan сообщает stack-use-after-return: evidence/lwip-lifetime.log.
6. **Воспроизведение:** задержать обработчик tcpip callback дольше timeout capture.
7. **Исправление:** использовать устойчивый heap/member/ref-counted context; callback context и semaphore нельзя освобождать до гарантированного завершения/cancel.
8. **Регрессия:** delayed callback до и после timeout под ASan; startup при загруженной tcpip thread.
9. **Уверенность:** высокая, ASan воспроизведение.

### P0-5. Multicast stack frame превышает stack owner task

1. **Приоритет:** P0 для Lite dataplane; аппаратный crash пока не воспроизведён.
2. **Файлы:** ZeroTierOne/node/Multicaster.cpp:158-160; ESP32LitePort.cpp:140; размер task около 32768 байт, ESP32LitePort.hpp:46.
3. **Причина:** idxbuf[4096] занимает 16 KiB на 32-bit Xtensa; compiler stack frame Multicaster::send около 32096 B, Packet::compress добавляет около 5168 B, не считая callers.
4. **Последствие:** переполнение stack при multicast, включая важный для ARP/address resolution путь.
5. **Доказательство:** Xtensa compiler prologue report evidence/xtensa-stack-prologues.log; S3 runtime не запускался.
6. **Воспроизведение:** вызвать multicast send в Lite с текущим owner stack и включённым stack canary.
7. **Исправление:** убрать большой локальный массив, применить bounded reusable buffer/итеративный алгоритм; увеличение стека само по себе может быть неприемлемо по RAM.
8. **Регрессия:** максимум multicast кадра на S3, stack canary и достаточный high-water запас.
9. **Уверенность:** средне-высокая: размер frame измерен, runtime stack crash не подтверждён.

### P0-6. Вшитый приватный test identity опубликован в исходниках

1. **Приоритет:** P0, credential compromise.
2. **Файл:** components/zerotier_node/port/zt_storage_esp.cpp:14-15,70-80.
3. **Причина:** приватная и публичная test identity находятся в tracked source и возвращаются storage callback.
4. **Последствие:** все устройства с fallback разделяют один identity; любой читатель репозитория знает private material. Identity нужно считать скомпрометированной и не использовать в сети.
5. **Доказательство:** секрет читается из source при запросе private/public identity state object.
6. **Воспроизведение:** legacy storage read при отсутствующей identity в NVS.
7. **Исправление:** убрать fallback; clean NVS должен возвращать provisioning error, пока уникальная identity не загружена. Тестовые ключи вынести из production build.
8. **Регрессия:** тест чистого NVS и сканирование production image/source на известный test secret.
9. **Уверенность:** высокая в наличии секрета; используется ли этот callback в конкретной прошивке — зависит от выбранного entry point.

### P1-1. PoW memory override несовместим с ZeroTier identity

1. **Приоритет:** P1, peer discovery/совместимость.
2. **Файлы:** components/zerotier_node/CMakeLists.txt:76; ZeroTierOne/node/Identity.cpp:26-27.
3. **Причина:** порт задаёт 65536 байт, upstream использует 2 MiB.
4. **Последствие:** identity взаимно не проходит locallyValidate при противоположных параметрах; identity порта нестандартна, unknown peers могут отвергаться на HELLO. WHOIS/topology путь может смягчить последствия, поэтому полную потерю соединений без сетевого теста не утверждаем.
5. **Доказательство:** cross-validation сгенерированных identity проходит только при совпадающем параметре; четыре лога identity-*-validate-*.log.
6. **Воспроизведение:** локальные identity probes с обоими значениями.
7. **Исправление:** вернуть канонический параметр; оценить S3 время/RAM, либо provisioning identity выполнять вне устройства, не меняя протокол.
8. **Регрессия:** cross-validate identity с upstream CLI и проверить peer discovery.
9. **Уверенность:** высокая в несовместимости; средняя в конкретном сетевом эффекте.

### P1-2. ESP32 managed route hook не подключён к фактическому lwIP hook

1. **Приоритет:** P1 для gateway/custom managed routes.
2. **Файлы:** ZeroTierOne/osdep/ESP32LwIPHooks.h:28-29, ESP32NetLink.cpp; ESP-IDF lwIP lwipopts.h задаёт ip4_route_src_hook.
3. **Причина:** ZeroTier define находится под #ifndef, поэтому остаётся существующий IDF hook. В скомпилированном объекте relocations идут к ip4_route_src_hook, а не к zt_esp32_lwip_ip4_route_src.
4. **Последствие:** connected subnet может работать штатно, но custom routes из ESP32NetLink не участвуют в route lookup. Default route архитектурой отдельно исключён.
5. **Доказательство:** evidence/lwip-route-relocations.log и фактическое IDF hook определение.
6. **Воспроизведение:** проверить relocations после IDF build; сетевой сценарий — назначить маршрут к удалённой managed subnet через gateway.
7. **Исправление:** интегрировать managed route selection в настоящий IDF hook с сохранением underlay поведения.
8. **Регрессия:** direct-subnet, overlay gateway и underlay route precedence на QEMU/S3.
9. **Уверенность:** высокая, что custom функция не вызывается; runtime маршрутизация требует end-to-end теста.

### P1-3. UDP TTL probe меняет TTL последующих пакетов

1. **Приоритет:** P1, underlay/NAT reliability.
2. **Файл:** ZeroTierOne/osdep/ESP32LitePort.cpp:303-319; NAT probe TTL=2 в node/IncomingPacket.cpp:962.
3. **Причина:** callback устанавливает IP_TTL для ненулевого TTL, но при ttl=0 не восстанавливает default.
4. **Последствие:** дальнейший обычный трафик по тому же UDP socket выходит с TTL 2 и может быть отброшен маршрутизаторами.
5. **Доказательство:** host socket probe показала initial TTL 64 и ttl_after_probe_and_normal_send=2: evidence/ttl.log.
6. **Воспроизведение:** последовательность probe TTL 2, затем обычная отправка ttl 0.
7. **Исправление:** восстановить сохранённое значение/default для ttl=0 либо применять per-packet TTL.
8. **Регрессия:** socket option до/после probe и NAT traversal на тестовой сети.
9. **Уверенность:** высокая, локально воспроизведено.

### P1-4. Сессионные ключи печатаются в логи

1. **Приоритет:** P1; выше при доступности serial console.
2. **Файлы:** ZeroTierOne/node/IncomingPacket.cpp:35-36,122-123; Peer.cpp:24-25,112; дополнительные capture/debug ветки в Packet.cpp/Topology.cpp.
3. **Причина:** ZT_ESP32_STEP45_DEBUG_KEYS по умолчанию равен 1; printf содержит key16 и производные session key данные.
4. **Последствие:** UART/log collector получает секретный материал активной сессии; риск раскрытия/имитации соответствующего peer до обновления ключа.
5. **Доказательство:** активные по умолчанию printf присутствуют в source; фактический serial log на плате не снимался.
6. **Воспроизведение:** собрать без дополнительных define и выполнить peer handshake в изолированной сети.
7. **Исправление:** удалить ключевые дампы и захваченные real packet ветки; оставить opt-in диагностику без секретов.
8. **Регрессия:** поиск secret fields в release source и анализ serial log.
9. **Уверенность:** высокая в активном коде, не проверено фактическое получение peer log на плате.

### P1-5. Замена managed IP теряет адреса, а смена маски игнорируется

1. **Приоритет:** P1, connectivity после config update.
2. **Файл:** ZeroTierOne/osdep/ESP32NetworkState.cpp:150-186; InetAddress::ipsEqual в node/InetAddress.hpp:452 сравнивает IP без маски.
3. **Причина:** сначала добавляются новые IP, затем удаляются старые; при полном списке из 4 slots новые адреса некуда добавить. Тот же IP с другой маской считается прежним.
4. **Последствие:** полная замена заполненного списка может очистить все адреса; смена prefix останется неприменённой.
5. **Доказательство:** state harness получил full_address_replacement count=0 expected=4 и сохранил /24 после запроса /16: evidence/state-lifecycle.log.
6. **Воспроизведение:** заменить 4 managed IP на другие 4; затем поменять prefix без смены IP.
7. **Исправление:** вычислять diff и применять атомарно, сравнивать IP вместе с prefix.
8. **Регрессия:** empty/partial/full list, mask update и идентичная повторная config.
9. **Уверенность:** высокая, host harness воспроизведение.

### P1-6. Rejoin на другой nwid сохраняет старый TAP/multicast state

1. **Приоритет:** P1, если поддерживается rejoin/multiple network; иначе P2 до фиксации single-network ограничения.
2. **Файл:** ZeroTierOne/osdep/ESP32NetworkState.cpp:73-128, особенно _ensureTap около 94 и _clearIps около 128.
3. **Причина:** существующий tap повторно используется без сверки nwid/MAC; DOWN очищает IP, но не полностью обновляет multicast membership state.
4. **Последствие:** после DOWN → join другой сети TAP может оставаться связан со старой сетью, ARP/multicast ломается.
5. **Доказательство:** host lifecycle harness применил nwid A, DOWN, nwid B; tap creation был только для A, multicast subscription counts не синхронизировались: evidence/state-lifecycle.log.
6. **Воспроизведение:** последовательность config A, DOWN, config B.
7. **Исправление:** либо явно отвергать вторую сеть и документировать single-network, либо иметь отдельный tap/state на каждый network ID.
8. **Регрессия:** DOWN/DESTROY/rejoin, смена MAC/nwid и membership.
9. **Уверенность:** высокая для текущего единственного состояния; точное поведение target dataplane не запускалось.

### P2-1. Публичная структура имеет разные ABI из-за PRIVATE compile definitions

1. **Приоритет:** P2, интеграционная совместимость.
2. **Файлы:** components/zerotier_node/CMakeLists.txt:64-76; ZeroTierOne/node/NetworkConfig.hpp и include/ZeroTierOne.h.
3. **Причина:** массивы в публичных структурах меняются compile macros, переданными только PRIVATE компоненту.
4. **Последствие:** сторонний app TU может иметь другой sizeof/offset структуры, чем библиотека.
5. **Доказательство:** abi-default.log и abi-lite.log показывают разные размеры с разными define.
6. **Воспроизведение:** собрать consumer без private macros и сравнить ABI.
7. **Исправление:** сделать configuration definitions публичными или стабилизировать API структуры.
8. **Регрессия:** внешняя consumer сборка и static_assert ABI.
9. **Уверенность:** высокая для разницы layout; фактической порчи в текущем app не воспроизведено.

### P2-2. NVS recovery стирает весь partition

1. **Приоритет:** P2, потеря сохранённого состояния.
2. **Файлы:** main/main.cpp:42-46; ZeroTierOne/osdep/ESP32LiteAppGlue.cpp:15-24.
3. **Причина:** при NO_FREE_PAGES/новой версии вызывается nvs_flash_erase без резервирования нужной identity.
4. **Последствие:** теряются все NVS namespaces; legacy fallback может перейти к общей тестовой identity, Lite provisioning может потеряться.
5. **Доказательство:** recovery path в исходниках; физическая NVS fault injection не запускалась.
6. **Воспроизведение:** создать NVS состояние NO_FREE_PAGES, перезагрузить и сравнить сохранённую identity.
7. **Исправление:** отдельный namespace/partition, корректный backup/restore; не стирать всё хранилище без необходимости.
8. **Регрессия:** fault injection corrupt/no-free-pages и проверка сохранности уникальной identity.
9. **Уверенность:** высокая в вызове erase, средняя в итоговом эффекте для каждой конфигурации.

## 6. Потенциальные риски

- Parser ограничивает NetworkConfig compile-time arrays; route probe показала 8 маршрутов из 9, а статические IP ограничены четырьмя. Нужен контракт: отклонять конфиг атомарно или объявить лимиты контроллеру.
- ESP32NetworkState поддерживает один tap/state, а core допускает несколько network IDs. Pending config может перезаписываться быстрыми callbacks. Нужен stress test, чтобы выяснить потерю CONFIG_UPDATE.
- _stopping, _udpSocket и RX/owner task затрагиваются разными задачами. volatile не заменяет C++ синхронизацию; подтверждённой data race не получено.
- _wireRxLoop опрашивает nonblocking socket с задержкой около 10 ms (ESP32LitePort.cpp:504-530), что добавляет latency и может потерять bursts; throughput не измерялся.
- Frame pool ограничен фиксированными буферами; timeout/error paths могут удержать слот. Нужен тест насыщения.
- esp_fill_random в zt_utils_esp.cpp требует корректного entropy источника. Не подтверждено, что key generation безопасна при всех boot режимах без активного радио/bootloader entropy.
- Route allow policy фильтрует ограниченно (ESP32NetworkState.cpp:216-228); полного анализа policy паритета upstream нет. Обход ACL не доказан.
- Реальные peak heap, fragmentation и high-water stack на ESP32-S3 не измерялись. 8 MiB PSRAM достаточно по суммарной ёмкости, если аллокатор задействует её для крупных объектов; DMA/lwIP buffers всё равно требуют internal RAM.
- Проверенный образ занимает 760224 B из app partition 1 MiB; это legacy build. IRAM занята 99.99%. Lite runtime footprint отдельно не измерен.
- NVS запись NetworkConfig/peer state нужно сверить с архитектурным требованием не использовать config как boot baseline; write rate и wear не измерялись.

## 7. Проблемы безопасности

- Приватная test identity в source считается скомпрометированной; её нельзя применять в пользовательской или приватной сети.
- Печать session keys должна быть удалена до сетевого использования. Serial console/log shipping рассматриваются как раскрытие ключа.
- Malformed HELLO crash не требует авторизации. NetworkConfig OOB проходит через доверенную подписанную конфигурацию, но может нарушить целостность устройства при компрометации controller.
- ZeroTier rule filtering и signature verification не были удалены в сравнённом core diff; подтверждённого ACL bypass нет. Нужны негативные тесты правил по protocol/port/IP и тест deauthorization.
- Проверенный sdkconfig не включает NVS encryption/flash encryption. При физическом чтении flash identity потенциально извлекается; рекомендация зависит от модели доступа, Secure Boot и аппаратной конфигурации.
- Не использовать тестовые ID/ключи/пакетные дампы из репозитория как credentials или внешние endpoints.

## 8. Проблемы производительности

- Multicaster локальный буфер 4096 unsigned long занимает 16 KiB, а compiler frame около 32 KiB. Это прямой риск stack, PSRAM проблему не решает.
- Сборка показывает статическую DIRAM 67.6 KiB legacy приложения и IRAM почти заполнена; runtime Lite heap и стек не измерены.
- Node sizeof 36672 B, Network 23296 B, NetworkConfig 10648 B, Packet 2564 B на текущей Xtensa конфигурации (evidence/xtensa-type-sizes.json). Эти объекты не все обязательно resident одновременно.
- -Og/-g3/-fno-inline раздуты для production. Отдельный release build не сравнивался.
- Нет чисел по AES/ECC CPU, PoW latency, peers capacity, queue losses, heap fragmentation или длительным сессиям. Криптографические параметры нельзя снижать ради скорости.
- Возможные безопасные оптимизации после корректности: заменить stack array на bounded reusable storage, убрать debug symbols/optimization overrides в release, выделять крупные редко используемые структуры в PSRAM, сохранить DMA/lwIP buffers во внутренней RAM, уменьшить копирование кадра после профилирования.

## 9. Отсутствующие тесты

1. Host ASan/UBSan/fuzz corpus для Identity, HELLO, NetworkConfig, rules, сертификатов, обрезанных и oversized packets.
2. External consumer ABI test с публичным header и без component-private defines.
3. NetworkState lifecycle: full IP replacement, prefix change, route update, DOWN/DESTROY/rejoin, status ACCESS_DENIED/NOT_FOUND, multicast memberships.
4. Lite application build/boot test с подтверждением, что app запускает Lite entry point.
5. QEMU: join, ONLINE, config, IP, peer, ARP, ICMP, UDP payload и TCP payload с peer-side logs и pcap в no-NAT topology.
6. ESP32-S3: cold boot, Wi-Fi interruption/recovery, NVS fault injection, watchdog, free heap и stack high-water.
7. ACL: разрешённый и запрещённый трафик по IP/protocol/port, deauthorization/revoke.
8. NAT traversal с собственным контролируемым underlay, TTL state до/после rendezvous probe.
9. Длительная peer churn/packet loss/full queue нагрузка, fragmentation и flash write rate.
10. MTU, максимальный кадр, fragmentation, multicast burst, ingress/egress concurrency.

## 10. Предложения по исправлению

1. Выбрать единый app путь и исправить Node bootstrap; успешная сборка должна запускать Lite, если это целевой продукт.
2. Устранить abort на непроверенных пакетах и bounds ошибки NetworkConfig, добавить host sanitizer/fuzz tests.
3. Удалить вшитую identity и key dumps; определить безопасный provisioning path.
4. Вернуть канонический PoW и отдельно измерить RAM/latency на S3 без изменения протокола.
5. Уменьшить multicast stack footprint и устранить lwIP callback lifetime.
6. Подключить route selection к фактическому hook IDF 5.3.5 и тестировать underlay precedence.
7. Исправить TTL и IP/TAP/multicast lifecycle; только потом тестировать reconnect.
8. Зафиксировать публичный ABI и безопасные compile options.
9. Заменить /project hardcoding на относительные CMake paths и сделать сборку воспроизводимой вне авторского контейнера.
10. После этого воспроизвести end-to-end dataplane и измерить устройство.

## 11. План Pull Requests

| Название на английском | Техническое изменение и файлы | Тесты / критерий готовности | Зависимость | Сложность / риск |
|---|---|---|---|---|
| Reject Malformed Identity Packets Without Aborting the Node | Identity.hpp, IncomingPacket.cpp; recoverable parse errors | malformed HELLO corpus/fuzz; Node жив после мусорного пакета | Нет | Средняя / низкий |
| Bounds-Check NetworkConfig Capability and Tag Arrays | NetworkConfig.cpp/.hpp и compile limits | ASan/UBSan max/max+1; переполнение целиком отклоняется атомарно | Нет | Низкая / низкий |
| Remove Embedded ESP32 Test Identity and Secret Logging | zt_storage_esp.cpp, Peer.cpp, IncomingPacket.cpp, Packet.cpp | clean NVS выдаёт provisioning error; image/log не содержит test secret/key dump | Нет, до hardware network test | Низкая / низкий |
| Use Canonical ZeroTier Identity Proof-of-Work Parameters | component CMake/Kconfig/provision docs | mutual validation upstream ↔ port; S3 RAM/time measured | Offline provisioning decision | Средняя / средний |
| Reduce ESP32 Multicast Stack Usage | Multicaster.cpp, Packet.cpp, owner stack defaults | Xtensa stack report и ARP/multicast под stack canary | Нет | Средняя / средний |
| Keep lwIP Callback Context Alive After Timeout | ESP32LwIPGlue.cpp/.hpp | delayed callback ASan и startup под нагрузкой без UAF | Нет | Средняя / низкий |
| Wire Managed IPv4 Routes Into ESP-IDF 5.3 lwIP Hook | ESP32LwIPHooks.h, ESP32NetLink.cpp, IDF config | фактический route hook вызывается; overlay gateway и underlay тесты | Нужен runnable Lite target | Высокая / высокий |
| Restore UDP TTL After ZeroTier Rendezvous Probes | ESP32LitePort.cpp | socket TTL state test + controlled NAT traversal | Нет | Низкая / низкий |
| Apply Network Address Updates Atomically | ESP32NetworkState.cpp, EthernetTap | full replacement, prefix, DOWN/rejoin, multicast lifecycle | Нет | Средняя / средний |
| Build ESP32 Lite Outside a Fixed /project Path | root/component CMake, Docker setup, example app | чистый IDF container собирает Lite entry point из произвольного path | Bootstrap PR | Средняя / средний |
| Initialize the ZeroTier Node With a Valid Configuration | main/main.cpp либо замена legacy app на Lite glue | boot smoke test; ZT_Node_new успешен и join вызывается | Согласовать с Lite build PR | Низкая / низкий |
| Make ZeroTier Public C API Struct Layout Stable | component definitions и public headers | внешний consumer ABI test | Решить контракт API | Средняя / средний |

### Топ-5 наиболее полезных PR

1. Reject Malformed Identity Packets Without Aborting the Node.
2. Bounds-Check NetworkConfig Capability and Tag Arrays.
3. Remove Embedded ESP32 Test Identity and Secret Logging.
4. Reduce ESP32 Multicast Stack Usage.
5. Keep lwIP Callback Context Alive After Timeout.

Для практического устройства build/bootstrap PR должен идти до end-to-end проверки. Следом — canonical PoW, route hook, TTL и state lifecycle.

## 12. Архитектурные рекомендации

Для ESP32-S3 с 8 MiB PSRAM основой может стать Lite: ZeroTier Node owner task, UDP underlay, frame pool, callbacks и lwIP netif. Исправить нужно entry point, provisioning, parser safety, stack, lwIP callback ownership, route hook и reconnect state.

Для задачи HTTP API кнопок питания нужны state machine offline/joining/authorized/online/retry; Wi-Fi events и bounded backoff; безопасное хранение identity; статус API; HTTP server, ограниченный ZeroTier netif/ACL; аутентификация запросов и защита от повторной команды; безопасная GPIO логика и watchdog. HTTP endpoint нельзя случайно открыть на Wi-Fi underlay.

Для узла в пределах одной managed IPv4 подсети штатного lwIP netif может быть достаточно. Gateway/custom routes требуют корректного IDF route hook; отдельный lwIP fork заранее не нужен, если hook можно безопасно составить с действующим ESP-IDF hook. IPv6/default route — отдельная будущая работа.

PSRAM увеличивает ёмкость heap, но не решает нехватку internal RAM для Wi-Fi/lwIP/DMA/interrupt paths и не увеличивает FreeRTOS stack. Замерить free heap каждого memory capability, stack high-water, frame-pool exhaustion, watchdog и reconnect на реальной S3. Для identity использовать unique provisioning и рассмотреть flash encryption/Secure Boot под физическую модель угроз.

## 13. Открытые вопросы и ограничения проверки

- Без физической платы stack finding не подтверждён runtime crash; compiler frame уже превышает выделенный stack вместе с обязательными callees.
- QEMU проект не находился в репозитории/среде, поэтому README QEMU claims на текущей ревизии независимо не повторены.
- Своей авторизованной сети не было; join, ACL и overlay protocol tests не запускались.
- Пользовательские сообщения GitHub issue не перепроверялись.
- Host selftest и loopback пробы не заменяют ESP32/QEMU dataplane.
- Успешная IDF сборка относится к legacy app, а не Lite application.
- Secure Boot, аппаратная ревизия, NVS encryption и предпочтительный provisioning flow требуют отдельного решения для целевого устройства.
- Route hook наблюдение относится к IDF 5.3.5; другая версия может иметь другую конфигурацию hook.

### Доказательства в каталоге evidence

- idf-canonical-build.log, idf-size.log, linked-entrypoints.log
- selftest.log, host-probes-final.log
- node-null-config.log, node-malformed-hello.log
- parser-caps.log, parser-tags.log, parser-routes.log
- lwip-lifetime.log, lwip-route-relocations.log
- identity-*-validate-*.log, ttl.log, state-lifecycle.log
- xtensa-stack-prologues.log, xtensa-type-sizes.json
- git-history.log, github-metadata.json, upstream-differences.json
