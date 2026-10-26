# Публикация WCLAP на plinken.org

Бандл Spiral303 публикуется в открытом каталоге WCLAP-плагинов
[`plinken.org/shelf.json`](https://plinken.org/shelf.json). Правила описывают
[REGISTRY.md](https://github.com/plinken-org/plinken-wclap/blob/main/REGISTRY.md)
и [`plugins/README.md`](https://github.com/plinken-org/plinken-wclap/blob/main/plugins/README.md)
репозитория `plinken-org/plinken-wclap`; эта заметка — наш чек-лист и найденные
грабли.

Форк для PR: <https://github.com/Spiralfractalplugins/plinken-wclap>
(родитель — `plinken-org/plinken-wclap`, ветка `main`). Папка плагина уже лежит
там в `plugins/com.spiral303/` — из этого репозитория в PR ездит только артефакт
и `version` в манифесте.

## Что куда

Каталог не агрегирует чужие реестры в рантайме: попадает только то, что явно
лежит в их `plugins/`. Путь добавления — PR в `main`. Форма папки (по их
README):

```
plugins/com.spiral303/
├── README.md                         # вендор: кто, контакт, лицензия
└── spiral303/
    ├── plugin.json                   # манифест v1
    ├── README.md                     # что делает плагин, как собрать
    ├── LICENSE                       # Apache-2.0 (см. «Лицензия»)
    └── dist/spiral303.wclap.tar.gz   # артефакт сборки
```

Тексты папки (`plugin.json`, README вендора и плагина, `LICENSE`) живут в
форке и в upstream после мержа; в этом репозитории они не дублируются — сюда
PR приносит только собранный артефакт и новую `version`.

Требования к `plugin.json`, которые проверяет их `scripts/build-shelf.mjs`:
`manifest_version: 1`, `id` = CLAP-ID (`com.spiral303.plugin`), `name`,
`vendor`, `version`, `artifact` (путь относительно манифеста), `format`
(`wasm` | `tar.gz`). Поля `features` берутся из дескриптора модуля
(`instrument, synthesizer, bass, lead, acid, monophonic`), `ui.expanded_size`
— 1180×780, наше окно.

`dist/` в их репозитории не живёт: на полку попадают байты, скопированные в
`apps/wclap-host/public/samples/<id>.wclap.tar.gz` и
`apps/site/static/wclap/<id>.wclap.tar.gz`, плюс пункт в
`apps/wclap-host/public/shelf.json` и `apps/site/static/shelf.json`. Всё это
пишет их `scripts/build-shelf.mjs`.

## Лицензия

Их правило: `plugins/README.md` — «We only ship plugins whose license is MIT-
or Apache-2.0-compatible for the public shelf», и `LICENSE` в папке плагина
ожидается MIT или Apache-2.0.

Родной плагин (VST3/AU/LV2/CLAP) линкует JUCE, а JUCE взят по GPLv3-ветке —
поэтому весь репозиторий `spiral303` распространяется под **GPL-3.0-only**
(`LICENSE` в корне). WCLAP-модуль к JUCE не обращается: `wclap/CMakeLists.txt`
собирает только `Source/wclap/*`, `Source/dsp/Sequence.cpp` и CLAP-заголовки,
вместо JUCE — наш же `Source/core/JuceLite.h`. Так что у артефакта две
лицензии законно: **Apache-2.0 на бандл в реестре**, GPL-3.0 на нативную
сборку. Это записано и в `plugins/com.spiral303/README.md`, чтобы ревьюер не
гадал.

## Два layout, два хоста

`tools/build_wclap.sh` пакует один и тот же `build-wclap/Spiral303.wclap/` дважды:

- `Spiral303-<версия>-wclap.tar.gz` — с верхним каталогом `Spiral303.wclap/`:
  так требует openDAW (`stripRoot` снимает единственный корень);
- `Spiral303-<версия>-wclap-registry.tar.gz` — `--flat`, файлы в корне архива:
  так требует браузерный хост plinken. Его загрузчик
  (`clap-audionode/wclap-js/es6/wclap-plugin.mjs`) ищет `module.wasm` по ключу
  `"<pluginPath>/module.wasm"`, а `pluginPath` у него не задан — запись
  `Spiral303.wclap/module.wasm` не находится, резервный путь читает `key` вместо
  `path` и не срабатывает, и `WebAssembly.compile` получает пустой буфер:
  «BufferSource argument is empty», плагин не грузится.

В каталог plinken идёт плоская копия; `check_bundle_tar.mjs` на ней падает
by design — проверка корня принадлежит openDAW-архиву, а реестровый проверяется
прогоном в их хосте (ниже).

## Чек-лист перед PR

1. Версия и CHANGELOG актуальны; `tools/build_wclap.sh` собрал свежий бандл
   (после любой пересборки модуля — упаковка заново, иначе
   `check_bundle_tar` поймает расхождение байтов).
2. Все проверки зелёные:
   - `./wclap/tools/check_wclap.sh` — звук, API, страница, архив, паритет;
   - `node wclap/tools/check_ui.mjs <state.json>` — интерфейс офлайн;
   - `node wclap/tools/check_browser_host.mjs` — интерфейс в их браузерном
     хосте (см. ниже), без uncaught-ошибок.
3. `version` и артефакт в `plugins/com.spiral303/spiral303/` (в форке)
   совпадают со сборкой; `license` манифеста — `Apache-2.0`, в корне нашего
   репозитория — `GPL-3.0-only`.
4. `node scripts/build-shelf.mjs` в клоне принимает папку (строка
   `✓ com.spiral303.plugin → com.spiral303.plugin.wclap.tar.gz`, без ошибок).

## Прогон в их браузерном хосте

Требование курации: плагин грузится в `wclap-host` без uncaught-ошибок.
Хост — [`WebCLAP/browser-test-host`](https://github.com/WebCLAP/browser-test-host).

```sh
cd /tmp && git clone --depth 1 --recurse-submodules \
    https://github.com/WebCLAP/browser-test-host
git clone --depth 1 https://github.com/WebCLAP/examples /tmp/wclap-examples  # если нет
mkdir -p /tmp/browser-test-host/spiral303
cp <repo>/dist/Spiral303-<версия>-wclap-registry.tar.gz \
   /tmp/browser-test-host/spiral303/Spiral303-wclap.tar.gz
(cd /tmp/browser-test-host && nohup python3 server.py 8137 >/tmp/bth_server.log 2>&1 &)
node wclap/tools/check_browser_host.mjs /spiral303/Spiral303-wclap.tar.gz \
     /tmp/spiral303_browser_testhost.png
```

Нужен `playwright-core` (`npm install --no-save playwright-core` в корне нашего
репозитория; chromium берётся из `~/Library/Caches/ms-playwright/`, путь
прописан в скрипте). Проверки: дескриптор дошёл до заголовка окна, канвас
интерфейса красится, стейт пришёл на страницу (`window.__state`), нет
uncaught-ошибок и 404. Скриншот — вся страница хоста с нашим интерфейсом.

Замеченное при прогоне:

- `clap-audionode/wclap-js` — сабмодуль `WebCLAP/wclap-host-js`; клонировать с
  `--recurse-submodules` (внутри ещё `es6/wasi`). В свежем клоне `wclap.mjs`
  и `es6/` уже на месте, разворачивать руками нечего — импорт `es6/wclap.mjs`
  проходит сразу.
- `examples/` должен содержать `signalsmith-clap-cpp/example-plugins.wclap.tar.gz`
  и `signalsmith-basics/basics.wclap.tar.gz` — иначе страница падает в
  `Failed to fetch` ещё до интерфейса; эти файлы из репозитория
  `WebCLAP/examples`.
- `404 favicon.ico` — норма. А вот подсказка «WCLAP bundle has WASM at …
  instead of /module.wasm» нормой не была: с корнем `Spiral303.wclap/` плагин
  не грузился вообще (см. «Два layout»).
- Их хост показывает наш редактор в узкой полосе (канвас 1468×64 в скриншоте
  — это их layout под вкладкой UI), но рисуется весь: ручки, сетка, гейт.

## Обновление версии в реестре

Первый PR (`plinken-org/plinken-wclap#4`) принёс в реестр саму папку; дальше из
этого репозитория в PR ездят только байты артефакта и `version`.

```sh
git clone https://github.com/Spiralfractalplugins/plinken-wclap && cd plinken-wclap
git remote add upstream https://github.com/plinken-org/plinken-wclap.git
git checkout -B spiral303-<версия> upstream/main

# артефакт из <repo> = этот репозиторий, после tools/build_wclap.sh
mkdir -p plugins/com.spiral303/spiral303/dist
cp <repo>/dist/Spiral303-<версия>-wclap-registry.tar.gz \
   plugins/com.spiral303/spiral303/dist/spiral303.wclap.tar.gz
python3 - <<PY   # версия — один источник правды, CMakeLists.txt этого репозитория
import json, re, pathlib
version = re.search (r'VERSION (\d+\.\d+\.\d+)', pathlib.Path ('<repo>/CMakeLists.txt').read_text()).group (1)
p = pathlib.Path ('plugins/com.spiral303/spiral303/plugin.json')
m = json.loads (p.read_text ()); m['version'] = version
p.write_text (json.dumps (m, indent=2, ensure_ascii=False) + '\n')
PY

# Агрегатор — в одноразовой копии клона (причины ниже).
rm -rf /tmp/shelftest && cp -R . /tmp/shelftest && cd /tmp/shelftest
python3 - <<'PY'   # вернуть dist/ всех остальных: без них shelf.json потеряет их пункты
import json, os, shutil
s = "apps/wclap-host/public/samples"
for v in os.listdir("plugins"):
    for p in os.listdir(os.path.join("plugins", v)):
        pj = os.path.join("plugins", v, p, "plugin.json")
        if not os.path.exists(pj): continue
        m = json.load(open(pj)); art = os.path.join("plugins", v, p, m["artifact"])
        src = os.path.join(s, m["id"] + (".wclap.tar.gz" if m["format"] == "tar.gz" else ".wclap.wasm"))
        if not os.path.exists(art) and os.path.exists(src):
            os.makedirs(os.path.dirname(art), exist_ok=True); shutil.copy(src, art)
PY
node scripts/build-shelf.mjs          # ждём "✓ com.spiral303.plugin → ..." без ошибок
cd -
cp /tmp/shelftest/apps/wclap-host/public/samples/com.spiral303.plugin.wclap.tar.gz \
   apps/wclap-host/public/samples/
cp /tmp/shelftest/apps/site/static/wclap/com.spiral303.plugin.wclap.tar.gz \
   apps/site/static/wclap/
cp /tmp/shelftest/apps/wclap-host/public/shelf.json apps/wclap-host/public/
cp /tmp/shelftest/apps/site/static/shelf.json apps/site/static/
git add -u apps plugins && git commit -m "Spiral303: <версия>"
git push -u fork spiral303-<версия>   # и PR в plinken-org/plinken-wclap:main
```

**Не запускать `build-shelf.mjs` вслепую в свежем клоне.** `dist/` у них в
`.gitignore`, поэтому без локальной сборки артефактов всех остальных
`✓`-строк не будет, а агрегатор молча выбросит их из `shelf.json` — diff
на пол-репозитория не из наших изменений. Отсюда и восстановление `dist/` из
их же `samples/` перед прогоном, и перенос в рабочий клон только наших файлов.
И ещё: `apps/site/static/wclap/com.plinken.brick-limiter.wclap.tar.gz`
у них расходится с копией в `samples/` (upstream починил `samples/`,
`site/` — нет), агрегатор перезапишет его нашим прогоном. В PR это не наше —
файл из diff убрать.

После мержа артефакт отдаётся с `wclap.plinken.org` (и
`plinken.org/wclap/<id>.wclap.tar.gz`) с открытым CORS — любой WCLAP-хост
загрузит его по `shelf.json`.
