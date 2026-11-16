# Исходные темы

Четыре примера готовы к установке через **Display → Theme clocks** на SmallTV Pro:

* [`pixel-room.stheme`](pixel-room.stheme) — комната в пиксель-арте с анимированным котом;
* [`terminal-ops.stheme`](terminal-ops.stheme) — плотная панель из текста и линий;
* [`live-status.stheme`](live-status.stheme) — динамическая панель, которой
  управляет JSON со статусом, получаемый по сети: заголовок с прокруткой loop,
  заголовок с прокруткой bounce, скруглённая полоса прогресса, цвет которой
  зависит от порогов и привязан к числовому уровню, и текст статуса;
* [`bebas-clock.stheme`](bebas-clock.stheme) — часы spec 2 шрифтом Bebas Neue,
  скомпилированным в два шрифта пакета из `bebas-clock/fonts/` (Bebas Neue (c)
  Dharma Type, SIL Open Font License, см. `bebas-clock/fonts/OFL.txt`; пакет
  называет шрифт в поле `author`). Прошивки до 3.3.0 включительно его отвергают.

Редактируемые исходники лежат в одноимённых каталогах. Для автономного
предпросмотра откройте
[pixel-room-preview.html](pixel-room-preview.html),
[terminal-ops-preview.html](terminal-ops-preview.html) или
[live-status-preview.html](live-status-preview.html).

Проверка, пересборка и предпросмотр (нужны Python 3.10+, Pillow, компилятор C++11
и заголовки ArduinoJson/GFX проекта; настройка описана в руководстве ниже):

```sh
python3 tools/smalltv_theme.py validate examples/themes/pixel-room
python3 tools/smalltv_theme.py build examples/themes/pixel-room examples/themes/pixel-room.stheme
python3 tools/smalltv_theme.py preview examples/themes/pixel-room examples/themes/pixel-room-preview.html --time 2026-09-18T10:24:55 --seconds 10 --fps 15
```

Исходные геометрические PNG воспроизводятся командой
`python3 examples/themes/make_pixel_room_assets.py`. Этот скрипт в установленный
пакет не входит; тема содержит только декларативные данные. Terminal Ops можно
создать заново командой `python3 tools/create_terminal_theme.py`.

У Live Status нет встроенного сервера с данными, поэтому её предпросмотр
подставляет фиксированные значения через `--data`:

```sh
python3 tools/smalltv_theme.py build examples/themes/live-status examples/themes/live-status.stheme
python3 tools/smalltv_theme.py preview examples/themes/live-status \
  examples/themes/live-status-preview.html --seconds 8 --fps 15 \
  --time 2026-09-22T12:00:00 \
  --data status.label='SmallTV dynamic dashboard headline' \
  --data status.level=82 --data status.state=Warning
```

Поля слоёв, ограничения, двоичные форматы и API установки описаны в
[руководстве по формату и созданию тем](../../docs/themes.md).
