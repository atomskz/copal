# copal — Структура исходников

Статус: **актуально** (соответствует коду по состоянию на Этап 7). Версия: 1.0.
Опирается на [ARCHITECTURE.md](ARCHITECTURE.md), [CODESTYLE.md](CODESTYLE.md),
[API.md](API.md).

Документ описывает **фактическое** дерево репозитория и ответственность файлов —
не эскиз. Разделы, начатые как проектные наброски (ARCHITECTURE §15, ранние
версии этого файла), заменены на то, что реально собрано и протестировано.

## 1. Дерево каталогов (фактическое)

```text
copal/
├── CMakeLists.txt                 корневой; target copal (alias copal::copal), опции, install
├── README.md CHANGELOG.md         описание проекта; история версий
├── COPYING                        полный текст GPL-3.0
├── .clang-format .editorconfig .gitattributes   стиль/атрибуты (LF, UTF-8, 4 пробела, 80 колонок)
├── .githooks/pre-commit           clang-format-проверка коммита
├── .github/workflows/ci.yml       CI: Linux gcc/clang × mock|SDL+GL|SDL-soft (ASan/UBSan), shared, Windows MSVC
├── cmake/
│   ├── CompilerWarnings.cmake      warnings (-Wall -Wextra -Wpedantic -Wshadow …) + sanitizers
│   ├── copalConfig.cmake.in        package config для find_package(copal); find_dependency(Threads)
│   └── copal.pc.in                 pkg-config-шаблон (устанавливается как lib/pkgconfig/copal.pc)
├── tools/
│   ├── setup-hooks.sh              активация .githooks (core.hooksPath)
│   └── lsan.supp                   supression-список для LeakSanitizer
├── include/copal/                 ПУБЛИЧНЫЕ заголовки (устанавливаются)
│   ├── backend/                   SPI бэкендов: platform.h, renderer.h (ARCHITECTURE §13)
│   ├── copal.h                    зонтичный заголовок (всё, кроме widget_impl.h)
│   ├── export.h                   CL_API (экспорт/видимость символов)
│   ├── version.h                  версия compile/runtime + проверка
│   ├── types.h                    геометрия, цвет, constraints, align/orientation, glyph handle
│   ├── error.h                    cl_result_t, log-level, last-error, лог-callback
│   ├── allocator.h                cl_allocator_t + обёртки cl_alloc/realloc/free
│   ├── event.h                    типы событий, клавиши, модификаторы, cl_event_t, callback-типы
│   ├── font.h                     загрузка шрифтов, метрики, измерение текста
│   ├── theme.h                    роли цветов, light/dark, cl_text_style_t, тема
│   ├── render.h                   cl_paint_context_t (рисование в paint)
│   ├── widget.h                   базовый виджет (публичная часть для приложения)
│   ├── widget_impl.h              база виджета для АВТОРОВ виджетов (vtable, class, каст)
│   ├── layout.h                   контейнеры vbox/hbox
│   ├── application.h              приложение, event loop, post
│   ├── window.h                   окно, content, overlay-popup, tooltip
│   ├── timer.h                    таймеры цикла приложения
│   └── widgets/                   label button checkbox radiobutton slider
│                                  menu combobox textbox scrollview
├── src/
│   ├── core/foundation/           фундамент (ни от чего не зависит)
│   │   ├── foundation_internal.h  внутренние объявления (cl_set_last_error)
│   │   ├── version.c              версия runtime
│   │   ├── error.c                thread-local last-error, строки ошибок, лог
│   │   ├── allocator.c            дефолтный malloc-аллокатор, обёртки, учёт OOM
│   │   ├── utf8.c                 декодер/итерация UTF-8 по кодовым точкам
│   │   ├── mutex.c                непрозрачный кросс-платформенный мьютекс (pthread/CRITICAL_SECTION)
│   │   └── mutex_internal.h       интерфейс мьютекса (для task-очереди приложения)
│   ├── widget/
│   │   ├── widget.c               база, vtable-диспетчер, RTTI/каст, дерево, инвалидция,
│   │   │                          clip-aware paint/hit-test, reveal, tooltip, event-dispatch
│   │   ├── widget_internal.h      внутренние объявления виджет-слоя
│   │   └── widget_host.h          host-интерфейс виджета (реализует окно; ацикличность §2)
│   ├── layout/
│   │   ├── vbox.c  hbox.c         measure/arrange контейнеров (flex, padding, spacing, align)
│   │   └── scrollview.c           ScrollView: две оси, clip_rect, reveal, smooth-анимация
│   ├── theme/
│   │   ├── theme.c                встроенные light/dark темы, роли цветов, радиус, шрифт
│   │   └── theme_internal.h
│   ├── render/
│   │   ├── renderer.h             обёртка над публичным copal/backend/renderer.h
│   │   ├── paint_context.c/.h     публичный cl_paint_context_t поверх renderer
│   │   ├── image.c image_internal.h   cl_image_t: RGBA8-ресурс (evict в рендерах)
│   │   ├── gl/                    OpenGL 3.3 core бэкенд (renderer_gl, gl_loader — glad-подобный)
│   │   ├── soft/renderer_soft.c   software/CPU растеризатор (SDF+AA, glyph-блит, клип; без GL; собирается всегда)
│   │   └── mock/                  record-renderer для headless-тестов (список draw-команд)
│   ├── text/
│   │   ├── font.c                 шрифт/метрики/измерение поверх stb_truetype
│   │   ├── font_internal.h
│   │   └── stb_impl.c             единственная TU с реализацией stb_truetype (STB_*_IMPLEMENTATION)
│   ├── platform/
│   │   ├── platform.h             обёртка над публичным copal/backend/platform.h
│   │   ├── sdl/                   SDL2 бэкенд: GL-окно (cl_platform_sdl_create) И software-окно
│   │   │                          без GL (cl_platform_sdl_soft_create, surface + UpdateWindowSurface)
│   │   └── mock/                  headless-бэкенд (скриптованные события, управляемые часы)
│   ├── widgets/                   реализации виджетов + внутренний tooltip-пузырь
│   │   ├── button.c label.c checkbox.c radiobutton.c slider.c imageview.c
│   │   ├── combobox.c menu.c menubar.c textbox.c
│   │   └── tooltip.c tooltip_internal.h   (внутренний виджет hover-подсказки)
│   └── app/
│       ├── application.c          создание/цикл, таймеры, task-очередь, IME-rect, диспетчер событий
│       ├── window.c               окно, content, overlay/tooltip-слой, focus/reveal, рендер
│       ├── timer.c                список таймеров приложения (FIFO, монотонные часы)
│       └── app_internal.h         приватные объявления app/window/timer/task
├── third_party/
│   ├── stb/stb_truetype.h         вендоренный растеризатор глифов
│   ├── GL/glcorearb.h             заголовок OpenGL core (для собственного загрузчика)
│   └── KHR/khrplatform.h          Khronos platform-типы (зависимость glcorearb.h)
├── examples/                      по каталогу на пример (build/examples/<name>/<name>)
│   ├── CMakeLists.txt              общий util-lib + хелпер copal_add_gui_example
│   ├── common/example_util.{h,c}  загрузка шрифта, headless-run (COPAL_MAX_FRAMES), LSan-суппрессии
│   ├── test_version/main.c        smoke-тест: печатает версию (без бэкендов)
│   ├── helloworld/main.c          тур по виджетам (layout, scroll, tooltip, timer, multiline, dark)
│   └── calc/                      калькулятор: calc_engine (модель) + calc_widgets
│                                  (calc_key/calc_display — кастомные виджеты) + main.c
├── tests/
│   ├── CMakeLists.txt             регистрирует CTest-сьюты (copal_add_test)
│   ├── test_foundation.c          версия/ошибки/аллокатор/UTF-8/типы
│   ├── test_soft.c                golden-пиксели software-рендерера (stub-fb, без SDL)
│   ├── test_theme.c               роли цветов, light/dark, радиус, шрифт
│   ├── test_widgets.c             базовый виджет, дерево, каст, layout, базовые виджеты
│   ├── test_textbox.c             однострочное редактирование/навигация/выделение
│   ├── test_multiline.c           перенос по ширине, навигация вверх/вниз, вертикальный скролл
│   ├── test_ime.c                 IME-композиция (preedit, commit, отмена)
│   ├── test_scrollview.c          две оси, reveal, scroll-to-widget
│   ├── test_combobox.c            combobox: элементы, выбор, popup
│   ├── test_popup.c               overlay-popup/menu, light-dismiss
│   ├── test_tooltip.c             hover-подсказка (dwell-таймер, отмена)
│   ├── test_timer.c               one-shot/repeat, cancel/restart, коалесинг
│   ├── test_post.c                cross-thread cl_application_post
│   └── test_gui.c                 интеграционный сценарий на mock-бэкендах
└── docs/                          ARCHITECTURE.md CODESTYLE.md API.md STRUCTURE.md
```

## 2. Ответственность ключевых файлов

| Файл | Ответственность | Публичный? |
|------|-----------------|-----------|
| `include/copal/export.h` | `CL_API`: dllexport/import, visibility | да |
| `include/copal/version.h` | версия compile/runtime, `COPAL_VERSION_ENCODE` | да |
| `include/copal/types.h` | `cl_point/size/rect/insets/color/constraints/glyph_handle`, `cl_align/orientation`, `cl_rgba` | да |
| `include/copal/error.h` | `cl_result_t`, `cl_log_*`, `cl_last_error`, `cl_result_string` | да |
| `include/copal/allocator.h` | `cl_allocator_t`, `cl_alloc/realloc/free`, дефолт | да |
| `include/copal/event.h` | типы событий/клавиш/модификаторов, `cl_event_t`, callback-типы | да |
| `include/copal/widget.h` | дерево/владение, геометрия, фокус, инвалидция, tooltip | да |
| `include/copal/widget_impl.h` | база виджета для авторов: vtable, class, флаги, каст | да (для расширения) |
| `include/copal/application.h` | `cl_application_*`, event loop, `cl_application_post` | да |
| `include/copal/window.h` | окно, content, overlay-popup, tooltip | да |
| `include/copal/timer.h` | таймеры цикла приложения | да |
| `src/core/foundation/mutex.c` | непрозрачный мьютекс под потокобезопасную task-очередь | нет |
| `src/widget/widget.c` | vtable-диспетчер, RTTI/каст, дерево, clip/hit/reveal, event-dispatch | нет |
| `src/app/application.c` | цикл, таймеры, task-очередь, диспетчеризация нейтральных событий | нет |
| `src/app/window.c` | content + overlay/tooltip-слой, focus/reveal, рендер кадра | нет |
| `src/render/paint_context.c` | публичный `cl_paint_context_t` поверх внутреннего renderer | нет |
| `src/render/soft/renderer_soft.c` | software/CPU растеризатор (9 op, SDF+AA, glyph-блит); без GL | нет |
| `src/text/stb_impl.c` | единственная TU, инстанцирующая stb_truetype | нет |

## 3. Направление зависимостей (без циклов)

Правило (ARCHITECTURE §1/§2): стрелка `A → B` = «A зависит от B»; циклов нет.

```text
Публичные заголовки (compile-time):
  copal.h → {export, version, types, error, allocator, event, font, theme,
             render, widget, layout, application, window, timer, widgets/*}
  types.h → (только <stdint.h>/<stdbool.h>);  export.h — лист
  widget_impl.h → widget.h, event.h, render.h        (для авторов виджетов)

Реализация:
  foundation (alloc/error/utf8/version/mutex) → свои публичные .h + foundation_internal.h
  widget/layout/theme/text → foundation + интерфейсы (platform/renderer)
  render/gl · render/soft · render/mock → внутренний renderer.h
      (gl: + third_party/GL,KHR; soft: платформо-нейтрален, только platform.h + font)
  platform/sdl · platform/mock → platform.h
  app/{application,window,timer} → всё вышеперечисленное; точка линковки бэкендов:
      SDL2 при COPAL_ENABLE_SDL (software-путь), OpenGL — только доп. при
      COPAL_ENABLE_OPENGL; либо DI из desc
```

Core (widget/layout/theme/text) на этапе компиляции зависит только от Foundation
и от **интерфейсов** platform/renderer — никогда от конкретных SDL/GL/stb-типов.
Конкретные бэкенды выбираются CMake-опциями и линкуются в `src/app`.

Mock-бэкенды (`render/mock`, `platform/mock`) собираются в отдельную
статическую библиотеку **`copal_mocks`** (цель `copal::mocks`, только при
`COPAL_BUILD_TESTS`) — тесты линкуются с ней, а в устанавливаемый артефакт
`libcopal` mock-код не попадает.

## 4. Публичные vs внутренние заголовки

- **Публичные** (`include/copal/*`) — устанавливаются, C++-совместимы (`extern "C"`),
  используют `CL_API`, guard `CL_*_H`. `copal.h` — единый зонтик.
- **Внутренние** (`src/**/*_internal.h` и пр.) — не устанавливаются, без
  `CL_API`, доступны только внутри сборки библиотеки.
- Исключения — заголовки «для расширения», устанавливаются, но в зонтичный
  `copal.h` НЕ входят: `widget_impl.h` (база виджета для авторов сторонних
  виджетов, ARCHITECTURE §9) и `backend/platform.h` + `backend/renderer.h`
  (SPI бэкендов с ABI-рукопожатием в ops-таблице, ARCHITECTURE §13;
  внутренние `src/platform/platform.h` и `src/render/renderer.h` — тонкие
  обёртки над ними).

## 5. Как собрать и проверить

Headless-сборка (mock-бэкенды, без SDL/GL) — по умолчанию:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure     # 17 сьютов + smoke-прогоны
./build/examples/test_version/test_version
```

Нативная сборка с окном (SDL2 + OpenGL) — собирает GUI-примеры helloworld и calc:

```sh
cmake -S . -B build-native -DCOPAL_ENABLE_SDL=ON -DCOPAL_ENABLE_OPENGL=ON
cmake --build build-native
SDL_VIDEODRIVER=offscreen COPAL_MAX_FRAMES=3 ./build-native/examples/calc/calc
SDL_VIDEODRIVER=offscreen COPAL_MAX_FRAMES=3 ./build-native/examples/helloworld/helloworld
```

Software-сборка **без OpenGL** (только SDL2; libGL не линкуется — лёгкий бэкенд):

```sh
cmake -S . -B build-sw -DCOPAL_ENABLE_SDL=ON -DCOPAL_ENABLE_OPENGL=OFF
cmake --build build-sw
# software выбирается автоматически (GL не собран); либо явно COPAL_RENDER=software
SDL_VIDEODRIVER=dummy COPAL_MAX_FRAMES=3 ./build-sw/examples/calc/calc
```

Выбор бэкенда в рантайме при обеих опциях: `COPAL_RENDER=software` (или
`cl_application_desc.render_backend = CL_RENDER_SOFTWARE`) — CPU-рендер без
GL-контекста (меньше памяти, RDP/CI); по умолчанию GL. Примеры calc/helloworld
принимают флаги **`--software`** / **`--gl`** (напр. `./calc --software`).

Опции: `-DCOPAL_BUILD_SHARED=ON` (shared; white-box тесты требуют статической
библиотеки и отключаются — в ctest остаются smoke-прогоны примеров),
`-DCOPAL_ENABLE_SANITIZERS=ON`
(ASan/UBSan), `-DCOPAL_ENABLE_COVERAGE=ON` (gcov/llvm-cov; не MSVC),
`-DCOPAL_FETCH_SDL2=ON` (скачать и собрать SDL2),
`-DCOPAL_BUILD_EXAMPLES=OFF`, `-DCOPAL_BUILD_TESTS=OFF`,
`-DCOPAL_ENABLE_INSTALL=OFF`.

Использование как зависимости: `find_package(copal CONFIG REQUIRED)` +
`target_link_libraries(app PRIVATE copal::copal)`, либо `add_subdirectory(copal)`.
