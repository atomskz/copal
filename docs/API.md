# copal — Public API

Статус: **актуально** (соответствует публичным заголовкам `include/copal/*` по
состоянию на Этап 7). Версия: 1.0. Текущая версия библиотеки — **0.1.0**.
Нотация — каноническая (`cl_*` / `cl_*_t` / `CL_*`), см. [CODESTYLE §2](CODESTYLE.md).
Опирается на [ARCHITECTURE.md](ARCHITECTURE.md).

Все сигнатуры ниже приведены **как в заголовках** и подразумевают `CL_API`
(макрос экспорта опущен для краткости). Единицы измерения в публичном API —
**логические пиксели** (`float`); перевод в физические — в renderer
(ARCHITECTURE §8.3). Все строки — **UTF-8**.

Часть 1 (§1–§10): соглашения, типы, handles, enum, события, callbacks, владение,
ошибки, версионирование, мини-пример.
Часть 2 (§11–§22): сигнатуры по модулям.

---

## 1. Соглашения об именовании

| Категория | Правило | Пример |
|-----------|---------|--------|
| Тип | `snake_case` + суффикс `_t`, префикс `cl_` | `cl_widget_t`, `cl_window_desc_t` |
| Функция | `cl_<модуль>_<действие>()` | `cl_window_create()`, `cl_widget_add_child()` |
| Макрос / enum-константа | `CL_*` (UPPER_SNAKE) | `CL_OK`, `CL_ALIGN_CENTER` |
| Callback-тип | `cl_<событие>_fn` | `cl_action_fn`, `cl_value_fn` |
| Desc-структура | `cl_<объект>_desc_t` | `cl_button_desc_t` |
| Getter (предикат) | `bool cl_<объект>_is_<свойство>()` | `cl_widget_is_visible()` |
| Setter | `void cl_<объект>_set_<свойство>()` | `cl_widget_set_visible()` |

---

## 2. Непрозрачные хэндлы (opaque handles)

Объявляются как `typedef struct cl_x cl_x_t;`, тело скрыто; доступ — только через
функции.

| Хэндл | Создаётся | Уничтожается | Владелец |
|-------|-----------|--------------|----------|
| `cl_application_t` | `cl_application_create()` | `cl_application_destroy()` | пользователь |
| `cl_window_t` | `cl_window_create()` | `cl_window_destroy()` / уничтожением app | приложение |
| `cl_timer_t` | `cl_timer_create()` | `cl_timer_cancel()` / уничтожением app | приложение |
| `cl_theme_t` | вместе с приложением (встроенная) | вместе с приложением | приложение |
| `cl_font_t` | `cl_font_load_*()` | `cl_font_release()` | пользователь |
| `cl_paint_context_t` | библиотека (в `paint`) | библиотека | библиотека (не хранить!) |
| `cl_platform_t` / `cl_renderer_t` | DI или встроенный bootstrap | вместе с приложением | приложение |

`cl_paint_context_t` действителен только внутри вызова `paint`; сохранять его или
использовать вне paint — запрещено.

`cl_widget_t` — **полу-непрозрачен**: для приложения (`<copal/copal.h>`) он
непрозрачен и используется через `cl_widget_*` и конкретные конструкторы
(`cl_button_create()` и т. п.); для авторов виджетов база раскрыта через
`<copal/widget_impl.h>` (§13).

## 3. Публичные value-типы (`types.h`)

```c
typedef struct cl_point  { float x, y; }                     cl_point_t;
typedef struct cl_size   { float w, h; }                     cl_size_t;
typedef struct cl_rect   { float x, y, w, h; }               cl_rect_t;
typedef struct cl_insets { float left, top, right, bottom; } cl_insets_t;
typedef struct cl_color  { uint8_t r, g, b, a; }             cl_color_t; /* НЕ premultiplied */

typedef struct cl_constraints { cl_size_t min; cl_size_t max; } cl_constraints_t;

typedef enum cl_align { CL_ALIGN_START, CL_ALIGN_CENTER, CL_ALIGN_END,
                        CL_ALIGN_STRETCH,
                        CL_ALIGN_AUTO /* per-child: как у контейнера */ } cl_align_t;
typedef enum cl_orientation { CL_ORIENT_HORIZONTAL, CL_ORIENT_VERTICAL }
                            cl_orientation_t;

#define CL_UNBOUNDED (3.4e38f)   /* маркер бесконечного ограничения в measure */

static inline cl_color_t cl_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
```

### 3.1 Desc-структуры и ABI-рукопожатие

Каждый `*_desc_t` начинается двумя служебными полями (`abi_version`,
`struct_size`), штампуемыми макросом-инициализатором (ARCHITECTURE §9, ADR-005).
Основной идиом — `CL_<OBJ>_DESC_INIT_FIELDS` (служебные поля для designated
initializer / составного литерала), доступный у **всех** desc:

```c
cl_window_desc_t wd = { CL_WINDOW_DESC_INIT_FIELDS,
                        .title = "Example", .width = 800, .height = 600 };
cl_button_create(app, &(cl_button_desc_t){ CL_BUTTON_DESC_INIT_FIELDS,
                                           .text = "Close" });
```

У каждого desc есть оба инициализатора: `CL_<OBJ>_DESC_INIT_FIELDS` штампует
только служебные поля (`abi_version`/`struct_size`) для использования внутри
составного литерала с designated-инициализаторами, а полный
`CL_<OBJ>_DESC_INIT` (= `{ CL_<OBJ>_DESC_INIT_FIELDS }`) даёт весь desc по
умолчанию — прочие поля нулевые.

`cl_*_create()` сверяет ABI-заголовок desc'а, и рукопожатие **эволюционируемо**:
совместим тот же мажор версии и размер не меньше служебного заголовка
(`abi_version`+`struct_size`). Библиотека нормализует desc в свою полную
структуру — незаполненный (более короткий, из старого заголовка) хвост трактуется
как значение по умолчанию, лишний (из более нового заголовка) игнорируется.
Поэтому в пределах одного мажора поля можно **дописывать в конец** desc, не ломая
уже собранных потребителей. Иной мажор или размер меньше заголовка → `NULL` +
`CL_ERROR_ABI_MISMATCH`. Ops-таблицы бэкендов должны нести весь базовый набор
(отсутствующую операцию нельзя вызвать), поэтому более короткая таблица
отклоняется, а более длинная (новый бэкенд) — принимается.

## 4. Enum'ы

```c
/* error.h */
typedef enum cl_result {
    CL_OK = 0,
    CL_ERROR_INVALID_ARGUMENT, CL_ERROR_OUT_OF_MEMORY, CL_ERROR_PLATFORM,
    CL_ERROR_RENDERER, CL_ERROR_FONT, CL_ERROR_UNSUPPORTED, CL_ERROR_ABI_MISMATCH
} cl_result_t;

typedef enum cl_log_level { CL_LOG_DEBUG, CL_LOG_INFO, CL_LOG_WARN, CL_LOG_ERROR }
                          cl_log_level_t;

/* event.h */
typedef enum cl_event_type {
    CL_EVENT_MOUSE_DOWN, CL_EVENT_MOUSE_UP, CL_EVENT_MOUSE_MOVE,
    CL_EVENT_MOUSE_WHEEL, CL_EVENT_MOUSE_ENTER, CL_EVENT_MOUSE_LEAVE,
    CL_EVENT_KEY_DOWN, CL_EVENT_KEY_UP, CL_EVENT_TEXT_INPUT,
    CL_EVENT_TEXT_EDIT,          /* IME pre-edit (композиция) */
    CL_EVENT_FOCUS_GAINED, CL_EVENT_FOCUS_LOST
} cl_event_type_t;

typedef enum cl_mouse_button { CL_MOUSE_LEFT, CL_MOUSE_MIDDLE, CL_MOUSE_RIGHT }
                             cl_mouse_button_t;

typedef enum cl_key_mods {   /* битовая маска */
    CL_MOD_NONE = 0, CL_MOD_SHIFT = 1<<0, CL_MOD_CTRL = 1<<1,
    CL_MOD_ALT = 1<<2, CL_MOD_SUPER = 1<<3
} cl_key_mods_t;

typedef enum cl_key {
    CL_KEY_UNKNOWN = 0,
    CL_KEY_LEFT, CL_KEY_RIGHT, CL_KEY_UP, CL_KEY_DOWN,
    CL_KEY_HOME, CL_KEY_END, CL_KEY_BACKSPACE, CL_KEY_DELETE,
    CL_KEY_ENTER, CL_KEY_TAB, CL_KEY_ESCAPE, CL_KEY_SPACE,
    CL_KEY_PAGE_UP, CL_KEY_PAGE_DOWN,
    CL_KEY_A, CL_KEY_B, /* ... */ CL_KEY_Z,  /* буквы — для сочетаний с модификаторами */
    CL_KEY_0, /* ... */ CL_KEY_9,            /* цифровой ряд */
    CL_KEY_F1, /* ... */ CL_KEY_F12
} cl_key_t;

/* theme.h */
typedef enum cl_color_role {
    CL_COLOR_BACKGROUND, CL_COLOR_SURFACE, CL_COLOR_SURFACE_HOVER,
    CL_COLOR_SURFACE_ACTIVE, CL_COLOR_SURFACE_RAISED, CL_COLOR_TEXT,
    CL_COLOR_TEXT_MUTED, CL_COLOR_ACCENT, CL_COLOR_BORDER, CL_COLOR_FOCUS_RING,
    CL_COLOR_SELECTION, CL_COLOR_SHADOW, CL_COLOR__COUNT
} cl_color_role_t;

typedef enum cl_theme_variant { CL_THEME_LIGHT, CL_THEME_DARK } cl_theme_variant_t;
```

## 5. Событие (`cl_event_t`)

```c
typedef struct cl_event {
    cl_event_type_t type;
    cl_key_mods_t   mods;
    union {
        struct { cl_point_t pos; cl_mouse_button_t button;
                 int clicks; /* 1 = одиночный, 2 = двойной, ... */ } mouse;
        struct { cl_point_t pos; float dx, dy; }             wheel;
        struct { cl_key_t key; bool repeat; }                key;
        struct { const char *utf8; }                         text;  /* NUL-терм. */
        struct { const char *utf8; int cursor; }             edit;  /* IME: строка + каретка (в кодовых точках) */
    } data;
} cl_event_t;
```

## 6. Callbacks (указатель на функцию + `void *user`)

```c
/* allocator.h */
typedef struct cl_allocator {
    void *userdata;
    void *(*alloc)(void *userdata, size_t size);
    void *(*realloc)(void *userdata, void *ptr, size_t size);
    void  (*free)(void *userdata, void *ptr);
} cl_allocator_t;

/* allocator.h — функции аллокатора. NULL везде выбирает встроенный malloc-дефолт;
 * при неудаче выделения обёртки фиксируют CL_ERROR_OUT_OF_MEMORY (cl_last_error). */
const cl_allocator_t *cl_allocator_default(void);
void *cl_alloc(const cl_allocator_t *a, size_t size);
void *cl_realloc(const cl_allocator_t *a, void *ptr, size_t size);
void  cl_free(const cl_allocator_t *a, void *ptr);

/* event.h — виджет-события */
typedef void (*cl_action_fn)(cl_widget_t *w, void *user);                 /* клик/действие */
typedef void (*cl_text_changed_fn)(cl_widget_t *w, const char *utf8, void *user);
typedef void (*cl_toggled_fn)(cl_widget_t *w, bool checked, void *user);  /* Checkbox/Radio */
typedef void (*cl_value_fn)(cl_widget_t *w, float value, void *user);     /* Slider */
typedef void (*cl_selection_fn)(cl_widget_t *w, int index, void *user);   /* ComboBox */

/* application.h / window.h / timer.h / error.h */
typedef void (*cl_task_fn)(void *user);                                   /* post из другого потока */
typedef bool (*cl_window_close_fn)(cl_window_t *win, void *user);         /* false = veto */
typedef void (*cl_timer_fn)(cl_timer_t *timer, void *user);
typedef void (*cl_log_fn)(cl_log_level_t level, const char *msg, void *user);
```

Правила: `cl_widget_destroy()` **отсоединяет сразу, освобождает отложенно**
(DEAD-очередь, ARCHITECTURE §5): поддерево немедленно исчезает из дерева,
hit-теста и событий, а память присоединённых к окну виджетов освобождается в
конце текущей итерации цикла. Поэтому уничтожать любой виджет из любого
callback безопасно; повторный destroy — no-op. Никогда не присоединённое к
окну дерево освобождается немедленно. `void *user` — «слабый»: библиотека им
не владеет.

## 7. Правила владения

- Иерархия: приложение → окно → корневой виджет → дети (рекурсивно).
- `cl_widget_add_child(parent, child)` — **владение переходит** к `parent`.
- `cl_widget_remove_child(parent, child)` — владение **возвращается** вызвавшему
  (обязан позже `cl_widget_destroy()` или переустановить).
- `cl_widget_destroy(w)` — отсоединяет поддерево немедленно, освобождает в
  конце итерации цикла (безопасно из любых callback — см. §6).
- `cl_window_set_content(win, root)`, `cl_scrollview_set_content(sv, content)`,
  `cl_window_open_popup(win, popup, at)` — принимающая сторона **владеет** поддеревом.
- Возвращаемые библиотекой строки (`cl_textbox_text`, `cl_combobox_selected_text`,
  `cl_widget_tooltip`, `cl_textbox_preedit`) — **не владеющие**, действительны до
  следующей мутации объекта.
- Живые виджеты — только в куче (конструкторы `cl_*_create`).

## 8. Модель ошибок

- Fallible-операции (`create`, `load`, `add_*`) → указатель (`NULL` при неудаче)
  **или** `cl_result_t`.
- Диагностика последней ошибки — thread-local:
  ```c
  cl_result_t cl_last_error(void);
  const char *cl_result_string(cl_result_t result);
  void        cl_set_log_callback(cl_log_fn fn, void *user);
  ```
- Лог-колбэк — **единственный** механизм логирования (process-wide); получает
  диагностику библиотеки (отказ бэкендов, ошибки шейдеров GL, отвергнутые
  шрифты). Без колбэка WARN/ERROR идут в stderr.
- Простые сеттеры — `void`, валидируют/клампят вход.
- Коды: `CL_ERROR_INVALID_ARGUMENT`, `CL_ERROR_OUT_OF_MEMORY`, `CL_ERROR_PLATFORM`,
  `CL_ERROR_RENDERER`, `CL_ERROR_FONT`, `CL_ERROR_UNSUPPORTED`, `CL_ERROR_ABI_MISMATCH`.

## 9. Версионирование (`version.h`)

```c
#define COPAL_VERSION_MAJOR 0
#define COPAL_VERSION_MINOR 1
#define COPAL_VERSION_PATCH 0
#define COPAL_VERSION_ENCODE(ma, mi, pa) (((ma) << 16) | ((mi) << 8) | (pa))
#define COPAL_VERSION COPAL_VERSION_ENCODE(COPAL_VERSION_MAJOR, COPAL_VERSION_MINOR, COPAL_VERSION_PATCH)

uint32_t    cl_version_runtime(void);
const char *cl_version_string(void);   /* "0.1.0" */
```

Несоответствие заголовков и `.so`/`.dll` ловится ABI-рукопожатием в `*_desc_t`
(§3.1) и `cl_version_runtime()`.

**Политика ABI:** в пределах одного мажора публичные desc/ops-структуры только
дополняются полями в конце — это бинарно совместимо благодаря tail-tolerant
рукопожатию (§3.1); ломающие изменения (перестановка/удаление/смена смысла полей,
рост базы виджета или vtable) повышают мажор и `SOVERSION`. До 1.0 ABI ещё не
заморожен: мажор остаётся 0, поэтому рукопожатие пропустит любой 0.x —
пересобирайте потребителей под каждую 0.x-версию.

## 10. Мини-пример

```c
#include <copal/copal.h>

static void on_close(cl_widget_t *w, void *user) { (void)w;
    cl_application_quit((cl_application_t *)user, 0);
}

int main(void)
{
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_application_t *app = cl_application_create(&ad);

    cl_window_desc_t wd = CL_WINDOW_DESC_INIT;
    wd.title = "Example"; wd.width = 800; wd.height = 600;
    cl_window_t *win = cl_window_create(app, &wd);

    cl_widget_t *root = cl_vbox_create(app, &(cl_vbox_desc_t){
        CL_VBOX_DESC_INIT_FIELDS, .spacing = 8, .padding = { 12, 12, 12, 12 } });
    cl_widget_t *label = cl_label_create(app, &(cl_label_desc_t){
        CL_LABEL_DESC_INIT_FIELDS, .text = "Hello" });
    cl_widget_t *button = cl_button_create(app, &(cl_button_desc_t){
        CL_BUTTON_DESC_INIT_FIELDS, .text = "Close" });

    cl_button_set_on_click(button, on_close, app);
    cl_widget_add_child(root, label);
    cl_widget_add_child(root, button);
    cl_window_set_content(win, root);
    cl_window_show(win);

    int rc = cl_application_run(app);
    cl_application_destroy(app);   /* уничтожает окно и дерево виджетов */
    return rc;
}
```

Все конструкторы виджетов принимают `cl_application_t *app` первым аргументом:
app даёт аллокатор, доступ к теме/шрифтам и предсказуемое владение памятью без
глобального состояния. Виджет привязывается к окну позже — через
`add_child`/`set_content`.

---

# Часть 2 — сигнатуры по модулям

## 11. Application (`application.h`)

```c
/* Встроенный рендер-бэкенд, если platform/renderer не инъецированы. */
typedef enum cl_render_backend {
    CL_RENDER_AUTO = 0,  /* OpenGL если собран, иначе software */
    CL_RENDER_GL,        /* OpenGL-рендерер */
    CL_RENDER_SOFTWARE   /* CPU-растеризатор, без GPU-контекста */
} cl_render_backend_t;

typedef struct cl_application_desc {
    uint32_t abi_version; size_t struct_size;
    const cl_allocator_t *allocator;      /* NULL -> встроенный malloc */
    cl_platform_t        *platform;       /* DI бэкенда; владение → app */
    cl_renderer_t        *renderer;       /* DI бэкенда; владение → app */
    cl_render_backend_t   render_backend; /* выбор встроенного бэкенда (0 = AUTO) */
} cl_application_desc_t;
#define CL_APPLICATION_DESC_INIT \
    { .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_application_desc_t) }

cl_application_t *cl_application_create(const cl_application_desc_t *desc);
void              cl_application_destroy(cl_application_t *app);   /* уничтожает окно и деревья */

int  cl_application_run(cl_application_t *app);            /* блокирующий цикл; код выхода */
bool cl_application_step(cl_application_t *app, bool wait); /* один шаг; false = пора выйти */
void cl_application_quit(cl_application_t *app, int exit_code);

/* Поставить fn(user) в очередь GUI-потока. Потокобезопасно (из любого потока);
 * FIFO; выполняется внутри run()/step(); задача может ставить новые задачи;
 * недренированные при destroy — отбрасываются. Требует потокобезопасного
 * аллокатора (дефолтный такой). Return: CL_OK / INVALID_ARGUMENT / OUT_OF_MEMORY. */
cl_result_t cl_application_post(cl_application_t *app, cl_task_fn fn, void *user);

cl_theme_t           *cl_application_theme(cl_application_t *app);
const cl_allocator_t *cl_application_allocator(cl_application_t *app);
```

Если `platform`/`renderer` не заданы (NULL), используется встроенный SDL2-бэкенд
(при сборке с `COPAL_ENABLE_SDL`); иначе `create` возвращает `NULL` +
`CL_ERROR_UNSUPPORTED`. `render_backend` выбирает рендерер: **GL** (по умолчанию
при AUTO, если собран OpenGL) или **software** (CPU-растеризатор без GL-контекста —
меньше памяти и быстрый старт, работает по RDP/в CI; см. ADR-015). При AUTO
software можно включить рантайм-переменной **`COPAL_RENDER=software`**; если
GL-окно не создаётся (headless/RDP/старый драйвер), AUTO **сам откатывается** на
software (WARN в лог); явный `CL_RENDER_GL` не откатывается — падает громко. Сборка
`COPAL_ENABLE_SDL` **без** `OPENGL` даёт software-бэкенд без линковки libGL.
`run` = «свой цикл» (возвращает exit_code из `quit`); `step(wait=true)` — один
шаг: ожидание ограничено ближайшим таймером (без таймеров — срезом ~100 мс),
всегда возвращается к вызывающему; idle-цикл `while (step(app, true))` не
выжигает ядро.

## 12. Window + overlay/popup + tooltip (`window.h`)

```c
typedef bool (*cl_window_close_fn)(cl_window_t *win, void *user);  /* false = veto */

typedef struct cl_window_desc {
    uint32_t abi_version; size_t struct_size;
    const char *title;                 /* UTF-8; NULL допустим */
    int32_t     width, height;
    int32_t     min_width, min_height; /* 0 -> без ограничения */
    bool        resizable;
} cl_window_desc_t;
#define CL_WINDOW_DESC_INIT \
    { .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_window_desc_t) }

/* В MVP — одно окно; второе → NULL + CL_ERROR_UNSUPPORTED (ADR-011). */
cl_window_t *cl_window_create(cl_application_t *app, const cl_window_desc_t *desc);
void         cl_window_destroy(cl_window_t *win);

void         cl_window_show(cl_window_t *win);
void         cl_window_set_content(cl_window_t *win, cl_widget_t *root); /* окно владеет root;
                                       замена уничтожает прежнее поддерево (NULL = очистить) */
cl_widget_t *cl_window_content(cl_window_t *win);
cl_application_t *cl_window_application(cl_window_t *win);
void         cl_window_set_title(cl_window_t *win, const char *utf8);
cl_size_t    cl_window_size(cl_window_t *win);
void         cl_window_set_on_close(cl_window_t *win, cl_window_close_fn fn, void *user);

/* Overlay-стек поверх content (попапы, подменю, диалоги). open_popup показывает
 * `popup` в позиции окна `at` (клампится по экрану) и берёт его во владение,
 * заменяя открытую цепочку; open_modal центрирует диалог и делает его барьером:
 * клики вне модала проглатываются, light-dismiss и замена работают только НАД
 * ним. Клик вне цепочки, либо close_popup(), закрывает её; закрытие отложено
 * (обработчик popup'а может сам запросить закрытие). */
void         cl_window_open_popup(cl_window_t *win, cl_widget_t *popup, cl_point_t at);
void         cl_window_open_modal(cl_window_t *win, cl_widget_t *dialog);
void         cl_window_close_popup(cl_window_t *win);
cl_widget_t *cl_window_popup(cl_window_t *win);

/* Текущий tooltip-пузырь hover-слоя, или NULL (интроспекция/тесты; владение — окна). */
cl_widget_t *cl_window_tooltip(cl_window_t *win);
```

Механизм overlay используют ComboBox и Menu (§18). Всплывашка клипуется границами
окна (ADR-011).

Ввод при открытых оверлеях: меню получают клавиатуру корнем (навигация, Escape)
и мышь без захвата (drag-to-select работает через всю цепочку). Модальные
диалоги ведут себя как content: клик внутри переводит фокус на ближайший
фокусируемый виджет и захватывает указатель на время драга; когда фокус внутри
верхнего оверлея, клавиши и текстовый ввод идут ему (всплывая до корня диалога
— Enter/Escape), необработанный Tab циклит фокусируемые виджеты диалога;
движение мыши над оверлеями обновляет hover и курсор.

## 13. Timer (`timer.h`)

```c
typedef void (*cl_timer_fn)(cl_timer_t *timer, void *user);

/* Таймеры срабатывают из цикла приложения (run/step), в том же потоке, между
 * dispatch и рендером. Тайминг — best-effort: срабатывание может опоздать
 * (никогда не раньше), отставший repeat-таймер коалесцирует пропуски в одно;
 * порядок срабатывания одновременно просроченных таймеров не специфицирован.
 * Таймер принадлежит приложению. interval_ms == 0 для one-shot срабатывает на
 * следующем опросе; repeat флорится до 1 мс. NULL при OOM/отсутствии часов. */
cl_timer_t *cl_timer_create(cl_application_t *app, uint32_t interval_ms,
                            bool repeat, cl_timer_fn fn, void *user);
void        cl_timer_cancel(cl_timer_t *timer);   /* стоп + free; безопасно из своего callback */
void        cl_timer_restart(cl_timer_t *timer);  /* перезапуск от «сейчас» */
```

Хэндл остаётся действительным после срабатывания one-shot (можно `restart`);
недействителен только после `cl_timer_cancel`.

## 13a. Animation (`animation.h`)

```c
typedef enum cl_easing { CL_EASE_LINEAR, CL_EASE_IN, CL_EASE_OUT, CL_EASE_IN_OUT } cl_easing_t;

typedef void (*cl_animation_fn)(cl_animation_t *anim, float t, void *user);      /* t ∈ [0,1], eased */
typedef void (*cl_animation_done_fn)(cl_animation_t *anim, bool finished, void *user);

typedef struct cl_animation_desc {
    uint32_t abi_version;  size_t struct_size;   /* CL_ANIMATION_DESC_INIT_FIELDS */
    uint32_t duration_ms;                        /* 0 — завершается на первом тике */
    cl_easing_t easing;
    cl_animation_fn on_progress;                 /* обязателен */
    cl_animation_done_fn on_done;                /* finished=false при отмене */
    void *user;
} cl_animation_desc_t;

cl_animation_t *cl_animation_start(cl_application_t *app, const cl_animation_desc_t *desc);
void            cl_animation_cancel(cl_animation_t *anim);  /* on_done(false) + free */

/* Интерполяция «анимируемых значений». */
float      cl_ease(cl_easing_t easing, float t);
float      cl_lerp(float from, float to, float t);
cl_color_t cl_color_lerp(cl_color_t from, cl_color_t to, float t);
cl_rect_t  cl_rect_lerp(cl_rect_t from, cl_rect_t to, float t);
```

Все анимации разделяют один тикер ~60 Гц поверх таймеров; прогресс считается от
прошедшего времени (`now_ms() - start`), а не от числа тиков — отставший цикл
«перепрыгивает» вперёд, а не замедляет анимацию. Финальный вызов `on_progress`
всегда получает ровно `1.0`. Анимации комбинируются: любое число может идти
одновременно, цепочка строится стартом следующей из `on_done`.

Владение отличается от таймера: анимация освобождает себя по завершении или
отмене — после `on_done` хэндл недействителен (обнуляйте его в `on_done`, если
храните для `cancel`). Живые на момент разрушения приложения анимации
освобождаются без коллбеков. `NULL` при OOM, отсутствии часов у платформы,
пропущенном `on_progress` или ABI-несовпадении desc.

## 14. Widget — базовый (`widget.h`)

```c
/* Дерево / владение. */
cl_result_t  cl_widget_add_child(cl_widget_t *parent, cl_widget_t *child);    /* владение → parent */
cl_result_t  cl_widget_remove_child(cl_widget_t *parent, cl_widget_t *child); /* владение → вызвавшему */
void         cl_widget_destroy(cl_widget_t *w);            /* уничтожает поддерево */
cl_widget_t *cl_widget_parent(cl_widget_t *w);
cl_window_t *cl_widget_window(cl_widget_t *w);

/* Геометрия / состояние. */
cl_rect_t cl_widget_rect(cl_widget_t *w);                  /* назначенный прямоугольник (абсолютный) */
void      cl_widget_set_visible(cl_widget_t *w, bool v);
bool      cl_widget_is_visible(cl_widget_t *w);
void      cl_widget_set_enabled(cl_widget_t *w, bool e);
void      cl_widget_set_cursor(cl_widget_t *w, cl_cursor_t cursor); /* форма при hover */
cl_cursor_t cl_widget_cursor(cl_widget_t *w);
bool      cl_widget_is_enabled(cl_widget_t *w);

/* Layout-атрибуты ребёнка (читают box-контейнеры):
 * preferred_size переопределяет собственный measure виджета по осям, где > 0;
 * flex > 0 получает эту долю остатка главной оси (только рост; 0 = fixed);
 * align переопределяет align_cross контейнера по поперечной оси
 * (CL_ALIGN_AUTO — дефолт — берёт выравнивание контейнера; компонент главной
 * оси box-ами игнорируется). */
void cl_widget_set_preferred_size(cl_widget_t *w, cl_size_t s);
void cl_widget_set_margin(cl_widget_t *w, cl_insets_t m);
void cl_widget_set_align(cl_widget_t *w, cl_align_t h, cl_align_t v);
void cl_widget_set_flex(cl_widget_t *w, float weight);
cl_size_t   cl_widget_preferred_size(cl_widget_t *w);     /* геттеры атрибутов выше */
cl_insets_t cl_widget_margin(cl_widget_t *w);
cl_align_t  cl_widget_align_h(cl_widget_t *w);
cl_align_t  cl_widget_align_v(cl_widget_t *w);
float       cl_widget_flex(cl_widget_t *w);

/* Фокус. */
void cl_widget_set_focusable(cl_widget_t *w, bool focusable);
bool cl_widget_is_focusable(cl_widget_t *w);
bool cl_widget_focus(cl_widget_t *w);                      /* запросить фокус; false если нельзя */
bool cl_widget_has_focus(cl_widget_t *w);

/* Инвалидция. */
void cl_widget_invalidate(cl_widget_t *w);                 /* перерисовать */
void cl_widget_invalidate_layout(cl_widget_t *w);          /* пересчитать layout */

/* Userdata. */
void  cl_widget_set_userdata(cl_widget_t *w, void *user);
void *cl_widget_userdata(cl_widget_t *w);

/* Hover-подсказка (текст копируется; NULL или "" очищает). */
void        cl_widget_set_tooltip(cl_widget_t *w, const char *utf8);
const char *cl_widget_tooltip(cl_widget_t *w);
```

## 15. Widget — база для авторов (`widget_impl.h`)

```c
#define CL_WIDGET_RESERVED 24

enum cl_widget_flags {
    CL_WF_VISIBLE = 1u<<0, CL_WF_ENABLED = 1u<<1, CL_WF_FOCUSABLE = 1u<<2,
    /* биты 3-4 зарезервированы (бывшие DIRTY/DEAD; не были реализованы) */
    CL_WF_CLIP = 1u<<5   /* клиповать детей прямоугольником этого виджета при paint */
};

typedef struct cl_widget_vtable {
    void      (*destroy)(cl_widget_t *w);
    cl_size_t (*measure)(cl_widget_t *w, cl_constraints_t c);
    void      (*arrange)(cl_widget_t *w, cl_rect_t rect);
    void      (*paint)(cl_widget_t *w, cl_paint_context_t *ctx);
    cl_rect_t (*clip_rect)(cl_widget_t *w);                 /* NULL -> весь rect (при CL_WF_CLIP) */
    void      (*reveal)(cl_widget_t *w, cl_rect_t target);  /* прокрутить потомка target в видимую зону */
    bool      (*hit_test)(cl_widget_t *w, cl_point_t p);    /* NULL -> прямоугольник */
    bool      (*on_event)(cl_widget_t *w, const cl_event_t *ev); /* NULL -> раскладка в удобные методы */
    bool (*mouse_down)(cl_widget_t *w, const cl_event_t *ev);
    bool (*mouse_up)(cl_widget_t *w, const cl_event_t *ev);
    bool (*mouse_move)(cl_widget_t *w, const cl_event_t *ev);
    bool (*mouse_wheel)(cl_widget_t *w, const cl_event_t *ev);
    void (*mouse_enter)(cl_widget_t *w);  /* hover: без всплытия */
    void (*mouse_leave)(cl_widget_t *w);
    bool (*key_down)(cl_widget_t *w, const cl_event_t *ev);
    bool (*key_up)(cl_widget_t *w, const cl_event_t *ev);
    bool (*text_input)(cl_widget_t *w, const cl_event_t *ev);
    bool (*text_edit)(cl_widget_t *w, const cl_event_t *ev); /* IME pre-edit */
    void (*focus_gained)(cl_widget_t *w);
    void (*focus_lost)(cl_widget_t *w);
} cl_widget_vtable_t;

struct cl_widget_class {
    const char               *name;
    const cl_widget_class_t   *base;          /* цепочка предков для RTTI */
    uint32_t                  type_id;        /* информационно/debug */
    size_t                    instance_size;
    const cl_widget_vtable_t *vtable;
    size_t                    vtable_size;    /* = sizeof(cl_widget_vtable_t);
                                     cl_widget_alloc сверяет (ABI vtable) */
};

/* Публичная база — ПЕРВОЕ поле производной структуры (наследование вложением). */
struct cl_widget {
    const cl_widget_class_t *cls;
    cl_application_t *app;      /* weak */
    cl_window_t      *window;   /* weak back-ref (O(1) зануление focus/hover) */
    cl_widget_t      *parent;   /* weak */
    cl_widget_t      *first_child, *last_child, *next_sibling;
    cl_rect_t         rect;     /* абсолютный, назначается arrange */
    cl_size_t         measured;
    cl_size_t         pref_size;
    cl_insets_t       margin;
    cl_align_t        align_h, align_v;
    float             flex;
    uint32_t          flags;
    void             *userdata;
    char             *tooltip;  /* owned UTF-8, или NULL */
    unsigned char     reserved[CL_WIDGET_RESERVED];
};

cl_widget_t *cl_widget_alloc(cl_application_t *app, const cl_widget_class_t *cls); /* zero-alloc + init_base */
void         cl_widget_init_base(cl_widget_t *w, cl_application_t *app,
                                 const cl_widget_class_t *cls);
void        *cl_widget_check_cast(cl_widget_t *w, const cl_widget_class_t *cls);   /* NULL при несовпадении */
bool         cl_widget_is_a(cl_widget_t *w, const cl_widget_class_t *cls);

#define CL_WIDGET_CAST(name, w) \
    ((name##_t *)cl_widget_check_cast((w), &name##_class))   /* checked; NULL при несовпадении */
#define CL_WIDGET_CAST_UNCHECKED(name, w) ((name##_t *)(w))  /* быстрый путь; UB при неверном типе */

/* Обвязка для КОНТЕЙНЕРОВ: измерить/разместить ребёнка из собственных
 * measure/arrange (применяют NULL-дефолты, учитывают pref_size, пишут
 * child->measured и child->rect — не зовите vtable ребёнка напрямую). */
cl_size_t cl_widget_do_measure(cl_widget_t *child, cl_constraints_t c);
void      cl_widget_do_arrange(cl_widget_t *child, cl_rect_t rect);
void      cl_widget_reveal(cl_widget_t *w); /* прокрутить предков до видимости w */
```

NULL-слот vtable означает поведение по умолчанию (`hit_test` = проверка по rect,
`on_event` = раскладка в `mouse_*`/`key_*`/`text_*`/`focus_*`). Слоты `clip_rect`,
`reveal`, `mouse_wheel`, `text_edit` — точки под ScrollView-клип, scroll-to-view,
колесо и IME соответственно; остальные виджеты их не задают.

## 16. Контейнеры / Layout (`layout.h`)

```c
typedef struct cl_vbox_desc {
    uint32_t abi_version; size_t struct_size;
    float spacing; cl_insets_t padding;
    cl_align_t align_cross;   /* выравнивание детей по поперечной (горизонтальной) оси */
} cl_vbox_desc_t;
#define CL_VBOX_DESC_INIT_FIELDS \
    .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_vbox_desc_t)
cl_widget_t *cl_vbox_create(cl_application_t *app, const cl_vbox_desc_t *desc);

/* cl_hbox_desc_t — то же (align_cross по вертикали); CL_HBOX_DESC_INIT_FIELDS. */
cl_widget_t *cl_hbox_create(cl_application_t *app, const cl_hbox_desc_t *desc);
```

Per-child атрибуты (`flex`, `margin`, `align`, `preferred_size`) задаются на самом
ребёнке через `cl_widget_set_*` (§14). Остаток главной оси (высота vbox / ширина
hbox сверх суммы измеренных размеров) распределяется между детьми с `flex > 0`
пропорционально весам; при нехватке места дети не ужимаются ниже измеренного.

## 17. Текстовые и управляющие виджеты (`widgets/*.h`)

```c
/* Label */
typedef struct cl_label_desc { uint32_t abi_version; size_t struct_size;
    const char *text; const cl_text_style_t *style; } cl_label_desc_t;  /* style NULL -> тема */
#define CL_LABEL_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_label_desc_t)
cl_widget_t *cl_label_create(cl_application_t *app, const cl_label_desc_t *desc);
void         cl_label_set_text(cl_widget_t *label, const char *utf8);

/* Button */
typedef struct cl_button_desc { uint32_t abi_version; size_t struct_size;
    const char *text; } cl_button_desc_t;
#define CL_BUTTON_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_button_desc_t)
cl_widget_t *cl_button_create(cl_application_t *app, const cl_button_desc_t *desc);
void         cl_button_set_text(cl_widget_t *button, const char *utf8);
void         cl_button_set_on_click(cl_widget_t *button, cl_action_fn fn, void *user);

/* CheckBox */
typedef struct cl_checkbox_desc { uint32_t abi_version; size_t struct_size;
    const char *text; bool checked; } cl_checkbox_desc_t;
#define CL_CHECKBOX_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_checkbox_desc_t)
cl_widget_t *cl_checkbox_create(cl_application_t *app, const cl_checkbox_desc_t *desc);
void  cl_checkbox_set_checked(cl_widget_t *cb, bool checked);   /* НЕ вызывает on_toggle */
bool  cl_checkbox_is_checked(cl_widget_t *cb);
void  cl_checkbox_set_text(cl_widget_t *cb, const char *utf8);
void  cl_checkbox_set_on_toggle(cl_widget_t *cb, cl_toggled_fn fn, void *user); /* только пользовательские */

/* RadioButton (взаимоисключение — по числовому group id, а не через отдельный контейнер) */
typedef struct cl_radiobutton_desc { uint32_t abi_version; size_t struct_size;
    const char *text; int group; bool selected; } cl_radiobutton_desc_t; /* group <= 0 = вне группы */
#define CL_RADIOBUTTON_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_radiobutton_desc_t)
cl_widget_t *cl_radiobutton_create(cl_application_t *app, const cl_radiobutton_desc_t *desc);
void  cl_radiobutton_set_selected(cl_widget_t *rb, bool selected); /* выбор снимает выбор с группы */
bool  cl_radiobutton_is_selected(cl_widget_t *rb);
void  cl_radiobutton_set_on_select(cl_widget_t *rb, cl_toggled_fn fn, void *user);

/* Slider */
typedef struct cl_slider_desc { uint32_t abi_version; size_t struct_size;
    float min, max, value, step; } cl_slider_desc_t;   /* step 0 -> (max-min)/20 */
#define CL_SLIDER_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_slider_desc_t)
cl_widget_t *cl_slider_create(cl_application_t *app, const cl_slider_desc_t *desc);
void  cl_slider_set_value(cl_widget_t *slider, float value);       /* клампится; НЕ вызывает on_change */
float cl_slider_value(cl_widget_t *slider);
void  cl_slider_set_range(cl_widget_t *slider, float min, float max);
void  cl_slider_set_on_change(cl_widget_t *slider, cl_value_fn fn, void *user);

/* TextBox (одно-/многострочный; password/readonly/multiline/max_length;
 * IME-композиция; выделение мышью: drag, Shift+клик, двойной клик = слово) */
typedef struct cl_textbox_desc { uint32_t abi_version; size_t struct_size;
    const char *text; const char *placeholder;
    bool password;   /* маска; password+multiline отклоняется (INVALID_ARGUMENT) */
    bool readonly;   /* навигация можно, ввод нельзя */
    bool multiline;  /* перенос по ширине + \n + вертикальный скролл */
    size_t max_length; /* макс. кодовых точек; 0 = без лимита */ } cl_textbox_desc_t;
#define CL_TEXTBOX_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_textbox_desc_t)
cl_widget_t *cl_textbox_create(cl_application_t *app, const cl_textbox_desc_t *desc);
void         cl_textbox_set_text(cl_widget_t *tb, const char *utf8);   /* НЕ вызывает on_changed */
const char  *cl_textbox_text(cl_widget_t *tb);          /* не владеющий; валиден до правки */
void         cl_textbox_set_on_changed(cl_widget_t *tb, cl_text_changed_fn fn, void *user);
void         cl_textbox_set_on_submit(cl_widget_t *tb, cl_text_changed_fn fn, void *user); /* Enter, однострочный */
size_t       cl_textbox_line_count(cl_widget_t *tb);    /* визуальных строк (multiline) */
size_t       cl_textbox_cursor_line(cl_widget_t *tb);   /* строка каретки (multiline) */
const char  *cl_textbox_preedit(cl_widget_t *tb);       /* строка IME-композиции у каретки, или NULL */
```

## 17a. Виджеты 0.2: list, progressbar, imageview, panel, spacer, radiogroup, menubar, messagebox

```c
/* widgets/list.h — выбор строк, полная клавиатура, активация */
cl_widget_t *cl_list_create(cl_application_t *app, const cl_list_desc_t *desc);
cl_result_t  cl_list_add_item(cl_widget_t *l, const char *text);
cl_result_t  cl_list_remove(cl_widget_t *l, size_t index);
void         cl_list_clear(cl_widget_t *l);
size_t       cl_list_count(cl_widget_t *l);
const char  *cl_list_item_text(cl_widget_t *l, size_t index);
int          cl_list_selected(cl_widget_t *l);            /* -1 = нет */
void         cl_list_set_selected(cl_widget_t *l, int index);
void         cl_list_set_on_select(cl_widget_t *l, cl_list_fn fn, void *user);
void         cl_list_set_on_activate(cl_widget_t *l, cl_list_fn fn, void *user);

/* widgets/progressbar.h */
cl_widget_t *cl_progressbar_create(cl_application_t *app, const cl_progressbar_desc_t *desc);
void         cl_progressbar_set_value(cl_widget_t *pb, float v); /* 0..1, клампится */
float        cl_progressbar_value(cl_widget_t *pb);

/* widgets/imageview.h + image.h (ресурс: сырой RGBA8, файлы не декодируются) */
cl_image_t  *cl_image_create(cl_application_t *app, int w, int h, const void *rgba);
void         cl_image_release(cl_image_t *img);           /* инвалидирует кэши рендера */
cl_size_t    cl_image_size(const cl_image_t *img);
const void  *cl_image_pixels(const cl_image_t *img);      /* RGBA8, для своих бэкендов */
cl_widget_t *cl_imageview_create(cl_application_t *app, const cl_imageview_desc_t *desc);
void         cl_imageview_set_image(cl_widget_t *iv, cl_image_t *img); /* borrowed */
cl_image_t  *cl_imageview_image(cl_widget_t *iv);

/* widgets/panel.h, widgets/spacer.h */
cl_widget_t *cl_panel_create(cl_application_t *app, const cl_panel_desc_t *desc);
cl_widget_t *cl_spacer_create(cl_application_t *app, const cl_spacer_desc_t *desc);

/* widgets/radiogroup.h — колонка взаимоисключающих радио */
cl_widget_t *cl_radiogroup_create(cl_application_t *app, const cl_radiogroup_desc_t *desc);
cl_widget_t *cl_radiogroup_add(cl_widget_t *g, const char *text);
size_t       cl_radiogroup_count(cl_widget_t *g);
int          cl_radiogroup_selected(cl_widget_t *g);
void         cl_radiogroup_set_selected(cl_widget_t *g, int index); /* без колбэка */
void         cl_radiogroup_set_on_change(cl_widget_t *g, cl_radiogroup_fn fn, void *user);

/* widgets/menubar.h — панель заголовков меню (владеет меню, переиспользует) */
cl_widget_t *cl_menubar_create(cl_application_t *app, const cl_menubar_desc_t *desc);
cl_result_t  cl_menubar_add_menu(cl_widget_t *bar, const char *title, cl_widget_t *menu);
size_t       cl_menubar_count(cl_widget_t *bar);

/* widgets/messagebox.h — модальный диалог поверх контента */
cl_widget_t *cl_messagebox_show(cl_window_t *win, const char *title, const char *text,
                                cl_msgbox_buttons_t buttons, cl_msgbox_fn fn, void *user);
```

## 18. Popup-виджеты: ComboBox и Menu (`widgets/combobox.h`, `widgets/menu.h`)

Оба используют overlay-слой окна (§12).

```c
/* ComboBox */
typedef struct cl_combobox_desc { uint32_t abi_version; size_t struct_size;
    const char *placeholder; } cl_combobox_desc_t;
#define CL_COMBOBOX_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_combobox_desc_t)
cl_widget_t *cl_combobox_create(cl_application_t *app, const cl_combobox_desc_t *desc);
cl_result_t  cl_combobox_add_item(cl_widget_t *combo, const char *text);
const char  *cl_combobox_item_text(cl_widget_t *combo, size_t index);
cl_result_t  cl_combobox_remove(cl_widget_t *combo, size_t index);
void         cl_combobox_clear(cl_widget_t *combo);
size_t       cl_combobox_count(cl_widget_t *combo);
void         cl_combobox_set_selected(cl_widget_t *combo, int index);  /* -1 = ничего; НЕ вызывает on_change */
int          cl_combobox_selected(cl_widget_t *combo);                 /* индекс или -1 */
const char  *cl_combobox_selected_text(cl_widget_t *combo);            /* текст или NULL */
void         cl_combobox_set_on_change(cl_widget_t *combo, cl_selection_fn fn, void *user);

/* Menu (popup-меню; собрать, затем cl_window_open_popup, который берёт владение) */
cl_widget_t *cl_menu_create(cl_application_t *app, const cl_menu_desc_t *desc);
cl_result_t  cl_menu_add_item(cl_widget_t *menu, const char *text, cl_action_fn fn, void *user);
cl_result_t  cl_menu_add_submenu(cl_widget_t *menu, const char *text, cl_widget_t *submenu);
const char  *cl_menu_item_text(cl_widget_t *menu, size_t index);
cl_result_t  cl_menu_remove(cl_widget_t *menu, size_t index);
void         cl_menu_clear(cl_widget_t *menu);
size_t       cl_menu_count(cl_widget_t *menu);
```

Пока не поддержано: подменю; открытие нового popup из callback пункта (меню
разрушается после возврата callback).

## 19. ScrollView (`widgets/scrollview.h`)

```c
typedef struct cl_scrollview_desc { uint32_t abi_version; size_t struct_size;
    bool horizontal;  /* разрешить горизонтальное переполнение и скролл */
    bool smooth;      /* анимировать скролл колесом вместо скачка */ } cl_scrollview_desc_t;
#define CL_SCROLLVIEW_DESC_INIT_FIELDS .abi_version = COPAL_VERSION, .struct_size = sizeof(cl_scrollview_desc_t)
cl_widget_t *cl_scrollview_create(cl_application_t *app, const cl_scrollview_desc_t *desc);
void         cl_scrollview_set_content(cl_widget_t *sv, cl_widget_t *content); /* владеет content */
cl_widget_t *cl_scrollview_content(cl_widget_t *sv);
void         cl_scrollview_scroll_to(cl_widget_t *sv, float y);    /* верт. смещение (клампится) */
void         cl_scrollview_scroll_to_x(cl_widget_t *sv, float x);  /* гориз. смещение (клампится) */
float        cl_scrollview_scroll_y(cl_widget_t *sv);
float        cl_scrollview_scroll_x(cl_widget_t *sv);
void         cl_scrollview_scroll_to_widget(cl_widget_t *sv, cl_widget_t *descendant); /* минимальный скролл в видимую зону */
```

ScrollView реализует vtable-хуки `clip_rect` (обрезка содержимого) и `reveal`
(scroll-to-view; используется при переводе фокуса, §14 `cl_widget_focus`).

## 20. Theme / Style (`theme.h`)

```c
typedef struct cl_text_style {
    cl_font_t *font;    /* NULL -> шрифт темы */
    cl_color_t color;
    cl_align_t align;   /* горизонтальное выравнивание */
} cl_text_style_t;

cl_color_t cl_theme_color(cl_theme_t *theme, cl_color_role_t role);
void       cl_theme_set_color(cl_theme_t *theme, cl_color_role_t role, cl_color_t color);
void       cl_theme_set_variant(cl_theme_t *theme, cl_theme_variant_t variant); /* light/dark; заменяет все цвета, шрифт/метрики сохраняются */
cl_theme_variant_t cl_theme_variant(cl_theme_t *theme);
void       cl_theme_set_radius(cl_theme_t *theme, float radius);   /* радиус углов по умолчанию */
cl_font_t *cl_theme_font(cl_theme_t *theme);                       /* может быть NULL до set */
void       cl_theme_set_font(cl_theme_t *theme, cl_font_t *font);  /* заимствуется, не владеет */
```

Тема приложения — `cl_application_theme(app)`. Роли цветов перечислены в §4.

## 21. Font / Text (`font.h`)

```c
typedef struct cl_font_metrics { float ascent, descent, line_height; } cl_font_metrics_t;

cl_font_t        *cl_font_load_file(cl_application_t *app, const char *path, float size_px);
cl_font_t        *cl_font_load_memory(cl_application_t *app, const void *data, size_t len, float size_px);
cl_font_t        *cl_font_load_system(cl_application_t *app, float size_px);
                  /* COPAL_FONT=/путь, затем известные системные пути
                   * (Segoe UI/Arial, DejaVu/Liberation/Noto); NULL + WARN */
void              cl_font_release(cl_font_t *font);
cl_font_metrics_t cl_font_metrics(cl_font_t *font);

/* Измерение без растеризации. max_width ЗАРЕЗЕРВИРОВАН под будущий перенос и
 * пока игнорируется: любое значение меряет одну строку (передавайте
 * CL_UNBOUNDED); перенос по ширине сейчас есть только внутри textbox. */
cl_size_t cl_text_measure(cl_font_t *font, const char *utf8, float max_width);
/* Ровно len байт (не NUL-терминированные) — для позиционирования каретки. */
cl_size_t cl_text_measure_bytes(cl_font_t *font, const char *utf8, size_t len, float max_width);
```

Данные, не являющиеся шрифтом, отклоняются (`NULL` + `CL_ERROR_FONT`), но
парсер (stb_truetype) не проверяет *усечённый* настоящий шрифт по границе
буфера — шрифты должны происходить из доверенного источника.

Ограничения текста (без shaping/bidi/mark-positioning) — ARCHITECTURE §12.

## 22. Renderer / PaintContext (для `paint`) (`render.h`)

```c
void cl_paint_fill_rect(cl_paint_context_t *ctx, cl_rect_t r, cl_color_t color);
void cl_paint_fill_round_rect(cl_paint_context_t *ctx, cl_rect_t r, float radius, cl_color_t color);
void cl_paint_stroke_round_rect(cl_paint_context_t *ctx, cl_rect_t r, float radius,
                                float width, cl_color_t color);
void cl_paint_draw_text(cl_paint_context_t *ctx, cl_font_t *font, const char *utf8,
                        cl_point_t pos, cl_color_t color);
void cl_paint_draw_image(cl_paint_context_t *ctx, cl_image_t *img, cl_rect_t dst);
void cl_paint_push_clip(cl_paint_context_t *ctx, cl_rect_t r);
void cl_paint_pop_clip(cl_paint_context_t *ctx);

/* Трансформация (translate + равномерный scale) для всего нарисованного до
 * парного pop; вложенные push компонуются. Клип-ректы трансформируются так же. */
void cl_paint_push_transform(cl_paint_context_t *ctx, cl_point_t offset, float scale);
void cl_paint_pop_transform(cl_paint_context_t *ctx);

/* Групповая прозрачность: альфа всего нарисованного до pop умножается на
 * alpha ∈ [0,1]; вложенные push перемножаются. Множитель применяется к каждому
 * примитиву отдельно (без промежуточного буфера) — перекрывающиеся элементы
 * внутри «тающей» группы просвечивают друг через друга. */
void cl_paint_push_opacity(cl_paint_context_t *ctx, float alpha);
void cl_paint_pop_opacity(cl_paint_context_t *ctx);

cl_theme_t *cl_paint_theme(cl_paint_context_t *ctx);
cl_color_t  cl_paint_theme_color(cl_paint_context_t *ctx, cl_color_role_t role);
```

`cl_paint_context_t` даётся только в `paint`; хранить его нельзя. Скругления/рамки
и текст растеризуются рендерером (GL: SDF-примитивы + glyph-атлас; mock: список
draw-команд). Push/pop (clip, transform, opacity) должны быть сбалансированы в
пределах одного вызова `paint`; глифы под scale-трансформацией растягиваются как
битмапы (качество достаточно для переходных анимаций).

## 23. Custom widget — минимальный пример

```c
#include <copal/widget_impl.h>

typedef struct cl_mycounter { cl_widget_t base; int value; } cl_mycounter_t;

static void mycounter_paint(cl_widget_t *w, cl_paint_context_t *ctx) {
    cl_mycounter_t *self = CL_WIDGET_CAST(cl_mycounter, w);   /* checked */
    cl_paint_fill_round_rect(ctx, w->rect, 6.0f,
                             cl_paint_theme_color(ctx, CL_COLOR_SURFACE));
    (void)self; /* нарисовать self->value ... */
}
static bool mycounter_mouse_down(cl_widget_t *w, const cl_event_t *ev) {
    cl_mycounter_t *self = CL_WIDGET_CAST(cl_mycounter, w);
    (void)ev; self->value++; cl_widget_invalidate(w); return true;
}
static const cl_widget_vtable_t cl_mycounter_vtable = {
    .paint = mycounter_paint, .mouse_down = mycounter_mouse_down,
};
static const cl_widget_class_t cl_mycounter_class = {
    .name = "cl_mycounter", .base = NULL, .type_id = 0,
    .instance_size = sizeof(cl_mycounter_t), .vtable = &cl_mycounter_vtable,
};
cl_widget_t *cl_mycounter_create(cl_application_t *app) {
    cl_widget_t *w = cl_widget_alloc(app, &cl_mycounter_class); /* zero + init_base */
    return w;   /* self->value уже 0 (zero-alloc) */
}
```

`cl_widget_alloc` выделяет `instance_size` нулём и вызывает `cl_widget_init_base`;
пользовательская инициализация — после. Касты опираются на идентичность указателя
класса (`&cl_mycounter_class`); `CL_WIDGET_CAST` всегда возвращает NULL при
несовпадении. Рост vtable (добавление слота) требует перекомпиляции приложения
(следствие ADR-005).
