# copal

Лёгкая кроссплатформенная GUI-библиотека на C11 для Windows и Linux.
Окна и ввод — через SDL2; отрисовка — OpenGL 3.3 core **или** встроенный
CPU-растеризатор (software), выбирается на этапе сборки и в рантайме.

- **Маленький публичный C-API** с ABI-рукопожатием (desc-структуры с
  `struct_size`/`abi_version`), без глобального состояния.
- **Два рендер-бэкенда**: OpenGL (глиф-атлас, HiDPI) и software (SDF-растеризация
  на CPU — работает по RDP, в CI и без GPU); `CL_RENDER_AUTO` сам падает на
  software, если GL-окно не поднялось.
- **Инъекция зависимостей**: платформа, рендерер и аллокатор подменяются через
  `cl_application_desc_t` — библиотека полностью тестируется headless на
  mock-бэкендах.
- **Виджеты**: label, button, checkbox, radiobutton, slider, textbox
  (однострочный/многострочный, пароль, undo/redo, выделение мышью, IME),
  combobox, menu, tooltip, scrollview; контейнеры vbox/hbox с flex-весами,
  выравниванием и отступами.
- **Текст**: UTF-8, TrueType через вендоренный stb_truetype, поиск системного
  шрифта (`cl_font_load_system`, переменная `COPAL_FONT`).
- **Темы**: светлая/тёмная палитра, переключение в рантайме.
- Кастомные виджеты и контейнеры — через устанавливаемый `widget_impl.h`.

Ограничения MVP 0.1: одно окно, нет image-примитива и курсоров мыши, нет
shaping/bidi (1 кодовая точка = 1 глиф), меню без подменю. Планы: см.
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Сборка

Headless-сборка (mock-бэкенды, без SDL/GL) — по умолчанию:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Нативная сборка с окном (SDL2 + OpenGL):

```sh
cmake -S . -B build-native -DCOPAL_ENABLE_SDL=ON -DCOPAL_ENABLE_OPENGL=ON
cmake --build build-native
./build-native/examples/helloworld/helloworld
```

Software-сборка без OpenGL (только SDL2, libGL не линкуется):

```sh
cmake -S . -B build-sw -DCOPAL_ENABLE_SDL=ON
cmake --build build-sw
./build-sw/examples/calc/calc
```

Windows (MSVC): `COPAL_FETCH_SDL2=ON` скачает и соберёт SDL2 сам, DLL кладётся
рядом с примерами:

```bat
cmake -S . -B build -DCOPAL_ENABLE_SDL=ON -DCOPAL_ENABLE_OPENGL=ON -DCOPAL_FETCH_SDL2=ON
cmake --build build --config Release
build\examples\helloworld\Release\helloworld.exe
```

Полезные опции: `-DCOPAL_ENABLE_SANITIZERS=ON` (ASan/UBSan),
`-DCOPAL_ENABLE_COVERAGE=ON` (gcov/llvm-cov; не MSVC),
`-DCOPAL_BUILD_SHARED=ON`, `-DCOPAL_BUILD_EXAMPLES=OFF`,
`-DCOPAL_BUILD_TESTS=OFF`, `-DCOPAL_ENABLE_INSTALL=OFF`.

Рантайм-переменные: `COPAL_RENDER=software` (CPU-рендер вместо GL),
`COPAL_FONT=/path/to/font.ttf` (явный шрифт), `COPAL_GL_DEBUG=1`
(версия GL при старте).

## Минимальный пример

```c
#include <copal/copal.h>

static void on_close(cl_widget_t *w, void *user)
{
    (void)w;
    cl_application_quit((cl_application_t *)user, 0);
}

int main(void)
{
    cl_application_desc_t ad = CL_APPLICATION_DESC_INIT;
    cl_application_t *app = cl_application_create(&ad);

    cl_font_t *font = cl_font_load_system(app, 16.0f);
    cl_theme_set_font(cl_application_theme(app), font);

    cl_window_desc_t wd = CL_WINDOW_DESC_INIT;
    wd.title = "Example";
    wd.width = 800;
    wd.height = 600;
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
    cl_font_release(font);
    cl_application_destroy(app); /* уничтожает окно и дерево виджетов */
    return rc;
}
```

Больше примеров — в [examples/](examples/): `helloworld` (тур по виджетам,
темы, меню, таймеры) и `calc` (калькулятор). Оба принимают `--software`/`--gl`.

## Использование как зависимости

```cmake
find_package(copal CONFIG REQUIRED)
target_link_libraries(app PRIVATE copal::copal)
```

либо `add_subdirectory(copal)` (examples/тесты/install при этом автоматически
отключаются), либо pkg-config: `pkg-config --cflags --libs copal`.

## Документация

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — архитектура, слои, ADR.
- [docs/API.md](docs/API.md) — обзор публичного API и сигнатуры по модулям.
- [docs/STRUCTURE.md](docs/STRUCTURE.md) — дерево репозитория и сборка.
- [docs/CODESTYLE.md](docs/CODESTYLE.md) — стиль кода.

## Лицензия

GPL-3.0-or-later, см. [COPYING](COPYING). Вендоренные сторонние файлы
(stb_truetype, заголовки Khronos) — под своими лицензиями, см.
[third_party/README.md](third_party/README.md).
