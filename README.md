# Spiral303

VST3 / Audio Unit / CLAP / LV2 плагин-синтезатор в духе Roland TB-303: кислотный
контурный фильтр, слайд, акцент и секвенсор с gate-сеткой. macOS,
Windows и Linux (AU — только macOS), JUCE 8.

## Возможности
- **Морфинг-осциллятор** синус → пила → квадрат (одна ручка Wave), квадратичный
  polyBLEP на обеих границах, DC-блок.
- **Контурный фильтр TB-303**: резонансный low-pass 12/24 dB/oct (Slope),
  огибающая по срезу с ручками Contour, EnvAmt и акцентом.
- **Семь моделей фильтра** (FilterType): классический контур, диодная и Moog
  лестницы, TPT SVF, Sallen-Key, однополюсник и **Vowel** — банк из трёх
  формант (F1/F2/F3) с голосовыми полосами. Ручка Cutoff ведёт голос по
  траектории `у → о → а → э → и`, Resonance сужает форманты, а огибающая ноты
  сама двигает рот — на атаке позиция яркая, к хвосту тёмная, то есть нота
  «говорит». Пять не-Classic моделей выровнены по громкости к классической
  калибровочной таблицей (и Vowel тоже).
- **Сатурация** (tanh-драйв + параллельный DistMix) и мастер-гейн.
- **Секвенсор в стиле 303**: сетка 8/12/16/24/32 шага, 12 гамм, на каждой ступени —
  tie (удержание), slide (глейд в ноту) и accent (громче + удар по фильтру; подряд
  идущие акценты заряжают друг друга до +75%, как конденсатор акцента в 303);
  свинг, акцент-огибающая, glide.
- **Импорт MIDI-файлов**: кнопка `MIDI` (или drag&drop `.mid` на окно) кладёт в
  секвенсор самую низкую по тесситуре дорожку файла. Сетка — 1/16, темп берётся
  из часов хоста или ручки BPM (tempo map файла игнорируется), строй и лад
  остаются ваши — чужие ноты прижимаются к ближайшей ступени, база уезжает вниз.
  Фраза длиннее паттерна обрубается, остаток ряда становится паузами.
- **Экспорт MIDI-файла**: та же кнопка `MIDI` → «Export pattern as MIDI». Пишется
  один цикл паттерна стандартным `.mid` (формат 0, 480 тиков на четверть), темп
  — в мета-событии. Акцент передаётся через velocity (обычные ноты ≤ 99, акценты
  ≥ 100), тай — удлинённой нотой, слайд — легато. Файл читается обратно импортом
  на тех же ступенях. Гейт и свинг в файл не пишутся.
- **Gate-сетка 4×4**: каждый квадрат — 1/16 доля, закрытые квадраты гасят слой
  гейта; поворот рисунка сдвигает его по доле, свои атака/затухание/уровень и
  делители (16th / 8th / 32nd / 16T).
- **Аналоговость**: ручка Warmth добавляет дрейф высоты тона и среза, флуктуации
  и разброс порога гейта.
- **Часы**: транспорт хоста или ручной BPM 20–400, делители 1/4…1/16T.
- **102 фабричных пресета** (acid, acid house, goa — включая известные линии), папка
  пользовательских пресетов, drag&drop файла на окно, формат — обычный JSON
  (см. [presets/README.md](presets/README.md)).
- **Экспорт в WAV**: кнопка Export рендерит текущий паттерн (со всеми голосами,
  фильтром и сатурацией) в моно 44.1 кГц, плюс превью двух тактов.
- **Scope**: осциллограф с АЧХ под основными ручками.
- **Четыре скина интерфейса** (переключатель в шапке, параметр `UiSkin`):
  тёмный плоский, пульт космического корабля (композит, hazard-лента,
  светящиеся дуги), дерево (ореховая рамка, алюминиевая вклейка, бакелитовые
  ручки с никелированной шайбой) и металл (шлифованный алюминий, карманы под
  винты, накатка). Скины меняют только покраску — звук остаётся бит-в-бит.

## Сборка (Windows)
См. [docs/BUILDING.md](docs/BUILDING.md). Кратко:

```bat
git clone --depth 1 --branch 8.0.14 https://github.com/juce-framework/JUCE.git vendor/JUCE
git clone https://github.com/dsp56300/clap-juce-extensions.git vendor/clap-juce-extensions
git -C vendor/clap-juce-extensions checkout e1f67893cc409a40c1154fa2e78c97046da24ce0
git -C vendor/clap-juce-extensions submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 18 2026"
cmake --build build --config Release
```

Артефакты:
- `build\Spiral303_artefacts\Release\VST3\Spiral303.vst3`
- `build\Spiral303_artefacts\Release\CLAP\Spiral303.clap`
- `build\Spiral303_artefacts\Release\LV2\Spiral303.lv2`
- `build\Spiral303_artefacts\Release\Standalone\Spiral303.exe` — проверка звуком
  без хоста.

## Сборка (macOS и Linux)
Как в [docs/BUILDING.md](docs/BUILDING.md): на macOS собираются universal
arm64+x86_64 VST3, AU, CLAP, LV2 и Standalone (плюс авто-копия в системные папки), на
Linux — VST3, CLAP, LV2 и Standalone.

## WebAssembly (WCLAP)
Собирается ещё и как WCLAP — CLAP-плагин в WebAssembly: один файл работает в
браузере и в нативных DAW (через мост wclap-bridge).

```sh
tools/build_wclap.sh          # sdk, сборка, бандл в dist/
wclap/tools/check_wclap.sh    # звук, интерфейс и сверка с нативной сборкой
```

Подробности — в [docs/WEBASSEMBLY.md](docs/WEBASSEMBLY.md).

## Тесты
Тесты — единственный источник правды по GUI и DSP: весь секвенсор и звук
прогоняются офлайн, без аудиоустройства, GUI проверяется пиксельно в `Image`.

```sh
cmake --build build --config Release --target Spiral303Tests
./build/Spiral303Tests_artefacts/Release/Spiral303Tests    # Windows: .exe
```

Должно напечататься `ALL TESTS PASSED`. Скриншоты GUI-проверок складываются в
`%TEMP%/spiral303_gui/`. Стендalone кликами мышью не проверяется — модальное
меню пресетов обнуляет `MainWindowHandle`.

## Структура
```
Source/dsp/                — осциллятор, фильтр, сатурация, голоса, секвенсор, WAV
Source/gui/                — тема, ручки, scope, редактор паттерна, gate-сетка
Source/core/               — мини-JUCE для сборки под WebAssembly
Source/wclap/              — CLAP-слой WebAssembly-сборки
Source/PluginProcessor.*   — параметры (APVTS), транспорт, состояние
Source/PluginEditor.*      — GUI-окно, меню пресетов, About
Source/Presets.*           — формат пресета, банк из бинарных данных + свои файлы
presets/                   — фабричные пресеты (.json) и описание формата
web/                       — интерфейс WebAssembly-сборки (страница)
wclap/                     — сборка модуля под wasm, хост для тестов, скрипты
tests/                     — TestRender (DSP), TestCoverage (границы), TestGui (пиксели)
docs/BUILDING.md           — пошаговая сборка под все платформы
docs/WEBASSEMBLY.md        — сборка и проверки WebAssembly-версии
```

## Версии
Версия задаётся один раз — `project(Spiral303 VERSION ...)` в `CMakeLists.txt`
и оттуда же попадает в VST3/AU-манифест, окно About и бинарь
тестов. Текущая — 0.37.0. Хронология — в [CHANGELOG.md](CHANGELOG.md).

## Лицензия
GPL-3.0-only — см. [LICENSE](LICENSE). JUCE (vendor/JUCE) распространяется
под GPLv3/commercial — при публикации плагина учитывайте лицензию выбранного
вами модуля JUCE. WCLAP-бандл публикуется в каталоге
[plinken.org/shelf.json](https://plinken.org/shelf.json) — см.
[docs/PUBLISHING.md](docs/PUBLISHING.md).
