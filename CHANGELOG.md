# Changelog

Формат — [Keep a Changelog](https://keepachangelog.com/ru/1.1.0/);
нумерация версий — [SemVer](https://semver.org/lang/ru/). ABI-политика: в
пределах одного мажора публичные desc/ops-структуры только дополняются полями в
конце — tail-tolerant ABI-рукопожатие принимает потребителя, собранного против
другого минора того же мажора, а несовместимый мажор отклоняет. До 1.0 ABI ещё
не заморожен: мажор остаётся 0, поэтому рукопожатие пропустит любой 0.x —
пересобирайте потребителей под каждую 0.x-версию.

## [Unreleased]

### Добавлено

- SPI бэкендов опубликован: устанавливаемые заголовки
  `copal/backend/platform.h` и `copal/backend/renderer.h`; ops-таблицы
  начинаются с ABI-рукопожатия `struct_size`/`abi_version`, инжектированный
  бэкенд с чужой таблицей отклоняется `CL_ERROR_ABI_MISMATCH`.
- SPI платформы подготовлен к мульти-окну: оконные операции принимают
  непрозрачный `cl_platform_window_t*` (create_window возвращает хэндл),
  события несут `window_id`.
- Цикл widget ↔ app/window развязан: widget-слой объявляет узкий
  host-интерфейс (invalidate/фокус/popup/буфер обмена/IME), окно реализует
  его; попутно закрыт висячий `content` при прямом destroy корневого виджета.
- Hover-события: окно доставляет `CL_EVENT_MOUSE_ENTER`/`CL_EVENT_MOUSE_LEAVE`
  (новые vtable-слоты `mouse_enter`/`mouse_leave`, без всплытия); кнопка
  подсвечивается ролью `CL_COLOR_SURFACE_HOVER`.

- Изображения: ресурс `cl_image_t` (image.h, сырой RGBA8),
  `cl_paint_draw_image`, операции SPI `draw_image`/`evict_image` во всех трёх
  рендерах (GL — кэш текстур, software — масштабируемый блит с блендингом,
  mock — запись команды) и виджет `cl_imageview`.
- Виджеты: `cl_list` (выбор мышью/клавиатурой, PageUp/Down, активация
  двойным кликом/Enter, remove/clear), `cl_progressbar`,
  `cl_messagebox_show` (модальный диалог OK/Cancel/Yes-No с Enter/Escape).
- Стек оверлеев: подменю (`cl_menu_add_submenu`, клик/Enter/Right открывают,
  Escape/Left закрывают только подменю), виджет `cl_menubar`, модальные
  диалоги `cl_window_open_modal` (клик снаружи не закрывает).
- Курсоры мыши: `cl_cursor_t` (default/ibeam/hand/crosshair/size),
  `cl_widget_set_cursor` (форма наследуется от ближайшего предка),
  операция SPI `set_cursor`; textbox по умолчанию показывает I-beam.
- `cl_key_t` расширен: цифровой ряд `CL_KEY_0..9`, `CL_KEY_F1..F12`,
  `CL_KEY_PAGE_UP`/`CL_KEY_PAGE_DOWN` (замаплены в SDL-бэкенде).

- Виджеты компоновки: `cl_panel` (группирующая поверхность с padding и
  рамкой), `cl_spacer` (фиксированный/гибкий промежуток),
  `cl_radiogroup` (колонка взаимоисключающих радиокнопок с автоматическим
  group-id и колбэком выбора).
- API-симметрия: `cl_combobox_item_text/remove/clear`,
  `cl_menu_item_text/remove/clear`, геттеры
  `cl_widget_preferred_size/margin/align_h/align_v/flex/is_focusable`.

- Анимации (`animation.h`): `cl_animation_start`/`cl_animation_cancel` —
  callback-анимации на общем ~60 Гц тикере с прогрессом от прошедшего
  времени (отставший цикл перепрыгивает вперёд, а не замедляется), easing
  linear/in/out/in-out, `on_done` с исходом (завершена/отменена), цепочки и
  параллельная композиция; хелперы `cl_ease`, `cl_lerp`, `cl_color_lerp`,
  `cl_rect_lerp`.
- Примитивы рендера: `cl_paint_push_transform`/`pop_transform`
  (translate + scale для поддерева, действует и на клипы) и
  `cl_paint_push_opacity`/`pop_opacity` (групповое умножение альфы) во всех
  трёх рендерах; новые операции SPI `push_transform`/`push_opacity` с
  парными pop.
- Damage-регионы: `cl_widget_invalidate` копит bounding-rect инвалидаций;
  software-путь очищает, рисует и презентует только его (операция SPI
  `set_damage` рендера, `present_region` платформы —
  `SDL_UpdateWindowSurfaceRects`). GL и полные инвалидации перерисовывают
  кадр целиком, как раньше.
- Пейсинг software-пути: презенты троттлятся до частоты дисплея
  frame-limiter'ом по `now_ms` (фолбэк 60 Гц) — анимация больше не
  презентует со скоростью цикла.
- Ввод в модальных диалогах: клик фокусирует ближайший фокусируемый виджет и
  захватывает указатель на время драга; клавиши и текст идут фокусу внутри
  верхнего оверлея (всплывая до корня диалога — Enter/Escape), Tab циклит
  фокусируемые виджеты диалога, hover и курсор работают над оверлеями.
- Mock-рендерер: команды несут указатель нарисованного изображения; регион
  `set_damage` последнего кадра доступен тестам; push/pop
  transform/opacity записываются, геометрия draw-команд — уже
  трансформированной.

### Изменено

- `cl_menu_create` принимает `cl_menu_desc_t` (последний виджет без desc);
  существующие вызовы обновляются добавлением
  `&(cl_menu_desc_t){ CL_MENU_DESC_INIT_FIELDS }`.

- `cl_widget_destroy`: поддерево отсоединяется немедленно, но память
  присоединённых виджетов освобождается в конце текущей итерации цикла
  (DEAD-очередь) — уничтожение любого виджета из любого callback безопасно,
  повторный destroy — no-op.

## [0.1.0] — 2026-07-12

Первый выпуск.

### Добавлено

- Ядро приложения: `cl_application_*` (run/step/quit, задачи из других
  потоков через `cl_application_post`, таймеры), одно окно `cl_window_*`
  с veto-колбэком закрытия.
- Виджеты: label, button, checkbox, radiobutton, slider, textbox
  (однострочный/многострочный, пароль, undo/redo, выделение мышью и
  клавиатурой, пословная навигация, IME), combobox, menu, tooltip,
  scrollview; контейнеры vbox/hbox (flex-веса, per-child выравнивание,
  margin/padding).
- Рендеры: OpenGL 3.3 core (глиф-атлас, HiDPI) и software (CPU-растеризация
  в буфер окна); `CL_RENDER_AUTO` с рантайм-фолбэком GL → software и
  переопределением `COPAL_RENDER=software`.
- Платформа: SDL2 (GL- и software-пути), события с модификаторами и
  счётчиком кликов, буфер обмена, IME-прямоугольник, EXPOSE.
- Текст: UTF-8 (валидирующий декодер), TrueType через stb_truetype,
  `cl_font_load_system()` с `COPAL_FONT`, кэш advance для Latin-1 и
  кириллицы, инвалидация глиф-кэшей при `cl_font_release`.
- Темы: светлая/тёмная, смена в рантайме.
- Расширяемость: инъекция платформы/рендерера/аллокатора через desc;
  кастомные виджеты и контейнеры через `widget_impl.h` (vtable с
  версионированием `vtable_size`).
- Сборка: CMake ≥ 3.16, опции SDL/OpenGL/shared/санитайзеры/покрытие,
  `find_package(copal)`, pkg-config `copal.pc`, вендоринг SDL2 по пину
  (`COPAL_FETCH_SDL2`).
- Тесты: 17 headless-сьютов на mock-бэкендах (layout, lifecycle, OOM-свип,
  golden-тесты software-рендера, UTF-8-таблица) + smoke-прогоны примеров.

### Известные ограничения

- Одно окно; меню без подменю; нет image-примитива, курсоров мыши,
  hover-событий, drag&drop.
- Нет text shaping/bidi: 1 кодовая точка = 1 глиф.
- Шрифты загружаются из доверенного источника (stb_truetype не проверяет
  усечённые данные по длине буфера).
