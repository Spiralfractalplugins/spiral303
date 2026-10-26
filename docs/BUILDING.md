# Сборка Spiral303

Требования (любая платформа):
- CMake ≥ 3.24
- Git
- JUCE 8.0.14 в `vendor/JUCE`:

```bash
git clone --depth 1 --branch 8.0.14 https://github.com/juce-framework/JUCE.git vendor/JUCE
```

- clap-juce-extensions в `vendor/clap-juce-extensions` (нужен для формата CLAP):

```bash
git clone https://github.com/dsp56300/clap-juce-extensions.git vendor/clap-juce-extensions
git -C vendor/clap-juce-extensions checkout e1f67893cc409a40c1154fa2e78c97046da24ce0
git -C vendor/clap-juce-extensions submodule update --init --recursive
```

В JUCE 8 цель `Spiral303` — только общий код; форматы собираются отдельными
целями `Spiral303_VST3`, `Spiral303_Standalone`, `Spiral303_CLAP`,
`Spiral303_LV2` (на macOS ещё `Spiral303_AU`), плюс `Spiral303Tests` —
офлайн-тесты, аудиоустройство не нужно.

## Windows нативно (Visual Studio 2026)

Требования: Windows 10/11 x64, Visual Studio 2026 с нагрузкой
«Desktop development with C++», CMake ≥ 3.24, Git.

```bat
cmake -S . -B build -G "Visual Studio 18 2026"
cmake --build build --config Release
```

Результат:
- `build\Spiral303_artefacts\Release\VST3\Spiral303.vst3` — скопируйте в
  `C:\Program Files\Common Files\VST3` (или укажите папку хосту).
- `build\Spiral303_artefacts\Release\CLAP\Spiral303.clap` — скопируйте в
  `C:\Program Files\Common Files\CLAP` (auto-copy для CLAP на Windows нет).
- `build\Spiral303_artefacts\Release\LV2\Spiral303.lv2` — скопируйте в
  `C:\Program Files\Common Files\LV2` (или `%APPDATA%\LV2`).
- `build\Spiral303_artefacts\Release\Standalone\Spiral303.exe` — проверка
  звуком без хоста.
- Тесты: `cmake --build build --config Release --target Spiral303Tests`, затем
  `build\Spiral303Tests_artefacts\Release\Spiral303Tests.exe`.

Имена генератора MSBuild 18.x («Visual Studio 18 2026») могут отличаться в
других редакциях — возьмите своё из `cmake --help`. Можно и Ninja:
`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`.

## macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

CMakeLists сам пинит universal-сборку `arm64;x86_64`, чтобы Intel-бандл не
отказывался грузиться в нативном Apple Silicon-хосте (и наоборот). После
сборки VST3/AU/Standalone, CLAP и LV2 копируются в системные папки плагинов
(`JUCE_COPY_PLUGIN_AFTER_BUILD`; для CLAP — `~/Library/Audio/Plug-Ins/CLAP/`,
для LV2 — `~/Library/Audio/Plug-Ins/LV2/`).

Артефакты: `build/Spiral303_artefacts/Release/{VST3,AU,Standalone,CLAP,LV2}/`.
AU на других платформах не собирается — формат существует только на macOS,
JUCE пропускает его автоматически.

## Linux (Ubuntu 24.04 x64)

```bash
sudo apt install build-essential cmake ninja-build pkg-config \
  libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev \
  libdbus-1-dev libfreetype-dev libfontconfig-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxdamage-dev \
  libxext-dev libxfixes-dev libxi-dev libxinerama-dev libxrandr-dev libxss-dev \
  mesa-common-dev

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j --target Spiral303_VST3 Spiral303_Standalone Spiral303_CLAP Spiral303_LV2 Spiral303Tests
build/Spiral303Tests_artefacts/Release/Spiral303Tests
```

Результат:
- `build/Spiral303_artefacts/Release/VST3/Spiral303.vst3/Contents/x86_64-linux/Spiral303.so`
  — в `~/.vst3/` или `/usr/local/lib/vst3/`.
- `build/Spiral303_artefacts/Release/Standalone/Spiral303`.
- `build/Spiral303_artefacts/Release/CLAP/Spiral303.clap` — в `~/.clap/`
  (auto-copy после сборки).
- `build/Spiral303_artefacts/Release/LV2/Spiral303.lv2` — в `~/.lv2/`
  или `/usr/lib/lv2/`.

## Тесты

`Spiral303Tests` собирается как console-app и гоняет весь DSP и секвенсор
офлайн, а GUI раскрашивает в `Image` без окна (`tests/TestGui.cpp`), сохраняя
PNG в `%TEMP%/spiral303_gui/`. Успех — строка `ALL TESTS PASSED` и код выхода 0
(на нём построена CI).

## Примечания
- `/utf-8` для MSVC включён в `CMakeLists.txt` — иначе символы `▾` и `◀▶`
  в подписях уходят как `?`.
- Формат **Audio Unit существует только на macOS/iOS** — под Windows и Linux
  сборка ограничена VST3, CLAP, LV2 и Standalone.
- **LV2** собран средствами самого JUCE 8 (`FORMATS ... LV2` и `LV2URI`
  в `juce_add_plugin`) — отдельной библиотеки не нужно. Бандл
  `Spiral303.lv2/` содержит `manifest.ttl`, `dsp.ttl`, `ui.ttl` и `libSpiral303.so`
  (на Windows — `.dll`); интерфейс — нативный JUCE-редактор, а не отдельная
  LV2-реализация. Проверить бандл можно сторонним lilv:
  `LV2_PATH=build/Spiral303_artefacts/Release lv2_validate \
  build/Spiral303_artefacts/Release/LV2/Spiral303.lv2/manifest.ttl`.
- Живой хост (jalv.gtk3 + Carla под Xvfb, Linux-бандл): `./tools/lv2-host-check.sh`
  — lilv видит `ui:X11UI`, jalv встраивает редактор ровно 1180×780, клики доходят
  до сетки, Carla поднимает тот же бинарник рядом, а секвенсор отдаёт звук в
  джек-порты хоста. Проверка замечает `lv2_validate` по URI: по URI он ничего не
  проверяет (пишет «Skipping file»), надо давать путь до `manifest.ttl`.
- В `dsp.ttl` у ручек нет `lv2:ControlPort`: JUCE выставляет параметры списком
  `patch:writable`/`patch:readable` (atom-сообщения), поэтому хост берёт их из
  патч-интерфейса, а не из портов.
- **CLAP** собирается через [clap-juce-extensions](https://github.com/dsp56300/clap-juce-extensions)
  (форк dsp56300, пин в `CMakeLists.txt` и CI): тот же общий код, что у VST3/AU,
  состояние и пресеты переносятся хостом через стандартный `setStateInformation`.
  На macOS и Linux `.clap` копируется в системную папку после сборки; на Windows
  копирование вручную — см. выше.
- Бинарники из чужих сборок не подписаны; перед распространением Windows-сборку
  стоит проверить в реальном DAW — тесты проверяют только DSP-путь.

## LV2: автоматизация параметров

JUCE 8 отдаёт параметры LV2 **только** через интерфейс `patch:` (atom-сообщения
`patch:Set`/`patch:Get`), а не через control-порты. Это значит:

- **Хосты с поддержкой `patch:`** (Ardour, Carla, jalv с `lv2:State`) автоматизируют
  все ручки как обычно: запись автоматизации, воспроизведение, сохранение в проект.
- **Хосты без `patch:`** (некоторые простые хосты и старые версии) видят плагин и
  его звук, но не могут автоматизировать параметры — ручки в них статичны. Это
  ограничение JUCE, а не нашего кода: мы не добавляем control-порты поверх, чтобы
  не расходиться с нативной VST3/AU-сборкой, где параметры идут через APVTS.

Проверка живым хостом: `tools/lv2-host-check.sh` поднимает jalv.gtk3 и Carla под
Xvfb и убеждается, что редактор открывается (1180×780), клики по сетке правят
узор, а секвенсор отдаёт звук в джек-порты Carla. Автоматизацию `patch:` в этих
хостах можно проверить вручную: записать автоматизацию ручки Cutoff в Carla и
убедиться, что она воспроизводится.

Если в будущем понадобится поддержка хостов без `patch:`, варианты:
1. Форк JUCE LV2-кода с добавлением control-портов (большой объём, расхождение
   с upstream).
2. Отдельный LV2-обёрточный проект поверх нативного бинаря.
