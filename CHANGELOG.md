# Changelog

Формат — [Keep a Changelog](https://keepachangelog.com/ru/1.1.0/);
нумерация версий — [SemVer](https://semver.org/lang/ru/). До 1.0 минорные
версии могут менять API/ABI (ABI-рукопожатие в desc-структурах отклонит
несовместимого потребителя).

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

- API-симметрия: `cl_combobox_item_text/remove/clear`,
  `cl_menu_item_text/remove/clear`, геттеры
  `cl_widget_preferred_size/margin/align_h/align_v/flex/is_focusable`.

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
