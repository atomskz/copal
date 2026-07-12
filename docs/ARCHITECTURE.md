# copal — Архитектура

Статус: **актуально** (соответствует коду по состоянию на Этап 7). Версия: 1.0.
Проект: **copal** · CMake-таргет `copal` (alias `copal::copal`) · публичный префикс
функций `cl_`, типы `snake_case`+`_t` (`cl_widget_t`), макросы/enum `CL_*` (CODESTYLE
§2, ADR-012) · лицензия **GPL-3.0-or-later**.

Документ описывает архитектуру **как она реализована**. Ранние версии (0.1–0.2)
были проектным черновиком в нотации `Gui*`/`GUI_*` и содержали задел, часть
которого реализована иначе или отложена; такие места отмечены явно
(«**не реализовано в MVP**», «**проще проектного эскиза**»). Фактическое дерево
файлов — в [STRUCTURE.md](STRUCTURE.md); публичные сигнатуры — в [API.md](API.md).

## 0. Зафиксированные решения (ADR-сводка)

| Ось | Решение | ADR | Статус |
|-----|---------|-----|--------|
| Платформа | SDL2 за интерфейсом `platform` | ADR-001 | реализовано (+ mock) |
| Renderer | OpenGL 3.3 core за интерфейсом, SDF-примитивы + glyph-атлас | ADR-002 | реализовано (+ mock) |
| Renderer (lightweight) | Выбираемый software/CPU бэкенд без GL-контекста | ADR-015 | реализовано (`render/soft`) |
| Внешний вид | Собственная отрисовка + темы, без нативных контролей | ADR-003 | реализовано (light/dark) |
| Текст | stb_truetype, латиница/кириллица, UTF-8, без shaping/bidi | ADR-004 | реализовано |
| Объектная модель | Публичная база первым полем + резерв + рантайм-проверка `size/version` | ADR-005 | реализовано |
| События | Гибрид: `on_event` по умолчанию раскладывает в удобные методы | ADR-006 | реализовано |
| Владение | Иерархия + weak focus/hover/popup; без refcount | ADR-007 | реализовано (упрощено, §5) |
| Ошибки | `cl_result_t` + thread-local last-error + `void`-сеттеры | ADR-008 | реализовано |
| Потоки | Один GUI-поток + потокобезопасный `post`/`wakeup` | ADR-009 | реализовано |
| Тестируемость | Mock-renderer + mock-platform (headless) | ADR-010 | реализовано (без mock-font, §3.4) |
| Окно | Одно окно ОС + overlay-слой для всплывашек (menu/combobox/tooltip) | ADR-011 | реализовано |
| DPI | Логические px в API/layout; округление на генерации команд рендера | ADR-013 | реализовано |
| Текст-данные | Ключ глиф-кэша по (font, codepoint) | ADR-014 | атлас в GL-рендерере; ключ по glyph_id — задел под shaping |

## 1. Слои и направление зависимостей

Правило: верхний слой зависит от нижнего; ничего снизу не зависит от верхнего.
Core (widget/layout/theme) на этапе компиляции зависит только от Foundation и от
**интерфейсов** renderer/platform — никогда от конкретных SDL/GL/stb-типов.
Конкретные бэкенды выбираются CMake-опциями и линкуются в `src/app`.

```text
┌─────────────────────────────────────────────────────────┐
│  App / Window            (публичный фасад, event loop,   │
│                           таймеры, task-очередь)         │
├─────────────────────────────────────────────────────────┤
│  Widgets library    (Label Button CheckBox RadioButton   │
│                      Slider ComboBox Menu TextBox …)      │
├─────────────────────────────────────────────────────────┤
│  Widget system │ Layout (vbox/hbox/scrollview) │ Theme   │  ← CORE (без платформы/GL)
├───────────────┴──────────────────────────┬──────────────┤
│  Renderer (iface)     Platform (iface)    │  Text/Font   │  ← абстракции
│   ├ GL 3.3 (+атлас)    ├ SDL2/GL          │  (stb_truetype)
│   ├ Software/CPU       ├ SDL2/software    │              │
│   └ Mock/record        └ Mock/headless    │              │  ← реализации (выбор из CMake/DI)
├─────────────────────────────────────────────────────────┤
│  Foundation: allocator · error · utf8 · mutex · version  │  ← ни от чего не зависит
└─────────────────────────────────────────────────────────┘
      external: SDL2, OpenGL (собственный загрузчик), stb_truetype (vendored)
```

## 2. Граф зависимостей (модули) — ацикличен

Стрелка `A ──► B` = «A зависит от B». Compile-time рёбра — к интерфейсам;
конкретные бэкенды подключает только точка композиции (`src/app`) на этапе
линковки.

```text
                         foundation            ◄── (нет исходящих)
                          ▲   ▲   ▲
        platform-if ──────┘   │   └────── theme
        renderer-if ──────────┘
             ▲   ▲            ▲   ▲
   sdl ──────┘   │     gl ────┘   │      (backend ──► своя iface + foundation)
   mock-plat ────┘     mock-rend ─┘

        widget-system ──► platform-if, renderer-if, theme, text   [интерфейсы, compile-time]
        widget-system ◄──► app/window  [осознанный цикл: widget.c/textbox.c/
                             combobox.c включают app_internal.h — focus,
                             clipboard, popup, IME-rect; см. §18]
             ▲
        { layout, scrollview }
             ▲
        widgets-lib
             ▲
        app/{application,window,timer} ──► widget-system, {interfaces}   [compile-time]
                                       --► sdl|mock-plat, gl|mock-rend    [LINK-TIME, точка композиции]
```

Core не линкует SDL/GL: символы SDL/GL присутствуют только в TU `src/app`
(bootstrap-фабрики под `COPAL_ENABLE_SDL/OPENGL`) или инъектируются вызывающим
через `cl_application_desc_t` (§3.9). Циклов нет.

## 3. Модули и ответственность

### 3.1 Foundation (`src/core/foundation`, публичные части в `include/copal/`)
- `cl_allocator_t` (userdata + alloc/realloc/free); дефолт — обёртка malloc
  (`allocator.c`), обёртки `cl_alloc/realloc/free` учитывают OOM в last-error.
- Ошибки (`error.c`): `cl_result_t`, `CL_ERROR_ABI_MISMATCH` и др.,
  **thread-local** last-error, лог-callback, в debug — `CL_ASSERT`.
- Геометрия/типы (`types.h`): `cl_point/size/rect/insets/color` (RGBA8, не
  premultiplied), `cl_constraints_t`. Координаты — `float`.
- UTF-8 (`utf8.c`): декодирование/итерация по кодовым точкам; отвергает
  overlong, суррогаты, `>U+10FFFF`, усечённые хвосты (подстановка `U+FFFD`).
- Мьютекс (`mutex.c`, **новое**): непрозрачный кросс-платформенный мьютекс
  (pthread / CRITICAL_SECTION) — под потокобезопасную task-очередь приложения.
- Версия (`version.c`): `CL_VERSION_*`, `cl_version_runtime/string`.
- Зависимостей нет; тестируется в изоляции.

### 3.2 Platform interface (`src/platform/platform.h`, `src/platform/`)
Таблица операций `cl_platform_ops_t` (ops-указатели, backend наследует
`cl_platform_t` первым полем):
- окно: `create_window`, `destroy_window` (откат нативного окна при провале
  `cl_window_create`; опционален), `set_title`, `drawable_size`, `scale`;
- события: `poll`/`wait` → `cl_platform_event_t` (нейтральный тип, в т. ч.
  `CL_PEV_TEXT_EDIT` для IME и `CL_PEV_EXPOSE` при повреждении поверхности;
  мышиные события несут модификаторы и счётчик кликов); `present`;
- **`wakeup()`** — потокобезопасный, коалесцируемый выход из ближайшего `wait`;
- ввод текста/IME: `start_text_input`, `set_ime_rect` (позиция окна композиции);
- буфер обмена: `clipboard_get`/`clipboard_set`;
- GL: `gl_get_proc` (адрес процедуры для загрузчика; NULL у не-GL бэкендов);
- **`now_ms()`** — монотонные миллисекунды (под таймеры, §7);
- software: `lock_framebuffer`/`unlock_framebuffer` — отдают CPU-буфер окна
  (`cl_pixmap`: pixels/размер/pitch + маски каналов) под software-рендер; NULL у GPU;
- `destroy`.
Реализации: **SDL2/GL** (`platform/sdl`, `cl_platform_sdl_create` — окно с
GL-контекстом), **SDL2/software** (`cl_platform_sdl_soft_create` — окно **без**
GL: `lock_framebuffer` = `SDL_GetWindowSurface`, `present` = `SDL_UpdateWindowSurface`;
**не требует OpenGL**) и **Mock/headless** (`platform/mock`, скриптованная очередь
`cl_platform_mock_push`, управляемые часы `cl_platform_mock_advance`, без окна).
`wait` блокируется через `SDL_WaitEvent(NULL)` (ждёт, но не извлекает событие —
его вычерпывает `process_events`), иначе при устойчивом потоке событий цикл спинил
бы на 100% одного ядра вместо сна.

### 3.3 Renderer interface (`src/render/`)
Разделён на две части, чтобы GPU-детали не протекали к авторам виджетов:
1. **Публичный `cl_paint_context_t`** (`render.h`, `paint_context.c`) —
   передаётся в `paint`. Только рисование: `fill_rect`, `fill_round_rect(r,
   radius)`, `stroke_round_rect(r, radius, width)`, `draw_text(font, utf8, pos,
   color)`, `push_clip`/`pop_clip`; плюс read-доступ к теме (`theme`,
   `theme_color`). Устройство/кадр/GPU-ресурсы недоступны.
2. **Внутренний device/frame-интерфейс** (`renderer.h`) — принадлежит App/Window:
   `begin_frame(size, scale)`/`end_frame`, управление GPU-ресурсами, загрузка
   глифов в атлас.
Реализации: **GL** (`render/gl`: GL 3.3 core, собственный загрузчик `gl_loader.c`
поверх `third_party/GL`+`KHR`, SDF-шейдер для скруглений/AA, glyph-атлас);
**Software/CPU** (`render/soft/renderer_soft.c`: те же 9 операций на CPU — тот же
SDF+AA скруглений, портированный per-pixel; текст — блиттинг coverage-битмапов
stb_truetype из CPU glyph-кэша; клип-стек; пиксельный буфер получает от платформы
через `lock/unlock_framebuffer`. Платформо-нейтрален, собирается **всегда**, **не
создаёт GL-контекст** → быстрый плоский старт и на порядок меньше памяти, работает
по RDP и в CI); **Mock/record** (`render/mock`: список draw-команд
`cl_mock_command_t` для детерминированных headless-тестов, без GL).

### 3.4 Text/Font (`src/text/`, `font.h`)
**Проще проектного эскиза.** Реализовано:
- `font.c` поверх **stb_truetype** (`stb_impl.c` — единственная TU с
  `STB_TRUETYPE_IMPLEMENTATION`): загрузка `.ttf/.otf` из файла/памяти, метрики
  (`cl_font_metrics_t`), измерение `cl_text_measure` и `cl_text_measure_bytes`
  (по байтовой длине — для позиционирования каретки) **без растеризации**;
- растеризация глифов и глиф-кэши живут в рендерерах (GL: атлас, soft:
  coverage-битмапы); ключ кэша — пара (font, **codepoint**), т.к. без
  shaping'а действует «1 кодовая точка = 1 глиф» (§12); ключ по glyph_id —
  задел на будущее (ADR-014). Поиск — открытая хэш-таблица; при переполнении
  таблицы (512 слотов) или GL-атласа кэш **сбрасывается** и строка
  дорисовывается (GL перед сбросом флашит накопленный батч — его квады ещё
  ссылаются на старые текселы); нерастеризуемый глиф пропускается, не
  обрывая строку. `cl_font_release` инвалидирует кэши через операцию
  `evict_font` (переиспользование адреса шрифта аллокатором больше не даёт
  ложных попаданий).
Проектные `GuiFontProvider`/`GuiShaper`/`GuiTextEngine`/`GuiGlyphCache` как
отдельные публичные сущности **не выделены**; их роль сведена к `font.c` +
атласу рендерера. Точки расширения под FreeType/HarfBuzz сохранены концептуально
(ADR-004/014). Ограничения — §12.

### 3.5 Widget system (`src/widget/`, `widget.h`, `widget_impl.h`)
- `cl_widget_t` — публичная база первым полем + резерв (`CL_WIDGET_RESERVED = 24`);
  `cl_widget_vtable_t`; `cl_widget_class_t` (имя, класс-предок, `type_id`
  информационный, размер экземпляра, vtable) для RTTI и проверяемых кастов.
- Дерево: `first_child`/`last_child`/`next_sibling`, `parent` (weak), `window`
  (weak back-ref), `app` (weak); add/remove/destroy.
- Состояние: `rect` (абсолютный), `measured`/`pref_size`, `margin`/`align`/`flex`,
  флаги (`VISIBLE`/`ENABLED`/`FOCUSABLE`/`DIRTY`/`DEAD`/`CLIP`), `generation`,
  `userdata`, `tooltip` (owned UTF-8).
- Диспетчер событий (`widget.c`): `on_event` по умолчанию раскладывает в удобные
  методы (§6); clip-aware `paint`/hit-test (при `CL_WF_CLIP` дети клипуются
  `clip_rect`); `cl_widget_reveal` — walk вверх по предкам с хуком `reveal`.
- Инвалидция: `cl_widget_invalidate` (paint → `window.dirty`),
  `cl_widget_invalidate_layout` (measure/arrange).

### 3.6 Layout (`src/layout/`, `layout.h`)
- Два прохода measure/arrange в логических float-px; модель ограничений
  (min/max, `CL_UNBOUNDED`).
- Контейнеры: `vbox`/`hbox` (spacing, padding, cross-align, flex-веса);
  **ScrollView** (`scrollview.c`, `widgets/scrollview.h`) — две оси (opt-in
  `horizontal`), реализует vtable-хуки `clip_rect` (обрезка содержимого) и
  `reveal`/scroll-to-view, opt-in `smooth`-анимация колеса (через таймер).
- Per-child атрибуты задаются на ребёнке (`cl_widget_set_flex/margin/align/…`).

### 3.7 Theme/Style (`src/theme/`, `theme.h`)
- `cl_theme_t`: роли цветов (`cl_color_role_t`: BACKGROUND, SURFACE(+HOVER/
  ACTIVE/RAISED), TEXT(+MUTED), ACCENT, BORDER, FOCUS_RING, SELECTION, SHADOW),
  встроенные **light/dark** схемы (`cl_theme_set_variant`), радиус углов, шрифт
  по умолчанию. `cl_text_style_t` (font/color/align) для text-виджетов.
- Виджеты запрашивают цвет по роли в `paint` через `cl_paint_context_t`.

### 3.8 Widgets library (`include/copal/widgets/`, `src/widgets/`)
Реализованы: **Label, Button, CheckBox, RadioButton** (взаимоисключение по
числовому `group` id, а не отдельным контейнером), **Slider, ComboBox, Menu**
(popup через overlay), **TextBox** (одно-/многострочный, password, readonly,
`max_length`, выделение/буфер обмена, IME-композиция), **ScrollView**. Внутренний
**tooltip**-пузырь (`src/widgets/tooltip.c`) — не публичный виджет, а элемент
hover-слоя окна. Каждый публичный виджет — `*_desc_t` + `*_create` (§API).
**Не реализованы** проектные Panel/Image/Spacer/RadioGroup.

### 3.9 App & Window (`src/app/`, `application.h`, `window.h`, `timer.h`)
- `cl_application_t`: владеет platform, renderer, theme, allocator; event loop
  (`run`/`step`/`quit`); **таймеры** (`timer.c`, §7); **потокобезопасная
  task-очередь** `cl_application_post` (mutex + FIFO, §7); IME-rect. Бэкенды —
  DI из `cl_application_desc_t` **или** встроенные фабрики bootstrap-TU.
  **Выбор бэкенда:** `cl_application_desc.render_backend`
  (`CL_RENDER_AUTO`/`GL`/`SOFTWARE`) + рантайм-override `COPAL_RENDER=software`
  для AUTO; при сборке `COPAL_ENABLE_SDL` **без** `COPAL_ENABLE_OPENGL` доступен
  только software (GL-рендерер не компилируется, libGL не линкуется — ADR-015).
  Встроенный рендер не привязывается к инжектированной платформе, которая не
  может его обслужить (software требует `lock_framebuffer`, GL — `gl_get_proc`;
  иначе `cl_application_create` → `CL_ERROR_UNSUPPORTED`). При AUTO со
  встроенными бэкендами отказ создания GL-окна один раз откатывается на
  software-пару (`cl_app_software_fallback`); явный GL и DI не откатываются.
  Отказ ленивой gl_init (шейдеры/загрузка функций) фиксируется в
  `cl_last_error` (CL_ERROR_RENDERER) и в логе.
- `cl_window_t`: нативное окно + GL-контекст, корневой виджет (content) **и
  overlay-слой** — единый временный popup поверх content (menu/combobox) плюс
  отдельный **hover-tooltip**-слой; light-dismiss; focus/mouse-target/reveal;
  dirty-флаг. Владеет content, popup и tooltip; хранит weak back-ref у виджетов.
  В MVP — одно окно (ADR-011; второе → `CL_ERROR_UNSUPPORTED`).

## 4. Модель владения

```text
cl_application_t
 ├─ owns platform, renderer, theme, allocator
 ├─ owns cl_window_t (одно в MVP)
 │    ├─ owns native surface + GL context
 │    ├─ owns content: cl_widget_t → owns children (рекурсивно)
 │    ├─ owns overlay popup (menu/combobox), если открыт
 │    └─ owns hover tooltip, если показан
 ├─ owns cl_timer_t[]        (FIFO-список)
 └─ owns posted-task queue   (из других потоков)

Weak (сырые указатели, зануляются при уничтожении цели):
  widget.parent / widget.window / widget.app
  window.focus / mouse_target → widget
```

Правила:
- **Родитель владеет детьми.** `cl_widget_add_child` передаёт владение;
  `cl_widget_remove_child` возвращает владение вызвавшему; `cl_widget_destroy`
  уничтожает поддерево.
- **Weak-ссылки** не владеют; при уничтожении виджета его window-weak-ссылки
  (focus/mouse_target/popup-owner/tooltip-target) зануляются (§5).
- **Без refcounting** (ADR-007).
- Все аллокации — через `cl_allocator_t` приложения; виджеты выделяются нулём
  (`cl_widget_alloc`, контракт §9).
- **Живые виджеты — только в куче** (создаются `cl_*_create`).

## 5. Уничтожение и безопасность из callback (как реализовано)

**Проще проектного эскиза 0.2** (там описывалась общая DEAD-очередь отложенного
удаления со снимками поддеревьев — она **не реализована**). Фактически:

- `cl_widget_destroy(w)` немедленно: отвязать `w` от родителя (`remove_child`) →
  рекурсивно уничтожить детей **bottom-up** (`widget_destroy_subtree`) → на каждом
  узле занулить window-weak-ссылки (`mouse_target`, `focus`), снести его popup
  (`cl_window_owner_destroyed`) и hover-tooltip (`cl_window_tooltip_target_gone`)
  → `vtable->destroy` → освободить `tooltip` и сам узел.
- **Weak-зануление при detach.** `remove_child` вызывает `cl_widget_set_window(child,
  NULL)` **до** обхода уничтожения, поэтому очистка window-based weak-ссылок
  продублирована и в detach-ветке `cl_widget_set_window` (не только в
  `widget_destroy_subtree`) — иначе focus/mouse_target/popup/tooltip могли бы
  указывать на уже отвязанный узел.
- **Отложенность там, где она реально нужна:**
  - **Overlay/popup** (menu/combobox): закрытие откладывается флагом
    `overlay_closing` и выполняется в `cl_window_reap_overlay` **после** dispatch,
    до рендера. Поэтому обработчик пункта меню/выбора может безопасно запросить
    закрытие своего же popup.
  - **Таймеры**: освобождение таймера во время fire-pass откладывается и
    выполняется `reap` после прохода; реентерабельность (вложенный
    `step`/`run`) защищена — reap делает только внешний проход (§7).
- **Callback — последнее действие.** Встроенные виджеты (button, checkbox,
  slider, radio, combobox, textbox, menu) не трогают собственное состояние
  после вызова пользовательского callback — уничтожение виджета из его
  callback безопасно при условии, что обработчик затем вернёт `true`
  (см. контракт `cl_widget_destroy` в widget.h и API §6).
- Бывшие `CL_WF_DIRTY`/`CL_WF_DEAD` и поле `generation` **удалены** из
  widget_impl.h (биты 3–4 флагов зарезервированы): DEAD-очередь не реализована,
  teardown немедленный.

## 6. Модель ошибок и события

- `cl_result_t` для fallible-операций (create app/window/timer, load font,
  add-item, ABI-mismatch). Конструкторы при неудаче → `NULL` + thread-local
  last-error.
- Сеттеры — `void`: валидируют/клампят; в debug — `CL_ASSERT` на грубые ошибки.
- `cl_last_error()`, `cl_result_string()`, `cl_set_log_callback()`. Лог-колбэк
  process-wide и единственный (per-app `log_fn` удалён); внутренние точки
  диагностики идут через `cl_log()` (foundation), фолбэк WARN/ERROR — stderr.

**Гибрид событий (ADR-006):** vtable содержит `on_event` + конкретные слоты.
Диспетчер по умолчанию раскладывает `cl_event_t` в `mouse_down/up/move/wheel`,
`key_down/up`, `text_input`, **`text_edit`** (IME pre-edit), `focus_gained/lost`.
Автор виджета переопределяет **либо** `on_event`, **либо** отдельные методы.
`on_event`, вернувший `true`, гасит дальнейшую передачу.

## 7. Модель потоков, таймеры, задачи

- Один GUI-поток владеет app/window/widgets/renderer.
- **`cl_application_post(app, fn, user)`** потокобезопасен: кладёт задачу в
  очередь под мьютексом (`mutex.c`) и будит цикл через `platform.wakeup()`;
  очередь **целиком отцепляется под локом и выполняется без лока** (задача может
  поставить новую — дренируется на следующем проходе, без дедлока); FIFO;
  недренированные при destroy — отбрасываются.
- **Таймеры** (`timer.c`): app-owned FIFO-список; срабатывают в GUI-потоке между
  dispatch и рендером; `wait` бланкуется до ближайшего дедлайна
  (`cl_app_timers_timeout`), опрос — `cl_app_timers_poll`. Время — монотонное
  `platform.now_ms`. one-shot с `interval_ms==0` срабатывает на следующем опросе;
  repeat флорится до 1 мс и коалесцирует пропуски. Освобождение во время
  fire-pass отложено (§5); окно уничтожается **до** `cl_app_timers_free_all`,
  чтобы виджеты успели отменить свои таймеры по живому списку.
- Ресурсы GPU/шрифта — только в GUI-потоке; из другого потока допустим лишь `post`.

## 8. Потоки данных

### 8.1 Event flow
```text
OS event → SDL2 → platform backend → cl_platform_event_t → app loop → window
  → hit-test (overlay/tooltip сверху → затем content; focus; mouse-target;
     light-dismiss popup по клику вне / Esc)
  → target: on_event(cl_event_t*)  [default → mouse_*/key_*/text_input/text_edit/focus_*]
  → виджет обновляет состояние → invalidate(paint|layout)
  → run tasks → poll timers → reap overlay → (layout если нужно) → paint → present
```

### 8.2 Layout flow
```text
invalidate_layout(w) → measure-dirty у w и предков до корня
  следующий кадр: measure(constraints) [2 прохода] → arrange(final rect) → paint
```

### 8.3 Rendering flow + округление DPI (ADR-013)
```text
window.dirty → begin_frame(size, scale)
  → content.paint(cl_paint_context_t): fill/stroke_round_rect (SDF), draw_text
    (квады из glyph-атласа), push/pop_clip; затем overlay popup и tooltip поверх
  → end_frame → present     (idle без dirty → кадр не рисуется)
```
**Контракт округления:** layout — целиком в логических float-px без округления;
округление — только на генерации команд рендера при переводе логических краёв в
физические (привязка к пикселю по абсолютным краям, чтобы общие границы соседних
виджетов совмещались); hairline ≥ 1 физического пикселя.

### 8.4 Text rendering flow
```text
draw_text — одна строка (перенос по ширине/`\n` реализован только внутри
TextBox, который сам режет текст на строки и зовёт draw_text per-line) →
  для каждой кодовой точки: lookup в кэше рендерера по (font, codepoint);
  miss → растеризация stb → GL: upload в атлас / soft: coverage-битмапа →
  quad/блит.
Измерение — по метрикам/advance, без растеризации; max_width в
cl_text_measure* зарезервирован и пока игнорируется.
```

## 9. Объектная модель на C (детали)

Приложение видит `cl_widget_t` непрозрачно (`copal/copal.h` не включает
`widget_impl.h`); авторы виджетов включают `copal/widget_impl.h` и наследуются
вложением базы **первым полем**.

```text
cl_widget_class_t {              // одна на тип (статическая, const)
  const char *name;
  const cl_widget_class_t *base; // цепочка предков; у листовых классов = NULL
  uint32_t type_id;              // ИНФОРМАЦИОННЫЙ (FourCC) — не нужен для каста
  size_t instance_size;
  const cl_widget_vtable_t *vtable;
}
struct cl_widget {               // публичная база, первое поле производной структуры
  const cl_widget_class_t *cls;  // основа каста (идентичность указателя класса)
  cl_application_t *app;         // weak
  cl_window_t *window;           // weak back-ref
  cl_widget_t *parent;           // weak
  cl_widget_t *first_child, *last_child, *next_sibling;
  cl_rect_t rect; cl_size_t measured, pref_size;
  cl_insets_t margin; cl_align_t align_h, align_v; float flex;
  uint32_t flags;                // VISIBLE|ENABLED|FOCUSABLE|DIRTY|DEAD|CLIP
  uint32_t generation;           // зарезервировано (валидация weak-хэндлов)
  void *userdata;
  char *tooltip;                 // owned UTF-8 или NULL
  unsigned char reserved[CL_WIDGET_RESERVED /* 24 */];
}
```

**vtable-слоты** (все с первым параметром `cl_widget_t*`; NULL = поведение по
умолчанию):
`destroy`, `measure`, `arrange`, `paint`, `clip_rect`, `reveal`, `hit_test`
(NULL = rect), `on_event` (NULL = раскладка), `mouse_down/up/move/wheel`,
`key_down/up`, `text_input`, `text_edit`, `focus_gained/lost`. `clip_rect`,
`reveal`, `mouse_wheel`, `text_edit` добавлены на Этапах 6/7 под ScrollView,
scroll-to-view, колесо и IME; остальные виджеты их не задают (NULL-совместимо).

**Проверяемый каст.** `CL_WIDGET_CAST(name, w)` — checked, **всегда NULL при
несовпадении** (walk по `cls->base`); `CL_WIDGET_CAST_UNCHECKED` — быстрый путь,
UB при неверном типе. Касты опираются на **идентичность указателя класса**
(`&name##_class`), поэтому `type_id` не нужен для корректности и не создаёт
риска коллизий между библиотеками.

**Контракт `cl_widget_init_base(w, app, cls)`.** Ставит `cls`/`app`, флаги
`VISIBLE|ENABLED`, дефолтные align. `cl_widget_alloc` выделяет `instance_size`
нулём и вызывает `init_base`; пользовательская инициализация — после.

**ABI-рукопожатие (ADR-005).** Каждый `*_desc_t` несёт `abi_version`+`struct_size`,
штампуемые макросом (`CL_*_DESC_INIT` для application/window; `CL_*_DESC_INIT_FIELDS`
для виджетов/layout — под составной литерал). `cl_*_create` сверяет их и при
расхождении → `NULL` + `CL_ERROR_ABI_MISMATCH`. Рост базы/vtable требует
перекомпиляции приложения.

**C++-путь авторства.** Слот-функции на C++ должны иметь C-линковку; объявляются
**ровно** с `cl_widget_t*` в первом параметре, downcast — внутри тела (иначе UB
на несовместимом типе указателя функции).

## 10. Виртуальные методы (обоснование)

| Метод | Виртуальный? | Почему |
|-------|-------------|--------|
| `destroy`, `measure`, `arrange`, `paint` | да | ресурсы/размер/раскладка/отрисовка специфичны типу |
| `clip_rect`, `reveal` | да (opt-in) | ScrollView: обрезка детей и scroll-to-view |
| `hit_test` | да (дефолт = rect) | нестандартные формы |
| `on_event` | да | точка расширения ввода (гибрид) |
| `mouse_down/up/move/wheel`, `key_down/up` | да (удобные) | выборочное переопределение |
| `text_input`, `text_edit` | да (удобные) | ввод текста и IME-композиция |
| `focus_gained/lost` | да | реакция на фокус |

## 11. Инвалидция

- Paint-инвалидция → `window.dirty`; коалесцируется на кадр.
- Layout-инвалидция → measure-dirty у виджета и предков; перед следующим paint —
  layout-проход. Инвалидции во время прохода откладываются на следующий кадр.

## 12. Ограничения текста (честно)

stb_truetype растеризует глифы, но не делает shaping, bidi, mark-positioning,
авто-fallback. Корректно для **NFC-прекомпозированных** латиницы/кириллицы при
«1 кодовая точка → 1 глиф». Некорректно для комбинирующих знаков/NFD (диакритика
отдельным глифом), арабицы/индийских (reordering/лигатуры), смешанного LTR/RTL
(bidi), отсутствующих в шрифте глифов («тофу», нет авто-fallback). Каретка/
выделение индексируются по кодовой точке. Пути расширения (FreeType/HarfBuzz)
заложены концептуально (ADR-004/014).

## 13. Точки расширения

- Пользовательский виджет: включить `widget_impl.h`, встроить базу первым полем,
  заполнить статические `cl_widget_class_t` + `cl_widget_vtable_t` (C и C++).
- Пользовательский renderer / platform (реализовать ops-таблицу и инъектировать
  через `cl_application_desc_t`), allocator, тема.

## 14. Жизненные циклы

```text
Приложение:
  cl_application_create(&desc) → allocator, platform, renderer, theme, task-mutex;
    бэкенды — DI из desc либо встроенные фабрики
  → run() | цикл: wait(до ближайшего таймера) → process_events → run_tasks →
     poll_timers → reap_overlay → (layout) → paint(dirty) → present
  → quit() ставит флаг выхода
  → destroy() → сначала окно (виджеты отменяют свои таймеры) → таймеры → задачи →
     тема/renderer/platform

Окно:
  cl_window_create(app, &desc) → platform.create_window + GL context
  → set_content(root) → окно владеет root; back-ref window у поддерева
  → события/resize/paint; overlay popup и tooltip поверх content

Виджет:
  alloc(zero) → init_base(app, cls) → пользовательская инициализация
  → add_child (владение → родителю; back-ref window)
  → measure/arrange/paint; события через on_event
  → destroy (bottom-up; зануление weak-ссылок) → vtable->destroy → free
```

## 15. Структура каталогов и CMake

Фактическое дерево файлов и ответственность — в [STRUCTURE.md](STRUCTURE.md).
Кратко по сборке:
- Target-based, таргет `copal` (alias `copal::copal`), static/shared, генерация
  export-заголовка (`CL_API`).
- Опции: `COPAL_BUILD_SHARED/EXAMPLES/TESTS`, `COPAL_ENABLE_SDL/OPENGL`,
  `COPAL_FETCH_SDL2`, `COPAL_ENABLE_SANITIZERS/INSTALL` (см. STRUCTURE §5).
- `find_package(Threads REQUIRED)` (под mutex/`cl_application_post`);
  SDL2+OpenGL линкуются только при обеих `COPAL_ENABLE_SDL`+`COPAL_ENABLE_OPENGL`
  в TU `src/app`.
- `find_package(copal CONFIG REQUIRED)` + `target_link_libraries(app PRIVATE
  copal::copal)`; поддержка `add_subdirectory`.

## 16. Реестр ADR

- **ADR-001** SDL2 за интерфейсом `platform` (заменяемо; есть mock).
- **ADR-002** OpenGL 3.3 core за интерфейсом; SDF-примитивы + glyph-атлас.
- **ADR-003** Собственная отрисовка + темы (light/dark); без нативных контролей.
- **ADR-004** stb_truetype; без shaping/bidi/mark-positioning; интерфейсы под
  FreeType/HarfBuzz зарезервированы концептуально.
- **ADR-005** Публичная база + резерв + ABI-рукопожатие `abi_version`/`struct_size`
  в `*_desc_t`, проверка в `cl_*_create`. Рост базы/vtable → перекомпиляция.
- **ADR-006** Гибридная диспетчеризация событий (`on_event` + удобные слоты).
- **ADR-007** Иерархическое владение + weak focus/mouse-target/popup/tooltip;
  без refcount. Отложенность — точечная (overlay/таймеры), не общая DEAD-очередь
  (§5).
- **ADR-008** `cl_result_t` + thread-local last-error + `void`-сеттеры;
  `CL_ERROR_ABI_MISMATCH`.
- **ADR-009** Один GUI-поток + потокобезопасный `cl_application_post` (mutex+FIFO)
  и платформо-нейтральный `wakeup()`; таймеры в GUI-потоке.
- **ADR-010** Mock-renderer + mock-platform для headless-тестов. **Mock-font не
  реализован**: текст измеряется реальным stb_truetype (тесты грузят системный
  DejaVu опционально; render-проверки под `if (font)`).
- **ADR-011** Одно окно ОС в MVP; menu/combobox/tooltip — overlay-слоем внутри
  окна (клип границами окна, light-dismiss). Швы под мульти-окно сохранены
  (`cl_window_t` отделено от app; ресурсы — на уровне app). Второе окно →
  `CL_ERROR_UNSUPPORTED`.
- **ADR-012** Имена: типы `cl_*_t`, функции `cl_*`, макросы/enum `CL_*`; CMake —
  `copal::copal`/`COPAL_*`. Лицензия — **GPL-3.0-or-later**.
- **ADR-013** Логические px в API/layout; округление по абсолютным краям на
  генерации команд рендера.
- **ADR-014** Глиф-кэши рендереров ключуются по (font, **codepoint**) —
  эквивалентно glyph_id, пока действует «1 кодовая точка = 1 глиф» (§12);
  переход на glyph_id — вместе с будущим shaping'ом (HarfBuzz).
- **ADR-015** **Software/CPU рендерер как выбираемый бэкенд** (`render/soft`).
  Не создаёт GL-контекст и **не линкует libGL при сборке `COPAL_ENABLE_SDL` без
  `OPENGL`** → «lightweight» по-настоящему: быстрый плоский старт, на порядок
  меньше памяти (замер: calc software 18.5 МБ на Windows / 8 МБ на Linux-dummy
  против ~70–83 МБ у GL), работает по RDP и в CI без GPU-драйвера. Плата —
  потолок скорости на тяжёлом/анимированном UI. GL остаётся дефолтом для AUTO
  при наличии; software выбирается через `render_backend`/`COPAL_RENDER`.

## 17. Что добавлено на Этапах 6–7 (дельта к MVP-дизайну)

- **Foundation**: непрозрачный мьютекс (`mutex.c`).
- **Platform**: `now_ms` (монотонные часы), `set_ime_rect`, событие
  `CL_PEV_TEXT_EDIT`; управляемые часы и очередь у mock.
- **Приложение**: таймеры (`timer.c`), потокобезопасная task-очередь
  (`cl_application_post`).
- **Окно**: hover-tooltip-слой поверх content и popup; scroll-to-focus (reveal
  при переводе фокуса).
- **Widget/vtable**: слоты `clip_rect`, `reveal`, `mouse_wheel`, `text_edit`;
  флаг `CL_WF_CLIP`; поле `tooltip`; `app` back-ref; `last_child`.
- **Layout**: ScrollView (две оси, clip/reveal, opt-in `smooth`-анимация).
- **TextBox**: многострочный режим (перенос по ширине, навигация вверх/вниз,
  вертикальный скролл), IME-композиция (preedit у каретки),
  `cl_text_measure_bytes` под каретку.
- **Theme**: тёмная схема (`cl_theme_set_variant`), роли `SURFACE_RAISED`/
  `SHADOW`, радиус.
- **Renderer (Этап оптимизации)**: **software/CPU бэкенд** (`render/soft`,
  ADR-015) с выбором через `render_backend`/`COPAL_RENDER`; SDL-платформа и выбор
  расцеплены от OpenGL (сборка `COPAL_ENABLE_SDL` без `OPENGL` = software без
  libGL). В GL-рендерере: hoisting per-frame состояния (проекция/программа/атлас
  один раз за кадр), переиспользование text-VBO; в шрифте — кэш advance по
  Latin-1. В SDL-платформе: `wait` через `SDL_WaitEvent(NULL)` — устранён
  busy-spin цикла при простое.

## 18. Самопроверка

- Граф в целом ацикличен (§2); единственный **осознанный цикл** — widget-слой ↔
  app/window (widget.c/textbox.c/combobox.c включают `app_internal.h` ради
  focus/clipboard/popup/IME; изменение раскладки `cl_window` перекомпилирует
  виджеты). Бэкенды — из `src/app` на линковке.
- Владение и время жизни определены (§4/§5): weak back-ref, зануление при detach,
  точечная отложенность overlay/таймеров.
- Core тестируется без окна (§3.2/§3.3): mock platform + renderer; измерение
  текста renderer-free.
- Публичный API не зависит от платформенных типов (§1); авторам виджетов —
  `cl_paint_context_t`, не device (§3.3).
- Кастомный виджет без приватных структур (§9, `widget_impl.h`).
- UTF-8 корректен (§3.1); текст — по `glyph_id` (ADR-014); ограничения честны (§12).
- Renderer/platform заменяемы (DI, §3.9); округление DPI специфицировано (§8.3).
- Касты без UB на типах указателей функций (§9); ABI-рукопожатие в `*_desc_t`.
- Собирается как C; заголовки C++-совместимы (`extern "C"`); экспорт через `CL_API`.
