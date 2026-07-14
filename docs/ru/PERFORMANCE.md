<p align="right"><a href="../PERFORMANCE.md">English</a> | <b>Русский</b></p>

# Производительность

copal несёт headless-набор микробенчмарков ([benchmarks/bench.c](../../benchmarks/bench.c)),
чтобы производительность была измеренной, а не аргументированной. Он гоняется на
mock- и software-бэкендах (без окна и GPU), поэтому воспроизводится где угодно и в
CI как отдельная informational-job.

## Как запустить

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCOPAL_BUILD_BENCHMARKS=ON
cmake --build build
COPAL_FONT=/path/to/font.ttf ./build/benchmarks/copal_bench
```

Каждый сценарий сам подбирает число итераций (~0.1 с настенного времени) и печатает
микросекунды на операцию и операции в секунду. Текстовые сценарии требуют
системного шрифта (`COPAL_FONT`), иначе пропускаются.

## Что измеряется

| Сценарий | Бэкенд | Что нагружает |
|---|---|---|
| `empty_frame` | software | Очистка кадра 800×600 + present. |
| `fullscreen_frame` | software | Полноэкранный fill c альфа-блендом — стоимость **per-pixel** растеризации. |
| `many_widgets` | mock | Paint-обход дерева из 300 виджетов (генерация команд). |
| `deep_layout` | mock | measure+arrange дерева глубиной 60. |
| `resize` | mock | Resize → relayout → paint (150 виджетов). |
| `input mouse-move` | mock | Один input-эвент через цикл (hit-test/hover/dispatch). |
| `scroll` | mock | Колесо → скролл контента из 300 виджетов. |
| `damage: full / one` | mock | Полная перерисовка против инвалидации одного виджета — **damage-cull**. |
| `resource_churn` | mock | 100 create+destroy виджетов (аллокатор). |
| `text_measure_mixed` | — | Измерение строки Latin/Cyrillic/CJK/символы — **advance-кэш**. |
| `text_edit` | mock | Ввод/backspace в textbox через цикл. |

## Результаты (indicative)

Один прогон на x86-64 Linux (gcc, `-O2`); абсолютные числа зависят от машины,
поэтому смотрите на **порядки** и на до/после оптимизаций. CI печатает свежие числа
каждой сборкой.

```
pixel (software renderer):
  empty_frame (800x600 clear)                24.7 us/op
  fullscreen_frame (800x600 blend)         1046.4 us/op
widget / layout / input (mock renderer):
  many_widgets (300, paint)                   5.2 us/op
  deep_layout (60 nested, relayout)           1.4 us/op
  resize + relayout (150)                     3.1 us/op
  input mouse-move (300 widgets)              1.4 us/op
  scroll (wheel, 300-tall content)            4.9 us/op
  damage: full redraw (300)                   5.2 us/op
  damage: one widget (300)                    1.3 us/op   (~4-6x быстрее full)
  resource_churn (100 create+destroy)         2.3 us/op
text:
  text_measure_mixed (Latin/Cyr/CJK/sym)      0.22 us/op
  text_edit (type/backspace)                 39.3 us/op
```

### Наблюдения

- **Стоимость кадра доминируется per-pixel растеризацией.** Полноэкранный
  software-блендинг (`fullscreen_frame`, ~1 мс на 800×600) на два-три порядка дороже
  генерации команд для дерева виджетов (`many_widgets`, ~5 мкс). Отсюда ценность
  damage-регионов и pacing’а: не перерисовывать то, что не изменилось.
- **Обход дерева дёшев.** Layout, paint-обход, hit-test и input укладываются в
  единицы микросекунд даже на 300 виджетах — узкое место в реальном UI это пиксели,
  а не CPU-обход.
- **CPU-цена мелкого обновления теперь пропорциональна изменению**, а не размеру
  дерева (см. damage-cull ниже).

## Эффект оптимизаций (до/после)

Числа с той же машины, тот же бенчмарк.

| Оптимизация | Метрика | До | После |
|---|---|---:|---:|
| **Advance-кэш** для не-Latin/Cyrillic (CJK/символы/эмодзи) | `text_measure_mixed` | 0.327 us/op | 0.222 us/op (**−32%**) |
| **Damage-cull** обхода paint | `damage: one widget` (дерево 300) | 5.87 us/op | 1.29 us/op |
| | ускорение vs full redraw | ~1.0x | **~4–6x** |

До damage-cull перерисовка одного виджета обходила и рисовала всё дерево и стоила
столько же, сколько полный кадр (≈1.0x). Теперь `cl_widget_do_paint` отсекает
виджеты и клипящие поддеревья вне damage-региона, и цена мелкого обновления падает
до нескольких раз (растёт с размером дерева) — на per-pixel software-пути выигрыш
ещё больше, потому что не блендятся пиксели вне региона.

## Что осталось за кадром измерений

`empty_frame`/`fullscreen_frame` изолируют per-pixel software-стоимость, но
покрытие ограничено: GL-путь бенчмарками не меряется (нужен GPU/llvmpipe в CI),
а абсолютные числа — с одной машины. Регрессии по производительности ловятся
глазами по CI-выводу, автоматического порога пока нет.
